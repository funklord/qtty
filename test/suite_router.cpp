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
	// What a click on a slider's GROOVE does, held rather than decided.
	// section 0b records whether SH_Slider_AbsoluteSetButtons should include
	// the left button as the copyright holder's, because it changes what a
	// click means rather than what it looks like. These two hold the current
	// answer so that changing it is visible rather than silent.
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
		// Middle-click sets the value where it landed, and always has: Fusion
		// answers Qt::MiddleButton for the hint and qtty's mouse routing
		// carries the button. Nothing exercised it until now, so a change to
		// the hint could have taken it away unnoticed.
		const int m5 = click_with(2, 5), m10 = click_with(2, 10), m15 = click_with(2, 15);
		CHECK(m5 < m10 && m10 < m15 && m5 > 0 && m15 < 100,
		      "a middle click sets a slider to where it landed");
		// A left click pages, so it lands in the same place wherever it is
		// clicked -- which is the behaviour the open question is about. The
		// pair is what says it: middle tracks the click, left does not.
		const int l5 = click_with(1, 5), l15 = click_with(1, 15);
		CHECK(l5 == l15 && l5 > 0,
		      "and a left click pages, landing in the same place either way");
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
	}
	// Still off the grid WITH GridSnap installed, which is the thing worth
	// asserting now that setup() installs it. A splitter assigns its panes'
	// geometry itself and re-asserts it, so the correction is fought rather
	// than missed -- the same shape as a fixed size the snap cannot move, and
	// the same division of labour: setSizes() in cell multiples is the
	// application's to call. Section 7.8 carries it.
	CHECK(GridSnap::installed() && GridGuard::violations() > 0,
	      "a QSplitter lays its panes off the grid, and a snap does not move them");
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

		// A click INSIDE the popup, which nothing had ever sent: every mouse
		// test here clicks the window under one, so the branch that finds a
		// popup by hit test had never been taken.
		// Asserted by the ACTION firing, not by the popup still being
		// tracked: a menu that handled the click closes itself, so the first
		// draft's "still one popup" could not tell delivery from the click
		// passing through and dismissing it. The two look identical from
		// outside and only one of them is the feature.
		// A click INSIDE a popup goes to the popup, which nothing had ever
		// sent: every mouse test here clicks the window under one, so the
		// branch that finds a popup by hit test had never been taken.
		//
		// Asserted by what does NOT receive it. The obvious assertion --
		// that the menu's action fires -- cannot be made here, and the
		// control says why: sending the same press straight to the QMenu,
		// with the router bypassed entirely, does not fire it either. A
		// QMenu under the offscreen platform has no popup grab and does not
		// activate from a synthetic press, so that probe measures Qt rather
		// than this router.
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

		// A FRESH menu for the close, never clicked. The press above already
		// dismissed the first one -- a QMenu closes on a synthetic press
		// rather than activating -- so closing it again removed nothing and
		// the hide branch never ran. The assertion failed with the code
		// correct, which is the useful direction for it to fail in.
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
		CHECK(through == 0 && under->presses > 0,
		      "a click inside a popup does not fall through to the window");
		CHECK(after_close > before_close,
		      "and a popup going away asks for a frame of its own");
		CHECK(r.popups().isEmpty(), "and stops being tracked");
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
		// Relationships, not the numbers: a step is one, a page is more than
		// one, the thumb moves nothing, and the two ends go opposite ways.
		CHECK(top == 49 && bottom == 51, "a vertical scroll bar's arrow rows step by one");
		CHECK(up < top && down > bottom, "its page rows move further than a step");
		CHECK(thumb == 50, "and a click on the thumb moves nothing");
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

	return fails;
}
