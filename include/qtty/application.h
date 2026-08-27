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
#include "cell.h"
#include "backend.h"

class QApplication;

namespace Qtty {

// Selects the offscreen platform and pins scaling. MUST precede QApplication.
void prepare_environment();

// Installs the cell-metric font and GridStyle. MUST precede widget
// construction: the shared UI derives its metrics from the application font.
void setup(QApplication &app);

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

// True while exec() is driving a terminal session. Overlay uses this to pick
// its rendering path (section 5.7); apps can branch on it for target-specific polish.
bool is_tui_active();

// Render one frame of `win` into `buf` (and collect section 5.7 placements when
// `placements` is non-null). Used by tests, tools, and custom frame loops.
void render_once(QWidget &win, CellBuffer &buf,
                QVector<CellImage> *placements = nullptr);

} // namespace Qtty
