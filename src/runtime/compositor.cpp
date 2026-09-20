// src/runtime/compositor.cpp -- L6 composition (section 5.4 step 3, section 8.1) and
// FrameScheduler (section 5.4 steps 1-4, simplified per section 16.1 F9: full render + diff
// beats damage tracking at these sizes).
#include "qtty/runtime.h"
#include "qtty/windows.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/application.h"
#include "qtty/graphics.h"
#include "qtty/overlay.h"
#include "title_keeper.h"
#include "placement_paint.h"
#include "../cell_geometry.h"
#include <QtWidgets>
#include <algorithm>
#include <cstdlib>

namespace Qtty {

// design.md section 5.7's stacking, which the struct had lost. Painter's
// algorithm, so the LAST one drawn is the one on top -- which makes the paint
// order the stacking order, and z is what overrides it.
//
// Sorted by INDEX rather than by copying the placements into a sorted vector.
// A CellImage carries a QPixmap, so a sorted copy is a pile of implicitly
// shared handles built and destroyed once per frame to decide a paint order,
// and the indices answer the same question in an int apiece.
//
// std::stable_sort, not std::sort, and the distinction is the whole of what
// the default z means. Equal z has to keep the order the painter produced,
// because that order is the only statement anybody has made about two
// placements that both say nothing about stacking -- and today EVERY
// placement in this tree is one of those, nothing yet having a reason to set
// z. A plain sort would therefore leave the overwhelmingly common case, a
// whole frame at z 0, painting in an order the standard declines to specify:
// two overlapping pictures could stack one way under one libstdc++ and the
// other way under the next, from a buffer that said exactly the same thing
// both times, and no check here could catch it because there is nothing to
// compare against.
//
// Overlay::visible_overlays() uses a plain std::sort over overlays, so
// equal-z OVERLAYS already have that gap. It is not copied here on purpose.
// Which one is right is not this function's to decide -- an overlay id is
// that list's index and the terminal holds it between frames, so changing
// the sort there is a change to what gets cleared, and it wants its own
// measurement rather than being swept along with this.
void paint_placements(QPainter &p, const QVector<CellImage> &images,
                      int cw, int ch) {
	QVector<int> order(images.size());
	for (int i = 0; i < order.size(); ++i) order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		return images[a].z < images[b].z;
	});
	for (int i : order) {
		const CellImage &ci = images[i];
		p.drawPixmap(ci.cell_rect.x() * cw, ci.cell_rect.y() * ch, ci.pixmap);
	}
}

// ----------------------------------------------------------------- Compositor
namespace {

// Whether a top-level can be composited at all. Anything the terminal cannot
// show is skipped rather than rendered into nowhere: a widget that is not
// visible, one of the platform's own bookkeeping windows, or a window with no
// size yet (section 5.4 step 3).
bool is_compositable(const QWidget *w) {
	return w && w->isVisible() && w->windowType() != Qt::Desktop && !w->size().isEmpty();
}

// Whether a top-level belongs in the window strip, asked in ONE place
// because two loops need it: the sweep over Qt's list, which decides
// membership, and the walk over the remembered order, which decides
// position. Written out twice it could not be broken by a single sabotage
// -- either copy alone keeps a window out, so an entry removing one left
// every check green and reported that the code was broken and nothing
// noticed.
//
// A POPUP is not a window to switch to, nor is a modal -- the modal is the
// whole input tree while it is up (section 8.3). Nor is a window that
// cannot take input at all: qtty's own overlay twins are frameless,
// always-on-top Qt::Tool windows carrying Qt::WindowTransparentForInput,
// and offering one as somewhere to go would put a person in a window where
// nothing they pressed could do anything. Measured when they briefly became
// strip members: every graphics fixture grew a strip row, which moved the
// damage under three checks that have nothing to do with windows.
bool strip_member(const QWidget *w) {
	if (!is_compositable(w)) return false;
	if (InputRouter::is_popup_layer(w)) return false;
	if (w->windowFlags().testFlag(Qt::WindowTransparentForInput)) return false;
	return !w->isModal();
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

// Where a dialog goes when nobody has said. Centred in the terminal and
// snapped to the grid, never negative -- a dialog bigger than the terminal
// starts at the origin and section 7's scrolling takes over from there.
//
// This exists because Qt's answer is computed against a screen that does not
// exist. QDialog places an unparented dialog by centring it on the primary
// screen, and the offscreen plugin reports 800x800 whatever the terminal is,
// so `QMessageBox::information(nullptr, ...)` -- as ordinary an idiom as
// there is -- came out jammed into the bottom-right corner. Measured: at
// 80x24 Qt asked for +326+325 and the clamp gave +320+304; at 40x12 it asked
// for the same and the clamp gave +192+112, which is flush against both far
// edges. A dialog WITH a parent is already right, Qt centring it over the
// parent, and that parent is inside the terminal.
QPoint centred_in(const QSize &size, int cols, int rows, int cw, int ch) {
	const int x = (cols * cw - size.width()) / 2;
	const int y = (rows * ch - size.height()) / 2;
	return QPoint(qMax(0, (x / cw) * cw), qMax(0, (y / ch) * ch));
}

// The rectangle a layer has to keep inside the terminal, in the layer's own
// coordinates, or nothing if it is not pointing at anything.
//
// A QMenu never calls setFocus() on anything: it tracks its current item in
// QMenu::activeAction() and draws the highlight itself, so layer->focusWidget()
// is null for every menu ever opened. Following the focus alone therefore left
// a menu taller than the terminal frozen on its first screenful while Down
// walked invisibly past the fold -- measured with a 30-item menu 608 px tall in
// a 190 px terminal: after twenty Down presses the active item was "Item 19"
// and the frame was byte-identical to the one before. Qt does not intervene,
// because the offscreen QScreen is 800x800 and nothing tells it what the
// terminal's size is.
std::optional<QRect> follow_rect(QWidget *layer) {
	if (auto *menu = qobject_cast<QMenu *>(layer)) {
		if (QAction *a = menu->activeAction()) {
			const QRect r = menu->actionGeometry(a);
			if (!r.isNull()) return r;
		}
		return std::nullopt;
	}
	if (QWidget *fw = layer->focusWidget()) {
		// A TEXT widget is followed by its CARET, not by its rectangle.
		// Measured: a line edit sixty cells wide in a twenty-cell view
		// scrolls to the widget's RIGHT edge under the rectangle rule --
		// the "else if right > ..." below can do nothing else when the
		// widget is wider than the view -- which puts the caret, at the
		// left, outside it. The compositor then places no cursor at all,
		// measured as present at (1,1) in a sixty-cell view and absent in
		// the twenty. **The user types with nothing on screen saying
		// where**, in the one case the small-terminal design is for.
		//
		// The caret is what a person needs kept in view, and it is the
		// same rect the cursor placement already asks Qt for. Mapped from
		// the focus widget rather than from the inner editor that may own
		// it: that distinction is worth a cell when PLACING a cursor and
		// is not worth one when deciding what to scroll into view.
		const QVariant v = fw->inputMethodQuery(Qt::ImCursorRectangle);
		if (v.isValid()) {
			const QRect caret = v.toRect();
			if (caret.isValid())
				return QRect(fw->mapTo(layer, caret.topLeft()),
				             caret.size());
		}
		return QRect(fw->mapTo(layer, QPoint()), fw->size());
	}
	return std::nullopt;
}

} // namespace

// The routers that want telling when the drawn window moves.
//
// A Compositor constructed with a router IS the statement "this router serves
// the windows I draw", so the pairing comes from there rather than from
// InputRouter reading a global. A stack, pushed and popped with the
// compositor, for the same reason ~Compositor() clears the tab strip: a
// compositor's facts must not outlive it, and a suite builds many.
static QVector<InputRouter *> g_switch_routers;

Compositor::Compositor(QWidget *window, InputRouter *router)
    : win_(window), router_(router)
{
	if (router_) g_switch_routers.append(router_);
}


void Compositor::apply_priority(int cols, int rows) {
	// NO WINDOW, NO LAYER TO APPLY IT TO. The overload below asks the layer
	// for minimumSizeHint() before anything else, so this is a dereference
	// and not a lookup. See win_ in runtime.h.
	if (!win_) return;
	apply_priority(win_, root_, cols, rows);
}

// Hide the widgets the application marked Priority::Optional when the layer's
// layout minimum does not fit the terminal, and show them again when it does.
//
// Re-evaluated from scratch on every frame rather than latched, so a terminal
// that grows brings the content back with no separate path to get wrong.
void Compositor::apply_priority(QWidget *layer, Layer &state, int cols, int rows) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QSize term(cols * cw, rows * ch);
	const auto fits = [&] {
		const QSize m = layer->minimumSizeHint();
		return m.width() <= term.width() && m.height() <= term.height();
	};

	// Put back first, then measure: a screen that fits ONLY because something
	// is hidden must not stay hidden for ever. Showing what this pass hid and
	// re-asking is the whole of the hysteresis, and it is why `state.dropped`
	// holds what we hid rather than what is hidden.
	bool changed = false;
	for (const QPointer<QWidget> &w : std::as_const(state.dropped)) {
		if (w) { w->show(); changed = true; }
	}
	state.dropped.clear();
	if (changed && layer->layout()) layer->layout()->activate();

	if (!fits()) {
		// The focused widget is never dropped, nor is any ancestor of it.
		// Hiding the widget that owns input moves focus somewhere the
		// application did not choose, and on a terminal there is no pointer to
		// put it back with.
		const QWidget *const focus = layer->focusWidget();
		const auto owns_focus = [&](const QWidget *w) {
			for (const QWidget *f = focus; f; f = f->parentWidget())
				if (f == w) return true;
			return false;
		};

		const auto all = layer->findChildren<QWidget *>();
		for (QWidget *w : all) {
			// findChildren() is recursive over the QObject TREE, not over this
			// window, so a dialog parented to the layer arrives in the list
			// along with everything inside it. Hiding content in another
			// top-level cannot make THIS one fit -- fits() asks this layer for
			// its own minimum -- so the drop was pure damage. Measured
			// 2026-09-02: a widget inside a child dialog went dark while the
			// root's minimum stayed at 95 px against a 57 px terminal.
			if (w->window() != layer->window()) continue;
			if (priority_of(w) != Priority::Optional) continue;
			if (!w->isVisible() || owns_focus(w)) continue;
			w->hide();
			state.dropped.append(w);
			changed = true;
			if (layer->layout()) layer->layout()->activate();
			if (fits()) break;                 // enough is enough: stop dropping
		}
	}

	// Dropping lowers the layout minimum, which is the whole point of it --
	// but the resize that provoked the drop was REFUSED before anything was
	// hidden, and nothing re-issues it. InputRouter::on_resize() resizes the
	// window and returns; the drop happens later, inside compose(). Measured
	// 2026-09-02 at a 20x3 terminal: the window stayed 200x133 px with two
	// optional widgets of six still up, against a minimum that had come down
	// to 57 and would have fitted -- and one row of the three carried
	// anything. At 20x1 the frame came out blank. So ask again, now that the
	// minimum has moved.
	//
	// Clamped to the terminal rather than set to it: a dialog smaller than the
	// screen must not be inflated to fill it, and a layer only ever needs to
	// come DOWN here, since a terminal that grows arrives through on_resize().
	//
	// Asked for whenever the pass changed something, not only when the layer
	// now fits, and that is deliberate. on_resize() does not test fits()
	// either -- it asks, and a layout that cannot go that small refuses the
	// last few pixels and keeps the rest. The one-row terminal is the case
	// that separates the two: nothing can make a 19-pixel screen hold a
	// 19-pixel label inside nine-pixel margins, so a fits() test would decline
	// to ask at all and leave a 133-pixel window on a 19-pixel screen, which
	// is the blank frame measured 2026-09-02. Asking gets 37 px -- still too
	// tall, and the row it does show now carries the label.
	const QSize want(qMin(layer->width(), term.width()),
	                 qMin(layer->height(), term.height()));
	if (changed && want != layer->size()) {
		layer->resize(want);
		if (layer->layout()) layer->layout()->activate();
	}
}

// design.md section 7's second half, for one layer. Follow the focus rather
// than binding a key: arrow keys belong to the focused widget and a chord
// would have to be learned, but Tab already walks the form -- so keeping the
// focused widget inside the terminal makes every widget reachable with the
// keys the application already answers. The offset is clamped to what actually
// overflows, so a layer that fits scrolls by nothing and this is invisible.
//
// What is followed is follow_rect()'s answer rather than focusWidget()
// directly, because a menu has no focus widget and its current item is the
// only thing a user moves in it.
void Compositor::follow_focus(QWidget *layer, Layer &state, int cols, int rows) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const int max_x = qMax(0, (layer->width() + cw - 1) / cw - cols);
	const int max_y = qMax(0, (layer->height() + ch - 1) / ch - rows);
	if (const std::optional<QRect> r = follow_rect(layer)) {
		const QPoint at = r->topLeft();
		const int left = at.x() / cw, top = at.y() / ch;
		const int right = (at.x() + r->width() - 1) / cw;
		const int bottom = (at.y() + r->height() - 1) / ch;
		// A rect WIDER than the view shows its LEFT edge, and one taller
		// its top. Without this the second branch is the only one that
		// can fire and it scrolls to the far edge -- measured on a menu
		// thirty cells wide in a twenty-cell terminal, which drew its
		// shortcut column and nothing else: Ctrl+Z, Ctrl+Y, Ctrl+X, with
		// every item NAME off the left of the screen. A person navigates
		// it blind.
		//
		// Left and top because that is where meaning starts: a label, a
		// menu item and a line of text all begin there. The caret of a
		// focused text field is followed separately by follow_rect(), so
		// this rule does not fight it.
		const bool wider = right - left + 1 > cols;
		const bool taller = bottom - top + 1 > rows;
		if (wider)                                     state.scroll.setX(left);
		else if (left < state.scroll.x())              state.scroll.setX(left);
		else if (right > state.scroll.x() + cols - 1)  state.scroll.setX(right - cols + 1);
		if (taller)                                    state.scroll.setY(top);
		else if (top < state.scroll.y())               state.scroll.setY(top);
		else if (bottom > state.scroll.y() + rows - 1) state.scroll.setY(bottom - rows + 1);
	}
	state.scroll.setX(qBound(0, state.scroll.x(), max_x));
	state.scroll.setY(qBound(0, state.scroll.y(), max_y));
}

// ------------------------------------------------- several windows, as tabs
//
// See qtty/windows.h for why a terminal arranges them this way at all. What
// lives here is the part compose() needs: which windows there are, which one
// is showing, and the strip that says so.
namespace {

QPointer<QWidget> g_current;
QVector<QPointer<QWidget>> g_tabs;
// Where the current window sat in the strip when it was last drawn. Kept so
// that a window CLOSING sends the user to its neighbour rather than to the
// far end: see choose_current_window().
int g_current_index = 0;
// Where each tab's name sits, so a press can be turned back into a window.
// Rebuilt every frame, because a window can be opened or closed between two.
QVector<QPair<int, int>> g_tab_spans;      // first column, last column

// The box a terminal draws around a dialog, which a desktop's window manager
// draws and this platform has nobody to draw. Measured without it: a QDialog
// titled "Preferences" over a window of labelled rows composed to
//
//     | window row 1  Theme:   |
//     | window row 2  [ ] Dark |
//
// -- nothing corrupted, since the dialog's widgets erase what they cover, and
// no way for a person to tell which columns are the dialog. `setWindowTitle()`
// was written nowhere at all: window_name() below serves the F6 strip, and a
// modal is not in it.
//
// NOT draw_box() from grid_style.cpp, and the difference is the reason rather
// than an oversight: that one writes glyphs and keeps whatever background it
// finds, which is right for a frame drawn INSIDE a widget's own cleared area
// and wrong here, where every cell of the ring still holds the window behind.
// This clears the ring first and carries a title.
//
// ONLY WHEN IT FITS, which is the holder's decision of 2026-09-17: the ring
// needs a row above and below and a column either side, and on a terminal
// that has not got them section 7 is already dropping content to make the
// dialog fit at all. A frame that pushed a field off the screen would be
// chrome winning over the thing it frames.
static void frame_layer(CellBuffer &out, const QRect &cells,
                        const QString &title) {
	const QRect ring = cells.adjusted(-1, -1, 1, 1);
	if (ring.left() < 0 || ring.top() < 0) return;
	if (ring.right() >= out.cols() || ring.bottom() >= out.rows()) return;
	if (ring.width() < 2 || ring.height() < 2) return;
	const auto put = [&out](int x, int y, const QString &g) {
		if (!out.writable(x, y)) return;
		out.at(x, y) = Cell{};
		out.at(x, y).ch = g;
	};
	for (int x = ring.left() + 1; x < ring.right(); ++x) {
		put(x, ring.top(), QStringLiteral("─"));
		put(x, ring.bottom(), QStringLiteral("─"));
	}
	for (int y = ring.top() + 1; y < ring.bottom(); ++y) {
		put(ring.left(), y, QStringLiteral("│"));
		put(ring.right(), y, QStringLiteral("│"));
	}
	put(ring.left(), ring.top(), QStringLiteral("┌"));
	put(ring.right(), ring.top(), QStringLiteral("┐"));
	put(ring.left(), ring.bottom(), QStringLiteral("└"));
	put(ring.right(), ring.bottom(), QStringLiteral("┘"));
	// The title sits IN the top rule with a space either side, which is what
	// every terminal dialog does, and is dropped whole rather than elided
	// when the rule is too short to hold it: half a title in a border reads
	// as a drawing fault, where a plain rule reads as a plain rule.
	if (title.isEmpty()) return;
	const QString label = QStringLiteral(" ") + title + QStringLiteral(" ");
	const int room = ring.width() - 3;             // corners, and one rule
	if (label.size() > room) return;
	int x = ring.left() + 2;
	for (const QString &cl : to_clusters(label)) {
		put(x, ring.top(), cl);
		x += cluster_width(cl);
	}
}

QString window_name(const QWidget *w, int index)
{
	if (!w->windowTitle().isEmpty()) return w->windowTitle();
	// A window with no title still needs something to click. Its class is
	// more use than a number on its own, and an application that cares sets
	// a title -- which is the same thing a desktop would show.
	return QStringLiteral("%1 %2").arg(QString::fromLatin1(
	    w->metaObject()->className())).arg(index + 1);
}

// The order windows were first SEEN in, which is the order the strip shows
// them in. QApplication::topLevelWidgets() is not that: its order is Qt's
// own bookkeeping and is not promised to be stable. Measured, three windows
// created first, second, third across five runs of one program: four gave
// `[first, second, third]` and one gave `[first, third, second]`.
//
// A tab strip that reorders itself between frames is bad on its own -- the
// user's second tab becomes their third while they are looking at it -- and
// it also makes anything derived from the order unstable, which is how it
// was found: the neighbour rule below picked a different survivor run to
// run, and a focus check three hundred lines away in the suite failed one
// run in three.
//
// Entries are never reordered once made, and dead ones are dropped rather
// than reused, so a window that closes does not shuffle the rest.
QVector<QPointer<QWidget>> g_seen;

static void remember(QWidget *w)
{
	for (const QPointer<QWidget> &p : std::as_const(g_seen))
		if (p.data() == w) return;
	g_seen.append(w);
}

QVector<QWidget *> collect_window_tabs(QWidget *root)
{
	QVector<QWidget *> out;
	// The root only while it can be SEEN. It used to go in unconditionally,
	// so an application that hides its main window kept a tab for it -- and
	// the pick below, which takes the first tab when the current window has
	// gone, chose a window the terminal cannot draw. Measured: close the
	// window you are in with the main one hidden, and the strip says
	// "[primary]" over a frame that does not hold it.
	if (root && is_compositable(root)) out.append(root);
	// Everything else in the order it was first seen. The sweep over Qt's
	// list is still what finds a new window; what it decides is membership,
	// not position.
	for (QWidget *w : QApplication::topLevelWidgets()) {
		if (w != root && strip_member(w)) remember(w);
	}
	for (int i = 0; i < g_seen.size();) {
		QWidget *const w = g_seen.at(i).data();
		if (!w) {                       // gone for good: drop the slot
			g_seen.remove(i);
			continue;
		}
		++i;
		if (w != root && strip_member(w)) out.append(w);
	}
	return out;
}

QWidget *choose_current_window(const QVector<QWidget *> &tabs, QWidget *root)
{
	// A window that has gone takes the selection with it, back to the root
	// rather than to nothing: a frame with no current window would draw an
	// empty screen and give a user nowhere to click.
	if (g_current && tabs.contains(g_current.data())) return g_current.data();
	// RETURNED, not assigned. Assigning here made this the second way the
	// current window changes, and the only one that carried nothing with it:
	// a switch moves input, focus, the open menu and the terminal's title
	// (8.107, 8.108), and a window closing under the user went through none
	// of that. Measured -- close the window you are in and the title still
	// names it, nothing has focus, and the next keystroke goes nowhere.
	// compose() hands the answer to enter_window(), so both routes carry the
	// same things.
	// THE NEIGHBOUR, not the first. Landing at the far end of the strip is
	// what every tabbed thing a terminal user knows does NOT do -- a browser,
	// an editor, a multiplexer all select the tab beside the one that closed
	// -- and `tabs.first()` was an accident of writing the simplest thing
	// that was never a user's expectation. Measured before this: three
	// windows, switch to the third, close it, and the terminal showed the
	// FIRST.
	//
	// Anchored to WIDGETS rather than to a number. The first version kept the
	// departed window's index and clamped it into the new list, and a bare
	// index outlives the windows it described: carried across unrelated
	// windows in one process it picked by a number that meant nothing, and
	// the suite went intermittent -- 1369 checks on three runs and 1368 on
	// the fourth, a focus check failing because a different window had
	// become current. Walking the strip THIS list was drawn from, outwards
	// from where the current one sat, cannot do that: every candidate is a
	// window that still exists and is still on the strip.
	if (tabs.isEmpty()) return root;
	for (int step = 0; step < g_tabs.size(); ++step) {
		for (const int dir : {1, -1}) {
			const int at = g_current_index + dir * step;
			if (at < 0 || at >= g_tabs.size()) continue;
			QWidget *const candidate = g_tabs.at(at).data();
			if (candidate && tabs.contains(candidate)) return candidate;
		}
	}
	return tabs.first();
}

// How long to coalesce damage before sending a frame, when nobody has said.
//
// design.md section 5.4 has called this "configurable" since it was written
// and section 11 goes further -- "frame budget 16 ms local, 50 ms over ssh;
// FrameScheduler coalesces to whichever applies" -- while the number was a
// literal 16 inside one expression, reachable by nothing and adapting to
// nothing. Two claims in the design document that the code did not hold.
//
// QTTY_FRAME_MS first, in the shape and with the bounds QTTY_ESCAPE_MS and
// QTTY_PROBE_MS already use, because the person who needs a different number
// is on a link nobody here can measure. 0 is accepted and means "next time
// the event loop comes back"; the upper bound is a second, past which a
// program stops feeling like it is responding at all.
//
// Then the link. SSH_CONNECTION is what every tool uses for this and it is
// the only signal available: the client sets it in the session's
// environment, and there is nothing to ask the terminal. Its limits are
// real and are the reason the override comes first -- a tmux session
// started over ssh and later reattached locally keeps a stale value in the
// server's environment, and mosh sets neither variable, so the two errors
// are a local program pacing itself for a slow link and a slow link paced
// for a local one.
//
// Both errors are cheap, which is what makes the guess worth making at all.
// Nothing is incorrect either way: frames are coalesced more or less
// aggressively, and section 11's own numbers are the two answers. A wrong
// guess costs latency or bandwidth, never a wrong picture -- and the
// environment variable is in front of it for anyone the guess fails.
int default_frame_ms() {
	if (const char *env = ::getenv("QTTY_FRAME_MS")) {
		const int v = ::atoi(env);
		if (v >= 0 && v <= 1000) return v;
	}
	// Non-empty, not merely present: an exported-but-empty variable is a
	// shell artefact rather than a statement about the link.
	const char *const conn = ::getenv("SSH_CONNECTION");
	const char *const tty = ::getenv("SSH_TTY");
	if ((conn && *conn) || (tty && *tty)) return 50;   // section 11, remote
	return 16;                                         // section 11, local
}

} // namespace

QVector<QWidget *> window_tabs()
{
	QVector<QWidget *> out;
	for (const QPointer<QWidget> &w : std::as_const(g_tabs))
		if (w) out.append(w.data());
	return out.size() > 1 ? out : QVector<QWidget *>();
}

QWidget *current_window() { return g_current.data(); }

// Leaving a window dismisses whatever it had open. On a desktop Qt does this
// for you: a popup closes when its window deactivates. No window ever
// activates here (F4), so nothing fired -- and a menu opened in one window
// stayed drawn over the next one AND kept input, because a popup owns input
// above everything. Measured through qtty-replay: F6 with the File menu up
// drew the menu over the second window and `text zz` reached neither, the
// second window's field unchanged and the keystrokes going to a menu
// belonging to a window nobody could see.
//
// NOT QApplication::activePopupWidget(), which was tried first and is null
// here for the same reason activeWindow() is: a popup carrying
// WA_DontShowOnScreen has no platform window for Qt to register as active.
// The flags are what qtty itself goes by, and InputRouter::is_popup_layer()
// is the one predicate that decides it -- asking anything else would be a
// second opinion about which widgets are popups.
//
// A free function rather than something on the router, because the guide
// tells an application to bind next_window() itself, and a fix living in
// the F6 path would miss every application that took that advice.
static void dismiss_popups()
{
	// Bounded: closing a popup can open another in principle, and a loop
	// that trusts the stack to shrink is a loop that can fail to.
	for (int guard = 0; guard < 32; ++guard) {
		QWidget *found = nullptr;
		const auto tops = QApplication::topLevelWidgets();
		for (QWidget *w : tops)
			if (w->isVisible() && InputRouter::is_popup_layer(w)) {
				found = w;
				break;
			}
		if (!found) return;
		found->close();
	}
}

// Arriving in a window is this runtime's activation, and it has to do what
// activation does. A desktop Qt gives a window with no focus widget its first
// tab stop when it activates -- QApplicationPrivate::setActiveWindow calls
// focusNextPrevChild() -- and nothing activates here (F4), so a window opened
// after exec() had NO focus widget at all and keys went to whatever the
// previous window had focused. Measured through qtty-replay: `window`, F6,
// then `text zz`, and the zz appeared in the FIRST window's field, which the
// user could no longer see. Drawing followed the switch and input did not.
//
// keyboard_reachable() rather than a walk of nextInFocusChain() written here,
// because that function is what decides a tab stop for Tab itself: seeding
// focus somewhere Tab would not have gone is a second focus order.
// Becoming the current window: everything a switch carries EXCEPT closing
// what the old window had open.
//
// Split out because compose() reaches this too, when the window you were in
// has gone and a new one has to be picked -- and that is not the same event
// as a person switching. Measured: calling the full switch from compose()
// dismissed popups during composition, so the first frame after a menu
// opened closed it, and fifteen checks about menus and popup placement
// failed at once.
static void adopt_window(QWidget *w)
{
	if (!w) return;
	if (!w->focusWidget()) {
		const QVector<QWidget *> stops = keyboard_reachable(w);
		if (!stops.isEmpty()) stops.first()->setFocus(Qt::OtherFocusReason);
	}
	g_current = w;
	// Keys follow the picture. Without this the router kept the window it was
	// constructed with and a switch moved only what is drawn.
	if (!g_switch_routers.isEmpty()) g_switch_routers.last()->set_input_window(w);
	// And so does what the terminal calls itself, which is the window
	// manager's job on a desktop and nobody's here.
	TitleKeeper::follow(w);
	// Qtty::focusWidget() is what the application, the tests and the cursor
	// placement read, and it is set by the router after each key. A switch is
	// not a key, so nothing would have updated it and the accessor would name
	// a widget in the window we just left.
	set_focus_widget(w->focusWidget());
}

// A switch, as a person makes it: the window being left gives up whatever it
// had open first.
static void enter_window(QWidget *w)
{
	if (!w) return;
	// Closing a popup runs the application's own code, which may close or
	// delete the window being entered -- and adopt_window() dereferences it
	// on the next line. Same class as the router's key and mouse paths.
	QPointer<QWidget> alive(w);
	dismiss_popups();
	if (!alive) return;
	adopt_window(w);
}

void set_current_window(QWidget *w)
{
	if (!w || w == g_current.data()) return;
	enter_window(w);
}

void next_window()
{
	const QVector<QWidget *> t = window_tabs();
	const int i = t.indexOf(g_current.data());
	if (t.isEmpty()) return;
	enter_window(t.at((i < 0 ? 0 : i + 1) % t.size()));
}

void previous_window()
{
	const QVector<QWidget *> t = window_tabs();
	const int i = t.indexOf(g_current.data());
	if (t.isEmpty()) return;
	enter_window(t.at((i <= 0 ? t.size() : i) - 1));
}

// The window a press in the strip selects, or null for a press anywhere
// else. The router asks before it does anything with the event, because a
// click on the strip belongs to no widget at all.
QWidget *window_tab_at(const QPoint &cell)
{
	if (cell.y() != 0 || g_tabs.size() < 2) return nullptr;
	for (int i = 0; i < g_tab_spans.size() && i < g_tabs.size(); ++i)
		if (cell.x() >= g_tab_spans[i].first && cell.x() <= g_tab_spans[i].second)
			return g_tabs[i].data();
	return nullptr;
}

// The strip itself, in row 0. Spelled like this style's own tab bar so the
// two do not look like different ideas: the current one in brackets, the
// rest plain, and a name elided rather than allowed to push the next one off
// the row.
static void draw_window_tabs(CellBuffer &out, const QVector<QWidget *> &tabs,
                             const QWidget *shown)
{
	g_tabs.clear();
	g_tab_spans.clear();
	// The row is the strip's, all of it. It used to be written only where a
	// tab label falls, which was invisible while the window below started at
	// row 1 -- and stopped being invisible the moment a scrolled window could
	// put its own border on row 0. What showed through was the window's frame
	// running out of the last tab to the right edge, which reads as part of
	// the strip and is not.
	for (int x = 0; x < out.cols(); ++x)
		if (out.writable(x, 0)) out.at(x, 0) = Cell();
	int x = 0;
	for (int i = 0; i < tabs.size(); ++i) {
		g_tabs.append(tabs[i]);
		const bool here = tabs[i] == shown;
		const int room = out.cols() - x;
		if (room <= 2) { g_tab_spans.append({out.cols(), out.cols() - 1}); continue; }
		QString name = window_name(tabs[i], i);
		// Two cells for the brackets, one so the next tab is not flush
		// against this one.
		if (name.size() > room - 3) name = name.left(qMax(0, room - 4)) + QStringLiteral("…");
		const QString text = (here ? QStringLiteral("[") : QStringLiteral(" "))
		                   + name
		                   + (here ? QStringLiteral("]") : QStringLiteral(" "));
		const int first = x;
		for (const QChar c : text) {
			if (x >= out.cols()) break;
			Cell v;
			v.ch = QString(c);
			if (here) v.attrs |= Attr::Reverse;
			if (out.writable(x, 0)) out.at(x, 0) = v;
			++x;
		}
		g_tab_spans.append({first, x - 1});
	}
}

Compositor::~Compositor()
{
	g_tabs.clear();
	g_tab_spans.clear();
	g_switch_routers.removeAll(router_);
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
		// Inside the lambda rather than after the walk, because the origin
		// is per-window: every top-level is drawn at its own position and
		// apply_blink() maps a widget rectangle into the buffer through that
		// same offset. Done once at the end it would map every secondary
		// window through the last origin used.
		apply_blink(*w, out, at);
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
	// The tab strip, and which window it has chosen. Collected BEFORE the
	// policy below, because the policy belongs to the window being drawn and
	// this is what decides which one that is -- and because the strip takes a
	// row off the top, which is a row the policy does not have to spend.
	const QVector<QWidget *> tabs = collect_window_tabs(win_);
	QWidget *const shown = choose_current_window(tabs, win_);
	// AFTER the choice, so the value read above is the previous frame's --
	// where the window that has just gone used to sit. Recorded against the
	// STRIP's order rather than this list's, because that is the list the
	// walk above searches and the one the user is looking at.
	for (int at = 0; at < g_tabs.size(); ++at)
		if (g_tabs.at(at).data() == shown) {
			g_current_index = at;
			break;
		}
	const int strip = tabs.size() > 1 ? 1 : 0;

	// design.md section 7's policy in the order it names: drop what the
	// application said is optional FIRST, and only then scroll what is left.
	// Dropping can make a screen fit; scrolling never does, it only makes the
	// rest reachable.
	//
	// ON THE WINDOW BEING DRAWN, and it was on win_ -- the window this
	// compositor was constructed with -- however many windows the application
	// had. So a second window too big for the terminal got neither half: it
	// dropped nothing and it never scrolled to its focus. Measured with two
	// windows in a 30x8 terminal, the second 26 rows tall with the focus on
	// its last field: the frame held row0 to row2 and the field being typed
	// into was row11, off the bottom with no way to reach it. That is 8.101's
	// defect exactly, for every window except the first, and it is the same
	// shape as 8.107 -- the picture followed the switch and the machinery
	// behind it did not.
	QWidget *const base = shown ? shown : win_.data();
	// A FRAME WITH NO ROOT LAYER IS NO FRAME, and this is the one place that
	// can say so. Everything below reads `base` -- focusWidget(), the priority
	// pass, render() -- so a null one is a dereference three lines later
	// rather than an empty picture.
	//
	// Reachable only once win_ is a QPointer: `shown` is null when the strip
	// is empty, and the strip is empty when this compositor's window has been
	// destroyed and no other top-level is up. A raw win_ took the same branch
	// holding a freed address, passed collect_window_tabs()'s `if (root &&
	// ...)` -- a freed pointer is not a null one -- and read isVisible() out
	// of the dead widget. So the honest answer is nothing drawn: out was
	// cleared at the top, and a scheduler diffing against the previous frame
	// sees the screen go blank, which is what has actually happened.
	if (!base) return;
	// The pick above may have chosen a different window because the current
	// one has gone. Going through the switch is what carries input, focus,
	// the open menu and the title to it.
	if (base != g_current.data()) adopt_window(base);
	// A layer's dropped-widget list and scroll belong to that layer, the rule
	// input_layer_ and popup_layer_ already follow. Carrying one window's
	// state into another would show the new window scrolled to a position
	// computed from the old one's focus, and would hide widgets it never
	// dropped.
	//
	// PUT BACK WHAT WE HID BEFORE FORGETTING THAT WE HID IT, which the first
	// version of this did not -- and its comment claimed to follow the rule
	// the two resets below follow, while following half of it. `dropped`
	// holds what THIS policy hid, so discarding it without showing those
	// widgets leaves them hidden with no record anywhere that they were:
	// returning to the window finds an empty list, and the drop loop skips
	// them because they are already invisible. Measured, with an optional
	// widget in a 30x7 window:
	//
	//     small, then roomy again          the widget comes back
	//     small, visit another window,
	//       return, roomy                  HIDDEN, and hidden for good
	//
	// A window that is not being drawn loses nothing by having its optional
	// widgets shown: nobody is looking at it, and the policy re-drops them
	// on the frame it becomes current again if the terminal still demands
	// it.
	if (base != root_layer_) {
		for (const QPointer<QWidget> &w : std::as_const(root_.dropped))
			if (w) w->show();
		root_ = Layer{};
		root_layer_ = base;
	}
	// WHO HAS FOCUS, RE-READ RATHER THAN WAITED FOR. An application moving
	// focus itself -- `edit->setFocus()` in a slot, the commonest thing there
	// is -- changes Qt's per-window focus widget and tells nobody: measured,
	// a window that never activates emits no focusChanged signal and delivers
	// no FocusIn or FocusOut event, so there is no hook to connect to. The
	// record then disagreed with Qt until the next keystroke, and everything
	// that asks this library who has focus -- the reverse-video mark, the
	// widget-shortcut contexts, an application calling Qtty::focusWidget() --
	// got the widget that used to.
	//
	// Before anything is drawn, so the frame already carries the synthetic
	// focus events set_focus_widget() sends rather than showing them a frame
	// late.
	if (base->focusWidget() != focusWidget()) set_focus_widget(base->focusWidget());

	apply_priority(base, root_, out.cols(), out.rows() - strip);
	follow_focus(base, root_, out.cols(), out.rows() - strip);

	const QPoint root_at(-root_.scroll.x() * cw, -root_.scroll.y() * ch);
	// The router maps a click from a screen cell to a window position, and
	// the root is not drawn at the screen's origin once this scrolls. Told
	// here rather than asked for, because compose() is the only place that
	// knows -- and because the Compositor already holds the router.
	// The strip is part of the offset input has to undo, not just part of
	// the picture. It takes row 0 and everything below it moves down a row,
	// and nothing shared that with the router: measured, with two windows up
	// a button DRAWN at screen row 1 could not be clicked there at all --
	// the same fault the comment beside root_scroll_ already records for
	// scrolling, arriving by a second route the day tabs were added.
	//
	// Folded into the scroll rather than sent separately, because the router
	// already subtracts one offset and two would be two chances to apply the
	// wrong one.
	if (router_) router_->set_root_scroll(root_.scroll - QPoint(0, strip));
	// The window the strip chose, at the origin below it, scrolled by ITS OWN
	// offset. That offset used to be applied only when the window happened to
	// be win_, which was right while nothing else could scroll and wrong the
	// moment the policy above learned to scroll another window.
	const QPoint base_at = root_at + QPoint(0, strip * ch);
	draw(base, base_at);
	if (strip) {
		// AFTER the window, not before it. A scrolled window is drawn at a
		// negative offset, so whichever of its rows lands on screen row 0
		// would paint straight over the strip -- measured the moment the
		// policy above learned to scroll a second window, and reachable for
		// the first window too, since a scrolled primary with a strip up has
		// always been able to do this. The strip beats the window it labels
		// and loses to the modals and popups drawn after it, which is the
		// layering the rest of this function already has.
		draw_window_tabs(out, tabs, shown);
	} else {
		// No strip means no tab to click, and the record of the last one has
		// to go with it. It is a file static, so without this it outlived the
		// frame that drew it and InputRouter went on treating a press in row
		// 0 as a tab selection -- measured, it swallowed the slider fixtures'
		// clicks, which land in row 0 of their own window and have nothing to
		// do with any strip.
		g_tabs.clear();
		g_tab_spans.clear();
	}

	QWidget *cursor_layer = base;
	QPoint cursor_origin = base_at;

	QVector<QWidget *> modals;
	const auto tops = QApplication::topLevelWidgets();
	for (QWidget *w : tops) {
		if (w == win_ || !is_compositable(w)) continue;
		if (InputRouter::is_popup_layer(w)) continue;     // the popup stack draws these
		if (w->isModal()) { modals.append(w); continue; } // the modal stack does
		// Every other plain top-level is a TAB now rather than a layer drawn
		// at its own position. It was the second that decided the frame:
		// measured, two windows drawn into one rectangle left only the last
		// one's contents and no way to reach the other. See qtty/windows.h.
		//
		// Nothing is drawn here; the bar above chose one and drew it.
		(void)w;
	}

	// section 8.1's mitigation: activeModalWidget() and activePopupWidget() as
	// an explicit z-ordered stack on top. Without it a modal QDialog was stamped
	// WA_DontShowOnScreen by the router and then drawn by nobody -- invisible
	// while still holding input, which is the worst of both.
	QWidget *const active_modal = QApplication::activeModalWidget();
	// A different layer owns input, so what the old one lost goes back. Hiding
	// is this class's doing and must not outlive the reason for it -- the same
	// hysteresis apply_priority() runs every frame, applied to a layer it is
	// not going to be called for again.
	if (active_modal != input_layer_) {
		for (const QPointer<QWidget> &w : std::as_const(input_.dropped))
			if (w) w->show();
		input_ = Layer();
		input_layer_ = active_modal;
	}
	if (active_modal && modals.removeAll(active_modal))
		modals.append(active_modal);                      // the active one is topmost
	// Which modals this compositor places, decided the FIRST time each is
	// seen and re-checked every frame after.
	//
	// The decision has to be taken first because compose() moves a modal to
	// where it draws it, and QWidget::move() sets WA_Moved -- so by the second
	// frame every dialog would look like one the application had positioned.
	// Measured that Qt leaves the flag clear after its own default placement
	// and sets it for an explicit move(), which is what makes the question
	// answerable at all.
	//
	// Re-checked, because an application may move its dialog later: the
	// position we last placed it at is remembered, and a geometry that no
	// longer matches means the application has spoken and we stop. That is
	// the popup stack's `placed` trick, for the same reason.
	QHash<QWidget *, QPoint> mine;
	for (QWidget *w : std::as_const(modals)) {
		QPoint at;
		const bool ours = modal_place_.contains(w)
		    ? modal_place_.value(w) == w->geometry().topLeft()
		    : (!w->parentWidget() && !w->testAttribute(Qt::WA_Moved));
		QRect want = w->geometry();
		if (ours)
			want.moveTopLeft(centred_in(w->size(), out.cols(), out.rows(),
			                            cw, ch));
		if (w == active_modal) {
			// design.md section 7's policy runs on the layer that OWNS INPUT,
			// not on the root. A modal owns input while it is up (section
			// 8.3), so a modal too big for the terminal is the WORSE of the
			// two cases: there is no Tab away to a window that does scroll.
			// Measured 2026-09-02 with the same eight-field form built twice
			// -- in the root it dropped and scrolled and "Accept" reached the
			// frame at 30x6 and at 30x3, and in a modal it did neither.
			apply_priority(w, input_, out.cols(), out.rows());
			follow_focus(w, input_, out.cols(), out.rows());
			// MOVED to the scrolled position rather than merely drawn at it.
			// InputRouter::on_mouse() hit-tests a modal against its own
			// geometry() and maps the press through it, and only the ROOT's
			// offset is shared with the router -- so a modal drawn somewhere
			// its geometry does not say would take every click on the wrong
			// widget. Moving it keeps the place that reads the position and
			// the place that draws it saying the same thing.
			at = placed_at(want, out.cols(), out.rows(), cw, ch, false)
			     - QPoint(input_.scroll.x() * cw, input_.scroll.y() * ch);
			if (at != w->geometry().topLeft()) w->move(at);
		} else {
			at = placed_at(want, out.cols(), out.rows(), cw, ch, false);
			if (at != w->geometry().topLeft()) w->move(at);
		}
		if (ours) mine.insert(w, at);
		draw(w, at);
		// AFTER the layer, because the ring is cells the window behind
		// drew and the modal did not: drawn first, the dialog's own
		// background would have painted over it.
		frame_layer(out, QRect(at.x() / cw, at.y() / ch,
		                       qMax(1, w->width() / cw),
		                       qMax(1, w->height() / ch)),
		            w->windowTitle());
		if (w == active_modal) { cursor_layer = w; cursor_origin = at; }
	}
	// Only what is still up, so a closed dialog's record cannot dangle -- the
	// popup stack's rule, applied to the same hazard.
	modal_place_ = mine;

	// popups last: always the top of the stack, and flipped rather than slid.
	const auto popups = router_ ? router_->popups() : QVector<QWidget *>{};
	// The popup that owns input, which is the top of the stack and not the
	// whole of it: section 7's policy belongs to the layer input goes to, the
	// same rule the modal branch above follows. A menu with a submenu open is
	// not the layer being driven, so it does not scroll.
	QWidget *top_popup = nullptr;
	for (QWidget *pop : popups)
		if (is_compositable(pop)) top_popup = pop;
	if (top_popup != popup_layer_) {
		for (const QPointer<QWidget> &w : std::as_const(popup_.dropped))
			if (w) w->show();
		popup_ = Layer();
		popup_layer_ = top_popup;
	}

	QHash<QWidget *, PopupPlace> anchors;
	for (QWidget *pop : popups) {
		if (!is_compositable(pop)) continue;
		// Where the popup ASKED to be. A popup is MOVED to where it is drawn
		// rather than drawn at an offset -- InputRouter::on_mouse() hit-tests
		// it against its own geometry() and only the ROOT's offset is shared
		// with the router, so a popup drawn somewhere its geometry does not
		// say would take every click on the wrong item. That is the modal
		// branch's reason verbatim. But moving it destroys the answer to
		// "where does this belong", and a root that scrolls again while the
		// popup is up would then compound the offset instead of replacing it.
		// So the anchor is remembered, and `placed` is what tells a popup that
		// moved ITSELF -- a submenu following its parent -- from one this
		// function moved.
		const PopupPlace known = popup_place_.value(pop);
		const QPoint anchor = known.placed == pop->geometry().topLeft()
		    ? known.anchor : pop->geometry().topLeft();

		// A popup anchored inside the ROOT moves with it. The root is the one
		// layer compose() does not move to where it draws it -- it is drawn at
		// -scroll -- so a popup left at its own geometry stays beside the
		// widget that used to be there. Measured 2026-09-02 with a menu that
		// fits exactly where it was opened, so that neither the clamp nor the
		// flip can explain it: the root scrolled four rows and the menu did
		// not, so it sat four rows below the widget it was opened at.
		//
		// A popup opened from a modal needs nothing: the modal IS moved to
		// where it is drawn, so a position mapped through it is already a
		// screen position.
		const bool in_root = !pop->parentWidget()
		    || pop->parentWidget()->window() == win_;
		const QPoint origin = in_root
		    ? anchor - QPoint(root_.scroll.x() * cw, root_.scroll.y() * ch)
		    : anchor;

		QPoint at = placed_at(QRect(origin, pop->size()), out.cols(), out.rows(),
		                      cw, ch, true);
		if (pop == top_popup) {
			// section 7's policy on the layer that owns input, which the popup
			// layer never got. A menu taller than the terminal is the case
			// with no way out at all: there is no Tab away from it, and Qt
			// will not paginate it either, because the offscreen QScreen is
			// 800x800 and nothing tells Qt how big the terminal is.
			apply_priority(pop, popup_, out.cols(), out.rows());
			follow_focus(pop, popup_, out.cols(), out.rows());
			at -= QPoint(popup_.scroll.x() * cw, popup_.scroll.y() * ch);
		}
		if (at != pop->geometry().topLeft()) pop->move(at);
		anchors.insert(pop, PopupPlace{anchor, at});
		draw(pop, at);
	}
	// Only what is still open: a closed popup's anchor is meaningless, and the
	// pointer would dangle if a menu were destroyed while this remembered it.
	popup_place_ = anchors;

	dev.origin = QPoint();
	out.images = dev.placements;

	// Hardware cursor from the focus widget of the layer that owns input
	// (section 5.5). A modal owns input while it is up (section 8.3), so it owns
	// the cursor too; a popup has no text cursor of its own.
	cursor_.reset();
	// Reset BESIDE the cell rather than anywhere else, because the pair is
	// one answer: Hidden is what "there is no caret" means on the wire, and
	// a shape left over from the last frame would be a stale request sitting
	// behind a cursor that has since moved to a different widget. Every path
	// below that sets a cell sets a shape with it.
	cursor_shape_ = CursorShape::Hidden;
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
	// AND NOT THE ITEM VIEW ITSELF, which carries that attribute the
	// moment its current item is editable -- which QStringListModel,
	// QStandardItemModel and every QTableWidget item are by default. The
	// attribute says the widget can accept input-method text, and the rule
	// above wants the narrower thing: a caret that exists right now.
	//
	// Measured on a focused QListView over the default list model: the
	// cursor was parked at cell (4,2), inside the first row, with no caret
	// anywhere on the screen -- the same fault the comment above records
	// fixing for a check box and a slider, arriving by a second route.
	// Nothing is lost by refusing it: while such a view IS editing, the
	// editor is a child QLineEdit and the FOCUS WIDGET, so it takes the
	// cursor on its own account -- measured, (4,3) on the editor with the
	// list at (4,2) before and after.
	if (qobject_cast<QAbstractItemView *>(fw)) fw = nullptr;
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
			// AND THE VERTICAL CENTRE, for the same reason, which this
			// comment used to deny: it said the top was right because
			// the rectangle is the line's full height and its top is the
			// caret's top. That holds only while the widget happens to
			// sit on a cell boundary, and a widget in a LAYOUT does not.
			// Measured on a 19-pixel cell, a QTableWidget's editor, with
			// the table at fixed geometry and then in a QVBoxLayout:
			//
			//   fixed    viewport at y=19   editor tops 19, 38, 57
			//            top -> rows 1,2,3        text drawn on 1,2,3
			//   layout   viewport at y=10   editor tops 10, 29, 48
			//            top -> rows 0,1,2        text drawn on 1,2,3
			//
			// Nine pixels above a cell boundary every time, so the
			// division floored to the row above and the terminal cursor
			// sat one row over the text being edited -- in every real
			// application, a view being in a layout rather than at fixed
			// geometry being the ordinary case.
			//
			// The caret rectangle is a whole line high -- measured 20 on
			// a 19-pixel cell, inflated like the horizontal one -- so its
			// centre is inside the line's own cell whatever the sub-cell
			// offset, and both readings agree where the widget IS
			// aligned. Taking an edge of an inflated rectangle was the
			// same mistake in both axes; it was found in one of them
			// first.
			const QRect cr = v.toRect();
			QPoint g = cursor_origin
			           + owner->mapTo(cursor_layer,
			                          QPoint(cr.x() + cr.width() / 2,
			                                 cr.y() + cr.height() / 2));
			QPoint cell(g.x() / cw, g.y() / ch);
			if (cell.x() >= 0 && cell.y() >= 0
			    && cell.x() < out.cols() && cell.y() < out.rows()) {
				cursor_ = cell;
				cursor_shape_ = shape_for(fw);
			}
		}
	}
}

// A bar by default, a block where the widget is OVERWRITING rather than
// inserting. That is the convention every terminal editor already follows
// and the one thing about a caret a terminal user reads without being told:
// a bar sits BETWEEN two characters, which is where an inserted one goes,
// and a block sits ON one, which is the character about to be replaced.
//
// It is derived from the widget rather than chosen by the application on
// purpose. `overwriteMode()` is a fact the application has already stated
// to Qt -- usually by handing the user an Insert key -- so the caret follows
// the mode with nothing added to any API, and an application that toggles
// the mode gets the caret change for free. Asking for a shape directly would
// be a second way to say the same thing, and two of them drift.
//
// QTextEdit and QPlainTextEdit are BOTH tested because neither derives from
// the other: they are siblings under QAbstractScrollArea, so a qobject_cast
// to the first answers false for the second. Nothing else in Qt Widgets has
// an overwrite mode -- QLineEdit has no such property at all, which is why
// it is the control the suite checks: a focused line edit must stay a bar
// however this is written.
//
// The FOCUS widget, not the rectangle's `owner`. The two differ for a widget
// that delegates editing to an inner QLineEdit -- a spin box, an item view --
// and in every one of those the inner editor is the line edit above, which
// has no overwrite mode to read. The mode belongs to the widget the
// application configured.
//
// AND NOTHING HERE PRODUCES CursorShape::Underline, deliberately. 8.249
// filed that as a gap and 8.250 answered it the other way: what this
// function can read is one property, overwriteMode(), which has two values
// and already has two shapes -- with Hidden for no caret at all, that is
// every condition qtty can see, spoken for. An underline needs a fourth
// condition invented for it.
//
// The one candidate anybody reaches for is read-only, and project.md's
// section 0b records marking read-only as an open scope question that
// "needs vocabulary". Answering it by choosing a caret shape would settle it
// sideways, in the place nobody would look for the decision -- and it would
// say something no terminal convention gives a reader any way to read. The
// other candidate, letting the application ask for a shape directly, the
// paragraph above already refuses: it would be a second way to say what
// overwriteMode() says, and two of them drift.
//
// AND READ-ONLY CANNOT BE WIRED HERE ANYWAY, which was found by sabotaging
// it and watching the check stay green. Qt reports no cursor rectangle for
// a read-only editor, and compose() above asks for a shape only inside the
// test that the rectangle lands on the grid -- so the caret is Hidden and
// this function is never called. Measured over nine focused states: three
// read-only ones, all three Hidden. Giving read-only a caret shape would
// take a change to that gate as well as to this, which is a larger
// decision than a caret.
//
// So Underline is not dead code. ITerminalBackend::set_cursor() is public,
// it takes the whole enum, and AnsiBackend encodes ESC[4 q for an
// application driving its own frame loop -- suite_backend checks those bytes
// on the wire. What has no producer is qtty's own chooser, and the suite
// asserts that, so a producer added later reddens a check and sends whoever
// added it here rather than leaving them to discover this paragraph.
CursorShape Compositor::shape_for(QWidget *fw) {
	if (auto *te = qobject_cast<QTextEdit *>(fw))
		return te->overwriteMode() ? CursorShape::Block : CursorShape::Bar;
	if (auto *pe = qobject_cast<QPlainTextEdit *>(fw))
		return pe->overwriteMode() ? CursorShape::Block : CursorShape::Bar;
	return CursorShape::Bar;
}

std::optional<QPoint> Compositor::cursor_cell() const { return cursor_; }
CursorShape Compositor::cursor_shape() const { return cursor_shape_; }

// ------------------------------------------------------------- FrameScheduler
// The interval an application asked for before any scheduler existed, and
// the schedulers running now. Both halves are needed and neither is enough,
// which is the argument set_quit_keys() already makes for its own pair: an
// application chooses how often to paint while building its window, long
// before exec() constructs a scheduler, and one that changes it from a slot
// is talking about the scheduler already driving the screen.
//
// -1 means "nobody has said", so that the measured default is asked for
// afresh -- QTTY_FRAME_MS and the ssh guess are read per scheduler, and
// caching the number here would freeze an answer the environment owns.
static int s_frame_ms_default = -1;
static QVector<FrameScheduler *> s_live_schedulers;

void set_frame_interval(int ms) {
	if (ms < 0) return;                  // refused, as the member refuses
	s_frame_ms_default = ms;
	for (FrameScheduler *f : std::as_const(s_live_schedulers))
		f->set_frame_interval(ms);
}

int frame_interval() {
	return s_frame_ms_default >= 0 ? s_frame_ms_default : default_frame_ms();
}

FrameScheduler::FrameScheduler(ITerminalBackend *backend, Compositor *compositor,
                               QWidget *window)
    : backend_(backend), comp_(compositor), win_(window),
      // NOT frame_interval(), which inside this class names the MEMBER and
      // reads frame_ms_ before it exists -- measured, a scheduler built
      // after a process-wide set came out 0. The free function is spelled
      // out instead of qualified so the trap is visible rather than
      // avoided.
      frame_ms_(s_frame_ms_default >= 0 ? s_frame_ms_default
                                        : default_frame_ms()) {
	s_live_schedulers.append(this);
	// The terminal's answer about wide clusters, taken once, here, because
	// this is where qtty is handed a backend: exec() builds one of these,
	// and so does an application running its own loop with a scheduler.
	// Reading it per cluster instead would be a virtual call and a struct
	// copy for every character of every frame.
	if (backend_) set_wide_clusters(backend_->capabilities().unicode_wide);
	coalesce_.setSingleShot(true);
	coalesce_.setInterval(0);
	QObject::connect(&coalesce_, &QTimer::timeout, this, [this] { render_now(); });
	idle_.setInterval(100);                       // catches timer-driven updates
	QObject::connect(&idle_, &QTimer::timeout, this, [this] {
		// `win_ &&` FIRST, and it is the whole of 8.246's third instance.
		// This timer is started in the constructor and stopped by nothing, so
		// a destroyed window is read here a tenth of a second later with no
		// other event required -- see win_ in runtime.h. An absent window is
		// not a visible one, so there is nothing to ask for.
		if (win_ && win_->isVisible()) request_frame();
	});
	idle_.start();
	since_last_.start();
	qApp->installEventFilter(this);
}

FrameScheduler::~FrameScheduler() { s_live_schedulers.removeAll(this); }

bool FrameScheduler::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::UpdateRequest || e->type() == QEvent::LayoutRequest)
		if (qobject_cast<QWidget *>(o)) request_frame();
	return false;
}

QRegion FrameScheduler::pixel_damage(const QRegion &cells,
                                     const QVector<QRect> &was,
                                     const QVector<QRect> &now) {
	QRegion out = cells;
	for (const QRect &r : was) out += r;
	for (const QRect &r : now) out += r;
	return out;
}

void FrameScheduler::request_frame() {
	// Section 11's budget; coalesce bursts into one frame.
	const int wait = qMax(0, frame_ms_ - int(since_last_.elapsed()));
	if (!coalesce_.isActive()) coalesce_.start(wait);
}

void FrameScheduler::set_frame_interval(int ms) {
	// Refused rather than clamped. A caller passing a negative interval has
	// made a mistake, and quietly treating it as 0 would turn that mistake
	// into a program that renders as fast as its event loop will let it --
	// on a slow link, the exact failure the budget exists to prevent.
	if (ms < 0) return;
	frame_ms_ = ms;
	// A frame already coalescing is re-timed on the new interval rather than
	// left running on the old one. Without this the setting takes effect
	// "from the next frame", which is a rule nobody could discover and which
	// makes the first call after a burst of damage do nothing at all -- and
	// damage is exactly what is happening when an application decides the
	// link is slower than it thought.
	if (coalesce_.isActive()) {
		coalesce_.stop();
		request_frame();
	}
}

void FrameScheduler::render_now() {
	// The terminal was handed over and taken back since the last frame, so
	// everything remembered about what is ON it is worthless. ESC[?1049h
	// switches to the alternate screen and CLEARS it, and prev_ is the frame
	// that screen used to be showing -- so every cell diffs to nothing,
	// present() is never called, and the user comes back from a Ctrl+Z to a
	// blank window. Measured with the chat example through a real bash: 24
	// rows of content became one, while ESC[23;2H ESC[?25h went out over and
	// over, frames being produced the whole time and each one deciding it had
	// nothing to say.
	//
	// The overlay bookkeeping goes with it and for the same reason: it
	// records what the TERMINAL is holding, and the terminal is holding
	// nothing now. Left alone, live_overlay_ids_ would have the next frame
	// deleting ids that are already gone.
	//
	// Asked rather than told, which is the shape this tree keeps arriving at:
	// a backend cannot reach a scheduler it does not know about, and a count
	// compared against a local copy cannot be consumed by another reader the
	// way a flag can.
	if (const int handed = backend_->handovers(); handed != seen_handovers_) {
		seen_handovers_ = handed;
		prev_.reset();
		prev_overlays_.clear();
		live_overlay_ids_ = 0;
	}
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
	const Capabilities caps = backend_->capabilities();
	const auto gmode = caps.graphics;
	// The terminal's own ground, for both of the sites below that paint one.
	// They took this library's constants instead until 8.250: the fallback by
	// omitting the argument, the rasteriser by carrying the pair as locals.
	// Resolved here, once, because this is where the backend is -- and out of
	// the same Capabilities an application reads, so the compositor cannot
	// hold a different opinion about the terminal from the one the program is
	// shown.
	const TerminalGround ground = TerminalGround::from(caps);
	auto *gfx = dynamic_cast<IGraphicsOutput *>(backend_);

	auto overlay_cell_rect = [&](Overlay *o) {
		return o->covers_terminal()
		    ? QRect(0, 0, frame.cols(), frame.rows())
		    : QRect(int(o->cell_rect().x()), int(o->cell_rect().y()),
		            int(o->cell_rect().width()), int(o->cell_rect().height()));
	};

	const bool software_composite = !overlays.isEmpty() && gfx
	    && (gmode == Capabilities::Sixel || gmode == Capabilities::ITerm2
	        || gmode == Capabilities::Kitty);

	if (!overlays.isEmpty() && !software_composite && gmode != Capabilities::KittyAlpha)
		for (Overlay *o : overlays)                       // fallback: half-blocks
			compose_halfblocks(frame, o->image(), overlay_cell_rect(o), ground);

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
	// An overlay that went away leaves nothing behind in the CELLS, so it
	// produces no damage and, on its own, would not even reach the branch
	// that talks to the terminal -- while its picture is still on the
	// screen. The count is therefore part of what makes a frame worth
	// sending.
	const bool overlays_retired = int(overlays.size()) < live_overlay_ids_;

	if (software_composite) {
		// One finished picture, kept between frames and repainted only where
		// the damage says. The placements and overlays are drawn every frame
		// but CLIPPED to the same region: they are composited over cells that
		// may have changed under them, and clipping is what stops that
		// costing the whole screen.
		const QSize want(frame.cols() * cw, frame.rows() * ch);
		QVector<QRect> now_over, was_over = prev_overlays_;
		for (Overlay *o : overlays) now_over.append(overlay_cell_rect(o));
		for (const CellImage &ci : frame.images) now_over.append(ci.cell_rect);
		if (prev_)
			for (const CellImage &ci : prev_->images) was_over.append(ci.cell_rect);
		QRegion pix = pixel_damage(damage, was_over, now_over);
		// A resized grid makes every pixel wrong at once, and there is no
		// previous image to repair -- so the region becomes everything and
		// the buffer is made afresh.
		if (pixels_.size() != want) {
			pixels_ = QImage(want, QImage::Format_ARGB32_Premultiplied);
			pixels_.fill(ground.default_bg);
			pix = QRegion(0, 0, frame.cols(), frame.rows());
		}
		const QRect cells_r = pix.isEmpty()
		    ? QRect(0, 0, frame.cols(), frame.rows())
		    : pix.boundingRect();
		// Clipped to what the rasteriser actually painted, not to what it
		// was asked for. It widens the rectangle leftwards to the start of a
		// wide cluster, and clipping to the narrower one would give that
		// column fresh cell pixels with no overlay drawn back over them -- a
		// hole in the overlay one cell wide, on a wide glyph at the left
		// edge of the damage. Asking for the rectangle back is what stops
		// the expansion rule being written in two places.
		const QRect painted =
		    rasterize_into(pixels_, frame, QGuiApplication::font(), cells_r,
		                   ground);
		QImage &px = pixels_;
		QPainter p(&px);
		p.setClipRect(QRect(painted.x() * cw, painted.y() * ch,
		                    painted.width() * cw, painted.height() * ch));
		// Z-ordered, which vector order is not. The ordering lives in
		// paint_placements() rather than here because a check cannot put a
		// placement of its own choosing into a composed frame; that header
		// carries the argument.
		paint_placements(p, frame.images, cw, ch);
		for (Overlay *o : overlays) {
			const QRect r = overlay_cell_rect(o);
			p.drawImage(QRect(r.x() * cw, r.y() * ch, r.width() * cw, r.height() * ch),
			            o->image());
		}
		p.end();
		gfx->present_pixels(px, pix);
	} else if (!damage.isEmpty() || images_changed || !prev_
	           || !overlays.isEmpty() || overlays_retired) {
		backend_->present(frame, damage);
		if (gmode == Capabilities::KittyAlpha && gfx) {   // terminal-blended alpha
			int id = 0;
			for (Overlay *o : overlays)
				gfx->present_overlay(id++, o->image(),
				                    overlay_cell_rect(o).topLeft(),
				                    qMax(1, o->z()));
			// And retire the ids that are no longer used. The id IS the
			// index in the z-sorted list, so a shrinking list leaves a tail
			// of placements the terminal is still showing -- measured on a
			// screen capture: an overlay presented and then dropped stayed
			// on the terminal at full size for as long as the program ran.
			// clear_overlay() has existed since the tier was written and
			// had no caller anywhere, which is the shape this project has
			// now met three times.
			for (int stale = id; stale < live_overlay_ids_; ++stale)
				gfx->clear_overlay(stale);
			live_overlay_ids_ = id;
		}
	}
	// The shape comes from the compositor rather than being decided here.
	// It was a literal Bar-or-Hidden written inline, which is the whole
	// reason CursorShape::Block and ::Underline had no producer anywhere in
	// the tree: the only chooser in qtty could not express them. The
	// compositor has the focus widget and so can answer; this has the
	// backend and cannot.
	backend_->set_cursor(comp_->cursor_cell(), comp_->cursor_shape());
	prev_ = std::make_unique<CellBuffer>(frame);
	// Beside prev_, and for the same reason: the next frame's damage cannot
	// be computed from this one's geometry alone.
	prev_overlays_.clear();
	for (Overlay *o : overlays) prev_overlays_.append(overlay_cell_rect(o));
	since_last_.restart();
}

} // namespace Qtty
