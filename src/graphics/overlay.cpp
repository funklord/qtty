// src/graphics/overlay.cpp -- Overlay registry + GUI twin (section 5.7).
#include "qtty/overlay.h"
#include "qtty/grid.h"
#include "qtty/application.h"
#include <QtWidgets>
#include <algorithm>

namespace Qtty {

static QVector<Overlay *> &registry() {
	static QVector<Overlay *> r;
	return r;
}

namespace {
class TwinWidget : public QWidget {           // GUI-mode rendering
public:
	QImage img;
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.drawImage(rect(), img);
	}
};
} // namespace

Overlay::Overlay(QObject *parent) : QObject(parent) { registry().append(this); }

Overlay::~Overlay() {
	registry().removeAll(this);
	delete gui_twin_;
}

void Overlay::set_image(const QImage &rgba) { img_ = rgba; sync_gui_twin(); }
// The sentinel for "the whole terminal" is a DEFAULT-CONSTRUCTED rect, not
// any empty one. It used to be QRectF::isNull(), which ignores position, so
// an application whose arithmetic produced 0x0 at (5,5) got a sheet over the
// entire terminal, while the same arithmetic producing 8x0 got silence --
// one pixel apart, and neither saying anything. Passing QRectF() still means
// the whole terminal, because that is how a caller resets it and is what the
// header has always documented.
//
// The empty-but-positioned case draws nothing, which is the honest reading,
// and says so once. qtty installs a message handler that keeps a warning off
// the drawn frame, so this is safe to emit from a library.
void Overlay::set_rect(const QRectF &cell_rect) {
	rect_ = cell_rect;
	whole_ = cell_rect == QRectF();
	if (!whole_ && cell_rect.isEmpty())
		qWarning("qtty: overlay rect %gx%g at (%g,%g) is empty, so nothing "
		         "will be drawn", cell_rect.width(), cell_rect.height(),
		         cell_rect.x(), cell_rect.y());
	sync_gui_twin();
}
void Overlay::set_opacity(qreal o) { opacity_ = qBound(0.0, o, 1.0); sync_gui_twin(); }
void Overlay::set_z(int z) { z_ = z; }
void Overlay::show() { visible_ = true; sync_gui_twin(); }
void Overlay::hide() { visible_ = false; sync_gui_twin(); }

QImage Overlay::image() const {
	if (opacity_ >= 1.0) return img_;
	QImage out = img_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	QPainter p(&out);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.fillRect(out.rect(), QColor(0, 0, 0, int(opacity_ * 255)));
	p.end();
	return out;
}

QVector<Overlay *> Overlay::visible_overlays() {
	QVector<Overlay *> out;
	for (Overlay *o : registry())
		if (o->isVisible() && !o->image().isNull()) out.append(o);
	std::sort(out.begin(), out.end(),
	          [](Overlay *a, Overlay *b) { return a->z() < b->z(); });
	return out;
}

void Overlay::sync_gui_twin() {
	if (is_tui_active()) return;                 // runtime composites in TUI mode
	if (!visible_ || img_.isNull()) {
		if (gui_twin_) gui_twin_->hide();
		return;
	}
	auto *tw = static_cast<TwinWidget *>(gui_twin_);
	if (!tw) {
		tw = new TwinWidget;
		// Named so GridGuard can let it past. It is a top-level widget qtty
		// builds for the GUI path and sizes in PIXELS deliberately -- the
		// grid does not govern it -- and the application never constructs it
		// and cannot size it, which is exactly the guard's own test for what
		// it must not report.
		tw->setObjectName(QStringLiteral("qtty_overlay_twin"));
		tw->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
		                   | Qt::Tool | Qt::WindowTransparentForInput);
		tw->setAttribute(Qt::WA_TranslucentBackground);
		gui_twin_ = tw;
	}
	tw->img = image();
	QWidget *base = QApplication::activeWindow();
	if (!base) {
		const auto tls = QApplication::topLevelWidgets();
		for (QWidget *w : tls) if (w->isVisible()) { base = w; break; }
	}
	if (base) {
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QRect target = whole_
		    ? base->geometry()
		    : QRect(base->geometry().topLeft()
		                + QPoint(int(rect_.x() * cw), int(rect_.y() * ch)),
		            QSize(int(rect_.width() * cw), int(rect_.height() * ch)));
		tw->setGeometry(target);
	}
	tw->show();
}

} // namespace Qtty
