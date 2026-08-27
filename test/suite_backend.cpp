// suite_backend -- the AnsiBackend escape decoder (section 5.1).
//
// The decoder had no test at all, which is how it kept a three-byte fixed-width
// CSI reader long after the runtime grew sinks for mouse, paste, resize and
// focus. Those sinks were implemented and never called: a terminal resize did
// nothing, and mouse input could not arrive from a real terminal at any point.
//
// Driving it needs no tty. The backend decodes from a buffer, so a recording
// sink plus a way to push bytes in exercises the real code path -- which is
// also the corpus shape doc/beerssh.md section 4 proposes sharing.
#include <qtty/qtty.h>
#include "src/backend/ansi/ansi_backend.h"
#include "src/backend/ansi/term_caps.h"
#include <QtWidgets>
#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <fcntl.h>
#include <csignal>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

namespace {

struct Recorder : ITerminalEventSink {
	QVector<KeyEvent> keys;
	QVector<MouseEvent> mice;
	QStringList pastes;
	QVector<QSize> resizes;
	QVector<bool> focus;
	void on_key(const KeyEvent &k) override { keys.append(k); }
	void on_mouse(const MouseEvent &m) override { mice.append(m); }
	void on_paste(const QString &s) override { pastes.append(s); }
	void on_resize(QSize c) override { resizes.append(c); }
	void on_focus_change(bool f) override { focus.append(f); }
	void clear() { keys.clear(); mice.clear(); pastes.clear();
		           resizes.clear(); focus.clear(); }
};

// The backend reads from fd 0, so bytes are fed through a pipe made to be
// stdin. That drives read_input() and the decoder exactly as a terminal would,
// rather than reaching past them into a helper written for the test.
struct Feeder {
	int saved_stdin = -1, write_fd = -1;
	Feeder() {
		int fds[2];
		if (::pipe(fds) != 0) return;
		saved_stdin = ::dup(0);
		::dup2(fds[0], 0);
		::close(fds[0]);
		write_fd = fds[1];
	}
	~Feeder() {
		if (write_fd >= 0) ::close(write_fd);
		if (saved_stdin >= 0) { ::dup2(saved_stdin, 0); ::close(saved_stdin); }
	}
	void send(const QByteArray &bytes) const {
		const ssize_t n = ::write(write_fd, bytes.constData(), bytes.size());
		(void)n;
	}
};

} // namespace

int suite_backend() {
	fails = 0;
	Feeder feeder;
	if (feeder.write_fd < 0) {
		printf("FAIL: could not build the input pipe\n");
		return 1;
	}
	AnsiBackend backend;
	Recorder rec;
	backend.set_event_sink(&rec);

	// One helper, so each case says only what it sends and what it expects.
	const auto feed = [&](const QByteArray &bytes) {
		rec.clear();
		feeder.send(bytes);
		QCoreApplication::processEvents();
	};

	// -- keys, including the modifier parameter the old decoder could not see
	feed("\033[A");
	CHECK(rec.keys.size() == 1 && rec.keys[0].qt_key == Qt::Key_Up,
	      "CSI A decodes as Up");
	feed("\033[1;5C");
	CHECK(rec.keys.size() == 1 && rec.keys[0].qt_key == Qt::Key_Right
	      && rec.keys[0].ctrl, "CSI 1;5C decodes as Ctrl-Right");
	feed("\033[3~");
	CHECK(rec.keys.size() == 1 && rec.keys[0].qt_key == Qt::Key_Delete,
	      "CSI 3~ decodes as Delete");

	// -- SGR 1006 mouse. Unreachable before: the backend never enabled the
	//    mode and the decoder had no branch for it.
	feed("\033[<0;34;12M");
	CHECK(rec.mice.size() == 1 && rec.mice[0].press
	      && rec.mice[0].button == 1 && rec.mice[0].cell == QPoint(33, 11),
	      "SGR mouse press decodes with 0-based cell");
	feed("\033[<0;34;12m");
	CHECK(rec.mice.size() == 1 && rec.mice[0].release,
	      "SGR mouse release decodes");
	feed("\033[<64;5;5M");
	CHECK(rec.mice.size() == 1 && rec.mice[0].wheel == 1
	      && !rec.mice[0].press && !rec.mice[0].release,
	      "wheel is neither a press nor a release");
	feed("\033[<32;7;3M");
	CHECK(rec.mice.size() == 1 && rec.mice[0].motion, "drag decodes as motion");

	// -- bracketed paste. The point of the mode is that a newline inside the
	//    paste is text and not Return, so that is the case worth checking.
	feed("\033[200~hello\nworld\033[201~");
	CHECK(rec.pastes.size() == 1 && rec.pastes[0] == QStringLiteral("hello\nworld"),
	      "bracketed paste arrives whole, newline included");
	CHECK(rec.keys.isEmpty(), "no key events are fabricated from pasted text");

	// -- focus reporting
	feed("\033[I");
	CHECK(rec.focus.size() == 1 && rec.focus[0], "CSI I is focus in");
	feed("\033[O");
	CHECK(rec.focus.size() == 1 && !rec.focus[0], "CSI O is focus out");

	// -- a sequence split across reads must not be mis-decoded. The old
	//    decoder read a fixed three bytes and would have consumed half of
	//    this as an unknown key, desynchronising everything after it.
	rec.clear();
	feeder.send("\033[<0;10");
	QCoreApplication::processEvents();
	CHECK(rec.mice.isEmpty(), "a partial CSI produces nothing yet");
	feeder.send(";4M");
	QCoreApplication::processEvents();
	CHECK(rec.mice.size() == 1 && rec.mice[0].cell == QPoint(9, 3),
	      "the sequence completes on the next read");

	// -- UTF-8 input. The suite fed only ASCII before, which is exactly why
	//    a Latin-1 single-byte decode survived here: every character it was
	//    ever given was one byte, and one byte is where the two agree.
	feed(QString::fromUtf8("\u00e9").toUtf8());
	CHECK(rec.keys.size() == 1
	      && rec.keys[0].text == QString::fromUtf8("\u00e9"),
	      "a two-byte character is one key event, not two");
	feed(QString::fromUtf8("\u6f22").toUtf8());
	CHECK(rec.keys.size() == 1
	      && rec.keys[0].text == QString::fromUtf8("\u6f22"),
	      "a three-byte character is one key event, not three");
	feed(QString::fromUtf8("\U0001F389").toUtf8());
	CHECK(rec.keys.size() == 1
	      && rec.keys[0].text == QString::fromUtf8("\U0001F389"),
	      "a four-byte character is one key event carrying a surrogate pair");

	// Split across two reads, which a terminal does at any byte. The CSI
	// parser already had to handle this; so does a character.
	{
		const QByteArray utf8 = QString::fromUtf8("\u6f22").toUtf8();
		rec.clear();
		feeder.send(utf8.left(2));
		QCoreApplication::processEvents();
		CHECK(rec.keys.isEmpty(), "a partial UTF-8 sequence produces nothing yet");
		feeder.send(utf8.mid(2));
		QCoreApplication::processEvents();
		CHECK(rec.keys.size() == 1
		      && rec.keys[0].text == QString::fromUtf8("\u6f22"),
		      "the character completes on the next read");
	}

	// A stray continuation byte is not a character and must not be delivered
	// as one -- passing it on as Latin-1 is what the old decode did.
	feed(QByteArray(1, char(0xA9)));
	CHECK(rec.keys.isEmpty(), "a stray continuation byte is dropped, not delivered");

	// Alt with a non-ASCII key: the same sequence decode, one byte later.
	feed(QByteArray("\033") + QString::fromUtf8("\u00e9").toUtf8());
	CHECK(rec.keys.size() == 1 && rec.keys[0].alt
	      && rec.keys[0].text == QString::fromUtf8("\u00e9"),
	      "Alt with a two-byte character is one key event");
	feed(QByteArray("\033a"));
	CHECK(rec.keys.size() == 1 && rec.keys[0].alt
	      && rec.keys[0].text == QStringLiteral("a"),
	      "Alt with an ASCII character still works");

	// -- plain text still works, and is what most input is
	feed("hi");
	CHECK(rec.keys.size() == 2 && rec.keys[0].text == QStringLiteral("h"),
	      "plain characters still decode");

	// ------------------------------------------ section 5.7: what the terminal says
	// The reply parser, driven directly. It is separate from the I/O for
	// exactly this reason: a terminal that answers the graphics query and not
	// the colour one, or answers out of order, or splits a reply across two
	// reads, is a line of test here and a flaky experiment against a live
	// terminal otherwise.
	{
		const auto caps_of = [](const QByteArray &b) {
			TermCaps c;
			scan_caps(b, c);
			return c;
		};

		// -- kitty. OK means yes; an error status proves the protocol is
		//    there but is still refused, because a terminal that cannot take
		//    one 1x1 direct pixel is not one to send an image to.
		CHECK(caps_of("\033_Gi=31;OK\033\\").kitty, "kitty OK is accepted");
		CHECK(!caps_of("\033_Gi=31;ENOTSUP:nope\033\\").kitty,
		      "a kitty error status is not");
		CHECK(!caps_of("\033[<0;1;1M").kitty, "and no APC at all is not");

		// -- device attributes, and the trap: searching for the text "4"
		//    matches the 4 inside 14, the DEC national-replacement-character
		//    attribute, and reports sixel on a terminal that has none.
		CHECK(caps_of("\033[?62;4;22c").sixel, "DA attribute 4 means sixel");
		CHECK(!caps_of("\033[?62;14;22c").sixel,
		      "but the 4 inside 14 does not (parsed as a list, not searched)");
		CHECK(caps_of("\033[?62;14;22c").answered, "either way DA1 is the fence");
		CHECK(!caps_of("\033[?62;4;22").answered, "an unterminated DA1 is not");

		// -- window operations. Height first: xterm reports rows before
		//    columns throughout, and swapping them stretches every image by
		//    the cell aspect -- a wrong picture rather than a missing one.
		CHECK(caps_of("\033[6;19;10t").cell_px == QSize(10, 19),
		      "the cell report is height then width");
		CHECK(caps_of("\033[4;760;800t").text_px == QSize(800, 760),
		      "and so is the text-area report");
		CHECK(!caps_of("\033[6;0;10t").cell_px.isValid(),
		      "a zero dimension is refused rather than stored");

		// -- XTGETTCAP, asked instead of trusting $COLORTERM.
		CHECK(caps_of("\033P1+r524742=38\033\\").truecolor, "RGB in a tcap reply");
		CHECK(caps_of("\033P1+r5463\033\\").truecolor, "Tc in a tcap reply");
		CHECK(!caps_of("\033P0+r524742\033\\").truecolor,
		      "but a 0 reply is the terminal saying it has neither");
		CHECK(!caps_of("\033P1+r4photo=1\033\\").truecolor,
		      "and an unrelated capability is not truecolor");

		// -- OSC 11, where each component is scaled by ITS OWN width. Reading
		//    two digits per field turns white into near-black on every
		//    terminal that answers in the short form.
		const TermCaps white = caps_of("\033]11;rgb:f/f/f\033\\");
		CHECK(white.bg_known && white.bg[0] == 255 && white.bg[1] == 255
		      && white.bg[2] == 255, "rgb:f/f/f is white, not near-black");
		const TermCaps dark = caps_of("\033]11;rgb:2e2e/3434/4e4e\033\\");
		CHECK(dark.bg_known && dark.bg[0] == 0x2e && dark.bg[1] == 0x34
		      && dark.bg[2] == 0x4e, "and a four-digit reply scales to itself");
		CHECK(!caps_of("\033]11;rgb:\033\\").bg_known,
		      "a truncated colour reply sets nothing");

		// -- additive, which is what lets the collector rescan as bytes
		//    arrive. Replies routinely arrive split over ssh, and a scan that
		//    reset would report "no graphics" for an answer that was merely
		//    slow.
		TermCaps grown;
		scan_caps("\033_Gi=31;OK\033\\", grown);
		CHECK(grown.kitty && !grown.answered, "a partial buffer sets what it has");
		scan_caps("\033[?62;4;22c", grown);
		CHECK(grown.kitty && grown.sixel && grown.answered,
		      "and a later scan adds without clearing");

		// -- order does not matter, since every scanner searches the buffer.
		const TermCaps jumbled =
		    caps_of("\033[?62;4;22c\033]11;rgb:f/f/f\033\\\033_Gi=31;OK\033\\");
		CHECK(jumbled.kitty && jumbled.sixel && jumbled.bg_known,
		      "replies arriving in the wrong order are all read");

		// -- the fence, which is the whole reason the query ends with DA1.
		CHECK(!caps_complete("\033_Gi=31;OK\033\\"),
		      "kitty alone does not complete the reply");
		CHECK(caps_complete("\033[?62;4;22c"), "DA1 does");

		// -- the query itself must carry the fence last, or the collector
		//    stops reading before the answers it is waiting for arrive.
		const QByteArray q = caps_query();
		CHECK(q.endsWith("\033[c"), "the batched query ends with DA1");
		CHECK(q.contains("\033_G") && q.contains("+q524742")
		      && q.contains("\033]11;?") && q.contains("\033[16t"),
		      "and asks for kitty, direct colour, the background and the cell");
	}

	// -- and the same replies arriving on the LIVE input path, which is the
	//    half that matters: a terminal may answer at any moment, and a resize
	//    brings new pixel geometry down the same channel as the keys. Before
	//    this the decoder had no branch for OSC, DCS or APC at all, so each
	//    reply was typed into the application: measured, 23 fake keys for a
	//    background reply, 14 for XTGETTCAP and 10 for the kitty answer.
	feed("\033]11;rgb:2e2e/3434/4e4e\033\\");
	CHECK(rec.keys.isEmpty(), "an OSC reply is consumed, not typed");
	feed("\033P1+r524742=38\033\\");
	CHECK(rec.keys.isEmpty(), "nor is a DCS reply");
	feed("\033_Gi=31;OK\033\\");
	CHECK(rec.keys.isEmpty(), "nor a kitty APC reply");
	feed("\033]0;a title\007");
	CHECK(rec.keys.isEmpty(), "and an OSC closed by BEL is framed too");
	// A device-attributes reply arriving unasked, which some terminals send
	// at startup and a multiplexer can deliver late. It reaches the decoder
	// rather than the startup collector, and must be read as an answer rather
	// than typed. The tier is not renegotiated from it: a mode that changes
	// under a running frame is worse than one chosen once.
	feed("\033[?62;4;22c");
	CHECK(rec.keys.isEmpty(), "and an unsolicited DA1 reply is read, not typed");

	// Keys on either side of a reply still arrive: the sequence is consumed
	// exactly, not skipped past.
	feed("a\033]11;rgb:f/f/f\033\\b");
	CHECK(rec.keys.size() == 2 && rec.keys[0].text == QStringLiteral("a")
	      && rec.keys[1].text == QStringLiteral("b"),
	      "a reply between two keys consumes only itself");

	// Split across reads, which is what happens over ssh.
	rec.clear();
	feeder.send("\033]11;rgb:2e2e/");
	QCoreApplication::processEvents();
	feeder.send("3434/4e4e\033\\");
	QCoreApplication::processEvents();
	CHECK(rec.keys.isEmpty(), "a reply split across two reads is still not keys");

	// -- the resize report, which arrives on stdin rather than as a signal.
	//    Some multiplexers send it, and a terminal answers window operations
	//    the same way, so the decoder has to know a report from a key.
	feed("\033[8;24;80t");
	CHECK(rec.resizes.size() == 1 && rec.resizes[0] == QSize(80, 24),
	      "CSI 8 t is a resize report, columns from the second field");
	feed("\033[6;19;10t");
	CHECK(rec.keys.isEmpty() && rec.resizes.isEmpty(),
	      "and the cell-size report is neither a key nor a resize");

	// An unterminated string sequence must not swallow the session. The
	// property asserted is that input AFTER it still arrives -- which is what
	// the byte cap buys, and what an unbounded wait would cost. Kept last
	// because it deliberately floods, and drained explicitly rather than
	// trusting one processEvents() to move five kilobytes.
	rec.clear();
	feeder.send(QByteArray("\033]11;") + QByteArray(5000, 'x'));
	for (int i = 0; i < 200; ++i) QCoreApplication::processEvents();
	rec.clear();
	feeder.send("\033[A");
	for (int i = 0; i < 200; ++i) QCoreApplication::processEvents();
	CHECK(rec.keys.size() == 1 && rec.keys[0].qt_key == Qt::Key_Up,
	      "input after an unterminated string sequence still arrives");

	// -- the collector, driven over a socketpair. It takes descriptors rather
	//    than reaching for 0 and 1 precisely so this can exist: a real
	//    terminal would make every one of these a flaky experiment.
	{
		const auto ask = [](const QByteArray &answer, int timeout_ms, QByteArray *sent) {
			int fds[2];
			if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return TermCaps{};
			// The far end answers immediately; the collector polls until the
			// fence or the timeout, so a prompt answer exercises the early
			// return and an empty one exercises the timeout path.
			if (!answer.isEmpty()) {
				const ssize_t w = ::write(fds[1], answer.constData(), answer.size());
				(void)w;
			}
			const TermCaps c = collect_caps(fds[0], fds[0], timeout_ms);
			if (sent) {
				char buf[512];
				const ssize_t got = ::recv(fds[1], buf, sizeof(buf), MSG_DONTWAIT);
				if (got > 0) *sent = QByteArray(buf, int(got));
			}
			::close(fds[0]);
			::close(fds[1]);
			return c;
		};

		QByteArray asked;
		const TermCaps full = ask("\033_Gi=31;OK\033\\\033P1+r524742\033\\"
		                          "\033]11;rgb:f/f/f\033\\\033[6;19;10t"
		                          "\033[?62;4;22c", 500, &asked);
		CHECK(full.answered && full.kitty && full.sixel && full.truecolor
		      && full.bg_known && full.cell_px == QSize(10, 19),
		      "a terminal that answers everything is read completely");
		CHECK(asked.contains("\033[c"), "and it was asked before being read");

		// The fence is what ends the wait. Without it the collector must run
		// out its timeout rather than return early -- and must still keep
		// what did arrive, because a terminal that answered the graphics
		// query and nothing else is one we can draw on.
		const TermCaps partial = ask("\033_Gi=31;OK\033\\", 120, nullptr);
		CHECK(partial.kitty && !partial.answered,
		      "an answer without the fence still counts after the timeout");

		// Nothing at all: no capabilities, no hang.
		const TermCaps silent = ask(QByteArray(), 120, nullptr);
		CHECK(!silent.answered && !silent.kitty && !silent.sixel,
		      "a silent terminal yields nothing and returns");

		// A terminal that has gone away. This one found a real fault rather
		// than testing one: writing the query to a socket whose peer had
		// closed raised SIGPIPE and killed the whole suite with signal 13 and
		// no message -- which is what that failure always looks like, and
		// which would equally kill any qtty program whose output is a pipe
		// the reader has finished with. AnsiBackend ignores SIGPIPE now, so
		// the write fails and is reported instead of being fatal.
		int fds[2];
		if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) {
			::close(fds[1]);
			const TermCaps dead = collect_caps(fds[0], fds[0], 120);
			CHECK(!dead.answered, "a terminal that has gone yields nothing");
			::close(fds[0]);
		}
	}

	// -- negotiation. The rules, against capability sets built by hand:
	//    owning a terminal that answers each way is not a thing a suite can
	//    do, and that is exactly why the rules were env-sniffing before.
	{
		const QByteArray had_term = qgetenv("TERM");
		const QByteArray had_prog = qgetenv("TERM_PROGRAM");
		const QByteArray had_kwid = qgetenv("KITTY_WINDOW_ID");
		const QByteArray had_gfx = qgetenv("QTTY_GRAPHICS");
		const QByteArray had_ct = qgetenv("COLORTERM");
		qunsetenv("QTTY_GRAPHICS");
		qunsetenv("KITTY_WINDOW_ID");
		qunsetenv("TERM_PROGRAM");
		qunsetenv("COLORTERM");

		TermCaps none;                       // answered nothing at all
		TermCaps kitty;  kitty.answered = true;  kitty.kitty = true;
		TermCaps sixel;  sixel.answered = true;  sixel.sixel = true;
		TermCaps plain;  plain.answered = true;  // answered, and has neither

		qputenv("TERM", "xterm-kitty");
		CHECK(negotiate_graphics(kitty) == Capabilities::KittyAlpha,
		      "a terminal that answers the kitty query gets the kitty tier");

		// The discriminating case, and the reason any of this exists. $TERM
		// says kitty; the terminal answered the fence and did NOT answer the
		// graphics query, so kitty is a real no. Sniffing alone would emit
		// kitty escapes at a terminal that cannot read them.
		CHECK(negotiate_graphics(plain) == Capabilities::Halfblocks,
		      "but a measured no beats $TERM saying yes");

		// ...and the other direction is preserved rather than sacrificed: a
		// terminal that answered NOTHING has told us nothing, so the old
		// reading of $TERM still stands. Dropping to the floor here would
		// regress every terminal behind a multiplexer that eats the query.
		CHECK(negotiate_graphics(none) == Capabilities::KittyAlpha,
		      "a silent terminal still gets what $TERM claims");

		qputenv("TERM", "xterm-256color");
		CHECK(negotiate_graphics(sixel) == Capabilities::Sixel,
		      "device attribute 4 selects sixel whatever $TERM says");
		CHECK(negotiate_graphics(none) == Capabilities::Halfblocks,
		      "and an unremarkable $TERM with no answer is half-blocks");

		// Inside tmux the pixel tiers are refused however capable the outer
		// terminal is, because passthrough carries the image but not the
		// cursor: it would arrive in the wrong place. Half-blocks are text
		// and tmux moves them like any other text.
		const QByteArray had_tmux = qgetenv("TMUX");
		qputenv("TMUX", "/tmp/tmux-1000/default,1234,0");
		qputenv("TERM", "xterm-kitty");
		CHECK(inside_tmux(), "$TMUX is how tmux is known");
		CHECK(negotiate_graphics(kitty) == Capabilities::Halfblocks,
		      "and inside it even a proven kitty terminal gets half-blocks");
		qunsetenv("TMUX");
		qputenv("TERM", "screen-256color");
		CHECK(inside_tmux(), "$TERM saying screen is enough on its own");
		qputenv("TERM", "xterm-kitty");
		CHECK(!inside_tmux(), "and an ordinary $TERM with no $TMUX is not tmux");
		if (!had_tmux.isEmpty()) qputenv("TMUX", had_tmux);

		// The query is wrapped so the terminal UNDERNEATH answers. Unwrapped,
		// tmux answers it itself -- it is a terminal too, and it knows
		// nothing about what it is sitting in -- and the fence rule would
		// then believe that answer.
		const QByteArray wrapped = tmux_wrap("\033[c");
		CHECK(wrapped.startsWith("\033Ptmux;") && wrapped.endsWith("\033\\"),
		      "passthrough is a DCS tmux string");
		CHECK(wrapped.contains("\033\033["),
		      "and every ESC inside it is doubled");
		CHECK(tmux_wrap("plain") == QByteArray("\033Ptmux;plain\033\\"),
		      "a payload with no ESC is carried as it is");

		// The variant is chosen by $TERM because it cannot be asked, and both
		// answers draw -- so a wrong one costs appearance, not correctness.
		qputenv("TERM_PROGRAM", "WezTerm");
		CHECK(negotiate_graphics(kitty) == Capabilities::Kitty,
		      "wezterm gets the kitty tier without alpha over text");
		qunsetenv("TERM_PROGRAM");

		// An explicit override outranks the terminal itself: it is the
		// contract with a cooperating one, and the only way to test a tier
		// this machine's terminal does not have.
		qputenv("QTTY_GRAPHICS", "sixel");
		CHECK(negotiate_graphics(plain) == Capabilities::Sixel,
		      "QTTY_GRAPHICS wins over the measurement");
		qunsetenv("QTTY_GRAPHICS");

		// The colour override, which is the same contract as QTTY_GRAPHICS
		// and the only way to exercise a depth this terminal does not have.
		qputenv("QTTY_COLOR", "mono");
		CHECK(negotiate_color(TermCaps{}) == Capabilities::Mono, "QTTY_COLOR=mono");
		qputenv("QTTY_COLOR", "16");
		CHECK(negotiate_color(TermCaps{}) == Capabilities::Ansi16, "QTTY_COLOR=16");
		qputenv("QTTY_COLOR", "256");
		CHECK(negotiate_color(TermCaps{}) == Capabilities::Xterm256, "QTTY_COLOR=256");
		qputenv("QTTY_COLOR", "truecolor");
		TermCaps nope;
		CHECK(negotiate_color(nope) == Capabilities::TrueColor, "QTTY_COLOR=truecolor");
		qunsetenv("QTTY_COLOR");

		// $TERM read upward only, and downward only to refuse: an empty or
		// dumb TERM is the one weak signal allowed to turn colour OFF,
		// because being wrong that way costs colour rather than garbage.
		qputenv("TERM", "dumb");
		CHECK(negotiate_color(nope) == Capabilities::Mono, "TERM=dumb is monochrome");
		qputenv("TERM", "xterm-direct");
		CHECK(negotiate_color(nope) == Capabilities::TrueColor,
		      "a -direct terminfo entry is direct colour");
		qputenv("TERM", "vt220");
		CHECK(negotiate_color(nope) == Capabilities::Ansi16,
		      "and the documented floor is sixteen colours");
		qputenv("COLORTERM", "truecolor");
		CHECK(negotiate_color(nope) == Capabilities::TrueColor,
		      "COLORTERM may say yes even though it is inherited across ssh");
		qunsetenv("COLORTERM");

		// Colour: measured truecolor is a yes, and $COLORTERM stays a
		// yes-only signal because it survives an ssh to a machine whose
		// terminal is not the one that set it.
		TermCaps tc; tc.answered = true; tc.truecolor = true;
		qputenv("TERM", "xterm-256color");
		CHECK(negotiate_color(tc) == Capabilities::TrueColor,
		      "XTGETTCAP proves direct colour");
		CHECK(negotiate_color(plain) == Capabilities::Xterm256,
		      "and without it $TERM is still read");

		if (had_term.isEmpty()) qunsetenv("TERM"); else qputenv("TERM", had_term);
		if (!had_prog.isEmpty()) qputenv("TERM_PROGRAM", had_prog);
		if (!had_kwid.isEmpty()) qputenv("KITTY_WINDOW_ID", had_kwid);
		if (!had_gfx.isEmpty()) qputenv("QTTY_GRAPHICS", had_gfx);
		if (!had_ct.isEmpty()) qputenv("COLORTERM", had_ct);
	}

	// ---------------------------------------------------------- a real terminal
	// The startup query, raw mode and the SIGWINCH path all require stdin and
	// stdout to BE a terminal: collect_caps() is skipped on a pipe, ioctl
	// TIOCGWINSZ fails, and query_geometry() writes nothing. Testing them
	// against a socketpair proves the parser and leaves the wiring uncovered,
	// so the suite makes a pty and puts the backend on it.
	{
		int master = -1, slave = -1;
		if (::openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
			printf("FAIL: could not open a pty\n");
			++fails;
		} else {
			// Non-blocking, or the drain loop below never returns: a pty
			// master read blocks once the buffer is empty, and "read until it
			// is empty" is only bounded if the descriptor says so.
			::fcntl(master, F_SETFL, O_NONBLOCK);
			// $TERM is set explicitly rather than inherited. This machine's
			// is "screen", which inside_tmux() correctly reads as a
			// multiplexer and which would make the tier half-blocks -- so
			// without this the test asserts the ambient environment rather
			// than the code, and passes or fails by where it was run.
			const QByteArray pty_term = qgetenv("TERM");
			const QByteArray pty_tmux = qgetenv("TMUX");
			qputenv("TERM", "xterm-kitty");
			qunsetenv("TMUX");

			winsize ws{};
			ws.ws_col = 80;
			ws.ws_row = 24;
			::ioctl(slave, TIOCSWINSZ, &ws);

			// Pre-loaded so it is already readable when the constructor asks.
			// A pty buffers, so this is the terminal having answered promptly
			// rather than a race being papered over.
			const QByteArray answer = "\033_Gi=31;OK\033\\"
			                          "\033]11;rgb:1c1c/1c1c/1c1c\033\\"
			                          "\033[6;19;10t"
			                          "\033[?62;4;22c";
			const ssize_t pre = ::write(master, answer.constData(), answer.size());
			(void)pre;

			// Flushed at every switch. stdout is block-buffered when it is
			// not a terminal, so anything still in the buffer would be
			// written down whichever descriptor 1 happens to be at flush
			// time -- which put half a PASS line into the pty the first time.
			const int keep_in = ::dup(0), keep_out = ::dup(1);
			fflush(stdout);
			::dup2(slave, 0);
			::dup2(slave, 1);
			{
				AnsiBackend live;
				Recorder live_rec;
				live.set_event_sink(&live_rec);
				const Capabilities c = live.capabilities();

				fflush(stdout);
				::dup2(keep_out, 1);          // report on the real stdout
				CHECK(c.graphics == Capabilities::KittyAlpha
				          || c.graphics == Capabilities::Kitty,
				      "on a real terminal the startup query picks the kitty tier");
				CHECK(c.cell_px == QSize(10, 19),
				      "and the cell size it reported is kept");
				CHECK(c.background_known && c.background == QColor(0x1c, 0x1c, 0x1c),
				      "and so is its background");
				CHECK(live.size() == QSize(80, 24),
				      "and the size came from the terminal, not the fallback");

				// The resize half, which is the reason this is tied to input
				// at all: a font change moves the pixel geometry without
				// moving the cell count, so the backend must ask again. The
				// query goes out on stdout; the answer would come back on
				// stdin like everything else.
				fflush(stdout);
				::dup2(slave, 1);
				char drain[4096];
				while (::read(master, drain, sizeof(drain)) > 0) { }
				ws.ws_col = 100;
				ws.ws_row = 30;
				::ioctl(slave, TIOCSWINSZ, &ws);
				::raise(SIGWINCH);
				for (int i = 0; i < 50; ++i) QCoreApplication::processEvents();
				char out[4096];
				const ssize_t got = ::read(master, out, sizeof(out));
				const QByteArray written(out, got > 0 ? int(got) : 0);
				fflush(stdout);
				::dup2(keep_out, 1);
				CHECK(written.contains("\033[16t"),
				      "a resize re-asks for the pixel geometry");
				CHECK(live_rec.resizes.size() == 1
				          && live_rec.resizes[0] == QSize(100, 30),
				      "and reports the new cell size once");
			}
			fflush(stdout);
			::dup2(keep_in, 0);
			::dup2(keep_out, 1);
			::close(keep_in);
			::close(keep_out);
			::close(master);
			::close(slave);
			if (pty_term.isEmpty()) qunsetenv("TERM"); else qputenv("TERM", pty_term);
			if (!pty_tmux.isEmpty()) qputenv("TMUX", pty_tmux);
		}
	}

	return fails;
}
