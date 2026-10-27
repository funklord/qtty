// tool/fuzz/caps_fuzz.cpp -- libFuzzer over the capability-reply parser.
//
// WHY THIS FUNCTION AND NOT ANOTHER. scan_caps() is the one place in this
// library that parses bytes somebody else chose. Everything else reads a
// widget tree, a QPainter call or a config value; this reads whatever the
// TERMINAL sent back, and a terminal is free to answer anything -- a buggy
// one, an old one, a multiplexer rewriting replies, or the far end of an
// ssh session that is not what it claims to be. term_caps.h keeps the
// parser deliberately separate from the I/O so a test can stage a terminal,
// and that same split is what makes it fuzzable at all: a pure function
// from a QByteArray to a TermCaps, with no descriptors and no timeouts.
//
// BOTH ENTRIES, because a caller reaches for both: scan_caps() fills the
// struct and caps_complete() looks for the device-attributes fence.
//
// AND THE BUFFER IS ALSO FED IN TWO PIECES, at a cut the input itself
// chooses so the split point is fuzzed rather than fixed. scan_caps() is
// documented as additive -- fields are only ever set, never cleared, so it
// may be called again as more bytes arrive -- and the header says replies
// "over ssh routinely do not" arrive together. A parser correct on a whole
// buffer and wrong on a split one is a fault no whole-buffer fixture can
// reach, which is the arrangement this adds.
//
// NOT IN `make check`: it needs clang (libFuzzer is a clang runtime) where
// the gate is g++, and it is time-bounded rather than finite. `make fuzz`
// runs it. See section 8.605 for what it has found and for the positive
// control that says the harness can fail at all.
#include "term_caps.h"
#include <QByteArray>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const QByteArray buf(reinterpret_cast<const char *>(data), int(size));

	Qtty::TermCaps whole;
	Qtty::scan_caps(buf, whole);
	(void)Qtty::caps_complete(buf);

	if (size >= 2) {
		const int cut = int(data[0] % size);
		Qtty::TermCaps split;
		Qtty::scan_caps(buf.left(cut), split);
		Qtty::scan_caps(buf.mid(cut), split);
	}
	return 0;
}
