#pragma once
// qtty_core.h — shared spike infrastructure, extracted from spike.cpp
#include <QtWidgets>
#include <QPaintEngine>
#include <QPaintDevice>
#include <QTextItem>
#include <cstdio>

static int CW = 8, CH = 16;   // measured at startup

// ---------------------------------------------------------------- CellBuffer
struct Cell { QString ch = QStringLiteral(" "); bool rev = false; bool bold = false; };

class CellBuffer {
public:
    CellBuffer(int cols, int rows) : c_(cols), r_(rows), d_(cols * rows) {}
    int cols() const { return c_; } int rows() const { return r_; }
    Cell &at(int x, int y) { static Cell junk; if (x<0||y<0||x>=c_||y>=r_) return junk; return d_[y*c_+x]; }
    void fill(const QRect &r, const Cell &v) {
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) at(x,y) = v;
    }
    void text(int x, int y, const QString &s, bool rev=false, bool bold=false) {
        for (int i = 0; i < s.size(); ++i) { Cell &c = at(x+i,y); c.ch = QString(s[i]); c.rev=rev; c.bold=bold; }
    }
    QString toText() const {
        QString out;
        for (int y = 0; y < r_; ++y) {
            QString line;
            for (int x = 0; x < c_; ++x) line += d_[y*c_+x].ch;
            while (line.endsWith(' ')) line.chop(1);
            out += line + '\n';
        }
        return out;
    }
private:
    int c_, r_; QVector<Cell> d_;
};

// ------------------------------------------------- CellPaintDevice / Engine
class CellPaintEngine;

struct CellImage {                       // §5.7 cell-anchored placement
    quint64 key;                         // QPixmap::cacheKey() → upload-once identity
    QRect   cellRect;                    // anchor + span, in cells
    QPixmap pixmap;                      // pixel source (spike keeps it inline)
};

class CellPaintDevice : public QPaintDevice {
public:
    CellPaintDevice(CellBuffer &b);
    ~CellPaintDevice() override;
    QPaintEngine *paintEngine() const override;
    int metric(PaintDeviceMetric m) const override;
    CellBuffer &buffer() const { return buf_; }
    QPoint origin;                       // widget-space offset, in pixels
    mutable QVector<CellImage> placements;   // collected by the engine per frame
private:
    CellBuffer &buf_;
    CellPaintEngine *eng_;
};

class CellPaintEngine : public QPaintEngine {
public:
    // Declare a rich feature set so QPainter does NOT emulate text as paths.
    CellPaintEngine()
        : QPaintEngine(QPaintEngine::AllFeatures) {}

    bool begin(QPaintDevice *pdev) override { dev_ = static_cast<CellPaintDevice*>(pdev); return true; }
    bool end() override { dev_ = nullptr; return true; }
    Type type() const override { return QPaintEngine::User; }

    void updateState(const QPaintEngineState &s) override {
        if (s.state() & DirtyPen)   pen_   = s.pen();
        if (s.state() & DirtyBrush) brush_ = s.brush();
        if (s.state() & DirtyFont)  font_  = s.font();
        if (s.state() & DirtyTransform) xf_ = s.transform();
        if (s.state() & DirtyClipRegion) { clip_ = s.clipRegion(); hasClip_ = true; }
        if (s.state() & DirtyClipPath)   { clip_ = QRegion(s.clipPath().boundingRect().toRect()); hasClip_ = true; }
    }

    void drawTextItem(const QPointF &p, const QTextItem &ti) override {
        ++textCalls;
        QPointF q = xf_.map(p) + QPointF(dev_->origin);
        QFontMetricsF fm(ti.font());
        int col = qRound(q.x() / CW);
        int row = qRound((q.y() - fm.ascent()) / CH);
        dev_->buffer().text(col, row, ti.text(), false, ti.font().bold());
    }

    void drawRects(const QRectF *rects, int n) override { for (int i=0;i<n;++i) fill(rects[i]); }
    void drawRects(const QRect  *rects, int n) override { for (int i=0;i<n;++i) fill(QRectF(rects[i])); }

    void drawLines(const QLineF *l, int n) override { ++lineCalls; for (int i=0;i<n;++i) line(l[i]); }
    void drawLines(const QLine  *l, int n) override { ++lineCalls; for (int i=0;i<n;++i) line(QLineF(l[i])); }

    void drawPath(const QPainterPath &path) override {
        ++pathCalls;
        // If text ever arrives here, feature declaration failed. Track it.
        fill(path.boundingRect(), /*outlineOnly=*/true);
    }
    void drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &) override {
        ++pixmapCalls;
        QRect c = toCells(r);
        if (!c.isValid()) return;
        if (c.width() >= 2 && c.height() >= 2)       // §5.7: real image → placement
            dev_->placements.append({pm.cacheKey(), c, pm});
        else                                          // tiny icon → glyph substitution
            dev_->buffer().text(c.left(), c.top(), QStringLiteral("▒"));
    }
    void drawPolygon(const QPointF *pts, int n, PolygonDrawMode) override {
        QPolygonF p; for (int i=0;i<n;++i) p << pts[i];
        fill(p.boundingRect(), true);
    }

    int textCalls = 0, lineCalls = 0, pathCalls = 0, pixmapCalls = 0, fillCalls = 0;
    CellPaintDevice *device() const { return dev_; }

private:
    QRect toCells(const QRectF &r) const {
        QRectF m = xf_.mapRect(r).translated(dev_->origin);
        return QRect(qRound(m.left()/CW), qRound(m.top()/CH),
                     qMax(1, qRound(m.width()/CW)), qMax(1, qRound(m.height()/CH)));
    }
    void fill(const QRectF &r, bool outlineOnly = false) {
        ++fillCalls;
        QRect c = toCells(r);
        if (!c.isValid() || c.width() > 400 || c.height() > 200) return;
        if (outlineOnly || brush_.style() == Qt::NoBrush) { box(c); return; }
        // Solid fill: only paint background-ish fills as blanks, don't erase text.
        Cell blank; blank.ch = QStringLiteral(" ");
        if (c.width() > 1 && c.height() > 1) dev_->buffer().fill(c, blank);
    }
    void box(const QRect &c) {
        if (c.width() < 2 || c.height() < 2) return;
        CellBuffer &b = dev_->buffer();
        for (int x = c.left()+1; x < c.right(); ++x) { b.at(x,c.top()).ch = "─"; b.at(x,c.bottom()).ch = "─"; }
        for (int y = c.top()+1; y < c.bottom(); ++y) { b.at(c.left(),y).ch = "│"; b.at(c.right(),y).ch = "│"; }
        b.at(c.left(),c.top()).ch="┌"; b.at(c.right(),c.top()).ch="┐";
        b.at(c.left(),c.bottom()).ch="└"; b.at(c.right(),c.bottom()).ch="┘";
    }
    void line(const QLineF &l) {
        QLineF m(xf_.map(l.p1()) + QPointF(dev_->origin), xf_.map(l.p2()) + QPointF(dev_->origin));
        CellBuffer &b = dev_->buffer();
        if (qAbs(m.dy()) < CH/2.0) {           // horizontal
            int y = qRound(m.y1()/CH);
            for (int x = qRound(qMin(m.x1(),m.x2())/CW); x <= qRound(qMax(m.x1(),m.x2())/CW); ++x)
                if (b.at(x,y).ch == " ") b.at(x,y).ch = "─";
        } else if (qAbs(m.dx()) < CW/2.0) {    // vertical
            int x = qRound(m.x1()/CW);
            for (int y = qRound(qMin(m.y1(),m.y2())/CH); y <= qRound(qMax(m.y1(),m.y2())/CH); ++y)
                if (b.at(x,y).ch == " ") b.at(x,y).ch = "│";
        }
    }
    CellPaintDevice *dev_ = nullptr;
    QPen pen_; QBrush brush_; QFont font_; QTransform xf_; QRegion clip_; bool hasClip_ = false;
};

CellPaintDevice::CellPaintDevice(CellBuffer &b) : buf_(b), eng_(new CellPaintEngine) {}
CellPaintDevice::~CellPaintDevice() { delete eng_; }
QPaintEngine *CellPaintDevice::paintEngine() const { return eng_; }
int CellPaintDevice::metric(PaintDeviceMetric m) const {
    switch (m) {
    case PdmWidth:              return buf_.cols() * CW;
    case PdmHeight:             return buf_.rows() * CH;
    case PdmWidthMM:            return buf_.cols() * CW / 4;
    case PdmHeightMM:           return buf_.rows() * CH / 4;
    case PdmNumColors:          return 256;
    case PdmDepth:              return 24;
    case PdmDpiX: case PdmPhysicalDpiX: return 96;
    case PdmDpiY: case PdmPhysicalDpiY: return 96;
    case PdmDevicePixelRatio:   return 1;
    case PdmDevicePixelRatioScaled: return 1 * QPaintDevice::devicePixelRatioFScale();
    default: return 0;
    }
}

// ------------------------------------------------------------------ GridStyle
class GridStyle : public QProxyStyle {
public:
    GridStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    int pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const override {
        switch (m) {
        case PM_LayoutLeftMargin: case PM_LayoutRightMargin:  return CW;
        case PM_LayoutTopMargin:  case PM_LayoutBottomMargin: return CH;
        case PM_LayoutHorizontalSpacing: return CW;
        case PM_LayoutVerticalSpacing:   return 0;
        case PM_ScrollBarExtent:         return CW;
        case PM_DefaultFrameWidth:       return CW;
        case PM_ButtonMargin:            return CW;
        case PM_FocusFrameHMargin: case PM_FocusFrameVMargin: return 0;
        case PM_MenuPanelWidth: case PM_MenuBarPanelWidth:    return CW;
        case PM_IndicatorWidth:  return 3*CW;
        case PM_IndicatorHeight: return CH;
        case PM_ExclusiveIndicatorWidth:  return 3*CW;
        case PM_ExclusiveIndicatorHeight: return CH;
        default: {
            int v = QProxyStyle::pixelMetric(m, o, w);
            return v; // deliberately unsnapped here; see findings
        }}
    }
    QSize sizeFromContents(ContentsType t, const QStyleOption *o, const QSize &cs,
                           const QWidget *w) const override {
        QSize s = QProxyStyle::sizeFromContents(t, o, cs, w);
        return QSize(((s.width()+CW-1)/CW)*CW, ((s.height()+CH-1)/CH)*CH);
    }
    // Channel A: semantic drawing when the target is a cell device.
    static CellPaintDevice *cellTarget(QPainter *p) {
        // NOT p->device(): during a paintEvent the device is the QWidget itself.
        // The engine is ours regardless of redirection.
        if (auto *e = dynamic_cast<CellPaintEngine*>(p->paintEngine())) return e->device();
        return nullptr;
    }
    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                       const QWidget *w) const override {
        if (auto *dev = cellTarget(p)) {
            QRect c = cells(opt->rect, p, dev, w);
            switch (pe) {
            case PE_IndicatorCheckBox:
                dev->buffer().text(c.left(), c.top(),
                    (opt->state & State_On) ? "[x]" : "[ ]");
                ++chanA; return;
            case PE_IndicatorRadioButton:
                dev->buffer().text(c.left(), c.top(),
                    (opt->state & State_On) ? "(o)" : "( )");
                ++chanA; return;
            case PE_FrameWindow: case PE_Frame: case PE_FrameGroupBox:
            case PE_PanelMenu:   case PE_FrameMenu:
                drawBox(dev->buffer(), c); ++chanA; return;
            default: break;
            }
        }
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }
    void drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                     const QWidget *w) const override {
        if (auto *dev = cellTarget(p)) {
            QRect c = cells(opt->rect, p, dev, w);
            if (ce == CE_PushButtonBevel || ce == CE_PushButtonLabel) {
                if (auto *b = qstyleoption_cast<const QStyleOptionButton*>(opt)) {
                    if (ce == CE_PushButtonLabel) {
                        QRect bw = w ? cells(w->rect(), p, dev, w) : c;
                        QString t = "<" + b->text + ">";
                        c = bw;
                        extern const QWidget *g_qttyFocus;
                        bool foc = (opt->state & State_HasFocus) || (w && w == g_qttyFocus);
                        dev->buffer().text(c.left(), c.top(), t, foc);
                        ++chanA;
                    }
                    return;
                }
            }
        }
        QProxyStyle::drawControl(ce, opt, p, w);
    }
    static int chanA;
private:
    static QRect cells(const QRect &r, QPainter *p, CellPaintDevice *dev,
                       const QWidget *w) {
        // Neither transform() nor combinedTransform() carries the redirection
        // offset applied by QWidget::render(). Map through the widget instead.
        QPoint tl = w ? w->mapTo(w->window(), r.topLeft())
                      : p->combinedTransform().map(r.topLeft());
        tl += dev->origin;
        return QRect(qRound(tl.x()/double(CW)), qRound(tl.y()/double(CH)),
                     qMax(1,qRound(r.width()/double(CW))), qMax(1,qRound(r.height()/double(CH))));
    }
    static void drawBox(CellBuffer &b, const QRect &c) {
        if (c.width() < 2 || c.height() < 2) return;
        for (int x=c.left()+1;x<c.right();++x){ b.at(x,c.top()).ch="─"; b.at(x,c.bottom()).ch="─"; }
        for (int y=c.top()+1;y<c.bottom();++y){ b.at(c.left(),y).ch="│"; b.at(c.right(),y).ch="│"; }
        b.at(c.left(),c.top()).ch="┌"; b.at(c.right(),c.top()).ch="┐";
        b.at(c.left(),c.bottom()).ch="└"; b.at(c.right(),c.bottom()).ch="┘";
    }
};
int GridStyle::chanA = 0;
inline const QWidget *g_qttyFocus = nullptr;   // InputRouter-owned focus (F4)

// ------------------------------------------------------------------- helpers
static void renderWidget(QWidget *w, CellBuffer &buf, QPoint originPx = QPoint()) {
    CellPaintDevice dev(buf);
    dev.origin = originPx;
    QPainter p(&dev);
    w->render(&p, QPoint(), QRegion(),
              QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
    p.end();
}

static void hr(const char *t) { printf("\n\033[1m=== %s ===\033[0m\n", t); }

