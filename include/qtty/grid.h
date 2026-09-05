// qtty/grid.h -- L3 grid metrics, style, and the alignment guard (section 5.3).
#pragma once
#include <QProxyStyle>
#include <QObject>
#include <QString>
#include <QIcon>
#include <QRect>
#include <QSize>

class QWidget;
class QCoreApplication;

namespace Qtty {

// One cell is cw() x ch() device pixels. Values are set once by Qtty::setup()
// from the measured application font (section 5.3 "font provisioning").
class GridMetrics {
public:
	static int cw();
	static int ch();
	static void set(int cw, int ch);            // called by Qtty::setup()
	static QSize cells(int c, int r) { return {c * cw(), r * ch()}; }
	static bool is_aligned(const QRect &px) {
		return px.x() % cw() == 0 && px.y() % ch() == 0
		    && px.width() % cw() == 0 && px.height() % ch() == 0;
	}
};

// section 5.3 and risk R3: the grid is sound only if one cell is a whole
// number of pixels and every glyph advances by exactly that. Returns an empty
// string when `font` can carry the grid, and otherwise says what failed.
//
// Qtty::setup() treats a non-empty answer as a hard startup error rather than
// a warning, which is what the design asks for: a font whose advance is 9.6
// pixels does not produce a slightly blurry TUI, it produces one where the
// column a widget thinks it occupies and the column it draws into diverge
// further with every column. The check this replaced was Q_ASSERT_X on
// `advance('i') == advance('M')`, which tested monospace-ness rather than
// integrality and compiled out in release -- so R3's stated mitigation was
// not in place in any shipping build.
QString grid_font_problem(const QFont &font);

// What `font` actually resolved to, when that is not what was asked for.
// Empty when the family Qt found is the family requested.
//
// The guard above is about METRICS, and a substitute satisfies it whenever its
// own metrics happen to be whole numbers. Measured: with DejaVu Sans Mono
// removed from the font list, qtty ran on Noto Mono -- same 10x19 cell, the
// whole suite green (828 checks on 2026-09-03, and the figure is dated
// because it is a record of that run and not a claim about this one),
// nothing anywhere saying a different font was in use. Every other
// ambient lever this library inherits is pinned (platform, platform theme,
// scaling) or asked for outright (hinting); the family cannot be pinned,
// because a font Qt does not resolve cannot be conjured. So it is announced
// instead, which is the weakest thing that is still honest -- and it says what
// it TESTED, which is that the name came back different. "Is not installed"
// was the first wording and named a cause this never asks about: under a
// platform with no font database an installed family resolves to '' too.
QString grid_font_substitution(const QFont &font);

// section 5.3, and the design calls this "the highest value-per-line component
// in the project": an event filter that checks every widget geometry against
// the grid as it is assigned, so a misalignment is reported where it happens
// rather than as a smeared frame several layers away.
//
// It reports rather than aborts. A misaligned widget is a quality defect, and
// a guard that takes the application down with it is one somebody switches
// off -- at which point it guards nothing.
class GridGuard : public QObject {
public:
	// Installs a single guard on `app`, owned by it. Calling twice is a no-op.
	static void install(QCoreApplication &app);
	static int violations();          // count since the last reset
	static void reset();
	// Widgets whose class self-sizes and cannot be gridded from the style
	// (measured F5). Named rather than silently skipped, so the exemption is
	// reviewable and does not quietly grow.
	static bool is_exempt(const QWidget *w);

	bool eventFilter(QObject *, QEvent *) override;
};

// section 7.8's CompactionPass, and design.md section 7's promise that Tier 1
// is free -- "style metrics differ, so the same layout compacts
// automatically". It is not free for a layout with slack in it, which is most
// of them: GridStyle can make the metrics cell multiples but nothing makes a
// layout hand the remainder to a child rather than leaving it as a gap.
//
// GridSnap is GridGuard's other half. The guard measures and reports; this
// corrects, using the same exemptions, the same event, and the same
// install-once shape.
//
// What Qtty::setup() does with them, which this said the opposite of until
// 2026-09-05: it installs GridSnap unconditionally, and GridGuard in a debug
// build only -- the guard is an assertion and is compiled out of release, as
// section 9 asks. So a program calling setup() has both in a debug build and
// the correction alone in a release one.
//
// They keep the explicit install() because setup() is not the only way in.
// A library that must behave normally in a GUI build (section 10.1) never
// calls setup() and gets neither, which is the inertness rule doing its job;
// and a program that wants the report without the correction, or the
// correction in a release build, installs the one it wants itself.
//
// **Round each edge to the nearest cell, and nothing else.** The policy is not
// a preference. Rounding is monotonic, so if one widget's right edge is at or
// before another's left edge, the rounded edges keep that order -- two
// disjoint siblings stay disjoint and become adjacent at worst. Flooring the
// origin and ceiling the size does NOT have that property and was measured
// overlapping a neighbour across any gap narrower than a cell (section 7.8).
//
// A consequence worth knowing rather than discovering: a widget narrower than
// half a cell rounds to zero width and disappears. Growing it to one cell
// instead is exactly the case that overlaps, so the alternative to vanishing
// is a widget drawn on top of its neighbour, and a cell grid cannot represent
// either widget honestly.
//
// Rounding is also idempotent, which is what makes this terminate: snapping an
// already-snapped rectangle is a no-op, so the geometry this sets raises one
// more resize that changes nothing and stops.
class GridSnap : public QObject {
public:
	static void install(QCoreApplication &app);
	// Uninstalls and destroys the filter. It exists because a correcting
	// filter installed on the application is not something a test can leave
	// behind: every later case would run with its geometry quietly rewritten,
	// including the ones checking that it is inert when absent.
	static void remove();
	static bool installed();
	// Round each edge to the nearest cell. Public because the property that
	// makes it safe is worth asserting directly rather than only through a
	// widget.
	static QRect snap(const QRect &px);
	static int snapped();             // geometries corrected since the last reset
	static void reset();

	bool eventFilter(QObject *, QEvent *) override;
};

// InputRouter-owned focus (section 5.5, measured F4): under the offscreen platform no
// window activates, so QApplication::focusWidget() is always null and
// State_HasFocus never set. GridStyle consults this instead.
//
// The two are spelled differently on purpose, and the rule that decides it is
// the member rename's own: an identifier is Qt's if it appears anywhere in
// Qt's headers. `focusWidget` does -- QApplication, QWidget and
// QGraphicsWidget all declare it -- so it keeps Qt's spelling, and an
// application replacing a `qApp->focusWidget()` call recognises it.
// `setFocusWidget` appears in NO Qt header; it was carried along by
// association with the getter, and it is qtty's own name like every other.
//
// The pairing was doing harm as well as being inconsistent.
// `setFocusWidget(scope->focusWidget())` reads as the setter and getter of
// one thing and is not: the argument is Qt's PER-WINDOW focus and the call is
// qtty's PROCESS-WIDE one. Four sites in the library read that way.
QWidget *focusWidget();
void set_focus_widget(QWidget *);

// design.md section 8.6: a substitution registry mapping an icon to a glyph.
//
// A terminal cannot draw a 16-pixel icon in one cell -- there is nothing to
// see at that size, which is why CellPaintEngine::drawPixmap() has always
// stamped a placeholder block there instead. The registry is what lets an
// application say what the icon MEANS: "document-save" is a floppy on a
// desktop and can be a disk glyph here, chosen by whoever knows the icon set
// rather than guessed by the library.
//
// Two ways in, and the widget wins. A `qtty.glyph` dynamic property on the
// widget is per-instance and answers the case a name cannot: two toolbars
// using the same standard icon for different things. The registry is keyed on
// QIcon::name(), which is set for themed icons and empty for one built from a
// pixmap -- so an unnamed icon simply finds nothing, which is the honest
// answer rather than a wrong glyph.
void set_icon_glyph(const QString &icon_name, const QString &glyph);
QString icon_glyph(const QString &icon_name);         // empty when unregistered
void clear_icon_glyphs();

// What to draw for this widget's icon, or empty if nothing is registered and
// no property is set. Checks the property first for the reason above.
//
// Split in two so the rules can be tested. QIcon::name() is empty unless an
// icon THEME resolved the icon, and there is no way to give an icon a name by
// hand -- so on a machine with no matching theme installed (this one, and any
// machine where qtty has pinned the platform theme off) the QIcon overload
// cannot be driven past its first line. The name overload carries every rule
// and is fully exercised; the other adds only the call to QIcon::name().
QString glyph_for(const QWidget *w, const QString &icon_name);
QString glyph_for(const QWidget *w, const QIcon &icon);

// ------------------------------------------------------- adaptation (7 T2)
// design.md section 7's Tier-2 hint, and the FIRST half of the policy it
// names for a terminal too small for a layout's minimum: drop the optional
// widgets, then scroll the root. The scrolling half is unconditional and is
// in the compositor; this half needs the application to say what it can
// afford to lose, because nothing else can know.
//
// Carried as the dynamic property "qtty.priority", which is what makes it a
// no-op in a GUI build the way design.md requires: nothing reads the property
// there, and the application does not have to link qtty or branch on target to
// set it. It can equally be set from a .ui file.
//
// design.md spells this setPriority(). It is set_priority() here, for the rule
// section 10 records and the reason the focusWidget rename records: a name
// qtty INTRODUCES is snake_case, and only a Qt name or a reimplemented Qt
// virtual keeps Qt's spelling. Recorded in section 8 as a divergence rather
// than resolved silently.
enum class Priority { Required, Optional };
void set_priority(QWidget *w, Priority p);
Priority priority_of(const QWidget *w);

// sections 5.3/5.4: cell-multiple metrics for layouts and widgets; Channel A
// (semantic) drawing when the paint target is a CellPaintDevice.
class GridStyle : public QProxyStyle {
public:
	GridStyle();
	int pixelMetric(PixelMetric, const QStyleOption *, const QWidget *) const override;
	QSize sizeFromContents(ContentsType, const QStyleOption *, const QSize &,
	                       const QWidget *) const override;
	void drawPrimitive(PrimitiveElement, const QStyleOption *, QPainter *,
	                   const QWidget *) const override;
	void drawControl(ControlElement, const QStyleOption *, QPainter *,
	                 const QWidget *) const override;
	void drawComplexControl(ComplexControl, const QStyleOptionComplex *, QPainter *,
	                        const QWidget *) const override;
	// section 5.3 lists this among the overrides that grid the widgets Qt
	// builds for itself. A combo box's internal QLineEdit is placed by it,
	// and without it the edit sits at the proxy style's pixel offsets inside
	// a cell-sized combo -- measured at 377x15+2+2 in a one-cell combo,
	// which GridGuard reports and which no application can correct, because
	// it never constructed that widget.
	// Snapping one Qt-placed rectangle: the close button a closable QTabBar
	// builds sits at a pixel offset inside the tab, so it lands off the grid
	// however it is sized. See the implementation for why this is answered
	// rather than exempted.
	QRect subElementRect(SubElement, const QStyleOption *,
	                     const QWidget *) const override;
	QRect subControlRect(ComplexControl, const QStyleOptionComplex *, SubControl,
	                     const QWidget *) const override;
	// A QProxyStyle passes style hints straight through, and some of them come
	// from the PLATFORM THEME rather than from the base style -- so a terminal
	// program's layout would depend on the desktop it happened to be launched
	// from. Swept: of 121 hints and 96 pixel metrics, exactly one differs
	// between the offscreen platform and xcb, and it is one a cell renderer
	// must refuse. See the implementation.
	int styleHint(StyleHint, const QStyleOption *, const QWidget *,
	              QStyleHintReturn *) const override;
};

} // namespace Qtty
