// src/backends/ansi/ansiruntime.h — built-in ANSI terminal runtime.
//
// Drives the controlling tty directly: alt screen, raw mode, 256-colour
// output, placement mosaic (the NoGraphics tier of §5.7), input through
// QSocketNotifier into the Qt event loop, cursor via ImCursorRectangle.
//
// Phase 2 rehosts this behind ITerminalBackend so the legacy backends and
// this one are interchangeable; its behaviour is already the §5 design.
#pragma once
#include <QObject>
#include <QByteArray>
#include <termios.h>

class QWidget;
class QSocketNotifier;

namespace qtty {
namespace detail {

class AnsiRuntime : public QObject {
public:
    explicit AnsiRuntime(QWidget *win);
    ~AnsiRuntime() override;

private:
    void readInput();
    bool step();                 // consume one key from pending_
    void render();

    QWidget *win_;
    int cols_, rows_;
    bool rawOk_ = false;
    bool quit_ = false;
    termios saved_{};
    QSocketNotifier *notifier_ = nullptr;
    QByteArray pending_;
};

} // namespace detail
} // namespace qtty
