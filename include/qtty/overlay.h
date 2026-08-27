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
	void set_opacity(qreal);                    // multiplied into image alpha
	void set_z(int);                            // stacking among overlays
	void show();
	void hide();

	QImage image() const;                      // opacity applied
	QRectF cell_rect() const { return rect_; }
	int z() const { return z_; }
	bool isVisible() const { return visible_; }

	// Runtime access: all visible overlays, z-ordered.
	static QVector<Overlay *> visible_overlays();

private:
	void sync_gui_twin();
	QImage img_;
	QRectF rect_;                              // isNull => full terminal
	qreal opacity_ = 1.0;
	int z_ = 0;
	bool visible_ = false;
	QWidget *gui_twin_ = nullptr;
};

} // namespace Qtty
