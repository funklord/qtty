// src/runtime/compositor.cpp — L6 composition (§5.4 step 3, §8.1) and
// FrameScheduler (§5.4 steps 1-4, simplified per §16.1 F9: full render + diff
// beats damage tracking at these sizes).
#include "qtty/runtime.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/application.h"
#include "qtty/graphics.h"
#include "qtty/overlay.h"
#include <QtWidgets>

namespace Qtty {

// ----------------------------------------------------------------- Compositor
Compositor::Compositor(QWidget *window, InputRouter *router)
    : win_(window), router_(router) {}

void Compositor::compose(CellBuffer &out) {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    out.images.clear();

    CellPaintDevice dev(out);
    {   // main window at origin
        QPainter p(&dev);
        win_->render(&p, QPoint(), QRegion(),
                     QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
        p.end();
    }
    // popup stack in z-order, clamped to the terminal rect (§8.1: a menu
    // opening at the right edge must flip inside)
    const auto popups = router_ ? router_->popups() : QVector<QWidget *>{};
    for (QWidget *pop : popups) {
        QRect g = pop->geometry();
        int maxX = out.cols() * cw - g.width();
        int maxY = out.rows() * ch - g.height();
        QPoint pos(qBound(0, g.x(), qMax(0, maxX)), qBound(0, g.y(), qMax(0, maxY)));
        // snap the clamp to the grid
        pos = QPoint((pos.x() / cw) * cw, (pos.y() / ch) * ch);
        if (pos != g.topLeft()) pop->move(pos);
        dev.origin = pos;
        QPainter p(&dev);
        pop->render(&p, QPoint(), QRegion(),
                    QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
        p.end();
    }
    dev.origin = QPoint();
    out.images = dev.placements;

    // hardware cursor from the focus widget (§5.5)
    cursor_.reset();
    if (QWidget *fw = win_->focusWidget()) {
        QVariant v = fw->inputMethodQuery(Qt::ImCursorRectangle);
        if (v.isValid()) {
            QPoint g = fw->mapTo(win_, v.toRect().topLeft());
            QPoint cell(g.x() / cw, g.y() / ch);
            if (cell.x() >= 0 && cell.y() >= 0
                && cell.x() < out.cols() && cell.y() < out.rows())
                cursor_ = cell;
        }
    }
}

std::optional<QPoint> Compositor::cursorCell() const { return cursor_; }

// ------------------------------------------------------------- FrameScheduler
FrameScheduler::FrameScheduler(ITerminalBackend *backend, Compositor *compositor,
                               QWidget *window)
    : backend_(backend), comp_(compositor), win_(window) {
    coalesce_.setSingleShot(true);
    coalesce_.setInterval(0);
    QObject::connect(&coalesce_, &QTimer::timeout, this, [this] { renderNow(); });
    idle_.setInterval(100);                       // catches timer-driven updates
    QObject::connect(&idle_, &QTimer::timeout, this, [this] {
        if (win_->isVisible()) requestFrame();
    });
    idle_.start();
    sinceLast_.start();
    qApp->installEventFilter(this);
}

bool FrameScheduler::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::UpdateRequest || e->type() == QEvent::LayoutRequest)
        if (qobject_cast<QWidget *>(o)) requestFrame();
    return false;
}

void FrameScheduler::requestFrame() {
    // 16 ms local budget (§11); coalesce bursts into one frame.
    const int wait = qMax(0, 16 - int(sinceLast_.elapsed()));
    if (!coalesce_.isActive()) coalesce_.start(wait);
}

void FrameScheduler::renderNow() {
    const QSize cells = backend_->size();
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    CellBuffer frame(cells.width(), cells.height());
    comp_->compose(frame);

    // Overlay tier selection (§5.7, §17.3).
    const auto overlays = Overlay::visibleOverlays();
    const auto gmode = backend_->capabilities().graphics;
    auto *gfx = dynamic_cast<IGraphicsOutput *>(backend_);

    auto overlayCellRect = [&](Overlay *o) {
        return o->cellRect().isNull()
            ? QRect(0, 0, frame.cols(), frame.rows())
            : QRect(int(o->cellRect().x()), int(o->cellRect().y()),
                    int(o->cellRect().width()), int(o->cellRect().height()));
    };

    const bool softwareComposite = !overlays.isEmpty() && gfx
        && (gmode == Capabilities::Sixel || gmode == Capabilities::ITerm2
            || gmode == Capabilities::Kitty);

    if (!overlays.isEmpty() && !softwareComposite && gmode != Capabilities::KittyAlpha)
        for (Overlay *o : overlays)                       // fallback: half-blocks
            composeHalfblocks(frame, o->image(), overlayCellRect(o));

    QRegion damage = prev_ ? frame.diff(*prev_)
                           : QRegion(0, 0, frame.cols(), frame.rows());
    const bool imagesChanged = !prev_ || frame.images.size() != prev_->images.size();

    if (softwareComposite) {
        // one finished picture: rasterise cells, blend placements + overlays
        QImage px = rasterize(frame, QGuiApplication::font());
        QPainter p(&px);
        for (const CellImage &ci : frame.images)
            p.drawPixmap(ci.cellRect.x() * cw, ci.cellRect.y() * ch, ci.pixmap);
        for (Overlay *o : overlays) {
            const QRect r = overlayCellRect(o);
            p.drawImage(QRect(r.x() * cw, r.y() * ch, r.width() * cw, r.height() * ch),
                        o->image());
        }
        p.end();
        gfx->presentPixels(px, QRegion(0, 0, frame.cols(), frame.rows()));
    } else if (!damage.isEmpty() || imagesChanged || !prev_ || !overlays.isEmpty()) {
        backend_->present(frame, damage);
        if (gmode == Capabilities::KittyAlpha && gfx) {   // terminal-blended alpha
            int id = 0;
            for (Overlay *o : overlays)
                gfx->presentOverlay(id++, o->image(),
                                    overlayCellRect(o).topLeft(),
                                    qMax(1, o->z()));
        }
    }
    backend_->setCursor(comp_->cursorCell(),
                        comp_->cursorCell() ? CursorShape::Bar : CursorShape::Hidden);
    prev_ = std::make_unique<CellBuffer>(frame);
    sinceLast_.restart();
}

} // namespace Qtty
