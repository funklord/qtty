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
// signal: QWindow has one, a widget is not its window, and
// QEvent::WindowTitleChange is the only report a widget makes.
//
// This used to say the window handle is null under WA_DontShowOnScreen, and
// that is not so -- measured under the offscreen platform, a widget with the
// attribute set still gets a QWindow on show(), WA_WState_Created and all.
// The reason stands without the claim: reaching a QWindow would mean waiting
// for one to exist, and the widget is what this class is given.
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
//
// IT RESOLVES `[*]` ITSELF, because nothing else in this runtime will.
// setWindowTitle("notes.txt[*]") with setWindowModified() is the standard Qt
// document-window idiom, and both halves of it were broken here:
//
//   - windowTitle() returns the caption the application SET, not the one a
//     desktop displays. Qt substitutes the placeholder in
//     qt_setWindowTitle_helperHelper() on the way to the platform window, so
//     a keeper that reads windowTitle() reads the unresolved string -- and
//     AnsiBackend::set_title() keeps every character >= 0x20, so `[`, `*`
//     and `]` all reach the terminal. Measured: a tab reading "notes.txt[*]"
//     for as long as the program runs.
//   - setWindowModified() sends NO WindowTitleChange, so the filter heard
//     nothing when the flag moved and the terminal never learned. Measured
//     under the offscreen platform this library pins: setWindowTitle()
//     delivered one WindowTitleChange, setWindowModified(true) delivered
//     zero more -- and one ModifiedChange, which is the report there is and
//     is what this filters now.
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
		publish_current();
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
		if (o != target_) return false;
		if (e->type() == QEvent::WindowTitleChange) publish_current();
		// The OTHER half of the document-window idiom, and the one that was
		// inert. setWindowModified() changes what a desktop displays without
		// changing the caption, so it sends ModifiedChange and no
		// WindowTitleChange -- two separate conditions rather than one
		// combined test, so that each can be broken on its own and the
		// sabotage spec can name which defect it is re-opening.
		else if (e->type() == QEvent::ModifiedChange) publish_current();
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
		publish_current();
	}

	// What the window is CALLED, as a desktop would show it: the caption
	// with its placeholder resolved against the window's modified flag.
	// Every publication goes through here, so there is one place that knows
	// the terminal shows a resolved title and none that can forget.
	void publish_current() {
		if (!target_) return;
		publish(resolve_modified_marker(target_->windowTitle(),
		                                target_->isWindowModified()));
	}

	// REPRODUCES Qt's qt_setWindowTitle_helperHelper(), in
	// qtbase/src/widgets/kernel/qwidget.cpp -- compare against that if Qt
	// ever changes it. Reimplemented rather than linked, and linking was
	// never on offer: Qt declares it `extern` at each use site inside
	// qtbase and exports it from nothing. Measured on the library this
	// tree builds against -- `nm -D --defined-only libQt6Widgets.so.6`
	// finds no such symbol -- so a consumer that named it would not link.
	//
	// The rule, which is Qt's and is stranger than it looks: find each run
	// of consecutive `[*]`; where the run length is ODD the LAST `[*]` in it
	// is the placeholder, replaced by `*` when the window is modified and
	// removed when it is not; afterwards `[*][*]` collapses to a literal
	// `[*]`. So a doubled placeholder is how an application writes a title
	// that really does contain the three characters.
	//
	// IT DOES NOT CONSULT QStyle::SH_TitleBar_ModifyNotification, and Qt
	// does. That hint exists so a platform showing modification some other
	// way can suppress the asterisk -- macOS puts a dot in the close button
	// and does not want a second mark in the caption. A terminal title has
	// no other way: there is no close button, no titlebar widget and no
	// dot, so honouring a style that answered 0 would put the modified flag
	// back where this found it, invisible. The hint also belongs to the
	// APPLICATION's style, which an application may replace for its widget
	// metrics, and a style chosen for how a scrollbar looks should not
	// decide whether a terminal tab can show unsaved work.
	//
	// Measured here rather than assumed: under GridStyle -- a QProxyStyle
	// over QFusionStyle -- the hint answers 1, so consulting it would change
	// nothing today and no check could tell the two implementations apart.
	// A branch nothing can distinguish is the vacuous pass this project
	// spends its time hunting, so it is not written.
	static QString resolve_modified_marker(const QString &title,
	                                       bool modified) {
		if (title.isEmpty()) return title;
		const QString mark = QStringLiteral("[*]");
		QString cap = title;
		int index = cap.indexOf(mark);
		while (index != -1) {
			index += 3;
			int run = 1;
			while (cap.indexOf(mark, index) == index) {
				++run;
				index += 3;
			}
			if (run % 2) {
				// lastIndexOf() searches BACKWARDS from its second
				// argument, so this is the final `[*]` of the run just
				// walked -- index is one past its closing bracket.
				const int last = cap.lastIndexOf(mark, index - 1);
				if (modified) cap.replace(last, 3, QStringLiteral("*"));
				else          cap.remove(last, 3);
			}
			index = cap.indexOf(mark, index);
		}
		cap.replace(QStringLiteral("[*][*]"), mark);
		return cap;
	}

	// One escape sequence per CHANGE, not per publication. What this
	// suppresses is a title the terminal is already showing: two windows
	// with the same name -- one document open twice -- and a re-point to the
	// window already being followed. A switch between two DIFFERENTLY named
	// windows does send bytes each way, and should: the title changed.
	//
	// It is also what makes the modified flag cost nothing on a title that
	// has no placeholder in it, which is worth knowing because Qt lets an
	// application do that and warns about it. setWindowModified() sends a
	// ModifiedChange either way, so the filter publishes either way -- and
	// a caption with nowhere to put the asterisk resolves to the string
	// already on the wire, so the comparison here drops it. Measured too:
	// Qt does not deduplicate the flag itself, and setWindowModified(false)
	// on an already-unmodified window sends a third ModifiedChange. Nothing
	// downstream of this line can tell.
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
