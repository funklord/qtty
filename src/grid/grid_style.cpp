// src/grid/grid_style.cpp -- GridMetrics, GridStyle, focus ownership (sections 5.3-5.5).
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "../cell_geometry.h"
#include <QStyleFactory>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QPainter>
#include <QHash>
#include <QToolButton>
#include <QWidget>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QFontInfo>

namespace Qtty {

// What a tool button actually shows: its icon's glyph, its text, or both.
// Built in one place because sizeFromContents() and drawComplexControl() must
// agree exactly -- a label measured without the glyph is drawn into a box one
// cell too narrow, and the elide would then eat the last letter rather than
// the thing that did not fit.
static QString tool_button_label(const QStyleOptionToolButton *tb, const QWidget *w) {
	const QString text = strip_mnemonic(tb->text);
	const QString glyph = glyph_for(w, tb->icon);
	if (glyph.isEmpty()) return text;
	if (text.isEmpty()) return glyph;
	return glyph + QLatin1Char(' ') + text;
}

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
	// Overlay's GUI twin. Not Qt's -- ours -- but the same test applies and
	// is the reason the residue is named rather than folded into the
	// principle: it is a top-level qtty builds for the GUI path and sizes in
	// PIXELS on purpose, so the grid has nothing to say about it, and the
	// application never constructs it and cannot size it. Reported as a
	// 640x480 default by every test that composites while not inside exec(),
	// which is where it was found.
	if (w && w->objectName() == QLatin1String("qtty_overlay_twin")) return true;
	return false;
}

bool GridGuard::eventFilter(QObject *obj, QEvent *event) {
	if (event->type() == QEvent::Resize || event->type() == QEvent::Move
	    || event->type() == QEvent::Show) {
		if (QWidget *w = qobject_cast<QWidget *>(obj)) {
			// A widget that has never been shown has a geometry nothing
			// draws from, and Qt gives it one anyway. A QMenu handed to a
			// tool button sits at its construction-time 100x30 until it is
			// popped up, at which point the compositor positions and snaps
			// it -- so the reported geometry is one the application should
			// not be setting, and an author told to fix it would resize a
			// menu that nothing reads. That is the deforming-the-source
			// failure the style gate taught this tree.
			//
			// Not an exemption by class, which would be wrong: a menu that
			// IS on screen must be checked like anything else. It is a
			// question of WHEN, so Show joins the triggers -- otherwise a
			// widget laid out while hidden and shown at an unchanged
			// geometry would escape, there being no resize to catch.
			if (event->type() != QEvent::Show && w->isHidden()) return false;
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
	case PM_MenuPanelWidth:                                return cw;
	// The menu BAR gets none, and the two were sharing a case. A panel width
	// is applied on every side, so a horizontal cell used as a VERTICAL inset
	// put every item at y=10 in a bar 19 tall: each one straddled two rows
	// and hung below the bar it belonged to, and nothing was drawn where the
	// bar was. A popup menu genuinely wants its border, which is why it keeps
	// cw; a menu bar has no border to draw on a grid.
	case PM_MenuBarPanelWidth:                             return 0;
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
	// A terminal toolbar cannot be dragged, so its grip is nothing rather
	// than the nine pixels Fusion reserves -- which was pushing every tool
	// button off the grid by most of a cell. The separator and the overflow
	// arrow each get a whole cell, being things that are actually drawn.
	case PM_ToolBarHandleExtent:                           return 0;
	case PM_ToolBarSeparatorExtent:                        return cw;
	case PM_ToolBarExtensionExtent:                        return cw;
	// Icons are not drawn at all (see SH_ToolButtonStyle below), so reserving
	// room for one spends cells on nothing.
	case PM_ToolBarIconSize:                               return 0;
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
	if (cc == CC_SpinBox && opt) {
		// The same fault as the combo below, in the same place, and it cost
		// more: the spin box's internal QLineEdit came out 280x13+3+3 inside a
		// one-cell spin box -- Fusion's three-pixel inset and a thirteen-pixel
		// height in a nineteen-pixel cell -- so the value did not reach the
		// buffer at all. A spin box whose value can be changed and cannot be
		// read.
		const QRect r = opt->rect;
		switch (sc) {
		case SC_SpinBoxEditField:
			return QRect(r.left() + cw, r.top(),
				         qMax(cw, r.width() - 3 * cw), qMax(ch, r.height()));
		case SC_SpinBoxUp:
			return QRect(r.right() + 1 - 2 * cw, r.top(), cw, qMax(ch, r.height() / 2));
		case SC_SpinBoxDown:
			return QRect(r.right() + 1 - 2 * cw, r.top() + r.height() / 2,
				         cw, qMax(ch, r.height() / 2));
		case SC_SpinBoxFrame:
			return r;
		default:
			break;
		}
	}
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
	case CT_MenuBarItem:
	case CT_TabBarTab:
	case CT_HeaderSection:
	case CT_ProgressBar:
	case CT_Slider:
		return QSize(width, ch);

	// A tool button is measured from its TEXT, not from what toolButtonStyle
	// asked for. QToolBar defaults to Qt::ToolButtonIconOnly and a terminal
	// draws no icon, so the base style returned an icon-sized button of about
	// two cells and the label had nowhere to go -- a toolbar rendered as "]]".
	// Pinning SH_ToolButtonStyle reaches only the widgets that follow the
	// style; measuring it here reaches the rest, and keeps the measurement and
	// the drawing in one place, which is what a style is for.
	//
	// It sits outside the group above rather than in it: those cases fall
	// through to a shared return, and a case with a body in the middle of them
	// is a trap for whoever adds the next one.
	case CT_ToolButton:
		if (auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(o)) {
			int cells = 0;
			for (const QString &cluster : to_clusters(tool_button_label(tb, w)))
				cells += cluster_width(cluster);
			// A button carrying a menu is measured for the marker that says
			// so, or the marker is drawn into the last cell of the label and
			// the elide eats a letter to make room for something the
			// measurement never admitted was there.
			const bool menu = tb->features & QStyleOptionToolButton::HasMenu;
			return QSize((cells + 2 + (menu ? 2 : 0)) * cw, ch);
		}
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
		case PE_PanelToolBar:
			// Nothing. Fusion paints a background and a border along the
			// bottom edge, and that border sits at y = ch exactly -- one pixel
			// into the row BELOW the toolbar, which Channel B then drew as a
			// full-width rule through whatever lived there. Measured: a
			// toolbar over a central widget wrote "<Save>----------" across
			// the button's own row.
			//
			// A toolbar has no frame on a grid. Its extent is legible from the
			// buttons in it, and a rule drawn in a neighbour's cells is not a
			// frame, it is damage.
			return;
		case PE_IndicatorToolBarHandle:
			return;                                    // extent is nil, so is this
		case PE_IndicatorToolBarSeparator:
			for (int y = c.top(); y <= c.bottom(); ++y)
				dev->buffer().put_cluster(c.left(), y, QStringLiteral("│"));
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
					               QLatin1Char('<') + strip_mnemonic(b->text)
					                   + QLatin1Char('>'),
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
				// strip_mnemonic(), not remove('&'): the ad-hoc version that
				// stood here turned "A && B" into "A  B" rather than
				// "A & B", because it did not know that a doubled ampersand
				// is a literal one. One spelling of the rule, in one place.
				const QString label = strip_mnemonic(mi->text);
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
				// The marker was drawn literally here and nowhere else was it
				// missing entirely -- measured, "&General" came out
				// "[&Genera..." , the ampersand both visible AND stealing the
				// cell that made the label elide a character early.
				const QString label = QLatin1Char('[') + strip_mnemonic(t->text)
					                    + QLatin1Char(']');
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
		case CC_ToolButton:
			if (auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(opt)) {
				// Bracketed like a tab, which is the nearest thing already in
				// this style: a row of adjacent labels, one of which may be
				// current. A push button's angle brackets would read as a
				// dialog button sitting in a toolbar.
				const int row = c.top() + c.height() / 2;
				// Clear first. A toolbar draws its own background through
				// Channel B before its children, and the leftover showed
				// between the label and the closing bracket -- "[Cut-]".
				dev->buffer().fill(c, Cell{});
				const bool on = (tb->state & State_On)
					             || (tb->state & State_Sunken)
					             || (w && w == s_focus);
				dev->buffer().put_cluster(c.left(), row, QStringLiteral("["));
				dev->buffer().put_cluster(c.right(), row, QStringLiteral("]"));
				// A menu is an affordance or it is nothing: a tool button
				// with a dropdown looked exactly like one without, so the
				// only way to discover it was to press it. The base style
				// draws this with PE_IndicatorArrowDown, which this style
				// answers -- but nothing reaches that primitive, because the
				// combo box, the spin box, the scroll bar and this are all
				// drawn whole here.
				const bool menu = tb->features & QStyleOptionToolButton::HasMenu;
				if (menu)
					dev->buffer().put_cluster(c.right() - 1, row,
						                      QStringLiteral("▾"));
				const int inner = c.width() - 2 - (menu ? 2 : 0);
				if (inner > 0)
					dev->buffer().text(c.left() + 1, row,
						                   elide_to_cells(tool_button_label(tb, w), inner),
						                   Color(), Color(),
						                   on ? Attrs(Attr::Reverse) : Attrs());
				return;
			}
			break;
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

int GridStyle::styleHint(StyleHint hint, const QStyleOption *opt, const QWidget *w,
                        QStyleHintReturn *ret) const {
	switch (hint) {
	case SH_DialogButtonBox_ButtonsHaveIcons:
		// qtty draws no icons, so a button that reserves width for one spends
		// cells on something invisible. Worse, this hint comes from the
		// platform theme: measured, offscreen answers 0 and xcb answers 1, so
		// the same dialog laid its buttons out two cells apart depending on
		// the desktop the program was started from -- a "Cancel" button 90px
		// wide became 110. It was found by running the suite under a second
		// platform and watching one snapshot fixture shift.
		//
		// This is the ONLY such divergence, which is a measurement rather than
		// a hope: all 121 style hints and all 96 pixel metrics were compared
		// across the two platforms, and the other 216 agree.
		return 0;
	case SH_DialogButtonLayout:
		// Button ORDER, and the desktop was choosing it: offscreen answers
		// WinLayout and a gtk3 platform theme answers GnomeLayout, which puts
		// Cancel first. Measured -- the prefs_dialog fixture came back as
		// "<Cancel><OK>" under gtk3.
		//
		// Pinning to the value this library has always produced is not a
		// choice about button order; it is a refusal to let the order change
		// underneath a program. Changing it would be the decision, and this
		// is not that. It matters more here than on a desktop because a
		// terminal program is routinely run over ssh, where the machine
		// holding the desktop theme is not the machine anybody is looking at.
		return 0;                                  // QDialogButtonBox::WinLayout
	case SH_ToolButtonStyle:
		// QToolBar defaults to Qt::ToolButtonIconOnly, and a terminal draws no
		// icon -- so a toolbar rendered as an empty strip. Measured: two
		// actions laid out correctly, 60x19 and 70x19, and not one glyph on
		// the screen. Text-only is the only setting that says anything here,
		// and it is the same judgement as pinning the dialog-button icon hint
		// above: space is not reserved for what cannot be drawn.
		//
		// A style hint rather than a change to QToolBar, because this is
		// exactly what the hint is for: an application that sets a style on
		// its toolbar explicitly still wins.
		return Qt::ToolButtonTextOnly;
	case SH_LineEdit_PasswordCharacter:
		// Same shape: U+25CF under offscreen, U+2022 under gtk3, so what a
		// password field showed depended on the desktop. Both are one cell
		// wide, so this is about determinism rather than about which glyph is
		// better -- and a snapshot suite whose output a desktop can change is
		// not a snapshot suite.
		return 0x25CF;
	default:
		break;
	}
	return QProxyStyle::styleHint(hint, opt, w, ret);
}

// ------------------------------------------------------------- icon glyphs
// design.md section 8.6. A flat map rather than anything cleverer: the
// population is an application's own icon names, and it is read once per
// icon drawn.
static QHash<QString, QString> &glyph_registry() {
	static QHash<QString, QString> map;
	return map;
}

void set_icon_glyph(const QString &icon_name, const QString &glyph) {
	if (icon_name.isEmpty()) return;             // nothing to key on
	glyph_registry().insert(icon_name, glyph);
}

QString icon_glyph(const QString &icon_name) {
	return glyph_registry().value(icon_name);
}

void clear_icon_glyphs() { glyph_registry().clear(); }

QString glyph_for(const QWidget *w, const QIcon &icon) {
	return glyph_for(w, icon.isNull() ? QString() : icon.name());
}

QString glyph_for(const QWidget *w, const QString &icon_name) {
	// The property first. It is per-instance, so it answers the case a name
	// cannot: two widgets sharing one standard icon for different meanings.
	if (w) {
		const QVariant v = w->property("qtty.glyph");
		if (v.isValid()) {
			const QString g = v.toString();
			if (!g.isEmpty()) return g;
		}
		// ...and the action behind it, which is the object an application
		// actually holds. A toolbar's QToolButton is built by Qt from a
		// QAction the application created, so requiring the property on the
		// button would mean requiring it on a widget the application never
		// sees. Measured: this is the route that works, because QIcon::name()
		// is empty unless an icon THEME resolved the icon, and qtty pins the
		// platform theme off.
		if (auto *tb = qobject_cast<const QToolButton *>(w)) {
			if (QAction *a = tb->defaultAction()) {
				const QVariant av = a->property("qtty.glyph");
				if (av.isValid()) {
					const QString g = av.toString();
					if (!g.isEmpty()) return g;
				}
			}
		}
	}
	// A themed icon has a name and one built from a pixmap does not, so an
	// unnamed icon finds nothing -- which is the honest answer rather than a
	// wrong glyph.
	return icon_glyph(icon_name);
}

// ------------------------------------------------------------------ GridSnap
// GridGuard's other half (section 7.8). See grid.h for why the policy is
// round-to-nearest and nothing else.
static GridSnap *s_snap = nullptr;
static int s_snapped = 0;
static bool s_snapping = false;

QRect GridSnap::snap(const QRect &px) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const int l = qRound(double(px.left()) / cw) * cw;
	const int t = qRound(double(px.top()) / ch) * ch;
	const int r = qRound(double(px.right() + 1) / cw) * cw;
	const int b = qRound(double(px.bottom() + 1) / ch) * ch;
	// qMax(0, ...) rather than qMax(cw, ...): a widget under half a cell wide
	// rounds away to nothing, and growing it to a whole cell is the case
	// measured overlapping its neighbour.
	return QRect(l, t, qMax(0, r - l), qMax(0, b - t));
}

int GridSnap::snapped() { return s_snapped; }
void GridSnap::reset() { s_snapped = 0; }
bool GridSnap::installed() { return s_snap != nullptr; }

bool GridSnap::eventFilter(QObject *obj, QEvent *event) {
	if (s_snapping) return false;                // our own setGeometry coming back
	if (event->type() != QEvent::Resize && event->type() != QEvent::Move)
		return false;
	QWidget *w = qobject_cast<QWidget *>(obj);
	// A top-level is left alone for the reason GridGuard does not check its
	// position: there is no window manager, and Compositor::compose() places
	// and snaps every top-level itself (section 8.1). Snapping one here would
	// fight that, and the terminal decides a window's size in any case.
	if (!w || w->isWindow() || !w->parentWidget() || GridGuard::is_exempt(w))
		return false;

	const QRect want = snap(w->geometry());
	if (want == w->geometry()) return false;

	s_snapping = true;
	w->setGeometry(want);
	s_snapping = false;
	++s_snapped;
	return false;                                // never consume
}

void GridSnap::install(QCoreApplication &app) {
	if (s_snap) return;
	s_snap = new GridSnap;
	s_snap->setParent(&app);
	app.installEventFilter(s_snap);
}

void GridSnap::remove() {
	if (!s_snap) return;
	if (QCoreApplication *app = qApp) app->removeEventFilter(s_snap);
	delete s_snap;
	s_snap = nullptr;
}

} // namespace Qtty
