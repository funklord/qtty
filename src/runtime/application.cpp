// src/runtime/application.cpp — L6 entry points (§5.6).
#include "qtty/application.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "../backends/ansi/ansiruntime.h"
#include <QtWidgets>

namespace qtty {

void prepareEnvironment() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
}

void setup(QApplication &app) {
    // Bundled-font provisioning (§5.3) is Phase 2; DejaVu Sans Mono is the
    // interim source of integral metrics, asserted below as designed.
    QFont f(QStringLiteral("DejaVu Sans Mono"));
    f.setPixelSize(16);
    QFontMetrics fm(f);
    const int cw = fm.horizontalAdvance(u'M'), ch = fm.height();
    Q_ASSERT_X(cw > 0 && fm.horizontalAdvance(u'i') == cw, "qtty::setup",
               "monospace font with integral metrics required");
    GridMetrics::set(cw, ch);
    app.setFont(f);
    app.setStyle(new GridStyle);
}

void renderOnce(QWidget &win, CellBuffer &buf, QVector<CellImage> *placements) {
    CellPaintDevice dev(buf);
    QPainter p(&dev);
    win.render(&p, QPoint(), QRegion(),
               QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
    p.end();
    if (placements) *placements = dev.placements;
}

int exec(QApplication &app, QWidget &win) {
    detail::AnsiRuntime rt(&win);
    return app.exec();
}

} // namespace qtty
