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

// What the terminal is, given what it answered and what the environment
// claims. The order is the whole design.
//
// 1. An explicit override wins outright: it is the contract with a
//    cooperating terminal (doc/beerssh.md) and the testing hook.
// 2. A terminal that ANSWERED is believed completely, and that includes
//    believing its silences: with the fence present, no kitty reply is a real
//    "no". This is the only step allowed to turn a capability off.
// 3. A terminal that answered nothing has told us nothing, so fall back to
//    reading $TERM as qtty always did. Not to the floor: an unanswering
//    terminal is usually one behind a multiplexer, not one without pixels.
//
// The asymmetry in step 3 is the point. A guess that says yes to kitty costs
// a screenful of escape sequences; a guess that says no costs half-blocks.
// Only step 2 has measured its answer, so only step 2 may say no.
Capabilities::GraphicsMode negotiate_graphics(const TermCaps &caps) {
	const QByteArray force = qgetenv("QTTY_GRAPHICS").toLower();
	if (!force.isEmpty()) return detect_graphics_mode();

	// Inside tmux the pixel tiers are refused, however capable the terminal
	// underneath turns out to be, and the reason is placement rather than
	// capability. Passthrough carries the IMAGE to the outer terminal, but
	// the cursor it lands at is the outer terminal's, not the one tmux is
	// drawing with -- so the picture arrives in the wrong place. Kitty's
	// Unicode-placeholder mode is the fix, because it makes a placement a run
	// of ordinary text cells that tmux moves like any other text; until that
	// exists, half-blocks are the honest answer, being text already.
	//
	// This is what qtty did before by accident -- $TERM reads as screen
	// inside tmux, so it fell to half-blocks without knowing why. The
	// difference is that the query still goes out, wrapped, so the colour
	// depth and the background are learned from the real terminal.
	// Inside tmux a DIRECT placement lands at the outer terminal's cursor
	// rather than where tmux is drawing, so it arrives in the wrong place.
	// Unicode placeholders fix that by making a placement ordinary text --
	// but only where the id can survive the trip, which is what
	// use_placeholders() decides. Where it cannot, half-blocks are still the
	// honest answer, being text already.
	if (inside_tmux())
		return use_placeholders(caps, negotiate_color(caps))
		           ? Capabilities::Kitty : Capabilities::Halfblocks;

	if (caps.kitty || caps.answered) {
		if (caps.kitty) {
			// The protocol is proven; which VARIANT is not, and cannot be
			// asked. $TERM chooses between two modes that both work, so a
			// wrong guess here costs appearance rather than correctness.
			const QByteArray term = qgetenv("TERM").toLower();
			const QByteArray prog = qgetenv("TERM_PROGRAM").toLower();
			if (prog.contains("wezterm")) return Capabilities::Kitty;
			if (term.contains("kitty") || term.contains("ghostty")
			    || !qgetenv("KITTY_WINDOW_ID").isEmpty())
				return Capabilities::KittyAlpha;
			return Capabilities::Kitty;      // proven protocol, unproven alpha
		}
		if (caps.sixel) return Capabilities::Sixel;
		return Capabilities::Halfblocks;     // answered, and has neither
	}
	return detect_graphics_mode();           // learned nothing; read $TERM
}

// Colour, under the same rule with one addition: a weak signal may say YES
// here because the cost of being wrong is a colour approximated rather than a
// screen full of bytes the terminal cannot read. $COLORTERM survives an ssh
// into a machine whose terminal is not the one that set it, which is exactly
// why XTGETTCAP is asked first and $COLORTERM is only ever consulted upward.
Capabilities::ColorDepth negotiate_color(const TermCaps &caps) {
	const QByteArray force = qgetenv("QTTY_COLOR").toLower();
	if (!force.isEmpty()) return detect_color_depth();
	if (caps.truecolor) return Capabilities::TrueColor;
	return detect_color_depth();
}

bool use_placeholders(const TermCaps &caps, Capabilities::ColorDepth depth) {
	// Needed only where something in between would move a direct placement
	// without knowing it had. Outside tmux a real placement is cheaper and
	// exact, so placeholders would be a downgrade.
	if (!inside_tmux()) return false;
	// The terminal must actually speak the protocol. Assuming it does is the
	// mistake this whole negotiation exists to stop.
	if (!caps.kitty) return false;
	// And the id must survive: it travels in the FOREGROUND COLOUR, so at 256
	// colours it would be quantised to a palette index and the terminal would
	// look up the wrong image, or none. True colour carries all 24 bits.
	return depth == Capabilities::TrueColor;
}

AnsiBackend::AnsiBackend() {
	clock_.start();
	winsize ws{};
	// Both dimensions, which this asked about one of. A terminal that reports
	// zero ROWS with a good column count is the same nonsense as one that
	// reports zero columns -- `stty rows 0` produces it, and so does a pty
	// whose size was set only partly -- and it arrives at the same place: a
	// frame with no cells, whose rasterisation is a null QImage that QPainter
	// refuses to open and warns about once per call, onto the terminal.
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0)
		cells_ = QSize(ws.ws_col, ws.ws_row);
	else
		cells_ = QSize(80, 24);                     // piped/CI fallback
	resume();

	// Ask the terminal what it is, now that resume() has put it in raw mode
	// -- a cooked line discipline would hold every reply until a newline that
	// is never coming -- and before the notifier owns stdin, since two readers
	// of the same descriptor is how a reply gets torn in half.
	//
	// Only when both ends are a terminal. Down a pipe there is nobody to
	// answer, and the query would be written into whatever is reading and the
	// caller's own input eaten waiting for a reply that cannot arrive.
	if (raw_ok_ && tty_out_)
		caps_ = collect_caps(0, 1, 100);

	mode_ = negotiate_graphics(caps_);
	depth_ = negotiate_color(caps_);

	// The terminal's own low sixteen, where it answered for all of them. A
	// partial answer is refused rather than mixed with the built-in table:
	// half a user's scheme and half xterm's is a palette no terminal has, and
	// matching against it would be worse than matching against either.
	if (caps_.palette16.size() == 16) {
		bool complete = true;
		for (QRgb c : caps_.palette16) if (c == 0) complete = false;
		if (complete) set_terminal_palette(caps_.palette16);
	}
	notifier_ = new QSocketNotifier(0, QSocketNotifier::Read, this);
	connect(notifier_, &QSocketNotifier::activated, this, [this] { read_input(); });
}

AnsiBackend::~AnsiBackend() { suspend(); }

Capabilities AnsiBackend::capabilities() const {
	Capabilities c;
	c.color = depth_;                               // negotiated (section 6)
	c.graphics = mode_;                             // negotiated (sections 5.7, 17.3)
	c.unicode_placements = use_placeholders(caps_, depth_);
	c.cell_px = caps_.cell_px;                      // invalid until it answers
	c.background_known = caps_.bg_known;
	if (caps_.bg_known)
		c.background = QColor(caps_.bg[0], caps_.bg[1], caps_.bg[2]);

	// These four were fields nobody set and nobody read. They are answers
	// about this backend now, and each is answerable because resume() asks
	// the terminal for the mode and decode_one() understands the replies.
	//
	// mouse and bracketed_paste are reported for a tty because the modes are
	// requested unconditionally and a terminal that does not implement one
	// ignores the request -- there is no reply to wait for, and the DCS
	// handshake doc/beerssh.md proposes is what would turn these from "asked
	// for" into "confirmed".
	// Raw mode is a fact about the local tty and says nothing about what the
	// terminal understands, so it is now only the ASSUMPTION -- what to
	// believe when the terminal did not answer. A definite "not recognised"
	// from DECRQM overrides it; silence leaves it alone.
	c.mouse = mode_usable(caps_, 1006, raw_ok_);
	c.bracketed_paste = mode_usable(caps_, 2004, raw_ok_);
	c.unicode_wide = true;                           // L2 measures width itself

	// DEC 2026 is NOT claimed. section 11 wants synchronised output to
	// eliminate tearing, and nothing emits the brackets yet; saying true here
	// would be a field describing an intention rather than the backend.
	// DEC 2026, and the field still means "qtty uses synchronised output"
	// rather than "the terminal has it" -- it is true because present()
	// brackets its frames, and it is only true when the terminal confirmed
	// the mode.
	//
	// CONFIRMED ONLY, with the assumption false, which is the opposite
	// default to mouse and paste above and deliberately so. Those two are
	// about input qtty would otherwise mishandle, so silence leaves the
	// working assumption alone. This is about an optimisation worth nothing
	// on a terminal that lacks it, and DECRQM is the discovery mechanism the
	// synchronised-output specification itself names -- so a terminal that
	// says nothing has declined to be asked, and gets unbracketed frames.
	c.synchronised_output = sync_frames();
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

// Ask again for the pixel geometry. The answer does NOT come back here -- it
// arrives on stdin like everything else the terminal says, and the decoder
// routes it into caps_. That is the shape of the whole capability channel:
// graphics is tied to input because the terminal has only one way to talk
// back, and a resize is the event that makes the old answer wrong.
// Whether frames are bracketed. One place, because present() and
// capabilities() must not be able to disagree: a field claiming synchronised
// output while the frames go out bare is exactly the shape of defect this
// negotiation exists to stop, and it would be invisible from inside.
bool AnsiBackend::sync_frames() const {
	return tty_out_ && mode_usable(caps_, 2026, false);
}

void AnsiBackend::query_geometry() {
	if (!tty_out_) return;
	const char q[] = "\033[14t\033[16t";
	const ssize_t n = ::write(1, q, sizeof(q) - 1);
	(void)n;                           // a terminal that will not answer is fine
}

void AnsiBackend::read_winch() {
	char drain[64];
	while (::read(s_winch_pipe[0], drain, sizeof drain) > 0) { }
	winsize ws{};
	// ws_row as well as ws_col, for the reason the constructor now gives:
	// this refused a zero column count and accepted a zero row count, and the
	// two are one condition arriving at one place.
	if (ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col <= 0 || ws.ws_row <= 0) return;

	// Before the early return below, not after it. The comment on that return
	// names the exact case this is for: SIGWINCH also fires when only the
	// PIXEL size changed -- a font size change, or a window resize that does
	// not cross a cell boundary -- and then the cell count is the one thing
	// that has not moved while every pixel measurement qtty holds has.
	query_geometry();

	const QSize now(ws.ws_col, ws.ws_row);
	if (now == cells_) return;
	cells_ = now;
	if (sink_) sink_->on_resize(cells_);
}

// ---- restoring the terminal when nothing gets to run ----------------------
// suspend() undoes everything resume() did, and it runs from the destructor.
// A destructor is not reached by a signal, and it is not reached by exit()
// either. Measured: before this, SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV and
// SIGABRT were all at their default disposition with the backend running --
// so a kill from another window, a crash, or a hangup left the terminal in
// raw mode, on the alternate screen, with mouse reporting on and the cursor
// hidden.
//
// suspend()'s own comment says what that costs: "a terminal left in mouse
// mode writes an escape burst into the user's shell on every click, for the
// rest of that shell's life." The happy path was defended and nothing else
// was.
//
// Everything here is async-signal-safe. write(2) and tcsetattr are on POSIX's
// list; printf and fflush, which suspend() uses, are not -- a handler that
// called them could deadlock on stdio's own lock, which is exactly the kind
// of failure that only happens on the day something has already gone wrong.
namespace {

struct Restore {
	struct termios saved {};
	volatile sig_atomic_t armed = 0;
	int raw = 0, tty = 0;
};
Restore g_restore;

// The signals that end a process without unwinding. SIGWINCH and SIGPIPE are
// not here: they have handlers of their own and do not terminate.
const int g_fatal[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT,
                        SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
struct sigaction g_prev[sizeof(g_fatal) / sizeof(g_fatal[0])];
bool g_installed = false;

void emergency_restore() {
	if (!g_restore.armed) return;
	g_restore.armed = 0;                 // once, however many signals arrive
	// The same sequence suspend() writes, in the same order and for the same
	// reason: the reporting modes go off before the alternate screen does.
	static const char seq[] = "\033[?1004l\033[?2004l\033[?1002l\033[?1006l"
	                          "\033[0m\033[?1049l\033[?25h";
	if (g_restore.tty) {
		const ssize_t n = ::write(STDOUT_FILENO, seq, sizeof(seq) - 1);
		(void)n;                         // nothing useful to do in a handler
	}
	if (g_restore.raw) tcsetattr(0, TCSANOW, &g_restore.saved);
}

extern "C" void qtty_fatal_handler(int sig) {
	emergency_restore();
	// Back to the default and re-raise, so the exit status, the core dump and
	// whatever the shell reports are what they would have been. Restoring the
	// terminal is the only thing this adds; it does not swallow the failure.
	signal(sig, SIG_DFL);
	raise(sig);
}

} // namespace

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

	// SIGPIPE would kill the process outright when the far end of the output
	// goes away, and for a terminal program that is an ordinary event: the
	// window is closed, or the output is a pipe whose reader has finished.
	// Found by writing the capability query to a socket whose peer had
	// closed -- the whole suite died with signal 13 and no message at all,
	// which is what this failure always looks like.
	//
	// Ignored rather than handled: every write here already checks its
	// result, so the error path exists and a signal only prevents it from
	// running. Taken at the same point as SIGWINCH, which is where qtty takes
	// over the terminal anyway.
	signal(SIGPIPE, SIG_IGN);

	if (s_winch_pipe[0] < 0 && ::pipe(s_winch_pipe) == 0) {
		// Both ends non-blocking, and the RESULT READ. read_winch() drains
		// this pipe with `while (read(...) > 0)` on the GUI thread, so a
		// descriptor that stayed blocking does not degrade the resize
		// handling -- it freezes the application on the first SIGWINCH, a
		// long way from here and with nothing pointing back.
		//
		// The result was discarded before, which is worse than an unchecked
		// guard: there was not even a condition to fail. Carrying on without
		// the flag keeps exactly the hazard the line exists to remove. If it
		// cannot be set, the pipe is closed and no notifier is installed --
		// resizes are missed, which is a visible degradation rather than a
		// hang.
		const int rf = fcntl(s_winch_pipe[0], F_SETFL, O_NONBLOCK);
		const int wf = fcntl(s_winch_pipe[1], F_SETFL, O_NONBLOCK);
		if (rf < 0 || wf < 0) {
			qWarning("qtty: the SIGWINCH pipe cannot be made non-blocking, so "
			         "terminal resizes will not be noticed; draining it on the "
			         "GUI thread would hang the application instead");
			::close(s_winch_pipe[0]);
			::close(s_winch_pipe[1]);
			s_winch_pipe[0] = s_winch_pipe[1] = -1;
			// Not a return: the notifier below is already conditional on a
			// valid descriptor, and returning here would skip `active_ = true`
			// and leave the backend in a state suspend() declines to undo.
			// Fixing one inert failure path by writing another would be a
			// poor way to take the lesson.
		} else {
			struct sigaction sa{};
			sa.sa_handler = qtty_winch_handler;
			sigemptyset(&sa.sa_mask);
			sa.sa_flags = SA_RESTART;      // do not break the read() in read_input()
			sigaction(SIGWINCH, &sa, nullptr);
		}
	}
	if (s_winch_pipe[0] >= 0 && !winch_notifier_) {
		winch_notifier_ = new QSocketNotifier(s_winch_pipe[0],
		                                     QSocketNotifier::Read, this);
		QObject::connect(winch_notifier_, &QSocketNotifier::activated,
		                 this, [this] { read_winch(); });
	}
	// Armed only once the state it would restore is known.
	g_restore.saved = saved_;
	g_restore.raw = raw_ok_ ? 1 : 0;
	g_restore.tty = tty_out_ ? 1 : 0;
	g_restore.armed = 1;
	if (!g_installed) {
		struct sigaction sa {};
		sa.sa_handler = qtty_fatal_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESETHAND;    // belt as well as braces on re-entry
		for (size_t i = 0; i < sizeof(g_fatal) / sizeof(g_fatal[0]); ++i)
			sigaction(g_fatal[i], &sa, &g_prev[i]);
		g_installed = true;
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
	// The handlers go with it: a program that suspends to shell out has a
	// terminal it did not take over, and a crash in the shell is not this
	// library's to tidy after.
	g_restore.armed = 0;
	if (g_installed) {
		for (size_t i = 0; i < sizeof(g_fatal) / sizeof(g_fatal[0]); ++i)
			sigaction(g_fatal[i], &g_prev[i], nullptr);
		g_installed = false;
	}
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
	// Sixel and iTerm2 have no placement handles, so moving an image means
	// re-emitting it -- on a slow link that is the whole frame budget spent
	// on a picture about to move again. While placements are moving they
	// degrade to the half-block mosaic, which is cells and diffs like any
	// other text, and the real pixels come back once scrolling settles
	// (design.md section 5.7).
	//
	// Kitty is excluded deliberately: there a placement has a handle and
	// moving it is one short escape with no re-upload, so degrading would
	// trade a cheap correct picture for a coarse one and buy nothing.
	// Placeholders first: they replace the placement mechanism entirely, so
	// none of the tier logic below applies. The image is transmitted once and
	// the placement becomes ordinary text cells, which the loop that emits
	// text handles with no knowledge that they are an image at all -- which
	// is the entire point of the mode.
	const bool placeholders = use_placeholders(caps_, depth_);
	QByteArray uploads;
	if (placeholders) {
		for (const CellImage &ci : frame.images) {
			const quint32 id = quint32(ci.key & 0xFFFFFF) + 1;
			if (!uploaded_.contains(ci.key)) {
				uploaded_.insert(ci.key);
				uploads += encode_kitty_virtual(id, ci.pixmap.toImage(),
				                                ci.cell_rect.width(),
				                                ci.cell_rect.height());
			}
			compose_kitty_placeholders(composed, id, ci.cell_rect);
		}
		// Wrapped so tmux forwards the transmission to the terminal
		// underneath. The placeholder CELLS are deliberately NOT wrapped:
		// they are text, and tmux is meant to see them and move them.
		if (!uploads.isEmpty() && inside_tmux()) uploads = tmux_wrap(uploads);
	}

	const bool handles = mode_ == Capabilities::Kitty
	                  || mode_ == Capabilities::KittyAlpha;
	const bool settled = handles
	                  || settle_.update(frame.images, clock_.elapsed());
	const bool pixel_placements = mode_ >= Capabilities::Sixel && settled
	                           && !placeholders;
	// Not when placeholders are carrying the images: a mosaic composed over
	// the placeholder cells would overwrite the very text that displays them.
	if (!pixel_placements && !placeholders)           // fallback tier: colour
		for (const CellImage &ci : frame.images)     // half-blocks (section 17.3)
			// The terminal's own background where it answered for it, rather
			// than the dark grey this guessed for its whole life. A light
			// terminal haloed every translucent edge, and the value was
			// always askable -- OSC 11 is in the startup query.
			compose_halfblocks(composed, ci.pixmap.toImage(), ci.cell_rect,
			                   caps_.bg_known
			                       ? qRgb(caps_.bg[0], caps_.bg[1], caps_.bg[2])
			                       : qRgb(16, 20, 24));

	// The transmission goes out ahead of the frame: the virtual placement has
	// to exist before the cells that reference it are printed.
	//
	// Inside the synchronised bracket when the terminal has it: the frame is
	// a cursor home followed by every cell, and a terminal painting halfway
	// through that shows a torn one. Begun before the uploads so an image and
	// the text referencing it land together.
	QByteArray out;
	if (sync_frames()) out += "\033[?2026h";
	out += uploads + "\033[H";
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
	if (placeholders || (pixel_placements && handles)) retire_uploads(frame, out);
	// Closed after the images, not after the text: on a tier that paints
	// pixels the picture is part of the frame, and ending the bracket before
	// it would leave exactly the tear this exists to prevent -- the text
	// updated and the image arriving separately.
	if (sync_frames()) out += "\033[?2026l";
	fwrite(out.constData(), 1, out.size(), stdout);
	fflush(stdout);
}

// Free the terminal's copy of a picture nothing is showing any more.
//
// kitty_delete_all() uses d=a, which drops PLACEMENTS and leaves the image
// data behind -- correct, and the reason an unchanged picture is re-placed
// rather than re-uploaded. But nothing ever freed the data, so a surface
// that animates uploaded one image per distinct frame and the terminal kept
// every one of them, in another process, for the life of the session. Our
// own key set grew with it.
//
// It went unseen because it was unreachable: until the frame loop compared
// placements by content, a picture that changed under unchanged cells was
// never presented at all, so the upload path ran once per surface and the
// leak had nothing to leak.
//
// A cap rather than freeing everything unreferenced this frame. A spinner
// alternating between a handful of pictures would otherwise delete and
// re-encode each of them on every frame, paying a full PNG for one it is
// about to want again -- and animation is exactly the case that reaches
// here at all.
void AnsiBackend::retire_uploads(const CellBuffer &frame, QByteArray &out) {
	static const int upload_cap = 16;
	QSet<quint64> live;
	for (const CellImage &ci : frame.images) {
		live.insert(ci.key);
		upload_order_.removeAll(ci.key);         // most recent last
		upload_order_.append(ci.key);
	}
	for (int i = 0; upload_order_.size() > upload_cap && i < upload_order_.size(); ) {
		const quint64 key = upload_order_.at(i);
		if (live.contains(key)) { ++i; continue; }
		// d=I, not d=i: uppercase frees the image data, which is the whole
		// point. The lowercase form deletes placements and leaves exactly
		// what this exists to release.
		out += "\033_Ga=d,d=I,q=2,i="
		     + QByteArray::number(quint32(key & 0xFFFFFF) + 1) + ";\033\\";
		uploaded_.remove(key);
		upload_order_.removeAt(i);
	}
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
                          QByteArray &inter, char &final) const {
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
	if (value >= 0) params.append(value);

	// Intermediate bytes, 0x20 to 0x2F, which ECMA-48 puts between the
	// parameters and the final. Without this the parser waited for a final it
	// would never see: a DECRPM reply is ESC [ ? 1006 ; 1 $ y, and "$" is an
	// intermediate. Measured -- one such reply wedged the decoder, and every
	// key after it was stuck behind the sequence that never ended. A terminal
	// may send one unsolicited, so this was reachable before anything here
	// asked a mode question.
	inter.clear();
	while (i < pending_.size() && pending_[i] >= 0x20 && pending_[i] <= 0x2f)
		inter.append(pending_[i++]);

	if (i >= pending_.size()) return -1;          // still arriving
	final = pending_[i];
	if (final < 0x40 || final > 0x7e) return -1;  // not a terminator: wait
	return i + 1;
}

// OSC, DCS, APC, PM and SOS share one shape: ESC <opener> ... terminator,
// where the terminator is ST (ESC backslash) or, for OSC, a bare BEL.
//
// Before this the decoder had no branch for any of them, so every byte of a
// reply arrived as a keystroke: measured, one OSC 11 background reply became
// 23 fake keys, an XTGETTCAP reply 14 and a kitty reply 10. That is why qtty
// could not ask the terminal anything -- the answer would have been typed
// into the application -- and it is the same defect either way round, because
// a terminal may send these unsolicited.
//
// Bounded. A stream that opens a string sequence and never closes it must not
// grow pending_ without limit, so past the cap the opener is dropped and the
// bytes after it are read as ordinary input.
int AnsiBackend::parse_string_sequence() const {
	const int cap = 4096;
	for (int i = 2; i < pending_.size(); ++i) {
		const unsigned char c = pending_[i];
		if (c == 0x07 && pending_[1] == ']') return i + 1;         // OSC, BEL
		if (c == 0x1b && i + 1 < pending_.size() && pending_[i + 1] == '\\')
			return i + 2;                                          // ST
		if (c == 0x1b) return i;      // a new escape: the old one was abandoned
	}
	return pending_.size() > cap ? 0 : -1;        // 0 asks the caller to drop
}

// A parameter list back to its wire form, so a reply parsed here can be
// handed to the one parser that knows what the fields mean.
static QByteArray params_to_bytes(const QVector<int> &params) {
	QByteArray out;
	for (int i = 0; i < params.size(); ++i) {
		if (i) out += ';';
		out += QByteArray::number(params[i]);
	}
	return out;
}

bool AnsiBackend::dispatch_csi(const QByteArray &prefix,
                              const QVector<int> &params,
                              const QByteArray &inter, char final) {
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
		// Bits 4, 8 and 16, which were parsed by nothing. A shift-click and a
		// control-click arrived as plain clicks, so an item view could not be
		// extend-selected or toggle-selected from a terminal at all.
		m.shift = (b & 4) != 0;
		m.alt   = (b & 8) != 0;
		m.ctrl  = (b & 16) != 0;
		if (b & 64) {
			// Wheel. It reports as a press with no matching release, so it is
			// neither a press nor a release here -- delivering it as a press
			// would leave a button stuck down for the rest of the session.
			// Bit 1 picks the axis: 64/65 are the vertical wheel and 66/67
			// the horizontal one. Reading bit 0 alone made a sideways scroll
			// a vertical one, and in the wrong direction half the time.
			if (b & 2) m.wheel_x = (b & 1) ? -1 : 1;
			else       m.wheel   = (b & 1) ? -1 : 1;
		} else {
			// 1 left, 2 middle, 3 right, 4..7 the extended buttons, 0 none.
			//
			// Bit 128 is what marks buttons 8..11, and masking it off was not
			// a fallback but an impersonation: a back-button click arrived as
			// 128 & 3 == 0, so it WAS a left press, forward WAS a middle
			// press, and button 10 WAS a right press -- which fires a context
			// menu. The router's "anything unrecognised is left" never saw
			// them, because the collapse happened here, one layer earlier.
			//
			// Low bits 3 is the protocol's "no button", which arrives on a
			// bare motion report. It is 0 rather than a button number, so the
			// router can say Qt::NoButton instead of guessing one.
			if (b & 128)          m.button = 4 + (b & 3);
			else if ((b & 3) == 3) m.button = 0;
			else                   m.button = (b & 3) + 1;
			m.press = (final == 'M') && !m.motion;
			m.release = (final == 'm');
		}
		sink_->on_mouse(m);
		return true;
	}

	// Window operations, ESC [ <what> ; height ; width t. 4 is the text area
	// in pixels and 6 is one cell; 8 is a resize the terminal is reporting
	// rather than one SIGWINCH announced, which some multiplexers send and
	// which arrives HERE rather than as a signal -- the same channel as the
	// keys, which is the whole reason this function has to know about it.
	if (final == 't') {
		const int what = param(0, 0);
		if (what == 8) {
			const int rows = param(1, 0), cols = param(2, 0);
			if (rows > 0 && cols > 0) {
				cells_ = QSize(cols, rows);
				sink_->on_resize(cells_);
			}
			return true;
		}
		// Rebuilt rather than passed through, because scan_caps() parses
		// bytes and this arrived as parsed parameters. One parser, one set of
		// rules about which field is height.
		if (what == 4 || what == 6) {
			scan_caps(QStringLiteral("\033[%1;%2;%3t").arg(what)
			              .arg(param(1, 0)).arg(param(2, 0)).toLatin1(), caps_);
			return true;
		}
		return true;
	}
	// A DECRPM reply, ESC [ ? <mode> ; <value> $ y. Rebuilt and handed to the
	// one parser, as the window operations are: the rules about what value 0
	// means live there and should not be restated here.
	//
	// It reaches this function at all only because parse_csi() learned about
	// intermediate bytes. Before that the "$" was not a final and the whole
	// sequence never terminated, taking every key behind it with it.
	if (final == 'y' && inter == QByteArrayLiteral("$")) {
		scan_caps(QByteArrayLiteral("\033[?") + params_to_bytes(params)
		              + QByteArrayLiteral("$y"), caps_);
		return true;
	}
	// Device attributes. The reply to our own fence, and unsolicited from
	// some terminals at startup; either way it is not a key.
	if (final == 'c') {
		scan_caps(QByteArrayLiteral("\033[") + prefix + params_to_bytes(params)
		              + QByteArrayLiteral("c"), caps_);
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
			QByteArray prefix, inter; QVector<int> params; char final = 0;
			const int n = parse_csi(prefix, params, inter, final);
			if (n < 0) return false;              // wait for the rest
			if (final == '~' && !params.isEmpty() && params[0] == 201) {
				pending_.remove(0, n);
				return dispatch_csi(prefix, params, inter, final);
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
			QByteArray prefix, inter; QVector<int> params; char final = 0;
			const int n = parse_csi(prefix, params, inter, final);
			if (n < 0) return false;              // still arriving
			pending_.remove(0, n);
			return dispatch_csi(prefix, params, inter, final);
		}
		// A string sequence -- OSC, DCS, APC, PM, SOS. Consumed and offered
		// to the capability parser, never turned into keys. The terminal's
		// answers to caps_query() come back here, and so does anything it
		// reports unasked.
		if (strchr("]P_^X", pending_[1])) {
			const int n = parse_string_sequence();
			if (n < 0) return false;              // still arriving
			if (n == 0) { pending_.remove(0, 1); return true; }   // over the cap
			scan_caps(pending_.left(n), caps_);
			pending_.remove(0, n);
			return true;
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
