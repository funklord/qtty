#include "ansi_backend.h"
#include "qtty/graphics.h"
#include "qtty/grid.h"
#include "qtty/theme.h"
#include <QSocketNotifier>
#include <QImage>
#include <QCoreApplication>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>

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
	return c;
}

static QByteArray moveTo(QPoint cell) {
	return "\033[" + QByteArray::number(cell.y() + 1) + ';'
		 + QByteArray::number(cell.x() + 1) + 'H';
}

QSize AnsiBackend::size() const { return cells_; }

void AnsiBackend::resume() {
	if (active_) return;
	if (isatty(0) && tcgetattr(0, &saved_) == 0) {
		termios t = saved_;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
		rawOk_ = true;
	}
	printf("\033[?1049h\033[?25l");
	fflush(stdout);
	active_ = true;
}

void AnsiBackend::suspend() {
	if (!active_) return;
	printf("\033[0m\033[?1049l\033[?25h");
	fflush(stdout);
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

bool AnsiBackend::decodeOne() {
	if (!sink_) { pending_.clear(); return false; }
	unsigned char c = pending_[0];
	if (c == 0x1b) {                                  // ESC sequences
		if (pending_.size() < 2) return false;
		if (pending_[1] == '[') {
			if (pending_.size() < 3) return false;
			char fin = pending_[2];
			int consumed = 3;
			KeyEvent k;
			switch (fin) {
			case 'A': k.qtKey = Qt::Key_Up; break;
			case 'B': k.qtKey = Qt::Key_Down; break;
			case 'C': k.qtKey = Qt::Key_Right; break;
			case 'D': k.qtKey = Qt::Key_Left; break;
			case 'H': k.qtKey = Qt::Key_Home; break;
			case 'F': k.qtKey = Qt::Key_End; break;
			case 'Z': k.qtKey = Qt::Key_Tab; k.shift = true; break;
			case '3': k.qtKey = Qt::Key_Delete;  consumed = 4; break;   // 3~
			case '5': k.qtKey = Qt::Key_PageUp;  consumed = 4; break;   // 5~
			case '6': k.qtKey = Qt::Key_PageDown; consumed = 4; break;  // 6~
			default:  k.qtKey = 0; break;
			}
			if (pending_.size() < consumed) return false;
			pending_.remove(0, consumed);
			if (k.qtKey) sink_->onKey(k);
			return true;
		}
		pending_.remove(0, 2);                        // Alt-<char>
		KeyEvent k; k.alt = true; k.text = QString(QChar(pending_.isEmpty() ? 0 : c));
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
