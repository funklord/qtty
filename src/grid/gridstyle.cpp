// src/grid/gridstyle.cpp — GridMetrics, GridStyle, focus ownership (§5.3–§5.5).
#include "qtty/grid.h"
#include "qtty/paint.h"
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QPainter>
#include <QWidget>

namespace qtty {

static int s_cw = 8, s_ch = 16;
int GridMetrics::cw() { return s_cw; }
int GridMetrics::ch() { return s_ch; }
void GridMetrics::set(int cw, int ch) { s_cw = cw; s_ch = ch; }

static QWidget *s_focus = nullptr;
QWidget *focusWidget() { return s_focus; }
void setFocusWidget(QWidget *w) { s_focus = w; }

// Channel A target detection: via the paint ENGINE, never p->device() — inside
// a paintEvent the device is the QWidget itself (§16, F1).
static CellPaintDevice *cellTarget(QPainter *p) {
    if (auto *e = dynamic_cast<CellPaintEngine *>(p->paintEngine())) return e->device();
    return nullptr;
}

// Neither transform() nor combinedTransform() carries render()'s redirection
// offset — map through the widget (§16, F2).
static QRect cellsOf(const QRect &r, CellPaintDevice *dev, const QWidget *w) {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    QPoint tl = w ? w->mapTo(w->window(), r.topLeft()) : r.topLeft();
    tl += dev->origin;
    return QRect(qRound(tl.x() / double(cw)), qRound(tl.y() / double(ch)),
                 qMax(1, qRound(r.width() / double(cw))),
                 qMax(1, qRound(r.height() / double(ch))));
}

static void drawBox(CellBuffer &b, const QRect &c) {
    if (c.width() < 2 || c.height() < 2) return;
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

GridStyle::GridStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

int GridStyle::pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    switch (m) {
    case PM_LayoutLeftMargin: case PM_LayoutRightMargin:   return cw;
    case PM_LayoutTopMargin:  case PM_LayoutBottomMargin:  return ch;
    case PM_LayoutHorizontalSpacing:                       return cw;
    case PM_LayoutVerticalSpacing:                         return 0;
    case PM_ScrollBarExtent:                               return cw;
    case PM_DefaultFrameWidth:                             return cw;   // §16.3: must be cell-safe
    case PM_ButtonMargin:                                  return cw;
    case PM_FocusFrameHMargin: case PM_FocusFrameVMargin:  return 0;
    case PM_MenuPanelWidth: case PM_MenuBarPanelWidth:     return cw;
    case PM_IndicatorWidth:                                return 3 * cw;
    case PM_IndicatorHeight:                               return ch;
    case PM_ExclusiveIndicatorWidth:                       return 3 * cw;
    case PM_ExclusiveIndicatorHeight:                      return ch;
    // §17.1 audit — every metric that shapes geometry lands on the grid:
    case PM_SplitterWidth:                                 return cw;
    case PM_MenuHMargin: case PM_MenuVMargin:              return 0;
    case PM_MenuBarHMargin: case PM_MenuBarVMargin:        return 0;
    case PM_MenuBarItemSpacing:                            return 2 * cw;
    case PM_TabBarTabHSpace:                               return 2 * cw;
    case PM_TabBarTabVSpace:                               return 0;
    case PM_TabBarBaseHeight: case PM_TabBarBaseOverlap:   return 0;
    case PM_TabBarTabShiftHorizontal:
    case PM_TabBarTabShiftVertical:                        return 0;
    case PM_ProgressBarChunkWidth:                         return cw;
    case PM_SliderThickness: case PM_SliderControlThickness: return ch;
    case PM_SliderLength:                                  return 3 * cw;
    case PM_CheckBoxLabelSpacing:
    case PM_RadioButtonLabelSpacing:                       return cw;
    case PM_ToolBarItemMargin: case PM_ToolBarItemSpacing: return 0;
    case PM_ToolBarFrameWidth:                             return 0;
    case PM_DockWidgetSeparatorExtent:                     return cw;
    case PM_HeaderMargin:                                  return 0;
    case PM_HeaderGripMargin:                              return cw;
    default:                                               return QProxyStyle::pixelMetric(m, o, w);
    }
}

QSize GridStyle::sizeFromContents(ContentsType t, const QStyleOption *o, const QSize &cs,
                                  const QWidget *w) const {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    QSize s = QProxyStyle::sizeFromContents(t, o, cs, w);
    return QSize(((s.width() + cw - 1) / cw) * cw, ((s.height() + ch - 1) / ch) * ch);
}

void GridStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                              const QWidget *w) const {
    if (auto *dev = cellTarget(p)) {
        QRect c = cellsOf(opt->rect, dev, w);
        switch (pe) {
        case PE_IndicatorCheckBox:
            dev->buffer().text(c.left(), c.top(),
                (opt->state & State_On) ? QStringLiteral("[x]") : QStringLiteral("[ ]"));
            return;
        case PE_IndicatorRadioButton:
            dev->buffer().text(c.left(), c.top(),
                (opt->state & State_On) ? QStringLiteral("(o)") : QStringLiteral("( )"));
            return;
        case PE_FrameWindow: case PE_Frame: case PE_FrameGroupBox:
        case PE_PanelMenu: case PE_FrameMenu:
            drawBox(dev->buffer(), c);
            return;
        default:
            break;
        }
    }
    QProxyStyle::drawPrimitive(pe, opt, p, w);       // GUI path, untouched
}

void GridStyle::drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                            const QWidget *w) const {
    if (auto *dev = cellTarget(p)) {
        if (ce == CE_PushButtonBevel || ce == CE_PushButtonLabel) {
            if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
                if (ce == CE_PushButtonLabel) {
                    QRect c = w ? cellsOf(w->rect(), dev, w) : cellsOf(opt->rect, dev, w);
                    // State_HasFocus never arrives in TUI mode (F4): consult the
                    // router-owned focus instead.
                    bool foc = (opt->state & State_HasFocus) || (w && w == s_focus);
                    dev->buffer().text(c.left(), c.top(),
                                       QLatin1Char('<') + b->text + QLatin1Char('>'),
                                       Color(), Color(),
                                       foc ? Attrs(Attr::Reverse) : Attrs());
                }
                return;
            }
        }
    }
    QProxyStyle::drawControl(ce, opt, p, w);
}

} // namespace qtty
