#include "ansi_backend.h"
#include "qtty/graphics.h"
#include "qtty/grid.h"
#include "qtty/theme.h"
#include <QSocketNotifier>
#include <QImage>
#include <QCoreApplication>
#include <unistd.h>
#include <sys/ioctl.h>
#include <csignal>
#include <fcntl.h>
#include <cstdio>
#include <cstring>

namespace Qtty {

// Colour-depth negotiation (sections 5.1 and 6). Same shape as
// detect_graphics_mode() in src/graphics/graphics.cpp, deliberately: an explicit
// environment override wins, then what the terminal says about itself, and the
// floor is the sixteen colours Capabilities already defaults to.
//
// Before this the backend hardcoded Xterm256 and emitted 38;5; whatever the
// terminal could do -- so a true-colour terminal was quantised for nothing and
// a 16-colour one was sent codes it does not understand.
static Capabilities::ColorDepth detect_color_depth() {
	const QByteArray force = qgetenv("QTTY_COLOR").toLower();
	if (!force.isEmpty()) {
		if (force == "mono" || force == "1")          return Capabilities::Mono;
		if (force == "16" || force == "ansi16")       return Capabilities::Ansi16;
		if (force == "256" || force == "xterm256")    return Capabilities::Xterm256;
		if (force == "truecolor" || force == "24bit") return Capabilities::TrueColor;
	}
	// COLORTERM is the de-facto announcement, and these two spellings are the
	// ones terminals actually export.
	const QByteArray colorterm = qgetenv("COLORTERM").toLower();
	if (colorterm == "truecolor" || colorterm == "24bit")
		return Capabilities::TrueColor;

	const QByteArray term = qgetenv("TERM").toLower();
	if (term.isEmpty() || term == "dumb")
		return Capabilities::Mono;                    // nothing claimed at all
	// terminfo spells a direct-colour entry "-direct" (xterm-direct,
	// tmux-direct), which is how the 24-bit terminals that do not export
	// COLORTERM say so.
	if (term.contains("direct"))   return Capabilities::TrueColor;
	if (term.contains("256color")) return Capabilities::Xterm256;
	return Capabilities::Ansi16;                     // the documented floor
}

AnsiBackend::AnsiBackend() {
	mode_ = detect_graphics_mode();
	depth_ = detect_color_depth();
	winsize ws{};
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		cells_ = QSize(ws.ws_col, ws.ws_row);
	else
		cells_ = QSize(80, 24);                     // piped/CI fallback
	resume();
	notifier_ = new QSocketNotifier(0, QSocketNotifier::Read, this);
	connect(notifier_, &QSocketNotifier::activated, this, [this] { read_input(); });
}

AnsiBackend::~AnsiBackend() { suspend(); }

Capabilities AnsiBackend::capabilities() const {
	Capabilities c;
	c.color = depth_;                               // negotiated (section 6)
	c.graphics = mode_;                             // negotiated (sections 5.7, 17.3)

	// These four were fields nobody set and nobody read. They are answers
	// about this backend now, and each is answerable because resume() asks
	// the terminal for the mode and decode_one() understands the replies.
	//
	// mouse and bracketed_paste are reported for a tty because the modes are
	// requested unconditionally and a terminal that does not implement one
	// ignores the request -- there is no reply to wait for, and the DCS
	// handshake doc/beerssh.md proposes is what would turn these from "asked
	// for" into "confirmed".
	c.mouse = raw_ok_;
	c.bracketed_paste = raw_ok_;
	c.unicode_wide = true;                           // L2 measures width itself

	// DEC 2026 is NOT claimed. section 11 wants synchronised output to
	// eliminate tearing, and nothing emits the brackets yet; saying true here
	// would be a field describing an intention rather than the backend.
	c.synchronised_output = false;
	c.title = false;                                // no OSC 0/2 emitter yet
	return c;
}

static QByteArray moveTo(QPoint cell) {
	return "\033[" + QByteArray::number(cell.y() + 1) + ';'
	     + QByteArray::number(cell.x() + 1) + 'H';
}

QSize AnsiBackend::size() const { return cells_; }

// ---- terminal resize -------------------------------------------------------
//
// A signal handler may call almost nothing, so it writes one byte to a pipe
// and returns; a QSocketNotifier on the read end turns that into an ordinary
// event in the Qt loop, where the real work is legal. This is the standard
// self-pipe, and it is the reason a resize can be handled at all: before it,
// ITerminalEventSink::on_resize existed, InputRouter implemented it, and
// nothing ever called it -- dragging a terminal's edge did nothing whatever.
static int s_winch_pipe[2] = {-1, -1};

extern "C" void qtty_winch_handler(int) {
	if (s_winch_pipe[1] >= 0) {
		const char b = 1;
		const ssize_t ignored = ::write(s_winch_pipe[1], &b, 1);
		(void)ignored;                 // nothing useful to do in a handler
	}
}

void AnsiBackend::read_winch() {
	char drain[64];
	while (::read(s_winch_pipe[0], drain, sizeof drain) > 0) { }
	winsize ws{};
	if (ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col <= 0) return;
	const QSize now(ws.ws_col, ws.ws_row);
	if (now == cells_) return;         // SIGWINCH also fires for pixel-only changes
	cells_ = now;
	if (sink_) sink_->on_resize(cells_);
}

void AnsiBackend::resume() {
	if (active_) return;
	// Terminal control goes to a terminal and nowhere else. Emitting it
	// unconditionally means a run whose output is redirected writes the
	// alternate-screen switch, the mode sets and the mode resets into the
	// file -- and a test that constructs a backend takes the developer's
	// screen with it. Found exactly that way: the suite's first PASS line
	// came out appended to a mode-setting sequence.
	//
	// Input setup is asked about separately, because the two ends are
	// independent: output can be a pipe while input is still a keyboard.
	tty_out_ = isatty(1);
	if (isatty(0) && tcgetattr(0, &saved_) == 0) {
		termios t = saved_;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
		raw_ok_ = true;
	}
	// Alternate screen and hidden cursor, then the three reporting modes the
	// runtime already has sinks for and never received anything on. Each is
	// switched off again in suspend(): a terminal left in mouse-reporting mode
	// after the program exits pastes escape sequences into the user's shell
	// every time they click, and that outlives the process.
	//
	//   1006  SGR mouse reporting -- coordinates as decimal parameters rather
	//         than as bytes, so columns past 223 are expressible at all.
	//   1002  report presses, releases and drags (not bare motion, which is a
	//         packet per cell moved over an ssh link nobody is reading).
	//   2004  bracketed paste, so a paste arrives as text rather than as a
	//         burst of keystrokes that autorepeat handling cannot tell from
	//         typing.
	//   1004  focus in/out, which is how a TUI knows to dim its selection.
	if (tty_out_) {
		printf("\033[?1049h\033[?25l\033[?1006h\033[?1002h"
		       "\033[?2004h\033[?1004h");
		fflush(stdout);
	}

	if (s_winch_pipe[0] < 0 && ::pipe(s_winch_pipe) == 0) {
		fcntl(s_winch_pipe[0], F_SETFL, O_NONBLOCK);
		fcntl(s_winch_pipe[1], F_SETFL, O_NONBLOCK);
		struct sigaction sa{};
		sa.sa_handler = qtty_winch_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;      // do not break the read() in read_input()
		sigaction(SIGWINCH, &sa, nullptr);
	}
	if (s_winch_pipe[0] >= 0 && !winch_notifier_) {
		winch_notifier_ = new QSocketNotifier(s_winch_pipe[0],
		                                     QSocketNotifier::Read, this);
		QObject::connect(winch_notifier_, &QSocketNotifier::activated,
		                 this, [this] { read_winch(); });
	}
	active_ = true;
}

void AnsiBackend::suspend() {
	if (!active_) return;
	// The reporting modes go off before the alternate screen does, and they
	// matter more than the screen restore: a terminal left in mouse mode
	// writes an escape burst into the user's shell on every click, for the
	// rest of that shell's life. Reset in the reverse order they were set.
	if (tty_out_) {
		printf("\033[?1004l\033[?2004l\033[?1002l\033[?1006l"
		       "\033[0m\033[?1049l\033[?25h");
		fflush(stdout);
	}
	if (raw_ok_) tcsetattr(0, TCSANOW, &saved_);
	active_ = false;
}

// ---- output ----------------------------------------------------------------
// The last SGR state written, so that a run of identical cells costs nothing.
// It caches the cell's colours rather than the quantised ones: equal inputs
// quantise equally, and the authored ANSI-16 index is part of Color's identity
// (color.h), so two colours that would emit different bytes never compare
// equal here.
struct Sgr { Color fg, bg; Attrs attrs; bool primed = false; };

static void emit_sgr(QByteArray &out, const Cell &c, Sgr &cur,
                    Capabilities::ColorDepth depth) {
	if (cur.primed && c.fg == cur.fg && c.bg == cur.bg && c.attrs == cur.attrs) return;
	out += sgr_sequence(c.fg, c.bg, c.attrs, depth);   // section 6: theme.cpp owns
	cur = {c.fg, c.bg, c.attrs, true};                 // the three depths
}

void AnsiBackend::present(const CellBuffer &frame, const QRegion &) {
	// Full-frame emission: measured cheap (section 16.1 F9); damage-limited output
	// arrives with DEC 2026 bracketing in later polish.
	CellBuffer composed = frame;
	const bool pixel_placements = mode_ >= Capabilities::Sixel;
	if (!pixel_placements)                            // fallback tier: colour
		for (const CellImage &ci : frame.images)     // half-blocks (section 17.3)
			compose_halfblocks(composed, ci.pixmap.toImage(), ci.cell_rect);

	QByteArray out = "\033[H";
	Sgr cur;
	for (int y = 0; y < composed.rows(); ++y) {
		for (int x = 0; x < composed.cols(); ++x) {
			const Cell &c = composed.at(x, y);
			if (c.width == 0) continue;              // continuation of wide cell
			emit_sgr(out, c, cur, depth_);
			out += c.ch.toUtf8();
		}
		if (y < composed.rows() - 1) out += "\033[0m\r\n", cur = Sgr{};
	}
	// section 6 contrast rule, applied after mapping -- the only point at which
	// the emitted pairing is known. Reporting only, and never fatal; theme.cpp
	// says why. The cost is one memoised lookup per glyph-bearing cell.
	//
	// Deliberately on `frame` and not on `composed`: a half-block mosaic cell's
	// foreground and background are two adjacent pixels of an image, and a
	// legibility rule has nothing to say about those. Checking the composed
	// frame would report every image as hundreds of violations and drown the
	// theme faults this exists to find.
	contrast_violations(frame, depth_);
	if (pixel_placements) {                           // real pixels (section 5.7)
		// Every tier crops to the viewport first (section 16.3). A sticker
		// scrolled half out of view has a cell_rect running past the grid, and
		// placing it whole drew outside the terminal -- or, scrolled off the
		// top, at a negative row.
		const QSize grid(composed.cols(), composed.rows());
		if (mode_ == Capabilities::Kitty || mode_ == Capabilities::KittyAlpha) {
			out += kitty_delete_all();
			for (const CellImage &ci : frame.images) {
				const QImage img = ci.pixmap.toImage();
				const CroppedPlacement cp =
				    crop_placement(ci.cell_rect, img.size(), grid);
				if (cp.cells.isEmpty()) continue;     // wholly off screen
				out += moveTo(cp.cells.topLeft());
				const quint32 id = quint32(ci.key & 0xFFFFFF) + 1;
				// The upload is always the WHOLE image, and the crop is
				// applied at placement time through kitty's source rectangle.
				// Uploading the cropped pixels instead would file them under
				// the full image's cache key, and the next unclipped sighting
				// would show the crop.
				const bool whole = cp.source == QRect(QPoint(0, 0), img.size());
				if (!uploaded_.contains(ci.key)) {
					uploaded_.insert(ci.key);
					out += encode_kitty_image(id, img);
					if (!whole) out += kitty_place(id, 0, cp.source);
				} else {
					out += kitty_place(id, 0, whole ? QRect() : cp.source);
				}
			}
		} else if (mode_ == Capabilities::Sixel) {
			for (const CellImage &ci : frame.images) {
				const QImage img = ci.pixmap.toImage();
				const CroppedPlacement cp =
				    crop_placement(ci.cell_rect, img.size(), grid);
				if (cp.cells.isEmpty()) continue;
				out += moveTo(cp.cells.topLeft());
				// No source-rectangle mechanism here, so the image itself is
				// cropped. Safe: sixel is re-encoded every frame and cached by
				// nothing, so there is no stored copy to poison.
				out += encode_sixel(img.copy(cp.source));
			}
		} else if (mode_ == Capabilities::ITerm2) {
			for (const CellImage &ci : frame.images) {
				const QImage img = ci.pixmap.toImage();
				const CroppedPlacement cp =
				    crop_placement(ci.cell_rect, img.size(), grid);
				if (cp.cells.isEmpty()) continue;
				out += moveTo(cp.cells.topLeft());
				// Cropped image and cropped cell size together: OSC 1337 sizes
				// the image in cells, so cropping one without the other would
				// squeeze the whole picture into the visible rows instead of
				// hiding the part that is off screen.
				out += encode_iterm2(img.copy(cp.source),
				                     cp.cells.width(), cp.cells.height());
			}
		}
	}
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::present_pixels(const QImage &frame, const QRegion &) {
	QByteArray out = "\033[H";
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	switch (mode_) {
	case Capabilities::Kitty:
	case Capabilities::KittyAlpha:
		out += kitty_delete_all();
		out += encode_kitty_image(0xFFFFFF0u, frame);
		break;
	case Capabilities::Sixel:
		out += encode_sixel(frame);
		break;
	case Capabilities::ITerm2:
		out += encode_iterm2(frame, frame.width() / cw, frame.height() / ch);
		break;
	default:
		return;                                      // no pixel path
	}
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::present_overlay(int id, const QImage &rgba, QPoint cell, int z) {
	if (mode_ != Capabilities::KittyAlpha) return;
	QByteArray out = moveTo(cell);
	out += encode_kitty_image(0xFFFFE00u + quint32(id), rgba, z > 0 ? z : 1);
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::clear_overlay(int id) {
	if (mode_ != Capabilities::KittyAlpha) return;
	QByteArray out = "\033_Ga=d,d=i,q=2,i="
	               + QByteArray::number(0xFFFFE00u + quint32(id)) + ";\033\\";
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::set_cursor(std::optional<QPoint> cell, CursorShape shape) {
	if (cell && shape != CursorShape::Hidden)
		printf("\033[%d;%dH\033[?25h", cell->y() + 1, cell->x() + 1);
	else
		printf("\033[?25l");
	fflush(stdout);
}

// ---- input decoding --------------------------------------------------------
void AnsiBackend::read_input() {
	char buf[256];
	ssize_t n = ::read(0, buf, sizeof buf);
	if (n <= 0) {                                     // EOF: quit politely
		if (sink_) sink_->on_key({Qt::Key_D, QString(), true, false, false});
		return;
	}
	pending_.append(buf, n);
	while (!pending_.isEmpty()) { if (!decode_one()) break; }
}

// A CSI is ESC [ , an optional private prefix, semicolon-separated decimal
// parameters, then a final byte in 0x40..0x7e. Returns the number of bytes the
// sequence occupies, or -1 when the buffer does not hold all of it yet.
//
// The decoder this replaced read a fixed three bytes and switched on the
// third, which works for the arrow keys and for nothing else: an SGR mouse
// report is `ESC [ < 0 ; 34 ; 12 M`, and a bracketed paste opens with
// `ESC [ 2 0 0 ~`. Both were read as an unknown key and thrown away, three
// bytes at a time, which then desynchronised the rest of the buffer.
int AnsiBackend::parse_csi(QByteArray &prefix, QVector<int> &params,
                          char &final) const {
	int i = 2;                                    // past ESC [
	prefix.clear();
	params.clear();
	while (i < pending_.size() && strchr("<>?!", pending_[i]))
		prefix.append(pending_[i++]);
	int value = -1;
	while (i < pending_.size()) {
		const char c = pending_[i];
		if (c >= '0' && c <= '9') {
			value = (value < 0 ? 0 : value) * 10 + (c - '0');
			++i;
		} else if (c == ';') {
			params.append(value < 0 ? 0 : value);
			value = -1;
			++i;
		} else {
			break;
		}
	}
	if (i >= pending_.size()) return -1;          // still arriving
	if (value >= 0) params.append(value);
	final = pending_[i];
	if (final < 0x40 || final > 0x7e) return -1;  // not a terminator: wait
	return i + 1;
}

bool AnsiBackend::dispatch_csi(const QByteArray &prefix,
                              const QVector<int> &params, char final) {
	const auto param = [&](int n, int fallback) {
		return n < params.size() ? params[n] : fallback;
	};

	// SGR 1006 mouse: ESC [ < button ; col ; row M (press) or m (release).
	// Coordinates are 1-based. The button word carries the button in its low
	// two bits, motion at 32 and the wheel at 64.
	if (prefix == "<" && (final == 'M' || final == 'm')) {
		const int b = param(0, 0);
		MouseEvent m;
		m.cell = QPoint(param(1, 1) - 1, param(2, 1) - 1);
		m.motion = (b & 32) != 0;
		if (b & 64) {
			// Wheel. It reports as a press with no matching release, so it is
			// neither a press nor a release here -- delivering it as a press
			// would leave a button stuck down for the rest of the session.
			m.wheel = (b & 1) ? -1 : 1;
		} else {
			m.button = (b & 3) + 1;               // 1 left, 2 middle, 3 right
			m.press = (final == 'M') && !m.motion;
			m.release = (final == 'm');
		}
		sink_->on_mouse(m);
		return true;
	}

	if (final == '~') {
		switch (param(0, 0)) {
		case 200: in_paste_ = true;  paste_.clear(); return true;
		case 201:
			in_paste_ = false;
			sink_->on_paste(QString::fromUtf8(paste_));
			paste_.clear();
			return true;
		case 1: case 7:  sink_->on_key({Qt::Key_Home, {}, false, false, false}); return true;
		case 2:          sink_->on_key({Qt::Key_Insert, {}, false, false, false}); return true;
		case 3:          sink_->on_key({Qt::Key_Delete, {}, false, false, false}); return true;
		case 4: case 8:  sink_->on_key({Qt::Key_End, {}, false, false, false}); return true;
		case 5:          sink_->on_key({Qt::Key_PageUp, {}, false, false, false}); return true;
		case 6:          sink_->on_key({Qt::Key_PageDown, {}, false, false, false}); return true;
		default:         return true;             // consumed, unmapped
		}
	}

	// Focus reporting (1004). A TUI dims its selection when the terminal
	// loses focus, the way a desktop window does.
	if (final == 'I') { sink_->on_focus_change(true);  return true; }
	if (final == 'O') { sink_->on_focus_change(false); return true; }

	KeyEvent k;
	switch (final) {
	case 'A': k.qt_key = Qt::Key_Up; break;
	case 'B': k.qt_key = Qt::Key_Down; break;
	case 'C': k.qt_key = Qt::Key_Right; break;
	case 'D': k.qt_key = Qt::Key_Left; break;
	case 'H': k.qt_key = Qt::Key_Home; break;
	case 'F': k.qt_key = Qt::Key_End; break;
	case 'Z': k.qt_key = Qt::Key_Tab; k.shift = true; break;
	default:  return true;                        // consumed, unmapped
	}
	// xterm reports modifiers as a second parameter, 1 + a bitmask.
	const int mods = param(1, 1) - 1;
	k.shift = k.shift || (mods & 1);
	k.alt   = (mods & 2) != 0;
	k.ctrl  = (mods & 4) != 0;
	sink_->on_key(k);
	return true;
}

bool AnsiBackend::decode_one() {
	if (!sink_) { pending_.clear(); return false; }
	const unsigned char c = pending_[0];

	// Inside a bracketed paste every byte is content until the closing CSI,
	// including bytes that would otherwise be keys. That is the whole point of
	// the mode: a newline in pasted text is text, not Return.
	if (in_paste_) {
		if (c == 0x1b && pending_.size() >= 2 && pending_[1] == '[') {
			QByteArray prefix; QVector<int> params; char final = 0;
			const int n = parse_csi(prefix, params, final);
			if (n < 0) return false;              // wait for the rest
			if (final == '~' && !params.isEmpty() && params[0] == 201) {
				pending_.remove(0, n);
				return dispatch_csi(prefix, params, final);
			}
			paste_.append(pending_.left(n));      // an escape inside the paste
			pending_.remove(0, n);
			return true;
		}
		paste_.append(char(c));
		pending_.remove(0, 1);
		return true;
	}

	if (c == 0x1b) {                              // ESC sequences
		if (pending_.size() < 2) return false;
		if (pending_[1] == '[') {
			QByteArray prefix; QVector<int> params; char final = 0;
			const int n = parse_csi(prefix, params, final);
			if (n < 0) return false;              // still arriving
			pending_.remove(0, n);
			return dispatch_csi(prefix, params, final);
		}
		if (pending_.size() < 2) return false;
		// Alt-<char>, and the character may be multi-byte for the same reason
		// the plain path below handles: ESC then a UTF-8 lead byte is Alt held
		// with a non-ASCII key. Decoded as a sequence rather than as one byte,
		// which is what made the plain path wrong.
		const unsigned char alt = static_cast<unsigned char>(pending_[1]);
		int len = 1;
		if      ((alt & 0xE0) == 0xC0) len = 2;
		else if ((alt & 0xF0) == 0xE0) len = 3;
		else if ((alt & 0xF8) == 0xF0) len = 4;
		if (pending_.size() < 1 + len) return false;      // still arriving
		KeyEvent k;
		k.alt = true;
		k.text = QString::fromUtf8(pending_.mid(1, len));
		pending_.remove(0, 1 + len);
		sink_->on_key(k);
		return true;
	}

	// A byte with the high bit set opens a UTF-8 sequence, and the whole
	// sequence is one character. It used to become QString(QChar(c)), which is
	// Latin-1 for one byte: typing an e-acute sent 0xC3 0xA9 and produced two
	// key events carrying two wrong characters, and a CJK character produced
	// three. Every non-ASCII keystroke was corrupted, in a library whose cell
	// model is built on grapheme clusters -- the input path could not deliver
	// even one.
	//
	// Incomplete sequences wait, exactly as a half-arrived CSI does: a
	// terminal splits input at any byte, and a read() boundary in the middle
	// of a character is ordinary rather than exceptional.
	if (c >= 0x80) {
		int len = 0;
		if      ((c & 0xE0) == 0xC0) len = 2;
		else if ((c & 0xF0) == 0xE0) len = 3;
		else if ((c & 0xF8) == 0xF0) len = 4;
		if (len == 0) {
			// A continuation byte with no lead, or an invalid lead. Dropped
			// rather than delivered: it is not a character, and passing it on
			// as Latin-1 is what this replaced.
			pending_.remove(0, 1);
			return true;
		}
		if (pending_.size() < len) return false;      // still arriving
		const QByteArray seq = pending_.left(len);
		pending_.remove(0, len);
		KeyEvent utf8;
		utf8.qt_key = 0;
		utf8.text = QString::fromUtf8(seq);
		sink_->on_key(utf8);
		return true;
	}

	pending_.remove(0, 1);
	KeyEvent k;
	if (c == '\r' || c == '\n')       k.qt_key = Qt::Key_Return;
	else if (c == 0x7f || c == 0x08)  k.qt_key = Qt::Key_Backspace;
	else if (c == '\t')               k.qt_key = Qt::Key_Tab;
	else if (c < 0x20) { k.qt_key = Qt::Key_A + (c - 1); k.ctrl = true; }   // ^A..^Z
	else { k.qt_key = 0; k.text = QString(QChar(c)); }
	sink_->on_key(k);
	return true;
}

} // namespace Qtty
