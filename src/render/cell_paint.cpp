// src/render/cell_paint.cpp -- CellPaintDevice / CellPaintEngine (section 5.4).
#include "qtty/paint.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <QVarLengthArray>
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
// design.md section 8.4's Unsupported tier: "renders a labelled placeholder
// box". It promised one and nothing drew it -- measured, a QGraphicsView came
// out as 42 glyphs, every one of them its own empty QFrame border, which is
// what any framed widget with no content draws. So an unsupported widget was
// indistinguishable from a bug, which is the one thing a placeholder exists
// to prevent.
//
// By class rather than by behaviour, because 8.4 names the classes and
// because there is nothing to detect at paint time: a widget that draws
// with OpenGL or into a scene issues no primitive this engine ever sees. The
// walk is up the metaObject chain so an application's own subclass of one of
// these is recognised too, which is how they are actually used.
static const char *unsupported_class(const QObject *o) {
	static const char *const names[] = {
		"QGraphicsView", "QOpenGLWidget", "QQuickWidget", "QQuickView",
		"QWebEngineView", "QVideoWidget", "QAxWidget",
	};
	for (const QMetaObject *m = o->metaObject(); m; m = m->superClass())
		for (const char *n : names)
			if (qstrcmp(m->className(), n) == 0) return n;
	return nullptr;
}

// The box, and the name of what is missing from it. A frame says "something
// belongs here" and the label says what, which together are the difference
// between an unsupported widget and a broken one.
static void draw_placeholder(CellBuffer &buf, const QRect &c, const QString &what) {
	if (c.width() < 2 || c.height() < 2) {
		// Too small for a box. One shaded cell still says something is here,
		// which is the same answer the pixmap substitution gives and for the
		// same reason.
		if (buf.writable(c.left(), c.top())) {
			Cell v; v.ch = QStringLiteral("▒");
			buf.at(c.left(), c.top()) = v;
		}
		return;
	}
	const auto put = [&](int x, int y, const QString &g) {
		if (buf.writable(x, y)) { Cell v; v.ch = g; buf.at(x, y) = v; }
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

	// The label, centred, elided to the room between the borders. A name cut
	// without an ellipsis reads as a different class.
	const int room = c.width() - 2;
	if (room <= 0) return;
	const QString label = elide_to_cells(what, room);
	const int x0 = c.left() + 1 + qMax(0, (room - int(label.size())) / 2);
	const int y = c.top() + c.height() / 2;
	// Written cell by cell like the border above, and not through
	// CellBuffer::text(), which honours the device clip -- measured, the box
	// appeared and the label did not, because a clip was in force from
	// whatever was drawing when this ran. The placeholder is not the
	// application's content and is not subject to the application's clip: it
	// is this library saying what it cannot draw.
	for (int i = 0; i < label.size(); ++i)
		put(x0 + i, y, QString(label.at(i)));
}

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
		// Before the two interfaces below, because an unsupported widget
		// that also implemented one of them would be a contradiction -- and
		// because what it draws is exactly what must not reach Channel B.
		if (const char *what = unsupported_class(o)) {
			draw_placeholder(dev->buffer(),
			                 cells_of_rect(w->rect(), w, dev->origin),
			                 QString::fromLatin1(what));
			return true;                           // consumed
		}
		// And the whole subtree under one, drawing nothing. The box above is
		// the widget's own paint; its CHILDREN paint separately and would go
		// straight over it -- measured, a QGraphicsView's viewport drew the
		// scene's text items across the placeholder's label, so the box said
		// QGraphicsView and then said "scene text" instead. Its scroll bars
		// would have followed. The ancestor has already said what is here.
		for (QWidget *p = w->parentWidget(); p; p = p->parentWidget())
			if (const char *pw = unsupported_class(p)) {
				// Drawn AGAIN, from the ancestor's rectangle, and not merely
				// consumed. Consuming a child's paint does not stop Qt
				// filling that child's BACKGROUND first, and the fill lands
				// inside the box: measured, the border survived and the
				// label did not, because a QGraphicsView's viewport is
				// inset by the frame and its fill cleared exactly the
				// interior. Redrawing on every consumed descendant makes the
				// placeholder the last thing written in its own area, which
				// is the only ordering that does not depend on how many
				// children a widget happens to have.
				draw_placeholder(dev->buffer(),
				                 cells_of_rect(p->rect(), p, dev->origin),
				                 QString::fromLatin1(pw));
				return true;
			}
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

// Defined below, each beside the rule it implements. Declared here because
// the text, stroke and fill paths all need them and all come first.
static QColor brush_colour(const QBrush &b);
static Color ink_over(Color ink, int alpha, const Color &ground);

void CellPaintEngine::updateState(const QPaintEngineState &s) {
	if (s.state() & DirtyPen)       pen_ = s.pen();
	if (s.state() & DirtyBrush)     brush_ = s.brush();
	if (s.state() & DirtyFont)      font_ = s.font();
	if (s.state() & DirtyTransform) xf_ = s.transform();
	if (s.state() & DirtyOpacity)   opacity_ = s.opacity();
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
		// No "+ 1". A QRectF's right() is EXCLUSIVE -- x + width, not
		// x + width - 1 -- so ceil(right / cw) is already one past the last
		// cell the rectangle covers, and adding another admitted a cell
		// nothing had drawn in. Measured: a widget six cells wide at column
		// two occupies pixels 20..79, ceil(80 / 10) is 8, and the clip came
		// out as columns 2..8 for a widget ending at column 7. That is where
		// the one-cell overhang came from -- a check box, a radio button, a
		// combo box and a group box each put the next character of their
		// label in the widget beside them, always exactly one cell.
		//
		// It was read as the "keep a straddling cluster whole" rule below,
		// which would have been a decision to reverse. It is arithmetic.
		//
		// Still outward, which is the point of the function: a rectangle
		// ending part-way into a cell still admits that cell, because
		// ceil() of a fraction rounds up. The eight-pixel clip against a
		// nineteen-pixel cell that this comment was written for gives
		// ceil(8 / 19) - 0 = 1 row, not none.
		const int l = int(std::floor(m.left() / cw)), t = int(std::floor(m.top() / ch));
		return QRect(l, t, int(std::ceil(m.right() / cw)) - l,
		             int(std::ceil(m.bottom() / ch)) - t);
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
// different one -- and the wrapper that used to stand here went with it, its
// last caller having moved to text_style_for(). The comment stays because it
// records where the rule LIVES, which is the thing a reader of this file
// needs; the function was one indirection to the same place.

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
	//
	// STRICTLY right of, not at or right of. Qt draws a selected field's text
	// TWICE at the same origin -- once clipped to the selection, once for the
	// rest -- and `>=` read the second as a run consecutive with the first,
	// pushing it right by the width of the first. A QLineEdit holding "hi"
	// with everything selected rendered "hihi".
	//
	// It was invisible until the SE_LineEditContents fix, because the invalid
	// contents rect made the clip an empty region and Qt's selected run was
	// dropped entirely: two defects holding each other up, and correcting
	// either one alone shows the other. A genuinely consecutive run always
	// advances -- a zero-advance run is empty and returns above -- so nothing
	// this guard exists for is lost by demanding it.
	if (row == last_row_ && q.x() > last_x_ && col < last_end_col_)
		col = last_end_col_;
	// The pen carries the state as well as the colour. Qt paints a disabled
	// widget with the palette's Disabled group, and text_style_for() now
	// recognises that group and answers with the role's ordinary colour plus
	// Attr::Dim -- the same sentence GridStyle writes through with_state(),
	// where before this channel wrote a 24-bit grey and no attribute.
	// Qt::transparent TEXT drew opaque BLACK, which is the worst of this
	// family: drawing a string in a transparent pen is an ordinary way to
	// hide it, and this made it visible. The alpha is the pen's own times
	// the painter's opacity, so both routes to invisibility are honoured.
	const QColor ink = pen_ink();
	if (ink.alpha() == 0) return;
	const TextStyle ts = text_style_for(qRgb(ink.red(), ink.green(),
	                                         ink.blue()));
	int x = col;
	for (const QString &cl : to_clusters(text)) {
		const Attrs had = dev_->buffer().at(x, row).attrs & Attrs(Attr::Reverse);
		// Blended per cell, so a translucent string over two different
		// grounds reads as two colours rather than one.
		const Color use = ink_over(ts.color, ink.alpha(),
		                           dev_->buffer().at(x, row).bg);
		x += dev_->buffer().text(x, row, cl, use, Color(), a | ts.attrs | had);
	}
	last_row_ = row;
	last_end_col_ = x;
	last_x_ = q.x();
}

void CellPaintEngine::drawRects(const QRectF *r, int n) { for (int i = 0; i < n; ++i) fill_rectf(r[i]); }
void CellPaintEngine::drawRects(const QRect *r, int n)  { for (int i = 0; i < n; ++i) fill_rectf(QRectF(r[i])); }
void CellPaintEngine::drawLines(const QLineF *l, int n) { for (int i = 0; i < n; ++i) line(l[i]); }
void CellPaintEngine::drawLines(const QLine *l, int n)  { for (int i = 0; i < n; ++i) line(QLineF(l[i])); }

// Solid-brush paths are fills -- this is how QTextLayout paints selection
// regions (section 17.2) -- and brushless ones are strokes. Both were the
// bounding rectangle: a fill of the box around the shape, or box() drawn round
// it. QPainter::drawEllipse and QPainter::drawArc arrive here, so a circle was
// a rectangle and an arc was a rectangle.
//
// Flattened THROUGH the transform rather than before it. QPainterPath decides
// how finely to subdivide a curve from the size of the thing it is
// subdividing, so flattening in logical coordinates and mapping afterwards
// makes a path that is scaled up come out as visible straight runs.
void CellPaintEngine::drawPath(const QPainterPath &path) {
	const std::optional<QRect> clip = clip_cells();
	if (clip && clip->isEmpty()) return;
	const QPointF origin(dev_->origin);
	if (brush_.style() != Qt::NoBrush) {
		// A path too thin to cover a cell centre is what fill_rectf()'s thin
		// branch exists for -- a caret, a rule, a hairline -- and the scanline
		// cannot represent it: no cell centre is inside, so it would draw
		// nothing where this used to colour a blank cell. Kept on the old road
		// for exactly that case, which is also the only case where the
		// bounding rectangle and the shape are the same thing by construction.
		if (is_thin(path.boundingRect())) {
			fill_rectf(path.boundingRect());
		} else {
			for (const QPolygonF &poly : path.toFillPolygons(xf_))
				fill_polygon(poly.translated(origin),
				             path.fillRule() == Qt::WindingFill, clip);
		}
	}
	if (pen_.style() != Qt::NoPen)
		for (const QPolygonF &poly : path.toSubpathPolygons(xf_))
			stroke_polyline(poly.translated(origin), false, clip);
}

void CellPaintEngine::drawPixmap(const QRectF &r, const QPixmap &whole,
                                 const QRectF &sr) {
	// The SOURCE rectangle, which this accepted and ignored. Qt passes the
	// whole pixmap for the two-argument forms, so nothing in this tree ever
	// exercised it -- measured, every drawPixmap the suite produces arrives
	// with `source 0,0 WxH` for a WxH pixmap. The gap belongs to an
	// application drawing one sprite out of an atlas: it got the ATLAS
	// placed, at the right size and silently, which is a wrong picture
	// rather than a missing one.
	const QRect src = sr.toRect();
	const QPixmap pm = (src.isValid() && src != whole.rect() && !whole.isNull())
	                 ? whole.copy(src)
	                 : whole;
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
		// One colour per HALF CELL rather than one for the whole picture.
		//
		// A single average is a picture reduced to its mean, and for an icon
		// that encodes its meaning as a SHAPE that is the whole meaning gone.
		// The case that produced this: a sibling project draws five status
		// icons whose states differ by shape deliberately, its own header
		// recording that "around one man in twelve cannot reliably tell the
		// amber from the green" -- and every one of them arrived here as two
		// cells of one averaged colour, distinct only by hue. The
		// accessibility property the design was built around was exactly
		// what the substitution removed.
		//
		// The upper half block gives a top and a bottom colour per cell, so
		// an icon over two cells carries four samples instead of one. That
		// is not a picture either, but it is the difference between a bar
		// and a disc.
		//
		// HALF blocks and not quadrants, measured: of 20 fixed-pitch
		// families here 11 carry U+2580 and only 8 carry U+2596..U+259F --
		// Liberation Mono, Noto Mono, Inconsolata and Nimbus Mono PS have
		// the half and not the quadrants. 11 is the same set that carries
		// the box-drawing rules this style already draws every frame, so
		// this asks for nothing new of a font.
		const auto mean = [&](int y0, int y1, int x0, int x1, bool *any) {
			qint64 r = 0, g = 0, b = 0, a = 0;
			const int sx = qMax(1, (x1 - x0) / 16), sy = qMax(1, (y1 - y0) / 16);
			for (int y = y0; y < y1; y += sy)
				for (int x = x0; x < x1; x += sx) {
					const QRgb px = img.pixel(x, y);
					const int al = qAlpha(px);
					r += qint64(qRed(px)) * al;
					g += qint64(qGreen(px)) * al;
					b += qint64(qBlue(px)) * al;
					a += al;
				}
			*any = a > 0;
			return a > 0 ? qRgb(int(r / a), int(g / a), int(b / a)) : qRgb(0, 0, 0);
		};

		bool whole_any = false;
		mean(0, img.height(), 0, img.width(), &whole_any);
		// Nothing to stand for. A fully transparent pixmap drew a block that
		// said a picture was there when none was.
		if (!whole_any) return;

		for (int cy = c.top(); cy <= c.bottom(); ++cy) {
			for (int cx = c.left(); cx <= c.right(); ++cx) {
				// The slice of the image this cell covers, and its two
				// halves. Derived from the cell's position within c so a
				// picture wider than one cell is sampled across rather than
				// repeated.
				const int x0 = (cx - c.left()) * img.width() / c.width();
				const int x1 = qMax(x0 + 1, (cx - c.left() + 1) * img.width() / c.width());
				const int y0 = (cy - c.top()) * img.height() / c.height();
				const int y1 = qMax(y0 + 1, (cy - c.top() + 1) * img.height() / c.height());
				const int mid = qMax(y0 + 1, (y0 + y1) / 2);
				bool top_any = false, bot_any = false;
				const QRgb top = mean(y0, mid, x0, x1, &top_any);
				const QRgb bot = mean(mid, qMax(mid + 1, y1), x0, x1, &bot_any);
				Cell v;
				// A cell whose two halves agree keeps the shaded block this
				// has always drawn. That is deliberate rather than
				// conservative: section 8.6's substitution says "a picture
				// is here", several checks pin it, and an icon with no
				// vertical structure has nothing more to say. The half
				// block is ADDED for the cells that do differ, so this
				// carries strictly more than before and changes no
				// convention.
				const auto close = [](QRgb a, QRgb b) {
					return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b))
					     + qAbs(qBlue(a) - qBlue(b)) < 24;
				};
				if (top_any && bot_any && close(top, bot)) {
					v.ch = QStringLiteral("▒");
					v.fg = Color::rgb(qRgb((qRed(top) + qRed(bot)) / 2,
					                       (qGreen(top) + qGreen(bot)) / 2,
					                       (qBlue(top) + qBlue(bot)) / 2));
				} else if (top_any && bot_any) {
					v.ch = QStringLiteral("▀");
					v.fg = Color::rgb(top);
					v.bg = Color::rgb(bot);
				} else if (top_any) {
					v.ch = QStringLiteral("▀");
					v.fg = Color::rgb(top);
				} else if (bot_any) {
					v.ch = QStringLiteral("▄");
					v.fg = Color::rgb(bot);
				} else {
					continue;          // this cell of the icon is transparent
				}
				if (dev_->buffer().writable(cx, cy)) dev_->buffer().at(cx, cy) = v;
			}
		}
	}
}

// The MODE, which this took and ignored. Qt sends a polyline, an odd-even
// polygon, a winding polygon and a convex polygon through one entry point and
// says which by the third argument; this passed outline_only = true for all
// four, so QPainter::drawPolyline came out as a closed box and a filled
// polygon drew no fill at all.
//
// The pen and the brush are honoured here rather than left to QPainter,
// because QPainter does not filter this call: QPainter::drawPolygon hands the
// engine the points whatever the pen and brush are, and the engine is what
// decides that Qt::NoPen means no outline.
void CellPaintEngine::drawPolygon(const QPointF *pts, int n, PolygonDrawMode mode) {
	if (n < 2) return;
	const std::optional<QRect> clip = clip_cells();
	if (clip && clip->isEmpty()) return;
	QPolygonF p;
	p.reserve(n);
	for (int i = 0; i < n; ++i) p << xf_.map(pts[i]) + QPointF(dev_->origin);
	const bool closed = mode != PolylineMode;
	// Fill first, then stroke. The fill writes whole cells and would erase an
	// outline drawn before it; the stroke writes only the glyph and leaves the
	// background the fill put there, which is how a filled shape keeps both.
	if (closed && brush_.style() != Qt::NoBrush)
		fill_polygon(p, mode == WindingMode, clip);
	if (pen_.style() != Qt::NoPen) stroke_polyline(p, closed, clip);
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
	// Bounded by the BUFFER rather than by a pair of literals. The 400x200
	// cap that stood here carried no reason anywhere and was applied to the
	// UNCLIPPED cell rect, before the clip narrowed it -- so it needed not a
	// huge terminal but a huge LAYER, which is the ordinary state of a
	// window whose layout minimum exceeds the screen and which section 7
	// then scrolls.
	//
	// The cliff was exact and the failure total: measured on an 80-column
	// buffer, a fill 400 cells wide coloured all 80 visible cells and one
	// 401 wide coloured none. Channel A keeps drawing either way, so what a
	// user got past the cliff was text on the terminal's own ground with
	// every application colour, themed surface and selection fill gone.
	//
	// Whatever the cap was for, it can only have been the cost of the loops
	// below, and the buffer bounds those exactly -- nothing outside it can
	// be written. So the test is whether the rectangle reaches the buffer at
	// all, which is the same question asked correctly.
	const QRect bounds(0, 0, dev_->buffer().cols(), dev_->buffer().rows());
	if (!c.isValid() || !c.intersects(bounds)) return;
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
	// box() keeps the UNCLAMPED rectangle deliberately: it draws borders at
	// the rectangle's own edges, and clamping first would move a border to
	// the buffer's edge and draw a frame the widget does not have.
	if (outline_only || brush_.style() == Qt::NoBrush) { box(c, clip); return; }
	// The fill path is clamped, and that is what pays for removing the
	// literal cap above. CellBuffer::fill() tests every write, so it was
	// never unsafe -- but it LOOPS the whole rectangle, and a layer wider
	// than the terminal is now the ordinary case rather than one the cap
	// refused. Bounded here, the loops below cost the buffer and not the
	// layer.
	c &= bounds;
	if (c.isEmpty()) return;
	if (clip) {
		c &= *clip;
		if (c.isEmpty()) return;
	}

	// Alpha, which this engine discarded entirely until it was measured
	// against a real application's overlay.
	//
	// A FULLY TRANSPARENT brush drew solid BLACK. brush_cell() takes
	// brush_.color().rgba(), which matches no palette role, and Color::rgb()
	// keeps the RGB bytes -- and for Qt::transparent those are zero. So
	// fillRect(r, Qt::transparent), an ordinary way of saying "leave this
	// alone", blacked the cells out instead.
	// The brush's own alpha TIMES the painter's opacity, which is how Qt
	// composes the two. Reading only the first drew a setOpacity(0.5) fill
	// fully opaque.
	const int alpha = qBound(0, int(brush_colour(brush_).alpha() * opacity_
	                               + 0.5), 255);
	if (alpha == 0) return;

	const FillCell f = brush_cell();
	if (f.erase) {
		if (!thin && c.width() > 1 && c.height() > 1) dev_->buffer().fill(c, Cell{});
		return;
	}
	// A TRANSLUCENT brush drew opaque, which is worse than it sounds: an
	// overlay exists to shade what is under it, so discarding the alpha
	// REPLACES the thing the overlay was drawn for. Measured against
	// bbq-predictor's grill window -- QColor(0xff, 0x8b, 0x33, 80), a 31%
	// wash the pixel version shows the temperature curve through -- which
	// came out here as a solid orange block with the curve gone underneath.
	//
	// Blended per cell against what the cell already holds, so one wash over
	// two different grounds gives two different answers. That is what makes
	// a shaded curve legible rather than uniform, and it is the whole
	// behaviour being restored.
	//
	// The glyph and its colour are left alone. A cell holds one character;
	// tinting the text as well costs contrast and buys nothing, and a wash
	// that ERASED text would be the same defect one layer along.
	//
	// Where the ground is not a concrete colour -- a Default background is
	// the terminal's own and this layer does not know it -- the shade is
	// laid down opaque, as it was before. That keeps it visible rather than
	// dropping it, and leaves nothing looking worse than it did.
	if (!thin && alpha < 255) {
		const QColor src = brush_colour(brush_);
		auto mix = [alpha](int s, int d) {
			return (s * alpha + d * (255 - alpha)) / 255;
		};
		for (int y = c.top(); y <= c.bottom(); ++y)
			for (int x = c.left(); x <= c.right(); ++x) {
				Cell &cell = dev_->buffer().at(x, y);
				if (cell.bg.kind() != Color::Rgb) {
					cell.bg = f.cell.bg;
					continue;
				}
				const QRgb d = cell.bg.value();
				cell.bg = Color::rgb(qRgb(mix(src.red(), qRed(d)),
				                          mix(src.green(), qGreen(d)),
				                          mix(src.blue(), qBlue(d))));
			}
		return;
	}
	if (!thin) { dev_->buffer().fill(c, f.cell); return; }
	for (int y = c.top(); y <= c.bottom(); ++y)
		for (int x = c.left(); x <= c.right(); ++x) {
			Cell &cell = dev_->buffer().at(x, y);
			if (cell.ch == QStringLiteral(" ")) {
				cell.bg = f.cell.bg;
				cell.attrs |= f.cell.attrs;
			}
		}
}

// The colour a BRUSH stands for, which is not always QBrush::color().
//
// For a gradient brush that accessor answers black -- it is documented to
// return "the brush colour", and a gradient has none -- so a gradient-filled
// area came out here as a solid BLACK block. Measured on the ordinary way a
// chart shades an area, a vertical blue gradient: eight cells of #000000,
// which is a colour nothing in the drawing contains.
//
// Averaged over the gradient's own stops instead, weighted by the span each
// stop covers so a long tail counts for more than a pinned endpoint. That is
// not a gradient: a cell grid cannot show one, and evaluating per cell would
// need the gradient's coordinate space, which is a larger change than this
// defect justifies. It is the colour the area actually is, and being roughly
// right beats being exactly black.
static QColor brush_colour(const QBrush &b) {
	// A TEXTURE brush is the other case where QBrush::color() means nothing
	// and answers black -- the same trap the gradient below was. Averaged
	// over the texture's own pixels, weighted by their alpha so a mostly
	// transparent texture does not report the colour of the parts nobody
	// sees. Sampled on a bounded grid rather than read whole: this runs per
	// fill, and a texture is an image of any size.
	if (b.style() == Qt::TexturePattern) {
		const QImage img = b.textureImage().isNull()
		                       ? b.texture().toImage()
		                       : b.textureImage();
		if (img.isNull() || img.width() < 1 || img.height() < 1)
			return b.color();
		const int steps = 16;
		double r = 0, g2 = 0, bl = 0, wsum = 0;
		for (int iy = 0; iy < steps; ++iy)
			for (int ix = 0; ix < steps; ++ix) {
				const int px = ix * img.width() / steps;
				const int py = iy * img.height() / steps;
				const QRgb v = img.pixel(px, py);
				const double w = qAlpha(v) / 255.0;
				r += qRed(v) * w; g2 += qGreen(v) * w; bl += qBlue(v) * w;
				wsum += w;
			}
		if (wsum <= 0) return QColor(0, 0, 0, 0);   // wholly transparent
		return QColor(int(r / wsum), int(g2 / wsum), int(bl / wsum),
		              int(255.0 * wsum / (steps * steps)));
	}
	const QGradient *g = b.gradient();
	if (!g) return b.color();
	const QGradientStops stops = g->stops();
	if (stops.isEmpty()) return b.color();
	if (stops.size() == 1) return stops.first().second;
	double r = 0, gr = 0, bl = 0, al = 0, total = 0;
	for (int i = 0; i + 1 < stops.size(); ++i) {
		const double span = stops[i + 1].first - stops[i].first;
		if (span <= 0) continue;
		const QColor &c0 = stops[i].second, &c1 = stops[i + 1].second;
		r  += span * (c0.red()   + c1.red())   / 2.0;
		gr += span * (c0.green() + c1.green()) / 2.0;
		bl += span * (c0.blue()  + c1.blue())  / 2.0;
		al += span * (c0.alpha() + c1.alpha()) / 2.0;
		total += span;
	}
	if (total <= 0) return stops.first().second;
	return QColor(int(r / total), int(gr / total), int(bl / total),
	              int(al / total));
}

// A resolved ink laid over what a cell already holds.
//
// The same rule a translucent FILL follows: blended where the ground is a
// concrete colour, laid down unchanged where it is not, because this layer
// does not know the terminal's own background and guessing it would be
// worse than leaving the ink alone.
static Color ink_over(Color ink, int alpha, const Color &ground) {
	if (alpha >= 255 || ink.kind() != Color::Rgb
	 || ground.kind() != Color::Rgb) return ink;
	const QRgb s = ink.value(), d = ground.value();
	auto mix = [alpha](int sv, int dv) {
		return (sv * alpha + dv * (255 - alpha)) / 255;
	};
	return Color::rgb(qRgb(mix(qRed(s), qRed(d)), mix(qGreen(s), qGreen(d)),
	                       mix(qBlue(s), qBlue(d))));
}

// The pen's colour with the painter's opacity folded in, and a gradient pen
// resolved through the same averaging a gradient brush gets. Normalised to
// opaque rgb for the same reason a fill's colour is: a terminal has no alpha
// channel, and the byte only made equal colours compare unequal.
QColor CellPaintEngine::pen_ink() const {
	QColor c = brush_colour(pen_.brush());
	const int a = qBound(0, int(c.alpha() * opacity_ + 0.5), 255);
	c.setAlpha(a);
	return c;
}

// The palette-role rule for a fill, in one place. See the declaration in
// qtty/paint.h for why it is not written out twice.
CellPaintEngine::FillCell CellPaintEngine::brush_cell() const {
	const QRgb col = brush_colour(brush_).rgba();
	// role_of(), not a copy of its list. This carried the same six roles in
	// the same order and asked `pal.color(role)` -- the palette's CURRENT
	// group, Active for the application palette -- so a disabled widget's
	// fill, which Qt takes from the Disabled group, matched no role and fell
	// through as a hard 24-bit colour. That is the #bebebe incident
	// cell_geometry.h records, and the pen path here was moved to the shared
	// helper for it while the fill path was left behind: three places giving
	// three answers, fixed in two.
	const QPalette::ColorRole matched =
	    role_of(col, {QPalette::Window, QPalette::Base, QPalette::Button,
	                  QPalette::AlternateBase, QPalette::Highlight,
	                  QPalette::ToolTipBase});

	// Normalised to OPAQUE rgb, because a Color is a terminal colour and a
	// terminal has no alpha channel. Storing the byte anyway made two cells
	// of the same visible colour compare unequal whenever their brushes
	// differed in transparency -- which the frame diff reads as a change and
	// retransmits -- and it let a check comparing whole QRgb values pass
	// against a fill that had stopped blending, the difference being in the
	// one byte nothing draws. Found by the sabotage harness refusing to make
	// that check fail.
	Color bg = matched == QPalette::NoRole
	               ? Color::rgb(qRgb(qRed(col), qGreen(col), qBlue(col)))
	               : theme().background(matched);
	Attrs mark;
	if (bg.kind() == Color::Default) {
		if (is_surface_role(matched)) return FillCell{Cell{}, true};
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
	return FillCell{v, false};
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
	// Qt::transparent drew an opaque BLACK rule, because this read
	// pen_.color() and Color::rgb() kept bytes that are zero for it. An
	// invisible line has to draw nothing at all: it clears the run it
	// covers, so returning after that would leave a wiped row behind.
	const QColor pi = pen_ink();
	if (pi.alpha() == 0) return;
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
	// A rule is a line that stays in ONE cell row, and the second half of
	// that sentence had to be added when the walk below arrived. The dy test
	// alone is an ABSOLUTE one: a line 200 px across dropping 9 px satisfies
	// it and still straddles a row boundary, and this branch draws such a
	// line as one flat rule at the row of its FIRST point -- so a chart
	// segment sloping gently across the screen came out in the wrong row with
	// none of its slope, which is a wrong picture rather than a coarse one.
	//
	// Nothing that was working moves, and that is measured rather than
	// argued. Counted over the whole suite: 1597 calls reach here, 1481 take
	// the horizontal branch and 111 the vertical, and the number of them that
	// straddle a cell row or a cell column is ZERO -- every rule widget
	// chrome draws has a dy or a dx of exactly nought, so its two ends are in
	// the same cell by construction. The 5 that are left are the diagonals
	// the checks in suite_render add, and before the branch below existed
	// they drew nothing at all.
	const bool one_row = cell_of(m.y1(), ch) == cell_of(m.y2(), ch);
	const bool one_col = cell_of(m.x1(), cw) == cell_of(m.x2(), cw);
	if (qAbs(m.dy()) < ch / 2.0 && one_row) {
		const int y = cell_of(m.y1(), ch);
		auto span = covered(qMin(m.x1(), m.x2()), qMax(m.x1(), m.x2()), cw);
		if (clip) {
			if (y < clip->top() || y > clip->bottom()) return;
			span.first = qMax(span.first, clip->left());
			span.second = qMin(span.second, clip->right());
			if (span.first > span.second) return;
		}
		if (!clear_run(y, span.first, span.second, true)) return;
		const Color ink = line_for(qRgb(pi.red(), pi.green(), pi.blue()));
		for (int x = span.first; x <= span.second; ++x) {
			b.at(x, y).ch = QStringLiteral("─");
			b.at(x, y).fg = ink_over(ink, pi.alpha(), b.at(x, y).bg);
		}
	} else if (qAbs(m.dx()) < cw / 2.0 && one_col) {
		const int x = cell_of(m.x1(), cw);
		auto span = covered(qMin(m.y1(), m.y2()), qMax(m.y1(), m.y2()), ch);
		if (clip) {
			if (x < clip->left() || x > clip->right()) return;
			span.first = qMax(span.first, clip->top());
			span.second = qMin(span.second, clip->bottom());
			if (span.first > span.second) return;
		}
		if (!clear_run(x, span.first, span.second, false)) return;
		const Color ink = line_for(qRgb(pi.red(), pi.green(), pi.blue()));
		for (int y = span.first; y <= span.second; ++y) {
			b.at(x, y).ch = QStringLiteral("│");
			b.at(x, y).fg = ink_over(ink, pi.alpha(), b.at(x, y).bg);
		}
	} else {
		// Everything that is neither a row nor a column, which until this
		// existed fell off the end of the function and drew nothing. The two
		// branches above are left exactly as they were: their behaviour was
		// measured over the whole suite -- 510 horizontal rules landing on
		// clear cells, 8 on occupied ones, 426 partial, and 102 vertical --
		// and a diagonal is a case they never saw rather than one they got
		// wrong.
		stroke_segment(m.p1(), m.p2(), clip);
	}
}

// ---- Channel B geometry: diagonals, polylines and polygons -----------------
//
// What this replaced, measured on a custom paintEvent:
//
//     horizontal line   renders as a rule
//     vertical line     renders as a rule
//     diagonal line     NOTHING
//     polyline, curve   NOTHING
//     filled polygon    the box of its bounding rectangle
//
// line() had exactly two branches and a diagonal matched neither, so it fell
// off the end of the function; drawPath() and drawPolygon() both reduced to
// fill_rectf() of the bounding rectangle, which replaces a curve by the box
// around it and a FLAT curve -- whose bounding rectangle collapses to one row
// -- by nothing at all, because box() refuses a rectangle under two cells.
//
// THE GLYPH REPERTOIRE, and why it stops where it does.
//
// The cells go to a TERMINAL, so what draws them is the user's terminal font
// and qtty cannot measure it -- the grid is the terminal's and does not depend
// on the glyph, but whether anything appears in the cell does. Coverage is
// therefore the question, and the only proxy available is what a machine has
// installed. Counted here with `fc-list :charset=NNNN:spacing=100` over the 21
// monospace families on this one that are not DOS bitmap fonts (`fc-list`
// reports 102 and 81 of them are the `Px ` CP437 set, which every terminal
// glyph below is in by construction and which nobody sets a terminal to):
//
//     U+2500, U+2502   the light rules       10, 11 of 21
//     U+2588, U+2592   full and medium       10,  9 of 21 -- drawPixmap()
//                      shade                                already emits the
//                                                           second of these
//     U+2581..U+2587   the eighth blocks          8 of 21
//     U+2596..U+259F   the quadrants              8 of 21
//     U+2571, U+2572   the light diagonals        5 of 21 -- DejaVu Sans Mono,
//                                                           Hack, Agave, and
//                                                           two home-computer
//                                                           revival fonts
//     U+1FB00..        the sextants               3 of 21
//     U+2800..U+28FF   braille                    1 of 21 -- Agave
//
// READ THAT AS AN ORDERING AND NOT AS A RATE. The denominator is one machine's
// font directory and it is noisy at both ends: the 21 include an emoji font, a
// SignWriting font and OCR A, none of which anybody reads a terminal in. What
// survives the noise is the ORDER, which is the same however the population is
// drawn -- box drawing is the most widely carried, the diagonals are a tier
// below it, and braille is alone at the bottom.
//
// So braille is out, and it is the option worth arguing about because it is
// the highest-fidelity one by a distance: 2x4 dots per cell against one glyph
// per cell, and every terminal plotting library that can assume a font reaches
// for it. Two measurements decide it against, and the second is the one that
// settles it, because it does not depend on the population above at all.
//
// DejaVu Sans Mono is the family qtty NAMES -- application.cpp asks for it and
// grid_font_problem() makes a font that cannot carry the grid a hard startup
// error -- and it has no braille. Asked of Qt directly rather than assumed,
// with a control at each end:
//
//     QRawFont::fromFont("DejaVu Sans Mono", 16px).supportsCharacter()
//       'M'                yes    advance 10.0     control: must be present
//       U+2500 U+2571      yes    advance 10.0
//       U+2800 U+2847      NO     advance 12.0
//       U+E000             NO     advance 12.0     control: must be absent
//
// A braille pattern reports exactly what a private-use codepoint reports. In a
// terminal that is a missing glyph rather than a broken grid -- the terminal
// owns the columns, not the font -- but it means qtty's own snapshots, taken
// under the one font the library refuses to start without, would be recorded
// against a character that font cannot draw. A glyph nobody can render is
// worse than a coarser one that everybody can.
//
// The diagonals being a tier weaker than the rules already emitted is a real
// cost rather than a rounding error: Liberation Mono and Nimbus Mono PS carry
// U+2500, U+2502 and the four corners box() draws, and not U+2571 or U+2572.
// They are in the same Unicode block as the glyphs box() has always written,
// the font qtty names has them, and the alternative for a 45-degree line is a
// staircase of rules and columns that reads as several disconnected marks.
// Taken, and recorded here so the trade is visible rather than discovered.
//
// The eighth blocks were considered for the top edge of a filled area and not
// taken. They are no better covered than the quadrants, and the fill below
// carries a BACKGROUND colour instead -- which needs no glyph at all, cannot
// be missing from any font, and composes with whatever character is already in
// the cell rather than replacing it.

// A segment clipped to `r`, or false when none of it is inside. Liang-Barsky,
// which answers in the segment's own parameter and so cannot move an endpoint
// off the line.
//
// This is what BOUNDS the walk below, and it is not tidiness. fill_rectf()
// records the same finding from the other side: a layer wider than the
// terminal is the ordinary state of a window whose layout minimum exceeds the
// screen, so a segment running corner to corner across one can be arbitrarily
// long while nothing outside the buffer can be written. Clipped first, the
// number of cells a segment can visit is bounded by the buffer's own extent.
static bool clip_segment(QPointF &a, QPointF &b, const QRectF &r) {
	// Rejected outright rather than clipped. An infinity or a NaN makes every
	// comparison below false, so the clip would pass it through unchanged and
	// the walk would then convert it to an int, which is undefined. An
	// application can produce one -- a chart dividing by an empty data range
	// is the ordinary way -- and the honest answer to a coordinate that names
	// no point is to draw nothing.
	if (!std::isfinite(a.x()) || !std::isfinite(a.y())
	 || !std::isfinite(b.x()) || !std::isfinite(b.y())) return false;
	const double dx = b.x() - a.x(), dy = b.y() - a.y();
	const double p[4] = {-dx, dx, -dy, dy};
	const double q[4] = {a.x() - r.left(), r.right() - a.x(),
	                     a.y() - r.top(),  r.bottom() - a.y()};
	double t0 = 0.0, t1 = 1.0;
	for (int i = 0; i < 4; ++i) {
		if (p[i] == 0.0) {
			if (q[i] < 0.0) return false;           // parallel and outside
			continue;
		}
		const double t = q[i] / p[i];
		if (p[i] < 0.0) { if (t > t1) return false; if (t > t0) t0 = t; }
		else            { if (t < t0) return false; if (t < t1) t1 = t; }
	}
	const QPointF from = a;
	a = QPointF(from.x() + dx * t0, from.y() + dy * t0);
	b = QPointF(from.x() + dx * t1, from.y() + dy * t1);
	return true;
}

// The cells a segment passes through, in order, with how far it travels inside
// each. Amanatides and Woo's grid traversal: every step advances to whichever
// of the next column boundary or the next row boundary the segment reaches
// first, so it visits exactly the cells the segment enters -- no cell missed,
// and none invented. A sampled walk can promise neither at a shallow slope,
// where consecutive samples straddle a column, nor at a steep one, where they
// straddle a row.
//
// `visit` is called as (cell x, cell y, span across in px, span down in px,
// first, last).
//
// TERMINATION, named because this walks coordinates an application supplied.
// `t` is the segment's own parameter, every step raises it to the next
// boundary crossing, and the loop ends when it reaches 1 or when the cell
// holding the far endpoint is reached. That is the bound, and it is enough for
// any finite segment. `cap` is the second one: the caller clips to the buffer
// first, so the cells a legitimate segment can visit are bounded by the
// buffer's own extent and a run that exceeds the cap is a run that should not
// exist. Non-finite input cannot reach here -- clip_segment() refuses it --
// which is what makes the first bound trustworthy rather than merely stated.
template <class Visit>
static void walk_segment(const QPointF &a, const QPointF &b, int cw, int ch,
                         int cap, Visit &&visit) {
	const double dx = b.x() - a.x(), dy = b.y() - a.y();
	// floor(), for the reason line() records: the cell a pixel is IN, not the
	// boundary it is nearest. Rounding put a rule on the last pixel row of a
	// widget into the row below it.
	int cx = int(std::floor(a.x() / cw)), cy = int(std::floor(a.y() / ch));
	const int end_x = int(std::floor(b.x() / cw)), end_y = int(std::floor(b.y() / ch));
	const int step_x = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
	const int step_y = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
	const double never = std::numeric_limits<double>::infinity();
	// Infinity where the segment does not move in that axis at all, which is
	// what stops a horizontal segment ever taking a row step: min() never
	// picks it, and the increment below never runs.
	double at_x = never, at_y = never, per_x = never, per_y = never;
	if (step_x) {
		at_x = ((cx + (step_x > 0 ? 1 : 0)) * double(cw) - a.x()) / dx;
		per_x = double(cw) / std::fabs(dx);
	}
	if (step_y) {
		at_y = ((cy + (step_y > 0 ? 1 : 0)) * double(ch) - a.y()) / dy;
		per_y = double(ch) / std::fabs(dy);
	}
	double t = 0.0;
	for (int guard = 0; guard <= cap; ++guard) {
		const double out = std::min(1.0, std::min(at_x, at_y));
		const double in_x = a.x() + dx * t,   in_y = a.y() + dy * t;
		const double out_x = a.x() + dx * out, out_y = a.y() + dy * out;
		// Both conditions, not either. `out >= 1` is the parametric end and
		// `cx == end_x && cy == end_y` is the geometric one; floating point
		// makes each of them arrive first in different cases, and a segment
		// ending exactly on a cell boundary has an end cell the walk never
		// enters, which only the first can see.
		const bool last = out >= 1.0 || (cx == end_x && cy == end_y);
		visit(cx, cy, std::fabs(out_x - in_x), std::fabs(out_y - in_y),
		      guard == 0, last);
		if (last) return;
		t = out;
		if (at_x < at_y) { cx += step_x; at_x += per_x; }
		else             { cy += step_y; at_y += per_y; }
	}
}

// The glyph for a segment crossing one cell, chosen from how far it travels
// INSIDE that cell rather than from the line's overall slope. The difference
// shows at a shallow slope: a line dropping one row every three columns is a
// run of rules with a single diagonal at each step, and a per-line glyph would
// make it either a staircase of rules with a visible break at every step or a
// run of diagonals that are not diagonal.
//
// The comparison is against half a cell in each axis, which is the same
// half-cell test fill_rectf() applies across a thin rect and covered() applies
// along a rule's length -- stated as a ratio here because a cell is 10 x 19 px
// and comparing raw pixel spans would call every 45-degree line vertical.
static QString segment_glyph(double across, double down, double dx, double dy,
                             int cw, int ch) {
	const double fx = across / cw, fy = down / ch;
	if (fy * 2.0 < fx) return QStringLiteral("─");
	if (fx * 2.0 < fy) return QStringLiteral("│");
	// Screen coordinates: y grows downwards, so a segment going right and
	// down leans the way U+2572 does. A degenerate segment -- a polygon with
	// a repeated vertex, which is common in generated geometry -- has no
	// direction at all and gets the rule, which is what line() already draws
	// for a zero-length QLineF.
	if (dx == 0.0 && dy == 0.0) return QStringLiteral("─");
	return (dx >= 0) == (dy >= 0) ? QStringLiteral("╲") : QStringLiteral("╱");
}

void CellPaintEngine::stroke_segment(const QPointF &a, const QPointF &b,
                                     const std::optional<QRect> &clip) {
	// An invisible pen writes no glyph. Guarded before anything touches a
	// cell rather than at the write: this path sets `ch` as well as `fg`,
	// so a transparent stroke that got as far as the loop would leave a
	// visible mark whatever colour it was given.
	const QColor seg_pen = pen_ink();
	if (seg_pen.alpha() == 0) return;
	// Resolved ONCE per segment, not once per cell. Reading it inside the
	// loop below cost a QColor construction -- and for a gradient pen, an
	// average over its stops -- for every cell of every stroke, against a
	// 16 ms frame budget. Nothing in the loop can change the pen.
	const int seg_alpha = seg_pen.alpha();
	const Color seg_ink = line_for(qRgb(seg_pen.red(), seg_pen.green(),
	                                    seg_pen.blue()));
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	CellBuffer &buf = dev_->buffer();
	QPointF from = a, to = b;
	if (!clip_segment(from, to, QRectF(0, 0, buf.cols() * double(cw),
	                                   buf.rows() * double(ch)))) return;
	const double dx = b.x() - a.x(), dy = b.y() - a.y();
	// The clip has already bounded the walk to the buffer; this is the cap
	// walk_segment() cannot reach. Every step advances one column or one row,
	// so a segment inside the buffer visits at most cols + rows cells, and the
	// slack is for the boundary cases at each end.
	const int cap = buf.cols() + buf.rows() + 4;
	// `share` is how much of the cell the segment crosses, on whichever axis
	// it crosses more. Collected rather than decided as it goes, because the
	// trim below is a property of the RUN and cannot be judged while the run
	// is still being built.
	struct Visited { int x, y; double across, down, share; };
	QVarLengthArray<Visited, 256> run;
	walk_segment(from, to, cw, ch, cap,
	             [&](int x, int y, double across, double down, bool, bool) {
		run.append({x, y, across, down, std::max(across / cw, down / ch)});
	});
	if (run.isEmpty()) return;

	// The cells the segment COVERS, not the ones it touches -- covered()'s
	// rule, generalised from a rule's length to a walk in two axes. A cell the
	// segment enters by a pixel is not a cell the segment is in: measured on a
	// mnemonic, Qt underlines the marked letter with a line that starts a
	// pixel early, and without that rule every check box, radio button and
	// group box with a mnemonic drew a rule in the gap between its indicator
	// and its label.
	//
	// It is applied to EVERY cell here and not only to the two ends, which is
	// where this had to differ from covered(), and the case that forced it is
	// a grid traversal's oldest one. When a segment passes exactly through a
	// lattice corner -- which a corner-to-corner diagonal on a square cell
	// grid does at every single step -- the walk reaches the column boundary
	// and the row boundary at the same parameter, takes them one at a time,
	// and so visits an extra cell that the segment's interior never enters at
	// all. Measured before the rule was widened: a diagonal across a 10 x 10
	// buffer drew twenty cells rather than ten, a doubled trace one row below
	// where the line is, and it overwrote a column of text the line does not
	// touch. Those cells have zero span in both axes, so the same half-cell
	// question that catches the mnemonic catches them, asked of the middle of
	// the run rather than only its ends.
	//
	// No gap can open. Where a segment crosses a cell boundary inside a
	// column, the two cells' shares of that column sum to one, so at most one
	// of them can be under a half -- and where they are exactly equal both are
	// kept. What the trim removes is a duplicate, never the only candidate.
	//
	// And a segment wholly inside ONE cell keeps that cell whatever its share:
	// it is the only cell it can be in, and a polyline dense enough to be a
	// curve is made almost entirely of such segments, so trimming them would
	// delete the curve rather than tidy it. The `best` fallback is that rule
	// stated so it also covers a short segment straddling two cells, which
	// covered() answers by trimming the first end and then finding the second
	// is no longer a second.
	int kept = 0, best = 0;
	for (int i = 0; i < run.size(); ++i) {
		if (run[i].share >= 0.5) ++kept;
		if (run[i].share > run[best].share) best = i;
	}
	for (int i = 0; i < run.size(); ++i) {
		if (kept > 0 ? run[i].share < 0.5 : i != best) continue;
		const int x = run[i].x, y = run[i].y;
		if (clip && !clip->contains(x, y)) continue;
		if (x < 0 || y < 0 || x >= buf.cols() || y >= buf.rows()) continue;
		Cell &cell = buf.at(x, y);
		// Per cell, where a rule is all or nothing, and the difference is
		// deliberate. line()'s clear_run() refuses a rule that meets ANY
		// content, because a table's grid line crossing a row of text
		// otherwise filled the gaps between the words and that reads as
		// corruption. A curve is not a rule: it crosses whatever a chart has
		// already drawn -- an axis label, a legend -- and dropping the whole
		// segment because one cell holds a character loses the curve rather
		// than tidying it. Skipping that one cell reads as the curve passing
		// behind the label, which is what it is doing.
		if (cell.ch != QStringLiteral(" ")) continue;
		cell.ch = segment_glyph(run[i].across, run[i].down, dx, dy, cw, ch);
		// The pen, by line_for()'s rule: an application's own colour is
		// carried and Qt's frame greys are not. A curve is the case that
		// makes this matter -- a chart draws several series and they are
		// told apart by colour, so a plot rendered in one ink is a plot with
		// its legend removed.
		cell.fg = ink_over(seg_ink, seg_alpha, cell.bg);
	}
}

void CellPaintEngine::stroke_polyline(const QPolygonF &pts, bool close,
                                      const std::optional<QRect> &clip) {
	for (int i = 0; i + 1 < pts.size(); ++i)
		stroke_segment(pts[i], pts[i + 1], clip);
	if (close && pts.size() > 2 && pts.first() != pts.last())
		stroke_segment(pts.last(), pts.first(), clip);
}

// The cells a polygon covers, filled with the brush.
//
// Scanline against each cell row's CENTRE, so a cell is filled when the
// polygon covers its middle. That is the same question to_cells() answers for
// a rectangle by rounding each edge to the nearest cell, asked in a form that
// works for a shape with no edges to round -- and it is why the two agree on a
// rectangle, which matters because drawPath() has always sent QTextLayout's
// selection rectangles down the fill road.
//
// The alternative this replaced was filling the BOUNDING RECTANGLE, and the
// case against it is not that it is coarse. A triangle's bounding rectangle is
// twice its area, so half the cells it colours are cells nothing was drawn in
// -- it invents content rather than losing it, and a reader cannot tell which
// half is which. Drawing nothing at all would at least be honest. This is
// neither: the polygon's own cells, and no others.
void CellPaintEngine::fill_polygon(const QPolygonF &pts, bool winding,
                                   const std::optional<QRect> &clip) {
	if (pts.size() < 3) return;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	CellBuffer &buf = dev_->buffer();
	const QRectF box_px = pts.boundingRect();
	if (!std::isfinite(box_px.left()) || !std::isfinite(box_px.top())
	 || !std::isfinite(box_px.right()) || !std::isfinite(box_px.bottom())) return;

	QRect cells(int(std::floor(box_px.left() / cw)),
	            int(std::floor(box_px.top() / ch)),
	            0, 0);
	cells.setRight(int(std::floor(box_px.right() / cw)));
	cells.setBottom(int(std::floor(box_px.bottom() / ch)));
	QRect bounded = cells & QRect(0, 0, buf.cols(), buf.rows());
	if (clip) bounded &= *clip;
	if (bounded.isEmpty()) return;

	const FillCell f = brush_cell();
	// The same test fill_rectf() applies, asked of the polygon's own cell
	// extent rather than of a scanline's: a surface role the theme has left at
	// the terminal's own background erases, and a shape one cell wide or one
	// cell tall is a rule or a caret rather than a surface and must not.
	const bool erasing = f.erase;
	if (erasing && (cells.width() <= 1 || cells.height() <= 1)) return;
	const Cell written = erasing ? Cell{} : f.cell;

	// Hoisted out of the row loop and cleared per row: a chart's area polygon
	// has one vertex per sample and this runs once per cell row, so a vector
	// allocated inside would be allocated once per row of every fill.
	QVector<double> crossings;
	QVector<int> directions, order;
	for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
		const double sample = (double(y) + 0.5) * ch;
		crossings.clear();
		directions.clear();
		for (int i = 0; i < pts.size(); ++i) {
			const QPointF &p1 = pts[i], &p2 = pts[(i + 1) % pts.size()];
			// Half-open in y -- [min, max) -- which is what stops a vertex
			// landing exactly on the sample line being counted twice and
			// turning the parity inside out for the rest of the row. A
			// horizontal edge contributes nothing, which is correct: it
			// crosses the sample line nowhere.
			if ((p1.y() <= sample) == (p2.y() <= sample)) continue;
			const double t = (sample - p1.y()) / (p2.y() - p1.y());
			crossings.append(p1.x() + t * (p2.x() - p1.x()));
			directions.append(p2.y() > p1.y() ? 1 : -1);
		}
		if (crossings.isEmpty()) continue;
		// Sorted together, so a winding count stays paired with its crossing.
		// An index sort rather than a struct, so the three vectors can be
		// reused across rows.
		order.resize(crossings.size());
		for (int i = 0; i < order.size(); ++i) order[i] = i;
		std::sort(order.begin(), order.end(),
		          [&](int l, int r) { return crossings[l] < crossings[r]; });

		int wind = 0;
		for (int i = 0; i + 1 < order.size(); ++i) {
			wind += winding ? directions[order[i]] : 1;
			const bool inside = winding ? (wind != 0) : (wind % 2 != 0);
			if (!inside) continue;
			const double lo = crossings[order[i]], hi = crossings[order[i + 1]];
			// A cell is in the span when its CENTRE is, which is the same
			// question the row sample above asks, asked along the other axis.
			const int first = int(std::ceil(lo / cw - 0.5));
			const int last = int(std::floor(hi / cw - 0.5));
			for (int x = qMax(first, bounded.left());
			     x <= qMin(last, bounded.right()); ++x)
				// writable(), which is what CellBuffer::fill() asks and so
				// what fill_rectf() has always honoured. It carries the
				// buffer's OWN clip as well as its bounds, and Channel A sets
				// that one around a widget it is drawing (CellClip) -- so a
				// fill reached from inside a style path stops where the widget
				// does. box() and line() write through at() and do not; that
				// is theirs to answer, and copying it here would be a second
				// wrong answer rather than consistency.
				if (buf.writable(x, y)) buf.at(x, y) = written;
		}
	}
}

} // namespace Qtty
