// suite_widgets -- section 17.2 Channel A coverage: targeted asserts per widget plus
// the widgets_gallery snapshot.
#include <qtty/qtty.h>
#include <qtty/delegate.h>
#include <QtWidgets>
#include <cstdio>
#include <functional>

using namespace Qtty;

static int fails = 0;
static bool g_record = false;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

static bool buffer_contains(const CellBuffer &b, const QString &glyph) {
	for (int y = 0; y < b.rows(); ++y)
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, y).ch == glyph) return true;
	return false;
}

static QPoint findText(const CellBuffer &b, const QString &s) {
	// Braced throughout, deliberately. tool/style_gate.py mis-nests a braceless
	// loop whose body opens a block -- it drops the braceless level at the inner
	// closing brace and then reports the following lines a tab too deep. This
	// function previously worked around that by indenting one line with two tabs
	// and four spaces, which is the mixed tab-then-space indent code-style.md
	// rule 2 forbids and which the gate accepts because it counts leading tabs.
	// Bracing is the honest fix: correct code that passes for the right reason.
	// The defect is signalled in claude-guidelines rather than worked around
	// further.
	for (int y = 0; y < b.rows(); ++y) {
		for (int x = 0; x + s.size() <= b.cols(); ++x) {
			bool ok = true;
			for (int i = 0; i < s.size(); ++i) {
				if (b.at(x + i, y).ch != QString(s[i])) { ok = false; break; }
			}
			if (ok) return {x, y};
		}
	}
	return {-1, -1};
}

static void show(QWidget &w, int cols, int rows) {
	w.setAttribute(Qt::WA_DontShowOnScreen);
	w.resize(GridMetrics::cells(cols, rows));
	w.show();
	QCoreApplication::processEvents();
}

// A delegate that reaches the style at no point at all: it writes its label
// through QPainter and never calls CE_ItemViewItem. That is what an
// application painting its own rows does, and it is the fixture that
// SEPARATES the two sites an alternating band is written at. The default
// delegate and CellItemDelegate both end up in CE_ItemViewItem, so neither
// can say whether the row panel the VIEW draws carries the band; this one
// can, because the row panel is the only thing that runs.
class OwnPaintDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *p, const QStyleOptionViewItem &opt,
	           const QModelIndex &index) const override {
		p->drawText(opt.rect, Qt::AlignLeft | Qt::AlignVCenter,
		            index.data(Qt::DisplayRole).toString());
	}
};

int suite_widgets() {
	fails = 0;
	// A STYLE SHEET TAKES A CONTROL'S TERMINAL AFFORDANCE AWAY. Nothing
	// in this tree mentioned style sheets before 8.86, and they are a
	// very common way a Qt application styles itself.
	//
	// Measured, a ten-cell push button at three settings:
	//
	//     no sheet                       <Hi>
	//     background/border/padding      Hi      -- the brackets are gone
	//     color: red                             -- nothing at all
	//
	// A QLabel with the same colour rule is unharmed, so this is about
	// controls rather than about drawing. The brackets are how a terminal
	// user knows a thing is a button, so the first row is not decoration:
	// a styled button still takes Enter and still shows focus, and NOTHING
	// ON SCREEN SAYS IT IS A BUTTON.
	//
	// PINNED AS MEASURED, NOT AS WANTED. Qt's style-sheet machinery takes
	// drawing over from the application style, which is what it is for;
	// what this check exists to catch is the behaviour CHANGING, in either
	// direction, since 0b carries the question of whether qtty should
	// notice a sheet at all.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const auto render = [&](QWidget &w) {
			w.setFixedSize(10 * cw, ch);
			show(w, 12, 2);
			CellBuffer b(12, 2);
			render_once(w, b);
			GridGuard::reset();
			return b.to_text().trimmed();
		};
		QPushButton plain(QStringLiteral("Hi"));
		QPushButton boxed(QStringLiteral("Hi"));
		boxed.setStyleSheet(QStringLiteral(
		    "background-color: red; border: 2px solid blue; padding: 6px;"));
		QPushButton tinted(QStringLiteral("Hi"));
		tinted.setStyleSheet(QStringLiteral("color: red;"));
		QLabel lab(QStringLiteral("Hi"));
		lab.setStyleSheet(QStringLiteral("color: red;"));

		const QString a1 = render(plain), a2 = render(boxed);
		const QString a3 = render(tinted), a4 = render(lab);
		printf("info: a button draws [%s]; with a box-model sheet [%s];"
		       " with a colour sheet [%s]; a label with one [%s]\n",
		       qPrintable(a1), qPrintable(a2), qPrintable(a3),
		       qPrintable(a4));
		CHECK(a1 == QStringLiteral("<Hi>") && a2 == QStringLiteral("Hi")
		      && a3.isEmpty() && a4 == QStringLiteral("Hi"),
		      "a style sheet costs a button its brackets, and a colour-only "
		      "one draws it not at all, while a label is unharmed -- pinned "
		      "as measured rather than as wanted");
	}


	// QGridLayout and QStackedWidget, neither of which this suite had ever
	// rendered, and both of which the consumers lean on: 37 uses of the
	// layout and 63 of the stack across the five Qt Widgets applications.
	{
		QWidget win;
		auto *g = new QGridLayout(&win);
		g->addWidget(new QLabel(QStringLiteral("Host")), 0, 0);
		g->addWidget(new QLineEdit(QStringLiteral("alpha")), 0, 1);
		g->addWidget(new QLabel(QStringLiteral("Port")), 1, 0);
		g->addWidget(new QSpinBox, 1, 1);
		// The spanning cell, which is what a grid layout does that a form
		// layout cannot, and the reason to render one at all.
		g->addWidget(new QPushButton(QStringLiteral("Connect")), 2, 0, 1, 2);
		show(win, 34, 6);
		CellBuffer b(34, 6);
		render_once(win, b);
		const QStringList rows = b.to_text().split(QLatin1Char('\n'));
		int host = -1, port = -1, conn = -1;
		for (int i = 0; i < rows.size(); ++i) {
			if (rows.at(i).contains(QStringLiteral("Host"))) host = i;
			if (rows.at(i).contains(QStringLiteral("Port"))) port = i;
			if (rows.at(i).contains(QStringLiteral("Connect"))) conn = i;
		}
		printf("info: grid layout -- Host row %d, Port row %d, Connect row"
		       " %d\n", host, port, conn);
		CHECK(host >= 0 && port > host && conn > port,
		      "a grid layout renders its rows in order, each on its own");
		// The spanning widget reaches past the field column's left edge,
		// which is what makes it a span rather than a third cell.
		const int field = rows.value(host).indexOf(QLatin1Char('['));
		CHECK(field > 0
		      && rows.value(conn).indexOf(QStringLiteral("<Connect>")) < field,
		      "and a widget spanning two columns starts left of the field "
		      "column rather than inside it");
		// The row spacing is UNEVEN and that is 7.8's decision showing
		// through rather than a defect: the layout puts its rows 33 px
		// apart on a 19 px grid, GridSnap rounds each to the nearest cell,
		// and 1.7 cells rounds to one gap and then to two. Recorded here
		// because a consumer will see it and nothing else says why.
		GridGuard::reset();
	}
	{
		QWidget win;
		auto *v = new QVBoxLayout(&win);
		auto *stack = new QStackedWidget;
		for (const char *t : { "PAGE-ONE", "PAGE-TWO" }) {
			auto *page = new QWidget;
			auto *pv = new QVBoxLayout(page);
			pv->addWidget(new QLabel(QString::fromLatin1(t)));
			stack->addWidget(page);
		}
		v->addWidget(stack);
		show(win, 30, 6);
		CellBuffer first(30, 6);
		render_once(win, first);
		stack->setCurrentIndex(1);
		QCoreApplication::processEvents();
		CellBuffer second(30, 6);
		render_once(win, second);
		const QString a = first.to_text(), b2 = second.to_text();
		// Both directions, because "the current page is drawn" passes
		// against a stack that draws every page on top of itself.
		//
		// What this defends is the DELEGATION rather than an arithmetic of
		// qtty's own: hidden children are skipped by `QWidget::render()`,
		// which `compose()` calls on the top level, so the property holds
		// because qtty hands the tree to Qt rather than walking it. That
		// is worth pinning precisely because the compositor DOES walk
		// children elsewhere -- for popups, modals and the priority pass --
		// and a change that composited children individually for damage
		// would put every page on the screen at once. No single-line
		// sabotage reddens this today; the fixture is here for the change
		// that would.
		CHECK(a.contains(QStringLiteral("PAGE-ONE"))
		      && !a.contains(QStringLiteral("PAGE-TWO")),
		      "a stacked widget draws its current page and not the ones "
		      "behind it");
		CHECK(b2.contains(QStringLiteral("PAGE-TWO"))
		      && !b2.contains(QStringLiteral("PAGE-ONE")),
		      "and changing the page changes what is drawn");
	}

	// A STATUS BAR, which nothing here had rendered either and which
	// netcfgd's main window uses -- `statusBar()->addWidget(status)`. It is
	// the one piece of a QMainWindow that carries text a user reads
	// continuously, so where it lands is the whole question: it must be the
	// LAST row and it must not collide with whatever the central widget put
	// at its bottom.
	//
	// That is not free. `QMainWindowLayout` splits the window in pixels
	// without regard to the cell, so the boundary falls mid-cell -- measured
	// on a 7-row window, the central widget gets 109 px of a 19 px grid and
	// the status bar starts there. The two still round onto separate rows,
	// and this check is what says so.
	{
		QMainWindow win;
		auto *c = new QWidget;
		auto *v = new QVBoxLayout(c);
		v->addWidget(new QLabel(QStringLiteral("Interfaces")));
		v->addStretch();
		v->addWidget(new QPushButton(QStringLiteral("Apply")));
		win.setCentralWidget(c);
		win.statusBar()->addWidget(new QLabel(QStringLiteral("wlan0 up")));
		show(win, 30, 7);
		CellBuffer b(30, 7);
		render_once(win, b);
		const QStringList rows = b.to_text().split(QLatin1Char('\n'));
		int apply = -1, status = -1;
		for (int i = 0; i < rows.size(); ++i) {
			if (rows.at(i).contains(QStringLiteral("Apply"))) apply = i;
			if (rows.at(i).contains(QStringLiteral("wlan0"))) status = i;
		}
		printf("info: main window -- Apply on row %d, status bar on row %d\n",
		       apply, status);
		CHECK(status >= 0 && apply >= 0 && status > apply,
		      "a status bar renders below the central widget's own last row "
		      "rather than in the same cell as it");
		// Qt's own furniture is off the grid here and cannot be placed from
		// the style -- 8.61 sizes it. Reset so the count this block leaves
		// is not attributed to whatever runs next, which is what the dock
		// block above does for the same reason.
		GridGuard::reset();
	}

	// QFormLayout, which nothing here had ever rendered. It is not an
	// exotic class: netcfgd, which vendors this library as a submodule,
	// uses it eleven times, and it is how nearly every settings dialog is
	// built.
	//
	// What a form layout IS, is the alignment: a label column sized to the
	// widest label and a field column that starts at one x for every row.
	// The width is computed in PIXELS from font metrics, so rounding it
	// onto cells is exactly where a grid can lose it -- and a form whose
	// fields start at four different columns still contains every widget,
	// so nothing but the alignment says it went wrong.
	{
		QWidget win;
		auto *form = new QFormLayout(&win);
		form->addRow(QStringLiteral("SSID"), new QLineEdit);
		form->addRow(QStringLiteral("Security"), new QComboBox);
		form->addRow(QStringLiteral("Priority"), new QSpinBox);
		show(win, 40, 8);
		CellBuffer b(40, 8);
		render_once(win, b);
		const QStringList rows = b.to_text().split(QLatin1Char('\n'));
		// Where each field begins, taken from the frame rather than from
		// the widgets: a check reading geometry would be asking Qt what it
		// intended, and the question is what reached the cells.
		QVector<int> starts;
		for (const QString &r : rows) {
			const int at = r.indexOf(QLatin1Char('['));
			if (at >= 0) starts.append(at);
		}
		printf("info: form fields begin at column(s)");
		for (int x : starts) printf(" %d", x);
		printf("\n");
		// The column alignment is printed and NOT asserted, which took a
		// sabotage to establish. QFormLayout gives every field one column,
		// so they share one pixel x, and one pixel x rounds to one cell x
		// -- the assertion holds by construction and tests Qt rather than
		// this library. Measured: with GridSnap::snap() returning its
		// argument unchanged the fields still land at 10, 10, 10. A check
		// that cannot fail is worse than none, so what is asserted below is
		// what qtty actually decides.
		CHECK(starts.size() == 3
		      && rows.value(0).contains(QStringLiteral("SSID"))
		      && rows.value(1).contains(QStringLiteral("Security"))
		      && rows.value(2).contains(QStringLiteral("Priority")),
		      "a form layout renders every row, each label on its own "
		      "field's row and each field drawn");
	}

	// combo: box + dropdown arrow
	{
		QComboBox combo;
		combo.addItems({"Alpha", "Beta"});
		show(combo, 20, 3);
		CellBuffer b(22, 4);
		render_once(combo, b);
		CHECK(buffer_contains(b, QStringLiteral("▾")), "combo draws dropdown arrow");
		CHECK(findText(b, QStringLiteral("Alpha")).x() >= 0, "combo label renders");
	}
	// progress bar: fill + percentage
	{
		QProgressBar pb;
		pb.setRange(0, 100); pb.setValue(50);
		show(pb, 20, 1);
		CellBuffer b(22, 2);
		render_once(pb, b);
		CHECK(buffer_contains(b, QStringLiteral("█")) && buffer_contains(b, QStringLiteral("░")),
		      "progress bar fills half");
		CHECK(findText(b, QStringLiteral("50%")).x() >= 0, "progress label centred");
	}
	// A font's emphasis at the sites GridStyle writes itself. Text that goes
	// through QPainter already carries it -- CellPaintEngine reads the
	// painter's font -- so a check on a QLabel would pass against a style
	// that does nothing, and did. These two are on labels the style writes.
	//
	// A pair again, for the reason the item-view checks are one: "the bold
	// button is bold" is satisfied by a style that bolds every button.
	{
		QFont bold = QApplication::font();
		bold.setBold(true);
		QWidget host;
		auto *lay = new QVBoxLayout(&host);
		auto *heavy = new QPushButton(QStringLiteral("heavy"), &host);
		heavy->setFont(bold);
		lay->addWidget(heavy);
		auto *light = new QPushButton(QStringLiteral("light"), &host);
		lay->addWidget(light);
		show(host, 24, 4);
		CellBuffer b(26, 5);
		render_once(host, b);
		const QPoint h = findText(b, QStringLiteral("heavy"));
		const QPoint l = findText(b, QStringLiteral("light"));
		CHECK(h.x() >= 0 && l.x() >= 0 && (b.at(h.x(), h.y()).attrs & Attr::Bold)
		      && !(b.at(l.x(), l.y()).attrs & Attr::Bold),
		      "a bold push button's label is bold and a plain one's is not");
	}
	// The menu bar is the case that settles WHICH font the answer comes from,
	// and it is why label_attrs() unions the widget's with the option's
	// rather than preferring one. Measured: QMenuBar leaves
	// QStyleOptionMenuItem::font at the application font, so an italic menu
	// bar read as plain while the option's font was taken as authoritative --
	// and a default menu action carries bold in the option and nowhere on the
	// widget. Neither font alone answers for both.
	{
		QFont ital = QApplication::font();
		ital.setItalic(true);
		QMenuBar bar;
		bar.addMenu(QStringLiteral("File"));
		bar.setFont(ital);
		show(bar, 24, 1);
		CellBuffer b(26, 2);
		render_once(bar, b);
		const QPoint f = findText(b, QStringLiteral("File"));
		CHECK(f.x() >= 0 && (b.at(f.x(), f.y()).attrs & Attr::Italic),
		      "a menu bar's font reaches its items, which the option's does not carry");
	}
	// The disabled state at the controls that draw themselves with
	// put_cluster rather than text(). GridStyle carries State_Enabled into
	// every label it writes, and carried it into no GLYPH it writes -- so a
	// disabled slider, scroll bar and progress bar were pixel-for-cell
	// identical to working ones, and the progress bar was the tell: its
	// percentage was dim because that goes through text(), while the bar
	// under it was not, so one widget showed both answers at once.
	//
	// A pair again. "The disabled one is dim" is satisfied by a style that
	// dims everything, and the enabled half is what says otherwise.
	{
		auto dim_run = [](const CellBuffer &b, int row) {
			int n = 0;
			for (int x = 0; x < b.cols(); ++x)
				if (b.at(x, row).attrs & Attr::Dim) ++n;
			return n;
		};
		auto build = [&](bool enabled) {
			auto *h = new QWidget;
			h->setAttribute(Qt::WA_DontShowOnScreen);
			auto *lay = new QVBoxLayout(h);
			lay->setContentsMargins(0, 0, 0, 0);
			lay->setSpacing(0);
			auto *sl = new QSlider(Qt::Horizontal, h);
			sl->setRange(0, 10); sl->setValue(5);
			sl->setFixedHeight(GridMetrics::ch());
			auto *sb = new QScrollBar(Qt::Horizontal, h);
			sb->setRange(0, 100); sb->setValue(20);
			sb->setFixedHeight(GridMetrics::ch());
			auto *pb = new QProgressBar(h);
			pb->setRange(0, 100); pb->setValue(40);
			pb->setFixedHeight(GridMetrics::ch());
			lay->addWidget(sl); lay->addWidget(sb); lay->addWidget(pb);
			sl->setEnabled(enabled); sb->setEnabled(enabled); pb->setEnabled(enabled);
			return h;
		};
		QWidget *off = build(false);
		show(*off, 20, 3);
		CellBuffer b_off(22, 4);
		render_once(*off, b_off);
		QWidget *on = build(true);
		show(*on, 20, 3);
		CellBuffer b_on(22, 4);
		render_once(*on, b_on);
		CHECK(dim_run(b_off, 0) == 20 && dim_run(b_on, 0) == 0,
		      "a disabled slider's groove and handle are dim");
		CHECK(dim_run(b_off, 1) == 20 && dim_run(b_on, 1) == 0,
		      "and a disabled scroll bar's arrows, track and thumb are");
		CHECK(dim_run(b_off, 2) == 20 && dim_run(b_on, 2) == 0,
		      "and a disabled progress bar's fill is, not only its percentage");
		delete off;
		delete on;
	}
	// The other direction, and the sweep this came from: what a control looks
	// like WHILE it is being used. Qt reports State_Sunken on a button held
	// under the pointer and on a slider whose handle has been grabbed; this
	// style spells pressed as reverse video at the tool button and the menu
	// bar item, and spelt it nowhere else -- so pressing a push button gave
	// no feedback at all, and a slider handle looked the same whether it was
	// being dragged or sitting where it was left.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *btn = new QPushButton(QStringLiteral("Press"), &h);
		btn->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		auto *sl = new QSlider(Qt::Horizontal, &h);
		sl->setRange(0, 100);
		sl->setValue(0);
		sl->setGeometry(0, GridMetrics::ch(), GridMetrics::cw() * 20, GridMetrics::ch());
		h.resize(GridMetrics::cells(20, 2));
		h.show();
		QCoreApplication::processEvents();
		auto shot = [&](CellBuffer &b) { render_once(h, b); };

		CellBuffer rest(22, 3);
		shot(rest);
		const QPoint label = findText(rest, QStringLiteral("Press"));

		InputRouter r(&h);
		r.on_mouse({QPoint(3, 0), 1, true, false, false, 0});
		QCoreApplication::processEvents();
		CellBuffer held(22, 3);
		shot(held);
		r.on_mouse({QPoint(3, 0), 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CellBuffer after(22, 3);
		shot(after);
		CHECK(label.x() >= 0 && (held.at(label.x(), label.y()).attrs & Attr::Reverse)
		      && !(rest.at(label.x(), label.y()).attrs & Attr::Reverse)
		      && !(after.at(label.x(), label.y()).attrs & Attr::Reverse),
		      "a push button held down is reverse, and is not before or after");

		// The slider's handle moves as it is dragged, so the cell to read is
		// the one the handle is in at the moment of the frame, not a fixed
		// column. Found by writing it the other way first: a check on the
		// press column passes on the groove, which is never reversed.
		auto handle_of = [](const CellBuffer &b, int row) {
			for (int x = 0; x < b.cols(); ++x)
				if (b.at(x, row).ch == QStringLiteral("●")) return x;
			return -1;
		};
		InputRouter r2(&h);
		r2.on_mouse({QPoint(0, 1), 1, true, false, false, 0});
		for (int x = 1; x <= 10; ++x)
			r2.on_mouse({QPoint(x, 1), 1, false, false, true, 0});
		QCoreApplication::processEvents();
		CellBuffer dragging(22, 3);
		shot(dragging);
		r2.on_mouse({QPoint(10, 1), 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CellBuffer dropped(22, 3);
		shot(dropped);
		const int held_at = handle_of(dragging, 1), rest_at = handle_of(dropped, 1);
		CHECK(held_at >= 0 && rest_at >= 0
		      && (dragging.at(held_at, 1).attrs & Attr::Reverse)
		      && !(dropped.at(rest_at, 1).attrs & Attr::Reverse),
		      "a slider handle being dragged is reverse, and is not once dropped");
	}
	// A mnemonic underline is a rule one cell long that starts a pixel early,
	// and it used to paint the cell before the letter. Found by rendering
	// dialogs nobody had rendered: a QErrorMessage drew a rule between its
	// check box's indicator and the first letter of its label. Every check
	// box, radio button and group box carrying a mnemonic had it.
	//
	// Four items, varying the mnemonic and the check state separately,
	// because the first version of this probe varied both at once and could
	// not have said which one produced the rule. The letter keeps its
	// underline attribute either way -- that arrives through the font, not
	// through the line -- so the check is on the gap cell and on the
	// attribute, which is what says the mnemonic still reads as one.
	{
		QWidget host;
		auto *v = new QVBoxLayout(&host);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto add = [&](const QString &text, bool checked) {
			auto *c = new QCheckBox(text, &host);
			c->setChecked(checked);
			c->setFixedHeight(GridMetrics::ch());
			v->addWidget(c);
		};
		add(QStringLiteral("&Mnemonic checked"), true);
		add(QStringLiteral("Plain checked"), true);
		show(host, 30, 2);
		CellBuffer b(32, 3);
		render_once(host, b);
		const QPoint marked = findText(b, QStringLiteral("Mnemonic"));
		const QPoint plain = findText(b, QStringLiteral("Plain"));
		CHECK(marked.x() > 0 && plain.x() > 0
		      && b.at(marked.x() - 1, marked.y()).ch == b.at(plain.x() - 1, plain.y()).ch,
		      "a mnemonic puts nothing in the gap a plain label leaves empty");
		CHECK(marked.x() > 0 && (b.at(marked.x(), marked.y()).attrs & Attr::Underline)
		      && !(b.at(plain.x(), plain.y()).attrs & Attr::Underline),
		      "and the marked letter is still underlined");
	}
	// The other half of that rule, and the reason it is a coverage test
	// rather than a "do not draw short lines" test: a rule that does cover
	// its cells still draws. Without this the fix above is satisfied by an
	// engine that has stopped drawing rules at all.
	{
		QWidget host;
		auto *v = new QVBoxLayout(&host);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *rule = new QFrame(&host);
		rule->setFrameShape(QFrame::HLine);
		rule->setFixedHeight(GridMetrics::ch());
		v->addWidget(rule);
		show(host, 20, 1);
		CellBuffer b(22, 2);
		render_once(host, b);
		int drawn = 0;
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, 0).ch == QStringLiteral("─")) ++drawn;
		printf("info: rule drew %d cells of a %d-cell widget\n",
		       drawn, rule->width() / GridMetrics::cw());
		CHECK(drawn == rule->width() / GridMetrics::cw(),
		      "a rule spanning its widget still draws every cell of it");
	}
	// tabs: selected tab reverse-video
	{
		QTabWidget tabs;
		tabs.addTab(new QWidget, "First");
		tabs.addTab(new QWidget, "Second");
		show(tabs, 30, 8);
		CellBuffer b(32, 9);
		render_once(tabs, b);
		QPoint p1 = findText(b, QStringLiteral("First"));
		QPoint p2 = findText(b, QStringLiteral("Second"));
		CHECK(p1.x() >= 0 && p2.x() >= 0, "both tab labels render");
		CHECK(p1.x() >= 0 && (b.at(p1.x(), p1.y()).attrs & Attr::Reverse),
		      "selected tab is reverse-video");
		CHECK(p2.x() < 0 || !(b.at(p2.x(), p2.y()).attrs & Attr::Reverse),
		      "unselected tab is plain");
	}
	// tree: branch glyphs + bold header + item selection
	{
		QTreeWidget tree;
		tree.setHeaderLabels({"Name", "Value"});
		auto *root = new QTreeWidgetItem(&tree);
		root->setText(0, "parent");
		auto *kid = new QTreeWidgetItem(root);
		kid->setText(0, "child");
		tree.expandAll();
		tree.setFrameShape(QFrame::NoFrame);
		show(tree, 30, 8);
		tree.setCurrentItem(kid);
		QCoreApplication::processEvents();
		CellBuffer b(32, 9);
		render_once(tree, b);
		CHECK(buffer_contains(b, QStringLiteral("▾")), "expanded branch shows ▾");
		QPoint h = findText(b, QStringLiteral("Name"));
		CHECK(h.x() >= 0 && (b.at(h.x(), h.y()).attrs & Attr::Bold), "header label bold");
		QPoint k = findText(b, QStringLiteral("child"));
		CHECK(k.x() >= 0 && (b.at(k.x(), k.y()).attrs & Attr::Reverse),
		      "selected row reverse-video");
		QPoint pr = findText(b, QStringLiteral("parent"));
		CHECK(pr.x() >= 0 && !(b.at(pr.x(), pr.y()).attrs & Attr::Reverse),
		      "unselected row plain");
	}
	// scrollbar column: arrows, thumb, groove (F5 fix)
	{
		QListView list;
		auto *m = new QStringListModel(&list);
		QStringList rows;
		for (int i = 0; i < 100; ++i) rows << QStringLiteral("row %1").arg(i);
		m->setStringList(rows);
		list.setModel(m);
		list.setFrameShape(QFrame::NoFrame);
		show(list, 24, 10);
		CellBuffer b(26, 11);
		render_once(list, b);
		CHECK(buffer_contains(b, QStringLiteral("▲")) && buffer_contains(b, QStringLiteral("▼")),
		      "scrollbar arrows render");
		CHECK(buffer_contains(b, QStringLiteral("█")) && buffer_contains(b, QStringLiteral("░")),
		      "scrollbar thumb + groove render");
	}
	// slider: handle on track
	{
		QSlider slider(Qt::Horizontal);
		slider.setRange(0, 10); slider.setValue(5);
		show(slider, 20, 1);
		CellBuffer b(22, 2);
		render_once(slider, b);
		CHECK(buffer_contains(b, QStringLiteral("●")), "slider handle renders");
		CHECK(buffer_contains(b, QStringLiteral("─")), "slider track renders");
	}
	// splitter handle between panes
	{
		QSplitter split(Qt::Horizontal);
		split.addWidget(new QLabel("left"));
		split.addWidget(new QLabel("right"));
		// A splitter divides its width by the panes' size hints, and a QLabel's
		// hint is however wide its text happens to be -- 129 and 161 px here,
		// neither a cell multiple. The sizes are the application's to state, so
		// state them: 30 cells of width less the one-cell handle
		// (PM_SplitterWidth) leaves 29 to divide, 14 and 15.
		//
		// The order is the point, and show() from the helper above cannot give
		// it. setSizes() lays the panes out against the width the splitter has
		// when it is called, so the resize has to come first; and it has to
		// come before show(), because the guard counts the layout that show()
		// performs. Setting the sizes afterwards leaves the right pixels
		// behind a violation that has already been recorded -- measured: the
		// panes were 140 and 150 by the time anything rendered, and the suite
		// still reported four QLabel geometries off the grid.
		split.setAttribute(Qt::WA_DontShowOnScreen);
		split.resize(GridMetrics::cells(30, 4));
		split.setSizes({14 * GridMetrics::cw(), 15 * GridMetrics::cw()});
		split.show();
		QCoreApplication::processEvents();
		CellBuffer b(32, 5);
		render_once(split, b);
		CHECK(buffer_contains(b, QStringLiteral("│")), "splitter handle renders");
	}
	// A toolbar button underlines its mnemonic, read off the ACTION.
	//
	// Its key works: Alt+C on a toolbar action "&Cut" triggers it,
	// measured. What makes this different from the menu cases is where the
	// marker survives -- by the time a tool button reaches the style,
	// `QStyleOptionToolButton::text` is already "Cut", Qt having stripped
	// it. A QStyleOptionMenuItem keeps its marker; this one does not. An
	// index taken from the option is therefore always -1, and the first
	// version of this fix was a silent no-op that rendered exactly as
	// before.
	//
	// So the check would pass against a style that never marked anything if
	// it only asserted "nothing is underlined outside the label" -- it pins
	// the letter instead.
	{
		QToolBar bar;
		bar.addAction(QStringLiteral("&Cut"));
		bar.setAttribute(Qt::WA_DontShowOnScreen);
		bar.resize(GridMetrics::cells(20, 1));
		bar.show();
		QCoreApplication::processEvents();
		CellBuffer b(20, 1);
		render_once(bar, b);
		QString under;
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, 0).attrs & Attrs(Attr::Underline)) under += b.at(x, 0).ch;
		CHECK(under == QStringLiteral("C"),
		      "a toolbar button underlines the letter its mnemonic uses");
	}

	// A menu ITEM does the same, and keeps a doubled ampersand literal.
	//
	// The item is a working key too: pressing 'o' in an open menu carrying
	// "&Open" triggers it, measured. It also carried the bug the menu bar
	// was fixed for -- `remove('&')` rather than `strip_mnemonic()`, which
	// does not know that "&&" is one literal ampersand, so "A && B"
	// rendered as "A  B".
	//
	// Both halves in one fixture, because they are the same rule seen twice:
	// the marked letter must be underlined AND the doubled one must survive
	// unmarked. A fix that underlined the wrong character would still print
	// the right text, and one that printed the right text could still
	// underline nothing.
	{
		QMenu menu;
		menu.addAction(QStringLiteral("&Open"));
		menu.addAction(QStringLiteral("A && B"));
		menu.setAttribute(Qt::WA_DontShowOnScreen);
		menu.resize(GridMetrics::cells(20, 4));
		menu.show();
		QCoreApplication::processEvents();
		CellBuffer b(20, 4);
		render_once(menu, b);
		QString text, under;
		for (int y = 0; y < b.rows(); ++y)
			for (int x = 0; x < b.cols(); ++x) {
				text += b.at(x, y).ch;
				if (b.at(x, y).attrs & Attrs(Attr::Underline))
					under += b.at(x, y).ch;
			}
		CHECK(text.contains(QStringLiteral("A & B")) && under == QStringLiteral("O"),
		      "a menu item underlines its mnemonic and keeps a doubled"
		      " ampersand");
	}

	// A menu bar underlines the letter its mnemonic uses.
	//
	// Measured before marking it: Alt+F on a bar carrying "&File" opens the
	// menu, so the affordance was real and invisible -- the oldest
	// convention a terminal menu bar has, missing while the key worked.
	//
	// The check pins WHICH letter, not merely that something is underlined:
	// marking the wrong character is the failure this can actually have,
	// since "&&" is a literal ampersand and counting one as a marker shifts
	// the mark. "E&xit" is the fixture for that -- the marked letter is not
	// the first.
	{
		QMenuBar bar;
		bar.addMenu(QStringLiteral("&File"));
		bar.addMenu(QStringLiteral("E&xit"));
		bar.setAttribute(Qt::WA_DontShowOnScreen);
		bar.resize(GridMetrics::cells(24, 1));
		bar.show();
		QCoreApplication::processEvents();
		CellBuffer b(24, 1);
		render_once(bar, b);
		QString under;
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, 0).attrs & Attrs(Attr::Underline))
				under += b.at(x, 0).ch;
		CHECK(under == QStringLiteral("Fx"),
		      "a menu bar underlines the letter its mnemonic uses");
	}

	// The DEFAULT button is marked, because Enter activates it.
	//
	// Measured on a dialog whose focus was elsewhere: Enter fired the
	// default button and not the focused one, so the behaviour was right
	// while nothing on screen said WHICH button that was. A terminal user
	// pressing Enter could not tell what would happen.
	//
	// Bold rather than another bracket: a second pair costs two columns on
	// a screen short of them and collides with the brackets that already
	// mean "button", while an attribute costs none.
	{
		auto bold_cells = [](bool dflt) {
			QPushButton b(QStringLiteral("Go"));
			b.setDefault(dflt);
			b.setAttribute(Qt::WA_DontShowOnScreen);
			b.resize(GridMetrics::cells(12, 1));
			b.show();
			QCoreApplication::processEvents();
			CellBuffer buf(12, 1);
			render_once(b, buf);
			int n = 0;
			for (int x = 0; x < buf.cols(); ++x)
				if (buf.at(x, 0).attrs & Attrs(Attr::Bold)) ++n;
			return n;
		};
		// Both directions: marking everything would satisfy "the default is
		// bold" while saying nothing, and marking nothing would satisfy a
		// check that only looked at the ordinary button.
		CHECK(bold_cells(true) > 0 && bold_cells(false) == 0,
		      "a default button is marked and an ordinary one is not");
	}

	// A FLAT group box draws only its top rule.
	//
	// Qt documents `flat` as "only the top part of the frame is drawn in
	// most styles", and this drew the whole box either way -- measured, 36
	// border cells with `setFlat(true)` and 36 without, so the property
	// changed nothing at all.
	//
	// Asserted as fewer-and-still-something rather than as an exact count:
	// a flat box that drew NOTHING would satisfy "fewer" while losing the
	// rule Qt says to keep, and an exact number would pin this to one
	// width.
	{
		auto borders = [](bool flat) {
			QGroupBox g(QStringLiteral("Group"));
			g.setFlat(flat);
			auto *v = new QVBoxLayout(&g);
			v->addWidget(new QLabel(QStringLiteral("body")));
			g.setAttribute(Qt::WA_DontShowOnScreen);
			g.resize(GridMetrics::cells(18, 5));
			g.show();
			QCoreApplication::processEvents();
			CellBuffer b(18, 5);
			render_once(g, b);
			int n = 0;
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x) {
					const QString &ch = b.at(x, y).ch;
					if (ch == QStringLiteral("│") || ch == QStringLiteral("─")
					    || ch == QStringLiteral("┌") || ch == QStringLiteral("┐")
					    || ch == QStringLiteral("└") || ch == QStringLiteral("┘"))
						++n;
				}
			return n;
		};
		const int boxed = borders(false), flat = borders(true);
		CHECK(flat > 0 && flat < boxed,
		      "a flat group box draws its top rule and not the whole box");
	}

	// A line edit that says it has no frame gets none -- and one that says
	// it has a frame still gets it.
	//
	// Both directions, because they fail in opposite ways: honouring the
	// flag by drawing nothing at all would pass a check that only asked
	// about the spin box, and drawing regardless would pass one that only
	// asked about a plain editor.
	//
	// The case that found it is a QSpinBox. Its internal QLineEdit is
	// frameless by construction, since the spin box draws the frame and
	// the editor sits inside it -- so a box drawn for the editor anyway
	// put two borders in adjacent columns and the widget opened with two
	// corners.
	{
		QSpinBox spin;
		spin.setRange(0, 999);
		spin.setValue(42);
		spin.setAttribute(Qt::WA_DontShowOnScreen);
		spin.resize(GridMetrics::cells(14, 3));
		spin.show();
		QCoreApplication::processEvents();
		CellBuffer sb(14, 3);
		render_once(spin, sb);
		int corners = 0;
		for (int x = 0; x < sb.cols(); ++x)
			if (sb.at(x, 0).ch == QStringLiteral("┌")) ++corners;

		QLineEdit plain;
		plain.setText(QStringLiteral("hi"));
		plain.setAttribute(Qt::WA_DontShowOnScreen);
		plain.resize(GridMetrics::cells(14, 3));
		plain.show();
		QCoreApplication::processEvents();
		CellBuffer pb(14, 3);
		render_once(plain, pb);
		bool framed = false;
		for (int x = 0; x < pb.cols(); ++x)
			if (pb.at(x, 0).ch == QStringLiteral("┌")) framed = true;

		CHECK(corners == 1 && framed,
		      "a frameless line edit draws no box and a framed one still"
		      " does");
	}

	// No container puts its own border flush against a framed child's.
	//
	// This is the tab defect's family swept rather than its one instance:
	// a container draws a one-cell border and then insets its content by
	// something that is not a cell, so a framed child -- an item view, a
	// nested QFrame -- lands its edge in the next column and the two read
	// as one doubled rule.
	//
	// The population is NAMED and its one exemption is named with it,
	// because a sweep that quietly skipped the awkward case would pass for
	// the wrong reason. QScrollArea is excluded and is not a defect: its
	// viewport IS inset a full cell -- measured, x=10 at cw=10 -- and the
	// child's own border then sits immediately inside it. That is two
	// frames with no margin between them, which is the open question in
	// section 8.25 rather than a wrong inset, and it stays visible here so
	// that settling it settles this line too.
	{
		const auto doubled = [](QWidget *host, int cols, int rows) {
			host->setAttribute(Qt::WA_DontShowOnScreen);
			host->resize(GridMetrics::cells(cols, rows));
			host->show();
			QCoreApplication::processEvents();
			CellBuffer b(cols, rows);
			render_once(*host, b);
			int n = 0;
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x + 1 < b.cols(); ++x)
					if (b.at(x, y).ch == QStringLiteral("│")
					    && b.at(x + 1, y).ch == QStringLiteral("│"))
						++n;
			return n;
		};
		int offenders = 0, swept = 0;
		{
			QGroupBox g(QStringLiteral("Group"));
			auto *v = new QVBoxLayout(&g);
			auto *l = new QListWidget; l->addItem(QStringLiteral("x"));
			v->addWidget(l);
			++swept; if (doubled(&g, 24, 7)) ++offenders;
		}
		{
			QTabWidget t;
			auto *page = new QWidget;
			auto *v = new QVBoxLayout(page);
			auto *l = new QListWidget; l->addItem(QStringLiteral("x"));
			v->addWidget(l);
			t.addTab(page, QStringLiteral("T"));
			++swept; if (doubled(&t, 24, 8)) ++offenders;
		}
		{
			QToolBox tb;
			auto *page = new QWidget;
			auto *v = new QVBoxLayout(page);
			auto *l = new QListWidget; l->addItem(QStringLiteral("x"));
			v->addWidget(l);
			tb.addItem(page, QStringLiteral("One"));
			++swept; if (doubled(&tb, 24, 8)) ++offenders;
		}
		{
			QFrame f; f.setFrameStyle(QFrame::StyledPanel);
			auto *v = new QVBoxLayout(&f);
			auto *l = new QListWidget; l->addItem(QStringLiteral("x"));
			v->addWidget(l);
			++swept; if (doubled(&f, 24, 7)) ++offenders;
		}
		{
			QMainWindow mw;
			auto *l = new QListWidget; l->addItem(QStringLiteral("x"));
			mw.setCentralWidget(l);
			++swept; if (doubled(&mw, 24, 7)) ++offenders;
		}
		{
			QDockWidget d(QStringLiteral("Dock"));
			auto *l = new QListWidget; l->addItem(QStringLiteral("x"));
			d.setWidget(l);
			++swept; if (doubled(&d, 24, 7)) ++offenders;
		}
		// The name says COLUMN, and the precision is the point. Sweeping
		// the other axis -- two horizontal rules on adjacent ROWS -- every
		// one of these fails: 36 cells in a QFrame, 18 in a QGroupBox, 17
		// in a QToolBox. That is not the same defect and not a defect at
		// all. `PM_LayoutTopMargin` is deliberately 0 where the left and
		// right margins are a cell, and the reason is written beside it:
		// "a column of eighty is cheap where a row of twenty-four is not".
		// So nested frames are separated by a column and stacked without a
		// row, on purpose.
		//
		// A check called "no container's border sits flush against a
		// framed child's" would therefore claim an axis it never looked
		// at, and would be false on the axis it implied.
		CHECK(swept == 6 && offenders == 0,
		      "no container's border shares a column with a framed"
		      " child's");
	}

	// A QTabWidget's page must clear the frame that is actually DRAWN.
	//
	// The pane's border is one cell wide, and the base style inset the page
	// by its own two-pixel frame width -- so a framed child put its edge in
	// the column next to the tab's, and the two read as one doubled rule.
	// Reported from fuzzypickles, whose chat tab "opens with two corners and
	// runs two rules down both panes"; their panes are item views, which are
	// QFrames carrying a StyledPanel.
	//
	// Asserted as a RELATIONSHIP -- the gap between the two borders -- rather
	// than as a column number, which would pin this to one terminal width and
	// one frame width.
	{
		QTabWidget tabs;
		auto *page = new QWidget;
		auto *v = new QVBoxLayout(page);
		auto *inner = new QListWidget;
		inner->addItem(QStringLiteral("x"));
		v->addWidget(inner);
		tabs.addTab(page, QStringLiteral("T"));
		tabs.setAttribute(Qt::WA_DontShowOnScreen);
		tabs.resize(GridMetrics::cells(20, 7));
		tabs.show();
		QCoreApplication::processEvents();
		CellBuffer b(20, 7);
		render_once(tabs, b);
		// The row through the middle: the tab's rule, then the pane's, with
		// at least one cell that is neither between them.
		int first = -1, second = -1;
		const int row = 3;
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, row).ch == QStringLiteral("│")) {
				if (first < 0) first = x;
				else if (second < 0) { second = x; break; }
			}
		CHECK(first >= 0 && second > first + 1,
		      "a tab widget's page clears the border it draws");
	}

	// The same handle between FRAMED panes, where it must not render.
	//
	// Reported from fuzzypickles, whose chat tab splits two framed panes:
	// the handle drew a bar between the two panes' own borders, so a
	// terminal showed three vertical rules side by side where it wants
	// one. The bar is right above and wrong here, and the difference is
	// whether anything else is already drawing an edge in that column.
	//
	// Both directions are asserted, because a fix that simply stopped
	// drawing the handle would pass this and fail the check above -- and
	// one that changed nothing would pass that and fail this.
	{
		QSplitter split(Qt::Horizontal);
		for (const char *t : {"left", "right"}) {
			auto *f = new QFrame;
			f->setFrameStyle(QFrame::StyledPanel);
			auto *v = new QVBoxLayout(f);
			v->addWidget(new QLabel(QLatin1String(t)));
			split.addWidget(f);
		}
		split.setAttribute(Qt::WA_DontShowOnScreen);
		split.resize(GridMetrics::cells(30, 5));
		split.setSizes({14 * GridMetrics::cw(), 15 * GridMetrics::cw()});
		split.show();
		QCoreApplication::processEvents();
		CellBuffer b(32, 6);
		render_once(split, b);
		// The row through the middle of the panes: exactly two vertical
		// rules on the left pane's side of centre and its neighbour's,
		// with a blank between them rather than a third.
		int rules = 0, blank_between = 0;
		const int row = 2;
		for (int x = 0; x + 1 < b.cols(); ++x) {
			if (b.at(x, row).ch == QStringLiteral("│")) {
				++rules;
				if (b.at(x + 1, row).ch == QStringLiteral(" ")
				    && b.at(x + 2, row).ch == QStringLiteral("│"))
					++blank_between;
			}
		}
		CHECK(rules == 4 && blank_between == 1,
		      "a splitter between framed panes leaves a gap, not a third"
		      " rule");
	}
	// A MIXED splitter keeps its bar, which is what "all panes" rather than
	// "any pane" buys.
	//
	// The style cannot tell which handle it is drawing -- `opt->rect` is the
	// handle's own rect at origin (0,0) while `handle(i)->geometry()` is its
	// place in the splitter, measured -- so the question it CAN answer is
	// about the panes as a set. All framed means no bar is needed anywhere.
	// One unframed pane means some junction has nothing else marking it, and
	// a missing separator is worse than a doubled rule.
	{
		QSplitter split(Qt::Horizontal);
		auto *f = new QFrame;
		f->setFrameStyle(QFrame::StyledPanel);
		auto *fv = new QVBoxLayout(f);
		fv->addWidget(new QLabel(QStringLiteral("framed")));
		split.addWidget(f);
		auto *bare = new QLabel(QStringLiteral("bare"));
		bare->setFrameStyle(QFrame::NoFrame);
		split.addWidget(bare);
		split.setAttribute(Qt::WA_DontShowOnScreen);
		split.resize(GridMetrics::cells(30, 5));
		split.setSizes({14 * GridMetrics::cw(), 15 * GridMetrics::cw()});
		split.show();
		QCoreApplication::processEvents();
		CellBuffer b(32, 6);
		render_once(split, b);
		// The row through the middle carries the framed pane's two edges
		// AND the handle's bar: three rules, where two framed panes give
		// two.
		int rules = 0;
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, 2).ch == QStringLiteral("│")) ++rules;
		CHECK(rules >= 3,
		      "and a splitter with one unframed pane keeps its bar");
	}
	// line edit: a selection is reverse video, the same as every other
	// selection in the program.
	//
	// It used to be the desktop's QPalette::Highlight as a literal RGB, which
	// qtty/theme.h's own rule forbids -- the default theme keeps every role at
	// Color::Default and marks emphasis with attrs, not colour -- and which
	// made the most common highlight in a program depend on which desktop
	// launched it. Measured side by side before the change: a QLineEdit's
	// selection came out bg=#308cc6 while a list's came out reverse.
	{
		QLineEdit edit;
		edit.setText("hello");
		show(edit, 20, 3);
		edit.selectAll();
		QCoreApplication::processEvents();
		CellBuffer b(22, 4);
		render_once(edit, b);
		QPoint h = findText(b, QStringLiteral("hello"));
		CHECK(h.x() >= 0, "line edit text renders");
		CHECK(h.x() >= 0 && (b.at(h.x(), h.y()).attrs & Attr::Reverse)
		      && b.at(h.x(), h.y()).bg.kind() == Color::Default,
		      "a text selection is reverse video, not the desktop's colour");
	}
	// menu: items, separator, shortcut, selected item highlight
	{
		QMenu menu;
		QAction *open = menu.addAction("Open");
		open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
		menu.addSeparator();
		menu.addAction("Quit");
		menu.setAttribute(Qt::WA_DontShowOnScreen);
		menu.popup(QPoint(0, 0));
		menu.setActiveAction(open);
		QCoreApplication::processEvents();
		CellBuffer b(30, 8);
		render_once(menu, b);
		QPoint o = findText(b, QStringLiteral("Open"));
		CHECK(o.x() >= 0, "menu item renders");
		CHECK(o.x() >= 0 && (b.at(o.x(), o.y()).attrs & Attr::Reverse),
		      "active menu item highlighted");
		CHECK(buffer_contains(b, QStringLiteral("─")), "separator renders");
		CHECK(findText(b, QStringLiteral("Ctrl+O")).x() >= 0, "shortcut right-aligned");
		menu.close();
	}

	// item views: the roles CellItemDelegate carries (design.md sections 8.4,
	// 8.6 and 17.2), on the QTableView the tier had never exercised at all.
	// Channel A already draws an item's frame -- the selection fill, and the
	// suppression of the pixel panels -- from CE_ItemViewItem, and the checks
	// below deliberately assert none of that. What they assert is the DATA the
	// style is handed and cannot lay out: the check state, the decoration, and
	// where the alignment asks for the text. Every one of them fails with the
	// delegate left uninstalled.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QStandardItemModel model(2, 2);
		auto *checked = new QStandardItem(QStringLiteral("open"));
		checked->setCheckable(true);
		checked->setCheckState(Qt::Checked);
		auto *clear = new QStandardItem(QStringLiteral("shut"));
		clear->setCheckable(true);
		clear->setCheckState(Qt::Unchecked);
		model.setItem(0, 0, checked);
		model.setItem(1, 0, clear);
		auto *right = new QStandardItem(QStringLiteral("42"));
		right->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		model.setItem(0, 1, right);
		model.setItem(1, 1, new QStandardItem(QStringLiteral("7")));

		QTableView table;
		table.setModel(&model);
		table.setItemDelegate(new CellItemDelegate(&table));
		table.setFrameShape(QFrame::NoFrame);
		// Both headers sized in cells, stated rather than left to the header's
		// own hint -- and shown rather than hidden, which is not cosmetic: the
		// corner button between them takes its geometry from the two headers,
		// and a QTableView whose headers are hidden leaves it at the size it
		// was constructed with, which is off the row grid. GridGuard does not
		// exempt it, so hiding the headers here costs a violation.
		table.horizontalHeader()->setFixedHeight(ch);
		table.horizontalHeader()->setDefaultSectionSize(12 * cw);
		table.verticalHeader()->setFixedWidth(4 * cw);
		table.verticalHeader()->setDefaultSectionSize(ch);
		show(table, 30, 6);
		table.setRowHeight(1, 3 * ch);
		QCoreApplication::processEvents();
		CellBuffer b(32, 7);
		render_once(table, b);

		// An item wider than the view it is in. Every write the delegate
		// makes goes straight into the buffer, so nothing bounds them: the
		// budget is the ITEM's rectangle, and an item is entitled to be
		// wider than the view showing it. GridStyle installs a clip on each
		// of its three entry points; the drawControl() the delegate calls
		// installs and tears down its own before returning, so it does not
		// cover the delegate's own writes.
		//
		// Four things had to be true at once for this to be visible, which
		// is why no existing fixture sees it: the buffer must be wider than
		// the view (or CellBuffer::at() absorbs the overdraw into its junk
		// cell), a column must exceed the viewport, the text must be RIGHT
		// aligned so the whole label lands past the edge, and something must
		// own the cells it lands in so the damage is not blank-on-blank.
		{
			QWidget host;
			auto *narrow = new QTableView(&host);
			auto *m2 = new QStandardItemModel(1, 2, &host);
			m2->setItem(0, 0, new QStandardItem(QStringLiteral("alpha")));
			auto *wide = new QStandardItem(QStringLiteral("TOTAL"));
			wide->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			m2->setItem(0, 1, wide);
			narrow->setModel(m2);
			narrow->setFrameShape(QFrame::NoFrame);
			narrow->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			narrow->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			narrow->horizontalHeader()->setFixedHeight(ch);
			narrow->horizontalHeader()->setDefaultSectionSize(12 * cw);
			narrow->verticalHeader()->setFixedWidth(2 * cw);
			narrow->verticalHeader()->setDefaultSectionSize(ch);
			narrow->setItemDelegate(new CellItemDelegate(narrow));
			narrow->setGeometry(0, 0, 16 * cw, 6 * ch);
			// The cells to the right belong to somebody else.
			auto *sib = new QLabel(QStringLiteral("....................."), &host);
			sib->setGeometry(16 * cw, 0, 20 * cw, ch);
			show(host, 40, 6);
			CellBuffer wideb(40, 7);
			render_once(host, wideb);

			const int view_right = 16;
			const QPoint spill = findText(wideb, QStringLiteral("TOTAL"));
			const QPoint kept = findText(wideb, QStringLiteral("alpha"));
			printf("info: TOTAL at x=%d, alpha at x=%d, view ends at %d\n",
			       spill.x(), kept.x(), view_right);
			// Both halves. "Nothing outside" alone is satisfied by a
			// delegate that draws nothing at all, which is the failure the
			// suite's other item-view checks exist to catch.
			CHECK(kept.x() >= 0 && kept.x() < view_right,
			      "an item view still draws the labels that fit");
			CHECK(spill.x() < 0 || spill.x() < view_right,
			      "and writes none of them past the edge of the view");
		}

		// Two labels written with no budget at all. The push button's is
		// cut by its own clip, so a long label loses its closing bracket
		// with nothing to say it was cut -- the tab's fault again, in
		// another control. The menu bar item's clip is the whole BAR, not
		// the item, so an over-long title is not cut at all: it writes into
		// the next item's cells.
		{
			const auto draw_into = [&](CellBuffer &b, QStyle::ControlElement ce,
			                           const QStyleOption &o) {
				Qtty::CellPaintDevice d(b);
				QPainter p(&d);
				QApplication::style()->drawControl(ce, &o, &p, nullptr);
				p.end();
				return b.to_text().split(QLatin1Char('\n')).value(0);
			};

			CellBuffer bb(10, 1);
			QStyleOptionButton bo;
			bo.rect = QRect(0, 0, 8 * cw, ch);
			bo.state = QStyle::State_Enabled;
			bo.text = QStringLiteral("Save Changes");
			const QString btn = draw_into(bb, QStyle::CE_PushButtonLabel, bo);

			CellBuffer mb(16, 1);
			QStyleOptionMenuItem mo;
			mo.rect = QRect(0, 0, 6 * cw, ch);
			mo.state = QStyle::State_Enabled;
			mo.menuItemType = QStyleOptionMenuItem::Normal;
			mo.text = QStringLiteral("Formatting");
			const QString mbi = draw_into(mb, QStyle::CE_MenuBarItem, mo);

			printf("info: a squeezed button is [%s] and a menu bar item"
			       " [%s]\n", qPrintable(btn.trimmed()),
			       qPrintable(mbi.trimmed()));
			CHECK(btn.contains(QLatin1Char('>')),
			      "a button too narrow for its label keeps its closing"
			      " bracket");
			CHECK(mbi.trimmed().size() <= 6,
			      "and a menu bar item does not write past its own cells");
		}

		// A progress bar nobody has set a value on. Qt constructs one with
		// value -1 against a minimum of 0, and QProgressBar::text() returns
		// an EMPTY string for a value below the minimum -- that empty string
		// is Qt saying "there is no percentage to show", and the style read
		// it as "nobody supplied text, so make one up". A fresh or reset bar
		// wrote -1% across itself.
		//
		// The CONTROL is the same bar with a value: the number must still be
		// written, or this would be "fixed" by never labelling anything.
		{
			const auto bar_text = [&](bool set_value) {
				QProgressBar pb;
				if (set_value) pb.setValue(40);
				pb.setFixedSize(cw * 18, ch);
				show(pb, 20, 2);
				CellBuffer b(20, 2);
				render_once(pb, b);
				return b.to_text().trimmed();
			};
			const QString fresh = bar_text(false), set = bar_text(true);
			printf("info: a fresh progress bar reads [%s]; one at 40 reads"
			       " [%s]\n", qPrintable(fresh), qPrintable(set));
			CHECK(set.contains(QStringLiteral("40%")),
			      "a progress bar with a value writes it");
			CHECK(!fresh.contains(QStringLiteral("-1")),
			      "and one with no value yet writes no percentage at all");
		}

		// THE VOCABULARY, PINNED AGAINST THE PAGE THAT PUBLISHES IT.
		// `doc/keyboard-first.md` has a table under "What the marks
		// mean" -- the whole set of characters a control says what it is
		// with -- and a table copied out of source goes stale the first
		// time a glyph moves, silently, in the one document an adopter
		// reads to learn the vocabulary.
		//
		// It exists because the absence cost something measurable: a
		// mark was nearly spelled "(X)" for a message box, which is a
		// CHOSEN RADIO BUTTON here, and the collision was found by
		// remembering the source rather than by reading anything.
		//
		// EVERY ROW, and that is the point of the list rather than a
		// sample of it. The first version of this check covered five of
		// twenty-one and the page said "every row above is rendered by a
		// check" -- a name claiming exhaustiveness over an enumeration
		// that had not achieved it, which is the one shape `evidence.md`
		// says to verify the quantifier of rather than the assertion.
		{
			// A table of DATA and one factory with a switch, rather than a
			// lambda per row: twenty-four inline lambdas nest two levels
			// deeper than the list that holds them, which the style gate
			// reads as under-indentation and which is genuinely harder to
			// scan than a switch.
			struct Row { const char *what; const char *mark; int kind; int cols; };
			static const Row rows[] = {
				{ "push button",              "<Save>",        0, 16 },
				{ "push button with a menu",  "<Save\u25BE>",   1, 16 },
				{ "check box, clear",         "[ ] Wrap",      2, 16 },
				{ "check box, ticked",        "[x] Wrap",      3, 16 },
				{ "check box, partial",       "[-] Wrap",      4, 16 },
				{ "radio, unchosen",          "( ) One",       5, 16 },
				{ "radio, chosen",            "(o) One",       6, 16 },
				{ "line edit",                "[text",         7, 16 },
				{ "password field",           "\u25CF\u25CF\u25CF",  8, 16 },
				{ "field with a clear button", "\u2715]",        9, 16 },
				{ "combo box",                "\u25BE]",        10, 16 },
				{ "spin box",                 "\u25B4\u25BE]",  11, 16 },
				{ "tool button with a menu",  "\u25BE",         12, 16 },
				{ "slider",                   "\u25CF",         13, 16 },
				{ "scroll bar",               "\u25C0",         14, 16 },
				{ "progress bar",             "\u2588",         15, 20 },
				{ "tabs",                     "[One      ][Two      ]", 16, 22 },
				{ "tabs you can close",       "\u2715]",        17, 22 },
				{ "a ticked menu item",       "\u2713 Word wrap", 18, 22 },
				{ "a chosen exclusive item",  "\u2022 By name", 19, 22 },
				{ "a submenu",                "\u25B8",         20, 22 },
				{ "a tree row that opens",    "\u25B8 Folder",  21, 22 },
				{ "a widget out of reach",    "QGraphicsView", 22, 22 },
				// The substitution's own cell, the one row of the table that
				// is not a control. A solid pixmap is the simplest thing that
				// reaches it: both halves agree in colour and neither leans.
				{ "a picture reduced to a cell", "\u2592",      23, 6 },
			};
			const auto marked_menu = []() {
				auto *m = new QMenu;
				auto *wrap = m->addAction(QStringLiteral("Word wrap"));
				wrap->setCheckable(true);
				wrap->setChecked(true);
				auto *g = new QActionGroup(m);
				g->setExclusive(true);
				auto *by = m->addAction(QStringLiteral("By name"));
				by->setCheckable(true);
				g->addAction(by);
				by->setChecked(true);
				m->addMenu(QStringLiteral("Recent"))
				    ->addAction(QStringLiteral("a.txt"));
				return static_cast<QWidget *>(m);
			};
			const auto make = [&](int kind) -> QWidget * {
				switch (kind) {
				case 0:
					return new QPushButton(QStringLiteral("Save"));
				case 1: {
					auto *b = new QPushButton(QStringLiteral("Save"));
					auto *m = new QMenu(b);
					m->addAction(QStringLiteral("x"));
					b->setMenu(m);
					return b;
				}
				case 2:
					return new QCheckBox(QStringLiteral("Wrap"));
				case 3: {
					auto *c = new QCheckBox(QStringLiteral("Wrap"));
					c->setChecked(true);
					return c;
				}
				case 4: {
					auto *c = new QCheckBox(QStringLiteral("Wrap"));
					c->setTristate(true);
					c->setCheckState(Qt::PartiallyChecked);
					return c;
				}
				case 5:
					return new QRadioButton(QStringLiteral("One"));
				case 6: {
					auto *b = new QRadioButton(QStringLiteral("One"));
					b->setChecked(true);
					return b;
				}
				case 7:
					return new QLineEdit(QStringLiteral("text"));
				case 8: {
					auto *e = new QLineEdit(QStringLiteral("secret"));
					e->setEchoMode(QLineEdit::Password);
					return e;
				}
				case 9: {
					auto *e = new QLineEdit(QStringLiteral("query"));
					e->setClearButtonEnabled(true);
					return e;
				}
				case 10: {
					auto *c = new QComboBox;
					c->addItem(QStringLiteral("One"));
					return c;
				}
				case 11: {
					auto *sp = new QSpinBox;
					sp->setValue(3);
					return sp;
				}
				case 12: {
					auto *t = new QToolButton;
					t->setText(QStringLiteral("Cut"));
					auto *m = new QMenu(t);
					m->addAction(QStringLiteral("x"));
					t->setMenu(m);
					return t;
				}
				case 13: {
					auto *sl = new QSlider(Qt::Horizontal);
					sl->setValue(50);
					return sl;
				}
				case 14: {
					auto *sb = new QScrollBar(Qt::Horizontal);
					sb->setRange(0, 100);
					sb->setValue(20);
					return sb;
				}
				case 15: {
					auto *pb = new QProgressBar;
					pb->setValue(40);
					return pb;
				}
				case 16: case 17: {
					auto *t = new QTabBar;
					t->addTab(QStringLiteral("One"));
					t->addTab(QStringLiteral("Two"));
					t->setTabsClosable(kind == 17);
					return t;
				}
				case 18: case 19: case 20:
					return marked_menu();
				case 21: {
					auto *t = new QTreeWidget;
					t->setHeaderHidden(true);
					auto *top = new QTreeWidgetItem(
					    t, QStringList(QStringLiteral("Folder")));
					new QTreeWidgetItem(top, QStringList(QStringLiteral("k")));
					return t;
				}
				case 22:
					return new QGraphicsView;
				case 23: {
					QPixmap pm(GridMetrics::cw(), GridMetrics::ch());
					pm.fill(QColor(128, 128, 128));
					auto *l = new QLabel;
					l->setPixmap(pm);
					return l;
				}
				default:
					return nullptr;
				}
			};
			int wrong = 0;
			QString first_bad;
			for (const Row &row : rows) {
				QWidget *w = make(row.kind);
				w->setAttribute(Qt::WA_DontShowOnScreen);
				const int high = qobject_cast<QMenu *>(w)
				              || qobject_cast<QTreeWidget *>(w)
				              || qobject_cast<QGraphicsView *>(w) ? 6 : 1;
				w->setFixedSize(cw * row.cols, ch * high);
				show(*w, row.cols, high);
				CellBuffer b(row.cols + 2, high + 1);
				render_once(*w, b);
				if (!b.to_text().contains(QString::fromUtf8(row.mark))) {
					++wrong;
					if (first_bad.isEmpty())
						first_bad = QStringLiteral("%1 wanted '%2', got [%3]")
						            .arg(QString::fromLatin1(row.what),
						                 QString::fromUtf8(row.mark),
						                 b.to_text().trimmed()
						                     .split(QLatin1Char('\n')).value(0));
				}
				delete w;
			}
			// AND THE COUNT, READ OFF THE PAGE. The list above is an
			// enumeration and the sentence on the page says "every row",
			// so the quantifier is the thing to verify rather than the
			// assertion under it -- count the population independently
			// of the list the test walks. A row added to the table with
			// no check, or a check with no row, fails here.
			int published = -1;
			QFile page(QStringLiteral(QTTY_SOURCE_DIR)
			           + QStringLiteral("/doc/keyboard-first.md"));
			if (page.open(QIODevice::ReadOnly | QIODevice::Text)) {
				const QStringList lines =
				    QString::fromUtf8(page.readAll()).split(QLatin1Char('\n'));
				bool in_section = false, in_table = false;
				published = 0;
				for (const QString &line : lines) {
					if (line.startsWith(QStringLiteral("## What the marks mean"))) {
						in_section = true;
						continue;
					}
					if (!in_section) continue;
					if (line.startsWith(QStringLiteral("| the cells"))) {
						in_table = true;                  // the header itself
						continue;
					}
					if (!in_table) continue;
					if (line.startsWith(QStringLiteral("|---"))) continue;
					if (!line.startsWith(QStringLiteral("| "))) break;
					++published;
				}
			}
			printf("info: the page publishes %d mark(s), the check renders "
			       "%d; %d did not render\n", published,
			       int(sizeof(rows) / sizeof(rows[0])),
			       wrong);
			CHECK(published == int(sizeof(rows) / sizeof(rows[0])),
			      "the table on the page and the list in this check name "
			      "the same number of marks, so neither can grow alone");
			CHECK(wrong == 0,
			      wrong == 0
			        ? "every mark the page publishes is the mark the style "
			          "draws, all of them rather than a sample"
			        : QStringLiteral("every mark the page publishes is the "
			                         "mark the style draws -- %1")
			              .arg(first_bad).toUtf8().constData());
			// THE COLLISION the page warns about, asserted rather than
			// asserted-about: a chosen radio really is "(o)" and not
			// "(x)", so a mark of somebody's own spelled with brackets
			// or parentheses really would be read as a control.
			QRadioButton chosen(QStringLiteral("One"));
			chosen.setChecked(true);
			chosen.setFixedSize(cw * 16, ch);
			show(chosen, 16, 2);
			CellBuffer rb(18, 3);
			render_once(chosen, rb);
			CHECK(rb.to_text().contains(QStringLiteral("(o)"))
			      && !rb.to_text().contains(QStringLiteral("(x)")),
			      "and a chosen radio is spelled so that a mark of your "
			      "own in parentheses would be read as one");
		}

		// A READ-ONLY LINE EDIT, which section 0b recorded as unmarked
		// and which is marked. The row there was measured at 8.33 --
		// "it renders identically to an editable one, so a user cannot
		// tell they cannot type" -- and the caret-or-mark work answered
		// it afterwards without anybody connecting the two.
		//
		// Measured through a compositor, which is what has the caret:
		//
		//   read-only focused   [readonly] reversed WHOLE, no caret
		//   editable focused    only the selected text reversed, caret
		//
		// The brackets are the difference. A caretless editor gets the
		// mark precisely because it gets no caret, so the two states a
		// user must tell apart are told apart by the rule this library
		// already has -- which is why this is a check rather than a
		// vocabulary decision.
		{
			// THE OTHER WINDOWS GO DOWN FIRST. A compositor composes the
			// SCREEN, so a top level another fixture left up puts its own
			// cells -- and a window tab strip -- into this frame. Measured
			// here: a reversed bracket was found in both states and
			// belonged to neither field. The chat-example block below does
			// the same thing for the same reason.
			QVector<QWidget *> hidden;
			for (QWidget *t : QApplication::topLevelWidgets())
				if (t->isVisible()) { t->hide(); hidden.append(t); }
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			host.resize(GridMetrics::cells(30, 6));
			auto *v = new QVBoxLayout(&host);
			auto *editable = new QLineEdit(QStringLiteral("editable"));
			auto *ro = new QLineEdit(QStringLiteral("readonly"));
			ro->setReadOnly(true);
			v->addWidget(editable);
			v->addWidget(ro);
			host.show();
			InputRouter r(&host);
			QCoreApplication::processEvents();
			const auto look = [&]() {
				CellBuffer b(30, 6);
				Compositor c(&host, &r);
				c.compose(b);
				struct Seen { bool bracket_marked = false; bool caret = false; };
				Seen out;
				out.caret = c.cursor_cell().has_value();
				for (int y = 0; y < 6; ++y)
					for (int x = 0; x < 30; ++x)
						if ((b.at(x, y).attrs & Attr::Reverse)
						    && b.at(x, y).ch == QStringLiteral("["))
							out.bracket_marked = true;
				return out;
			};
			Qtty::test::press(r, Qt::Key_Tab);
			QCoreApplication::processEvents();
			const bool ro_first = host.focusWidget() == ro;
			const auto first = look();
			Qtty::test::press(r, Qt::Key_Tab);
			QCoreApplication::processEvents();
			const auto second = look();
			const auto on_ro = ro_first ? first : second;
			const auto on_edit = ro_first ? second : first;
			CHECK(on_ro.bracket_marked && !on_ro.caret,
			      "a focused read-only line edit marks its brackets and "
			      "shows no caret, which is the caret-or-mark rule "
			      "answering what section 0b left open");
			CHECK(!on_edit.bracket_marked && on_edit.caret,
			      "while a focused editable one shows a caret and leaves "
			      "its brackets alone, so the two are told apart");
			for (QWidget *t : hidden) t->show();
			QCoreApplication::processEvents();
			GridGuard::reset();
		}

		// A LINE EDIT'S CLEAR BUTTON, which drew a cell of noise. It is
		// a private QToolButton subclass whose paintEvent draws the icon
		// itself, so it never reaches QStyle and grid_style.cpp's list of
		// Qt's own furniture could not cover it; what a search field
		// showed was the pixmap substitution's shaded block. The same
		// library already draws a close mark on a closable tab and on a
		// dock widget's title bar, and clearing a field is that act.
		{
			// ONE cell high, which is a line edit's natural height here
			// and the shape a form gives it. At two Qt lays the clear
			// button out at 30x0 -- measured -- so it paints nothing and
			// there is no mark to find: a fixture that says the feature
			// is missing when what is missing is the button. Read as the
			// whole frame rather than the first non-blank row, since a
			// framed edit puts its border there.
			const auto field_row = [&](QLineEdit &e) {
				show(e, 20, 1);
				CellBuffer buf(20, 2);
				render_once(e, buf);
				return buf.to_text();
			};
			QLineEdit e(QStringLiteral("query"));
			e.setClearButtonEnabled(true);
			const QString with_text = field_row(e);
			CHECK(with_text.contains(QChar(0x2715)),
			      "a line edit's clear button draws the same close mark a "
			      "closable tab does");

			// ONLY WHILE THERE IS SOMETHING TO CLEAR. Qt fades the button
			// out on an empty field and hides it when the ANIMATION
			// finishes, so between those two moments it is a visible
			// widget at zero opacity -- and a terminal has no opacity.
			// Measured before this was handled: clear the text, process
			// events once, and the mark was still there.
			e.clear();
			QCoreApplication::processEvents();
			CHECK(!field_row(e).contains(QChar(0x2715)),
			      "and none on an empty field, where a terminal has no "
			      "opacity to fade it with");

			// THE CONTROL: an application's own side widget is NOT the
			// clear button and keeps the substitution. Without it the
			// check above passes for a rule that marks every icon button
			// inside a line edit.
			QLineEdit own(QStringLiteral("query"));
			QAction act(QStringLiteral("Search"), &own);
			act.setIcon(own.style()->standardIcon(QStyle::SP_FileDialogContentsView));
			own.addAction(&act, QLineEdit::TrailingPosition);
			CHECK(!field_row(own).contains(QChar(0x2715)),
			      "while an application's own side widget is left alone, "
			      "the mark naming Qt's clear action rather than any icon "
			      "button in a field");
		}

		// A PUSH BUTTON WITH A MENU, which drew exactly like one without.
		// The tool button arm settles this in as many words -- a menu is
		// an affordance or it is nothing -- and the same argument had not
		// reached the control most applications actually use, so the only
		// way to discover the menu was to press the button.
		//
		// The behaviour was measured before the arrow was drawn, because
		// an affordance for something a key cannot reach is a lie:
		//
		//   Space   activates the button, and opens the menu, in both
		//           a window and a dialog
		//   Enter   activates only in a dialog, plain and menu button
		//           alike, which is Qt's autoDefault and not a gap
		{
			const auto button_cells = [&](bool with_menu) {
				QPushButton b(QStringLiteral("Open"));
				QMenu m(&b);
				m.addAction(QStringLiteral("Recent"));
				if (with_menu) b.setMenu(&m);
				b.setFixedSize(cw * 12, ch);
				show(b, 12, 2);
				CellBuffer buf(12, 2);
				render_once(b, buf);
				return buf.to_text().trimmed();
			};
			const QString plain = button_cells(false);
			const QString menu = button_cells(true);
			CHECK(menu.contains(QChar(0x25BE)) && menu.endsWith(QLatin1Char('>')),
			      "a push button with a menu says so, inside its closing "
			      "bracket");
			// THE CONTROL: one without a menu must NOT carry the arrow,
			// or the check above passes for a style that draws it on
			// every button.
			CHECK(plain == QStringLiteral("<Open>"),
			      "while one without a menu is unchanged, so the arrow "
			      "marks the menu rather than the button");
			// AND THE ARROW COMES OUT OF THE LABEL'S ROOM. A button is as
			// wide as the layout gave it, so an arrow added beside the
			// bracket would push the bracket off the end -- which is the
			// elide fault this arm already carries a comment about.
			QPushButton tight(QStringLiteral("Openable"));
			QMenu tm(&tight);
			tm.addAction(QStringLiteral("Recent"));
			tight.setMenu(&tm);
			tight.setFixedSize(cw * 7, ch);
			show(tight, 7, 2);
			CellBuffer tb(7, 2);
			render_once(tight, tb);
			const QString cut = tb.to_text().trimmed();
			CHECK(cut.startsWith(QLatin1Char('<'))
			      && cut.endsWith(QLatin1Char('>'))
			      && cut.size() == 7 && cut.contains(QChar(0x25BE)),
			      "and a button too narrow for its label keeps both "
			      "brackets and the arrow, losing only label");
		}

		// An application that installs a style of its OWN after setup().
		// QApplication::setStyle() REPLACES, so GridStyle went away and with
		// it every Channel A drawing in the program -- silently, everywhere
		// at once. A surveyed sibling does this today to supply its toolbar
		// icons, which is an ordinary thing for a Qt program to want.
		//
		// The CONTROL is the button BEFORE the replacement: it must draw as
		// cells to begin with, or the assertion after would pass against a
		// button that never drew properly at all.
		//
		// Note this check cannot put the old style back and does not try.
		// setStyle() ADOPTS its argument and DELETES the previous style, so
		// saving the pointer and restoring it follows a dangling one -- which
		// segfaulted the whole suite when this was first written that way.
		// It is safe to leave because the fix re-wraps: what the suite is
		// left with is a GridStyle again.
		{
			const auto render_button = [&] {
				QPushButton b(QStringLiteral("Push"));
				b.setFixedSize(cw * 8, ch);
				show(b, 10, 2);
				CellBuffer buf(10, 2);
				render_once(b, buf);
				return buf.to_text().trimmed();
			};
			const QString before = render_button();
			QApplication::setStyle(QStringLiteral("Fusion"));
			QCoreApplication::processEvents();
			const QString after = render_button();
			printf("info: a button draws [%s]; after the application sets its"
			       " own style, [%s]\n", qPrintable(before), qPrintable(after));
			CHECK(before == QStringLiteral("<Push>"),
			      "a button draws as cells");
			CHECK(after == before,
			      "and still does after the application installs its own"
			      " style");
		}

		// AN APPLICATION STYLE SHEET, which is the other way Qt replaces
		// the app style and which CRASHED. setStyleSheet() wraps the
		// current style in a QStyleSheetStyle and installs that; the
		// filter above saw a StyleChange to something that is not a
		// GridStyle and re-wrapped from inside Qt's own setStyle(), which
		// deleted the sheet style while the outer call was still using it.
		// Segfault in QMetaObject::cast(), reproduced in forty lines of
		// plain Qt with no qtty in them.
		//
		// The sheet is CLEARED at the end, and that matters beyond
		// tidiness: everything this suite renders afterwards would
		// otherwise be drawn through it.
		{
			const auto render_three = [&] {
				QWidget host;
				auto *v = new QVBoxLayout(&host);
				v->addWidget(new QPushButton(QStringLiteral("Save")));
				v->addWidget(new QCheckBox(QStringLiteral("Wrap")));
				show(host, 24, 6);
				CellBuffer buf(24, 6);
				render_once(host, buf);
				return buf.to_text();
			};
			const QString before = render_three();
			qApp->setStyleSheet(QStringLiteral("QPushButton { padding: 2px }"));
			QCoreApplication::processEvents();
			const QString during = render_three();
			qApp->setStyleSheet(QString());
			QCoreApplication::processEvents();
			const QString after = render_three();
			// Reaching this line at all is most of the check: the crash
			// was inside setStyleSheet() and took the process with it.
			CHECK(during.contains(QStringLiteral("Save")),
			      "an application style sheet does not take the process "
			      "down with it");
			// The sheet matches QPushButton and nothing else, so the
			// check box is the control that says GridStyle is still
			// underneath rather than gone.
			CHECK(during.contains(QStringLiteral("[ ] Wrap")),
			      "and a control the sheet does not match still draws as "
			      "cells, so GridStyle is still the sheet's base");
			// THE CONTROL, and it is what says the sheet did anything at
			// all: without it both checks above pass for a sheet that was
			// silently dropped.
			CHECK(before.contains(QStringLiteral("<Save>"))
			      && !during.contains(QStringLiteral("<Save>")),
			      "while the button the sheet DOES match loses its "
			      "brackets, which is practice 12 measured");
			CHECK(after == before,
			      "and clearing the sheet puts every one of them back");
		}

		// RIGHT TO LEFT, which nothing in this library mentioned. Qt's
		// layouts mirror on their own and GridStyle followed them for
		// anything laid out -- a form's rows, a check box's indicator, a
		// tab bar's order. What it did NOT follow is the four controls it
		// positions itself, and for those a mirrored layout read wrong
		// rather than merely looking unfamiliar: a progress bar filled
		// from the wrong end, so a meter showed its own complement.
		//
		// The intent is Qt's, measured rather than assumed, with plain Qt
		// and Fusion and nothing of this library in it:
		//
		//   QProgressBar at 25%  every lit pixel left, then every one right
		//   QScrollBar 10/100    thumb 35..60, then 199..224, of 260
		//   QSpinBox             up-button 108..121, then -2..11, of 120
		//   QLabel               ink left BOTH times -- Qt does not mirror it
		//
		// The label is in that list because it was the first suspicion and
		// the measurement refused it. It is drawn the same in both
		// directions here, and that is correct.
		{
			const auto in_both = [&](std::function<QWidget *()> make,
			                         int cols, int rows) {
				QApplication::setLayoutDirection(Qt::LeftToRight);
				QWidget *a = make();
				a->setFixedSize(cw * cols, ch * rows);
				show(*a, cols, rows + 1);
				CellBuffer ab(cols, rows + 1);
				render_once(*a, ab);
				const QString ltr = ab.to_text();
				delete a;
				QApplication::setLayoutDirection(Qt::RightToLeft);
				QWidget *b = make();
				b->setFixedSize(cw * cols, ch * rows);
				show(*b, cols, rows + 1);
				CellBuffer bb(cols, rows + 1);
				render_once(*b, bb);
				const QString rtl = bb.to_text();
				delete b;
				QApplication::setLayoutDirection(Qt::LeftToRight);
				return QPair<QString, QString>(ltr.trimmed(), rtl.trimmed());
			};
			const auto bar = in_both([] {
				auto *p = new QProgressBar;
				p->setRange(0, 100);
				p->setValue(40);
				return static_cast<QWidget *>(p);
			}, 20, 1);
			// The lit run is at the start in one direction and at the end
			// in the other. Asserted on WHERE the fill is rather than on
			// the whole row, because the percentage sits in the middle and
			// is the same either way.
			const QChar full(0x2588);
			CHECK(bar.first.startsWith(full) && !bar.first.endsWith(full),
			      "a progress bar fills from the left in a left-to-right "
			      "layout");
			CHECK(bar.second.endsWith(full) && !bar.second.startsWith(full),
			      "and from the right in a right-to-left one, which is "
			      "Qt's own behaviour and was the complement of it");

			// THE CONTROL, and it is the one the measurement above earned:
			// a label is NOT mirrored, so a change that mirrored
			// everything would be caught here rather than praised.
			const auto lbl = in_both([] {
				return static_cast<QWidget *>(new QLabel(QStringLiteral("Hi")));
			}, 20, 1);
			CHECK(lbl.first == lbl.second && lbl.first.startsWith(QStringLiteral("Hi")),
			      "while a label is drawn the same in both, because Qt "
			      "does not mirror one either");
		}

		// BIDIRECTIONAL TEXT, PINNED AS MEASURED RATHER THAN AS WANTED.
		// A string mixing scripts does not survive the trip to the cells:
		// the runs are reordered and the last letter is lost. That is not
		// the layout direction and setting it changes nothing -- a
		// terminal decides for itself whether it reorders what it is
		// sent, and this library does not yet take a position on which
		// order to send.
		//
		// Recorded so that whoever changes it finds the page that says
		// so. This check is meant to be UPDATED, not satisfied: it goes
		// red the day bidi is handled, and `doc/keyboard-first.md` under
		// "Right to left" is what has to change with it.
		{
			const QString want = QString::fromUtf8("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d abc");
			QLabel lab(want);
			lab.setFixedSize(cw * 20, ch);
			show(lab, 20, 2);
			CellBuffer b(20, 2);
			render_once(lab, b);
			const QString got =
			    b.to_text().split(QLatin1Char('\n')).value(0).trimmed();
			printf("info: a bidirectional label asked for %d code point(s) "
			       "and %d reached the cells\n", int(want.size()),
			       int(got.size()));
			CHECK(want.size() == 8 && got.size() == 7
			      && got.startsWith(QStringLiteral("abc")),
			      "bidirectional text reaches the cells reordered and one "
			      "letter short -- pinned as measured, and the page that "
			      "says so changes when this does");
		}

		// AND THE HIT TEST AGREES WITH THE PICTURE IN BOTH DIRECTIONS,
		// which is the half that breaks silently: a mirrored drawing whose
		// subControlRect stayed put puts the arrow in a cell no click
		// reaches. Both derivations were changed together for exactly this
		// reason, and this is what says they still match. The first
		// version of the spin box mirror failed here, one cell out.
		{
			for (int pass = 0; pass < 2; ++pass) {
				const bool rtl = pass == 1;
				QApplication::setLayoutDirection(rtl ? Qt::RightToLeft
				                                     : Qt::LeftToRight);
				QScrollBar sb(Qt::Horizontal);
				sb.setRange(0, 100);
				sb.setValue(20);
				sb.setPageStep(10);
				sb.setFixedSize(cw * 20, ch);
				show(sb, 20, 2);
				CellBuffer b(20, 2);
				render_once(sb, b);
				const QString row = b.to_text().split(QLatin1Char('\n')).value(0);
				const int drawn = row.indexOf(QChar(0x2588));
				QStyleOptionSlider o;
				o.initFrom(&sb);
				o.minimum = 0; o.maximum = 100; o.sliderPosition = 20;
				o.sliderValue = 20; o.pageStep = 10;
				o.orientation = Qt::Horizontal; o.rect = sb.rect();
				o.subControls = QStyle::SC_All;
				const QRect th = sb.style()->subControlRect(
				    QStyle::CC_ScrollBar, &o, QStyle::SC_ScrollBarSlider, &sb);
				CHECK(drawn >= 0 && drawn * cw >= th.left()
				      && drawn * cw <= th.right(),
				      rtl ? "a right-to-left scroll bar's thumb is where a "
				            "click reaches it"
				          : "a left-to-right scroll bar's thumb is where a "
				            "click reaches it");

				// THE COMBO'S ARROW, the fifth control of this family
				// and the one 8.284 missed. Found by re-measuring
				// section 0b's right-to-left row, which still named it
				// among the things that do not mirror -- and was right
				// about that one while being stale about three others.
				QComboBox cb;
				cb.addItem(QStringLiteral("One"));
				cb.setFixedSize(cw * 18, ch);
				show(cb, 18, 2);
				CellBuffer cbb(19, 2);
				render_once(cb, cbb);
				const QString crow = cbb.to_text().split(QLatin1Char('\n')).value(0);
				const int arrow_drawn = crow.indexOf(QChar(0x25BE));
				QStyleOptionComboBox co;
				co.initFrom(&cb);
				co.rect = cb.rect();
				co.subControls = QStyle::SC_All;
				const QRect ar = cb.style()->subControlRect(
				    QStyle::CC_ComboBox, &co, QStyle::SC_ComboBoxArrow, &cb);
				CHECK(arrow_drawn >= 0 && arrow_drawn == ar.left() / cw,
				      rtl ? "and a right-to-left combo box's arrow is drawn"
				            " in the cell its hit test names"
				          : "and a left-to-right combo box's arrow is drawn"
				            " in the cell its hit test names");

				QSpinBox sp;
				sp.setFixedSize(cw * 12, ch);
				show(sp, 12, 2);
				CellBuffer sb2(12, 2);
				render_once(sp, sb2);
				const QString srow = sb2.to_text().split(QLatin1Char('\n')).value(0);
				const int up_drawn = srow.indexOf(QChar(0x25B4));
				QStyleOptionSpinBox so;
				so.initFrom(&sp);
				so.rect = sp.rect();
				so.subControls = QStyle::SC_All;
				const QRect up = sp.style()->subControlRect(
				    QStyle::CC_SpinBox, &so, QStyle::SC_SpinBoxUp, &sp);
				CHECK(up_drawn >= 0 && up_drawn == up.left() / cw,
				      rtl ? "and a right-to-left spin box's step-up arrow is"
				            " drawn in the cell its hit test names"
				          : "and a left-to-right spin box's step-up arrow is"
				            " drawn in the cell its hit test names");
			}
			QApplication::setLayoutDirection(Qt::LeftToRight);
		}

		// A scroll bar squeezed to ONE cell. The control returned without
		// drawing anything at all below two cells, so the cell was blank and
		// nothing said the view scrolls.
		//
		// It is reached by qtty's own documented hint: setting the
		// "qtty.cells" property to 1x1 is how an application says a control
		// needs one cell, and saying it produced an invisible control. A
		// table with both bars in a two-row slot reaches it too, the
		// horizontal bar taking the vertical one's second row.
		//
		// The two-cell CONTROL is what makes this mean anything: at two
		// cells the arrows are drawn and no position at all, so the guard
		// was not withholding position information -- it was withholding
		// the last "a bar is here". The same argument CC_ToolButton settles
		// three arms away in this function, the other way round: "two cells
		// of [] say a button is here and nothing else, and that is more
		// than two blank cells say".
		{
			const auto bar_cells = [&](int rows) {
				QScrollBar sb(Qt::Vertical);
				sb.setRange(0, 100);
				sb.setValue(50);
				sb.setFixedSize(cw, rows * ch);
				show(sb, 3, rows + 1);
				CellBuffer b(3, rows + 1);
				render_once(sb, b);
				return b.to_text().trimmed();
			};
			const QString one = bar_cells(1), two = bar_cells(2);
			printf("info: a scroll bar draws [%s] at one cell and [%s] at"
			       " two\n", qPrintable(one.simplified()),
			       qPrintable(two.simplified()));
			CHECK(!two.isEmpty(), "a two-cell scroll bar draws something");
			CHECK(!one.isEmpty(),
			      "and a one-cell scroll bar says a bar is there");
		}

		// A framed scroll area shorter than three rows. SE_FrameContents
		// insets by PM_DefaultFrameWidth on all four sides, so at two rows
		// QCommonStyle's answer is a viewport of nearly nothing and at one
		// row it is NEGATIVE -- and this style declined to correct either,
		// because the branch carried an r.isValid() guard and a
		// three-row floor. The comment said Fusion's answer was "wrong by
		// less than an empty viewport is"; measured, Fusion's answer IS an
		// empty viewport.
		//
		// Asserted on the ITEM TEXT rather than on the rectangle, because
		// the rectangle being right is not the claim -- the claim is that
		// a list two rows tall shows a list.
		{
			const auto rows_of = [&](int cells_tall) {
				QListWidget lw;
				lw.addItem(QStringLiteral("one"));
				lw.addItem(QStringLiteral("two"));
				lw.setFixedSize(6 * cw, cells_tall * ch);
				show(lw, 8, cells_tall + 1);
				CellBuffer b(8, cells_tall + 1);
				render_once(lw, b);
				return b.to_text();
			};
			const QString two = rows_of(2), four = rows_of(4);
			printf("info: a 2-row list shows [%s]; a 4-row list shows"
			       " [%s]\n",
			       qPrintable(two.simplified()),
			       qPrintable(four.simplified()));
			// The 4-row case is the CONTROL: it must show an item either
			// way, or a blank 2-row result would only prove the fixture
			// was broken.
			CHECK(four.contains(QStringLiteral("one")),
			      "a framed list four rows tall shows its first item");
			CHECK(two.contains(QStringLiteral("one")),
			      "and a framed list two rows tall shows it too");
		}

		// A tab bar with more tabs than fit. Qt gives it two scroll
		// QToolButtons sized from PM_TabBarScrollButtonWidth -- 16 px,
		// which is 1.6 columns here -- and those two widgets are exempt
		// from BOTH GridGuard and GridSnap (a QToolButton child of a
		// QTabBar, by name). So they are the one class of off-grid widget
		// nothing reports and nothing repairs, and they are clickable.
		{
			QWidget host;
			auto *bar = new QTabBar(&host);
			for (int i = 0; i < 12; ++i)
				bar->addTab(QStringLiteral("Tab %1").arg(i));
			bar->setGeometry(0, 0, 20 * cw, ch);
			show(host, 22, 4);
			// VISIBLE ones. QTabBar constructs both scroll buttons in its
			// init() unconditionally -- before it reads the hint -- and the
			// hint decides only whether they are shown and positioned. A
			// check counting children therefore finds two either way and
			// cannot fail.
			int off_grid = 0, buttons = 0;
			for (QObject *o : bar->children()) {
				auto *b = qobject_cast<QWidget *>(o);
				if (!b || !qobject_cast<QToolButton *>(b) || b->isHidden())
					continue;
				++buttons;
				const QRect g = b->geometry();
				if (g.x() % cw || g.width() % cw || g.y() % ch
				    || g.height() % ch)
					++off_grid;
			}
			printf("info: an overflowing tab bar has %d tool button(s), %d"
			       " off the grid\n", buttons, off_grid);
			CHECK(off_grid == 0,
			      "an overflowing tab bar puts no off-grid widget on the"
			      " screen");
		}

		// A VERTICAL closable tab bar. CT_TabBarTab's West/East branch
		// rebuilds the width from the text and the two brackets, discarding
		// the proxied width -- which is where Qt reserves room for the close
		// button. So the button has nowhere of its own to go, and
		// SE_TabBarTabRightButton snaps it onto a neighbouring tab's row.
		{
			QWidget host;
			auto *bar = new QTabBar(&host);
			bar->setShape(QTabBar::RoundedWest);
			bar->setTabsClosable(true);
			bar->addTab(QStringLiteral("General"));
			bar->addTab(QStringLiteral("Advanced"));
			// The HINT is what this asserts, below; the render uses the
			// size the bar asks for. Squeezed to one row per tab the button
			// has nowhere to go and the defect returns -- which is true of
			// any widget below its hint, and is why the hint is the thing
			// to fix.
			bar->setGeometry(0, 0, 14 * cw, bar->sizeHint().height());
			show(host, 16, 6);
			CellBuffer vb(16, 6);
			render_once(host, vb);
			const QStringList rows = vb.to_text().split(QLatin1Char('\n'));
			for (int i = 0; i < qMin(4, int(rows.size())); ++i)
				printf("info: vertical tab row %d [%s]\n", i,
				       qPrintable(rows.value(i)));
			// The close glyph must not land inside a word. Asserted as: no
			// row carries the glyph with a letter immediately either side of
			// it, which is what "drawn into the neighbour's label" looks
			// like and holds however many tabs there are.
			bool inside_a_word = false;
			for (const QString &row : rows) {
				const int x = row.indexOf(QChar(0x2715));
				if (x > 0 && x + 1 < row.size()
				    && row.at(x - 1).isLetter() && row.at(x + 1).isLetter())
					inside_a_word = true;
			}
			CHECK(!inside_a_word,
			      "a vertical tab's close button is not drawn inside a"
			      " neighbouring label");

			// And the measurement that makes it so, which is what a squeezed
			// bar cannot show: a vertical tab carrying a close button is
			// measured for a row to put it in.
			QStyleOptionTab vt;
			vt.shape = QTabBar::RoundedWest;
			vt.text = QStringLiteral("General");
			const int plain_h = QApplication::style()->sizeFromContents(
			    QStyle::CT_TabBarTab, &vt, QSize(), nullptr).height();
			vt.rightButtonSize = QSize(cw, ch);
			const int with_button = QApplication::style()->sizeFromContents(
			    QStyle::CT_TabBarTab, &vt, QSize(), nullptr).height();
			printf("info: a vertical tab is %d row(s), and %d with a close"
			       " button\n", plain_h / ch, with_button / ch);
			CHECK(plain_h == ch && with_button == 2 * ch,
			      "and a vertical tab with one is measured for the row it"
			      " needs");
		}

		// A tab pads its label to the width of the tab, and it counted the
		// padding in QCHARS while the room is in CELLS. A wide cluster is
		// one QChar and two cells, so a tab titled with one CJK character
		// was padded as though it were one cell wide -- the label then
		// overran the tab and the outer elide dropped the closing bracket
		// for an ellipsis. A tab that is not truncated then looks as though
		// it is, and the bracket is what says where a tab ENDS.
		{
			QWidget host;
			auto *bar = new QTabBar(&host);
			bar->addTab(QString::fromUtf8("\u4e2d"));
			bar->setGeometry(0, 0, 8 * cw, ch);
			show(host, 12, 3);
			CellBuffer tb(12, 3);
			render_once(host, tb);
			const QString row = tb.to_text().split(QLatin1Char('\n')).value(0);
			printf("info: a tab titled with one wide cluster renders [%s]\n",
			       qPrintable(row.trimmed()));
			CHECK(row.contains(QLatin1Char(']')),
			      "a tab that fits keeps its closing bracket");
			CHECK(!row.contains(QChar(0x2026)),
			      "and is not elided when it did not need to be");
		}

		// A framed view's viewport, which is inset by PM_DefaultFrameWidth on
		// ALL FOUR sides -- and that metric is cw, a width. On the vertical
		// axis cw is not a row, so the viewport starts part-way down a cell
		// and every model row is offset by the remainder. At 10x19 the paint
		// and the click happen to round the same way; at 8x16 they do not,
		// and a click on the cell showing row 0 selects row 1.
		//
		// Asserted as the RULE rather than a coordinate, so it holds at any
		// cell: the viewport's origin is a whole number of cells in both
		// axes. The second half refuses a fix that removes the frame.
		//
		// The DEFAULT frame, deliberately. Twenty-seven fixtures in this
		// tree call setFrameShape(NoFrame), which is why nothing had ever
		// exercised this.
		{
			QWidget host;
			auto *lw = new QListWidget(&host);
			lw->addItem(QStringLiteral("r0"));
			lw->addItem(QStringLiteral("r1"));
			lw->setGeometry(0, 0, 12 * cw, 5 * ch);
			show(host, 14, 6);
			const QPoint off = lw->viewport()->mapTo(lw, QPoint(0, 0));
			printf("info: a framed view's viewport sits at %d,%d on a %dx%d"
			       " cell\n", off.x(), off.y(), cw, ch);
			CHECK(off.x() % cw == 0 && off.y() % ch == 0,
			      "a framed view's viewport starts on a cell in both axes");
			CHECK(off.y() > 0,
			      "and it still has a frame above it");

			// And the smallest thing the inset can describe is three rows:
			// two borders and one of content. At exactly two the
			// subtraction gives a height of zero, which QRect calls invalid
			// and QFrame derives its four widths from.
			//
			// Asked of subElementRect directly rather than through a
			// widget. The first version built a two-row QListWidget and
			// asserted its viewport was valid -- and passed with the guard
			// removed, because a widget that small never reaches this
			// branch at all: Fusion's own inset is not the one the guard
			// recognises. A check that cannot fail is worse than none, so
			// this asks the function the question instead.
			// A QStyleOptionFrame with a lineWidth, which is what
			// QCommonStyle needs to answer SE_FrameContents at all: asked
			// with a bare QStyleOption it returns an empty rect and the
			// check reads 0x0 whatever the guard says.
			QStyleOptionFrame fo;
			fo.lineWidth = QApplication::style()->pixelMetric(
			    QStyle::PM_DefaultFrameWidth);
			fo.frameShape = QFrame::StyledPanel;
			fo.rect = QRect(0, 0, 12 * cw, 2 * ch);
			const QRect tinyc = QApplication::style()->subElementRect(
			    QStyle::SE_FrameContents, &fo, nullptr);
			fo.rect = QRect(0, 0, 12 * cw, 3 * ch);
			const QRect okc = QApplication::style()->subElementRect(
			    QStyle::SE_FrameContents, &fo, nullptr);
			printf("info: frame contents are %dx%d at two rows and %dx%d at"
			       " three\n", tinyc.width(), tinyc.height(),
			       okc.width(), okc.height());
			CHECK(tinyc.height() > 0 && okc.height() == ch,
			      "a frame too short for the inset keeps a contents rect,"
			      " and a taller one gets its row");
		}

		// A tree's indent band is both the expander's picture and its hit
		// target: QTreeView::drawBranches and itemDecorationRect build the
		// same rectangle from PM_TreeViewIndentation. QCommonStyle answers a
		// hardcoded 20 px, so the band is a whole number of cells only where
		// cw divides 20 -- at cw = 8 it rounds to three cells, the glyph is
		// drawn in a cell whose centre is past the band's right edge, and
		// the expander cannot be clicked at any level.
		//
		// Asked at TWO cell sizes, which is the whole of the check: 20 is a
		// multiple of this machine's 10, so "the metric is a multiple of cw"
		// is true of the broken value and discriminates nothing. Only the
		// second size separates a constant from a derived one.
		{
			const auto indent = [] {
				return QApplication::style()->pixelMetric(
				    QStyle::PM_TreeViewIndentation);
			};
			const int here = indent();
			GridMetrics::set(cw + 3, ch + 5);
			const int there = indent();
			GridMetrics::set(cw, ch);
			printf("info: a tree indents %d px on a %d-wide cell and %d on a"
			       " %d-wide one\n", here, cw, there, cw + 3);
			CHECK(here == 2 * cw && there == 2 * (cw + 3),
			      "a tree's indent is two columns, at either cell size");

			// The header's sort mark, for the same reason and by the same
			// method. QCommonStyle answers a fraction of the font height,
			// which need not be a whole column -- and SE_HeaderLabel and
			// SE_HeaderArrow are derived from it by independent roundings,
			// so where it lands on a half column the arrow takes the
			// label's last cell, which is its ellipsis whenever the label
			// was elided.
			const auto mark = [] {
				return QApplication::style()->pixelMetric(
				    QStyle::PM_HeaderMarkSize);
			};
			const int mark_here = mark();
			GridMetrics::set(cw + 3, ch + 5);
			const int mark_there = mark();
			GridMetrics::set(cw, ch);
			printf("info: a header's sort mark is %d px and %d px at the two"
			       " cell sizes\n", mark_here, mark_there);
			CHECK(mark_here == cw && mark_there == cw + 3,
			      "a header's sort mark is one column, at either cell size");

			// A menu's tearoff and scroller strips, the same shape again:
			// both are heights QCommonStyle answers as 10 px, and each is
			// added to the y-origin of every item below it.
			const auto strip = [](QStyle::PixelMetric m) {
				return QApplication::style()->pixelMetric(m);
			};
			const int tear_here = strip(QStyle::PM_MenuTearoffHeight);
			const int scroll_here = strip(QStyle::PM_MenuScrollerHeight);
			GridMetrics::set(cw + 3, ch + 5);
			const int tear_there = strip(QStyle::PM_MenuTearoffHeight);
			GridMetrics::set(cw, ch);
			printf("info: a menu's tearoff is %d px and %d px; its scroller"
			       " %d px\n", tear_here, tear_there, scroll_here);
			CHECK(tear_here == ch && tear_there == ch + 5
			      && scroll_here == ch,
			      "a menu's tearoff and scroller are one row, at either cell"
			      " size");
		}

		// A menu row, drawn straight rather than through a QMenu. QMenu only
		// ever GROWS a column -- it takes the maximum of every item's hint
		// and floors it -- so an auto-sized menu always has slack and the
		// budget never binds. The narrow case that is reachable in a real
		// program is a non-editable QComboBox popup, whose items are drawn
		// at the combo's own width. Calling drawControl directly asks the
		// same question without the popup.
		{
			const auto menu_row = [&](const QString &text, int cells) {
				CellBuffer mb(cells, 1);
				Qtty::CellPaintDevice mdev(mb);
				QPainter mp(&mdev);
				QStyleOptionMenuItem mi;
				mi.rect = QRect(0, 0, cells * cw, ch);
				mi.state = QStyle::State_Enabled;
				mi.menuItemType = QStyleOptionMenuItem::Normal;
				mi.checkType = QStyleOptionMenuItem::NotCheckable;
				// Said outright, because the DEFAULT is the other way:
				// QStyleOptionMenuItem constructs with
				// menuHasCheckableItems TRUE, so an option built by hand
				// claims its menu has toggles in it unless told otherwise --
				// and the style then reserves the check column for this row,
				// which is two cells of the twelve this fixture is counting.
				// Measured rather than assumed, a real QMenu setting the
				// field correctly and only a hand-built option meeting the
				// default.
				mi.menuHasCheckableItems = false;
				mi.text = text;
				QApplication::style()->drawControl(QStyle::CE_MenuItem, &mi,
				                                   &mp, nullptr);
				mp.end();
				return mb.to_text().trimmed();
			};
			// Eleven characters in a twelve-cell row: one cell of indent and
			// eleven of label is exactly the row. One shorter and it fits
			// either way, which is a fixture that proves nothing.
			const QString full = menu_row(QStringLiteral("Preferences"), 12);
			// And a label that does NOT fit beside its shortcut. The
			// shortcut is written after the label, so it lands on the
			// label's own tail -- and on its ellipsis, which is what makes a
			// truncated item read as a complete shorter one.
			const QString tight = menu_row(QStringLiteral("Save As\tCtrl+S"), 12);
			printf("info: menu rows are [%s] and [%s]\n",
			       qPrintable(full), qPrintable(tight));
			CHECK(full == QStringLiteral("Preferences"),
			      "a menu label may use the last cell of its own row");
			CHECK(tight.contains(QStringLiteral("Ctrl+S"))
			      && tight.contains(QChar(0x2026)),
			      "and a label that does not fit beside its shortcut is"
			      " elided rather than overwritten");
			// And narrower still, where there is no room for both at all.
			// elide_to_cells returns nothing for a budget of zero or less,
			// so the label contributed no cells and the shortcut alone
			// became the item's name -- the same damage as the twelve-cell
			// case, reached at a width the fix for it did not cover. A
			// command's NAME is what identifies it; the accelerator is
			// redundant beside it, so the name wins the room.
			const QString narrow = menu_row(QStringLiteral("Save As\tCtrl+S"), 8);
			printf("info: the same item in eight cells is [%s]\n",
			       qPrintable(narrow));
			CHECK(narrow.contains(QStringLiteral("Sa")),
			      "a menu item too narrow for both keeps its own name");
		}

		// What the style MEASURES an item as, against the row it DRAWS.
		// CT_ItemViewItem fell to the default arm, which ceilings Fusion's
		// answer -- and Fusion's omits the cell of indent this style draws
		// and absorbs the indicator's two-pixel margin in the ceiling. So a
		// column sized to its contents came out one cell short and the last
		// character elided. CellItemDelegate::sizeHint derives it correctly,
		// so this bites only where the delegate is absent and the style
		// draws the row itself.
		//
		// The plain case is asserted as well as the checkable one: the
		// missing cell is the INDENT, which both have, and a fix written as
		// a check-box special case would leave the plain row short.
		{
			QStyleOptionViewItem vo;
			vo.features = QStyleOptionViewItem::HasDisplay
			            | QStyleOptionViewItem::HasCheckIndicator;
			vo.text = QStringLiteral("label");
			QStyle *st = QApplication::style();
			const int checkable =
			    st->sizeFromContents(QStyle::CT_ItemViewItem, &vo, QSize(),
			                         nullptr).width();
			vo.features &= ~QStyleOptionViewItem::HasCheckIndicator;
			const int plain =
			    st->sizeFromContents(QStyle::CT_ItemViewItem, &vo, QSize(),
			                         nullptr).width();
			printf("info: an item measures %d cell(s) checkable and %d plain,"
			       " for a five-cell label\n", checkable / cw, plain / cw);
			// 1 indent + 4 for "[x] " + 5 label, and 1 + 5 without.
			CHECK(checkable == (1 + 4 + 5) * cw,
			      "a checkable item is measured for the indent it is drawn"
			      " with");
			CHECK(plain == (1 + 5) * cw,
			      "and so is a plain one");
		}

		// A CHECKABLE group box, which is a different drawing from a plain
		// one: CC_GroupBox is not drawn by this style, so QCommonStyle draws
		// the title and then the indicator, in that order. The title goes
		// into SC_GroupBoxLabel -- the whole top row here, with nothing
		// reserved -- and QCommonStyle forces AlignHCenter, which beats
		// QGroupBox's AlignLeft. The indicator then lands at eight PIXELS
		// in, which rounds to cell 1, and overwrites the centred title's
		// first letters.
		//
		// Fourteen cells, and the width is the discriminating part: the
		// title is centred, so it only reaches the indicator's cells when
		// the box is narrow enough. "Advanced" is eight cells, so a box of
		// 24 renders correctly today and would prove nothing.
		{
			QWidget host;
			auto *box = new QGroupBox(QStringLiteral("Advanced"), &host);
			box->setCheckable(true);
			box->setMinimumSize(0, 0);
			box->setGeometry(2 * cw, 1 * ch, 14 * cw, 4 * ch);
			show(host, 20, 7);
			CellBuffer gb(20, 7);
			render_once(host, gb);

			const QPoint title = findText(gb, QStringLiteral("Advanced"));
			// [x], not [ ]: a checkable QGroupBox starts checked.
			const QPoint mark = findText(gb, QStringLiteral("[x]"));
			printf("info: a checkable group box puts its indicator at %d,%d and its"
			       " title at %d,%d\n", mark.x(), mark.y(),
			       title.x(), title.y());

			CHECK(title.x() >= 0,
			      "a checkable group box's title survives its own check box");
			CHECK(mark.x() >= 0 && title.x() >= 0 && mark.y() == title.y()
			      && title.x() > mark.x() + 2,
			      "and begins after the indicator rather than under it");
		}

		const QPoint on = findText(b, QStringLiteral("[x]"));
		const QPoint off = findText(b, QStringLiteral("[ ]"));
		CHECK(on.x() >= 0 && off.x() >= 0, "table draws check state as [x] and [ ]");
		const QPoint label = findText(b, QStringLiteral("open"));
		CHECK(on.x() >= 0 && label.y() == on.y()
		      && label.x() == on.x() + CellItemDelegate::check_cells(),
		      "display text follows the check indicator");
		// The far edge of column 1, in cells. Derived from the view rather
		// than written down, so the check holds if a section size changes.
		const int column_right = (table.viewport()->x() + table.columnViewportPosition(1)
		                          + table.columnWidth(1)) / cw - 1;
		const QPoint number = findText(b, QStringLiteral("42"));
		CHECK(number.x() >= 0 && number.x() + 1 == column_right,
		      "AlignRight lands against the column's far edge");
		const int row_top = (table.viewport()->y() + table.rowViewportPosition(1)) / ch;
		CHECK(findText(b, QStringLiteral("shut")).y() == row_top + 1,
		      "AlignVCenter centres text in a three-row section");
	}
	// The state and the font, which are the two the delegate wrote nothing for
	// and which the probe method found by rendering a configuration nothing
	// exercised (project.md section 0d).
	//
	// Both checks are a PAIR against a neighbouring item, deliberately.
	// Asserting that a disabled label is dim, alone, passes on any row the
	// style has filled dim underneath -- and the fill was already correct
	// while the label was not, which is the exact state that shipped. What
	// discriminates is the difference between two items in one view.
	{
		QStandardItemModel m;
		auto *on = new QStandardItem(QStringLiteral("enabled"));
		auto *off = new QStandardItem(QStringLiteral("disabled"));
		off->setFlags(Qt::ItemIsSelectable);           // everything but enabled
		m.appendRow(on);
		m.appendRow(off);
		QListView list;
		list.setModel(&m);
		list.setItemDelegate(new CellItemDelegate(&list));
		list.setFrameShape(QFrame::NoFrame);
		show(list, 20, 3);
		CellBuffer b(22, 4);
		render_once(list, b);
		const QPoint live = findText(b, QStringLiteral("enabled"));
		const QPoint dead = findText(b, QStringLiteral("disabled"));
		CHECK(live.x() >= 0 && dead.x() >= 0 && !(b.at(live.x(), live.y()).attrs & Attr::Dim)
		      && (b.at(dead.x(), dead.y()).attrs & Attr::Dim),
		      "a disabled item's label is dim and an enabled one's is not");
	}
	{
		QStandardItemModel m;
		QFont bold = QApplication::font();
		bold.setBold(true);
		auto *heavy = new QStandardItem(QStringLiteral("heavy"));
		heavy->setData(bold, Qt::FontRole);
		m.appendRow(heavy);
		m.appendRow(new QStandardItem(QStringLiteral("light")));
		QListView list;
		list.setModel(&m);
		list.setItemDelegate(new CellItemDelegate(&list));
		list.setFrameShape(QFrame::NoFrame);
		show(list, 20, 3);
		CellBuffer b(22, 4);
		render_once(list, b);
		const QPoint heavy_at = findText(b, QStringLiteral("heavy"));
		const QPoint light_at = findText(b, QStringLiteral("light"));
		CHECK(heavy_at.x() >= 0 && light_at.x() >= 0
		      && (b.at(heavy_at.x(), heavy_at.y()).attrs & Attr::Bold)
		      && !(b.at(light_at.x(), light_at.y()).attrs & Attr::Bold),
		      "Qt::FontRole reaches the cells: a bold row is bold");
	}
	// decoration role (section 8.6). The delegate does not decide what an icon
	// becomes: it hands the pixmap to QPainter, and CellPaintEngine::drawPixmap
	// is already the funnel -- two cells or more in each direction is a
	// section 5.7 placement carrying real pixels, and anything smaller
	// substitutes a glyph. Both answers are asserted here because the delegate
	// is what makes an item view reach that funnel at all: CE_ItemViewItem
	// drops the icon.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QPixmap avatar(4 * cw, 2 * ch);
		avatar.fill(Qt::red);
		QStandardItemModel model;
		model.appendRow(new QStandardItem(QIcon(avatar), QStringLiteral("avatar")));
		QListView list;
		list.setModel(&model);
		list.setItemDelegate(new CellItemDelegate(&list));
		list.setFrameShape(QFrame::NoFrame);
		list.setIconSize(QSize(4 * cw, 2 * ch));
		show(list, 24, 6);
		CellBuffer b(26, 7);
		QVector<CellImage> placements;
		render_once(list, b, &placements);
		CHECK(placements.size() == 1 && placements[0].cell_rect.size() == QSize(4, 2),
		      "a readable decoration becomes a 4x2 placement");
		const QPoint label = findText(b, QStringLiteral("avatar"));
		CHECK(!placements.isEmpty() && label.y() >= 0
		      && label.x() == placements[0].cell_rect.right() + 2,
		      "text starts one cell past the decoration");

		QPixmap dot(cw, ch);
		dot.fill(Qt::red);
		QStandardItemModel one;
		one.appendRow(new QStandardItem(QIcon(dot), QStringLiteral("dot")));
		QListView narrow;
		narrow.setModel(&one);
		narrow.setItemDelegate(new CellItemDelegate(&narrow));
		narrow.setFrameShape(QFrame::NoFrame);
		narrow.setIconSize(QSize(cw, ch));
		show(narrow, 24, 4);
		CellBuffer nb(26, 5);
		QVector<CellImage> none;
		render_once(narrow, nb, &none);
		CHECK(none.isEmpty() && buffer_contains(nb, QStringLiteral("▒")),
		      "a one-cell decoration substitutes a glyph, no placement");
	}
	// sizeHint. Neither "it is a cell multiple" nor "it is exactly the cells
	// a plain row occupies" is a check: GridStyle already snaps
	// CT_ItemViewItem, and measured against the delegate this replaces, a
	// plain five-character item comes back 60x19 from both. The first version
	// asserted exactly that and passed with the whole override taken out.
	//
	// What discriminates is the part of the row the proxied answer sizes
	// differently -- the indicator (four cells here against Fusion's three,
	// because "[x] " is what gets drawn) and the decoration with the gap after
	// it. Both were measured before being written down.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QStandardItemModel model(3, 1);
		model.setItem(0, 0, new QStandardItem(QStringLiteral("label")));
		auto *checkable = new QStandardItem(QStringLiteral("label"));
		checkable->setCheckable(true);
		model.setItem(1, 0, checkable);
		QPixmap avatar(4 * cw, 2 * ch);
		avatar.fill(Qt::red);
		model.setItem(2, 0, new QStandardItem(QIcon(avatar), QStringLiteral("label")));

		CellItemDelegate delegate;
		QStyleOptionViewItem option;
		option.decorationSize = QSize(4 * cw, 2 * ch);   // what a view would set
		const QSize plain = delegate.sizeHint(option, model.index(0, 0));
		const QSize with_check = delegate.sizeHint(option, model.index(1, 0));
		const QSize with_icon = delegate.sizeHint(option, model.index(2, 0));
		CHECK(with_check.width() - plain.width() == CellItemDelegate::check_cells() * cw,
		      "sizeHint reserves the indicator it draws");
		// indent + four cells of decoration + the gap + five cells of "label".
		CHECK(with_icon.width() == (CellItemDelegate::indent_cells() + 4 + 1 + 5) * cw
		      && with_icon.height() % ch == 0 && with_icon.height() >= 2 * ch,
		      "sizeHint reserves the decoration and the gap, in whole cells");
	}

	// AN MDI SUBWINDOW'S NAME, which had nowhere to go. A desktop gives it a
	// title bar of its own; here that row IS the top border, and the two were
	// fighting over it -- Qt sizes the bar at 24 pixels against a 19-pixel
	// row, and the frame is painted after the title bar and twice, so the
	// name was written and then covered. Two subwindows showed two identical
	// boxes and nothing to tell them apart.
	{
		QMdiArea mdi;
		mdi.setAttribute(Qt::WA_DontShowOnScreen);
		auto *sub = mdi.addSubWindow(new QTextEdit(QStringLiteral("body")));
		sub->setWindowTitle(QStringLiteral("Report.txt"));
		mdi.resize(GridMetrics::cells(40, 10));
		mdi.show();
		sub->resize(GridMetrics::cells(30, 7));
		QCoreApplication::processEvents();
		CellBuffer buf(40, 10);
		Qtty::render_once(mdi, buf);
		const QString top = buf.to_text().section(QLatin1Char('\n'), 0, 0);
		CHECK(top.contains(QStringLiteral("Report.txt")),
		      "an MDI subwindow wears its name on its top border, a terminal "
		      "having no row to spare for a title bar of its own");
		CHECK(top.startsWith(QStringLiteral("\u250c\u2500 ")),
		      "and the border is still a border either side of it, rather "
		      "than a line of text where a frame should be");

		// The title bar is one row, which is what lets the name share the
		// border rather than straddling two.
		CHECK(mdi.style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr, sub)
		          == GridMetrics::ch(),
		      "the title bar itself measures one row, Qt's own 24 pixels "
		      "against nineteen being what put it between two");
	}

	// A CALENDAR's month arrows, which Qt draws as pixmaps rather than by
	// setting arrowType -- so the arrow branch cannot see them -- and which
	// carry no text, no tool tip and no action: measured, every one of those
	// is empty. They came out as `[]` twice, a previous and a next nobody
	// could tell apart, in Qt's own QCalendarWidget.
	//
	// Named from the widget, which is the dock buttons' rule one widget
	// along: identity read from the object rather than from a picture.
	{
		QCalendarWidget cal;
		cal.setAttribute(Qt::WA_DontShowOnScreen);
		cal.setSelectedDate(QDate(2026, 9, 15));
		cal.resize(GridMetrics::cells(34, 12));
		cal.show();
		QCoreApplication::processEvents();
		CellBuffer buf(34, 12);
		Qtty::render_once(cal, buf);
		const QString top = buf.to_text().section(QLatin1Char('\n'), 0, 0);
		CHECK(top.contains(QStringLiteral("◂")) && top.contains(QStringLiteral("▸")),
		      "a calendar's month arrows say which way they go, Qt giving "
		      "them a pixmap and nothing else a terminal can read");
		CHECK(top.indexOf(QStringLiteral("◂")) < top.indexOf(QStringLiteral("▸")),
		      "and the previous one is on the left, which is the half a "
		      "single glyph for both would have hidden");
	}

	// A SMALL icon beside the text costs no second row, which is what a
	// terminal can afford to be strict about: the icon is drawn as a glyph in
	// the item's own row, so a row measured for 16 pixels of picture shows
	// nothing in its lower half.
	//
	// Measured in Qt's own QFileDialog before this: every file was followed
	// by a blank line, so a twenty-row terminal listed ten files. A
	// QTreeWidget item with an icon measured 38 pixels against a 19-pixel
	// row -- the base style wants the icon plus margins, and the snap to
	// whole rows then takes it to two.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		// THROUGH A REAL VIEW, because a bare option does not reach the
		// case: with no widget to ask, the base style answers one row for a
		// 16-pixel icon anyway, so an option-only check passes whatever this
		// style does -- the sabotage run said so, reverting the rule and
		// watching the check stay green. What changes is a view's own row,
		// which is the thing the terminal shows.
		QPixmap icon(16, 16);
		icon.fill(Qt::red);
		QTreeWidget tree;
		tree.setAttribute(Qt::WA_DontShowOnScreen);
		tree.setColumnCount(1);
		auto *plain_row = new QTreeWidgetItem(&tree, QStringList{QStringLiteral("plain")});
		auto *iconed = new QTreeWidgetItem(&tree, QStringList{QStringLiteral("iconed")});
		iconed->setIcon(0, QIcon(icon));
		tree.resize(GridMetrics::cells(30, 8));
		tree.show();
		QCoreApplication::processEvents();
		CHECK(tree.visualItemRect(iconed).height() == ch
		      && tree.visualItemRect(plain_row).height() == ch,
		      "an item whose icon sits beside its text is one row tall, the "
		      "icon being a glyph here rather than a picture needing room");

		QStyleOptionViewItem option;
		option.text = QStringLiteral("file");
		option.features = QStyleOptionViewItem::HasDecoration;
		option.decorationPosition = QStyleOptionViewItem::Left;

		// The first exception: an application asking for a decoration
		// TALLER than a row has asked for an avatar, and a style that
		// shrank it would be overruling a request rather than declining to
		// waste a row on a 16-pixel icon.
		option.decorationSize = QSize(4 * cw, 2 * ch);
		const QSize avatar = QApplication::style()->sizeFromContents(
		    QStyle::CT_ItemViewItem, &option, QSize(cw, ch), nullptr);
		CHECK(avatar.height() >= 2 * ch,
		      "while a decoration taller than a row keeps its rows, an "
		      "application that asked for an avatar having asked for it");

		// The second exception is ICON MODE, and it is asserted through a
		// real view rather than through a bare option: with no widget to ask,
		// the base style answers one row for a top decoration as readily as
		// for a side one, so an option-only check would pass whatever this
		// style did. A QListView in IconMode puts the picture above the
		// caption, and one row would leave the caption nowhere.
		QPixmap dot(16, 16);
		dot.fill(Qt::blue);
		QListWidget icons;
		icons.setAttribute(Qt::WA_DontShowOnScreen);
		icons.setViewMode(QListView::IconMode);
		auto *tile = new QListWidgetItem(QIcon(dot), QStringLiteral("name"));
		icons.addItem(tile);
		icons.resize(GridMetrics::cells(20, 6));
		icons.show();
		QCoreApplication::processEvents();
		CHECK(icons.visualItemRect(tile).height() >= 2 * ch,
		      "and an icon-mode tile keeps room for the caption under its "
		      "picture");
	}

	// Qt::ForegroundRole and Qt::BackgroundRole, which reached nothing at all
	// -- and were deferred once as a design question, wrongly. The project had
	// already decided this somewhere else under a different name:
	// CellPaintEngine passes a colour no palette role explains through as the
	// application's own, which is why a QLabel given a red palette comes out
	// red. Measured, in one program: three answers to one question -- the
	// label red, the same red on a model row nothing, and the same row with no
	// delegate installed nothing again.
	{
		QStandardItemModel m;
		auto *red = new QStandardItem(QStringLiteral("red row"));
		red->setForeground(QBrush(QColor(255, 0, 0)));
		auto *blue = new QStandardItem(QStringLiteral("bg row"));
		blue->setBackground(QBrush(QColor(0, 0, 255)));
		m.appendRow(red);
		m.appendRow(blue);
		m.appendRow(new QStandardItem(QStringLiteral("plain row")));

		auto render = [&](QListView &v, CellBuffer &b) {
			v.setModel(&m);
			v.setFrameShape(QFrame::NoFrame);
			show(v, 24, 4);
			render_once(v, b);
		};
		QListView with;
		with.setItemDelegate(new CellItemDelegate(&with));
		CellBuffer wb(26, 5);
		render(with, wb);

		const QPoint red_at = findText(wb, QStringLiteral("red row"));
		const QPoint plain_at = findText(wb, QStringLiteral("plain row"));
		CHECK(red_at.x() >= 0 && plain_at.x() >= 0
		      && wb.at(red_at.x(), red_at.y()).fg == Color::rgb(qRgb(255, 0, 0))
		      && wb.at(plain_at.x(), plain_at.y()).fg.kind() == Color::Default,
		      "Qt::ForegroundRole reaches the cells, and a plain row stays plain");

		// The whole row, not the cells the label happens to occupy. A
		// background role that coloured only the text would pass any check
		// taken at the label's position, and leave a stripe the width of the
		// word on the screen.
		const QPoint bg_at = findText(wb, QStringLiteral("bg row"));
		int coloured = 0;
		if (bg_at.y() >= 0)
			for (int x = 0; x < 24; ++x)
				if (wb.at(x, bg_at.y()).bg == Color::rgb(qRgb(0, 0, 255))) ++coloured;
		CHECK(coloured == 24, "Qt::BackgroundRole fills the row, not the label");

		// The same model with no delegate. GridStyle's own CE_ItemViewItem is
		// the other half of the same question, and it answered differently
		// until it was asked -- so this is the check that says one program
		// gives one answer.
		QListView without;
		CellBuffer ob(26, 5);
		render(without, ob);
		const QPoint bare_red = findText(ob, QStringLiteral("red row"));
		const QPoint bare_bg = findText(ob, QStringLiteral("bg row"));
		CHECK(bare_red.x() >= 0 && bare_bg.y() >= 0
		      && ob.at(bare_red.x(), bare_red.y()).fg
		             == wb.at(red_at.x(), red_at.y()).fg
		      && ob.at(0, bare_bg.y()).bg == Color::rgb(qRgb(0, 0, 255)),
		      "and the style's own path answers the same with no delegate");
	}

	// A header's default SECTION size, which is where an item view's rows and
	// columns come from. GridStyle overrode every other metric that shapes
	// geometry and not these two, so Fusion's answers stood: 30 px for a row
	// on a 19-px grid, and 100 px for a column.
	//
	// Both are asked at a SECOND cell size, and that is the check rather than
	// thoroughness. 100 divides exactly by this machine's 10-px cell, so
	// "the metric is a multiple of cw" is true of the BROKEN metric here and
	// discriminates nothing; the horizontal half was aligned by luck and
	// nothing on this machine could see it. A metric derived from the grid
	// moves when the grid moves, and a constant does not.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const auto metric = [](QStyle::PixelMetric m) {
			return QApplication::style()->pixelMetric(m);
		};
		const int v_here = metric(QStyle::PM_HeaderDefaultSectionSizeVertical);
		const int h_here = metric(QStyle::PM_HeaderDefaultSectionSizeHorizontal);
		GridMetrics::set(cw + 3, ch + 5);
		const int v_there = metric(QStyle::PM_HeaderDefaultSectionSizeVertical);
		const int h_there = metric(QStyle::PM_HeaderDefaultSectionSizeHorizontal);
		GridMetrics::set(cw, ch);
		printf("info: header sections are %dx%d px on a %dx%d cell, %dx%d on a %dx%d one\n",
		       h_here, v_here, cw, ch, h_there, v_there, cw + 3, ch + 5);
		CHECK(v_here == ch && v_there == ch + 5,
		      "a header section defaults to exactly one row, at either cell size");
		CHECK(h_here > 0 && h_here % cw == 0 && h_there > 0 && h_there % (cw + 3) == 0,
		      "and to a whole number of columns, at either cell size");
	}

	// What that metric costs on the screen. Nothing here is stated in cells:
	// the default section size is the subject, so setting one -- which every
	// other table in this file does -- would remove it.
	//
	// Measured before the fix, with the vertical header hidden: rows at pixel
	// 0, 30, 60 and 90 landed on buffer rows 1, 3, 4 and 6, a blank line
	// between the first pair and none between the second, and the selected
	// row reversed two buffer rows rather than one.
	{
		QStandardItemModel m(4, 1);
		for (int r = 0; r < 4; ++r)
			m.setItem(r, 0, new QStandardItem(QStringLiteral("row%1").arg(r)));
		m.setHorizontalHeaderLabels({QStringLiteral("Name")});
		QTableView t;
		t.setModel(&m);
		t.setFrameShape(QFrame::NoFrame);
		t.verticalHeader()->hide();
		show(t, 20, 7);
		t.selectRow(1);
		QCoreApplication::processEvents();
		CellBuffer b(22, 8);
		render_once(t, b);
		int y[4];
		for (int r = 0; r < 4; ++r)
			y[r] = findText(b, QStringLiteral("row%1").arg(r)).y();
		printf("info: four default rows land on buffer rows %d %d %d %d\n",
		       y[0], y[1], y[2], y[3]);
		CHECK(y[0] >= 0 && y[1] == y[0] + 1 && y[2] == y[0] + 2 && y[3] == y[0] + 3,
		      "a table's rows land one per buffer row, with none skipped");
		// The pair, and it is not the same assertion twice. A view that
		// reversed nothing at all would satisfy "the highlight does not reach
		// the row below" on its own, which is what a check for the absence
		// alone would be asking.
		int reversed = 0;
		for (int r = 0; r < b.rows(); ++r)
			if (b.at(0, r).attrs & Attr::Reverse) ++reversed;
		printf("info: a selected row reverses %d buffer row(s)\n", reversed);
		CHECK(reversed == 1 && y[1] >= 0 && (b.at(0, y[1]).attrs & Attr::Reverse),
		      "and a selected row's highlight is its own row and no other");
		GridGuard::reset();
	}

	// A heading over its own column. CE_ItemViewItem indents an item's text by
	// a cell and CE_HeaderLabel did not, so measured on a two-column table
	// "Name" began at column 0 with "r0" at column 1 -- every heading one cell
	// left of the data it names, which is the one thing a heading is for.
	{
		QStandardItemModel m(1, 2);
		m.setItem(0, 0, new QStandardItem(QStringLiteral("aaa")));
		m.setItem(0, 1, new QStandardItem(QStringLiteral("bbb")));
		m.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Value")});
		QTableView t;
		t.setModel(&m);
		t.setFrameShape(QFrame::NoFrame);
		t.verticalHeader()->hide();
		show(t, 30, 5);
		CellBuffer b(32, 6);
		render_once(t, b);
		const QPoint first = findText(b, QStringLiteral("Name"));
		const QPoint under_first = findText(b, QStringLiteral("aaa"));
		const QPoint second = findText(b, QStringLiteral("Value"));
		const QPoint under_second = findText(b, QStringLiteral("bbb"));
		printf("info: headings at columns %d and %d over data at %d and %d\n",
		       first.x(), second.x(), under_first.x(), under_second.x());
		CHECK(first.x() >= 0 && under_first.x() >= 0 && first.x() == under_first.x(),
		      "a heading starts in the same column as the data under it");
		// The second column as well, because the first alone is satisfied by a
		// header offset applied once to the whole strip rather than to each
		// section -- and because two columns is what makes it a table.
		CHECK(second.x() > first.x() && under_second.x() >= 0
		      && second.x() == under_second.x(),
		      "and so does the next column's heading, at its own offset");
		GridGuard::reset();
	}

	// The CURRENT item, which was drawn nowhere at all: measured with a full
	// to_snapshot() so a colour-only difference could not hide, moving it
	// through a three-item list changed ZERO cells. State_HasFocus is never
	// set here (project.md F4) -- measured on every item of both frames -- so
	// the mark is the router-owned focus and the view's own currentIndex,
	// and it is an underline because reverse already means selected.
	//
	// NoSelection throughout, deliberately. It is the mode where the current
	// item is the ONLY thing an arrow key changes, and it keeps the
	// selection's reverse out of a picture that is about a different mark.
	{
		QStandardItemModel m;
		for (int i = 0; i < 3; ++i)
			m.appendRow(new QStandardItem(QStringLiteral("item%1").arg(i)));
		// Both paths, because they write into the same cells: the style's own
		// CE_ItemViewItem fills the item, and CellItemDelegate writes the
		// label over the middle of that fill.
		const auto render = [&](bool with_delegate, int current, bool focused,
		                        CellBuffer &b) {
			QListView v;
			v.setModel(&m);
			v.setFrameShape(QFrame::NoFrame);
			v.setSelectionMode(QAbstractItemView::NoSelection);
			if (with_delegate) v.setItemDelegate(new CellItemDelegate(&v));
			show(v, 20, 5);
			v.setCurrentIndex(m.index(current, 0));
			QCoreApplication::processEvents();
			Qtty::set_focus_widget(focused ? &v : nullptr);
			render_once(v, b);
			Qtty::set_focus_widget(nullptr);
		};
		CellBuffer at_first(22, 6), at_second(22, 6), keyless(22, 6), through_delegate(22, 6);
		render(false, 0, true, at_first);
		render(false, 1, true, at_second);
		render(false, 1, false, keyless);
		render(true, 1, true, through_delegate);

		const int y0 = findText(at_first, QStringLiteral("item0")).y();
		const int y1 = findText(at_first, QStringLiteral("item1")).y();
		const auto marked = [](const CellBuffer &b, int row) {
			int n = 0;
			if (row < 0) return -1;
			for (int x = 0; x < b.cols(); ++x)
				if (b.at(x, row).attrs & Attr::Underline) ++n;
			return n;
		};
		printf("info: the current item marks %d cells of its own row and %d of its neighbour\n",
		       marked(at_second, y1), marked(at_second, y0));
		CHECK(y0 >= 0 && y1 == y0 + 1
		      && marked(at_first, y0) > 0 && marked(at_first, y1) == 0
		      && marked(at_second, y1) > 0 && marked(at_second, y0) == 0,
		      "the current item is marked, and the mark moves with it");
		// Drawing nothing satisfies half of that by itself, so the frames have
		// to differ AND the row that moved has to still be on the screen.
		CHECK(at_second.diff_cells(at_first) > 0
		      && findText(at_second, QStringLiteral("item1")).y() == y1,
		      "moving the current item changes cells, and the item is still drawn");
		// The other direction. A view without the keys marks nothing -- and
		// still draws its items, which is what says the absence is a decision
		// rather than an empty frame.
		CHECK(marked(keyless, y0) == 0 && marked(keyless, y1) == 0
		      && findText(keyless, QStringLiteral("item1")).y() == y1,
		      "a view that does not own the keys marks no item, and still draws them");
		// The two paths agreeing, cell for cell. A mark on the padding and not
		// on the word is exactly the shape the disabled-item fault had, and it
		// is what a check taken at the label alone cannot see.
		const QPoint label = findText(through_delegate, QStringLiteral("item1"));
		CHECK(label.x() > 0
		      && (through_delegate.at(label.x(), label.y()).attrs & Attr::Underline)
		      && (through_delegate.at(label.x() - 1, label.y()).attrs & Attr::Underline),
		      "and the delegate's label carries the mark the style's fill does");
		GridGuard::reset();
	}

	// ---- Qt's alternating-row switch, which reached no cell (8.261) ------
	//
	// setAlternatingRowColors(true) is the standard way to ask an item view
	// for banded rows, and it produced nothing here. The band is
	// QStyleOptionViewItem::Alternate, a FEATURE the view sets on every other
	// row, and QCommonStyle reads it in PE_PanelItemViewRow -- GridStyle
	// answers both that primitive and CE_ItemViewItem itself and neither
	// looked at it, so all four rows came out the ordinary ground. Measured
	// again before this section was written, on all three view classes, with
	// and without CellItemDelegate.
	//
	// WHICH ELEMENT EACH CLASS REACHES, measured with a tracing style over
	// GridStyle rather than read off Qt's source: QListView, QTableView and
	// QTreeView each draw PE_PanelItemViewRow themselves, before any delegate
	// runs, and each then reaches CE_ItemViewItem -- via the default
	// delegate, and via CellItemDelegate, which hands the frame back to the
	// style. So the split is not per view class. It is per DELEGATE: an
	// application's own painter never reaches CE_ItemViewItem, and the row
	// panel is the only site it passes through. Both are written, from one
	// rule, and OwnPaintDelegate above is the fixture that tells them apart.
	{
		const CellTheme saved_theme = theme();
		set_theme(CellTheme::from_palette(QGuiApplication::palette()));
		const Color base_ground = theme().background(QPalette::Base);
		const Color band_ground = theme().background(QPalette::AlternateBase);
		printf("info: under from_palette() Base is #%06x/%d and AlternateBase"
		       " #%06x/%d\n",
		       base_ground.value() & 0xffffffu, base_ground.authored_ansi16(),
		       band_ground.value() & 0xffffffu, band_ground.authored_ansi16());
		// THE PARTITION, before a cell is read. background(AlternateBase)
		// answers the window ground wherever the palette separates Window
		// from Base and the palette's own alternate ground where it does not
		// (8.256) -- so on a palette that named one colour for all three,
		// every check below would agree with itself and discriminate
		// nothing.
		CHECK(base_ground.kind() != Color::Default
		      && band_ground.kind() != Color::Default
		      && base_ground != band_ground,
		      "the theme this section renders through names an alternate "
		      "ground and an ordinary one, and they differ");

		QStandardItemModel model(4, 1);
		for (int i = 0; i < 4; ++i)
			model.setItem(i, 0, new QStandardItem(QStringLiteral("row%1").arg(i)));

		const auto render_view = [&](QAbstractItemView &v, bool alternate,
		                             CellBuffer &into) {
			v.setModel(&model);
			v.setAlternatingRowColors(alternate);
			v.setFrameShape(QFrame::NoFrame);
			if (auto *t = qobject_cast<QTableView *>(&v)) {
				t->horizontalHeader()->hide();
				t->verticalHeader()->hide();
			}
			if (auto *t = qobject_cast<QTreeView *>(&v)) {
				t->setHeaderHidden(true);
				t->setRootIsDecorated(false);
			}
			show(v, into.cols(), into.rows());
			render_once(v, into);
		};
		// The RELATIONSHIP the switch exists to produce: a row differs from
		// its neighbour and agrees with the row two away. A literal would say
		// which shade row 1 is, and what is wrong when this fails is not the
		// shade -- it is that row 1 is the same cell as row 0.
		const auto banded = [](const CellBuffer &b) {
			return b.at(0, 0).bg != b.at(0, 1).bg
			    && b.at(0, 2).bg != b.at(0, 3).bg
			    && b.at(0, 0).bg == b.at(0, 2).bg
			    && b.at(0, 1).bg == b.at(0, 3).bg;
		};

		CellBuffer list_b(12, 4);
		{ QListView v; render_view(v, true, list_b); }
		CHECK(banded(list_b),
		      "setAlternatingRowColors() bands a QListView: a row differs "
		      "from its neighbour and agrees with the row two away");
		// The whole row, not the cells the label occupies. A band drawn only
		// under the text is a stripe the width of the word, which is exactly
		// the fault Qt::BackgroundRole had before it filled.
		int across = 0;
		for (int x = 0; x < list_b.cols(); ++x)
			if (list_b.at(x, 1).bg == band_ground) ++across;
		CHECK(across == list_b.cols(),
		      "and the band is the whole row rather than a stripe the width "
		      "of the label");

		// The other two classes, because one fix that leaves the others
		// blind is the thing this section was written against.
		CellBuffer tree_b(12, 4), table_b(12, 4);
		{ QTreeView v;  render_view(v, true, tree_b); }
		{ QTableView v; render_view(v, true, table_b); }
		CHECK(banded(tree_b) && banded(table_b),
		      "and a QTreeView and a QTableView band the same way");

		// CellItemDelegate, which hands the frame back to CE_ItemViewItem and
		// then writes its label over it. The label must not punch the band
		// out from under itself, which is what a text write carrying its own
		// ground would do.
		CellBuffer delegated(12, 4);
		{
			QListView v;
			v.setItemDelegate(new CellItemDelegate(&v));
			render_view(v, true, delegated);
		}
		const QPoint label = findText(delegated, QStringLiteral("row1"));
		CHECK(banded(delegated) && label.y() == 1
		      && delegated.at(label.x(), 1).bg == band_ground,
		      "CellItemDelegate keeps the band, under its label as well as "
		      "beside it");

		// THE OTHER PATH. OwnPaintDelegate reaches the style at no point, so
		// CE_ItemViewItem never runs and the row panel the view draws is the
		// only thing that can have banded this. Without it the second site
		// would be untested and an application's own painter would get no
		// band at all.
		CellBuffer own_b(12, 4);
		{
			QListView v;
			v.setItemDelegate(new OwnPaintDelegate(&v));
			render_view(v, true, own_b);
		}
		CHECK(banded(own_b),
		      "and a delegate that never calls the style is banded too, by "
		      "the row panel the view draws itself");

		// THE CONTROL. Without it a fix that bands unconditionally passes
		// every check above.
		CellBuffer off_b(12, 4);
		{ QListView v; render_view(v, false, off_b); }
		bool all_base = true;
		for (int y = 0; y < off_b.rows(); ++y)
			for (int x = 0; x < off_b.cols(); ++x)
				if (off_b.at(x, y).bg != base_ground) all_base = false;
		CHECK(all_base,
		      "with alternating row colours off every row is the ordinary "
		      "ground, so the band is the switch and not the view");

		// PRECEDENCE, and it is the half that makes a banded view usable. A
		// selection here is Attr::Reverse rather than a colour, so banding a
		// selected row would reverse the BAND -- the odd rows of a selection
		// coming out a different colour from the even ones. Selection wins,
		// which is QCommonStyle's precedence too, and the assertion is that
		// a selected odd row is the SAME CELL as a selected even one.
		const auto select = [&](int row, CellBuffer &into) {
			QListView v;
			render_view(v, true, into);
			v.setCurrentIndex(model.index(row, 0));
			QCoreApplication::processEvents();
			render_once(v, into);
		};
		CellBuffer sel0(12, 4), sel1(12, 4);
		select(0, sel0);
		select(1, sel1);
		CHECK(sel0.at(0, 0).bg == sel1.at(0, 1).bg
		      && (sel0.at(0, 0).attrs & Attr::Reverse)
		      && (sel1.at(0, 1).attrs & Attr::Reverse),
		      "a selected row is the same cell on an odd row as on an even "
		      "one: the selection wins over the band, so a reverse never "
		      "lands on it");
		// And the rows around it are still banded, or the line above would
		// be satisfied by a fix that simply stopped banding.
		CHECK(sel1.at(0, 3).bg == band_ground && sel1.at(0, 2).bg == base_ground,
		      "and the rows the selection did not touch are banded still");

		// The CURRENT item keeps its band, because its mark is an attribute
		// and an underline composes with a ground rather than replacing it.
		// This is the case CE_ItemViewItem's own fill would have eaten: that
		// fill REPLACES a cell whenever the item carries any attribute at
		// all, so without the band reaching it the current row would be a
		// hole in the stripe.
		CellBuffer current_b(12, 4);
		{
			QListView v;
			v.setSelectionMode(QAbstractItemView::NoSelection);
			render_view(v, true, current_b);
			v.setCurrentIndex(model.index(1, 0));
			QCoreApplication::processEvents();
			Qtty::set_focus_widget(&v);
			render_once(v, current_b);
			Qtty::set_focus_widget(nullptr);
		}
		CHECK(current_b.at(0, 1).bg == band_ground
		      && (current_b.at(0, 1).attrs & Attr::Underline)
		      && !(current_b.at(0, 1).attrs & Attr::Reverse),
		      "the current item keeps its band and is underlined over it");

		// A colour the MODEL named wins over the band. The palette's
		// alternate ground is a default and the model's colour is a choice,
		// and reversing the two would make an application's own row colour
		// disappear on every other row.
		CellBuffer chosen(12, 4);
		{
			model.item(1, 0)->setBackground(QColor(0, 0, 255));
			QListView v;
			render_view(v, true, chosen);
			model.item(1, 0)->setBackground(QBrush());
		}
		CHECK(chosen.at(0, 1).bg == Color::rgb(qRgb(0, 0, 255))
		      && chosen.at(0, 3).bg == band_ground,
		      "a background the model named wins over the band, and the next "
		      "alternate row still carries it");

		// THE SIXTEEN-COLOUR TIER, which is what the role table was authored
		// for: AlternateBase is index 8, "the only index that reads as
		// slightly off the ground rather than as a second foreground", and
		// this is the first time a real fill has carried it to a cell. Body
		// text is 7, and 7 on 8 clears the section 6 contrast minimum -- the
		// comment beside index 7 says it clears "0 and 4, which are the only
		// two backgrounds this table produces", and 8 is now a third.
		printf("info: at Ansi16 the rows read %d and %d, with %d contrast"
		       " violations\n",
		       list_b.at(0, 0).bg.to_ansi16(), list_b.at(0, 1).bg.to_ansi16(),
		       contrast_violations(list_b, Capabilities::Ansi16));
		CHECK(list_b.at(0, 0).bg.to_ansi16() == 0
		      && list_b.at(0, 1).bg.to_ansi16() == 8
		      && contrast_violations(list_b, Capabilities::Ansi16) == 0,
		      "at sixteen colours the band is the authored 8 against the "
		      "ground's 0, and every glyph on it still clears the contrast "
		      "minimum");

		// THE LIMIT, pinned so that nobody reads the checks above as saying
		// more than they do. terminal_default() names no colour for any
		// surface role, so it names none for this one either and a banded row
		// is the terminal's own ground -- exactly as a selected row is the
		// terminal's own ground plus a reverse. The band is a colour, and the
		// default theme's whole contract is that it chooses none.
		set_theme(CellTheme::terminal_default());
		CellBuffer bare(12, 4);
		{ QListView v; render_view(v, true, bare); }
		CHECK(bare.at(0, 0).bg.kind() == Color::Default
		      && bare.at(0, 1).bg.kind() == Color::Default,
		      "under the default theme a banded row is still the terminal's "
		      "own ground, because that theme names no colour to band with");

		set_theme(saved_theme);
		GridGuard::reset();
	}


	// A table's grid must not eat its labels' own spaces. Qt draws the grid
	// itself, after the items, with QPainter::drawLine, and
	// CellPaintEngine::line() wrote a rule into any cell whose glyph was a
	// space -- which a label's own spaces are. So "a label far wider than its
	// column" rendered with a rule in place of every gap between its words.
	//
	// A rule that meets content is not drawn at all now, which is this tree's
	// answer for chrome a cell grid cannot represent. Asserted as a
	// DIFFERENCE: the label's cells must be identical with the grid on and
	// off, which is what says the grid changed nothing about the text. A
	// check on the text alone would pass against a table that failed to
	// render its label at all.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QStandardItemModel m(2, 2);
		const QString wide = QStringLiteral("a label far wider than its column");
		m.setItem(0, 0, new QStandardItem(wide));
		m.setItem(0, 1, new QStandardItem(QStringLiteral("two words")));
		m.setItem(1, 0, new QStandardItem(QStringLiteral("mid")));
		m.setItem(1, 1, new QStandardItem(QStringLiteral("x")));

		auto render = [&](bool grid, CellBuffer &b) {
			auto *v = new QTableView;
			v->setModel(&m);
			v->setShowGrid(grid);
			v->setItemDelegate(new CellItemDelegate(v));
			v->horizontalHeader()->setFixedHeight(ch);
			v->horizontalHeader()->setDefaultSectionSize(14 * cw);
			v->verticalHeader()->setFixedWidth(2 * cw);
			v->verticalHeader()->setDefaultSectionSize(ch);
			v->setFrameShape(QFrame::NoFrame);
			show(*v, 32, 4);
			render_once(*v, b);
			delete v;
		};
		CellBuffer on(34, 5), off(34, 5);
		render(true, on);
		render(false, off);

		const QPoint at = findText(off, QStringLiteral("two words"));
		bool same = at.x() >= 0;
		for (int x = 0; same && x < 28; ++x)
			same = on.at(x, at.y()).ch == off.at(x, at.y()).ch;
		CHECK(same, "a table's grid leaves its labels' own spaces alone");
	}


	// The property that matters is the AGREEMENT, and it needs both selections
	// in one frame: a text selection and an item view's must be the same
	// thing, because they are the same thing to whoever is looking at the
	// screen. Checked separately, either side could drift and both checks
	// would stay green.
	{
		QWidget host;
		auto *v = new QVBoxLayout(&host);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *le = new QLineEdit(QStringLiteral("hello world"), &host);
		le->setFrame(false);
		le->setFixedHeight(GridMetrics::ch());
		v->addWidget(le);
		auto *list = new QListWidget(&host);
		list->addItem(QStringLiteral("row one"));
		list->setItemDelegate(new CellItemDelegate(list));
		list->setFrameShape(QFrame::NoFrame);
		list->setFixedHeight(GridMetrics::ch());
		v->addWidget(list);
		show(host, 24, 2);
		le->setSelection(0, 5);
		list->setCurrentRow(0);
		QCoreApplication::processEvents();
		CellBuffer b(26, 3);
		render_once(host, b);
		const QPoint text_sel = findText(b, QStringLiteral("hello"));
		const QPoint item_sel = findText(b, QStringLiteral("row one"));
		const bool same = text_sel.x() >= 0 && item_sel.x() >= 0
		    && b.at(text_sel.x(), text_sel.y()).attrs
		           == b.at(item_sel.x(), item_sel.y()).attrs
		    && b.at(text_sel.x(), text_sel.y()).bg
		           == b.at(item_sel.x(), item_sel.y()).bg;
		CHECK(same && (b.at(text_sel.x(), text_sel.y()).attrs & Attr::Reverse),
		      "a text selection and an item view's are the same thing");
	}


	// Qt's standard iconography, and what a glyph table would and would not
	// reach. Recorded as two results because it was recorded as one cause and
	// is not: the message box severity icon and a dock widget's title buttons
	// fail for different reasons.
	//
	// GridStyle::standardIcon() IS consulted for both -- measured, 28 calls
	// across the suite -- and returning an icon that paints a glyph changes
	// neither, because Qt rasterises an icon to a QPixmap before the style
	// draws it. The severity icon arrives at drawItemPixmap() as 48x48 pixels
	// with no identity left; the dock buttons arrive as a 0x0 pixmap, which is
	// no icon area at all and no iconography decision can fill it.
	{
		QMessageBox mb;
		mb.setIcon(QMessageBox::Warning);
		mb.setText(QStringLiteral("The file has been modified."));
		mb.setStandardButtons(QMessageBox::Ok);
		mb.setAttribute(Qt::WA_DontShowOnScreen);
		mb.resize(GridMetrics::cells(40, 6));
		mb.show();
		QCoreApplication::processEvents();
		CellBuffer b(42, 7);
		QVector<CellImage> placements;
		render_once(mb, b, &placements);
		CHECK(placements.size() == 1 && placements[0].cell_rect.width() >= 2
		      && placements[0].cell_rect.height() >= 2,
		      "a message box's severity icon is a picture, not a glyph");
		// And that the picture costs a whole number of rows. PM_MessageBoxIconSize
		// was the one metric in GridStyle's switch that still answered Fusion's
		// pixels -- 48, which is 2.53 rows -- so the dialog asked for 3.5 cells
		// and the half was unusable. Two rows exactly keeps it a picture and
		// makes the dialog 3.0.
		CHECK(mb.sizeHint().height() % GridMetrics::ch() == 0,
		      "and the dialog it sits in is a whole number of rows tall");

		// And WHERE a key registry would have to mint, which is the part the
		// recorded option got wrong. standardIcon() is not the mint point: one
		// QIcon caches its own pixmap, so asking it twice gives one identity --
		// but a SECOND standardIcon() call for the same value re-renders, and
		// the identity the message box actually placed is neither of them.
		// Measured, and printed as three keys that do not meet.
		//
		// So a cacheKey-to-glyph map minted at standardIcon() would resolve
		// nothing, and the mint point has to be inside the icon -- a
		// QIconEngine that registers each pixmap it returns, because that is
		// the object QIcon asks once per size, mode and state and then caches.
		//
		// The relationships are the assertion, not the numbers. This goes red
		// the day Qt gives standard icons a stable identity, and that day is
		// exactly when this decision is worth re-opening.
		QStyle *st = mb.style();
		const QIcon i1 = st->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, &mb);
		const QIcon i2 = st->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, &mb);
		const quint64 k1 = quint64(i1.pixmap(48, 48).cacheKey());
		const quint64 k1_again = quint64(i1.pixmap(48, 48).cacheKey());
		const quint64 k2 = quint64(i2.pixmap(48, 48).cacheKey());
		const quint64 placed = placements.isEmpty() ? 0 : placements[0].key;
		CHECK(k1 == k1_again && k2 != k1 && placed != k1 && placed != k2,
		      "a standard icon's identity does not survive standardIcon()");

		// The message box is put away before the rest of this block runs.
		// It is a MODAL, so while it is up QApplication::activeModalWidget()
		// names it -- and that is what InputRouter::input_scope() reads, so a
		// routing check placed after this would deliver into a dialog nobody
		// in its own block had opened. Nothing here does today, and the
		// checks below still pass with it hidden, which is why this is a fix
		// rather than a change.
		//
		// The rule this file has now been bitten by twice: a fixture that
		// shows a MODAL owns it for the whole block unless it takes it down,
		// because a modal is global state with a router reading it.
		mb.hide();
		QCoreApplication::processEvents();
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *dock = new QDockWidget(QStringLiteral("Panel"), &win);
		dock->setWidget(new QLabel(QStringLiteral("body")));
		win.addDockWidget(Qt::LeftDockWidgetArea, dock);
		win.resize(GridMetrics::cells(30, 8));
		win.show();
		QCoreApplication::processEvents();
		CellBuffer db(32, 9);
		QVector<CellImage> dp;
		render_once(win, db, &dp);
		// This used to assert "[][]", and it was right to: two identical empty
		// brackets were what a close and a float rendered as, and the note here
		// said the check would go red the day somebody fixed its half. It did.
		//
		// The fix was not iconography. Both buttons carry Qt's own object
		// names, so the style can say which is which without any icon
		// identity at all -- read from the widget, exactly as the arrowType
		// case is. What was actually missing was room: Qt sizes these in
		// pixels at not quite two cells each, and two cells hold "[]" and
		// nothing else, so the whole budget went on chrome.
		const QString title = db.to_text();
		const int close = title.indexOf(QStringLiteral("✕"));
		const int flt   = title.indexOf(QStringLiteral("↗"));
		CHECK(close >= 0 && flt >= 0 && close != flt && dp.isEmpty(),
		      "a dock widget's close and float buttons are told apart");
		// And the rule that made room for them, which is the horizontal form
		// of one the rendering side already states: chrome goes where chrome
		// fits. Below three cells the brackets are dropped and the content
		// keeps the cells -- so no empty pair survives anywhere in the frame.
		CHECK(!title.contains(QStringLiteral("[]")),
		      "a tool button too narrow to bracket spends its cells on content");
		// A QMainWindow's dock layout puts three widgets off the grid, and
		// they are Qt's own rather than this suite's to place -- the same
		// category section 7.8 exempts by principle. Reset so the count this
		// block leaves behind is not attributed to whatever runs next.
		GridGuard::reset();
	}


	// A one-row QLineEdit is bracketed, like the combo box and the spin box.
	// The principle was already stated where those two were decided: the
	// control has to be visibly a control, and at one row a frame cannot say
	// so -- draw_box() needs two rows and silently draws nothing below that.
	//
	// The cost this was deferred over was an artefact of the first attempt.
	// Bracketing every PE_PanelLineEdit gave an editable combo and a spin box
	// a SECOND closing bracket inside their own, because each contains a
	// QLineEdit reaching the same primitive. QLineEdit::hasFrame() separates
	// them with no class list and no parent test: a plain edit answers true,
	// and the two composite controls answer false because they draw the
	// boundary themselves. An application that calls setFrame(false) answers
	// false too, and is obeyed.
	{
		const int ch = GridMetrics::ch();
		QWidget d;
		auto *v = new QVBoxLayout(&d);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *le = new QLineEdit(QStringLiteral("plain edit"), &d);
		le->setFixedHeight(ch);
		v->addWidget(le);
		auto *ec = new QComboBox(&d);
		ec->setEditable(true);
		ec->addItem(QStringLiteral("editable combo"));
		ec->setFixedHeight(ch);
		v->addWidget(ec);
		auto *sb = new QSpinBox(&d);
		sb->setValue(42);
		sb->setFixedHeight(ch);
		v->addWidget(sb);
		auto *nf = new QLineEdit(QStringLiteral("no frame"), &d);
		nf->setFrame(false);
		nf->setFixedHeight(ch);
		v->addWidget(nf);
		show(d, 30, 4);
		CellBuffer b(32, 5);
		render_once(d, b);
		auto row = [&](int y) {
			return b.to_text().section(QLatin1Char('\n'), y, y).trimmed();
		};
		CHECK(row(0).startsWith(QLatin1Char('[')) && row(0).endsWith(QLatin1Char(']')),
		      "a one-row line edit is bracketed, as the combo and spin box are");
		// One closing bracket, not two. Counting is what discriminates: a
		// check that the row merely ENDS in ']' passes against the double.
		CHECK(row(1).count(QLatin1Char(']')) == 1
		      && row(2).count(QLatin1Char(']')) == 1,
		      "and a combo's and spin box's own edit does not add a second");
		// Honest about what this one can prove: it cannot fail against this
		// code. Qt skips PE_PanelLineEdit entirely for an unframed edit, so
		// the primitive never runs and the gate is never asked -- confirmed
		// by sabotage, which reddened the check above and left this one
		// green. It stays because the PROPERTY is worth holding: an
		// application that asks for no frame must not be given one, whichever
		// layer keeps that promise.
		CHECK(!row(3).contains(QLatin1Char('[')),
		      "and setFrame(false) is obeyed, so a bare field stays bare");

		// Where a one-row field puts its TEXT, which is a second consumer of
		// PM_DefaultFrameWidth: QLineEdit hands that number to lineWidth and
		// QCommonStyle insets by it on all four sides, so a one-row field's
		// contents rect is a column wide and 19 - 2 * 10 = -1 pixels tall.
		// QRect calls that invalid, and Qt hands it to setClipRect() and to
		// the vertical-alignment arithmetic without ever asking.
		//
		// Centred text survives it by cancellation -- a symmetric inset drops
		// out of the centring formula exactly -- which is why nothing above
		// this ever noticed. AlignTop and AlignBottom read the rect's own y
		// and height instead, so they are where the invalid rect is
		// expressible. The centred case is the CONTROL: it must keep passing,
		// or a blank field would only prove the fixture was broken.
		{
			const auto text_row = [&](Qt::Alignment va) {
				QLineEdit f(QStringLiteral("abcdef"));
				f.setAlignment(Qt::AlignLeft | va);
				f.setFixedHeight(ch);
				show(f, 14, 1);
				CellBuffer fb(16, 2);
				render_once(f, fb);
				return findText(fb, QStringLiteral("abcdef")).y();
			};
			const int mid = text_row(Qt::AlignVCenter);
			const int top = text_row(Qt::AlignTop);
			const int bot = text_row(Qt::AlignBottom);
			printf("info: a one-row field draws centred text on row %d, top on"
			       " %d, bottom on %d\n", mid, top, bot);
			CHECK(mid == 0, "a one-row field draws centred text on its own row");
			CHECK(top == 0 && bot == 0,
			      "and draws top- and bottom-aligned text there too");
		}

		// And what the fix above uncovered. Qt draws a selected field's text
		// TWICE at the same origin -- once clipped to the selection, once for
		// the rest -- and the engine's wide-cluster guard read the second as a
		// run CONSECUTIVE with the first, pushing it right by the first's
		// width. A field holding "hi" with everything selected rendered
		// "hihi".
		//
		// It could not be reached while the contents rect was invalid: the
		// empty clip dropped Qt's selected run entirely, so the two defects
		// held each other up and correcting either alone shows the other. The
		// check is on the GLYPHS, because that is what was wrong -- a
		// selection is an attribute and must not add characters.
		{
			QLineEdit f(QStringLiteral("hi"));
			f.setFixedHeight(ch);
			show(f, 14, 1);
			f.selectAll();
			CellBuffer sb(16, 2);
			render_once(f, sb);
			const QString got =
			    sb.to_text().section(QLatin1Char('\n'), 0, 0).trimmed();
			printf("info: a fully selected field renders [%s]\n",
			       qPrintable(got));
			CHECK(got.count(QStringLiteral("hi")) == 1,
			      "selecting a field's text does not draw it twice");
		}

		// The sibling question in the same entry, answered by the same rule:
		// an empty field was invisible until tabbed through, and a form of
		// them was a blank screen. It has a boundary now.
		QLineEdit empty;
		empty.setFixedHeight(ch);
		show(empty, 20, 1);
		CellBuffer eb(22, 2);
		render_once(empty, eb);
		const QString e = eb.to_text().section(QLatin1Char('\n'), 0, 0).trimmed();
		CHECK(e.startsWith(QLatin1Char('[')) && e.endsWith(QLatin1Char(']')),
		      "an empty field shows its boundary rather than nothing");
	}


	// A tab pane carries no background colour. Fusion fills one with a
	// gradient, and the engine recovers a role by comparing the brush colour
	// against each role's for exact equality -- so the stop colour #fbfbfb,
	// which is no role, fell through to a true-colour background and put a
	// near-white block behind every tab page. On a dark terminal that is
	// exactly as bad as it sounds, and the gallery fixture carried ten rows
	// of it.
	//
	// GridStyle draws the pane as the frame it is now, so the base style
	// never runs and there is no fill to classify. The check is on the CELLS
	// rather than on the fixture, because a fixture says "this is what it
	// looks like" and this says why.
	{
		QWidget host;
		auto *tabs = new QTabWidget(&host);
		auto *page = new QWidget;
		auto *pv = new QVBoxLayout(page);
		pv->addWidget(new QLabel(QStringLiteral("body"), page));
		tabs->addTab(page, QStringLiteral("One"));
		tabs->setGeometry(0, 0, GridMetrics::cw() * 20, GridMetrics::ch() * 6);
		show(host, 22, 7);
		CellBuffer b(24, 8);
		render_once(host, b);
		int coloured = 0;
		for (int y = 0; y < b.rows(); ++y)
			for (int x = 0; x < b.cols(); ++x)
				if (b.at(x, y).bg.kind() != Color::Default) ++coloured;
		// Paired with the pane being drawn at all: "no colour" is also what an
		// empty frame produces, and the box is what says the pane is there.
		CHECK(coloured == 0 && buffer_contains(b, QStringLiteral("┌")),
		      "a tab pane is a frame, not a near-white block");
	}

	// gallery snapshot: one window with the whole tier
	{
		QWidget win;
		auto *v = new QVBoxLayout(&win);
		auto *combo = new QComboBox(&win);
		combo->addItems({"Alpha", "Beta"});
		v->addWidget(combo);
		auto *pb = new QProgressBar(&win);
		pb->setRange(0, 100); pb->setValue(40);
		// One cell tall, stated rather than left to the widget. GridStyle
		// answers CT_ProgressBar with exactly ch and the hint obeys it, but
		// QProgressBar's minimumSizeHint() does not go through the style at
		// all, and a layout honours the minimum over the hint. Measured at
		// ch = 19: fontMetrics().height() is 19, sizeHint() 110x19,
		// minimumSizeHint() 110x21 -- so the bar came out 21 tall, which moved
		// the slider to y = 59 and the tab widget to y = 78 and left the tabs
		// 207 tall. One widget two pixels over put three of them off the grid.
		// QSlider and QComboBox were measured on the same run and answer 19
		// for both, so this is the progress bar's alone.
		pb->setFixedHeight(GridMetrics::ch());
		v->addWidget(pb);
		auto *slider = new QSlider(Qt::Horizontal, &win);
		slider->setRange(0, 10); slider->setValue(3);
		v->addWidget(slider);
		auto *tabs = new QTabWidget(&win);
		auto *page = new QWidget;
		auto *pv = new QVBoxLayout(page);
		auto *chk = new QCheckBox("Enable", page);
		chk->setChecked(true);
		pv->addWidget(chk);
		tabs->addTab(page, "General");
		tabs->addTab(new QWidget, "Advanced");
		// The tab widget is the one item that stretches, and that is what
		// keeps the column on the grid rather than a trailing addStretch():
		// the window, the layout margins and every other item are cell
		// multiples, so whatever is left over for the single expanding item
		// is a cell multiple too. (suite_render's dialog needs the stretch
		// instead, because nothing in it expands.)
		v->addWidget(tabs, 1);
		show(win, 44, 16);
		const QString got = Qtty::test::snapshot_of(win, 46, 17);
		// The delta, not the running total. This read `fails` -- every
		// failure the suite had accumulated -- so the line naming the gallery
		// snapshot reported on whatever had gone wrong earlier in the file.
		// Found by sabotaging a delegate check twenty lines up and watching
		// the snapshot go red without the fixture differing by a cell: a
		// check that names one thing and answers about another, which is
		// worse than no check, because it sends the reader to the fixture.
		const int before = fails;
		fails += Qtty::test::check_snapshot(QStringLiteral(QTTY_SOURCE_DIR),
		                                   QStringLiteral("widgets_gallery"), got, g_record);
		if (!g_record) printf("%s: gallery snapshot\n", fails > before ? "FAIL" : "PASS");
	}

	// THE CARET, which no fixture could see before: the cells say what is
	// written and the compositor says where the terminal's cursor goes, so
	// a frame that lost its caret, or put it in the wrong cell, or showed
	// a block where it had shown a bar, compared equal to one that did not.
	//
	// ONE WINDOW ALIVE AT A TIME, which is the fixture and took finding.
	// snapshot_of_screen() composes the SCREEN: a second visible top level
	// puts a window tab strip in the frame and leaves the FIRST one
	// current, so the caret reported is that window's. Measured -- with a
	// button window still up, a focused field's frame carried both "hi"
	// and "Open" and reported the caret hidden, which reads as the section
	// being inert when it is the fixture holding two windows.
	{
		QString with_field, with_button;
		{
			QWidget w;
			w.resize(GridMetrics::cells(30, 4));
			auto *v = new QVBoxLayout(&w);
			auto *edit = new QLineEdit(QStringLiteral("hi"));
			v->addWidget(edit);
			w.show();
			InputRouter r2(&w);
			QCoreApplication::processEvents();
			edit->setFocus();
			QCoreApplication::processEvents();
			with_field = Qtty::test::snapshot_of_screen(w, r2, 30, 4);
		}
		QCoreApplication::processEvents();
		{
			QWidget w;
			w.resize(GridMetrics::cells(30, 4));
			auto *v = new QVBoxLayout(&w);
			auto *b = new QPushButton(QStringLiteral("Open"));
			v->addWidget(b);
			w.show();
			InputRouter r2(&w);
			QCoreApplication::processEvents();
			b->setFocus();
			QCoreApplication::processEvents();
			with_button = Qtty::test::snapshot_of_screen(w, r2, 30, 4);
		}
		QCoreApplication::processEvents();
		CHECK(with_field.contains(QStringLiteral("--- cursor ---")),
		      "a screen snapshot records where the caret is, which the "
		      "cells cannot say");
		// Asserted as the CLAIM rather than as a cell: a literal "4,1 bar"
		// was written here first and is a property of this fixture's
		// margins, not of the feature.
		const QString caret =
		    with_field.section(QStringLiteral("--- cursor ---\n"), 1).trimmed();
		CHECK(!caret.isEmpty() && caret != QStringLiteral("hidden"),
		      "and says which cell and which shape when a field has it");
		// THE PAIR is what makes either mean anything: a section that
		// always said the same thing would satisfy both checks above and
		// report nothing.
		CHECK(with_button.endsWith(QStringLiteral("--- cursor ---\nhidden\n")),
		      "while a window whose focus is on a button reports none, so "
		      "the section is not a constant");
	}

	// THE WHOLE SCREEN, not one widget. snapshot_of() renders the widget
	// it is given, which is right for a control and wrong for everything
	// a layer covers -- and a menu is the commonest thing to get wrong.
	// Measured with a QMenu popped over a window: render_once() shows the
	// button the menu is sitting ON and no menu at all, so a fixture taken
	// that way is a picture nobody sees.
	{
		QWidget win;
		win.resize(GridMetrics::cells(30, 8));
		auto *v = new QVBoxLayout(&win);
		v->addWidget(new QPushButton(QStringLiteral("Open")));
		win.show();
		InputRouter router(&win);
		QCoreApplication::processEvents();

		// THE CONTROL FIRST, and it is the one that says the new call is
		// not inventing a layer: with nothing popped, the screen snapshot
		// is the widget's picture and then what only a composed frame
		// knows -- the caret, which a widget rendered on its own has no
		// answer for.
		const QString quiet_widget = Qtty::test::snapshot_of(win, 30, 8);
		const QString quiet_screen =
		    Qtty::test::snapshot_of_screen(win, router, 30, 8);
		CHECK(quiet_screen.startsWith(quiet_widget),
		      "with no layer up, a screen snapshot is the widget's own "
		      "picture with the frame's caret after it");

		QMenu m(&win);
		m.addAction(QStringLiteral("New"));
		m.addAction(QStringLiteral("Quit"));
		m.popup(QPoint(2 * GridMetrics::cw(), 3 * GridMetrics::ch()));
		QCoreApplication::processEvents();
		const QString with_widget = Qtty::test::snapshot_of(win, 30, 8);
		const QString with_screen =
		    Qtty::test::snapshot_of_screen(win, router, 30, 8);
		CHECK(with_screen.contains(QStringLiteral("Quit")),
		      "a screen snapshot holds the menu standing over the window");
		CHECK(!with_widget.contains(QStringLiteral("Quit")),
		      "while a widget snapshot does not, which is what the new "
		      "call is for rather than a preference between them");
		m.hide();
		QCoreApplication::processEvents();
	}

	// A selected item taller than one cell is reversed throughout, not just on
	// its first line. Invisible while every item was one cell tall, which is
	// every item this suite had before CellItemDelegate could return a taller
	// sizeHint -- so the check needs a multi-row item to say anything at all.
	{
		QListWidget list;
		list.setFrameShape(QFrame::NoFrame);
		auto *tall = new QListWidgetItem(QStringLiteral("tall"));
		tall->setSizeHint(QSize(GridMetrics::cw() * 10, GridMetrics::ch() * 3));
		list.addItem(tall);
		list.setCurrentItem(tall);
		show(list, 12, 4);
		CellBuffer b(12, 4);
		render_once(list, b);

		int reversed = 0;
		for (int y = 0; y < 3; ++y)
			if (b.at(0, y).attrs & Attr::Reverse) ++reversed;
		CHECK(reversed == 3, "a three-cell selected item is reversed on all "
		                     "three rows, not only the first");
	}

	// ---- editable combo and the text editors (section 17.2) ------------------
	//
	// Both were recorded as gaps -- "editable variant untested/unhandled" and
	// a QTextEdit interaction layer estimated at up to four days. Neither
	// needed code. F8 already measured that display is free when the document
	// font's line height equals the cell height; what was missing was keys
	// reaching them, which is the routing fixed for menus. Recorded as absent,
	// obstructed in fact -- the third and fourth time in this tree.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&host);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *combo = new QComboBox(&host);
		combo->setEditable(true);
		combo->addItems({QStringLiteral("alpha")});
		// Five rows and four, not four and three. A framed widget spends a
		// row on each border, so a four-row editor has TWO rows of content
		// -- it used to appear to have three by drawing one of them over its
		// own bottom border, which is the fault the viewport inset fixes.
		// With two rows and the cursor at the end, the first line scrolls
		// off and this check could not see the text it is about.
		auto *doc = new QPlainTextEdit(&host);
		doc->setFixedHeight(GridMetrics::ch() * 5);
		auto *rich = new QTextEdit(&host);
		rich->setFixedHeight(GridMetrics::ch() * 4);
		v->addWidget(combo);
		v->addWidget(doc);
		v->addWidget(rich);
		v->addStretch();
		host.resize(GridMetrics::cells(40, 12));
		host.show();
		QCoreApplication::processEvents();
		InputRouter er(&host);

		const auto typed = [&](const QString &text) {
			for (const QString &cl : to_clusters(text))
				er.on_key({0, cl, false, false, false});
		};

		combo->lineEdit()->clear();
		combo->lineEdit()->setFocus();
		set_focus_widget(combo->lineEdit());
		typed(QString::fromUtf8("h\u00e9llo"));
		CHECK(combo->currentText() == QString::fromUtf8("h\u00e9llo"),
		      "an editable combo takes typed text, non-ASCII included");

		doc->setFocus();
		set_focus_widget(doc);
		typed(QStringLiteral("abc"));
		er.on_key({Qt::Key_Return, {}, false, false, false});
		typed(QString::fromUtf8("\u6f22\u5b57"));
		CHECK(doc->toPlainText() == QString::fromUtf8("abc\n\u6f22\u5b57"),
		      "a plain text editor takes typing, Return and wide clusters");

		rich->setFocus();
		set_focus_widget(rich);
		typed(QStringLiteral("rich"));
		CHECK(rich->toPlainText() == QStringLiteral("rich"),
		      "and so does QTextEdit, which section 8.4 lists as Replaced");

		// Display is the half F8 already measured; this is the half that
		// proves the two agree -- what was typed is what the cells carry.
		CellBuffer b(40, 12);
		render_once(host, b);
		const QString frame = b.to_text();
		CHECK(frame.contains(QString::fromUtf8("h\u00e9llo")),
		      "the combo's text reaches the cells");
		CHECK(frame.contains(QStringLiteral("abc")),
		      "and the editor's does too");
	}

	// QSpinBox, which section 7.2 recorded as having no test at all.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&host);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *spin = new QSpinBox(&host);
		spin->setRange(0, 10);
		spin->setValue(5);
		v->addWidget(spin);
		v->addStretch();
		host.resize(GridMetrics::cells(30, 6));
		host.show();
		QCoreApplication::processEvents();
		InputRouter sr(&host);
		spin->setFocus();
		set_focus_widget(spin);

		sr.on_key({Qt::Key_Up, {}, false, false, false});
		CHECK(spin->value() == 6, "Up steps a spin box");
		sr.on_key({Qt::Key_Down, {}, false, false, false});
		sr.on_key({Qt::Key_Down, {}, false, false, false});
		CHECK(spin->value() == 4, "and Down steps it back");

		// The internal QLineEdit is placed by subControlRect, which this
		// style did not override: it came out 280x13+3+3 inside a one-cell
		// spin box, at the proxy style's pixel insets. No application can
		// correct that -- it never constructed the widget.
		QLineEdit *edit = spin->findChild<QLineEdit *>();
		CHECK(edit && GridMetrics::is_aligned(edit->geometry()),
		      "a spin box's internal edit lands on the grid");

		// The value must still be readable after all of that. It was not:
		// stepping selects the text, a selection is what makes Qt paint a
		// caret, and the caret erased the digit (section 7.2). The end-to-end
		// symptom is kept here as well as the engine rule in suite_render,
		// because what a user meets is a spin box they can change and cannot
		// read -- and the state that produces it is exactly the state this
		// test is already in, focused and stepped.
		Qtty::CellBuffer buf(30, 6);
		Qtty::render_once(host, buf);
		CHECK(buffer_contains(buf, QString::number(spin->value())),
		      "a stepped spin box still shows its value");
	}
	// QToolBar rendered as an empty strip. Two things were wrong and only the
	// pair produces anything: QToolBar defaults to Qt::ToolButtonIconOnly and
	// a terminal draws no icon, so the buttons were measured for an icon and
	// the label had nowhere to go; and nothing drew a tool button's label at
	// all. Measured before the fix: two actions laid out correctly at 60x19
	// and 70x19, and not one glyph on the screen.
	//
	// The default style is used deliberately rather than
	// setToolButtonStyle(Qt::ToolButtonTextOnly), because with the style set
	// explicitly this passes with the sizing left broken -- it is the default
	// that carries the defect, and it is what an application gets.
	{
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = win.addToolBar(QStringLiteral("main"));
		bar->addAction(QStringLiteral("Cut"));
		bar->addAction(QStringLiteral("Copy"));
		auto *central = new QWidget;
		win.setCentralWidget(central);
		auto *label = new QLabel(QStringLiteral("body"), central);
		label->setGeometry(0, 0, GridMetrics::cw() * 8, GridMetrics::ch());
		win.resize(GridMetrics::cells(40, 6));
		win.show();
		QCoreApplication::processEvents();

		Qtty::CellBuffer buf(40, 6);
		Qtty::render_once(win, buf);
		const QStringList rows = buf.to_text().split(QLatin1Char('\n'));
		CHECK(rows.value(0).startsWith(QStringLiteral("[Cut][Copy]")),
		      "a toolbar draws its actions");

		bool aligned = true;
		for (QToolButton *b : bar->findChildren<QToolButton *>())
			if (b->isVisible() && !GridMetrics::is_aligned(b->geometry())) aligned = false;
		CHECK(aligned, "and its buttons land on the grid");

		// The other half, and a separate defect: a rule drawn on the LAST
		// pixel row of a widget was rounded into the row below it, so the
		// toolbar's own bottom border was written across the central widget's
		// row -- measured as "body" followed by a full-width rule. A line
		// belongs to the cell it is in, not the boundary it is nearest.
		CHECK(rows.value(1).trimmed() == QStringLiteral("body"),
		      "and its border stays in its own row");
	}

	// The mnemonic rule, everywhere that draws its own text. Fixing the push
	// button removed that symptom and left the CAUSE -- a style that writes
	// option text straight into cells never reaches drawItemText(), which is
	// where Qt strips the marker -- alive in two more places. Found by
	// looking for it deliberately, after the beerssh session observed that a
	// fix removing a symptom can hide what caused it.
	//
	// Three spellings of one rule stood here: strip_mnemonic() on the button,
	// an ad-hoc remove('&') on the menu bar which turned "A && B" into
	// "A  B", and nothing at all on the tab bar.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *tabs = new QTabWidget(&h);
		tabs->addTab(new QWidget, QStringLiteral("&General"));
		tabs->addTab(new QWidget, QStringLiteral("A&dvanced"));
		tabs->setGeometry(0, 0, GridMetrics::cw() * 30, GridMetrics::ch() * 4);
		h.resize(GridMetrics::cells(30, 6));
		h.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(30, 6);
		Qtty::render_once(h, buf);
		const QString row = buf.to_text().split(QLatin1Char('\n')).value(0);
		// Measured before the fix as "[&Genera...": the marker was drawn AND
		// stole the cell that made the label elide a character early, so the
		// two symptoms had one cause.
		CHECK(row.startsWith(QStringLiteral("[General][Advanced]")),
		      "a tab's mnemonic marker is not drawn, and does not cost a cell");
	}
	{
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		// addAction rather than addMenu: a menu bar item is an action either
		// way, and addMenu() would build a QMenu this never shows -- which
		// then sits at Qt's default 100x30 and is reported off the grid, a
		// widget nothing has laid out rather than one laid out wrongly.
		win.menuBar()->addAction(QStringLiteral("A && B"));
		win.menuBar()->setGeometry(0, 0, GridMetrics::cw() * 30, GridMetrics::ch());
		win.resize(GridMetrics::cells(30, 6));
		win.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(30, 6);
		Qtty::render_once(*win.menuBar(), buf);
		CHECK(buf.to_text().contains(QStringLiteral("A & B")),
		      "and a menu bar's doubled ampersand is one, not none");
	}

	// design.md section 8.6: the icon substitution registry. A terminal
	// cannot draw a 16-pixel icon in one cell, which is why drawPixmap()
	// stamps a placeholder block there; the registry is how an application
	// says what the icon MEANS, chosen by whoever knows the icon set.
	{
		Qtty::clear_icon_glyphs();
		Qtty::set_icon_glyph(QStringLiteral("edit-cut"), QStringLiteral("XC"));
		CHECK(Qtty::icon_glyph(QStringLiteral("edit-cut")) == QStringLiteral("XC"),
		      "an icon name resolves to its registered glyph");
		CHECK(Qtty::icon_glyph(QStringLiteral("edit-paste")).isEmpty(),
		      "and an unregistered one resolves to nothing");
		Qtty::set_icon_glyph(QString(), QStringLiteral("x"));
		CHECK(Qtty::icon_glyph(QString()).isEmpty(),
		      "an empty name is not a key -- every unnamed icon would share it");

		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		CHECK(Qtty::glyph_for(&host, QStringLiteral("edit-cut"))
		          == QStringLiteral("XC"), "the registry answers for a widget");
		host.setProperty("qtty.glyph", QStringLiteral("PP"));
		CHECK(Qtty::glyph_for(&host, QStringLiteral("edit-cut"))
		          == QStringLiteral("PP"),
		      "and a widget property beats it, being per-instance");
		host.setProperty("qtty.glyph", QString());
		CHECK(Qtty::glyph_for(&host, QStringLiteral("edit-cut"))
		          == QStringLiteral("XC"),
		      "an empty property falls through rather than blanking the icon");
		CHECK(Qtty::glyph_for(nullptr, QStringLiteral("edit-cut"))
		          == QStringLiteral("XC"), "and no widget is not an error");
	}
	{
		// End to end on a toolbar, and through the ACTION's property rather
		// than the button's: a toolbar's QToolButton is built by Qt from a
		// QAction the application created, so requiring the property on the
		// button would require it on a widget the application never sees.
		// Measured on this machine, that is also the only route that works --
		// QIcon::name() is empty unless an icon theme resolved the icon, and
		// qtty pins the platform theme off.
		Qtty::clear_icon_glyphs();
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = win.addToolBar(QStringLiteral("main"));
		auto *cut = bar->addAction(QStringLiteral("Cut"));
		cut->setProperty("qtty.glyph", QStringLiteral("XC"));
		bar->addAction(QStringLiteral("Copy"));
		win.resize(GridMetrics::cells(40, 6));
		win.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(40, 6);
		Qtty::render_once(win, buf);
		const QString row = buf.to_text().split(QLatin1Char('\n')).value(0);
		CHECK(row.startsWith(QStringLiteral("[XC Cut][Copy]")),
		      "an action's glyph is drawn beside its text");

		// The measurement must agree with the drawing, or the label is put in
		// a box a cell too narrow and the elide eats the last letter instead
		// of the thing that did not fit. Asserted through the button's width
		// rather than by reading the row twice.
		QToolButton *first = bar->findChildren<QToolButton *>().value(0);
		for (QToolButton *b : bar->findChildren<QToolButton *>())
			if (b->isVisible()) { first = b; break; }
		CHECK(first && first->width() == GridMetrics::cw() * 8,
		      "and the button was measured with the glyph in it");
		Qtty::clear_icon_glyphs();
	}
	{
		// The four arrow primitives, which coverage named as never drawn.
		// They are not dead: the qtty style draws the combo box, the spin box
		// and the scroll bar WHOLE, so none of the obvious candidates reaches
		// them -- but a tool button with a menu falls through to the base
		// style, which asks this one for PE_IndicatorArrowDown.
		//
		// Note what that means for the combo test above, which asserts the
		// same glyph: it passes through CC_ComboBox's own drawing, so a check
		// for the arrow cannot say which path drew it. This window holds one
		// tool button and nothing else, so here it can.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *tb = new QToolButton(&host);
		auto *menu = new QMenu(tb);
		menu->addAction(QStringLiteral("One"));
		tb->setText(QStringLiteral("Go"));
		tb->setMenu(menu);
		tb->setPopupMode(QToolButton::MenuButtonPopup);
		// At its size HINT, which is what a layout or a toolbar gives it.
		// Sized wider the label pads and the marker tracks the closing
		// bracket -- also correct, and not what discriminates: the exact row
		// is only exact when the button is the width it asked for.
		tb->setGeometry(0, 0, tb->sizeHint().width(), GridMetrics::ch());
		host.resize(GridMetrics::cells(20, 3));
		host.show();
		QCoreApplication::processEvents();

		Qtty::CellBuffer buf(20, 3);
		Qtty::render_once(host, buf);
		// The exact row, not "the arrow appears somewhere": position is the
		// whole of it. An arrow drawn over the label, or outside the closing
		// bracket, satisfies any check that only asks whether the glyph is
		// present -- and one drawn over the label is the toolbar fault this
		// suite already found once, in the other order.
		const QString row = buf.to_text().split(QLatin1Char('\n')).value(0);
		// The observed row on failure, because the condition alone cannot
		// separate the hypotheses it generates -- an absent marker, one
		// drawn over the label, one outside the bracket, and a button at a
		// width other than its hint all fail it identically. Diagnosing this
		// one during development took a temporary print, which is the proof
		// that the sentence was not enough.
		if (row.startsWith(QStringLiteral("[Go ▾]"))) {
			printf("PASS: a tool button with a menu says so, beside its label\n");
		} else {
			printf("FAIL: a tool button with a menu says so, beside its label\n"
			       "      row '%s', hint %d px, cell %d px\n",
			       qPrintable(row), tb->sizeHint().width(), GridMetrics::cw());
			++fails;
		}
		// And the measurement must agree with the drawing, or the marker is
		// drawn into a cell the width never admitted was needed and the
		// elide eats a letter to pay for it.
		CHECK(tb->sizeHint().width() == GridMetrics::cw() * 6,
		      "and the button was measured with the marker in it");
	}

	{
		// A toolbar SEPARATOR, which nothing had ever drawn. QToolBar draws
		// it through PE_IndicatorToolBarSeparator, and a terminal has a
		// character for exactly this -- so the alternative to drawing it is
		// a gap the user cannot tell from spacing.
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = win.addToolBar(QStringLiteral("main"));
		bar->addAction(QStringLiteral("Cut"));
		bar->addSeparator();
		bar->addAction(QStringLiteral("Quit"));
		win.resize(GridMetrics::cells(40, 6));
		win.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(40, 6);
		Qtty::render_once(win, buf);
		const QString row = buf.to_text().split(QLatin1Char('\n')).value(0);
		// Between the two buttons, not merely present: a rule drawn anywhere
		// satisfies "contains a bar", and the whole job of a separator is
		// where it is.
		const int cut = row.indexOf(QStringLiteral("Cut"));
		const int quit = row.indexOf(QStringLiteral("Quit"));
		const int rule = row.indexOf(QStringLiteral("│"));
		CHECK(cut >= 0 && quit > cut && rule > cut && rule < quit,
		      "a toolbar separator is drawn between the actions it separates");
	}
	{
		// The four arrow primitives. No widget in this style reaches them --
		// the combo box, spin box, scroll bar and tool button are all drawn
		// whole -- so they are asserted as what they are: public style API,
		// which an application's own widget calls through drawPrimitive().
		Qtty::CellBuffer buf(6, 4);
		QStyle *st = QApplication::style();
		const struct { QStyle::PrimitiveElement pe; const char *glyph; } arrows[] = {
			{QStyle::PE_IndicatorArrowDown,  "▾"},
			{QStyle::PE_IndicatorArrowUp,    "▴"},
			{QStyle::PE_IndicatorArrowLeft,  "◂"},
			{QStyle::PE_IndicatorArrowRight, "▸"},
		};
		bool all = true;
		int i = 0;
		for (const auto &a : arrows) {
			Qtty::CellPaintDevice dev(buf);
			QPainter p(&dev);
			QStyleOption opt;
			opt.rect = QRect(i * GridMetrics::cw(), 0,
			                 GridMetrics::cw(), GridMetrics::ch());
			st->drawPrimitive(a.pe, &opt, &p, nullptr);
			p.end();
			all = all && buf.at(i, 0).ch == QString::fromUtf8(a.glyph);
			++i;
		}
		// One glyph per direction, and each in its own cell: drawing them
		// into one buffer at four columns is what catches an arrow that
		// ignores the rect it was given, which the earlier draft did not.
		// The POPULATION pinned beside the claim, as suite_theme and
		// suite_graphics do for their own "each of the six". Without it an
		// arrow taken out of the list leaves this saying "each arrow
		// primitive" over three of them.
		CHECK(sizeof(arrows) / sizeof(arrows[0]) == 4,
		      "the four arrow directions are all covered, a count that cannot"
		      " shrink without this saying so");
		CHECK(all, "each arrow primitive draws its own glyph at its own rect");
	}

	{
		// A HORIZONTAL scrollbar, which nothing had drawn: every scrollbar
		// test here is vertical, so the else arm of the one loop that places
		// the thumb had never run. The two are not symmetric in the code --
		// one walks rows and the other columns -- so a fault in either is
		// invisible from the other.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = new QScrollBar(Qt::Horizontal, &host);
		bar->setRange(0, 100);
		bar->setValue(0);
		bar->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		host.resize(GridMetrics::cells(20, 3));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(20, 3);
		Qtty::render_once(host, buf);
		const QString row = buf.to_text().split(QLatin1Char('\n')).value(0);
		const int at_start = row.indexOf(QStringLiteral("█"));

		bar->setValue(100);
		QCoreApplication::processEvents();
		Qtty::CellBuffer moved(20, 3);
		Qtty::render_once(host, moved);
		const QString row2 = moved.to_text().split(QLatin1Char('\n')).value(0);
		const int at_end = row2.indexOf(QStringLiteral("█"));

		// The thumb MOVES along the row. Merely drawing one satisfies a check
		// for the glyph, and a horizontal bar that placed its thumb by row
		// would draw an identical row at every value.
		CHECK(at_start >= 0 && at_end > at_start,
		      "a horizontal scrollbar puts its thumb where the value is");
	}
	{
		// A spin box tall enough to have a box drawn round it. The one-row
		// form is what every other test builds, and it takes the other arm:
		// brackets rather than a frame.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *spin = new QSpinBox(&host);
		spin->setRange(0, 99);
		spin->setValue(7);
		spin->setGeometry(0, 0, GridMetrics::cw() * 8, GridMetrics::ch() * 3);
		host.resize(GridMetrics::cells(20, 5));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(20, 5);
		Qtty::render_once(host, buf);
		// A corner, not just any box character: a frame drawn one cell wrong
		// still contains horizontal rules, and the corner is what says the
		// rectangle is where the widget is.
		CHECK(buf.at(0, 0).ch == QStringLiteral("┌"),
		      "a spin box more than one row tall is framed, not bracketed");
	}

	{
		// A framed scroll area keeps its own bottom border. Its viewport is
		// inset by one frame width, which is a whole column but only half a
		// row on a cell taller than it is wide -- so the viewport's height
		// rounded up to an extra row and its background fill erased the rule
		// the frame had just drawn, leaving the corners standing. Every
		// framed QAbstractScrollArea had it: text edit, list, table, tree.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *doc = new QPlainTextEdit(&host);
		doc->setPlainText(QStringLiteral("aa\nbb"));
		doc->setGeometry(0, 0, GridMetrics::cw() * 12, GridMetrics::ch() * 5);
		host.resize(GridMetrics::cells(20, 6));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(20, 6);
		Qtty::render_once(host, buf);
		const QStringList rows = buf.to_text().split(QLatin1Char('\n'));
		const QString bottom = rows.value(4).left(12);
		// The whole rule, not "a corner is present": the corners survived the
		// bug, so a check for them passes against the broken frame. What was
		// missing is everything between them.
		CHECK(bottom == QStringLiteral("└──────────┘"),
		      "a framed scroll area's bottom border survives its own viewport");
		// Paired with the top, which never broke -- so a frame that stopped
		// being drawn at all fails this rather than passing the check above
		// by drawing nothing anywhere.
		CHECK(rows.value(0).left(12) == QStringLiteral("┌──────────┐"),
		      "and its top border is still there too");
	}
	{
		// A tristate checkbox's middle state. It arrives as State_NoChange
		// and drew "[ ]" -- identical to unchecked, so the state existed in
		// the model and not on the screen, and the only way to find it was to
		// click and watch the box cycle somewhere unexpected.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *box = new QCheckBox(QStringLiteral("Tri"), &host);
		box->setTristate(true);
		box->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		host.resize(GridMetrics::cells(20, 3));
		host.show();
		QCoreApplication::processEvents();
		const auto glyph_at = [&](Qt::CheckState st) {
			box->setCheckState(st);
			QCoreApplication::processEvents();
			Qtty::CellBuffer b(20, 3);
			Qtty::render_once(host, b);
			return b.to_text().split(QLatin1Char('\n')).value(0).left(3);
		};
		const QString un = glyph_at(Qt::Unchecked);
		const QString part = glyph_at(Qt::PartiallyChecked);
		const QString on = glyph_at(Qt::Checked);
		// All three compared against each other, which is the assertion that
		// cannot be satisfied by a widget drawing one glyph for two states.
		CHECK(un != part && part != on && un != on,
		      "three check states draw three different boxes");
		CHECK(part == QStringLiteral("[-]"),
		      "and the middle one says it is neither");
	}

	{
		// A disabled control is dim. Qt reports the state in every option it
		// hands the style and the style tested for it nowhere, so a button
		// nobody can press looked exactly like one they can: same characters,
		// same colours, no attribute. The only way to find out was to click
		// and have nothing happen.
		//
		// Asserted on the ATTRIBUTE, which is the whole of the change --
		// to_text() shows characters, so a check on the rendered string
		// passes against the bug and would have gone on passing.
		const auto attrs_of = [&](bool enabled) {
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			auto *b = new QPushButton(QStringLiteral("Go"), &host);
			b->setEnabled(enabled);
			b->setGeometry(0, 0, GridMetrics::cw() * 6, GridMetrics::ch());
			host.resize(GridMetrics::cells(12, 3));
			host.show();
			QCoreApplication::processEvents();
			Qtty::CellBuffer buf(12, 3);
			Qtty::render_once(host, buf);
			Attrs seen;
			for (int x = 0; x < 12; ++x)
				if (buf.at(x, 0).ch == QStringLiteral("G")) seen = buf.at(x, 0).attrs;
			return seen;
		};
		const Attrs on = attrs_of(true), off = attrs_of(false);
		// Both directions. "The disabled one is dim" is satisfied by a style
		// that dims everything, which would be a different bug wearing the
		// same green.
		CHECK(off.testFlag(Attr::Dim), "a disabled button's label is dim");
		CHECK(!on.testFlag(Attr::Dim), "and an enabled one's is not");
	}
	{
		// A checkable item view showed its text and nothing else, so the
		// state a user opens such a list to set was invisible -- and there is
		// no second place to read it from, unlike a checkbox which at least
		// has a label beside it.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *list = new QListWidget(&host);
		for (int i = 0; i < 2; ++i) {
			auto *it = new QListWidgetItem(QStringLiteral("row%1").arg(i));
			it->setCheckState(i ? Qt::Checked : Qt::Unchecked);
			list->addItem(it);
		}
		list->setGeometry(0, 0, GridMetrics::cw() * 16, GridMetrics::ch() * 4);
		host.resize(GridMetrics::cells(20, 5));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(20, 5);
		Qtty::render_once(host, buf);
		const QString text = buf.to_text();
		// Both states and the text: a box drawn over the label would satisfy
		// a check for "[x] appears somewhere", and the label is what says
		// which row the box belongs to.
		CHECK(text.contains(QStringLiteral("[ ] row0")),
		      "an unchecked item shows an empty box before its text");
		CHECK(text.contains(QStringLiteral("[x] row1")),
		      "and a checked one shows a filled box");
	}
	{
		// A vertical progress bar was drawn as a horizontal one in its top
		// row, leaving the rest of the widget blank -- a meter reading
		// nothing, in the orientation an application picks precisely because
		// it has a tall space to fill.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = new QProgressBar(&host);
		bar->setOrientation(Qt::Vertical);
		bar->setTextVisible(false);
		bar->setRange(0, 4);
		bar->setValue(2);
		bar->setGeometry(0, 0, GridMetrics::cw(), GridMetrics::ch() * 4);
		host.resize(GridMetrics::cells(6, 5));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(6, 5);
		Qtty::render_once(host, buf);
		// Half full, filling UPWARD: the bottom two cells are solid and the
		// top two are not. A bar that filled downward passes any check that
		// only counts solid cells, and is the one thing a reader would call
		// obviously wrong.
		CHECK(buf.at(0, 3).ch == QStringLiteral("█")
		      && buf.at(0, 2).ch == QStringLiteral("█"),
		      "a vertical progress bar fills from the bottom");
		CHECK(buf.at(0, 0).ch == QStringLiteral("░")
		      && buf.at(0, 1).ch == QStringLiteral("░"),
		      "and leaves the unfilled part above it");
	}

	{
		// An indeterminate progress bar. minimum == maximum is Qt's way of
		// saying the length of the job is unknown, and it was drawn as a bar
		// at 0% with "0%" written across it -- which does not read as
		// working, it reads as stalled, the one thing it is not.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *busy = new QProgressBar(&host);
		busy->setRange(0, 0);
		busy->setGeometry(0, 0, GridMetrics::cw() * 8, GridMetrics::ch());
		auto *known = new QProgressBar(&host);
		known->setRange(0, 10);
		known->setValue(5);
		known->setGeometry(0, GridMetrics::ch(), GridMetrics::cw() * 8,
		                   GridMetrics::ch());
		host.resize(GridMetrics::cells(12, 3));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(12, 3);
		Qtty::render_once(host, buf);
		const QStringList rows = buf.to_text().split(QLatin1Char('\n'));
		// Paired with a bar whose length IS known, so "shows no percentage"
		// is not satisfied by a style that never shows one, and "uses its own
		// shade" is not satisfied by one drawing the same thing everywhere.
		CHECK(rows.value(0).startsWith(QStringLiteral("▒▒▒"))
		      && !rows.value(0).contains(QLatin1Char('%')),
		      "an unknown-length bar says so, and quotes no percentage");
		CHECK(rows.value(1).contains(QLatin1Char('%'))
		      && rows.value(1).contains(QStringLiteral("█")),
		      "while a known one still fills and still says how far");
	}
	{
		// A tab bar down the side is a column of rows, not a rotated strip.
		// Qt hands a West tab its contents size already rotated -- narrow and
		// tall -- so taking that width gave a tab two cells wide and a label
		// elided to "[...", which is what a vertical tab bar rendered as.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *tabs = new QTabWidget(&host);
		tabs->setTabPosition(QTabWidget::West);
		tabs->addTab(new QWidget, QStringLiteral("General"));
		tabs->addTab(new QWidget, QStringLiteral("Network"));
		tabs->setGeometry(0, 0, GridMetrics::cw() * 18, GridMetrics::ch() * 5);
		host.resize(GridMetrics::cells(24, 6));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(24, 6);
		Qtty::render_once(host, buf);
		const QStringList rows = buf.to_text().split(QLatin1Char('\n'));
		// Whole labels, and one BELOW the other. Either alone is weaker than
		// it looks: a strip that merely fitted would put both on row 0, and a
		// column of elided tabs would stack correctly and say nothing.
		CHECK(rows.value(0).startsWith(QStringLiteral("[General]")),
		      "a west tab bar shows its first label whole");
		CHECK(rows.value(1).startsWith(QStringLiteral("[Network]")),
		      "and the next one on the row below, being a column");
	}

	{
		// A sort indicator. It fell through to the base style, which draws
		// one as a PIXMAP, so it reached the cell painter as an image too
		// small to place and came out as the tiny-icon substitute -- a shaded
		// block. A column sorted ascending and one sorted descending carried
		// the same meaningless mark, which is worse than none: it looks like
		// a rendering fault rather than like information.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *view = new QHeaderView(Qt::Horizontal, &host);
		auto *model = new QStandardItemModel(1, 2, view);
		model->setHorizontalHeaderLabels({QStringLiteral("Aa"), QStringLiteral("Bb")});
		view->setModel(model);
		view->setSortIndicatorShown(true);
		view->setGeometry(0, 0, GridMetrics::cw() * 20, GridMetrics::ch());
		host.resize(GridMetrics::cells(24, 2));
		host.show();
		QCoreApplication::processEvents();
		const auto row_for = [&](Qt::SortOrder o) {
			view->setSortIndicator(0, o);
			QCoreApplication::processEvents();
			Qtty::CellBuffer buf(24, 2);
			Qtty::render_once(host, buf);
			return buf.to_text().split(QLatin1Char('\n')).value(0);
		};
		const QString up = row_for(Qt::AscendingOrder);
		const QString down = row_for(Qt::DescendingOrder);
		// The two orders must DIFFER, which is the whole point of the mark
		// and the thing the shaded block could not do. Checking only that an
		// arrow appears would pass for a style that drew the same one both
		// ways.
		CHECK(up.contains(QStringLiteral("▴")) && down.contains(QStringLiteral("▾")),
		      "a sorted column says which way it is sorted");
		CHECK(up != down, "and the two orders do not draw the same mark");
	}

	{
		// A closable tab's close mark, and an arrow-type tool button. Both
		// were predicted by the sort indicator rather than found: anything
		// the base style draws as a pixmap arrives at the cell painter as an
		// image too small to place and comes out as the tiny-icon
		// substitute, so the close button offered a shaded block to click on.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = new QTabBar(&host);
		bar->setTabsClosable(true);
		bar->addTab(QStringLiteral("One"));
		bar->setGeometry(0, 0, GridMetrics::cw() * 20, GridMetrics::ch());
		host.resize(GridMetrics::cells(24, 2));
		host.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(24, 2);
		Qtty::render_once(host, buf);
		const QString row = buf.to_text().split(QLatin1Char('\n')).value(0);
		// The mark AND the absence of the substitute: a style that drew both
		// would satisfy a check for the cross alone, and the shaded block is
		// exactly what this replaced.
		CHECK(row.contains(QStringLiteral("✕")),
		      "a closable tab offers a close mark");
		CHECK(!row.contains(QStringLiteral("▒")),
		      "and not the shaded block a pixmap turns into");
	}
	{
		// An arrow-type tool button -- the scroll and navigation buttons Qt
		// builds, and any QToolButton given an arrowType -- has no text and
		// no icon, and nothing asked what kind of arrow it was, so it drew an
		// empty pair of brackets.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *b = new QToolButton(&host);
		host.resize(GridMetrics::cells(12, 3));
		host.show();
		const auto drawn = [&](Qt::ArrowType t) {
			b->setArrowType(t);
			b->setGeometry(0, 0, b->sizeHint().width(), GridMetrics::ch());
			QCoreApplication::processEvents();
			Qtty::CellBuffer buf(12, 3);
			Qtty::render_once(host, buf);
			return buf.to_text().split(QLatin1Char('\n')).value(0).left(3);
		};
		// All four directions, each its own glyph: a button drawing one
		// default arrow passes any check that only asks whether an arrow is
		// there, and the direction is the entire content of this widget.
		CHECK(drawn(Qt::UpArrow) == QStringLiteral("[▴]")
		      && drawn(Qt::DownArrow) == QStringLiteral("[▾]")
		      && drawn(Qt::LeftArrow) == QStringLiteral("[◂]")
		      && drawn(Qt::RightArrow) == QStringLiteral("[▸]"),
		      "an arrow tool button draws the arrow it was given");
		// Measured for it too, or the bracket eats the arrow -- the same
		// pairing the menu marker needed.
		b->setArrowType(Qt::DownArrow);
		CHECK(b->sizeHint().width() == GridMetrics::cw() * 3,
		      "and is measured as one cell of arrow between its brackets");
	}






	// An icon-only toolbar action, which is the common toolbar shape and had
	// nothing to draw at all. SH_ToolButtonStyle is pinned to text-only
	// because a terminal draws no icon, so an action carrying only a picture
	// carried nothing: measured on four actions, "[Cut]" and "[Find]"
	// rendered and the two icon-only ones occupied four cells between them
	// and drew NOTHING.
	//
	// The tool tip is where such an action already keeps its words -- what Qt
	// shows on hover and what a screen reader announces -- so it is what the
	// label falls back to. design.md asks for Compact::IconsToLetters here; a
	// word beats a letter and costs the application nothing new, which is why
	// this is unconditional rather than a hint (section 8).
	{
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *tb = win.addToolBar(QStringLiteral("Main"));
		QPixmap pm(16, 16);
		pm.fill(Qt::red);
		const QIcon icon(pm);
		tb->addAction(icon, QStringLiteral("&Cut"));
		tb->addAction(icon, QString())->setToolTip(QStringLiteral("Copy"));
		tb->addAction(icon, QString());          // no text, no tip: no words
		tb->addAction(QStringLiteral("Find"));
		win.setCentralWidget(new QLabel(QStringLiteral("body")));
		win.resize(GridMetrics::cells(40, 6));
		win.show();
		QCoreApplication::processEvents();
		CellBuffer b(40, 6);
		render_once(win, b);
		const QString row = b.to_text().split(QLatin1Char('\n')).value(0);
		CHECK(row.contains(QStringLiteral("[Copy]")),
		      "an icon-only action falls back to its tool tip");
		// And the one that has no words anywhere is still visibly a control.
		// The bracket-dropping rule written for the dock widget's title
		// buttons made this WORSE before this pair existed: it turned two
		// cells of "[]" into two blank cells, which is an invisible button.
		// Brackets are dropped to buy room for content, so with no content
		// there is nothing to buy.
		CHECK(row.contains(QStringLiteral("[]")),
		      "and one with no words at all is still visibly a control");
		CHECK(row.contains(QStringLiteral("[Cut]")) && row.contains(QStringLiteral("[Find]")),
		      "while an action with text is untouched");
		GridGuard::reset();
	}



	// design.md section 7's third Tier-2 hint: "qtty.cells" says how many
	// cells a widget needs, in the application's own words, with no branch on
	// target and no call into qtty. Applied as a MINIMUM, which is the
	// non-destructive reading of "this field needs twenty columns" -- fewer
	// makes it useless, more is fine -- so it composes with stretch and feeds
	// the small-terminal policy rather than fighting either.
	//
	// The FIRST check is the one that decides where this is read. design.md
	// section 5.1 says the style reads it, "the style receives the QWidget*".
	// It does, but only for the widgets Qt asks it about, and
	// QStyle::ContentsType has twenty-four values and no entry for a label, a
	// text edit, a view, or an application's own QWidget subclass -- not even
	// for the case the document's own example uses. A style-side reader would
	// silently do nothing for most of a tree. It is read in GridSnap's filter
	// instead, and section 8.8 records the divergence.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QWidget d;
		auto *v = new QVBoxLayout(&d);
		auto *lab = new QLabel(QStringLiteral("a label"));
		lab->setProperty("qtty.cells", QSize(20, 1));
		auto *btn = new QPushButton(QStringLiteral("press"));
		btn->setProperty("qtty.cells", QSize(12, 1));
		auto *bad = new QLineEdit(QStringLiteral("edit"));
		bad->setProperty("qtty.cells", QSize(-1, 2));   // half nonsense
		auto *plain = new QLineEdit(QStringLiteral("plain"));   // no property
		auto *late = new QLabel(QStringLiteral("later"));
		v->addWidget(lab);
		v->addWidget(btn);
		v->addWidget(bad);
		v->addWidget(plain);
		v->addWidget(late);
		d.setAttribute(Qt::WA_DontShowOnScreen);
		d.resize(GridMetrics::cells(30, 12));
		d.show();
		QCoreApplication::processEvents();

		CHECK(lab->minimumSize() == QSize(20 * cw, ch),
		      "qtty.cells sizes a QLabel, which the style is never asked about");
		CHECK(btn->minimumSize() == QSize(12 * cw, ch),
		      "and a push button, which it is");
		// A typo must not be half obeyed. The value here is -1 by 2, and the
		// case that matters is not that the -1 is refused -- Qt clamps a
		// negative minimum to zero by itself -- but that the 2 is refused
		// WITH it, so a widget cannot end up two cells tall because its width
		// was misspelt.
		//
		// Two goes at this check, and the sabotage caught both. The first
		// asserted the minimum was non-zero, which no QWidget's default
		// minimumSize() is. The second used QSize(0, 0), which is exactly
		// what setMinimumSize() would have applied anyway -- so removing the
		// guard changed nothing and the check could not see it. A guard is
		// only testable through a value that would do damage if obeyed.
		CHECK(bad->minimumSize() == plain->minimumSize(),
		      "while a half-nonsense size is refused whole");
		// Set after the widget is up, which is the case Polish alone misses.
		late->setProperty("qtty.cells", QSize(8, 2));
		QCoreApplication::processEvents();
		CHECK(late->minimumSize() == QSize(8 * cw, 2 * ch),
		      "and setting it later works too");
		GridGuard::reset();
	}



	// The shape an application asked for, not just the floor. A widget's
	// vertical size policy decides whether a layout stretches it, and most do:
	// a QLineEdit's is Fixed and holds at one row on its own, but a QLabel's
	// is Preferred, so "20x1" became 38x11 -- a one-row annotation stretched
	// over eleven rows with the text floating in the middle.
	//
	// So the width is a floor and the height is exact, which is the rule
	// sizeFromContents() already states: a width is a count of characters and
	// rounding one down truncates text, while a single-line control is one
	// cell tall by construction.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QWidget d;
		auto *v = new QVBoxLayout(&d);
		auto *stretchy = new QLabel(QStringLiteral("a label"));
		stretchy->setProperty("qtty.cells", QSize(20, 1));
		v->addWidget(stretchy);
		d.setAttribute(Qt::WA_DontShowOnScreen);
		d.resize(GridMetrics::cells(40, 12));
		d.show();
		QCoreApplication::processEvents();
		// The pair, because either half alone is satisfied by a bug: a fixed
		// size in BOTH axes would hold the height and wrongly refuse the
		// width, and a floor in both would grow the height as it did before.
		CHECK(stretchy->height() == ch,
		      "a one-row hint stays one row however stretchy the widget");
		CHECK(stretchy->width() > 20 * cw,
		      "while its width is a floor and still grows to fill");
		GridGuard::reset();
	}



	// A closable tab, whose close mark was drawn OUTSIDE the tab it closes.
	// Qt sizes a tab wider than its label -- a closable one wider still, to
	// hold the button -- and this style drew "[One]" at the left of it, so the
	// tab bar's base rule filled the rest and the mark landed near the tab's
	// right edge:
	//
	//     [One]-------X-[Two]-------X-
	//
	// which reads as a rule with a cross in it. The existing pair of checks
	// passed throughout: they ask that the mark is present and that no shaded
	// block replaced it, and a mark in the wrong place satisfies both. Found
	// by rendering a form rather than by asking a question about it.
	//
	// Asserted as the relationship that was broken -- the mark is inside the
	// tab's own brackets, and nothing of the base rule is -- rather than as a
	// literal row, which would pin the tab's width.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = new QTabBar(&host);
		bar->setTabsClosable(true);
		bar->addTab(QStringLiteral("One"));
		bar->addTab(QStringLiteral("Two"));
		bar->setGeometry(0, 0, GridMetrics::cw() * 30, GridMetrics::ch());
		host.resize(GridMetrics::cells(32, 2));
		host.show();
		QCoreApplication::processEvents();
		CellBuffer b(32, 2);
		render_once(host, b);
		const QString row = b.to_text().split(QLatin1Char('\n')).value(0);
		const int open = row.indexOf(QLatin1Char('['));
		const int close = row.indexOf(QLatin1Char(']'));
		const int mark = row.indexOf(QStringLiteral("✕"));
		const QString tab = open >= 0 && close > open
		                        ? row.mid(open, close - open + 1) : QString();
		CHECK(open >= 0 && mark > open && mark < close
		      && !tab.contains(QStringLiteral("─")),
		      "a closable tab's mark is inside the tab, with no rule between");
	}



	// Where the one-number-metric fault does NOT reach, which is worth a check
	// because it is held incidentally rather than by design.
	//
	// Three widgets in a row were wrong because a QStyle metric is one number
	// and a cell is not square: a group box's contents, a popup's panel, a
	// tab's drawn bracket. PM_ScrollBarExtent and PM_SplitterWidth are the
	// same shape of metric -- `cw` used for a HEIGHT when the widget is
	// horizontal -- so the obvious guess is that a horizontal scroll bar is
	// ten pixels tall, half a row.
	//
	// Measured, both are exactly one row -- and the first explanation for that
	// was wrong, which is why the mechanism is named here rather than assumed.
	// It looked like GridSnap: it snaps every child widget's geometry, and a
	// scroll bar and a splitter handle are child widgets. Removing the snap
	// reddened two OTHER checks and left this one green.
	//
	// It is sizeFromContents(), whose snap-up list carries CT_ScrollBar and
	// CT_Splitter: dropping just those two from it reddens this check alone.
	//
	// So the boundary the three broken widgets share is sharper than "a metric
	// is one number": a metric that reaches a widget's own SIZE is caught by
	// the snap-up, and one that describes an inset INSIDE a widget -- a group
	// box's contents, a popup's panel -- or a rectangle the style draws
	// itself -- a tab's bracket -- is not. This pins that dependence: narrow
	// the snap-up list and these go red and say why.
	{
		QWidget d;
		d.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&d);
		auto *area = new QScrollArea;
		area->setFrameShape(QFrame::NoFrame);
		auto *wide = new QLabel(QString(80, QLatin1Char('x')));
		wide->setMinimumWidth(GridMetrics::cw() * 80);
		area->setWidget(wide);
		area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		v->addWidget(area);
		auto *split = new QSplitter(Qt::Vertical);
		split->addWidget(new QLabel(QStringLiteral("top")));
		split->addWidget(new QLabel(QStringLiteral("bottom")));
		v->addWidget(split);
		d.resize(GridMetrics::cells(24, 10));
		d.show();
		QCoreApplication::processEvents();
		const int ch = GridMetrics::ch();
		const int bar = area->horizontalScrollBar()->height();
		const int handle = split->count() > 1 && split->handle(1)
		                       ? split->handle(1)->height() : 0;
		CHECK(bar > 0 && bar % ch == 0 && handle > 0 && handle % ch == 0,
		      "a horizontal bar and a splitter handle are whole rows tall");
		GridGuard::reset();
	}



	// The two orientations nothing rendered, which section 0a named as the
	// coverage residue worth taking and which is the shape every defect in the
	// last sweep had: a state that exists in the model and not on the screen.
	//
	// **A vertical slider was drawn upside down.** QSlider sets the option's
	// `upsideDown` to !invertedAppearance() for a vertical slider, so it is
	// true by default and the minimum belongs at the BOTTOM. This style read
	// the orientation and not the flag, and mapped value to row top-down:
	// measured against Qt's own SC_SliderHandle for the same widget, value 0
	// wanted y=84 of a six-row slider and was drawn at row 0, and value 100
	// wanted y=0 and was drawn at row 5. Every vertical slider ran backwards.
	//
	// The pair is the assertion. "The minimum is at the bottom" alone is
	// satisfied by a slider that never moves.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const auto thumb_row = [&](int value, Qt::Orientation o, bool inverted) {
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			auto *sl = new QSlider(o, &h);
			sl->setRange(0, 100);
			sl->setInvertedAppearance(inverted);
			sl->setValue(value);
			if (o == Qt::Vertical) sl->setGeometry(0, 0, cw, ch * 6);
			else                   sl->setGeometry(0, 0, cw * 6, ch);
			h.resize(GridMetrics::cells(8, 7));
			h.show();
			QCoreApplication::processEvents();
			CellBuffer b(8, 7);
			render_once(h, b);
			const QStringList rows = b.to_text().split(QLatin1Char('\n'));
			if (o == Qt::Vertical) {
				for (int y = 0; y < rows.size(); ++y)
					if (rows[y].startsWith(QStringLiteral("●"))) return y;
			} else {
				return int(rows.value(0).indexOf(QStringLiteral("●")));
			}
			return -1;
		};
		const int v_min = thumb_row(0, Qt::Vertical, false);
		const int v_max = thumb_row(100, Qt::Vertical, false);
		CHECK(v_min > v_max && v_max == 0,
		      "a vertical slider puts its minimum at the bottom");
		// The horizontal one is left alone, and inverting it is the same
		// question with the same answer -- the flag, not the orientation.
		const int h_min = thumb_row(0, Qt::Horizontal, false);
		const int h_max = thumb_row(100, Qt::Horizontal, false);
		const int h_inv_min = thumb_row(0, Qt::Horizontal, true);
		CHECK(h_min < h_max && h_min == 0 && h_inv_min == h_max,
		      "while a horizontal one runs left to right, or inverted if asked");
		GridGuard::reset();
	}

	// A VERTICAL indeterminate progress bar, the other orientation section 0a
	// named. Paired the way the horizontal check above is: against a bar whose
	// length IS known, so "shows no percentage" is not satisfied by a style
	// that never shows one.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *busy = new QProgressBar(&host);
		busy->setOrientation(Qt::Vertical);
		busy->setRange(0, 0);
		busy->setGeometry(0, 0, cw * 2, ch * 6);
		auto *known = new QProgressBar(&host);
		known->setOrientation(Qt::Vertical);
		known->setRange(0, 100);
		known->setValue(40);
		// Three cells, not two. "40%" is three characters, and at two the
		// only reason this ever passed was that Channel A had no bound and
		// wrote the third into the widget beside it. Bounding the style to
		// its widget turned that into a truncated "40", which is the honest
		// rendering of a two-cell bar -- so the fixture is what was wrong,
		// and the claim it makes needs a bar wide enough to make it.
		known->setGeometry(cw * 4, 0, cw * 3, ch * 6);
		host.resize(GridMetrics::cells(8, 7));
		host.show();
		QCoreApplication::processEvents();
		CellBuffer b(8, 7);
		render_once(host, b);
		const QString frame = b.to_text();
		// The busy bar's OWN columns, not the whole frame: the first version
		// asked that the frame does not contain "0%", and the bar beside it
		// says "40%".
		QString busy_col;
		for (const QString &line : frame.split(QLatin1Char('\n')))
			busy_col += line.left(2);
		bool digit = false;
		for (QChar ch2 : busy_col) if (ch2.isDigit()) digit = true;
		CHECK(busy_col.contains(QStringLiteral("▒")) && !digit,
		      "a vertical indeterminate bar shades and shows no percentage");
		CHECK(frame.contains(QStringLiteral("40%")),
		      "while one whose length is known still says it");
		GridGuard::reset();
	}



	// Wide clusters through the widgets, which nothing had rendered. The
	// elision helper counts cells and was tested for it, but that is Channel A
	// -- GridStyle writing clusters into cells. Channel B places glyphs by
	// PIXEL position, and a wide cluster is not two cells wide in pixels.
	//
	// Measured on this machine: 'M' advances 10.0, exactly one cell, and a CJK
	// character advances **16.0**, not 20. Three of them end at pixel 48 where
	// six cells end at 60, so Qt starts the next run at 48 -- inside the third
	// cluster's own cells -- and a QLineEdit holding CJK followed by Latin
	// **lost a character**: written, then overwritten.
	//
	// drawTextItem() continues a run from where the last one ended in cells.
	// The assertion is the cell contents rather than the joined text, because
	// to_text() cannot show a width-2 cluster sitting in one cell and the
	// widths summing wrong is exactly the corruption.
	{
		const QString cjk = QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
		QWidget d;
		d.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&d);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		// The fixture that reproduced it, kept whole rather than reduced: a
		// two-widget version of this passed with the fix REMOVED, because the
		// collision depends on where Qt puts each run and that moves with the
		// layout. A reduction that stops reproducing is not a reduction.
		const QString mixed = QString::fromUtf8("ab\xe6\x97\xa5\xe6\x9c\xac cd");
		v->addWidget(new QLabel(cjk));                       // Channel A
		v->addWidget(new QPushButton(mixed));
		v->addWidget(new QLineEdit(cjk + QStringLiteral("xy")));   // Channel B
		auto *lw = new QListWidget;
		lw->setFrameShape(QFrame::NoFrame);
		lw->addItem(mixed);
		v->addWidget(lw);
		auto *tabs = new QTabBar;
		tabs->addTab(cjk);
		v->addWidget(tabs);
		d.resize(GridMetrics::cells(20, 10));
		d.show();
		QCoreApplication::processEvents();
		CellBuffer b(20, 10);
		render_once(d, b);
		// Every row still spans exactly the buffer: a width-2 cluster written
		// into one cell breaks this sum, and nothing in the text would show it.
		bool spans = true;
		for (int y = 0; y < b.rows(); ++y) {
			int sum = 0;
			for (int x = 0; x < b.cols(); ++x) sum += b.at(x, y).width;
			if (sum != b.cols()) spans = false;
		}
		// And the edit's row carries all three clusters, each two cells wide.
		int wide_on_edit = 0;
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, 2).width == 2) ++wide_on_edit;
		// True and worth holding, but NOT the discriminating half: under the
		// sabotage this stayed green, because the overwrite replaces a lead
		// cell with a one-cell glyph and leaves the stray continuation, so the
		// sum still comes to the buffer's width. The count below is what fails.
		CHECK(spans, "wide clusters leave every row spanning its full width");
		CHECK(wide_on_edit == 3,
		      "and a line edit keeps every one of them, not the ones that fit "
		      "the font's advance");
		GridGuard::reset();
	}



	// ---- focus is visible, for every widget that can take it ----
	{
		// A keyboard user in a terminal has no pointer to hover with and no
		// window manager to tell them which control is live. Measured
		// 2026-09-01, with focus moved to each widget in turn and the whole
		// frame compared cell by cell INCLUDING attributes: only the push
		// button changed anything it owned. What hid it was the terminal's
		// hardware cursor, which landed on every widget because design.md
		// 5.5's test for "has a caret" -- a valid ImCursorRectangle -- is one
		// every QWidget answers.
		//
		// This asserts the property a user has, not the mechanism: a focused
		// widget is distinguishable, by its own cells or by the cursor. The
		// line edit is distinguished by the cursor alone and that is correct,
		// so a check demanding changed cells everywhere would have to make an
		// exception for it and would then be asserting the implementation.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);

		struct Row { const char *what; QWidget *w; };
		QVector<Row> rows;
		auto add = [&](const char *what, QWidget *w) {
			v->addWidget(w);
			rows.append(Row{ what, w });
		};
		auto *anchor = new QPushButton(QStringLiteral("Anchor"));
		add("push button", anchor);
		add("line edit",   new QLineEdit(QStringLiteral("text")));
		add("check box",   new QCheckBox(QStringLiteral("Wrap")));
		add("radio",       new QRadioButton(QStringLiteral("One")));
		auto *combo = new QComboBox;
		combo->addItems({ QStringLiteral("one"), QStringLiteral("two") });
		add("combo box",   combo);
		add("spin box",    new QSpinBox);
		add("slider",      new QSlider(Qt::Horizontal));
		auto *list = new QListWidget;
		list->addItems({ QStringLiteral("alpha"), QStringLiteral("beta") });
		list->setFixedHeight(3 * GridMetrics::ch());
		add("list widget", list);
		auto *tabs = new QTabWidget;
		tabs->addTab(new QWidget, QStringLiteral("One"));
		tabs->addTab(new QWidget, QStringLiteral("Two"));
		tabs->setFixedHeight(3 * GridMetrics::ch());
		add("tab widget",  tabs);
		auto *bar = new QScrollBar(Qt::Horizontal);
		bar->setRange(0, 100);
		add("scroll bar",  bar);

		win.resize(GridMetrics::cells(24, 18));
		win.show();
		QCoreApplication::processEvents();

		InputRouter router(&win);
		Compositor comp(&win, &router);

		// Glyphs AND attributes, per row. An earlier version compared
		// to_text(), which carries glyphs only -- and focus is spelled with
		// Attr::Reverse, which to_text() cannot see, so every widget came
		// back "identical" including the push button that already had a
		// focus rendering.
		auto shoot = [&](QWidget *focus, QString *out_cursor) {
			focus->setFocus();
			// Asked of the WINDOW, which is what Application and InputRouter
			// do. A QTabWidget forwards focus to its tab bar through a focus
			// proxy, so taking the widget setFocus was called on named one
			// the style never draws.
			set_focus_widget(win.focusWidget());
			QCoreApplication::processEvents();
			CellBuffer b(24, 18);
			comp.compose(b);
			*out_cursor = comp.cursor_cell()
			    ? QStringLiteral("%1,%2").arg(comp.cursor_cell()->x())
			                             .arg(comp.cursor_cell()->y())
			    : QStringLiteral("none");
			QVector<QString> sig;
			for (int y = 0; y < b.rows(); ++y) {
				QString line;
				for (int x = 0; x < b.cols(); ++x) {
					const Cell &c = b.at(x, y);
					line += c.ch.isEmpty() ? QStringLiteral(".") : c.ch;
					if (c.attrs & Attr::Reverse)   line += QLatin1Char('R');
					if (c.attrs & Attr::Bold)      line += QLatin1Char('B');
					if (c.attrs & Attr::Underline) line += QLatin1Char('U');
					if (c.attrs & Attr::Dim)       line += QLatin1Char('D');
				}
				sig.append(line);
			}
			return sig;
		};

		QString base_cur;
		const QVector<QString> base = shoot(anchor, &base_cur);
		int invisible = 0;
		for (const Row &r : rows) {
			if (r.w == anchor) continue;
			QString cur;
			const QVector<QString> shot = shoot(r.w, &cur);
			// Restricted to the rows the widget itself occupies, so that
			// "something changed" cannot be satisfied by the ANCHOR losing
			// its own highlight -- which happens on every one of these and
			// would make the check pass whatever the widget did.
			const QRect g(r.w->mapTo(&win, QPoint()), r.w->size());
			bool own = false;
			for (int y = g.top() / GridMetrics::ch();
			     y <= g.bottom() / GridMetrics::ch() && y < shot.size(); ++y)
				if (base[y] != shot[y]) own = true;
			if (!own && cur == base_cur) {
				printf("FAIL: every widget that can take focus shows that"
				       " it has it -- focus on a %s shows nothing at"
				       " all\n", r.what);
				++invisible;
			}
		}
		fails += invisible;
		if (!invisible)
			printf("PASS: every widget that can take focus shows that it has it\n");

		// And the cursor is a caret, not a focus marker. It says "type here",
		// and a screen reader says so out loud; parking it in the middle of a
		// slider's track is a statement, not a decoration.
		int stray = 0;
		for (const Row &r : rows) {
			QString cur;
			shoot(r.w, &cur);
			const bool edits = r.w->testAttribute(Qt::WA_InputMethodEnabled);
			if (!edits && cur != QStringLiteral("none")) {
				printf("FAIL: and the terminal's cursor goes only where text"
				       " is edited -- a %s is not a text field and got it"
				       " at %s\n", r.what, qPrintable(cur));
				++stray;
			}
			if (edits && cur == QStringLiteral("none")) {
				printf("FAIL: and the terminal's cursor goes only where text"
				       " is edited -- a %s edits text and got none\n",
				       r.what);
				++stray;
			}
		}
		fails += stray;
		if (!stray)
			printf("PASS: and the terminal's cursor goes only where text is edited\n");
		GridGuard::reset();
	}


	// ---- the terminal cursor sits where typing goes ----
	{
		// Measured 2026-09-01: it sat one cell to the LEFT of that, on the
		// character before the caret, and on a spin box it sat on the
		// bracket. What Qt returns from ImCursorRectangle is the caret's
		// REPAINT rectangle rather than the caret -- a QLineEdit inflates it
		// five pixels either side so a redraw covers the glyph beside it --
		// and the compositor was reading its top-left corner.
		//
		// Asserted as relationships, because the absolute column depends on
		// the bracket the style draws and on the font: the caret at the start
		// of the text is on the first text cell, the caret at the end is one
		// cell past the last, and each step of one character is one cell.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *le = new QLineEdit(QStringLiteral("abcdef"));
		v->addWidget(le);
		// One row exactly. At two, the layout has 38 pixels for a 19-pixel
		// field whose vertical policy is Fixed, centres it at y = 9, and the
		// grid guard reports the fixture rather than the code.
		win.resize(GridMetrics::cells(12, 1));
		win.show();
		QCoreApplication::processEvents();
		InputRouter router(&win);
		Compositor comp(&win, &router);

		QVector<int> col;
		int first = -1, last = -1;
		for (int at = 0; at <= 6; ++at) {
			le->setCursorPosition(at);
			le->setFocus();
			set_focus_widget(win.focusWidget());
			QCoreApplication::processEvents();
			CellBuffer b(12, 1);
			comp.compose(b);
			if (at == 0)
				for (int x = 0; x < b.cols(); ++x) {
					const QString &g = b.at(x, 0).ch;
					if (g == QStringLiteral("a")) first = x;
					if (g == QStringLiteral("f")) last = x;
				}
			col.append(comp.cursor_cell() ? comp.cursor_cell()->x() : -1);
		}
		// The fixture has to have drawn the text, or every claim below is
		// about a blank row. "abcdef" is six distinct letters for exactly
		// this reason -- a repeated one would make "the last f" ambiguous.
		CHECK(first >= 0 && last == first + 5,
		      "the field drew its six characters before the caret was asked about");
		CHECK(col.value(0) == first,
		      "the caret at the start of the text is on the first character's cell");
		CHECK(col.value(6) == last + 1,
		      "and at the end it is one cell past the last, where typing goes");
		bool step = true;
		for (int i = 1; i < col.size(); ++i)
			if (col[i] != col[i - 1] + 1) step = false;
		CHECK(step, "and one character of movement is one cell of movement");
	}
	{
		// A spin box reaches the same code by a different road: it forwards
		// the query to its inner editor verbatim, so the rectangle arrives in
		// the editor's coordinates and the compositor has to find which
		// widget answered. Its editor reported 10x20+-3+0 -- a rectangle
		// beginning three pixels outside the spin box -- and the left-corner
		// reading put the cursor on the opening bracket.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *spin = new QSpinBox;
		spin->setRange(0, 999);
		spin->setValue(42);
		v->addWidget(spin);
		win.resize(GridMetrics::cells(12, 1));
		win.show();
		QCoreApplication::processEvents();
		InputRouter router(&win);
		Compositor comp(&win, &router);
		spin->setFocus();
		set_focus_widget(win.focusWidget());
		QCoreApplication::processEvents();
		CellBuffer b(12, 1);
		comp.compose(b);
		int digit = -1;
		for (int x = 0; x < b.cols() && digit < 0; ++x)
			if (b.at(x, 0).ch == QStringLiteral("4")) digit = x;
		CHECK(digit > 0, "a spin box drew its value inside its brackets");
		CHECK(comp.cursor_cell() && comp.cursor_cell()->x() == digit,
		      "and its cursor is in the field rather than on the bracket");
	}


	// ---- disabled looks disabled, and says so once ----
	{
		// Measured 2026-09-01, thirteen widgets disabled one at a time. Both
		// channels marked the state and they marked it DIFFERENTLY: GridStyle
		// wrote Attr::Dim and left the colour alone, while everything drawn
		// through QPainter -- a label's text, a field's contents, a check
		// box's own label, a list's rows -- came out as a hard 24-bit
		// #bebebe with no attribute. role_of() asked the palette's current
		// colour group only, so the Disabled group matched no role and fell
		// through as "a colour the application chose".
		//
		// #bebebe is Fusion's grey for a light desktop: nearly invisible on a
		// light terminal, brighter than ordinary text on a dark one, so
		// "disabled" read as "emphasised" on half the terminals in use.
		//
		// The first version of this probe recorded the glyph and the Dim
		// attribute and not the colour, and reported that a disabled QLabel
		// and QLineEdit changed NOTHING. They changed colour. A signature
		// that cannot see the field the bug is in reports the fixture.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);

		struct Row { const char *what; QWidget *w; };
		QVector<Row> rows;
		auto add = [&](const char *what, QWidget *w) {
			v->addWidget(w);
			rows.append(Row{ what, w });
		};
		add("push button", new QPushButton(QStringLiteral("OK")));
		add("line edit",   new QLineEdit(QStringLiteral("text")));
		auto *cb = new QCheckBox(QStringLiteral("Wrap"));
		cb->setChecked(true);
		add("check box",   cb);
		add("radio",       new QRadioButton(QStringLiteral("One")));
		auto *combo = new QComboBox;
		combo->addItems({ QStringLiteral("one"), QStringLiteral("two") });
		add("combo box",   combo);
		add("spin box",    new QSpinBox);
		auto *sl = new QSlider(Qt::Horizontal);
		sl->setValue(40);
		add("slider",      sl);
		auto *pb = new QProgressBar;
		pb->setValue(40);
		add("progress",    pb);
		auto *bar = new QScrollBar(Qt::Horizontal);
		bar->setRange(0, 100);
		add("scroll bar",  bar);
		auto *tool = new QToolBar;
		tool->addAction(QStringLiteral("Cut"));
		add("tool bar",    tool);
		auto *list = new QListWidget;
		list->addItems({ QStringLiteral("alpha"), QStringLiteral("beta") });
		list->setFixedHeight(3 * GridMetrics::ch());
		add("list widget", list);

		win.resize(GridMetrics::cells(24, 14));
		win.show();
		QCoreApplication::processEvents();

		// glyphs, undimmed, true-coloured -- over the rows one widget owns.
		auto survey = [&](const QRect &g, int *glyphs, int *undimmed, int *rgb) {
			QCoreApplication::processEvents();
			CellBuffer b(24, 14);
			render_once(win, b);
			*glyphs = *undimmed = *rgb = 0;
			for (int y = g.top() / GridMetrics::ch();
			     y <= g.bottom() / GridMetrics::ch() && y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x) {
					const Cell &c = b.at(x, y);
					if (c.ch.isEmpty() || c.ch == QStringLiteral(" ")) continue;
					++*glyphs;
					if (!(c.attrs & Attr::Dim)) ++*undimmed;
					if (c.fg.kind() == Color::Rgb) ++*rgb;
				}
		};

		int bad_dim = 0, bad_rgb = 0, bad_enabled = 0;
		for (const Row &r : rows) {
			const QRect g(r.w->mapTo(&win, QPoint()), r.w->size());
			int glyphs = 0, undimmed = 0, rgb = 0;

			// The paired half: while it is ENABLED, nothing is dim. Without
			// it, a library that dimmed everything unconditionally would
			// satisfy every claim below.
			survey(g, &glyphs, &undimmed, &rgb);
			if (glyphs == 0 || undimmed != glyphs) {
				printf("FAIL: an enabled widget is drawn at full brightness"
				       " -- a %s has %d of %d glyphs dim\n",
				       r.what, glyphs - undimmed, glyphs);
				++bad_enabled;
			}

			r.w->setEnabled(false);
			survey(g, &glyphs, &undimmed, &rgb);
			r.w->setEnabled(true);
			if (glyphs == 0 || undimmed != 0) {
				printf("FAIL: and every cell of a disabled one is dim, both"
				       " channels -- a %s has %d of %d undimmed\n",
				       r.what, undimmed, glyphs);
				++bad_dim;
			}
			// The half that found the bug. Disabling must not turn a themed
			// colour into a literal one: section 6 spends true colour only on
			// a colour no palette role explains, and the Disabled group's
			// grey has a role like any other.
			if (rgb != 0) {
				printf("FAIL: and disabling spends no true colour -- a %s"
				       " spends it on %d cell(s)\n", r.what, rgb);
				++bad_rgb;
			}
		}
		fails += bad_enabled + bad_dim + bad_rgb;
		if (!bad_enabled)
			printf("PASS: an enabled widget is drawn at full brightness\n");
		if (!bad_dim)
			printf("PASS: and every cell of a disabled one is dim, both channels\n");
		if (!bad_rgb)
			printf("PASS: and disabling spends no true colour\n");
		GridGuard::reset();
	}


	// ---- a control draws inside the widget it was given ----
	{
		// Measured 2026-09-01 over twelve widget kinds at six sizes each,
		// with each widget's minimum cleared first so the rectangle asked
		// for is the rectangle it got -- the first version of the probe did
		// not clear it, and every overdraw it reported was setGeometry()
		// clamping to the minimum and the probe comparing against the wrong
		// rectangle.
		//
		// What it found: a one-cell QPushButton wrote "<OK>" and put three
		// cells of it in whatever sat beside it, a one-row QGroupBox spent
		// twelve cells outside itself, a QTabBar drew its tabs at their own
		// widths whatever the bar's width was. Section 7.7 had one instance
		// of this recorded as a fault in its own right; it is one fault, and
		// CellBuffer's clip is the bound rather than a dozen corrections.
		//
		// These are the kinds Channel A alone draws, so the bound is the
		// whole answer for them. Four others still overdraw through Channel
		// B -- a check box's and a radio's own label, a combo box, a group
		// box, and a list view's frame -- and section 7.8 carries that as
		// the next piece rather than this check pretending otherwise.
		const int cols = 24, rows = 8;
		struct Case { const char *what; std::function<QWidget *()> make; };
		QVector<Case> cases;
		auto one = [&](const char *what, std::function<QWidget *()> make) {
			cases.append(Case{ what, make });
		};
		one("push button", [] { return new QPushButton(QStringLiteral("OK")); });
		one("line edit", [] { return new QLineEdit(QStringLiteral("text")); });
		one("spin box", [] { return new QSpinBox; });
		one("slider", [] { return new QSlider(Qt::Horizontal); });
		one("progress", [] {
			auto *p = new QProgressBar;
			p->setValue(40);
			return p;
		});
		one("scroll bar", [] { return new QScrollBar(Qt::Horizontal); });
		// The four that used to leak one cell of label into the widget next
		// door, and the list whose scroll bar landed on the row above. Both
		// causes are fixed -- an off-by-one in the engine's clip rounding and
		// a bound that stopped at the widget instead of its ancestors -- so
		// they belong in the same list as the rest rather than in a comment
		// explaining why they are exempt.
		one("check box", [] { return new QCheckBox(QStringLiteral("Wrap")); });
		one("radio", [] { return new QRadioButton(QStringLiteral("One")); });
		one("combo box", [] {
			auto *c = new QComboBox;
			c->addItem(QStringLiteral("one"));
			return c;
		});
		one("group box", [] { return new QGroupBox(QStringLiteral("Box")); });
		one("list widget", [] {
			auto *l = new QListWidget;
			l->addItems({ QStringLiteral("alpha"), QStringLiteral("beta") });
			return l;
		});

		const QVector<QSize> sizes = { QSize(1, 1), QSize(2, 1), QSize(3, 1),
			                           QSize(1, 2), QSize(2, 2), QSize(6, 1) };
		int leaked = 0, drew = 0;
		for (const Case &c : cases) {
			for (const QSize &sz : sizes) {
				QWidget host;
				host.setAttribute(Qt::WA_DontShowOnScreen);
				host.resize(GridMetrics::cells(cols, rows));
				host.show();
				QCoreApplication::processEvents();
				CellBuffer empty(cols, rows);
				render_once(host, empty);

				QWidget *w = c.make();
				w->setParent(&host);
				w->setMinimumSize(0, 0);
				// Two cells in and two down, so an overdraw has room to show
				// on every side rather than falling off the buffer, where
				// CellBuffer would absorb it and the check would see nothing.
				w->setGeometry(2 * GridMetrics::cw(), 2 * GridMetrics::ch(),
				               sz.width() * GridMetrics::cw(),
				               sz.height() * GridMetrics::ch());
				w->show();
				QCoreApplication::processEvents();
				CellBuffer b(cols, rows);
				render_once(host, b);

				const QRect g = w->geometry();
				const QRect own(g.x() / GridMetrics::cw(), g.y() / GridMetrics::ch(),
				                g.width() / GridMetrics::cw(),
				                g.height() / GridMetrics::ch());
				int inside = 0, outside = 0;
				for (int y = 0; y < rows; ++y)
					for (int x = 0; x < cols; ++x) {
						const Cell &n = b.at(x, y), &o = empty.at(x, y);
						if (n.ch == o.ch && n.attrs == o.attrs) continue;
						if (own.contains(QPoint(x, y))) ++inside; else ++outside;
					}
				if (outside) {
					printf("FAIL: and none of them wrote a cell outside its"
					       " own rectangle -- a %dx%d %s wrote %d\n",
					       sz.width(), sz.height(), c.what, outside);
					++leaked;
				}
				drew += inside;
			}
			GridGuard::reset();
		}
		fails += leaked;
		// The paired half. Every one of these could satisfy "drew nothing
		// outside itself" by drawing nothing at all, and three of the twelve
		// kinds measured DO come out blank at some sizes -- so a check
		// without this would be green on a style that had stopped working.
		CHECK(drew >= 60, "the widgets that must not overdraw drew something");
		if (!leaked)
			printf("PASS: and none of them wrote a cell outside its own rectangle\n");
	}


	// ---- a frame that does not fit is not drawn somewhere it does ----
	{
		// The clip on CellBuffer bounded put_cluster(), text() and fill() and
		// left draw_box() alone, because draw_box() writes through at() --
		// the RAW accessor, deliberately unclipped, since reads use it too.
		// So every box in the library stayed unbounded, and a probe found it
		// at once: a QGroupBox six cells wide and one row TALL drew a
		// complete twelve-cell box on the two rows below itself, because
		// subControlRect hands it a frame rect needing a height it does not
		// have. The clip had caught the group box's title and not its frame.
		const int cols = 16, rows = 6;
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(cols, rows));
		host.show();
		QCoreApplication::processEvents();
		CellBuffer empty(cols, rows);
		render_once(host, empty);

		auto *box = new QGroupBox(QStringLiteral("Box"), &host);
		box->setMinimumSize(0, 0);
		box->setGeometry(2 * GridMetrics::cw(), 2 * GridMetrics::ch(),
		                 6 * GridMetrics::cw(), 1 * GridMetrics::ch());
		box->show();
		QCoreApplication::processEvents();
		CellBuffer b(cols, rows);
		render_once(host, b);

		// Box-drawing characters specifically, on the rows the widget does
		// not own. Counting every changed cell would fold in the one-cell
		// text overhang that CellPaintEngine still produces by its own
		// stated rule -- a different question, in section 7.2 -- and this
		// check would then be about two things and diagnose neither.
		int frame_outside = 0, frame_inside = 0;
		for (int y = 0; y < rows; ++y)
			for (int x = 0; x < cols; ++x) {
				const QString &g = b.at(x, y).ch;
				if (g.isEmpty()) continue;
				const char32_t u = g.at(0).unicode();
				if (u < 0x2500 || u > 0x257f) continue;      // Box Drawing
				if (y == 2) ++frame_inside; else ++frame_outside;
			}
		CHECK(frame_outside == 0,
		      "a group box too short for a frame draws none of it elsewhere");
		// x() >= 0, not !isNull(): findText returns {-1,-1} when it finds
		// nothing, and QPoint::isNull() asks whether both are ZERO -- so the
		// obvious spelling is true whether the text is there or not.
		CHECK(findText(b, QStringLiteral("Box")).x() >= 0,
		      "and still says its name, which is the part that fits");
		(void)frame_inside;

		// The pairing, and the first attempt at it was not one. "Still says
		// its name" survives draw_box() drawing nothing at all, because a
		// group box's title comes through Channel B -- sabotaging draw_box
		// to a no-op left that check green. What pairs with "no frame
		// outside" is a frame INSIDE, at a size with room for one.
		box->setGeometry(2 * GridMetrics::cw(), 2 * GridMetrics::ch(),
		                 6 * GridMetrics::cw(), 3 * GridMetrics::ch());
		QCoreApplication::processEvents();
		CellBuffer tall(cols, rows);
		render_once(host, tall);
		int frame_seen = 0;
		for (int y = 0; y < rows; ++y)
			for (int x = 0; x < cols; ++x) {
				const QString &g = tall.at(x, y).ch;
				if (g.isEmpty()) continue;
				const char32_t u = g.at(0).unicode();
				if (u >= 0x2500 && u <= 0x257f) ++frame_seen;
			}
		CHECK(frame_seen >= 8, "while one with room for a frame draws one");
	}


	// ---- a child that does not fit is clipped by its parent ----
	{
		// On a pixel screen a parent clips its children; nothing here did.
		// Measured on a QListWidget six cells wide and ONE ROW tall: Qt gives
		// its horizontal scroll bar y = -10 inside the list -- there is no
		// room, so the layout puts it above the top edge -- and its arrows
		// and thumb landed on the row above, over whatever widget was there.
		//
		//   QWidget    geom 30x19+10+-10   in host +30+28   rows 1..2
		//   QScrollBar geom 30x19+0+0      in host +30+28   rows 1..2
		//
		// Qt is not wrong to place it there. A scroll bar that does not fit
		// has to go somewhere, and on a screen the parent's clip makes the
		// question moot.
		const int cols = 16, rows = 6;
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(cols, rows));
		host.show();
		QCoreApplication::processEvents();
		CellBuffer empty(cols, rows);
		render_once(host, empty);

		auto *list = new QListWidget(&host);
		list->addItems({ QStringLiteral("alpha"), QStringLiteral("beta") });
		list->setMinimumSize(0, 0);
		list->setGeometry(2 * GridMetrics::cw(), 2 * GridMetrics::ch(),
		                  6 * GridMetrics::cw(), 1 * GridMetrics::ch());
		list->show();
		QCoreApplication::processEvents();
		CellBuffer b(cols, rows);
		render_once(host, b);

		// Row 1 specifically -- the row the scroll bar reached -- rather than
		// "anything outside", which would also count the one-cell text
		// overhang CellPaintEngine produces by its own rule. A check that
		// folds in two faults diagnoses neither.
		int above = 0;
		for (int x = 0; x < cols; ++x)
			if (b.at(x, 1).ch != empty.at(x, 1).ch) ++above;
		CHECK(above == 0, "a child with no room in its parent draws above it");

		// Paired: with room, the same list DOES draw. Otherwise a style that
		// had stopped drawing item views entirely would satisfy the above.
		list->setGeometry(2 * GridMetrics::cw(), 2 * GridMetrics::ch(),
		                  6 * GridMetrics::cw(), 3 * GridMetrics::ch());
		QCoreApplication::processEvents();
		CellBuffer room(cols, rows);
		render_once(host, room);
		int drew = 0;
		for (int y = 0; y < rows; ++y)
			for (int x = 0; x < cols; ++x)
				if (room.at(x, y).ch != empty.at(x, y).ch) ++drew;
		CHECK(drew >= 8, "while one with room draws itself");
		GridGuard::reset();
	}

	// ---- a drop-down draws every item, not just the first ----
	{
		// A regression introduced the same day, by the clip that bounds a
		// style's drawing to the widget it was given. QComboMenuDelegate
		// paints the drop-down's items and hands the style the COMBO BOX --
		// not the popup it is painting into -- so a one-row combo clipped its
		// own four-row drop-down to one row. Measured: three blank lines
		// where "two", "three" and "four" should have been.
		//
		// The clip now asks the paint device which widget is being painted,
		// the same question cells_of() has always asked. Nothing checked a
		// drop-down's contents before, which is why the clip could break it
		// and the suite stay green.
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }

		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *combo = new QComboBox;
		combo->addItems({ QStringLiteral("one"), QStringLiteral("two"),
			              QStringLiteral("three"), QStringLiteral("four") });
		v->addWidget(combo);
		win.resize(GridMetrics::cells(20, 8));
		win.show();
		QCoreApplication::processEvents();

		Qtty::InputRouter router(&win);
		Qtty::Compositor comp(&win, &router);
		combo->showPopup();
		QCoreApplication::processEvents();
		CellBuffer b(20, 8);
		comp.compose(b);
		const QString frame = b.to_text();
		int seen = 0;
		for (const QString &item : { QStringLiteral("two"),
			                         QStringLiteral("three"),
			                         QStringLiteral("four") })
			if (frame.contains(item)) ++seen;
		// "one" is in the closed combo as well as the popup, so the three
		// that are only ever in the popup are what the count is over.
		CHECK(seen == 3, "an open drop-down draws the items below the first");
		combo->hidePopup();
		QCoreApplication::processEvents();
		win.hide();
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	{
		// Partial-line scrolling, which section 7.2 listed as the one thing
		// text widgets that nothing exercised. It does the right thing, and
		// this records what that is: content moves in WHOLE CELLS, and a
		// scroll of part of a line shows the same frame as the line it is
		// part of. Measured on a 12x5 edit with a 19-pixel cell, the
		// scrollbar counting pixels with a 20-pixel step:
		//
		//     scroll 0        line0..line4
		//     scroll 1        unchanged -- a pixel is not a cell
		//     scroll 20       line1..line5
		//     scroll 21,26,29 unchanged from 20
		//     scroll 33       line2..line6
		//
		// Nothing tears and no row is lost; the flip happens once the offset
		// passes the half cell, which is the rounding rule the clip and the
		// snap both use.
		QTextEdit edit;
		edit.setAttribute(Qt::WA_DontShowOnScreen);
		edit.setFrameShape(QFrame::NoFrame);
		QString doc;
		for (int i = 0; i < 20; ++i) doc += QStringLiteral("line%1\n").arg(i);
		edit.setPlainText(doc);
		edit.resize(GridMetrics::cells(12, 5));
		edit.show();
		QCoreApplication::processEvents();
		QScrollBar *sb = edit.verticalScrollBar();

		const auto rows = [&] {
			CellBuffer b(12, 5);
			render_once(edit, b);
			return b.to_text();
		};
		const QString top = rows();
		sb->setValue(sb->singleStep());
		QCoreApplication::processEvents();
		const QString line = rows();
		// The control, and it is the half that matters: without it, "a part
		// of a line changes nothing" passes against an edit that does not
		// scroll at all.
		CHECK(line != top, "a whole line scrolls a text edit");
		sb->setValue(sb->singleStep() + 1);
		QCoreApplication::processEvents();
		CHECK(rows() == line,
		      "and a scroll of part of a line shows the same cells");
		GridGuard::reset();
	}

	// Can a keyboard user SEE where focus is? That is the whole premise of
	// driving a form without a mouse, and it is not a property of any one
	// widget: PE_FrameFocusRect is suppressed outright -- "focus by the
	// router-owned focus attr" -- and the mark is then drawn per control
	// type, in five places, each asking whether it owns the router's focus.
	// So focus visibility is OPT-IN, and a control the style does not handle
	// has none at all.
	//
	// Swept rather than sampled, and asserted as a PARTITION rather than as a
	// count: the set of standard widgets whose focus is invisible must be
	// exactly {QDial}. A widget that stops showing focus fails this, and so
	// does a QDial that starts showing it -- the second being a message to
	// whoever fixed it that the exception can go, rather than a check quietly
	// passing for a new reason.
	//
	// QDial WAS the exception and is not any more. Fusion's dial never asks
	// for a focus indicator at all -- measured by making PE_FrameFocusRect
	// draw a mark, which changed every other widget and left the dial
	// byte-identical -- so it needed a control implementation of its own, and
	// has one now: a slider's groove and handle, the circle being the single
	// part of a dial that a cell grid cannot show.
	{
		struct Case { const char *name; std::function<QWidget *()> make; };
		const QVector<Case> cases = {
			{"QPushButton",    [] { return new QPushButton(QStringLiteral("Press")); }},
			{"QCheckBox",      [] { return new QCheckBox(QStringLiteral("Tick")); }},
			{"QRadioButton",   [] { return new QRadioButton(QStringLiteral("Pick")); }},
			{"QLineEdit",      [] { return new QLineEdit(QStringLiteral("text")); }},
			{"QComboBox",      [] { auto *c = new QComboBox; c->addItems({"one", "two"}); return c; }},
			{"QSpinBox",       [] { return new QSpinBox; }},
			{"QSlider",        [] { return new QSlider(Qt::Horizontal); }},
			{"QToolButton",    [] { auto *t = new QToolButton; t->setText(QStringLiteral("Tool")); return t; }},
			{"QListWidget",    [] { auto *l = new QListWidget; l->addItems({"a", "b", "c"}); return l; }},
			{"QTreeWidget",    [] { auto *t = new QTreeWidget; t->setColumnCount(1); new QTreeWidgetItem(t, QStringList{"leaf"}); return t; }},
			{"QTableWidget",   [] { return new QTableWidget(2, 2); }},
			{"QTabBar",        [] { auto *t = new QTabBar; t->addTab(QStringLiteral("one")); t->addTab(QStringLiteral("two")); return t; }},
			{"QPlainTextEdit", [] { return new QPlainTextEdit(QStringLiteral("body")); }},
			{"QTextEdit",      [] { return new QTextEdit(QStringLiteral("body")); }},
			{"QDateEdit",      [] { return new QDateEdit; }},
			{"QDial",          [] { return new QDial; }},
		};
		QStringList blind, colour_only;
		bool all_took = true;
		for (const Case &c : cases) {
			const auto shot = [&](bool focused) {
				QWidget host;
				host.setAttribute(Qt::WA_DontShowOnScreen);
				auto *box = new QVBoxLayout(&host);
				QWidget *w = c.make();
				box->addWidget(w);
				host.resize(GridMetrics::cells(30, 6));
				host.show();
				QCoreApplication::processEvents();
				if (focused) {
					set_focus_widget(w);
					QCoreApplication::processEvents();
					// The fixture has to REACH the hazard: a focus that did
					// not take renders identically for a reason that has
					// nothing to do with the style.
					if (Qtty::focusWidget() != w) all_took = false;
				}
				return Qtty::test::snapshot_of(host, 30, 6);
			};
			const QString off = shot(false), on = shot(true);
			if (off == on) blind.append(QString::fromLatin1(c.name));
			// What a MONO terminal can still show: the glyphs and the SGR
			// attributes. Colour depth removes colour; reverse, bold and
			// underline survive at every depth, so a mark that lives only in
			// the colours is one a mono user cannot see at all.
			const auto mono_sees = [](const QString &snap) {
				const int cut = snap.indexOf(QStringLiteral("--- colours ---"));
				return cut < 0 ? snap : snap.left(cut);
			};
			if (mono_sees(off) == mono_sees(on))
				colour_only.append(QString::fromLatin1(c.name));
		}
		// The POPULATION, pinned beside the partition, which this check did
		// not do and both of its elders do: suite_theme and suite_graphics
		// each assert `sizeof(list)/sizeof(list[0]) == 6` next to their own
		// "each of the six" claim. Without it a sweep that says "every
		// standard widget" goes on passing while widgets are quietly taken
		// OUT of the list -- the assertion under the quantifier holding while
		// the quantifier shrinks, which is `evidence.md`'s whole point about
		// names that claim exhaustiveness.
		CHECK(cases.size() == 16,
		      "the focus sweep covers sixteen standard widgets, a count that"
		      " cannot shrink without this saying so");
		CHECK(all_took,
		      "every widget in the focus sweep actually took focus, so an"
		      " identical render means the style and not the fixture");
		// EMPTY, and it was {QDial} until the dial was given a control
		// implementation of its own. The partition is what forced that: it
		// fails when a widget stops showing focus AND when the known
		// exception starts showing it, so closing the gap could not leave a
		// stale allowance behind, quietly passing for a new reason.
		CHECK(blind.isEmpty(),
		      "every standard widget draws a focus mark, so a keyboard user"
		      " can always see where they are");
		// And the same partition at the depth where it is hardest. Mono is a
		// depth qtty negotiates -- TERM=dumb reaches it, and QTTY_COLOR=mono
		// asks for it -- so a focus mark that is only a colour leaves a
		// keyboard user with nothing.
		//
		// QLineEdit is in this set deliberately and is NOT a gap: a focused
		// text field is marked by the TERMINAL'S OWN CURSOR, which the
		// compositor places for exactly the widgets carrying
		// WA_InputMethodEnabled, and which a cell snapshot cannot see. The
		// two mechanisms are complementary and this records which widget
		// relies on which -- so a day when the line edit stops getting the
		// cursor, or another widget quietly becomes colour-only, fails here
		// rather than being discovered by somebody who cannot find their
		// place in a form.
		QStringList by_colour_only;
		by_colour_only << QStringLiteral("QLineEdit");
		CHECK(colour_only == by_colour_only,
		      "and exactly one marks focus in colour alone -- the line edit,"
		      " which the terminal cursor marks instead");
		GridGuard::reset();
	}

	// Which way round a dial reads, asserted as a RELATIONSHIP rather than as
	// a position, because both ways of getting it wrong are positions that
	// look plausible on their own.
	//
	// QStyleOptionSlider::upsideDown carries it, and a dial reads the flag
	// with the OPPOSITE polarity to a slider. Measured, printing what Qt put
	// in the option:
	//
	//     invertedAppearance(false)   upsideDown = 1
	//     invertedAppearance(true)    upsideDown = 0
	//
	// So `if (upsideDown) mirror`, which is correct for the slider a few
	// lines above it in the style, puts 0 at the right-hand end of every
	// ORDINARY dial. Ignoring the flag instead -- which is how the dial was
	// first drawn -- makes an inverted dial identical to a normal one.
	//
	// The pair catches both: the first assertion fails if the polarity is
	// the slider's, the second if the flag is not read at all.
	{
		const auto handle_x = [](bool inverted, int value) {
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			auto *box = new QVBoxLayout(&host);
			auto *d = new QDial;
			d->setRange(0, 100);
			d->setInvertedAppearance(inverted);
			box->addWidget(d);
			host.resize(GridMetrics::cells(20, 5));
			host.show();
			QCoreApplication::processEvents();
			d->setValue(value);
			QCoreApplication::processEvents();
			CellBuffer b(20, 5);
			render_once(host, b);
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x)
					if (b.at(x, y).ch == QStringLiteral("\u25cf")) return x;
			return -1;
		};
		const int lo = handle_x(false, 0), hi = handle_x(false, 100);
		const int ilo = handle_x(true, 0), ihi = handle_x(true, 100);
		CHECK(lo >= 0 && hi >= 0 && lo < hi,
		      "a dial's handle moves rightwards as its value rises");
		CHECK(ilo >= 0 && ihi >= 0 && ilo > ihi,
		      "and leftwards when its appearance is inverted, which is the"
		      " same flag the slider reads the other way round");
		GridGuard::reset();
	}

	// A scroll bar's direction, and the agreement between what is DRAWN and
	// where a click LANDS.
	//
	// Three controls share QStyleOptionSlider and none of them agrees about
	// upsideDown. Measured, printing what Qt put in the option:
	//
	//     QScrollBar   both orientations   upsideDown == inverted
	//     QDial                            upsideDown == !inverted
	//     QSlider      vertical            upsideDown == !inverted
	//
	// So the line that is right for the dial is wrong here and vice versa,
	// and the bar ignored the flag altogether: an inverted bar drew exactly
	// like an ordinary one.
	//
	// The second assertion is the one worth the trouble. thumb_pos is
	// computed in TWO places -- the drawing and subControlRect(), which is
	// the hit test -- so mirroring one alone puts the thumb where a click
	// cannot reach it, which is worse than a bar that reads backwards
	// consistently.
	{
		const auto thumb_row = [](bool inverted, int value) {
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			auto *box = new QVBoxLayout(&host);
			auto *sb = new QScrollBar(Qt::Vertical);
			sb->setRange(0, 100);
			sb->setPageStep(10);
			sb->setInvertedAppearance(inverted);
			box->addWidget(sb);
			host.resize(GridMetrics::cells(16, 8));
			host.show();
			QCoreApplication::processEvents();
			sb->setValue(value);
			QCoreApplication::processEvents();
			CellBuffer b(16, 8);
			render_once(host, b);
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x)
					if (b.at(x, y).ch == QStringLiteral("\u2588")) return y;
			return -1;
		};
		const int top = thumb_row(false, 0), bottom = thumb_row(false, 100);
		const int itop = thumb_row(true, 0), ibottom = thumb_row(true, 100);
		CHECK(top >= 0 && bottom >= 0 && top < bottom,
		      "a scroll bar's thumb moves down as its value rises");
		CHECK(itop >= 0 && ibottom >= 0 && itop > ibottom,
		      "and up when its appearance is inverted, a flag it reads with"
		      " the opposite polarity to the dial");

		// And the hit test agrees with the drawing, asked of the style with
		// the same option the widget would hand it.
		QScrollBar bar(Qt::Vertical);
		bar.setRange(0, 100);
		bar.setPageStep(10);
		bar.setInvertedAppearance(true);
		bar.resize(GridMetrics::cells(1, 8));
		bar.setValue(0);
		QStyleOptionSlider so;
		so.initFrom(&bar);
		so.orientation = Qt::Vertical;
		so.minimum = bar.minimum();
		so.maximum = bar.maximum();
		so.pageStep = bar.pageStep();
		so.sliderPosition = bar.value();
		so.sliderValue = bar.value();
		so.upsideDown = bar.invertedAppearance();
		const QRect slider = bar.style()->subControlRect(
		    QStyle::CC_ScrollBar, &so, QStyle::SC_ScrollBarSlider, &bar);
		const int hit_row = slider.center().y() / GridMetrics::ch();
		CHECK(!slider.isNull() && hit_row == itop,
		      "and a click lands on the row the inverted thumb is drawn in,"
		      " the two being computed in different places");
		GridGuard::reset();
	}

	// A progress bar's fill direction, the third control in this file found
	// ignoring invertedAppearance -- the dial and the scroll bar being the
	// other two. Its option names the property outright rather than folding
	// it into an upsideDown flag that means something different per control,
	// so there is no polarity to measure here, only a reversal to apply.
	//
	// Asserted in BOTH orientations because the two reversals compose: a
	// vertical bar already fills from the far end, so inverting it fills from
	// the near one, and a fix written as "inverted means fill from the right"
	// would leave the vertical case alone or reverse it twice.
	//
	// Text off, or the percentage overwrites the cells being counted.
	{
		const auto first_block = [](bool vertical, bool inverted) {
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			auto *box = new QVBoxLayout(&host);
			auto *p = new QProgressBar;
			p->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
			p->setRange(0, 100);
			p->setTextVisible(false);
			p->setInvertedAppearance(inverted);
			box->addWidget(p);
			host.resize(GridMetrics::cells(12, 8));
			host.show();
			QCoreApplication::processEvents();
			p->setValue(25);
			QCoreApplication::processEvents();
			CellBuffer b(12, 8);
			render_once(host, b);
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x)
					if (b.at(x, y).ch == QStringLiteral("\u2588"))
						return vertical ? y : x;
			return -1;
		};
		const int h_norm = first_block(false, false);
		const int h_inv = first_block(false, true);
		const int v_norm = first_block(true, false);
		const int v_inv = first_block(true, true);
		CHECK(h_norm >= 0 && h_inv >= 0 && h_norm < h_inv,
		      "a horizontal progress bar fills from the left, and from the"
		      " right when its appearance is inverted");
		CHECK(v_norm >= 0 && v_inv >= 0 && v_norm > v_inv,
		      "and a vertical one fills upward, and downward when inverted,"
		      " the two reversals composing rather than cancelling");
		GridGuard::reset();
	}

	// The elide mode reaching the ITEM VIEW, which is a different claim from
	// the helper honouring it: Qt puts textElideMode in the option and both
	// writers -- the style and CellItemDelegate -- discarded it. A correct
	// helper nobody passes the mode to looks exactly like a working feature.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *box = new QVBoxLayout(&host);
		box->setContentsMargins(0, 0, 0, 0);
		auto *tree = new QTreeWidget;
		tree->setFrameShape(QFrame::NoFrame);
		tree->setHeaderHidden(true);
		tree->setColumnCount(1);
		tree->setTextElideMode(Qt::ElideLeft);
		const QString path = QStringLiteral("/home/user/deep/dir/report.txt");
		new QTreeWidgetItem(tree, QStringList{path});
		box->addWidget(tree);
		host.resize(GridMetrics::cells(16, 3));
		host.show();
		QCoreApplication::processEvents();
		tree->setColumnWidth(0, 12 * GridMetrics::cw());
		QCoreApplication::processEvents();
		CellBuffer b(16, 3);
		render_once(host, b);
		QString row;
		for (int x = 0; x < b.cols(); ++x) row += b.at(x, 0).ch;
		CHECK(row.contains(QStringLiteral("report.txt")),
		      "a tree column elides the end its view asked for, so a path"
		      " shows its filename rather than its directory");
		GridGuard::reset();
	}

	// A right-aligned column, through BOTH writers, asserted as agreement
	// rather than twice over.
	//
	// QStyleOptionViewItem::displayAlignment was honoured by CellItemDelegate
	// and discarded by the style, so a program that installed qtty's delegate
	// was right and the same program without it was wrong -- and the delegate
	// is the optional extra, so the default was the broken one. A model that
	// right-aligns a numeric column is not being decorative: it is asking for
	// the digits to line up, which is the whole reason the convention exists.
	// Left-aligned, "7" and "1234" share a column and agree about nothing.
	//
	// This is the second field found split between the two writers, after
	// textElideMode, which is why the check is on their AGREEMENT: a third
	// one read by only one of them fails here without anybody having thought
	// to test that particular property.
	{
		const auto render = [](bool with_delegate) {
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			auto *box = new QVBoxLayout(&host);
			box->setContentsMargins(0, 0, 0, 0);
			auto *table = new QTableWidget(2, 1);
			table->setFrameShape(QFrame::NoFrame);
			table->horizontalHeader()->hide();
			table->verticalHeader()->hide();
			if (with_delegate)
				table->setItemDelegate(new CellItemDelegate(table));
			for (int row = 0; row < 2; ++row) {
				auto *cell = new QTableWidgetItem(row ? QStringLiteral("1234")
				                                      : QStringLiteral("7"));
				cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				table->setItem(row, 0, cell);
			}
			box->addWidget(table);
			host.resize(GridMetrics::cells(14, 4));
			host.show();
			QCoreApplication::processEvents();
			table->setColumnWidth(0, 12 * GridMetrics::cw());
			QCoreApplication::processEvents();
			CellBuffer b(14, 4);
			render_once(host, b);
			QStringList out;
			for (int y = 0; y < 2; ++y) {
				QString line;
				for (int x = 0; x < b.cols(); ++x) line += b.at(x, y).ch;
				out << line;
			}
			return out;
		};
		const QStringList plain = render(false), custom = render(true);
		// The units digits share a column, which is the thing a reader of a
		// numeric table is actually using.
		const auto last_digit = [](const QString &line) {
			for (int i = line.size() - 1; i >= 0; --i)
				if (line.at(i).isDigit()) return i;
			return -1;
		};
		CHECK(last_digit(plain.value(0)) > 0
		          && last_digit(plain.value(0)) == last_digit(plain.value(1)),
		      "a right-aligned column lines its digits up, which is what the"
		      " alignment was asked for");
		CHECK(plain == custom,
		      "and the style and CellItemDelegate draw it identically, two"
		      " writers of one rule having twice disagreed about a field");
		GridGuard::reset();
	}

	// A right-aligned column and its HEADING, asserted as sharing an edge.
	//
	// The heading is positioned to line up with the data rather than by its
	// own alignment -- that decision is recorded at CE_HeaderLabel and two
	// checks above enforce its left-hand half, because Qt centres a
	// horizontal header by default and centring would undo it. The half that
	// was missing is the other edge: when the data moves right, the heading
	// stayed where it was, so a numeric column read as a right-aligned body
	// under a left-aligned title.
	//
	// Asserted on the shared edge and not on a column number, so what is
	// pinned is "they line up" rather than one table's arithmetic.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *box = new QVBoxLayout(&host);
		box->setContentsMargins(0, 0, 0, 0);
		auto *table = new QTableWidget(2, 1);
		table->setFrameShape(QFrame::NoFrame);
		table->verticalHeader()->hide();
		auto *head = new QTableWidgetItem(QStringLiteral("Size"));
		head->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		table->setHorizontalHeaderItem(0, head);
		for (int row = 0; row < 2; ++row) {
			auto *cell = new QTableWidgetItem(row ? QStringLiteral("1234")
			                                      : QStringLiteral("7"));
			cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			table->setItem(row, 0, cell);
		}
		box->addWidget(table);
		host.resize(GridMetrics::cells(14, 5));
		host.show();
		QCoreApplication::processEvents();
		table->setColumnWidth(0, 12 * GridMetrics::cw());
		QCoreApplication::processEvents();
		CellBuffer b(14, 5);
		render_once(host, b);
		const auto right_edge = [&](int y) {
			for (int x = b.cols() - 1; x >= 0; --x)
				if (!b.at(x, y).ch.trimmed().isEmpty()) return x;
			return -1;
		};
		const int title = right_edge(0), small = right_edge(1), big = right_edge(2);
		CHECK(title > 0 && title == small && small == big,
		      "a right-aligned heading ends where its right-aligned data"
		      " does, a heading being a label for what is below it");
		GridGuard::reset();
	}

	// Every label in a menu starts in the same column, whether or not that
	// particular item is checkable.
	//
	// The check cell used to be reserved only for checkable items, so an
	// ordinary item in a menu with toggles began two cells to the left and
	// the menu read ragged. The comment recording that called it "the cost of
	// deciding per item, Qt handing the style one item at a time" -- and the
	// information was in the option all along:
	// QStyleOptionMenuItem::menuHasCheckableItems says what the MENU is like
	// to a style being handed one row.
	//
	// A menu with nothing checkable reserves nothing, which the second
	// assertion pins: the fix must not spend a column on every menu in the
	// program to tidy the ones with toggles.
	{
		const auto label_columns = [](bool checkable) {
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			host.resize(GridMetrics::cells(24, 8));
			host.show();
			QMenu menu(&host);
			menu.setAttribute(Qt::WA_DontShowOnScreen);
			menu.addAction(QStringLiteral("Open"));
			auto *wrap = menu.addAction(QStringLiteral("Word wrap"));
			wrap->setCheckable(checkable);
			wrap->setChecked(checkable);
			menu.addAction(QStringLiteral("Quit"));
			menu.resize(menu.sizeHint());
			menu.show();
			QCoreApplication::processEvents();
			CellBuffer b(24, 8);
			render_once(menu, b);
			// The first LETTER of each row, which is the label start
			// whatever sits before it. Scanning for the first non-blank
			// instead counts the frame and the tick, and skips the ticked
			// row altogether -- the first version of this did, and reported
			// a fault that a rendering of the same menu plainly did not
			// have.
			QVector<int> starts;
			for (int y = 0; y < b.rows(); ++y) {
				for (int x = 0; x < b.cols(); ++x) {
					const QString ch = b.at(x, y).ch;
					if (ch.isEmpty() || !ch.at(0).isLetter()) continue;
					starts.append(x);
					break;
				}
			}
			return starts;
		};
		const QVector<int> mixed = label_columns(true);
		const QVector<int> plain = label_columns(false);
		const auto all_same = [](const QVector<int> &v) {
			for (int i = 1; i < v.size(); ++i)
				if (v[i] != v[0]) return false;
			return !v.isEmpty();
		};
		CHECK(all_same(mixed),
		      "every label in a menu with toggles starts in the same column,"
		      " checkable or not");
		CHECK(all_same(plain) && plain.value(0) < mixed.value(0),
		      "and a menu with nothing checkable spends no column on one");
		GridGuard::reset();
	}

	// A SIZE GRIP, which Qt puts in the corner of every QMainWindow's status
	// bar and of any dialog that asks for one. What it cost here, and why the
	// style takes its cells away rather than merely declining to paint it, is
	// measured beside CT_SizeGrip in grid_style.cpp; these are the checks that
	// stop it coming back.
	{
		QStyleOption o;
		const QSize grip = QApplication::style()->sizeFromContents(
		    QStyle::CT_SizeGrip, &o, QSize(13, 13), nullptr);
		CHECK(grip.isEmpty(), "a size grip is asked for no cells at all");

		// THE RELATIONSHIP, not either frame. The same window rendered with
		// Qt's grip switched on and with it switched off must come out the
		// same cells -- which is the claim, "it costs a status bar nothing".
		// An assertion on one frame would say what a status bar looks like
		// today and go stale the next time anything in one moves; this can
		// only fail if the grip starts costing something again.
		const auto render_bar = [](bool grip_enabled) {
			QMainWindow win;
			win.setCentralWidget(new QLabel(QStringLiteral("body")));
			win.statusBar()->addWidget(new QLabel(
			    QStringLiteral("wlan0 up, dhcp lease 12h, gw 192.168.1.1")));
			win.statusBar()->setSizeGripEnabled(grip_enabled);
			show(win, 30, 7);
			CellBuffer b(30, 7);
			render_once(win, b);
			return b.to_text();
		};
		CHECK(render_bar(true) == render_bar(false),
		      "and costs a status bar nothing: the same window renders the "
		      "same cells with Qt's grip switched on as with it switched off");

		// THE DETECTOR THAT FOUND IT, kept as a check. These tests run under
		// a theme whose every role answers Color::Default, so an Rgb colour
		// in a frame is raw painting that reached a cell without passing
		// through the style -- a pen, a gradient, a pixmap. The grip's black
		// corner was the only one a plain main window had.
		const auto raw_cells = [](const CellBuffer &b) {
			int n = 0;
			for (int y = 0; y < b.rows(); ++y) {
				for (int x = 0; x < b.cols(); ++x) {
					if (b.at(x, y).bg.kind() == Color::Rgb
					    || b.at(x, y).fg.kind() == Color::Rgb) ++n;
				}
			}
			return n;
		};
		{
			QMainWindow win;
			win.setCentralWidget(new QLabel(QStringLiteral("body")));
			win.statusBar()->addWidget(new QLabel(QStringLiteral("ready")));
			show(win, 30, 7);
			CellBuffer b(30, 7);
			render_once(win, b);
			CHECK(raw_cells(b) == 0,
			      "and a main window with a status bar leaves no cell "
			      "carrying a colour the theme never named");
		}
		// AND THE ELEMENT A GRIP WITH NO CELLS NEVER ASKS FOR. The escape
		// above is the only way to reach CE_SizeGrip at all, so it is the
		// only fixture that can test it: the application asks for its grip
		// back and gives it a size, and the base style's own grip lands on
		// a cell as an opaque black ground when nothing answers the element.
		{
			QMainWindow win;
			win.setCentralWidget(new QLabel(QStringLiteral("body")));
			win.statusBar()->addWidget(new QLabel(QStringLiteral("ready")));
			show(win, 30, 7);
			win.statusBar()->setSizeGripEnabled(true);
			QCoreApplication::processEvents();
			for (QSizeGrip *g : win.statusBar()->findChildren<QSizeGrip *>())
				g->setGeometry(28 * GridMetrics::cw(), 0,
				               2 * GridMetrics::cw(), GridMetrics::ch());
			QCoreApplication::processEvents();
			CellBuffer b(30, 7);
			render_once(win, b);
			CHECK(raw_cells(b) == 0,
			      "and a grip an application sized itself draws nothing "
			      "rather than the base style's own");
		}
		GridGuard::reset();
	}

	// A TOOLBAR'S OWN GROUND, which the base style was painting under
	// everything. QToolBar::paintEvent() asks for CE_ToolBar and reaches
	// PE_PanelToolBar only when the bar floats, so answering the primitive
	// alone left every docked toolbar in the tree drawn by the base style:
	// eighteen cells of horizontal rule on an #f4f4f4 ground, and the rule
	// visible only where no button covered it. Same detector as the size
	// grip above -- an Rgb colour under a theme whose every role answers
	// Default is raw painting that did not pass through the style.
	{
		const auto toolbar_frame = [](bool separator) {
			QMainWindow win;
			win.setCentralWidget(new QLabel(QStringLiteral("body")));
			QToolBar *bar = win.addToolBar(QStringLiteral("main"));
			bar->addAction(QStringLiteral("Open"));
			if (separator) bar->addSeparator();
			bar->addAction(QStringLiteral("Save"));
			show(win, 30, 7);
			CellBuffer b(30, 7);
			render_once(win, b);
			int raw = 0;
			for (int y = 0; y < b.rows(); ++y) {
				for (int x = 0; x < b.cols(); ++x) {
					if (b.at(x, y).bg.kind() == Color::Rgb
					    || b.at(x, y).fg.kind() == Color::Rgb) ++raw;
				}
			}
			return QPair<int, QString>(raw, b.to_text().split(
			                                    QLatin1Char('\n')).value(0));
		};
		const QPair<int, QString> plain = toolbar_frame(false);
		CHECK(plain.first == 0,
		      "a docked toolbar leaves no cell carrying a colour the theme "
		      "never named");
		// The rule and the buttons are one claim: what was wrong was not
		// only the ground but that the bar's empty extent carried a line.
		// Asserted as "nothing after the last button" rather than as the
		// whole row's text, which would go stale the next time a button
		// changes width.
		CHECK(plain.second.trimmed() == QStringLiteral("[Open][Save]"),
		      "and its empty extent is empty rather than a rule that starts "
		      "after the last button");
		const QPair<int, QString> separated = toolbar_frame(true);
		CHECK(separated.first == 0
		      && separated.second.trimmed()
		             == QStringLiteral("[Open]\u2502[Save]"),
		      "and a separator in one is this style's glyph on no ground of "
		      "its own");
		GridGuard::reset();
	}

	// A QToolBox, which nothing here had ever rendered and which came out as
	// rules and diagonals: QCommonStyle draws a section's tab as a polygon
	// with a corner, and on a grid that arrived as a #7b7b7b rule trailing
	// each title, ending in a box-drawing diagonal, with a stray rule on a
	// row of its own -- 76 Rgb cells in a three-section box. Drawn here as
	// what a tool box IS, a disclosure list, with the tree's own pair of
	// marks.
	{
		QWidget host;
		auto *v = new QVBoxLayout(&host);
		auto *box = new QToolBox;
		box->addItem(new QLabel(QStringLiteral("alpha")),
		             QStringLiteral("One"));
		box->addItem(new QLabel(QStringLiteral("beta")),
		             QStringLiteral("Two"));
		box->addItem(new QLabel(QStringLiteral("gamma")),
		             QStringLiteral("Three"));
		v->addWidget(box);
		show(host, 30, 9);
		CellBuffer b(30, 9);
		render_once(host, b);
		int raw = 0;
		for (int y = 0; y < b.rows(); ++y) {
			for (int x = 0; x < b.cols(); ++x) {
				if (b.at(x, y).bg.kind() == Color::Rgb
				    || b.at(x, y).fg.kind() == Color::Rgb) ++raw;
			}
		}
		CHECK(raw == 0,
		      "a tool box leaves no cell carrying a colour the theme never "
		      "named");
		const QPoint open = findText(b, QStringLiteral("One"));
		const QPoint shut = findText(b, QStringLiteral("Two"));
		const QPoint last = findText(b, QStringLiteral("Three"));
		CHECK(open.x() >= 2 && shut.x() >= 2 && last.x() >= 2
		      && b.at(open.x() - 2, open.y()).ch == QStringLiteral("\u25be")
		      && b.at(shut.x() - 2, shut.y()).ch == QStringLiteral("\u25b8")
		      && b.at(last.x() - 2, last.y()).ch == QStringLiteral("\u25b8"),
		      "and each section says whether it is open, with the same pair "
		      "of marks a tree uses");
		// THE RELATIONSHIP rather than either attribute: what matters is
		// that the open section differs from the shut ones, not which
		// attribute spells it. A check on Attr::Reverse alone would go
		// stale the day the mark changes and would have passed with every
		// section reversed.
		const Attrs a_open = b.at(open.x(), open.y()).attrs;
		const Attrs a_shut = b.at(shut.x(), shut.y()).attrs;
		CHECK(a_open != a_shut && a_shut == Attrs(),
		      "and the open one is marked where the shut ones are plain");
		// A tool box's section headers are Qt::NoFocus and in nobody's tab
		// chain, so a keyboard user cannot open a section at all. That is
		// the application's to answer, and pointer_only() already names
		// them -- a QToolBoxButton is a QAbstractButton, so the audit
		// covers it by construction. Asserted here because this block is
		// where somebody will next read about a tool box.
		CHECK(pointer_only(&host).size() == 3,
		      "and its three section headers are named pointer-only, "
		      "legible now and still not openable from the keyboard");
		GridGuard::reset();
	}

	// AND THE REMEDY, which is one character rather than a focus policy: a
	// mnemonic in the section title. Qt registers a shortcut for it on the
	// header button, so Alt opens that section, and pointer_only() stops
	// naming it -- measured, all three at once. The letter is underlined
	// because it is the ONLY key into a shut section: a header is
	// Qt::NoFocus and in nobody's tab chain, so an unmarked letter is a key
	// nobody finds.
	{
		QWidget host;
		auto *v = new QVBoxLayout(&host);
		auto *box = new QToolBox;
		box->addItem(new QLabel(QStringLiteral("alpha")),
		             QStringLiteral("&One"));
		box->addItem(new QLabel(QStringLiteral("beta")),
		             QStringLiteral("T&wo"));
		v->addWidget(box);
		show(host, 30, 8);
		CellBuffer b(30, 8);
		render_once(host, b);
		CHECK(pointer_only(&host).isEmpty(),
		      "a tool box whose titles carry a mnemonic names nobody, the "
		      "letter being a key that opens the section");
		const QPoint one = findText(b, QStringLiteral("One"));
		const QPoint two = findText(b, QStringLiteral("Two"));
		// THE RELATIONSHIP between the marked letter and its neighbours,
		// rather than the attribute alone: what is wrong when this fails is
		// that the letter is not picked out, and "T&wo" is in the fixture
		// so a check that assumed the first letter would pass for the wrong
		// reason.
		const bool first_marked =
		    one.x() >= 0
		    && (b.at(one.x(), one.y()).attrs & Attr::Underline)
		    && !(b.at(one.x() + 1, one.y()).attrs & Attr::Underline);
		const bool inner_marked =
		    two.x() >= 0
		    && (b.at(two.x() + 1, two.y()).attrs & Attr::Underline)
		    && !(b.at(two.x(), two.y()).attrs & Attr::Underline);
		CHECK(first_marked && inner_marked,
		      "and the letter it uses is underlined, wherever in the title "
		      "the ampersand sits");
		GridGuard::reset();
	}

	// A CARET OR A MARK, NEVER NEITHER. A one-row editor shows focus with
	// the terminal's cursor and draws nothing of its own -- right for a
	// QLineEdit, and wrong for a control the cursor is not placed on.
	// QKeySequenceEdit is that control and it is the purest "type here"
	// widget Qt ships: its internal QLineEdit's focus proxy is the outer
	// widget, so the focus lands on the outer, which does not carry
	// WA_InputMethodEnabled because it takes raw key presses rather than
	// input-method text. focus_invisible() named it and nothing else in a
	// sweep of twenty-five standard controls.
	//
	// COMPARED INSIDE THE CANDIDATE'S OWN RECTANGLE, which is not a detail:
	// the first version of this block compared whole frames, and focusing
	// one widget takes the mark OFF another, so "the screen changed" was
	// satisfied by the BUTTON losing its reverse. The control below caught
	// it -- a plain line edit came out different from itself -- which is
	// the only reason it is written this way. focus_invisible() restricts
	// the comparison the same way and says so in its own comment.
	{
		const auto own_cells = [](QWidget &host, QWidget *w, int cols,
		                          int rows) {
			CellBuffer b(cols, rows);
			render_once(host, b);
			const QPoint at = w->mapTo(&host, QPoint());
			const int x0 = at.x() / GridMetrics::cw();
			const int y0 = at.y() / GridMetrics::ch();
			QString sig;
			for (int y = y0; y < y0 + qMax(1, w->height() / GridMetrics::ch())
			                 && y < b.rows(); ++y) {
				for (int x = x0; x < x0 + qMax(1, w->width() / GridMetrics::cw())
				                 && x < b.cols(); ++x)
					sig += b.at(x, y).ch
					     + QString::number(unsigned(b.at(x, y).attrs));
			}
			return sig;
		};
		QWidget host;
		auto *v = new QVBoxLayout(&host);
		auto *elsewhere = new QPushButton(QStringLiteral("Elsewhere"));
		auto *keys = new QKeySequenceEdit;
		auto *plain = new QLineEdit(QStringLiteral("plain"));
		v->addWidget(elsewhere);
		v->addWidget(keys);
		v->addWidget(plain);
		show(host, 26, 7);
		const auto focus_on = [&host](QWidget *w) {
			w->setFocus();
			set_focus_widget(w);
			QCoreApplication::processEvents();
		};
		focus_on(elsewhere);
		const QString keys_off = own_cells(host, keys, 26, 7);
		const QString plain_off = own_cells(host, plain, 26, 7);
		focus_on(keys);
		const QString keys_on = own_cells(host, keys, 26, 7);
		focus_on(plain);
		const QString plain_on = own_cells(host, plain, 26, 7);
		CHECK(keys_on != keys_off,
		      "a focused key sequence edit differs inside its own cells "
		      "from an unfocused one, the cursor not being placed on a "
		      "widget that takes no input-method text");
		// THE CONTROL, the same rule from the other side: a plain line edit
		// must stay unmarked, because the caret marks it. A fix that marked
		// every focused editor would satisfy the check above and fail here.
		CHECK(plain_on == plain_off,
		      "and a plain line edit still draws no mark of its own, the "
		      "terminal's cursor being what says where the typing goes");
		GridGuard::reset();
	}

	return fails;
}

int suite_widgets_entry(bool record) { g_record = record; return suite_widgets(); }
