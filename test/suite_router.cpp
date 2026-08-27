// suite_router -- section 5.5: shortcuts (F3), tab order, arrow fallback, mouse
// dispatch, popup stamping + compositor placement (F7, section 8.1), the
// top-level walk (section 5.4 step 3), and modal handling (section 8.3).
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
	router.on_key({Qt::Key_S, QString(), true, false, false});
	CHECK(fired == 1, "router resolves Ctrl+S to QAction (F3)");

	// typing reaches the focus widget
	edit->setFocus(Qt::OtherFocusReason);
	QCoreApplication::processEvents();
	router.on_key({0, QStringLiteral("h"), false, false, false});
	router.on_key({0, QStringLiteral("i"), false, false, false});
	CHECK(edit->text() == QStringLiteral("hi"), "text keys reach focus widget (F4)");

	// arrow the focus widget ignores falls back to scrolling -- focus is the
	// QLineEdit here, which ignores vertical arrows. (A focused QAbstractButton
	// ACCEPTS arrows for button-group navigation, so no fallback there -- by
	// design, not a bug.)
	const int before = list->verticalScrollBar()->value();
	router.on_key({Qt::Key_Down, QString(), false, false, false});
	CHECK(list->verticalScrollBar()->value() > before,
	      "ignored Down scrolls the scroll area");

	// Tab walks the focus chain
	router.on_key({Qt::Key_Tab, QString(), false, false, false});
	CHECK(win.focusWidget() == btn, "Tab advances focus chain");

	// mouse: click the button by cell
	int clicked = 0;
	QObject::connect(btn, &QPushButton::clicked, [&] { clicked++; });
	QPoint center = btn->geometry().center();
	MouseEvent press{QPoint(center.x() / cw, center.y() / ch), 1, true, false, false, 0};
	MouseEvent release = press; release.press = false; release.release = true;
	router.on_mouse(press);
	router.on_mouse(release);
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
	CHECK(comp.cursor_cell().has_value(), "cursor cell reported for focused editor");

	// ------------------------------------------------ section 8.1: modals
	// A modal QDialog was stamped WA_DontShowOnScreen by the filter above and
	// then drawn by nobody: compose() rendered the one tracked window plus the
	// popup stack, and a modal is neither. It was invisible while still taking
	// input, which is why these checks come in a group -- drawing it and
	// routing to it are the same defect from two sides.
	QDialog dlg(&win);
	dlg.setModal(true);
	auto *dv = new QVBoxLayout(&dlg);
	dv->setContentsMargins(0, 0, 0, 0);
	dv->setSpacing(0);
	dv->addWidget(new QLabel(QStringLiteral("MODALHERE"), &dlg));
	auto *ok = new QPushButton(QStringLiteral("Ok"), &dlg);
	dv->addWidget(ok);
	dlg.resize(GridMetrics::cells(12, 4));
	dlg.move(4 * cw, 8 * ch);
	dlg.show();
	QCoreApplication::processEvents();
	CHECK(dlg.testAttribute(Qt::WA_DontShowOnScreen),
	      "modal dialog stamped WA_DontShowOnScreen (F7)");
	CHECK(QApplication::activeModalWidget() == &dlg, "dialog is the active modal");

	CellBuffer modal_frame(40, 16);
	comp.compose(modal_frame);
	CHECK(modal_frame.to_text().contains(QStringLiteral("MODALHERE")),
	      "modal dialog is composited (section 8.1)");

	// section 8.3: a click outside the modal is dropped, not delivered to what
	// happens to sit under it. The button is the one the click test above used,
	// so it is known to be reachable when no modal is up.
	const QPoint under = btn->geometry().center();
	CHECK(!dlg.geometry().contains(under),
	      "the button used for the drop check really is outside the modal");
	const int clicked_before = clicked;
	MouseEvent outside{QPoint(under.x() / cw, under.y() / ch), 1, true, false, false, 0};
	MouseEvent outside_up = outside; outside_up.press = false; outside_up.release = true;
	router.on_mouse(outside);
	router.on_mouse(outside_up);
	CHECK(clicked == clicked_before,
	      "mouse outside the modal is dropped before dispatch (section 8.3)");

	// ...and the same click inside it still lands, so the rule is a filter and
	// not a blanket refusal.
	int modal_clicked = 0;
	QObject::connect(ok, &QPushButton::clicked, [&] { modal_clicked++; });
	const QPoint inside = dlg.geometry().topLeft() + ok->geometry().center();
	MouseEvent on_ok{QPoint(inside.x() / cw, inside.y() / ch), 1, true, false, false, 0};
	MouseEvent on_ok_up = on_ok; on_ok_up.press = false; on_ok_up.release = true;
	router.on_mouse(on_ok);
	router.on_mouse(on_ok_up);
	CHECK(modal_clicked == 1, "mouse inside the modal reaches the dialog");

	dlg.close();
	QCoreApplication::processEvents();
	CHECK(!QApplication::activeModalWidget(), "modal leaves on close");

	// ---------------------------------- section 5.4 step 3: top-level walk
	// A second plain top-level is part of the frame. compose() used to render
	// only the window it was constructed with, so this one was simply absent.
	QWidget second;
	second.setAttribute(Qt::WA_DontShowOnScreen);
	auto *sv = new QVBoxLayout(&second);
	sv->setContentsMargins(0, 0, 0, 0);
	sv->setSpacing(0);
	sv->addWidget(new QLabel(QStringLiteral("SECONDWIN"), &second));
	second.resize(GridMetrics::cells(12, 2));
	second.move(20 * cw, 13 * ch);
	second.show();
	QCoreApplication::processEvents();
	CellBuffer walk_frame(40, 16);
	comp.compose(walk_frame);
	CHECK(walk_frame.to_text().contains(QStringLiteral("SECONDWIN")),
	      "second top-level is composited (section 5.4 step 3)");
	second.hide();
	QCoreApplication::processEvents();

	// -------------------------------- section 8.1: flip, rather than slide
	// "A menu opening at x=78 must flip left, which the desktop code never had
	// to do." Sliding also lands inside the terminal, so a check for "inside"
	// alone passes either way; what tells them apart is where the far edge
	// ends up. Flipped, it is on the anchor. Slid, it is on the screen edge.
	QMenu edge(&win);
	edge.addAction(QStringLiteral("FLIPME"));
	edge.popup(QPoint(37 * cw, 2 * ch));
	QCoreApplication::processEvents();
	const int anchor_x = edge.geometry().x();
	CHECK(anchor_x + edge.width() > 40 * cw,
	      "the flip case is real: the menu overhangs the right edge as opened");
	CellBuffer flip_frame(40, 16);
	comp.compose(flip_frame);
	CHECK(edge.geometry().right() <= anchor_x,
	      "menu at the right edge flips left of its anchor (section 8.1)");
	CHECK(edge.geometry().left() >= 0 && edge.geometry().right() < 40 * cw,
	      "flipped menu lands fully inside the terminal rectangle");
	CHECK(flip_frame.to_text().contains(QStringLiteral("FLIPME")),
	      "flipped menu is drawn whole, not clipped away");
	edge.close();
	QCoreApplication::processEvents();


	// ---- paste (section 5.5) -------------------------------------------------
	//
	// The router's paste path had no test at all. It became reachable only
	// when the backend learned to decode bracketed paste, and until then
	// nothing could have exercised it -- an implemented sink that nothing
	// calls, which is section 7.4's theme.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *vb = new QVBoxLayout(&host);
		vb->setContentsMargins(0, 0, 0, 0);
		vb->setSpacing(0);
		auto *line = new QLineEdit(&host);
		auto *doc = new QPlainTextEdit(&host);
		// Sized in whole cells by hand, because section 7.8 says an
		// application must: a QPlainTextEdit's minimum is not a cell multiple
		// and the layout honours the minimum, so leaving it to the layout put
		// this fixture one pixel off the grid and GridGuard said so. That is
		// the guard earning its place on a test written minutes earlier.
		doc->setFixedHeight(GridMetrics::ch() * 4);
		vb->addWidget(line);
		vb->addWidget(doc);
		host.resize(GridMetrics::cells(30, 5));
		host.show();
		QCoreApplication::processEvents();
		InputRouter pr(&host);

		line->setFocus();
		setFocusWidget(line);
		pr.on_paste(QStringLiteral("hello"));
		CHECK(line->text() == QStringLiteral("hello"), "a paste arrives as text");

		line->clear();
		pr.on_paste(QString::fromUtf8("caf\u00e9 \u6f22"));
		CHECK(line->text() == QString::fromUtf8("caf\u00e9 \u6f22"),
		      "a paste carries non-ASCII whole");

		// The case bracketed paste exists for. Delivering the newline as
		// Return would fire the default button and submit the dialog
		// mid-paste; delivering it raw leaves a newline inside a QLineEdit,
		// which is a state no user can type.
		line->clear();
		pr.on_paste(QStringLiteral("two\nlines"));
		CHECK(!line->text().contains(QLatin1Char('\n')),
		      "a multi-line paste leaves no newline in a single-line editor");
		CHECK(line->text() == QStringLiteral("two lines"),
		      "the newline becomes a space, and nothing else is lost");

		// A multi-line editor keeps them: the fold is about the target, not
		// about pastes.
		doc->setFocus();
		setFocusWidget(doc);
		pr.on_paste(QStringLiteral("a\nb"));
		CHECK(doc->toPlainText() == QStringLiteral("a\nb"),
		      "a multi-line editor keeps the newline it can hold");
	}

	// ---- mnemonics (section 17.2) --------------------------------------------
	//
	// Alt-<letter> against the `&` markers in action text. It could not have
	// been implemented before the backend learned to decode Alt at all, and
	// it cannot use the shortcut matcher: a mnemonic arrives with text and no
	// Qt::Key, because a terminal sends ESC then the letter.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		auto *bar = new QMenuBar(&host);
		bar->setGeometry(0, 0, GridMetrics::cw() * 30, GridMetrics::ch());
		QMenu *file = bar->addMenu(QStringLiteral("&File"));
		int opened = 0;
		QAction *open = file->addAction(QStringLiteral("&Open"));
		QObject::connect(open, &QAction::triggered, [&] { ++opened; });
		QAction *quit = file->addAction(QStringLiteral("&&Literal"));
		QObject::connect(quit, &QAction::triggered, [&] { ++opened; });
		host.show();
		QCoreApplication::processEvents();
		InputRouter mr(&host);

		// Alt-F opens the File menu rather than triggering anything.
		//
		// Asserted against the ROUTER's stack, not QApplication::
		// activePopupWidget(), and the first version of this check asked the
		// wrong one. That function returns null for every popup here: the
		// stamping filter sets WA_DontShowOnScreen as the popup is shown, the
		// platform never maps it, and Qt's open-popup list is driven by that
		// mapping. The check failed while the menu was open and visible.
		mr.on_key({0, QStringLiteral("f"), false, true, false});
		CHECK(mr.popups().contains(file),
		      "Alt with a menu's mnemonic opens that menu");
		CHECK(opened == 0, "and triggers nothing while doing it");

		// The finding that came out of that: keys reach an open menu at all.
		// key_target() consulted activePopupWidget(), so the branch could
		// never fire and Down and Return went to the widget behind the menu.
		// The menu drew correctly throughout, because the compositor reads
		// the router's stack rather than Qt's -- so nothing looked wrong.
		CHECK(mr.key_target() == file || file->isAncestorOf(mr.key_target()),
		      "an open menu is what keys are aimed at");

		// Alt-O inside the open menu finds the menu's own item, not the
		// window behind it.
		mr.on_key({0, QStringLiteral("o"), false, true, false});
		CHECK(opened == 1, "Alt with an item's mnemonic triggers that item");

		// And the keyboard actually drives it: Down then Return fires the
		// highlighted item. design.md section 16's gate 2 declared popups
		// working on a synthetic mouse CLICK, which triggers an action
		// without consulting key_target() at all, so this path went
		// unexercised by the measurement that signed it off.
		const int before_keys = opened;
		mr.on_key({Qt::Key_Down, {}, false, false, false});
		mr.on_key({Qt::Key_Return, {}, false, false, false});
		CHECK(opened > before_keys, "Down then Return fires the menu's item");

		file->close();
		QCoreApplication::processEvents();

		// "&&" is a literal ampersand and marks no letter. Alt-L must not
		// reach it -- the item's mnemonic is nothing, not 'l'.
		const int before = opened;
		mr.on_key({0, QStringLiteral("l"), false, true, false});
		CHECK(opened == before, "a doubled ampersand marks no mnemonic");

		// A letter with no marker anywhere is not swallowed: it must still
		// reach the focus widget as ordinary Alt-text.
		mr.on_key({0, QStringLiteral("z"), false, true, false});
		CHECK(opened == before, "an unmatched mnemonic triggers nothing");
	}
	return fails;
}
