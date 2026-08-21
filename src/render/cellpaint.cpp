// src/render/cellpaint.cpp — CellPaintDevice / CellPaintEngine (§5.4).
#include "qtty/paint.h"
#include "qtty/grid.h"
#include "qtty/theme.h"
#include <QGuiApplication>
#include <QPalette>
#include <QPainterPath>
#include <QFontMetricsF>

namespace qtty {

CellPaintDevice::CellPaintDevice(CellBuffer &b) : buf_(b), eng_(new CellPaintEngine) {}
CellPaintDevice::~CellPaintDevice() { delete eng_; }
QPaintEngine *CellPaintDevice::paintEngine() const { return eng_; }

int CellPaintDevice::metric(PaintDeviceMetric m) const {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    switch (m) {
    case PdmWidth:                  return buf_.cols() * cw;
    case PdmHeight:                 return buf_.rows() * ch;
    case PdmWidthMM:                return buf_.cols() * cw / 4;
    case PdmHeightMM:               return buf_.rows() * ch / 4;
    case PdmNumColors:              return 256;
    case PdmDepth:                  return 24;
    case PdmDpiX: case PdmPhysicalDpiX: return 96;
    case PdmDpiY: case PdmPhysicalDpiY: return 96;
    case PdmDevicePixelRatio:       return 1;
    case PdmDevicePixelRatioScaled: return int(1 * QPaintDevice::devicePixelRatioFScale());
    default:                        return 0;
    }
}

bool CellPaintEngine::begin(QPaintDevice *pdev) {
    dev_ = static_cast<CellPaintDevice *>(pdev);
    return true;
}
bool CellPaintEngine::end() { dev_ = nullptr; return true; }

void CellPaintEngine::updateState(const QPaintEngineState &s) {
    if (s.state() & DirtyPen)       pen_ = s.pen();
    if (s.state() & DirtyBrush)     brush_ = s.brush();
    if (s.state() & DirtyFont)      font_ = s.font();
    if (s.state() & DirtyTransform) xf_ = s.transform();
}

QRect CellPaintEngine::toCells(const QRectF &r) const {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    QRectF m = xf_.mapRect(r).translated(dev_->origin);
    return QRect(qRound(m.left() / cw), qRound(m.top() / ch),
                 qMax(1, qRound(m.width() / cw)), qMax(1, qRound(m.height() / ch)));
}

// Text colour policy (§6): colours matching the app palette's standard text
// roles emit Color::Default so the terminal's own scheme applies; anything
// the app explicitly coloured passes through as Rgb.
static Color penToFg(const QPen &pen) {
    const QRgb c = pen.color().rgba();
    const QPalette &pal = QGuiApplication::palette();
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        if (pal.color(r).rgba() == c) return Color();
    return Color::rgb(c);
}

void CellPaintEngine::drawTextItem(const QPointF &p, const QTextItem &ti) {
    QPointF q = xf_.map(p) + QPointF(dev_->origin);
    QFontMetricsF fm(ti.font());
    int col = qRound(q.x() / GridMetrics::cw());
    int row = qRound((q.y() - fm.ascent()) / GridMetrics::ch());
    Attrs a;
    if (ti.font().bold()) a |= Attr::Bold;
    if (ti.font().italic()) a |= Attr::Italic;
    if (ti.font().underline()) a |= Attr::Underline;
    dev_->buffer().text(col, row, ti.text(), penToFg(pen_), Color(), a);
}

void CellPaintEngine::drawRects(const QRectF *r, int n) { for (int i = 0; i < n; ++i) fillRectF(r[i]); }
void CellPaintEngine::drawRects(const QRect *r, int n)  { for (int i = 0; i < n; ++i) fillRectF(QRectF(r[i])); }
void CellPaintEngine::drawLines(const QLineF *l, int n) { for (int i = 0; i < n; ++i) line(l[i]); }
void CellPaintEngine::drawLines(const QLine *l, int n)  { for (int i = 0; i < n; ++i) line(QLineF(l[i])); }

void CellPaintEngine::drawPath(const QPainterPath &path) {
    fillRectF(path.boundingRect(), /*outlineOnly=*/true);
}

void CellPaintEngine::drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &) {
    QRect c = toCells(r);
    if (!c.isValid()) return;
    if (c.width() >= 2 && c.height() >= 2)      // §5.7: real image → placement
        dev_->placements.append({pm.cacheKey(), c, pm});
    else                                        // tiny icon → glyph substitution (§8.6)
        dev_->buffer().text(c.left(), c.top(), QStringLiteral("▒"));
}

void CellPaintEngine::drawPolygon(const QPointF *pts, int n, PolygonDrawMode) {
    QPolygonF p;
    for (int i = 0; i < n; ++i) p << pts[i];
    fillRectF(p.boundingRect(), true);
}

void CellPaintEngine::fillRectF(const QRectF &r, bool outlineOnly) {
    QRect c = toCells(r);
    if (!c.isValid() || c.width() > 400 || c.height() > 200) return;
    if (outlineOnly || brush_.style() == Qt::NoBrush) { box(c); return; }
    if (c.width() > 1 && c.height() > 1) dev_->buffer().fill(c, Cell{});
}

void CellPaintEngine::box(const QRect &c) {
    if (c.width() < 2 || c.height() < 2) return;
    CellBuffer &b = dev_->buffer();
    for (int x = c.left() + 1; x < c.right(); ++x) {
        b.at(x, c.top()).ch = QStringLiteral("─");
        b.at(x, c.bottom()).ch = QStringLiteral("─");
    }
    for (int y = c.top() + 1; y < c.bottom(); ++y) {
        b.at(c.left(), y).ch = QStringLiteral("│");
        b.at(c.right(), y).ch = QStringLiteral("│");
    }
    b.at(c.left(), c.top()).ch = QStringLiteral("┌");
    b.at(c.right(), c.top()).ch = QStringLiteral("┐");
    b.at(c.left(), c.bottom()).ch = QStringLiteral("└");
    b.at(c.right(), c.bottom()).ch = QStringLiteral("┘");
}

void CellPaintEngine::line(const QLineF &l) {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    QLineF m(xf_.map(l.p1()) + QPointF(dev_->origin), xf_.map(l.p2()) + QPointF(dev_->origin));
    CellBuffer &b = dev_->buffer();
    if (qAbs(m.dy()) < ch / 2.0) {
        int y = qRound(m.y1() / ch);
        for (int x = qRound(qMin(m.x1(), m.x2()) / cw); x <= qRound(qMax(m.x1(), m.x2()) / cw); ++x)
            if (b.at(x, y).ch == QStringLiteral(" ")) b.at(x, y).ch = QStringLiteral("─");
    } else if (qAbs(m.dx()) < cw / 2.0) {
        int x = qRound(m.x1() / cw);
        for (int y = qRound(qMin(m.y1(), m.y2()) / ch); y <= qRound(qMax(m.y1(), m.y2()) / ch); ++y)
            if (b.at(x, y).ch == QStringLiteral(" ")) b.at(x, y).ch = QStringLiteral("│");
    }
}

} // namespace qtty
