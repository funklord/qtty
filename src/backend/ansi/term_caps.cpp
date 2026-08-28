// src/backend/ansi/term_caps.cpp -- the reply parser. See term_caps.h for why
// it is separate from the I/O that collects the bytes.
#include "term_caps.h"

#include <poll.h>
#include <unistd.h>

namespace Qtty {
namespace {

// A decimal run at *i, advancing it. Returns -1 when there are no digits at
// all, which is what separates "parameter absent" from "parameter zero" -- a
// distinction every one of the reports below depends on.
int scan_uint(const QByteArray &b, int &i) {
	long v = 0;
	int digits = 0;
	while (i < b.size() && b[i] >= '0' && b[i] <= '9') {
		if (v < 1000000) v = v * 10 + (b[i] - '0');
		++i;
		++digits;
	}
	return digits ? int(v) : -1;
}

int hex_digit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// The kitty graphics reply, ESC _ G <key=value pairs> ; <status> ESC \.
//
// A terminal implementing the protocol answers OK to a query action; one that
// implements it but refused this particular image answers an error code. Both
// prove the protocol is there, which is the question being asked -- and only
// OK is accepted anyway, because a terminal that cannot take a single 1x1
// direct-transmission pixel is not one to send an image to.
bool find_kitty(const QByteArray &b) {
	for (int i = 0; i + 3 < b.size(); ++i) {
		if (uchar(b[i]) != 0x1b || b[i + 1] != '_' || b[i + 2] != 'G') continue;
		int j = i + 3;
		while (j < b.size() && b[j] != ';' && uchar(b[j]) != 0x1b) ++j;
		if (j + 2 < b.size() && b[j] == ';' && b[j + 1] == 'O' && b[j + 2] == 'K')
			return true;
	}
	return false;
}

// Device attributes, ESC [ ? <p> ; <p> ; ... c. Attribute 4 means sixel, and
// has since the VT240.
//
// Parsed as a parameter LIST rather than searched for the text "4", which
// would match the 4 inside 14 -- the DEC national-replacement-character-sets
// attribute -- and report sixel on a terminal that has none.
void scan_da(const QByteArray &b, TermCaps &out) {
	for (int i = 0; i + 2 < b.size(); ++i) {
		if (uchar(b[i]) != 0x1b || b[i + 1] != '[') continue;
		int j = i + 2;
		if (b[j] == '?' || b[j] == '>') ++j;
		bool saw_param = false, sixel = false;
		for (;;) {
			const int v = scan_uint(b, j);
			if (v >= 0) {
				saw_param = true;
				if (v == 4) sixel = true;
			}
			if (j < b.size() && b[j] == ';') { ++j; continue; }
			break;
		}
		if (!saw_param || j >= b.size() || b[j] != 'c') continue;
		out.answered = true;
		if (sixel) out.sixel = true;
		return;
	}
}

// The window-operation reports, ESC [ <what> ; height ; width t.
//   4  the text area in pixels
//   6  one cell in pixels
//
// Height first. xterm's window operations report rows before columns
// throughout, and swapping them stretches every image by the cell aspect --
// a wrong picture rather than a missing one, which is the worse failure.
void scan_winop(const QByteArray &b, TermCaps &out) {
	for (int i = 0; i + 2 < b.size(); ++i) {
		if (uchar(b[i]) != 0x1b || b[i + 1] != '[') continue;
		int j = i + 2;
		const int what = scan_uint(b, j);
		if (what != 4 && what != 6) continue;
		if (j >= b.size() || b[j] != ';') continue;
		++j;
		const int h = scan_uint(b, j);
		if (h < 0 || j >= b.size() || b[j] != ';') continue;
		++j;
		const int w = scan_uint(b, j);
		if (w < 0 || j >= b.size() || b[j] != 't') continue;
		if (w > 0 && h > 0) {
			if (what == 6) out.cell_px = QSize(w, h);
			else           out.text_px = QSize(w, h);
		}
	}
}

// XTGETTCAP, ESC P 1 + r <hex name> [= <hex value>] [; ...] ST. A leading 1
// is the success form; 0 means the terminal knows the request and has none of
// the capabilities asked for.
//
// "RGB" and "Tc" are the two spellings terminfo uses for direct colour. Asked
// rather than taken from $COLORTERM, which is inherited by everything the
// shell starts and so survives an ssh to a machine whose terminal is not the
// one that set it.
void scan_tcap(const QByteArray &b, TermCaps &out) {
	static const QByteArray rgb_hex("524742");     // "RGB"
	static const QByteArray tc_hex("5463");        // "Tc"
	for (int i = 0; i + 4 < b.size(); ++i) {
		if (uchar(b[i]) != 0x1b || b[i + 1] != 'P') continue;
		int j = i + 2;
		if (j + 2 >= b.size() || b[j] != '1' || b[j + 1] != '+' || b[j + 2] != 'r')
			continue;
		j += 3;
		while (j < b.size() && uchar(b[j]) != 0x1b && uchar(b[j]) != 0x07) {
			const int name = j;
			while (j < b.size() && b[j] != '=' && b[j] != ';'
			       && uchar(b[j]) != 0x1b && uchar(b[j]) != 0x07)
				++j;
			const QByteArray got = b.mid(name, j - name);
			if (got == rgb_hex || got == tc_hex) out.truecolor = true;
			while (j < b.size() && b[j] != ';'
			       && uchar(b[j]) != 0x1b && uchar(b[j]) != 0x07)
				++j;
			if (j < b.size() && b[j] == ';') ++j;
		}
		return;
	}
}

// The OSC 11 reply, ESC ] 11 ; rgb:<r>/<g>/<b> ST.
//
// Each component is one to four hex digits and is scaled to eight bits by ITS
// OWN width: "rgb:f/f/f" is white, and reading the first two digits of each
// field would make it near-black on every terminal that answers in the short
// form.
void scan_osc11(const QByteArray &b, TermCaps &out) {
	static const QByteArray prefix("\033]11;rgb:");
	for (int i = 0; i + prefix.size() < b.size(); ++i) {
		if (b.mid(i, prefix.size()) != prefix) continue;
		int j = i + prefix.size();
		unsigned char rgb[3];
		for (int c = 0; c < 3; ++c) {
			long v = 0;
			int digits = 0;
			while (j < b.size() && hex_digit(b[j]) >= 0 && digits < 4) {
				v = v * 16 + hex_digit(b[j]);
				++j;
				++digits;
			}
			if (digits == 0) return;
			const long span = (1L << (4 * digits)) - 1;
			rgb[c] = (unsigned char)((v * 255 + span / 2) / span);
			if (c < 2) {
				if (j >= b.size() || b[j] != '/') return;
				++j;
			}
		}
		out.bg_known = true;
		out.bg[0] = rgb[0]; out.bg[1] = rgb[1]; out.bg[2] = rgb[2];
		return;
	}
}

// The OSC 4 reply, ESC ] 4 ; <index> ; rgb:RRRR/GGGG/BBBB ST -- one reply per
// index, so a query naming several gets several back.
//
// The components are scaled by their own digit width exactly as OSC 11's are,
// and for the same reason: "rgb:f/f/f" is white and reading two digits per
// field would make it near-black.
void scan_osc4(const QByteArray &b, TermCaps &out) {
	static const QByteArray prefix("\033]4;");
	for (int i = 0; i + prefix.size() < b.size(); ++i) {
		if (b.mid(i, prefix.size()) != prefix) continue;
		int j = i + prefix.size();
		const int index = scan_uint(b, j);
		if (index < 0 || index > 15) continue;      // only the low sixteen asked
		if (j + 5 >= b.size() || b.mid(j, 5) != QByteArrayLiteral(";rgb:")) continue;
		j += 5;
		unsigned char rgb[3];
		bool ok = true;
		for (int c = 0; c < 3 && ok; ++c) {
			long v = 0;
			int digits = 0;
			while (j < b.size() && hex_digit(b[j]) >= 0 && digits < 4) {
				v = v * 16 + hex_digit(b[j]);
				++j;
				++digits;
			}
			if (digits == 0) { ok = false; break; }
			const long span = (1L << (4 * digits)) - 1;
			rgb[c] = (unsigned char)((v * 255 + span / 2) / span);
			if (c < 2) {
				if (j >= b.size() || b[j] != '/') { ok = false; break; }
				++j;
			}
		}
		if (!ok) continue;
		if (out.palette16.isEmpty()) out.palette16.resize(16);
		out.palette16[index] = qRgb(rgb[0], rgb[1], rgb[2]);
	}
}

// The DECRPM reply, ESC [ ? <mode> ; <value> $ y.
//
// The "$" is an INTERMEDIATE byte rather than a final, which is what made this
// reply wedge the CSI parser before intermediates were handled: it waited for
// a final it would never see, and every key behind it was stuck.
void scan_decrqm(const QByteArray &b, TermCaps &out) {
	for (int i = 0; i + 4 < b.size(); ++i) {
		if (uchar(b[i]) != 0x1b || b[i + 1] != '[' || b[i + 2] != '?') continue;
		int j = i + 3;
		const int mode = scan_uint(b, j);
		if (mode < 0 || j >= b.size() || b[j] != ';') continue;
		++j;
		const int value = scan_uint(b, j);
		if (value < 0 || j + 1 >= b.size()) continue;
		if (b[j] != '$' || b[j + 1] != 'y') continue;
		out.dec_modes.insert(mode, value);
	}
}

} // namespace

int dec_mode(const TermCaps &caps, int mode) {
	return caps.dec_modes.value(mode, -1);
}

bool mode_usable(const TermCaps &caps, int mode, bool assumed) {
	const int v = dec_mode(caps, mode);
	if (v < 0) return assumed;                    // silence: learned nothing
	return v != 0;                                // 0 is the only definite no
}


bool inside_tmux() {
	if (!qgetenv("TMUX").isEmpty()) return true;
	// $TERM alone is weaker and is used only to say yes, never to rule tmux
	// out: a terminal calling itself screen or tmux is one, and being wrong
	// costs a wrapper that the terminal ignores as an unknown DCS string.
	const QByteArray term = qgetenv("TERM").toLower();
	return term.startsWith("screen") || term.startsWith("tmux");
}

QByteArray tmux_wrap(const QByteArray &payload) {
	QByteArray out("\033Ptmux;");
	for (char c : payload) {
		out += c;
		if (uchar(c) == 0x1b) out += c;       // every ESC is doubled
	}
	out += "\033\\";
	return out;
}

QByteArray caps_query() {
	// In the order the replies are expected back, with primary device
	// attributes LAST as the fence.
	return QByteArray(
	    // The kitty protocol's own "can you hear me": a query action carrying
	    // one 24-bit pixel of direct data. A terminal without the protocol
	    // ignores the APC string entirely, which is what makes it safe to
	    // send blind to something that may be anything at all.
	    "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\"
	    // Direct colour, asked rather than inherited.
	    "\033P+q524742;5463\033\\"
	    // Background, which every tier below kitty needs: they composite an
	    // image's alpha against it themselves.
	    "\033]11;?\033\\"
	    // The text area and one cell, both in pixels. The cell is what keeps
	    // an image's aspect ratio: a half-block pixel is one cell wide and
	    // half a cell tall, and treating that as square squashes every
	    // picture on a terminal whose cells are not 1:2.
	    "\033[14t"
	    "\033[16t"
	    // What the terminal makes of the modes qtty has just switched on.
	    // Asked rather than assumed: qtty reported mouse and bracketed paste
	    // from whether it got RAW MODE, which is a fact about the local tty
	    // and says nothing whatever about what the terminal understands.
	    "\033[?1006$p"                            // SGR mouse
	    "\033[?1004$p"                            // focus reporting
	    "\033[?2004$p"                            // bracketed paste
	    "\033[?2026$p"                            // synchronised output
	    // The low sixteen palette entries. Only these: 16 to 255 are a formula
	    // every terminal shares, so asking would be 240 round trips to learn
	    // what is already known. Several index/? pairs in one OSC, answered
	    // one reply each.
	    "\033]4;0;?;1;?;2;?;3;?;4;?;5;?;6;?;7;?"
	    ";8;?;9;?;10;?;11;?;12;?;13;?;14;?;15;?\033\\"
	    // Device attributes, doubling as the sixel probe and as the fence.
	    "\033[c");
}

void scan_caps(const QByteArray &buf, TermCaps &out) {
	if (find_kitty(buf)) out.kitty = true;
	scan_tcap(buf, out);
	scan_osc11(buf, out);
	scan_osc4(buf, out);
	scan_winop(buf, out);
	scan_decrqm(buf, out);
	scan_da(buf, out);
}

bool caps_complete(const QByteArray &buf) {
	TermCaps probe;
	scan_da(buf, probe);
	return probe.answered;
}

TermCaps collect_caps(int in_fd, int out_fd, int timeout_ms) {
	TermCaps caps;
	// Wrapped inside tmux, or the query is answered by tmux itself: it is a
	// terminal too, and it answers device attributes while knowing nothing
	// about what it is sitting in. That reply would arrive as a measured
	// "this terminal has no graphics", which is worse than not asking --
	// the fence rule would then believe it.
	const QByteArray query = inside_tmux() ? tmux_wrap(caps_query()) : caps_query();
	int off = 0;
	while (off < query.size()) {
		const ssize_t w = ::write(out_fd, query.constData() + off, query.size() - off);
		if (w <= 0) return caps;                  // cannot ask; assume nothing
		off += int(w);
	}

	QByteArray buf;
	int remaining = timeout_ms;
	while (remaining > 0 && buf.size() < 4096) {
		pollfd p{};
		p.fd = in_fd;
		p.events = POLLIN;
		const int slice = remaining < 50 ? remaining : 50;
		const int r = ::poll(&p, 1, slice);
		if (r < 0) break;
		if (r == 0) { remaining -= slice; continue; }
		char chunk[512];
		const ssize_t got = ::read(in_fd, chunk, sizeof(chunk));
		if (got <= 0) break;
		buf.append(chunk, int(got));
		if (!caps_complete(buf)) continue;
		scan_caps(buf, caps);
		return caps;
	}
	scan_caps(buf, caps);                         // whatever arrived still counts
	return caps;
}

} // namespace Qtty
