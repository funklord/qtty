// src/runtime/application.cpp — L6 entry points (§5.6), wired to the real
// §5 architecture: AnsiBackend -> InputRouter -> Compositor -> FrameScheduler.
#include "qtty/application.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/runtime.h"
#include "qtty/theme.h"
#include "../backends/ansi/ansibackend.h"
#include <QtWidgets>

namespace Qtty {

void prepareEnvironment() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
}

void setup(QApplication &app) {
    // Bundled-font provisioning (§5.3) is later Phase-2 work; DejaVu Sans
    // Mono is the interim source of integral metrics, asserted as designed.
    QFont f(QStringLiteral("DejaVu Sans Mono"));
    f.setPixelSize(16);
    QFontMetrics fm(f);
    const int cw = fm.horizontalAdvance(u'M'), ch = fm.height();
    Q_ASSERT_X(cw > 0 && fm.horizontalAdvance(u'i') == cw, "Qtty::setup",
               "monospace font with integral metrics required");
    GridMetrics::set(cw, ch);
    app.setFont(f);
    app.setStyle(new GridStyle);
    setTheme(CellTheme::terminalDefault());
}

void renderOnce(QWidget &win, CellBuffer &buf, QVector<CellImage> *placements) {
    CellPaintDevice dev(buf);
    QPainter p(&dev);
    win.render(&p, QPoint(), QRegion(),
               QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
    p.end();
    buf.images = dev.placements;
    if (placements) *placements = dev.placements;
}

static bool s_tuiActive = false;
bool isTuiActive() { return s_tuiActive; }

int exec(QApplication &app, QWidget &win) {
    s_tuiActive = true;
    AnsiBackend backend;

    const QSize cells = backend.size();
    win.setAttribute(Qt::WA_DontShowOnScreen);
    win.resize(cells.width() * GridMetrics::cw(), cells.height() * GridMetrics::ch());
    win.show();
    QCoreApplication::processEvents();
    setFocusWidget(win.focusWidget());

    InputRouter router(&win);
    Compositor compositor(&win, &router);
    FrameScheduler scheduler(&backend, &compositor, &win);
    backend.setEventSink(&router);
    router.frameRequested = [&scheduler] { scheduler.requestFrame(); };

    scheduler.renderNow();                      // initial frame
    const int rc = app.exec();
    s_tuiActive = false;
    return rc;
}

} // namespace Qtty
