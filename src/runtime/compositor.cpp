// src/runtime/compositor.cpp -- L6 composition (section 5.4 step 3, section 8.1) and
// FrameScheduler (section 5.4 steps 1-4, simplified per section 16.1 F9: full render + diff
// beats damage tracking at these sizes).
#include "qtty/runtime.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/application.h"
#include "qtty/graphics.h"
#include "qtty/overlay.h"
#include <QtWidgets>

namespace Qtty {

// ----------------------------------------------------------------- Compositor
namespace {

// Whether a top-level can be composited at all. Anything the terminal cannot
// show is skipped rather than rendered into nowhere: a widget that is not
// visible, one of the platform's own bookkeeping windows, or a window with no
// size yet (section 5.4 step 3).
bool is_compositable(const QWidget *w) {
	return w && w->isVisible() && w->windowType() != Qt::Desktop && !w->size().isEmpty();
}

// Where a layer goes inside the terminal rectangle (section 8.1).
//
// `flip` is for anchored layers -- menus, combo drop-downs, tooltips -- whose
// top-left IS the point they were opened at. section 8.1: "a menu opening at
// x=78 must flip left, which the desktop code never had to do". Flipping puts
// the far edge on the anchor, so the menu stays attached to the item it was
// opened from; sliding it along the edge, which qBound alone does, detaches it
// and can cover that item. Sliding stays as the fallback for a layer too big
// to fit on either side of its anchor, where there is nothing better to do.
QPoint placed_at(const QRect &g, int cols, int rows, int cw, int ch, bool flip) {
	const int limit_x = cols * cw, limit_y = rows * ch;
	int x = g.x(), y = g.y();
	if (flip) {
		if (x + g.width() > limit_x && x - g.width() >= 0) x -= g.width();
		if (y + g.height() > limit_y && y - g.height() >= 0) y -= g.height();
	}
	x = qBound(0, x, qMax(0, limit_x - g.width()));
	y = qBound(0, y, qMax(0, limit_y - g.height()));
	return QPoint((x / cw) * cw, (y / ch) * ch);          // snap the result to the grid
}

} // namespace

Compositor::Compositor(QWidget *window, InputRouter *router)
    : win_(window), router_(router) {}

void Compositor::compose(CellBuffer &out) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	out.images.clear();

	CellPaintDevice dev(out);
	auto draw = [&](QWidget *w, QPoint at) {
		dev.origin = at;
		QPainter p(&dev);
		w->render(&p, QPoint(), QRegion(),
		          QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
		p.end();
	};
	// Move a layer inside the terminal rectangle and report where it landed.
	auto place = [&](QWidget *w, bool flip) {
		const QPoint pos = placed_at(w->geometry(), out.cols(), out.rows(), cw, ch, flip);
		if (pos != w->geometry().topLeft()) w->move(pos);
		return pos;
	};

	// section 5.4 step 3: walk QApplication::topLevelWidgets(), rather than
	// rendering the one window we were handed. The primary window is the base
	// layer and is drawn at the origin; every other plain top-level follows at
	// its own position, kept inside the terminal rectangle.
	//
	// There is no window manager here and so no true z-order to read, which is
	// why modals and popups are pulled out of this pass and stacked explicitly
	// below (section 8.1: treat them as an explicit stack "rather than trusting
	// window flags"). Within the plain layer the list order is all there is.
	draw(win_, QPoint());
	QWidget *cursor_layer = win_;
	QPoint cursor_origin;

	QVector<QWidget *> modals;
	const auto tops = QApplication::topLevelWidgets();
	for (QWidget *w : tops) {
		if (w == win_ || !is_compositable(w)) continue;
		if (InputRouter::is_popup_layer(w)) continue;     // the popup stack draws these
		if (w->isModal()) { modals.append(w); continue; } // the modal stack does
		draw(w, place(w, false));
	}

	// section 8.1's mitigation: activeModalWidget() and activePopupWidget() as
	// an explicit z-ordered stack on top. Without it a modal QDialog was stamped
	// WA_DontShowOnScreen by the router and then drawn by nobody -- invisible
	// while still holding input, which is the worst of both.
	QWidget *const active_modal = QApplication::activeModalWidget();
	if (active_modal && modals.removeAll(active_modal))
		modals.append(active_modal);                      // the active one is topmost
	for (QWidget *w : std::as_const(modals)) {
		const QPoint at = place(w, false);                // a dialog is not anchored
		draw(w, at);
		if (w == active_modal) { cursor_layer = w; cursor_origin = at; }
	}

	// popups last: always the top of the stack, and flipped rather than slid.
	const auto popups = router_ ? router_->popups() : QVector<QWidget *>{};
	for (QWidget *pop : popups) {
		if (!is_compositable(pop)) continue;
		draw(pop, place(pop, true));
	}

	dev.origin = QPoint();
	out.images = dev.placements;

	// Hardware cursor from the focus widget of the layer that owns input
	// (section 5.5). A modal owns input while it is up (section 8.3), so it owns
	// the cursor too; a popup has no text cursor of its own.
	cursor_.reset();
	if (QWidget *fw = cursor_layer->focusWidget()) {
		QVariant v = fw->inputMethodQuery(Qt::ImCursorRectangle);
		// A widget that delegates editing to an internal editor forwards this
		// query to it VERBATIM, so the rect comes back in the editor's own
		// coordinates rather than the focus widget's. Measured on QSpinBox:
		// the spin box and its inner QLineEdit both answer 10x20+17+0 while
		// the edit sits at +10, so mapping from the spin box put the terminal
		// cursor a cell to the left of the caret, on the frame instead of in
		// the field. The widget that owns the rect is the one that returned
		// it, so find it and map from there.
		QWidget *owner = fw;
		if (v.isValid())
			for (QWidget *child : fw->findChildren<QWidget *>())
				if (child->isVisible()
				    && child->inputMethodQuery(Qt::ImCursorRectangle) == v) {
					owner = child;
					break;
				}
		if (v.isValid()) {
			QPoint g = cursor_origin + owner->mapTo(cursor_layer, v.toRect().topLeft());
			QPoint cell(g.x() / cw, g.y() / ch);
			if (cell.x() >= 0 && cell.y() >= 0
			    && cell.x() < out.cols() && cell.y() < out.rows())
				cursor_ = cell;
		}
	}
}

std::optional<QPoint> Compositor::cursor_cell() const { return cursor_; }

// ------------------------------------------------------------- FrameScheduler
FrameScheduler::FrameScheduler(ITerminalBackend *backend, Compositor *compositor,
                               QWidget *window)
    : backend_(backend), comp_(compositor), win_(window) {
	coalesce_.setSingleShot(true);
	coalesce_.setInterval(0);
	QObject::connect(&coalesce_, &QTimer::timeout, this, [this] { render_now(); });
	idle_.setInterval(100);                       // catches timer-driven updates
	QObject::connect(&idle_, &QTimer::timeout, this, [this] {
		if (win_->isVisible()) request_frame();
	});
	idle_.start();
	since_last_.start();
	qApp->installEventFilter(this);
}

bool FrameScheduler::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::UpdateRequest || e->type() == QEvent::LayoutRequest)
		if (qobject_cast<QWidget *>(o)) request_frame();
	return false;
}

void FrameScheduler::request_frame() {
	// 16 ms local budget (section 11); coalesce bursts into one frame.
	const int wait = qMax(0, 16 - int(since_last_.elapsed()));
	if (!coalesce_.isActive()) coalesce_.start(wait);
}

void FrameScheduler::render_now() {
	const QSize cells = backend_->size();
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	CellBuffer frame(cells.width(), cells.height());
	comp_->compose(frame);

	// Overlay tier selection (sections 5.7, 17.3).
	const auto overlays = Overlay::visible_overlays();
	const auto gmode = backend_->capabilities().graphics;
	auto *gfx = dynamic_cast<IGraphicsOutput *>(backend_);

	auto overlay_cell_rect = [&](Overlay *o) {
		return o->cell_rect().isNull()
		    ? QRect(0, 0, frame.cols(), frame.rows())
		    : QRect(int(o->cell_rect().x()), int(o->cell_rect().y()),
		            int(o->cell_rect().width()), int(o->cell_rect().height()));
	};

	const bool software_composite = !overlays.isEmpty() && gfx
	    && (gmode == Capabilities::Sixel || gmode == Capabilities::ITerm2
	        || gmode == Capabilities::Kitty);

	if (!overlays.isEmpty() && !software_composite && gmode != Capabilities::KittyAlpha)
		for (Overlay *o : overlays)                       // fallback: half-blocks
			compose_halfblocks(frame, o->image(), overlay_cell_rect(o));

	QRegion damage = prev_ ? frame.diff(*prev_)
	                       : QRegion(0, 0, frame.cols(), frame.rows());
	const bool images_changed = !prev_ || frame.images.size() != prev_->images.size();

	if (software_composite) {
		// one finished picture: rasterise cells, blend placements + overlays
		QImage px = rasterize(frame, QGuiApplication::font());
		QPainter p(&px);
		for (const CellImage &ci : frame.images)
			p.drawPixmap(ci.cell_rect.x() * cw, ci.cell_rect.y() * ch, ci.pixmap);
		for (Overlay *o : overlays) {
			const QRect r = overlay_cell_rect(o);
			p.drawImage(QRect(r.x() * cw, r.y() * ch, r.width() * cw, r.height() * ch),
			            o->image());
		}
		p.end();
		gfx->present_pixels(px, QRegion(0, 0, frame.cols(), frame.rows()));
	} else if (!damage.isEmpty() || images_changed || !prev_ || !overlays.isEmpty()) {
		backend_->present(frame, damage);
		if (gmode == Capabilities::KittyAlpha && gfx) {   // terminal-blended alpha
			int id = 0;
			for (Overlay *o : overlays)
				gfx->present_overlay(id++, o->image(),
				                    overlay_cell_rect(o).topLeft(),
				                    qMax(1, o->z()));
		}
	}
	backend_->set_cursor(comp_->cursor_cell(),
	                    comp_->cursor_cell() ? CursorShape::Bar : CursorShape::Hidden);
	prev_ = std::make_unique<CellBuffer>(frame);
	since_last_.restart();
}

} // namespace Qtty
