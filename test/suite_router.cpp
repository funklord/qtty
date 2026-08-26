// suite_router -- section 5.5: shortcuts (F3), tab order, arrow fallback, mouse
// dispatch, popup stamping + compositor clamping (F7, section 8.1).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_router() {
	fails = 0;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	QWidget win;
	auto *v = new QVBoxLayout(&win);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto *edit = new QLineEdit(&win);
	auto *btn = new QPushButton("Go", &win);
	auto *list = new QListView(&win);
	auto *model = new QStringListModel(&win);
	QStringList rows;
	for (int i = 0; i < 100; ++i) rows << QStringLiteral("row %1").arg(i);
	model->setStringList(rows);
	list->setModel(model);
	list->setFrameShape(QFrame::NoFrame);
	list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	v->addWidget(edit); v->addWidget(btn); v->addWidget(list, 1);
	win.setAttribute(Qt::WA_DontShowOnScreen);
	win.resize(GridMetrics::cells(40, 16));
	win.show();
	QCoreApplication::processEvents();

	InputRouter router(&win);

	// F3: shortcut resolved by the router, not QShortcutMap
	int fired = 0;
	auto *save = new QAction("Save", &win);
	save->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
	win.addAction(save);
	QObject::connect(save, &QAction::triggered, [&] { fired++; });
	router.onKey({Qt::Key_S, QString(), true, false, false});
	CHECK(fired == 1, "router resolves Ctrl+S to QAction (F3)");

	// typing reaches the focus widget
	edit->setFocus(Qt::OtherFocusReason);
	QCoreApplication::processEvents();
	router.onKey({0, QStringLiteral("h"), false, false, false});
	router.onKey({0, QStringLiteral("i"), false, false, false});
	CHECK(edit->text() == QStringLiteral("hi"), "text keys reach focus widget (F4)");

	// arrow the focus widget ignores falls back to scrolling -- focus is the
	// QLineEdit here, which ignores vertical arrows. (A focused QAbstractButton
	// ACCEPTS arrows for button-group navigation, so no fallback there -- by
	// design, not a bug.)
	const int before = list->verticalScrollBar()->value();
	router.onKey({Qt::Key_Down, QString(), false, false, false});
	CHECK(list->verticalScrollBar()->value() > before,
	      "ignored Down scrolls the scroll area");

	// Tab walks the focus chain
	router.onKey({Qt::Key_Tab, QString(), false, false, false});
	CHECK(win.focusWidget() == btn, "Tab advances focus chain");

	// mouse: click the button by cell
	int clicked = 0;
	QObject::connect(btn, &QPushButton::clicked, [&] { clicked++; });
	QPoint center = btn->geometry().center();
	MouseEvent press{QPoint(center.x() / cw, center.y() / ch), 1, true, false, false, 0};
	MouseEvent release = press; release.press = false; release.release = true;
	router.onMouse(press);
	router.onMouse(release);
	CHECK(clicked == 1, "cell-space click reaches the button");

	// popup stamping + tracking (F7)
	QMenu menu(&win);
	menu.addAction("One");
	menu.addAction("Two");
	menu.popup(QPoint(30 * cw, 4 * ch));
	QCoreApplication::processEvents();
	CHECK(menu.testAttribute(Qt::WA_DontShowOnScreen), "popup stamped WA_DontShowOnScreen");
	CHECK(router.popups().contains(&menu), "popup tracked for the compositor");

	// compositor clamps an off-screen popup inside the terminal (section 8.1)
	Compositor comp(&win, &router);
	menu.move(39 * cw, 4 * ch);                          // hangs off the right edge
	CellBuffer frame(40, 16);
	comp.compose(frame);
	CHECK(menu.geometry().right() <= 40 * cw, "compositor clamps popup inside terminal");
	menu.close();
	QCoreApplication::processEvents();
	CHECK(router.popups().isEmpty(), "closed popup leaves the stack");

	// cursor comes from the focused editor (section 5.5)
	edit->setFocus(Qt::OtherFocusReason);
	QCoreApplication::processEvents();
	comp.compose(frame);
	CHECK(comp.cursorCell().has_value(), "cursor cell reported for focused editor");

	return fails;
}
