// suite_placements -- section 5.7 drawPixmap funnel (section 16.3 semantics).
#include <qtty/qtty.h>
#include "src/render/content_hash.h"
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
	// A ONE-CELL pixmap is a placement as well, which it was not. The engine
	// gated on "two cells or more in each direction" and substituted a glyph
	// below that, which is this funnel deciding a question it cannot answer:
	// whether a 1x1 image is an icon whose shape will not survive being made
	// of cells, or a picture. What can be answered is whether the terminal
	// draws pictures at all, and the backend already answers it -- so the
	// smallest image goes the same road as the largest and the tier that
	// knows decides.
	CHECK(tp.size() == 1 && tp[0].cell_rect.size() == QSize(1, 1),
	      "a one-cell pixmap is a placement too, of exactly one cell");

	// -- the key a PixelSurface is identified by (8.616) ------------------
	//
	// These are about content_hash() rather than about a placement, and they
	// are here because the key IS the placement's identity: the checks above
	// assert that an unchanged image keeps its key, and these assert the
	// discriminating half, that a changed one does not. A key collision is
	// not cosmetic -- AnsiBackend::wire_id_for() returns the wire id it
	// already holds with minted = false, so nothing is re-uploaded and the
	// terminal goes on showing whichever picture claimed the key first.
	{
		QImage a(64, 16, QImage::Format_ARGB32_Premultiplied);
		a.fill(Qt::transparent);
		for (int y = 0; y < a.height(); ++y) {
			QRgb *row = reinterpret_cast<QRgb *>(a.scanLine(y));
			// Half gradient, half flat: a uniform run is where a hash that
			// only accumulates collapses, so the fixture has to contain one.
			for (int x = 0; x < a.width(); ++x)
				row[x] = (y < a.height() / 2)
				             ? qRgba(x * 3, y * 7, 90, 255)
				             : qRgba(20, 20, 20, 255);
		}
		const size_t n = size_t(a.sizeInBytes());
		const quint64 base = Qtty::content_hash(a.constBits(), n);

		QImage same = a.copy();
		CHECK(Qtty::content_hash(same.constBits(), n) == base,
		      "the same pixels give the same key, which is what upload-once"
		      " rests on");

		// One flipped bit, in the flat region where a change is hardest to
		// see: the key has to move or the frame is stale for ever.
		QImage one = a.copy();
		reinterpret_cast<QRgb *>(one.scanLine(one.height() - 1))[5] =
		    qRgba(20, 20, 21, 255);
		CHECK(Qtty::content_hash(one.constBits(), n) != base,
		      "one pixel changed by one level gives a different key");

		// THE REGRESSION, and it is a Qt 5 one: qHashBits there is
		// `h = 31 * h + byte`, so a byte raised by 1 beside a byte lowered
		// by 31 leaves the hash exactly where it was -- 31 * 31^k is
		// 31^(k+1). In one ARGB32 pixel, whose bytes are B G R A, that is
		// green up one and red down 31: an ordinary colour step. Measured
		// against Qt 5.15.15 on this exact fixture, the old hash gave
		// 00000000ed98b400 before and after, and this one 36127850b1565d71
		// then 1358a68ca05f5a66. Qt 6's hash separates them too, so the
		// check passes trivially there and earns its place on the other arm.
		// x = 20 rather than the first pixel to hand, because the fixture's
		// red is x * 3 and the step needs red >= 31 to be a step of 31 at
		// all: at x = 9 red is 27, qRgba clamps the negative, and the check
		// passed against BOTH hashes while appearing to test the collision.
		// The premise is asserted below rather than trusted, which is what
		// caught it.
		QImage lattice = a.copy();
		QRgb *lrow = reinterpret_cast<QRgb *>(lattice.scanLine(2));
		const QRgb was = lrow[20];
		lrow[20] = qRgba(qRed(was) - 31, qGreen(was) + 1, qBlue(was),
		                 qAlpha(was));
		const quint64 moved = Qtty::content_hash(lattice.constBits(), n);
		CHECK(moved != base && qRed(was) >= 31 && qGreen(was) <= 254,
		      "a pixel stepping green up one and red down 31 changes the key,"
		      " where the polynomial hash this replaced could not see it");

		// A uniform run must not be free: two buffers of identical bytes
		// differing only in how many must differ, or a picture that grew by
		// one row of background keeps its key.
		QByteArray flat(4096, '\x00');
		const quint64 h_long = Qtty::content_hash(flat.constData(), 4096);
		const quint64 h_short = Qtty::content_hash(flat.constData(), 4088);
		CHECK(h_long != h_short,
		      "a run of identical bytes does not collapse: the same bytes at"
		      " two lengths give two keys");
	}

	return fails;
}
