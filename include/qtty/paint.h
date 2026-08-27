// qtty/paint.h -- L4 rendering: the custom paint device/engine (section 5.4).
// Advanced API: applications normally never touch this; tests, tools, and
// custom ICellPainted widgets do.
#pragma once
#include <QPaintDevice>
#include <QtPlugin>
#include <QPaintEngine>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QTransform>
#include "cell.h"

class QCoreApplication;

namespace Qtty {

class CellPaintEngine;

// section 5.3, and risk R5's stated mitigation: a widget that knows how to
// draw itself in cells says so, and qtty lets it, instead of sending its
// QPainter output through Channel B and hoping.
//
// Deliberately NOT a QObject. A widget already inherits one and two QObject
// bases are illegal, so the interface is a plain abstract class the widget
// inherits second.
//
// `cells` is the widget's own rectangle in buffer coordinates: it already
// carries the compositor's origin, so an implementation draws at cells.left()
// and cells.top() and maps nothing itself.
class ICellPainted {
public:
	virtual ~ICellPainted() = default;
	virtual void paint_cells(CellBuffer &buffer, const QRect &cells) const = 0;
};

// Installs the one filter that lets an ICellPainted widget paint itself.
// Called by Qtty::setup(); an application never calls it.
void install_cell_paint_filter(QCoreApplication &app);

class CellPaintDevice : public QPaintDevice {
public:
	explicit CellPaintDevice(CellBuffer &b);
	~CellPaintDevice() override;
	QPaintEngine *paintEngine() const override;
	// metric() is virtual but NOT pure -- omitting it silently breaks the
	// alignment invariant (section 16, verification finding 3).
	int metric(PaintDeviceMetric m) const override;

	CellBuffer &buffer() const { return buf_; }

	// The device a frame is being rendered into, or null outside a render.
	// The ICellPainted filter needs it: a paint event says which widget is
	// painting and nothing about where the pixels go, and in a GUI build they
	// go to a screen and the filter must stand down.
	static CellPaintDevice *active();
	QPoint origin;                              // widget-space offset, px
	mutable QVector<CellImage> placements;      // section 5.7 funnel output, per frame

private:
	CellBuffer &buf_;
	CellPaintEngine *eng_;
	CellPaintDevice *outer_;                    // restored on destruction
};

class CellPaintEngine : public QPaintEngine {
public:
	// AllFeatures stops QPainter emulating text as paths (section 16, F-census).
	CellPaintEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}

	bool begin(QPaintDevice *) override;
	bool end() override;
	Type type() const override { return QPaintEngine::User; }
	void updateState(const QPaintEngineState &) override;

	void drawTextItem(const QPointF &, const QTextItem &) override;
	void drawRects(const QRectF *, int) override;
	void drawRects(const QRect *, int) override;        // integer overloads matter
	void drawLines(const QLineF *, int) override;
	void drawLines(const QLine *, int) override;
	void drawPath(const QPainterPath &) override;
	void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override;
	void drawPolygon(const QPointF *, int, PolygonDrawMode) override;

	CellPaintDevice *device() const { return dev_; }

private:
	QRect to_cells(const QRectF &) const;
	bool is_thin(const QRectF &) const;
	void fill_rectf(const QRectF &, bool outline_only = false);
	void box(const QRect &cells);
	void line(const QLineF &);

	CellPaintDevice *dev_ = nullptr;
	QPen pen_; QBrush brush_; QFont font_; QTransform xf_;
};

} // namespace Qtty

// Declared so an application can use qobject_cast, which is what design.md
// section 5.3 reaches for. qtty itself dispatches with dynamic_cast, and that
// is not an oversight: qobject_cast across an interface needs Q_INTERFACES on
// the widget and therefore moc, while dynamic_cast needs neither and works on
// a widget that merely inherits the class. IGraphicsOutput is already
// dispatched exactly this way. Accepting both costs one macro and turns away
// nobody.
Q_DECLARE_INTERFACE(Qtty::ICellPainted, "org.qtty.ICellPainted/1.0")
