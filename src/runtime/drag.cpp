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
	QDrag *drag = nullptr;
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
	// porting from it must not be left holding one.
	drag->deleteLater();
	return live.result;
}

} // namespace Qtty
