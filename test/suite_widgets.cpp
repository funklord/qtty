// suite_widgets -- section 17.2 Channel A coverage: targeted asserts per widget plus
// the widgets_gallery snapshot.
#include <qtty/qtty.h>
#include <qtty/delegate.h>
#include <QtWidgets>
#include <cstdio>

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

int suite_widgets() {
	fails = 0;

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
		auto *doc = new QPlainTextEdit(&host);
		doc->setFixedHeight(GridMetrics::ch() * 4);
		auto *rich = new QTextEdit(&host);
		rich->setFixedHeight(GridMetrics::ch() * 3);
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


	return fails;
}

int suite_widgets_entry(bool record) { g_record = record; return suite_widgets(); }
