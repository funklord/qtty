// src/grid/grid_style.cpp -- GridMetrics, GridStyle, focus ownership (sections 5.3-5.5).
#include "qtty/grid.h"
#include "qtty/delegate.h"
#include "qtty/paint.h"
#include "../cell_geometry.h"
// QAction, which this file uses through QToolButton::defaultAction() and had
// never included. It compiled because something else drags it in -- and that
// is luck rather than a fact about this file: Qt 6 MOVED QAction from
// QtWidgets to QtGui, which is exactly the kind of reorganisation that
// changes what a header brings with it. Found by building against Qt 5.15,
// where nothing here drags it in and the type is incomplete at the point of
// use. Including what the file uses is right on every version and is not a
// position on which versions are supported (section 8.1).
#include <QPointer>
#include <QAction>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QPainter>
#include <QHash>
#include <QAbstractItemView>
#include <QLineEdit>
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
	// A button whose whole content is an ARROW -- the scroll and navigation
	// buttons Qt builds, and any QToolButton given an arrowType -- rendered
	// as an empty pair of brackets: it has no text and no icon, and nothing
	// here asked what kind of arrow it was. Answered in the label helper
	// rather than at the drawing site so that sizeFromContents() measures the
	// same string that gets drawn, which is what the tool button's menu
	// marker had to be taught separately.
	if (tb->features & QStyleOptionToolButton::Arrow) {
		switch (tb->arrowType) {
		case Qt::UpArrow:    return QStringLiteral("▴");
		case Qt::DownArrow:  return QStringLiteral("▾");
		case Qt::LeftArrow:  return QStringLiteral("◂");
		case Qt::RightArrow: return QStringLiteral("▸");
		default:             break;
		}
	}
	// A dock widget's title buttons, which carry no text and no icon this
	// style can reach -- so they came out as two identical empty brackets, a
	// close and a float that could not be told apart. They DO carry Qt's own
	// object names, which is identity read from the widget rather than from a
	// picture: the same shape as the arrowType case above, and the reason this
	// is not the iconography question it was filed as. The close mark is the
	// one PE_IndicatorTabClose already chose for a button drawn as a pixmap
	// that said nothing about what pressing it does.
	if (w) {
		const QString n = w->objectName();
		if (n == QLatin1String("qt_dockwidget_closebutton"))
			return QStringLiteral("✕");
		if (n == QLatin1String("qt_dockwidget_floatbutton"))
			return QStringLiteral("↗");
	}
	QString text = strip_mnemonic(tb->text);
	// An icon-only action, which is the common toolbar shape and had nothing
	// to draw: SH_ToolButtonStyle is pinned to text-only because a terminal
	// draws no icon, so an action carrying only a picture carried nothing at
	// all. Measured on a four-action toolbar -- "[Cut]" and "[Find]" rendered
	// and the two icon-only actions occupied four cells between them and drew
	// nothing.
	//
	// The tool tip is where such an action already keeps its words: it is
	// what Qt shows on hover, what a screen reader announces, and what an
	// application has therefore already written. A word beats the letter
	// design.md's Compact::IconsToLetters asks for, and it costs the
	// application nothing new -- which is why this is unconditional rather
	// than a hint. section 8 records that divergence.
	if (text.isEmpty() && w) {
		text = w->toolTip();
		if (text.isEmpty()) {
			if (auto *btn = qobject_cast<const QToolButton *>(w))
				if (QAction *a = btn->defaultAction()) text = a->toolTip();
		}
		text = strip_mnemonic(text);
	}
	const QString glyph = glyph_for(w, tb->icon);
	if (glyph.isEmpty()) return text;
	if (text.isEmpty()) return glyph;
	return glyph + QLatin1Char(' ') + text;
}

static int s_cw = 8, s_ch = 16;
int GridMetrics::cw() { return s_cw; }
int GridMetrics::ch() { return s_ch; }
void GridMetrics::set(int cw, int ch) { s_cw = cw; s_ch = ch; }

// A QPointer, for the reason InputRouter gives for `grab_` and `hovered_`: a
// widget can be destroyed while something still points at it. This is
// process-wide state refreshed only on input, so the gap it has to survive is
// a widget destroyed by something that is NOT input -- a timer, a network
// reply, an application closing a dialog of its own accord -- and
// FrameScheduler's idle tick exists precisely to render in that window. A
// bare pointer never crashed here, because every use is a comparison rather
// than a dereference; what it could do is quieter, and worse for being
// quiet. Qt reuses heap addresses, so a new widget landing where the old one
// was compares EQUAL and draws itself focused while focus is elsewhere.
//
// It also makes the public accessor honest: focusWidget() answers null once
// the widget is gone, rather than a pointer its caller must not follow.
static QPointer<QWidget> s_focus;
QWidget *focusWidget() { return s_focus.data(); }
void set_focus_widget(QWidget *w) { s_focus = w; }

// Reverse video on the control that owns focus. design.md F10 settled this in
// the spike -- "moving focus to a button changes exactly the button's cells to
// reverse-video" -- and until now exactly two controls did it, the push button
// and the tool button.
//
// Measured 2026-09-01, with focus moved to each of nine widgets in turn and
// the whole frame compared cell by cell including attributes: not one of the
// other seven changed a cell it owned. What hid it is that the terminal's
// hardware cursor landed on every one of them, for a reason that is itself
// wrong -- see Compositor::compose().
//
// The mark goes on the control's own glyph -- the check box's brackets, the
// slider's handle, the scroll bar's thumb -- and not on everything the widget
// covers. A whole reversed scroll bar says "focused" by shouting, where this
// style already spells pressed, checked and selected the same quiet way.
//
// A line edit is deliberately not in the list: it has a real caret, which
// says where typing goes as well as that typing goes here, and reversing the
// field would hide it.
// Every Channel A draw stays inside the widget it is drawing. Measured over
// twelve widget kinds at six sizes each, with the widget's minimum cleared so
// the rectangle asked for is the rectangle it got: a one-cell QPushButton put
// three of "<OK>" outside itself, a one-row QGroupBox twelve cells, a QTabBar
// drew its tabs at their own widths whatever the bar's width was. Section 7.7
// had recorded one instance of this as a fault in its own right; it is one
// fault, and this is the bound rather than a dozen separate corrections.
//
// The WIDGET's rectangle rather than the option's, because a control is
// entitled to draw over its own frame inset -- CE_PushButtonLabel already
// takes w->rect() deliberately, to put its brackets where the bevel would be.
// An option rect is a hint about where the part goes; the widget rect is the
// promise about what belongs to somebody else.
//
// Cut, not elided, which is the rule Channel B's clip already states: a clip
// is not a shortage of room, and an ellipsis would be qtty inventing a
// character nobody drew.
// CellClip, painted_widget() and visible_rect() live in cell_geometry.h
// now: CellItemDelegate needs the same clip, and it was reachable only
// from this file. The comments that record why each is shaped as it is
// moved with them.

// A widget's rectangle cut down by every ancestor's, in the widget's own
// coordinates. On a pixel screen a parent clips its children and nothing here
// did: measured on a QListWidget six cells wide and ONE ROW tall, Qt gives the
// horizontal scroll bar y = -10 inside the list -- there is no room for it, so
// the layout puts it above the top edge -- and its arrows and thumb were drawn
// on the row above, over whatever widget was there.
//
// Qt is not wrong to place it there; a scroll bar that does not fit has to go
// somewhere, and on a screen the parent's clip makes the question moot. This
// is the clip.
//
// Stops at a window, because a top-level's logical parent is a different
// top-level and does not clip it -- a dialog is not bounded by the widget that
// opened it. An empty intersection means a child wholly outside its parent,
// and it draws nothing, which is what a screen would show.
// The offset is accumulated on the way up rather than asked for per ancestor.
// mapTo() walks the parent chain itself, so calling it once per level makes
// this quadratic in the depth -- and it runs on every style call. Measured:
// the 200x60 table render went 1.39 ms to 2.49 ms with the mapTo version.
// Whoever is actually being painted, which is not always the widget the style
// was handed. QComboMenuDelegate::paint() passes the COMBO BOX while painting
// into the drop-down's own view, so clipping to the style's `w` clipped a
// popup to the one-row combo that opened it -- measured, a four-item
// drop-down drew its first item and three blank lines. The paint device knows,
// and cells_of() has always asked it; the clip has to ask the same question or
// the two disagree about which widget the drawing belongs to.


static bool owns_focus(const QWidget *w) { return w && w == s_focus.data(); }
static Attrs focus_attrs(const QWidget *w) {
	return owns_focus(w) ? Attrs(Attr::Reverse) : Attrs();
}

// The CURRENT item of a view that has the keys -- the one an arrow key moves
// and Space or Return acts on. It was drawn nowhere at all: measured with a
// full to_snapshot() so that a colour-only difference could not hide, moving
// the current item of a three-item QListView changed ZERO cells, in
// ExtendedSelection with the selection cleared and in NoSelection, with the
// list confirmed as Qtty::focusWidget() and with CellItemDelegate installed
// and again without it. A user pressing an arrow key saw nothing move.
//
// Neither half of the question can be read off the option:
//
// - Qt sets State_HasFocus on the current item only when the view itself
//   hasFocus(), and no window activates here (project.md F4), so it is never
//   set. Measured on every item of both renders: hasfocus=0 throughout. The
//   router-owned focus is the one that answers, exactly as it does for the
//   push button, the tab bar and the scroll bar above.
// - The option carries no "this one is current" flag, so it is asked of the
//   view, which is why this needs a view at all.
//
// The widget a style is handed for an item is the VIEW, not its viewport:
// measured, w == opt.widget == the QListView and the QTableView, on the
// delegate's path and on the one Qt's own delegate takes. QAbstractItemView
// sets option->widget to itself and both delegates pass that straight
// through, so there is no viewport to climb out of and no parent to consult.
//
// Underline, not reverse, and the tab bar is the precedent: reverse already
// means "selected", and which item the keys would act on is a different fact
// from which items are chosen. A view with the keys shows its current item
// underlined; the same item selected as well is underlined AND reversed.
//
// Not static: the style's fill and the delegate's label land in the SAME
// cells, so they ask one function. Declared in cell_geometry.h beside
// with_state(), defined here because the body needs QAbstractItemView and that
// header is included by most of the tree.
bool item_view_current(const QStyleOptionViewItem *vi, const QWidget *w) {
	if (!vi || !vi->index.isValid() || !owns_focus(w)) return false;
	const auto *view = qobject_cast<const QAbstractItemView *>(w);
	return view && vi->index == view->currentIndex();
}

// ------------------------------------------------- font provisioning (5.3/R3)

// One sentence, one place. Both the announcement and the refusal below have to
// say this and they used to carry a copy each -- which is the parallel-copy
// hazard code-style.md names, and it showed itself immediately: sabotaging one
// copy left `make test-platforms`' minimal arm green, because the other was
// still printing it.
static QString no_font_at_all(const QFont &font) {
	return QStringLiteral("'%1' resolved to no font at all; the font database"
	                      " offers %2 families")
	       // Through an instance rather than statically. Qt 6 made
	       // QFontDatabase static-only and keeps the constructor without
	       // deprecating it, so this spelling compiles clean on both -- 0
	       // warnings under -Wall -Wextra -Wdeprecated-declarations on 6.8
	       // and on 5.15 alike, measured. It is NOT a version conditional,
	       // which is why it does not settle the question section 8.1
	       // reserves: it takes no position on which Qt versions are
	       // supported, it just stops being one of the things that would
	       // have to change if the answer were ever "both".
	       .arg(font.family()).arg(QFontDatabase().families().size());
}

QString grid_font_substitution(const QFont &font) {
	const QFontInfo info(font);
	if (info.family().compare(font.family(), Qt::CaseInsensitive) == 0)
		return QString();
	// What is tested here is "Qt resolved this to a different name", and the
	// first version of this sentence said "is not installed" -- a cause it
	// never asked about. Under the `minimal` platform, which ships no font
	// database, an INSTALLED DejaVu Sans Mono resolves to '' and the message
	// sent the reader to install a font they already had. The count is the
	// part that separates the two: nothing resolves when there is nothing to
	// resolve against.
	if (info.family().isEmpty()) return no_font_at_all(font);
	return QStringLiteral("'%1' resolved to '%2', which is not the family"
	                      " asked for")
	       .arg(font.family(), info.family());
}

QString grid_font_problem(const QFont &font) {
	const QFontInfo info(font);
	// Asked before fixed-pitch, because "which is not fixed pitch" about an
	// empty family is a statement about nothing: fixedPitch() is false for a
	// font that was never resolved, so the message accused a font of being
	// proportional when none had been found.
	if (info.family().isEmpty()) return no_font_at_all(font);
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


// A framed widget that owns focus gets the double-line box. The two sets live
// in the same Unicode block, so a font with one has the other, and saying it
// with glyphs rather than with an attribute matters here: the contents of a
// focused list are already using reverse video to say which row is selected,
// and a reversed border would be competing with them for the same signal.
// The attributes are ORed rather than assigned: a box drawn over a filled
// region keeps the fill's reverse video, and this used to write the glyph and
// nothing else -- so a disabled list view's frame stayed at full brightness
// around dimmed contents.
static void draw_box(CellBuffer &b, const QRect &c, bool focused = false,
                     Attrs a = Attrs()) {
	if (c.width() < 2 || c.height() < 2) return;
	const QString h  = focused ? QStringLiteral("═") : QStringLiteral("─");
	const QString v  = focused ? QStringLiteral("║") : QStringLiteral("│");
	const QString tl = focused ? QStringLiteral("╔") : QStringLiteral("┌");
	const QString tr = focused ? QStringLiteral("╗") : QStringLiteral("┐");
	const QString bl = focused ? QStringLiteral("╚") : QStringLiteral("└");
	const QString br = focused ? QStringLiteral("╝") : QStringLiteral("┘");
	// Through writable(), because at() is the RAW accessor and is deliberately
	// not clipped -- reads go through it too. Adding the buffer's clip and
	// leaving this alone left every box in the library still unbounded, which
	// a probe found immediately: a QGroupBox six cells wide and one row tall
	// drew a complete twelve-cell box on the two rows BELOW itself, because
	// subControlRect gives it a frame rect that needs a height it does not
	// have. The clip caught the group box's title and not its frame.
	auto put = [&](int x, int y, const QString &g) {
		if (!b.writable(x, y)) return;
		b.at(x, y).ch = g;
		b.at(x, y).attrs |= a;
	};
	for (int x = c.left() + 1; x < c.right(); ++x) {
		put(x, c.top(), h);
		put(x, c.bottom(), h);
	}
	for (int y = c.top() + 1; y < c.bottom(); ++y) {
		put(c.left(), y, v);
		put(c.right(), y, v);
	}
	put(c.left(), c.top(), tl);
	put(c.right(), c.top(), tr);
	put(c.left(), c.bottom(), bl);
	put(c.right(), c.bottom(), br);
}

GridStyle::GridStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

// Which way a slider runs, for the metrics that measure along its axis. A
// null option means no slider is asking -- Qt queries these metrics without
// one -- and horizontal is the answer that matches what those callers expect.
static bool slider_vertical(const QStyleOption *opt) {
	if (auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt))
		return sl->orientation == Qt::Vertical;
	return false;
}

int GridStyle::pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	switch (m) {
	// Vertical space is precious and horizontal space is not, which is the
	// rule the spacings below already state -- one column between items and
	// nothing between rows. The margins said the opposite: a whole ROW above
	// the first widget and another below the last, on a screen with
	// twenty-four of them.
	//
	// It cost more than the eight per cent. Measured on an 80x1 terminal, a
	// window with a plain QVBoxLayout rendered ENTIRELY BLANK -- the menu bar
	// sat at y=19, one row below the only row there was. That was written up
	// as Qt's default nine-pixel margins rounding badly and it was neither
	// Qt's nor rounding: it was this line.
	//
	// Left and right keep a column. A column of eighty is cheap where a row
	// of twenty-four is not, and the indent is what stops text touching the
	// screen edge.
	case PM_LayoutLeftMargin: case PM_LayoutRightMargin:   return cw;
	case PM_LayoutTopMargin:  case PM_LayoutBottomMargin:  return 0;
	case PM_LayoutHorizontalSpacing:                       return cw;
	case PM_LayoutVerticalSpacing:                         return 0;
	case PM_ScrollBarExtent:                               return cw;
	// The close button QTabBar builds for a closable tab. Qt sizes it from
	// these and nothing overrode them, so it came out 20x20 -- off the grid
	// vertically, and reported by the guard as a widget the application
	// cannot reach to fix, which is exactly the class that would otherwise
	// have to be exempted by name. A cell is the right size for it: what it
	// holds is one glyph.
	case PM_TabCloseIndicatorWidth:                        return cw;
	case PM_TabCloseIndicatorHeight:                       return ch;
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
	case PM_MenuHMargin:                                   return 0;
	// The vertical half of a popup's frame, which PM_MenuPanelWidth cannot
	// supply: it is one number and a cell is not square. A panel of `cw` is a
	// whole column and 10/19 of a row, so a combo box's popup came out 3.05
	// cells tall -- measured, 220x58 px with its list view at y=10 -- and the
	// second item drew ON the frame's bottom border:
	//
	//     +--------------------+
	//     | v alpha            |
	//     +   beta             +
	//
	// the same fault as the group box in another widget. This margin makes the
	// vertical frame up to a whole row, so the panel costs one column on each
	// side and one row on each side.
	case PM_MenuVMargin:                                   return ch - cw;
	case PM_MenuBarHMargin: case PM_MenuBarVMargin:        return 0;
	case PM_MenuBarItemSpacing:                            return 2 * cw;
	case PM_TabBarTabHSpace:                               return 2 * cw;
	case PM_TabBarTabVSpace:                               return 0;
	case PM_TabBarBaseHeight: case PM_TabBarBaseOverlap:   return 0;
	case PM_TabBarTabShiftHorizontal:
	case PM_TabBarTabShiftVertical:                        return 0;
	case PM_ProgressBarChunkWidth:                         return cw;
	// Along the axis and across it, which these did not distinguish. Qt
	// treats PM_SliderLength as the handle's size ALONG the slider, so
	// 3 * cw for a vertical one is 30 px against a 19 px row -- 1.58 rows,
	// fractional on the axis it measures. The drawing paints a handle ONE
	// cell long, so that is what the metric says, and the thickness is the
	// cell across the axis.
	case PM_SliderThickness: case PM_SliderControlThickness:
		return slider_vertical(o) ? cw : ch;
	case PM_SliderLength:
		return slider_vertical(o) ? ch : cw;
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
	// The one metric in this switch that was never gridded, and it cost a
	// message box half a cell of height it could not use: Fusion answers 48
	// px, which is 2.53 rows and 4.8 columns, so the dialog came out 3.5
	// cells tall. Two whole rows keeps it a PICTURE -- 4x2 cells, still above
	// the "two cells in each direction" the placement rule wants -- and makes
	// the dialog 3.0 cells. Whether it should be a glyph instead is a
	// different question and the copyright holder's; this is the same
	// rounding every other metric here already does.
	case PM_MessageBoxIconSize:                            return 2 * ch;
	case PM_DockWidgetSeparatorExtent:                     return cw;
	case PM_HeaderMargin:                                  return 0;
	case PM_HeaderGripMargin:                              return cw;
	// A header's default SECTION size, which is where every row and column of
	// an item view comes from: a QTableView takes its row height from the
	// vertical header's default and its column width from the horizontal
	// one's, whether or not either header is shown.
	//
	// Fusion answers 30 px for the vertical one. That is 1.58 rows at ch = 19,
	// so a four-row table put its model rows on buffer rows 1, 3, 4 and 6 --
	// a blank line between the first two and none between the next -- and a
	// selected row reversed TWO buffer rows, its own and the top of the one
	// below. Measured, with the section size left at the default: rows at
	// pixel 0, 30, 60, 90 on a 19-pixel grid.
	//
	// The horizontal one is 100 px, and it is on the grid HERE only because
	// this machine's cell happens to be 10 px wide. It is the same wrong
	// number as the vertical one; nothing on this machine can see it, which
	// is why the check that pins it asks at a second cell size rather than
	// dividing this one by cw. Ten columns is the same default said in the
	// unit the grid is measured in.
	case PM_HeaderDefaultSectionSizeVertical:              return ch;
	// A tree's indent band is both the expander's picture and its hit
	// target: QTreeView::drawBranches and itemDecorationRect build the same
	// rectangle from this. QCommonStyle answers a hardcoded 20 px, so the
	// band is a whole number of cells only where cw divides 20 -- at cw = 8
	// it rounds to three cells, the glyph is drawn in a cell whose centre is
	// past the band's right edge, and the expander cannot be clicked at any
	// level. Two columns, which is the same 20 px on this machine said in
	// the unit the grid is measured in, so nothing here moves.
	case PM_TreeViewIndentation:                           return 2 * cw;
	// The sort indicator's cell. QCommonStyle answers `fontMetrics.height()
	// * 5 / 8`, a pixel count that need not be a whole column -- and both
	// SE_HeaderLabel and SE_HeaderArrow are derived from it by INDEPENDENT
	// roundings, so where it lands on a half cell the arrow takes the
	// label's last cell. That cell is the ellipsis whenever the label was
	// elided, so a truncated column would read as a complete one that
	// happens to be sorted. One column, which no rounding can disagree
	// about. Not reachable at this machine's 10x19 or at the 8x16 default
	// -- it needs the mark to land on a half column -- which is exactly why
	// the metric rather than a rendering is what is asserted.
	case PM_HeaderMarkSize:                                return cw;
	// A menu's two strips, both of which QCommonStyle answers as 10 px --
	// no whole number of rows at either cell size. They are HEIGHTS, and
	// each is added to the y-origin of every item below it, so an ungridded
	// one displaces the whole menu rather than just itself.
	//
	// The tearoff is reachable today: QMenu::setTearOffEnabled(true) puts it
	// into base_y for every item. The scroller is not -- a menu scrolls only
	// under SH_Menu_Scrollable, which nothing here turns on, and a combo's
	// popup is never shortened because every qtty window carries
	// WA_DontShowOnScreen, so Qt does not bound it to a screen. Answered
	// anyway, because the two are the same number in the same arithmetic and
	// fixing one while leaving the other is how a pair drifts.
	case PM_MenuScrollerHeight:
	case PM_MenuTearoffHeight:                             return ch;
	case PM_HeaderDefaultSectionSizeHorizontal:            return 10 * cw;
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
// The close button on a closable tab. QTabBar places it inside the tab rect
// at a pixel offset, so it lands between columns however it is sized -- and
// the guard reports it, correctly, as a widget the application cannot reach.
//
// Answered rather than exempted, which is the choice worth recording. An
// exemption would silence the report and leave the button drawn half in one
// cell and half in the next; snapping the rectangle the style is asked for
// puts it in a cell, which is where a one-glyph button belongs. The
// exemption list exists for widgets nothing can place, and this is not one
// of them: the style is asked, so the style can answer.
QRect GridStyle::subElementRect(SubElement se, const QStyleOption *opt,
                                const QWidget *w) const {
	QRect r = QProxyStyle::subElementRect(se, opt, w);
	// The vertical half of a framed widget's inset, which
	// PM_DefaultFrameWidth cannot supply: it is one number and a cell is not
	// square. QFrame insets its contents -- and QAbstractScrollArea its
	// VIEWPORT -- by that number on all four sides, so a view's first model
	// row sat ten pixels down a nineteen-pixel row. Measured: the viewport's
	// origin was 10,10. Same fault and same remedy as PM_MenuVMargin, which
	// exists because PM_MenuPanelWidth could not say the vertical half
	// either.
	//
	// Only an inset this style produced: a Box or Panel frame is inset by
	// its own lineWidth and is not ours to move, and NoFrame has none.
	//
	// And nothing below THREE rows. Two borders and a content row is the
	// smallest thing this can describe: at exactly two rows the subtraction
	// gives a height of zero, which is not a rectangle Qt should be handed
	// -- QRect calls it invalid, and QFrame derives its four widths from it.
	// Below that the frame keeps Fusion's answer, which is wrong by less
	// than an empty viewport is.
	if ((se == SE_FrameContents || se == SE_ShapedFrameContents) && opt
	    && r.isValid()) {
		const int ch = GridMetrics::ch();
		const int fw = pixelMetric(PM_DefaultFrameWidth, opt, w);
		if (r.top() - opt->rect.top() == fw
		    && opt->rect.bottom() - r.bottom() == fw
		    && opt->rect.height() >= 3 * ch)
			return QRect(r.left(), opt->rect.top() + ch,
			             r.width(), opt->rect.height() - 2 * ch);
	}
	// The VERTICAL half of a line edit's inset, from the same number and for
	// the same reason. QLineEdit puts PM_DefaultFrameWidth into lineWidth and
	// QCommonStyle insets by it on all four sides, so a one-row field's
	// contents rect comes out 19 - 2 * 10 = -1 pixels tall. QRect calls that
	// invalid, and Qt then hands it to setClipRect() and to the alignment
	// arithmetic without asking.
	//
	// Centred text survived it by cancellation -- a symmetric inset drops out
	// of the centring formula exactly -- which is why nothing noticed. Top-
	// and bottom-aligned text reads the rect's own y and height instead, and
	// landed a row outside the clip: measured, a one-row field with
	// AlignTop rendered BLANK, all six characters of it.
	//
	// The HORIZONTAL inset is kept and is not incidental: PE_PanelLineEdit
	// writes '[' at the first cell and ']' at the last, so a column each side
	// is exactly what the drawing reserves. Only the second axis needs
	// saying, as PM_MenuVMargin already does for PM_MenuPanelWidth.
	//
	// No isValid() guard here, deliberately: r is invalid in precisely the
	// case being fixed, and copying the one above would make this a no-op at
	// one row -- the whole of the defect. The frameless test is
	// r.top() > opt->rect.top(), which is false exactly when lineWidth is 0,
	// so a combo's and a spin box's inner editors are untouched by
	// construction rather than by a class list.
	if (se == SE_LineEditContents && opt && r.top() > opt->rect.top()) {
		const int ch = GridMetrics::ch();
		// Nothing below three rows, as above: two borders and a content row.
		const int v = opt->rect.height() >= 3 * ch ? ch : 0;
		return QRect(r.left(), opt->rect.top() + v,
		             r.width(), opt->rect.height() - 2 * v);
	}
	// The cells the check box is DRAWN in. Both CE_ItemViewItem here and
	// CellItemDelegate put "[x]" at one cell of indent, so cells 1, 2 and 3
	// of the item. QCommonStyle builds this rectangle from PM_IndicatorWidth
	// -- which this style answers correctly, three cells -- and a margin of
	// PM_FocusFrameHMargin + 1, i.e. one PIXEL, so it lands at cells 0, 1
	// and 2. One cell apart from the picture at every cell size, which is
	// not the parity accident the scroll bar's arrows had.
	//
	// Measured before this: clicking the blank indent cell toggled the item
	// and clicking the box's own closing bracket did nothing.
	//
	// Full item height on purpose. The box is drawn on one row and the item
	// may be taller, but those columns hold nothing else -- the text starts
	// after them -- so a click anywhere in them means the box, and demanding
	// the exact row would mean deriving it twice, once here and once in each
	// of the two drawing paths.
	if (se == SE_ItemViewItemCheckIndicator && opt) {
		const int cw = GridMetrics::cw();
		if (r.isValid())
			return QRect(opt->rect.left() + cw, opt->rect.top(),
			             3 * cw, opt->rect.height());
	}
	if (se == SE_TabBarTabRightButton || se == SE_TabBarTabLeftButton) {
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		r.moveLeft(qRound(double(r.left()) / cw) * cw);
		r.moveTop(qRound(double(r.top()) / ch) * ch);
		// Nothing else. A version of this moved the button to the cell
		// before the tab's closing bracket, on the theory that snapping put
		// it in a cell but not the right one -- and removing that again
		// changed no rendered cell, because once the bracket spans the whole
		// tab (CE_TabBarTab) Qt's own position is already inside it. The
		// sabotage said so, and the line came out: a fix that cannot be shown
		// to do anything is a fix for a defect that was somewhere else.
	}
	return r;
}

// One row for a title, none without one. A check box in the title takes the
// same row, which is why this asks the option rather than the text alone.
static int title_rows(const QStyleOptionComplex *opt) {
	if (auto *gb = qstyleoption_cast<const QStyleOptionGroupBox *>(opt))
		return (!gb->text.isEmpty()
		        || (gb->subControls & QStyle::SC_GroupBoxCheckBox)) ? 1 : 0;
	return 0;
}

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
			             qMax(cw, r.width() - 4 * cw), qMax(ch, r.height()));
		// A cell each, side by side, rather than a cell split in half. The
		// halves were r.height()/2 apart -- nine pixels on a nineteen-pixel
		// cell -- so both rectangles covered the same cell and only the first
		// could be clicked.
		case SC_SpinBoxUp:
			return QRect(r.right() + 1 - 3 * cw, r.top(), cw, qMax(ch, r.height()));
		case SC_SpinBoxDown:
			return QRect(r.right() + 1 - 2 * cw, r.top(), cw, qMax(ch, r.height()));
		case SC_SpinBoxFrame:
			return r;
		default:
			break;
		}
	}
	// The tool button's menu area. It draws a down-arrow glyph in the cell before its
	// closing bracket -- the affordance that says a menu is there -- and Qt
	// asked SC_ToolButtonMenu, which QCommonStyle builds from
	// PM_MenuButtonIndicator: 12 px, ungridded. At a ten-pixel cell the live
	// band is the last 12 px, which is the closing BRACKET and not the
	// arrow. Measured: pressing the arrow cell fired the default action and
	// opened nothing.
	//
	// The arrow's cell, and only that. The bracket is the button's own
	// frame, drawn for a button with no menu as well, so it belongs to the
	// button -- one meaning per cell, which is the rule the spin box's two
	// arrows established here.
	if (cc == CC_ToolButton && sc == SC_ToolButtonMenu && opt) {
		if (auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(opt)) {
			if (tb->features & QStyleOptionToolButton::MenuButtonPopup) {
				const QRect r = opt->rect;
				return QRect(r.right() + 1 - 2 * cw, r.top(), cw, r.height());
			}
		}
	}
	// The scroll bar is drawn whole in cells and its hit test was left to
	// Fusion, which answers in pixels from PM_ScrollBarExtent. The two
	// disagreed about the one thing a user does with a scroll bar.
	//
	// Measured on a six-row vertical bar at value 50 of 100, clicking the
	// centre of each drawn row: 49, 40, 50, 50, 60, 51. Row 3 is drawn as
	// track and did nothing -- Fusion's thumb is PM_ScrollBarSliderMin, 26
	// px, which is 1.37 rows, so it covered a row the picture said was
	// track. The fixture that pinned this behaviour clicked rows 0, 1, 2, 4
	// and 5 and skipped 3.
	//
	// The arithmetic below is the DRAWING's, deliberately: thumb_len and
	// thumb_pos are copied from drawComplexControl's CC_ScrollBar arm
	// because the hit test has to agree with the picture, and two
	// derivations of one layout is what put this file's spin box arrows in
	// the same cell.
	if (cc == CC_ScrollBar && opt) {
		if (auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
			const QRect r = opt->rect;
			const bool vert = sb->orientation == Qt::Vertical;
			const int cell = vert ? ch : cw;
			const int len = (vert ? r.height() : r.width()) / qMax(1, cell);
			if (len >= 2) {
				const int track = len - 2;
				const int span = sb->maximum - sb->minimum;
				int thumb_len = 1, thumb_pos = 0;
				if (span > 0 && track > 0) {
					thumb_len = qBound(1, track * sb->pageStep
					                     / qMax(1, span + sb->pageStep), track);
					thumb_pos = (track - thumb_len)
					          * (sb->sliderPosition - sb->minimum) / span;
				}
				// A run of whole cells along the axis, full width across it.
				const auto band = [&](int first, int count) {
					if (count <= 0) return QRect();
					return vert
					    ? QRect(r.left(), r.top() + first * ch, r.width(), count * ch)
					    : QRect(r.left() + first * cw, r.top(), count * cw, r.height());
				};
				switch (sc) {
				case SC_ScrollBarSubLine: return band(0, 1);
				case SC_ScrollBarAddLine: return band(len - 1, 1);
				case SC_ScrollBarSlider:  return band(1 + thumb_pos, thumb_len);
				case SC_ScrollBarSubPage: return band(1, thumb_pos);
				case SC_ScrollBarAddPage:
					return band(1 + thumb_pos + thumb_len,
					            track - thumb_pos - thumb_len);
				case SC_ScrollBarGroove:  return band(1, track);
				case SC_ScrollBarFirst:
				case SC_ScrollBarLast:    return QRect();
				default: break;
				}
			}
		}
	}
	// The slider is drawn as ONE handle cell over the whole length, and Qt
	// maps a THREE-cell handle over `length - 3 cells`. Two mappings of value
	// to position, so a click on the handle the user can see misses it
	// wherever they disagree -- and the miss does not reach the groove
	// either: Fusion centres a seven-pixel groove on the widget, which at the
	// hint width of two cells spans 6..12 and contains neither cell centre,
	// 5 nor 15. Measured at that width, every row of a six-row vertical
	// slider left the value at 50. The control could not be moved by mouse
	// at all.
	//
	// The arithmetic is the drawing's, including the upsideDown flip, for
	// the reason the scroll bar's is: the hit test has to agree with the
	// picture.
	if (cc == CC_Slider && opt) {
		if (auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
			const QRect r = opt->rect;
			const bool vert = sl->orientation == Qt::Vertical;
			const int cell = vert ? ch : cw;
			const int len = (vert ? r.height() : r.width()) / qMax(1, cell);
			if (len >= 1) {
				const int span = sl->maximum - sl->minimum;
				int pos = span > 0
				    ? (len - 1) * (sl->sliderPosition - sl->minimum) / span : 0;
				if (sl->upsideDown) pos = len - 1 - pos;
				pos = qBound(0, pos, len - 1);
				switch (sc) {
				case SC_SliderHandle:
					return vert
					    ? QRect(r.left(), r.top() + pos * ch, r.width(), ch)
					    : QRect(r.left() + pos * cw, r.top(), cw, r.height());
				case SC_SliderGroove: return r;
				default: break;
				}
			}
		}
	}
	if (cc == CC_GroupBox && opt) {
		// Fusion answers a 25-pixel contents top -- a title's height in
		// pixels -- and 25 is 1.3 cells. GridSnap rounds to NEAREST, so the
		// first child inside the box rounded DOWN onto the frame's own top
		// row and drew through it:
		//
		//     +[ ]-Option 0--------+   the frame's own top border
		//     |                    |
		//     +[ ]-Option 2--------+   and its bottom one
		//
		// Measured on a group box squeezed to five rows; at six it was fine,
		// which is why it took a small terminal to see. This is the overlap
		// section 7.8 named as the risk to measure before writing any
		// snapping -- "whether closing a layout's gap can overlap two
		// widgets" -- arriving in the wild, and the answer is not to change
		// the rounding but to hand it rectangles it cannot round wrong.
		//
		// A titled box therefore spends one row on the title, one on the
		// frame's top border, and starts its contents on the row after. An
		// untitled one has no title row to spend.
		const QRect r = opt->rect;
		const int title = title_rows(opt);
		const bool checkable = opt->subControls & SC_GroupBoxCheckBox;
		switch (sc) {
		// The indicator's own cells. QCommonStyle places it at eight PIXELS
		// in -- cell 1 at a ten-pixel cell, cell 2 at a four-pixel one --
		// and draws it AFTER the title, so it overwrote whatever the title
		// had put there. Measured on a fourteen-cell box titled "Advanced":
		// the buffer read "[x]anced".
		case SC_GroupBoxCheckBox:
			return checkable ? QRect(r.left(), r.top(), 3 * cw, ch) : QRect();
		// And the title starts after it. Reserving the cells is the half
		// that makes the fix sound at any cell size: moving the indicator
		// alone would leave the title centred across it again on a box
		// narrow enough, which is exactly the case this was found in.
		case SC_GroupBoxLabel: {
			const int in = checkable ? 4 * cw : 0;
			return title ? QRect(r.left() + in, r.top(),
			                     qMax(cw, r.width() - in), ch)
			             : QRect();
		}
		case SC_GroupBoxFrame:
			return QRect(r.left(), r.top() + title * ch,
			             r.width(), qMax(2 * ch, r.height() - title * ch));
		case SC_GroupBoxContents:
			return QRect(r.left() + cw, r.top() + (title + 1) * ch,
			             qMax(cw, r.width() - 2 * cw),
			             qMax(ch, r.height() - (title + 2) * ch));
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
	// A tab bar down the side is a column of rows, not a rotated strip. Qt
	// hands a West or East tab its contents size already rotated -- narrow
	// and tall -- so taking that width gave a tab two cells wide and the
	// label elided to "[...", which is what a vertical tab bar rendered as.
	// Measured from the text like a tool button, the bar becomes as wide as
	// its longest label and each tab is one row, which is what a terminal
	// application with side tabs looks like.
	case CT_TabBarTab:
		if (auto *t = qstyleoption_cast<const QStyleOptionTab *>(o)) {
			const bool vertical = t->shape == QTabBar::RoundedWest
			                   || t->shape == QTabBar::RoundedEast
			                   || t->shape == QTabBar::TriangularWest
			                   || t->shape == QTabBar::TriangularEast;
			if (vertical) {
				int cells = 0;
				for (const QString &cl : to_clusters(strip_mnemonic(t->text)))
					cells += cluster_width(cl);
				// A ROW for a close button, where the tab carries one. This
				// branch rebuilds the size from the text and so discards the
				// proxied one -- which is where Qt reserves room for the
				// button -- and the button then has nowhere of its own:
				// SE_TabBarTabRightButton centres it over the label and
				// snaps it to the next row, which on a one-row tab is the
				// NEXT TAB. Measured on a squeezed West bar: the second tab
				// rendered "[Adva" then the close glyph then "ced".
				//
				// A row rather than a column because that is where Qt puts
				// it once there is room: given two rows a vertical tab draws
				// its label on the first and its button on the second, which
				// is what this now asks for.
				const int rows = (t->rightButtonSize.isValid()
				                  || t->leftButtonSize.isValid()) ? 2 : 1;
				return QSize((cells + 2) * cw, rows * ch);   // + two brackets
			}
		}
		return QSize(width, ch);

	// Measured from the row CE_ItemViewItem DRAWS, the way CT_TabBarTab and
	// CT_ToolButton above are: one cell of indent, four more for "[x] " when
	// there is an indicator, then the label in clusters. Fusion's answer
	// absorbs the indicator's two-pixel margin in the ceiling, so a
	// CHECKABLE item came out a cell short -- measured at nine cells for a
	// row this style draws in ten -- and a column sized to its contents
	// elided the last character. A plain item was already right, which is
	// why the derived width is taken with qMax rather than outright: it
	// widens the case that is short and moves nothing else, including a
	// decorated item, whose icon CellItemDelegate draws and this does not.
	case CT_ItemViewItem:
		if (auto *vi = qstyleoption_cast<const QStyleOptionViewItem *>(o)) {
			int cells = CellItemDelegate::indent_cells();
			if (vi->features & QStyleOptionViewItem::HasCheckIndicator)
				cells += CellItemDelegate::check_cells();
			for (const QString &cl : to_clusters(vi->text))
				cells += cluster_width(cl);
			return QSize(qMax(width, cells * cw), snapped);
		}
		return QSize(width, snapped);
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

// with_state() was here, file-static, and is now in cell_geometry.h beside
// the other rules this file shares with CellItemDelegate. It moved because
// the delegate needs the same answer and was giving a different one: a
// disabled item view drew its padding dim from the fill below and then had
// the label written over it with no attributes at all.

void GridStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                              const QWidget *w) const {
	if (auto *dev = cell_target(p)) {
		QRect c = cells_of(opt->rect, p, dev, w);
		const QWidget *pw = painted_widget(p, w);
		const CellClip bound(dev, pw ? cells_of(visible_rect(pw), p, dev, pw) : c);
		switch (pe) {
		case PE_IndicatorCheckBox:
			// Three states, three glyphs. A tristate box at PartiallyChecked
			// arrives as State_NoChange and drew "[ ]" -- identical to
			// unchecked, so the middle state existed in the model and not on
			// the screen, and the only way to tell was to click and watch it
			// cycle somewhere unexpected. Qt sets State_On for Checked and
			// State_NoChange for the middle, and neither for Unchecked.
			dev->buffer().text(c.left(), c.top(),
			    (opt->state & State_NoChange) ? QStringLiteral("[-]")
			    : (opt->state & State_On)     ? QStringLiteral("[x]")
			                                  : QStringLiteral("[ ]"),
			    Color(), Color(), with_state(opt) | focus_attrs(w));
			return;
		case PE_IndicatorRadioButton:
			dev->buffer().text(c.left(), c.top(),
			    (opt->state & State_On) ? QStringLiteral("(o)") : QStringLiteral("( )"),
			    Color(), Color(), with_state(opt) | focus_attrs(w));
			return;
		case PE_FrameWindow: case PE_Frame: case PE_FrameGroupBox:
		case PE_PanelMenu: case PE_FrameMenu:
		// A tab pane is a frame like the others, and letting the base style
		// draw it was the whole of section 7.7's gradient finding. Fusion fills a
		// pane with a gradient, and the engine recovers a role by comparing
		// the brush colour against each role's for exact equality -- so the
		// stop colour #fbfbfb, which is no role, fell through to a
		// true-colour background and put a near-white block behind every tab
		// page. On a dark terminal that is exactly as bad as it sounds.
		//
		// Drawing the box here means Fusion never runs and there is no fill
		// to classify. That is the third of the three fixes 7.7 recorded --
		// suppress the fill for a region Channel A has already drawn as a box
		// -- and it needed no new mechanism, only this case label.
		case PE_FrameTabWidget:
			// A framed widget is the only thing an item view, a text edit or
			// a scroll area gives this style to mark, and until now nothing
			// marked them: focus moved into a list and not one cell changed.
			// The tab pane is in this group and never matches, because a
			// QTabWidget hands focus to its tab bar through a focus proxy --
			// which the bar's own case answers.
			draw_box(dev->buffer(), c, owns_focus(w), with_state(opt));
			return;
		// A one-row line edit is bracketed, the way the combo box and the spin
		// box below already are and for the reason written there: the control
		// has to be visibly a control, and at one row a frame cannot say so --
		// draw_box() needs two rows and silently draws nothing below that. An
		// empty field was invisible until tabbed through, and a form of them
		// was a blank screen.
		//
		// hasFrame() is what makes it safe, and it needs no class list and no
		// parent test. Bracketing every panel call gave an editable combo box
		// and a spin box a SECOND closing bracket inside their own, because
		// each contains a QLineEdit that reaches this primitive -- measured,
		// and the reason this was deferred. Those two answer false because
		// they draw the boundary themselves, and an application that calls
		// setFrame(false) answers false and is obeyed. The widget says
		// whether it wants a boundary; this only asks.
		case PE_PanelLineEdit: {
			if (c.height() >= 2) {
				draw_box(dev->buffer(), c, owns_focus(w), with_state(opt));
				return;
			}
			const auto *le = qobject_cast<const QLineEdit *>(w);
			if (le && le->hasFrame()) {
				dev->buffer().put_cluster(c.left(), c.top(), QStringLiteral("["),
				                          Color(), Color(), with_state(opt));
				dev->buffer().put_cluster(c.right(), c.top(), QStringLiteral("]"),
				                          Color(), Color(), with_state(opt));
			}
			return;
		}
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
				dev->buffer().put_cluster(c.left(), y, QStringLiteral("│"),
				                          Color(), Color(), with_state(opt));
			return;
		case PE_IndicatorBranch: {                    // tree expanders (section 17.2)
			QString g = QStringLiteral(" ");
			if (opt->state & State_Children)
				g = (opt->state & State_Open) ? QStringLiteral("▾") : QStringLiteral("▸");
			const Attrs a = with_state(opt);
			dev->buffer().text(c.right(), c.top(), g, Color(), Color(), a);
			return;
		}
		case PE_IndicatorTabClose:
			// Drawn by the base style as a pixmap, so it arrived as the
			// tiny-icon substitute: a closable tab offered a shaded block to
			// click on, which says nothing about what clicking it does.
			dev->buffer().text(c.left(), c.top(), QStringLiteral("✕"),
			                   Color(), Color(), with_state(opt));
			return;
		case PE_IndicatorHeaderArrow:
			// A sort indicator, which fell through to the base style and was
			// drawn as a PIXMAP -- so it arrived at the cell painter as an
			// image too small to place and came out as the tiny-icon
			// substitute, a shaded block. A column sorted ascending and one
			// sorted descending were the same meaningless mark.
			if (auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
				if (h->sortIndicator == QStyleOptionHeader::None) return;
				// SortDown is ASCENDING. Measured, not read off the enum:
				// QHeaderView sets sortIndicator to SortDown when the order
				// is Qt::AscendingOrder, so taking the name at face value
				// drew an A-to-Z column with a downward arrow and a Z-to-A
				// one with an upward arrow -- confidently backwards, which is
				// worse than the shaded block this replaced, that at least
				// claimed nothing.
				dev->buffer().text(c.left(), c.top(),
				    h->sortIndicator == QStyleOptionHeader::SortDown
				        ? QStringLiteral("▴") : QStringLiteral("▾"),
				    Color(), Color(), with_state(opt));
			}
			return;
		case PE_IndicatorArrowDown:  dev->buffer().text(c.left(), c.top(), QStringLiteral("▾"),
		                     Color(), Color(), with_state(opt)); return;
		case PE_IndicatorArrowUp:    dev->buffer().text(c.left(), c.top(), QStringLiteral("▴"),
		                     Color(), Color(), with_state(opt)); return;
		case PE_IndicatorArrowLeft:  dev->buffer().text(c.left(), c.top(), QStringLiteral("◂"),
		                     Color(), Color(), with_state(opt)); return;
		case PE_IndicatorArrowRight: dev->buffer().text(c.left(), c.top(), QStringLiteral("▸"),
		                     Color(), Color(), with_state(opt)); return;
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
		const QWidget *pw = painted_widget(p, w);
		const CellClip bound(dev, pw ? cells_of(visible_rect(pw), p, dev, pw) : c);
		switch (ce) {
		case CE_PushButtonBevel:
			return;                                   // bevel is the label's brackets
		case CE_PushButtonLabel:
			if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
				QRect bc = w ? cells_of(w->rect(), p, dev, w) : c;
				// State_HasFocus never arrives in TUI mode (F4): router focus.
				//
				// Sunken and On join it, which the tool button one case down
				// already does and this did not. A button held under the
				// pointer looked exactly like one at rest, so pressing it
				// gave no feedback at all until whatever it does happens --
				// the same shape as the disabled control, in the other
				// direction. A checkable button that is checked was equally
				// invisible, and Qt reports both in the same option.
				bool foc = (opt->state & (State_HasFocus | State_Sunken | State_On))
				           || (w && w == s_focus.data());
				// Elided to the room BETWEEN the brackets, so the closing
				// one survives. This wrote the whole label and let the clip
				// cut it: a button squeezed below its label rendered
				// "<Save Ch" -- no bracket, no ellipsis, nothing to say it
				// had been cut. Same fault the tab had, in another control,
				// and the bracket is what says where the button ends.
				dev->buffer().text(bc.left(), bc.top(),
				                   QLatin1Char('<')
				                       + elide_to_cells(strip_mnemonic(b->text),
				                                        qMax(0, bc.width() - 2))
				                       + QLatin1Char('>'),
				                   Color(), Color(),
				                   label_attrs(opt, w, foc ? Attrs(Attr::Reverse)
				                                           : Attrs()));
			}
			return;
		case CE_MenuItem:
			if (auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
				if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
					for (int x = c.left(); x <= c.right(); ++x)
						dev->buffer().put_cluster(x, c.top(), QStringLiteral("─"),
						                          Color(), Color(), with_state(opt));
					return;
				}
				const Attrs a = with_state(opt, (opt->state & State_Selected)
				                                ? Attrs(Attr::Reverse) : Attrs());
				// The fill carries the state and not the font: bold on a
				// space is nothing to look at and something to read in a
				// snapshot. The label carries both.
				const Attrs la = label_attrs(opt, w, mi->font, a);
				if (a) {                              // highlight spans the row
					Cell v; v.attrs = a;
					dev->buffer().fill(QRect(c.left(), c.top(), c.width(), 1), v);
				}
				// A checkable item's mark, which was not drawn at all: a
				// menu is where a toggle usually lives, and "Word Wrap" with
				// no tick beside it says nothing about whether it is on. The
				// state was in the action and nowhere on the screen.
				//
				// One cell, and the shape says which kind of toggle it is --
				// a tick for an independent one, a bullet for a member of an
				// exclusive group, matching the checkbox and radio button
				// this style already draws. The cell is reserved whenever the
				// item is checkable, so a run of checkable items aligns; an
				// ordinary item in the same menu starts one cell earlier,
				// which is the cost of deciding per item, Qt handing the
				// style one item at a time.
				int label_at = c.left() + 1;
				if (mi->checkType != QStyleOptionMenuItem::NotCheckable) {
					const bool one_of = mi->checkType
					                  == QStyleOptionMenuItem::Exclusive;
					const QString mark = !mi->checked ? QStringLiteral(" ")
					                   : one_of       ? QStringLiteral("•")
					                                  : QStringLiteral("✓");
					dev->buffer().text(label_at, c.top(), mark, Color(), Color(), la);
					label_at += 2;
				}
				const QStringList parts = mi->text.split(QLatin1Char('\t'));
				QString label = parts.value(0);
				label.remove(QLatin1Char('&'));       // mnemonic markers
				// The last cell the label may use. The row ends at
				// c.right() -- the item-view path one case down says the
				// same with `- text_at + 1` -- less the arrow cell a submenu
				// spends, and less the shortcut and one cell of gap.
				//
				// Both halves were wrong. The budget was one short, so an
				// eleven-cell label in eleven cells of room lost its
				// last character to an ellipsis. And the shortcut, written AFTER the label,
				// landed on the label's tail including its ellipsis: "Save
				// As\tCtrl+S" in twelve cells rendered "SaveCtrl+S", which
				// reads as a complete item called "Save" rather than a
				// truncated one. A menu that lies about which command it is
				// offering is worse than one that elides.
				int last = c.right();
				if (mi->menuItemType == QStyleOptionMenuItem::SubMenu) --last;
				// The shortcut is dropped rather than allowed to take the
				// whole row. Reserving room for it can leave the label a
				// budget of zero or less, and elide_to_cells returns nothing
				// for that -- so the accelerator alone became the item's
				// name. Measured: "Save As\tCtrl+S" in eight cells rendered
				// "Ctrl+S". A command's NAME is what identifies it and the
				// accelerator is redundant beside it, so the name wins the
				// room and the shortcut goes.
				bool room_for_shortcut = parts.size() > 1;
				if (room_for_shortcut) {
					const int reserved = last - parts[1].size() - 1;
					if (reserved - label_at + 1 > 0) last = reserved;
					else                             room_for_shortcut = false;
				}
				dev->buffer().text(label_at, c.top(),
				                   elide_to_cells(label, last - label_at + 1),
				                   Color(), Color(), la);
				if (room_for_shortcut) {              // right-aligned shortcut
					const QString sc = parts[1];
					dev->buffer().text(c.right() - sc.size(), c.top(), sc,
					                   Color(), Color(), la | Attr::Dim);
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
				// Budgeted, like every sibling in this function. This wrote
				// the label whole and relied on the clip -- and a menu bar
				// item's clip is the BAR, not the item, so an over-long
				// title is not cut at all: it writes into the next item's
				// cells. Qt sizes the items to fit, so it takes an imposed
				// width to reach; the rule is applied here anyway rather
				// than left as the one unbudgeted write among four.
				const QString label =
				    elide_to_cells(strip_mnemonic(mi->text),
				                   qMax(0, c.width() - 1));
				dev->buffer().text(c.left() + 1, c.top(), label, Color(), Color(),
				                   label_attrs(opt, w, mi->font,
				                               hot ? Attrs(Attr::Reverse) : Attrs()));
				return;
			}
			break;
		case CE_MenuBarEmptyArea:
		case CE_MenuEmptyArea:
			return;
		case CE_ItemViewItem:                          // list/table/tree cells
			if (auto *vi = qstyleoption_cast<const QStyleOptionViewItem *>(opt)) {
				Attrs mark = (opt->state & State_Selected) ? Attrs(Attr::Reverse)
				                                           : Attrs();
				// The current item, underlined, by the rule item_view_current()
				// states and CellItemDelegate asks the same way.
				if (item_view_current(vi, w)) mark |= Attr::Underline;
				const Attrs a = with_state(opt, mark);
				// Qt::FontRole arrives in the option, and reaches the label
				// whether or not CellItemDelegate is installed. The fill
				// stays state-only, as in a menu item.
				const Attrs la = label_attrs(opt, w, vi->font, a);
				// Qt::ForegroundRole and Qt::BackgroundRole, by the rule
				// cell_geometry.h now shares with CellPaintEngine: a colour
				// no palette role explains is one the application chose, and
				// it passes through. Without them a model that coloured a row
				// got nothing here and nothing from the delegate either,
				// while the same colour on a QLabel came out -- three answers
				// to one question in one program.
				const Color fg = fg_for(vi->palette.color(QPalette::Text).rgba());
				Color bg;
				if (vi->backgroundBrush.style() != Qt::NoBrush)
					bg = bg_for(vi->backgroundBrush.color().rgba());
				// The whole item, not its top row. A one-cell fill was
				// indistinguishable from a correct one while every item in
				// the suite was one cell tall, and wrong the moment a
				// delegate returned a taller sizeHint: the row highlighted
				// its first line and left the rest on the ordinary ground.
				//
				// A background role fills too, and has to: a row given a
				// colour is a coloured ROW, and colouring only the cells its
				// text happens to occupy leaves a stripe the width of the
				// label.
				if (a || bg.kind() != Color::Default) {
					Cell v;
					v.attrs = a;
					v.bg = bg;
					dev->buffer().fill(c, v);
				}
				// The check indicator, which was not drawn at all: an item
				// view whose items are checkable showed the text and nothing
				// else, so the state a user is there to set was invisible and
				// unsettable by eye. Qt says whether an item has one and what
				// it is; the glyphs are the checkbox's, because a check is a
				// check wherever it appears.
				int text_at = c.left() + 1;
				if (vi->features & QStyleOptionViewItem::HasCheckIndicator) {
					const QString box =
					    vi->checkState == Qt::Checked            ? QStringLiteral("[x]")
					    : vi->checkState == Qt::PartiallyChecked ? QStringLiteral("[-]")
					                                            : QStringLiteral("[ ]");
					dev->buffer().text(text_at, c.top(), box, fg, bg, la);
					text_at += 4;                  // the box and one space
				}
				const int room = c.right() - text_at + 1;
				dev->buffer().text(text_at, c.top(), elide_to_cells(vi->text, room),
				                   fg, bg, la);
				return;
			}
			break;
		case CE_HeaderSection:
			return;                                    // no chrome; label only
		case CE_HeaderLabel:
			if (auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
				// One cell in, which is where CE_ItemViewItem starts an item's
				// text. The two are read as a COLUMN and they did not line up:
				// measured on a two-column table, "Name" began at column 0 and
				// the "r0" under it at column 1, so every heading sat one cell
				// to the left of the data it names. A header is only a label
				// because of what is below it.
				dev->buffer().text(c.left() + 1, c.top(),
				                   elide_to_cells(h->text, c.width() - 1),
				                   Color(), Color(),
				                   label_attrs(opt, w, Attr::Bold));
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
				// The brackets span the WHOLE tab, not just its text. Qt
				// sizes a tab wider than its label -- a closable one wider
				// still, to hold the close button -- and drawing "[One]" at
				// the left of a thirteen-cell tab left eight cells for the
				// tab bar's base rule to show through: "[One]-------X-". A
				// tab is a region you can click, and the bracket is what says
				// where it ends.
				QString inner = strip_mnemonic(t->text);
				const int room = qMax(0, c.width() - 2);
				inner = elide_to_cells(inner, room);
				// Padded in CELLS, which is the unit `room` is in.
				// `inner.size()` counts QChars: a wide cluster is one QChar
				// and two cells, so a tab titled with a single CJK character
				// was padded as though it were one cell wide, the label
				// overran the tab, and the outer elide below dropped the
				// CLOSING BRACKET for an ellipsis. Measured on a tab whose
				// one-character CJK title fits it exactly: the bracket came
				// back as an ellipsis. A tab that is not
				// truncated then looks as though it is, and the bracket is
				// what says where a tab ends -- which the comment above
				// argues at length and this arithmetic then undid.
				int shown = 0;
				for (const QString &cl : to_clusters(inner))
					shown += cluster_width(cl);
				inner += QString(qMax(0, room - shown), QLatin1Char(' '));
				const QString label = QLatin1Char('[') + inner + QLatin1Char(']');
				// Underline rather than reverse, because reverse is taken:
				// the selected tab already carries it, and the tab bar owning
				// focus is a different fact from which tab is current. A bar
				// that has the keys shows its current tab underlined as well
				// as reversed; one that does not shows it reversed alone.
				Attrs ta = sel ? Attrs(Attr::Reverse) : Attrs();
				if (sel && owns_focus(w)) ta |= Attr::Underline;
				dev->buffer().text(c.left(), c.top(), elide_to_cells(label, c.width()),
				                   Color(), Color(), label_attrs(opt, w, ta));
				return;
			}
			break;
		case CE_ProgressBar:
			if (auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt)) {
				// minimum == maximum is Qt's indeterminate bar: the range
				// is unknown and the desktop animates it. It was drawn as a
				// bar at 0% with "0%" written across it, which does not read
				// as "working" -- it reads as stalled, which is the one thing
				// it is not. A distinct shade and no number says the length
				// of the job is unknown, without inventing an animation a
				// frame-diffing renderer would repaint the screen for.
				// The bar itself, and not only its percentage. Drawn with
				// put_cluster and no attributes, a disabled progress bar was
				// identical to a running one while the number over it was
				// dim -- one widget carrying both answers, which is the
				// disabled-item-view fault at a control that happens not to
				// write its glyphs through text().
				const Attrs bar = with_state(opt);
				const int span = pb->maximum - pb->minimum;
				const bool unknown = span <= 0;
				const double frac = span > 0 ? double(pb->progress - pb->minimum) / span : 0.0;
				// Vertical bars fill upward, and were drawn as a horizontal
				// bar in their top row with the rest of the widget left
				// blank -- a meter reading nothing, in the orientation an
				// application picks precisely because it has a tall space.
				// Qt reports the orientation in the state flags.
				const bool horizontal = pb->state & State_Horizontal;
				const int extent = horizontal ? c.width() : c.height();
				const int filled = qRound(frac * extent);
				if (unknown) {
					for (int i = 0; i < extent; ++i) {
						if (horizontal)
							dev->buffer().put_cluster(c.left() + i, c.top(),
							                          QStringLiteral("▒"),
							                          Color(), Color(), bar);
						else
							dev->buffer().put_cluster(c.left(), c.top() + i,
							                          QStringLiteral("▒"),
							                          Color(), Color(), bar);
					}
					return;
				}
				for (int i = 0; i < extent; ++i) {
					// Upward: the bottom cell is the first to fill, which is
					// what a column of liquid does and what a bar drawn from
					// the top would get exactly backwards.
					const bool on = horizontal ? i < filled : i >= extent - filled;
					const QString g = on ? QStringLiteral("█") : QStringLiteral("░");
					if (horizontal)
						dev->buffer().put_cluster(c.left() + i, c.top(), g,
						                          Color(), Color(), bar);
					else
						dev->buffer().put_cluster(c.left(), c.top() + i, g,
						                          Color(), Color(), bar);
				}
				if (pb->textVisible) {
					const QString label = pb->text.isEmpty()
					    ? QStringLiteral("%1%").arg(qRound(frac * 100)) : pb->text;
					dev->buffer().text(c.center().x() - label.size() / 2, c.top(),
					                   label, Color(), Color(),
					                   label_attrs(opt, w, Attr::Reverse));
				}
				return;
			}
			break;
		case CE_Splitter: {
			const bool horizontal_handle = opt->rect.width() < opt->rect.height();
			const QString g = horizontal_handle ? QStringLiteral("│") : QStringLiteral("─");
			const Attrs a = with_state(opt);
			for (int y = c.top(); y <= c.bottom(); ++y)
				for (int x = c.left(); x <= c.right(); ++x)
					dev->buffer().put_cluster(x, y, g, Color(), Color(), a);
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
		const QWidget *pw = painted_widget(p, w);
		const CellClip bound(dev, pw ? cells_of(visible_rect(pw), p, dev, pw) : c);
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
				const Attrs a = with_state(opt);
				for (int i = 0; i < len; ++i) {
					QString g;
					if (i == 0)            g = vert ? QStringLiteral("▲") : QStringLiteral("◀");
					else if (i == len - 1) g = vert ? QStringLiteral("▼") : QStringLiteral("▶");
					else {
						const int t = i - 1;
						g = (t >= thumb_pos && t < thumb_pos + thumb_len)
						    ? QStringLiteral("█") : QStringLiteral("░");
					}
					// Focus marks the thumb, which is the part that moves.
					// The arrow heads and the track are the same whoever
					// owns the keys.
					const Attrs at = (i > 0 && i < len - 1
					                  && i - 1 >= thumb_pos
					                  && i - 1 < thumb_pos + thumb_len)
					                 ? a | focus_attrs(w) : a;
					if (vert) dev->buffer().put_cluster(c.left(), c.top() + i, g,
					                                   Color(), Color(), at);
					else      dev->buffer().put_cluster(c.left() + i, c.top(), g,
					                                   Color(), Color(), at);
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
			const Attrs a = with_state(opt) | focus_attrs(w);
			CellBuffer &b = dev->buffer();
			if (c.height() >= 2) {
				draw_box(b, c, false, a);
			} else {
				b.put_cluster(c.left(), row, QStringLiteral("["), Color(), Color(), a);
				b.put_cluster(c.right(), row, QStringLiteral("]"), Color(), Color(), a);
			}
			b.put_cluster(c.right() - 1, row, QStringLiteral("▾"), Color(), Color(), a);
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
				                 || (w && w == s_focus.data());
				// A bracket goes where a bracket fits, which is the rule the
				// rendering side already states for a rule. Two cells hold
				// "[]" and nothing else, so a dock widget's title buttons --
				// which Qt sizes in pixels, at not quite two cells each --
				// spent their whole budget on chrome and drew a pair of empty
				// boxes. Below three cells the content wins and the brackets
				// are dropped, for the reason the one-row line edit keeps its
				// two: the control is still visibly a control, and an empty
				// box is not one.
				// ...unless there is nothing to put in the cells. Dropping
				// the brackets buys room for content, and with no content it
				// buys an invisible control instead -- which is what an
				// icon-only action with no words became when this rule was
				// first written for the dock buttons. Two cells of "[]" say
				// "a button is here" and nothing else, and that is more than
				// two blank cells say.
				const QString label = tool_button_label(tb, w);
				const bool bracket = c.width() >= 3 || label.isEmpty();
				if (bracket) {
					dev->buffer().put_cluster(c.left(), row, QStringLiteral("["),
					                          Color(), Color(), with_state(opt));
					dev->buffer().put_cluster(c.right(), row, QStringLiteral("]"),
					                          Color(), Color(), with_state(opt));
				}
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
					                          QStringLiteral("▾"),
					                          Color(), Color(), with_state(opt));
				const int inner = c.width() - (bracket ? 2 : 0) - (menu ? 2 : 0);
				if (inner > 0)
					dev->buffer().text(c.left() + (bracket ? 1 : 0), row,
					                       elide_to_cells(label, inner),
					                       Color(), Color(),
					                       label_attrs(opt, w, on ? Attrs(Attr::Reverse)
					                                              : Attrs()));
				return;
			}
			break;
		case CC_Slider:
			if (auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
				const bool vert = sl->orientation == Qt::Vertical;
				const int len = vert ? c.height() : c.width();
				const int span = sl->maximum - sl->minimum;
				int pos = span > 0
				    ? (len - 1) * (sl->sliderPosition - sl->minimum) / span : 0;
				// A vertical slider's minimum is at the BOTTOM, which the
				// option says and this did not read: `upsideDown` is set by
				// QSlider to !invertedAppearance() for a vertical one, so it
				// is true by default. Drawn top-down, the whole control was
				// upside down -- 0 at the top, 100 at the bottom, and the
				// handle moving the wrong way under the arrow keys.
				//
				// Measured against Qt's own SC_SliderHandle for the same
				// widget: value 0 at y=84 of a six-row slider and value 100
				// at y=0, against this drawing's row 0 and row 5.
				//
				// The flag rather than the orientation, because a horizontal
				// slider with invertedAppearance set is the same question and
				// carries the same answer.
				if (sl->upsideDown) pos = len - 1 - pos;
				// The groove carries the state; the handle also says whether
				// it is being held. Qt sets State_Sunken on a slider whose
				// handle has been grabbed, and this style already spells
				// "pressed" as reverse video at the tool button and the menu
				// bar item -- so the drag was a state in the model with
				// nothing on the screen, and the handle looked the same
				// whether it was being moved or merely sat where it was left.
				const Attrs a = with_state(opt);
				const Attrs held = ((opt->state & State_Sunken) ? Attrs(Attr::Reverse)
				                                                : Attrs())
				                   | focus_attrs(w) | a;
				for (int i = 0; i < len; ++i) {
					const bool handle = i == pos;
					const QString g = handle ? QStringLiteral("●")
					                : (vert ? QStringLiteral("│") : QStringLiteral("─"));
					const Attrs at = handle ? held : a;
					if (vert) dev->buffer().put_cluster(c.left(), c.top() + i, g,
					                                   Color(), Color(), at);
					else      dev->buffer().put_cluster(c.left() + i, c.top(), g,
					                                   Color(), Color(), at);
				}
				return;
			}
			break;
		case CC_SpinBox: {
			// Same as the combo above, and for the same reason.
			const int row = c.top() + c.height() / 2;
			const Attrs a = with_state(opt) | focus_attrs(w);
			CellBuffer &b = dev->buffer();
			if (c.height() >= 2) {
				draw_box(b, c, false, a);
			} else {
				b.put_cluster(c.left(), row, QStringLiteral("["), Color(), Color(), a);
				b.put_cluster(c.right(), row, QStringLiteral("]"), Color(), Color(), a);
			}
			// Two cells, two arrows, and the reason is that a click has to be
			// able to reach both. A single plus-minus glyph said "this steps"
			// and nothing more, while SC_SpinBoxUp and SC_SpinBoxDown were
			// 10x19 rectangles at the SAME cell offset by half a row --
			// +100+0 and +100+9 -- so on a one-cell spin box they
			// overlapped, Qt picked the first, and
			// **no cell decremented**. Measured: clicking the arrow cell gave
			// 51 from 50, and no click anywhere gave 49.
			//
			// Half a row cannot be hit on a grid, so the fix is not a better
			// rectangle but a second cell. The glyphs are the tree's own
			// arrows, the ones tool_button_label() uses for an arrowType.
			b.put_cluster(c.right() - 2, row, QStringLiteral("▴"), Color(), Color(), a);
			b.put_cluster(c.right() - 1, row, QStringLiteral("▾"), Color(), Color(), a);
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
	// No scroll arrows on a tab bar that overflows. Qt gives it two
	// QToolButtons sized from PM_TabBarScrollButtonWidth -- 16 px, which is
	// no whole number of columns at either cell size -- and those two are
	// exempt from BOTH GridGuard and GridSnap, a QToolButton child of a
	// QTabBar being on is_exempt's list. So they were the one class of
	// off-grid widget nothing reports and nothing repairs, and they are
	// clickable. Measured: an overflowing bar carried two of them, both off
	// the grid on both axes.
	//
	// Refusing them costs the mouse a way to page the bar and leaves
	// elision, which is what a terminal tab bar does anyway; the keyboard
	// reaches every tab regardless. Two cells of arrow that cannot be
	// addressed in cells are worth less than that.
	case SH_TabBar_PreferNoArrows:                         return 1;
	// A click on a slider goes to the cell clicked. QCommonStyle gives the
	// absolute set to the MIDDLE button and paging to the left one, which on
	// a terminal is close to unusable: the middle button is usually spent on
	// paste, so the cells this style went to the trouble of making
	// addressable -- SC_SliderHandle is exactly the cell the handle is drawn
	// in -- were reachable only by dragging.
	//
	// Settled by the copyright holder 2026-09-05; section 0b carried it as
	// an open question. A cell is a coarse target and a terminal slider has
	// few of them, so "go where I pointed" is the behaviour a reader
	// expects from a picture made of cells, and paging by a fraction of a
	// six-cell range is not.
	case SH_Slider_AbsoluteSetButtons:                     return Qt::LeftButton;
	case SH_Slider_PageSetButtons:                         return Qt::MiddleButton;
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

// --------------------------------------------------------- adaptation (7 T2)
static const char *const k_priority = "qtty.priority";

void set_priority(QWidget *w, Priority p) {
	if (!w) return;
	// Cleared rather than set to Required, so that a widget carrying no hint
	// and a widget explicitly told it is required are the same object. An
	// application that sets the property back is not left with a stale one.
	if (p == Priority::Required) w->setProperty(k_priority, QVariant());
	else                         w->setProperty(k_priority, int(p));
}

Priority priority_of(const QWidget *w) {
	if (!w) return Priority::Required;
	const QVariant v = w->property(k_priority);
	if (!v.isValid()) return Priority::Required;
	// An out-of-range value is Required, which is the safe direction: a typo
	// in a .ui file must not make a widget disappear.
	return v.toInt() == int(Priority::Optional) ? Priority::Optional
	                                            : Priority::Required;
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

// design.md section 7's third Tier-2 hint: "qtty.cells" says how many cells a
// widget needs, in the application's own words, without branching on target.
// A no-op in a GUI build, where nothing reads the property.
//
// Applied as a MINIMUM rather than a fixed size, which is the non-destructive
// reading of "this field needs twenty columns": fewer makes it useless and
// more is fine, so it composes with stretch instead of fighting it, and it
// feeds the small-terminal policy -- a layout that cannot honour its minimums
// is exactly what makes section 7.8 drop and scroll.
//
// Read HERE rather than in the style, and that is a divergence from design.md
// section 5.1, which says the style reads it because "the style receives the
// QWidget*". It does -- but only for the widgets Qt asks it about, and
// QStyle::ContentsType has **no entry for a label, a text edit, a view, or an
// application's own QWidget subclass**. Twenty-four values and not one of
// them is the case the document's own example uses. A style-side reader would
// silently do nothing for most of the tree, which is worse than not having
// the property. section 8.8 records it.
static void apply_cells(QWidget *w) {
	const QVariant v = w->property("qtty.cells");
	if (!v.isValid()) return;
	const QSize c = v.toSize();
	// A typo must not be HALF obeyed, which is what this is for and what the
	// check had to be rewritten twice to see. Qt clamps a negative minimum to
	// zero by itself, so refusing -1 alone buys nothing; what this refuses is
	// the rest of a size that carries one, so a widget cannot end up two
	// cells tall because its width was misspelt. Same direction priority_of()
	// takes for an out-of-range value.
	if (c.width() <= 0 || c.height() <= 0) return;
	// Width is a floor and height is exact, which is not an arbitrary
	// asymmetry -- it is the rule sizeFromContents() already states for every
	// control it sizes: "a single-line control is one cell tall by
	// construction. The width still snaps up, because a width is a count of
	// characters and rounding one down truncates text."
	//
	// Measured, and the case that forced it is the one design.md's own example
	// uses. A QLineEdit asked for 20x1 got 38x1 -- its vertical policy is
	// Fixed, so the height held on its own. A QLabel asked for the same got
	// 38x**11**: policy Preferred, so the layout stretched a one-row
	// annotation over eleven rows and floated the text in the middle of an
	// empty box. The minimum was honoured and the shape was not.
	w->setMinimumSize(c.width() * GridMetrics::cw(), c.height() * GridMetrics::ch());
	w->setMaximumHeight(c.height() * GridMetrics::ch());
}

bool GridSnap::eventFilter(QObject *obj, QEvent *event) {
	if (s_snapping) return false;                // our own setGeometry coming back
	// Polish catches a property set at construction, the property change
	// catches one set later, and between them there is no moment where an
	// application has asked and been ignored.
	if (event->type() == QEvent::Polish
	    || event->type() == QEvent::DynamicPropertyChange) {
		if (QWidget *pw = qobject_cast<QWidget *>(obj)) apply_cells(pw);
		return false;
	}
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
