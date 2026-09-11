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
//
// IT FOLLOWS THE CURRENT WINDOW, and did not. A terminal shows one title
// where a window manager shows the active window's, so with two windows it
// has to name the one the user is in -- and this was bound to the window
// exec() handed it for its whole life. Measured: switch to a second window
// titled "Log -- live" and the terminal still said "Editor -- notes.txt", so
// a tabbed terminal named the window nobody was looking at. Same family as
// 8.107, and found by asking what else activation carries.
//
// The filter MOVES rather than being installed on qApp. An application-wide
// filter sees every event in the process to answer about one, and following
// the window is what the class means anyway.
#pragma once

#include <qtty/backend.h>

#include <QEvent>
#include <QObject>
#include <QPointer>
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
	    : target_(&win), backend_(backend) {
		win.installEventFilter(this);
		publish(win.windowTitle());
		// The window this keeper watches is NOT read from
		// Qtty::current_window() here, deliberately. exec() installs the
		// keeper before the first compose, so there is no current window
		// yet -- and in a suite there is one, left by an earlier section,
		// which would have this publish a title belonging to a window the
		// application under test does not own.
		s_live = this;
	}

	~TitleKeeper() override { if (s_live == this) s_live = nullptr; }

	// Watch this window instead, and say what it is called. Called by the
	// window switch (Qtty::set_current_window() and its neighbours), which
	// is this runtime's activation.
	static void follow(QWidget *w) {
		if (s_live && w) s_live->point_at(w);
	}

protected:
	bool eventFilter(QObject *o, QEvent *e) override {
		// `o == target_` because a filter installed on a widget sees that
		// widget only -- but this class is small enough to be installed on
		// something else by a later reader, and a title read off the wrong
		// object is a bug that looks like a rendering fault.
		if (e->type() == QEvent::WindowTitleChange && o == target_)
			publish(target_->windowTitle());
		return false;                            // observed, never consumed
	}

private:
	void point_at(QWidget *w) {
		if (w == target_) return;
		// Housekeeping, NOT the behaviour, and a sabotage run said so: with
		// this line disabled the window left behind is still unheard,
		// because the filter's own guard rejects an event from anything but
		// target_. What it buys is not leaving a filter on every window
		// that has ever been current -- each one an extra call for every
		// event that window sees. The guard is what the checks rest on.
		if (target_) target_->removeEventFilter(this);
		target_ = w;
		w->installEventFilter(this);
		publish(w->windowTitle());
	}

	// One escape sequence per CHANGE, not per publication. What this
	// suppresses is a title the terminal is already showing: two windows
	// with the same name -- one document open twice -- and a re-point to the
	// window already being followed. A switch between two DIFFERENTLY named
	// windows does send bytes each way, and should: the title changed.
	void publish(const QString &title) {
		if (title.isEmpty() || title == last_) return;
		last_ = title;
		backend_.set_title(title);
	}

	QPointer<QWidget> target_;
	ITerminalBackend &backend_;
	QString last_;
	// The keeper the window switch talks to. exec() has exactly one, on the
	// stack; a suite may build several in sequence, so it is the one alive
	// rather than the first ever made -- and it clears itself on the way
	// out, because a switch reaching a destroyed keeper is a dangling call
	// rather than a stale title.
	static inline TitleKeeper *s_live = nullptr;
};

}  // namespace Qtty
