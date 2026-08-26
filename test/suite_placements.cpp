// suite_placements -- section 5.7 drawPixmap funnel (section 16.3 semantics).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

namespace {
class PixWidget : public QWidget {
public:
	QPixmap pm;
	QPoint cellPos{3, 2};
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.drawPixmap(cellPos.x() * GridMetrics::cw(), cellPos.y() * GridMetrics::ch(), pm);
	}
};
} // namespace

int suite_placements() {
	fails = 0;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	QPixmap sticker(10 * cw, 4 * ch);
	sticker.fill(Qt::red);

	PixWidget w;
	w.pm = sticker;
	w.setAttribute(Qt::WA_DontShowOnScreen);
	w.resize(GridMetrics::cells(40, 12));
	w.show();
	QCoreApplication::processEvents();

	Qtty::CellBuffer buf(40, 12);
	QVector<Qtty::CellImage> pl;
	Qtty::renderOnce(w, buf, &pl);
	CHECK(pl.size() == 1, "one drawPixmap -> one placement");
	CHECK(!pl.isEmpty() && pl[0].cellRect == QRect(3, 2, 10, 4), "placement at drawn cell rect");
	CHECK(!pl.isEmpty() && pl[0].key == sticker.cacheKey(), "identity is pixmap cacheKey");
	CHECK(buf.images.size() == 1, "placements travel with the frame buffer");

	quint64 keyBefore = pl.isEmpty() ? 0 : pl[0].key;
	w.cellPos = QPoint(3, 0);
	QVector<Qtty::CellImage> pl2;
	Qtty::renderOnce(w, buf, &pl2);
	CHECK(!pl2.isEmpty() && pl2[0].cellRect.topLeft() == QPoint(3, 0),
	      "placement tracks the new anchor (scroll)");
	CHECK(!pl2.isEmpty() && pl2[0].key == keyBefore, "same image -> same key (upload-once)");

	PixWidget tiny;
	tiny.pm = QPixmap(cw, ch);
	tiny.pm.fill(Qt::blue);
	tiny.setAttribute(Qt::WA_DontShowOnScreen);
	tiny.resize(GridMetrics::cells(10, 4));
	tiny.show();
	QCoreApplication::processEvents();
	Qtty::CellBuffer tb(10, 4);
	QVector<Qtty::CellImage> tp;
	Qtty::renderOnce(tiny, tb, &tp);
	CHECK(tp.isEmpty(), "1-cell pixmap substitutes a glyph, no placement");

	return fails;
}
