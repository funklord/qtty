// src/backend/null/null_backend.h -- CI backend (section 9): captures frames so
// snapshot tests run with no tty attached.
#pragma once
#include "qtty/backend.h"

namespace Qtty {

class NullBackend : public ITerminalBackend {
public:
	explicit NullBackend(QSize cells = {80, 24}) : size_(cells) {}
	Capabilities capabilities() const override { return {}; }
	QSize size() const override { return size_; }
	void present(const CellBuffer &frame, const QRegion &) override {
		last_frame_ = frame.to_text();
		++frames_;
	}
	void set_cursor(std::optional<QPoint> cell, CursorShape) override { cursor_ = cell; }
	void set_event_sink(ITerminalEventSink *s) override { sink_ = s; }
	void suspend() override {}
	void resume() override {}

	// test accessors
	QString last_frame() const { return last_frame_; }
	int frame_count() const { return frames_; }
	std::optional<QPoint> cursor() const { return cursor_; }
	ITerminalEventSink *sink() const { return sink_; }

private:
	QSize size_;
	QString last_frame_;
	int frames_ = 0;
	std::optional<QPoint> cursor_;
	ITerminalEventSink *sink_ = nullptr;
};

} // namespace Qtty
