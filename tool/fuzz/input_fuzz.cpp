// tool/fuzz/input_fuzz.cpp -- libFuzzer over the live-input decoder, which
// is the larger of this library's two trust boundaries. 8.605 took the
// other one -- scan_caps(), the startup probe parser -- and this is the
// surface that carries every keystroke, mouse report, paste and resize
// claim a terminal sends while the program runs.
//
// IT IS ALSO THE ONE WITH A MEASURED INTEGER FAULT ALREADY. ansi_backend.h
// records `CSI < 0 ; 99999999999 ; 1 M` decoding to a mouse row of
// 1215752190 -- that number wrapped into 32 bits -- which InputRouter then
// multiplied by a cell height: signed overflow, undefined behaviour. The
// fix was kCsiParamCap. So this is not a speculative target; it is the same
// surface, fuzzed, with UBSan watching the arithmetic that was wrong.
//
// NO PURE SLICE EXISTS, unlike scan_caps(): parse_csi() is a const member
// reading pending_, so the decoder has to be driven through a real backend.
// That is cheaper than it sounds because of one line in the constructor --
// the capability probe runs only `if (raw_ok_ && tty_out_)`, and fd 0 here
// is a PIPE rather than a pty, so construction neither probes nor waits.
// It is the suite's own Feeder arrangement.
//
// WHAT THE SINK IS FOR. Decoded events have to go somewhere or decode_one()
// clears the buffer without parsing -- the header says so -- so a sink that
// counts and discards is what keeps the decoder actually decoding. It also
// reads every field of every event, because a decoder that produces a
// nonsense QString or an out-of-range key is only caught by somebody
// touching it.
//
// NOT IN `make check`, for 8.605's two reasons: it needs clang, and it is
// time-bounded rather than finite. `make fuzz-input` runs it. section 8.606
// has what it found, which is nothing, and the control that says so
// honestly -- the decoder's own historical integer overflow, restored in a
// scratch copy and caught by UBSan on the input the header records.
#include <qtty/backend.h>
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <cstddef>
#include <fcntl.h>
#include <unistd.h>

// The backend's header is private to the library, so this includes it by
// path the way the library's own translation units do.
#include "ansi_backend.h"

namespace {

struct Counting : Qtty::ITerminalEventSink {
	// Touched rather than stored: the point is to read what the decoder
	// produced, so that a bad QString or a wild key number is dereferenced
	// by somebody while the sanitizers are watching.
	unsigned long long seen = 0;
	void on_key(const Qtty::KeyEvent &k) override {
		seen += unsigned(k.qt_key) + unsigned(k.text.size());
		for (QChar c : k.text) seen += c.unicode();
	}
	void on_mouse(const Qtty::MouseEvent &m) override {
		seen += unsigned(m.cell.x()) + unsigned(m.cell.y());
	}
	void on_paste(const QString &s) override {
		seen += unsigned(s.size());
		for (QChar c : s) seen += c.unicode();
	}
	void on_resize(QSize cells) override {
		seen += unsigned(cells.width()) + unsigned(cells.height());
	}
	void on_focus_change(bool f) override { seen += f ? 1 : 0; }
};

struct Rig {
	int write_fd = -1;
	QCoreApplication *app = nullptr;
	Qtty::AnsiBackend *backend = nullptr;
	Counting sink;

	Rig() {
		// fd 1 to /dev/null first: the backend writes to it, and a full pipe
		// or the fuzzer's own stdout would both be wrong.
		const int null_fd = ::open("/dev/null", O_WRONLY);
		if (null_fd >= 0) { ::dup2(null_fd, 1); ::close(null_fd); }

		int fds[2];
		if (::pipe(fds) != 0) return;
		::dup2(fds[0], 0);
		::close(fds[0]);
		write_fd = fds[1];
		// NON-BLOCKING, and that is a decision rather than an oversight: if
		// the decoder does not drain as fast as this writes, a blocking
		// write would hang the fuzzer for ever. Short writes mean some
		// inputs arrive truncated, which is a case the terminal produces
		// anyway.
		::fcntl(write_fd, F_SETFL, O_NONBLOCK);

		static int argc = 1;
		static char arg0[] = "inputfuzz";
		static char *argv[] = {arg0, nullptr};
		app = new QCoreApplication(argc, argv);
		backend = new Qtty::AnsiBackend();
		backend->set_event_sink(&sink);
	}
};

Rig &rig() { static Rig r; return r; }

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	Rig &r = rig();
	if (r.write_fd < 0 || !r.backend) return 0;
	ssize_t n = ::write(r.write_fd, data, size);
	(void)n;
	// Let the notifier read and the decoder run. Twice, because one pass
	// may leave a partial sequence pending and the second delivers it once
	// the rest arrives -- which is the split-sequence case the decoder has
	// a branch for.
	QCoreApplication::processEvents();
	QCoreApplication::processEvents();
	return 0;
}
