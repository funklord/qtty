// qtty/runtime.h — L5/L6 runtime tier (§5.5, §5.6, §17.1):
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
    QWidget *keyTarget() const;

    bool eventFilter(QObject *, QEvent *) override;   // popup stamping + tracking

    // Set by FrameScheduler: called after each handled input batch.
    std::function<void()> frameRequested;

private:
    bool matchShortcut(const KeyEvent &);
    void deliverKey(QWidget *target, const KeyEvent &);

    QWidget *win_;
    QVector<QPointer<QWidget>> popups_;
    QVector<KeyEvent> quitKeys_;
};

// ----------------------------------------------------------------- Compositor
// Walks window + popup stack in z-order into one CellBuffer (§5.4 step 3),
// clamping popups to the terminal rectangle (§8.1) and collecting placements
// and the cursor position (§5.5).
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
// Coalesces frame production (§5.4): renders through the Compositor at most
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
