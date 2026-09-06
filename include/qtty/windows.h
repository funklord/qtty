// qtty/windows.h -- several top-level windows in one terminal, as tabs.
//
// A desktop gives a program as many windows as it asks for and lets a window
// manager arrange them. A terminal is one rectangle. Before this, every
// visible non-modal top-level was drawn into that rectangle at its own
// position, so a second window OVERWROTE the first: measured, two windows
// each 30x5 left only the second one's contents on the screen and no way to
// reach the other.
//
// The arrangement is a tab bar, which is what a terminal already teaches its
// users -- a row of names, one view at a time. It appears only when there is
// more than one window, so a program with a single window pays nothing and
// looks exactly as it did.
//
// Modals and popups are unaffected and still stack on top of whichever
// window is current: a dialog belongs to the window that opened it, and
// putting it in the tab strip would make it something a user could tab
// AWAY from, which is the one thing a modal must not be.
#pragma once
#include <QVector>
#include <QPoint>

class QWidget;

namespace Qtty {

// The windows the tab bar is showing, in the order it shows them. Empty
// when there is only one -- the bar is not drawn then, and there is nothing
// to choose between.
QVector<QWidget *> window_tabs();

// Which one is drawn. Never null once compose() has run.
QWidget *current_window();
void set_current_window(QWidget *);

// For an application that wants to bind keys to this, which it must: a
// terminal user needs a keyboard route, and qtty deliberately binds no
// shortcut of its own -- every combination a tab switch conventionally uses
// is one some application already means something else by.
void next_window();
void previous_window();

// The window a press in the strip selects, or null for a press anywhere
// else. InputRouter asks before it does anything with the event, because a
// click on the strip belongs to no widget at all. Not application API.
QWidget *window_tab_at(const QPoint &cell);

} // namespace Qtty
