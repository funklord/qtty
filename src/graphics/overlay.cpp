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
	delete guiTwin_;
}

void Overlay::setImage(const QImage &rgba) { img_ = rgba; syncGuiTwin(); }
void Overlay::setRect(const QRectF &cellRect) { rect_ = cellRect; syncGuiTwin(); }
void Overlay::setOpacity(qreal o) { opacity_ = qBound(0.0, o, 1.0); syncGuiTwin(); }
void Overlay::setZ(int z) { z_ = z; }
void Overlay::show() { visible_ = true; syncGuiTwin(); }
void Overlay::hide() { visible_ = false; syncGuiTwin(); }

QImage Overlay::image() const {
	if (opacity_ >= 1.0) return img_;
	QImage out = img_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	QPainter p(&out);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.fillRect(out.rect(), QColor(0, 0, 0, int(opacity_ * 255)));
	p.end();
	return out;
}

QVector<Overlay *> Overlay::visibleOverlays() {
	QVector<Overlay *> out;
	for (Overlay *o : registry())
		if (o->isVisible() && !o->image().isNull()) out.append(o);
	std::sort(out.begin(), out.end(),
	          [](Overlay *a, Overlay *b) { return a->z() < b->z(); });
	return out;
}

void Overlay::syncGuiTwin() {
	if (isTuiActive()) return;                 // runtime composites in TUI mode
	if (!visible_ || img_.isNull()) {
		if (guiTwin_) guiTwin_->hide();
		return;
	}
	auto *tw = static_cast<TwinWidget *>(guiTwin_);
	if (!tw) {
		tw = new TwinWidget;
		tw->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
		                   | Qt::Tool | Qt::WindowTransparentForInput);
		tw->setAttribute(Qt::WA_TranslucentBackground);
		guiTwin_ = tw;
	}
	tw->img = image();
	QWidget *base = QApplication::activeWindow();
	if (!base) {
		const auto tls = QApplication::topLevelWidgets();
		for (QWidget *w : tls) if (w->isVisible()) { base = w; break; }
	}
	if (base) {
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QRect target = rect_.isNull()
		    ? base->geometry()
		    : QRect(base->geometry().topLeft()
		                + QPoint(int(rect_.x() * cw), int(rect_.y() * ch)),
		            QSize(int(rect_.width() * cw), int(rect_.height() * ch)));
		tw->setGeometry(target);
	}
	tw->show();
}

} // namespace Qtty
