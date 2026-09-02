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
#include "src/backend/ansi/scroll_settle.h"
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
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

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

// A real pseudo-terminal on fd 0 and 1, because the raw-mode path is gated on
// isatty() and the Feeder's pipe is not one -- so everything termios does was
// untestable, which is why nobody had noticed what it left switched on.
//
// posix_openpt and friends rather than openpty(), which lives in libutil and
// would put a link dependency in the test build for one fixture.
//
// STDIN ONLY. The first version put the pty on fd 1 as well, and stdout is
// where this suite prints: four of its own checks wrote their PASS lines into
// the pseudo-terminal and were never seen, while the count dropped by eleven
// and the run still said "OK". A fixture that swallows the output of the
// checks inside it is worse than no fixture -- it fails silently and looks
// like a pass. Only isatty(0) gates the raw-mode path being tested here.
struct Tty {
	int master = -1, slave = -1, saved_in = -1;
	Tty() {
		master = posix_openpt(O_RDWR | O_NOCTTY);
		if (master < 0) return;
		if (grantpt(master) != 0 || unlockpt(master) != 0) return;
		const char *name = ptsname(master);
		if (!name) return;
		slave = ::open(name, O_RDWR | O_NOCTTY);
		if (slave < 0) return;
		saved_in = ::dup(0);
		::dup2(slave, 0);
	}
	bool ok() const { return slave >= 0; }
	~Tty() {
		if (saved_in >= 0) { ::dup2(saved_in, 0); ::close(saved_in); }
		if (slave >= 0) ::close(slave);
		if (master >= 0) ::close(master);
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
	// Bit 1 is the axis. 66 and 67 are wheel-left and wheel-right, and reading
	// bit 0 alone made them identical to 64 and 65 -- a sideways scroll came
	// out as a vertical one. Asserted as the axes being separate, which is
	// what was broken, rather than as the four values.
	feed("\033[<66;5;5M");
	const int hl = rec.mice.isEmpty() ? 0 : rec.mice[0].wheel_x;
	const int hlv = rec.mice.isEmpty() ? 9 : rec.mice[0].wheel;
	feed("\033[<67;5;5M");
	const int hr = rec.mice.isEmpty() ? 0 : rec.mice[0].wheel_x;
	feed("\033[<65;5;5M");
	const int vx = rec.mice.isEmpty() ? 9 : rec.mice[0].wheel_x;
	CHECK(hl != 0 && hr != 0 && hl != hr && hlv == 0 && vx == 0,
	      "the two wheel axes decode separately");
	feed("\033[<32;7;3M");
	CHECK(rec.mice.size() == 1 && rec.mice[0].motion, "drag decodes as motion");

	// The whole button word, not just its low two bits. Bit 128 marks buttons
	// 8..11 and masking it off was an impersonation rather than a fallback:
	// 128 became a LEFT press, 129 a middle one and 130 a RIGHT one, which
	// fires a context menu. Low bits 3 is the protocol's "no button" and is
	// now 0 rather than a fourth button number.
	//
	// Asserted as the relationship that was broken -- the extended buttons are
	// distinct from the plain ones and from each other -- rather than as four
	// literals, since the numbering is this decoder's own.
	feed("\033[<128;5;5M");
	const int back = rec.mice.isEmpty() ? -1 : rec.mice[0].button;
	feed("\033[<129;5;5M");
	const int fwd = rec.mice.isEmpty() ? -1 : rec.mice[0].button;
	feed("\033[<130;5;5M");
	const int ext = rec.mice.isEmpty() ? -1 : rec.mice[0].button;
	CHECK(back > 3 && fwd > 3 && ext > 3 && back != fwd && fwd != ext,
	      "an extended mouse button is not one of the first three");
	feed("\033[<35;5;5M");
	CHECK(rec.mice.size() == 1 && rec.mice[0].motion && rec.mice[0].button == 0,
	      "and a bare motion report carries no button at all");

	// Bits 4, 8 and 16 are shift, meta and control, and they were decoded by
	// nothing at all. Checked one at a time and then together, because a
	// decoder that returned all three for any modifier would satisfy a check
	// on the combination alone.
	feed("\033[<4;5;5M");
	const bool sh = !rec.mice.isEmpty() && rec.mice[0].shift
	             && !rec.mice[0].ctrl && !rec.mice[0].alt;
	feed("\033[<16;5;5M");
	const bool ct = !rec.mice.isEmpty() && rec.mice[0].ctrl
	             && !rec.mice[0].shift && !rec.mice[0].alt;
	feed("\033[<20;5;5M");
	const bool both = !rec.mice.isEmpty() && rec.mice[0].ctrl && rec.mice[0].shift
	               && rec.mice[0].button == 1;
	CHECK(sh && ct && both, "a click carries its keyboard modifiers");

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

		// -- OSC 4, the palette. Only the low sixteen are asked for, because
		//    16 to 255 are a formula every terminal shares and asking would
		//    be 240 round trips to learn what is already known.
		const TermCaps pal = caps_of("\033]4;0;rgb:0000/0000/0000\033\\"
		                             "\033]4;1;rgb:cdcd/0000/0000\033\\"
		                             "\033]4;15;rgb:ffff/ffff/ffff\033\\");
		CHECK(pal.palette16.size() == 16, "an OSC 4 reply sizes the palette");
		CHECK(pal.palette16[1] == qRgb(0xcd, 0, 0), "and lands each index");
		CHECK(pal.palette16[15] == qRgb(255, 255, 255),
		      "including the four-digit white, which two digits would darken");
		CHECK(caps_of("\033]4;99;rgb:ffff/ffff/ffff\033\\").palette16.isEmpty(),
		      "an index outside the sixteen asked for is ignored");

		// -- DECRQM, which is how a capability is asked about rather than
		//    assumed. The distinction that matters is between silence and a
		//    definite zero: qtty used to report mouse and bracketed paste
		//    from whether it got RAW MODE, which is a fact about the local
		//    tty and says nothing about what the terminal understands.
		const TermCaps modes = caps_of("\033[?1006;1$y\033[?2004;2$y"
		                               "\033[?2026;0$y");
		CHECK(dec_mode(modes, 1006) == 1, "DECRPM set is read");
		CHECK(dec_mode(modes, 2004) == 2, "DECRPM reset is read, and is not zero");
		CHECK(dec_mode(modes, 2026) == 0, "and not-recognised is read as zero");
		CHECK(dec_mode(modes, 1004) == -1,
		      "a mode the terminal said nothing about is unknown, not absent");

		// The asymmetry, which is this file's rule applied one layer down.
		// Only a definite zero may turn a capability off; silence leaves the
		// caller's own belief alone, because it learned nothing.
		CHECK(mode_usable(modes, 1006, false),
		      "a mode the terminal confirms is usable even if unassumed");
		CHECK(mode_usable(modes, 2004, false),
		      "reset still means recognised -- the mode exists, it is just off");
		CHECK(!mode_usable(modes, 2026, true),
		      "not-recognised overrides the assumption");
		CHECK(mode_usable(modes, 1004, true) && !mode_usable(modes, 1004, false),
		      "and silence leaves the assumption exactly as it was");

		// -- the query itself must carry the fence last, or the collector
		//    stops reading before the answers it is waiting for arrive.
		const QByteArray q = caps_query();
		CHECK(q.endsWith("\033[c"), "the batched query ends with DA1");
		CHECK(q.contains("\033_G") && q.contains("+q524742")
		      && q.contains("\033]11;?") && q.contains("\033[16t"),
		      "and asks for kitty, direct colour, the background and the cell");
		CHECK(q.contains("\033]4;0;?") && q.contains(";15;?"),
		      "and for the low sixteen of the palette, not all 256");
		CHECK(q.contains("\033[?1006$p") && q.contains("\033[?2004$p")
		      && q.contains("\033[?1004$p") && q.contains("\033[?2026$p"),
		      "and asks about the modes it had been assuming");
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

	// Every string-sequence opener the decoder claims to frame, each followed
	// by a key that must still arrive. The list in decode_one() is
	// "]P_^X" -- OSC, DCS, APC, PM, SOS -- and only the first three had a
	// test, so PM and SOS were framed by code nothing exercised.
	//
	// Prompted by the beerssh session pointing out that it now answers many
	// more queries than it did, and every reply is one this decoder has to
	// terminate. The failure mode is not a wrong answer: an unterminated
	// sequence swallows the session, as the DECRPM case did.
	{
		struct { const char *name; QByteArray bytes; } openers[] = {
			{"OSC ended by ST",  QByteArray("\033]11;rgb:f/f/f\033\\")},
			{"OSC ended by BEL", QByteArray("\033]0;title\007")},
			{"DCS",              QByteArray("\033P1+r524742=38\033\\")},
			{"DCS, DA3 shape",   QByteArray("\033P!|00000000\033\\")},
			{"APC",              QByteArray("\033_Gi=31;OK\033\\")},
			{"PM",               QByteArray("\033^private\033\\")},
			{"SOS",              QByteArray("\033Xstring\033\\")},
		};
		for (const auto &o : openers) {
			feed(o.bytes + QByteArray("\033[A"));
			const bool ok = rec.keys.size() == 1
			             && rec.keys[0].qt_key == Qt::Key_Up;
			printf("%s: a key after %s still arrives\n", ok ? "PASS" : "FAIL", o.name);
			if (!ok) ++fails;
		}
	}

	// Bytes on stdin becoming text in a widget. The decoder has 33 checks
	// here and the router has 20 of its own; NOTHING ran a byte through both.
	// Every test on this side stops at a recording sink, and every test on
	// that side starts from a hand-built event -- both halves exhaustively
	// covered and the chain between them covered nowhere, which is the one
	// path a terminal library exists for.
	//
	// The mirror of what the beerssh session found on its side within the
	// hour: thirty-two router tests, a transport test, and nothing asserting
	// that pressing a key sends a byte.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *edit = new QLineEdit(&win);
		edit->setGeometry(0, 0, GridMetrics::cw() * 20, GridMetrics::ch());
		win.resize(GridMetrics::cells(30, 4));
		win.show();
		QCoreApplication::processEvents();
		edit->setFocus();
		set_focus_widget(edit);
		QCoreApplication::processEvents();

		InputRouter router(&win);
		backend.set_event_sink(&router);

		const auto type = [&](const QByteArray &bytes) {
			feeder.send(bytes);
			for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
		};

		type("ab");
		CHECK(edit->text() == QStringLiteral("ab"),
		      "a byte on stdin becomes text in the focused widget");

		// The discriminators, chosen because a plain letter proves almost
		// nothing: a path that simply forwarded each byte's character would
		// pass the check above with the decoder cut out entirely.
		//
		// An arrow is three bytes that must move the cursor and insert
		// NOTHING. A forwarder inserts "[C".
		type("\033[D\033[D");
		type("X");
		CHECK(edit->text() == QStringLiteral("Xab"),
		      "and an arrow moves the cursor rather than inserting its bytes");

		// A multi-byte character must arrive as ONE character. A byte-at-a-
		// time path yields three, and its length is what says so -- the
		// glyph looks plausible either way.
		edit->clear();
		type(QString(QChar(0x6f22)).toUtf8());
		CHECK(edit->text().size() == 1 && edit->text().at(0) == QChar(0x6f22),
		      "and a three-byte character arrives as one, not three");

		// Home and End, in BOTH encodings, because a terminal picks one and
		// the application meets whichever it picked. Coverage named these:
		// the decoder maps Up, Right, Left and Delete and every test used
		// those four, so the cases below had never run -- in a library whose
		// whole subject is editing text in a terminal.
		//
		// Asserted by where the next character LANDS, which is the only
		// thing that distinguishes a key that moved the cursor from one
		// consumed and dropped. A test on the sink would pass for both.
		edit->setText(QStringLiteral("bc"));
		edit->end(false);
		type("\033[H");                   // CSI H
		type("a");
		CHECK(edit->text() == QStringLiteral("abc"), "CSI H is Home");
		type("\033[F");                   // CSI F
		type("d");
		CHECK(edit->text() == QStringLiteral("abcd"), "and CSI F is End");
		type("\033[1~");                  // the ~ encoding of the same two
		type("z");
		CHECK(edit->text() == QStringLiteral("zabcd"), "CSI 1~ is Home as well");
		type("\033[4~");
		type("y");
		CHECK(edit->text() == QStringLiteral("zabcdy"), "and CSI 4~ is End");

		// Down, PageDown and PageUp, on a list rather than a line edit --
		// the widget where they mean something, so the assertion is the row
		// that ended up current rather than a key that was seen.
		auto *list = new QListWidget(&win);
		for (int i = 0; i < 40; ++i)
			list->addItem(QStringLiteral("row %1").arg(i));
		list->setGeometry(0, GridMetrics::ch(),
		                  GridMetrics::cw() * 20, GridMetrics::ch() * 6);
		list->show();
		list->setCurrentRow(0);
		list->setFocus();
		set_focus_widget(list);
		QCoreApplication::processEvents();

		type("\033[B");
		CHECK(list->currentRow() == 1, "CSI B is Down");
		const int after_down = list->currentRow();
		type("\033[6~");
		const int after_pgdn = list->currentRow();
		CHECK(after_pgdn > after_down + 1, "CSI 6~ is PageDown, not one row");
		type("\033[5~");
		CHECK(list->currentRow() < after_pgdn, "and CSI 5~ is PageUp");

		// Insert has no standard effect in a Qt widget, so it is asserted
		// where it can be: arriving AT one, as Qt::Key_Insert. That is still
		// the delivery half -- the sink checks above prove the decode, and
		// this proves a widget was handed the result.
		struct Watcher : QWidget {
			using QWidget::QWidget;
			int last = 0;
			void keyPressEvent(QKeyEvent *e) override { last = e->key(); }
		};
		auto *watch = new Watcher(&win);
		watch->setFocusPolicy(Qt::StrongFocus);
		watch->setGeometry(0, GridMetrics::ch() * 8, GridMetrics::cw() * 4,
		                   GridMetrics::ch());
		watch->show();
		watch->setFocus();
		set_focus_widget(watch);
		QCoreApplication::processEvents();
		type("\033[2~");
		CHECK(watch->last == Qt::Key_Insert, "CSI 2~ reaches a widget as Insert");

		// CSI Z is Shift+Tab, and shift is the whole of it: without it this
		// moves focus FORWARD and the assertion below passes for the wrong
		// reason, since with two widgets forward and backward are the same
		// place. Three, so they are not.
		auto *a1 = new QLineEdit(&win), *a2 = new QLineEdit(&win),
		     *a3 = new QLineEdit(&win);
		for (QLineEdit *e : {a1, a2, a3}) {
			e->setGeometry(0, GridMetrics::ch() * 9, GridMetrics::cw() * 4,
			               GridMetrics::ch());
			e->show();
		}
		QWidget::setTabOrder(a1, a2);
		QWidget::setTabOrder(a2, a3);
		a2->setFocus();
		set_focus_widget(a2);
		QCoreApplication::processEvents();
		type("\033[Z");
		// Which widget ended up focused, not merely that it was not a1: the
		// interesting failures are focus moving FORWARD (the shift lost) and
		// focus going nowhere (the key never arriving), and those are
		// different bugs that this condition reports identically. The first
		// draft of this assertion asked Qt for the focus and had to be
		// diagnosed with a temporary print.
		{
			QWidget *f = win.focusWidget();
			const char *where = f == a1 ? "a1" : f == a2 ? "a2"
			                  : f == a3 ? "a3" : f ? "elsewhere" : "nowhere";
			if (f == a1) {
				printf("PASS: CSI Z is a back-tab, and moves focus backwards\n");
			} else {
				printf("FAIL: CSI Z is a back-tab, and moves focus backwards\n"
				       "      focus is %s, having started at a2\n", where);
				++fails;
			}
		}

		backend.set_event_sink(&rec);
	}

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

		// Inside tmux a DIRECT placement would land at the outer terminal's
		// cursor rather than where tmux draws, so it arrives in the wrong
		// place. Unicode placeholders carry an image through correctly -- but
		// only where the id survives, and this capability set is not true
		// colour, so the id could not. Half-blocks are the fallback, being
		// text already. The placeholder case is checked below.
		const QByteArray had_tmux = qgetenv("TMUX");
		qputenv("TMUX", "/tmp/tmux-1000/default,1234,0");
		qputenv("TERM", "xterm-kitty");
		CHECK(inside_tmux(), "$TMUX is how tmux is known");
		CHECK(negotiate_graphics(kitty) == Capabilities::Halfblocks,
		      "and a kitty terminal whose depth cannot carry the id falls back");
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

		// Placeholders: needed only where something in between would move a
		// direct placement without knowing it had, and safe only where the
		// image id survives the trip.
		TermCaps tcaps; tcaps.answered = true; tcaps.kitty = true;
		tcaps.truecolor = true;
		qputenv("TMUX", "/tmp/tmux-1000/default,1234,0");
		CHECK(use_placeholders(tcaps, Capabilities::TrueColor),
		      "inside tmux a true-colour kitty terminal uses placeholders");
		// The id travels in the FOREGROUND COLOUR, so at 256 colours it would
		// be quantised to a palette index and the terminal would look up the
		// wrong image, or none at all.
		CHECK(!use_placeholders(tcaps, Capabilities::Xterm256),
		      "but not at 256 colours, which would quantise the id away");
		TermCaps nokitty; nokitty.answered = true;
		CHECK(!use_placeholders(nokitty, Capabilities::TrueColor),
		      "and neither does one proven NOT to speak the protocol");
		// And with them available, tmux no longer forces half-blocks: that
		// refusal was only ever standing in for this.
		CHECK(negotiate_graphics(tcaps) == Capabilities::Kitty,
		      "so tmux stops forcing half-blocks once placeholders can carry it");
		qunsetenv("TMUX");
		CHECK(!use_placeholders(tcaps, Capabilities::TrueColor),
		      "outside tmux a real placement is cheaper and exact");
		if (!had_tmux.isEmpty()) qputenv("TMUX", had_tmux);

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
			                          "\033[?2026;1$y"
			                          "\033P1+r524742=38\033\\"
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
				// The settle policy WIRED, not merely correct: a policy
				// nothing consults is the fault this suite keeps finding.
				// Driven at the sixel tier, since that is the one that pays
				// for movement -- kitty is excluded by design because a
				// placement there has a handle.
				{
					const QByteArray had_gfx = qgetenv("QTTY_GRAPHICS");
					qputenv("QTTY_GRAPHICS", "sixel");
					AnsiBackend sx;
					Recorder sx_rec;
					sx.set_event_sink(&sx_rec);
					CellBuffer f(20, 6);
					CellImage ci;
					ci.key = 77;
					ci.cell_rect = QRect(0, 0, 4, 2);
					ci.pixmap = QPixmap::fromImage(
					    QImage(8, 8, QImage::Format_ARGB32));
					f.images.append(ci);

					const auto emit_frame = [&](const QRect &at) {
						f.images[0].cell_rect = at;
						while (::read(master, drain, sizeof(drain)) > 0) { }
						sx.present(f, QRegion());
						QByteArray got;
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							got.append(drain, int(n));
						return got;
					};
					const QByteArray still = emit_frame(QRect(0, 0, 4, 2));
					const QByteArray moved = emit_frame(QRect(0, 2, 4, 2));
					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(still.contains("\033P0;1;0q"),
					      "a still placement is emitted as real sixel");
					CHECK(!moved.contains("\033P0;1;0q"),
					      "and a moved one degrades to the mosaic instead");
					// iTerm2, the one tier whose emission nothing asserted.
					// 68 checks in suite_graphics round-trip the encoders --
					// the parse half is the strongest in that file -- and
					// sixel and kitty are each checked on the wire above,
					// while OSC 1337 appeared in no test at all. Third
					// application of the search key, third find.
					fflush(stdout);
					::dup2(slave, 1);          // present() must write to the pty
					qputenv("QTTY_GRAPHICS", "iterm2");
					AnsiBackend it2;
					Recorder it2_rec;
					it2.set_event_sink(&it2_rec);
					f.images[0].cell_rect = QRect(0, 0, 4, 2);
					while (::read(master, drain, sizeof(drain)) > 0) { }
					it2.present(f, QRegion());
					QByteArray itout;
					{
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							itout.append(drain, int(n));
					}
					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(itout.contains("\033]1337;File=inline=1"),
					      "the iTerm2 tier emits its image to the terminal");
					// Sized in CELLS, which is the half that makes cropping
					// work: OSC 1337 takes a cell count, so cropping the
					// pixels without it squeezes the whole picture into the
					// visible rows instead of hiding the rest.
					CHECK(itout.contains("width=4") && itout.contains("height=2"),
					      "sized in cells, matching the placement");
					fflush(stdout);
					::dup2(slave, 1);

					// The exclusion, which is a claim and therefore needs a
					// check: kitty placements have handles, so moving one is
					// a short escape with no re-upload and degrading it would
					// trade a cheap correct picture for a coarse one. Without
					// this, "excluded" is a comment.
					fflush(stdout);
					::dup2(slave, 1);          // present() must write to the pty
					qputenv("QTTY_GRAPHICS", "kitty");
					AnsiBackend kt;
					Recorder kt_rec;
					kt.set_event_sink(&kt_rec);
					const auto kitty_frame = [&](const QRect &at) {
						f.images[0].cell_rect = at;
						while (::read(master, drain, sizeof(drain)) > 0) { }
						kt.present(f, QRegion());
						QByteArray got;
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							got.append(drain, int(n));
						return got;
					};
					kitty_frame(QRect(0, 0, 4, 2));
					const QByteArray kmoved = kitty_frame(QRect(0, 2, 4, 2));
					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(kmoved.contains("\033_G"),
					      "a moved kitty placement is NOT degraded, having a handle");

					// DEC 2026, and the pair that matters: the bracket must
					// open AND close, and the claim in capabilities() must
					// match what actually goes out. A field saying
					// "synchronised" over bare frames is exactly the defect
					// shape this negotiation exists to stop, and it would be
					// invisible from inside this process.
					fflush(stdout);
					::dup2(slave, 1);
					{
						const ssize_t w3 = ::write(master, answer.constData(),
						                           answer.size());
						(void)w3;
					}
					AnsiBackend sy;
					Recorder sy_rec;
					sy.set_event_sink(&sy_rec);
					const bool claims = sy.capabilities().synchronised_output;
					while (::read(master, drain, sizeof(drain)) > 0) { }
					CellBuffer sf(20, 4);
					sy.present(sf, QRegion());
					QByteArray syout;
					{
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							syout.append(drain, int(n));
					}
					fflush(stdout);
					::dup2(keep_out, 1);
					// The pty answered 2026;1 in `answer`, so this terminal
					// has it. Both halves, and the claim, or none.
					CHECK(claims, "a terminal that confirms 2026 is reported as synchronised");
					CHECK(syout.contains("\033[?2026h") && syout.contains("\033[?2026l"),
					      "and its frames are bracketed at both ends");
					CHECK(syout.indexOf("\033[?2026h") < syout.indexOf("\033[?2026l"),
					      "in that order, which is the only order that syncs anything");

					// The other half, and the one that discriminates: a
					// terminal answering 0 must get BARE frames and must not
					// be reported as synchronised. Without this, bracketing
					// unconditionally would pass every check above.
					fflush(stdout);
					::dup2(slave, 1);
					{
						QByteArray no = answer;
						no.replace("\033[?2026;1$y", "\033[?2026;0$y");
						const ssize_t w4 = ::write(master, no.constData(), no.size());
						(void)w4;
					}
					AnsiBackend nosy;
					Recorder nosy_rec;
					nosy.set_event_sink(&nosy_rec);
					const bool nosy_claims = nosy.capabilities().synchronised_output;
					while (::read(master, drain, sizeof(drain)) > 0) { }
					nosy.present(sf, QRegion());
					QByteArray nosyout;
					{
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							nosyout.append(drain, int(n));
					}
					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(!nosy_claims, "a terminal answering 0 is not reported as synchronised");
					CHECK(!nosyout.contains("\033[?2026"),
					      "and gets no bracket at all, rather than a claim over bare frames");

					// The wide-cluster model reaching the WIRE. suite_cells
					// asserts 48 things about clusters and widths -- the
					// parse half is the most thoroughly tested code in this
					// tree -- and nothing asserted what present() does with
					// one. Its whole handling is a single line,
					// `if (c.width == 0) continue;`, and a passing parser is
					// exactly what stopped anyone looking at it.
					//
					// Found with the search key the beerssh session drew out
					// of this: find the well-tested parser, then ask what
					// consumes it and whether anything asserts the
					// consumption.
					fflush(stdout);
					::dup2(slave, 1);
					{
						// COLOURED, and that is the whole test. The first
						// version used default colours and could not fail:
						// a continuation cell's `ch` is EMPTY, so emitting it
						// appends no bytes, and with equal colours it emits no
						// SGR either -- the skip was a no-op and the sabotage
						// that removed it stayed green.
						//
						// What the skip actually protects is the colour run.
						// The continuation carries DEFAULT colours, so without
						// it a coloured wide glyph is followed by an SGR reset
						// and then the next cell's colour again, breaking the
						// run in the middle of a character.
						CellBuffer wide(6, 1);
						wide.put_cluster(0, 0, QStringLiteral("\u6f22"));  // wide
						wide.put_cluster(2, 0, QStringLiteral("x"));
						wide.at(0, 0).fg = Color::rgb(qRgb(200, 40, 40));
						wide.at(2, 0).fg = Color::rgb(qRgb(200, 40, 40));
						while (::read(master, drain, sizeof(drain)) > 0) { }
						live.present(wide, QRegion());
						QByteArray wout;
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							wout.append(drain, int(n));
						const QByteArray glyph = QStringLiteral("\u6f22").toUtf8();
						fflush(stdout);
						::dup2(keep_out, 1);
						CHECK(wout.count(glyph) == 1,
						      "a wide cluster is written once, not once per cell");
						// The continuation must contribute NOTHING. If it
						// emitted its own blank the row would be a cell too
						// long and everything after it would shift, which is
						// the failure the skip exists to prevent.
						const int lead = wout.indexOf(glyph);
						CHECK(lead >= 0
						      && wout.mid(lead + glyph.size(), 1) == QByteArrayLiteral("x"),
						      "and the colour run is not broken across its continuation");
						fflush(stdout);
						::dup2(slave, 1);
					}

					// Placeholders end to end. This is the case the whole
					// tmux path exists for: a proven kitty terminal behind a
					// multiplexer, where a direct placement would land at the
					// outer terminal's cursor rather than where tmux draws.
					fflush(stdout);
					::dup2(slave, 1);
					const QByteArray had_tmux2 = qgetenv("TMUX");
					qputenv("TMUX", "/tmp/tmux-1000/default,1,0");
					qunsetenv("QTTY_GRAPHICS");
					// Re-armed: each backend runs its own startup query, and
					// the answer loaded at the top was consumed by the first
					// one. Without this it measures a silent terminal and
					// correctly concludes there is no kitty here.
					{
						const ssize_t w2 = ::write(master, answer.constData(),
						                           answer.size());
						(void)w2;
					}
					AnsiBackend ph;
					Recorder ph_rec;
					ph.set_event_sink(&ph_rec);
					const Capabilities pc = ph.capabilities();
					while (::read(master, drain, sizeof(drain)) > 0) { }
					f.images[0].cell_rect = QRect(0, 0, 2, 2);
					ph.present(f, QRegion());
					QByteArray pout;
					{
						ssize_t n;
						while ((n = ::read(master, drain, sizeof(drain))) > 0)
							pout.append(drain, int(n));
					}
					const char32_t place = 0x10EEEE;
					const QByteArray glyph = QString::fromUcs4(&place, 1).toUtf8();
					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(pc.unicode_placements,
					      "a kitty terminal inside tmux reports placeholder delivery");
					CHECK(pout.contains("\033Ptmux;"),
					      "the transmission is wrapped through tmux");
					CHECK(pout.contains(glyph),
					      "and the placement itself is printed as ordinary text");
					CHECK(!pout.contains("\033_Ga=p"),
					      "with no direct placement, which tmux would misplace");
					if (had_tmux2.isEmpty()) qunsetenv("TMUX");
					else qputenv("TMUX", had_tmux2);

					// What the upload-once cache does when the pictures
					// keep coming. kitty_delete_all() uses d=a, which drops
					// PLACEMENTS and leaves the image data -- so nothing ever
					// freed a picture, and a surface that animates uploads one
					// image per distinct frame into another process for the
					// life of the session.
					//
					// Unreachable until an hour ago: the frame loop compared
					// placements by COUNT, so a picture that changed under
					// unchanged cells was never presented, the upload path ran
					// once per surface, and the leak had nothing to leak.
					// Fixing the gate is what turned this on.
					fflush(stdout);
					::dup2(slave, 1);
					{
						const QByteArray had_gfx3 = qgetenv("QTTY_GRAPHICS");
						qputenv("QTTY_GRAPHICS", "kitty");
						{
							const ssize_t w3 = ::write(master, answer.constData(),
							                           answer.size());
							(void)w3;
						}
						AnsiBackend up;
						Recorder up_rec;
						up.set_event_sink(&up_rec);
						(void)up.capabilities();
						while (::read(master, drain, sizeof(drain)) > 0) { }

						CellBuffer uf(20, 6);
						CellImage uci;
						uci.cell_rect = QRect(0, 0, 2, 2);
						uci.pixmap = QPixmap::fromImage(
						    QImage(8, 8, QImage::Format_ARGB32));
						uf.images.append(uci);
						QByteArray uout;
						// Drained after every frame rather than at the end:
						// twenty frames of pixels and text will fill a pty
						// buffer, and a full one blocks the writer inside
						// present() with nobody reading.
						const auto show = [&](quint64 key) {
							uf.images[0].key = key;
							up.present(uf, QRegion());
							ssize_t n;
							while ((n = ::read(master, drain, sizeof(drain))) > 0)
								uout.append(drain, int(n));
						};

						for (quint64 k = 1; k <= 20; ++k) show(k);   // animating
						const int freed = uout.count("\033_Ga=d,d=I");
						const int sent = uout.count("a=T,f=32");

						// Then the case the cap exists for: four pictures
						// cycled, which is a spinner. Each is uploaded once
						// and none is ever freed, because freeing on every
						// unreferenced frame would re-encode a full image for
						// one it is about to want again.
						//
						// A SECOND backend, and the first draft's fault. Run
						// on the one above it started with sixteen live keys
						// from the animation, so the frees it counted were
						// those -- correct behaviour, arriving as a failure of
						// an assertion that had measured a shared cache.
						fflush(stdout);
						::dup2(slave, 1);
						{
							const ssize_t w4 = ::write(master, answer.constData(),
							                           answer.size());
							(void)w4;
						}
						AnsiBackend sp;
						Recorder sp_rec;
						sp.set_event_sink(&sp_rec);
						(void)sp.capabilities();
						while (::read(master, drain, sizeof(drain)) > 0) { }
						uout.clear();
						const auto spin = [&](quint64 key) {
							uf.images[0].key = key;
							sp.present(uf, QRegion());
							ssize_t n;
							while ((n = ::read(master, drain, sizeof(drain))) > 0)
								uout.append(drain, int(n));
						};
						for (int cycle = 0; cycle < 5; ++cycle)
							for (quint64 k = 101; k <= 104; ++k) spin(k);
						const int spin_sent = uout.count("a=T,f=32");
						const int spin_freed = uout.count("\033_Ga=d,d=I");

						fflush(stdout);
						::dup2(keep_out, 1);
						CHECK(sent == 20, "each distinct picture is uploaded once");
						// Exactly four: the cap is 16, so the seventeenth
						// upload is the first that can evict anything, and
						// frames 17 to 20 evict one apiece.
						CHECK(freed == 4,
						      "and the terminal is told to free the ones nothing shows");
						CHECK(spin_sent == 4,
						      "a cycle of pictures within the cap uploads each once");
						CHECK(spin_freed == 0, "and frees none of them");
						fflush(stdout);
						::dup2(slave, 1);
						if (had_gfx3.isEmpty()) qunsetenv("QTTY_GRAPHICS");
						else qputenv("QTTY_GRAPHICS", had_gfx3);
					}

					// The four methods that WRITE and had no wire test at
					// all: present_pixels, present_overlay, clear_overlay and
					// set_cursor were every uncovered line left in this file
					// once the keys were done. Each is a public entry point
					// whose whole job is to emit, and each was reached only by
					// callers no test drove -- the seam finding again, from
					// the emission end.
					fflush(stdout);
					::dup2(slave, 1);
					{
						const QByteArray had_gfx4 = qgetenv("QTTY_GRAPHICS");
						QByteArray eout;
						const auto pump = [&] {
							ssize_t n;
							while ((n = ::read(master, drain, sizeof(drain))) > 0)
								eout.append(drain, int(n));
						};
						const auto fresh = [&] {
							const ssize_t w5 = ::write(master, answer.constData(),
							                           answer.size());
							(void)w5;
						};
						const int icw = GridMetrics::cw(), ich = GridMetrics::ch();
						QImage art(icw * 2, ich * 2, QImage::Format_ARGB32);
						art.fill(QColor(10, 200, 30));

						// The cursor. Its POLICY -- which cell, and whether a
						// delegating widget gets one -- is asserted in the
						// widget suite; what it emits was asserted nowhere.
						// The exact sequence, because the interesting part is
						// the 1-based conversion: a check for "some CUP" would
						// pass with the row and column off by one, which is
						// the whole of what this function computes.
						fresh();
						{
							AnsiBackend cur;
							Recorder cur_rec;
							cur.set_event_sink(&cur_rec);
							(void)cur.capabilities();
							while (::read(master, drain, sizeof(drain)) > 0) { }
							eout.clear();
							cur.set_cursor(QPoint(3, 2), CursorShape::Bar);
							pump();
							const bool placed = eout.contains("\033[3;4H")
							                 && eout.contains("\033[?25h");
							eout.clear();
							cur.set_cursor(std::nullopt, CursorShape::Hidden);
							pump();
							const bool hidden = eout.contains("\033[?25l");
							fflush(stdout);
							::dup2(keep_out, 1);
							CHECK(placed,
							      "set_cursor puts the cursor at the cell, counting from one");
							CHECK(hidden, "and hides it when there is none");
							fflush(stdout);
							::dup2(slave, 1);
						}

						// present_pixels: one finished picture, which is what
						// the compositor hands over when an overlay forces a
						// software composite. Three tiers, and all three are
						// asserted together so that a switch answering the
						// same way whatever the mode fails rather than passes.
						QByteArray kout, sout2, iout;
						const auto pixels = [&](const char *mode, QByteArray &into) {
							qputenv("QTTY_GRAPHICS", mode);
							fresh();
							AnsiBackend px;
							Recorder px_rec;
							px.set_event_sink(&px_rec);
							(void)px.capabilities();
							while (::read(master, drain, sizeof(drain)) > 0) { }
							eout.clear();
							px.present_pixels(art, QRegion());
							pump();
							into = eout;
						};
						pixels("kitty", kout);
						pixels("sixel", sout2);
						pixels("iterm2", iout);

						// The overlay pair, on the tier that has one. The id
						// arithmetic is the assertion: overlays live in their
						// own id space above the placements, and a transmit
						// and a delete that disagreed about it would leave the
						// picture on screen for ever.
						qputenv("QTTY_GRAPHICS", "kitty-alpha");
						fresh();
						QByteArray ovon, ovoff;
						{
							AnsiBackend ov;
							Recorder ov_rec;
							ov.set_event_sink(&ov_rec);
							(void)ov.capabilities();
							while (::read(master, drain, sizeof(drain)) > 0) { }
							eout.clear();
							ov.present_overlay(7, art, QPoint(2, 1), 3);
							pump();
							ovon = eout;
							eout.clear();
							ov.clear_overlay(7);
							pump();
							ovoff = eout;
						}
						const QByteArray ovid =
						    "i=" + QByteArray::number(0xFFFFE00u + 7u);

						fflush(stdout);
						::dup2(keep_out, 1);
						CHECK(kout.contains("\033_Ga=T") && kout.contains("a=d,d=a"),
						      "present_pixels on kitty replaces the picture");
						CHECK(sout2.contains("\033P0;1;0q"),
						      "and on sixel it is a sixel");
						CHECK(iout.contains("\033]1337;File=inline=1"),
						      "and on iTerm2 it is an inline file");
						CHECK(!sout2.contains("\033_Ga=T")
						      && !iout.contains("\033_Ga=T"),
						      "each tier emits only its own, rather than one answer for all");
						CHECK(ovon.contains("\033_Ga=T") && ovon.contains(ovid),
						      "present_overlay transmits in the overlay id space");
						CHECK(ovoff.contains("a=d,d=i") && ovoff.contains(ovid),
						      "and clear_overlay deletes the same id");
						fflush(stdout);
						::dup2(slave, 1);
						if (had_gfx4.isEmpty()) qunsetenv("QTTY_GRAPHICS");
						else qputenv("QTTY_GRAPHICS", had_gfx4);
					}

					// The terminal's own low sixteen, adopted only when it
					// answered for ALL of them. Half a user's scheme and half
					// xterm's is a palette no terminal has, and matching
					// against it would be worse than matching against either
					// -- so the refusal is the half worth asserting, and the
					// half a test written to the feature's name would skip.
					fflush(stdout);
					::dup2(slave, 1);
					{
						const QVector<QRgb> had_pal = terminal_palette();
						const auto osc4 = [](int n, int v) {
							return QByteArray("\033]4;") + QByteArray::number(n)
							     + ";rgb:" + QByteArray::number(v, 16).rightJustified(2, '0')
							     + '/' + QByteArray::number(v, 16).rightJustified(2, '0')
							     + '/' + QByteArray::number(v, 16).rightJustified(2, '0')
							     + "\033\\";
						};
						// Fifteen of sixteen: one short is the whole test.
						set_terminal_palette({});
						QByteArray partial;
						for (int i = 0; i < 15; ++i) partial += osc4(i, 0x10 + i);
						{
							const ssize_t wp = ::write(master,
							    (partial + answer).constData(),
							    partial.size() + answer.size());
							(void)wp;
						}
						AnsiBackend few;
						Recorder few_rec;
						few.set_event_sink(&few_rec);
						(void)few.capabilities();
						while (::read(master, drain, sizeof(drain)) > 0) { }
						const bool refused = terminal_palette().isEmpty();

						QByteArray full;
						for (int i = 0; i < 16; ++i) full += osc4(i, 0x10 + i);
						{
							const ssize_t wf = ::write(master,
							    (full + answer).constData(),
							    full.size() + answer.size());
							(void)wf;
						}
						AnsiBackend all;
						Recorder all_rec;
						all.set_event_sink(&all_rec);
						(void)all.capabilities();
						while (::read(master, drain, sizeof(drain)) > 0) { }
						const QVector<QRgb> got = terminal_palette();

						fflush(stdout);
						::dup2(keep_out, 1);
						CHECK(refused, "fifteen colours out of sixteen is not a palette");
						CHECK(got.size() == 16 && got[0] == qRgb(0x10, 0x10, 0x10)
						      && got[15] == qRgb(0x1f, 0x1f, 0x1f),
						      "and sixteen of sixteen is adopted as the terminal's own");
						fflush(stdout);
						::dup2(slave, 1);
						set_terminal_palette(had_pal);   // global: put it back
					}

					// Half-blocks against the terminal's OWN background. The
					// fallback tier blends a translucent image over whatever
					// sits behind it, and it guessed a dark grey for its
					// whole life -- so on a light terminal every translucent
					// edge was haloed. OSC 11 is in the startup query and
					// this terminal answers 1c1c1c, so a fully transparent
					// image must come out as exactly that and not as the
					// old guess.
					fflush(stdout);
					::dup2(slave, 1);
					{
						const QByteArray had_gfx5 = qgetenv("QTTY_GRAPHICS");
						qputenv("QTTY_GRAPHICS", "halfblocks");
						{
							const ssize_t w8 = ::write(master, answer.constData(),
							                           answer.size());
							(void)w8;
						}
						AnsiBackend hb;
						Recorder hb_rec;
						hb.set_event_sink(&hb_rec);
						const Capabilities hc = hb.capabilities();
						while (::read(master, drain, sizeof(drain)) > 0) { }

						// HALF transparent, not fully: a fully transparent
						// image is not painted at all, which is correct and
						// which the first draft asserted a blend against --
						// the frame came back blank and said so.
						QImage clear(GridMetrics::cw() * 2, GridMetrics::ch() * 2,
						             QImage::Format_ARGB32);
						clear.fill(QColor(200, 0, 0, 128));
						CellBuffer hf(20, 4);
						CellImage hci;
						hci.key = 4242;
						hci.cell_rect = QRect(0, 0, 2, 1);
						hci.pixmap = QPixmap::fromImage(clear);
						hf.images.append(hci);
						hb.present(hf, QRegion());
						QByteArray hout;
						{
							ssize_t n;
							while ((n = ::read(master, drain, sizeof(drain))) > 0)
								hout.append(drain, int(n));
						}
						fflush(stdout);
						::dup2(keep_out, 1);
						CHECK(hc.background_known
						      && hc.background == QColor(0x1c, 0x1c, 0x1c),
						      "the terminal answered for its background");
						// Asserted on the SHAPE of the result rather than
						// on exact bytes: the terminal's background is a
						// neutral grey, so blending a pure red over it leaves
						// green and blue EQUAL. The grey this used to guess is
						// (16, 20, 24), which is not neutral -- no blend over
						// it can produce equal channels. That holds whichever
						// way the arithmetic rounds.
						const QRegularExpression sgr(
						    QStringLiteral("[34]8;2;(\\d+);(\\d+);(\\d+)"));
						auto m = sgr.match(QString::fromLatin1(hout));
						const int g = m.hasMatch() ? m.captured(2).toInt() : -1;
						const int b = m.hasMatch() ? m.captured(3).toInt() : -2;
						CHECK(m.hasMatch(),
						      "a half-transparent image paints cells in real colours");
						CHECK(g == b && g > 12,
						      "blended over the terminal's own neutral background");
						fflush(stdout);
						::dup2(slave, 1);
						if (had_gfx5.isEmpty()) qunsetenv("QTTY_GRAPHICS");
						else qputenv("QTTY_GRAPHICS", had_gfx5);
					}

					fflush(stdout);            // before switching BACK, too
					::dup2(slave, 1);
					if (had_gfx.isEmpty()) qunsetenv("QTTY_GRAPHICS");
					else qputenv("QTTY_GRAPHICS", had_gfx);
				}

				fflush(stdout);
				::dup2(keep_out, 1);
				CHECK(written.contains("\033[16t"),
				      "a resize re-asks for the pixel geometry");
				CHECK(live_rec.resizes.size() == 1
				          && live_rec.resizes[0] == QSize(100, 30),
				      "and reports the new cell size once");

				// Zero ROWS with a good column count. read_winch() refused a
				// zero column count and accepted this, which is the same
				// nonsense arriving at the same place -- a frame with no
				// cells, whose rasterisation is a null QImage that QPainter
				// refuses to open and warns about once per call, onto the
				// terminal. `stty rows 0` produces it, and so does a pty
				// sized partly.
				//
				// fd 1 must BE the pty, for the reason the block below gives
				// at length: read_winch() asks TIOCGWINSZ on descriptor 1,
				// and the line above put it back on the real stdout. Written
				// without this the check passed against the defect, because
				// the signal never reached the backend at all -- which is the
				// plumbing fault this file already warns about once.
				//
				// Asserted on BOTH halves: the size the backend reports must
				// not move, and the sink must not be told about a resize that
				// did not happen. A guard that returned after assigning would
				// pass the second and fail the first.
				{
					fflush(stdout);
					::dup2(slave, 1);
					ws.ws_col = 100;
					ws.ws_row = 0;
					::ioctl(slave, TIOCSWINSZ, &ws);
					::raise(SIGWINCH);
					for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
					const QSize after = live.size();
					const int reported = int(live_rec.resizes.size());
					// The other door into the same fault. A backend
					// CONSTRUCTED against a terminal already reporting zero
					// rows never sees a resize at all, so the guard above
					// cannot answer for it -- and the constructor asked about
					// columns only, exactly as read_winch() did. Its fallback
					// for a size it cannot use is 80x24, which is what a
					// piped run gets.
					const QSize born = AnsiBackend().size();
					ws.ws_row = 30;
					::ioctl(slave, TIOCSWINSZ, &ws);
					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(after == QSize(100, 30) && reported == 1,
					      "a terminal reporting zero rows is refused, as zero "
					      "columns already was");
					CHECK(born == QSize(80, 24),
					      "and a backend born against one falls back, as it "
					      "does for zero columns");
				}

				// End to end, because the two halves being right separately
				// is not the same fact as the chain working. The backend
				// delivering to a sink is checked above; the router resizing
				// its window is checked in suite_router; nothing had ever run
				// SIGWINCH through a real InputRouter to a real window --
				// which is exactly the "correct function, unwired feature"
				// shape this suite keeps turning up.
				{
					// fd 1 must BE the pty for this: read_winch() asks
					// TIOCGWINSZ on descriptor 1, and the preceding case left
					// it pointing at the real stdout so it would have read the
					// wrong terminal's size and returned early. A test that
					// measured that would have reported a library fault that
					// was its own plumbing.
					fflush(stdout);
					::dup2(slave, 1);

					QWidget win;
					win.setAttribute(Qt::WA_DontShowOnScreen);
					win.resize(GridMetrics::cells(80, 24));
					win.show();
					QCoreApplication::processEvents();
					InputRouter router(&win);
					live.set_event_sink(&router);

					ws.ws_col = 60;
					ws.ws_row = 20;
					::ioctl(slave, TIOCSWINSZ, &ws);
					::raise(SIGWINCH);
					for (int i = 0; i < 50; ++i) QCoreApplication::processEvents();

					fflush(stdout);
					::dup2(keep_out, 1);
					CHECK(win.size() == QSize(60 * GridMetrics::cw(),
					                          20 * GridMetrics::ch()),
					      "a terminal resize reaches the window, signal to geometry");
					fflush(stdout);            // before switching BACK, again
					::dup2(slave, 1);
					live.set_event_sink(&live_rec);
				}
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

	// -- section 5.7's scroll-settle policy. The clock is a parameter, so
	//    this exercises a hundred-millisecond debounce without sleeping for
	//    one: a test that sleeps is a test that is flaky on a loaded machine.
	{
		const auto placed = [](quint64 key, QRect at) {
			QVector<CellImage> v;
			CellImage ci;
			ci.key = key;
			ci.cell_rect = at;
			v.append(ci);
			return v;
		};
		ScrollSettle s(100);

		// Nothing has moved yet, so the pixels go out at once. Degrading the
		// FIRST frame would be exactly backwards: that is when the picture is
		// most wanted and nothing is costing anything yet.
		CHECK(s.update(placed(1, QRect(0, 0, 4, 2)), 0),
		      "the first frame draws real pixels");
		CHECK(s.update(placed(1, QRect(0, 0, 4, 2)), 10),
		      "and a frame where nothing moved still does");

		// A placement moves: degrade now, and keep degrading until the
		// debounce has elapsed with it standing still.
		CHECK(!s.update(placed(1, QRect(0, 1, 4, 2)), 20),
		      "a moved placement degrades to the mosaic");
		CHECK(!s.update(placed(1, QRect(0, 1, 4, 2)), 60),
		      "and stays degraded while the debounce runs");
		CHECK(!s.update(placed(1, QRect(0, 2, 4, 2)), 100),
		      "a move during the wait restarts it");
		CHECK(!s.update(placed(1, QRect(0, 2, 4, 2)), 150),
		      "so the clock runs from the LAST move, not the first");
		CHECK(s.update(placed(1, QRect(0, 2, 4, 2)), 200),
		      "and the pixels come back once it has settled");
		CHECK(s.update(placed(1, QRect(0, 2, 4, 2)), 210),
		      "and stay back rather than flickering");

		// A placement appearing is a picture arriving, not a scroll. Counting
		// it would degrade the first frame of every image -- the one case the
		// pixels are most wanted -- which is why only a MOVED placement
		// counts.
		ScrollSettle arrive(100);
		CHECK(arrive.update(placed(1, QRect(0, 0, 4, 2)), 0), "an image arrives");
		QVector<CellImage> two = placed(1, QRect(0, 0, 4, 2));
		CellImage second;
		second.key = 2;
		second.cell_rect = QRect(5, 0, 4, 2);
		two.append(second);
		CHECK(arrive.update(two, 10),
		      "and a second one appearing beside it is not a scroll");
		CHECK(arrive.update(placed(1, QRect(0, 0, 4, 2)), 20),
		      "nor is one going away");
	}

	// A DECRPM reply, ESC [ ? 1006 ; 1 $ y. The "$" is an intermediate byte,
	// not a final, so a parser that only accepts finals in 0x40..0x7e never
	// terminates the sequence. Probing whether the decoder survives one
	// BEFORE relying on the mode query that produces them.
	// queried_modes() must agree with the query it is derived from. This is
	// the defence for a fault that already happened: a per-probe report
	// carried its own list of modes, included 1002 -- which qtty SETS at
	// startup but never asks DECRQM about -- and therefore reported "silent"
	// for a question nobody sent. That was one edit from reaching a
	// terminal's author as their defect.
	{
		const QVector<int> modes = queried_modes();
		const QByteArray q = caps_query();
		// The count, independently: every DECRQM request ends "$p", so the
		// number of those is how many modes were asked about, arrived at
		// without walking the string the same way the function does. A
		// derivation that dropped entries would still return a plausible
		// list, and the report would be quietly narrower than the query.
		CHECK(!modes.isEmpty() && modes.size() == q.count("$p"),
		      "queried_modes() returns exactly as many modes as the query asks");
		bool all_present = true;
		for (int m : modes)
			all_present = all_present
			           && q.contains("\033[?" + QByteArray::number(m) + "$p");
		CHECK(all_present, "and every one of them is a mode the query names");
		// The specific fault, by name: a mode qtty sets but never queries
		// must not appear, or silence gets reported for it for ever.
		CHECK(!modes.contains(1002),
		      "and 1002, which is set but never asked about, is not among them");
	}

	// A paste carrying an ESCAPE, which is the case bracketed paste exists
	// for and which nothing had sent. Everything pasted between the brackets
	// is text, including bytes that look like a control sequence: a terminal
	// that decoded them would let anything a user pastes drive the
	// application, which is the whole reason the mode was invented.
	rec.clear();
	feeder.send("\033[200~a\033[Ab\033[201~");
	for (int i = 0; i < 50; ++i) QCoreApplication::processEvents();
	// Both halves. That the text arrived whole says the escape was kept; that
	// NO key arrived says it was not also acted on -- and the second is the
	// one that fails if the branch is missing, because then the paste is
	// three fragments and an Up key.
	CHECK(rec.pastes.size() == 1
	      && rec.pastes[0] == QStringLiteral("a\033[Ab"),
	      "an escape inside a paste is pasted, not obeyed");
	CHECK(rec.keys.isEmpty(), "and produces no keystroke of its own");

	rec.clear();
	feeder.send("\033[?1006;1$y");
	QCoreApplication::processEvents();
	feeder.send("\033[A");
	for (int i = 0; i < 50; ++i) QCoreApplication::processEvents();
	CHECK(rec.keys.size() == 1 && rec.keys[0].qt_key == Qt::Key_Up,
	      "a DECRPM reply does not stall the decoder");

	// The terminal going away. read() returns 0, and the backend turns that
	// into Ctrl+D rather than spinning on a descriptor that will never carry
	// anything again -- an EOF descriptor is permanently READABLE, so a
	// notifier over one fires for ever and a backend that merely returned
	// would burn a core doing nothing.
	{
		int eof_fds[2];
		if (::pipe(eof_fds) == 0) {
			const int keep0 = ::dup(0);
			::dup2(eof_fds[0], 0);
			::close(eof_fds[1]);              // nothing will ever be written
			rec.clear();
			for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
			// Restored before asserting, so that a failing CHECK's own
			// printf is not the thing that has to survive a broken stdin.
			::dup2(keep0, 0);
			::close(keep0);
			::close(eof_fds[0]);
			CHECK(!rec.keys.isEmpty() && rec.keys[0].qt_key == Qt::Key_D
			      && rec.keys[0].ctrl,
			      "a terminal that went away arrives as Ctrl+D");
		} else {
			printf("FAIL: could not build the EOF pipe\n");
			++fails;
		}
	}

	// The gate that decides whether to WRITE at all, which is the half of
	// that lens this suite had not asked -- every check above asks what is
	// written. tty_out_ gates resume(), suspend() and the geometry query and
	// gates none of the frame output; nothing asserted either side of it.
	//
	// It matters most in suspend(). A terminal left in mouse-reporting mode
	// writes an escape burst into the user's shell on every click for the
	// rest of that shell's life, so the modes must go off -- and must never
	// have been set for a stream that is not a terminal, since then there is
	// nothing to reset and the bytes land in somebody's file.
	{
		int fds[2];
		int pm = -1, ps = -1;
		if (::pipe(fds) == 0 && ::openpty(&pm, &ps, nullptr, nullptr, nullptr) == 0) {
			const auto run_into = [&](int fd) {
				const int keep = ::dup(1);
				fflush(stdout);
				::dup2(fd, 1);
				{
					AnsiBackend b;
					Recorder r;
					b.set_event_sink(&r);
					b.resume();
					// A frame, between the two, because whether CONTENT is
					// gated on isatty(1) is a separate question from whether
					// CONTROL is -- and it is answered the other way. See
					// below.
					CellBuffer frame(4, 1);
					frame.text(0, 0, QStringLiteral("hi"));
					b.present(frame, QRegion(0, 0, 4, 1));
					b.suspend();
				}
				fflush(stdout);
				::dup2(keep, 1);
				::close(keep);
			};
			// The CONTROL, and the reason it is here rather than assumed:
			// "wrote nothing" is satisfied by a backend that writes nothing
			// ever, so the same two calls are made down a pty first.
			::fcntl(pm, F_SETFL, O_NONBLOCK);
			run_into(ps);
			QByteArray on_tty;
			{
				char b[4096];
				ssize_t n;
				while ((n = ::read(pm, b, sizeof(b))) > 0) on_tty.append(b, int(n));
			}
			run_into(fds[1]);
			::close(fds[1]);
			::fcntl(fds[0], F_SETFL, O_NONBLOCK);
			QByteArray on_pipe;
			{
				char b[4096];
				ssize_t n;
				while ((n = ::read(fds[0], b, sizeof(b))) > 0)
					on_pipe.append(b, int(n));
			}
			::close(fds[0]);
			::close(pm);
			::close(ps);
			CHECK(on_tty.contains("\033[?1049h") && on_tty.contains("\033[?1006h"),
			      "a terminal gets the alternate screen and the reporting modes");
			CHECK(on_tty.contains("\033[?1006l") && on_tty.contains("\033[?1049l"),
			      "and gets them all switched off again");
			CHECK(!on_pipe.contains("\033[?1049h") && !on_pipe.contains("\033[?1006h"),
			      "while a pipe is sent no mode it cannot be in");
			// Content is NOT gated, and that is a decision the tree already
			// took by building on it: `qtty-replay --ansi > corpus` drives
			// this backend with stdout redirected to a file, and the whole
			// point is the byte stream it captures -- doc/beerssh.md section
			// 4's parser corpus. Measured: 1683 bytes from a two-line script.
			// Gating present() would make that tool emit nothing.
			//
			// The line the tree draws is between a terminal's STATE and its
			// CONTENT. Setting modes on a stream that is not a terminal
			// changes something we do not own and cannot reset; writing the
			// frame is what was asked for, and `program | cat` wants it as
			// much as `program > recording` does.
			CHECK(on_pipe.contains("hi") && !on_pipe.contains("\033[?1049h"),
			      "a pipe is written the frame and none of the modes");
		} else {
			printf("FAIL: could not build the tty/pipe pair\n");
			++fails;
		}
	}

	return fails;
}

// suite_exec -- the two-argument exec(), which is the front door every
// application calls and which had no coverage at all: everything else tests
// the three-argument form with a NullBackend, so the overload that builds a
// real AnsiBackend and owns it for the run was never entered.
//
// Its OWN suite, and that is not tidiness. AnsiBackend::read_input() does a
// blocking read(0) when its QSocketNotifier says stdin is ready, which is
// correct for the one backend a program has. Two of them on one descriptor
// both wake, the first takes the bytes, and the second blocks for ever --
// measured, as a hang whose syscall was read on the pty slave. suite_backend
// keeps a backend alive for the whole of its run, so this cannot live there.
// qtty owning the terminal exclusively is the design (section 5.1), so the
// test respects it rather than the product being widened to a configuration
// it excludes.
int suite_exec() {
	int fails = 0;
	int master = -1, slave = -1;
	if (::openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
		printf("FAIL: could not open a pty for the front door\n");
		return 1;
	}
	::fcntl(master, F_SETFL, O_NONBLOCK);

	// A SMALL terminal, and this is what makes the test possible at all.
	// exec() writes a whole frame from the same thread that has to drain the
	// pty, so a frame larger than the buffer blocks in fwrite with its only
	// reader stuck behind it. At 20x4 a frame is a few hundred bytes.
	winsize ws{};
	ws.ws_col = 20;
	ws.ws_row = 4;
	::ioctl(slave, TIOCSWINSZ, &ws);

	const QByteArray answer = "\033_Gi=31;OK\033\\"
	                          "\033[?2026;1$y"
	                          "\033P1+r524742=38\033\\"
	                          "\033]11;rgb:1c1c/1c1c/1c1c\033\\"
	                          "\033[6;19;10t"
	                          "\033[?62;4;22c";
	const ssize_t pre = ::write(master, answer.constData(), answer.size());
	(void)pre;

	const int keep_in = ::dup(0), keep_out = ::dup(1);
	fflush(stdout);
	::dup2(slave, 0);
	::dup2(slave, 1);

	QWidget win;
	auto *edit = new QLineEdit(&win);
	edit->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
	edit->setFocus();

	// Repeating, not singleShot: exec() calls processEvents() before
	// app.exec(), so a zero timer fires while there is no loop to quit and
	// the test hangs for ever. Steps rather than one deadline, so the
	// keystroke goes in after the loop is actually running.
	int step = 0;
	bool during = false;
	QTimer drive;
	drive.setInterval(10);
	QObject::connect(&drive, &QTimer::timeout, qApp, [&] {
		// Drain every tick. Nothing else reads the master during the run and
		// the scheduler writes a frame on every idle tick, so the buffer
		// fills and present() blocks inside fwrite.
		char sink[4096];
		while (::read(master, sink, sizeof(sink)) > 0) { }
		if (step == 1) {
			const ssize_t typed = ::write(master, "hi", 2);
			(void)typed;
		}
		during = during || is_tui_active();
		if (step >= 4) QCoreApplication::quit();
		++step;
	});
	drive.start();

	const bool active_before = is_tui_active();
	auto *qa = qobject_cast<QApplication *>(qApp);
	const int rc = qa ? Qtty::exec(*qa, win) : -1;
	drive.stop();
	const QString got = edit->text();
	const bool active_after = is_tui_active();

	fflush(stdout);
	::dup2(keep_in, 0);
	::dup2(keep_out, 1);
	::close(keep_in);
	::close(keep_out);
	::close(master);
	::close(slave);

	// Driven with a keystroke rather than merely started and stopped:
	// starting proves construction, and a keystroke proves the wiring exec()
	// exists to do -- backend to router to widget, through the frame callback
	// exec() installs, whose call site was uncovered for the same reason.
	CHECK(rc == 0, "exec(app, win) runs and returns cleanly");
	// Read from INSIDE the run as well. False before and false after is
	// satisfied by a flag nothing ever sets, which is the whole of what the
	// flag is for -- is_tui_active() is how a widget knows not to take the
	// GUI path (section 10.1).
	CHECK(!active_before && during && !active_after,
	      "and the TUI flag is set for the run and cleared after it");
	CHECK(got == QStringLiteral("hi"),
	      "and a byte typed during the run reaches the widget");

	// ---- a signal restores the terminal ----
	{
		// suspend() undoes everything resume() did and runs from the
		// destructor, which a signal does not reach. Measured before this,
		// with the backend running:
		//
		//   SIGINT=dfl SIGTERM=dfl SIGHUP=dfl SIGQUIT=dfl
		//   SIGSEGV=dfl SIGABRT=dfl
		//
		// So a kill from another window, a hangup, or a crash left the
		// terminal in raw mode, on the alternate screen, with mouse
		// reporting on -- and suspend()'s own comment says what that costs:
		// an escape burst into the user's shell on every click for the rest
		// of that shell's life.
		//
		// The disposition is what is checked rather than the effect. Sending
		// a real SIGSEGV to the test process to watch the bytes come out
		// would mean forking, and a suite that kills itself to make a point
		// is a worse trade than asking the kernel what is installed.
		auto disposition = [](int sig) {
			struct sigaction old {};
			sigaction(sig, nullptr, &old);
			return old.sa_handler;
		};
		const int fatal[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT,
			                  SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };

		// A known baseline, saved and put back. "Whatever the environment
		// had" was the first version and it does not work: an earlier
		// backend in this same suite leaves handlers installed if the
		// restore is broken, so "before" and "after" move together and a
		// sabotage of the restore reddened the wrong check. Two numbers that
		// move together cannot separate anything.
		//
		// The harness's own dispositions are saved and restored around the
		// block rather than clobbered: QtTest installs a stack-dump handler,
		// and a test that takes it away permanently would change how every
		// later crash reports.
		struct sigaction outer[sizeof(fatal) / sizeof(fatal[0])];
		for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); ++i) {
			struct sigaction def {};
			def.sa_handler = SIG_DFL;
			sigemptyset(&def.sa_mask);
			sigaction(fatal[i], &def, &outer[i]);
		}
		int set_before = 0;
		for (int sig : fatal)
			if (disposition(sig) != SIG_DFL) ++set_before;

		int set_during = 0, still_set_after = 0;
		{
			Feeder feeder;
			Qtty::AnsiBackend backend;
			backend.resume();
			for (int sig : fatal)
				if (disposition(sig) != SIG_DFL) ++set_during;
			backend.suspend();
			for (int sig : fatal)
				if (disposition(sig) != SIG_DFL) ++still_set_after;
		}
		// A second cycle, in this block rather than relying on an earlier
		// one somewhere else in the suite. Install and restore are coupled by
		// a "did I install" flag, so a broken restore shows up as a failure
		// to install the SECOND time -- and that is what a sabotage of the
		// restore actually reddens. Saying it here makes the block test its
		// own claim instead of inheriting the evidence from suite order.
		int set_second = 0;
		{
			Feeder feeder;
			Qtty::AnsiBackend backend;
			backend.resume();
			for (int sig : fatal)
				if (disposition(sig) != SIG_DFL) ++set_second;
			backend.suspend();
		}
		for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); ++i)
			sigaction(fatal[i], &outer[i], nullptr);
		CHECK(set_during == int(sizeof(fatal) / sizeof(fatal[0])),
		      "a running backend handles every signal that ends a process");
		// Paired both ways: it was not already so, and it does not stay so.
		// A backend that suspends has given the terminal back, and a crash
		// after that is not its to tidy after.
		CHECK(set_before == 0,
		      "which it was not before the backend existed");
		CHECK(still_set_after == 0,
		      "and it puts the previous handlers back when it suspends");
		CHECK(set_second == int(sizeof(fatal) / sizeof(fatal[0])),
		      "so a second backend installs them again, as it must");
	}

	// ---- raw mode takes the keys the driver would have eaten ----
	{
		// Measured by reading, then confirmed here: the raw-mode setup
		// cleared ICANON and ECHO and left ISIG and IXON alone. With ISIG on,
		// the terminal DRIVER turns Ctrl+C into SIGINT before a byte reaches
		// read_input() -- so InputRouter's quit keys, which default to
		// Ctrl+C, and the rule that makes Ctrl+C copy inside a text field,
		// could never see that chord from a real keyboard. With IXON on,
		// Ctrl+S freezes the terminal and the application looks hung.
		//
		// This is the first check in the file to run against a real tty. The
		// raw-mode path is gated on isatty(), and the pipe every other
		// fixture uses is not one -- which is why what termios left switched
		// on had never been looked at.
		Tty tty;
		if (!tty.ok()) {
			printf("FAIL: a pseudo-terminal could not be opened, so the raw"
			       " mode checks say nothing\n");
			++fails;
		} else {
			termios before {};
			tcgetattr(0, &before);
			// The premise: a fresh pty has them ON, so the checks below are
			// about this code turning them off rather than about a terminal
			// that never had them.
			CHECK((before.c_lflag & ISIG) && (before.c_iflag & IXON),
			      "a fresh terminal signals on Ctrl+C and flow-controls on Ctrl+S");
			{
				Qtty::AnsiBackend backend;
				backend.resume();
				termios during {};
				tcgetattr(0, &during);
				CHECK(!(during.c_lflag & ISIG),
				      "and a running backend takes Ctrl+C for itself");
				CHECK(!(during.c_iflag & IXON),
				      "and Ctrl+S, which would otherwise freeze the screen");
				CHECK(!(during.c_lflag & (ICANON | ECHO)),
				      "while still doing what it already did");
				backend.suspend();
			}
			termios after {};
			tcgetattr(0, &after);
			CHECK((after.c_lflag & ISIG) && (after.c_iflag & IXON),
			      "and gives them back when it suspends");
		}
	}

	return fails;
}
