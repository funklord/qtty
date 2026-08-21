// src/backends/ansi/ansibackend.h — built-in ITerminalBackend over a raw
// ANSI tty (§5.1). Escape decoding lives here (the backend side of the seam);
// input is pushed to the sink, never polled. Placements render as the
// NoGraphics mosaic tier (§5.7); richer tiers land with the kitty/sixel
// encoders (§17.3).
#pragma once
#include <QObject>
#include <QByteArray>
#include <termios.h>
#include "qtty/backend.h"

class QSocketNotifier;

namespace Qtty {

class AnsiBackend : public QObject, public ITerminalBackend {
public:
    AnsiBackend();
    ~AnsiBackend() override;

    Capabilities capabilities() const override;
    QSize size() const override;
    void present(const CellBuffer &frame, const QRegion &damage) override;
    void setCursor(std::optional<QPoint> cell, CursorShape shape) override;
    void setEventSink(ITerminalEventSink *s) override { sink_ = s; }
    void suspend() override;
    void resume() override;

private:
    void readInput();
    bool decodeOne();                    // one key from pending_ → sink

    ITerminalEventSink *sink_ = nullptr;
    QSocketNotifier *notifier_ = nullptr;
    QByteArray pending_;
    QSize cells_;
    bool rawOk_ = false;
    bool active_ = false;
    termios saved_{};
};

} // namespace Qtty
