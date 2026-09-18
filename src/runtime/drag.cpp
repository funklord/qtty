// The platform half of drag and drop, supplied by qtty. See qtty/drag.h for
// why it has to be supplied at all.
#include "qtty/drag.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QMimeData>
#include <QPointer>
#include <QWidget>

namespace Qtty {

namespace {

// One drag at a time, which is what a pointer can do. Kept in a file static
// rather than passed around because InputRouter has to reach it from a
// mouse event that knows nothing about the drag, and threading it through
// the router's signature would put drag and drop in the way of every click.
struct Live {
	// A QPointer, because THE NESTED LOOP RUNS THE APPLICATION AND THE
	// APPLICATION OWNS THIS QDrag.
	//
	// drag.h asks an application to call exec_drag(drag, actions) where it
	// would have called drag->exec(actions), so what arrives here is
	// whatever the application wrote for QDrag -- and the spelling in Qt's
	// own documentation is `new QDrag(this)` inside a mouse handler. That
	// makes the QDrag a CHILD OF THE SOURCE WIDGET, dying exactly when the
	// source widget does. exec_drag() then runs a nested event loop for as
	// long as the button is held, which is arbitrary application code, and
	// the sequence that kills the source from inside it is an ordinary
	// one: an item is dragged out of a panel, the drop handler takes it, and
	// the panel that is now empty closes itself -- or the dialog it was in is
	// dismissed. `delete panel` inside dropEvent() takes this QDrag with it.
	//
	// What the raw pointer cost is two things, and the second is the quiet
	// one. exec_drag() resumed after the loop at `drag->deleteLater()`,
	// which is a heap-use-after-free on the QDrag: the loud version, and the
	// one the sanitized suite aborts on. And `mime` below is the QMimeData
	// this QDrag OWNS and deletes, so every move delivered after the death
	// handed the target a payload that had been freed -- a target reads its
	// payload, that being what a target is for, and a QMimeData reallocated
	// where the old one stood answers text() with somebody else's string
	// while nothing crashes at all. That is the argument runtime.h makes
	// about win_ and grid_style.cpp makes about s_focus, met a third time.
	//
	// So a drag whose QDrag is gone is a drag that is OVER: drag_move_to()
	// and drag_drop_at() end it rather than merely refusing, because the
	// nested loop has nothing else left to quit it and a hang is worse than
	// either failure above -- it produces neither a PASS nor a FAIL.
	QPointer<QDrag> drag;
	// Valid exactly as long as `drag` is: QDrag::setMimeData() takes
	// ownership and ~QDrag deletes it, so the guard that ends a drag whose
	// QDrag has gone is what keeps this from being followed afterwards.
	const QMimeData *mime = nullptr;
	Qt::DropActions supported = Qt::IgnoreAction;
	Qt::DropAction preferred = Qt::IgnoreAction;
	Qt::DropAction result = Qt::IgnoreAction;
	QPointer<QWidget> over;          // the widget the pointer is inside now
	bool over_accepts = false;       // and whether it took the last event
	QEventLoop *loop = nullptr;
};

Live *g_live = nullptr;

// The widget a drop should be offered to: the deepest one under the pointer
// that accepts drops. Qt walks up from the child on the desktop too -- a
// label inside a drop area is not itself a target, and the area is.
QWidget *drop_target(QWidget *under)
{
	for (QWidget *w = under; w; w = w->parentWidget())
		if (w->acceptDrops()) return w;
	return nullptr;
}

} // namespace

bool drag_active() { return g_live != nullptr; }

void drag_move_to(QWidget *under, const QPoint &local, const QPoint &screen)
{
	if (!g_live) return;
	// THE QDrag DIED WHILE THE DRAG WAS UP, which is a state this function
	// can be entered in: the previous move ran the application's
	// dragMoveEvent, and that handler is allowed to destroy the widget the
	// QDrag is parented to. Nothing tells this file when that happens, so it
	// has to ask -- and `mime` is the QDrag's own property, so asking here
	// is what keeps the events below from carrying a freed payload.
	//
	// Ending the drag rather than returning, for the reason `drag` in Live
	// records: a bare return would leave the nested loop in exec_drag() with
	// nothing to quit it.
	if (!g_live->drag) { drag_cancel(); return; }
	QWidget *const target = drop_target(under);
	Q_UNUSED(screen);

	// Left one widget for another: the old one is told the drag has gone
	// before the new one is told it has arrived. Qt's own order, and a
	// target that highlights on enter and clears on leave depends on it --
	// the other way round leaves two widgets both looking like the drop
	// site.
	if (target != g_live->over) {
		if (g_live->over) {
			QDragLeaveEvent leave;
			QApplication::sendEvent(g_live->over, &leave);
		}
		g_live->over = target;
		g_live->over_accepts = false;
		if (target) {
			const QPoint p = target->mapFrom(under, local);
			QDragEnterEvent enter(p, g_live->supported, g_live->mime,
			                      Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(target, &enter);
			g_live->over_accepts = enter.isAccepted();
			if (g_live->over_accepts) g_live->preferred = enter.dropAction();
		}
		return;
	}

	if (!target) return;
	const QPoint p = target->mapFrom(under, local);
	QDragMoveEvent move(p, g_live->supported, g_live->mime,
	                    Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(target, &move);
	g_live->over_accepts = move.isAccepted();
	if (g_live->over_accepts) g_live->preferred = move.dropAction();
}

void drag_drop_at(QWidget *under, const QPoint &local, const QPoint &screen)
{
	if (!g_live) return;
	// The move first, so a target that has never seen this position decides
	// on it before being asked to take a drop there. Without it a drop
	// straight after a press -- a click that never moved -- reaches a widget
	// that was never offered the drag at all.
	drag_move_to(under, local, screen);

	// AND THE SAME QUESTION AFTER IT, because the move above ran the
	// application's dragMoveEvent -- the last handler of the application's
	// that runs before the drop -- and it can destroy the widget the QDrag
	// is parented to just as a drop handler can. A check at the TOP of this
	// function cannot see that: it has already run. This is the one that
	// keeps the QDropEvent below from carrying a QMimeData the QDrag freed
	// on its way out. See drag_move_to(), and `drag` in Live.
	if (!g_live->drag) { drag_cancel(); return; }

	QWidget *const target = g_live->over;
	if (target && g_live->over_accepts) {
		QDropEvent drop(QPointF(target->mapFrom(under, local)),
		                g_live->supported, g_live->mime,
		                Qt::LeftButton, Qt::NoModifier);
		drop.setDropAction(g_live->preferred);
		QApplication::sendEvent(target, &drop);
		g_live->result = drop.isAccepted() ? drop.dropAction()
		                                   : Qt::IgnoreAction;
	} else {
		// Released over nothing that wants it. The target still hears a
		// leave, because it heard an enter.
		if (target) {
			QDragLeaveEvent leave;
			QApplication::sendEvent(target, &leave);
		}
		g_live->result = Qt::IgnoreAction;
	}
	g_live->over = nullptr;
	if (g_live->loop) g_live->loop->quit();
}

void drag_cancel()
{
	if (!g_live) return;
	if (g_live->over) {
		QDragLeaveEvent leave;
		QApplication::sendEvent(g_live->over, &leave);
		g_live->over = nullptr;
	}
	g_live->result = Qt::IgnoreAction;
	if (g_live->loop) g_live->loop->quit();
}

Qt::DropAction exec_drag(QDrag *drag, Qt::DropActions supported,
                         Qt::DropAction preferred)
{
	if (!drag) return Qt::IgnoreAction;
	// A second drag while one is running is not a thing a pointer can do,
	// and answering it with a nested event loop would be worse than
	// refusing. The QDrag is still consumed, so a caller's ownership rule is
	// the same either way.
	if (g_live) { drag->deleteLater(); return Qt::IgnoreAction; }

	Live live;
	live.drag = drag;
	live.mime = drag->mimeData();
	live.supported = supported;
	live.preferred = preferred == Qt::IgnoreAction
	                     ? (supported & Qt::CopyAction ? Qt::CopyAction
	                                                   : Qt::MoveAction)
	                     : preferred;
	QEventLoop loop;
	live.loop = &loop;
	g_live = &live;

	// The nested loop is what makes exec_drag() a drop-in for QDrag::exec():
	// the caller is inside a mouse press handler and expects not to return
	// until the drag is over. Input keeps arriving, so InputRouter goes on
	// running and its moves reach drag_move_to() above.
	loop.exec();

	g_live = nullptr;
	// QDrag::exec() deletes the QDrag, and the header says so, so a caller
	// porting from it must not be left holding one -- but only if it is
	// still there to delete. The loop above ran the application, and the
	// application owns this object: see `drag` in Live, which is why that
	// member is a QPointer and why this reads it rather than the argument.
	if (live.drag) live.drag->deleteLater();
	return live.result;
}

} // namespace Qtty
