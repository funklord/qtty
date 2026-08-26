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
	void onKey(const KeyEvent &) override;
	void onMouse(const MouseEvent &) override;
	void onPaste(const QString &) override;
	void onResize(QSize cells) override;
	void onFocusChange(bool) override;

	// Keys that quit the application (default: Ctrl-C, Ctrl-D).
	void setQuitKeys(const QVector<KeyEvent> &);

	// Visible popup stack in z-order, maintained by the stamping filter.
	QVector<QWidget *> popups() const;

	// The widget key events target right now (popup > modal > window focus).
	// Nothing outside the active modal is ever returned: section 8.3 requires
	// input outside activeModalWidget() to be dropped before dispatch, and
	// there is no window manager to enforce it.
	QWidget *keyTarget() const;

	// True for a layer the popup stack owns -- Qt::Popup or Qt::ToolTip. The
	// Compositor asks the same question, so it is answered in one place: a
	// window the router tracks must not also be drawn by the top-level walk.
	static bool is_popup_layer(const QWidget *);

	bool eventFilter(QObject *, QEvent *) override;   // popup stamping + tracking

	// Set by FrameScheduler: called after each handled input batch.
	std::function<void()> frameRequested;

private:
	bool matchShortcut(const KeyEvent &);
	void deliverKey(QWidget *target, const KeyEvent &);
	// The widget tree input is allowed to reach: the active modal if there is
	// one, else the window (section 8.3).
	QWidget *input_scope() const;

	QWidget *win_;
	QVector<QPointer<QWidget>> popups_;
	QVector<KeyEvent> quitKeys_;
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
	std::optional<QPoint> cursorCell() const;             // after compose()

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
	void requestFrame();                                   // coalesced
	void renderNow();                                      // immediate (initial frame)
	bool eventFilter(QObject *, QEvent *) override;        // UpdateRequest watcher

private:
	ITerminalBackend *backend_;
	Compositor *comp_;
	QWidget *win_;
	QTimer coalesce_;
	QTimer idle_;
	QElapsedTimer sinceLast_;
	std::unique_ptr<CellBuffer> prev_;
};

} // namespace Qtty
