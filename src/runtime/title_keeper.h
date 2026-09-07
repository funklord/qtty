// src/runtime/title_keeper.h -- the window's title, kept on the terminal.
//
// INTERNAL. Not shipped in include/qtty/, because an application needs
// nothing from it: exec() installs one and that is the whole interface.
//
// A CLASS RATHER THAN FOUR LINES INSIDE exec(). It was four lines inside
// exec() first, and nothing could reach them: exec() runs an event loop and
// does not return until the application quits, so a check that wanted to ask
// whether a title change reaches the backend had no way in. That is the
// project's most expensive recurring shape -- a correct function nothing can
// be shown to call -- and the fix is a seam rather than a cleverer test.
//
// It filters rather than connecting, because QWidget has NO windowTitleChanged
// signal: QWindow has one, a widget's window handle is null under
// WA_DontShowOnScreen, and QEvent::WindowTitleChange is the only report there
// is.
#pragma once

#include <qtty/backend.h>

#include <QEvent>
#include <QObject>
#include <QWidget>

namespace Qtty {

class TitleKeeper : public QObject {
public:
	// Takes the title the window ALREADY has, which is the ordinary case:
	// an application calls setWindowTitle() while building its window, long
	// before exec(), and a change event fires for a change. Installing the
	// filter and waiting would show a title only to an application that
	// renamed its window afterwards.
	TitleKeeper(QWidget &win, ITerminalBackend &backend)
	    : win_(win), backend_(backend) {
		win.installEventFilter(this);
		if (!win.windowTitle().isEmpty()) backend.set_title(win.windowTitle());
	}

protected:
	bool eventFilter(QObject *o, QEvent *e) override {
		// `o == &win_` because a filter installed on a widget sees that
		// widget only -- but this class is small enough to be installed on
		// something else by a later reader, and a title read off the wrong
		// object is a bug that looks like a rendering fault.
		if (e->type() == QEvent::WindowTitleChange && o == &win_)
			backend_.set_title(win_.windowTitle());
		return false;                            // observed, never consumed
	}

private:
	QWidget &win_;
	ITerminalBackend &backend_;
};

}  // namespace Qtty
