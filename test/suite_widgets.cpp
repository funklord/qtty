// suite_widgets -- section 17.2 Channel A coverage: targeted asserts per widget plus
// the widgets_gallery snapshot.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
static bool g_record = false;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

static bool bufferContains(const CellBuffer &b, const QString &glyph) {
	for (int y = 0; y < b.rows(); ++y)
		for (int x = 0; x < b.cols(); ++x)
			if (b.at(x, y).ch == glyph) return true;
	return false;
}

static QPoint findText(const CellBuffer &b, const QString &s) {
	for (int y = 0; y < b.rows(); ++y)
		for (int x = 0; x + s.size() <= b.cols(); ++x) {
			bool ok = true;
			for (int i = 0; i < s.size(); ++i)
				if (b.at(x + i, y).ch != QString(s[i])) { ok = false; break; }
		    if (ok) return {x, y};
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
		renderOnce(combo, b);
		CHECK(bufferContains(b, QStringLiteral("▾")), "combo draws dropdown arrow");
		CHECK(findText(b, QStringLiteral("Alpha")).x() >= 0, "combo label renders");
	}
	// progress bar: fill + percentage
	{
		QProgressBar pb;
		pb.setRange(0, 100); pb.setValue(50);
		show(pb, 20, 1);
		CellBuffer b(22, 2);
		renderOnce(pb, b);
		CHECK(bufferContains(b, QStringLiteral("█")) && bufferContains(b, QStringLiteral("░")),
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
		renderOnce(tabs, b);
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
		renderOnce(tree, b);
		CHECK(bufferContains(b, QStringLiteral("▾")), "expanded branch shows ▾");
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
		renderOnce(list, b);
		CHECK(bufferContains(b, QStringLiteral("▲")) && bufferContains(b, QStringLiteral("▼")),
		      "scrollbar arrows render");
		CHECK(bufferContains(b, QStringLiteral("█")) && bufferContains(b, QStringLiteral("░")),
		      "scrollbar thumb + groove render");
	}
	// slider: handle on track
	{
		QSlider slider(Qt::Horizontal);
		slider.setRange(0, 10); slider.setValue(5);
		show(slider, 20, 1);
		CellBuffer b(22, 2);
		renderOnce(slider, b);
		CHECK(bufferContains(b, QStringLiteral("●")), "slider handle renders");
		CHECK(bufferContains(b, QStringLiteral("─")), "slider track renders");
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
		renderOnce(split, b);
		CHECK(bufferContains(b, QStringLiteral("│")), "splitter handle renders");
	}
	// line edit: selection carries a background colour
	{
		QLineEdit edit;
		edit.setText("hello");
		show(edit, 20, 3);
		edit.selectAll();
		QCoreApplication::processEvents();
		CellBuffer b(22, 4);
		renderOnce(edit, b);
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
		renderOnce(menu, b);
		QPoint o = findText(b, QStringLiteral("Open"));
		CHECK(o.x() >= 0, "menu item renders");
		CHECK(o.x() >= 0 && (b.at(o.x(), o.y()).attrs & Attr::Reverse),
		      "active menu item highlighted");
		CHECK(bufferContains(b, QStringLiteral("─")), "separator renders");
		CHECK(findText(b, QStringLiteral("Ctrl+O")).x() >= 0, "shortcut right-aligned");
		menu.close();
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
		const QString got = Qtty::test::snapshotOf(win, 46, 17);
		fails += Qtty::test::checkSnapshot(QStringLiteral(QTTY_SOURCE_DIR),
		                                   QStringLiteral("widgets_gallery"), got, g_record);
		if (!g_record) printf("%s: gallery snapshot\n", fails ? "FAIL" : "PASS");
	}
	return fails;
}

int suite_widgets_entry(bool record) { g_record = record; return suite_widgets(); }
