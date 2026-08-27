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
};

// The batched query. One write rather than five, because five sequential
// probes cost five round trips before the first frame and over ssh that is
// the whole of a visible startup delay.
QByteArray caps_query();

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
TermCaps collect_caps(int in_fd, int out_fd, int timeout_ms);

} // namespace Qtty

#endif // QTTY_TERM_CAPS_H
