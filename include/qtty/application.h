// qtty/application.h -- L6 runtime entry points (section 5.6).
//
// Canonical usage (see example/chat/main.cpp):
//     decide frontend
//       -> Qtty::prepareEnvironment()   BEFORE QApplication
//       -> QApplication ctor
//       -> Qtty::setup(app)             BEFORE any widget (font + style)
//       -> construct shared widgets
//       -> Qtty::exec(app, win)  /  win.show(); app.exec()
#pragma once
#include <QWidget>
#include "cell.h"

class QApplication;

namespace Qtty {

// Selects the offscreen platform and pins scaling. MUST precede QApplication.
void prepareEnvironment();

// Installs the cell-metric font and GridStyle. MUST precede widget
// construction: the shared UI derives its metrics from the application font.
void setup(QApplication &app);

// Run `win` full-screen on the controlling terminal until quit.
int exec(QApplication &app, QWidget &win);

// True while exec() is driving a terminal session. Overlay uses this to pick
// its rendering path (section 5.7); apps can branch on it for target-specific polish.
bool isTuiActive();

// Render one frame of `win` into `buf` (and collect section 5.7 placements when
// `placements` is non-null). Used by tests, tools, and custom frame loops.
void renderOnce(QWidget &win, CellBuffer &buf,
	            QVector<CellImage> *placements = nullptr);

} // namespace Qtty
