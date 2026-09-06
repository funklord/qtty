// qtty/drag.h -- drag and drop, driven by qtty rather than by a platform.
//
// Qt splits drag and drop in two. The WIDGET half -- dragEnterEvent,
// dragMoveEvent, dropEvent, the mime data, the accepted actions -- is
// ordinary Qt code and works here untouched: measured, a hand-delivered
// QDragEnterEvent and QDropEvent reach a target and it reads the payload.
// The PLATFORM half is what carries the pointer around while the drag is up,
// and qtty's offscreen platform has none, so QDrag::exec() returns
// Qt::IgnoreAction in under a millisecond and no target ever hears anything.
//
// That leaves the pointer, and the pointer is qtty's: InputRouter already
// owns every mouse event in the program because there is no platform to own
// them. So the missing half is one this library can supply, and exec_drag()
// is it.
//
// An application calls exec_drag(drag, actions) where it would have called
// drag->exec(actions). That is a real shortfall and is stated rather than
// hidden: QDrag::exec() is not virtual, returns before anything can be
// filtered, and there is no hook to make the ordinary spelling work. The
// same shortfall as Qtty::SystemTrayIcon and for the same reason -- Qt asks
// the platform, and the platform is a stub.
#pragma once
#include <QtGlobal>
#include <QPoint>

class QDrag;
class QWidget;
class QMimeData;

namespace Qtty {

// Runs a drag until the button is released or it is cancelled, delivering
// the enter, move, leave and drop events to whatever the pointer is over.
// Returns the action the target accepted, or Qt::IgnoreAction.
//
// `drag` is consumed exactly as QDrag::exec() consumes it: it is deleted
// when the drag finishes, so a caller must not touch it afterwards.
Qt::DropAction exec_drag(QDrag *drag,
                         Qt::DropActions supported = Qt::CopyAction,
                         Qt::DropAction preferred = Qt::IgnoreAction);

// True while exec_drag() is running. InputRouter asks, because a mouse move
// during a drag is a drag move and not a hover, and the release is a drop
// and not a click.
bool drag_active();

// The router's side of it. Not application API: these are how the mouse
// reaches a drag, and they do nothing when no drag is up.
void drag_move_to(QWidget *under, const QPoint &local, const QPoint &screen);
void drag_drop_at(QWidget *under, const QPoint &local, const QPoint &screen);
void drag_cancel();

} // namespace Qtty
