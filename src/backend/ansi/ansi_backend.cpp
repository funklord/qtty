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
// detectGraphicsMode() in src/graphics/graphics.cpp, deliberately: an explicit
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
	mode_ = detectGraphicsMode();
	depth_ = detect_color_depth();
	winsize ws{};
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		cells_ = QSize(ws.ws_col, ws.ws_row);
	else
		cells_ = QSize(80, 24);                     // piped/CI fallback
	resume();
	notifier_ = new QSocketNotifier(0, QSocketNotifier::Read, this);
	connect(notifier_, &QSocketNotifier::activated, this, [this] { readInput(); });
}

AnsiBackend::~AnsiBackend() { suspend(); }

Capabilities AnsiBackend::capabilities() const {
	Capabilities c;
	c.color = depth_;                               // negotiated (section 6)
	c.graphics = mode_;                             // negotiated (sections 5.7, 17.3)

	// These four were fields nobody set and nobody read. They are answers
	// about this backend now, and each is answerable because resume() asks
	// the terminal for the mode and decodeOne() understands the replies.
	//
	// mouse and bracketedPaste are reported for a tty because the modes are
	// requested unconditionally and a terminal that does not implement one
	// ignores the request -- there is no reply to wait for, and the DCS
	// handshake doc/beerssh.md proposes is what would turn these from "asked
	// for" into "confirmed".
	c.mouse = rawOk_;
	c.bracketedPaste = rawOk_;
	c.unicodeWide = true;                           // L2 measures width itself

	// DEC 2026 is NOT claimed. section 11 wants synchronised output to
	// eliminate tearing, and nothing emits the brackets yet; saying true here
	// would be a field describing an intention rather than the backend.
	c.synchronisedOutput = false;
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
// ITerminalEventSink::onResize existed, InputRouter implemented it, and
// nothing ever called it -- dragging a terminal's edge did nothing whatever.
static int s_winch_pipe[2] = {-1, -1};

extern "C" void qtty_winch_handler(int) {
	if (s_winch_pipe[1] >= 0) {
		const char b = 1;
		const ssize_t ignored = ::write(s_winch_pipe[1], &b, 1);
		(void)ignored;                 // nothing useful to do in a handler
	}
}

void AnsiBackend::readWinch() {
	char drain[64];
	while (::read(s_winch_pipe[0], drain, sizeof drain) > 0) { }
	winsize ws{};
	if (ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col <= 0) return;
	const QSize now(ws.ws_col, ws.ws_row);
	if (now == cells_) return;         // SIGWINCH also fires for pixel-only changes
	cells_ = now;
	if (sink_) sink_->onResize(cells_);
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
		rawOk_ = true;
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
		sa.sa_flags = SA_RESTART;      // do not break the read() in readInput()
		sigaction(SIGWINCH, &sa, nullptr);
	}
	if (s_winch_pipe[0] >= 0 && !winch_notifier_) {
		winch_notifier_ = new QSocketNotifier(s_winch_pipe[0],
		                                     QSocketNotifier::Read, this);
		QObject::connect(winch_notifier_, &QSocketNotifier::activated,
		                 this, [this] { readWinch(); });
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
	if (rawOk_) tcsetattr(0, TCSANOW, &saved_);
	active_ = false;
}

// ---- output ----------------------------------------------------------------
// The last SGR state written, so that a run of identical cells costs nothing.
// It caches the cell's colours rather than the quantised ones: equal inputs
// quantise equally, and the authored ANSI-16 index is part of Color's identity
// (color.h), so two colours that would emit different bytes never compare
// equal here.
struct Sgr { Color fg, bg; Attrs attrs; bool primed = false; };

static void emitSgr(QByteArray &out, const Cell &c, Sgr &cur,
                    Capabilities::ColorDepth depth) {
	if (cur.primed && c.fg == cur.fg && c.bg == cur.bg && c.attrs == cur.attrs) return;
	out += sgr_sequence(c.fg, c.bg, c.attrs, depth);   // section 6: theme.cpp owns
	cur = {c.fg, c.bg, c.attrs, true};                 // the three depths
}

void AnsiBackend::present(const CellBuffer &frame, const QRegion &) {
	// Full-frame emission: measured cheap (section 16.1 F9); damage-limited output
	// arrives with DEC 2026 bracketing in later polish.
	CellBuffer composed = frame;
	const bool pixelPlacements = mode_ >= Capabilities::Sixel;
	if (!pixelPlacements)                            // fallback tier: colour
		for (const CellImage &ci : frame.images)     // half-blocks (section 17.3)
			composeHalfblocks(composed, ci.pixmap.toImage(), ci.cellRect);

	QByteArray out = "\033[H";
	Sgr cur;
	for (int y = 0; y < composed.rows(); ++y) {
		for (int x = 0; x < composed.cols(); ++x) {
			const Cell &c = composed.at(x, y);
			if (c.width == 0) continue;              // continuation of wide cell
			emitSgr(out, c, cur, depth_);
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
	if (pixelPlacements) {                           // real pixels (section 5.7)
		if (mode_ == Capabilities::Kitty || mode_ == Capabilities::KittyAlpha) {
			out += kittyDeleteAll();
			for (const CellImage &ci : frame.images) {
				out += moveTo(ci.cellRect.topLeft());
				const quint32 id = quint32(ci.key & 0xFFFFFF) + 1;
				if (!uploaded_.contains(ci.key)) {
					uploaded_.insert(ci.key);
					out += encodeKittyImage(id, ci.pixmap.toImage());
				} else {
					out += kittyPlace(id);           // upload-once: ~30 bytes
				}
			}
		} else if (mode_ == Capabilities::Sixel) {
			for (const CellImage &ci : frame.images) {
				out += moveTo(ci.cellRect.topLeft());
				out += encodeSixel(ci.pixmap.toImage());
			}
		} else if (mode_ == Capabilities::ITerm2) {
			for (const CellImage &ci : frame.images) {
				out += moveTo(ci.cellRect.topLeft());
				out += encodeITerm2(ci.pixmap.toImage(),
				                    ci.cellRect.width(), ci.cellRect.height());
			}
		}
	}
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::presentPixels(const QImage &frame, const QRegion &) {
	QByteArray out = "\033[H";
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	switch (mode_) {
	case Capabilities::Kitty:
	case Capabilities::KittyAlpha:
		out += kittyDeleteAll();
		out += encodeKittyImage(0xFFFFFF0u, frame);
		break;
	case Capabilities::Sixel:
		out += encodeSixel(frame);
		break;
	case Capabilities::ITerm2:
		out += encodeITerm2(frame, frame.width() / cw, frame.height() / ch);
		break;
	default:
		return;                                      // no pixel path
	}
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::presentOverlay(int id, const QImage &rgba, QPoint cell, int z) {
	if (mode_ != Capabilities::KittyAlpha) return;
	QByteArray out = moveTo(cell);
	out += encodeKittyImage(0xFFFFE00u + quint32(id), rgba, z > 0 ? z : 1);
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::clearOverlay(int id) {
	if (mode_ != Capabilities::KittyAlpha) return;
	QByteArray out = "\033_Ga=d,d=i,q=2,i="
	               + QByteArray::number(0xFFFFE00u + quint32(id)) + ";\033\\";
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

void AnsiBackend::setCursor(std::optional<QPoint> cell, CursorShape shape) {
	if (cell && shape != CursorShape::Hidden)
		printf("\033[%d;%dH\033[?25h", cell->y() + 1, cell->x() + 1);
	else
		printf("\033[?25l");
	fflush(stdout);
}

// ---- input decoding --------------------------------------------------------
void AnsiBackend::readInput() {
	char buf[256];
	ssize_t n = ::read(0, buf, sizeof buf);
	if (n <= 0) {                                     // EOF: quit politely
		if (sink_) sink_->onKey({Qt::Key_D, QString(), true, false, false});
		return;
	}
	pending_.append(buf, n);
	while (!pending_.isEmpty()) { if (!decodeOne()) break; }
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
int AnsiBackend::parseCsi(QByteArray &prefix, QVector<int> &params,
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

bool AnsiBackend::dispatchCsi(const QByteArray &prefix,
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
		sink_->onMouse(m);
		return true;
	}

	if (final == '~') {
		switch (param(0, 0)) {
		case 200: in_paste_ = true;  paste_.clear(); return true;
		case 201:
			in_paste_ = false;
			sink_->onPaste(QString::fromUtf8(paste_));
			paste_.clear();
			return true;
		case 1: case 7:  sink_->onKey({Qt::Key_Home, {}, false, false, false}); return true;
		case 2:          sink_->onKey({Qt::Key_Insert, {}, false, false, false}); return true;
		case 3:          sink_->onKey({Qt::Key_Delete, {}, false, false, false}); return true;
		case 4: case 8:  sink_->onKey({Qt::Key_End, {}, false, false, false}); return true;
		case 5:          sink_->onKey({Qt::Key_PageUp, {}, false, false, false}); return true;
		case 6:          sink_->onKey({Qt::Key_PageDown, {}, false, false, false}); return true;
		default:         return true;             // consumed, unmapped
		}
	}

	// Focus reporting (1004). A TUI dims its selection when the terminal
	// loses focus, the way a desktop window does.
	if (final == 'I') { sink_->onFocusChange(true);  return true; }
	if (final == 'O') { sink_->onFocusChange(false); return true; }

	KeyEvent k;
	switch (final) {
	case 'A': k.qtKey = Qt::Key_Up; break;
	case 'B': k.qtKey = Qt::Key_Down; break;
	case 'C': k.qtKey = Qt::Key_Right; break;
	case 'D': k.qtKey = Qt::Key_Left; break;
	case 'H': k.qtKey = Qt::Key_Home; break;
	case 'F': k.qtKey = Qt::Key_End; break;
	case 'Z': k.qtKey = Qt::Key_Tab; k.shift = true; break;
	default:  return true;                        // consumed, unmapped
	}
	// xterm reports modifiers as a second parameter, 1 + a bitmask.
	const int mods = param(1, 1) - 1;
	k.shift = k.shift || (mods & 1);
	k.alt   = (mods & 2) != 0;
	k.ctrl  = (mods & 4) != 0;
	sink_->onKey(k);
	return true;
}

bool AnsiBackend::decodeOne() {
	if (!sink_) { pending_.clear(); return false; }
	const unsigned char c = pending_[0];

	// Inside a bracketed paste every byte is content until the closing CSI,
	// including bytes that would otherwise be keys. That is the whole point of
	// the mode: a newline in pasted text is text, not Return.
	if (in_paste_) {
		if (c == 0x1b && pending_.size() >= 2 && pending_[1] == '[') {
			QByteArray prefix; QVector<int> params; char final = 0;
			const int n = parseCsi(prefix, params, final);
			if (n < 0) return false;              // wait for the rest
			if (final == '~' && !params.isEmpty() && params[0] == 201) {
				pending_.remove(0, n);
				return dispatchCsi(prefix, params, final);
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
			const int n = parseCsi(prefix, params, final);
			if (n < 0) return false;              // still arriving
			pending_.remove(0, n);
			return dispatchCsi(prefix, params, final);
		}
		if (pending_.size() < 2) return false;
		const char alt = pending_[1];
		pending_.remove(0, 2);
		KeyEvent k;
		k.alt = true;
		k.text = QString(QChar(alt));
		sink_->onKey(k);
		return true;
	}

	pending_.remove(0, 1);
	KeyEvent k;
	if (c == '\r' || c == '\n')       k.qtKey = Qt::Key_Return;
	else if (c == 0x7f || c == 0x08)  k.qtKey = Qt::Key_Backspace;
	else if (c == '\t')               k.qtKey = Qt::Key_Tab;
	else if (c < 0x20) { k.qtKey = Qt::Key_A + (c - 1); k.ctrl = true; }   // ^A..^Z
	else { k.qtKey = 0; k.text = QString(QChar(c)); }
	sink_->onKey(k);
	return true;
}

} // namespace Qtty
