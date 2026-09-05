#include "ansi_backend.h"
#include <cstdlib>
#include "../../runtime/terminal_owner.h"
#include "qtty/graphics.h"
#include "qtty/grid.h"
#include "qtty/theme.h"
#include <QSocketNotifier>
#include <QImage>
#include <QCoreApplication>
#include <unistd.h>
#include <sys/ioctl.h>
#include <csignal>
#include "qtty/application.h"
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
	//
	// The window is overridable, and it was not. `qtty-negotiate` reads
	// QTTY_PROBE_MS and, when the fence goes unanswered, PRINTS advice to
	// raise it -- advice nobody could follow for the library, because this
	// call ignored the variable and the tool was the only reader. A knob
	// named in a diagnostic that the diagnosed code does not read is worse
	// than no knob: it sends the next person to change something that
	// cannot affect the result. The bounds are the tool's, so the two agree
	// about what the variable means.
	//
	// 100 ms is kept as the default. It is not a latency budget -- a settled
	// terminal answers the fence at 5 ms, measured -- it is a readiness one,
	// and raising it costs every startup where nothing is going to answer.
	if (raw_ok_ && tty_out_) {
		int wait_ms = 100;
		if (const char *env = ::getenv("QTTY_PROBE_MS")) {
			const int v = ::atoi(env);
			if (v > 0 && v <= 60000) wait_ms = v;
		}
		caps_ = collect_caps(0, 1, wait_ms, nullptr, &pending_);
		// Inside a multiplexer, ask again when the first attempt was met
		// with silence. Measured in kitty: three probes in one process
		// inside tmux answer 0, 1, 1 -- the first gets nothing at all and
		// the next gets everything, immediately. It is READINESS rather
		// than latency, which is why a 1000 ms window changed nothing and
		// a second 100 ms one changes everything, and it is the same shape
		// this project already recorded for kitty under Xvfb.
		//
		// The consequence was not cosmetic. The backend probes once, at
		// construction, so inside tmux it always took the failing attempt:
		// caps.kitty stayed false, use_placeholders() therefore returned
		// false, and the unicode-placeholder mode -- which exists ONLY for
		// the multiplexer case -- could never engage anywhere. A whole
		// subsystem was unreachable in the one place it is for.
		//
		// Bounded and targeted: only inside tmux, only when nothing at all
		// answered, and only once. Outside tmux nothing changes, and where
		// no terminal is ever going to answer the cost is one more window.
		if (!caps_.answered && inside_tmux()) {
			QByteArray more;
			const TermCaps again =
			    collect_caps(0, 1, wait_ms, nullptr, &more);
			pending_ += more;
			// Only when it learned something. `collect_caps()` builds a
			// fresh TermCaps, so assigning it unconditionally throws away
			// whatever the first attempt DID gather -- and the first
			// attempt is not necessarily empty just because the fence went
			// unanswered. A terminal that answers CSI 16t promptly and
			// DA1 slowly gives exactly that shape, and the cell size it
			// reported would have been discarded by the retry meant to
			// improve on it. Found by reading the diff rather than by a
			// failing test, which is the instrument the rest of this
			// change did not use.
			if (again.answered) caps_ = again;
		}
	}

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
	//
	// And gated on tty_out_, which is the half this got wrong. resume()
	// writes the enabling sequence only when stdout is a terminal, so with
	// stdout redirected -- `app > out.txt`, typed at a shell -- the modes are
	// never requested and nothing can report a press or bracket a paste.
	// Keying them to raw mode alone claimed both anyway, for the same reason
	// sync_frames() below is written with tty_out_ in front of it: this
	// function and the one that writes the sequence must not be able to
	// disagree.
	c.mouse = tty_out_ && mode_usable(caps_, 1006, raw_ok_);
	c.bracketed_paste = tty_out_ && mode_usable(caps_, 2004, raw_ok_);
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
	// Drained first, then refused. This is where the whole defect lives: the
	// handler stays installed across a suspend (see the note beside
	// g_prev_tstp), so a resize while suspended reached query_geometry() and
	// put \033[14t\033[16t into a terminal an editor had just been handed,
	// whose reply then arrives at the editor as keystrokes. Measured at 10
	// bytes. It also closes the window a restored handler could not: a byte
	// written by a resize that arrived just BEFORE suspend() is already in
	// the pipe.
	if (!active_) return;
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

// The two sequences, named once. Each is written from two places -- resume()
// and suspend() for the ordinary path, and enter_terminal()/leave_terminal()
// below for the signal handlers, which cannot call into Qt. Naming them once
// is the point: a second copy of a rule arrived at by measurement is the kind
// that drifts.
//
// This comment said "three places for the enter and three for the leave"
// until 2026-09-03, when they were counted. Two each.
const char kEnter[] = "\033[?1049h\033[?25l\033[?1006h\033[?1002h"
                      "\033[?2004h\033[?1004h";
const char kLeave[] = "\033[?1004l\033[?2004l\033[?1002l\033[?1006l"
                      "\033[0m\033[?1049l\033[?25h";

struct Restore {
	struct termios saved {};      // as the terminal was before qtty
	struct termios raw_mode {};   // as qtty wants it, to put back on SIGCONT
	volatile sig_atomic_t armed = 0;
	int raw = 0, tty = 0;
};
Restore g_restore;

// The signals that end a process without unwinding. SIGWINCH and SIGPIPE are
// not here: they have handlers of their own and do not terminate.
const int g_fatal[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT,
                        SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
struct sigaction g_prev[sizeof(g_fatal) / sizeof(g_fatal[0])];
struct sigaction g_prev_tstp, g_prev_cont, g_prev_winch;
bool g_winch_saved = false;
// SIGWINCH is in this group now, and getting it here took two tries.
//
// suspend() says the handlers go with the terminal -- "a program that suspends
// to shell out has a terminal it did not take over" -- and SIGWINCH was
// installed once, with nullptr for the old action, so the host application's
// own handler was taken permanently. Restoring it on suspend() is the obvious
// fix and it reddened an existing check, because the flag it was guarded by
// answers "did anybody install" and the question is "is anybody still using
// it": PROCESS-WIDE state released on a PER-INSTANCE event, so a second
// backend going out of scope took the handler from the first, which was still
// active and still owned the terminal.
//
// A count answers the real question. resume() takes a reference and installs
// on the first, suspend() drops one and restores on the last -- both guarded
// by `active_`, so a repeated resume or suspend on one backend moves nothing.
// The fatal handlers and the job-control pair were under the same flag and
// had the same latent fault; they share the count, so the fix is one
// mechanism rather than three.
//
// What made it look like a design decision the first time is that "an
// ownership model" is a bigger phrase than the thing needed. No policy
// changes here: the documented behaviour is already "the handlers go with the
// terminal", and the count is what makes that sentence true when more than
// one backend exists.
// How many backends are active. The handlers are installed on the first and
// restored on the last.
//
// Kept with the pid that counted them, because fork() copies the number and
// not the terminal. A child that takes a backend of its own is the FIRST one
// in its process however many its parent holds, and without this it inherits
// a non-zero count, installs nothing and arms nothing -- so a crash in the
// child leaves the child's terminal broken. Found by the suite, which forks
// onto a pty to watch a process die; the fix for the nested case had made
// every forked child look nested.
int g_owners = 0;
pid_t g_owner_pid = 0;

// Give the terminal back. Does NOT disarm: SIGTSTP hands it back and SIGCONT
// takes it again, any number of times, and only the fatal path is once.
void leave_terminal() {
	if (!g_restore.armed) return;
	if (g_restore.tty) {
		const ssize_t n = ::write(STDOUT_FILENO, kLeave, sizeof(kLeave) - 1);
		(void)n;                         // nothing useful to do in a handler
	}
	if (g_restore.raw) tcsetattr(0, TCSANOW, &g_restore.saved);
}

// And take it back, after a stop. The mirror of the above, in the mirror
// order: the terminal's modes before the screen, because the screen is what
// the user sees change.
void enter_terminal() {
	if (!g_restore.armed) return;
	if (g_restore.raw) tcsetattr(0, TCSANOW, &g_restore.raw_mode);
	if (g_restore.tty) {
		const ssize_t n = ::write(STDOUT_FILENO, kEnter, sizeof(kEnter) - 1);
		(void)n;
	}
}

extern "C" void qtty_fatal_handler(int sig) {
	leave_terminal();
	g_restore.armed = 0;                 // once, however many signals arrive
	// Back to the default and re-raise, so the exit status, the core dump and
	// whatever the shell reports are what they would have been. Restoring the
	// terminal is the only thing this adds; it does not swallow the failure.
	signal(sig, SIG_DFL);
	raise(sig);
}

// Ctrl+Z is a key now that ISIG is cleared, but `kill -TSTP` still arrives --
// and backend.h has named this case from the start: suspend() is documented
// as being for "SIGTSTP / shelling out". Nothing implemented it, so a stopped
// program left its shell looking at the alternate screen, in raw mode, with
// the cursor hidden and mouse reporting on. The user's next keystroke went
// nowhere visible.
extern "C" void qtty_stop_handler(int sig) {
	leave_terminal();
	// Stop for real, with the default action, then put the handler back so
	// the next Ctrl+Z or kill behaves the same. Execution resumes here when
	// SIGCONT arrives.
	//
	// The signal is UNBLOCKED around the raise, and that is the whole of the
	// difference between this and a livelock. A handler runs with its own
	// signal blocked, so raise() only makes it PENDING: it is delivered when
	// the handler returns -- by which time the line below has put this handler
	// back, so it arrives here again, and again. Measured on a pseudo-terminal
	// before the fix: the child never returned from raise(), and wrote the
	// leave sequence four hundred times until the test killed it.
	//
	// Unblocked, the raise stops the process HERE. Execution resumes on the
	// continue, after which the mask and the handler are restored -- in that
	// order, so nothing arrives between them.
	//
	// The loop was blamed on an orphaned process group before it was measured,
	// on the theory that the stop was being discarded. It was not: with this
	// fix the same child stops and its parent sees it stop. The livelock was
	// the whole cause, and the environment was innocent -- which is worth the
	// line, because the wrong explanation was the more comfortable one.
	struct sigaction dfl {}, prev_action {};
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	sigaction(sig, &dfl, &prev_action);
	sigset_t just_this, prev_mask;
	sigemptyset(&just_this);
	sigaddset(&just_this, sig);
	sigprocmask(SIG_UNBLOCK, &just_this, &prev_mask);
	raise(sig);
	sigprocmask(SIG_SETMASK, &prev_mask, nullptr);
	sigaction(sig, &prev_action, nullptr);
}

// And take the terminal back when the job is resumed. The repaint is not done
// here -- a handler cannot compose a frame -- it is asked for through the
// SIGWINCH pipe, which already exists and already means "look at the terminal
// again". That is not a trick: a terminal genuinely may have been resized
// while the program was stopped, so re-measuring is the correct thing to do
// as well as the convenient one.
extern "C" void qtty_cont_handler(int) {
	enter_terminal();
	qtty_winch_handler(SIGWINCH);
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
		// ISIG and IXON go too, and leaving them on was not a small thing.
		// With ISIG set the terminal DRIVER turns Ctrl+C into SIGINT before
		// any byte reaches read_input(), so InputRouter's quit keys -- which
		// default to Ctrl+C, and which set_quit_keys() exists to change --
		// could never see that chord from a real keyboard, and neither could
		// the rule that makes Ctrl+C copy inside a text field. Two mechanisms
		// for one key, and the one that ran was not the one the code reasons
		// about.
		//
		// IXON is the same shape with a worse symptom: Ctrl+S is flow
		// control, so a user who types it sees the application stop
		// responding with no way to know why, and Ctrl+Q is spent unfreezing
		// it rather than reaching the application.
		//
		// A full-screen program owns its keyboard; that is what taking the
		// alternate screen means. The cost is that Ctrl+C no longer kills a
		// program whose event loop has stopped -- a kill from another window
		// still does, and so does the quit key once the loop is running.
		//
		// The translation flags are deliberately left alone. ICRNL and the
		// rest decide what byte Enter arrives as, and the decoder was written
		// against what they do now; changing them is a separate measurement.
		t.c_lflag &= ~(ICANON | ECHO | ISIG);
		t.c_iflag &= ~IXON;
		t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
		g_restore.raw_mode = t;
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
		fputs(kEnter, stdout);
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
		}
	}
	if (s_winch_pipe[0] >= 0 && !winch_notifier_) {
		winch_notifier_ = new QSocketNotifier(s_winch_pipe[0],
		                                     QSocketNotifier::Read, this);
		QObject::connect(winch_notifier_, &QSocketNotifier::activated,
		                 this, [this] { read_winch(); });
	}
	if (g_owner_pid != ::getpid()) {   // a fork: the parent's count is not ours
		g_owners = 0;
		g_winch_saved = false;
		g_owner_pid = ::getpid();
	}
	if (g_owners++ == 0) {
		// Armed only once the state it would restore is known, and only by
		// the FIRST backend, which is the one holding the terminal as the
		// user left it. A nested resume would re-arm with what the outer
		// backend left behind -- raw mode, alternate screen -- so a crash
		// would "restore" the terminal to the state it needed rescuing
		// from. Same fault as the handler release below and found beside
		// it: the arm and the disarm were both per-instance where the
		// thing they guard is process-wide.
		g_restore.saved = saved_;
		g_restore.raw = raw_ok_ ? 1 : 0;
		g_restore.tty = tty_out_ ? 1 : 0;
		g_restore.armed = 1;

		// SIGWINCH with the rest, now that there is something that knows
		// when the last one leaves. The pipe it writes to is process-wide
		// and made once; the HANDLER belongs to the span in which a backend
		// owns the terminal.
		if (s_winch_pipe[0] >= 0) {
			struct sigaction wa{};
			wa.sa_handler = qtty_winch_handler;
			sigemptyset(&wa.sa_mask);
			wa.sa_flags = SA_RESTART;   // do not break the read() in read_input()
			sigaction(SIGWINCH, &wa, &g_prev_winch);
			g_winch_saved = true;
		}

		struct sigaction sa {};
		sa.sa_handler = qtty_fatal_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESETHAND;    // belt as well as braces on re-entry
		for (size_t i = 0; i < sizeof(g_fatal) / sizeof(g_fatal[0]); ++i)
			sigaction(g_fatal[i], &sa, &g_prev[i]);
		// Job control, which does not end the process and so is not in that
		// list: hand the terminal back on a stop and take it again on a
		// continue. SA_RESTART on both, so a read() in read_input() is not
		// broken by being suspended and resumed.
		struct sigaction job {};
		sigemptyset(&job.sa_mask);
		job.sa_flags = SA_RESTART;
		job.sa_handler = qtty_stop_handler;
		sigaction(SIGTSTP, &job, &g_prev_tstp);
		job.sa_handler = qtty_cont_handler;
		sigaction(SIGCONT, &job, &g_prev_cont);
	}
	active_ = true;
	// From here the screen is this backend's, which the fatal-message path
	// needs to know: it gives the terminal back before printing, and until
	// this line only exec() knew, so an application driving its own frame
	// loop printed onto a frame about to be torn down (terminal_owner.h).
	take_terminal(this);
}

void AnsiBackend::suspend() {
	if (!active_) return;
	// The reporting modes go off before the alternate screen does, and they
	// matter more than the screen restore: a terminal left in mouse mode
	// writes an escape burst into the user's shell on every click, for the
	// rest of that shell's life. Reset in the reverse order they were set.
	if (tty_out_) {
		fputs(kLeave, stdout);
		fflush(stdout);
	}
	if (raw_ok_) tcsetattr(0, TCSANOW, &saved_);
	// The handlers go with it: a program that suspends to shell out has a
	// terminal it did not take over, and a crash in the shell is not this
	// library's to tidy after.
	if (g_owners > 0 && --g_owners == 0) {
		// Disarmed with the handlers rather than before them. An inner
		// backend going out of scope used to disarm the emergency restore
		// while the outer one was still drawing, so a kill after that left
		// the terminal raw and on the alternate screen with nothing to put
		// it back -- exactly the damage this mechanism exists to prevent,
		// removed by the object that was not using it.
		g_restore.armed = 0;
		for (size_t i = 0; i < sizeof(g_fatal) / sizeof(g_fatal[0]); ++i)
			sigaction(g_fatal[i], &g_prev[i], nullptr);
		sigaction(SIGTSTP, &g_prev_tstp, nullptr);
		sigaction(SIGCONT, &g_prev_cont, nullptr);
		if (g_winch_saved) {
			sigaction(SIGWINCH, &g_prev_winch, nullptr);
			g_winch_saved = false;
		}
	}

	active_ = false;
	release_terminal(this);          // and the outer one, if any, has it again
	// The terminal is the user's again, so anything held back can be said.
	flush_deferred_messages();
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

void AnsiBackend::present(const CellBuffer &frame, const QRegion &damage) {
	// Damage-limited where the caller says what changed, whole-frame where it
	// does not. An EMPTY region means "everything", which is what every
	// existing caller means by it: the compositor passes one when only an
	// image moved, and the suite writes present(f, QRegion()) throughout.
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
	// Asked ONCE and held, because the two halves of a bracket must not be
	// able to disagree. They cannot today -- caps_ is written in exactly one
	// place, the constructor -- but backend.h says of the cell size that the
	// terminal "may answer LATER: the reply arrives on stdin like everything
	// else", and the day caps_ learns to take a late reply, a bracket opened
	// under one answer and closed under another leaves 2026 SET. The symptom
	// of that is a terminal that stops repainting, which is the worst failure
	// this file has available and would be nobody's first hypothesis.
	const bool sync = sync_frames();
	if (sync) out += "\033[?2026h";
	out += uploads;
	Sgr cur;
	const auto emit_run = [&](int y, int from, int to) {
		for (int x = from; x < to; ++x) {
			const Cell &c = composed.at(x, y);
			if (c.width == 0) continue;              // continuation of wide cell
			emit_sgr(out, c, cur, depth_);
			out += c.ch.toUtf8();
		}
	};
	if (damage.isEmpty()) {
		out += "\033[H";
		for (int y = 0; y < composed.rows(); ++y) {
			emit_run(y, 0, composed.cols());
			if (y < composed.rows() - 1) out += "\033[0m\r\n", cur = Sgr{};
		}
	} else {
		// One addressed run per damaged row. `cur` is reset before each,
		// because a cursor jump breaks the SGR run: carrying the state across
		// one would colour a cell by whatever happened to precede it
		// somewhere else on the screen.
		for (const QRect &r : damage) {
			const int y0 = qMax(0, r.top());
			const int y1 = qMin(composed.rows() - 1, r.bottom());
			const int to = qMin(composed.cols(), r.right() + 1);
			for (int y = y0; y <= y1; ++y) {
				// Back up to the start of a wide cluster the rect cuts in
				// half. Its continuation cell carries no glyph, so a run
				// beginning there would write nothing and leave the stale
				// character on screen -- the one way a smaller write can be
				// WRONG rather than merely partial.
				int from = qMax(0, r.left());
				while (from > 0 && composed.at(from, y).width == 0) --from;
				out += "\033[" + QByteArray::number(y + 1) + ";"
				     + QByteArray::number(from + 1) + "H";
				cur = Sgr{};
				emit_run(y, from, to);
				out += "\033[0m";
				cur = Sgr{};
			}
		}
	}
	// And after the last row, which had no terminator to carry one. Measured
	// on a frame whose last CELL was coloured: the bytes ended
	// "...[91m[44m[1mzzzz" and the terminal kept bright red on blue, bold,
	// for whatever was written next. Every other row was already left in a
	// defined state and only the last was not, which makes this an
	// inconsistency rather than a decision.
	//
	// Four bytes a frame for the guarantee that a frame ends where it
	// started. The next frame does re-emit from its first cell -- `cur` is
	// fresh per present() -- so the cost of NOT doing it falls on anything
	// written BETWEEN frames: a deferred diagnostic, an application's own
	// stray output, an image sequence emitted after the cell loop.
	out += "\033[0m";
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
				// In the terminal's pixels, both of them. What is stored is
				// what the source rectangle indexes, so they convert together
				// or the crop selects the wrong part.
				const QRect src = for_terminal(cp.source);
				if (!uploaded_.contains(ci.key)) {
					uploaded_.insert(ci.key);
					out += encode_kitty_image(id, for_terminal(img));
					if (!whole) out += kitty_place(id, 0, src);
				} else {
					out += kitty_place(id, 0, whole ? QRect() : src);
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
				out += encode_sixel(for_terminal(img.copy(cp.source)));
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
	if (sync) out += "\033[?2026l";
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

// qtty's pixel units into the terminal's. GridMetrics measures the font qtty
// laid the widgets out in; the terminal answers CSI 16t with its own, and on
// kitty here those are 10x19 and 9x18. An image sized in the first lands on
// more cells than qtty thinks in the second, so every placement overlaps its
// neighbour -- measured on a screen capture, which is the only thing that
// could see it.
//
// Scaled at the point of transmission rather than by rasterising differently:
// the frame loop lays out in GridMetrics and every other consumer of that
// image expects those units. Only what goes on the wire has to speak the
// terminal's.
//
// A named method rather than a lambda inside present_pixels, because that is
// how the fix came to reach one of the two functions that transmit an image
// and not the other. present_overlay() went on sending at qtty's size for as
// long as the lambda was out of its reach: measured in one frame on screen,
// the base scaled to 36 px and the overlay over it did not, at 40. The ratio
// is written out rather than a cell count because an overlay is not
// necessarily a whole number of cells; for the crops that are, it is the
// same arithmetic.
// The same conversion for a rectangle INTO such an image. kitty's placement
// selects a source rectangle out of the stored picture, so scaling the
// picture without scaling the selection crops the wrong part of it -- the two
// have to move together or neither may move.
QRect AnsiBackend::for_terminal(const QRect &r) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	if (cw <= 0 || ch <= 0 || r.isNull()) return r;
	if (!caps_.cell_px.isValid() || caps_.cell_px.width() <= 0
	    || caps_.cell_px.height() <= 0 || caps_.cell_px == QSize(cw, ch))
		return r;
	const int tw = caps_.cell_px.width(), th = caps_.cell_px.height();
	return QRect(r.x() * tw / cw, r.y() * th / ch,
	             r.width() * tw / cw, r.height() * th / ch);
}

QImage AnsiBackend::for_terminal(const QImage &img) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	if (cw <= 0 || ch <= 0) return img;
	if (!caps_.cell_px.isValid() || caps_.cell_px.width() <= 0
	    || caps_.cell_px.height() <= 0 || caps_.cell_px == QSize(cw, ch))
		return img;
	return img.scaled(img.width()  * caps_.cell_px.width()  / cw,
	                  img.height() * caps_.cell_px.height() / ch,
	                  Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void AnsiBackend::present_pixels(const QImage &frame, const QRegion &damage) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	// A SUPERSET of the damage is always safe -- repainting more than
	// changed is slow, repainting less is wrong -- so the bounding rectangle
	// is taken rather than each rectangle in turn. An empty region means the
	// whole screen, as it does for present().
	// The TERMINAL's cell, not this build's font. GridMetrics measures the
	// font qtty laid the widgets out in; the terminal answers CSI 16t with
	// its own, and on kitty here those are 10x19 and 9x18. An image sized in
	// the first lands on more cells than qtty thinks in the second, so every
	// tile overlaps its neighbour -- measured on a screen capture, which is
	// the only thing that could see it.
	//
	// Scaled at the point of transmission rather than by rasterising
	// differently: the frame loop lays out in GridMetrics and every other
	// consumer of that image expects those units. Only what goes on the wire
	// has to speak the terminal's.
	const QRect all(0, 0, frame.width() / cw, frame.height() / ch);
	QRect cells = damage.isEmpty() ? all : damage.boundingRect().intersected(all);
	if (cells.isEmpty()) return;

	// Only the positional tiers crop. Sixel and iTerm2 paint at the cursor
	// and leave no handle, so a partial update is an address and a smaller
	// encode. Kitty deletes every placement and re-places the screen as one
	// image: skipping the delete would accumulate a placement per frame, and
	// a lifecycle for SCREEN images is a different piece of work -- section 7.4
	// scopes it. So kitty keeps the whole frame, and says so here rather
	// than looking like an oversight.
	const bool positional = mode_ == Capabilities::Sixel
	                     || mode_ == Capabilities::ITerm2;
	const QImage cropped = (positional && cells != all)
	    ? frame.copy(QRect(cells.x() * cw, cells.y() * ch,
	                       cells.width() * cw, cells.height() * ch))
	    : frame;
	// The size in CELLS, kept beside the converted pixels rather than
	// recovered from them. iTerm2 states an image's size in cells, and
	// dividing the CONVERTED width by this build's cell answers a different
	// question: on a terminal whose cell is 9 where the font is 10, a
	// four-cell crop came back as three. The placement path forty lines
	// above already passes cell counts and says why; this is the same rule,
	// and it was the one site that derived them instead.
	const QRect part_cells = (positional && cells != all) ? cells : all;
	const QImage part = for_terminal(cropped);

	QByteArray out;
	if (positional && cells != all)
		out = "\033[" + QByteArray::number(cells.y() + 1) + ";"
		    + QByteArray::number(cells.x() + 1) + "H";
	else
		out = "\033[H";
	switch (mode_) {
	case Capabilities::Kitty:
	case Capabilities::KittyAlpha: {
		// A fixed grid of tiles, each with its own image and placement id,
		// each replaced when the damage touches it. Replacing a placement
		// VACATES its old rectangle -- measured, two placements sharing ids
		// leave 0 pixels of the first -- so an id may only be reused where
		// the rectangle repeats, and a fixed grid is what makes it repeat.
		// The placement count is then bounded by the tile count rather than
		// by frames, and nothing is ever deleted.
		//
		// Four cells, measured rather than chosen: kitty answers a DA1 after
		// 50, 200, 750 and 3000 placements in 0.001, 0.074, 0.079 and 0.217
		// seconds, so the count is not the binding constraint at this scale
		// and the tile can be small enough that a one-cell edit is cheap. At
		// 200x60 that is 750 tiles and about 14 KB for a single changed
		// cell, against 284 KB for the screen.
		const int tile = 4;
		if (frame.size() != last_pixel_size_) {
			// The only case a tile cannot repair: the grid itself moved, so
			// every placement is at a position that no longer means
			// anything.
			out += kitty_delete_all();
			last_pixel_size_ = frame.size();
			cells = all;
		}
		const int across = (all.width() + tile - 1) / tile;
		for (const QRect &t : dirty_tiles(QRegion(cells), all.size(), tile)) {
			const quint32 idx = quint32(t.y() / tile * across + t.x() / tile);
			out += "\033[" + QByteArray::number(t.y() + 1) + ";"
			     + QByteArray::number(t.x() + 1) + "H";
			out += encode_kitty_tile(
			    0xFFF0000u + idx, 1 + idx,
			    for_terminal(frame.copy(QRect(t.x() * cw, t.y() * ch,
			                                  t.width() * cw,
			                                  t.height() * ch))));
		}
		break;
	}
	case Capabilities::Sixel:
		out += encode_sixel(part);
		break;
	case Capabilities::ITerm2:
		out += encode_iterm2(part, part_cells.width(), part_cells.height());
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
	out += encode_kitty_image(0xFFFFE00u + quint32(id), for_terminal(rgba),
	                          z > 0 ? z : 1);
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
			const QSize was = caps_.cell_px;
			scan_caps(QStringLiteral("\033[%1;%2;%3t").arg(what)
			              .arg(param(1, 0)).arg(param(2, 0)).toLatin1(), caps_);
			// Everything already uploaded was scaled by the OLD cell. The
			// upload cache is keyed on the source pixmap, which does not
			// move when the terminal's cell does, so a hit would re-place a
			// picture at the old scale -- and worse, the crop rectangle is
			// computed at the new one, so it would index the wrong pixels of
			// it. read_winch() asks for the geometry on every SIGWINCH
			// precisely because a font-size change moves this without moving
			// the cell COUNT, so this is the arrival it was asking for.
			if (caps_.cell_px.isValid() && caps_.cell_px != was) {
				uploaded_.clear();
				upload_order_.clear();
				last_pixel_size_ = QSize();
			}
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
