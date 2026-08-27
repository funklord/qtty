// qtty/cell.h -- L2 cell model (section 5.2): grapheme clusters, wide cells,
// colours/attrs, region diff, and frame-attached image placements (section 5.7).
#pragma once
#include <QString>
#include <QVector>
#include <QRect>
#include <QRegion>
#include <QPixmap>
#include "color.h"

namespace Qtty {

struct Cell {
	QString ch = QStringLiteral(" ");   // ONE grapheme cluster, not one QChar
	Color fg, bg;
	Attrs attrs;
	quint8 width = 1;                   // 1, 2 (wide), or 0 (continuation cell)

	bool operator==(const Cell &o) const {
		return ch == o.ch && fg == o.fg && bg == o.bg && attrs == o.attrs && width == o.width;
	}
	bool operator!=(const Cell &o) const { return !(*this == o); }
};

// section 5.7 cell-anchored placement: a pixel image riding the cell grid.
struct CellImage {
	// QPixmap::cacheKey() -> upload-once identity. Qt returns qint64 and
	// this is unsigned because the value is an opaque handle rather than a
	// number: it is compared and used as a map key, never ordered or
	// arithmetic. The conversion is written out at every call site so it
	// reads as intended rather than as a narrowing the compiler noticed.
	quint64 key = 0;
	QRect   cellRect;       // anchor + span, in cells
	QPixmap pixmap;
};

// Display width of one grapheme cluster: 2 for East Asian wide/fullwidth and
// emoji presentation, else 1. The table is deliberately simple; terminals
// disagree at the margins, and Capabilities::unicodeWide lets a backend
// override behaviour (section 5.2).
int clusterWidth(QStringView cluster);

// Split text into grapheme clusters (QTextBoundaryFinder::Grapheme).
QVector<QString> toClusters(const QString &text);

class CellBuffer {
public:
	CellBuffer(int cols, int rows) : c_(cols), r_(rows), d_(cols * rows) {}
	int cols() const { return c_; }
	int rows() const { return r_; }

	Cell &at(int x, int y) {
		static Cell junk;
		if (x < 0 || y < 0 || x >= c_ || y >= r_) { junk = Cell{}; return junk; }
		return d_[y * c_ + x];
	}
	const Cell &at(int x, int y) const { return const_cast<CellBuffer *>(this)->at(x, y); }

	void fill(const QRect &r, const Cell &v);

	// Write one grapheme cluster at (x,y), handling wide-cell continuation:
	// a width-2 cluster claims (x,y) and marks (x+1,y) as continuation; any
	// write over half of a wide pair clears the partner first (section 5.2 -- the
	// classic corruption source, unit-tested).
	void putCluster(int x, int y, const QString &cluster,
	                Color fg = {}, Color bg = {}, Attrs attrs = {});

	// Write a string of clusters starting at (x,y); returns cells consumed.
	int text(int x, int y, const QString &s,
	         Color fg = {}, Color bg = {}, Attrs attrs = {});

	// Damage vs a previous frame, as a cell-space region.
	QRegion diff(const CellBuffer &prev) const;
	int diffCells(const CellBuffer &prev) const;

	QString toText() const;             // glyphs only, one row per line

	// The full section 9 snapshot: the glyph plane, then an attribute plane
	// and a colour plane over the same grid, then a legend naming what each
	// colour letter stands for.
	//
	// toText() alone was what the fixtures recorded, and it drops everything
	// except the character -- so the reverse video, bold and dim that most of
	// the Channel A work produces could not be snapshotted at all. A fixture
	// that cannot see a selection is one that goes green when a selection
	// stops being drawn.
	//
	// The planes line up with the glyph plane column for column, including
	// skipping a wide cluster's continuation cell, so a column can be read
	// straight down across all three.
	QString to_snapshot() const;

	// Frame payload: placements collected for this frame (section 5.7). Travels with
	// the buffer through present().
	QVector<CellImage> images;

private:
	void clearWidePartner(int x, int y);
	int c_, r_;
	QVector<Cell> d_;
};

} // namespace Qtty
