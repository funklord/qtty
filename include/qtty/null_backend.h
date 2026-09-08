// qtty/null_backend.h -- the harness backend (section 9): captures frames so
// snapshot tests run with no tty attached.
//
// Public because it is half of what exec(app, win, backend) is for. That
// overload is the seam a test drives, and it was reachable only from inside
// this tree while the README advertised "deterministic snapshot testing with
// no tty (NullBackend)" to everybody else -- a documented capability an
// adopter could not reach, which is the same shape as a method with no
// caller.
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
	// The SHAPE as well as the cell. It was dropped, so an adopter's
	// snapshot test could read where the cursor is and not what it was
	// asked to look like -- the same half-recorded seam the title had
	// before 8.44, and found by the same question asked of a different
	// enum.
	void set_cursor(std::optional<QPoint> cell, CursorShape shape) override {
		cursor_ = cell;
		shape_ = shape;
	}
	// Recorded like the frame and the cursor, and for the same reason: this
	// is the backend an adopter's snapshot test drives, and a title is
	// something their application sets and would want to assert on. It was
	// the one thing the runtime hands a backend that this dropped.
	//
	// `capabilities().title` stays false, which is not a contradiction: this
	// backend has no terminal, so nothing will DISPLAY the title. Recording
	// it is what a harness does, exactly as it records frames while
	// reporting no graphics.
	void set_title(const QString &t) override { title_ = t; ++titles_; }
	void set_event_sink(ITerminalEventSink *s) override { sink_ = s; }
	void suspend() override {}
	void resume() override {}

	// test accessors
	QString last_frame() const { return last_frame_; }
	int frame_count() const { return frames_; }
	std::optional<QPoint> cursor() const { return cursor_; }
	CursorShape cursor_shape() const { return shape_; }
	QString last_title() const { return title_; }
	int title_count() const { return titles_; }
	ITerminalEventSink *sink() const { return sink_; }

private:
	QSize size_;
	QString last_frame_;
	int frames_ = 0;
	std::optional<QPoint> cursor_;
	// Hidden until told otherwise, which is what a backend with no frame
	// yet has: a cursor nobody has placed is not a Block at 0,0.
	CursorShape shape_ = CursorShape::Hidden;
	QString title_;
	int titles_ = 0;
	ITerminalEventSink *sink_ = nullptr;
};

} // namespace Qtty
