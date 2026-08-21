// qtty/application.h — L6 runtime entry points (§5.6).
//
// Canonical usage (see examples/chat/main.cpp):
//     decide frontend
//       -> qtty::prepareEnvironment()   BEFORE QApplication
//       -> QApplication ctor
//       -> qtty::setup(app)             BEFORE any widget (font + style)
//       -> construct shared widgets
//       -> qtty::exec(app, win)  /  win.show(); app.exec()
#pragma once
#include <QWidget>
#include "cell.h"

class QApplication;

namespace qtty {

// Selects the offscreen platform and pins scaling. MUST precede QApplication.
void prepareEnvironment();

// Installs the cell-metric font and GridStyle. MUST precede widget
// construction: the shared UI derives its metrics from the application font.
void setup(QApplication &app);

// Run `win` full-screen on the controlling terminal until quit.
int exec(QApplication &app, QWidget &win);

// Render one frame of `win` into `buf` (and collect §5.7 placements when
// `placements` is non-null). Used by tests, tools, and custom frame loops.
void renderOnce(QWidget &win, CellBuffer &buf,
                QVector<CellImage> *placements = nullptr);

} // namespace qtty
