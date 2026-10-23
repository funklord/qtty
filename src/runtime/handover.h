// src/runtime/handover.h -- what a handover ends that nothing else would.
//
// INTERNAL. Not shipped in include/qtty/, because no application needs it and
// a header that leaves the tree is a promise about its contents.
//
// A press sets InputRouter's grab and only a release clears it. A handover
// takes the release away: kLeave turns mouse reporting off, so a button held
// while an editor or a shell has the terminal has its release delivered
// there, and every mouse event after the return is routed to the widget that
// was pressed until the next click's release happens to clear it.
//
// Section 0b carried this as a question and deferred it on a cost that had
// already stopped being real. It said the router cannot hear about a handover
// and named two candidates -- a public accessor on Compositor, or a non-pure
// on_handover() on ITerminalEventSink -- calling both "additions to a PUBLIC
// header for a one-click symptom". There is a third route and the tree
// already uses it: `relayout_live_routers()` in late_font.h fans a
// process-wide fact over the live routers from an internal header, which
// 8.516 established for a late font change. This is the same shape, so the
// public cost the row weighed does not arise.
//
// Called from Compositor::compose(), which already asks the backend for its
// handover count and compares it against a copy of its own -- so the news is
// where it is needed and nothing new has to notice it.
#pragma once

namespace Qtty {

// Every live router forgets a grab a handover has ended. A fan-out rather
// than a call on one router for the reason `set_quit_keys()` is one: a
// terminal session may have several, and "the terminal changed hands" is a
// property of the process rather than of whichever router happens to be last.
void forget_mouse_grabs();

} // namespace Qtty
