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
#include <QtWidgets>
#include <cstdio>
#include <unistd.h>

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

	return fails;
}
