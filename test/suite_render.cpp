// suite_render -- Gate-1 regression as a snapshot (section 9).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <QTemporaryDir>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

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
	// The SOURCE rectangle of drawPixmap(target, pixmap, source), which the
	// engine accepted and ignored: an application drawing one sprite out of
	// an atlas got the whole atlas placed, at the right size and silently.
	// Nothing in this tree exercises it -- every drawPixmap the suite
	// produces arrives with the full rect, measured with a probe -- so the
	// check has to construct the case rather than find it.
	//
	// Paired, because "the placement carries the right half" is also true of
	// an engine that crops unconditionally and would then wreck every
	// ordinary drawPixmap, which is the far commoner call.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		Qtty::CellBuffer buf(8, 4);
		QPixmap atlas(cw * 4, ch * 2);      // two halves, told apart by colour
		{
			QPainter ap(&atlas);
			ap.fillRect(0, 0, cw * 2, ch * 2, Qt::red);
			ap.fillRect(cw * 2, 0, cw * 2, ch * 2, Qt::blue);
		}
		QPixmap placed_part, placed_whole;
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			p.drawPixmap(QRectF(0, 0, cw * 2, ch * 2), atlas,
			             QRectF(cw * 2, 0, cw * 2, ch * 2));   // the blue half
			p.end();
			if (!dev.placements.isEmpty()) placed_part = dev.placements.first().pixmap;
		}
		{
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			p.drawPixmap(QRect(0, 0, cw * 4, ch * 2), atlas);   // no source rect
			p.end();
			if (!dev.placements.isEmpty()) placed_whole = dev.placements.first().pixmap;
		}
		const QImage part = placed_part.toImage();
		const QImage all = placed_whole.toImage();
		const bool part_right = !part.isNull()
		    && part.size() == QSize(cw * 2, ch * 2)
		    && QColor(part.pixel(1, 1)) == QColor(Qt::blue);
		const bool whole_right = !all.isNull()
		    && all.size() == QSize(cw * 4, ch * 2)
		    && QColor(all.pixel(1, 1)) == QColor(Qt::red);
		if (part_right && whole_right)
			printf("PASS: a source rectangle places that part of the pixmap,"
			       " and no source rectangle places all of it\n");
		else {
			printf("FAIL: a source rectangle places that part of the pixmap,"
			       " and no source rectangle places all of it\n"
			       "      condition: part %dx%d %s, whole %dx%d %s\n",
			       part.width(), part.height(),
			       part.isNull() ? "none" : qPrintable(QColor(part.pixel(1, 1)).name()),
			       all.width(), all.height(),
			       all.isNull() ? "none" : qPrintable(QColor(all.pixel(1, 1)).name()));
			++r;
		}
	}

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

		// ALPHA, which this engine discarded until a real application's
		// overlay was rendered through it.
		//
		// Both halves are asserted because they fail in opposite
		// directions and a fixture that catches one says nothing about
		// the other: a fully transparent brush drew SOMETHING (solid
		// black, the RGB bytes of Qt::transparent kept after the alpha
		// was dropped), and a translucent brush drew EVERYTHING (opaque,
		// replacing the very thing the overlay existed to shade).
		{
			auto ground_of = [&](QColor over) {
				Qtty::CellBuffer b(4, 1);
				Qtty::CellPaintDevice dev(b);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch), QColor(0xd5, 0x20, 0x2a));
				p.fillRect(QRect(0, 0, cw * 4, ch), over);
				p.end();
				return b.at(1, 0).bg;
			};
			const Qtty::Color plain = ground_of(QColor(0xd5, 0x20, 0x2a));
			const Qtty::Color washed = ground_of(QColor(0xff, 0x8b, 0x33, 80));
			const Qtty::Color opaque = ground_of(QColor(0xff, 0x8b, 0x33));

			// Transparent over an EMPTY cell, so "left alone" is
			// Default and cannot be confused with the ground below.
			Qtty::CellBuffer clear_buf(4, 1);
			{
				Qtty::CellPaintDevice dev(clear_buf);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch), Qt::transparent);
				p.end();
			}
			if (clear_buf.at(1, 0).bg.kind() == Qtty::Color::Default)
				printf("PASS: a fully transparent fill leaves the cell alone\n");
			else {
				printf("FAIL: a fully transparent fill leaves the cell alone\n");
				++r;
			}

			// The relationship, not the value: a wash is neither of its
			// two operands. Pinning the blended constant would pass just
			// as well against a fill that had simply stopped, and would
			// go stale the moment the arithmetic is tuned.
			//
			// Compared on the VISIBLE channels. Written against whole
			// QRgb values this check passed with blending disabled,
			// because the opaque path then stored the brush's alpha byte
			// and the two differed in the one byte nothing draws. The
			// engine normalises that away now, and comparing channels
			// says what is meant either way.
			if (washed.kind() == Qtty::Color::Rgb
			    && (washed.value() & 0xffffff)
			           != (plain.value() & 0xffffff)
			    && (washed.value() & 0xffffff)
			           != (opaque.value() & 0xffffff))
				printf("PASS: a translucent fill is neither its ground nor its own colour\n");
			else {
				printf("FAIL: a translucent fill is neither its ground nor its own colour\n");
				++r;
			}

			// And it lies BETWEEN them per channel, which is what makes
			// it a blend rather than merely a third colour.
			const QRgb w = washed.value(), g = plain.value(),
			           o = opaque.value();
			if (qRed(w) > qMin(qRed(g), qRed(o))
			    && qRed(w) < qMax(qRed(g), qRed(o))
			    && qGreen(w) > qMin(qGreen(g), qGreen(o))
			    && qGreen(w) < qMax(qGreen(g), qGreen(o)))
				printf("PASS: and sits between the two it was blended from\n");
			else {
				printf("FAIL: and sits between the two it was blended from\n");
				++r;
			}
		}

		// The PEN path, which had the same two defects as the fill and one
		// of them worse. Qt::transparent drew an opaque BLACK rule, and
		// transparent TEXT drew opaque BLACK text -- drawing a string in a
		// transparent pen is an ordinary way to hide it, so this made
		// hidden text visible rather than merely miscoloured.
		{
			auto cell_after = [&](std::function<void(QPainter &)> draw) {
				Qtty::CellBuffer b(6, 1);
				Qtty::CellPaintDevice dev(b);
				QPainter p(&dev);
				draw(p);
				p.end();
				return b.at(1, 0);
			};
			const Qtty::Cell rule = cell_after([&](QPainter &p) {
				p.setPen(QPen(Qt::transparent, 1.0));
				p.drawLine(0, ch / 2, cw * 5, ch / 2);
			});
			const Qtty::Cell words = cell_after([&](QPainter &p) {
				p.setPen(QPen(Qt::transparent, 1.0));
				p.drawText(QRectF(0, 0, cw * 5, ch), Qt::AlignLeft,
				           QStringLiteral("XXXX"));
			});
			// The GLYPH, not the colour. A transparent stroke that got as
			// far as writing a mark would be wrong whatever colour it
			// carried, and this path sets the character as well as the ink.
			if (rule.ch == QStringLiteral(" ")
			    && rule.fg.kind() == Qtty::Color::Default)
				printf("PASS: a transparent pen draws no rule\n");
			else {
				printf("FAIL: a transparent pen draws no rule\n");
				++r;
			}
			if (words.ch == QStringLiteral(" "))
				printf("PASS: and a transparent pen draws no text\n");
			else {
				printf("FAIL: and a transparent pen draws no text\n");
				++r;
			}

			// A DIAGONAL, which reaches the guard in stroke_segment
			// rather than the one in line(). line() returns early for a
			// transparent pen now, so a plain drawLine cannot get there
			// -- only a polyline can, and without this the guard is code
			// no check exercises.
			const Qtty::Cell diag = cell_after([&](QPainter &p) {
				p.setPen(QPen(Qt::transparent, 1.0));
				QPolygonF poly;
				poly << QPointF(0, 0) << QPointF(cw * 5, ch);
				p.drawPolyline(poly);
			});
			if (diag.ch == QStringLiteral(" "))
				printf("PASS: and a transparent pen draws no diagonal\n");
			else {
				printf("FAIL: and a transparent pen draws no diagonal\n");
				++r;
			}

			// And a translucent one blends, the way a translucent fill
			// does -- over a ground this layer can actually see.
			const Qtty::Cell wash = cell_after([&](QPainter &p) {
				p.fillRect(QRect(0, 0, cw * 5, ch),
				           QColor(0xd5, 0x20, 0x2a));
				QColor c(0x2e, 0x7e, 0xbb);
				c.setAlpha(80);
				p.setPen(QPen(c, 1.0));
				p.drawLine(0, ch / 2, cw * 5, ch / 2);
			});
			const QRgb v = wash.fg.value();
			if (wash.fg.kind() == Qtty::Color::Rgb
			    && qRed(v) > 0x2e && qRed(v) < 0xd5
			    && qBlue(v) > 0x2a && qBlue(v) < 0xbb)
				printf("PASS: and a translucent pen blends with the"
				       " ground\n");
			else {
				printf("FAIL: and a translucent pen blends with the"
				       " ground\n");
				++r;
			}
		}

		// QPainter::setOpacity(), which multiplies the brush's own alpha
		// and which this engine did not track at all -- so a half-opaque
		// fill drew fully opaque. The same defect as discarding a colour's
		// alpha byte, arriving by a route the alpha checks above cannot
		// see, because the brush here is FULLY opaque and the
		// transparency lives in the painter.
		{
			Qtty::CellBuffer b(4, 1);
			{
				Qtty::CellPaintDevice dev(b);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch), QColor(0xd5, 0x20, 0x2a));
				p.setOpacity(0.5);
				p.fillRect(QRect(0, 0, cw * 4, ch), QColor(0x2e, 0x7e, 0xbb));
				p.end();
			}
			const Qtty::Color got = b.at(1, 0).bg;
			const QRgb v = got.value();
			// Between the two, per channel -- the relationship, so the
			// arithmetic can be tuned without rewriting the check, and so
			// that ignoring the opacity (which lands exactly on the blue)
			// fails.
			const bool between =
			    got.kind() == Qtty::Color::Rgb
			    && qRed(v) > 0x2e && qRed(v) < 0xd5
			    && qGreen(v) < 0x7e && qGreen(v) > 0x20
			    && qBlue(v) < 0xbb && qBlue(v) > 0x2a;
			if (between)
				printf("PASS: a fill under setOpacity blends with what is"
				       " under it\n");
			else {
				printf("FAIL: a fill under setOpacity blends with what is"
				       " under it\n");
				++r;
			}
		}

		// A GRADIENT brush, whose QBrush::color() is documented to be
		// "the brush colour" and answers BLACK -- a gradient has none. A
		// chart shading an area is the ordinary way to meet this, and it
		// filled the cells with a colour the drawing does not contain.
		{
			QLinearGradient gr(0, 0, 0, ch);
			gr.setColorAt(0, QColor(0x2e, 0x7e, 0xbb));
			gr.setColorAt(1, QColor(0x2e, 0x7e, 0xbb));
			Qtty::CellBuffer b(4, 1);
			{
				Qtty::CellPaintDevice dev(b);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch), QBrush(gr));
				p.end();
			}
			const Qtty::Color got = b.at(1, 0).bg;
			// Against the STOPS, not against a constant: what is wrong
			// with black here is that it is a colour no stop names, and
			// asserting the blue directly would pass just as well for a
			// fill that ignored the gradient and happened to be handed
			// blue by something else.
			const bool from_stops =
			    got.kind() == Qtty::Color::Rgb
			    && (got.value() & 0xffffff) == 0x2e7ebbu;
			if (from_stops)
				printf("PASS: a gradient fill takes its colour from the"
				       " gradient\n");
			else {
				printf("FAIL: a gradient fill takes its colour from the"
				       " gradient\n");
				++r;
			}
		}

		// A DISABLED widget's fill. Qt takes a disabled widget's brush from
		// the palette's Disabled group, and this engine matched the brush
		// against the Active group only -- its own copy of the role list,
		// without the fallback role_of() performs. So a disabled field's
		// background matched no role and went out as a hard 24-bit colour,
		// which is the #bebebe incident cell_geometry.h records, in the one
		// path that had not been moved to the shared helper.
		//
		// The partition is asserted first: on a palette where the two groups
		// agree for Base this fixture cannot discriminate, and saying so is
		// better than passing for the wrong reason.
		{
			const QPalette &pal = QGuiApplication::palette();
			// HIGHLIGHT, and the choice is measured rather than convenient.
			// Of the six roles this engine matches, five have a Disabled
			// colour that coincides with some ACTIVE role -- Window, Base
			// and Button are all #efefef disabled, which is Active Window --
			// so the old Active-only loop still found *a* role for them and
			// wrote no true colour. Only Highlight's #919191 matches no
			// active role at all, so it is the one fill on this palette
			// where the missing fallback is observable. A fixture on any of
			// the other five passes with the defect in place; the first
			// version of this check used Base and did exactly that.
			const QPalette::ColorRole probe = QPalette::Highlight;
			const QColor act = pal.color(QPalette::Active, probe);
			const QColor dis = pal.color(QPalette::Disabled, probe);
			bool coincides = false;
			for (QPalette::ColorRole r : {QPalette::Window, QPalette::Base,
			                              QPalette::Button,
			                              QPalette::AlternateBase,
			                              QPalette::Highlight,
			                              QPalette::ToolTipBase})
				if (pal.color(QPalette::Active, r) == dis) coincides = true;
			printf("info: Highlight is %s active, %s disabled%s\n",
			       qPrintable(act.name()), qPrintable(dis.name()),
			       coincides ? " (which some active role also has)" : "");
			if (act != dis && !coincides)
				printf("PASS: the disabled Highlight is a colour no active"
				       " role has, so this fixture can discriminate\n");
			else {
				printf("FAIL: the disabled Highlight is a colour no active"
				       " role has, so this fixture can discriminate\n"
				       "      condition: active %s, disabled %s, coincides"
				       " with an active role: %s\n", qPrintable(act.name()),
				       qPrintable(dis.name()), coincides ? "yes" : "no");
				++r;
			}

			Qtty::CellBuffer greyed(20, 2), invented(20, 2);
			auto paint = [&](Qtty::CellBuffer &b, const QColor &c) {
				Qtty::CellPaintDevice dev(b);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch), c);
				p.end();
			};
			auto rgb_cells = [](const Qtty::CellBuffer &b) {
				int n = 0;
				for (int x = 0; x < b.cols(); ++x)
					if (b.at(x, 0).bg.kind() == Qtty::Color::Rgb) ++n;
				return n;
			};
			paint(greyed, dis);
			// The control, and it is what stops this being an assertion that
			// the engine simply never writes an Rgb background: a colour no
			// role explains still has to pass through as true colour.
			paint(invented, QColor(203, 17, 89));
			printf("info: %d rgb cell(s) from the disabled Highlight, %d from"
			       " a colour no role explains\n",
			       rgb_cells(greyed), rgb_cells(invented));
			if (rgb_cells(greyed) == 0 && rgb_cells(invented) > 0)
				printf("PASS: a disabled widget's fill is matched to its role,"
				       " while an unexplained colour is not\n");
			else {
				printf("FAIL: a disabled widget's fill is matched to its role,"
				       " while an unexplained colour is not\n"
				       "      condition: %d rgb from the disabled Highlight,"
				       " %d from the unexplained colour\n",
				       rgb_cells(greyed), rgb_cells(invented));
				++r;
			}
		}
		// Four, exactly. The clip is QRect(0, 0, cw * 4, ch) -- pixels 0..39
		// on a ten-pixel cell, which is four whole cells with nothing
		// part-covered, so outward rounding has nothing to round.
		//
		// This said "Five, not four: the clip rounds OUTWARD, so a cell it
		// covers in part is admitted whole", and asserted a range so that
		// either would pass. There was no part-covered cell; the fifth was
		// clip_cells() adding one cell too many on the far edge, and the
		// sentence explaining it made the wrong number look deliberate --
		// the same way section 7.8's child check allowed eight cells for a
		// six-cell parent and called the slack intentional. Two comments,
		// one off-by-one, and a range wide enough to hide it in both.
		const int c = filled(clipped), o = filled(open);
		// Printed, because a range that accepts 4 or 5 cannot say which one
		// the code gives -- and the sibling check in section 7.8 allowed
		// eight cells for a six-cell parent, called the slack deliberate, and
		// hid an off-by-one in this very function for as long as it existed.
		// A tolerance is only honest when the value inside it is visible.
		printf("info: a clip four cells wide admits %d cells\n", c);
		if (c == 4 && o == 20)
			printf("PASS: a clip trims what is drawn, and no clip trims nothing\n");
		else {
			printf("FAIL: a clip trims what is drawn, and no clip trims nothing\n"
			       "      condition: %d cells clipped, %d unclipped\n", c, o);
			++r;
		}

		// design.md section 8.4's Unsupported tier: "renders a labelled
		// placeholder box". It promised one and nothing drew it -- measured,
		// a QGraphicsView came out as 42 glyphs, every one its own empty
		// QFrame border, which is what any framed widget with no content
		// draws. An unsupported widget was indistinguishable from a bug,
		// which is the one thing a placeholder exists to prevent.
		//
		// The LABEL is the assertion, not the box. A box alone is what the
		// old behaviour already looked like, so a check on the frame would
		// pass against the defect.
		{
			QGraphicsView v;
			auto *sc = new QGraphicsScene(&v);
			sc->addText(QStringLiteral("SCENETEXT"));
			v.setScene(sc);
			v.setAttribute(Qt::WA_DontShowOnScreen);
			v.resize(GridMetrics::cells(22, 5));
			v.show();
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(24, 6);
			Qtty::render_once(v, b);
			const QString frame = b.to_text();
			printf("info: an unsupported widget renders [%s]\n",
			       qPrintable(frame.simplified().left(46)));
			if (frame.contains(QStringLiteral("QGraphicsView")))
				printf("PASS: an unsupported widget says what it is\n");
			else {
				printf("FAIL: an unsupported widget says what it is\n");
				++r;
			}
			// And its content does NOT leak through. A scene item drawing
			// over the label is what the first version did, and it made the
			// box say something other than the truth.
			if (!frame.contains(QStringLiteral("SCENETEXT")))
				printf("PASS: and its contents do not draw over the box\n");
			else {
				printf("FAIL: and its contents do not draw over the box\n");
				++r;
			}
		}

		// The PEN on a stroke, which Channel B used to throw away: every
		// rule and every diagonal drew in the terminal's default colour, so
		// an application's red graph line and Qt's grey frame shading came
		// out identically.
		//
		// Carrying it naively is the #bebebe incident by another route.
		// Measured over one suite run, 1586 of 1597 pens reaching this path
		// resolve to a hard 24-bit colour and 1569 of those to ONE grey,
		// because Qt shades a sunken border with pal.dark() and pal.light().
		// So the rule is by ROLE: a colour the frame furniture uses draws in
		// the terminal's own colour as it always has, and a colour no role
		// explains is the application saying something and is carried.
		//
		// The pair is the whole assertion. Either half alone would pass
		// against a renderer that ignored the pen entirely, or against one
		// that carried every pen including Fusion's grey.
		{
			const auto ink = [&](const QColor &pen) {
				Qtty::CellBuffer b(8, 2);
				{
					Qtty::CellPaintDevice dev(b);
					QPainter p(&dev);
					p.setPen(pen);
					p.drawLine(0, ch / 2, cw * 6, ch / 2);
					p.end();
				}
				return b.at(1, 0).fg;
			};
			const QColor furniture = QGuiApplication::palette().dark().color();
			const Qtty::Color as_furniture = ink(furniture);
			const Qtty::Color as_content = ink(QColor(220, 40, 40));
			printf("info: a rule in the frame grey draws %s, one in the"
			       " application's red draws %s\n",
			       as_furniture == Qtty::Color() ? "default" : "a colour",
			       as_content == Qtty::Color() ? "default" : "a colour");
			if (as_furniture == Qtty::Color())
				printf("PASS: a rule Qt draws to shade a frame keeps the"
				       " terminal's colour\n");
			else {
				printf("FAIL: a rule Qt draws to shade a frame keeps the"
				       " terminal's colour\n");
				++r;
			}
			if (as_content != Qtty::Color())
				printf("PASS: and a rule in a colour no role explains carries"
				       " it\n");
			else {
				printf("FAIL: and a rule in a colour no role explains carries"
				       " it\n");
				++r;
			}
		}

		// An icon whose meaning is its SHAPE, substituted. The old
		// substitution averaged the whole picture into one colour per cell,
		// which for an icon encoding its state as a shape is the whole
		// meaning gone -- a sibling project draws five status icons that
		// differ deliberately by shape, its header recording that "around
		// one man in twelve cannot reliably tell the amber from the green",
		// and every one arrived as two cells of one colour.
		//
		// The pair is what says it, and it is the same picture twice: one
		// with vertical structure and one without. The flat one must still
		// substitute to the shaded block, because that convention is what
		// says "a picture is here" and several other checks pin it; the
		// structured one must carry its two halves.
		{
			const auto substitute = [&](bool structured) {
				QPixmap pm(cw * 2, ch);
				pm.fill(QColor(40, 160, 60));
				if (structured) {
					QPainter p(&pm);
					p.fillRect(0, 0, pm.width(), pm.height() / 2,
					           QColor(200, 40, 40));
				}
				Qtty::CellBuffer b(4, 2);
				{
					Qtty::CellPaintDevice dev(b);
					QPainter p(&dev);
					p.drawPixmap(QRect(0, 0, cw * 2, ch), pm);
					p.end();
				}
				return b.at(0, 0);
			};
			const Qtty::Cell flat = substitute(false);
			const Qtty::Cell split = substitute(true);
			printf("info: a flat icon substitutes [%s], one with a top half"
			       " [%s] fg/bg %s\n", qPrintable(flat.ch), qPrintable(split.ch),
			       split.bg == Qtty::Color() ? "one colour" : "two");
			if (flat.ch == QStringLiteral("▒"))
				printf("PASS: a flat icon keeps the shaded block that says a"
				       " picture is here\n");
			else {
				printf("FAIL: a flat icon keeps the shaded block that says a"
				       " picture is here\n      condition: got [%s]\n",
				       qPrintable(flat.ch));
				++r;
			}
			if (split.ch == QStringLiteral("▀") && split.bg != Qtty::Color()
			    && split.fg != split.bg)
				printf("PASS: and one with a top and a bottom carries both\n");
			else {
				printf("FAIL: and one with a top and a bottom carries both\n"
				       "      condition: [%s], two colours %d\n",
				       qPrintable(split.ch), int(split.bg != Qtty::Color()));
				++r;
			}
		}

		// A fill WIDER than the cap this engine used to carry. The literal
		// 400x200 was applied to the UNCLIPPED cell rect, before the clip
		// narrowed it -- so it does not need a huge terminal, only a huge
		// LAYER, which is the ordinary state of a window whose layout
		// minimum exceeds the screen and which section 7 then scrolls.
		//
		// The cliff was exact and the failure total: at 400 cells the whole
		// visible run was coloured, at 401 nothing was. Channel A keeps
		// drawing, so what a user got was text on the terminal's own ground
		// with every application colour, themed surface and selection fill
		// gone -- a partial render, which is worse than either extreme.
		//
		// The pair is what says it. A check on the wide case alone would
		// pass against an engine that filled nothing at all.
		{
			const auto coloured = [&](int wide) {
				Qtty::CellBuffer b(80, 2);
				{
					Qtty::CellPaintDevice dev(b);
					QPainter p(&dev);
					p.fillRect(QRect(0, 0, wide * cw, ch), QColor(200, 30, 30));
					p.end();
				}
				int n = 0;
				for (int x = 0; x < b.cols(); ++x)
					if (b.at(x, 0).bg != Qtty::Color()) ++n;
				return n;
			};
			const int narrow = coloured(400), wide = coloured(401);
			printf("info: a fill 400 cells wide colours %d of 80 visible"
			       " cells; one 401 wide colours %d\n", narrow, wide);
			if (narrow == 80)
				printf("PASS: a wide fill colours the cells it covers\n");
			else {
				printf("FAIL: a wide fill colours the cells it covers\n"
				       "      condition: %d of 80 at 400 cells\n", narrow);
				++r;
			}
			if (wide == 80)
				printf("PASS: and a wider one does not stop colouring them\n");
			else {
				printf("FAIL: and a wider one does not stop colouring them\n"
				       "      condition: %d of 80 at 401 cells, %d at 400\n",
				       wide, narrow);
				++r;
			}
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


	// Qt clips a widget's children through the SYSTEM clip, not the user clip,
	// and this engine asked only about the user one. So a QScrollArea's
	// content was clipped by Qt and unclipped by us: scrolled out of view, it
	// painted over whatever was above the area. Section 8.7 recorded that as
	// "Qt sets no clip", which was this engine measuring the wrong channel --
	// hasClipping() answers about the user clip and was honestly false.
	//
	// Both checks are pairs, because "nothing drawn" passes any assertion
	// about content NOT appearing where it should not.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QWidget sh;
		sh.setAttribute(Qt::WA_DontShowOnScreen);
		auto *top = new QLabel(QStringLiteral("ABOVE THE AREA"), &sh);
		top->setGeometry(0, 0, cw * 20, ch);
		auto *area = new QScrollArea(&sh);
		area->setGeometry(0, ch, cw * 20, ch * 3);
		area->setFrameShape(QFrame::NoFrame);
		auto *inner = new QWidget;
		inner->resize(cw * 20, ch * 9);
		auto *deep = new QLabel(QStringLiteral("XXXXXX"), inner);
		deep->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		deep->setGeometry(0, ch * 4, cw * 6, ch * 3);
		area->setWidget(inner);
		sh.resize(GridMetrics::cells(22, 6));
		sh.show();
		QCoreApplication::processEvents();

		// In view first, so the check below cannot pass by the label never
		// rendering at all.
		area->verticalScrollBar()->setValue(4 * ch);
		QCoreApplication::processEvents();
		Qtty::CellBuffer shown(22, 6);
		Qtty::render_once(sh, shown);
		const bool visible_when_in_view = shown.to_text().contains(QStringLiteral("XXXXXX"));

		// Then scrolled one row further, so its only text row is above the
		// viewport and lands on the label outside the scroll area.
		area->verticalScrollBar()->setValue(5 * ch);
		QCoreApplication::processEvents();
		Qtty::CellBuffer hidden(22, 6);
		Qtty::render_once(sh, hidden);
		const QString first = hidden.to_text().section(QLatin1Char('\n'), 0, 0);
		if (visible_when_in_view && first.startsWith(QStringLiteral("ABOVE THE AREA")))
			printf("PASS: a scroll area's content does not paint outside it\n");
		else {
			printf("FAIL: a scroll area's content does not paint outside it\n"
			       "      condition: in view %d, first row '%s'\n",
			       int(visible_when_in_view), qPrintable(first));
			++r;
		}

		// The same rule without any scrolling: a child wider than its parent
		// is clipped to it. This used to allow eight cells for a six-cell
		// parent and called the slack "the outward rounding the clip does on
		// purpose" -- it was not on purpose. clip_cells() took a QRectF's
		// right(), which is EXCLUSIVE, and added one more cell on top of the
		// ceil(), so every clip in the engine was a column and a row too
		// large. That is where a check box, a radio button, a combo box and
		// a group box each put one cell of their label in the widget beside
		// them.
		//
		// Six cells for a six-cell parent, cell-aligned, is the whole answer.
		// The intent this check states -- eighteen cells of label do not
		// arrive -- is better served by it, not weakened.
		QWidget h2;
		h2.setAttribute(Qt::WA_DontShowOnScreen);
		h2.resize(GridMetrics::cells(20, 2));
		auto *box = new QWidget(&h2);
		box->setGeometry(0, 0, cw * 6, ch);
		auto *over = new QLabel(QStringLiteral("OVERFLOWING"), box);
		over->setGeometry(0, 0, cw * 18, ch);
		h2.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer b2(20, 2);
		Qtty::render_once(h2, b2);
		const QString row = b2.to_text().section(QLatin1Char('\n'), 0, 0).trimmed();
		if (row == QStringLiteral("OVERFL"))
			printf("PASS: a child wider than its parent is clipped to it\n");
		else {
			printf("FAIL: a child wider than its parent is clipped to it\n"
			       "      condition: row '%s'\n", qPrintable(row));
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

	// ---- Channel B geometry: diagonals, polylines, polygons ----------------
	//
	// The gap, measured on a custom paintEvent before any of this existed:
	//
	//     horizontal line   renders as a rule
	//     vertical line     renders as a rule
	//     diagonal line     NOTHING
	//     polyline, curve   NOTHING
	//     filled polygon    the box of its bounding rectangle
	//
	// line() had exactly two branches, |dy| < ch/2 and |dx| < cw/2, and a
	// diagonal matched neither and fell off the end of the function.
	// drawPath() and drawPolygon() both reduced to fill_rectf() of the
	// bounding rectangle, so a curve was replaced by its own bounding box --
	// and a FLAT curve, whose bounding box collapses to one row, by nothing,
	// because box() refuses a rectangle under two cells.
	//
	// Every check below asserts a RELATIONSHIP between the cells and the
	// geometry that produced them, never a named cell: a named cell measures
	// this suite's own arithmetic and goes stale the first time the walk is
	// touched. Three carry an explicit control -- a fixture where the answer
	// this replaced is ALSO non-empty -- so "something was drawn" cannot pass
	// them.
	//
	// Driven at the engine rather than through a widget, like the caret above:
	// reaching it through a widget would depend on which style path that
	// widget happens to take, and the qtty style draws the combo, spin and
	// scroll bars whole, so the obvious candidates never call it.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const auto occupied = [](const Qtty::CellBuffer &b, int x, int y) {
			return b.at(x, y).ch != QStringLiteral(" ");
		};
		const auto shown = [](const Qtty::CellBuffer &b) {
			return b.to_text().replace(QLatin1Char('\n'), QLatin1Char('/'));
		};
		// How far each occupied cell sits from the line the fixture drew, and
		// how many cells were touched at all. Both halves are needed and
		// neither is enough: "every row was reached" passes for a bounding
		// box, and "few cells" passes for a stray mark somewhere else.
		struct Trace { int rows_reached = 0, cells = 0, off_line = 0,
			           widest_row = 0; double worst = 0; };
		const auto trace = [&](const Qtty::CellBuffer &b, double y_at_x0,
		                       double rows_per_col) {
			Trace t;
			for (int y = 0; y < b.rows(); ++y) {
				int in_row = 0;
				for (int x = 0; x < b.cols(); ++x) {
					if (!occupied(b, x, y)) continue;
					++in_row;
					++t.cells;
					// The cell centre against the line's own equation. One
					// cell of slack, because a cell is atomic and a line
					// crossing its edge legitimately marks either side.
					const double ideal = y_at_x0 + (double(x) + 0.5) * rows_per_col;
					const double miss = qAbs((double(y) + 0.5) - ideal);
					t.worst = qMax(t.worst, miss);
					if (miss > 1.0) ++t.off_line;
				}
				if (in_row) ++t.rows_reached;
				t.widest_row = qMax(t.widest_row, in_row);
			}
			return t;
		};

		// A diagonal, corner to corner. The whole of the reported gap: this
		// drew nothing at all.
		const int cols = 10, rows = 10;
		Qtty::CellBuffer diag(cols, rows);
		{
			Qtty::CellPaintDevice dev(diag);
			QPainter p(&dev);
			p.drawLine(QLineF(0, 0, cw * cols, ch * rows));
		}
		const Trace d = trace(diag, 0.0, double(rows) / cols);
		if (d.rows_reached == rows)
			printf("PASS: a diagonal line touches a cell in every row\n");
		else {
			printf("FAIL: a diagonal line touches a cell in every row\n"
			       "      %d of %d rows, buffer '%s'\n",
			       d.rows_reached, rows, qPrintable(shown(diag)));
			++r;
		}
		// The control, and the reason the check above is not enough on its
		// own: the bounding box of this line is the WHOLE buffer, so filling
		// it -- or drawing box() round it, which is what drawPolygon did --
		// reaches every row too. What separates a line from its own bounding
		// box is that every cell is ON the line and there are few of them.
		//
		// ONE cell per row, and that is geometry rather than arithmetic: this
		// fixture is as many columns as rows, so the line crosses exactly one
		// cell of each. It is here because a grid walk's classic defect
		// produces a trace that passes every other clause -- when a segment
		// runs through a lattice corner, which this one does at every step,
		// the walk can take the column boundary and the row boundary
		// separately and visit a cell the segment never enters. That draws
		// twenty cells rather than ten, every one of them within a row of the
		// line and the total landing exactly on the `cols + rows` limit, so
		// only the per-row count sees it. Measured rather than argued: that
		// was the first version's behaviour, and putting it back turns this
		// check, the rising-line check below and the overwrite check further
		// down red, and nothing else in the suite.
		//
		// `d.cells > 0` is not padding either. Without it this check passes
		// over an engine that drew NOTHING -- which is exactly the engine it
		// was written against, and it did pass, green, directly under the
		// failure above. A control has to be able to fail the way the thing
		// it controls for fails, and "no cell is off the line" is true of an
		// empty buffer.
		if (d.cells > 0 && d.off_line == 0 && d.widest_row == 1
		    && d.cells <= cols + rows)
			printf("PASS: and every cell it touches lies on the line, not in"
			       " its bounding box\n");
		else {
			printf("FAIL: and every cell it touches lies on the line, not in"
			       " its bounding box\n"
			       "      %d cells, %d off the line by up to %.2f rows,"
			       " widest row %d, buffer '%s'\n",
			       d.cells, d.off_line, d.worst, d.widest_row,
			       qPrintable(shown(diag)));
			++r;
		}

		// A SHALLOW diagonal, which is the shape a chart actually draws: one
		// row per three and a third columns. It is the case a Bresenham walk
		// has to get right and a per-line glyph cannot, and it is what the
		// blocked sibling project's temperature curve is made of.
		Qtty::CellBuffer shallow(20, 6);
		{
			Qtty::CellPaintDevice dev(shallow);
			QPainter p(&dev);
			p.drawLine(QLineF(0, 0, cw * 20, ch * 6));
		}
		const Trace s = trace(shallow, 0.0, 6.0 / 20.0);
		if (s.rows_reached == 6 && s.off_line == 0)
			printf("PASS: a shallow diagonal follows its own slope down the"
			       " rows\n");
		else {
			printf("FAIL: a shallow diagonal follows its own slope down the"
			       " rows\n      %d of 6 rows, %d cells off the line by up to"
			       " %.2f rows, buffer '%s'\n",
			       s.rows_reached, s.off_line, s.worst, qPrintable(shown(shallow)));
			++r;
		}

		// The sign of the slope has to survive into the cells, or a rising
		// line and a falling one render identically and a chart reads
		// backwards. Asserted as the two glyph sets being DISJOINT rather
		// than by naming a glyph: which characters carry the slope is the
		// engine's choice, that it carries the slope at all is not.
		Qtty::CellBuffer rising(cols, rows);
		{
			Qtty::CellPaintDevice dev(rising);
			QPainter p(&dev);
			p.drawLine(QLineF(0, ch * rows, cw * cols, 0));
		}
		const Trace u = trace(rising, double(rows), -double(rows) / cols);
		QSet<QString> falling_glyphs, rising_glyphs;
		for (int y = 0; y < rows; ++y)
			for (int x = 0; x < cols; ++x) {
				if (occupied(diag, x, y)) falling_glyphs.insert(diag.at(x, y).ch);
				if (occupied(rising, x, y)) rising_glyphs.insert(rising.at(x, y).ch);
			}
		const bool disjoint = !falling_glyphs.isEmpty() && !rising_glyphs.isEmpty()
		                   && !falling_glyphs.intersects(rising_glyphs);
		if (u.rows_reached == rows && u.off_line == 0 && u.widest_row == 1
		    && disjoint)
			printf("PASS: and a rising line is drawn with different glyphs"
			       " from a falling one\n");
		else {
			printf("FAIL: and a rising line is drawn with different glyphs"
			       " from a falling one\n      %d of %d rows, %d off the line,"
			       " disjoint=%d, buffer '%s'\n",
			       u.rows_reached, rows, u.off_line, int(disjoint),
			       qPrintable(shown(rising)));
			++r;
		}

		// A polyline follows its POINTS. The fixture is a V whose bounding
		// box is the whole buffer, so the answer this replaced -- the box of
		// that rectangle -- is non-empty, reaches every row, and marks the
		// whole top edge. The V does not go along the top edge: it touches
		// the top row only at the two ends.
		Qtty::CellBuffer vee(16, 8);
		{
			Qtty::CellPaintDevice dev(vee);
			QPainter p(&dev);
			const QPointF pts[3] = { QPointF(0, 0), QPointF(cw * 8, ch * 8),
				                     QPointF(cw * 16, 0) };
			p.drawPolyline(pts, 3);
		}
		int top_middle = 0;
		for (int x = 4; x < 12; ++x) if (occupied(vee, x, 0)) ++top_middle;
		const bool ends = occupied(vee, 0, 0) && occupied(vee, 15, 0);
		bool apex = false;
		for (int x = 6; x < 10; ++x) if (occupied(vee, x, 7)) apex = true;
		if (ends && apex && top_middle == 0)
			printf("PASS: a polyline follows its points rather than its"
			       " bounding box\n");
		else {
			printf("FAIL: a polyline follows its points rather than its"
			       " bounding box\n      ends=%d apex=%d stray-top=%d,"
			       " buffer '%s'\n",
			       int(ends), int(apex), top_middle, qPrintable(shown(vee)));
			++r;
		}

		// A FLAT polyline, whose bounding rectangle has no height at all.
		// This is the case that drew literally nothing: to_cells() rounds the
		// extent up to one row and box() refuses a rectangle under two cells,
		// so a horizontal trace across a chart vanished.
		Qtty::CellBuffer flat(12, 4);
		{
			Qtty::CellPaintDevice dev(flat);
			QPainter p(&dev);
			const qreal y = ch * 2 + ch / 2.0;
			const QPointF pts[3] = { QPointF(0, y), QPointF(cw * 6, y),
				                     QPointF(cw * 12, y) };
			p.drawPolyline(pts, 3);
		}
		int on_row = 0, off_row = 0;
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 12; ++x)
				if (occupied(flat, x, y)) { (y == 2 ? on_row : off_row)++; }
		if (on_row >= 10 && off_row == 0)
			printf("PASS: a flat polyline draws a rule instead of nothing\n");
		else {
			printf("FAIL: a flat polyline draws a rule instead of nothing\n"
			       "      %d cells on its row, %d elsewhere, buffer '%s'\n",
			       on_row, off_row, qPrintable(shown(flat)));
			++r;
		}

		// drawPolygon honouring its mode. A filled triangle standing on its
		// base: the bounding-rect fill this replaced is non-empty, covers
		// every row and every column, and therefore passes any "did it draw"
		// test -- so the discriminator is the two TOP corners, which are
		// inside the bounding rectangle and outside the triangle.
		Qtty::CellBuffer tri(16, 8);
		{
			Qtty::CellPaintDevice dev(tri);
			QPainter p(&dev);
			const QPointF pts[3] = { QPointF(cw * 8, 0), QPointF(0, ch * 8),
				                     QPointF(cw * 16, ch * 8) };
			// A colour no palette role explains, so the fill passes through
			// as the application's own rather than resolving to a theme
			// answer that may be Color::Default and write nothing.
			p.setBrush(QColor(0x20, 0x90, 0x40));
			p.setPen(Qt::NoPen);
			p.drawPolygon(pts, 3);
		}
		const auto painted = [&](int x, int y) {
			return tri.at(x, y).bg.kind() != Qtty::Color::Default
			    || occupied(tri, x, y);
		};
		int base = 0;
		for (int x = 0; x < 16; ++x) if (painted(x, 7)) ++base;
		const bool corners_clear = !painted(0, 0) && !painted(15, 0);
		bool apex_painted = false;
		for (int x = 6; x < 10; ++x) if (painted(x, 0)) apex_painted = true;
		if (base >= 12 && apex_painted && corners_clear)
			printf("PASS: a filled polygon fills its own area, not the box of"
			       " its bounding rectangle\n");
		else {
			printf("FAIL: a filled polygon fills its own area, not the box of"
			       " its bounding rectangle\n"
			       "      base=%d apex=%d corners-clear=%d\n",
			       base, int(apex_painted), int(corners_clear));
			++r;
		}

		// The other half of the mode, and the same control: an OUTLINE has
		// nothing in the middle. box() of the bounding rectangle draws all
		// four corners, and the triangle has only one cell near each of two
		// of them, so the top corners separate the two answers again.
		Qtty::CellBuffer wire(16, 8);
		{
			Qtty::CellPaintDevice dev(wire);
			QPainter p(&dev);
			const QPointF pts[3] = { QPointF(cw * 8, 0), QPointF(0, ch * 8),
				                     QPointF(cw * 16, ch * 8) };
			p.setBrush(Qt::NoBrush);
			p.drawPolygon(pts, 3);
		}
		// Rows 4 to 6 and the middle six columns, which is inside the
		// triangle and clear of both its legs -- at row 4 they are near
		// columns 3 and 12, and they only spread further apart below that.
		// Sampling nearer the apex would sample the legs themselves, which is
		// what the first version of this check did.
		int interior = 0;
		for (int y = 4; y < 7; ++y)
			for (int x = 5; x < 11; ++x) if (occupied(wire, x, y)) ++interior;
		const bool wire_corners = !occupied(wire, 0, 0) && !occupied(wire, 15, 0);
		bool wire_edges = false;
		for (int x = 0; x < 16; ++x) if (occupied(wire, x, 7)) wire_edges = true;
		if (interior == 0 && wire_corners && wire_edges)
			printf("PASS: an unfilled polygon draws its edges and leaves its"
			       " middle alone\n");
		else {
			printf("FAIL: an unfilled polygon draws its edges and leaves its"
			       " middle alone\n      interior=%d corners-clear=%d"
			       " edges=%d, buffer '%s'\n",
			       interior, int(wire_corners), int(wire_edges),
			       qPrintable(shown(wire)));
			++r;
		}

		// A curve, which is what an application actually draws and what
		// arrives through drawPath() rather than drawPolygon(). An arc from
		// one corner to the other: the box of its bounding rectangle marks
		// BOTH remaining corners and the arc marks neither.
		Qtty::CellBuffer arc(16, 8);
		{
			Qtty::CellPaintDevice dev(arc);
			QPainter p(&dev);
			QPainterPath path(QPointF(0, ch * 8));
			path.cubicTo(QPointF(cw * 5, 0), QPointF(cw * 11, 0),
			             QPointF(cw * 16, ch * 8));
			p.setBrush(Qt::NoBrush);
			p.strokePath(path, QPen(QGuiApplication::palette().color(QPalette::Text)));
		}
		int arc_cells = 0, arc_rows = 0;
		for (int y = 0; y < 8; ++y) {
			bool any = false;
			for (int x = 0; x < 16; ++x) if (occupied(arc, x, y)) { any = true; ++arc_cells; }
			if (any) ++arc_rows;
		}
		const bool arc_corners = !occupied(arc, 0, 0) && !occupied(arc, 15, 0);
		if (arc_rows >= 6 && arc_corners && arc_cells <= 16 + 8)
			printf("PASS: a curve is rasterised rather than replaced by its"
			       " bounding box\n");
		else {
			printf("FAIL: a curve is rasterised rather than replaced by its"
			       " bounding box\n      %d rows, %d cells, corners-clear=%d,"
			       " buffer '%s'\n",
			       arc_rows, arc_cells, int(arc_corners), qPrintable(shown(arc)));
			++r;
		}

		// -- what must NOT have changed --------------------------------------
		// The two branches that already worked, and the rule they enforce.
		// Each was paid for by a defect: a rule lands in the cell it COVERS
		// rather than the one it is nearest, and a rule that meets any
		// content is not drawn at all, because a table grid crossing a row of
		// text otherwise filled the gaps between the words.
		Qtty::CellBuffer rules(10, 3);
		{
			Qtty::CellPaintDevice dev(rules);
			QPainter p(&dev);
			p.drawLine(QLineF(0, ch / 2.0, cw * 10, ch / 2.0));
			p.drawLine(QLineF(cw * 5 + cw / 2.0, ch, cw * 5 + cw / 2.0, ch * 3));
		}
		int h_run = 0, v_run = 0;
		for (int x = 0; x < 10; ++x)
			if (rules.at(x, 0).ch == QStringLiteral("─")) ++h_run;
		for (int y = 1; y < 3; ++y)
			if (rules.at(5, y).ch == QStringLiteral("│")) ++v_run;
		if (h_run == 10 && v_run == 2)
			printf("PASS: horizontal and vertical rules still render as rules\n");
		else {
			printf("FAIL: horizontal and vertical rules still render as rules\n"
			       "      h=%d v=%d, buffer '%s'\n",
			       h_run, v_run, qPrintable(shown(rules)));
			++r;
		}

		// The refusal, for both the rule path and the new walk. A diagonal
		// crossing a label must leave the label standing: a cell already
		// holding a glyph is content somebody drew, and a line is chrome.
		Qtty::CellBuffer over(10, 10);
		for (int i = 0; i < 10; ++i)
			over.text(i, i, QStringLiteral("#"));
		const QString labelled = over.to_text();
		const QString labelled_flat =
		    QString(labelled).replace(QLatin1Char('\n'), QLatin1Char('/'));
		{
			Qtty::CellPaintDevice dev(over);
			QPainter p(&dev);
			p.drawLine(QLineF(0, 0, cw * 10, ch * 10));
			p.drawLine(QLineF(0, ch / 2.0, cw * 10, ch / 2.0));
		}
		if (over.to_text() == labelled)
			printf("PASS: neither a rule nor a diagonal overwrites a cell that"
			       " holds a glyph\n");
		else {
			printf("FAIL: neither a rule nor a diagonal overwrites a cell that"
			       " holds a glyph\n      before '%s'\n      after  '%s'\n",
			       qPrintable(labelled_flat),
			       qPrintable(shown(over)));
			++r;
		}

		// And the clip, which the walk has to honour cell by cell for the
		// reason box() does: a shape crossing the clip's edge keeps the part
		// inside it and loses the part outside, rather than being shrunk to
		// fit or dropped whole.
		Qtty::CellBuffer clipped(10, 10);
		{
			Qtty::CellPaintDevice dev(clipped);
			QPainter p(&dev);
			p.setClipRect(QRect(cw * 2, ch * 2, cw * 4, ch * 4));
			p.drawLine(QLineF(0, 0, cw * 10, ch * 10));
		}
		int inside = 0, outside = 0;
		for (int y = 0; y < 10; ++y)
			for (int x = 0; x < 10; ++x)
				if (occupied(clipped, x, y)) {
					if (x >= 2 && x < 6 && y >= 2 && y < 6) ++inside;
					else ++outside;
				}
		if (inside > 0 && outside == 0)
			printf("PASS: and a clipped diagonal keeps only the part inside"
			       " the clip\n");
		else {
			printf("FAIL: and a clipped diagonal keeps only the part inside"
			       " the clip\n      %d inside, %d outside, buffer '%s'\n",
			       inside, outside, qPrintable(shown(clipped)));
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

	{
		// A rectangle taller than two rows, which is the only kind that has
		// SIDES: the loop drawing them runs from top+1 to bottom-1, and
		// coverage showed those two lines with no caller in a whole run.
		// Every frame the suite drew came through GridStyle's Channel A box;
		// this is the Channel B path, an application painting its own
		// rectangle into a cell device.
		Qtty::CellBuffer buf(8, 4);
		Qtty::CellPaintDevice dev(buf);
		{
			QPainter p(&dev);
			p.setPen(QColor(3, 250, 137));
			p.drawRect(0, 0, GridMetrics::cw() * 6 - 1,
			           GridMetrics::ch() * 4 - 1);
		}
		const QStringList rows = buf.to_text().split(QLatin1Char('\n'));
		const bool sides = rows.value(1).left(6) == QStringLiteral("\u2502    \u2502")
		                && rows.value(2).left(6) == QStringLiteral("\u2502    \u2502");
		// Paired with the corners, because a box that stopped being drawn at
		// all would satisfy neither -- and because the corners are what
		// survived the framed-scroll-area defect while the rules did not.
		const bool ends = rows.value(0).left(6) == QStringLiteral("\u250c\u2500\u2500\u2500\u2500\u2510")
		               && rows.value(3).left(6) == QStringLiteral("\u2514\u2500\u2500\u2500\u2500\u2518");
		if (ends)
			printf("PASS: a painted rectangle draws its corners and rules\n");
		else {
			printf("FAIL: a painted rectangle draws its corners and rules\n"
			       "      row0 [%s] row3 [%s]\n",
			       qPrintable(rows.value(0)), qPrintable(rows.value(3)));
			++r;
		}
		if (sides)
			printf("PASS: and its sides, which only a box three rows tall has\n");
		else {
			printf("FAIL: and its sides, which only a box three rows tall has\n"
			       "      row1 [%s]\n", qPrintable(rows.value(1)));
			++r;
		}
	}

	{
		// A fill in a colour matching no palette role -- what an application
		// paints for itself. The themed path is everywhere in this suite and
		// this branch had no caller: the one case that used to reach it was
		// the tab pane's gradient, and that was fixed by giving the frame a
		// role rather than by letting it fall through.
		Qtty::CellBuffer buf(6, 3);
		Qtty::CellPaintDevice dev(buf);
		{
			QPainter p(&dev);
			p.fillRect(QRect(0, 0, GridMetrics::cw() * 4, GridMetrics::ch() * 2),
			           QColor(3, 250, 137));
		}
		const bool filled = buf.at(0, 0).bg.kind() == Qtty::Color::Rgb
		                 && buf.at(3, 1).bg.kind() == Qtty::Color::Rgb;
		const bool bounded = buf.at(4, 0).bg.kind() == Qtty::Color::Default
		                  && buf.at(0, 2).bg.kind() == Qtty::Color::Default;
		if (filled)
			printf("PASS: an unthemed fill keeps the application's own colour\n");
		else {
			printf("FAIL: an unthemed fill keeps the application's own colour\n");
			++r;
		}
		if (bounded)
			printf("PASS: and stops at the rectangle it was given\n");
		else {
			printf("FAIL: and stops at the rectangle it was given\n");
			++r;
		}
	}

	// -- the snapshot harness's own failure paths ----------------------------
	// check_snapshot() writes a fixture and reads one, and both halves
	// answered with a sentence they had not tested. Recording dropped
	// open()'s result, so on a path it could not write it printed
	// "new fixture <path>" and returned 0 having written nothing -- to a
	// reader who had just been told by the other half to run with --record.
	// And that other half called every open() failure "missing", which covers
	// a mode-000 file and a directory at the path just as well, neither of
	// which --record can fix.
	//
	// The helper prints its diagnosis to stderr, and a literal "FAIL:" line
	// from a check that is passing would read as a failure to anything
	// grepping this log -- so stderr is captured for the length of each call,
	// which is also what makes the sentence itself assertable.
	{
		QTemporaryDir tmp;
		QString said;
		const auto run = [&](const QString &root, const QString &name,
		                     const QString &got, bool record) {
			const QByteArray log = tmp.filePath(QStringLiteral("err.txt")).toUtf8();
			fflush(stderr);
			const int saved = ::dup(2);
			const int to = ::open(log.constData(),
			                      O_WRONLY | O_CREAT | O_TRUNC, 0600);
			if (to >= 0) ::dup2(to, 2);
			const int rc = Qtty::test::check_snapshot(root, name, got, record);
			fflush(stderr);
			if (to >= 0) { ::dup2(saved, 2); ::close(to); }
			::close(saved);
			QFile f(QString::fromUtf8(log));
			said = f.open(QIODevice::ReadOnly)
			     ? QString::fromUtf8(f.readAll()) : QString();
			return rc;
		};

		if (!tmp.isValid()) {
			printf("SKIP: no temporary directory, so the harness's own"
			       " failure paths are untested\n");
		} else {
			QDir(tmp.path()).mkpath(QStringLiteral("test/snapshot"));
			const QString fixture =
			    tmp.filePath(QStringLiteral("test/snapshot/probe.txt"));

			// The control. Without it the two refusals below would pass
			// against a helper that had simply stopped writing anything.
			const int wrote = run(tmp.path(), QStringLiteral("probe"),
			                      QStringLiteral("one\ntwo\n"), true);
			QFile made(fixture);
			const bool exact = made.open(QIODevice::ReadOnly)
			                && QString::fromUtf8(made.readAll())
			                   == QStringLiteral("one\ntwo\n");
			made.close();
			if (wrote == 0 && exact)
				printf("PASS: recording a fixture writes what it was handed\n");
			else {
				printf("FAIL: recording a fixture writes what it was handed\n"
				       "      rc %d, bytes match %d\n", wrote, int(exact));
				++r;
			}

			if (run(QStringLiteral("/no/such/root"), QStringLiteral("probe"),
			        QStringLiteral("x"), true) == 1)
				printf("PASS: and recording where nothing can be written"
				       " fails instead of claiming success\n");
			else {
				printf("FAIL: and recording where nothing can be written"
				       " fails instead of claiming success\n");
				++r;
			}

			const int absent = run(tmp.path(),
			                       QStringLiteral("never-recorded"),
			                       QStringLiteral("x"), false);
			if (absent == 1 && said.contains(QStringLiteral("does not exist")))
				printf("PASS: a fixture that is not there is diagnosed as"
				       " not being there\n");
			else {
				printf("FAIL: a fixture that is not there is diagnosed as"
				       " not being there\n      rc %d, said: %s\n",
				       absent, qPrintable(said.trimmed()));
				++r;
			}

			// The other cause, which must NOT be called absence. Skipped for
			// a user who can read anything, since the fixture would open.
			QFile::setPermissions(fixture, QFileDevice::Permissions());
			if (::geteuid() == 0 || QFile(fixture).open(QIODevice::ReadOnly)) {
				printf("SKIP: this user can read a mode-000 file, so the"
				       " unreadable-fixture message is untested\n");
			} else {
				const int unreadable =
				    run(tmp.path(), QStringLiteral("probe"),
				        QStringLiteral("x"), false);
				if (unreadable == 1
				    && said.contains(QStringLiteral("could not be read")))
					printf("PASS: while one that cannot be read is not"
					       " called missing\n");
				else {
					printf("FAIL: while one that cannot be read is not"
					       " called missing\n      rc %d, said: %s\n",
					       unreadable, qPrintable(said.trimmed()));
					++r;
				}
			}
		}
	}

	return r;
}
