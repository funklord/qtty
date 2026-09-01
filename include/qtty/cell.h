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
	QRect   cell_rect;       // anchor + span, in cells
	QPixmap pixmap;

	// The pixmap is deliberately NOT compared, and this is not an omission:
	// `key` is its cacheKey, so two placements have equal keys exactly when
	// they carry the same pixels. Comparing the key IS comparing the image,
	// at the cost of a quint64 rather than of a per-frame pixel walk.
	bool operator==(const CellImage &o) const {
		return key == o.key && cell_rect == o.cell_rect;
	}
	bool operator!=(const CellImage &o) const { return !(*this == o); }
};

// Display width of one grapheme cluster: 2 for East Asian wide/fullwidth and
// emoji presentation, else 1. The table is deliberately simple; terminals
// disagree at the margins, and Capabilities::unicode_wide lets a backend
// override behaviour (section 5.2).
int cluster_width(QStringView cluster);

// Split text into grapheme clusters (QTextBoundaryFinder::Grapheme).
QVector<QString> to_clusters(const QString &text);

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

	// A write outside this rectangle is dropped; a null rectangle means no
	// clip. READS are never clipped -- at() is the raw accessor and the
	// snapshot, diff and test paths all go through it.
	//
	// It exists because Channel A had no bound at all. Measured across twelve
	// widget kinds at six sizes each: a QPushButton one cell wide wrote
	// "<OK>" and put three cells of it in the widget beside it; a QGroupBox
	// one row tall spent twelve cells outside itself; a QTabBar's tabs were
	// drawn at their own widths regardless of the bar's. Section 7.7 had one
	// instance of this recorded as a fault of its own -- it is one fault,
	// twelve times, and the fix is a bound rather than twelve corrections.
	// "Is there a clip" is a flag of its own rather than QRect::isNull(),
	// which a zero-sized rectangle also answers true to. A widget resized to
	// nothing produces exactly that rectangle, and reading it as "no clip"
	// would let the one case with no room at all draw without a bound --
	// found by a sabotage that set QRect(0, 0, 0, 0) expecting everything to
	// vanish and watched the suite pass.
	void set_clip(const QRect &r) { clip_ = r; has_clip_ = true; }
	void clear_clip() { has_clip_ = false; clip_ = QRect(); }
	QRect clip() const { return clip_; }
	bool has_clip() const { return has_clip_; }
	bool writable(int x, int y) const {
		return x >= 0 && y >= 0 && x < c_ && y < r_
		    && (!has_clip_ || clip_.contains(QPoint(x, y)));
	}

	void fill(const QRect &r, const Cell &v);

	// Write one grapheme cluster at (x,y), handling wide-cell continuation:
	// a width-2 cluster claims (x,y) and marks (x+1,y) as continuation; any
	// write over half of a wide pair clears the partner first (section 5.2 -- the
	// classic corruption source, unit-tested).
	void put_cluster(int x, int y, const QString &cluster,
	                Color fg = {}, Color bg = {}, Attrs attrs = {});

	// Write a string of clusters starting at (x,y); returns cells consumed.
	int text(int x, int y, const QString &s,
	         Color fg = {}, Color bg = {}, Attrs attrs = {});

	// Damage vs a previous frame, as a cell-space region.
	QRegion diff(const CellBuffer &prev) const;
	int diff_cells(const CellBuffer &prev) const;

	QString to_text() const;             // glyphs only, one row per line

	// The full section 9 snapshot: the glyph plane, then an attribute plane
	// and a colour plane over the same grid, then a legend naming what each
	// colour letter stands for.
	//
	// to_text() alone was what the fixtures recorded, and it drops everything
	// except the character -- so the reverse video, bold and dim that most of
	// the Channel A work produces could not be snapshotted at all. A fixture
	// that cannot see a selection is one that goes green when a selection
	// stops being drawn.
	//
	// The planes line up with the glyph plane COLUMN for column, so a column
	// can be read straight down across all three. That needs the attribute
	// and colour planes to carry one character per CELL while the glyph plane
	// carries one per CLUSTER: a wide cluster is one glyph occupying two
	// columns, so its continuation cell contributes no glyph and does
	// contribute an attribute and a colour. Skipping it in all three left the
	// planes a character short under every wide cluster.
	QString to_snapshot() const;

	// Frame payload: placements collected for this frame (section 5.7). Travels with
	// the buffer through present().
	QVector<CellImage> images;

private:
	void clear_wide_partner(int x, int y);
	int c_, r_;
	QRect clip_;
	bool has_clip_ = false;
	QVector<Cell> d_;
};

} // namespace Qtty
