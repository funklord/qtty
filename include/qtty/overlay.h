// qtty/overlay.h -- app-facing full/partial-terminal pixel overlays (section 5.7).
// One object, target-independent: in the TUI build the runtime composites it
// per the terminal's graphics tier; in the GUI build it renders through an
// ordinary translucent top-level widget.
#pragma once
#include <QObject>
#include <QImage>
#include <QRectF>
#include <QVector>

namespace Qtty {

class Overlay : public QObject {
public:
	explicit Overlay(QObject *parent = nullptr);
	~Overlay() override;

	void set_image(const QImage &rgba);
	void set_rect(const QRectF &cell_rect);      // in cells; default: whole terminal
	// Multiplied into the image's alpha, and CLAMPED to 0..1 -- a value
	// outside that range is a caller's arithmetic having gone wrong, and
	// the alternative is an alpha byte that wraps.
	void set_opacity(qreal);
	void set_z(int);                            // stacking among overlays
	void show();
	void hide();

	QImage image() const;                      // opacity applied
	QRectF cell_rect() const { return rect_; }
	// Whether this overlay covers the whole terminal, which is the state a
	// freshly constructed one is in. Asked rather than inferred from the
	// rectangle: QRectF::isNull() is width and height both zero and says
	// nothing about position, so a rect COMPUTED as 0x0 at (5,5) used to be
	// read as "the whole terminal" -- the largest interpretation available
	// of a value that had most likely come from arithmetic that went wrong.
	bool covers_terminal() const { return whole_; }
	// What was set, before it reaches the image. `image()` applies it, so
	// an application fading an overlay cannot recover the current value
	// from there -- and this was the one property of this class that could
	// be written and not read back, which also left the clamp above
	// invisible to everything including a check.
	qreal opacity() const { return opacity_; }
	int z() const { return z_; }
	bool isVisible() const { return visible_; }

	// Runtime access: all visible overlays, z-ordered.
	static QVector<Overlay *> visible_overlays();

private:
	void sync_gui_twin();
	QImage img_;
	// THE OPACITY APPLIED, REMEMBERED. image() converted the format and
	// painted the whole image on every call, and compose asks once a frame:
	// measured on a 200x60 terminal, a translucent full-screen overlay cost
	// 4.94 and 4.87 ms a frame against 4.08 and 4.21 for an opaque one, so
	// about 0.75 ms of every frame was spent re-deriving a picture from
	// inputs that had not changed.
	//
	// KEYED ON WHAT THE RESULT DEPENDS ON, which is the whole of it: the
	// source image's identity and the opacity. Both are invalidated where
	// they are set rather than compared on the way out, so a cache cannot
	// outlive either -- a key that omits one of its inputs is the fault the
	// 2026-10-26 cache sweep went looking for and did not find here. 8.598.
	mutable QImage faded_;
	mutable qint64 faded_key_ = 0;
	mutable qreal faded_opacity_ = -1.0;
	QRectF rect_;                              // see covers_terminal()
	bool whole_ = true;                        // until set_rect() says otherwise
	qreal opacity_ = 1.0;
	int z_ = 0;
	bool visible_ = false;
	QWidget *gui_twin_ = nullptr;
};

} // namespace Qtty
