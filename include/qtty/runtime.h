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

	// Where the Compositor has scrolled the ROOT layer, in cells. Set by
	// compose(); a click carries a screen position and the root may not be
	// drawn at the screen's origin, so the two have to agree about the offset
	// or every press lands on the wrong widget.
	void set_root_scroll(QPoint cells);

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
	// The widget a press grabbed, held until the release (section 5.5). A
	// QPointer because a press can destroy its own target -- a button that
	// closes a dialog -- and the release then arrives for a widget that is
	// gone.
	QPointer<QWidget> grab_;
	// The widget the pointer is over, so that Enter and Leave can be sent
	// when it changes. The platform layer normally does this -- the same
	// reason the right-press context menu is synthesised here -- and without
	// it QWidget::underMouse() is permanently false and no application's
	// enterEvent() or leaveEvent() ever runs.
	QPointer<QWidget> hovered_;
	QPoint root_scroll_;
	void update_hover(QWidget *now, const QPoint &window_pos);
	// The last press, for recognising a double click. The platform layer
	// does this too, from QApplication::doubleClickInterval(); with no
	// platform, QWidget::mouseDoubleClickEvent() never ran anywhere.
	QElapsedTimer since_press_;
	QPoint last_press_cell_ = QPoint(-1, -1);
	int last_press_button_ = 0;
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
	// design.md section 7's small-terminal policy, first half. Public so the
	// behaviour can be exercised without a terminal; compose() calls it.
	void apply_priority(int cols, int rows);
	std::optional<QPoint> cursor_cell() const;             // after compose()

private:
	// Where the root layer is scrolled to, in cells. A layout refuses to
	// shrink below its minimum, so on a terminal smaller than that the window
	// keeps its size and the frame simply stops -- measured on a nine-cell
	// dialog in a six-row terminal, which showed six fields and neither the
	// last two nor the button that closes it. design.md section 7 names the
	// policy: drop the optional widgets, then scroll the root. This is the
	// second half, which needs no annotation from the application.
	QPoint scroll_;
	// The widgets THIS pass hid, so that growing the terminal back shows
	// exactly those and no others. A widget the application hid for its own
	// reasons must stay hidden, and it is not in here.
	QVector<QPointer<QWidget>> dropped_;
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
