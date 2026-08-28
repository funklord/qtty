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
		setFocusWidget(edit);
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
	rec.clear();
	feeder.send("\033[?1006;1$y");
	QCoreApplication::processEvents();
	feeder.send("\033[A");
	for (int i = 0; i < 50; ++i) QCoreApplication::processEvents();
	CHECK(rec.keys.size() == 1 && rec.keys[0].qt_key == Qt::Key_Up,
	      "a DECRPM reply does not stall the decoder");

	return fails;
}
