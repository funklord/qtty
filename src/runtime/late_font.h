// src/runtime/late_font.h -- applying a font chosen after setup().
//
// INTERNAL. Not shipped in include/qtty/, because no application needs it and
// a header that leaves the tree is a promise about its contents.
//
// `Qtty::set_font()` lives in the grid layer with the two statics it writes,
// and until 8.516 a call after setup() stored a family nobody would ask for
// again -- setup() being the only reader. It warned and stopped there (8.414),
// with the other half recorded in project.md section 0b as the holder's: cw
// and ch size every widget, so making a late call WORK means re-measuring and
// then re-laying every window.
//
// The work has to happen in the runtime rather than in the grid, for two
// reasons that are not layering taste. `FontEnforcer` is file-local to
// application.cpp and holds a COPY of the font -- deliberately, since reading
// QApplication::font() would adopt whatever the desktop theme last set, which
// is the thing the enforcer exists to resist -- so a late change has to reach
// it where it lives. And re-laying the windows is `InputRouter::on_resize()`,
// which already knows the one rule that matters: a modal is centred and a
// popup is anchored, so neither is resized to the terminal.
//
// Two functions rather than one because they sit in different files, and the
// second is written as a fan-out over the live routers for the same reason
// `set_quit_keys()` is: a terminal session may have several, and "the
// metrics moved" is a property of the process rather than of whichever
// router happens to be last.
#pragma once
#include <QSize>

namespace Qtty {

// Re-measure from the statics `set_font()` just wrote, and install the
// result: GridMetrics, the application font, and FontEnforcer's base.
//
// FALSE when the font is refused, and then nothing has changed. setup() ends
// a bad font with qFatal, which is right at startup -- a grid that cannot be
// laid is not a program that should carry on. It is wrong here: a font picker
// in a preferences dialog must not be able to kill a running application, so
// the late path keeps the grid it has and says why. The caller puts the
// statics back, so a refused call leaves nothing of itself behind.
bool apply_font_now();

// Whether setup() has run, which is the question "is this call late?" asks.
// 8.414 asked is_tui_active() instead, and 8.512 showed that to be a
// different question: a program driving its own frame loop through
// render_once() has no backend, so that flag is false while setup() has very
// much run -- and a font chosen then went back to being stored and ignored
// in silence, which is the defect 8.414 set out to end.
bool setup_has_run();

// Every live router re-lays its windows for `cells`, which is what makes a
// changed cell size reach the screen. The cell COUNT has not moved -- the
// terminal is the size it was -- and on_resize() multiplies it by the cell
// that GridMetrics now reports, so this is the resize qtty already does.
void relayout_live_routers(QSize cells);

} // namespace Qtty
