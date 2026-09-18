// qtty/application.h -- L6 runtime entry points (section 5.6).
//
// Canonical usage (see example/chat/main.cpp):
//     decide frontend
//       -> Qtty::prepare_environment()   BEFORE QApplication
//       -> QApplication ctor
//       -> Qtty::setup(app)             BEFORE any widget (font + style)
//       -> construct shared widgets
//       -> Qtty::exec(app, win)  /  win.show(); app.exec()
#pragma once
#include <QWidget>
#include <functional>
#include "cell.h"
#include "backend.h"

class QApplication;

namespace Qtty {

// Selects the offscreen platform and pins scaling. MUST precede QApplication.
void prepare_environment();

// Installs the cell-metric font and GridStyle. MUST precede widget
// construction: the shared UI derives its metrics from the application font.
//
// WHAT "MUST" MEANS HERE, measured rather than asserted, because the font
// enforcer (8.180) covers more of this than the rule implies. A window of
// ordinary widgets built BEFORE this call renders byte-for-byte the same as
// one built after: the enforcer puts the grid font back on every widget, and
// a layout measures at layout time, not at construction.
//
// What it cannot put back is a number somebody already wrote down. A widget
// that measures in its CONSTRUCTOR --
// `setFixedHeight(QFontMetrics(font()).height() * 2)` -- gets whatever font
// the application had at that moment. Measured: 17 px a row before this call
// against the grid's 19, so the widget fixes itself at 34 px where two rows
// are 38, and no later font change moves it. The rule is therefore real and
// its reason is narrow: call this first, and a constructor that measures is
// measuring the grid.
void setup(QApplication &app);

// Write out any diagnostics that were held back while qtty owned the terminal,
// and stop holding them. setup() installs a message handler that buffers
// instead of writing when stderr is a terminal, because that terminal is the
// one the frame is on: a qWarning lands in the middle of the drawing, and
// nothing repaints over it because the cell plane never changed.
//
// It is called for you when the backend gives the terminal back. An
// application that takes the screen some other way can call it itself.
void flush_deferred_messages();

// Run `win` on `backend` until quit. The backend is L1's seam (section 5.1):
// a legacy adapter, termpaint, or NullBackend under test all drive the same
// runtime through it. This overload is what makes the seam real -- until it
// existed, exec() constructed an AnsiBackend on its own stack and no other
// backend could reach the runtime at all, which blocked Phase 1 and left the
// section 9 harness unable to use the backend it was specified in terms of.
int exec(QApplication &app, QWidget &win, ITerminalBackend &backend);

// Run `win` full-screen on the controlling terminal until quit. The
// convenience form of the above, on the built-in AnsiBackend.
int exec(QApplication &app, QWidget &win);

// Hand the terminal back for the duration of `body`, and take it again
// afterwards -- running an editor, a pager, or anything else that wants the
// screen. Returns false when there was no terminal to hand over, in which
// case `body` has still run: a program whose output is a pipe has nothing to
// suspend and its work should happen anyway.
//
// It exists because the pair it wraps could not be reached. backend.h has
// named this case from the start -- suspend() is documented as being for
// "SIGTSTP / shelling out" -- and both halves are pure virtuals on
// ITerminalBackend, which the public headers only ever CONSUME: exec() takes
// a backend and nothing hands one out, and the only concrete implementation
// an installed program gets is NullBackend, whose suspend() and resume() are
// empty. So the feature was implemented, carefully maintained, and callable
// by nobody.
//
// That is the same fault Qtty::capabilities() was added for, and this follows
// its answer rather than inventing one: a free function rather than a handle
// nobody is given.
//
// WHICH backend it acts on is where the two part company, and the difference
// is deliberate. This one asks who currently OWNS the terminal, because its
// return value is a claim that a screen was handed over and only ownership
// supports that claim -- a backend with no screen would make it a false one.
// capabilities() asks which backend is DRIVING the session first, and falls
// back to ownership; a terminal's cell size does not stop being true while a
// child has the screen. See 8.153: the two were one sentence in this comment
// for a while, and they are not one fact.
//
// Scoped rather than a suspend()/resume() pair, because the pair has a wrong
// way to use it and this does not -- the terminal comes back if `body`
// throws, and there is no way to forget the second call.
bool shell_out(const std::function<void()> &body);

// What the terminal qtty is driving turned out to be, as negotiated (section
// 5.7). Valid while a backend is driving -- under exec(), and equally in a
// frame loop an application runs itself, which has no exec() to ask and was
// answered "nothing known" until 8.153. A default-constructed Capabilities
// when neither is true, which reads as "nothing known" rather than as a
// claim.
//
// It exists because an application had no way to ask. Capabilities were
// reachable only through ITerminalBackend, and the convenience exec() builds
// its backend internally -- so every field on that struct was declared and
// unreachable from the seat an application sits in, which is section 7.4's
// own fault. The three fields the negotiation added made it three instances
// worse before this closed it.
//
// What an application does with the answer is real work rather than
// curiosity: Qtty::cells() needs cell_px to size an image without squashing
// it, and a lower graphics tier needs the background to composite against.
Capabilities capabilities();

// The keys that quit, for an application that uses exec() and therefore
// never sees the router. Ctrl-C and Ctrl-D by default; an empty list means
// no quit key at all, and the application is then responsible for offering
// a way out of its own.
//
// It exists because there was NO way to change them. exec() builds the
// router on its own stack and hands it to nobody, so
// InputRouter::set_quit_keys() -- which has always existed -- was reachable
// only by reimplementing the whole of exec(). project.md 0e carried that as
// the last of the adoption decisions, and its shape is taken from the
// sibling that was closed the same way: capabilities() and shell_out() are
// free functions that reach what exec() owns.
//
// PROCESS-WIDE AND BEFORE THE RUN, which is the difference from those two.
// An application decides its quit keys while building its window, long
// before any router exists, so this sets the default every router starts
// from rather than poking a live one -- and it also reaches the routers
// already running, so a call from inside a slot takes effect at once.
// InputRouter::set_quit_keys() still overrides it for one router.
//
// THE HAZARD THAT MADE THIS DELICATE IS GONE. While a vanished terminal was
// reported by synthesising Ctrl-D, an application able to redefine the quit
// keys could also stop being told its terminal had closed; 8.148 gave that
// its own seam, on_terminal_lost(), which nothing routes and no quit key
// can take away.
void set_quit_keys(const QVector<KeyEvent> &keys);

// True while a terminal session is being driven -- by exec(), or by an
// application's own frame loop, which is the seat 8.153 found answering
// false beside a backend holding the alternate screen. Overlay uses this to
// pick its rendering path (section 5.7); apps can branch on it for
// target-specific polish.
bool is_tui_active();

// Render one frame of `win` into `buf` (and collect section 5.7 placements when
// `placements` is non-null). Used by tests, tools, and custom frame loops.
void render_once(QWidget &win, CellBuffer &buf,
                QVector<CellImage> *placements = nullptr);

} // namespace Qtty
