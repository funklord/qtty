// qtty/runtime.h -- L5/L6 runtime tier (sections 5.5, 5.6, 17.1):
// InputRouter, Compositor, FrameScheduler. Wired together by Qtty::exec();
// exposed for custom frame loops and tests.
#pragma once
#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <functional>
#include <QTimer>
#include <memory>
#include "cell.h"
#include "backend.h"

class QWidget;
class QApplication;

namespace Qtty {

// ---------------------------------------------------------------- InputRouter
// Owns everything Qt's platform layer would normally own (measured F3/F4):
// the shortcut table (synthetic keys never reach QShortcutMap), focus
// (no window ever activates), popup/modal routing, and popup attribute
// stamping via a global event filter.
class InputRouter : public QObject, public ITerminalEventSink {
public:
	explicit InputRouter(QWidget *window);
	~InputRouter() override;

	// ITerminalEventSink
	void on_key(const KeyEvent &) override;
	void on_mouse(const MouseEvent &) override;
	void on_paste(const QString &) override;
	void on_resize(QSize cells) override;
	void on_focus_change(bool) override;

	// Keys that quit the application (default: Ctrl-C, Ctrl-D).
	void set_quit_keys(const QVector<KeyEvent> &);

	// Visible popup stack in z-order, maintained by the stamping filter.
	QVector<QWidget *> popups() const;

	// The widget key events target right now (popup > modal > window focus).
	// Nothing outside the active modal is ever returned: section 8.3 requires
	// input outside activeModalWidget() to be dropped before dispatch, and
	// there is no window manager to enforce it.
	QWidget *key_target() const;

	// True for a layer the popup stack owns -- Qt::Popup or Qt::ToolTip. The
	// Compositor asks the same question, so it is answered in one place: a
	// window the router tracks must not also be drawn by the top-level walk.
	static bool is_popup_layer(const QWidget *);

	bool eventFilter(QObject *, QEvent *) override;   // popup stamping + tracking

	// Set by FrameScheduler: called after each handled input batch.
	std::function<void()> frame_requested;

private:
	bool match_shortcut(const KeyEvent &);
	// Alt-<letter> against the `&` markers in action text (section 17.2). A
	// separate matcher because a mnemonic is not a shortcut: it carries no
	// Qt::Key at all -- a terminal sends ESC then the letter -- and it opens
	// a menu where a shortcut triggers an action.
	bool match_mnemonic(const KeyEvent &);
	void deliver_key(QWidget *target, const KeyEvent &);
	// The widget tree input is allowed to reach: the active modal if there is
	// one, else the window (section 8.3).
	QWidget *input_scope() const;

	QWidget *win_;
	QVector<QPointer<QWidget>> popups_;
	QVector<KeyEvent> quit_keys_;
};

// ----------------------------------------------------------------- Compositor
// Walks QApplication::topLevelWidgets() in z-order into one CellBuffer
// (section 5.4 step 3), then stacks activeModalWidget() and the popups on top
// as section 8.1 asks -- explicitly, rather than trusting window flags. Layers
// are kept inside the terminal rectangle: an anchored one (menu, drop-down,
// tooltip) flips to the other side of its anchor, everything else slides.
// Placements and the cursor position (section 5.5) are collected as it goes.
class Compositor {
public:
	Compositor(QWidget *window, InputRouter *router);
	void compose(CellBuffer &out);                        // fills out + out.images
	std::optional<QPoint> cursor_cell() const;             // after compose()

private:
	QWidget *win_;
	InputRouter *router_;
	std::optional<QPoint> cursor_;
};

// ------------------------------------------------------------- FrameScheduler
// Coalesces frame production (section 5.4): renders through the Compositor at most
// once per interval, diffs, presents. Frames are requested by the router
// after input and by a global UpdateRequest watcher; a coarse idle tick
// catches timer-driven model updates.
class FrameScheduler : public QObject {
public:
	FrameScheduler(ITerminalBackend *backend, Compositor *compositor, QWidget *window);
	void request_frame();                                   // coalesced
	void render_now();                                      // immediate (initial frame)
	bool eventFilter(QObject *, QEvent *) override;        // UpdateRequest watcher

private:
	ITerminalBackend *backend_;
	Compositor *comp_;
	QWidget *win_;
	QTimer coalesce_;
	QTimer idle_;
	QElapsedTimer since_last_;
	std::unique_ptr<CellBuffer> prev_;
};

} // namespace Qtty
