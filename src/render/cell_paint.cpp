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
	return true;
}
bool CellPaintEngine::end() { dev_ = nullptr; return true; }

void CellPaintEngine::updateState(const QPaintEngineState &s) {
	if (s.state() & DirtyPen)       pen_ = s.pen();
	if (s.state() & DirtyBrush)     brush_ = s.brush();
	if (s.state() & DirtyFont)      font_ = s.font();
	if (s.state() & DirtyTransform) xf_ = s.transform();
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
static Color pen_to_fg(const QPen &pen) {
	const QRgb c = pen.color().rgba();
	const QPalette &pal = QGuiApplication::palette();
	for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text,
		                          QPalette::ButtonText, QPalette::HighlightedText})
		if (pal.color(r).rgba() == c) return theme().foreground(r);
	return Color::rgb(c);
}

void CellPaintEngine::drawTextItem(const QPointF &p, const QTextItem &ti) {
	QPointF q = xf_.map(p) + QPointF(dev_->origin);
	QFontMetricsF fm(ti.font());
	int col = qRound(q.x() / GridMetrics::cw());
	int row = qRound((q.y() - fm.ascent()) / GridMetrics::ch());
	Attrs a;
	if (ti.font().bold()) a |= Attr::Bold;
	if (ti.font().italic()) a |= Attr::Italic;
	if (ti.font().underline()) a |= Attr::Underline;
	dev_->buffer().text(col, row, ti.text(), pen_to_fg(pen_), Color(), a);
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
	if (c.width() >= 2 && c.height() >= 2)      // section 5.7: real image -> placement
		dev_->placements.append({quint64(pm.cacheKey()), c, pm});
	else                                        // tiny icon -> glyph substitution (section 8.6)
		dev_->buffer().text(c.left(), c.top(), QStringLiteral("▒"));
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
	if (outline_only || brush_.style() == Qt::NoBrush) { box(c); return; }

	const QRgb col = brush_.color().rgba();
	const QPalette &pal = QGuiApplication::palette();
	QPalette::ColorRole matched = QPalette::NoRole;
	for (QPalette::ColorRole role : {QPalette::Window, QPalette::Base,
		                             QPalette::Button, QPalette::AlternateBase,
		                             QPalette::Highlight, QPalette::ToolTipBase})
		if (pal.color(role).rgba() == col) { matched = role; break; }

	Color bg = matched == QPalette::NoRole ? Color::rgb(col)
	                                       : theme().background(matched);
	if (bg.kind() == Color::Default) {
		if (is_surface_role(matched)) {
			if (!thin && c.width() > 1 && c.height() > 1) dev_->buffer().fill(c, Cell{});
			return;
		}
		bg = Color::rgb(col);                       // unthemed selection etc.
	}
	Cell v; v.bg = bg;
	if (!thin) { dev_->buffer().fill(c, v); return; }
	for (int y = c.top(); y <= c.bottom(); ++y)
		for (int x = c.left(); x <= c.right(); ++x) {
			Cell &cell = dev_->buffer().at(x, y);
			if (cell.ch == QStringLiteral(" ")) cell.bg = bg;
		}
}

void CellPaintEngine::box(const QRect &c) {
	if (c.width() < 2 || c.height() < 2) return;
	CellBuffer &b = dev_->buffer();
	for (int x = c.left() + 1; x < c.right(); ++x) {
		b.at(x, c.top()).ch = QStringLiteral("─");
		b.at(x, c.bottom()).ch = QStringLiteral("─");
	}
	for (int y = c.top() + 1; y < c.bottom(); ++y) {
		b.at(c.left(), y).ch = QStringLiteral("│");
		b.at(c.right(), y).ch = QStringLiteral("│");
	}
	b.at(c.left(), c.top()).ch = QStringLiteral("┌");
	b.at(c.right(), c.top()).ch = QStringLiteral("┐");
	b.at(c.left(), c.bottom()).ch = QStringLiteral("└");
	b.at(c.right(), c.bottom()).ch = QStringLiteral("┘");
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
	if (qAbs(m.dy()) < ch / 2.0) {
		const int y = cell_of(m.y1(), ch);
		const auto span = covered(qMin(m.x1(), m.x2()), qMax(m.x1(), m.x2()), cw);
		for (int x = span.first; x <= span.second; ++x)
			if (b.at(x, y).ch == QStringLiteral(" ")) b.at(x, y).ch = QStringLiteral("─");
	} else if (qAbs(m.dx()) < cw / 2.0) {
		const int x = cell_of(m.x1(), cw);
		const auto span = covered(qMin(m.y1(), m.y2()), qMax(m.y1(), m.y2()), ch);
		for (int y = span.first; y <= span.second; ++y)
			if (b.at(x, y).ch == QStringLiteral(" ")) b.at(x, y).ch = QStringLiteral("│");
	}
}

} // namespace Qtty
