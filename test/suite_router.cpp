// suite_router -- section 5.5: shortcuts (F3), tab order, arrow fallback, mouse
// dispatch, popup stamping + compositor placement (F7, section 8.1), the
// top-level walk (section 5.4 step 3), and modal handling (section 8.3).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

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

	// BY HOW MUCH, and in Qt's own default mode. The fixture above switches
	// the view to ScrollPerPixel, which is not what an application gets: a
	// QAbstractItemView defaults to ScrollPerItem, where the bar's value is
	// an ITEM INDEX. A cell height applied to that scrolled nineteen rows
	// for one arrow key and ninety-five for a page, on any list, table or
	// tree nobody had switched over -- and the check above could not see it,
	// asserting a direction rather than a distance on the one configuration
	// where the units happen to be pixels.
	{
		list->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
		QCoreApplication::processEvents();
		QScrollBar *bar = list->verticalScrollBar();
		bar->setValue(0);
		router.on_key({Qt::Key_Down, QString(), false, false, false});
		const int one = bar->value();
		bar->setValue(0);
		router.on_key({Qt::Key_PageDown, QString(), false, false, false});
		const int page = bar->value();
		printf("info: per-item Down moves %d row(s), PageDown %d, of %d\n",
		       one, page, bar->maximum());
		// One row for an arrow. For a page, whatever the bar says a page is
		// -- that is the number of visible rows, which is what a reader
		// expects and what this cannot get wrong by assuming a unit.
		CHECK(one == 1 && page == qMax(1, bar->pageStep()) && page > 1,
		      "and in item-scrolling mode it moves rows, not pixels-worth of"
		      " them");
		list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		bar->setValue(before);
		QCoreApplication::processEvents();
	}

	// The ROOT SCROLL, which nothing in this suite touched at all --
	// set_root_scroll() had no caller outside the Compositor, so the whole
	// offset was measured by hand once ("a 30x4 terminal scrolled four
	// rows", in the comment that records the fix) and never became a check.
	// That is how a THIRD derivation of the position went on using the
	// unscrolled cell: the comment in on_mouse() says "two derivations of
	// one position, and only one of them was fixed", and the wheel was
	// neither of the two it counted.
	{
		struct Spy : QWidget {
			QPoint press_at, wheel_at;
			bool got_press = false, got_wheel = false;
			using QWidget::QWidget;
			void mousePressEvent(QMouseEvent *e) override {
				press_at = e->position().toPoint(); got_press = true;
				e->accept();
			}
			void wheelEvent(QWheelEvent *e) override {
				wheel_at = e->position().toPoint(); got_wheel = true;
				e->accept();
			}
		};
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QWidget scrolled;
		scrolled.setAttribute(Qt::WA_DontShowOnScreen);
		scrolled.resize(GridMetrics::cells(30, 12));
		auto *spy = new Spy(&scrolled);
		spy->setGeometry(0, 6 * ch, 10 * cw, 2 * ch);
		scrolled.show();
		QCoreApplication::processEvents();

		InputRouter r2(&scrolled);
		r2.set_root_scroll(QPoint(0, 4));      // the root is drawn four rows up

		// Screen row 2 is window row 6, which is the spy's first row. Both
		// events are sent to the same cell, so the two derivations have to
		// agree -- and the assertion is that agreement rather than either
		// number, which is what makes it independent of the font.
		MouseEvent click; click.cell = QPoint(3, 2); click.button = 1;
		click.press = true;
		r2.on_mouse(click);
		MouseEvent turn; turn.cell = QPoint(3, 2); turn.wheel = -1;
		r2.on_mouse(turn);

		printf("info: scrolled root: press at %d,%d and wheel at %d,%d\n",
		       spy->press_at.x(), spy->press_at.y(),
		       spy->wheel_at.x(), spy->wheel_at.y());
		CHECK(spy->got_press && spy->got_wheel
		      && spy->press_at == spy->wheel_at,
		      "a wheel on a scrolled root arrives where a click on the same"
		      " cell does");
		CHECK(spy->got_press && spy->press_at.y() >= 0
		      && spy->press_at.y() < 2 * ch,
		      "and both land inside the widget the user can see there");
	}

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

	// ...and it must land in the right cell, which has_value() cannot say. A
	// widget that delegates editing to an internal editor -- QSpinBox --
	// forwards ImCursorRectangle to it VERBATIM, so the rect arrives in the
	// editor's coordinates while compose() was mapping it from the outer
	// widget, dropping the editor's offset and putting the terminal cursor on
	// the spin box's frame instead of in its field.
	//
	// The invariant is that the same editor, at the same place on screen,
	// reports the same cell whether it is standalone or nested. Two caret
	// positions, because at position 0 the two answers round to the same cell
	// and a one-position test passes with the bug present. The standalone edit
	// is frameless to match the spin box's, or the two are not the same
	// editor at the same place and the test compares nothing.
	{
		auto cursor_of = [&](bool nested, int pos) {
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			QWidget *outer = nullptr;
			QLineEdit *inner = nullptr;
			if (nested) {
				auto *sp = new QSpinBox(&h);
				sp->setRange(0, 100);
				sp->setValue(43);
				sp->setGeometry(0, 0, cw * 20, ch);
				outer = sp;
				inner = sp->findChild<QLineEdit *>();
			} else {
				inner = new QLineEdit(&h);
				inner->setText(QStringLiteral("43"));
				inner->setFrame(false);
				outer = inner;
			}
			h.resize(GridMetrics::cells(30, 2));
			h.show();
			QCoreApplication::processEvents();
			if (!nested) inner->setGeometry(cw, 0, cw * 17, ch);
			outer->setFocus();
			QCoreApplication::processEvents();
			inner->setCursorPosition(pos);
			QCoreApplication::processEvents();
			InputRouter rr(&h);
			Compositor cc(&h, &rr);
			CellBuffer bb(30, 2);
			cc.compose(bb);
			return cc.cursor_cell();
		};
		bool same = true;
		for (int pos : {1, 2}) same = same && cursor_of(false, pos) == cursor_of(true, pos);
		CHECK(same, "a nested editor reports the same cursor cell as a standalone one");
	}

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
		set_focus_widget(line);
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
		set_focus_widget(doc);
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

		// Alt-O inside the open menu fires the menu's own item, not the
		// window behind it. It reaches the item through the MENU now rather
		// than through the router's mnemonic table -- the table stands down
		// while a popup owns input, for the reasons at the end of this suite
		// -- and the outcome asserted here is the same either way.
		mr.on_key({0, QStringLiteral("o"), false, true, false});
		CHECK(opened == 1, "Alt with an item's mnemonic triggers that item");

		// Reopened, and that is a consequence of the line above rather than
		// tidying: an item fired from a menu CLOSES it now. These lines were
		// written when it did not, so they inherited an open menu from the
		// defect and would otherwise be sending Down and Return at the window.
		mr.on_key({0, QStringLiteral("f"), false, true, false});
		QCoreApplication::processEvents();

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

	// ---- submenus (section 17.2) ---------------------------------------------
	//
	// Recorded as absent -- "nothing opens or routes a submenu" -- and it
	// needed no new code. Qt's own QMenu::keyPressEvent opens a submenu on
	// Right; it never ran because keys were not reaching the menu at all.
	// Fixing key_target() to read the router's popup stack made this work,
	// and this suite exists so the next change to that routing says so.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 10));
		QMenu menu(&host);
		menu.addAction(QStringLiteral("Plain"));
		QMenu *sub = menu.addMenu(QStringLiteral("More"));
		int fired = 0;
		QAction *deep = sub->addAction(QStringLiteral("Deep"));
		QObject::connect(deep, &QAction::triggered, [&] { ++fired; });
		host.show();
		QCoreApplication::processEvents();
		InputRouter sr(&host);

		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		sr.on_key({Qt::Key_Down, {}, false, false, false});
		sr.on_key({Qt::Key_Down, {}, false, false, false});
		CHECK(menu.activeAction() && menu.activeAction()->text() == QStringLiteral("More"),
		      "Down walks the menu to the submenu's item");

		sr.on_key({Qt::Key_Right, {}, false, false, false});
		QCoreApplication::processEvents();
		CHECK(sub->isVisible(), "Right opens the submenu");
		CHECK(sr.popups().size() == 2, "and both menus are on the popup stack");
		CHECK(sr.popups().last() == sub, "with the submenu on top");

		// Both are drawn: a submenu the compositor does not know about is a
		// menu the user cannot see themselves navigating.
		Compositor sc(&host, &sr);
		CellBuffer sb(40, 10);
		sc.compose(sb);
		const QString frame = sb.to_text();
		CHECK(frame.contains(QStringLiteral("Plain")), "the parent menu is drawn");
		CHECK(frame.contains(QStringLiteral("Deep")), "and the submenu over it");

		sr.on_key({Qt::Key_Down, {}, false, false, false});
		sr.on_key({Qt::Key_Return, {}, false, false, false});
		QCoreApplication::processEvents();
		CHECK(fired == 1, "Return in the submenu fires its item, not the parent's");
	}
	// Tab when NOTHING has focus yet, which is the state a window is in the
	// moment it opens. Every other focus check here sets focus first, so this
	// arrangement had none at all.
	//
	// It was written believing it would exercise on_key()'s
	// focusNextPrevChild() fallback, which coverage reports as never run.
	// SABOTAGE SAYS OTHERWISE: with that call replaced by nothing, this check
	// still passes, because Qt's own QWidget::event() reaches its Tab branch,
	// accepts the event and moves the focus first. So the fallback stays
	// unreached and this pins the BEHAVIOUR instead -- which is worth pinning
	// on its own, and is all this claims.
	//
	// What was tried and did not reach the fallback: no focus widget at all
	// (here), which Qt handles. Reaching it needs a scope where the press is
	// refused AND the focus does not move, and no arrangement of ordinary
	// widgets found here does both. Recorded so the next person does not
	// spend the same hour.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&h);
		auto *one = new QPushButton(QStringLiteral("one"), &h);
		auto *two = new QPushButton(QStringLiteral("two"), &h);
		v->addWidget(one);
		v->addWidget(two);
		h.resize(GridMetrics::cells(20, 4));
		h.show();
		QCoreApplication::processEvents();
		set_focus_widget(nullptr);
		InputRouter r(&h);
		const QWidget *before = h.focusWidget();
		r.on_key({Qt::Key_Tab, QStringLiteral("\t"), false, false, false});
		QCoreApplication::processEvents();
		const QWidget *after = h.focusWidget();
		printf("info: Tab from no focus moved focus from %s to %s\n",
		       before ? before->metaObject()->className() : "(none)",
		       after ? after->metaObject()->className() : "(none)");
		CHECK(after != nullptr && (after == one || after == two),
		      "Tab reaches a widget when nothing has focus yet");
	}

	// What a click on a slider's GROOVE does. section 0b carried this as the
	// copyright holder's question, because it changes what a click means
	// rather than what it looks like; it was settled on 2026-09-05 in favour
	// of the left button setting the value. The pair below holds the answer
	// so that changing it again is visible rather than silent.
	//
	// Asserted as RELATIONSHIPS, not values: the numbers depend on the
	// groove's geometry and on pageStep, and a check on 24 or 83 would be
	// measuring this test's arithmetic.
	{
		auto click_with = [&](int button, int cell) {
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			auto *sl = new QSlider(Qt::Horizontal, &h);
			sl->setRange(0, 100);
			sl->setValue(0);
			sl->setGeometry(0, 0, cw * 20, ch);
			h.resize(GridMetrics::cells(30, 4));
			h.show();
			QCoreApplication::processEvents();
			InputRouter r(&h);
			r.on_mouse({QPoint(cell, 0), button, true, false, false, 0});
			r.on_mouse({QPoint(cell, 0), button, false, true, false, 0});
			QCoreApplication::processEvents();
			return sl->value();
		};
		// A left click sets the value where it landed. Under QCommonStyle's
		// defaults this was the MIDDLE button's job and the left one paged --
		// close to unusable on a terminal, where the middle button is usually
		// spent on paste, so the cells this style makes addressable were
		// reachable only by dragging.
		const int l5 = click_with(1, 5), l10 = click_with(1, 10), l15 = click_with(1, 15);
		CHECK(l5 < l10 && l10 < l15 && l5 > 0 && l15 < 100,
		      "a left click sets a slider to where it landed");
		// And the middle button pages, so it lands in the same place wherever
		// it is clicked. The pair is what says it: one tracks the click and
		// the other does not, which is what makes either assertion mean
		// anything -- a slider that ignored the button entirely would satisfy
		// the first alone.
		const int m5 = click_with(2, 5), m15 = click_with(2, 15);
		CHECK(m5 == m15 && m5 > 0,
		      "and a middle click pages, landing in the same place either way");
	}


	// What the modifiers are FOR, end to end. A control-click on an item view
	// toggles rather than replaces the selection, and that is the behaviour a
	// terminal could not reach at all while the decoder threw the bits away --
	// SGR carries them, and every mouse event was built with Qt::NoModifier.
	//
	// The pair is what says it: the same two clicks without the modifier leave
	// ONE row selected, with it leave TWO. A check on the second alone would
	// pass against a view that never deselects anything.
	{
		const auto two_clicks = [&](bool ctrl) {
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			auto *lw = new QListWidget(&h);
			for (int i = 0; i < 4; ++i)
				lw->addItem(QStringLiteral("row %1").arg(i));
			lw->setSelectionMode(QAbstractItemView::ExtendedSelection);
			// section 7.1 records why: the default frame offsets the viewport
			// by PM_DefaultFrameWidth in BOTH axes, which is a whole cell
			// here, so a click aimed at row 0 lands in the frame above it.
			// The first version of this fixture did exactly that and read as
			// "the modifier does not arrive".
			lw->setFrameShape(QFrame::NoFrame);
			lw->setGeometry(0, 0, cw * 12, ch * 4);
			h.resize(GridMetrics::cells(14, 6));
			h.show();
			QCoreApplication::processEvents();
			InputRouter r(&h);
			// Built field by field rather than as a positional list. The first
			// version was positional and broke the moment MouseEvent grew a
			// field in the middle: every argument after it re-bound one place
			// along, so `ctrl` silently became the horizontal wheel and this
			// check went red for a reason that had nothing to do with it.
			const auto click = [&](int row, bool press, bool with_ctrl) {
				MouseEvent m;
				m.cell = QPoint(1, row);
				m.button = 1;
				m.press = press;
				m.release = !press;
				m.ctrl = with_ctrl;
				r.on_mouse(m);
			};
			click(0, true, false);
			click(0, false, false);
			click(2, true, ctrl);
			click(2, false, ctrl);
			QCoreApplication::processEvents();
			return lw->selectedItems().size();
		};
		const int plain = two_clicks(false), toggled = two_clicks(true);
		CHECK(plain == 1 && toggled == 2,
		      "a control-click adds to an item view's selection");
	}


	// ------------------------------------------------ section 5.5: drags
	// Motion was parsed by the backend and dropped by the router, and there
	// was no grab, so nothing that needs a drag worked -- section 7.2 recorded
	// the splitter as unimplemented and section 7.1 as "no grab-widget
	// branch"; they were one defect with two halves.
	//
	// Both are asserted because the halves fail separately, which was measured
	// rather than assumed: with motion delivery removed, the slider AND the
	// splitter go red; with motion kept and only the grab removed, the slider
	// still passes and the splitter alone fails. A slider drag never leaves
	// the slider, so it needs the moves and nothing more; a splitter handle is
	// one cell wide and the pointer is off it after the first move, so it
	// needs the moves to keep arriving at the widget the press landed on.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *sl = new QSlider(Qt::Horizontal, &h);
		sl->setRange(0, 100);
		sl->setValue(0);
		sl->setGeometry(0, 0, cw * 20, ch);
		h.resize(GridMetrics::cells(30, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);
		r.on_mouse({QPoint(1, 0), 1, true, false, false, 0});
		for (int x = 2; x <= 15; ++x) r.on_mouse({QPoint(x, 0), 1, false, false, true, 0});
		r.on_mouse({QPoint(15, 0), 1, false, true, false, 0});
		CHECK(sl->value() > 0, "dragging a slider moves it");
	}
	{
		// Selecting text with the mouse came free with the same change, and
		// section 7.2 had recorded selection as the untested half of the text
		// widgets. It is a third distinct shape: the target never changes and
		// the moves land inside the widget, but each one has to carry the
		// held button or QLineEdit reads them as the pointer passing over.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *le = new QLineEdit(&h);
		le->setText(QStringLiteral("hello world"));
		le->setFrame(false);
		le->setGeometry(0, 0, cw * 20, ch);
		h.resize(GridMetrics::cells(30, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);
		r.on_mouse({QPoint(0, 0), 1, true, false, false, 0});
		for (int x = 1; x <= 5; ++x) r.on_mouse({QPoint(x, 0), 1, false, false, true, 0});
		r.on_mouse({QPoint(5, 0), 1, false, true, false, 0});
		CHECK(le->selectedText() == QStringLiteral("hello"),
		      "dragging across a line edit selects the text under it");
	}
	// ------------------------------------------- section 5.5: which button
	// SGR 1006 carries the button and the backend hands it on, and on_mouse()
	// sent Qt::LeftButton whatever arrived -- the same parsed-and-discarded
	// shape as motion, one field along. The consequence was not a missing
	// feature but a wrong action: a right click ACTIVATED whatever it landed
	// on. That is what the first check is for, and it is the one that would
	// have caught this, since a check that only asked for a context menu
	// would read as an absent feature rather than a misfire.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *b = new QPushButton(QStringLiteral("Go"), &h);
		b->setGeometry(0, 0, cw * 10, ch);
		int hits = 0, asked = 0;
		QObject::connect(b, &QPushButton::clicked, [&] { ++hits; });
		b->setContextMenuPolicy(Qt::CustomContextMenu);
		QObject::connect(b, &QWidget::customContextMenuRequested, [&] { ++asked; });
		h.resize(GridMetrics::cells(30, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		r.on_mouse({QPoint(2, 0), 3, true, false, false, 0});
		r.on_mouse({QPoint(2, 0), 3, false, true, false, 0});
		CHECK(hits == 0, "a right click does not activate a button");
		CHECK(asked == 1, "a right click asks for a context menu");

		// ...and the left button still does what it did, so the mapping is a
		// mapping and not a blanket refusal of the buttons it does not know.
		r.on_mouse({QPoint(2, 0), 1, true, false, false, 0});
		r.on_mouse({QPoint(2, 0), 1, false, true, false, 0});
		CHECK(hits == 1, "a left click still activates it");
		CHECK(asked == 1, "and asks for no context menu");
	}
	{
		// End to end, because contextMenuEvent() reaching the widget is only
		// worth having if what it opens is then drawn: the menu has to be
		// picked up by the stamping filter and composited like any other
		// popup. A check that stopped at the event would pass with the
		// compositor blind to it.
		struct Ctx : QWidget {
			using QWidget::QWidget;
			QMenu *menu = nullptr;
			void contextMenuEvent(QContextMenuEvent *e) override {
				menu = new QMenu(this);
				menu->addAction(QStringLiteral("Cut"));
				menu->popup(e->globalPos());
			}
		};
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *c = new Ctx(&h);
		c->setGeometry(0, 0, cw * 20, ch * 3);
		h.resize(GridMetrics::cells(40, 10));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);
		r.on_mouse({QPoint(3, 1), 3, true, false, false, 0});
		QCoreApplication::processEvents();
		CHECK(c->menu != nullptr, "a right click opens the widget's context menu");
		CHECK(r.popups().size() == 1, "and the router tracks it as a popup");
		Compositor cc(&h, &r);
		CellBuffer bb(40, 10);
		cc.compose(bb);
		CHECK(bb.to_text().contains(QStringLiteral("Cut")),
		      "and the compositor draws it");
		if (c->menu) c->menu->close();
		QCoreApplication::processEvents();
	}

	// The third mouse feature, and the one still unexercised after motion and
	// the button were both found wrong here. The backend parses the wheel --
	// bit 64 of the SGR button word -- and the router turns it into a
	// QWheelEvent; nothing had ever checked that it arrives.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *area = new QScrollArea(&h);
		auto *tall = new QWidget;
		tall->setFixedSize(cw * 10, ch * 40);          // far taller than the view
		area->setWidget(tall);
		area->setGeometry(0, 0, cw * 20, ch * 5);
		h.resize(GridMetrics::cells(30, 8));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		const int start = area->verticalScrollBar()->value();
		MouseEvent down;
		down.cell = QPoint(2, 2);
		down.wheel = -1;                               // 64 | 1: wheel down
		for (int i = 0; i < 3; ++i) r.on_mouse(down);
		CHECK(area->verticalScrollBar()->value() > start,
		      "a wheel event scrolls the area under the pointer");

		// HOW FAR, and on the widget class an application actually scrolls.
		// Every wheel fixture here is a bare QScrollArea, whose bar is in
		// pixels; a QAbstractItemView's is in ITEM INDICES by default, which
		// is the same unit confusion the arrow-key fallback had. And the
		// fourth argument of QWheelEvent is angleDelta, which Qt defines in
		// EIGHTHS OF A DEGREE with 120 to a notch -- so a cell height put
		// there claims a notch is 19/120 of one.
		{
			QWidget wv;
			wv.setAttribute(Qt::WA_DontShowOnScreen);
			auto *lv = new QListView(&wv);
			auto *lm = new QStringListModel(&wv);
			QStringList many;
			for (int i = 0; i < 200; ++i) many << QStringLiteral("row %1").arg(i);
			lm->setStringList(many);
			lv->setModel(lm);
			lv->setFrameShape(QFrame::NoFrame);
			lv->setGeometry(0, 0, cw * 20, ch * 6);
			wv.resize(GridMetrics::cells(20, 6));
			wv.show();
			QCoreApplication::processEvents();
			InputRouter rv(&wv);
			QScrollBar *bar = lv->verticalScrollBar();
			bar->setValue(0);
			MouseEvent notch;
			notch.cell = QPoint(2, 2);
			notch.wheel = -1;
			rv.on_mouse(notch);
			const int one = bar->value();
			bar->setValue(0);
			for (int i = 0; i < 3; ++i) rv.on_mouse(notch);
			const int three = bar->value();
			// EXACTLY what a notch means, not "some". A per-item bar has a
			// singleStep of one row, so Qt scrolls it by
			// QApplication::wheelScrollLines() per notch -- the same number
			// a desktop mouse produces. Naming it rather than writing 3
			// keeps the check honest on a machine configured differently.
			const int lines = QApplication::wheelScrollLines();
			printf("info: on an item view, one notch moves %d row(s) and"
			       " three move %d, against %d line(s) per notch\n",
			       one, three, lines);
			CHECK(one == lines && three == 3 * lines,
			      "a wheel notch scrolls a notch's worth of rows on an item"
			      " view");
			GridGuard::reset();
		}

		// A wheel is neither a press nor a release, which is why the backend
		// sets neither: delivering it as a press would leave a button stuck
		// down for the rest of the session.
		CHECK(!down.press && !down.release,
		      "and carries no button state to leave stuck down");

		// PageUp steps by five rows rather than one -- the branch the arrow
		// fallback only reaches for the paging keys.
		area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
		const int before_page = area->verticalScrollBar()->value();
		r.on_key({Qt::Key_PageUp, {}, false, false, false});
		const int paged = before_page - area->verticalScrollBar()->value();
		r.on_key({Qt::Key_Down, {}, false, false, false});
		printf("info: PageUp scrolled %d pixels, %d rows (floor is five)\n",
		       paged, paged / ch);
		CHECK(paged >= 5 * ch,
		      "PageUp scrolls five rows, not one");
	}

	// The two sinks a terminal drives that nothing had called. A resize
	// arrives from SIGWINCH or from the terminal's own report, and the window
	// must follow it or every widget is laid out for a size that is gone.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		h.resize(GridMetrics::cells(20, 6));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		int frames = 0;
		r.frame_requested = [&] { ++frames; };
		r.on_resize(QSize(40, 12));
		CHECK(h.size() == QSize(40 * cw, 12 * ch),
		      "a resize sink resizes the window to the new cell count");
		CHECK(frames > 0, "and asks for a frame, the old one being the wrong size");

		const int after_resize = frames;
		r.on_focus_change(true);
		CHECK(frames > after_resize,
		      "and a focus change asks for one too, since focus is drawn");

		// Quit keys are matched before anything is delivered, so the key must
		// not also reach the focus widget. Asserted by its absence rather than
		// by quitting, which a suite cannot observe.
		auto *edit = new QLineEdit(&h);
		edit->setGeometry(0, 0, cw * 10, ch);
		edit->show();                  // created after its parent was shown
		edit->setFocus();
		set_focus_widget(edit);
		QCoreApplication::processEvents();
		// The ordinary case FIRST. Calling qApp->quit() puts the application
		// into a state where a later processEvents() need not deliver, so a
		// test that quits and then expects typing is asserting the order it
		// happens to have written rather than the behaviour -- measured, it
		// failed exactly that way round.
		r.on_key({Qt::Key_Q, QStringLiteral("q"), false, false, false});
		CHECK(edit->text() == QStringLiteral("q"),
		      "a key with no quit binding types into the focus widget");

		r.set_quit_keys({{Qt::Key_Q, QStringLiteral("q"), false, false, false}});
		r.on_key({Qt::Key_Q, QStringLiteral("q"), false, false, false});
		CHECK(edit->text() == QStringLiteral("q"),
		      "and the same key, once it is a quit key, is consumed not typed");
	}

	// The splitter goes last and takes the guard with it, because it lays its
	// panes out off the grid and always has: a 300px splitter with a one-cell
	// handle splits evenly into 145/145, before any input is involved. That is
	// section 7.8's open question -- whether qtty snaps child geometry -- and
	// implementing the drag surfaced it rather than caused it. Asserting the
	// count rather than prose means the day snapping lands this check goes red
	// and the record has to be brought up to date.
	CHECK(GridGuard::violations() == 0, "nothing before the splitter left the grid");
	GridGuard::reset();
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *sp = new QSplitter(Qt::Horizontal, &h);
		sp->addWidget(new QLabel(QStringLiteral("A")));
		sp->addWidget(new QLabel(QStringLiteral("B")));
		sp->setGeometry(0, 0, cw * 30, ch * 4);
		h.resize(GridMetrics::cells(30, 4));
		h.show();
		QCoreApplication::processEvents();
		const QList<int> before = sp->sizes();
		QSplitterHandle *handle = sp->handle(1);
		const int hcell = handle ? handle->mapTo(&h, QPoint(0, 0)).x() / cw : -1;
		InputRouter r(&h);
		GridSnap::reset();                   // so the count below is this drag's
		r.on_mouse({QPoint(hcell, 1), 1, true, false, false, 0});
		for (int x = hcell; x <= 22; ++x) r.on_mouse({QPoint(x, 1), 1, false, false, true, 0});
		r.on_mouse({QPoint(22, 1), 1, false, true, false, 0});
		CHECK(sp->sizes() != before, "dragging a splitter handle resizes its panes");

		// A stray release far from the handle was checked here too and is
		// gone: it passes with the grab removed, because the cell it names
		// holds a QLabel and a release on a QLabel does nothing either way. A
		// check that cannot fail for the reason it was written is worse than
		// no check, so the splitter drag above carries the grab on its own --
		// which the sabotage run confirms it does.

		// Still off the grid WITH GridSnap installed, which is the thing
		// worth asserting now that setup() installs it. A splitter assigns
		// its panes' geometry itself and re-asserts it, so the correction is
		// fought rather than missed -- the same shape as a fixed size the
		// snap cannot move, and the same division of labour: setSizes() in
		// cell multiples is the application's to call. Section 7.8 carries
		// it.
		//
		// And the snap WINS, which is not what the sentence here used to
		// say. Measured after the drag:
		//
		//     pane 0    230x76+0+0     on the grid
		//     pane 1     60x76+240+0   on the grid
		//     splitter  300x76, handle 10 px = one cell
		//     32 snaps, and 24 guard violations
		//
		// The 24 are the ASSIGNMENTS the guard saw before the snap corrected
		// them, not a lasting state -- and the check that stood here read
		// exactly that counter, so it was reporting a property of the HARNESS
		// rather than of the splitter. The guard sees pre-snap geometry only
		// when it is installed after GridSnap; a debug build swaps the two,
		// because setup() installs the guard itself under !QT_NO_DEBUG while
		// in release main.cpp installs it after setup() has installed the
		// snap. Measured: the identical check passed in release and failed
		// under DEBUG=1, with nothing about the splitter different in either.
		//
		// So it reads the panes. `snapped() > 0` is the other half: without
		// it, a splitter that never left the grid would satisfy `off == 0`
		// and the check would say nothing about snapping at all.
		int off = 0;
		for (int i = 0; i < sp->count(); ++i)
			if (!GridMetrics::is_aligned(sp->widget(i)->geometry())) ++off;
		CHECK(GridSnap::installed() && off == 0 && GridSnap::snapped() > 0,
		      "a QSplitter's panes are dragged off the grid and snapped back");
	}
	GridGuard::reset();

	{
		// A popup CLOSING has to ask for a frame, and nothing asserted it.
		// The showing half is what a menu test naturally covers, because a
		// menu that never appeared fails visibly; a menu that never goes
		// away is drawn from a frame nobody asked for, so the screen keeps
		// showing it until something unrelated triggers a redraw. That is
		// the ghost-menu class, and it is invisible to a test that only
		// looks at what compose() produces while the menu is up.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		h.resize(GridMetrics::cells(30, 8));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);
		int frames = 0;
		r.frame_requested = [&frames] { ++frames; };

		QMenu menu(&h);
		QAction *cut = menu.addAction(QStringLiteral("Cut"));
		bool fired = false;
		QObject::connect(cut, &QAction::triggered, &menu, [&fired] { fired = true; });
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		const int after_show = frames;
		CHECK(after_show > 0, "a popup appearing asks for a frame");
		CHECK(r.popups().size() == 1, "and is tracked while it is up");

		// A click INSIDE a popup goes to the popup, which nothing had ever
		// sent: every mouse test here clicks the window under one, so the
		// branch that finds a popup by hit test had never been taken.
		//
		// What used to stand here was a MISDIAGNOSIS, and it is worth
		// keeping the correction visible because it kept a defect of this
		// project's filed as Qt's for as long as it stood: "a QMenu under
		// the offscreen platform has no popup grab and does not activate
		// from a synthetic press". The platform has nothing to do with it,
		// and there is no grab involved. QMenuPrivate::hasMouseMoved()
		// counts the mouse MOTIONS a menu has received and refuses a press
		// until there are more than six; a terminal in \033[?1002h reports
		// no bare motion at all, so the count was zero and the press
		// dismissed the menu. The router synthesises that motion now, the
		// same way it synthesises Enter, Leave and QContextMenuEvent, and
		// the assertion the old comment said could not be made is the first
		// one below.
		struct Catcher : QWidget {
			using QWidget::QWidget;
			int presses = 0;
			void mousePressEvent(QMouseEvent *) override { ++presses; }
		};
		auto *under = new Catcher(&h);
		under->setGeometry(0, 0, cw * 20, ch * 4);
		under->show();
		QCoreApplication::processEvents();

		const QRect ag = menu.actionGeometry(cut);
		const QPoint hit = menu.mapToGlobal(ag.center());
		const QPoint hc(hit.x() / GridMetrics::cw(), hit.y() / GridMetrics::ch());
		r.on_mouse({hc, 1, true, false, false, 0});
		r.on_mouse({hc, 1, false, true, false, 0});
		QCoreApplication::processEvents();
		const int through = under->presses;
		const bool fired_by_click = fired;

		// A FRESH menu for the close, never clicked. The click above is gone
		// by now either way -- it used to dismiss the menu and now it fires
		// its item, and an item that fires closes the menu it is in -- so
		// closing this one again removes nothing and the hide branch never
		// runs. The assertion failed with the code correct, which is the
		// useful direction for it to fail in.
		menu.close();
		QCoreApplication::processEvents();
		QMenu again(&h);
		again.addAction(QStringLiteral("Copy"));
		again.popup(QPoint(0, 0));
		QCoreApplication::processEvents();

		// Measured across the CLOSE alone. Compared against the count taken
		// when the menu opened, this passed with the hide branch's
		// frame_requested() deleted, because on_mouse() ends by asking for a
		// frame too and the clicks had already moved the number. A counter
		// several things increment says nothing unless it is read either
		// side of the one under test.
		const int before_close = frames;
		again.close();
		QCoreApplication::processEvents();
		const int after_close = frames;

		// The pair for the click check, and without it that check passes for
		// a router that delivers no click anywhere: the SAME cell, once the
		// popup is gone, must reach the widget underneath.
		r.on_mouse({hc, 1, true, false, false, 0});
		r.on_mouse({hc, 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CHECK(fired_by_click,
		      "a click on a menu item triggers it");
		CHECK(through == 0 && under->presses > 0,
		      "a click inside a popup does not fall through to the window");
		CHECK(after_close > before_close,
		      "and a popup going away asks for a frame of its own");
		CHECK(r.popups().isEmpty(), "and stops being tracked");
	}

	{
		// A click OUTSIDE an open popup. The hit test walks the popup stack
		// and, finding nothing that contains the point, used to fall through
		// to the modal-or-window branch -- so the click went to whatever sat
		// behind the popup while the popup stayed up and kept the keyboard.
		// Measured twice before the fix: a catcher widget took the press,
		// and a QPushButton behind an open QMenu emitted clicked() with the
		// menu still visible, still the one entry in popups(), and still
		// what key_target() named.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *behind = new QPushButton(QStringLiteral("Behind"), &h);
		behind->setGeometry(0, ch * 6, cw * 10, ch);
		int hits = 0;
		QObject::connect(behind, &QPushButton::clicked, [&hits] { ++hits; });
		h.resize(GridMetrics::cells(30, 10));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		QMenu menu(&h);
		menu.addAction(QStringLiteral("Cut"));
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();

		const QPoint c = behind->geometry().center();
		const QPoint cell(c.x() / cw, c.y() / ch);
		const QPoint px(cell.x() * cw + cw / 2, cell.y() * ch + ch / 2);
		// That the cell is outside the menu, asserted rather than assumed. A
		// menu tall enough to cover the button would make every check below
		// pass for the opposite reason, and the height of a menu is the
		// style's business rather than this fixture's.
		CHECK(!menu.geometry().contains(px),
		      "the cell this clicks is outside the open menu");

		r.on_mouse({cell, 1, true, false, false, 0});
		r.on_mouse({cell, 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CHECK(hits == 0,
		      "a click outside an open popup does not reach the window behind");
		CHECK(!menu.isVisible() && r.popups().isEmpty(),
		      "and closes the popup stack instead");

		// The pair, and without it both of those pass for a router that
		// delivers no click anywhere: the SAME cell, with the popup gone,
		// must press the button.
		r.on_mouse({cell, 1, true, false, false, 0});
		r.on_mouse({cell, 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CHECK(hits == 1, "and the same cell presses it once the popup is gone");
		GridGuard::reset();
	}

	{
		// WHICH item a click activates. The check above says a click on a
		// menu item fires it; this one varies the row and nothing else, so a
		// router that fired the menu's first action -- or all of them --
		// stops passing.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		h.resize(GridMetrics::cells(30, 10));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		QMenu menu(&h);
		QAction *cut = menu.addAction(QStringLiteral("Cut"));
		QAction *copy = menu.addAction(QStringLiteral("Copy"));
		int cut_fired = 0, copy_fired = 0;
		QObject::connect(cut, &QAction::triggered, [&cut_fired] { ++cut_fired; });
		QObject::connect(copy, &QAction::triggered, [&copy_fired] { ++copy_fired; });
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();

		const QPoint top = menu.mapToGlobal(menu.actionGeometry(cut).center());
		const QPoint low = menu.mapToGlobal(menu.actionGeometry(copy).center());
		// A terminal addresses a cell, not a pixel, so two items that share
		// a row are one item as far as this check can tell.
		CHECK(top.y() / ch != low.y() / ch,
		      "the two menu items occupy different terminal rows");
		printf("info: the menu's items are on rows %d and %d\n",
		       top.y() / ch, low.y() / ch);

		const QPoint cell(low.x() / cw, low.y() / ch);
		r.on_mouse({cell, 1, true, false, false, 0});
		r.on_mouse({cell, 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CHECK(copy_fired == 1, "a click on the second menu item fires that item");
		CHECK(cut_fired == 0, "and not the one above it");
		CHECK(!menu.isVisible() && r.popups().isEmpty(),
		      "and the menu goes away when its item fires");
		GridGuard::reset();
	}

	{
		// A mnemonic opening a submenu whose owner is NOT a menu bar. The
		// menu-bar case is covered because that is where a reader expects a
		// menu; this is the other branch, and it decides where the submenu
		// appears -- at the owning widget's corner rather than at the
		// terminal's origin, which is where a null owner would put it.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		h.resize(GridMetrics::cells(40, 10));
		auto *host = new QWidget(&h);
		host->setGeometry(cw * 6, ch * 3, cw * 10, ch * 2);
		auto *act = new QAction(QStringLiteral("&Edit"), host);
		auto *sub = new QMenu(host);
		sub->addAction(QStringLiteral("Undo"));
		act->setMenu(sub);
		host->addAction(act);
		h.show();
		QCoreApplication::processEvents();

		InputRouter r(&h);
		r.on_key({0, QStringLiteral("e"), false, true, false});
		QCoreApplication::processEvents();
		// Where it opened, not merely that it did: a submenu at the origin is
		// what a null owner produces, and it is the failure this branch
		// exists to prevent -- the menu appears in the corner of the terminal
		// with nothing beside it to say what it belongs to.
		CHECK(sub->isVisible(), "a mnemonic on an action with a submenu opens it");
		CHECK(sub->pos() == host->mapToGlobal(QPoint(0, 0)),
		      "at the corner of the widget that owns it, not at the origin");
		sub->close();
		QCoreApplication::processEvents();
	}

	{
		// And the menu-bar branch of the same code, which is about
		// ATTACHMENT rather than about position. match_mnemonic() used to
		// call QMenu::popup() at a point it worked out from the bar's
		// actionGeometry(); that puts the menu in the right place and leaves
		// QMenuPrivate::causedPopup unset, so the menu does not know which
		// bar opened it and the bar does not know it is open.
		//
		// Nothing looked wrong, which is why it stood: the menu drew, the
		// keys reached it, and its items fired. What could not happen was
		// everything Qt hangs off causedPopup -- QMenu::keyPressEvent's
		// menu-bar traversal tests qobject_cast<QMenuBar *>(topCausedWidget())
		// and could never fire, and QMenu::hideEvent's matching clean-up
		// could not either. Measured: activeAction() stayed null and Right
		// did nothing at all.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = new QMenuBar(&h);
		bar->setGeometry(0, 0, cw * 30, ch);
		QMenu *file = bar->addMenu(QStringLiteral("&File"));
		file->addAction(QStringLiteral("Open"));
		QMenu *edit = bar->addMenu(QStringLiteral("&Edit"));
		edit->addAction(QStringLiteral("Undo"));
		h.resize(GridMetrics::cells(30, 8));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		r.on_key({0, QStringLiteral("f"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(file->isVisible() && bar->activeAction() == file->menuAction(),
		      "a mnemonic opens its menu through the bar, which marks it active");

		// The pair, and the whole reason the attachment is worth having:
		// Qt's own menu-bar traversal, which no key could reach while
		// causedPopup was unset.
		r.on_key({Qt::Key_Right, {}, false, false, false});
		QCoreApplication::processEvents();
		CHECK(!file->isVisible() && edit->isVisible(),
		      "and Right walks the bar from File to Edit");
		file->close();
		edit->close();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	{
		// A checkable menu item's mark. A menu is where a toggle usually
		// lives, and "Wrap" with no tick beside it says nothing about whether
		// it is on: the state was in the action and nowhere on the screen.
		//
		// Driven through the compositor rather than by calling the style,
		// because a menu is a top-level and render_once() on the window it
		// belongs to does not contain it -- the same reason the context menu
		// above is asserted this way.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		h.resize(GridMetrics::cells(30, 8));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		QMenu menu(&h);
		QAction *wrap = menu.addAction(QStringLiteral("Wrap"));
		wrap->setCheckable(true);
		wrap->setChecked(true);
		QAction *off = menu.addAction(QStringLiteral("Trim"));
		off->setCheckable(true);
		menu.addAction(QStringLiteral("Quit"));
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();

		Compositor comp(&h, &r);
		CellBuffer buf(30, 8);
		comp.compose(buf);
		const QString drawn = buf.to_text();
		// All three items, which is what makes it an assertion about the
		// MARK rather than about a tick appearing somewhere: a checked item
		// carries one, a checkable-but-unchecked item carries a space in the
		// same column so the two line up, and an ordinary item carries
		// neither.
		CHECK(drawn.contains(QStringLiteral("✓ Wrap")),
		      "a checked menu item is ticked");
		CHECK(drawn.contains(QStringLiteral("  Trim")),
		      "an unchecked checkable one keeps the column, so they align");
		CHECK(drawn.contains(QStringLiteral("Quit")),
		      "and an ordinary item is unaffected");
		menu.close();
		QCoreApplication::processEvents();
	}

	{
		// What a user actually DOES, asserted end to end. The mirror of the
		// rendering sweep, and it found no defect -- which is worth having as
		// a test rather than as a sentence, because these are the paths every
		// application depends on and nothing here covered them: the key tests
		// stop at a QLineEdit and the mouse tests at a button.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		h.resize(GridMetrics::cells(40, 12));
		auto *btn = new QPushButton(QStringLiteral("Go"), &h);
		btn->setGeometry(0, 0, cw * 6, ch);
		auto *chk = new QCheckBox(QStringLiteral("On"), &h);
		chk->setGeometry(0, ch, cw * 8, ch);
		auto *sld = new QSlider(Qt::Horizontal, &h);
		sld->setRange(0, 10);
		sld->setValue(5);
		sld->setGeometry(0, ch * 2, cw * 10, ch);
		auto *cmb = new QComboBox(&h);
		cmb->addItems({QStringLiteral("a"), QStringLiteral("b")});
		cmb->setGeometry(0, ch * 3, cw * 10, ch);
		auto *spn = new QSpinBox(&h);
		spn->setRange(0, 10);
		spn->setValue(3);
		spn->setGeometry(0, ch * 4, cw * 8, ch);
		auto *tabs = new QTabBar(&h);
		tabs->addTab(QStringLiteral("T1"));
		tabs->addTab(QStringLiteral("T2"));
		tabs->setGeometry(0, ch * 5, cw * 20, ch);
		h.show();
		QCoreApplication::processEvents();

		InputRouter r(&h);
		int clicks = 0;
		QObject::connect(btn, &QPushButton::clicked, btn, [&clicks] { ++clicks; });
		const auto press = [&](QWidget *w, int k, const QString &t = QString()) {
			w->setFocus();
			set_focus_widget(w);
			QCoreApplication::processEvents();
			r.on_key({k, t, false, false, false});
			QCoreApplication::processEvents();
		};
		const auto click = [&](int x, int y) {
			r.on_mouse({QPoint(x, y), 1, true, false, false, 0});
			r.on_mouse({QPoint(x, y), 1, false, true, false, 0});
			QCoreApplication::processEvents();
		};

		press(btn, Qt::Key_Space, QStringLiteral(" "));
		CHECK(clicks == 1, "space on a focused button presses it");
		press(chk, Qt::Key_Space, QStringLiteral(" "));
		CHECK(chk->isChecked(), "and on a checkbox ticks it");
		// Each of these asserts the DIRECTION as well as the change: a widget
		// that moved on any key would satisfy "the value differs".
		press(sld, Qt::Key_Right);
		CHECK(sld->value() == 6, "right on a slider moves it up one step");
		press(cmb, Qt::Key_Down);
		CHECK(cmb->currentIndex() == 1, "down on a combo takes the next item");
		press(spn, Qt::Key_Up);
		CHECK(spn->value() == 4, "up on a spin box steps it");

		click(1, 1);
		CHECK(!chk->isChecked(), "a click on a checkbox toggles it back");
		click(14, 5);
		CHECK(tabs->currentIndex() == 1, "and a click on a tab selects it");
	}


	// The horizontal wheel, which was delivered as a vertical one. SGR puts
	// the axis in bit 1 of the button word -- 64/65 up/down, 66/67 left/right
	// -- and the decoder read bit 0 alone, so a sideways scroll scrolled the
	// view up and down instead.
	//
	// The pair that says it is the horizontal bar moving in OPPOSITE
	// directions for the two horizontal reports, and not moving at all for a
	// vertical one. The second half is what the old code failed: it turned a
	// wheel-left into a wheel-up, which a check on "the bar moved" would not
	// have noticed.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *area = new QScrollArea(&h);
		// section 7.1: the default frame offsets the viewport in both axes.
		area->setFrameShape(QFrame::NoFrame);
		auto *inner = new QLabel(QString(200, QLatin1Char('x')));
		inner->setMinimumWidth(cw * 200);
		area->setWidget(inner);
		area->setGeometry(0, 0, cw * 10, ch * 3);
		h.resize(GridMetrics::cells(12, 5));
		h.show();
		QCoreApplication::processEvents();
		QScrollBar *hb = area->horizontalScrollBar();
		const auto scroll = [&](int wx, int wy) {
			hb->setValue(hb->maximum() / 2);
			const int before = hb->value();
			InputRouter r(&h);
			MouseEvent m;
			m.cell = QPoint(4, 1);
			m.wheel = wy;
			m.wheel_x = wx;
			r.on_mouse(m);
			QCoreApplication::processEvents();
			return hb->value() - before;
		};
		const int left = scroll(1, 0), right = scroll(-1, 0), vert = scroll(0, 1);
		CHECK(left < 0 && right > 0 && vert == 0,
		      "a horizontal wheel moves the horizontal bar, and only it");
		GridGuard::reset();
	}



	// A spin box's two arrows, and whether a click can reach both. It drew a
	// single plus-minus glyph in one cell, and SC_SpinBoxUp and SC_SpinBoxDown were 10x19
	// rectangles at the SAME cell, offset by half a row -- +100+0 and +100+9.
	// On a one-cell spin box they overlapped, Qt picked the first, and **no
	// cell decremented**: from 50, the arrow cell gave 51 and nothing gave 49.
	//
	// Half a row cannot be hit on a grid, so the answer was not a better
	// rectangle but a second cell. This is the category the metric sweep
	// named: an inset INSIDE a widget, which the snap-up list cannot reach.
	//
	// The pair is the assertion. "Up increments" passed against the broken
	// version; it is the two together, from the same starting value, that say
	// the arrows are separately reachable.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *sp = new QSpinBox(&h);
		sp->setRange(0, 100);
		sp->setGeometry(0, 0, cw * 12, ch);
		h.resize(GridMetrics::cells(14, 3));
		h.show();
		QCoreApplication::processEvents();
		const auto click_cell = [&](int x) {
			sp->setValue(50);
			InputRouter r2(&h);
			MouseEvent m;
			m.cell = QPoint(x, 0);
			m.button = 1;
			m.press = true;
			r2.on_mouse(m);
			m.press = false;
			m.release = true;
			r2.on_mouse(m);
			QCoreApplication::processEvents();
			return sp->value();
		};
		CHECK(click_cell(9) == 51 && click_cell(10) == 49,
		      "a spin box's up and down arrows are separately clickable");
		GridGuard::reset();
	}



	// A VERTICAL scroll bar, which nothing had exercised: every slider and bar
	// this suite drove was horizontal, and vertical is where a length metric
	// meant for the other axis would go wrong.
	//
	// Its sub-control rectangles are all fractional -- the arrows are 10x10,
	// **0.53 of a row**, and the thumb is 1.37 rows at 2.32 -- so the obvious
	// expectation after the spin box was another unreachable control. It is
	// not: a click lands at the CELL'S CENTRE, and with five sub-controls
	// spread down six rows each centre falls in the right rectangle.
	//
	// Which sharpens the rule the last three fixes were converging on. The
	// fault is not a fractional rectangle; it is **two sub-controls sharing a
	// cell** -- the spin box's arrows -- or content overlapping a frame row --
	// the group box, the popup. A fractional rectangle with one meaning per
	// cell is harmless.
	//
	// It is harmless by arithmetic that nothing states, though, so this pins
	// the behaviour rather than the rectangles: step, page, thumb, page, step.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *sb = new QScrollBar(Qt::Vertical, &h);
		sb->setRange(0, 100);
		sb->setGeometry(0, 0, cw, ch * 6);
		h.resize(GridMetrics::cells(8, 7));
		h.show();
		QCoreApplication::processEvents();
		const auto click_row = [&](int y) {
			sb->setValue(50);
			InputRouter r2(&h);
			MouseEvent m;
			m.cell = QPoint(0, y);
			m.button = 1;
			m.press = true;
			r2.on_mouse(m);
			m.press = false;
			m.release = true;
			r2.on_mouse(m);
			QCoreApplication::processEvents();
			return sb->value();
		};
		const int top = click_row(0), up = click_row(1);
		const int thumb = click_row(2), down = click_row(4), bottom = click_row(5);
		// Row 3, which this fixture used to skip. The thumb is drawn in row
		// 2 and rows 3 and 4 are both drawn as track, so both must page.
		const int gap = click_row(3);
		// Relationships, not the numbers: a step is one, a page is more than
		// one, the thumb moves nothing, and the two ends go opposite ways.
		CHECK(top == 49 && bottom == 51, "a vertical scroll bar's arrow rows step by one");
		CHECK(up < top && down > bottom, "its page rows move further than a step");
		CHECK(thumb == 50, "and a click on the thumb moves nothing");
		printf("info: rows 0..5 give %d %d %d %d %d %d\n",
		       top, up, thumb, gap, down, bottom);
		CHECK(gap > 50 && gap == down,
		      "and every row drawn as track pages, including the one below"
		      " the thumb");

		// At a cell whose height is EVEN, which is the case the old
		// behaviour could not survive. Fusion's arrow button is
		// PM_ScrollBarExtent along the axis -- cw -- while a click lands at
		// ch/2, so the top arrow was hit only where cw > ch/2. At 10x19 that
		// is 10 > 9, true by one pixel because 19 is odd; at 8x16 it is
		// 8 > 8, false, and the arrow paged instead of stepping. The cells
		// come from the drawing now, so the question does not arise -- and
		// this is the check that says so rather than the arithmetic.
		{
			const int was_cw = GridMetrics::cw(), was_ch = GridMetrics::ch();
			GridMetrics::set(8, 16);
			QWidget h2;
			h2.setAttribute(Qt::WA_DontShowOnScreen);
			auto *sb2 = new QScrollBar(Qt::Vertical, &h2);
			sb2->setRange(0, 100);
			sb2->setGeometry(0, 0, 8, 16 * 6);
			h2.resize(GridMetrics::cells(8, 7));
			h2.show();
			QCoreApplication::processEvents();
			const auto click2 = [&](int y) {
				sb2->setValue(50);
				InputRouter r3(&h2);
				MouseEvent m;
				m.cell = QPoint(0, y);
				m.button = 1;
				m.press = true;
				r3.on_mouse(m);
				m.press = false;
				m.release = true;
				r3.on_mouse(m);
				QCoreApplication::processEvents();
				return sb2->value();
			};
			const int t2 = click2(0), b2 = click2(5);
			GridMetrics::set(was_cw, was_ch);
			printf("info: on an 8x16 cell the arrow rows give %d and %d\n",
			       t2, b2);
			CHECK(t2 == 49 && b2 == 51,
			      "and the arrow rows still step by one where the cell height"
			      " is even");
			GridGuard::reset();
		}
		GridGuard::reset();
	}



	// A slider is drawn as one handle cell over the whole length, and its
	// hit test was left to Fusion, which places a THREE-cell handle over
	// `length - 3 cells`. Two different mappings of value to position, so a
	// click on the handle the user can see misses it whenever the two
	// disagree -- and on a vertical slider the miss does not even reach the
	// groove, Fusion centring a seven-pixel groove that no cell centre falls
	// inside. The press is then ignored entirely.
	{
		QWidget sh;
		sh.setAttribute(Qt::WA_DontShowOnScreen);
		auto *sl = new QSlider(Qt::Vertical, &sh);
		sl->setRange(0, 100);
		// TWO cells wide, which is what QSlider's own size hint gives for a
		// vertical one: PM_SliderThickness is a cell, and Qt snaps the hint
		// up. It is also the width where the old hit test failed hardest --
		// Fusion centres a seven-pixel groove on the widget's centre, so at
		// 20 px it spans 6..12 and NEITHER cell centre, 5 nor 15, is inside
		// it. A click that missed the handle then reached nothing at all.
		sl->setGeometry(0, 0, cw * 2, ch * 6);
		sh.resize(GridMetrics::cells(8, 7));
		sh.show();
		QCoreApplication::processEvents();
		const auto click_at = [&](int y) {
			sl->setValue(50);
			InputRouter r4(&sh);
			MouseEvent m;
			m.cell = QPoint(0, y);
			m.button = 1;
			m.press = true;
			r4.on_mouse(m);
			m.press = false;
			m.release = true;
			r4.on_mouse(m);
			QCoreApplication::processEvents();
			return sl->value();
		};
		int v[6];
		for (int i = 0; i < 6; ++i) v[i] = click_at(i);
		printf("info: slider rows 0..5 from 50 give %d %d %d %d %d %d\n",
		       v[0], v[1], v[2], v[3], v[4], v[5]);
		// A vertical slider's maximum is at the TOP -- upsideDown is true by
		// default, which the drawing reads. So a click above the handle
		// raises the value and one below lowers it, and the assertion is
		// that ORDER rather than any number: whatever the page step is, the
		// rows have to be monotonic in the direction the picture implies.
		bool monotonic = true;
		for (int i = 1; i < 6; ++i)
			if (v[i] > v[i - 1]) monotonic = false;
		CHECK(monotonic && v[0] > 50 && v[5] < 50,
		      "a click on a vertical slider moves it toward the row clicked");
		GridGuard::reset();
	}

	// An item's check box is drawn at cells 1..3 -- one cell of indent, then
	// "[x]" -- by both the style and the delegate. Qt's hit rectangle for it
	// is SE_ItemViewItemCheckIndicator, which QCommonStyle builds from
	// PM_IndicatorWidth and a one-PIXEL margin, so it starts at the item's
	// left edge plus one pixel and spans three cells' worth: cells 0, 1 and
	// 2. The drawn box and the live rectangle are one cell apart, at every
	// cell size -- not a parity accident like the scroll bar's arrows.
	{
		QWidget ch_host;
		ch_host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *lv = new QListView(&ch_host);
		auto *im = new QStandardItemModel(&ch_host);
		auto *it = new QStandardItem(QStringLiteral("item"));
		it->setCheckable(true);
		it->setCheckState(Qt::Unchecked);
		im->appendRow(it);
		lv->setModel(im);
		lv->setFrameShape(QFrame::NoFrame);
		lv->setGeometry(0, 0, cw * 12, ch * 3);
		ch_host.resize(GridMetrics::cells(12, 3));
		ch_host.show();
		QCoreApplication::processEvents();

		const auto click_cell = [&](int x) {
			it->setCheckState(Qt::Unchecked);
			InputRouter rc(&ch_host);
			MouseEvent m;
			m.cell = QPoint(x, 0);
			m.button = 1;
			m.press = true;
			rc.on_mouse(m);
			m.press = false;
			m.release = true;
			rc.on_mouse(m);
			QCoreApplication::processEvents();
			return it->checkState() == Qt::Checked;
		};
		const bool indent = click_cell(0), open_b = click_cell(1);
		const bool mark = click_cell(2), close_b = click_cell(3);
		printf("info: check box cells 0..3 toggle: %d %d %d %d\n",
		       int(indent), int(open_b), int(mark), int(close_b));
		// The three cells the box is DRAWN in toggle it, and the blank
		// indent cell beside it does not. Asserting both halves, because
		// "cell 3 works" is satisfied by a rectangle covering the whole row.
		CHECK(!indent && open_b && mark && close_b,
		      "the cells an item's check box is drawn in are the cells that"
		      " toggle it");
		GridGuard::reset();
	}

	// A tool button with a drop-down draws a down-arrow glyph in the cell before its
	// closing bracket, which is the affordance saying a menu is there. Qt
	// asks SC_ToolButtonMenu whether a press was in the menu area, and
	// QCommonStyle builds that from PM_MenuButtonIndicator -- 12 px,
	// ungridded -- so the live band was the last 12 px of the button. At a
	// ten-pixel cell that is the closing BRACKET and not the arrow, and
	// pressing the arrow fired the default action instead.
	//
	// Asserted on the RECTANGLE rather than by pressing. The behavioural
	// form was written first and had to be withdrawn: with the fix in place
	// the press genuinely opens a QMenu, and a real popup under the
	// offscreen platform took the suite down with it. The rectangle is what
	// QToolButton::mousePressEvent tests, so this asks the same question
	// without a popup -- and the cell centres are what a terminal click
	// becomes, per on_mouse().
	{
		QWidget tb_host;
		tb_host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *tb = new QToolButton(&tb_host);
		auto *act = new QAction(QStringLiteral("Cut"), tb);
		tb->setDefaultAction(act);
		auto *menu = new QMenu(tb);
		menu->addAction(QStringLiteral("More"));
		tb->setMenu(menu);
		tb->setPopupMode(QToolButton::MenuButtonPopup);
		tb->setGeometry(0, 0, cw * 8, ch);
		tb_host.resize(GridMetrics::cells(10, 3));
		tb_host.show();
		QCoreApplication::processEvents();

		QStyleOptionToolButton o;
		o.initFrom(tb);
		o.rect = tb->rect();
		o.features = QStyleOptionToolButton::MenuButtonPopup;
		const QRect band = tb->style()->subControlRect(
		    QStyle::CC_ToolButton, &o, QStyle::SC_ToolButtonMenu, tb);
		// Cell 6 is the arrow on an eight-cell button, cell 7 the bracket.
		const QPoint arrow(6 * cw + cw / 2, ch / 2);
		const QPoint bracket(7 * cw + cw / 2, ch / 2);
		const QPoint label(1 * cw + cw / 2, ch / 2);
		printf("info: the menu band is %d..%d px; arrow %d bracket %d"
		       " label %d\n", band.left(), band.right(),
		       int(band.contains(arrow)), int(band.contains(bracket)),
		       int(band.contains(label)));
		CHECK(band.contains(arrow) && !band.contains(label),
		      "a tool button's menu opens from the cell its arrow is drawn"
		      " in");
		GridGuard::reset();
	}

	// ---- the pointer enters and leaves ----
	{
		// Nothing sent Enter or Leave. QApplicationPrivate does it from the
		// platform's mouse events, and there is no platform -- the same gap
		// the right-press context menu had two lines away in the same
		// function. Measured before the fix, sweeping the pointer over every
		// cell of a form: underMouse() false on every widget, while Qt had
		// set WA_Hover on the push button, so it was prepared to repaint for
		// a hover that could never arrive.
		struct Counting : QWidget {
			int enters = 0, leaves = 0;
			using QWidget::QWidget;
			void enterEvent(QEnterEvent *) override { ++enters; }
			void leaveEvent(QEvent *) override { ++leaves; }
		};
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *top = new Counting;
		auto *bottom = new Counting;
		v->addWidget(top);
		v->addWidget(bottom);
		win.resize(GridMetrics::cells(10, 2));
		win.show();
		QCoreApplication::processEvents();

		Qtty::InputRouter router(&win);
		auto move_to = [&](int x, int y) {
			router.on_mouse({ QPoint(x, y), 0, false, false, true,
			                  0, 0, false, false, false });
		};

		move_to(2, 0);
		CHECK(top->enters == 1 && top->underMouse(),
		      "moving onto a widget enters it and it knows it");
		// Along the SAME widget: a widget still under the pointer must not be
		// told it was left and re-entered, which an implementation that sends
		// on every move would do -- an application's enterEvent() firing once
		// per cell of travel.
		move_to(5, 0);
		move_to(8, 0);
		CHECK(top->enters == 1 && top->leaves == 0,
		      "and moving within it sends nothing more");
		move_to(2, 1);
		CHECK(top->leaves == 1 && !top->underMouse()
		      && bottom->enters == 1 && bottom->underMouse(),
		      "while moving to another leaves the first and enters the second");
		// The window is an ancestor of both and was never left, so it keeps
		// its own answer throughout: underMouse() is true for a container
		// while the pointer is over its child.
		// The check that pins the mechanism. Only the DIFFERENCE between the
		// two ancestor chains gets an event, and a sabotage that sends to
		// every widget in both reddens exactly this line: the window is an
		// ancestor of both children, so it would be told it was left while
		// the pointer never went outside it.
		CHECK(win.underMouse(),
		      "and the window under both stays entered the whole time");
	}


	// ---- a second click is a double click ----
	{
		// Measured before this existed: two clicks in the same cell gave two
		// presses and zero double-click events, so QWidget::
		// mouseDoubleClickEvent() never ran anywhere -- itemDoubleClicked, a
		// line edit selecting a word, a tree expanding on double click were
		// all dead. The platform layer does this from
		// QApplication::doubleClickInterval(), and there is no platform.
		struct Counting : QWidget {
			int presses = 0, doubles = 0;
			using QWidget::QWidget;
			void mousePressEvent(QMouseEvent *) override { ++presses; }
			void mouseDoubleClickEvent(QMouseEvent *) override { ++doubles; }
		};
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *w = new Counting;
		v->addWidget(w);
		auto *button = new QPushButton(QStringLiteral("OK"));
		int clicks = 0;
		QObject::connect(button, &QPushButton::clicked, [&clicks] { ++clicks; });
		v->addWidget(button);
		win.resize(GridMetrics::cells(10, 2));
		win.show();
		QCoreApplication::processEvents();

		Qtty::InputRouter router(&win);
		auto click = [&](int x, int y) {
			router.on_mouse({ QPoint(x, y), 1, true, false, false,
			                  0, 0, false, false, false });
			router.on_mouse({ QPoint(x, y), 1, false, true, false,
			                  0, 0, false, false, false });
		};

		click(2, 0);
		click(2, 0);
		CHECK(w->doubles == 1 && w->presses == 1,
		      "a second click in the same cell arrives as a double click");
		// A third starts again rather than chaining, which is what a platform
		// does -- otherwise every click after the first in a fast sequence
		// would be a double.
		click(2, 0);
		CHECK(w->doubles == 1 && w->presses == 2,
		      "and a third click starts the count again");

		// The half that says REPLACING the press is right rather than adding
		// to it. Qt's QWidget::mouseDoubleClickEvent() forwards to
		// mousePressEvent() by default -- which is why QAbstractButton has no
		// override and QAbstractItemView does -- so a double-clicked button
		// must still count two clicks. If this library sent the press AND the
		// double click, it would count three.
		clicks = 0;
		click(2, 1);
		click(2, 1);
		CHECK(clicks == 2, "and a double-clicked button still counts two clicks");

		// Far apart in space: different cells are different clicks however
		// fast they arrive.
		w->presses = 0;
		w->doubles = 0;
		click(2, 0);
		click(7, 0);
		CHECK(w->presses == 2 && w->doubles == 0,
		      "while two clicks in different cells are two presses");
	}


	// ---- copy does not end the application ----
	{
		// Measured with the whole of a QLineEdit selected: Ctrl+X cut it to
		// the clipboard, Ctrl+V pasted, Ctrl+A selected all -- and Ctrl+C
		// reached nothing, because the quit-key loop is the first thing in
		// on_key(). Cut and paste worked and copy ended the application: the
		// one clipboard operation that changes nothing was the one that
		// destroyed the most.
		//
		// QClipboard itself is fine under this platform, which the round trip
		// below says before anything else is claimed about it -- a check on
		// copy would otherwise be a check on whether the platform has a
		// clipboard at all.
		QClipboard *cb = QApplication::clipboard();
		cb->setText(QStringLiteral("round trip"));
		CHECK(cb->text() == QStringLiteral("round trip"),
		      "the platform has a working clipboard to test against");

		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *edit = new QLineEdit(QStringLiteral("hello"));
		v->addWidget(edit);
		// Counts the keys it is given, because "the clipboard did not change"
		// does not separate "the key quit" from "the key reached a widget
		// that ignores it". The first version asserted the clipboard and a
		// sabotage that took the exemption ALWAYS left it green -- the key
		// went to the button, the button did not copy, and the check saw
		// exactly what it saw when the quit path ran.
		struct Keys : QWidget {
			int keys = 0;
			using QWidget::QWidget;
			void keyPressEvent(QKeyEvent *) override { ++keys; }
		};
		auto *button = new Keys;
		button->setFocusPolicy(Qt::StrongFocus);
		v->addWidget(button);
		win.resize(GridMetrics::cells(20, 2));
		win.show();
		QCoreApplication::processEvents();
		Qtty::InputRouter router(&win);

		edit->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		edit->selectAll();
		cb->setText(QString());
		router.on_key({ Qt::Key_C, QStringLiteral("c"), true, false, false });
		CHECK(cb->text() == QStringLiteral("hello"),
		      "Ctrl+C in a text field copies rather than quitting");

		// The escape hatch, which is the half that makes the exemption narrow
		// rather than a removal. A form is mostly buttons and lists, and the
		// key still ends the application from all of them.
		button->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		button->keys = 0;
		router.on_key({ Qt::Key_C, QStringLiteral("c"), true, false, false });
		// The key was consumed before dispatch, which is what the quit path
		// does and what reaching the widget does not.
		CHECK(button->keys == 0,
		      "while from a plain widget it never reaches one, being the quit key");
		// And the same widget DOES see other keys, or the line above is a
		// claim about a widget that receives nothing at all.
		router.on_key({ Qt::Key_F1, QString(), false, false, false });
		CHECK(button->keys == 1,
		      "though that widget is reachable by any key that is not one");

		// Read-only is the case the attribute gets right and a class list
		// would not: there is nothing to copy, so the key should quit.
		edit->setReadOnly(true);
		edit->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		edit->selectAll();
		cb->setText(QStringLiteral("still here"));
		router.on_key({ Qt::Key_C, QStringLiteral("c"), true, false, false });
		CHECK(cb->text() == QStringLiteral("still here"),
		      "and a read-only field has nothing to copy, so it quits too");
	}


	// ---- Tab reaches the widget that wants it ----
	{
		// This drove the focus chain unconditionally, so every widget that
		// WANTS a tab lost it. Measured: QTextEdit reports tabChangesFocus()
		// false -- Qt saying it wants the key -- and a tab typed into one
		// moved focus to the next button instead, while a 2x2 QTableWidget's
		// current cell stayed at 0,0. The same shape as the quit key: an
		// interception before dispatch takes a key from the widget that had a
		// use for it.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *edit = new QTextEdit;
		edit->setPlainText(QStringLiteral("x"));
		v->addWidget(edit);
		auto *b1 = new QPushButton(QStringLiteral("One"));
		v->addWidget(b1);
		auto *b2 = new QPushButton(QStringLiteral("Two"));
		v->addWidget(b2);
		win.resize(GridMetrics::cells(20, 6));
		win.show();
		QCoreApplication::processEvents();
		Qtty::InputRouter router(&win);
		auto tab = [&] {
			router.on_key({ Qt::Key_Tab, QStringLiteral("\t"), false, false, false });
		};

		// The premise, stated rather than assumed: Qt says this widget wants
		// the key. If a future Qt changed the default, the checks below would
		// be asserting something else entirely.
		CHECK(!edit->tabChangesFocus(),
		      "Qt says a text edit wants Tab for itself");
		edit->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		edit->moveCursor(QTextCursor::End);
		tab();
		CHECK(edit->toPlainText().contains(QLatin1Char('\t')),
		      "so a tab typed into it is a tab, not a change of focus");
		CHECK(win.focusWidget() == edit,
		      "and focus stays where it was");

		// The half the interception existed for, which must still work: on a
		// widget that does NOT want the key, Tab moves along the chain.
		b1->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		tab();
		CHECK(win.focusWidget() == b2,
		      "while on a button it still moves to the next widget");
		// Exactly one widget along, not two. Qt's own default handler may
		// move focus and accept, so driving the chain on top of that would
		// skip one -- which is why the code compares the focus widget as well
		// as the accepted flag.
		tab();
		CHECK(win.focusWidget() == edit,
		      "one widget at a time, not two");
		GridGuard::reset();
	}


	// ---- an ignored arrow scrolls the area the focus is inside ----
	{
		// This began as a suspected defect and the probe corrected it. The
		// fallback in deliver_key() said it scrolls "the nearest scroll
		// area" and asks findChild() for the scope's FIRST one, which are
		// different whenever there are two. Measured with the focus on a
		// key-ignoring widget inside the SECOND of two areas: the second
		// scrolled, the first did not, and findChild() returns the first --
		// so the fallback never ran at all. Qt propagates an unaccepted key
		// press up the parent chain and the enclosing QScrollArea took it.
		//
		// The behaviour is right and qtty does not implement it. That is
		// exactly what to pin: nothing here would notice if a future change
		// made deliver_key() consume the press before it could propagate,
		// and the symptom would be arrow keys going dead inside every scroll
		// area in every application.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto make_area = [&] {
			auto *area = new QScrollArea;
			auto *inner = new QWidget;
			inner->setFixedSize(GridMetrics::cw() * 10, GridMetrics::ch() * 40);
			area->setWidget(inner);
			area->setFixedHeight(GridMetrics::ch() * 3);
			v->addWidget(area);
			return area;
		};
		auto *first = make_area();
		auto *second = make_area();
		struct Deaf : QWidget {
			using QWidget::QWidget;
			void keyPressEvent(QKeyEvent *e) override { e->ignore(); }
		};
		auto *deaf = new Deaf(second->widget());
		deaf->setFocusPolicy(Qt::StrongFocus);
		deaf->setGeometry(0, 0, GridMetrics::cw() * 4, GridMetrics::ch());
		win.resize(GridMetrics::cells(20, 8));
		win.show();
		QCoreApplication::processEvents();
		Qtty::InputRouter router(&win);

		deaf->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		const int f0 = first->verticalScrollBar()->value();
		const int s0 = second->verticalScrollBar()->value();
		router.on_key({ Qt::Key_Down, QString(), false, false, false });
		const int moved_second = second->verticalScrollBar()->value() - s0;
		const int moved_first = first->verticalScrollBar()->value() - f0;
		CHECK(moved_second > 0,
		      "an arrow a widget ignores scrolls the area it sits inside");
		// The paired half, and the one that would have failed if the
		// suspicion had been right: the OTHER area, which findChild() names,
		// must not move.
		CHECK(moved_first == 0,
		      "and not the first one in the window, which is a different area");
		GridGuard::reset();
	}

	// ---- a click lands on the widget the user can see ----
	{
		// The root is drawn at -scroll cells when the terminal is too small
		// for the window, and nothing shared that offset with on_mouse().
		// Measured on a 30x4 terminal scrolled four rows: the button the user
		// could see was hit as the label four rows above it.
		//
		// This is the cost of a feature added earlier in the same session --
		// the scroll that keeps the focused widget on screen. It made the
		// screen right and the mouse wrong, and nothing noticed because no
		// check drove a click at a scrolled root.
		// Compositor::compose() walks EVERY top-level, and earlier cases in
		// this file leave theirs alive and visible -- the first version of
		// this check composed somebody else's window and reported that its
		// own button was missing. Reaping deferred deletes was not enough;
		// those widgets are not dying, they are simply still there. So this
		// takes the screen for the length of the check and gives it back.
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }

		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *top = new QPushButton(QStringLiteral("Top"));
		v->addWidget(top);
		for (int i = 0; i < 6; ++i)
			v->addWidget(new QLabel(QStringLiteral("pad%1").arg(i)));
		auto *bottom = new QPushButton(QStringLiteral("Bottom"));
		v->addWidget(bottom);
		int top_hits = 0, bottom_hits = 0;
		QObject::connect(top, &QPushButton::clicked, [&] { ++top_hits; });
		QObject::connect(bottom, &QPushButton::clicked, [&] { ++bottom_hits; });
		win.resize(GridMetrics::cells(20, 8));
		win.show();
		QCoreApplication::processEvents();

		Qtty::InputRouter router(&win);
		Qtty::Compositor comp(&win, &router);

		// Small enough that the window cannot fit, with the focus at the
		// bottom so the compositor scrolls to it.
		bottom->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		Qtty::CellBuffer b(20, 3);
		comp.compose(b);

		// Where the button actually is ON SCREEN, read from the frame rather
		// than assumed -- the whole point is that screen and window
		// coordinates have come apart.
		// findText lives in suite_widgets; the same three lines here rather
		// than a header for one caller.
		QPoint seen(-1, -1);
		for (int y = 0; y < b.rows() && seen.x() < 0; ++y)
			for (int x = 0; x + 6 <= b.cols(); ++x)
				if (b.at(x, y).ch == QStringLiteral("B")
				    && b.at(x + 1, y).ch == QStringLiteral("o")
				    && b.at(x + 2, y).ch == QStringLiteral("t")) {
					seen = QPoint(x, y);
					break;
				}
		if (seen.x() < 0) {
			// Say what was there instead. A check that only reports "not
			// found" costs an afternoon of guessing; suite_cells' CHECK macro
			// carries the same lesson.
			printf("info: no 'Bot' in the frame; it holds:\n%s",
			       qPrintable(b.to_text()));
		}
		CHECK(seen.x() >= 0, "the bottom button is on screen after the scroll");
		if (seen.x() >= 0) {
			router.on_mouse({ QPoint(seen.x(), seen.y()), 1, true, false, false,
			                  0, 0, false, false, false });
			router.on_mouse({ QPoint(seen.x(), seen.y()), 1, false, true, false,
			                  0, 0, false, false, false });
		}
		CHECK(bottom_hits == 1 && top_hits == 0,
		      "and clicking where it is drawn presses it, not the widget above");
		win.hide();
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- and a click on a POPUP over a scrolled root ----
	{
		// A popup anchored inside the root moves with the root now, and this
		// is the check that says the frame and the hit test still agree about
		// where it went. on_mouse() finds a popup by testing the press against
		// the popup's own geometry(), and only the ROOT's offset is shared
		// with the router -- so a popup DRAWN at an offset while its geometry
		// stayed put would take every click on the wrong item, or on nothing
		// at all. compose() moves it for exactly that reason, which is the
		// rule the modal branch already followed.
		//
		// This is the fault that cost this tree four builds in one day, in
		// the other layer: the scroll made the screen right and the mouse
		// wrong, and nothing noticed because no check clicked at a scrolled
		// root. So the position clicked is read out of the FRAME. Recomputing
		// it from the menu's geometry the way the compositor does would agree
		// with the compositor however wrong the compositor was.
		//
		// Compositor::compose() walks EVERY top-level and the cases above
		// leave theirs alive and visible, so this takes the screen for the
		// length of the check and gives it back.
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }

		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		for (int i = 0; i < 13; ++i)
			v->addWidget(new QLabel(QStringLiteral("row%1").arg(i)));
		auto *bottom = new QPushButton(QStringLiteral("Bottom"));
		v->addWidget(bottom);
		win.show();
		win.resize(GridMetrics::cells(30, 14));
		QCoreApplication::processEvents();

		Qtty::InputRouter router(&win);
		Qtty::Compositor comp(&win, &router);
		bottom->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		QCoreApplication::processEvents();

		QMenu menu(&win);
		QAction *cut = menu.addAction(QStringLiteral("Cut"));
		QAction *copy = menu.addAction(QStringLiteral("Copy"));
		int cut_hits = 0, copy_hits = 0;
		QObject::connect(cut, &QAction::triggered, [&cut_hits] { ++cut_hits; });
		QObject::connect(copy, &QAction::triggered, [&copy_hits] { ++copy_hits; });
		menu.popup(QPoint(15 * cw, 5 * ch));
		QCoreApplication::processEvents();

		Qtty::CellBuffer b(30, 10);
		comp.compose(b);
		QPoint seen(-1, -1);
		for (int y = 0; y < b.rows() && seen.x() < 0; ++y)
			for (int x = 0; x + 3 <= b.cols(); ++x)
				if (b.at(x, y).ch == QStringLiteral("C")
				    && b.at(x + 1, y).ch == QStringLiteral("u")
				    && b.at(x + 2, y).ch == QStringLiteral("t")) {
					seen = QPoint(x, y);
					break;
				}
		if (seen.x() < 0)
			printf("info: no 'Cut' in the frame; it holds:\n%s",
			       qPrintable(b.to_text()));
		// The pair. "The item fired" is satisfied by a menu that was never
		// moved at all, so the first half says the frame really did put it
		// somewhere the root's scroll had moved it to.
		CHECK(seen.x() >= 0 && !b.to_text().contains(QStringLiteral("row0")),
		      "the menu's first item is on screen over a root that scrolled");
		if (seen.x() >= 0) {
			router.on_mouse({ QPoint(seen.x(), seen.y()), 1, true, false, false,
			                  0, 0, false, false, false });
			router.on_mouse({ QPoint(seen.x(), seen.y()), 1, false, true, false,
			                  0, 0, false, false, false });
			QCoreApplication::processEvents();
		}
		CHECK(cut_hits == 1 && copy_hits == 0,
		      "and clicking it where it is drawn fires that item, not the "
		      "one below it");
		menu.close();
		win.hide();
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	{
		// A shortcut does not fire from behind an open menu. That is section
		// 5.5's routing order -- popup > modal > window -- applied to the
		// table this router owns, and it is the same argument input_scope()
		// already makes one layer up for a modal.
		//
		// Measured against Qt itself first, with a real popup and no router
		// involved:
		//
		//     menu closed, Ctrl+S to the window   the action triggered
		//     menu open,   Ctrl+S to the menu     nothing, and NOT accepted
		//     menu open,   bare 's' to the menu   triggered it, closed the menu
		//
		// and against this router, before the fix, with a File menu open:
		// Ctrl+W triggered a WINDOW action, Ctrl+S triggered the menu's own
		// Save without closing the menu, and Alt+O triggered Open and left
		// the menu on screen.
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = new QMenuBar(&h);
		bar->setGeometry(0, 0, cw * 30, ch);
		QMenu *file = bar->addMenu(QStringLiteral("&File"));
		int opened = 0, elsewhere = 0;
		QAction *open = file->addAction(QStringLiteral("&Open"));
		QObject::connect(open, &QAction::triggered, [&opened] { ++opened; });
		QAction *away = new QAction(QStringLiteral("away"), &h);
		away->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
		QObject::connect(away, &QAction::triggered,
		                 [&elsewhere] { ++elsewhere; });
		h.addAction(away);
		h.resize(GridMetrics::cells(30, 8));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		// The control first, or everything below passes against a shortcut
		// table that never fires at all.
		r.on_key({Qt::Key_W, QStringLiteral("w"), true, false, false});
		QCoreApplication::processEvents();
		CHECK(elsewhere == 1, "a window shortcut fires with no menu open");

		r.on_key({0, QStringLiteral("f"), false, true, false});
		QCoreApplication::processEvents();
		const int was = elsewhere;
		r.on_key({Qt::Key_W, QStringLiteral("w"), true, false, false});
		QCoreApplication::processEvents();
		CHECK(file->isVisible() && elsewhere == was,
		      "and does not fire from behind an open menu, which still stands");

		// Selective, and this is the half that keeps the swallow honest: only
		// a chord that MATCHES a shortcut is taken. A bare letter matches
		// none, falls through, and reaches QMenu::keyPressEvent -- which is
		// where the desktop answers it from.
		r.on_key({Qt::Key_O, QStringLiteral("o"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(opened == 1 && !file->isVisible(),
		      "while a bare letter still reaches the menu and closes it");

		// The mnemonic table stands down for the same reason, so Alt+letter
		// is answered by the menu rather than by a global search that fires
		// an item and leaves the menu on screen.
		r.on_key({0, QStringLiteral("f"), false, true, false});
		QCoreApplication::processEvents();
		const int before_alt = opened;
		r.on_key({Qt::Key_O, QStringLiteral("o"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(opened == before_alt + 1 && !file->isVisible(),
		      "and Alt+letter is answered by the menu, which closes");

		// With no popup up, the same key reaches a plain action on the
		// window. Coverage is what asked for this: every other mnemonic
		// check in this suite either opens a menu or is answered by one
		// already open, so the branch that TRIGGERS an action rather than
		// opening a menu had no caller in a whole run.
		int plain = 0;
		QAction *reload = new QAction(QStringLiteral("&Reload"), &h);
		QObject::connect(reload, &QAction::triggered, [&plain] { ++plain; });
		h.addAction(reload);
		CHECK(!file->isVisible(), "no menu is open for the next case");
		r.on_key({0, QStringLiteral("r"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(plain == 1,
		      "and Alt+letter triggers a plain action when no menu is open");
		h.hide();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	return fails;
}
