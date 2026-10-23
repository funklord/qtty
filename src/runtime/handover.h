// src/runtime/handover.h -- what a handover ends that nothing else would.
//
// INTERNAL. Not shipped in include/qtty/, because no application needs it and
// a header that leaves the tree is a promise about its contents.
//
// A press sets InputRouter's grab, its hover and its double-click window,
// and only a later mouse event clears each. A handover takes that event
// away: kLeave turns mouse reporting off, so a button held
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

// Every live router forgets the mouse state a handover has ended -- the grab,
// the hover and the double-click window -- and a drag in progress is
// cancelled. A fan-out rather than a call on one
// router for the reason `set_quit_keys()` is one: a terminal session may have
// several, and "the terminal changed hands" is a property of the process
// rather than of whichever router happens to be last.
//
// FOUR THINGS RATHER THAN ONE, because they are the same fault and the grab
// was simply the one section 0b had written down. Each is state a press
// starts and some later event ends, and the handover is where that later
// event goes to somebody else:
//
//   - the GRAB misroutes every motion and release to the pressed widget;
//   - the HOVER leaves a control painting its highlight, with nothing able
//     to correct it until the pointer moves to a DIFFERENT widget, since
//     update_hover() early-returns when it has not changed;
//   - a DRAG is the worst of the four, because exec_drag() runs a NESTED
//     event loop that only a release or Escape ends: a release delivered to
//     the shell leaves the application in a permanent drag, with every
//     motion going to drag_move_to() and no way out the user can see. It is
//     CANCELLED rather than dropped, the release having gone somewhere else
//     so that where the user meant to drop is not known;
//   - the DOUBLE-CLICK WINDOW misreports an action rather than a state. A
//     press, a shell_out() whose body returns in milliseconds, and one click
//     on the same cell arrive as a double click nobody made -- and Qt's own
//     arrangement has a double click REPLACE the press, so the real click is
//     not delivered at all.
void forget_mouse_state();

} // namespace Qtty
