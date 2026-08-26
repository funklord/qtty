// qtty/paint.h -- L4 rendering: the custom paint device/engine (section 5.4).
// Advanced API: applications normally never touch this; tests, tools, and
// custom ICellPainted widgets do.
#pragma once
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QTransform>
#include "cell.h"

namespace Qtty {

class CellPaintEngine;

class CellPaintDevice : public QPaintDevice {
public:
	explicit CellPaintDevice(CellBuffer &b);
	~CellPaintDevice() override;
	QPaintEngine *paintEngine() const override;
	// metric() is virtual but NOT pure -- omitting it silently breaks the
	// alignment invariant (section 16, verification finding 3).
	int metric(PaintDeviceMetric m) const override;

	CellBuffer &buffer() const { return buf_; }
	QPoint origin;                              // widget-space offset, px
	mutable QVector<CellImage> placements;      // section 5.7 funnel output, per frame

private:
	CellBuffer &buf_;
	CellPaintEngine *eng_;
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
	QRect toCells(const QRectF &) const;
	void fillRectF(const QRectF &, bool outlineOnly = false);
	void box(const QRect &cells);
	void line(const QLineF &);

	CellPaintDevice *dev_ = nullptr;
	QPen pen_; QBrush brush_; QFont font_; QTransform xf_;
};

} // namespace Qtty
