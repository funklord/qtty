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
	void paint_cells(Qtty::CellBuffer &buffer, const QRect &cells) const override {
		++calls;
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
		else { printf("FAIL: a fill that does cover the cells still paints\n"); ++r; }
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
