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

// Hide the widgets the application marked Priority::Optional when the window's
// layout minimum does not fit the terminal, and show them again when it does.
//
// Re-evaluated from scratch on every frame rather than latched, so a terminal
// that grows brings the content back with no separate path to get wrong.
void Compositor::apply_priority(int cols, int rows) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QSize term(cols * cw, rows * ch);
	const auto fits = [&] {
		const QSize m = win_->minimumSizeHint();
		return m.width() <= term.width() && m.height() <= term.height();
	};

	// Put back first, then measure: a screen that fits ONLY because something
	// is hidden must not stay hidden for ever. Showing what this pass hid and
	// re-asking is the whole of the hysteresis, and it is why `dropped_` holds
	// what we hid rather than what is hidden.
	bool restored = false;
	for (const QPointer<QWidget> &w : std::as_const(dropped_)) {
		if (w) { w->show(); restored = true; }
	}
	dropped_.clear();
	if (restored && win_->layout()) win_->layout()->activate();
	if (fits()) return;

	// The focused widget is never dropped, nor is any ancestor of it. Hiding
	// the widget that owns input moves focus somewhere the application did not
	// choose, and on a terminal there is no pointer to put it back with.
	const QWidget *const focus = win_->focusWidget();
	const auto owns_focus = [&](const QWidget *w) {
		for (const QWidget *f = focus; f; f = f->parentWidget())
			if (f == w) return true;
		return false;
	};

	const auto all = win_->findChildren<QWidget *>();
	for (QWidget *w : all) {
		if (priority_of(w) != Priority::Optional) continue;
		if (!w->isVisible() || owns_focus(w)) continue;
		w->hide();
		dropped_.append(w);
		if (win_->layout()) win_->layout()->activate();
		if (fits()) break;                 // enough is enough: stop dropping
	}
}

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
	// design.md section 7's policy in the order it names: drop what the
	// application said is optional FIRST, and only then scroll what is left.
	// Dropping can make a screen fit; scrolling never does, it only makes the
	// rest reachable.
	apply_priority(out.cols(), out.rows());

	// Follow the focus rather than binding a key. Arrow keys belong to the
	// focused widget and a chord would have to be learned, but Tab already
	// walks the form -- so keeping the focused widget inside the terminal
	// makes every widget reachable with the keys the application already
	// answers. The offset is clamped to what actually overflows, so a window
	// that fits scrolls by nothing and this is invisible.
	const int max_x = qMax(0, (win_->width() + cw - 1) / cw - out.cols());
	const int max_y = qMax(0, (win_->height() + ch - 1) / ch - out.rows());
	if (QWidget *fw = win_->focusWidget()) {
		const QPoint at = fw->mapTo(win_, QPoint());
		const int left = at.x() / cw, top = at.y() / ch;
		const int right = (at.x() + fw->width() - 1) / cw;
		const int bottom = (at.y() + fw->height() - 1) / ch;
		if (left < scroll_.x())                     scroll_.setX(left);
		else if (right > scroll_.x() + out.cols() - 1) scroll_.setX(right - out.cols() + 1);
		if (top < scroll_.y())                      scroll_.setY(top);
		else if (bottom > scroll_.y() + out.rows() - 1) scroll_.setY(bottom - out.rows() + 1);
	}
	scroll_.setX(qBound(0, scroll_.x(), max_x));
	scroll_.setY(qBound(0, scroll_.y(), max_y));

	const QPoint root_at(-scroll_.x() * cw, -scroll_.y() * ch);
	draw(win_, root_at);
	QWidget *cursor_layer = win_;
	QPoint cursor_origin = root_at;

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
	// design.md 5.5 adopts a generic trick -- ask the focus widget for
	// ImCursorRectangle rather than special-casing input classes -- on the
	// premise that "any widget that supports input methods reports its caret
	// this way". Measured 2026-09-01, and the premise is false in the
	// direction that matters: EVERY QWidget answers ImCursorRectangle, with
	// QWidget's own default of a one-pixel rectangle at its horizontal
	// centre. A check box, a radio button, a slider, a list, a tab bar and a
	// scroll bar all returned 1x19+120+0 in a 24-cell form, and each of them
	// got the terminal's hardware cursor parked in the middle of its label.
	//
	// A caret does not mean "this is focused", it means "type here" -- and a
	// screen reader says so out loud, which is the reason design.md gives for
	// placing it accurately in the first place.
	//
	// WA_InputMethodEnabled is the test that separates them, and it was
	// measured rather than assumed: of the nine widgets, exactly the line
	// edit and the spin box carry it, which is exactly the two that edit
	// text. Qt clears it on a read-only line edit, so one of those loses its
	// caret too, which is right.
	QWidget *fw = cursor_layer->focusWidget();
	if (fw && !fw->testAttribute(Qt::WA_InputMethodEnabled)) fw = nullptr;
	if (fw) {
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
			// The HORIZONTAL CENTRE, not the left edge. What Qt returns is
			// the caret's repaint rectangle rather than the caret: a
			// QLineEdit inflates it five pixels either side so a redraw
			// covers the glyph beside it, and taking topLeft() took the
			// inflation as the position. Measured on a 10-pixel cell, with
			// the caret walked along "abcdef" in a bracketed field whose
			// text begins at column 1:
			//
			//   caret at 0   rect 10x20+7+1    left -> col 0   centre -> 1
			//   caret at 1   rect 10x20+17+1   left -> col 1   centre -> 2
			//   caret at 6   rect 10x20+67+1   left -> col 6   centre -> 7
			//
			// The left reading was one cell out every time, sitting ON the
			// character before the caret instead of where typing goes -- and
			// on a one-cell field it sat on the bracket. A QSpinBox showed
			// the same fault from the other end: its editor answers
			// 10x20+-3+0, a rectangle starting outside the widget, whose
			// centre is the caret at +2.
			//
			// Vertically the top is right: the rectangle is the line's full
			// height and its top is the caret's top.
			const QRect cr = v.toRect();
			QPoint g = cursor_origin
			           + owner->mapTo(cursor_layer,
			                          QPoint(cr.x() + cr.width() / 2, cr.y()));
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
	// A terminal with no cells has no frame to be given. Without this the
	// buffer is empty, rasterize() answers a null QImage, and the software
	// composite path below opens a QPainter on it -- which fails and then
	// warns on every call, into the stderr that is the terminal. The ANSI
	// backend refuses a degenerate size at both of its own doors now; this
	// says the same thing for every backend, including one an application
	// injects through exec().
	if (cells.isEmpty()) return;
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
	// By CONTENT, not by count. This compared frame.images.size() against
	// the previous frame's, which cannot express the case that matters: a
	// picture repainted in place keeps its cell geometry, so the cells under
	// it diff to nothing and the placement count is unchanged -- and the new
	// image was never presented at all. A PixelSurface showing a plot, a
	// meter or a video still simply froze, while both halves of the seam
	// stayed innocent, the compositor having built the right frame and
	// present() being correct about writing one it was never handed.
	const bool images_changed = !prev_ || frame.images != prev_->images;

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
