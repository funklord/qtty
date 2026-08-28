// suite_placements -- section 5.7 drawPixmap funnel (section 16.3 semantics).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

static int fails = 0;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

namespace {
class PixWidget : public QWidget {
public:
	QPixmap pm;
	QPoint cell_pos{3, 2};
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.drawPixmap(cell_pos.x() * GridMetrics::cw(), cell_pos.y() * GridMetrics::ch(), pm);
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
	Qtty::render_once(w, buf, &pl);
	CHECK(pl.size() == 1, "one drawPixmap -> one placement");
	CHECK(!pl.isEmpty() && pl[0].cell_rect == QRect(3, 2, 10, 4), "placement at drawn cell rect");
	CHECK(!pl.isEmpty() && pl[0].key == quint64(sticker.cacheKey()), "identity is pixmap cacheKey");
	CHECK(buf.images.size() == 1, "placements travel with the frame buffer");

	quint64 key_before = pl.isEmpty() ? 0 : pl[0].key;
	w.cell_pos = QPoint(3, 0);
	QVector<Qtty::CellImage> pl2;
	Qtty::render_once(w, buf, &pl2);
	CHECK(!pl2.isEmpty() && pl2[0].cell_rect.topLeft() == QPoint(3, 0),
	      "placement tracks the new anchor (scroll)");
	CHECK(!pl2.isEmpty() && pl2[0].key == key_before, "same image -> same key (upload-once)");

	PixWidget tiny;
	tiny.pm = QPixmap(cw, ch);
	tiny.pm.fill(Qt::blue);
	tiny.setAttribute(Qt::WA_DontShowOnScreen);
	tiny.resize(GridMetrics::cells(10, 4));
	tiny.show();
	QCoreApplication::processEvents();
	Qtty::CellBuffer tb(10, 4);
	QVector<Qtty::CellImage> tp;
	Qtty::render_once(tiny, tb, &tp);
	CHECK(tp.isEmpty(), "1-cell pixmap substitutes a glyph, no placement");

	return fails;
}
