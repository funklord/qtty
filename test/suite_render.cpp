// suite_render -- Gate-1 regression as a snapshot (section 9).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <QDirIterator>
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

	// ONE LINE OF RICH TEXT IS ONE ROW, whatever font sizes are on it.
	// The row a run lands in is computed from its own TOP -- baseline
	// minus its own ascent -- so two runs Qt placed on the same line
	// landed on different rows the moment their ascents differed.
	//
	// Measured through a QTextBrowser, before: `x<sub>2</sub> world` put
	// "x  world" on one row and a lone "2" on the next, and a 24pt word
	// followed by ordinary text split the same way. Both read as a
	// missing character and a stray line rather than as one sentence.
	{
		const auto rows_of = [](const QString &html) {
			QTextBrowser br;
			br.setAttribute(Qt::WA_DontShowOnScreen);
			br.setFrameShape(QFrame::NoFrame);
			br.setHtml(html);
			br.resize(GridMetrics::cells(30, 6));
			br.show();
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(30, 6);
			Qtty::render_once(br, b);
			QStringList out;
			for (const QString &line : b.to_text().split(QLatin1Char('\n')))
				if (!line.trimmed().isEmpty()) out << line.trimmed();
			return out;
		};
		const QStringList sub = rows_of(QStringLiteral("x<sub>2</sub> world"));
		if (sub.size() == 1 && sub.value(0).startsWith(QStringLiteral("x2")))
			printf("PASS: a subscript stays on the line it belongs to rather "
			       "than dropping to the row below\n");
		else {
			printf("FAIL: a subscript stays on the line it belongs to rather "
			       "than dropping to the row below\n      got %d row(s), "
			       "first '%s'\n", int(sub.size()),
			       qPrintable(sub.value(0)));
			++r;
		}
		const QStringList big = rows_of(QStringLiteral(
		    "<span style='font-size:24pt'>hi</span> world"));
		if (big.size() == 1 && big.value(0).contains(QStringLiteral("world")))
			printf("PASS: and a bigger word and the text after it are one "
			       "line, not two\n");
		else {
			printf("FAIL: and a bigger word and the text after it are one "
			       "line, not two\n      got %d row(s), first '%s'\n",
			       int(big.size()), qPrintable(big.value(0)));
			++r;
		}
		// THE CONTROL, and it is what stops the rule swallowing a
		// document: two paragraphs are two lines and must stay two rows.
		// Without this the check above passes for an engine that puts
		// everything on the first row it drew.
		const QStringList two = rows_of(QStringLiteral("<p>one</p><p>two</p>"));
		if (two.size() == 2)
			printf("PASS: while two paragraphs are still two rows, which is "
			       "what says the rule joins a line rather than a "
			       "document\n");
		else {
			printf("FAIL: while two paragraphs are still two rows, which is "
			       "what says the rule joins a line rather than a "
			       "document\n      got %d row(s)\n", int(two.size()));
			++r;
		}
		// AND THE CONTROL FOR THE OTHER HALF OF THE RULE, the lower
		// bound, which the three above cannot reach: a text document
		// lays out downwards, so a later run's top is never above the
		// previous baseline by more than a row and the bound is never
		// the thing that refuses. One painter drawing bottom-up is,
		// and it is an ordinary thing for a widget to do -- paint the
		// footer before the title. Without the bound the second call
		// adopts the first's row and the two collide on one line.
		Qtty::CellBuffer b(20, 6);
		{
			Qtty::CellPaintDevice dev(b);
			QPainter p(&dev);
			p.setFont(QGuiApplication::font());
			p.drawText(QPoint(0, 4 * GridMetrics::ch()),
			           QStringLiteral("lower"));
			p.drawText(QPoint(0, 1 * GridMetrics::ch()),
			           QStringLiteral("upper"));
		}
		const QStringList painted = b.to_text().split(QLatin1Char('\n'));
		if (painted.value(0).startsWith(QStringLiteral("upper"))
		    && painted.value(3).startsWith(QStringLiteral("lower")))
			printf("PASS: and a run drawn above one already drawn keeps its "
			       "own row, which is the rule's lower bound\n");
		else {
			printf("FAIL: and a run drawn above one already drawn keeps its "
			       "own row, which is the rule's lower bound\n      row 0 "
			       "'%s', row 3 '%s'\n",
			       qPrintable(painted.value(0).trimmed()),
			       qPrintable(painted.value(3).trimmed()));
			++r;
		}
	}

	// AN UNDERLINE QT DOES NOT PUT ON THE FONT, which is the form every
	// editor uses for a misspelling and every rich-text style sheet uses
	// for a dotted or dashed rule. QTextCharFormat spells an underline two
	// ways and only one reaches the text item: setFontUnderline() sets
	// QFont::underline(), while setUnderlineStyle() touches no font
	// property at all, so the run arrives plain and Qt's own decoration is
	// the only evidence there is.
	//
	// Measured before this: a DotLine drew a rule of box-drawing glyphs on
	// the row BELOW the word -- which reads as a rule under the paragraph
	// rather than a mark on the word -- and a WaveUnderline drew NOTHING,
	// its two-pixel fill being thinner than half a cell.
	//
	// THE FIXTURE'S ORDER IS LOAD-BEARING. QTextCharFormat::setFont()
	// writes the font properties, fontUnderline among them, so setting the
	// style before the font clears it again -- the first version of this
	// probe reported that Qt draws nothing at all, which is what a cleared
	// style looks like from outside.
	{
		const auto marked = [](QTextCharFormat::UnderlineStyle style) {
			QTextEdit e;
			e.setAttribute(Qt::WA_DontShowOnScreen);
			e.resize(GridMetrics::cells(28, 5));
			e.show();
			QCoreApplication::processEvents();
			QTextCharFormat f;
			f.setFont(e.font());
			if (style != QTextCharFormat::NoUnderline)
				f.setUnderlineStyle(style);
			QTextCursor c = e.textCursor();
			c.insertText(QStringLiteral("hello"), f);
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(28, 5);
			Qtty::render_once(e, b);
			int row = -1, col = -1;
			for (int y = 0; y < b.rows() && row < 0; ++y)
				for (int x = 0; x + 4 < b.cols(); ++x)
					if (b.at(x, y).ch == QStringLiteral("h")
					    && b.at(x + 1, y).ch == QStringLiteral("e")) {
						row = y; col = x; break;
					}
			struct Seen { bool underlined = false; bool rule_below = false; };
			Seen out;
			if (row >= 0) {
				out.underlined = bool(b.at(col, row).attrs & Qtty::Attr::Underline)
				              && bool(b.at(col + 4, row).attrs
				                      & Qtty::Attr::Underline);
				if (row + 1 < b.rows())
					out.rule_below =
					    b.at(col, row + 1).ch == QStringLiteral("\u2500");
			}
			return out;
		};
		const auto wave = marked(QTextCharFormat::WaveUnderline);
		const auto dots = marked(QTextCharFormat::DotLine);
		const auto none = marked(QTextCharFormat::NoUnderline);
		if (wave.underlined && !wave.rule_below)
			printf("PASS: a spell-check squiggle marks the word it is under "
			       "rather than drawing nothing\n");
		else {
			printf("FAIL: a spell-check squiggle marks the word it is under "
			       "rather than drawing nothing\n");
			++r;
		}
		if (dots.underlined && !dots.rule_below)
			printf("PASS: and a dotted underline marks the word rather than "
			       "ruling the row beneath it\n");
		else {
			printf("FAIL: and a dotted underline marks the word rather than "
			       "ruling the row beneath it\n");
			++r;
		}
		// THE CONTROL, without which both lines above pass for a decoder
		// that underlines everything it draws.
		if (!none.underlined)
			printf("PASS: while text with no underline style carries no "
			       "underline, which is what says the two above measured "
			       "the style\n");
		else {
			printf("FAIL: while text with no underline style carries no "
			       "underline, which is what says the two above measured "
			       "the style\n");
			++r;
		}
	}

	// RICH TEXT'S OWN ATTRIBUTES, found by rendering a QTextBrowser --
	// eleven uses across the consumers and nothing here had drawn one.
	// `<b>`, `<i>` and `<u>` reached the cells; `<s>` reached nothing, and
	// the underline arrived TWICE.
	{
		QTextBrowser br;
		br.setAttribute(Qt::WA_DontShowOnScreen);
		br.setHtml(QStringLiteral("<u>under</u> <s>struck</s>"));
		br.resize(GridMetrics::cells(30, 5));
		br.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer b(30, 5);
		Qtty::render_once(br, b);
		const QString snap = b.to_snapshot();
		// Through the SNAPSHOT, because that is where an attribute is
		// visible at all: the glyph plane says "struck" either way.
		if (snap.contains(QStringLiteral("strike")))
			printf("PASS: rich text's strikethrough reaches the cells as an "
			       "attribute rather than as unmarked characters\n");
		else {
			printf("FAIL: rich text's strikethrough reaches the cells as an "
			       "attribute rather than as unmarked characters\n");
			++r;
		}
		// And the underline exactly once. Qt draws it as QFont::underline()
		// on the text item AND as a separate line just below the baseline,
		// so honouring both put a rule of box-drawing glyphs on the next
		// cell row under every underlined word.
		// The row BELOW the word, not any row: a QTextBrowser draws its own
		// frame, and its border is made of the same box-drawing character.
		// The first version of this check matched the frame and reported a
		// defect that was not there.
		const QStringList rows = b.to_text().split(QLatin1Char('\n'));
		int word = -1;
		for (int i = 0; i < rows.size(); ++i)
			if (rows.at(i).contains(QStringLiteral("under"))) word = i;
		const bool rule = word >= 0 && word + 1 < rows.size()
		               && rows.at(word + 1).contains(QStringLiteral("──"));
		printf("info: the word is on row %d; the row under it is |%s|\n",
		       word, qPrintable(rows.value(word + 1)));
		if (!rule && snap.contains(QStringLiteral("underline")))
			printf("PASS: and its underline is the attribute alone, not the "
			       "attribute and a drawn rule under it\n");
		else {
			printf("FAIL: and its underline is the attribute alone, not the "
			       "attribute and a drawn rule under it\n");
			++r;
		}
	}
	{
		// THE APPLICATION'S FONT DOES NOT MOVE THE GRID. setup() installs a
		// monospace font and derives the cell from it; an application is
		// free to call QApplication::setFont() afterwards -- a settings
		// dialog, a theme, a zoom control -- and a terminal's columns must
		// not move when it does.
		//
		// A PROPORTIONAL family is the case that could hurt, since its
		// advances are not one cell each. What makes this hold is that the
		// engine walks clusters by cell width rather than by the font's
		// advances; a change reaching for font metrics to place columns
		// would break it quietly, and this is what would say so.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		auto *edit = new QLineEdit(QStringLiteral("iiiii WWWWW"));
		v->addWidget(edit);
		win.resize(GridMetrics::cells(20, 4));
		win.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer before(20, 4);
		Qtty::render_once(win, before);
		const int cell_w = GridMetrics::cw(), cell_h = GridMetrics::ch();

		const QFont kept = QApplication::font();
		QFont proportional(QStringLiteral("DejaVu Sans"));
		proportional.setPixelSize(cell_h - 3);
		QApplication::setFont(proportional);
		for (QWidget *w : QApplication::allWidgets()) w->setFont(proportional);
		QCoreApplication::processEvents();
		// Read WHILE the proportional font is installed: an assertion taken
		// after the restore below is true whatever the enforcer did, which
		// is how the first version of this passed against broken code.
		const QString family_while_set = edit->font().family();
		Qtty::CellBuffer after(20, 4);
		Qtty::render_once(win, after);
		QApplication::setFont(kept);
		for (QWidget *w : QApplication::allWidgets()) w->setFont(kept);
		QCoreApplication::processEvents();
		// The guard is told to forget what the proportional font did to the
		// widgets while it was installed. Laying a form out in a font whose
		// advances are not one cell each puts geometries off the grid by
		// definition -- that is the state under test, not a fault -- and the
		// suite's own idiom for a fixture that produces one deliberately is
		// to reset afterwards, as the wheel and show() cases do.
		Qtty::GridGuard::reset();

		// TWO MECHANISMS, and the check below cannot tell which one held --
		// which is why this line is here. setup() installs a font enforcer
		// that puts the grid's family back on every widget at FontChange, so
		// the proportional font never reaches the paint engine; and if it
		// did, the engine walks clusters by CELL width rather than by the
		// font's advances. Either alone is enough, so no single sabotage can
		// redden the picture comparison -- 8.151's shape, met while writing
		// the check rather than a run later.
		//
		// This one names the mechanism a sabotage can reach: the enforcer.
		if (family_while_set == kept.family())
			printf("PASS: a font set on every widget is put back by the "
			       "enforcer, which is the first of the two things keeping "
			       "the grid still\n");
		else {
			printf("FAIL: a font set on every widget is put back by the "
			       "enforcer, which is the first of the two things keeping "
			       "the grid still\n");
			printf("      the field kept [%s]\n",
			       qPrintable(family_while_set));
			++r;
		}
		if (after.to_text() == before.to_text()
		    && GridMetrics::cw() == cell_w && GridMetrics::ch() == cell_h)
			printf("PASS: a font the application changes after setup moves "
			       "neither the cell nor a glyph\n");
		else {
			printf("FAIL: a font the application changes after setup moves "
			       "neither the cell nor a glyph\n");
			printf("      before [%s]\n      after  [%s]\n",
			       qPrintable(before.to_text()), qPrintable(after.to_text()));
			++r;
		}
	}

	{
		// WHAT THE ENFORCER LEAVES ALONE, which is the half an application
		// depends on and nothing asserted. It replaces the FACE and the
		// SIZE, because a cell is one glyph wide and the grid rests on
		// that; it keeps bold, italic and underline, because those are the
		// application's meaning rather than the grid's measurement.
		//
		// Nothing said so, and the direction of a silent failure here is
		// bad: every bold label in a program would go plain with no error
		// anywhere, and the check above -- which asks only whether the
		// family came back -- would pass throughout.
		//
		// The measurement an application would make, since a font's return
		// value is the only thing it can read: `QFontDialog::currentFont()`
		// is defined as the sample widget's font, so a font chooser here
		// hands back the weight the user picked and the grid's face. 8.202
		// records that with the numbers.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(20, 4));
		auto *field = new QLineEdit(&host);
		host.show();
		QCoreApplication::processEvents();
		const QFont grid = QApplication::font();
		QFont want(QStringLiteral("Courier"), 10);
		want.setBold(true);
		want.setItalic(true);
		want.setUnderline(true);
		field->setFont(want);
		QCoreApplication::processEvents();
		const QFont kept = field->font();
		printf("info: asked Courier 10 bold+italic+underline, kept [%s] "
		       "px=%d b=%d i=%d u=%d\n", qPrintable(kept.family()),
		       kept.pixelSize(), int(kept.bold()), int(kept.italic()),
		       int(kept.underline()));
		if (kept.family() == grid.family()
		    && kept.pixelSize() == grid.pixelSize())
			printf("PASS: a face an application asks for is replaced by the "
			       "grid's, which is the measurement a cell rests on\n");
		else {
			printf("FAIL: a face an application asks for is replaced by the "
			       "grid's, which is the measurement a cell rests on\n");
			++r;
		}
		if (kept.bold() && kept.italic() && kept.underline())
			printf("PASS: and its bold, italic and underline are kept, "
			       "those being the application's meaning rather than the "
			       "grid's measurement\n");
		else {
			printf("FAIL: and its bold, italic and underline are kept, "
			       "those being the application's meaning rather than the "
			       "grid's measurement\n");
			++r;
		}
	}

	{
		// TWO LINES A CELL APART BOTH SURVIVE, and two lines closer than
		// that do not. The second half is a limit rather than a defect to
		// fix here, and it is pinned so that it is a known shape rather
		// than a surprise: a cell row is the unit, so a widget drawing its
		// lines 14 pixels apart in a 19-pixel grid has asked for one row
		// twice and the later line wins.
		//
		// Measured against Qt's own QCommandLinkButton, which paints its
		// title and description itself rather than through the style:
		//
		//     text [Link Button] at 33.0,21.0 -> row 1 col 3
		//     text [Description] at 33.0,35.0 -> row 1 col 3
		//
		// -- so the title vanished under the description, in Qt's own
		// widget, with the application doing nothing wrong. project.md
		// 8.166 carries the finding and what the options cost.
		const double a = QFontMetricsF(QGuiApplication::font()).ascent();
		const double ch = GridMetrics::ch();
		Qtty::CellBuffer b(24, 4);
		{
			Qtty::CellPaintDevice dev(b);
			QPainter p(&dev);
			p.drawText(QPoint(0, GridMetrics::ch() - 4), QStringLiteral("first"));
			p.drawText(QPoint(0, 2 * GridMetrics::ch() - 4), QStringLiteral("second"));
		}
		// The collision in a buffer of its own, built from the metrics
		// rather than from a number that happens to work here: the row is
		// round((baseline - ascent) / ch), so baselines at 0.6 and 1.3 of a
		// row above the ascent both round to row 1 -- which is the 21-and-35
		// pair Qt's own command link button produces.
		Qtty::CellBuffer c(24, 4);
		{
			Qtty::CellPaintDevice dev(c);
			QPainter p(&dev);
			p.drawText(QPointF(0, a + ch * 0.6), QStringLiteral("apart"));
			p.drawText(QPointF(0, a + ch * 1.3), QStringLiteral("collided"));
		}
		const QString text = b.to_text() + c.to_text();
		if (b.to_text().contains(QStringLiteral("first"))
		    && b.to_text().contains(QStringLiteral("second")))
			printf("PASS: lines a cell apart each keep their own row\n");
		else {
			printf("FAIL: lines a cell apart each keep their own row\n");
			++r;
		}
		if (!text.contains(QStringLiteral("apart"))
		    && text.contains(QStringLiteral("collided")))
			printf("PASS: and two lines closer than a row share one, the "
			       "later one winning -- a limit, pinned so it cannot "
			       "change unnoticed\n");
		else {
			printf("FAIL: and two lines closer than a row share one, the "
			       "later one winning -- a limit, pinned so it cannot "
			       "change unnoticed\n");
			++r;
		}
	}

	{
		// The control for that suppression, and it is the whole of why it
		// is narrow: a rule the APPLICATION draws below underlined text
		// must survive. Only a horizontal line inside the band the last
		// underlined run occupies is dropped.
		Qtty::CellBuffer b(20, 4);
		{
			Qtty::CellPaintDevice dev(b);
			QPainter p(&dev);
			QFont f = QGuiApplication::font();
			f.setUnderline(true);
			p.setFont(f);
			p.drawText(QPoint(0, GridMetrics::ch() - 4),
			           QStringLiteral("Heading"));
			p.setFont(QGuiApplication::font());
			p.drawLine(0, GridMetrics::ch() * 2 + 9,
			           GridMetrics::cw() * 10, GridMetrics::ch() * 2 + 9);
		}
		if (b.to_text().contains(QStringLiteral("──────")))
			printf("PASS: and a rule the application draws under an "
			       "underlined heading is left alone\n");
		else {
			printf("FAIL: and a rule the application draws under an "
			       "underlined heading is left alone\n");
			++r;
		}
	}

	// AN INDEPENDENT READING OF THE FIXTURES, which is what a
	// regenerate-and-diff gate cannot give. `check_snapshot` proves a
	// fixture is what THIS renderer produces; it says nothing about whether
	// that is right, and it goes green in the same words either way -- the
	// generator is one witness, and asking it twice is not two.
	//
	// So these assertions read the committed file and nothing else. They
	// cannot be satisfied by re-recording, which is the property that makes
	// them worth having: 8.54 found four attribute names that had never
	// been printed by anything, and a fixture recorded with a wrong one
	// agrees with every later run for ever.
	if (!record) {
		// The row counts come from the CALL SITES above and below, not from
		// the fixtures. A fixture whose shape changed would otherwise be
		// absorbed silently by `make record`.
		const struct { const char *name; int rows; } fixtures[] = {
			{ "prefs_dialog",     14 },
			{ "widgets_gallery",  17 },
		};
		for (const auto &fx : fixtures) {
			QFile f(QStringLiteral(QTTY_SOURCE_DIR "/test/snapshot/")
			        + QLatin1String(fx.name) + QStringLiteral(".txt"));
			if (!f.open(QIODevice::ReadOnly)) {
				printf("FAIL: fixture %s cannot be read, so nothing below it"
				       " means anything\n", fx.name);
				++r;
				continue;
			}
			const QStringList line =
			    QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
			const int at_attrs = line.indexOf(QStringLiteral("--- attrs ---"));
			// The blink plane sits between them (8.238), so the attribute
			// plane now ends at ITS header rather than at the colours. Read
			// out of the file like everything else here: measuring the
			// attribute plane to the colours header would silently count
			// the blink plane as attribute rows, and the count would still
			// be a number.
			const int at_blink = line.indexOf(QStringLiteral("--- blink ---"));
			const int at_cols = line.indexOf(QStringLiteral("--- colours ---"));
			// A plane with nothing in it is one "(none)" line rather than
			// one line per row, so its body is not a row of symbols and
			// must not be read as one -- "(none)" would otherwise report
			// six undefined characters.
			const auto collapsed = [&](int at) {
				return at + 1 < line.size()
				    && line.at(at + 1) == QStringLiteral("(none)");
			};
			const bool blink_none = collapsed(at_blink);
			QString used, defined, blink_used, blink_defined;
			for (const QString &l : line) {
				if (l.startsWith(QStringLiteral("attrs:")))
					for (const QString &e :
					     l.mid(6).split(QStringLiteral(",")))
						if (!e.trimmed().isEmpty())
							defined += e.trimmed().at(0);
				if (l.startsWith(QStringLiteral("blink:")))
					for (const QString &e :
					     l.mid(6).split(QStringLiteral(",")))
						if (!e.trimmed().isEmpty())
							blink_defined += e.trimmed().at(0);
			}
			for (int i = at_attrs + 1; i > 0 && i < at_blink; ++i)
				for (const QChar c : line.at(i))
					if (c != QLatin1Char(' ') && !used.contains(c)) used += c;
			if (!blink_none)
				for (int i = at_blink + 1; i > 0 && i < at_cols; ++i)
					for (const QChar c : line.at(i))
						if (c != QLatin1Char(' ') && !blink_used.contains(c))
							blink_used += c;
			QString undefined;
			for (const QChar c : used)
				if (!defined.contains(c)) undefined += c;
			for (const QChar c : blink_used)
				if (!blink_defined.contains(c)) undefined += c;

			printf("info: %s has %d glyph row(s), %d attr row(s), %d blink"
			       " row(s), planes use [%s][%s], legends define [%s][%s]\n",
			       fx.name, at_attrs, at_blink - at_attrs - 1,
			       at_cols - at_blink - 1, qPrintable(used),
			       qPrintable(blink_used), qPrintable(defined),
			       qPrintable(blink_defined));
			// The blink plane is one row per glyph row like the others, or
			// the single collapsed line. Asserted rather than skipped: a
			// plane that came up short under a wide cluster is exactly the
			// failure the attribute plane's own count exists to catch, and
			// a new plane inherits the hazard rather than being exempt from
			// it.
			const int blink_rows = at_cols - at_blink - 1;
			if (at_attrs == fx.rows && at_blink - at_attrs - 1 == fx.rows
			    && blink_rows == (blink_none ? 1 : fx.rows))
				printf("PASS: %s carries one attribute row per glyph row, "
				       "both at the height the call asked for\n", fx.name);
			else {
				printf("FAIL: %s carries one attribute row per glyph row, "
				       "both at the height the call asked for\n", fx.name);
				++r;
			}
			// The half that a re-record cannot fix: a plane symbol the
			// legend does not define is a fixture nobody can read, and the
			// serialiser has a branch that emits `?` once it runs out of
			// alphabet -- which would land here.
			// Named, like its sibling above: this runs once per fixture,
			// and a verdict that does not say which one is a verdict
			// whose reader has to count lines to find out.
			if (undefined.isEmpty())
				printf("PASS: and every symbol %s's attribute and blink planes "
				       "use is named in its legend\n", fx.name);
			else {
				printf("FAIL: and every symbol %s's attribute and blink planes "
				       "use is named in its legend\n"
				       "      condition: %s defines no [%s]\n",
				       fx.name, fx.name, qPrintable(undefined));
				++r;
			}
		}
	}

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

	// The clip, which design.md section 5.4 names among the four things
	// updateState() carries -- `pen/brush/font/clip -> Attrs` -- and which
	// was the one of those four not implemented.
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

			// The FALLBACK, which coverage found untested: a translucent
			// fill over a ground this layer cannot resolve is laid down
			// opaque, and nothing asserted it. The rule is documented
			// beside the code and was defended by nothing -- gcov named
			// the two lines, `cell.bg = f.cell.bg; continue;`.
			//
			// A Default background is that case: it is the terminal's own
			// and unknown here.
			{
				Qtty::CellBuffer plainbg(4, 1);
				Qtty::CellPaintDevice dev(plainbg);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch),
				           QColor(0xff, 0x8b, 0x33, 80));
				p.end();
				const Qtty::Color got = plainbg.at(1, 0).bg;
				if (got.kind() == Qtty::Color::Rgb
				    && (got.value() & 0xffffff) == 0xff8b33u)
					printf("PASS: and over a ground it cannot resolve it is"
					       " laid down opaque\n");
				else {
					printf("FAIL: and over a ground it cannot resolve it is"
					       " laid down opaque\n");
					++r;
				}
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

		// A TEXTURE brush, the other kind whose QBrush::color() means
		// nothing and answers black. Checked beside the gradient because
		// they are one defect with two spellings, and fixing either alone
		// leaves brush_colour() answering wrongly for a case it claims.
		{
			QPixmap tex(4, 4);
			tex.fill(QColor(0x11, 0xaa, 0x44));
			Qtty::CellBuffer b(4, 1);
			{
				Qtty::CellPaintDevice dev(b);
				QPainter p(&dev);
				p.fillRect(QRect(0, 0, cw * 4, ch), QBrush(tex));
				p.end();
			}
			const Qtty::Color got = b.at(1, 0).bg;
			if (got.kind() == Qtty::Color::Rgb
			    && (got.value() & 0xffffff) == 0x11aa44u)
				printf("PASS: a texture fill takes its colour from the"
				       " texture\n");
			else {
				printf("FAIL: a texture fill takes its colour from the"
				       " texture\n");
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

		{
			// THE SAME WIDGET, TOO SMALL FOR A BOX. Branch coverage said the
			// `c.width() < 2 || c.height() < 2` arm of draw_placeholder had
			// never run: every unsupported widget the suite had rendered was
			// big enough to draw a frame and a label in.
			//
			// The whole point of the placeholder is that an unsupported
			// widget is not silently missing. Below two cells the box has no
			// interior, and measured with this arm deleted a one-cell widget
			// renders a bare "\u2518" -- the bottom-right corner of a frame
			// that has no other side. Not nothing, which was the guess, but
			// worse than nothing in one way: a stray corner reads as a
			// drawing fault in whatever is around it, where a shade block
			// reads as "something is here that qtty cannot draw".
			QGraphicsView tiny;
			tiny.setScene(new QGraphicsScene(&tiny));
			tiny.setFrameStyle(QFrame::NoFrame);
			tiny.setAttribute(Qt::WA_DontShowOnScreen);
			tiny.resize(GridMetrics::cells(1, 1));
			tiny.show();
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(3, 2);
			Qtty::render_once(tiny, b);
			const QString frame = b.to_text();
			printf("info: a one-cell unsupported widget renders [%s]\n",
			       qPrintable(frame.simplified()));
			if (frame.contains(QStringLiteral("\u2592")))
				printf("PASS: an unsupported widget too small for a box is "
				       "still a mark on the screen rather than nothing\n");
			else {
				printf("FAIL: an unsupported widget too small for a box is "
				       "still a mark on the screen rather than nothing\n");
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

		// A widget laid OVER another one's text, which is what an item
		// view's editor is and what nothing here had ever drawn. Two
		// separate faults met in this fixture and each alone leaves the
		// other's symptom, so the pair below asks two questions of one
		// frame:
		//
		//   columns          0123456789...
		//   the label        Wednesday
		//   the edit, 4..13      MXYZ------
		//
		//   both wrong       WednesdayMXYZ   text pushed past the label,
		//                                    nothing erased
		//   erase only       Wedn     MXYZ   erased, still pushed
		//   placement only   WednMXYZy       placed, label's tail left
		//   both right       WednMXYZ
		QWidget ov;
		ov.setAttribute(Qt::WA_DontShowOnScreen);
		ov.resize(GridMetrics::cells(20, 2));
		auto *beneath = new QLabel(QStringLiteral("Wednesday"), &ov);
		beneath->setGeometry(0, 0, cw * 14, ch);
		auto *on_top = new QLineEdit(QStringLiteral("MXYZ"), &ov);
		on_top->setFrame(false);
		on_top->setGeometry(cw * 4, 0, cw * 10, ch);
		ov.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer ob(20, 2);
		Qtty::render_once(ov, ob);
		const QString orow = ob.to_text().section(QLatin1Char('\n'), 0, 0);
		// Its OWN columns. A single painter pass draws the whole window,
		// so the rule that keeps consecutive runs of one text layout from
		// overlapping used to join two unrelated widgets and push the
		// second out of its geometry -- the column it was given was
		// discarded, and moving this edit changed nothing at all.
		if (orow.mid(4, 4) == QStringLiteral("MXYZ"))
			printf("PASS: a widget drawn over another's text lands in its"
			       " own columns\n");
		else {
			printf("FAIL: a widget drawn over another's text lands in its"
			       " own columns\n      condition: row '%s'\n",
			       qPrintable(orow));
			++r;
		}
		// And covers what it was drawn over. One cell high is the ordinary
		// height of a single-line widget in a terminal, and the erase was
		// gated on more than one cell in each direction -- so the text
		// beneath survived beside the text on top, which is how an item
		// view's editor left the cell it was editing legible underneath
		// what the user was typing.
		if (orow.mid(8, 6).trimmed().isEmpty())
			printf("PASS: and its ground erases the text it covers, a widget"
			       " one cell high included\n");
		else {
			printf("FAIL: and its ground erases the text it covers, a widget"
			       " one cell high included\n      condition: row '%s'\n",
			       qPrintable(orow));
			++r;
		}

		// The same question of the OTHER channel, because the two answer
		// it separately and a line edit only exercises one. A plain
		// QWidget with autoFillBackground reaches no style primitive at
		// all: its ground is a fill on the paint engine.
		//
		// It takes the OPAQUE path there -- the theme names Window, so
		// the fill writes whole cells -- and that path has never had a
		// cell-count guard. Worth saying precisely, because the guess it
		// replaces was wrong and a sabotage caught it: fill_rectf()'s
		// OTHER branch, the one for a surface role the theme leaves at
		// Color::Default, still refuses to erase anything narrower or
		// shorter than two cells. Restoring that guard leaves this check
		// green, which is how the wrong guess was found -- so the branch
		// is reached by no check in this suite, and this one does not
		// reach it either. Recorded in project.md 8.205 rather than
		// pretended away.
		QWidget fb;
		fb.setAttribute(Qt::WA_DontShowOnScreen);
		fb.resize(GridMetrics::cells(20, 2));
		auto *word = new QLabel(QStringLiteral("Wednesday"), &fb);
		word->setGeometry(0, 0, cw * 14, ch);
		auto *ground = new QWidget(&fb);
		ground->setAutoFillBackground(true);
		ground->setGeometry(cw * 4, 0, cw * 10, ch);
		fb.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer fbb(20, 2);
		Qtty::render_once(fb, fbb);
		const QString frow = fbb.to_text().section(QLatin1Char('\n'), 0, 0);
		ground->setGeometry(cw * 4, 0, cw * 10, ch * 2);
		QCoreApplication::processEvents();
		Qtty::CellBuffer fbb2(20, 2);
		Qtty::render_once(fb, fbb2);
		const QString frow2 = fbb2.to_text().section(QLatin1Char('\n'), 0, 0);
		if (frow.trimmed() == QStringLiteral("Wedn")
		    && frow2.trimmed() == QStringLiteral("Wedn"))
			printf("PASS: a plain autoFillBackground widget erases too, at one"
			       " cell high as at two\n");
		else {
			printf("FAIL: a plain autoFillBackground widget erases too, at one"
			       " cell high as at two\n      condition: one row '%s',"
			       " two rows '%s'\n", qPrintable(frow), qPrintable(frow2));
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
			printf("FAIL: an ICellPainted widget paints itself in cells --"
			       " it was never asked to\n");
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

		{
			// THE SAME HAIRLINE AS A PATH. The check above reaches
			// fill_rectf() through fillRect(); drawPath() is a second front
			// door with its own thin branch, and branch coverage said
			// `is_thin(path.boundingRect())` had never been true -- every
			// path this suite filled was big enough to cover a cell centre.
			//
			// The comment beside that branch says what it is for and what
			// happens without it: the scanline fill asks which cell CENTRES
			// lie inside the shape, and a one-pixel-wide path contains none,
			// so a caret or a rule drawn through QPainterPath rather than
			// fillRect draws nothing at all. Same picture, different Qt
			// call, and only one of the two roads was tested.
			// A BLANK cell under the hairline, which is the case the branch
			// is written for: a thin fill colours a cell only where its
			// glyph is a space, so that a caret over a letter leaves the
			// letter alone -- the check above. Over a blank it is the only
			// thing that puts the colour there at all.
			Qtty::CellBuffer hair(6, 1);
			hair.text(0, 0, QStringLiteral("ab def"));
			{
				Qtty::CellPaintDevice dev(hair);
				QPainter p(&dev);
				QPainterPath path;
				path.addRect(QRectF(GridMetrics::cw() * 2, 0,
				                    1, GridMetrics::ch()));
				p.fillPath(path, QColor(Qt::red));
			}
			const Qtty::Cell &c = hair.at(2, 0);
			const bool reddened = c.bg.kind() == Qtty::Color::Rgb
			                   && qRed(c.bg.value()) > 150
			                   && qGreen(c.bg.value()) < 100;
			printf("info: a hairline path left cell 2 as bg kind %d, glyph"
			       " '%s'\n", int(c.bg.kind()), qPrintable(c.ch));
			if (reddened && c.ch == QStringLiteral(" "))
				printf("PASS: a hairline drawn as a PATH colours the blank "
				       "cell it lands on, as the same shape does through "
				       "fillRect\n");
			else {
				printf("FAIL: a hairline drawn as a PATH colours the blank "
				       "cell it lands on, as the same shape does through "
				       "fillRect\n");
				++r;
			}
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

	// A QLCDNumber, READ BACK OFF ITS OWN SEGMENTS. It draws seven-segment
	// digits as filled polygons and routes none of it through QStyle, so
	// Channel B turned each segment into a box-drawing glyph and
	// display(1234.5) came out as four rows of rules and diagonals.
	//
	// The string it shows has no accessor, and Qt's own accessibility
	// interface answers QString::number(value()) -- which is 0 for every
	// display() of a string that does not parse, so a clock tells a screen
	// reader "0". Reading the segments is the only exact answer.
	{
		const auto lcd_rows = [](QLCDNumber &l, int cols, int rows) {
			l.setAttribute(Qt::WA_DontShowOnScreen);
			l.resize(GridMetrics::cells(cols, rows));
			l.show();
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(cols, rows);
			Qtty::render_once(l, b);
			QStringList out;
			for (const QString &line : b.to_text().split(QLatin1Char('\n')))
				out << line;
			return out;
		};
		// THE ORACLE for a string display is the string: whatever the
		// decoder does to the segments, display(s) must come back as s.
		// Chosen so that between them they light every segment and both
		// kinds of dot.
		struct Case { const char *shown; const char *expect; };
		static const Case cases[] = {
			{ "0123456789", "0123456789" },
			{ "ABCDEF",     "AbCdEF" },      // Qt's 'b' and 'd' are lower
			{ "12:34:56",   "12:34:56" },
			{ "1.5",        "1.5" },
			{ "-42",        "-42" },
		};
		int wrong = 0;
		QString first_bad;
		for (const Case &c : cases) {
			QLCDNumber l(int(qstrlen(c.shown)));
			l.display(QString::fromLatin1(c.shown));
			const QStringList r = lcd_rows(l, 24, 3);
			bool found = false;
			for (const QString &line : r)
				if (line.contains(QString::fromLatin1(c.expect))) found = true;
			if (!found) {
				++wrong;
				if (first_bad.isEmpty())
					first_bad = QStringLiteral("%1 -> %2")
					            .arg(QString::fromLatin1(c.shown),
					                 r.join(QLatin1Char('/')));
			}
		}
		if (wrong == 0)
			printf("PASS: every character a QLCDNumber can draw reads back off "
			       "its segments, digits, hex letters, colons and points\n");
		else {
			printf("FAIL: every character a QLCDNumber can draw reads back off "
			       "its segments, digits, hex letters, colons and points\n"
			       "      %d of %d wrong, first '%s'\n", wrong,
			       int(sizeof(cases) / sizeof(cases[0])),
			       qPrintable(first_bad));
			++r;
		}
		// A STRING WITH NO BAR LIT ANYWHERE, which is where the segment
		// length has to come from the uprights: taking it from the bars
		// reads zero here and the decode gives up. It is also the only
		// shape in which every digit takes the no-bar path, since a '1'
		// has no bar to say where its cell's edges are.
		QLCDNumber ones(3);
		ones.display(QStringLiteral("111"));
		bool got_ones = false;
		for (const QString &line : lcd_rows(ones, 24, 3))
			if (line.contains(QStringLiteral("111"))) got_ones = true;
		if (got_ones)
			printf("PASS: and three adjacent ones are three digits, with no "
			       "bar lit anywhere to measure them by\n");
		else {
			printf("FAIL: and three adjacent ones are three digits, with no "
			       "bar lit anywhere to measure them by\n");
			++r;
		}
		// AND THE CONTROL: nothing but the frame and the digits reaches a
		// cell. Read off the CELLS and not off to_text(), which took two
		// attempts to get right. The first control looked for the two
		// diagonal glyphs the old noise used and stayed green over a
		// sabotage that drew the segments as well as recording them,
		// because a filled polygon is not a diagonal; the second counted
		// non-blank characters and stayed green too. A filled polygon
		// writes a cell's BACKGROUND and leaves its glyph a space, so a
		// text-shaped check cannot see it at all -- and under the default
		// theme every role answers Color::Default, which makes any
		// coloured cell here raw painting by definition.
		QLCDNumber noisy(5);
		noisy.display(1234.5);
		noisy.setAttribute(Qt::WA_DontShowOnScreen);
		noisy.resize(GridMetrics::cells(24, 5));
		noisy.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer nb(24, 5);
		Qtty::render_once(noisy, nb);
		int painted = 0;
		for (int y = 0; y < 5; ++y)
			for (int x = 0; x < 24; ++x) {
				const Qtty::Cell &cell = nb.at(x, y);
				if (cell.bg.kind() != Qtty::Color::Default) ++painted;
			}
		if (painted == 0
		    && nb.to_text().split(QLatin1Char('\n')).value(2)
		       .contains(QStringLiteral("1235")))
			printf("PASS: while the segments themselves colour no cell, so the "
			       "digits replace the noise rather than covering it\n");
		else {
			printf("FAIL: while the segments themselves colour no cell, so the "
			       "digits replace the noise rather than covering it\n"
			       "      %d cell(s) carry a background:\n%s\n", painted,
			       qPrintable(nb.to_text()));
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


	// ---- which font the grid is laid on ------------------------------------
	//
	// The family and the size were hardcoded, so an application that wanted
	// another mono font could not have one and a user whose machine could
	// not carry the default had a fatal message naming a font they had no
	// way to change. project.md 0e carried it; grid_font_request() is the
	// answer, and it is asked here rather than through setup() because
	// setup() runs once per process and the question is which font it would
	// ask for.
	{
		const QByteArray had_family = qgetenv("QTTY_FONT");
		const QByteArray had_size = qgetenv("QTTY_FONT_SIZE");
		qunsetenv("QTTY_FONT");
		qunsetenv("QTTY_FONT_SIZE");
		Qtty::set_font(QString(), 0);                 // nothing chosen

		const QFont fallback = Qtty::grid_font_request();
		const bool default_ok =
		    fallback.family() == QStringLiteral("DejaVu Sans Mono")
		    && fallback.pixelSize() == 16
		    && fallback.hintingPreference() == QFont::PreferFullHinting;
		if (default_ok)
			printf("PASS: with nobody asking, the grid is laid on DejaVu Sans"
			       " Mono at 16 pixels with full hinting\n");
		else {
			printf("FAIL: with nobody asking, the grid is laid on DejaVu Sans"
			       " Mono at 16 pixels with full hinting\n");
			++r;
		}

		qputenv("QTTY_FONT", "Liberation Mono");
		qputenv("QTTY_FONT_SIZE", "18");
		const QFont from_env = Qtty::grid_font_request();
		if (from_env.family() == QStringLiteral("Liberation Mono")
		    && from_env.pixelSize() == 18
		    && from_env.hintingPreference() == QFont::PreferFullHinting)
			printf("PASS: the environment can name the family and the size,"
			       " which is the lever a user has when a machine cannot"
			       " carry the default\n");
		else {
			printf("FAIL: the environment can name the family and the size,"
			       " which is the lever a user has when a machine cannot"
			       " carry the default\n");
			++r;
		}

		Qtty::set_font(QStringLiteral("Noto Mono"), 20);
		const QFont from_app = Qtty::grid_font_request();
		if (from_app.family() == QStringLiteral("Noto Mono")
		    && from_app.pixelSize() == 20)
			printf("PASS: and an application's own choice wins over it, a"
			       " program that names a font having usually measured"
			       " something against it\n");
		else {
			printf("FAIL: and an application's own choice wins over it, a"
			       " program that names a font having usually measured"
			       " something against it\n");
			++r;
		}

		// A SIZE THAT IS NOT A SIZE is ignored rather than obeyed: a cell of
		// zero pixels divides into everything the grid computes.
		Qtty::set_font(QString(), 0);
		qputenv("QTTY_FONT_SIZE", "nonsense");
		const int junk = Qtty::grid_font_request().pixelSize();
		qputenv("QTTY_FONT_SIZE", "0");
		const int zero = Qtty::grid_font_request().pixelSize();
		// AND ONE THAT IS ABSURD, which is the half the other two cannot
		// ask: nonsense and zero both come back as 0 and the default
		// below catches them whatever the range test does, so a check
		// resting on those two passed with the range test deleted -- the
		// harness said so. A cell of 100000 pixels is larger than any
		// terminal, and a grid computed from it has one column.
		qputenv("QTTY_FONT_SIZE", "100000");
		const int absurd = Qtty::grid_font_request().pixelSize();
		if (junk == 16 && zero == 16 && absurd == 16)
			printf("PASS: while a size that is not a number, is zero, or is"
			       " larger than any terminal leaves the default standing"
			       " rather than laying the grid on it\n");
		else {
			printf("FAIL: while a size that is not a number, is zero, or is"
			       " larger than any terminal leaves the default standing"
			       " rather than laying the grid on it\n");
			++r;
		}

		// A FONT FILE, which is the third lever and the one that can carry
		// a family the machine has not installed -- the half of design.md
		// 5.3's bundled font that belongs to this library, the other half
		// being which font to ship and whose licence that is.
		//
		// WHAT THESE ESTABLISH is the plumbing: a bad path is refused, a
		// real file yields the family Qt took from it, the environment
		// reaches grid_font_request() through it, and an application's own
		// choice still wins. That a registered family need not be
		// INSTALLED is QFontDatabase::addApplicationFont()'s own
		// behaviour, cited rather than re-proved -- proving it here would
		// need a font this machine does not have, which is a fixture no
		// check can carry.
		{
			// Any font file the machine has. Named by search rather than
			// by path, because a hardcoded one is a check that passes on
			// the machine it was written on.
			QString file;
			const QStringList roots{QStringLiteral("/usr/share/fonts"),
			                        QStringLiteral("/usr/local/share/fonts")};
			for (const QString &root : roots) {
				if (!file.isEmpty()) break;
				QDirIterator it(root, QStringList{QStringLiteral("*.ttf")},
				                QDir::Files, QDirIterator::Subdirectories);
				if (it.hasNext()) file = it.next();
			}
			if (file.isEmpty()) {
				// A SKIP rather than a pass, and loud: this machine has no
				// font file, so nothing below was measured.
				printf("SKIP: no font file under /usr/share/fonts, so the"
				       " font-file lever was not measured here\n");
			} else {
				qunsetenv("QTTY_FONT");
				qunsetenv("QTTY_FONT_SIZE");
				Qtty::set_font(QString(), 0);
				const QString bad =
				    Qtty::add_font_file(QStringLiteral("/nonexistent.ttf"));
				const QString good = Qtty::add_font_file(file);
				if (bad.isEmpty() && !good.isEmpty())
					printf("PASS: a font file names the family it holds, and"
					       " a path that is not one answers empty rather"
					       " than a family nothing can resolve\n");
				else {
					printf("FAIL: a font file names the family it holds, and"
					       " a path that is not one answers empty rather"
					       " than a family nothing can resolve\n");
					++r;
				}
				// The environment reaches the same place, which is the
				// distributor's lever: a family nobody named comes from
				// the file.
				Qtty::set_font(QString(), 0);
				qputenv("QTTY_FONT_FILE", file.toLocal8Bit());
				const QFont from_file = Qtty::grid_font_request();
				if (!good.isEmpty() && from_file.family() == good)
					printf("PASS: and QTTY_FONT_FILE supplies the family when"
					       " nobody named one\n");
				else {
					printf("FAIL: and QTTY_FONT_FILE supplies the family when"
					       " nobody named one\n");
					++r;
				}
				// And the documented order: the application still wins.
				Qtty::set_font(QStringLiteral("Noto Mono"), 20);
				const QFont app_wins = Qtty::grid_font_request();
				if (app_wins.family() == QStringLiteral("Noto Mono"))
					printf("PASS: while an application that named a family"
					       " keeps it, which is the order set_font already"
					       " has\n");
				else {
					printf("FAIL: while an application that named a family"
					       " keeps it, which is the order set_font already"
					       " has\n");
					++r;
				}
				qunsetenv("QTTY_FONT_FILE");
			}
		}

		if (had_family.isEmpty()) qunsetenv("QTTY_FONT");
		else qputenv("QTTY_FONT", had_family);
		if (had_size.isEmpty()) qunsetenv("QTTY_FONT_SIZE");
		else qputenv("QTTY_FONT_SIZE", had_size);
		Qtty::set_font(QString(), 0);
		if (Qtty::grid_font_request().family()
		    == QStringLiteral("DejaVu Sans Mono"))
			printf("PASS: and the process is left as it was found, this being"
			       " the one check that can change what every later font"
			       " is\n");
		else {
			printf("FAIL: and the process is left as it was found, this being"
			       " the one check that can change what every later font"
			       " is\n");
			++r;
		}
	}


	// ---- the differential test design.md section 9 asks for ---------------
	//
	// "The same model driven through GUI and TUI builds must produce the
	// same observable state after the same event script -- catching logic
	// that accidentally lives in the view." project.md 7.5 listed it as
	// absent entirely, and it was.
	//
	// A REAL GUI reference is possible here, which is what makes the test
	// worth writing: a window that is mapped and activated -- no
	// WA_DontShowOnScreen, activateWindow() -- gets Qt's own focus,
	// hasFocus() and shortcut map under the offscreen platform. Measured:
	// isActiveWindow 1, Tab moves through Qt's chain, Ctrl+S fires a
	// QAction. So one side is Qt deciding everything and the other is this
	// library deciding it, and the comparison means something.
	//
	// WITH THE CONVENTIONS OFF, because the bundle exists to diverge: with
	// them on, Ctrl+A is start-of-line in a field where Qt selects all,
	// and the differential would be measuring the feature. Off is the
	// unmodified-application contract, and that is the one worth holding.
	//
	// It found one the day it was written -- the focus reason, 8.226.
	//
	// AND IT LIVES IN THIS SUITE BECAUSE NO ROUTER DOES. A live
	// Qtty::InputRouter stamps WA_DontShowOnScreen on every top-level shown
	// while it exists, which is its job (F7) and which stops the GUI side
	// ever mapping -- so written in suite_router, beside the other input
	// checks, the control below failed at once and said why. The control
	// is what made that legible rather than puzzling.
	{
		const bool had_conv = Qtty::keyboard_conventions();
		Qtty::set_keyboard_conventions(false);
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }

		struct Tree {
			QWidget *win = nullptr;
			QLineEdit *first = nullptr, *second = nullptr;
			QCheckBox *check = nullptr;
			QListWidget *list = nullptr;
			QPushButton *button = nullptr;
		};
		// ONE builder, so the two trees cannot differ by construction.
		const auto build = [](bool tui) {
			Tree t;
			t.win = new QWidget;
			if (tui) t.win->setAttribute(Qt::WA_DontShowOnScreen);
			t.win->resize(400, 300);
			t.first = new QLineEdit(t.win);
			t.first->setGeometry(0, 0, 200, 20);
			t.second = new QLineEdit(t.win);
			t.second->setGeometry(0, 30, 200, 20);
			t.check = new QCheckBox(QStringLiteral("on"), t.win);
			t.check->setGeometry(0, 60, 200, 20);
			t.list = new QListWidget(t.win);
			for (int i = 0; i < 4; ++i)
				t.list->addItem(QStringLiteral("row %1").arg(i));
			t.list->setGeometry(0, 90, 200, 80);
			t.button = new QPushButton(QStringLiteral("Go"), t.win);
			t.button->setGeometry(0, 180, 100, 20);
			return t;
		};
		const auto state = [](const Tree &t, int clicks) {
			QWidget *f = t.win->focusWidget();
			QString who = QStringLiteral("none");
			if (f == t.first) who = QStringLiteral("first");
			else if (f == t.second) who = QStringLiteral("second");
			else if (f == t.check) who = QStringLiteral("check");
			else if (f == t.list) who = QStringLiteral("list");
			else if (f == t.button) who = QStringLiteral("button");
			return QStringLiteral("[%1][%2] check=%3 row=%4 clicks=%5 on=%6")
			    .arg(t.first->text(), t.second->text())
			    .arg(int(t.check->isChecked()))
			    .arg(t.list->currentRow()).arg(clicks).arg(who);
		};
		struct Act { int key; const char *text; Qt::KeyboardModifiers mods; };
		static const Act script[] = {
			{0, "a", Qt::NoModifier}, {0, "b", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{0, "c", Qt::NoModifier},
			{Qt::Key_Backspace, "", Qt::NoModifier},
			{0, "d", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{Qt::Key_Space, " ", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{Qt::Key_Down, "", Qt::NoModifier},
			{Qt::Key_Down, "", Qt::NoModifier},
			{Qt::Key_Up, "", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{Qt::Key_Space, " ", Qt::NoModifier},
			{Qt::Key_Backtab, "", Qt::ShiftModifier},
			{Qt::Key_Backtab, "", Qt::ShiftModifier},
			{Qt::Key_A, "a", Qt::ControlModifier},
			{Qt::Key_Delete, "", Qt::NoModifier},
			{0, "z", Qt::NoModifier},
			{Qt::Key_Home, "", Qt::NoModifier},
			{Qt::Key_Right, "", Qt::NoModifier},
			{Qt::Key_End, "", Qt::NoModifier},
			{Qt::Key_Left, "", Qt::ShiftModifier},
			{Qt::Key_Escape, "", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{Qt::Key_Tab, "", Qt::NoModifier},
			{Qt::Key_Return, "", Qt::NoModifier},
			{Qt::Key_Space, " ", Qt::NoModifier},
		};

		Tree g = build(false);
		int gui_clicks = 0;
		QObject::connect(g.button, &QPushButton::clicked,
		                 [&gui_clicks] { ++gui_clicks; });
		g.win->show();
		g.win->activateWindow();
		g.first->setFocus();
		QCoreApplication::processEvents();
		const bool gui_is_real = g.win->isActiveWindow() && g.first->hasFocus();
		QStringList gui_trace;
		for (const Act &a : script) {
			QWidget *target = QApplication::focusWidget();
			if (!target) target = g.win;
			QKeyEvent down(QEvent::KeyPress, a.key, a.mods,
			               QString::fromLatin1(a.text));
			QApplication::sendEvent(target, &down);
			QKeyEvent up(QEvent::KeyRelease, a.key, a.mods,
			             QString::fromLatin1(a.text));
			QApplication::sendEvent(target, &up);
			QCoreApplication::processEvents();
			gui_trace << state(g, gui_clicks);
		}
		g.win->hide();
		QCoreApplication::processEvents();

		Tree t = build(true);
		int tui_clicks = 0;
		QObject::connect(t.button, &QPushButton::clicked,
		                 [&tui_clicks] { ++tui_clicks; });
		t.win->show();
		QCoreApplication::processEvents();
		Qtty::InputRouter dr(t.win);
		t.first->setFocus();
		Qtty::set_focus_widget(t.win->focusWidget());
		QCoreApplication::processEvents();
		QStringList tui_trace;
		for (const Act &a : script) {
			dr.on_key({a.key, QString::fromLatin1(a.text),
			           bool(a.mods & Qt::ControlModifier),
			           bool(a.mods & Qt::AltModifier),
			           bool(a.mods & Qt::ShiftModifier)});
			QCoreApplication::processEvents();
			tui_trace << state(t, tui_clicks);
		}

		// THE CONTROL FIRST. A GUI side that was not really active would
		// make every step agree for the wrong reason -- two windows
		// neither of which Qt is driving.
		if (gui_is_real)
			printf("PASS: the GUI side of the differential is a genuinely"
			       " active window, so what it does is Qt's answer rather"
			       " than a second copy of this library's\n");
		else {
			printf("FAIL: the GUI side of the differential is a genuinely"
			       " active window, so what it does is Qt's answer rather"
			       " than a second copy of this library's\n");
			++r;
		}
		int first_diff = -1;
		for (int i = 0; i < gui_trace.size() && first_diff < 0; ++i)
			if (gui_trace[i] != tui_trace[i]) first_diff = i;
		if (first_diff >= 0)
			printf("info: step %d\n  GUI %s\n  TUI %s\n", first_diff,
			       qPrintable(gui_trace[first_diff]),
			       qPrintable(tui_trace[first_diff]));
		if (first_diff < 0)
			printf("PASS: and the same event script leaves the same model"
			       " state in both builds, which is the differential test"
			       " design.md section 9 asks for\n");
		else {
			printf("FAIL: and the same event script leaves the same model"
			       " state in both builds, which is the differential test"
			       " design.md section 9 asks for\n");
			++r;
		}
		delete g.win;
		delete t.win;
		for (QWidget *w : hidden) w->show();
		QCoreApplication::processEvents();
		Qtty::set_keyboard_conventions(had_conv);
		Qtty::GridGuard::reset();
	}

	// ---- the seventh attribute, from the widget that asked for it (8.238) -
	//
	// `qtty.blink` is a dynamic property rather than anything read off a
	// QFont, because SGR 5 has no QFont property, no QTextCharFormat field
	// and no palette role to read. What that buys is what set_priority()
	// buys: nothing in a GUI build reads it, so the same application source
	// runs both ways, and it can be set from a .ui file by a program that
	// does not link qtty.
	//
	// THE CONTROL IS THE POINT. A pass that only shows a marked widget
	// blinking is equally consistent with everything blinking, which is what
	// a rectangle pass gets wrong first -- so the unmarked widget is
	// asserted to carry no blink at all, and the two-label case below asks
	// the question the single-widget case cannot: does the attribute land on
	// the widget that asked, or on the frame.
	{
		const auto measure = [&](bool set, bool value, int &blink,
		                         int &glyphs) {
			QLabel lab(QStringLiteral("alert"));
			lab.setAttribute(Qt::WA_DontShowOnScreen);
			if (set) lab.setProperty("qtty.blink", value);
			lab.resize(GridMetrics::cells(20, 3));
			lab.show();
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(20, 3);
			Qtty::render_once(lab, b);
			blink = 0;
			glyphs = 0;
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x) {
					const Qtty::Cell &c = b.at(x, y);
					if (c.ch != QStringLiteral(" ")) ++glyphs;
					if (c.attrs & Qtty::Attr::Blink) ++blink;
				}
		};

		int off_blink = 0, off_glyphs = 0;
		int on_blink = 0, on_glyphs = 0;
		int no_blink = 0, no_glyphs = 0;
		measure(false, false, off_blink, off_glyphs);
		measure(true, true, on_blink, on_glyphs);
		measure(true, false, no_blink, no_glyphs);
		printf("info: label without the property: %d glyph cell(s), %d"
		       " blinking; with it: %d and %d; with it false: %d and %d\n",
		       off_glyphs, off_blink, on_glyphs, on_blink, no_glyphs,
		       no_blink);

		if (off_glyphs > 0 && off_blink == 0)
			printf("PASS: a widget that was never given qtty.blink draws"
			       " text and none of it carries the attribute\n");
		else {
			printf("FAIL: a widget that was never given qtty.blink draws"
			       " text and none of it carries the attribute\n");
			++r;
		}
		// Every glyph and no blank, which is stronger than "some cell
		// blinks" and is the rule cell_geometry.h states: a blank cell has
		// nothing to blink, and marking the whole rectangle would bury the
		// text in the snapshot plane under a wall of 'b'.
		if (on_blink > 0 && on_blink == on_glyphs)
			printf("PASS: and setting qtty.blink puts the attribute on every"
			       " glyph that widget drew, and on no blank cell\n");
		else {
			printf("FAIL: and setting qtty.blink puts the attribute on every"
			       " glyph that widget drew, and on no blank cell\n");
			++r;
		}
		if (no_blink == 0 && no_glyphs == off_glyphs)
			printf("PASS: and qtty.blink set to false is the same widget as"
			       " one that never carried it\n");
		else {
			printf("FAIL: and qtty.blink set to false is the same widget as"
			       " one that never carried it\n");
			++r;
		}
	}
	{
		// Two labels in one window, one marked. The single-widget case
		// above cannot tell "the marked widget blinks" from "the window
		// blinks", because there the two are the same rectangle.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *column = new QVBoxLayout(&host);
		auto *quiet = new QLabel(QStringLiteral("quiet"), &host);
		auto *loud = new QLabel(QStringLiteral("loud"), &host);
		column->addWidget(quiet);
		column->addWidget(loud);
		loud->setProperty("qtty.blink", true);
		host.resize(GridMetrics::cells(24, 8));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer b(24, 8);
		Qtty::render_once(host, b);

		const QStringList rows = b.to_text().split(QLatin1Char('\n'));
		int at_quiet = -1, at_loud = -1;
		for (int i = 0; i < rows.size(); ++i) {
			if (rows.at(i).contains(QStringLiteral("quiet"))) at_quiet = i;
			if (rows.at(i).contains(QStringLiteral("loud"))) at_loud = i;
		}
		const auto blink_in = [&](int row) {
			int n = 0;
			if (row < 0 || row >= b.rows()) return -1;
			for (int x = 0; x < b.cols(); ++x)
				if (b.at(x, row).attrs & Qtty::Attr::Blink) ++n;
			return n;
		};
		printf("info: quiet on row %d with %d blinking cell(s), loud on row"
		       " %d with %d\n", at_quiet, blink_in(at_quiet), at_loud,
		       blink_in(at_loud));
		if (at_quiet >= 0 && at_loud >= 0 && at_quiet != at_loud
		    && blink_in(at_loud) > 0 && blink_in(at_quiet) == 0)
			printf("PASS: the attribute lands on the label that asked for it"
			       " and not on its unmarked sibling in the same window\n");
		else {
			printf("FAIL: the attribute lands on the label that asked for it"
			       " and not on its unmarked sibling in the same window\n");
			++r;
		}
	}

	return r;
}
