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
	// line edit: selection carries a background colour
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
		CHECK(h.x() >= 0 && b.at(h.x(), h.y()).bg.kind() != Color::Default,
		      "selected text carries highlight background");
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
		fails += Qtty::test::check_snapshot(QStringLiteral(QTTY_SOURCE_DIR),
		                                   QStringLiteral("widgets_gallery"), got, g_record);
		if (!g_record) printf("%s: gallery snapshot\n", fails ? "FAIL" : "PASS");
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
		setFocusWidget(combo->lineEdit());
		typed(QString::fromUtf8("h\u00e9llo"));
		CHECK(combo->currentText() == QString::fromUtf8("h\u00e9llo"),
		      "an editable combo takes typed text, non-ASCII included");

		doc->setFocus();
		setFocusWidget(doc);
		typed(QStringLiteral("abc"));
		er.on_key({Qt::Key_Return, {}, false, false, false});
		typed(QString::fromUtf8("\u6f22\u5b57"));
		CHECK(doc->toPlainText() == QString::fromUtf8("abc\n\u6f22\u5b57"),
		      "a plain text editor takes typing, Return and wide clusters");

		rich->setFocus();
		setFocusWidget(rich);
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
		setFocusWidget(spin);

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

	return fails;
}

int suite_widgets_entry(bool record) { g_record = record; return suite_widgets(); }
