// suite_render -- Gate-1 regression as a snapshot (section 9).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

namespace {

// A widget that draws itself in cells rather than in pixels. Inherits QWidget
// first and the interface second, which is the only legal order: two QObject
// bases are illegal, which is why ICellPainted is not one.
class CellDrawn : public QWidget, public Qtty::ICellPainted {
public:
	using QWidget::QWidget;
	mutable int calls = 0;
	mutable QRect got;
	void paint_cells(Qtty::CellBuffer &buffer, const QRect &cells) const override {
		++calls;
		got = cells;
		buffer.text(cells.left(), cells.top(), QStringLiteral("CELLS"));
	}
	// Ordinary painting, which must NOT happen while a cell render is running.
	// If it does it lands underneath, and a cell the interface left alone
	// shows Channel B output through it.
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.drawText(rect(), Qt::AlignLeft, QStringLiteral("PIXELS"));
	}
};

bool buffer_has(const Qtty::CellBuffer &b, const QString &needle) {
	return b.to_text().contains(needle);
}

// A widget claiming both interfaces. PixelSurface is tested first in the
// filter, so this is what says which one such a class actually gets --
// qtty/paint.h tells applications not to write one, and this is what would
// notice the consequence changing.
class Both : public Qtty::PixelSurface, public Qtty::ICellPainted {
public:
	using Qtty::PixelSurface::PixelSurface;
	mutable int cell_calls = 0;
	void paint_cells(Qtty::CellBuffer &buffer, const QRect &cells) const override {
		++cell_calls;
		buffer.text(cells.left(), cells.top(), QStringLiteral("BOTH"));
	}
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.fillRect(rect(), Qt::red);
	}
};

} // namespace

int suite_render(bool record) {
	QDialog dlg;
	auto *v = new QVBoxLayout(&dlg);
	auto *chk = new QCheckBox("Enable telemetry", &dlg);
	chk->setChecked(true);
	v->addWidget(chk);
	auto *h = new QHBoxLayout;
	auto *r1 = new QRadioButton("Daily", &dlg);
	auto *r2 = new QRadioButton("Weekly", &dlg);
	r2->setChecked(true);
	h->addWidget(r1); h->addWidget(r2);
	v->addLayout(h);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	// The stretch is not decoration. This dialog is 12 cells tall and its
	// content needs 9, and QBoxLayout hands the slack to the items when
	// nothing can absorb it -- in shares that are not cell multiples, which
	// puts every row off the grid (measured: y = 33, 85, 137 against ch=19,
	// and a row at y=33 renders into row 1 rather than row 2). A stretch
	// gives the slack somewhere to go. GridGuard reports the difference.
	v->addStretch();
	v->addWidget(bb);
	dlg.setAttribute(Qt::WA_DontShowOnScreen);
	dlg.resize(GridMetrics::cells(48, 12));
	dlg.show();
	QCoreApplication::processEvents();

	const QString got = Qtty::test::snapshot_of(dlg, 52, 14);
	int r = Qtty::test::check_snapshot(QStringLiteral(QTTY_SOURCE_DIR),
	                                  QStringLiteral("prefs_dialog"), got, record);
	if (!r && !record) printf("PASS: snapshot matches\n");

	// An image too small to be a picture is substituted by a glyph, and the
	// substitution has to cover the cells the image OCCUPIES. For the 1x1 icon
	// that motivated the rule those are the same thing; for anything wider
	// they are not, and the difference is stale cells -- a picture covering
	// eight of them marking one and leaving seven showing whatever was
	// underneath.
	//
	// Measured on a tab being dragged: Qt moves a movable tab by grabbing it
	// into a pixmap 82x19 px here, which is 8 cells by 1, so it fails "two
	// cells in each direction" and takes this branch. Driven at the engine
	// rather than through QTabBar, because the widget doing the grabbing is
	// private to Qt and the rule under test is the engine's.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		Qtty::CellBuffer buf(12, 2);
		buf.text(0, 0, QStringLiteral("aaaaaaaaaaaa"));
		QPixmap wide(cw * 8, ch);
		wide.fill(Qt::red);
		QPixmap tiny(cw, ch);
		tiny.fill(Qt::red);
		int wide_placements = 0, tiny_placements = 0;
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			p.drawPixmap(QRect(0, 0, cw * 8, ch), wide);
			p.end();
			wide_placements = int(dev.placements.size());
		}
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			p.drawPixmap(QRect(0, ch, cw, ch), tiny);
			p.end();
			tiny_placements = int(dev.placements.size());
		}
		int covered = 0;
		for (int x = 0; x < 12; ++x)
			if (buf.at(x, 0).ch == QStringLiteral("▒")) ++covered;
		// The pair: the wide one covers its eight cells and stops there, so a
		// substitution that filled the row would fail this as surely as one
		// that marked a single cell.
		if (covered == 8 && buf.at(8, 0).ch == QStringLiteral("a")
		    && wide_placements == 0)
			printf("PASS: an image too small to be a picture covers the cells "
			       "it occupies\n");
		else {
			printf("FAIL: an image too small to be a picture covers the cells "
			       "it occupies\n      condition: %d of 8 cells marked, cell 8 is "
			       "'%s', %d placement(s)\n",
			       covered, qPrintable(buf.at(8, 0).ch), wide_placements);
			++r;
		}
		if (buf.at(0, 1).ch == QStringLiteral("▒") && buf.at(1, 1).ch != QStringLiteral("▒")
		    && tiny_placements == 0)
			printf("PASS: and a one-cell icon still marks one cell, with no placement\n");
		else {
			printf("FAIL: and a one-cell icon still marks one cell, with no placement\n"
			       "      condition: %d placement(s)\n", tiny_placements);
			++r;
		}
	}

	// The clip, which design.md section 432 lists among the four things
	// updateState() carries and which was the one of the four not implemented.
	// An application's own setClipRect() was ignored outright: a painter told
	// to keep inside four cells filled twenty.
	//
	// Asserted as a pair against the same fill unclipped, because "few cells
	// filled" is also what an engine that stopped filling would produce.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		auto fill_with = [&](bool clipped, Qtty::CellBuffer &b) {
			Qtty::CellPaintDevice dev(b);
			QPainter p(&dev);
			if (clipped) p.setClipRect(QRect(0, 0, cw * 4, ch));
			p.fillRect(QRect(0, 0, cw * 20, ch), Qt::red);
			p.end();
		};
		auto filled = [](const Qtty::CellBuffer &b) {
			int n = 0;
			for (int x = 0; x < b.cols(); ++x)
				if (b.at(x, 0).bg.kind() != Qtty::Color::Default) ++n;
			return n;
		};
		Qtty::CellBuffer clipped(20, 2), open(20, 2);
		fill_with(true, clipped);
		fill_with(false, open);
		// Five, not four: the clip rounds OUTWARD, so a cell it covers in part
		// is admitted whole. Asserted as a range rather than a number because
		// the rule is "no more than a cell of slack", not a magic 5.
		const int c = filled(clipped), o = filled(open);
		if (c >= 4 && c <= 5 && o == 20)
			printf("PASS: a clip trims what is drawn, and no clip trims nothing\n");
		else {
			printf("FAIL: a clip trims what is drawn, and no clip trims nothing\n"
			       "      condition: %d cells clipped, %d unclipped\n", c, o);
			++r;
		}

		// Outward, and this is the half that cost an afternoon. to_cells()
		// rounds each edge to the NEAREST cell, so a clip thinner than a cell
		// rounds to nothing -- and read as "a clip that admits nothing" it
		// makes a QLineEdit's text disappear, because the text area's inset is
		// a few pixels on a nineteen-pixel cell. A cell is atomic: a clip
		// covering part of one either admits it or loses content that was
		// inside it.
		//
		// The clip runs from six pixels into cell 0 to four pixels into cell
		// 2, so rounding each edge to the nearest cell gives cells 1 and 2 --
		// dropping the cell the clip starts in and the one it ends in.
		// Rounding outward gives 0 through 3. Chosen so the two answers differ
		// by more than one cell, which a check on "at least three" separates.
		Qtty::CellBuffer part(20, 2);
		{
			Qtty::CellPaintDevice dev(part);
			QPainter p(&dev);
			p.setClipRect(QRectF(cw * 0.6, 0, cw * 1.8, ch));
			p.fillRect(QRect(0, 0, cw * 20, ch), Qt::red);
			p.end();
		}
		if (filled(part) >= 3)
			printf("PASS: a clip admits every cell it covers any part of\n");
		else {
			printf("FAIL: a clip admits every cell it covers any part of\n"
			       "      condition: %d cells filled, nearest-rounding gives 2\n",
			       filled(part));
			++r;
		}
	}


	// ---- ICellPainted (section 5.3, risk R5) ---------------------------------
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(20, 4));
		auto *drawn = new CellDrawn(&host);
		drawn->setGeometry(0, 0, GridMetrics::cw() * 20, GridMetrics::ch() * 2);
		host.show();
		QCoreApplication::processEvents();

		Qtty::CellBuffer buf(20, 4);
		Qtty::render_once(host, buf);

		if (drawn->calls == 0) {
			printf("FAIL: ICellPainted widget was never asked to paint cells\n");
			++r;
		} else printf("PASS: an ICellPainted widget paints itself in cells\n");

		if (buffer_has(buf, QStringLiteral("CELLS")))
			printf("PASS: what it drew reached the buffer\n");
		else { printf("FAIL: what it drew reached the buffer\n"); ++r; }

		// The discriminating half. Channel B would put "PIXELS" there, and a
		// filter that painted cells WITHOUT consuming the event would leave
		// both -- passing the two checks above while still being wrong.
		if (!buffer_has(buf, QStringLiteral("PIXELS")))
			printf("PASS: its ordinary painting was skipped, not overdrawn\n");
		else { printf("FAIL: its ordinary painting was skipped, not overdrawn\n"); ++r; }

		// section 10.1's inertness rule, made testable. Outside a cell render
		// there is no active CellPaintDevice, so the filter must stand down and
		// the widget must paint the way it would with qtty absent. A filter
		// that consumed paint events unconditionally would take an
		// ICellPainted widget's rendering away in the GUI build -- the exact
		// failure the rule exists to forbid, and one nothing else here would
		// catch, because every other check runs inside a render.
		const int before = drawn->calls;
		QImage offscreen(drawn->size(), QImage::Format_ARGB32);
		offscreen.fill(Qt::transparent);
		drawn->render(&offscreen);
		if (drawn->calls == before)
			printf("PASS: outside a cell render the interface is not consulted\n");
		else { printf("FAIL: outside a cell render the interface is not consulted\n"); ++r; }
	}

	// What the interface is handed, in the three configurations the first
	// ICellPainted checks above do not reach: a widget inside a scrolled
	// viewport, one hanging off the edge of the window, and one that claims
	// both interfaces at once. Found by rendering them, which is the method
	// project.md section 0d describes.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

		// The rect is in WINDOW cells, so scrolling the viewport under the
		// widget has to move it. A rect taken from the widget's own
		// coordinates would be right at a scroll of zero and wrong at every
		// other, which is why the check is a difference rather than a value.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *area = new QScrollArea(&host);
		area->setGeometry(0, ch, cw * 20, ch * 3);
		area->setFrameShape(QFrame::NoFrame);
		auto *inner = new QWidget;
		inner->resize(cw * 20, ch * 9);
		auto *drawn = new CellDrawn(inner);
		// INSIDE the viewport at rest, which is the whole of the baseline
		// working. Placed below it, the widget is never painted, `got` keeps
		// the QRect it was default-constructed with, and the difference this
		// check measures is taken against a rect nothing produced -- which is
		// what the first version of it did, and it read as a real failure.
		drawn->setGeometry(0, ch, cw * 6, ch);
		area->setWidget(inner);
		host.resize(GridMetrics::cells(22, 6));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer b(22, 6);
		Qtty::render_once(host, b);
		const QRect at_rest = drawn->got;
		const int painted_once = drawn->calls;
		area->verticalScrollBar()->setValue(ch);
		QCoreApplication::processEvents();
		Qtty::CellBuffer b2(22, 6);
		Qtty::render_once(host, b2);
		if (painted_once > 0 && drawn->calls > painted_once
		    && drawn->got.y() == at_rest.y() - 1 && drawn->got.x() == at_rest.x())
			printf("PASS: a scrolled viewport moves the rect the interface is handed\n");
		else {
			printf("FAIL: a scrolled viewport moves the rect the interface is handed\n"
			       "      condition: %d,%d became %d,%d over %d then %d paint(s), "
			       "expected one row up\n",
			       at_rest.x(), at_rest.y(), drawn->got.x(), drawn->got.y(),
			       painted_once, drawn->calls - painted_once);
			++r;
		}

		// Hanging off the left edge. The rect goes negative rather than being
		// clamped, which is what lets an implementation draw its whole width
		// and let the part that is off-screen fall away; CellBuffer drops a
		// write out of range rather than wrapping it onto the row above,
		// which is the half that would corrupt a frame silently.
		QWidget edge;
		edge.setAttribute(Qt::WA_DontShowOnScreen);
		auto *hung = new CellDrawn(&edge);
		hung->setGeometry(-cw * 3, ch, cw * 8, ch);
		edge.resize(GridMetrics::cells(20, 3));
		edge.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer eb(20, 3);
		Qtty::render_once(edge, eb);
		bool wrapped = false;
		for (int x = 0; x < eb.cols(); ++x)
			if (eb.at(x, 0).ch != QStringLiteral(" ")) wrapped = true;
		if (hung->got.x() == -3 && !wrapped)
			printf("PASS: a widget off the left edge gets a negative rect, "
			       "and what falls outside is dropped\n");
		else {
			printf("FAIL: a widget off the left edge gets a negative rect, "
			       "and what falls outside is dropped\n"
			       "      condition: rect x %d, row above %s\n",
			       hung->got.x(), wrapped ? "written" : "clear");
			++r;
		}

		// Both interfaces on one class. It compiles and the pixel path wins,
		// because the filter tests for a surface first -- so paint_cells() is
		// never called and the widget is harvested as an image with no
		// warning. qtty/paint.h says not to do this; this is what says the
		// consequence has not quietly changed, in either direction.
		QWidget bh;
		bh.setAttribute(Qt::WA_DontShowOnScreen);
		auto *both = new Both(&bh);
		both->setGeometry(0, 0, cw * 8, ch * 2);
		bh.resize(GridMetrics::cells(20, 4));
		bh.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer bb(20, 4);
		QVector<Qtty::CellImage> placements;
		Qtty::render_once(bh, bb, &placements);
		if (both->cell_calls == 0 && placements.size() == 1
		    && !buffer_has(bb, QStringLiteral("BOTH")))
			printf("PASS: a widget claiming both interfaces takes the pixel path\n");
		else {
			printf("FAIL: a widget claiming both interfaces takes the pixel path\n"
			       "      condition: paint_cells called %d time(s), "
			       "%d placement(s)\n", both->cell_calls, int(placements.size()));
			++r;
		}
	}


	// A caret is a fill, and a fill used to erase the cell it lands in.
	// QLineEdit paints its text cursor as a ~1px-wide rect in the Text colour
	// AFTER drawing the line, so to_cells() rounded it up to a whole cell and
	// blanked the character under it. Found as a QSpinBox whose value
	// disappeared once it had focus and a key: the value was correct in the
	// widget and drawn in the trace, then removed by a 1.0x19.0px fill.
	//
	// The caret is not lost by dropping it -- Compositor::compose() places the
	// terminal's own cursor from the focus widget. What this asserts is the
	// rule that made it safe to drop: a rect covering less than half a cell
	// cannot stand for that cell's background.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *edit = new QLineEdit(&host);
		edit->setText(QStringLiteral("abcde"));
		edit->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		host.resize(GridMetrics::cells(20, 2));
		host.show();
		QCoreApplication::processEvents();
		edit->setFocus();
		QCoreApplication::processEvents();

		Qtty::CellBuffer buf(20, 2);
		Qtty::render_once(host, buf);
		const QString before = buf.to_text();

		// Paint a caret-shaped fill straight at the engine, in the Text
		// colour, the way QLineEdit does. Going through the engine rather than
		// through focus is deliberate: whether Qt shows a caret depends on a
		// blink timer and on window activation, so a test that waited for one
		// would pass vacuously on the run where it did not blink.
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			p.fillRect(QRectF(GridMetrics::cw() * 2, 0, 1, GridMetrics::ch()),
			           QGuiApplication::palette().color(QPalette::Text));
		}

		if (buf.to_text() == before)
			printf("PASS: a caret-width fill leaves the glyph under it\n");
		else {
			printf("FAIL: a caret-width fill leaves the glyph under it\n");
			printf("      before '%s'\n      after  '%s'\n",
			       qPrintable(before.section('\n', 0, 0)),
			       qPrintable(buf.to_text().section('\n', 0, 0)));
			++r;
		}

		// The other half, so the rule is not just "thin fills do nothing": a
		// fill of the same colour that DOES cover the cell still paints.
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			p.fillRect(QRectF(0, 0, GridMetrics::cw() * 5, GridMetrics::ch()),
			           QGuiApplication::palette().color(QPalette::Text));
		}
		if (buf.to_text() != before)
			printf("PASS: a fill that does cover the cells still paints\n");
		else {
			printf("FAIL: a fill that does cover the cells still paints\n"
			       "      before '%s'\n      after  '%s'\n",
			       qPrintable(before.section('\n', 0, 0)),
			       qPrintable(buf.to_text().section('\n', 0, 0)));
			++r;
		}
	}

	// section 5.7's PixelSurface: the mirror of ICellPainted. That interface
	// is for a widget that draws itself in CELLS; this is for one whose
	// content is genuinely pixels, which Channel B would mangle by snapping
	// every primitive in it to the grid.
	{
		struct Plot : Qtty::PixelSurface {
			using Qtty::PixelSurface::PixelSurface;
			QColor tone = Qt::red;
			void paintEvent(QPaintEvent *) override {
				QPainter p(this);
				p.fillRect(rect(), tone);
				// Deliberately sub-cell: through Channel B this would round
				// to whole cells and stop being a diagonal at all.
				p.setPen(Qt::blue);
				p.drawLine(0, 0, width(), height());
			}
		};

		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *plot = new Plot(&host);
		plot->setGeometry(0, 0, GridMetrics::cw() * 6, GridMetrics::ch() * 3);
		host.resize(GridMetrics::cells(20, 5));
		host.show();
		QCoreApplication::processEvents();

		Qtty::CellBuffer buf(20, 5);
		Qtty::render_once(host, buf);

		if (buf.images.size() == 1)
			printf("PASS: a pixel surface arrives as one placement\n");
		else { printf("FAIL: a pixel surface arrives as one placement\n"); ++r; }

		if (buf.images.size() == 1 && buf.images[0].cell_rect == QRect(0, 0, 6, 3))
			printf("PASS: with the widget's own cell geometry\n");
		else { printf("FAIL: with the widget's own cell geometry\n"); ++r; }

		if (buf.images.size() == 1 && buf.images[0].pixmap.size() == plot->size())
			printf("PASS: and its pixels at pixel resolution, not snapped to cells\n");
		else {
			printf("FAIL: and its pixels at pixel resolution, not snapped to cells\n");
			++r;
		}

		// Consumed, so Channel B never saw it. Without that the red fill would
		// have painted the cells underneath the placement as well, which is
		// the mangling this exists to avoid.
		bool tinted = false;
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 6; ++x)
				if (buf.at(x, y).bg.kind() != Qtty::Color::Default) tinted = true;
		if (!tinted) printf("PASS: and Channel B did not also paint it into the cells\n");
		else {
			printf("FAIL: and Channel B did not also paint it into the cells\n");
			++r;
		}

		// The key is content-addressed. One taken from the widget would tell
		// the kitty tier the image had not changed and it would keep showing
		// the first frame; a fresh key every time would re-upload an
		// unchanged plot on every repaint.
		Qtty::CellBuffer again(20, 5);
		Qtty::render_once(host, again);
		if (again.images.size() == 1 && buf.images.size() == 1
		    && again.images[0].key == buf.images[0].key)
			printf("PASS: an unchanged surface keeps its key, so it uploads once\n");
		else {
			printf("FAIL: an unchanged surface keeps its key, so it uploads once\n");
			++r;
		}

		// The discriminating half, and the one that was missing: a key taken
		// from the WIDGET also survives the check above, so unchanged-keeps-
		// its-key proves nothing on its own. Changing the content must change
		// the key, or the kitty tier keeps showing the first frame for ever.
		plot->tone = Qt::green;
		plot->update();
		QCoreApplication::processEvents();
		Qtty::CellBuffer moved(20, 5);
		Qtty::render_once(host, moved);
		if (moved.images.size() == 1 && buf.images.size() == 1
		    && moved.images[0].key != buf.images[0].key)
			printf("PASS: and changed content changes it, so the frame is not stale\n");
		else {
			printf("FAIL: and changed content changes it, so the frame is not stale\n");
			++r;
		}
	}

	// drawPolygon, which coverage named as a paint-engine primitive nothing
	// ever exercised. It is not decoration: every Qt widget that draws an
	// arrow, a triangle or a chevron through the default style arrives here,
	// and the engine turns it into the box of its bounding rectangle rather
	// than dropping it.
	//
	// Driven at the engine, like the caret above, because reaching it through
	// a widget would depend on which style path that widget happens to take
	// -- and the qtty style draws the combo, spin and scroll bars whole, so
	// the obvious candidates never call it.
	{
		Qtty::CellBuffer buf(10, 4);
		const QString before = buf.to_text();
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
			const QPointF tri[3] = { QPointF(cw, ch), QPointF(cw * 4, ch),
				                     QPointF(cw * 2.5, ch * 3) };
			p.setBrush(QGuiApplication::palette().color(QPalette::Text));
			p.drawPolygon(tri, 3);
		}
		// Asserted as CHANGED plus a corner, not merely changed: a polygon
		// that painted one stray cell would satisfy "something happened",
		// and the bounding box is what this primitive actually computes.
		const bool drew = buf.to_text() != before;
		const bool boxed = buf.at(1, 1).ch != QStringLiteral(" ")
		                && buf.at(3, 1).ch != QStringLiteral(" ");
		if (drew && boxed)
			printf("PASS: a polygon becomes the box of its bounding rectangle\n");
		else {
			printf("FAIL: a polygon becomes the box of its bounding rectangle\n");
			printf("      drew=%d boxed=%d, buffer '%s'\n", int(drew), int(boxed),
			       qPrintable(buf.to_text().replace(QLatin1Char('\n'),
			                                        QLatin1Char('/'))));
			++r;
		}
	}

	{
		// The paint device's own metrics, which nothing had asked for. Qt
		// asks through QPaintDevice::width() and friends whenever it decides
		// how to scale or whether a device is monochrome, so a wrong answer
		// here is a wrong decision made inside Qt where nothing of ours can
		// see it.
		Qtty::CellBuffer buf(12, 5);
		Qtty::CellPaintDevice dev(buf);
		const bool sized = dev.width() == 12 * GridMetrics::cw()
		                && dev.height() == 5 * GridMetrics::ch();
		// The ratio matters most: anything other than 1 makes Qt lay out at
		// one scale and this device round at another, which is the whole
		// class of fault GridMetrics exists to prevent.
		const bool plain = qFuzzyCompare(dev.devicePixelRatio(), 1.0)
		                && dev.depth() > 1 && dev.widthMM() > 0
		                && dev.heightMM() > 0
		                // colorCount() asks for a metric this device does not
		                // answer specially, which is the default arm: a device
		                // that returned something arbitrary there would have
		                // Qt believing it was a paletted display.
		                && dev.colorCount() >= 0;
		if (sized && plain)
			printf("PASS: the paint device reports its own size, depth and ratio\n");
		else {
			printf("FAIL: the paint device reports its own size, depth and ratio\n"
			       "      %dx%d px, depth %d, ratio %f, %dx%d mm, %d colours\n",
			       dev.width(), dev.height(), dev.depth(),
			       dev.devicePixelRatio(), dev.widthMM(), dev.heightMM(),
			       dev.colorCount());
			++r;
		}

		// A pen colour matching no palette foreground role, which is the
		// branch that falls through to a literal RGB. Every existing text
		// test paints in a themed colour, so the fallback had never run.
		{
			QPainter p(&dev);
			p.setPen(QColor(3, 250, 137));
			p.drawText(QPoint(0, GridMetrics::ch()), QStringLiteral("z"));
		}
		if (buf.at(0, 0).ch == QStringLiteral("z"))
			printf("PASS: text in an unthemed colour still lands in its cell\n");
		else {
			printf("FAIL: text in an unthemed colour still lands in its cell\n");
			++r;
		}
	}

	return r;
}
