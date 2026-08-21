// src/grid/gridstyle.cpp — GridMetrics, GridStyle, focus ownership (§5.3–§5.5).
#include "qtty/grid.h"
#include "qtty/paint.h"
#include <QStyleFactory>
#include <QStyleOption>
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
// offset — map through a widget (§16, F2). WHICH widget matters: opt->rect is
// in the coordinates of the widget being painted, and during a paintEvent
// that widget IS p->device() (§16, F1) — for item views the *viewport*, not
// the view the style is handed as `w`. Mapping through `w` there silently
// drops the header/frame offset.
static QRect cellsOf(const QRect &r, QPainter *p, CellPaintDevice *dev,
                     const QWidget *w) {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    const QWidget *paintWidget = dynamic_cast<QWidget *>(p->device());
    const QWidget *m = paintWidget ? paintWidget : w;
    QPoint tl = m ? m->mapTo(m->window(), r.topLeft()) : r.topLeft();
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
        QRect c = cellsOf(opt->rect, p, dev, w);
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
        case PE_PanelMenu: case PE_FrameMenu: case PE_PanelLineEdit:
            drawBox(dev->buffer(), c);
            return;
        case PE_IndicatorBranch: {                    // tree expanders (§17.2)
            QString g = QStringLiteral(" ");
            if (opt->state & State_Children)
                g = (opt->state & State_Open) ? QStringLiteral("▾") : QStringLiteral("▸");
            dev->buffer().text(c.right(), c.top(), g);
            return;
        }
        case PE_IndicatorArrowDown:  dev->buffer().text(c.left(), c.top(), QStringLiteral("▾")); return;
        case PE_IndicatorArrowUp:    dev->buffer().text(c.left(), c.top(), QStringLiteral("▴")); return;
        case PE_IndicatorArrowLeft:  dev->buffer().text(c.left(), c.top(), QStringLiteral("◂")); return;
        case PE_IndicatorArrowRight: dev->buffer().text(c.left(), c.top(), QStringLiteral("▸")); return;
        // Suppress pixel-noise primitives; selection is handled semantically
        // in CE_ItemViewItem, focus by the router-owned focus attr.
        case PE_FrameFocusRect:
        case PE_PanelItemViewItem:
        case PE_PanelItemViewRow:
            return;
        default:
            break;
        }
    }
    QProxyStyle::drawPrimitive(pe, opt, p, w);       // GUI path, untouched
}

// Elide a string to a cell budget (cluster-aware enough for labels).
static QString elide(const QString &s, int cells) {
    if (cells <= 0) return {};
    int used = 0; QString out;
    for (const QString &cl : toClusters(s)) {
        int cw = clusterWidth(cl);
        if (used + cw > cells) { if (!out.isEmpty()) out.chop(1), out += QLatin1Char('…'); break; }
        out += cl; used += cw;
    }
    return out;
}

void GridStyle::drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                            const QWidget *w) const {
    if (auto *dev = cellTarget(p)) {
        QRect c = cellsOf(opt->rect, p, dev, w);
        switch (ce) {
        case CE_PushButtonBevel:
            return;                                   // bevel is the label's brackets
        case CE_PushButtonLabel:
            if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
                QRect bc = w ? cellsOf(w->rect(), p, dev, w) : c;
                // State_HasFocus never arrives in TUI mode (F4): router focus.
                bool foc = (opt->state & State_HasFocus) || (w && w == s_focus);
                dev->buffer().text(bc.left(), bc.top(),
                                   QLatin1Char('<') + b->text + QLatin1Char('>'),
                                   Color(), Color(),
                                   foc ? Attrs(Attr::Reverse) : Attrs());
            }
            return;
        case CE_MenuItem:
            if (auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
                if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
                    for (int x = c.left(); x <= c.right(); ++x)
                        dev->buffer().putCluster(x, c.top(), QStringLiteral("─"));
                    return;
                }
                const Attrs a = (opt->state & State_Selected) ? Attrs(Attr::Reverse) : Attrs();
                if (a) {                              // highlight spans the row
                    Cell v; v.attrs = a;
                    dev->buffer().fill(QRect(c.left(), c.top(), c.width(), 1), v);
                }
                const QStringList parts = mi->text.split(QLatin1Char('\t'));
                QString label = parts.value(0);
                label.remove(QLatin1Char('&'));       // mnemonic markers
                dev->buffer().text(c.left() + 1, c.top(), elide(label, c.width() - 2),
                                   Color(), Color(), a);
                if (parts.size() > 1) {               // right-aligned shortcut
                    const QString sc = parts[1];
                    dev->buffer().text(c.right() - sc.size(), c.top(), sc,
                                       Color(), Color(), a | Attr::Dim);
                }
                if (mi->menuItemType == QStyleOptionMenuItem::SubMenu)
                    dev->buffer().text(c.right(), c.top(), QStringLiteral("▸"),
                                       Color(), Color(), a);
                return;
            }
            break;
        case CE_MenuBarItem:
            if (auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
                const bool hot = opt->state & (State_Selected | State_Sunken);
                QString label = mi->text;
                label.remove(QLatin1Char('&'));
                dev->buffer().text(c.left() + 1, c.top(), label, Color(), Color(),
                                   hot ? Attrs(Attr::Reverse) : Attrs());
                return;
            }
            break;
        case CE_MenuBarEmptyArea:
        case CE_MenuEmptyArea:
            return;
        case CE_ItemViewItem:                          // list/table/tree cells
            if (auto *vi = qstyleoption_cast<const QStyleOptionViewItem *>(opt)) {
                const Attrs a = (opt->state & State_Selected) ? Attrs(Attr::Reverse) : Attrs();
                if (a) { Cell v; v.attrs = a;
                         dev->buffer().fill(QRect(c.left(), c.top(), c.width(), 1), v); }
                dev->buffer().text(c.left() + 1, c.top(), elide(vi->text, c.width() - 1),
                                   Color(), Color(), a);
                return;
            }
            break;
        case CE_HeaderSection:
            return;                                    // no chrome; label only
        case CE_HeaderLabel:
            if (auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
                dev->buffer().text(c.left(), c.top(), elide(h->text, c.width()),
                                   Color(), Color(), Attr::Bold);
                return;
            }
            break;
        case CE_TabBarTab:                             // shape + label in one
            if (auto *t = qstyleoption_cast<const QStyleOptionTab *>(opt)) {
                const bool sel = opt->state & State_Selected;
                const QString label = QLatin1Char('[') + t->text + QLatin1Char(']');
                dev->buffer().text(c.left(), c.top(), elide(label, c.width()),
                                   Color(), Color(),
                                   sel ? Attrs(Attr::Reverse) : Attrs());
                return;
            }
            break;
        case CE_ProgressBar:
            if (auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt)) {
                const int span = pb->maximum - pb->minimum;
                const double frac = span > 0 ? double(pb->progress - pb->minimum) / span : 0.0;
                const int filled = qRound(frac * c.width());
                for (int x = 0; x < c.width(); ++x)
                    dev->buffer().putCluster(c.left() + x, c.top(),
                        x < filled ? QStringLiteral("█") : QStringLiteral("░"));
                if (pb->textVisible) {
                    const QString label = pb->text.isEmpty()
                        ? QStringLiteral("%1%").arg(qRound(frac * 100)) : pb->text;
                    dev->buffer().text(c.center().x() - label.size() / 2, c.top(),
                                       label, Color(), Color(), Attr::Reverse);
                }
                return;
            }
            break;
        case CE_Splitter: {
            const bool horizontalHandle = opt->rect.width() < opt->rect.height();
            const QString g = horizontalHandle ? QStringLiteral("│") : QStringLiteral("─");
            for (int y = c.top(); y <= c.bottom(); ++y)
                for (int x = c.left(); x <= c.right(); ++x)
                    dev->buffer().putCluster(x, y, g);
            return;
        }
        case CE_ScrollBarAddLine: case CE_ScrollBarSubLine:
        case CE_ScrollBarAddPage: case CE_ScrollBarSubPage:
        case CE_ScrollBarSlider:
            return;                                    // drawn whole in CC_ScrollBar
        default:
            break;
        }
    }
    QProxyStyle::drawControl(ce, opt, p, w);
}

void GridStyle::drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt,
                                   QPainter *p, const QWidget *w) const {
    if (auto *dev = cellTarget(p)) {
        QRect c = cellsOf(opt->rect, p, dev, w);
        switch (cc) {
        case CC_ScrollBar:                             // §16 F5: self-drawn whole
            if (auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
                const bool vert = sb->orientation == Qt::Vertical;
                const int len = vert ? c.height() : c.width();
                if (len < 2) return;
                const int track = len - 2;
                const int span = sb->maximum - sb->minimum;
                int thumbLen = 1, thumbPos = 0;
                if (span > 0 && track > 0) {
                    thumbLen = qBound(1, track * sb->pageStep
                                         / qMax(1, span + sb->pageStep), track);
                    thumbPos = (track - thumbLen) * (sb->sliderPosition - sb->minimum) / span;
                }
                for (int i = 0; i < len; ++i) {
                    QString g;
                    if (i == 0)            g = vert ? QStringLiteral("▲") : QStringLiteral("◀");
                    else if (i == len - 1) g = vert ? QStringLiteral("▼") : QStringLiteral("▶");
                    else {
                        const int t = i - 1;
                        g = (t >= thumbPos && t < thumbPos + thumbLen)
                            ? QStringLiteral("█") : QStringLiteral("░");
                    }
                    if (vert) dev->buffer().putCluster(c.left(), c.top() + i, g);
                    else      dev->buffer().putCluster(c.left() + i, c.top(), g);
                }
                return;
            }
            break;
        case CC_ComboBox: {
            if (c.height() >= 2) drawBox(dev->buffer(), c);
            const int row = c.top() + c.height() / 2;
            dev->buffer().putCluster(c.right() - 1, row, QStringLiteral("▾"));
            return;                                    // label via CE_ComboBoxLabel
        }
        case CC_Slider:
            if (auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
                const bool vert = sl->orientation == Qt::Vertical;
                const int len = vert ? c.height() : c.width();
                const int span = sl->maximum - sl->minimum;
                const int pos = span > 0
                    ? (len - 1) * (sl->sliderPosition - sl->minimum) / span : 0;
                for (int i = 0; i < len; ++i) {
                    const QString g = i == pos ? QStringLiteral("●")
                                    : (vert ? QStringLiteral("│") : QStringLiteral("─"));
                    if (vert) dev->buffer().putCluster(c.left(), c.top() + i, g);
                    else      dev->buffer().putCluster(c.left() + i, c.top(), g);
                }
                return;
            }
            break;
        case CC_SpinBox: {
            if (c.height() >= 2) drawBox(dev->buffer(), c);
            const int row = c.top() + c.height() / 2;
            dev->buffer().putCluster(c.right() - 1, row, QStringLiteral("±"));
            return;                                    // value text via child edit
        }
        default:
            break;
        }
    }
    QProxyStyle::drawComplexControl(cc, opt, p, w);
}

} // namespace qtty
