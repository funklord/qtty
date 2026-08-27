// src/grid/grid_style.cpp -- GridMetrics, GridStyle, focus ownership (sections 5.3-5.5).
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "../cell_geometry.h"
#include <QStyleFactory>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QPainter>
#include <QWidget>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QFontInfo>

namespace Qtty {

static int s_cw = 8, s_ch = 16;
int GridMetrics::cw() { return s_cw; }
int GridMetrics::ch() { return s_ch; }
void GridMetrics::set(int cw, int ch) { s_cw = cw; s_ch = ch; }

static QWidget *s_focus = nullptr;
QWidget *focusWidget() { return s_focus; }
void setFocusWidget(QWidget *w) { s_focus = w; }

// ------------------------------------------------- font provisioning (5.3/R3)

QString grid_font_problem(const QFont &font) {
	const QFontInfo info(font);
	if (!info.fixedPitch())
		return QStringLiteral("'%1' resolved to '%2', which is not fixed pitch")
		       .arg(font.family(), info.family());

	// Real-valued metrics, because the integer ones have already rounded and
	// would report 10 for an advance of 9.6. Integrality is the property the
	// whole grid rests on, so it is the one to ask about directly.
	const QFontMetricsF fm(font);
	const qreal height = fm.height();
	if (!qFuzzyCompare(height, qRound(height)))
		return QStringLiteral("line height is %1 px, which is not a whole number")
		       .arg(height);
	if (qRound(height) <= 0)
		return QStringLiteral("line height is %1 px").arg(height);

	// A spread of characters rather than a pair: 'i' against 'M' is the check
	// this replaced, and it passes on a font that is fixed-pitch for Latin and
	// proportional anywhere else. These cover thin, wide, punctuation and
	// digits, which is where a not-quite-monospace font gives itself away.
	const QString probe = QStringLiteral("MilW@#0oX|._");
	const qreal advance = fm.horizontalAdvance(QChar(u'M'));
	if (!qFuzzyCompare(advance, qRound(advance)))
		return QStringLiteral("advance is %1 px, which is not a whole number")
		       .arg(advance);
	if (qRound(advance) <= 0)
		return QStringLiteral("advance is %1 px").arg(advance);
	for (const QChar c : probe) {
		const qreal a = fm.horizontalAdvance(c);
		if (!qFuzzyCompare(a, advance))
			return QStringLiteral("'%1' advances %2 px against %3 px for 'M'")
			       .arg(c).arg(a).arg(advance);
	}
	return QString();
}

// ------------------------------------------------------------------ GridGuard

static int s_violations = 0;
static GridGuard *s_guard = nullptr;

int GridGuard::violations() { return s_violations; }
void GridGuard::reset() { s_violations = 0; }

bool GridGuard::is_exempt(const QWidget *w) {
	// Widgets Qt builds for itself, which the application never constructs
	// and cannot size. Measured F5 named two of these -- QHeaderView and
	// QScrollBar -- and the class is larger than that: running the guard over
	// the whole suite turned up a combo box's popup container, scroller and
	// list view, a splitter handle, a tab widget's stacked page area, a tab
	// bar's two scroll buttons, and a scroll area's viewport and its
	// scrollbar containers. Every one is internal, and none of them is
	// reachable to be fixed from the style, which is why design.md section
	// 5.3 reaches for ICellPainted or fixed sizing for this class instead.
	//
	// The test is a principle rather than a list, and deliberately so. A list
	// of nine class names is one somebody adds a tenth to without deciding
	// anything, and that is how an exemption grows until the guard reports
	// nothing. Qt marks most of its own internal children two ways and both
	// are stable: an objectName beginning "qt_", and a class name carrying
	// "Private". Anything descended from such a widget is inside Qt's own
	// construction and is exempt with it.
	//
	// The principle is not complete, and saying so is better than pretending.
	// Qt has private widget classes that follow neither convention -- declared
	// in a .cpp, exported nowhere, named like ordinary public classes. They
	// are the named residue below, and the point of naming them HERE rather
	// than folding them into the principle is that each one is then visibly
	// an exception with a reason attached, instead of a list entry somebody
	// appended. An addition should be as hard to make as this comment is to
	// write.
	//
	// What is NOT exempt is the case worth catching: a widget the
	// application made, at a geometry the application can change.
	static const char *const by_class[] = {
		// F5 measured these two: they ignore style metrics and size
		// themselves, so they land off the grid however the style is written.
		"QHeaderView", "QScrollBar",
		// The corner between a table's two headers. A private class in
		// qtableview.cpp -- no qt_ objectName, no "Private" in the name --
		// that QTableView constructs and sizes, and that an application
		// cannot reach. It sits at its construction-time 10x19+10+10 when
		// both headers are hidden, which is off any row grid taller than 10.
		// Found the first time a QTableView was exercised at all; it had
		// never appeared because nothing had built one.
		"QTableCornerButton",
	};
	for (const QObject *o = w; o; o = o->parent()) {
		const QString name = o->objectName();
		if (name.startsWith(QLatin1String("qt_"))) return true;
		const QLatin1String cls(o->metaObject()->className());
		if (cls.contains(QLatin1String("Private"))) return true;
		for (const char *k : by_class)
			if (o->inherits(k)) return true;
	}
	// A QTabBar's scroll buttons are named rather than qt_-prefixed, and are
	// created and sized by the tab bar. Reached through the parent so a
	// QToolButton the application puts in a tab bar is still checked.
	if (w && w->parent() && w->parent()->inherits("QTabBar")
	    && w->inherits("QToolButton"))
		return true;
	return false;
}

bool GridGuard::eventFilter(QObject *obj, QEvent *event) {
	if (event->type() == QEvent::Resize || event->type() == QEvent::Move) {
		if (QWidget *w = qobject_cast<QWidget *>(obj)) {
			// A top-level is asked about its SIZE and not its position. In
			// qtty a window's own x and y mean nothing: there is no window
			// manager, and Compositor::compose() decides where each
			// top-level is drawn and snaps that to the grid on the way
			// (section 8.1). Qt assigns a position anyway -- centring a
			// dialog over its parent -- and checking it reported every
			// dialog in the suite for a coordinate nothing reads.
			const QRect g = w->geometry();
			const QRect asked = w->isWindow() ? QRect(QPoint(), g.size()) : g;
			if (!is_exempt(w) && !GridMetrics::is_aligned(asked)) {
				++s_violations;
				qWarning("qtty: %s '%s' geometry %dx%d+%d+%d is off the "
				         "%dx%d grid",
				         w->metaObject()->className(),
				         qPrintable(w->objectName()),
				         g.width(), g.height(), g.x(), g.y(),
				         GridMetrics::cw(), GridMetrics::ch());
			}
		}
	}
	return false;                                // never consume
}

void GridGuard::install(QCoreApplication &app) {
	if (s_guard) return;
	s_guard = new GridGuard;
	s_guard->setParent(&app);
	app.installEventFilter(s_guard);
}


static void draw_box(CellBuffer &b, const QRect &c) {
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
	case PM_DefaultFrameWidth:                             return cw;   // section 16.3: must be cell-safe
	case PM_ButtonMargin:                                  return cw;
	case PM_FocusFrameHMargin: case PM_FocusFrameVMargin:  return 0;
	case PM_MenuPanelWidth: case PM_MenuBarPanelWidth:     return cw;
	case PM_IndicatorWidth:                                return 3 * cw;
	case PM_IndicatorHeight:                               return ch;
	case PM_ExclusiveIndicatorWidth:                       return 3 * cw;
	case PM_ExclusiveIndicatorHeight:                      return ch;
	// section 17.1 audit -- every metric that shapes geometry lands on the grid:
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

// section 5.3: a size for every ContentsType, rather than one rule for all of
// them. Snapping the proxy's answer up was the first version and it is wrong in
// the direction that matters: Fusion sizes a control for a mouse, so a check
// box asks for 25 pixels against a 19-pixel cell and a push button for 27, and
// rounding those up gives two and three ROWS for a control that occupies one.
//
// Measured before this was written, at cw=10 ch=19: check box and radio button
// came out 38 (2 cells), push button, line edit and combo box 57 (3 cells).
// Only QLabel was right, and only because its height already equals the line
// height. A dialog of five controls was therefore twice as tall as it needed to
// be, and -- the reason this surfaced -- a control two cells tall puts its
// indicator on one row and its text on the other, which is the off-by-one
// vertical centring section 16 recorded and could not place.
//
// So a single-line control is one cell tall by construction. The width still
// snaps up, because a width is a count of characters and rounding one down
// truncates text.
QRect GridStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex *opt,
                                SubControl sc, const QWidget *w) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	if (cc == CC_ComboBox && opt) {
		// The cell layout drawComplexControl draws: an opening bracket, the
		// text, the arrow, a closing bracket. The edit field is what is left
		// between them, and it has to be stated here or Qt places the internal
		// QLineEdit at the proxy style's pixel offsets -- two pixels in from a
		// border this style never draws.
		const QRect r = opt->rect;
		switch (sc) {
		case SC_ComboBoxEditField:
			return QRect(r.left() + cw, r.top(),
				         qMax(cw, r.width() - 3 * cw), qMax(ch, r.height()));
		case SC_ComboBoxArrow:
			return QRect(r.right() + 1 - 2 * cw, r.top(), cw, qMax(ch, r.height()));
		case SC_ComboBoxFrame:
		case SC_ComboBoxListBoxPopup:
			return r;
		default:
			break;
		}
	}
	return QProxyStyle::subControlRect(cc, opt, sc, w);
}

QSize GridStyle::sizeFromContents(ContentsType t, const QStyleOption *o, const QSize &cs,
                                  const QWidget *w) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QSize s = QProxyStyle::sizeFromContents(t, o, cs, w);
	const int width = ((s.width() + cw - 1) / cw) * cw;
	const int snapped = ((s.height() + ch - 1) / ch) * ch;

	switch (t) {
	// Controls that hold exactly one line of text, whatever a desktop style
	// would give them for a mouse target.
	case CT_CheckBox:
	case CT_RadioButton:
	case CT_PushButton:
	case CT_LineEdit:
	case CT_ComboBox:
	case CT_SpinBox:
	case CT_ToolButton:
	case CT_MenuBarItem:
	case CT_TabBarTab:
	case CT_HeaderSection:
	case CT_ProgressBar:
	case CT_Slider:
		return QSize(width, ch);

	// A menu item is one row, except a separator, which is also one row but
	// asks for a few pixels and would otherwise round to nothing.
	case CT_MenuItem:
		return QSize(width, ch);

	// These genuinely carry more than a line -- an item view row with a
	// multi-line delegate, a group box around other widgets, a whole menu --
	// so the snap-up is the right rule and the only one that cannot clip.
	case CT_ItemViewItem:
	case CT_GroupBox:
	case CT_Menu:
	case CT_MdiControls:
	case CT_ScrollBar:
	case CT_SizeGrip:
	case CT_Splitter:
	case CT_TabWidget:
	default:
		return QSize(width, snapped);
	}
}

void GridStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                              const QWidget *w) const {
	if (auto *dev = cell_target(p)) {
		QRect c = cells_of(opt->rect, p, dev, w);
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
			draw_box(dev->buffer(), c);
			return;
		case PE_IndicatorBranch: {                    // tree expanders (section 17.2)
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


void GridStyle::drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                            const QWidget *w) const {
	if (auto *dev = cell_target(p)) {
		QRect c = cells_of(opt->rect, p, dev, w);
		switch (ce) {
		case CE_PushButtonBevel:
			return;                                   // bevel is the label's brackets
		case CE_PushButtonLabel:
			if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
				QRect bc = w ? cells_of(w->rect(), p, dev, w) : c;
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
						dev->buffer().put_cluster(x, c.top(), QStringLiteral("─"));
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
				dev->buffer().text(c.left() + 1, c.top(), elide_to_cells(label, c.width() - 2),
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
				// The whole item, not its top row. A one-cell fill was
				// indistinguishable from a correct one while every item in
				// the suite was one cell tall, and wrong the moment a
				// delegate returned a taller sizeHint: the row highlighted
				// its first line and left the rest on the ordinary ground.
				if (a) { Cell v; v.attrs = a; dev->buffer().fill(c, v); }
				dev->buffer().text(c.left() + 1, c.top(), elide_to_cells(vi->text, c.width() - 1),
					               Color(), Color(), a);
				return;
			}
			break;
		case CE_HeaderSection:
			return;                                    // no chrome; label only
		case CE_HeaderLabel:
			if (auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
				dev->buffer().text(c.left(), c.top(), elide_to_cells(h->text, c.width()),
					               Color(), Color(), Attr::Bold);
				return;
			}
			break;
		case CE_TabBarTab:                             // shape + label in one
			if (auto *t = qstyleoption_cast<const QStyleOptionTab *>(opt)) {
				const bool sel = opt->state & State_Selected;
				const QString label = QLatin1Char('[') + t->text + QLatin1Char(']');
				dev->buffer().text(c.left(), c.top(), elide_to_cells(label, c.width()),
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
					dev->buffer().put_cluster(c.left() + x, c.top(),
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
			const bool horizontal_handle = opt->rect.width() < opt->rect.height();
			const QString g = horizontal_handle ? QStringLiteral("│") : QStringLiteral("─");
			for (int y = c.top(); y <= c.bottom(); ++y)
				for (int x = c.left(); x <= c.right(); ++x)
					dev->buffer().put_cluster(x, y, g);
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
	if (auto *dev = cell_target(p)) {
		QRect c = cells_of(opt->rect, p, dev, w);
		switch (cc) {
		case CC_ScrollBar:                             // section 16 F5: self-drawn whole
			if (auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
				const bool vert = sb->orientation == Qt::Vertical;
				const int len = vert ? c.height() : c.width();
				if (len < 2) return;
				const int track = len - 2;
				const int span = sb->maximum - sb->minimum;
				int thumb_len = 1, thumb_pos = 0;
				if (span > 0 && track > 0) {
					thumb_len = qBound(1, track * sb->pageStep
						                 / qMax(1, span + sb->pageStep), track);
					thumb_pos = (track - thumb_len) * (sb->sliderPosition - sb->minimum) / span;
				}
				for (int i = 0; i < len; ++i) {
					QString g;
					if (i == 0)            g = vert ? QStringLiteral("▲") : QStringLiteral("◀");
					else if (i == len - 1) g = vert ? QStringLiteral("▼") : QStringLiteral("▶");
					else {
						const int t = i - 1;
						g = (t >= thumb_pos && t < thumb_pos + thumb_len)
							? QStringLiteral("█") : QStringLiteral("░");
					}
					if (vert) dev->buffer().put_cluster(c.left(), c.top() + i, g);
					else      dev->buffer().put_cluster(c.left() + i, c.top(), g);
				}
				return;
			}
			break;
		case CC_ComboBox: {
			// A box needs two rows to have a top and a bottom, and since
			// sizeFromContents made a combo one cell tall it never has them.
			// Bracket it instead, which is what CE_PushButtonLabel already
			// does for a button and what a TUI reader expects: the control
			// has to be visibly a control, and at one row a frame cannot say
			// so. Before this it rendered as bare text with a marker, wearing
			// the one-cell indent where the frame used to be.
			const int row = c.top() + c.height() / 2;
			if (c.height() >= 2) {
				draw_box(dev->buffer(), c);
			} else {
				dev->buffer().put_cluster(c.left(), row, QStringLiteral("["));
				dev->buffer().put_cluster(c.right(), row, QStringLiteral("]"));
			}
			dev->buffer().put_cluster(c.right() - 1, row, QStringLiteral("▾"));
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
					if (vert) dev->buffer().put_cluster(c.left(), c.top() + i, g);
					else      dev->buffer().put_cluster(c.left() + i, c.top(), g);
				}
				return;
			}
			break;
		case CC_SpinBox: {
			// Same as the combo above, and for the same reason.
			const int row = c.top() + c.height() / 2;
			if (c.height() >= 2) {
				draw_box(dev->buffer(), c);
			} else {
				dev->buffer().put_cluster(c.left(), row, QStringLiteral("["));
				dev->buffer().put_cluster(c.right(), row, QStringLiteral("]"));
			}
			dev->buffer().put_cluster(c.right() - 1, row, QStringLiteral("±"));
			return;                                    // value text via child edit
		}
		default:
			break;
		}
	}
	QProxyStyle::drawComplexControl(cc, opt, p, w);
}

} // namespace Qtty
