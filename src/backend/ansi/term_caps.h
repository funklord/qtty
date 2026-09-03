// src/backend/ansi/term_caps.h -- what the terminal says about itself when
// asked, rather than what $TERM claims on its behalf (sections 5.1, 5.7).
//
// qtty used to decide its colour depth and graphics tier from $TERM,
// $TERM_PROGRAM, $COLORTERM and $KITTY_WINDOW_ID alone. Every one of those is
// inherited across ssh and su, so they are wrong in both directions: a tmux
// inside kitty exports TERM=screen and loses the protocol, and a TERM that
// says kitty on the far side of something which does not forward APC strings
// makes qtty write escape sequences onto the user's screen.
//
// The two failure directions are NOT equal, and that asymmetry is the rule
// this file is built on: a signal that cannot be verified may only ever say
// YES to a capability, never turn one on that would be emitted blind. A stale
// COLORTERM costs a picture drawn in approximated colour; a wrongly assumed
// kitty costs a screenful of garbage.
//
// The parser is deliberately separate from the I/O. Staging a terminal that
// answers the graphics query but not the colour one, or whose replies arrive
// in the wrong order or split across reads, is a few lines of test here and a
// flaky experiment against a live terminal otherwise.
#ifndef QTTY_TERM_CAPS_H
#define QTTY_TERM_CAPS_H

#include <QByteArray>
#include <QHash>
#include <QRgb>
#include <QVector>
#include <QSize>

namespace Qtty {

struct TermCaps {
	bool answered = false;      // the DA1 reply arrived -- see caps_complete()
	bool kitty = false;         // answered the kitty graphics query with OK
	bool sixel = false;         // listed attribute 4 in its device attributes
	bool truecolor = false;     // XTGETTCAP confirmed RGB or Tc
	bool bg_known = false;
	unsigned char bg[3] = {0, 0, 0};
	QSize cell_px;              // one cell in pixels, invalid until reported
	QSize text_px;              // the text area in pixels, invalid until reported

	// What DECRQM said about each private mode asked about, keyed by mode
	// number. DECRPM's values: 0 not recognised, 1 set, 2 reset, 3 and 4 the
	// permanent forms. An absent key means the terminal did not answer at
	// all, which is a different thing from answering 0 and must stay so --
	// silence is no information, and 0 is a definite no.
	QHash<int, int> dec_modes;

	// The terminal's low sixteen palette entries, from OSC 4, or empty. Only
	// 0..15 are asked for: 16..255 are a formula every terminal shares, so
	// asking would cost 240 round trips to learn what is already known.
	QVector<QRgb> palette16;
};

// Tri-state: -1 when the terminal said nothing about this mode, otherwise the
// DECRPM value. Named rather than inlined because the distinction between
// "unknown" and "not recognised" is the whole point and is easy to lose.
int dec_mode(const TermCaps &caps, int mode);

// Whether a mode may be relied on, under this file's asymmetry rule. Silence
// leaves the caller's own belief alone -- it learned nothing -- and only a
// definite 0 turns a capability off.
bool mode_usable(const TermCaps &caps, int mode, bool assumed);

// Inside tmux, everything below is answered by TMUX rather than by the
// terminal, and anything written for the terminal is swallowed. $TMUX is set
// by tmux for its own children and is not inherited across ssh into somewhere
// else, which makes it the one environment variable here that does not lie.
bool inside_tmux();

// Wrap a payload so tmux forwards it to the terminal underneath: DCS tmux ;
// <payload, every ESC doubled> ST. Requires `set -g allow-passthrough on`,
// and degrades correctly without it -- tmux eats the wrapper, the terminal
// never sees the query, nothing answers, and the negotiation concludes no
// graphics. Which is the right answer for a tmux that will not pass them.
QByteArray tmux_wrap(const QByteArray &payload);

// The batched query. One write rather than five, because five sequential
// probes cost five round trips before the first frame and over ssh that is
// the whole of a visible startup delay.
QByteArray caps_query();

// The private modes caps_query() asks DECRQM about, derived from the query
// itself rather than listed a second time. A tool reporting per-probe results
// needs to know what was asked, and the first version of that report carried
// its own list which already disagreed: it named 1002, which is set at
// startup but never queried, so the report said "silent" about a question
// nobody sent -- one edit from being sent to a terminal's author as their
// defect. Two lists of one thing drift; this one drifted before it was run.
QVector<int> queried_modes();

// Additive: fields are only ever set, never cleared, so this may be called
// again as more bytes arrive and again later for a reply that turns up during
// normal input. Feeding it one sequence or a whole buffer is the same thing.
void scan_caps(const QByteArray &buf, TermCaps &out);

// True once the device-attributes reply is present. That reply is the fence:
// every terminal worth the name answers it, so its arrival means a missing
// kitty or colour reply is a real "no" rather than a slow one.
bool caps_complete(const QByteArray &buf);

// Write the query to out_fd and gather what comes back on in_fd, for at most
// timeout_ms. Takes descriptors rather than reaching for 0 and 1 so that a
// test can drive it over a socketpair -- the same reason the parser is split
// from the I/O, one layer out.
//
// Polled in slices and rescanned after each chunk, because the replies are not
// guaranteed to arrive together and over ssh routinely do not: stopping at the
// first readable chunk would report "no graphics" for a terminal whose answer
// merely got split. Returns as soon as the fence arrives.
//
// A timeout is not a failure. Whatever did arrive still counts -- a terminal
// that answered the graphics query and nothing else is one we can draw on.
// `raw`, when given, receives every byte that arrived. The parse deliberately
// forgets whether a capability was DECLINED or merely unanswered -- for most
// of them the two mean the same thing to a caller -- but the difference is
// exactly what a terminal implementer needs when checking that switching a
// feature off silences the ANSWER rather than only the behaviour. A terminal
// that still replies while ignoring the payload is worse than one that never
// claimed the feature, because the reply is then cached as a lie.
//
// `typed`, when given, receives the TYPE-AHEAD: bytes that arrived in the same
// read and are not part of any reply. Until this existed they were read into
// the scan buffer and dropped, so a key pressed before a qtty program had
// drawn was a key nobody ever saw -- measured on a pseudo-terminal, a byte
// written before the child started never reached the widget while the same
// byte written 300 ms later did.
//
// Everything that is not part of an escape sequence is kept, and escapes are
// dropped whether or not they are recognised. Keeping an unrecognised one
// would mean a terminal's reply -- or half of one, split across two reads --
// reaching the application as input, which types garbage into somebody's
// document; dropping it costs a typed arrow or function key in the few
// milliseconds of the query. The smaller loss, and stated rather than hidden.
//
// This started narrower, keeping only the bytes before the first ESC, which
// left the case it exists for: over a slow link the reply window is longest,
// which is exactly when somebody types ahead, and their keys arrive
// interleaved with the answers rather than in front of them.
TermCaps collect_caps(int in_fd, int out_fd, int timeout_ms,
                      QByteArray *raw = nullptr, QByteArray *typed = nullptr);

} // namespace Qtty

#endif // QTTY_TERM_CAPS_H
