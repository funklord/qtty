// qtty/grid.h -- L3 grid metrics, style, and the alignment guard (section 5.3).
#pragma once
#include <QProxyStyle>
#include <QRect>
#include <QSize>

class QWidget;

namespace Qtty {

// One cell is cw() x ch() device pixels. Values are set once by Qtty::setup()
// from the measured application font (section 5.3 "font provisioning").
class GridMetrics {
public:
	static int cw();
	static int ch();
	static void set(int cw, int ch);            // called by Qtty::setup()
	static QSize cells(int c, int r) { return {c * cw(), r * ch()}; }
	static bool isAligned(const QRect &px) {
		return px.x() % cw() == 0 && px.y() % ch() == 0
			&& px.width() % cw() == 0 && px.height() % ch() == 0;
	}
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
};

} // namespace Qtty
