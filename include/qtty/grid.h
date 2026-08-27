// qtty/grid.h -- L3 grid metrics, style, and the alignment guard (section 5.3).
#pragma once
#include <QProxyStyle>
#include <QObject>
#include <QString>
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

// InputRouter-owned focus (section 5.5, measured F4): under the offscreen platform no
// window activates, so QApplication::focusWidget() is always null and
// State_HasFocus never set. GridStyle consults this instead.
QWidget *focusWidget();
void setFocusWidget(QWidget *);

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
	QRect subControlRect(ComplexControl, const QStyleOptionComplex *, SubControl,
	                     const QWidget *) const override;
};

} // namespace Qtty
