// src/backends/null/nullbackend.h — CI backend (§9): captures frames so
// snapshot tests run with no tty attached.
#pragma once
#include "qtty/backend.h"

namespace qtty {

class NullBackend : public ITerminalBackend {
public:
    explicit NullBackend(QSize cells = {80, 24}) : size_(cells) {}
    Capabilities capabilities() const override { return {}; }
    QSize size() const override { return size_; }
    void present(const CellBuffer &frame, const QRegion &) override {
        lastFrame_ = frame.toText();
        ++frames_;
    }
    void setCursor(std::optional<QPoint> cell, CursorShape) override { cursor_ = cell; }
    void setEventSink(ITerminalEventSink *s) override { sink_ = s; }
    void suspend() override {}
    void resume() override {}

    // test accessors
    QString lastFrame() const { return lastFrame_; }
    int frameCount() const { return frames_; }
    std::optional<QPoint> cursor() const { return cursor_; }
    ITerminalEventSink *sink() const { return sink_; }

private:
    QSize size_;
    QString lastFrame_;
    int frames_ = 0;
    std::optional<QPoint> cursor_;
    ITerminalEventSink *sink_ = nullptr;
};

} // namespace qtty
