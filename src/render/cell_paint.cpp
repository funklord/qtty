// src/render/cell_paint.cpp -- CellPaintDevice / CellPaintEngine (section 5.4).
#include "qtty/paint.h"
#include <cmath>
#include <QCoreApplication>
#include <QEvent>
#include <QWidget>
#include "qtty/grid.h"
#include "qtty/theme.h"
#include "../cell_geometry.h"
#include <QGuiApplication>
#include <QPalette>
#include <QPainterPath>
#include <QFontMetricsF>

namespace Qtty {

// The device currently being rendered into. Single-threaded by construction:
// rendering happens on the GUI thread and nowhere else (design.md section 5.4),
// which is the same assumption Color::to_xterm256()'s memo already rests on.
//
// Saved and restored rather than merely set and cleared, so that a nested
// render -- a tool rendering into its own buffer while a frame is in flight --
// puts the outer device back instead of leaving null behind it.
static CellPaintDevice *s_active = nullptr;

CellPaintDevice *CellPaintDevice::active() { return s_active; }

CellPaintDevice::CellPaintDevice(CellBuffer &b)
    : buf_(b), eng_(new CellPaintEngine), outer_(s_active) { s_active = this; }

CellPaintDevice::~CellPaintDevice() { s_active = outer_; delete eng_; }

// section 5.3, risk R5: a widget that implements ICellPainted paints itself
// into the buffer, and its ordinary painting is skipped entirely rather than
// being drawn first and overwritten -- Channel B output underneath would show
// wherever the cell painting left a cell alone.
//
// A paint event is the only hook Qt offers for replacing a widget's painting,
// and it says nothing about where the pixels are going. That is what
// CellPaintDevice::active() is for: in a GUI build there is no active cell
// device, the filter stands down, and the widget paints normally. Section
// 10.1's inertness rule made concrete.
class CellPaintFilter : public QObject {
public:
	bool eventFilter(QObject *o, QEvent *e) override {
		if (e->type() != QEvent::Paint) return false;
		CellPaintDevice *dev = CellPaintDevice::active();
		if (!dev) return false;                    // GUI build, or not rendering
		QWidget *w = qobject_cast<QWidget *>(o);
		if (!w) return false;
		// dynamic_cast, not qobject_cast: the latter needs Q_INTERFACES on the
		// widget and therefore moc. IGraphicsOutput is dispatched the same way.
		// A pixel surface is harvested rather than drawn: its content is
		// genuinely pixels, and Channel B would snap every primitive in it to
		// the grid. Guarded against its own render() below, which sends
		// another paint event straight back here.
		if (auto *surface = dynamic_cast<PixelSurface *>(o)) {
			if (harvesting_) return false;         // our own render(): paint
			return harvest(surface, dev);
		}
		// dynamic_cast, not qobject_cast: the latter needs Q_INTERFACES on the
		// widget and therefore moc. IGraphicsOutput is dispatched the same way.
		auto *painted = dynamic_cast<ICellPainted *>(o);
		if (!painted) return false;

		painted->paint_cells(dev->buffer(), cells_of_rect(w->rect(), w, dev->origin));
		return true;                               // consumed: no Channel B pass
	}

private:
	bool harvesting_ = false;

	bool harvest(QWidget *w, CellPaintDevice *dev) {
		if (w->width() <= 0 || w->height() <= 0) return true;
		QImage img(w->size(), QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::transparent);
		harvesting_ = true;
		w->render(&img);
		harvesting_ = false;

		const QRect cells = cells_of_rect(w->rect(), w, dev->origin);

		// Content-addressed, because a surface is repainted rather than
		// cached: a key taken from the widget would tell the kitty tier the
		// image had not changed and it would keep showing the first frame,
		// while a fresh key every frame would re-upload an unchanged plot on
		// every repaint. Hashing the pixels is what makes upload-once mean
		// what it says here.
		const QByteArray bits(reinterpret_cast<const char *>(img.constBits()),
		                      int(img.sizeInBytes()));
		dev->placements.append({quint64(qHash(bits)), cells,
			                        QPixmap::fromImage(img)});
		return true;                               // consumed: no Channel B pass
	}
};

PixelSurface::~PixelSurface() = default;

void install_cell_paint_filter(QCoreApplication &app) {
	static CellPaintFilter *filter = nullptr;
	if (filter) return;
	filter = new CellPaintFilter;
	filter->setParent(&app);
	app.installEventFilter(filter);
}
QPaintEngine *CellPaintDevice::paintEngine() const { return eng_; }

int CellPaintDevice::metric(PaintDeviceMetric m) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	switch (m) {
	case PdmWidth:                  return buf_.cols() * cw;
	case PdmHeight:                 return buf_.rows() * ch;
	case PdmWidthMM:                return buf_.cols() * cw / 4;
	case PdmHeightMM:               return buf_.rows() * ch / 4;
	case PdmNumColors:              return 256;
	case PdmDepth:                  return 24;
	case PdmDpiX: case PdmPhysicalDpiX: return 96;
	case PdmDpiY: case PdmPhysicalDpiY: return 96;
	case PdmDevicePixelRatio:       return 1;
	case PdmDevicePixelRatioScaled: return int(1 * QPaintDevice::devicePixelRatioFScale());
	default:                        return 0;
	}
}

bool CellPaintEngine::begin(QPaintDevice *pdev) {
	dev_ = static_cast<CellPaintDevice *>(pdev);
	last_row_ = -1;
	last_end_col_ = 0;
	last_x_ = 0;
	return true;
}
bool CellPaintEngine::end() { dev_ = nullptr; return true; }

void CellPaintEngine::updateState(const QPaintEngineState &s) {
	if (s.state() & DirtyPen)       pen_ = s.pen();
	if (s.state() & DirtyBrush)     brush_ = s.brush();
	if (s.state() & DirtyFont)      font_ = s.font();
	if (s.state() & DirtyTransform) xf_ = s.transform();
}

// The clip in cells, or an invalid rect when there is none.
//
// design.md section 432 lists clip among the four things updateState() carries,
// and three of the four were implemented: an application's own setClipRect()
// was ignored outright, so a QPainter told to keep inside four cells filled
// twenty. Asked of the PAINTER rather than reassembled from the state flags,
// because Qt composes NoClip, ReplaceClip and IntersectClip itself and a
// second implementation of that composition is a second thing to get wrong.
//
// The bounding rectangle, not the region. That under-clips a region with a
// hole in it -- it draws a little more than it was allowed -- which is the
// safe direction on a grid, and it is rare: of 3386 clip changes the suite
// makes, 13 involve more than one rectangle.
std::optional<QRect> CellPaintEngine::clip_cells() const {
	// Outward, which is the whole of why this does not reuse to_cells(): a
	// cell is atomic, so a clip covering part of one either admits that cell
	// or loses content that was inside it. to_cells() rounds each edge to the
	// NEAREST cell, which is right for placing a rectangle and wrong for
	// admitting one -- used here it made a clip eight pixels tall against a
	// nineteen-pixel cell round to nothing, and a QLineEdit's text vanished.
	const auto outward = [this](const QRectF &device_px) {
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const QRectF m = device_px.translated(dev_->origin);
		const int l = int(std::floor(m.left() / cw)), t = int(std::floor(m.top() / ch));
		return QRect(l, t, int(std::ceil(m.right() / cw)) - l + 1,
		             int(std::ceil(m.bottom() / ch)) - t + 1);
	};

	std::optional<QRect> clip;
	// The SYSTEM clip, which is the one Qt actually uses for widgets and the
	// one this engine never asked about. Measured: rendering a window sets it
	// on every child and leaves the user clip alone, so a scroll area's
	// content was clipped by Qt and unclipped by us. It is in device
	// coordinates already, so it does not go through the transform.
	const QRegion sys = systemClip();
	if (!sys.isEmpty()) clip = outward(QRectF(sys.boundingRect()));

	const QPainter *p = painter();
	if (p && p->hasClipping()) {
		const QRect user = outward(xf_.mapRect(p->clipBoundingRect()));
		clip = clip ? clip->intersected(user) : user;
	}
	return clip;
}

QRect CellPaintEngine::to_cells(const QRectF &r) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	QRectF m = xf_.mapRect(r).translated(dev_->origin);
	// Each EDGE is rounded, and the extent follows from the two. Rounding the
	// extent instead loses where the rectangle actually sits: a scroll area's
	// viewport is inset by one frame width, which is a whole column but only
	// half a row on a cell taller than it is wide, so its height rounded up
	// to a whole extra row and its background fill erased the bottom of the
	// frame drawn around it. Every framed QAbstractScrollArea -- text edit,
	// list, table, tree -- lost the bottom rule of its own border, with the
	// corners left standing because the frame drew those.
	const qreal l = m.left() / cw, t = m.top() / ch;
	const qreal r2 = m.right() / cw, b = m.bottom() / ch;
	return QRect(qRound(l), qRound(t),
	             qMax(1, qRound(r2) - qRound(l)), qMax(1, qRound(b) - qRound(t)));
}

// Text colour policy (section 6). The application palette is consulted for one
// thing only -- which ROLE produced this pen -- and what that role looks like
// on a terminal is theme()'s to say. That is the wiring project.md section 11
// item 3 asks for: theme() is the single source, and set_theme() therefore
// changes what gets drawn. Under CellTheme::terminal_default() every role
// resolves to Color::Default and the terminal's own scheme applies, which is
// the behaviour this replaced.
//
// A pen colour no role explains is Channel B output -- something the
// application coloured itself -- and passes through as true colour, carrying
// no authored ANSI-16 index because no role authored one.
// The rule itself is fg_for() in cell_geometry.h now, because CellItemDelegate
// and GridStyle's CE_ItemViewItem need the same answer and were each giving a
// different one.
static Color pen_to_fg(const QPen &pen) { return fg_for(pen.color().rgba()); }

void CellPaintEngine::drawTextItem(const QPointF &p, const QTextItem &ti) {
	QPointF q = xf_.map(p) + QPointF(dev_->origin);
	QFontMetricsF fm(ti.font());
	int col = qRound(q.x() / GridMetrics::cw());
	int row = qRound((q.y() - fm.ascent()) / GridMetrics::ch());
	Attrs a;
	if (ti.font().bold()) a |= Attr::Bold;
	if (ti.font().italic()) a |= Attr::Italic;
	if (ti.font().underline()) a |= Attr::Underline;
	const std::optional<QRect> clip = clip_cells();
	if (clip && (clip->isEmpty() || row < clip->top() || row > clip->bottom())) return;
	QString text = ti.text();
	if (clip) {
		// Trimmed by CLUSTER, and from the left as well as the right: a string
		// that starts before the clip loses its leading clusters and its
		// origin moves with them, or what is left lands where the whole string
		// would have. Cut, not elided -- a clip is not a shortage of room and
		// an ellipsis would be qtty inventing a character nobody drew.
		//
		// A cluster straddling the edge is kept whole, which draws at most one
		// cell more than allowed. Same direction as taking the clip's bounding
		// rectangle: under-clip rather than lose a glyph.
		QString visible;
		int x = col;
		for (const QString &cl : to_clusters(text)) {
			const int w = cluster_width(cl);
			if (x + w - 1 >= clip->left() && x <= clip->right()) {
				if (visible.isEmpty()) col = x;
				visible += cl;
			}
			x += w;
		}
		text = visible;
		if (text.isEmpty()) return;
	}
	// Cluster by cluster, so that a glyph landing on a cell that is already
	// reversed stays reversed. CellItemDelegate carries the same rule for the
	// row it draws and says why: text written over a reverse-video cell must
	// carry the attribute too, or the cell's own reverse video hides it.
	//
	// The engine has the same problem and had no answer. Qt fills a
	// QLineEdit's selection and then draws the text over it as two unrelated
	// calls, so the fill's attribute was set and the text's write replaced
	// it. Reverse is a property of the CELL rather than of whoever wrote it
	// last.
	// A run of wide clusters is narrower in PIXELS than it is in CELLS, and
	// Qt positions each run by the font's advances. Measured on this machine:
	// 'M' advances 10.0 -- exactly one cell -- while a CJK character advances
	// **16.0**, not 20, so three of them end at pixel 48 where six cells end
	// at 60. Qt then starts the next run at 48, which is column 4 or 5, on top
	// of the third cluster's own cells. A QLineEdit holding "<CJK>xy" lost its
	// third character entirely: the cells were written and then overwritten.
	//
	// Channel A never had this because GridStyle counts cells. Here the fix is
	// to remember where the last run ENDED in cells and start no earlier,
	// which is only applied when the runs are genuinely consecutive: same row,
	// and the new run's pixel origin at or right of the previous one's. Text
	// drawn leftwards after text drawn rightwards -- a right-aligned label
	// after a left-aligned one -- is left alone.
	if (row == last_row_ && q.x() >= last_x_ && col < last_end_col_)
		col = last_end_col_;
	int x = col;
	for (const QString &cl : to_clusters(text)) {
		const Attrs had = dev_->buffer().at(x, row).attrs & Attrs(Attr::Reverse);
		x += dev_->buffer().text(x, row, cl, pen_to_fg(pen_), Color(), a | had);
	}
	last_row_ = row;
	last_end_col_ = x;
	last_x_ = q.x();
}

void CellPaintEngine::drawRects(const QRectF *r, int n) { for (int i = 0; i < n; ++i) fill_rectf(r[i]); }
void CellPaintEngine::drawRects(const QRect *r, int n)  { for (int i = 0; i < n; ++i) fill_rectf(QRectF(r[i])); }
void CellPaintEngine::drawLines(const QLineF *l, int n) { for (int i = 0; i < n; ++i) line(l[i]); }
void CellPaintEngine::drawLines(const QLine *l, int n)  { for (int i = 0; i < n; ++i) line(QLineF(l[i])); }

void CellPaintEngine::drawPath(const QPainterPath &path) {
	// Solid-brush paths are fills -- this is how QTextLayout paints selection
	// regions (section 17.2). Only brushless paths degrade to outline boxes.
	fill_rectf(path.boundingRect(), /*outline_only=*/brush_.style() == Qt::NoBrush);
}

void CellPaintEngine::drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &) {
	QRect c = to_cells(r);
	if (!c.isValid()) return;
	// Wholly outside the clip is nothing to draw. A partly-clipped placement
	// is left whole: the graphics tier crops every placement to the viewport
	// already (crop_placement), and cropping twice by different rules is how
	// the two answers part company.
	const std::optional<QRect> clip = clip_cells();
	if (clip && !clip->intersects(c)) return;
	if (c.width() >= 2 && c.height() >= 2)      // section 5.7: real image -> placement
		dev_->placements.append({quint64(pm.cacheKey()), c, pm});
	else {
		// Too small to be a picture -- an icon -- so it is substituted by a
		// glyph (section 8.6). The substitution covers the CELLS THE IMAGE
		// OCCUPIES rather than one of them, which for a 1x1 icon is the same
		// thing and for anything wider is not.
		//
		// Measured on a tab being dragged. Qt moves a movable tab by grabbing
		// it into a pixmap inside a private widget, 82x19 px here, which is
		// 8 cells by 1 -- so it failed "two cells in each direction", took
		// this branch, and marked ONE cell. The other seven went on showing
		// the tab bar underneath, which is not what the widget tree says is
		// there: a picture covering eight cells left seven of them stale.
		// One shaded block is an honest "a picture is here"; seven cells of
		// something that has moved away is not.
		//
		// Whether a wide, short image should be a PLACEMENT instead of a
		// glyph at all is a separate question and a real one -- 8x1 is a
		// perfectly good kitty placement, and the mosaic tier has two
		// vertical samples per cell to draw it with. It is not answered here,
		// because relaxing the threshold by area or by aspect would also
		// promote the 2x1 that a 16x16 icon becomes, and that icon arriving
		// as a shaded block rather than a glyph is the fault this branch
		// exists to prevent.
		// The colour goes with it. Two icons that differ only in colour --
		// a red status light and a grey one -- substituted to the same
		// default-coloured block, so a row of them was a row of identical
		// smudges. The mosaic tier, which is the other path the same content
		// takes when a terminal has no graphics protocol, carries colour;
		// this one threw it away.
		//
		// An application's own colour, passed through, which is the rule
		// cell_geometry.h's fg_for() applies to every colour no palette role
		// explains. Averaged over the image and weighted by alpha, because
		// that is what the half-block tier would show if the picture had room
		// to be one. Strided so a large pixmap substituted into one cell
		// costs a bounded scan rather than one per pixel.
		const QImage img = pm.toImage();
		qint64 r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
		const int step_x = qMax(1, img.width() / 32), step_y = qMax(1, img.height() / 32);
		for (int y = 0; y < img.height(); y += step_y)
			for (int x = 0; x < img.width(); x += step_x) {
				const QRgb px = img.pixel(x, y);
				const int a = qAlpha(px);
				r_sum += qint64(qRed(px)) * a;
				g_sum += qint64(qGreen(px)) * a;
				b_sum += qint64(qBlue(px)) * a;
				a_sum += a;
			}
		// Nothing to stand for. A fully transparent pixmap drew a block that
		// said a picture was there when none was.
		if (a_sum == 0) return;
		Cell v;
		v.ch = QStringLiteral("▒");
		v.fg = Color::rgb(qRgb(int(r_sum / a_sum), int(g_sum / a_sum),
		                       int(b_sum / a_sum)));
		dev_->buffer().fill(c, v);
	}
}

void CellPaintEngine::drawPolygon(const QPointF *pts, int n, PolygonDrawMode) {
	QPolygonF p;
	for (int i = 0; i < n; ++i) p << pts[i];
	fill_rectf(p.boundingRect(), true);
}

// Is `role` one of the surfaces a widget sits ON, as opposed to something
// drawn over one? The distinction decides what an unthemed fill means: a
// surface the theme has not coloured is the terminal's own background and is
// erased, while a selection the theme has not coloured still has to be
// visible.
static bool is_surface_role(QPalette::ColorRole role) {
	return role == QPalette::Window || role == QPalette::Base
	    || role == QPalette::Button || role == QPalette::AlternateBase;
}

// A rect covering less than half a cell in either direction: a caret, a rule,
// a hairline. to_cells() cannot represent it, since it rounds every extent up
// to at least one whole cell.
bool CellPaintEngine::is_thin(const QRectF &r) const {
	const QRectF m = xf_.mapRect(r).translated(dev_->origin);
	return m.width() * 2 < GridMetrics::cw() || m.height() * 2 < GridMetrics::ch();
}

// Fill classification (sections 6 and 17.2). The brush colour is matched back to
// the palette role that produced it, and the role is resolved through the
// active CellTheme -- theme() is the single source for what a cell is
// coloured, not QGuiApplication::palette().
//
// A surface role the theme leaves at Color::Default erases to the terminal's
// own background, which is what CellTheme::terminal_default() means and is the
// behaviour this replaced. A themed surface paints. A non-surface role the
// theme does not name, and any colour with no role behind it at all, keeps the
// application's own colour -- that is how a selection reaches the cells under
// the default theme, and how Channel B output reaches them at all.
void CellPaintEngine::fill_rectf(const QRectF &r, bool outline_only) {
	QRect c = to_cells(r);
	if (!c.isValid() || c.width() > 400 || c.height() > 200) return;
	const std::optional<QRect> clip = clip_cells();
	if (clip && clip->isEmpty()) return;

	// A rect thinner than half a cell does not cover the cell, so it cannot
	// stand for the cell's background -- to_cells() rounds it up to a whole
	// cell, and filling that cell erases whatever glyph is in it. The case
	// that found this is a text caret: QLineEdit paints it as a 1px-wide rect
	// in the Text colour AFTER drawing the line, so a focused editor blanked
	// the character the caret sat on. Reproduced as a QSpinBox whose value
	// vanished once it had focus and a key -- the value was in the widget and
	// in the trace, and a 1.0x19.0px fill at its cell removed it.
	//
	// Dropping it loses nothing: the caret is carried by the terminal's own
	// cursor, which Compositor::compose() places from the focus widget and
	// ITerminalBackend::set_cursor() emits. A thin fill may still colour a
	// cell that is empty, which is what keeps a rule drawn on a blank row.
	const bool thin = is_thin(r);
	// A box keeps its shape and loses the cells outside the clip, rather than
	// being shrunk to fit: a smaller complete rectangle is a different frame,
	// and dropping the whole thing loses a border that is mostly visible.
	if (outline_only || brush_.style() == Qt::NoBrush) { box(c, clip); return; }
	if (clip) {
		c &= *clip;
		if (c.isEmpty()) return;
	}

	const QRgb col = brush_.color().rgba();
	const QPalette &pal = QGuiApplication::palette();
	QPalette::ColorRole matched = QPalette::NoRole;
	for (QPalette::ColorRole role : {QPalette::Window, QPalette::Base,
		                             QPalette::Button, QPalette::AlternateBase,
		                             QPalette::Highlight, QPalette::ToolTipBase})
		if (pal.color(role).rgba() == col) { matched = role; break; }

	Color bg = matched == QPalette::NoRole ? Color::rgb(col)
	                                       : theme().background(matched);
	Attrs mark;
	if (bg.kind() == Color::Default) {
		if (is_surface_role(matched)) {
			if (!thin && c.width() > 1 && c.height() > 1) dev_->buffer().fill(c, Cell{});
			return;
		}
		// A HIGHLIGHT the theme has not coloured is reverse video, not the
		// desktop's blue.
		//
		// qtty/theme.h states the rule this restores: the default theme keeps
		// every role at Color::Default and "marks emphasis with attrs, not
		// colour". GridStyle already obeys it -- State_Selected is
		// Attr::Reverse at the item view, the menu item and the tab -- and
		// this one line did not, so one program showed a selection two ways:
		// measured side by side, a QLineEdit's came out bg=#308cc6 while the
		// list beside it came out reverse.
		//
		// The fallback below is right for what it was written for and this is
		// not it. It exists for a colour with NO palette role behind it --
		// Channel B output, something the application coloured itself -- and
		// Highlight is a role, matched, whose themed answer was "the
		// terminal's own scheme". Taking the desktop's literal RGB overrode
		// the theme rather than standing in for it, and made the most common
		// highlight in the program depend on which desktop launched it.
		if (matched == QPalette::Highlight) mark = Attr::Reverse;
		else bg = Color::rgb(col);                  // unthemed fill, kept
	}
	Cell v;
	v.bg = bg;
	v.attrs = mark;
	if (!thin) { dev_->buffer().fill(c, v); return; }
	for (int y = c.top(); y <= c.bottom(); ++y)
		for (int x = c.left(); x <= c.right(); ++x) {
			Cell &cell = dev_->buffer().at(x, y);
			if (cell.ch == QStringLiteral(" ")) { cell.bg = bg; cell.attrs |= mark; }
		}
}

void CellPaintEngine::box(const QRect &c, const std::optional<QRect> &clip) {
	if (c.width() < 2 || c.height() < 2) return;
	CellBuffer &b = dev_->buffer();
	// Per cell, so that a box crossing the clip's edge keeps the part inside
	// it and loses the part outside. Shrinking the rectangle instead would
	// draw a smaller complete frame, which is a different frame; dropping it
	// would lose a border that is mostly visible.
	const auto put = [&](int x, int y, const QString &g) {
		if (clip && !clip->contains(x, y)) return;
		b.at(x, y).ch = g;
	};
	for (int x = c.left() + 1; x < c.right(); ++x) {
		put(x, c.top(), QStringLiteral("─"));
		put(x, c.bottom(), QStringLiteral("─"));
	}
	for (int y = c.top() + 1; y < c.bottom(); ++y) {
		put(c.left(), y, QStringLiteral("│"));
		put(c.right(), y, QStringLiteral("│"));
	}
	put(c.left(), c.top(), QStringLiteral("┌"));
	put(c.right(), c.top(), QStringLiteral("┐"));
	put(c.left(), c.bottom(), QStringLiteral("└"));
	put(c.right(), c.bottom(), QStringLiteral("┘"));
}

void CellPaintEngine::line(const QLineF &l) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	QLineF m(xf_.map(l.p1()) + QPointF(dev_->origin), xf_.map(l.p2()) + QPointF(dev_->origin));
	CellBuffer &b = dev_->buffer();
	// The cell a pixel is IN, not the boundary it is nearest. Rounding put a
	// rule on the last pixel row of a widget into the row below it: a toolbar
	// 19 pixels tall draws its bottom border at y = 17 and 18, and qRound(17/19)
	// is 1 -- so the border was written across a row the toolbar does not
	// occupy, over whatever lived there. Measured as "<Save>----------" on a
	// central widget's own row, and it is the same shape as the caret fault in
	// section 7.2: a sub-cell mark landing in a neighbour's cell.
	//
	// Flooring also fixes the span's far end, which ran one column past the
	// buffer -- a line to x=399 asked for column qRound(39.9) = 40 on a
	// 40-column buffer. CellBuffer::at() returns a scratch cell out of range,
	// so it wrote nowhere rather than corrupting anything; it was invisible
	// for that reason rather than harmless by design.
	const auto cell_of = [](double px, int size) { return int(std::floor(px / size)); };

	// The cells the line COVERS, not the ones it touches. A cell the line
	// enters by a pixel is not a cell the line is in, and painting it writes a
	// rule where nothing was drawn.
	//
	// This is the same half-cell test fill_rectf() already applies across a
	// rect's thin axis, applied along a line's length instead: a rect thinner
	// than half a cell does not stand for that cell's background, and a line
	// overlapping less than half a cell does not stand for that cell's rule.
	//
	// Measured on a mnemonic. Qt underlines the marked letter with a line one
	// cell long that starts a pixel early -- traced as (39.00,16.50) to
	// (49.00,16.50) against cw = 10 -- so flooring both ends gave cells 3 and
	// 4. Cell 4 held the letter and was skipped by the blank test below; cell
	// 3 was the gap between a check box's indicator and its label, and every
	// check box, radio button and group box with a mnemonic rendered a rule
	// in that gap, between the indicator and the first letter. The letter
	// keeps its underline attribute either way, which arrives through the
	// font rather than through here.
	//
	// A line wholly inside one cell keeps that cell: it is the only cell it
	// can be in, and a short rule on a blank row is a thing this draws on
	// purpose.
	const auto covered = [](double lo, double hi, int size) {
		int first = int(std::floor(lo / size)), last = int(std::floor(hi / size));
		if (first < last && (first + 1) * double(size) - lo < size / 2.0) ++first;
		if (last > first && hi - last * double(size) < size / 2.0) --last;
		return QPair<int, int>(first, last);
	};
	// A rule goes where a rule fits, and nowhere else.
	//
	// Writing only into a blank cell is right and is not enough: it cannot
	// tell a space a LABEL wrote from a cell nothing has touched, so a rule
	// crossing a row of text filled the gaps between the words. Measured,
	// that is what a QTableView's grid did to its own labels -- "a label far
	// wider than its column" came out with a rule in place of every space.
	//
	// So a rule that meets any content is not drawn at all. That is this
	// tree's existing answer for chrome a cell grid cannot represent, applied
	// where it had not reached: CE_HeaderSection draws no chrome and only its
	// label, PE_PanelToolBar draws nothing, PE_IndicatorToolBarHandle draws
	// nothing because its extent is nil, and draw_box() refuses a rectangle
	// under two cells because a border needs a cell of its own. A horizontal
	// grid line between two ONE-CELL rows has no cell of its own either.
	//
	// The blast radius was measured over the whole suite rather than assumed:
	// 510 horizontal rules land on entirely clear cells and are untouched
	// here; 8 land on entirely occupied ones and already drew nothing; and
	// every one of the 426 that were partial belongs to a table's grid, as do
	// all 102 vertical rules, which run down columns already carrying the
	// horizontal grid they crossed.
	//
	// What this does not do is make a table grid possible. That needs the
	// buffer to know a cell was WRITTEN, which is a per-cell flag and a change
	// to the model every tier reads -- and the choice then stops being "a
	// broken grid or none" and becomes a real one. Until then a table renders
	// the way a TUI table usually does, with whitespace between its columns.
	const auto clear_run = [&b](int fixed, int from, int to, bool horizontal) {
		for (int i = from; i <= to; ++i) {
			const Cell &c = horizontal ? b.at(i, fixed) : b.at(fixed, i);
			if (c.ch != QStringLiteral(" ")) return false;
		}
		return true;
	};
	const std::optional<QRect> clip = clip_cells();
	if (clip && clip->isEmpty()) return;
	if (qAbs(m.dy()) < ch / 2.0) {
		const int y = cell_of(m.y1(), ch);
		auto span = covered(qMin(m.x1(), m.x2()), qMax(m.x1(), m.x2()), cw);
		if (clip) {
			if (y < clip->top() || y > clip->bottom()) return;
			span.first = qMax(span.first, clip->left());
			span.second = qMin(span.second, clip->right());
			if (span.first > span.second) return;
		}
		if (!clear_run(y, span.first, span.second, true)) return;
		for (int x = span.first; x <= span.second; ++x)
			b.at(x, y).ch = QStringLiteral("─");
	} else if (qAbs(m.dx()) < cw / 2.0) {
		const int x = cell_of(m.x1(), cw);
		auto span = covered(qMin(m.y1(), m.y2()), qMax(m.y1(), m.y2()), ch);
		if (clip) {
			if (x < clip->left() || x > clip->right()) return;
			span.first = qMax(span.first, clip->top());
			span.second = qMin(span.second, clip->bottom());
			if (span.first > span.second) return;
		}
		if (!clear_run(x, span.first, span.second, false)) return;
		for (int y = span.first; y <= span.second; ++y)
			b.at(x, y).ch = QStringLiteral("│");
	}
}

} // namespace Qtty
