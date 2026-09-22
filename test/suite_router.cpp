// suite_router -- section 5.5: shortcuts (F3), tab order, arrow fallback, mouse
// dispatch, popup stamping + compositor placement (F7, section 8.1), the
// top-level walk (section 5.4 step 3), and modal handling (section 8.3).
#include <qtty/qtty.h>
#include <qtty/drag.h>
#include <qtty/windows.h>
#include <QtWidgets>
#include <QTemporaryDir>
#include "chat.h"
#include <QShortcut>
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

	// ------------------------------------------------------------- 8.251
	// THE TERMINAL'S OWN FOCUS, AND IT IS FIRST IN THE SUITE ON PURPOSE.
	// Two of the assertions below are about the state before anything has
	// reported focus -- a terminal with no focus reporting sends neither
	// sequence, ever, and must render exactly as it always did. There is
	// one moment in the process when that is observable, and this is it:
	// InputRouter::on_focus_change() is the only writer of the record, the
	// suites that run before this one build no router, and this suite's own
	// call is four thousand lines below. Written at the bottom first, where
	// the sink it extends lives, and moved here when the sabotage that
	// inverts the default could not redden it -- the control had been
	// asserting a record something else had already set.
	//
	// THE SINK'S OLD CHECK, further down this file, drives `true` alone
	// and asserts only that a frame was requested -- so it passed with
	// the parameter deleted, which is what the parameter effectively was.
	//
	// What is asserted here is Qt's own deactivation, measured under xcb
	// on a real display rather than reasoned about: activating a second
	// window clears State_HasFocus and State_Active on the first and
	// leaves QLineEdit::hasSelectedText() at 1. So the FOCUS MARK goes
	// and the SELECTION stays, and the frames are compared for that
	// relationship rather than against any particular cell.
	{
		// The button, which carries the focus mark and nothing else.
		QWidget bw;
		bw.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bv = new QVBoxLayout(&bw);
		bv->setContentsMargins(0, 0, 0, 0);
		bv->setSpacing(0);
		auto *go = new QPushButton(QStringLiteral("Go"), &bw);
		bv->addWidget(go);
		bw.resize(GridMetrics::cells(10, 1));
		bw.show();
		QCoreApplication::processEvents();
		set_focus_widget(go);
		QCoreApplication::processEvents();

		InputRouter fr(&bw);
		const auto shot = [](QWidget &w, int cols, int cells_high) {
			CellBuffer b(cols, cells_high);
			render_once(w, b);
			return b;
		};
		const auto marks = [](const CellBuffer &b, Attr a) {
			int n = 0;
			for (int y = 0; y < b.rows(); ++y)
				for (int x = 0; x < b.cols(); ++x)
					if (b.at(x, y).attrs.testFlag(a)) ++n;
			return n;
		};

		// THE CONTROL, and it comes first because it is about the state
		// before anything has been reported. A terminal with no focus
		// reporting never sends either sequence, and silence must not
		// dim it -- so the abstention is "focused" and nothing about
		// such a session changes.
		CHECK(terminal_focused(),
		      "a terminal that has reported nothing counts as focused");
		const CellBuffer silent = shot(bw, 10, 1);

		fr.on_focus_change(true);
		const CellBuffer lit = shot(bw, 10, 1);
		CHECK(lit.to_snapshot() == silent.to_snapshot(),
		      "and reporting focus GAINED changes nothing about it, so a"
		      " terminal without focus reporting renders as it always did");
		CHECK(marks(lit, Attr::Reverse) > 0,
		      "a focused button is drawn focused while the terminal is");

		fr.on_focus_change(false);
		const CellBuffer dark = shot(bw, 10, 1);
		CHECK(marks(dark, Attr::Reverse) == 0,
		      "and loses the mark when the terminal loses the focus, as"
		      " a deactivated window loses State_HasFocus");
		CHECK(dark.to_snapshot() != lit.to_snapshot(),
		      "so the two values of on_focus_change's argument produce"
		      " different frames, which is what makes it load-bearing");

		fr.on_focus_change(true);
		CHECK(shot(bw, 10, 1).to_snapshot() == lit.to_snapshot(),
		      "and the mark comes back when the terminal does, byte for"
		      " byte -- a fix that dimmed and never recovered would pass"
		      " every check above this one");

		// The list, which carries BOTH marks: reverse for the selected
		// row, underline for the current one. Qt keeps the first across
		// a deactivation and drops the second with State_HasFocus, and
		// that is the direction asserted -- nothing is gained, and less
		// is spent.
		QWidget lw;
		lw.setAttribute(Qt::WA_DontShowOnScreen);
		auto *lv2 = new QVBoxLayout(&lw);
		lv2->setContentsMargins(0, 0, 0, 0);
		lv2->setSpacing(0);
		auto *view = new QListView(&lw);
		auto *rows2 = new QStringListModel(
		    QStringList{QStringLiteral("one"), QStringLiteral("two")}, &lw);
		view->setModel(rows2);
		view->setFrameShape(QFrame::NoFrame);
		lv2->addWidget(view);
		lw.resize(GridMetrics::cells(10, 2));
		lw.show();
		QCoreApplication::processEvents();
		view->setCurrentIndex(rows2->index(0, 0));
		view->selectionModel()->select(rows2->index(0, 0),
		                               QItemSelectionModel::Select);
		set_focus_widget(view);
		QCoreApplication::processEvents();

		fr.on_focus_change(true);
		const CellBuffer row_lit = shot(lw, 10, 2);
		fr.on_focus_change(false);
		const CellBuffer row_dark = shot(lw, 10, 2);

		CHECK(marks(row_lit, Attr::Reverse) > 0
		      && marks(row_dark, Attr::Reverse) == marks(row_lit, Attr::Reverse),
		      "an unfocused terminal keeps a selected row's reverse video,"
		      " Qt keeping hasSelectedText across a deactivation");
		CHECK(marks(row_lit, Attr::Underline) > 0
		      && marks(row_dark, Attr::Underline) == 0,
		      "and drops the current item's underline, which is the focus"
		      " mark and not the selection");

		// The direction, stated as a relationship over every cell: an
		// unfocused frame may say LESS than a focused one and may not
		// say anything a focused one does not.
		int gained = 0, lost = 0;
		for (int y = 0; y < row_lit.rows(); ++y)
			for (int x = 0; x < row_lit.cols(); ++x) {
				const Attrs a = row_lit.at(x, y).attrs;
				const Attrs b = row_dark.at(x, y).attrs;
				if (b & ~a) ++gained;
				if (a & ~b) ++lost;
			}
		CHECK(gained == 0 && lost > 0,
		      "so an unfocused frame emphasises a subset of what a focused"
		      " one does, and strictly less of it");

		fr.on_focus_change(true);            // left as the suite found it
	}

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

	// A MENU's action, which is the case the context rule could break and
	// which nothing here had ever asserted -- the three setShortcut fixtures
	// in this suite were all on a window or a plain widget. A QMenu is a
	// top-level widget carrying Qt::Popup, so asking "is this action's widget
	// in the current window" as w->window() == scope answers with the menu
	// itself and refuses every menu shortcut in every application. What holds
	// is the parent chain, and this is what says so.
	int printed = 0;
	QMenu *file_menu = new QMenu(QStringLiteral("&File"), &win);
	QAction *print_it = file_menu->addAction(QStringLiteral("&Print"));
	print_it->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
	QObject::connect(print_it, &QAction::triggered, [&] { ++printed; });
	router.on_key({Qt::Key_P, QString(), true, false, false});
	QCoreApplication::processEvents();
	CHECK(printed == 1,
	      "a shortcut on a menu's action fires with the menu closed, the menu "
	      "being a popup top-level whose window is itself");

	// And the context an action asks for is honoured, which it was not: the
	// action arm fired anything in the scope whose sequence matched, so an
	// action asking for Qt::WidgetShortcut answered while its widget was not
	// focused -- a key the desktop leaves alone. The QShortcut arm had
	// honoured context from the day it was written and the two disagreed.
	int narrow = 0;
	QAction *only_here = new QAction(QStringLiteral("Narrow"), edit);
	only_here->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
	only_here->setShortcutContext(Qt::WidgetShortcut);
	edit->addAction(only_here);
	QObject::connect(only_here, &QAction::triggered, [&] { ++narrow; });
	btn->setFocus(Qt::OtherFocusReason);
	set_focus_widget(win.focusWidget());
	router.on_key({Qt::Key_N, QString(), true, false, false});
	QCoreApplication::processEvents();
	const int while_away = narrow;
	edit->setFocus(Qt::OtherFocusReason);
	set_focus_widget(win.focusWidget());
	router.on_key({Qt::Key_N, QString(), true, false, false});
	QCoreApplication::processEvents();
	CHECK(while_away == 0 && narrow == 1,
	      "an action asking for WidgetShortcut fires only while its own "
	      "widget has focus, as the desktop has it");

	// AND THE CONTEXT BESIDE IT, which the guide's table states and no
	// check held. Qt::WidgetWithChildrenShortcut is implemented twice --
	// context_applies() for actions and shortcut_context_applies() for
	// QShortcuts -- and the word appeared nowhere in any suite, so both
	// copies could have been changed to anything without a check
	// noticing. Found by auditing the guide's tables: the row says "while
	// that widget or a descendant has it", and a row in the most-read
	// file with nothing behind it is one that rots.
	//
	// A DESCENDANT is the whole of the difference from the row above, so
	// the fixture needs one: a container holding the field, with the
	// action on the CONTAINER and focus on the field inside it. A check
	// that put focus on the owner itself would pass identically for
	// WidgetShortcut and prove nothing about this context at all.
	{
		auto *box = new QWidget(&win);
		box->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		auto *inner = new QLineEdit(box);
		inner->setGeometry(0, 0, GridMetrics::cw() * 8, GridMetrics::ch());
		box->show();
		QCoreApplication::processEvents();

		int wide = 0;
		auto *subtree = new QAction(QStringLiteral("Subtree"), box);
		subtree->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
		subtree->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		box->addAction(subtree);
		QObject::connect(subtree, &QAction::triggered, [&] { ++wide; });

		// The control first, and it is the one that makes this a
		// discrimination: the same chord with the NARROWER context on the
		// same owner must NOT fire from the descendant. Without it, an
		// implementation that treated every widget context as "anywhere
		// in the window" would pass the positive below.
		int narrower = 0;
		auto *own_only = new QAction(QStringLiteral("Owner"), box);
		own_only->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
		own_only->setShortcutContext(Qt::WidgetShortcut);
		box->addAction(own_only);
		QObject::connect(own_only, &QAction::triggered, [&] { ++narrower; });

		inner->setFocus(Qt::OtherFocusReason);
		set_focus_widget(win.focusWidget());
		router.on_key({Qt::Key_J, QString(), true, false, false});
		router.on_key({Qt::Key_H, QString(), true, false, false});
		QCoreApplication::processEvents();
		const int from_child = wide, narrow_from_child = narrower;

		// And away from the subtree entirely, which is the other edge: the
		// context is "or a descendant", not "anywhere".
		btn->setFocus(Qt::OtherFocusReason);
		set_focus_widget(win.focusWidget());
		router.on_key({Qt::Key_J, QString(), true, false, false});
		QCoreApplication::processEvents();

		CHECK(from_child == 1,
		      "an action asking for WidgetWithChildrenShortcut fires from a "
		      "DESCENDANT of its widget, which is what separates it from "
		      "WidgetShortcut");
		CHECK(narrow_from_child == 0,
		      "and the narrower context on the same owner does not, so this "
		      "tells the two apart rather than testing that shortcuts work");
		CHECK(wide == 1,
		      "and it is silent from outside the subtree, the context being "
		      "\"or a descendant\" rather than \"anywhere in the window\"");
		box->hide();
		QCoreApplication::processEvents();
	}

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
	QWidget *before_tab = win.focusWidget();
	router.on_key({Qt::Key_Tab, QString(), false, false, false});
	CHECK(win.focusWidget() == btn, "Tab advances focus chain");

	// And BACKWARD, which nothing here asked about until now: backward focus
	// appeared nowhere in the suite while Tab appeared four times. It works
	// -- measured -- and a behaviour that works and is undefended is one a
	// refactor takes away silently.
	//
	// Key_Tab WITH SHIFT, not Key_Backtab, and the difference is the whole
	// value of the check. A terminal sends Shift+Tab as CSI Z, and this
	// library's own backend turns that into `Key_Tab` with `shift` set --
	// `case 'Z': k.qt_key = Qt::Key_Tab; k.shift = true;`. Key_Backtab is a
	// spelling no terminal here produces: sending it skips the router's own
	// branch entirely, because that branch tests `qt_key == Key_Tab`, and
	// Qt's default handling moves the focus instead. The check passes either
	// way and only one of them exercises this project's code.
	//
	// Asserted as a RETURN to where Tab came from rather than against a
	// named widget: that is what "backward" means, and it stays true if the
	// fixture above gains a widget.
	router.on_key({Qt::Key_Tab, QString(), false, false, true});
	CHECK(win.focusWidget() == before_tab,
	      "and Shift+Tab walks it back to where Tab came from");

	// The router's OWN fallback, which the check above does not reach.
	//
	// `focusNextPrevChild(!k.shift)` runs only when the target did not
	// accept the key AND the focus did not move -- and a focused widget
	// accepts Tab, because QWidget::event() handles it. So with anything
	// focused, Qt moves the focus and this line never executes: measured,
	// breaking it to `focusNextPrevChild(true)` left the check above green.
	//
	// With NOTHING focused there is no target, so the fallback is the only
	// thing that can move focus -- and its direction is then observable:
	// backward from nothing is the LAST field, forward from nothing is the
	// FIRST. Asserting the pair is what makes a flipped direction fail;
	// either alone would pass with the argument hardcoded one way.
	{
		if (QWidget *f = win.focusWidget()) f->clearFocus();
		QCoreApplication::processEvents();
		router.on_key({Qt::Key_Tab, QString(), false, false, true});
		QCoreApplication::processEvents();
		QWidget *back = win.focusWidget();
		if (QWidget *f = win.focusWidget()) f->clearFocus();
		QCoreApplication::processEvents();
		router.on_key({Qt::Key_Tab, QString(), false, false, false});
		QCoreApplication::processEvents();
		QWidget *fwd = win.focusWidget();
		CHECK(back && fwd && back != fwd,
		      "and with nothing focused the two directions differ");
	}
	router.on_key({Qt::Key_Tab, QString(), false, false, false});

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
	//
	// It is a TAB now rather than a layer drawn at its own position, which is
	// the same requirement answered differently: before tabs the second
	// window was composited and OVERWROTE the first, measured, leaving only
	// the later one's contents and no way to reach the other. So what is
	// asserted is that the second window is REACHABLE and that selecting it
	// shows it -- being present in the frame was never the point, being
	// usable was.
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
	// Named in the strip, rather than "either drawn OR two tabs exist".
	// That disjunction was written when tabs were added and its first arm is
	// DEAD -- measured, the second window's contents are never in this frame
	// now, because only the current window is drawn -- so the whole check
	// rested on a global COUNT that any other visible top-level moves. It
	// asks for this window by identity instead.
	CHECK(Qtty::window_tabs().contains(&second),
	      "a second top-level joins the frame (section 5.4 step 3)");
	// And selecting it shows it, which is the half that says the strip is an
	// affordance rather than a label. The FIRST window's contents must be
	// gone once it is not current -- that is what stops this passing against
	// the overlapping behaviour it replaced.
	Qtty::set_current_window(&second);
	CellBuffer picked(40, 16);
	comp.compose(picked);
	CHECK(picked.to_text().contains(QStringLiteral("SECONDWIN")),
	      "and choosing its tab shows it");
	// next_window() and previous_window(), which coverage found had never
	// run and a grep found had no caller anywhere -- not the library, not
	// this suite, not a tool. They are public API and the header says why:
	// an application MUST bind keys to them, because qtty deliberately binds
	// no shortcut of its own. So the one route a terminal user has to a
	// second window was carried by two functions nothing had ever called.
	//
	// Asserted as a round trip rather than against a named window: forward
	// then back must return where it started, whatever the order the strip
	// happens to use, and that is what a key binding actually promises.
	{
		// THREE windows, not two, and the sabotage is what said so. With
		// two, stepping forward twice returns to the start -- so a round
		// trip holds even if previous_window() steps FORWARD, and the
		// check passed with the direction reversed. A fixture has to be
		// chosen so that the plausible wrong answer differs from the right
		// one, and two windows cannot tell those apart.
		QWidget third;
		third.setObjectName(QStringLiteral("THIRDWIN"));
		third.setAttribute(Qt::WA_DontShowOnScreen);
		third.resize(GridMetrics::cells(10, 3));
		third.show();
		QCoreApplication::processEvents();
		// Compose again, because window_tabs() reports the set the LAST
		// compose collected -- not the widgets that exist now. Measured:
		// without this the strip still holds two and the third window is
		// invisible to the cycling it was created to exercise.
		{
			CellBuffer refresh(40, 16);
			comp.compose(refresh);
		}

		Qtty::set_current_window(&win);
		QWidget *start = Qtty::current_window();
		Qtty::next_window();
		QWidget *stepped = Qtty::current_window();
		Qtty::previous_window();
		CHECK(Qtty::window_tabs().size() >= 3 && stepped && stepped != start
		      && Qtty::current_window() == start,
		      "next_window moves to another window and previous_window"
		      " comes back");

		// F6, which is what makes those two reachable by a person. They
		// are public and this library binds no key of its own, so until
		// the conventions were asked for a second window could be
		// composed and never got to -- an application had to bind
		// something or the window was unreachable without a mouse.
		Qtty::set_current_window(&win);
		QWidget *const was = Qtty::current_window();
		router.on_key({Qt::Key_F6, QString(), false, false, false});
		QCoreApplication::processEvents();
		const bool moved_off = Qtty::current_window() != was;
		router.on_key({Qt::Key_F6, QString(), false, false, true});
		QCoreApplication::processEvents();
		CHECK(!moved_off && Qtty::current_window() == was,
		      "F6 does not move between windows until an application asks "
		      "for the terminal conventions");

		Qtty::set_keyboard_conventions(true);
		Qtty::set_current_window(&win);
		router.on_key({Qt::Key_F6, QString(), false, false, false});
		QCoreApplication::processEvents();
		QWidget *const after_f6 = Qtty::current_window();
		router.on_key({Qt::Key_F6, QString(), false, false, true});
		QCoreApplication::processEvents();
		CHECK(after_f6 != &win && Qtty::current_window() == &win,
		      "asked for, F6 moves to the next window and Shift+F6 comes "
		      "back, so two windows are reachable with no mouse");
		Qtty::set_keyboard_conventions(false);
		third.hide();
		QCoreApplication::processEvents();
	}

	// ---- a palette window, which was a lockout. Qt's window types are a
	// bitfield whose interesting members are supersets of each other:
	// Qt::Popup is 0x9, Qt::Tool is 0xb and Qt::SplashScreen 0xf, so
	// `(flags & Qt::Popup) == Qt::Popup` -- which reads as "is this a
	// popup" -- was true for a palette. Measured before the fix, with a
	// Qt::Tool window open:
	//
	//     it went on the popup stack, so the top layer owned input
	//     keys typed with the focus in the MAIN window landed NOWHERE
	//     the strip carried nothing, so F6 had nowhere to go
	//     Esc does not dismiss a tool window
	//
	// A terminal the user could not type into and could not get back
	// from. windowType() is the same flags masked, which is how Qt asks.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 20 * GridMetrics::cw(), GridMetrics::ch());
		host.show();
		QCoreApplication::processEvents();
		InputRouter pr(&host);
		Compositor pc(&host, &pr);
		CellBuffer pb(30, 8);
		pc.compose(pb);

		QWidget palette(nullptr, Qt::Tool);
		palette.setAttribute(Qt::WA_DontShowOnScreen);
		palette.setWindowTitle(QStringLiteral("palette"));
		palette.resize(GridMetrics::cells(20, 4));
		auto *tool_field = new QLineEdit(&palette);
		tool_field->setGeometry(0, 0, 10 * GridMetrics::cw(),
		                        GridMetrics::ch());
		palette.show();
		QCoreApplication::processEvents();
		pc.compose(pb);
		QCoreApplication::processEvents();

		CHECK(pr.popups().isEmpty(),
		      "a Qt::Tool window is not a popup layer, though Qt's flag "
		      "for it contains every bit Qt::Popup has");
		CHECK(Qtty::window_tabs().contains(&palette),
		      "and the strip carries it, so F6 reaches a palette rather "
		      "than leaving it somewhere no key goes");

		// THE LOCKOUT ITSELF: type at the main window and the letters
		// have to arrive there. Before the fix they arrived nowhere at
		// all -- the popup owned input and had no focus widget.
		//
		// NO set_current_window() FIRST, and that is the whole fixture.
		// Calling it enters the window, which dismisses the popup stack
		// -- so with the call in place this check passed even with the
		// defect present, and the entry that restores the bit test said
		// so: the named check PASSED against broken code. What a person
		// does is open the palette and keep typing.
		field->setText(QString());
		field->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		pr.on_key({0, QStringLiteral("h"), false, false, false});
		pr.on_key({0, QStringLiteral("i"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(field->text() == QStringLiteral("hi"),
		      "and a person can still type in the window they are in, "
		      "which a palette took away entirely");

		// A REAL popup still is one, which is the other half: the fix
		// must not stop a menu owning input while it is up.
		QMenu menu(&host);
		menu.addAction(QStringLiteral("Item"));
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		CHECK(pr.popups().size() == 1 && pr.popups().first() == &menu,
		      "while a QMenu is still a popup layer and still owns the "
		      "input while it is up");
		menu.close();
		QCoreApplication::processEvents();

		// AND A WINDOW NOBODY CAN TYPE INTO is not a window to switch
		// to. qtty's own overlay twins are exactly this -- frameless,
		// always on top, Qt::WindowTransparentForInput -- and with the
		// type fix alone they became strip entries, which added a strip
		// row to every graphics fixture and moved the damage under three
		// checks that had nothing to do with windows.
		QWidget ghost(nullptr, Qt::Tool | Qt::WindowTransparentForInput);
		ghost.setAttribute(Qt::WA_DontShowOnScreen);
		ghost.resize(GridMetrics::cells(10, 2));
		ghost.show();
		QCoreApplication::processEvents();
		pc.compose(pb);
		CHECK(!Qtty::window_tabs().contains(&ghost),
		      "and a window transparent for input is not in the strip at "
		      "all, there being nothing a person could do once they got "
		      "there");
		ghost.hide();
		palette.hide();
		QCoreApplication::processEvents();
	}

	// ---- the two remaining rows of the guide's window-kind table, and
	// what they COST an application, which nothing had pinned.
	//
	// A non-modal dialog and a splash screen are strip windows, exactly as
	// the guide says. The consequence is the part worth asserting: a strip
	// window that is not current is not DRAWN, so a QProgressDialog left
	// non-modal -- the ordinary Qt idiom for a long operation -- shows a
	// tab label and no bar, and a splash shows nobody anything. The modal
	// case beside it is the contrast that makes this a measurement rather
	// than a complaint: the same dialog, made modal, draws over the window.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }

		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(50, 10));
		auto *body = new QLabel(QStringLiteral("MAIN"), &host);
		body->setGeometry(0, 4 * GridMetrics::ch(), 10 * GridMetrics::cw(),
		                  GridMetrics::ch());
		host.show();
		QCoreApplication::processEvents();
		InputRouter wr(&host);
		Compositor wc(&host, &wr);

		auto *loose = new QProgressDialog(QStringLiteral("Copying files"),
		                                  QString(), 0, 100, &host);
		loose->setWindowModality(Qt::NonModal);
		loose->setMinimumDuration(0);
		loose->setValue(40);
		loose->show();
		QCoreApplication::processEvents();
		CellBuffer wb(50, 10);
		wc.compose(wb);
		const QString loose_frame = wb.to_text();
		CHECK(Qtty::window_tabs().contains(loose)
		      && !loose_frame.contains(QStringLiteral("Copying files")),
		      "a NON-MODAL dialog is a strip window, so its contents are "
		      "not drawn while another window is current -- a progress "
		      "dialog left non-modal shows a tab label and no bar");
		loose->close();
		QCoreApplication::processEvents();

		auto *tight = new QProgressDialog(QStringLiteral("Copying files"),
		                                  QString(), 0, 100, &host);
		tight->setWindowModality(Qt::ApplicationModal);
		tight->setMinimumDuration(0);
		tight->setValue(40);
		tight->show();
		QCoreApplication::processEvents();
		CellBuffer mb(50, 10);
		wc.compose(mb);
		CHECK(!Qtty::window_tabs().contains(tight)
		      && mb.to_text().contains(QStringLiteral("Copying files")),
		      "while the same dialog made modal is drawn over the window "
		      "and is in no strip, which is the remedy an application has "
		      "today");
		tight->close();
		QCoreApplication::processEvents();

		QPixmap art(GridMetrics::cw() * 20, GridMetrics::ch() * 3);
		art.fill(Qt::darkBlue);
		QSplashScreen splash(art);
		splash.setAttribute(Qt::WA_DontShowOnScreen);
		splash.show();
		QCoreApplication::processEvents();
		wc.compose(wb);
		CHECK(Qtty::window_tabs().contains(&splash),
		      "and a QSplashScreen is a strip window too, which is the "
		      "guide's table pinned rather than asserted in prose");
		splash.finish(&host);
		QCoreApplication::processEvents();

		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- and a tooltip window, which was the same lockout one layer
	// along. A Qt::ToolTip top-level is a drawn layer that owns no input
	// anywhere: on a desktop it is a label that appears and goes away, and
	// Qt's own QWidget::keyPressEvent closes a popup on Escape only when
	// `windowType() == Qt::Popup` -- 0xd does not match, so Qt offers no
	// way out of one either. Measured over every window kind Qt has, it
	// was the only row where a key never came back.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 20 * GridMetrics::cw(), GridMetrics::ch());
		host.show();
		QCoreApplication::processEvents();
		InputRouter tr(&host);
		Compositor tc(&host, &tr);
		CellBuffer tb(30, 8);
		tc.compose(tb);

		QWidget tip(nullptr, Qt::ToolTip);
		tip.setAttribute(Qt::WA_DontShowOnScreen);
		tip.resize(GridMetrics::cells(12, 2));
		tip.show();
		QCoreApplication::processEvents();
		tc.compose(tb);

		CHECK(tr.popups().contains(&tip) && !tr.input_popups().contains(&tip),
		      "a tooltip window is a layer the compositor draws and not one "
		      "that owns keys, which is the split the stack needed");

		field->setText(QString());
		field->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		tr.on_key({0, QStringLiteral("t"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(field->text() == QStringLiteral("t"),
		      "so typing still reaches the window a person is in, which a "
		      "tooltip took away with no Escape to give it back");

		// AND A MENU STILL OWNS THEM, which is the half the split must
		// not cost: the two readers agree about a real popup.
		QMenu menu(&host);
		menu.addAction(QStringLiteral("Item"));
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		field->setText(QString());
		tr.on_key({0, QStringLiteral("m"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(tr.input_popups().contains(&menu) && field->text().isEmpty(),
		      "while a menu is in both lists and still takes the keys away "
		      "from the window behind it");
		menu.close();
		tip.hide();
		QCoreApplication::processEvents();
	}

	// ---- a second dialog, which used to be a SEGMENTATION FAULT. The
	// stamping filter set WA_DontShowOnScreen from QEvent::Show, and Qt
	// sends Show AFTER it has created the platform window: the attribute
	// arrived too late, the first dialog's teardown left Qt's blocked-window
	// bookkeeping holding a dead handle, and the next dialog's
	// setTransientParent() walked it. Measured in gdb --
	// QWindow::handle() inside QGuiApplicationPrivate::isWindowBlocked --
	// and an ordinary application reaches it by opening a message box,
	// dismissing it, and opening another.
	//
	// NOTHING IN THIS SUITE COULD SEE IT, and that is the part worth
	// keeping. Every fixture here sets WA_DontShowOnScreen itself before
	// showing, because a headless run demands it -- which makes the filter's
	// stamp a no-op and the hazard unreachable. This block deliberately does
	// NOT pre-stamp, so the filter is the only thing setting the attribute,
	// which is exactly an application's arrangement.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		host.show();
		QCoreApplication::processEvents();
		InputRouter dr(&host);
		int shown = 0;
		for (int round = 0; round < 3; ++round) {
			// A QMessageBox rather than a bare QDialog, and the
			// difference is the whole fixture: measured against the
			// unfixed library, three QDialogs open and close happily
			// and the SECOND QMessageBox takes the process down. Its
			// own show path is what reaches the hazard, so a check
			// built on the simpler dialog defends nothing -- the
			// entry below said so, running to the end where it
			// expects a crash.
			QMessageBox box(QMessageBox::Question, QStringLiteral("t"),
			                QStringLiteral("body"), QMessageBox::Ok,
			                &host);                 // NOT pre-stamped
			box.show();
			QCoreApplication::processEvents();
			if (box.isVisible()
			    && box.testAttribute(Qt::WA_DontShowOnScreen))
				++shown;
			box.close();
			QCoreApplication::processEvents();
		}
		CHECK(shown == 3,
		      "three dialogs opened and closed in turn, each stamped by the "
		      "filter rather than by the fixture -- the second one used to "
		      "take the process down");
	}

	// Where the keys go once the picture has moved. Two windows with a field
	// each, because a window holding only a label has no tab stop and cannot
	// answer this at all -- the `second` above is that window, which is why
	// this section builds its own.
	{
		QWidget a_win, b_win;
		QLineEdit *a_edit = nullptr;
		QLineEdit *b_edit = nullptr;
		for (QWidget *w : { &a_win, &b_win }) {
			w->setAttribute(Qt::WA_DontShowOnScreen);
			auto *v = new QVBoxLayout(w);
			v->setContentsMargins(0, 0, 0, 0);
			auto *e = new QLineEdit(w);
			v->addWidget(e);
			(w == &a_win ? a_edit : b_edit) = e;
			w->resize(GridMetrics::cells(20, 2));
			w->show();
		}
		QCoreApplication::processEvents();
		{
			CellBuffer reg(40, 16);
			comp.compose(reg);
		}

		Qtty::set_current_window(&a_win);
		a_edit->setFocus();
		router.on_key({Qt::Key_A, QStringLiteral("a"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(a_edit->text() == QStringLiteral("a"),
		      "the fixture is live: a key reaches the current window's field");

		// The defect this was written for. InputRouter::input_scope() answered
		// win_ -- the window the router was constructed with -- so every key
		// went to the primary window whichever one was being drawn. Measured
		// through qtty-replay before the fix: F6 to a second window, type, and
		// the text appeared in the first window's field, off screen.
		//
		// BOTH fields are asserted. "b arrived in b" passes while b is also
		// arriving somewhere else, and the whole fault was a keystroke landing
		// in a window nobody can see -- so the untouched field is the half
		// that says where it did NOT go.
		Qtty::set_current_window(&b_win);
		router.on_key({Qt::Key_B, QStringLiteral("b"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(b_edit->text() == QStringLiteral("b")
		      && a_edit->text() == QStringLiteral("a"),
		      "a key after a window switch reaches the window on screen and "
		      "not the one left behind");

		// And it had a target to reach at all. A desktop Qt seeds a window's
		// focus on activation; nothing activates here, so a window opened
		// after exec() had no focus widget and the switch above would have
		// had nowhere to deliver to. Asserted against keyboard_reachable(),
		// which is what decides a tab stop for Tab -- naming the widget would
		// let focus be seeded somewhere Tab cannot go and still pass.
		const QVector<QWidget *> b_stops = Qtty::keyboard_reachable(&b_win);
		CHECK(!b_stops.isEmpty() && Qtty::focusWidget() == b_stops.first(),
		      "arriving in a window that has never had focus seeds its first "
		      "tab stop");

		// A popup belongs to the window it was opened in, and on a desktop it
		// closes when that window deactivates. Nothing deactivates here, so a
		// menu opened in one window stayed drawn over the next -- and kept
		// input, because a popup outranks the window in key_target(). That is
		// the dangerous half: a letter fires an action in a window the user
		// cannot see.
		Qtty::set_current_window(&a_win);
		QMenu stale(&a_win);
		stale.addAction(QStringLiteral("STALEITEM"));
		stale.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		CellBuffer with_menu(40, 16);
		comp.compose(with_menu);
		CHECK(with_menu.to_text().contains(QStringLiteral("STALEITEM")),
		      "the stale-popup case is real: the menu is drawn while its own "
		      "window is current");

		Qtty::set_current_window(&b_win);
		CellBuffer after_switch(40, 16);
		comp.compose(after_switch);
		CHECK(!after_switch.to_text().contains(QStringLiteral("STALEITEM")),
		      "leaving a window dismisses the menu it had open");
		router.on_key({Qt::Key_C, QStringLiteral("c"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(b_edit->text() == QStringLiteral("bc"),
		      "and the keys go to the new window rather than to the menu the "
		      "old one had open");

		a_win.hide();
		b_win.hide();
		QCoreApplication::processEvents();
	}

	Qtty::set_current_window(&win);
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

		// And the paste reaches QClipboard, which is the only store a widget
		// reads. Without it the two ways of pasting disagreed: the terminal's
		// paste arrived and Ctrl+V beside it inserted whatever the program
		// itself had last copied -- nothing, in a program that has copied
		// nothing at all.
		//
		// Asserted through Ctrl+V rather than on clipboard()->text(), because
		// the value is not the point: what was wrong is that the user's two
		// ways of asking for the same text gave different answers. The
		// clipboard is seeded with something else first, so a check that
		// passed before the mirror existed would have to insert THAT.
		QClipboard *const board = QGuiApplication::clipboard();
		board->setText(QStringLiteral("what the program copied"));
		line->clear();
		line->setFocus();
		set_focus_widget(line);
		pr.on_paste(QStringLiteral("what the terminal sent"));
		line->clear();
		pr.on_key({Qt::Key_V, QStringLiteral("v"), true, false, false});
		CHECK(line->text() == QStringLiteral("what the terminal sent"),
		      "Ctrl+V after a terminal paste inserts what was pasted, the"
		      " arriving text having gone into QClipboard");

		// Marked, because a backend forwarding the clipboard has to tell this
		// write from a copy -- see backend.h. Unmarked, a middle-click paste
		// would go back out as OSC 52 and replace the clipboard the user had.
		const QMimeData *const held = board->mimeData();
		CHECK(held && held->hasFormat(QLatin1String(terminal_paste_format())),
		      "and it is marked as a terminal paste rather than a copy");

		// The fold is the field's, not the text's: a single-line target still
		// gets spaces, and the clipboard still holds the newline.
		line->clear();
		pr.on_paste(QStringLiteral("one\ntwo"));
		CHECK(line->text() == QStringLiteral("one two")
		      && board->text() == QStringLiteral("one\ntwo"),
		      "and the clipboard keeps the newline a single-line field folded");
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

	// ---- mnemonic collisions, which an application cannot otherwise see ----
	//
	// The guide's first and highest-value practice is "give every control a
	// mnemonic", and the failure it creates scales with how well it is
	// followed: the more letters an application claims, the likelier two
	// claims collide. Nothing says so. The screen is unchanged, nothing is
	// logged, and one of the two controls simply never answers its key.
	//
	// Asserted against what the KEYS do rather than against the report's own
	// contents, because the report is only worth anything if it names the
	// same winner the router picks -- a report that could disagree with the
	// keys would send an application to fix the control that works.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 8));
		auto *bar = new QMenuBar(&host);
		QMenu *file = bar->addMenu(QStringLiteral("&File"));
		int triggered = 0;
		QAction *act = file->addAction(QStringLiteral("&Save"));
		QObject::connect(act, &QAction::triggered, [&] { ++triggered; });
		int clicked = 0;
		auto *button = new QPushButton(QStringLiteral("&Save all"), &host);
		button->setGeometry(0, GridMetrics::ch(), 12 * GridMetrics::cw(),
		                    GridMetrics::ch());
		QObject::connect(button, &QPushButton::clicked, [&] { ++clicked; });
		auto *edit = new QLineEdit(&host);
		edit->setGeometry(0, 2 * GridMetrics::ch(), 12 * GridMetrics::cw(),
		                  GridMetrics::ch());
		auto *label = new QLabel(QStringLiteral("&Host"), &host);
		label->setBuddy(edit);
		label->setGeometry(0, 3 * GridMetrics::ch(), 12 * GridMetrics::cw(),
		                   GridMetrics::ch());
		host.show();
		QCoreApplication::processEvents();

		const auto clash = mnemonic_conflicts(&host);
		QStringList letters;
		for (const auto &c : clash) letters << QString(c.first);
		printf("info: letters claimed twice [%s]\n",
		       qPrintable(letters.join(QStringLiteral(", "))));
		CHECK(clash.size() == 1 && clash[0].first == QLatin1Char('s')
		          && clash[0].second.size() == 2,
		      "two controls claiming one Alt+letter are reported, which is "
		      "the only way an application is told at all");
		CHECK(!clash.isEmpty()
		          && clash[0].second.first() == QStringLiteral("&Save"),
		      "and the winner is named first, in the order the router tries "
		      "them, so the report cannot send anybody to the control that "
		      "works");

		InputRouter cr(&host);
		cr.on_key({0, QStringLiteral("s"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(triggered == 1 && clicked == 0,
		      "and the key does what the report said it would, the report "
		      "and the matcher reading one enumeration rather than two");

		// A letter claimed once is not a conflict, and neither is a claim
		// the router would walk past: a hidden buddy means the label does
		// not answer at all, so naming it would invent a collision.
		auto *shadow = new QLabel(QStringLiteral("&Save it"), &host);
		auto *hidden = new QLineEdit(&host);
		// SHOWN, and its buddy hidden. A child built after its parent was
		// shown is not visible until it is told to be, so without this line
		// the label is skipped for being invisible and the buddy is never
		// asked about -- the check would pass with the buddy rule deleted,
		// which is how the sabotage run found it.
		shadow->setGeometry(0, 4 * GridMetrics::ch(), 12 * GridMetrics::cw(),
		                    GridMetrics::ch());
		shadow->show();
		hidden->hide();
		shadow->setBuddy(hidden);
		QCoreApplication::processEvents();
		const auto still = mnemonic_conflicts(&host);
		CHECK(still.size() == 1 && still[0].second.size() == 2,
		      "while a claim whose buddy is hidden is no claim, the report "
		      "and the router agreeing about who is out of play");
		// ONE ACTION REACHED TWICE IS ONE CLAIM. An action added to a
		// window and to a menu -- or a menu's own action, which Qt puts
		// where this walk finds it twice -- would otherwise be reported as
		// colliding with itself, which sends somebody to rename a letter
		// only one control answers.
		//
		// Measured against Qt's own `menus` example before this was fixed:
		// 32 actions enumerate to 30 distinct ones, and the report accused
		// `&Edit` and `&Help` of claiming their own letters twice.
		{
			auto *twice = new QAction(QStringLiteral("&Zoom"), &host);
			host.addAction(twice);
			file->addAction(twice);           // the same object, second route
			QCoreApplication::processEvents();
			bool z_reported = false;
			for (const auto &c : mnemonic_conflicts(&host))
				if (c.first == QLatin1Char('z')) z_reported = true;
			CHECK(!z_reported,
			      "an action reached by two routes is one claim, not a "
			      "collision with itself");
		}

		// A TAB is the fourth population, and it is answered somewhere
		// else: not by the mnemonic matcher but by the conventions block
		// below delivery, so it LOSES to an action, a button or a buddy
		// label claiming the same letter -- and it claims nothing at all
		// with the conventions off. A report that could not see tab bars
		// told an application its letters were unique when they were not,
		// which is the failure this whole helper exists to end.
		auto *tabs = new QTabWidget(&host);
		tabs->setGeometry(0, 5 * GridMetrics::ch(), 20 * GridMetrics::cw(),
		                  2 * GridMetrics::ch());
		tabs->addTab(new QWidget, QStringLiteral("&Summary"));
		tabs->addTab(new QWidget, QStringLiteral("&Detail"));
		tabs->show();
		QCoreApplication::processEvents();

		const auto quiet = mnemonic_conflicts(&host);
		CHECK(quiet.size() == 1 && quiet[0].second.size() == 2,
		      "with the conventions off a tab's letter is not a claim, "
		      "because nothing answers it");

		set_keyboard_conventions(true);
		const auto loud = mnemonic_conflicts(&host);
		QStringList tab_claimants;
		for (const auto &c : loud)
			if (c.first == QLatin1Char('s')) tab_claimants = c.second;
		CHECK(tab_claimants.size() == 3
		          && tab_claimants.last() == QStringLiteral("&Summary"),
		      "and with them on it is, named last because a tab is answered "
		      "after the matcher has had the key and refused it");

		triggered = 0;
		clicked = 0;
		InputRouter tr(&host);
		tr.on_key({0, QStringLiteral("s"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(triggered == 1 && tabs->currentIndex() == 0,
		      "and the order is the keys' own: the action answers and the "
		      "tab does not move");

		// The tab's own letter still works where nothing else claims it,
		// which is the half a collision must not break.
		tr.on_key({0, QStringLiteral("d"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(tabs->currentIndex() == 1,
		      "while an uncontested tab letter still switches to it");
		set_keyboard_conventions(false);
		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- the letters nothing carries --------------------------------------
	//
	// mnemonic_conflicts() finds two controls claiming one letter. Until
	// mnemonic_missing() there was no way to ask the other half -- whether a
	// control has a letter at all -- and practice 1 calls giving one the
	// highest-value thing on that page. An application could assert that its
	// mnemonics did not collide and could not assert that it had any.
	{
		// A WINDOW THAT FOLLOWED THE PRACTICE reports nothing, which is the
		// answer an application asserts and the one a report invents a
		// finding to avoid giving.
		QWidget done;
		done.setAttribute(Qt::WA_DontShowOnScreen);
		done.resize(GridMetrics::cells(30, 8));
		auto *lay = new QVBoxLayout(&done);
		auto *field = new QLineEdit;
		auto *label = new QLabel(QStringLiteral("&Host"));
		label->setBuddy(field);
		lay->addWidget(label);
		lay->addWidget(field);
		lay->addWidget(new QPushButton(QStringLiteral("&Connect")));
		done.show();
		QCoreApplication::processEvents();
		CHECK(mnemonic_missing(&done).isEmpty(),
		      "a window whose every control carries a letter reports none "
		      "missing, which is what an application asserts");

		// AND IT CAN SAY NO. Without this the line above would pass on a
		// function that returned empty whatever it was given.
		auto *bare = new QPushButton(QStringLiteral("Go"));
		lay->addWidget(bare);
		QCoreApplication::processEvents();
		const auto found = mnemonic_missing(&done);
		CHECK(found.size() == 1 && found[0].second == QStringLiteral("Go"),
		      "and a button with no letter is named, by the text it shows "
		      "rather than by a pointer a reader cannot see");

		// A BUDDY LABEL WITH NO LETTER is the sharper finding of the two,
		// and it is the guide's own argument rather than a new one: a label
		// is here to carry a letter and costs a whole row on twenty-four of
		// them, so one without a letter is buying nothing.
		label->setText(QStringLiteral("Host"));
		QCoreApplication::processEvents();
		QStringList texts;
		for (const auto &r : mnemonic_missing(&done)) texts << r.second;
		CHECK(texts.contains(QStringLiteral("Host")),
		      "and so is a buddy label with no letter, which is a row spent "
		      "on nothing");
	}
	{
		// THE TWO EXCLUSIONS AND A FINDING IN ONE FIXTURE, because each is
		// only meaningful against the others: Enter reaches the default
		// button and Escape reaches Cancel, so naming those two would put a
		// finding in every dialog that nobody can act on -- while Help has
		// no key at all and is exactly what this report is for.
		QDialog dlg;
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		dlg.resize(GridMetrics::cells(30, 6));
		auto *v = new QVBoxLayout(&dlg);
		auto *box = new QDialogButtonBox(QDialogButtonBox::Ok
		                                 | QDialogButtonBox::Cancel
		                                 | QDialogButtonBox::Help);
		v->addWidget(box);
		dlg.show();
		QCoreApplication::processEvents();
		QStringList texts;
		for (const auto &r : mnemonic_missing(&dlg)) texts << r.second;
		CHECK(texts == QStringList{QStringLiteral("Help")},
		      "a dialog's OK and Cancel are not named, Enter and Escape "
		      "reaching them, while Help is -- which is the whole of what "
		      "this report is for");
	}
	{
		// QT'S OWN WIZARD lays its buttons out itself rather than through a
		// QDialogButtonBox, so the box's rule cannot reach its Cancel and
		// the wizard's own handle is what excludes it. Measured before that
		// exclusion existed: one finding, on a button whose text no
		// application wrote.
		QWizard wiz;
		auto *page = new QWizardPage;
		page->setTitle(QStringLiteral("Account"));
		auto *pv = new QVBoxLayout(page);
		pv->addWidget(new QLineEdit);
		wiz.addPage(page);
		wiz.addPage(new QWizardPage);
		wiz.setAttribute(Qt::WA_DontShowOnScreen);
		wiz.resize(GridMetrics::cells(40, 12));
		wiz.show();
		QCoreApplication::processEvents();
		CHECK(mnemonic_missing(&wiz).isEmpty(),
		      "and Qt's own wizard reports none, its Back and Next carrying "
		      "letters and Escape reaching its Cancel");

		// ASKED FROM OUTSIDE AS WELL, which is not the same code path and
		// had no check until a sabotage found it: the first draft reached
		// the scope's own wizard by one line and a nested one by another,
		// and every fixture here asked a wizard about itself -- so removing
		// the second line broke nothing any check could see. A wizard
		// parented to a window is that window's QObject child, which is
		// exactly how an application opens one.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 12));
		auto *nested = new QWizard(&host);
		auto *np = new QWizardPage;
		np->setTitle(QStringLiteral("Account"));
		nested->addPage(np);
		nested->addPage(new QWizardPage);
		nested->setAttribute(Qt::WA_DontShowOnScreen);
		host.show();
		nested->show();
		QCoreApplication::processEvents();
		CHECK(mnemonic_missing(&host).isEmpty(),
		      "and so does a window holding one, the exclusion reaching a "
		      "wizard inside the scope and not only a scope that is one");
		GridGuard::reset();
	}
	{
		// THE TWO REPORTS ARE A PARTITION, not two opinions. A button with
		// no focus policy, no letter and no chord satisfies both sentences
		// -- no key reaches it, and no letter reaches it directly -- and
		// audit() must put ONE row on it rather than two on one fault.
		// pointer_only() is the older question and the worse finding, so it
		// keeps the widget.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(30, 6));
		auto *v = new QVBoxLayout(&win);
		v->addWidget(new QLineEdit);
		win.show();
		auto *stray = new QPushButton(QStringLiteral("Go"), &win);
		stray->setFocusPolicy(Qt::NoFocus);
		stray->setGeometry(0, 4 * GridMetrics::ch(),
		                   8 * GridMetrics::cw(), GridMetrics::ch());
		stray->show();
		QCoreApplication::processEvents();
		QStringList questions;
		for (const auto &row : Qtty::audit(&win)) questions << row.first;
		CHECK(questions.count(QStringLiteral("pointer_only")) == 1
		          && !questions.contains(QStringLiteral("mnemonic_missing")),
		      "a control nothing reaches at all is one report's finding and "
		      "not both, so the audit names the fault once");
		GridGuard::reset();
	}

	// ---- a popup that takes another one with it ---------------------------
	//
	// A press outside an open popup closes the stack from the top down, and
	// close() runs the application's own code. One popup deleting another is
	// supported Qt -- neither is the widget being delivered to -- and the
	// list this walks was snapshotted before any of it ran, so the entries
	// below the one that fired can be gone by the time the walk reaches
	// them. 8.161's rule, met in a list rather than in a variable.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		host.show();
		QCoreApplication::processEvents();
		InputRouter pr(&host);

		auto *under = new QMenu(&host);
		under->addAction(QStringLiteral("under"));
		struct Taking : QMenu {
			QWidget *other = nullptr;
			void closeEvent(QCloseEvent *e) override {
				delete other;               // another popup, not this one
				other = nullptr;
				QMenu::closeEvent(e);
			}
		};
		auto *over = new Taking;
		over->setParent(&host, Qt::Popup);
		over->addAction(QStringLiteral("over"));
		over->other = under;
		under->popup(QPoint(0, 0));
		over->popup(QPoint(4 * GridMetrics::cw(), 2 * GridMetrics::ch()));
		QCoreApplication::processEvents();
		CHECK(pr.popups().size() == 2,
		      "two popups are on the stack, the second over the first");

		QPointer<QWidget> gone(under);
		pr.on_mouse({QPoint(25, 7), 1, true, false, false, 0});   // outside both
		QCoreApplication::processEvents();
		CHECK(gone.isNull() && pr.popups().isEmpty(),
		      "and a press outside closes the stack even when closing one of "
		      "them destroys another, the walk holding what it walks weakly");
		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- a target deleted before the event reaches it ---------------------
	//
	// The router holds a raw pointer across code that runs the application's
	// own handlers -- update_hover() sends Leave and Enter before a press is
	// delivered, and dismiss_popups() closes popups before a window is
	// entered -- and then uses it. What can actually destroy a widget there
	// was MEASURED rather than assumed, because the obvious fixtures test
	// nothing:
	//
	//   delete this, inside the widget's own handler   unsupported in Qt; it
	//                                                  crashes inside
	//                                                  QWidget::event() before
	//                                                  this library sees it
	//   deleteLater() inside the handler, then a
	//   nested processEvents()                         does NOT destroy: a
	//                                                  deferred delete posted
	//                                                  during delivery waits
	//                                                  for the loop level it
	//                                                  was posted at
	//   another widget's handler deleting this one     supported, immediate,
	//                                                  and the case below
	//
	// So the fixture is a leaveEvent that deletes the widget the pointer is
	// moving ONTO -- legal Qt, since that widget is not on the delivery
	// stack -- and the press that follows would land on freed memory.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(20, 4));
		int presses = 0;
		struct Doomed : QWidget {
			int *presses = nullptr;
			void mousePressEvent(QMouseEvent *) override { ++*presses; }
		};
		struct Leaver : QWidget {
			QWidget *kill = nullptr;
			void leaveEvent(QEvent *) override { delete kill; kill = nullptr; }
		};
		auto *leaver = new Leaver;
		leaver->setParent(&host);
		leaver->setGeometry(0, 0, 8 * GridMetrics::cw(), GridMetrics::ch());
		auto *doomed = new Doomed;
		doomed->setParent(&host);
		doomed->presses = &presses;
		doomed->setGeometry(8 * GridMetrics::cw(), 0, 8 * GridMetrics::cw(),
		                    GridMetrics::ch());
		leaver->kill = doomed;
		QPointer<QWidget> alive(doomed);
		host.show();
		QCoreApplication::processEvents();

		InputRouter mr(&host);
		mr.on_mouse({QPoint(1, 0), 0, false, false, true, 0});   // over the leaver
		QCoreApplication::processEvents();
		mr.on_mouse({QPoint(10, 0), 1, true, false, false, 0});  // press on the doomed one
		QCoreApplication::processEvents();
		CHECK(alive.isNull() && presses == 0,
		      "a press is not delivered to a widget the hover before it "
		      "destroyed, which is the one deletion route Qt supports here");

		// And the router is still usable, which a crash would have taken
		// with it.
		auto *after = new QLineEdit(&host);
		after->setGeometry(0, GridMetrics::ch(), 10 * GridMetrics::cw(),
		                   GridMetrics::ch());
		after->show();
		after->setFocus();
		set_focus_widget(after);
		QCoreApplication::processEvents();
		mr.on_key({Qt::Key_Z, QStringLiteral("z"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(after->text() == QStringLiteral("z"),
		      "and the next key still arrives, the window having lost a "
		      "widget rather than the router its footing");
		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- a status tip follows focus, since nobody can hover --------------
	//
	// Qt shows a statusTip when the mouse rests on a control. A terminal
	// user has no pointer to rest, so every setStatusTip() an application
	// has written is text explaining a control to nobody -- and an
	// application gets a status bar for free from QMainWindow, which is
	// where the tip lands.
	//
	// Asserted through the STATUS BAR rather than by counting events: what
	// an application gets out of this is the sentence on the screen, and a
	// check on the event would pass with the propagation broken.
	{
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *central = new QWidget(&win);
		auto *host = new QLineEdit(central);
		host->setStatusTip(QStringLiteral("the host to connect to"));
		auto *port = new QLineEdit(central);
		auto *box = new QGroupBox(QStringLiteral("where"), central);
		box->setStatusTip(QStringLiteral("where to connect"));
		auto *inside = new QLineEdit(box);
		win.setCentralWidget(central);
		win.statusBar()->showMessage(QString());
		win.resize(GridMetrics::cells(40, 8));
		win.show();
		QCoreApplication::processEvents();

		set_keyboard_conventions(false);
		set_focus_widget(nullptr);
		set_focus_widget(host);
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage().isEmpty(),
		      "with the conventions off a status tip stays where Qt left "
		      "it, which is on the mouse nobody has");

		set_keyboard_conventions(true);
		set_focus_widget(nullptr);
		set_focus_widget(host);
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage()
		          == QStringLiteral("the host to connect to"),
		      "and with them on the focused control's own status tip is on "
		      "the status bar, which an unmodified application already "
		      "wrote and nobody could read");

		set_focus_widget(inside);
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage()
		          == QStringLiteral("where to connect"),
		      "a field with no tip of its own shows the one its group box "
		      "carries, as the mouse would find it");

		set_focus_widget(port);
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage().isEmpty(),
		      "and moving to a control nothing explains takes the last "
		      "explanation away rather than leaving it under the wrong "
		      "field");

		// The application's OWN message is not ours to clear, and this is
		// the case the flag exists for: tabbing between two controls that
		// explain nothing must leave a sentence the program put there
		// itself alone. Without the flag every focus move would send an
		// empty tip and the bar would be blanked by the act of tabbing.
		//
		// Chosen so the two answers differ: a check that moved focus onto a
		// control WITH a tip would be satisfied either way, Qt's hover
		// overwriting the message as well.
		win.statusBar()->showMessage(QStringLiteral("connecting..."));
		set_focus_widget(port);
		set_focus_widget(nullptr);
		set_focus_widget(port);
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage()
		          == QStringLiteral("connecting..."),
		      "while a message the application wrote survives tabbing "
		      "between controls that explain nothing, qtty clearing only "
		      "tips it put up itself");
		// A widget that moves focus ON from its own focusInEvent, which is
		// what a container handing focus to the field inside it does. The
		// inner call runs to the end and then the outer one resumes, so
		// without a guard the bar would end up explaining the control
		// focus has already left -- the last writer being the outer call
		// and the outer call being the stale one.
		struct Forwarder : QWidget {
			QWidget *onward = nullptr;
			void focusInEvent(QFocusEvent *) override {
				if (onward) set_focus_widget(onward);
			}
		};
		auto *gate = new Forwarder;
		gate->setParent(central);
		gate->setStatusTip(QStringLiteral("not where focus ends up"));
		gate->onward = host;
		set_keyboard_conventions(true);
		set_focus_widget(nullptr);
		win.statusBar()->showMessage(QString());
		set_focus_widget(gate);
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage()
		          == QStringLiteral("the host to connect to"),
		      "and a widget that passes focus straight on leaves the tip of "
		      "the control focus reached, not of the one it went through");

		set_keyboard_conventions(false);
		win.hide();
		QCoreApplication::processEvents();
	}

	// A MENU action's status tip is Qt's own, not this library's, and it
	// arrives only if the menu knows which bar opened it: QMenu walks
	// causedPopup to find who should hear the tip, and popup() leaves that
	// unset. Measured in plain Qt with no qtty present -- opened with
	// popup(), the keyboard moves the highlight and the bar stays empty;
	// opened through setActiveAction(), each Down puts the action's tip on
	// the status bar.
	//
	// That is exactly the chain 8.31 chose the mnemonic path for, so what is
	// asserted here is that an application keeps a Qt behaviour it already
	// had -- with the conventions OFF, because this one is not ours to turn
	// on. A router that opened menus the convenient way would take it away
	// and nothing else would say so.
	{
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(40, 8));
		QMenu *file = win.menuBar()->addMenu(QStringLiteral("&File"));
		QAction *open = file->addAction(QStringLiteral("&Open"));
		open->setStatusTip(QStringLiteral("open a file"));
		win.setCentralWidget(new QWidget);
		win.statusBar()->showMessage(QString());
		win.show();
		QCoreApplication::processEvents();
		set_keyboard_conventions(false);

		InputRouter mr(&win);
		Qtty::set_current_window(&win);
		mr.on_key({0, QStringLiteral("f"), false, true, false});
		QCoreApplication::processEvents();
		mr.on_key({Qt::Key_Down, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(win.statusBar()->currentMessage()
		          == QStringLiteral("open a file"),
		      "a menu item's own status tip still reaches the status bar "
		      "here, the menu being opened through the bar that owns it");
		file->close();
		win.hide();
		QCoreApplication::processEvents();
	}

	// ---- which other window answers an application-wide chord -------------
	//
	// An application-context claim in another window is far from the focus by
	// definition, so every one of them ties and the tie goes to the order the
	// windows are searched in. That order was
	// QApplication::topLevelWidgets(), which is Qt's bookkeeping: measured,
	// four windows in one program gave the strip as [root, b, a, c] and Qt's
	// list as [b, root, a, c]. They disagree, so which window answered a
	// chord was decided by something no user can see.
	//
	// The strip's order is the one they can: it is what the tab row shows.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.setWindowTitle(QStringLiteral("current"));
		host.resize(GridMetrics::cells(20, 4));
		auto *here = new QLineEdit(&host);
		here->setGeometry(0, 0, 10 * cw, ch);
		// BUILT SO THE TWO ORDERS DISAGREE, or the check cannot tell them
		// apart -- and the first version could not: its sabotage reddened
		// nothing, because the strip and Qt's list happened to agree.
		//
		// 8.177's own limit is what makes this constructible. Qt's list is
		// built when a widget is CREATED; the strip records a window when it
		// is first SEEN by a compose. So `late` is created first and shown
		// second, and the two lists end up in opposite orders.
		auto *late = new QWidget;
		auto *early = new QWidget;
		int early_fired = 0, late_fired = 0;
		const auto claim = [](QWidget *w, const char *name, int *counter) {
			w->setAttribute(Qt::WA_DontShowOnScreen);
			w->setWindowTitle(QString::fromLatin1(name));
			w->resize(GridMetrics::cells(14, 3));
			auto *a = new QAction(QString::fromLatin1(name), w);
			a->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
			a->setShortcutContext(Qt::ApplicationShortcut);
			QObject::connect(a, &QAction::triggered, [counter] { ++*counter; });
			w->addAction(a);
		};
		claim(late, "late", &late_fired);
		claim(early, "early", &early_fired);
		host.show();
		early->show();
		QCoreApplication::processEvents();

		InputRouter ar(&host);
		Compositor ac(&host, &ar);
		CellBuffer ab(20, 4);
		ac.compose(ab);                          // early is seen first
		late->show();
		QCoreApplication::processEvents();
		ac.compose(ab);                          // and late after it
		Qtty::set_current_window(&host);
		QCoreApplication::processEvents();
		ac.compose(ab);
		const QVector<QWidget *> strip = Qtty::window_tabs();
		const bool early_first = strip.indexOf(early) < strip.indexOf(late);
		QStringList qt_order;
		for (QWidget *w : QApplication::topLevelWidgets())
			if (w == early || w == late)
				qt_order << w->windowTitle();
		printf("info: strip has early first: %d; Qt's list: [%s]\n",
		       int(early_first), qPrintable(qt_order.join(QStringLiteral(", "))));
		here->setFocus();
		QCoreApplication::processEvents();
		set_focus_widget(here);
		ar.on_key({Qt::Key_G, QString(), true, false, false});
		QCoreApplication::processEvents();
		// The control asserts the STRIP's order, which is this library's and
		// is deterministic. It does NOT assert that Qt's list disagrees: a
		// version of this check did, and went red on a later run, because
		// `QApplication::topLevelWidgets()` has no promised order and
		// sometimes agrees. The info line above prints both, so a reader of
		// a failing run can see which case they are in.
		CHECK(strip.contains(early) && strip.contains(late) && early_first,
		      "the control: both other windows are on the strip, with the "
		      "one shown first ahead of the other");
		CHECK(early_first ? (early_fired == 1 && late_fired == 0)
		                  : (late_fired == 1 && early_fired == 0),
		      "an application-wide chord is answered by the window that "
		      "comes first on the STRIP, which is the order a user can see, "
		      "rather than by Qt's own list order");
		delete early;
		delete late;
		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- the strip keeps its order ----------------------------------------
	//
	// It did not. The list came from QApplication::topLevelWidgets(), whose
	// order is Qt's own bookkeeping and is not promised: measured across five
	// runs of one program, three windows created first, second, third drew as
	// `[first, second, third]` four times and `[first, third, second]` once.
	//
	// A strip that reorders itself is bad on its own -- the second tab
	// becomes the third while the user is looking at it -- and it makes
	// anything derived from the order unstable. That is how it was found: the
	// neighbour rule below picked a different survivor run to run, and a
	// focus check three hundred lines away failed one run in three.
	//
	// Asserted through hide and show, which is what an application does and
	// what moves a window in Qt's list.
	{
		QWidget one, two, three;
		for (QWidget *w : {&one, &two, &three}) {
			w->setAttribute(Qt::WA_DontShowOnScreen);
			w->resize(GridMetrics::cells(16, 3));
			(new QLineEdit(w))->setGeometry(0, 0, 8 * cw, ch);
		}
		one.setWindowTitle(QStringLiteral("one"));
		two.setWindowTitle(QStringLiteral("two"));
		three.setWindowTitle(QStringLiteral("three"));
		one.show();
		two.show();
		three.show();
		QCoreApplication::processEvents();
		InputRouter orr(&one);
		Compositor oc(&one, &orr);
		CellBuffer ob(40, 8);
		oc.compose(ob);
		const QVector<QWidget *> first_order = Qtty::window_tabs();

		two.hide();
		QCoreApplication::processEvents();
		oc.compose(ob);
		two.show();
		QCoreApplication::processEvents();
		oc.compose(ob);
		const QVector<QWidget *> after = Qtty::window_tabs();
		CHECK(after == first_order,
		      "a window hidden and shown again comes back where it was on "
		      "the strip, rather than at the end of it");
		// AND A NEW WINDOW GOES TO THE END, which is the control: a strip
		// that never changed at all would satisfy the check above, and this
		// one says it still takes newcomers.
		//
		// The limit worth knowing, since the check is shaped around it:
		// windows first seen in the SAME pass arrive in whatever order Qt's
		// list had them, because that is the pass that discovers them. What
		// this rule promises is that the order does not change afterwards --
		// which is what a user watching the strip cares about.
		QWidget late;
		late.setAttribute(Qt::WA_DontShowOnScreen);
		late.setWindowTitle(QStringLiteral("late"));
		late.resize(GridMetrics::cells(16, 3));
		(new QLineEdit(&late))->setGeometry(0, 0, 8 * cw, ch);
		late.show();
		QCoreApplication::processEvents();
		oc.compose(ob);
		const QVector<QWidget *> grown = Qtty::window_tabs();
		CHECK(grown.size() == after.size() + 1 && grown.last() == &late,
		      "and a window shown later joins the end of the strip rather "
		      "than the middle of it");
		late.hide();
		one.hide();
		two.hide();
		three.hide();
		QCoreApplication::processEvents();
	}

	// ---- which window a closing one hands you to --------------------------
	//
	// The far end, until this: `tabs.first()` was the simplest thing to
	// write and was never a user's expectation. Every tabbed thing a
	// terminal user knows -- a browser, an editor, a multiplexer -- selects
	// the tab BESIDE the one that closed. Measured before the fix with three
	// windows: switch to the third, close it, and the terminal showed the
	// first.
	//
	// Composed frame by frame rather than driven by timers, because the
	// position is the one the strip last DREW: a probe that closed the
	// window before any frame had been composed with it current read a
	// stale index and reported the old behaviour, which cost a wrong
	// conclusion before the timing was noticed.
	{
		QWidget root;
		root.setAttribute(Qt::WA_DontShowOnScreen);
		root.setWindowTitle(QStringLiteral("root"));
		root.resize(GridMetrics::cells(40, 6));
		auto *in_root = new QLineEdit(&root);
		in_root->setGeometry(0, 0, 10 * cw, ch);
		root.show();
		QWidget middle;
		middle.setAttribute(Qt::WA_DontShowOnScreen);
		middle.setWindowTitle(QStringLiteral("middle"));
		middle.resize(GridMetrics::cells(20, 4));
		(new QLineEdit(&middle))->setGeometry(0, 0, 10 * cw, ch);
		middle.show();
		auto *last = new QWidget;
		last->setAttribute(Qt::WA_DontShowOnScreen);
		last->setWindowTitle(QStringLiteral("last"));
		last->resize(GridMetrics::cells(20, 4));
		(new QLineEdit(last))->setGeometry(0, 0, 10 * cw, ch);
		last->show();
		QCoreApplication::processEvents();

		InputRouter wr(&root);
		Compositor wc(&root, &wr);
		CellBuffer frame(40, 6);
		wc.compose(frame);                       // the strip exists
		Qtty::set_current_window(last);
		QCoreApplication::processEvents();
		wc.compose(frame);                       // and last is current in it
		const QVector<QWidget *> before = Qtty::window_tabs();
		const int was_at = before.indexOf(last);
		CHECK(was_at >= 0 && Qtty::current_window() == last,
		      "the control: the window about to close is current and is on "
		      "the strip");

		delete last;
		QCoreApplication::processEvents();
		wc.compose(frame);
		const QVector<QWidget *> after = Qtty::window_tabs();
		QWidget *const now = Qtty::current_window();
		// GUARDED, because the strip can be empty here and this check used
		// to crash the suite when it was: `window_tabs()` answers nothing
		// while fewer than two windows are on it, so `after.size() - 1` is
		// -1 and qBound(0, was_at, -1) trips Qt's own assertion. A sabotage
		// that removes the strip row produced exactly that, and the harness
		// reported it as a run cut off rather than as a check -- which is
		// the honest verdict and cost a backtrace to read.
		CHECK(!after.isEmpty()
		      && now == after.at(qBound(0, was_at, after.size() - 1)),
		      "closing the current window hands you its NEIGHBOUR on the "
		      "strip rather than the far end, which is what every tabbed "
		      "thing a terminal user knows does");
		// Guarded for the same reason as the line above: `before` is the
		// strip, and a sabotage that removes the strip row leaves it empty,
		// so `first()` on it is Qt's assertion rather than a check. The
		// harness found both in one run, as a suite cut off twice.
		CHECK(!before.isEmpty()
		      && (now != before.first() || was_at == 0),
		      "and that is a different window from the first one, which is "
		      "what this used to answer whatever you had been looking at");
		root.hide();
		middle.hide();
		QCoreApplication::processEvents();
	}

	// ---- and the same for a letter, within its population -----------------
	//
	// 8.154 settled the order BETWEEN populations: a menu's letter beats a
	// button's, the menu being the older meaning. The order WITHIN one was
	// never settled -- it was findChildren order, which is where the widgets
	// happen to have been built -- so two buttons claiming `S` answered the
	// same way wherever the focus was. Measured before this: with the focus
	// in the right-hand panel, Alt+S fired the LEFT panel's button.
	//
	// Both halves are asserted, because a rule that fixed the second by
	// breaking the first would look like a pass from the failing side.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 6));
		auto *left = new QWidget(&host);
		left->setGeometry(0, 0, 14 * cw, 4 * ch);
		auto *right = new QWidget(&host);
		right->setGeometry(15 * cw, 0, 14 * cw, 4 * ch);
		int in_left = 0, in_right = 0;
		auto *bl = new QPushButton(QStringLiteral("&Save"), left);
		bl->setGeometry(0, 0, 10 * cw, ch);
		QObject::connect(bl, &QPushButton::clicked, [&in_left] { ++in_left; });
		auto *br = new QPushButton(QStringLiteral("&Save"), right);
		br->setGeometry(0, 0, 10 * cw, ch);
		QObject::connect(br, &QPushButton::clicked, [&in_right] { ++in_right; });
		auto *field = new QLineEdit(right);
		field->setGeometry(0, ch, 10 * cw, ch);
		auto *far_field = new QLineEdit(left);
		far_field->setGeometry(0, ch, 10 * cw, ch);
		host.show();
		QCoreApplication::processEvents();

		InputRouter nr(&host);
		Qtty::set_current_window(&host);
		field->setFocus();
		QCoreApplication::processEvents();
		set_focus_widget(field);
		nr.on_key({0, QStringLiteral("s"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(in_right == 1 && in_left == 0,
		      "a letter two buttons claim answers for the one in the panel "
		      "the focus is in, rather than for whichever was built first");

		far_field->setFocus();
		QCoreApplication::processEvents();
		set_focus_widget(far_field);
		nr.on_key({0, QStringLiteral("s"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(in_left == 1 && in_right == 1,
		      "and it follows the focus to the other panel");

		// THE POPULATION ORDER IS UNTOUCHED, which is 8.154's decision: a
		// menu's letter still beats a button's, however far the menu is
		// from the focus.
		auto *bar = new QMenuBar(&host);
		bar->setGeometry(0, 5 * ch, 30 * cw, ch);
		QMenu *menu = bar->addMenu(QStringLiteral("&Save as"));
		menu->addAction(QStringLiteral("nothing"));
		QCoreApplication::processEvents();
		const int before_left = in_left, before_right = in_right;
		field->setFocus();
		QCoreApplication::processEvents();
		set_focus_widget(field);
		nr.on_key({0, QStringLiteral("s"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(in_left == before_left && in_right == before_right,
		      "while a menu claiming the same letter still takes it from "
		      "both buttons, near or far -- the order between populations "
		      "being a decision and the order within one an accident");
		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- two containers claiming one chord, which is nearest-wins ---------
	//
	// Qt's own MDI is where this was found: every QMdiSubWindow's system menu
	// carries the same `&Close` on Ctrl+F4, all with window context and all
	// in one window, so the chord is ambiguous before an application binds
	// anything -- and with the first claimant answering it closed a
	// subwindow the user was not in.
	//
	// The fixture is built here rather than borrowed from QMdiArea, because
	// Qt itself does not create those actions on every platform: measured,
	// two of them under the offscreen plugin and NONE under xcb, so a check
	// resting on them passes in one arm and fails in another for a reason
	// that has nothing to do with this rule. What is asserted is the rule.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		auto *left = new QWidget(&host);
		left->setGeometry(0, 0, 14 * GridMetrics::cw(), 4 * GridMetrics::ch());
		auto *right = new QWidget(&host);
		right->setGeometry(15 * GridMetrics::cw(), 0,
		                   14 * GridMetrics::cw(), 4 * GridMetrics::ch());
		auto *in_left = new QLineEdit(left);
		auto *in_right = new QLineEdit(right);
		int closed_left = 0, closed_right = 0;
		const auto claim = [](QWidget *owner, const char *name, int *counter) {
			auto *a = new QAction(QString::fromLatin1(name), owner);
			a->setShortcut(QKeySequence(QStringLiteral("Ctrl+F4")));
			a->setShortcutContext(Qt::WindowShortcut);
			QObject::connect(a, &QAction::triggered, [counter] { ++*counter; });
			owner->addAction(a);
		};
		claim(left, "close left", &closed_left);
		claim(right, "close right", &closed_right);
		host.show();
		QCoreApplication::processEvents();

		const auto clash = shortcut_conflicts(&host);
		bool ambiguous = false;
		for (const auto &c : clash)
			if (c.first == QKeySequence(QStringLiteral("Ctrl+F4"))
			    && c.second.size() == 2)
				ambiguous = true;
		CHECK(ambiguous,
		      "one chord claimed by two containers in one window is a "
		      "collision, which is the shape Qt's own MDI ships");

		InputRouter cr(&host);
		Qtty::set_current_window(&host);
		// The RECORD is set last, after the events. Under xcb a window that
		// has just been shown activates asynchronously, and the activation
		// moves Qt's focus to the first widget in the chain -- so a record
		// set before processEvents() was overwritten and the chord answered
		// for the wrong container. Measured: this fixture passed under the
		// offscreen plugin and failed under xcb until the two lines swapped.
		in_right->setFocus();
		QCoreApplication::processEvents();
		set_focus_widget(in_right);
		cr.on_key({Qt::Key_F4, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(closed_right == 1 && closed_left == 0,
		      "and the chord answers for the container the focus is in "
		      "rather than whichever the walk reached first");

		in_left->setFocus();
		QCoreApplication::processEvents();
		set_focus_widget(in_left);
		cr.on_key({Qt::Key_F4, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(closed_left == 1 && closed_right == 1,
		      "and it follows the focus to the other one, which a rule that "
		      "merely reversed the order would not do");
		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- a button with no room, and an action with no business -----------
	//
	// A toolbar too narrow for its actions hides the buttons it cannot fit
	// and puts them behind an extension chevron -- which a pointer clicks
	// and a keyboard cannot reach at all. The ACTION is still there, so its
	// letter still answers, and on a terminal that is the difference
	// between a narrow window losing commands and merely losing buttons.
	//
	// The other half is the distinction that makes it safe: an action the
	// APPLICATION hid must answer nothing. Qt reports an invisible action as
	// disabled -- measured, `QAction::trigger()` on one still fires, so it
	// is the enabled flag rather than Qt refusing the trigger that stops it
	// here -- and the claim enumeration already skips what is disabled.
	{
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *bar = win.addToolBar(QStringLiteral("Main"));
		const char *const names[] = {"&New", "&Open", "&Save", "Save &As",
			                         "&Print", "Pr&eview", "&Quit"};
		for (const char *n : names) bar->addAction(QString::fromLatin1(n));
		win.setCentralWidget(new QTextEdit(QStringLiteral("central")));
		win.resize(GridMetrics::cells(24, 8));          // too narrow for them
		win.show();
		QCoreApplication::processEvents();

		QAction *last = nullptr;
		for (QAction *a : bar->actions())
			if (a->text() == QStringLiteral("&Quit")) last = a;
		QToolButton *its_button = nullptr;
		for (QToolButton *b : bar->findChildren<QToolButton *>())
			if (b->defaultAction() == last) its_button = b;
		int fired = 0;
		QObject::connect(last, &QAction::triggered, [&fired] { ++fired; });

		InputRouter tr(&win);
		Qtty::set_current_window(&win);
		tr.on_key({0, QStringLiteral("q"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(its_button && !its_button->isVisible() && last->isVisible()
		      && fired == 1,
		      "an action whose toolbar button had no room still answers its "
		      "letter, the chevron hiding it being a thing only a pointer "
		      "can open");

		int ghost_fired = 0;
		auto *ghost = new QAction(QStringLiteral("&Zap"), &win);
		QObject::connect(ghost, &QAction::triggered,
		                 [&ghost_fired] { ++ghost_fired; });
		win.addAction(ghost);
		ghost->setVisible(false);
		QCoreApplication::processEvents();
		tr.on_key({0, QStringLiteral("z"), false, true, false});
		QCoreApplication::processEvents();
		const int while_hidden = ghost_fired;
		ghost->setVisible(true);
		QCoreApplication::processEvents();
		tr.on_key({0, QStringLiteral("z"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(while_hidden == 0 && ghost_fired == 1,
		      "while an action the application hid answers nothing, and the "
		      "same action shown answers again -- asserted as a pair, since "
		      "silence alone would also be a letter that never worked");
		win.hide();
		QCoreApplication::processEvents();
	}

	// ---- chords two things answer, which Qt would report and cannot -------
	//
	// QShortcutMap is what detects an ambiguous binding on a desktop, it
	// gates on the window being active, and no window activates here. So
	// Qt's own answer to "two things claim Ctrl+S" is gone: the router fires
	// the first and the other is silent for ever, with nothing to ask.
	//
	// CONTEXT is the half that makes the report worth reading. Two
	// WidgetShortcut claims on different widgets are not a collision -- only
	// one can ever be in play -- and a report that could not tell the
	// difference would be a list of every sequence used twice, which every
	// real application has.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 6));
		auto *one = new QLineEdit(&host);
		one->setGeometry(0, 0, 10 * GridMetrics::cw(), GridMetrics::ch());
		auto *two = new QLineEdit(&host);
		two->setGeometry(0, GridMetrics::ch(), 10 * GridMetrics::cw(),
		                 GridMetrics::ch());
		host.show();
		QCoreApplication::processEvents();

		// The same chord on two widgets, each asking for its OWN widget.
		// This is the arrangement an application reaches for deliberately,
		// and it is not a conflict.
		int narrow = 0;
		for (QLineEdit *e : {one, two}) {
			auto *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")),
			                         e);
			sc->setContext(Qt::WidgetShortcut);
			QObject::connect(sc, &QShortcut::activated, [&] { ++narrow; });
		}
		QCoreApplication::processEvents();
		CHECK(shortcut_conflicts(&host).isEmpty(),
		      "one chord on two widgets, each scoped to its own, is not a "
		      "conflict: only one of them is ever in play");

		// A window-context action and a widget-context shortcut on the same
		// chord. With focus in that widget both answer, and only the first
		// the router reaches ever fires.
		auto *act = new QAction(QStringLiteral("&Save"), &host);
		act->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
		act->setShortcutContext(Qt::WindowShortcut);
		int saved = 0;
		QObject::connect(act, &QAction::triggered, [&] { ++saved; });
		host.addAction(act);
		auto *rival = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")),
		                            two);
		rival->setContext(Qt::WidgetShortcut);
		rival->setObjectName(QStringLiteral("the rival"));
		int rivals = 0;
		QObject::connect(rival, &QShortcut::activated, [&] { ++rivals; });
		QCoreApplication::processEvents();

		const auto clash = shortcut_conflicts(&host);
		QStringList names;
		for (const auto &c : clash)
			names << c.first.toString() + QStringLiteral(": ")
			         + c.second.join(QStringLiteral(" / "));
		printf("info: chords answered twice [%s]\n",
		       qPrintable(names.join(QStringLiteral("; "))));
		CHECK(clash.size() == 1
		          && clash[0].first == QKeySequence(QStringLiteral("Ctrl+S"))
		          && clash[0].second.size() == 2
		          && clash[0].second.first() == QStringLiteral("the rival"),
		      "a chord two things answer under one focus is reported, with "
		      "the NEAREST claimant named first -- the one scoped to the "
		      "widget that has the focus, over the one scoped to the window "
		      "around it");

		InputRouter sr(&host);
		two->setFocus();
		set_focus_widget(two);
		QCoreApplication::processEvents();
		sr.on_key({Qt::Key_S, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(rivals == 1 && saved == 0,
		      "and the chord does what the report said: the nearest claimant "
		      "answers, which is the whole of why the report is ordered that "
		      "way rather than by the order the walk found them");

		// THE OTHER DIRECTION, which is what stops this being a rule that
		// always prefers a QShortcut: with the focus on a widget the rival
		// does not own, the window's own action is the nearest thing left
		// and answers.
		one->setFocus();
		set_focus_widget(one);
		QCoreApplication::processEvents();
		sr.on_key({Qt::Key_S, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(saved == 1 && rivals == 1,
		      "while from a widget the narrow claim does not cover, the "
		      "window's own action answers");

		// AND THE SUBTREE CONTEXT, through the OTHER copy of the rule.
		// This tree states the widget-context rule twice -- once in
		// context_applies() for actions, once in
		// shortcut_context_applies() for QShortcuts, which is what the
		// report asks -- and the comment above the second says a report
		// built on its own copy would be describing a different program.
		// The subtree case was checked in neither. A QShortcut is used
		// here deliberately, because an action would go through the first
		// copy and leave this one exactly as unwatched as it was.
		auto *deep = new QWidget(&host);
		auto *buried = new QLineEdit(deep);
		deep->show();
		QCoreApplication::processEvents();
		auto *subtree = new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")),
		                              deep);
		subtree->setContext(Qt::WidgetWithChildrenShortcut);
		subtree->setObjectName(QStringLiteral("the subtree claim"));
		auto *elsewhere = new QAction(QStringLiteral("Elsewhere"), &host);
		elsewhere->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
		elsewhere->setShortcutContext(Qt::WindowShortcut);
		host.addAction(elsewhere);
		QCoreApplication::processEvents();

		buried->setFocus();
		set_focus_widget(buried);
		QCoreApplication::processEvents();
		const auto deep_clash = shortcut_conflicts(&host);
		bool names_subtree = false;
		for (const auto &c : deep_clash)
			if (c.first == QKeySequence(QStringLiteral("Ctrl+G")))
				names_subtree =
				    c.second.contains(QStringLiteral("the subtree claim"));
		CHECK(names_subtree,
		      "a QShortcut scoped to a subtree is reported as answering "
		      "from a widget INSIDE it, which is the second copy of the "
		      "widget-context rule and was checked by nothing");

		// The discrimination, and the first version of this check had it
		// wrong in a way worth keeping. It moved the focus outside the
		// subtree and expected the claim to drop out of the report -- and
		// it did not, because THIS REPORT IS NOT ABOUT THE CURRENT FOCUS.
		// It asks whether any focus a user can reach makes two things
		// answer, which is what the candidates block below is about. A
		// claim inside a reachable subtree therefore answers somewhere,
		// and saying so is correct.
		//
		// So the edge has to be a claim that answers NOWHERE. The same
		// container with the narrower context is exactly that: `deep` is
		// a plain QWidget with no focus policy, so a WidgetShortcut on it
		// covers only a widget that can never hold focus, while
		// WidgetWithChildren on the same object covers the field inside.
		// One object, one chord, two contexts, opposite answers -- which
		// is the pair that tells the two apart rather than testing that
		// the reporter reports.
		auto *narrow_deep =
		    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), deep);
		narrow_deep->setContext(Qt::WidgetShortcut);
		narrow_deep->setObjectName(QStringLiteral("the owner-only claim"));
		auto *rival_y = new QAction(QStringLiteral("RivalY"), &host);
		rival_y->setShortcut(QKeySequence(QStringLiteral("Ctrl+Y")));
		rival_y->setShortcutContext(Qt::WindowShortcut);
		host.addAction(rival_y);
		QCoreApplication::processEvents();
		bool narrow_named = false;
		for (const auto &c : shortcut_conflicts(&host))
			if (c.first == QKeySequence(QStringLiteral("Ctrl+Y")))
				narrow_named = c.second.contains(
				    QStringLiteral("the owner-only claim"));
		CHECK(!narrow_named,
		      "while the same container claiming with the NARROWER context "
		      "answers at no reachable focus and is not reported, the "
		      "container itself taking none");

		host.hide();
		QCoreApplication::processEvents();
	}

	// ---- a focus the tab chain cannot reach, which the report used to
	// miss. The candidates were keyboard_reachable(), and a user reaches
	// focus by three routes rather than one: Tab, a click, and a label's
	// mnemonic, which calls setFocus() on its buddy whatever the buddy's
	// policy. A claim that answers only there was reported as no conflict
	// at all -- the direction that leaves the silent loser in place.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 10));
		auto *lay = new QVBoxLayout(&host);
		auto *tabbable = new QLineEdit;
		lay->addWidget(tabbable);
		auto *clicky = new QLineEdit;
		clicky->setFocusPolicy(Qt::ClickFocus);
		lay->addWidget(clicky);
		auto *label = new QLabel(QStringLiteral("&Notes"));
		label->setBuddy(clicky);
		lay->addWidget(label);

		int first = 0, second = 0;
		auto *one = new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")),
		                          clicky);
		one->setContext(Qt::WidgetShortcut);
		one->setObjectName(QStringLiteral("the first"));
		QObject::connect(one, &QShortcut::activated, [&] { ++first; });
		auto *two = new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")),
		                          clicky);
		two->setContext(Qt::WidgetShortcut);
		two->setObjectName(QStringLiteral("the second"));
		QObject::connect(two, &QShortcut::activated, [&] { ++second; });
		host.show();
		QCoreApplication::processEvents();

		const QVector<QWidget *> stops = keyboard_reachable(&host);
		CHECK(!stops.contains(clicky),
		      "a ClickFocus field is no tab stop, which is why the report "
		      "could not see a claim that answers only while it has focus");
		const auto clash = shortcut_conflicts(&host);
		CHECK(clash.size() == 1
		          && clash[0].first == QKeySequence(QStringLiteral("Ctrl+G"))
		          && clash[0].second.size() == 2,
		      "and the chord two claims answer there IS reported, the "
		      "candidates being every focus a user can reach rather than "
		      "the ones Tab walks");

		// THE RELATIONSHIP: the key really does reach that focus, and the
		// loser really is silent. Without both halves this is a check on
		// the report agreeing with itself.
		InputRouter br(&host);
		tabbable->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		br.on_key({0, QStringLiteral("n"), false, true, false});
		QCoreApplication::processEvents();
		CHECK(host.focusWidget() == clicky,
		      "a label's letter puts focus on a buddy Tab never visits, "
		      "which is the focus the conflict lives in");
		br.on_key({Qt::Key_G, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(first == 1 && second == 0,
		      "and there the chord answers one of the two and the other "
		      "never fires, which is the collision the report now names");
		host.hide();
		QCoreApplication::processEvents();
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

	// ---- the menu idiom an application actually writes: exec(), which runs
	// a NESTED event loop, re-entered from inside contextMenuEvent() -- so
	// the loop starts while the router is still dispatching the press that
	// opened it. Every fixture above uses popup(), which returns at once,
	// and 8.196 showed what "nested loop, never run here" can hide.
	//
	// Driven by a timer inside the loop, bounded by TRIES rather than by the
	// clock: under valgrind a wall-clock bound shrinks while the work does
	// not, which is how a timing fix becomes a second timing bug.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		host.show();
		QCoreApplication::processEvents();
		InputRouter mr(&host);
		Compositor mc(&host, &mr);
		CellBuffer mb(30, 8);
		mc.compose(mb);

		int chosen = 0, drawn_while_up = 0, gave_up = 0;
		for (int round = 0; round < 2; ++round) {      // twice: one proves
			QMenu menu(&host);                         // only the first
			menu.addAction(QStringLiteral("Alpha"));
			QAction *bravo = menu.addAction(QStringLiteral("Bravo"));
			QTimer driver;
			int tries = 0;
			driver.setInterval(5);
			QObject::connect(&driver, &QTimer::timeout, [&] {
				CellBuffer inner(30, 8);
				mc.compose(inner);
				if (inner.to_text().contains(QStringLiteral("Bravo")))
					++drawn_while_up;
				mr.on_key({Qt::Key_Down, QString(), false, false, false});
				mr.on_key({Qt::Key_Down, QString(), false, false, false});
				mr.on_key({Qt::Key_Return, QStringLiteral("\r"), false,
					       false, false});
				if (++tries > 200) { gave_up = 1; menu.close(); }
			});
			driver.start();
			if (menu.exec(QPoint(0, 0)) == bravo) ++chosen;
			driver.stop();
			QCoreApplication::processEvents();
		}
		CHECK(chosen == 2 && !gave_up,
		      "a context menu run with exec() returns the action the keys "
		      "chose, twice in a row -- the idiom an application writes, "
		      "and a nested loop inside the router's own dispatch");
		CHECK(drawn_while_up > 0 && mr.popups().isEmpty(),
		      "and the terminal drew the menu while that loop was spinning, "
		      "then the stack unwound to nothing");
	}

	// ---- a QCompleter, which is the other common popup and behaves
	// nothing like a menu. Qt marks its list by pointing the popup's focus
	// proxy at the widget being edited, and its own event filter hides that
	// list after any key the editor ACCEPTS unless `widget->hasFocus()` --
	// which is permanently false here. Measured before the split: typing
	// a-l-p gave visible, hidden, visible, so Down after a two-letter
	// prefix reached nothing at all.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 10));
		auto *edit = new QLineEdit(&host);
		edit->setGeometry(0, 0, 30 * GridMetrics::cw(), GridMetrics::ch());
		QStringList words;
		words << QStringLiteral("alpha") << QStringLiteral("alpine")
		      << QStringLiteral("alps");
		auto *comp = new QCompleter(words, edit);
		edit->setCompleter(comp);
		host.show();
		QCoreApplication::processEvents();
		InputRouter cr(&host);
		edit->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();

		int visible_after = 0;
		for (const char *ch : { "a", "l", "p" }) {
			cr.on_key({0, QString::fromLatin1(ch), false, false, false});
			QCoreApplication::processEvents();
			if (comp->popup()->isVisible()) ++visible_after;
		}
		CHECK(visible_after == 3 && edit->text() == QStringLiteral("alp"),
		      "a completer's list survives every letter typed at it, rather "
		      "than flickering out on the ones the editor accepts");
		CHECK(cr.input_popups().isEmpty() && !cr.popups().isEmpty(),
		      "and it is a layer this draws without giving it the keys, "
		      "which is what Qt's own focus proxy on it says to do");

		cr.on_key({Qt::Key_Down, QString(), false, false, false});
		QCoreApplication::processEvents();
		cr.on_key({Qt::Key_Down, QString(), false, false, false});
		QCoreApplication::processEvents();
		cr.on_key({Qt::Key_Return, QStringLiteral("\r"), false, false,
			       false});
		QCoreApplication::processEvents();
		CHECK(edit->text() == QStringLiteral("alpine"),
		      "while Down and Return DO reach it, so a completion can be "
		      "chosen with the keyboard -- the half a desktop's popup grab "
		      "does and neither arrangement here managed alone");

		// AND THE EDITOR IS STILL AN EDITOR afterwards, which is the
		// lockout question asked of this layer too.
		cr.on_key({0, QStringLiteral("!"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(edit->text().endsWith(QLatin1Char('!')),
		      "and typing goes on reaching the field once the list has "
		      "answered");
	}
	// What Qt tells a widget when focus LEAVES it. Qt delivers a QFocusEvent
	// only for an ACTIVE window, and no qtty window ever activates -- every
	// one carries WA_DontShowOnScreen -- so focusInEvent() and
	// focusOutEvent() never ran anywhere, and neither did anything Qt builds
	// on them.
	//
	// The one that costs an application most is QLineEdit::editingFinished(),
	// which Qt emits from focusOutEvent: a form only ever heard about a field
	// the user pressed Return in. The CONTROL is Return, which goes through a
	// different path and always worked -- so the pair separates "focus does
	// not notify" from "the signal is broken".
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *one = new QLineEdit(&h);
		auto *two = new QLineEdit(&h);
		one->setGeometry(0, 0, cw * 8, ch);
		two->setGeometry(0, ch, cw * 8, ch);
		int finished = 0;
		QObject::connect(one, &QLineEdit::editingFinished, [&] { ++finished; });
		h.resize(GridMetrics::cells(10, 3));
		h.show();
		one->setFocus();
		set_focus_widget(one);
		QCoreApplication::processEvents();
		InputRouter r(&h);
		// Typed into, and that is not decoration. Qt 6 gates
		// editingFinished on the field having actually been EDITED, so an
		// untouched one emits nothing on focus-out and would have made this
		// check pass against the defect for the wrong reason -- which it
		// did, on the first attempt.
		for (const QChar c : QStringLiteral("hi"))
			r.on_key({0, QString(c), false, false, false});
		QCoreApplication::processEvents();
		r.on_key({Qt::Key_Tab, QStringLiteral("\t"), false, false, false});
		QCoreApplication::processEvents();
		const int on_tab = finished;

		int by_return = 0;
		QLineEdit solo;
		solo.setText(QStringLiteral("x"));
		QObject::connect(&solo, &QLineEdit::editingFinished, [&] { ++by_return; });
		solo.setFocus();
		QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QCoreApplication::sendEvent(&solo, &ret);
		QCoreApplication::processEvents();

		printf("info: editingFinished fired %d time(s) on Tab, %d on"
		       " Return\n", on_tab, by_return);
		CHECK(by_return == 1, "Return finishes editing a field");
		CHECK(on_tab == 1, "and so does moving the focus off it");
	}

	// A second consumer was tried here and is NOT fixed, which is worth a
	// sentence rather than a silent absence. QAbstractItemView closes an
	// inline editor when the editor loses focus, and an editor still does
	// not close: measured, one open after editItem() and one still open
	// after the focus moved.
	//
	// The reason is that this fix reaches only focus qtty ITSELF moves.
	// QAbstractItemView::edit() calls setFocus() on the editor directly, so
	// s_focus never learns the editor has it, and the FocusOut goes to
	// whatever qtty last recorded instead. Closing that gap means noticing
	// Qt-initiated focus changes -- a filter on QEvent::FocusIn, or a
	// connection to QApplication::focusChanged, neither of which fires here
	// for the same reason this whole entry exists.
	//
	// Recorded in project.md; not attempted in passing, because a second
	// authority for focus is exactly the kind of thing this tree has
	// already been bitten by keeping two of.

	// WHAT REACHES AN APPLICATION when the application itself moves the
	// focus, which is the other half of the entry above and the half the
	// guide had stale. It said a window that never activates delivers no
	// FocusIn or FocusOut -- true of QT, and no longer true of this
	// library, which sends them itself. What an application can rely on is
	// worth pinning precisely, because the answer is "yes, one frame
	// later" and that is neither of the two obvious answers.
	{
		struct Watch : QLineEdit {
			int in = 0, out = 0;
			using QLineEdit::QLineEdit;
			void focusInEvent(QFocusEvent *e) override {
				++in;
				QLineEdit::focusInEvent(e);
			}
			void focusOutEvent(QFocusEvent *e) override {
				++out;
				QLineEdit::focusOutEvent(e);
			}
		};
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 8));
		auto *one = new Watch(&host);
		one->setGeometry(0, 0, 20 * cw, ch);
		auto *two = new Watch(&host);
		two->setGeometry(0, 2 * ch, 20 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter fr(&host);
		Compositor fc(&host, &fr);
		int app_signal = 0;
		const QMetaObject::Connection sig =
		    QObject::connect(qApp, &QApplication::focusChanged,
		                     [&](QWidget *, QWidget *) { ++app_signal; });
		one->setFocus();
		set_focus_widget(one);
		QCoreApplication::processEvents();
		one->in = one->out = two->in = two->out = 0;
		app_signal = 0;

		// The application moves focus in a slot, calling nothing of ours.
		two->setFocus();
		const bool silent_at_first = one->out == 0 && two->in == 0;
		CellBuffer fb(40, 8);
		fc.compose(fb);
		CHECK(silent_at_first && one->out == 1 && two->in == 1
		      && Qtty::focusWidget() == two,
		      "focus events reach a widget the APPLICATION focused, at the "
		      "next frame rather than at the call -- Qt sends none here at "
		      "all, and this library sends them when it re-reads the focus");

		// THE LIMIT, asserted rather than described: this one cannot be
		// repaired from outside Qt, QApplication::focusChanged being
		// emitted only by Qt's own activation path. An application told
		// that focus events work would otherwise reasonably reach for it.
		CHECK(app_signal == 0,
		      "while QApplication::focusChanged stays silent throughout, "
		      "which no library can emit on Qt's behalf -- so a program "
		      "watching focus watches the events, not the signal");
		QObject::disconnect(sig);
		GridGuard::reset();
	}

	// A ONE-CELL scroll bar's hit test. Its drawing learned about one cell;
	// subControlRect did not, so it fell through to Fusion's pixel
	// rectangles below two cells -- which is exactly what the comment above
	// that block forbids: "the hit test has to agree with the picture, and
	// two derivations of one layout is what put this file's spin box arrows
	// in the same cell."
	//
	// The fall-through was also asymmetric, which is the tell. Fusion's
	// SubLine and AddLine rectangles are computed in pixels, and a single
	// cell's centre falls inside one of them horizontally and between them
	// vertically -- so a one-cell horizontal bar always stepped and a
	// one-cell vertical bar never did. The two axes are asserted together
	// for that reason.
	//
	// Reached with no hint at all: a QTableWidget four rows tall lays its
	// vertical bar out at exactly one cell, the horizontal bar below taking
	// the other row.
	{
		const auto step = [&](Qt::Orientation o) {
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			auto *sb = new QScrollBar(o, &h);
			sb->setRange(0, 100);
			sb->setValue(0);                       // at the minimum: draws a
			sb->setGeometry(0, 0, cw, ch);         // step-forward arrow
			h.resize(GridMetrics::cells(6, 3));
			h.show();
			QCoreApplication::processEvents();
			InputRouter r(&h);
			r.on_mouse({QPoint(0, 0), 1, true, false, false, 0});
			r.on_mouse({QPoint(0, 0), 1, false, true, false, 0});
			QCoreApplication::processEvents();
			return sb->value();
		};
		const int v = step(Qt::Vertical), hz = step(Qt::Horizontal);
		printf("info: clicking a one-cell scroll bar at its minimum gives"
		       " %d vertical, %d horizontal\n", v, hz);
		CHECK(v > 0 && hz > 0,
		      "a one-cell scroll bar steps when its cell is clicked");
		CHECK(v == hz, "and both axes agree about how far");
	}

	// The window tab strip, and the press that chooses with it. A terminal is
	// one rectangle, so several top-level windows have to take turns; the
	// strip is what says which turn it is and how to change it.
	//
	// Three things, and the first is the one that keeps this from costing
	// anything: with ONE window there is no strip at all, so a program that
	// has always had one window looks exactly as it did and nothing below
	// row 0 moves.
	{
		QWidget a;
		a.setAttribute(Qt::WA_DontShowOnScreen);
		a.setWindowTitle(QStringLiteral("Alpha"));
		auto *al = new QLabel(QStringLiteral("AAA"), &a);
		al->setGeometry(0, 0, cw * 6, ch);
		a.resize(GridMetrics::cells(60, 4));
		a.show();
		QCoreApplication::processEvents();
		InputRouter r(&a);
		Compositor c(&a, &r);

		// SEVENTY columns, which is not arbitrary. This suite leaves visible
		// top-levels behind between blocks, so the strip carries more names
		// than this block made -- and a narrow strip ELIDES them. Measured:
		// at 18 columns the check passed on offscreen and failed under the
		// sanitizer and under xcb, where a different set of windows was
		// still up. The fixture was reading the suite's leftovers, not this
		// code.
		//
		// The single-window case is NOT asserted here, and the reason is
		// worth writing down: this suite leaves visible top-levels behind
		// between blocks, so by this point the strip legitimately carries
		// windows other blocks made. Measured -- a third tab appears in it.
		// Asserting "no strip" would be asserting the suite's tidiness
		// rather than this code's behaviour.
		//
		// What DOES hold that property is every other check in this file and
		// the others: they compose a single window and would every one be
		// offset by a row if a strip had appeared. A thousand of them pass,
		// which is a stronger control than one assertion here would be.
		QWidget b;
		b.setAttribute(Qt::WA_DontShowOnScreen);
		b.setWindowTitle(QStringLiteral("Beta"));
		auto *bl = new QLabel(QStringLiteral("BBB"), &b);
		bl->setGeometry(0, 0, cw * 6, ch);
		b.resize(GridMetrics::cells(60, 4));
		b.show();
		QCoreApplication::processEvents();

		CellBuffer two(70, 6);
		c.compose(two);
		const QString strip = two.to_text().section(QLatin1Char('\n'), 0, 0);
		const QString body = two.to_text().section(QLatin1Char('\n'), 1, 6);
		printf("info: two windows give strip [%s], showing %s\n",
		       qPrintable(strip.trimmed()),
		       body.contains(QStringLiteral("AAA")) ? "Alpha" : "Beta");
		CHECK(strip.contains(QStringLiteral("Alpha"))
		      && strip.contains(QStringLiteral("Beta")),
		      "two windows are both named in the strip");
		CHECK(!(body.contains(QStringLiteral("AAA"))
		        && body.contains(QStringLiteral("BBB"))),
		      "and they do not both draw into the same rectangle");

		// The press that switches. Its column is taken from the strip that
		// was drawn rather than counted by hand, so the assertion is about
		// the affordance and not about this test's arithmetic.
		const int beta_col = strip.indexOf(QStringLiteral("Beta"));
		r.on_mouse({QPoint(beta_col, 0), 1, true, false, false, 0});
		r.on_mouse({QPoint(beta_col, 0), 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CellBuffer after(70, 6);
		c.compose(after);
		const QString body2 = after.to_text().section(QLatin1Char('\n'), 1, 6);
		CHECK(body2.contains(QStringLiteral("BBB"))
		      && !body2.contains(QStringLiteral("AAA")),
		      "and a press on a tab shows that window instead");
		// And back again, which is what makes the strip a way to reach
		// EVERY window rather than a one-way door. A check on one direction
		// would pass against a strip that could only ever move forwards.
		const int alpha_col = strip.indexOf(QStringLiteral("Alpha"));
		r.on_mouse({QPoint(alpha_col, 0), 1, true, false, false, 0});
		r.on_mouse({QPoint(alpha_col, 0), 1, false, true, false, 0});
		QCoreApplication::processEvents();
		CellBuffer back(70, 6);
		c.compose(back);
		const QString body3 = back.to_text().section(QLatin1Char('\n'), 1, 6);
		CHECK(body3.contains(QStringLiteral("AAA"))
		      && !body3.contains(QStringLiteral("BBB")),
		      "and a press on the other tab comes back");

		// A click has to land where the widget is DRAWN, and the strip moved
		// everything down a row. Nothing shared that with the router:
		// measured, a button drawn at screen row 1 could not be clicked
		// there at all -- the same fault the comment beside root_scroll_
		// already records for scrolling, arriving by a second route on the
		// day tabs were added.
		//
		// The row is taken from the RENDER rather than counted: the button's
		// own row is found in the frame and clicked there, so the assertion
		// is that drawing and input agree, not that either is at row 1.
		int fired = 0;
		auto *go = new QPushButton(QStringLiteral("GOGO"), &a);
		go->setGeometry(0, 0, cw * 6, ch);
		// A child added to a parent that is ALREADY shown is not itself
		// shown, and a widget nobody shows is a widget nobody draws -- the
		// first version of this looked for it in the frame and found row -1.
		go->show();
		QObject::connect(go, &QPushButton::clicked, [&] { ++fired; });
		Qtty::set_current_window(&a);
		QCoreApplication::processEvents();
		CellBuffer withbtn(70, 6);
		c.compose(withbtn);
		int drawn_row = -1;
		const QStringList rows = withbtn.to_text().split(QLatin1Char('\n'));
		for (int i = 0; i < rows.size(); ++i)
			if (rows.at(i).contains(QStringLiteral("GOGO"))) { drawn_row = i; break; }
		if (drawn_row >= 0) {
			r.on_mouse({QPoint(1, drawn_row), 1, true, false, false, 0});
			r.on_mouse({QPoint(1, drawn_row), 1, false, true, false, 0});
			QCoreApplication::processEvents();
		}
		printf("info: with a strip up the button is drawn on row %d and a"
		       " click there fired %d\n", drawn_row, fired);
		CHECK(drawn_row > 0,
		      "a strip moves the window it shows down a row");
		CHECK(fired == 1,
		      "and a click lands where the widget is drawn, not a row above");

		// SECTION 7's POLICY BELONGS TO THE WINDOW BEING DRAWN, and it ran on
		// the window this compositor was CONSTRUCTED with. So a second window
		// too big for the terminal got neither half of it: nothing optional
		// was dropped and it never scrolled to its focus. Measured before the
		// fix with two windows in a 30x8 terminal, the second 26 rows tall
		// with focus on its last field -- the frame held row0 to row2 while
		// the field being typed into was row11, off the bottom with no key
		// that could reach it. That is 8.101's defect for every window except
		// the first.
		QWidget tall;
		tall.setAttribute(Qt::WA_DontShowOnScreen);
		tall.setWindowTitle(QStringLiteral("Tall"));
		auto *tv = new QVBoxLayout(&tall);
		tv->setContentsMargins(0, 0, 0, 0);
		tv->setSpacing(0);
		QLineEdit *bottom = nullptr;
		for (int i = 0; i < 12; ++i) {
			auto *e = new QLineEdit(QStringLiteral("ROW%1").arg(i), &tall);
			e->setFixedHeight(ch);
			tv->addWidget(e);
			bottom = e;
		}
		// An optional widget, so the FIRST half of the policy can be asked
		// about as well as the second. Section 7 drops these before it
		// scrolls, and a second window was dropping nothing.
		auto *spare = new QLabel(QStringLiteral("OPTIONALROW"), &tall);
		spare->setFixedHeight(ch);
		Qtty::set_priority(spare, Qtty::Priority::Optional);
		tv->addWidget(spare);
		// SIZED TO ITS CONTENT, and that is what makes the strip questions
		// below askable at all. At 26 cells the window is mostly empty space:
		// thirteen rows of fields and thirteen of nothing, so the row that
		// lands on screen row 0 once it scrolls carries no glyph, and a strip
		// drawn underneath it would survive by luck. Both strip sabotages
		// passed that way before this line was measured and changed. Thirteen
		// is the content, and still taller than the eight-row terminal, which
		// is what the other checks need.
		tall.resize(GridMetrics::cells(60, 13));
		tall.show();
		QCoreApplication::processEvents();

		Qtty::set_current_window(&tall);
		bottom->setFocus();
		set_focus_widget(bottom);
		QCoreApplication::processEvents();
		CellBuffer deep(70, 8);
		c.compose(deep);
		const QString shown = deep.to_text();

		// The control: this window cannot fit, so the question is real. A
		// window that fitted would be in the frame whatever the policy did.
		CHECK(tall.height() > 8 * ch,
		      "the case is real: the second window is taller than the "
		      "terminal it is drawn in");
		CHECK(shown.contains(bottom->text()),
		      "a window that is not the first still scrolls to its focus, so "
		      "the field being typed into is on screen");
		CHECK(!shown.contains(QStringLiteral("OPTIONALROW")),
		      "and still drops what the application marked optional");

		// The strip survives it. A scrolled window is drawn at a negative
		// offset, so whichever of its rows lands on screen row 0 painted
		// straight over the strip -- and the row is the strip's, all of it,
		// which is why the window's border no longer runs out of the last tab
		// to the right edge either.
		const QString top = shown.section(QLatin1Char('\n'), 0, 0);
		// NOT "does row 0 hold the window's text". Both of these were written
		// that way first and both sabotages -- the window drawn over the
		// strip, and the strip writing only where a label falls -- left them
		// GREEN and said so. What bleeds through is the window's BORDER, not
		// its text, so a check looking for "ROW" cannot see it.
		//
		// The relationship instead: row 0 holds the strip and nothing else.
		// Every character in it must belong to a tab name, to the brackets
		// around the current one, or be blank -- which fails whether the
		// window painted over the strip or showed through the gaps in it.
		QString leftover = top;
		for (QWidget *t : Qtty::window_tabs())
			leftover.remove(Qtty::window_tabs().isEmpty()
			                ? QString()
			                : t->windowTitle());
		leftover.remove(QLatin1Char('['));
		leftover.remove(QLatin1Char(']'));
		leftover.remove(QLatin1Char(' '));
		// Tab names are elided with an ellipsis when the strip is narrow, and
		// an object name stands in for a window with no title.
		leftover.remove(QChar(0x2026));
		leftover.remove(QStringLiteral("QWidget"));
		for (int d = 0; d < 10; ++d) leftover.remove(QChar(QLatin1Char('0' + d)));
		CHECK(top.contains(QStringLiteral("Tall")),
		      "the strip survives a window scrolled underneath it");
		//
		// WHAT THIS PAIR DOES NOT COVER, pinned rather than left to be
		// assumed: the strip CLEARING its row. Disabling the clear leaves
		// both of these green here, because at 70 columns with a 60-column
		// window the cells a label does not cover carry nothing of the
		// window either. The clear was added for a measured bleed at 30
		// columns -- the window's frame running out of the last tab to the
		// right edge -- and a 30-column fixture cannot be used here: the
		// strip elides names at that width and this suite leaves visible
		// top-levels behind, so the assertion would read the suite's
		// leftovers rather than this code. That trap is recorded at the top
		// of this block, and it is why there is no sabotage entry for the
		// clear rather than one that cannot redden.
		CHECK(leftover.isEmpty(),
		      "and keeps its whole row: nothing of the scrolled window shows "
		      "through it, border included");

		// And input agrees with the picture. set_root_scroll() now carries a
		// scroll that can belong to a window other than win_, and a click is
		// mapped through it: if the two disagreed, the field found in the
		// frame would not be the field a press on it reaches.
		int hit_row = -1;
		const QStringList deep_rows = shown.split(QLatin1Char('\n'));
		for (int i = 0; i < deep_rows.size(); ++i)
			if (deep_rows.at(i).contains(bottom->text())) { hit_row = i; break; }
		if (hit_row >= 0) {
			r.on_mouse({QPoint(2, hit_row), 1, true, false, false, 0});
			r.on_mouse({QPoint(2, hit_row), 1, false, true, false, 0});
			QCoreApplication::processEvents();
		}
		CHECK(hit_row > 0 && Qtty::focusWidget() == bottom,
		      "and a press on a scrolled second window's field reaches that "
		      "field, so drawing and input agree about the offset");

		// A WINDOW'S DROPPED WIDGETS ARE ITS OWN. root_ is reset when the
		// drawn window changes, and the first version of this entry had no
		// check that could see it: both windows' state came out the same, so
		// sabotaging the reset changed nothing and said so.
		//
		// What separates them is the hysteresis. apply_priority() remembers
		// what IT hid so that a terminal growing back shows exactly those --
		// so if one window's list is carried into another's layer, the policy
		// running for the second window restores the FIRST window's widgets
		// while it is not even being drawn.
		// The window that genuinely does not fit is the one to ask with:
		// the policy measures minimumSizeHint(), and `a` has no layout, so it
		// fits any terminal and drops nothing however small the buffer. The
		// first version of this check used `a` and failed as a control --
		// correctly, and for a reason about the fixture rather than the code.
		const bool dropped_in_tall = !spare->isVisible();
		CHECK(dropped_in_tall,
		      "the control: the optional widget of the window being drawn is "
		      "hidden while the terminal is too small for it");

		// LEAVING THE WINDOW PUTS BACK WHAT THE POLICY HID, and the check
		// that stood here asserted the opposite -- that the widget stays
		// hidden while another window is drawn. That reads as careful and
		// codified a defect: `dropped` is the only record that this policy
		// hid anything, so a reset that discards it without showing them
		// leaves them hidden with nobody holding the record. Returning finds
		// an empty list, and the drop loop skips them for being invisible
		// already. Measured, with an optional widget in a 30x7 window:
		//
		//     small, then roomy again            comes back
		//     small, visit another window,
		//       return, roomy                    HIDDEN, and for good
		//
		// The two sibling resets -- the modal's and the popup's -- had always
		// shown their dropped widgets before clearing. This one claimed in a
		// comment to follow that rule while following half of it.
		Qtty::set_current_window(&a);
		QCoreApplication::processEvents();
		CellBuffer roomy(70, 40);
		c.compose(roomy);
		CHECK(spare->isVisible(),
		      "leaving a window puts back the widgets its own policy hid, "
		      "rather than discarding the only record that they were hidden");

		// And the round trip ends where it started. This is the property the
		// hysteresis exists for, stated across a window switch: the widget is
		// back, and a roomy terminal keeps it.
		Qtty::set_current_window(&tall);
		QCoreApplication::processEvents();
		CellBuffer back_roomy(70, 40);
		c.compose(back_roomy);
		CHECK(spare->isVisible(),
		      "and it is still there on the way back, so a terminal that grew "
		      "while you were in another window shows what it can");

		// What the old check was reaching for, stated so that it does not
		// also forbid the window's own reset: a window's policy manages ITS
		// widgets. Composing `a` -- which has no layout and therefore fits
		// any terminal -- must leave the other window's optional widget
		// alone whatever size it is given.
		Qtty::set_current_window(&a);
		QCoreApplication::processEvents();
		CellBuffer pinched(70, 2);
		c.compose(pinched);
		CHECK(spare->isVisible(),
		      "while another window's policy leaves it alone, however little "
		      "room that window is given");

		// AND A WINDOW'S SCROLL DOES NOT FOLLOW YOU INTO THE NEXT ONE.
		//
		// This needs a window with nothing focusable in it, and the reason is
		// the whole point: follow_focus() recomputes the scroll every frame
		// from the focused widget, so a window that HAS one hides the fault
		// by immediately overwriting the inherited value. A window with no
		// focus widget gets no answer from follow_rect() and keeps whatever
		// the layer state already held -- which is the previous window's
		// scroll, computed against a window this one has never met.
		//
		// Found by sabotage: disabling the reset outright left all three
		// checks above green, because apply_priority()'s own put-back loop
		// restores the widgets whichever layer it is handed. The dropped
		// list was never the half that only the reset could protect.
		// A LAYOUT, and fixed heights, so the window's own minimum is
		// taller than the view. A layoutless window is resized down to the
		// terminal by the policy's second pass -- traced, this one arrived
		// at compose() 7 cells tall rather than the 20 it was given -- and
		// then `max_y` is zero, so the clamp at the end of follow_focus()
		// erases any inherited scroll and the check cannot fail. A fixture
		// that cannot express the failure is no check, which is the second
		// time in two days this section has taught that.
		QWidget flat;
		flat.setAttribute(Qt::WA_DontShowOnScreen);
		flat.setWindowTitle(QStringLiteral("Flat"));
		auto *flv = new QVBoxLayout(&flat);
		flv->setContentsMargins(0, 0, 0, 0);
		flv->setSpacing(0);
		for (int i = 0; i < 20; ++i) {
			// Labels only: nothing here may take focus, or follow_focus()
			// computes a scroll for this window and overwrites the
			// inherited one before it can be seen.
			auto *lab = new QLabel(i == 0 ? QStringLiteral("TOPROW")
			                              : QStringLiteral("flat%1").arg(i),
			                       &flat);
			lab->setFixedHeight(ch);
			flv->addWidget(lab);
		}
		flat.resize(GridMetrics::cells(60, 20));
		flat.show();
		QCoreApplication::processEvents();

		// Scroll `tall` first, so there is something to inherit: its focus is
		// on its last field and the terminal is short, so the policy scrolls
		// well down before the switch.
		Qtty::set_current_window(&tall);
		bottom->setFocus();
		set_focus_widget(bottom);
		QCoreApplication::processEvents();
		CellBuffer scrolled(70, 8);
		c.compose(scrolled);
		// BOTH HALVES, and the first version had only the absent one --
		// "ROW0 is not in the frame" is satisfied by a frame that does not
		// hold this window at all, which is what was happening: with five
		// windows up in this suite the control passed vacuously, the scroll
		// under test was another window's zero, and the sabotage below stayed
		// green. Asserting the LAST row present is what says this window is
		// drawn and scrolled, rather than absent.
		CHECK(scrolled.to_text().contains(QStringLiteral("ROW11"))
		      && !scrolled.to_text().contains(QStringLiteral("ROW0")),
		      "the control: the window switched away from is drawn and "
		      "genuinely scrolled, so there is an offset to inherit");

		Qtty::set_current_window(&flat);
		QCoreApplication::processEvents();
		CellBuffer plain(70, 8);
		c.compose(plain);
		CHECK(plain.to_text().contains(QStringLiteral("TOPROW")),
		      "a window with nothing focusable in it draws from its own top "
		      "rather than at the scroll the last window needed");

		// A TERMINAL RESIZE REACHES EVERY WINDOW THAT TAKES A TURN AT IT.
		// on_resize() resized win_ alone -- the window this router was built
		// with -- so a second window kept the size the terminal used to be.
		// Measured: growing to 80x24 left it 40x10, and switching to it drew
		// two rows of content in the corner of an empty screen.
		{
			QWidget other;
			other.setAttribute(Qt::WA_DontShowOnScreen);
			other.setWindowTitle(QStringLiteral("Other"));
			auto *ov = new QVBoxLayout(&other);
			ov->addWidget(new QLabel(QStringLiteral("OTHERCONTENT"), &other));
			other.resize(GridMetrics::cells(20, 4));
			other.show();
			QCoreApplication::processEvents();
			const QSize before = other.size();
			CHECK(before == GridMetrics::cells(20, 4),
			      "the control: the second window starts at the size it was "
			      "given");

			r.on_resize(QSize(60, 14));
			QCoreApplication::processEvents();
			CHECK(other.width() == 60 * cw && other.height() == 14 * ch,
			      "a terminal resize reaches a window that is not the one the "
			      "router was built with, since every window takes its turn "
			      "at the whole screen");

			// And NOT the layers whose geometry is their own. A dialog is
			// centred and clamped, a menu sits at its anchor; resizing either
			// to the whole terminal would be a different bug.
			QDialog dlg(&a);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.setModal(true);
			dlg.resize(GridMetrics::cells(10, 3));
			dlg.show();
			QCoreApplication::processEvents();
			r.on_resize(QSize(50, 12));
			QCoreApplication::processEvents();
			CHECK(dlg.width() == 10 * cw && dlg.height() == 3 * ch,
			      "while a modal keeps its own size, its geometry being the "
			      "dialog's business rather than the terminal's");
			dlg.close();
			other.hide();
			QCoreApplication::processEvents();
		}

		// CLOSING THE WINDOW YOU ARE IN, which is the second route that
		// changes the current window and the one that carried nothing.
		// Nothing in this suite had ever destroyed a window while it was
		// current, so the path compose() takes when the old one has gone --
		// which its own comment describes -- had no check over it at all.
		{
			auto *doomed = new QWidget;
			doomed->setAttribute(Qt::WA_DontShowOnScreen);
			doomed->setWindowTitle(QStringLiteral("Doomed"));
			auto *dv = new QVBoxLayout(doomed);
			dv->addWidget(new QLineEdit(QStringLiteral("doomed"), doomed));
			doomed->resize(GridMetrics::cells(20, 3));
			doomed->show();
			QCoreApplication::processEvents();
			Qtty::set_current_window(doomed);
			QCoreApplication::processEvents();
			CellBuffer with_it(70, 8);
			c.compose(with_it);
			CHECK(Qtty::current_window() == doomed,
			      "the control: the window about to be closed is the current "
			      "one");

			// The survivor's Qt focus is CLEARED first, and that is what
			// makes the focus check below able to fail at all.
			//
			// Two mechanisms produce "something inside the new window has
			// focus", and they agree on every window that already has a Qt
			// focus widget. adopt_window() seeds one -- but only `if
			// (!w->focusWidget())` -- and then records it; compose()'s own
			// re-read (8.119) records whatever Qt already has, whether or not
			// the pick carried anything. So with a survivor that had been
			// typed into, the sabotage for 8.118 stopped failing: the full
			// run reported "the named check PASSED against broken code", and
			// it was right.
			//
			// Cleared, only the seeding can supply a focus, and the two
			// answers separate. This is 8.121's shape exactly -- a check that
			// went on passing because a later fix covered its fixture by a
			// second route -- and it is the second time a full sabotage run
			// has been the only thing that could notice.
			// EVERY window the close could land on, not just this one.
			// 8.151 cleared the survivor's Qt focus so that only
			// adopt_window()'s seeding could supply one -- and 8.176 changed
			// which window survives, so clearing the main window alone left
			// the neighbour arriving with a focus of its own and the check
			// passing by the route it exists to exclude. The full run said
			// so: "the named check PASSED against broken code", twice in two
			// days, for the same check and two different reasons.
			for (QWidget *w : Qtty::window_tabs())
				if (QWidget *had = w->focusWidget()) had->clearFocus();
			if (QWidget *had = a.focusWidget()) had->clearFocus();
			// And the windows with NOTHING focusable in them are hidden,
			// because landing on one is correct behaviour and would make the
			// check below fail for a reason that is not the one it exists
			// for: `flat`'s own comment says nothing in it may take focus.
			flat.hide();
			QCoreApplication::processEvents();
			delete doomed;               // as an application closes a window
			QCoreApplication::processEvents();
			CellBuffer without(70, 8);
			c.compose(without);

			QWidget *const now = Qtty::current_window();
			CHECK(now && now->isVisible(),
			      "closing the current window leaves a visible one current");

			// And it is a window somebody can type into. The pick carried
			// nothing, so nothing seeded focus and the next keystroke went
			// nowhere at all -- measured before the fix.
			CHECK(Qtty::focusWidget()
			      && now->isAncestorOf(Qtty::focusWidget()),
			      "and something inside it has focus, so the next keystroke "
			      "has somewhere to land");

			// WITH THE MAIN WINDOW HIDDEN, which is what makes the check
			// above able to fail at all: the tab list held the root
			// unconditionally while every other window had to be
			// compositable, so the pick could land on a window the terminal
			// cannot draw -- and with a visible root it never does. The first
			// version of this check omitted the hiding and passed against the
			// defect, measured.
			auto *doomed2 = new QWidget;
			doomed2->setAttribute(Qt::WA_DontShowOnScreen);
			doomed2->setWindowTitle(QStringLiteral("Doomed2"));
			auto *d2v = new QVBoxLayout(doomed2);
			d2v->addWidget(new QLineEdit(QStringLiteral("d2"), doomed2));
			doomed2->resize(GridMetrics::cells(20, 3));
			doomed2->show();
			QCoreApplication::processEvents();
			a.hide();                       // the compositor's own window
			Qtty::set_current_window(doomed2);
			QCoreApplication::processEvents();
			CellBuffer hidden_root(70, 8);
			c.compose(hidden_root);
			CHECK(!a.isVisible() && Qtty::current_window() == doomed2,
			      "the control: the main window is hidden and another is "
			      "current");
			// AND THE STRIP DOES NOT NAME IT. This is what
			// `is_compositable(root)` actually guards, and the check below
			// used to stand in for it: with the root smuggled into the tab
			// list, the pick landed on a window the terminal cannot draw.
			// 8.176's pick no longer can -- it walks outwards and takes the
			// first SURVIVING candidate, so any visible window is chosen
			// before an invisible root -- and the full run said so, reporting
			// that check as passing against broken code. The strip's own
			// contents are the honest subject: a tab nobody can switch to is
			// a tab that should not be drawn.
			CHECK(!Qtty::window_tabs().contains(&a),
			      "and the strip does not name a window the terminal cannot "
			      "draw, a tab nobody can switch to being worse than no tab");

			delete doomed2;
			QCoreApplication::processEvents();
			CellBuffer after2(70, 8);
			c.compose(after2);
			QWidget *const now2 = Qtty::current_window();
			CHECK(now2 && now2->isVisible(),
			      "and with the main window hidden, closing the current one "
			      "still leaves a window the terminal can draw, rather than "
			      "the invisible root");
			a.show();
			QCoreApplication::processEvents();
		}

		Qtty::set_current_window(&a);
		flat.hide();
		QCoreApplication::processEvents();

		Qtty::set_current_window(&a);
		tall.hide();
		QCoreApplication::processEvents();
	}

	// DRAG AND DROP, which had no platform half at all. Qt splits it in
	// two: the widget side -- dragEnterEvent, dropEvent, the mime data -- is
	// ordinary Qt and works here untouched, and the platform side is what
	// carries the pointer while the drag is up. qtty's offscreen platform
	// has none, so QDrag::exec() returned Qt::IgnoreAction in under a
	// millisecond and no target ever heard anything.
	//
	// The pointer is this library's, so the missing half is one it can
	// supply. Asserted end to end: the target must hear the enter, must be
	// offered the moves, must receive the PAYLOAD, and exec_drag() must
	// return the action the target accepted rather than a hopeful default.
	{
		struct Target : QWidget {
			int enters = 0, moves = 0, drops = 0, leaves = 0;
			QString got;
			explicit Target(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
			void dragEnterEvent(QDragEnterEvent *e) override {
				++enters; e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dragMoveEvent(QDragMoveEvent *e) override {
				++moves; e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
			void dropEvent(QDropEvent *e) override {
				++drops; got = e->mimeData()->text();
				e->setDropAction(Qt::MoveAction); e->accept();
			}
		};
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *t = new Target(&h);
		t->setGeometry(0, 0, cw * 10, ch * 2);
		h.resize(GridMetrics::cells(12, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		auto *mime = new QMimeData;
		mime->setText(QStringLiteral("payload"));
		auto *drag = new QDrag(&h);
		drag->setMimeData(mime);

		// The drag is started from a timer rather than inline, because
		// exec_drag() does not return until the drop -- exactly as
		// QDrag::exec() does not. The mouse that drives it therefore has to
		// come from inside the nested loop, which is where a real one comes
		// from too.
		Qt::DropAction acted = Qt::IgnoreAction;
		QTimer::singleShot(0, [&] {
			r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
		});
		acted = Qtty::exec_drag(drag, Qt::CopyAction | Qt::MoveAction);
		QCoreApplication::processEvents();

		printf("info: a drag saw enter=%d move=%d drop=%d leave=%d, payload"
		       " \"%s\", action %d\n", t->enters, t->moves, t->drops,
		       t->leaves, qPrintable(t->got), int(acted));
		CHECK(t->enters == 1 && t->moves >= 1,
		      "a drag reaches the widget under the pointer");
		CHECK(t->drops == 1 && t->got == QStringLiteral("payload"),
		      "and the drop carries the payload");
		CHECK(acted == Qt::MoveAction,
		      "and the action the target chose is what exec_drag returns");

		// Escape ABANDONS a drag, which is what it does on every desktop.
		// drag_cancel() existed, was exported, and had no caller anywhere --
		// so a drag could be started and not given up, with the pointer
		// captured and every widget under it being offered something the
		// user had changed their mind about.
		//
		// The pair is the assertion: the target must have been offered the
		// drag (or cancelling proves nothing, an unstarted drag cancels
		// trivially) and must NOT have received a drop.
		auto *mime2 = new QMimeData;
		mime2->setText(QStringLiteral("abandoned"));
		auto *drag2 = new QDrag(&h);
		drag2->setMimeData(mime2);
		const int drops_before = t->drops, enters_before = t->enters;
		QTimer::singleShot(0, [&] {
			r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
			r.on_key({Qt::Key_Escape, {}, false, false, false});
		});
		// A bound, because exec_drag() does not return until the drag ends
		// and this check exists precisely to test the thing that ends it.
		// Without it a broken Escape does not fail the check -- it HANGS the
		// suite, which running-code.md says is worse than no gate at all.
		// Measured: with the cancel disabled the run stopped at 212 checks
		// and was killed by the suite's own timeout, and the sabotage harness
		// reported "the code was broken and nothing noticed" because a hang
		// produces neither a PASS nor a FAIL line.
		//
		// The rescue is not a workaround that hides the defect: it records
		// that it was NEEDED, and that is what the check asserts. If Escape
		// works the timer finds no drag and does nothing.
		bool needed_rescue = false;
		QTimer::singleShot(2000, [&] {
			if (Qtty::drag_active()) { needed_rescue = true; Qtty::drag_cancel(); }
		});
		const Qt::DropAction gave_up =
		    Qtty::exec_drag(drag2, Qt::CopyAction | Qt::MoveAction);
		QCoreApplication::processEvents();
		printf("info: an abandoned drag entered %d more, dropped %d more,"
		       " returned %d\n", t->enters - enters_before,
		       t->drops - drops_before, int(gave_up));
		CHECK(t->enters > enters_before,
		      "an abandoned drag was offered to the target first");
		CHECK(t->drops == drops_before && gave_up == Qt::IgnoreAction
		      && !needed_rescue,
		      "and Escape abandons it without a drop");

		// CROSSING from one widget to another, which coverage said had
		// never happened: every drag this suite had run saw one target or
		// none. The order is the whole of it, and drag.cpp says why -- the
		// widget being left hears the leave BEFORE the widget being entered
		// hears the enter, because a target that highlights on enter and
		// clears on leave otherwise ends up with two widgets both looking
		// like the drop site.
		//
		// Asserted as a SEQUENCE rather than as two counters. Counters are
		// satisfied by either order, which is exactly the fault.
		{
			QStringList order;
			struct Watched : QWidget {
				QStringList *log = nullptr;
				QString name;
				explicit Watched(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				void dragEnterEvent(QDragEnterEvent *e) override {
					*log << name + QStringLiteral(":enter");
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragLeaveEvent(QDragLeaveEvent *) override {
					*log << name + QStringLiteral(":leave");
				}
				void dropEvent(QDropEvent *e) override {
					*log << name + QStringLiteral(":drop");
					e->setDropAction(Qt::MoveAction); e->accept();
				}
			};
			QWidget h2;
			h2.setAttribute(Qt::WA_DontShowOnScreen);
			auto *left = new Watched(&h2);
			left->log = &order; left->name = QStringLiteral("left");
			left->setGeometry(0, 0, cw * 5, ch * 2);
			auto *right = new Watched(&h2);
			right->log = &order; right->name = QStringLiteral("right");
			right->setGeometry(cw * 6, 0, cw * 5, ch * 2);
			h2.resize(GridMetrics::cells(12, 4));
			h2.show();
			QCoreApplication::processEvents();
			InputRouter r2(&h2);

			auto *mime3 = new QMimeData;
			mime3->setText(QStringLiteral("crossing"));
			auto *drag3 = new QDrag(&h2);
			drag3->setMimeData(mime3);
			QTimer::singleShot(0, [&] {
				r2.on_mouse({QPoint(1, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(8, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(8, 0), 1, false, true, false, 0});
			});
			// The same rescue the abandoned-drag check carries, and for the
			// reason recorded there: a broken drag does not fail this, it
			// HANGS the suite, and a hang produces neither a PASS nor a FAIL.
			bool rescued = false;
			QTimer::singleShot(2000, [&] {
				if (Qtty::drag_active()) { rescued = true; Qtty::drag_cancel(); }
			});
			Qtty::exec_drag(drag3, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: crossing between two targets gave %s\n",
			       qPrintable(order.join(QStringLiteral(" "))));
			const int lv = order.indexOf(QStringLiteral("left:leave"));
			const int en = order.indexOf(QStringLiteral("right:enter"));
			CHECK(!rescued && lv >= 0 && en >= 0 && lv < en,
			      "a drag crossing from one widget to another tells the one "
			      "it left before the one it reached, so two widgets are "
			      "never both lit as the drop site");
		}

		// And a drop on a widget that was OFFERED the drag and refused it.
		// The else arm: it hears a leave, because it heard an enter, and
		// exec_drag reports IgnoreAction rather than a hopeful default.
		// Never run either -- every drag here had accepted.
		{
			struct Refuser : QWidget {
				int enters = 0, leaves = 0, drops = 0;
				explicit Refuser(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				// Offered and declined, which is not the same as a widget
				// that takes no drops at all: this one is asked and says no,
				// so it is the branch where `over` is set and
				// `over_accepts` is false.
				void dragEnterEvent(QDragEnterEvent *e) override {
					++enters; e->ignore();
				}
				void dragMoveEvent(QDragMoveEvent *e) override { e->ignore(); }
				void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
				void dropEvent(QDropEvent *e) override { ++drops; e->ignore(); }
			};
			QWidget h3;
			h3.setAttribute(Qt::WA_DontShowOnScreen);
			auto *no = new Refuser(&h3);
			no->setGeometry(0, 0, cw * 10, ch * 2);
			h3.resize(GridMetrics::cells(12, 4));
			h3.show();
			QCoreApplication::processEvents();
			InputRouter r3(&h3);

			auto *mime4 = new QMimeData;
			mime4->setText(QStringLiteral("refused"));
			auto *drag4 = new QDrag(&h3);
			drag4->setMimeData(mime4);
			QTimer::singleShot(0, [&] {
				r3.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r3.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
				r3.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
			});
			bool rescued2 = false;
			QTimer::singleShot(2000, [&] {
				if (Qtty::drag_active()) { rescued2 = true; Qtty::drag_cancel(); }
			});
			const Qt::DropAction refused =
			    Qtty::exec_drag(drag4, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: a refusing target saw enter=%d leave=%d drop=%d,"
			       " and the drag returned %d\n", no->enters, no->leaves,
			       no->drops, int(refused));
			CHECK(!rescued2 && no->enters >= 1 && no->drops == 0
			      && refused == Qt::IgnoreAction,
			      "a widget offered a drag and refusing it gets no drop, and "
			      "the drag reports that nothing was taken");
			CHECK(no->leaves >= 1,
			      "and it still hears the leave, because it heard the enter");
		}

		// THE WALK UP THE WIDGET TREE, which line coverage could not see:
		// `drop_target` was 100% of lines and its loop had never taken the
		// step that makes it a loop. Branch coverage says which direction --
		// the `acceptDrops()` test had never been false, so every drag this
		// suite ran landed on a widget that was itself the target.
		//
		// That is the case the function exists for and its comment says so:
		// a label inside a drop area is not itself a target, and the area
		// is. It is also the ordinary shape of a real drop target -- a panel
		// of labels, an item view's viewport -- so the untested path is the
		// common one and the tested one was the special case.
		{
			struct Area : QWidget {
				int enters = 0, drops = 0;
				QString got;
				explicit Area(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				void dragEnterEvent(QDragEnterEvent *e) override {
					++enters; e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dropEvent(QDropEvent *e) override {
					++drops; got = e->mimeData()->text();
					e->setDropAction(Qt::CopyAction); e->accept();
				}
			};
			QWidget h4;
			h4.setAttribute(Qt::WA_DontShowOnScreen);
			auto *area = new Area(&h4);
			area->setGeometry(0, 0, cw * 10, ch * 2);
			// The child takes no drops and is what the pointer is actually
			// over. Without the walk it is the target, and a target that
			// accepts nothing means the drop goes nowhere.
			auto *inner = new QLabel(QStringLiteral("label"), area);
			inner->setGeometry(0, 0, cw * 5, ch);
			h4.resize(GridMetrics::cells(12, 4));
			h4.show();
			QCoreApplication::processEvents();
			InputRouter r4(&h4);

			auto *mime5 = new QMimeData;
			mime5->setText(QStringLiteral("through the child"));
			auto *drag5 = new QDrag(&h4);
			drag5->setMimeData(mime5);
			QTimer::singleShot(0, [&] {
				r4.on_mouse({QPoint(1, 0), 1, false, false, true, 0});
				r4.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r4.on_mouse({QPoint(2, 0), 1, false, true, false, 0});
			});
			bool rescued3 = false;
			QTimer::singleShot(2000, [&] {
				if (Qtty::drag_active()) { rescued3 = true; Qtty::drag_cancel(); }
			});
			const Qt::DropAction took =
			    Qtty::exec_drag(drag5, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: a drop over a child gave the area enter=%d drop=%d"
			       " \"%s\"\n", area->enters, area->drops,
			       qPrintable(area->got));
			CHECK(!rescued3 && area->enters >= 1 && area->drops == 1
			      && area->got == QStringLiteral("through the child")
			      && took == Qt::CopyAction,
			      "a drop over a child that takes no drops reaches the "
			      "ancestor that does, which is how every real drop area is "
			      "built");
		}

		// AND OFF THE END OF IT: released where nothing accepts anything.
		// The other direction of the same loop -- it had never run out of
		// parents, so `drop_target` had never returned null and no drag had
		// ever been over nothing. It is what a user does to change their
		// mind: drag out of the drop area and let go over the background.
		{
			struct Area : QWidget {
				int enters = 0, leaves = 0, drops = 0;
				explicit Area(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				void dragEnterEvent(QDragEnterEvent *e) override {
					++enters; e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
				void dropEvent(QDropEvent *e) override { ++drops; e->accept(); }
			};
			// The HOST counts drag events and takes no drops, and that is
			// what makes this check discriminate. Asserting only that the
			// area got no drop passes against a `drop_target` that returns
			// the widget under the pointer instead of null: the host is then
			// the target, it ignores the enter, `over_accepts` stays false,
			// and the drop is refused anyway -- same leave, same
			// IgnoreAction, same everything a check would look at. Measured:
			// that sabotage passed until the host was watched.
			// NOT asserted here: that nothing was offered the drag over the
			// background. It cannot be, and the reason is worth the lines
			// because the obvious check passes in both worlds.
			//
			// `drop_target` returning null and returning the widget under
			// the pointer differ only in a value nothing can observe. With
			// the walk sabotaged to return `under`, the enter IS sent -- a
			// probe in `drag_move_to` shows `enter -> QWidget accepts=0` --
			// and Qt delivers it nowhere: `QWidget::event()` does not
			// dispatch a drag event to a widget with no drop support, and an
			// event filter on the receiver does not see it either, both
			// measured. The else arm then sends that widget a leave, equally
			// unseen. Same leave to the area, same absent drop, same
			// IgnoreAction.
			//
			// So a "the background was not offered the drag" check would be
			// a check that cannot fail, which this project holds to be worse
			// than none. What defends the walk is the sibling check above,
			// where the ancestor DOES accept drops and the difference is
			// visible.
			QWidget h5;
			h5.setAttribute(Qt::WA_DontShowOnScreen);
			auto *area = new Area(&h5);
			area->setGeometry(0, 0, cw * 4, ch);
			h5.resize(GridMetrics::cells(12, 4));
			h5.show();
			QCoreApplication::processEvents();
			InputRouter r5(&h5);

			auto *mime6 = new QMimeData;
			mime6->setText(QStringLiteral("changed my mind"));
			auto *drag6 = new QDrag(&h5);
			drag6->setMimeData(mime6);
			QTimer::singleShot(0, [&] {
				r5.on_mouse({QPoint(1, 0), 1, false, false, true, 0});
				r5.on_mouse({QPoint(9, 3), 1, false, false, true, 0});
				r5.on_mouse({QPoint(9, 3), 1, false, true, false, 0});
			});
			bool rescued4 = false;
			QTimer::singleShot(2000, [&] {
				if (Qtty::drag_active()) { rescued4 = true; Qtty::drag_cancel(); }
			});
			const Qt::DropAction nowhere =
			    Qtty::exec_drag(drag6, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: dragged off the area: enter=%d leave=%d drop=%d,"
			       " returned %d\n", area->enters, area->leaves,
			       area->drops, int(nowhere));
			CHECK(!rescued4 && area->enters >= 1 && area->leaves >= 1
			      && area->drops == 0 && nowhere == Qt::IgnoreAction,
			      "dragging off the drop area and letting go over the "
			      "background drops nothing, and the area is told it was "
			      "left");

		}
	}

	// A modal with an EMPTY geometry. The rule above drops every click
	// outside the modal, and an empty rectangle contains no point at all --
	// so such a dialog swallows every click on every cell of the terminal
	// while Compositor::is_compositable() refuses to draw it. Invisible and
	// holding the pointer, which the compositor's own modal comment calls
	// the worst of both.
	//
	// What makes it qtty's rather than the application's: Qt does NOT make
	// such a window modal on the desktop -- activeModalWidget() is null and
	// the clicks get through. It becomes modal only under
	// WA_DontShowOnScreen, which qtty stamps on every window. The same
	// application code is harmless with a platform window and locks a
	// terminal without one.
	//
	// The CONTROL is the ordinary modal beside it: real modality must still
	// block, or this would be "fixed" by not blocking at all.
	{
		const auto clicks_through = [&](bool empty) {
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			int fired = 0;
			auto *b = new QPushButton(QStringLiteral("root"), &h);
			b->setGeometry(0, 0, cw * 6, ch);
			QObject::connect(b, &QPushButton::clicked, [&] { ++fired; });
			h.resize(GridMetrics::cells(20, 6));
			h.show();
			QWidget d(&h, Qt::Dialog);
			d.setAttribute(Qt::WA_DontShowOnScreen);
			d.setWindowModality(Qt::ApplicationModal);
			if (empty) d.setFixedSize(0, 0);
			else d.setGeometry(cw * 8, ch * 2, cw * 8, ch * 2);
			d.show();
			QCoreApplication::processEvents();
			InputRouter r(&h);
			r.on_mouse({QPoint(1, 0), 1, true, false, false, 0});
			r.on_mouse({QPoint(1, 0), 1, false, true, false, 0});
			QCoreApplication::processEvents();
			return fired;
		};
		const int blocked = clicks_through(false);
		const int empty = clicks_through(true);
		printf("info: a click under a modal fires the button %d time(s);"
		       " under an EMPTY modal %d\n", blocked, empty);
		CHECK(blocked == 0, "a real modal blocks a click on the window under it");
		CHECK(empty == 1,
		      "and one with no geometry blocks nothing, being on no screen");
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


	// A WHEEL NOTHING TAKES, INSIDE A MODAL. Branch coverage found this:
	// the walk up the parent chain had never once run out of parents, and
	// `if (w == top) break;` -- the line that stops it escaping the input
	// layer -- had never fired. Every wheel this suite sent was accepted by
	// something before the question could arise.
	//
	// The consequence when it does: a modal's parent is the window behind
	// it, so a wheel over something in the dialog that does not scroll
	// walks straight out of the modal and into the window it is blocking.
	// That is section 8.3's rule -- while a modal is up it is the whole of
	// the input tree -- broken by a mouse wheel.
	//
	// The ROOT COUNTS WHEELS, and that is what makes this discriminate. The
	// first version put a QScrollArea behind the modal and asserted its
	// scroll bar had not moved: it passed with the guard deleted, because a
	// QScrollArea acts on wheels delivered to its VIEWPORT and the walk
	// reaches the area itself, so nothing would have moved either way. A
	// fixture on the safe side of the hazard, which this project has now
	// paid for often enough to look for first.
	{
		struct CountingRoot : QWidget {
			int wheels = 0;
			void wheelEvent(QWheelEvent *e) override { ++wheels; e->ignore(); }
		};
		CountingRoot root;
		root.setAttribute(Qt::WA_DontShowOnScreen);
		root.resize(GridMetrics::cells(20, 8));
		root.show();
		QCoreApplication::processEvents();

		QDialog dlg(&root);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		dlg.setModal(true);
		// A label takes no wheel, which is the point: something inside the
		// dialog has to DECLINE the event for the walk to continue past it.
		auto *inert = new QLabel(QStringLiteral("nothing here scrolls"), &dlg);
		inert->setGeometry(0, 0, 10 * cw, 2 * ch);
		dlg.setGeometry(0, 0, 12 * cw, 3 * ch);
		dlg.show();
		QCoreApplication::processEvents();

		InputRouter r6(&root);
		MouseEvent turn; turn.cell = QPoint(2, 1); turn.wheel = -3;
		r6.on_mouse(turn);
		QCoreApplication::processEvents();
		printf("info: a wheel inside a modal reached the window behind it"
		       " %d time(s)\n", root.wheels);
		CHECK(root.wheels == 0,
		      "a wheel nothing in a modal accepts stops at the modal rather "
		      "than reaching the window it is blocking");

		// The control, and it is not optional: with the modal gone the same
		// wheel over the same cell MUST reach the root. Without it, "0"
		// above is equally the answer for a router that delivers no wheel
		// anywhere, and the check would pass against a suite where the
		// whole mechanism was dead.
		dlg.hide();
		QCoreApplication::processEvents();
		r6.on_mouse(turn);
		QCoreApplication::processEvents();
		printf("info: and with the modal gone it reached it %d time(s)\n",
		       root.wheels);
		CHECK(root.wheels >= 1,
		      "and the same wheel does reach it once the modal is down, so "
		      "the zero above is the guard rather than a dead router");
	}

	// ---- the keyboard a terminal user expects --------------------------------
	//
	// A terminal has no mouse, so every control has to be reachable by key.
	// Two things were missing and they are different in kind: a MNEMONIC on
	// a button is a desktop behaviour this router dropped, and Enter-on-
	// focus and arrow navigation are terminal CONVENTIONS the desktop does
	// not have.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *field = new QLineEdit(QStringLiteral("one"));
		auto *name = new QLabel(QStringLiteral("&Name"));
		auto *named = new QLineEdit(QStringLiteral("two"));
		name->setBuddy(named);
		auto *apply = new QPushButton(QStringLiteral("&Apply"));
		int fired = 0;
		QObject::connect(apply, &QPushButton::clicked, [&] { ++fired; });
		v->addWidget(field);
		v->addWidget(name);
		v->addWidget(named);
		v->addWidget(apply);
		win.resize(GridMetrics::cells(30, 8));
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);
		auto key = [&](int qt_key, const QString &text, bool alt = false) {
			r.on_key({qt_key, text, false, alt, false});
			QCoreApplication::processEvents();
		};

		// ALT NEVER TYPES. A terminal sends Alt+Z as ESC then 'z', so the
		// decoder hands the router the letter in `text` -- and a text
		// field read the text and inserted it. Measured before the fix: a
		// field holding "abc" became "abcz".
		//
		// That is worse than a missing binding, and it is why this comes
		// first. Alt+letter is how a terminal user reaches a menu, so
		// pressing Alt+F for a File menu that is not there put an "f" in
		// whatever they were typing, silently.
		field->setFocus();
		set_focus_widget(win.focusWidget());
		field->setText(QStringLiteral("abc"));
		key(Qt::Key_Z, QStringLiteral("z"), true);
		CHECK(field->text() == QStringLiteral("abc"),
		      "Alt and a letter that matches nothing types nothing, rather "
		      "than putting the letter in whatever had focus");

		// A BUTTON's mnemonic, which is not an action and so was
		// unreachable: the router searched QActions and a push button is
		// not one. On a desktop Alt+A activates `&Apply`.
		field->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_A, QStringLiteral("a"), true);
		CHECK(fired == 1,
		      "Alt and a button's own letter activates it, which is how a "
		      "person without a mouse reaches it without tabbing there");

		// A LABEL's mnemonic moves focus to the field it names, which is
		// the other half of the same convention and the reason a form
		// labels its fields with an ampersand at all.
		field->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_N, QStringLiteral("n"), true);
		CHECK(win.focusWidget() == named,
		      "and a label's letter moves focus to the field it is the "
		      "buddy of");

		// The CONVENTIONS are off unless asked for, and that is the half
		// that keeps an unmodified application unmodified: an application
		// that binds Down or Enter itself keeps them.
		CHECK(!keyboard_conventions(),
		      "the terminal keyboard conventions are off until an "
		      "application asks for them");
		field->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_Down, QString());
		CHECK(win.focusWidget() == field,
		      "so Down does not move focus by itself, as it does not on a "
		      "desktop");
		const int before_enter = fired;
		apply->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_Return, QStringLiteral("\r"));
		CHECK(fired == before_enter,
		      "and Enter on a focused button does nothing, Space being the "
		      "desktop's key for that");

		set_keyboard_conventions(true);
		field->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_Down, QString());
		CHECK(win.focusWidget() != field,
		      "asked for, Down moves to the next control, so a form is "
		      "walkable without Tab");
		apply->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_Return, QStringLiteral("\r"));
		CHECK(fired == before_enter + 1,
		      "and Enter activates the control that has focus, which is "
		      "what Enter means on a terminal");

		// A DISABLED control is stepped over rather than landed on. The
		// walk is this library's own -- the obvious route to Qt's is
		// protected, and reaching it by casting a widget to a fake derived
		// class is the undefined behaviour 8.68 removed -- so what it
		// skips is qtty's to get right, and a form that parks focus on a
		// greyed-out field is one a person cannot get out of by pressing
		// the same key again.
		named->setEnabled(false);
		field->setFocus();
		set_focus_widget(win.focusWidget());
		key(Qt::Key_Down, QString());
		CHECK(win.focusWidget() != named && win.focusWidget() != field,
		      "and a disabled control is stepped over rather than landed "
		      "on");
		named->setEnabled(true);
		// A widget that WANTED the key keeps it. The conventions fire only
		// where the focused widget ignored one, and the case that shows it
		// is a list: Down moves its selection AND is accepted, so a
		// convention that ran anyway would move the selection and then
		// take focus off the list the user was working in.
		//
		// The first version of this check asserted that Enter in a text
		// field did not click the button -- which is true whether the
		// guard is there or not, because the convention clicks only a
		// focused BUTTON and a field is not one. The sabotage harness
		// caught it: "the code was broken and nothing noticed".
		{
			auto *list = new QListWidget;
			list->addItems({ QStringLiteral("one"), QStringLiteral("two") });
			v->addWidget(list);
			QCoreApplication::processEvents();
			list->setCurrentRow(0);
			list->setFocus();
			set_focus_widget(win.focusWidget());
			key(Qt::Key_Down, QString());
			CHECK(list->currentRow() == 1 && win.focusWidget() == list,
			      "a control that takes the key keeps it: Down moves a "
			      "list's selection and leaves focus on the list");
		}

		// KEYBOARD_REACHABLE -- the list an application's own test
		// asserts on. 8.71.
		//
		// `doc/keyboard-first.md` asks every implementer to check that
		// each control can be reached without a mouse, and told them the
		// loop takes ten lines. It does not: it is the router's own
		// traversal, and a hand-written walk of `nextInFocusChain()`
		// gets the filter wrong in the direction that hides the fault --
		// an invisible widget, one belonging to another window, or one
		// whose focus policy excludes Tab all appear in a raw walk and
		// are not stops. A test built on that reports a control
		// reachable that a user cannot get to, which is the single
		// answer it exists to rule out.
		{
			QWidget scope;
			auto *sv = new QVBoxLayout(&scope);
			auto *one = new QLineEdit;
			auto *two = new QPushButton(QStringLiteral("two"));
			auto *lab = new QLabel(QStringLiteral("a label"));
			auto *dis = new QLineEdit;
			auto *hid = new QLineEdit;
			for (QWidget *w : { (QWidget *)one, (QWidget *)two,
				                (QWidget *)lab, (QWidget *)dis,
				                (QWidget *)hid })
				sv->addWidget(w);
			dis->setEnabled(false);
			scope.setAttribute(Qt::WA_DontShowOnScreen);
			scope.show();
			hid->hide();
			QCoreApplication::processEvents();

			const QVector<QWidget *> reach = keyboard_reachable(&scope);
			CHECK(reach.contains(one) && reach.contains(two),
			      "keyboard_reachable lists the controls Tab reaches");
			CHECK(!reach.contains(lab) && !reach.contains(dis)
			      && !reach.contains(hid),
			      "and leaves out what Tab does not reach: a label, a "
			      "disabled field, and a hidden one");

			// THE RELATIONSHIP, which is the check worth having: Tab
			// really does visit exactly this set. Asserting the list
			// against a second copy of the filter would be asserting
			// the helper against itself.
			InputRouter r2(&scope);
			one->setFocus();
			QCoreApplication::processEvents();
			QVector<QWidget *> visited;
			for (int i = 0; i < reach.size(); ++i) {
				r2.on_key({Qt::Key_Tab, QStringLiteral("\t"),
					       false, false, false});
				QCoreApplication::processEvents();
				visited.append(scope.focusWidget());
			}
			QVector<QWidget *> a = reach, b = visited;
			std::sort(a.begin(), a.end());
			std::sort(b.begin(), b.end());
			CHECK(!a.isEmpty() && a == b,
			      "and pressing Tab that many times visits exactly the "
			      "widgets it named, so the list is what a person "
			      "walking the form with one key actually meets");

			// A NESTED scope, which is where the ancestor filter earns
			// its keep -- and the first version of this check could not
			// fail. It asserted that ANOTHER WINDOW's controls were
			// absent, and they are absent whatever the filter does:
			// Qt's focus chain does not span top-level windows, so the
			// walk returns to `scope` without ever meeting them. The
			// sabotage harness reported it, one entry after 8.70 said
			// the same thing about a different check.
			//
			// The chain DOES run on past the end of a group box, so a
			// walk from one reaches its siblings and comes back. That
			// is the case the filter exists for.
			auto *box = new QGroupBox(QStringLiteral("group"));
			auto *bv = new QVBoxLayout(box);
			auto *inner = new QLineEdit;
			bv->addWidget(inner);
			sv->addWidget(box);
			QCoreApplication::processEvents();
			const QVector<QWidget *> in_box = keyboard_reachable(box);
			CHECK(in_box.contains(inner) && !in_box.contains(one)
			      && !in_box.contains(two),
			      "asking about a container lists what is inside it and "
			      "not its siblings, the focus chain running on past the "
			      "end of a group");
		}

		// ---- the other side of the same question: what only a POINTER
		// reaches. Practice 4 of the guide is "never let an action be
		// reachable only by pointer", and until now nothing could check
		// it -- keyboard_reachable() names what Tab visits, which is not
		// the same list, because a button a mnemonic or a chord reaches
		// is keyed and in nobody's tab chain.
		//
		// The TOOLBAR is the case that decides the design. Its button is
		// Qt::NoFocus and not a tab stop, and `&Save` on the action it
		// carries reaches it perfectly well -- so an application sweeping
		// the focus chain for its own buttons reports a fault that is not
		// there. Only the library can subtract it, holding the claim
		// enumerations the router itself matches against.
		{
			QMainWindow win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			auto *tabs = new QTabWidget;
			tabs->setTabsClosable(true);
			tabs->addTab(new QLineEdit, QStringLiteral("One"));
			win.setCentralWidget(tabs);
			auto *bar = win.addToolBar(QStringLiteral("Main"));
			int saved = 0;
			QAction *save = bar->addAction(QStringLiteral("&Save"));
			QObject::connect(save, &QAction::triggered, [&] { ++saved; });
			int lettered = 0, silent = 0;
			auto *keyed = new QPushButton(QStringLiteral("&Lettered"), tabs);
			keyed->setFocusPolicy(Qt::NoFocus);
			QObject::connect(keyed, &QPushButton::clicked, [&] { ++lettered; });
			auto *mouse = new QPushButton(QStringLiteral("No focus"), tabs);
			mouse->setFocusPolicy(Qt::NoFocus);
			QObject::connect(mouse, &QPushButton::clicked, [&] { ++silent; });
			auto *gone = new QPushButton(QStringLiteral("Hidden"), tabs);
			gone->setFocusPolicy(Qt::NoFocus);
			auto *off = new QPushButton(QStringLiteral("Disabled"), tabs);
			off->setFocusPolicy(Qt::NoFocus);
			off->setEnabled(false);
			win.resize(GridMetrics::cells(60, 10));
			win.show();
			gone->hide();
			QCoreApplication::processEvents();

			const QVector<QWidget *> only = pointer_only(&win);
			QStringList named;
			for (QWidget *w : only)
				named << QString::fromLatin1(w->metaObject()->className());
			printf("info: pointer-only [%s]\n",
			       qPrintable(named.join(QStringLiteral(", "))));
			CHECK(only.contains(mouse),
			      "pointer_only names a button no key reaches, which is "
			      "the practice the guide asks for and the one thing an "
			      "application could not ask about");
			CHECK(!only.contains(gone) && !only.contains(off),
			      "and not a hidden or a disabled one, neither of which a "
			      "pointer reaches either");

			// The two exclusions, each proved by the key that makes it
			// true rather than by the report agreeing with itself.
			QWidget *button = nullptr;
			for (QToolButton *t : bar->findChildren<QToolButton *>())
				if (t->defaultAction() == save) button = t;
			CHECK(button && !only.contains(button),
			      "a toolbar's button is not named, its action's mnemonic "
			      "reaching it while Tab does not -- the case a sweep of "
			      "the focus chain alone gets wrong");
			CHECK(!only.contains(keyed),
			      "nor a button whose own letter Qt bound for it");

			InputRouter pr(&win);
			pr.on_key({0, QStringLiteral("s"), false, true, false});
			QCoreApplication::processEvents();
			pr.on_key({0, QStringLiteral("l"), false, true, false});
			QCoreApplication::processEvents();
			CHECK(saved == 1 && lettered == 1 && silent == 0,
			      "and both of those keys do reach their button, so the "
			      "report is excluding what a person can actually press");

			// QT'S OWN, which is 8.159 measured from the widget side: the
			// `x` on a closable tab is a real QAbstractButton with no
			// focus, no action and no key anywhere. Named, because the
			// remedy is the application's -- an entry in a menu -- and a
			// report that hid it would hide the practice's worst case.
			bool tab_close = false;
			for (QWidget *w : only)
				if (w->parentWidget() == tabs->tabBar()) tab_close = true;
			CHECK(tab_close,
			      "and Qt's own close button on a closable tab is named, "
			      "which is where the toolkit breaks the practice on the "
			      "application's behalf");
		}

		// A SPLITTER is the same finding in a control you drag rather than
		// one you click, and the guide had it wrong: it said a splitter, a
		// slider and a scroll bar all answer arrows when focused and are
		// merely hard to reach. Measured, with the focus forced onto each:
		//
		//     QSlider      StrongFocus, a tab stop,  50 -> 53
		//     QScrollBar   NoFocus, no tab stop,     50 -> 47
		//     QSplitter    NoFocus, no tab stop,     287/287 unchanged
		//
		// So a scroll bar answers and cannot be reached, while a splitter
		// does not answer at all -- there is no keyboard route to a split
		// anywhere in Qt, and the remedy is an action of the application's.
		{
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			host.resize(GridMetrics::cells(40, 10));
			auto *lay = new QVBoxLayout(&host);
			auto *split = new QSplitter(Qt::Horizontal);
			split->addWidget(new QLineEdit);
			split->addWidget(new QLineEdit);
			lay->addWidget(split);
			host.show();
			QCoreApplication::processEvents();

			QSplitterHandle *const grip = split->handle(1);
			CHECK(grip && pointer_only(&host).contains(grip),
			      "a splitter's handle is named, Qt's own word for a thing "
			      "you drag having no keyboard route at all");

			// THE RELATIONSHIP: not merely unreachable, unanswering. The
			// focus is put on the handle by hand -- which a user cannot
			// do -- and the arrows still move nothing.
			const QList<int> before = split->sizes();
			InputRouter sr(&host);
			grip->setFocus();
			set_focus_widget(host.focusWidget());
			QCoreApplication::processEvents();
			for (int i = 0; i < 3; ++i)
				sr.on_key({Qt::Key_Right, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(split->sizes() == before,
			      "and even with the focus put on it by hand the arrows "
			      "move the split by nothing, which is what makes it a "
			      "different case from a scroll bar");
		}

		// A SORTING HEADER is the third shape: what a pointer acts on is a
		// SECTION rather than a widget, so the report was silent on a
		// window where sorting is reachable only by pointer. Measured, in
		// this library and in plain Qt alike: 45 key combinations moved
		// neither the sort column nor the order, and QHeaderView declares
		// four mouse handlers and no keyPressEvent at all. Qt never
		// promised the practice; this guide does.
		{
			QWidget tables;
			tables.setAttribute(Qt::WA_DontShowOnScreen);
			tables.resize(GridMetrics::cells(30, 12));
			auto *sorted = new QTableWidget(2, 2, &tables);
			sorted->setGeometry(0, 0, 20 * GridMetrics::cw(),
			                    4 * GridMetrics::ch());
			sorted->setSortingEnabled(true);
			auto *plainly = new QTableWidget(2, 2, &tables);
			plainly->setGeometry(0, 5 * GridMetrics::ch(),
			                     20 * GridMetrics::cw(),
			                     4 * GridMetrics::ch());
			tables.show();
			QCoreApplication::processEvents();
			const QVector<QWidget *> named = pointer_only(&tables);
			CHECK(named.contains(sorted->horizontalHeader()),
			      "a header that offers sorting is named, the click that "
			      "sorts it having no key anywhere");
			CHECK(!named.contains(plainly->horizontalHeader()),
			      "and a header that offers none is not, since every table "
			      "has clickable sections and a report of all of them is "
			      "one nobody reads");
		}

		// THE CONTAINERS QT BUILDS CONTROLS INSIDE, asserted as a
		// POPULATION rather than one at a time. Each of these makes its own
		// buttons or handles, an application never sees them, and the
		// report is the only way anybody learns they are there. Swept:
		//
		//     QToolBox, 2 sections    2   the section headers
		//     QCalendarWidget         0   excluded, the keys reach it
		//     QSplitter               1   the handle
		//     QDockWidget             2   float and close
		//     QTabWidget, closable    2   the two close buttons
		//     QTableWidget, sortable  1   the header, not the corner
		//     QMdiArea, QScrollArea   0   nothing of their own
		//
		// The two zeroes are the control: a report that named everything
		// would satisfy every line above them and fail here.
		{
			const auto count_in = [](QWidget *w) {
				w->setAttribute(Qt::WA_DontShowOnScreen);
				w->resize(GridMetrics::cells(40, 14));
				w->show();
				QCoreApplication::processEvents();
				const int n = pointer_only(w).size();
				GridGuard::reset();
				return n;
			};
			QWidget boxed;
			{
				auto *v = new QVBoxLayout(&boxed);
				auto *box = new QToolBox;
				box->addItem(new QLineEdit, QStringLiteral("One"));
				box->addItem(new QLineEdit, QStringLiteral("Two"));
				v->addWidget(box);
			}
			CHECK(count_in(&boxed) == 2,
			      "a tool box's two section headers are named, Qt building "
			      "them Qt::NoFocus and in nobody's tab chain");
			QWidget dated;
			{
				auto *v = new QVBoxLayout(&dated);
				v->addWidget(new QCalendarWidget);
			}
			CHECK(count_in(&dated) == 0,
			      "and a calendar's four navigation buttons are not, "
			      "PageUp and PageDown reaching what they do");
			QWidget corner;
			{
				auto *v = new QVBoxLayout(&corner);
				auto *t = new QTableWidget(3, 3);
				v->addWidget(t);
			}
			CHECK(count_in(&corner) == 0,
			      "nor a table's corner button, Ctrl+A selecting exactly "
			      "what clicking it selects");
			QWidget closable;
			{
				auto *v = new QVBoxLayout(&closable);
				auto *t = new QTabWidget;
				t->setTabsClosable(true);
				t->addTab(new QLineEdit, QStringLiteral("One"));
				t->addTab(new QLineEdit, QStringLiteral("Two"));
				v->addWidget(t);
			}
			CHECK(count_in(&closable) == 2,
			      "a closable tab bar's two close buttons are, no key "
			      "closing a tab in this library or in plain Qt");
			QWidget scrolled;
			{
				auto *v = new QVBoxLayout(&scrolled);
				auto *a = new QScrollArea;
				auto *in = new QLineEdit;
				in->setMinimumWidth(600);
				a->setWidget(in);
				v->addWidget(a);
			}
			CHECK(count_in(&scrolled) == 0,
			      "and a scroll area names nothing, which is the control: "
			      "a report that named every internal widget would have "
			      "passed every line above and fails here");
		}

		// A LINK NO KEY CAN FOLLOW, the same shape one member along: the
		// thing clicked is an anchor inside a label rather than a widget.
		// It is the worse of the two for a user, because nothing on the
		// screen separates a link they can reach from one they cannot --
		// both are drawn underlined and coloured.
		//
		// Three labels, and the third is why this is a discrimination
		// rather than a flag test: Qt gives EVERY QLabel
		// Qt::LinksAccessibleByMouse, so a report keyed on the flag names
		// every label in the program. Measured -- default flags and an
		// explicit LinksAccessibleByMouse are the same 0x04, and plain
		// words carry it too.
		{
			QWidget page;
			page.setAttribute(Qt::WA_DontShowOnScreen);
			page.resize(GridMetrics::cells(40, 8));
			auto *mouse_only = new QLabel(
			    QStringLiteral("see <a href=\"http://x\">the docs</a>"), &page);
			mouse_only->setGeometry(0, 0, 30 * GridMetrics::cw(),
			                        GridMetrics::ch());
			auto *keyed_link = new QLabel(
			    QStringLiteral("or <a href=\"http://y\">the manual</a>"), &page);
			keyed_link->setTextInteractionFlags(
			    Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
			keyed_link->setGeometry(0, 2 * GridMetrics::ch(),
			                        30 * GridMetrics::cw(), GridMetrics::ch());
			auto *no_link = new QLabel(QStringLiteral("just words"), &page);
			no_link->setGeometry(0, 4 * GridMetrics::ch(),
			                     30 * GridMetrics::cw(), GridMetrics::ch());
			page.show();
			QCoreApplication::processEvents();
			const QVector<QWidget *> found = pointer_only(&page);
			CHECK(found.contains(mouse_only),
			      "a link only a click can follow is named, and it is drawn "
			      "exactly like one a key can reach");
			CHECK(!found.contains(keyed_link),
			      "a link with LinksAccessibleByKeyboard is not, Qt having "
			      "made it a tab stop");
			CHECK(!found.contains(no_link),
			      "and a label with no link in it is not, though it carries "
			      "the very same interaction flag every label carries");
		}

		// THE POPULATION ITSELF, which nothing asserted -- and the
		// installed header understated it for five days as a result.
		// `runtime.h` is the only contract that leaves this tree, and it
		// named three kinds while the function returned four: the link
		// label above landed in the code, the guide and the suite, and
		// the header was not in that commit. An application reading it
		// would not know a QLabel can come back.
		//
		// TWO ASSERTIONS, because one of them cannot see the fault that
		// happened. The first builds one pointer-only instance of each
		// kind and requires the result to be exactly those four, which
		// catches a kind that stops working or one the header names and
		// the code does not. It is blind to the real case -- a FIFTH
		// kind added to the code -- because no fixture here would hold
		// one.
		//
		// So the second counts the places the function appends against
		// the kinds the header names, which is the partition rather than
		// a member of it: a new `out.append` with no sentence beside it
		// reddens. It reads the two files rather than a copy of either,
		// so it cannot agree with itself the way a generated artifact
		// does.
		{
			QWidget page;
			page.setAttribute(Qt::WA_DontShowOnScreen);
			page.resize(GridMetrics::cells(40, 16));
			auto *lay = new QVBoxLayout(&page);

			auto *button = new QPushButton(QStringLiteral("Go"));
			button->setFocusPolicy(Qt::NoFocus);
			lay->addWidget(button);

			auto *split = new QSplitter(Qt::Horizontal);
			split->addWidget(new QLineEdit);
			split->addWidget(new QLineEdit);
			lay->addWidget(split);

			auto *table = new QTableWidget(2, 2);
			table->setSortingEnabled(true);
			lay->addWidget(table);

			auto *link = new QLabel(
			    QStringLiteral("<a href=\"http://e.invalid\">go</a>"));
			lay->addWidget(link);

			page.show();
			QCoreApplication::processEvents();

			QStringList kinds;
			for (QWidget *w : pointer_only(&page)) {
				const QString k =
				    QString::fromLatin1(w->metaObject()->className());
				if (!kinds.contains(k)) kinds.append(k);
			}
			kinds.sort();
			QStringList want;
			want << QStringLiteral("QHeaderView")
			     << QStringLiteral("QLabel")
			     << QStringLiteral("QPushButton")
			     << QStringLiteral("QSplitterHandle");
			want.sort();
			if (kinds != want)
				fprintf(stderr, "pointer_only kinds: %s\n",
				        kinds.join(QLatin1Char(' ')).toUtf8().constData());
			CHECK(kinds == want,
			      "one pointer-only instance of each documented kind comes "
			      "back and nothing else does");

			// The header's own paragraph, read rather than remembered.
			QFile head(QStringLiteral(QTTY_SOURCE_DIR
			                          "/include/qtty/runtime.h"));
			QFile code(QStringLiteral(QTTY_SOURCE_DIR
			                          "/src/runtime/input_router.cpp"));
			QString doc, body;
			if (head.open(QIODevice::ReadOnly | QIODevice::Text))
				doc = QString::fromUtf8(head.readAll());
			if (code.open(QIODevice::ReadOnly | QIODevice::Text))
				body = QString::fromUtf8(code.readAll());
			const int from = doc.indexOf(QStringLiteral(
			    "// The buttons in `scope` that only a pointer can press"));
			const int to = doc.indexOf(
			    QStringLiteral("QVector<QWidget *> pointer_only("), from);
			const QString para =
			    from >= 0 && to > from ? doc.mid(from, to - from) : QString();
			CHECK(!para.isEmpty(), "the header's pointer_only paragraph is "
			                       "where this check expects it");
			int named = 0;
			for (const QString &k : {QStringLiteral("QAbstractButton"),
			                         QStringLiteral("QSplitterHandle"),
			                         QStringLiteral("QHeaderView"),
			                         QStringLiteral("QLabel")})
				if (para.contains(k)) ++named;
			CHECK(named == 4, "and it names all four kinds, which it did "
			                  "not for the five days after the fourth "
			                  "landed");

			const int def = body.indexOf(
			    QStringLiteral("QVector<QWidget *> pointer_only(QWidget *scope) {"));
			const int end = body.indexOf(QStringLiteral("\n}\n"), def);
			const QString fn =
			    def >= 0 && end > def ? body.mid(def, end - def) : QString();
			CHECK(!fn.isEmpty(), "and the function body is where this check "
			                     "expects it");
			CHECK(fn.count(QStringLiteral("out.append(")) == named,
			      "and the function appends in exactly as many places as "
			      "the header names kinds, so a fifth cannot arrive "
			      "undocumented");

			// AND THE GUIDE, which was the worse of the two: it said the
			// population was QAbstractButton alone, so three documents
			// gave three different answers -- one kind, three kinds and
			// four. The guide is the deliverable and is the one an
			// implementer reads first.
			QFile page_md(QStringLiteral(QTTY_SOURCE_DIR
			                             "/doc/keyboard-first.md"));
			QString guide;
			if (page_md.open(QIODevice::ReadOnly | QIODevice::Text))
				guide = QString::fromUtf8(page_md.readAll());
			const int gfrom = guide.indexOf(
			    QStringLiteral("The population is four kinds"));
			const int gto = guide.indexOf(
			    QStringLiteral("The subtraction is the part you cannot"),
			    gfrom);
			const QString gpara =
			    gfrom >= 0 && gto > gfrom ? guide.mid(gfrom, gto - gfrom)
			                              : QString();
			int in_guide = 0;
			for (const QString &k : {QStringLiteral("QAbstractButton"),
			                         QStringLiteral("QSplitterHandle"),
			                         QStringLiteral("QHeaderView"),
			                         QStringLiteral("QLabel")})
				if (gpara.contains(k)) ++in_guide;
			CHECK(in_guide == named,
			      "and the guide names the same kinds the header does, "
			      "the two having disagreed with each other and with the "
			      "code");
		}

		// WHAT A CUSTOM WIDGET ASKS TO DRAW ITS OWN MARK. Practice 10
		// told it to write `Qtty::focusWidget() == this`, and the style
		// has never asked the question that way. Two things the pointer
		// test gets wrong, both silent and both only on the terminal,
		// which is the pair practice 10 exists to prevent.
		{
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			host.resize(GridMetrics::cells(30, 6));
			auto *lay = new QVBoxLayout(&host);
			auto *composite = new QWidget;
			auto *inner = new QVBoxLayout(composite);
			auto *edit = new QLineEdit;
			inner->addWidget(edit);
			composite->setFocusProxy(edit);      // the ordinary way
			lay->addWidget(composite);
			auto *plain = new QPushButton(QStringLiteral("Go"));
			lay->addWidget(plain);
			host.show();
			InputRouter hr(&host);
			composite->setFocus();
			set_focus_widget(host.focusWidget());
			QCoreApplication::processEvents();

			CHECK(Qtty::has_focus(composite)
			          && Qtty::focusWidget() != composite,
			      "a composite widget that delegates focus to an inner "
			      "editor is focused, and the pointer test the guide used "
			      "to give says it is not");
			CHECK(Qtty::has_focus(edit),
			      "and so is the editor it delegated to, both ends of the "
			      "proxy chain answering yes as QWidget::hasFocus() does");
			CHECK(!Qtty::has_focus(plain),
			      "while the button beside it is not, so this can say no "
			      "-- without which the two above would pass on a "
			      "function that always said yes");

			set_terminal_focused(false);
			CHECK(!Qtty::has_focus(edit)
			          && Qtty::focusWidget() == edit,
			      "a terminal that has lost focus withholds the mark, "
			      "where the pointer test still says to draw it -- which "
			      "is a custom widget marked alone on a screen every "
			      "standard control has gone quiet on");
			set_terminal_focused(true);
			CHECK(Qtty::has_focus(edit),
			      "and it comes back when the terminal does, so the row "
			      "above is the terminal's focus and not a widget that "
			      "lost its own");

			CHECK(!Qtty::has_focus(nullptr),
			      "and a null widget is not focused rather than a crash, "
			      "as the other questions here answer about nothing");
		}

		// SCROLLING, which the guide promises twice and nothing here
		// checked. Both of these came out of asking whether a keyboard
		// user can reach a scrollable region at all, and the answer was
		// yes both times -- so what these pin is a promise rather than a
		// repair, and the comment on the second is the part worth having.
		{
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(30, 8));
			auto *lay = new QVBoxLayout(&win);
			lay->addWidget(new QPushButton(QStringLiteral("&Before")));
			auto *area = new QScrollArea;
			auto *text = new QLabel;
			QString body;
			for (int i = 0; i < 40; ++i)
				body += QStringLiteral("line %1\n").arg(i);
			text->setText(body);
			area->setWidget(text);
			lay->addWidget(area);
			lay->addWidget(new QPushButton(QStringLiteral("&After")));
			win.show();
			InputRouter r(&win);
			// ADOPTED, or the Tab below counts from nothing rather than
			// from the button: a fixture with no composed frame has no
			// seeded focus, so the first Tab lands ON the first tab stop
			// instead of past it. Measured here the long way round -- the
			// check read QPushButton where it expected the area -- which
			// is 8.309 met again in a fixture of my own.
			Qtty::set_current_window(&win);
			QCoreApplication::processEvents();

			// NOTHING FOCUSABLE IS IN IT -- a label of text, which is the
			// ordinary shape of a licence, a log or a help page. Practice
			// 4 says every control is on the tab chain; a region a user
			// must be able to READ is the same promise, and it holds
			// because Qt gives QAbstractScrollArea a focus policy of its
			// own rather than because anything here arranged it.
			CHECK(keyboard_reachable(&win).contains(area),
			      "a scroll area holding nothing focusable is itself a tab "
			      "stop, so a page of text with no controls in it can "
			      "still be reached");

			const int flat = area->verticalScrollBar()->value();
			Qtty::test::press(r, Qt::Key_Tab);
			QCoreApplication::processEvents();
			CHECK(Qtty::has_focus(area),
			      "and one Tab past the button before it lands there");
			Qtty::test::press(r, Qt::Key_PageDown);
			QCoreApplication::processEvents();
			CHECK(area->verticalScrollBar()->value() > flat,
			      "and PageDown then moves it, which is what a reader "
			      "wants the key for");
		}

		// WHICH AREA, with two of them. The guide says an arrow the
		// focused widget ignores reaches "the scroll area that widget is
		// inside", and the fallback in on_key() does not do that -- it
		// takes the FIRST QAbstractScrollArea in the scope, whichever it
		// is. Both statements are true because the fallback is not what
		// runs here: Qt propagates an unhandled key up the parent chain,
		// the containing area consumes it, and the event comes back
		// accepted before the fallback is reached.
		//
		// Worth pinning precisely because reading the fallback suggests
		// the wrong answer. Every other fixture in this suite has ONE
		// scroll area, where the two rules agree and nothing can tell
		// them apart; this is the arrangement that can.
		{
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(60, 12));
			auto *lay = new QHBoxLayout(&win);
			auto *first = new QScrollArea;
			auto *tall_first = new QLabel;
			QString a_body;
			for (int i = 0; i < 60; ++i)
				a_body += QStringLiteral("first %1\n").arg(i);
			tall_first->setText(a_body);
			first->setWidget(tall_first);

			auto *second = new QScrollArea;
			auto *holder = new QWidget;
			auto *hv = new QVBoxLayout(holder);
			auto *inner = new QLineEdit;
			hv->addWidget(inner);
			auto *tall_second = new QLabel;
			QString b_body;
			for (int i = 0; i < 60; ++i)
				b_body += QStringLiteral("second %1\n").arg(i);
			tall_second->setText(b_body);
			hv->addWidget(tall_second);
			second->setWidget(holder);

			lay->addWidget(first);
			lay->addWidget(second);
			win.show();
			InputRouter r(&win);
			inner->setFocus();
			set_focus_widget(win.focusWidget());
			QCoreApplication::processEvents();

			const int f0 = first->verticalScrollBar()->value();
			const int s0 = second->verticalScrollBar()->value();
			for (int i = 0; i < 3; ++i) {
				Qtty::test::press(r, Qt::Key_PageDown);
				QCoreApplication::processEvents();
			}
			CHECK(second->verticalScrollBar()->value() > s0,
			      "with the focus inside the second of two scroll areas, "
			      "PageDown moves that one");
			CHECK(first->verticalScrollBar()->value() == f0,
			      "and leaves the first alone, which the fallback on its "
			      "own would not -- it takes whichever comes first");
		}

		// THE TEN AT ONCE, which exists because the hand-written list
		// drifts and this tree has watched it happen twice: the example
		// asserted eight reports while the page said nine, and adding a
		// tenth meant editing every place that enumerated them. An
		// eleventh has since been added and this check is what named
		// every place it had to go.
		{
			QWidget clean;
			clean.setAttribute(Qt::WA_DontShowOnScreen);
			clean.resize(GridMetrics::cells(30, 8));
			auto *lay = new QVBoxLayout(&clean);
			auto *field = new QLineEdit;
			auto *label = new QLabel(QStringLiteral("&Host"));
			label->setBuddy(field);
			lay->addWidget(label);
			lay->addWidget(field);
			lay->addWidget(new QPushButton(QStringLiteral("&Connect")));
			clean.show();
			InputRouter cr(&clean);
			QCoreApplication::processEvents();
			CHECK(Qtty::audit(&clean).isEmpty(),
			      "a window that passes every report passes this one, "
			      "which is the answer an application asserts");

			// AND IT CAN SAY NO, without which the line above would pass
			// on a function that returned empty whatever it was given.
			// One fault of a kind only ONE of the reports can see, so
			// the row has to have come from that report.
			auto *stray = new QPushButton(QStringLiteral("Go"), &clean);
			stray->setFocusPolicy(Qt::NoFocus);
			stray->setGeometry(0, 6 * GridMetrics::ch(),
			                   8 * GridMetrics::cw(), GridMetrics::ch());
			stray->show();
			QCoreApplication::processEvents();
			const QVector<QPair<QString, QString>> found =
			    Qtty::audit(&clean);
			QStringList questions;
			for (const auto &row : found) questions.append(row.first);
			CHECK(questions.contains(QStringLiteral("pointer_only")),
			      "a button no key reaches puts a row in it, named with "
			      "the question that found it rather than a bare widget");
			CHECK(found.size() == Qtty::pointer_only(&clean).size(),
			      "and nothing else does, so the aggregate agrees with "
			      "the single report it came from rather than inventing "
			      "or dropping a finding");

			CHECK(Qtty::audit(nullptr).isEmpty(),
			      "and a null scope answers empty, as every question it "
			      "asks does");
		}

		// THE TWO ANSWER DIFFERENT QUESTIONS, and a user meets the
		// difference. shortcut_help() lists what the application BOUND;
		// ambiguous_chords() says which of those a terminal cannot send.
		// So a program that prints the help line and never runs the audit
		// shows a user "Ctrl+I  Italic" on a screen where pressing it
		// moves the focus.
		//
		// Asserted as a PAIR rather than fixed, because neither answer is
		// wrong on its own: the binding exists and works wherever the
		// keyboard protocol does, and hiding it from the help would
		// silently drop a key that some terminals deliver. What the guide
		// tells an author is to ask both, and this is that partition --
		// the day one of them starts omitting the other's rows, this
		// fails rather than the two quietly disagreeing in a user's
		// status bar.
		{
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(40, 8));
			auto *bar = new QMenuBar(&win);
			auto *m = bar->addMenu(QStringLiteral("&Format"));
			auto *ital = m->addAction(QStringLiteral("&Italic"));
			ital->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
			auto *bold = m->addAction(QStringLiteral("&Bold"));
			bold->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
			win.show();
			QCoreApplication::processEvents();

			QStringList helped, flagged;
			for (const auto &row : shortcut_help(&win))
				helped.append(row.first);
			for (const auto &row : ambiguous_chords(&win))
				flagged.append(row.first);

			CHECK(helped.contains(QStringLiteral("Ctrl+I")),
			      "the help line lists a chord a terminal cannot send, "
			      "because the application did bind it and a terminal that "
			      "speaks the protocol delivers it");
			CHECK(flagged.contains(QStringLiteral("Ctrl+I")),
			      "and the audit names the same chord, which is why an "
			      "application has to ask both rather than trusting the "
			      "line it prints");
			CHECK(helped.contains(QStringLiteral("Ctrl+B"))
			          && !flagged.contains(QStringLiteral("Ctrl+B")),
			      "while an ordinary chord is in one and not the other, "
			      "without which the pair above would hold for a report "
			      "that named everything");
		}

		// THE CHORDS A TERMINAL CANNOT DELIVER, which is the gap the
		// keyboard-protocol section left open: the guide tells an
		// application to prefer another letter and nothing checked it.
		// shortcut_conflicts() structurally cannot -- to Qt a Ctrl+I and
		// a Tab are two different key sequences, which they are right up
		// until the wire.
		{
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(40, 10));
			auto *bar = new QMenuBar(&win);
			auto *m = bar->addMenu(QStringLiteral("&Edit"));
			const auto act = [m](const char *text, const char *key) {
				auto *a = m->addAction(QString::fromLatin1(text));
				a->setShortcut(QKeySequence(QString::fromLatin1(key)));
				return a;
			};
			act("&Italic", "Ctrl+I");
			act("&Mark", "Ctrl+M");
			act("&Help", "Ctrl+H");
			act("&Open", "Ctrl+O");
			act("&Paste special", "Ctrl+Shift+V");
			act("&Bracket", "Ctrl+[");
			act("&Plain", "F5");
			win.show();
			QCoreApplication::processEvents();

			QStringList named, labels;
			for (const auto &row : ambiguous_chords(&win)) {
				named.append(row.first);
				labels.append(row.second);
			}

			CHECK(named.contains(QStringLiteral("Ctrl+I"))
			          && named.contains(QStringLiteral("Ctrl+M"))
			          && named.contains(QStringLiteral("Ctrl+H"))
			          && named.contains(QStringLiteral("Ctrl+[")),
			      "the chords whose control byte is another key are named, "
			      "which is the half that does not merely fail but does "
			      "something else");
			CHECK(named.contains(QStringLiteral("Ctrl+Shift+V")),
			      "and a shifted control chord, which cannot be sent at "
			      "all because a control byte carries no shift bit");

			// THE CONTROL, and it is what keeps this from being a report
			// that names every binding: an ordinary Ctrl+letter is sent
			// and means itself, and a function key is not a control byte
			// at all.
			CHECK(!named.contains(QStringLiteral("Ctrl+O"))
			          && !named.contains(QStringLiteral("F5")),
			      "while an ordinary Ctrl+letter and a function key are "
			      "not, so this is a report an application can act on "
			      "rather than one it learns to ignore");
			CHECK(named.size() == 5,
			      "and nothing else in a window holding seven bindings, "
			      "five of which are the ones planted");

			CHECK(labels.contains(QStringLiteral("Italic")),
			      "each is named with the action's own text, its mnemonic "
			      "ampersand taken out as the help line does");

			CHECK(ambiguous_chords(nullptr).isEmpty(),
			      "and a null scope answers empty, as the other questions "
			      "do");
		}

		// THE APPLICATION'S OWN KEYS, which practice 8 asks every
		// terminal program to draw and which it could only hand-write.
		// The guide already says not to hand-write the ones this library
		// binds -- `keyboard_conventions_help()` -- on the ground that a
		// second copy of a fact is wrong the day the fact moves. The
		// application's own half was still a second copy.
		//
		// Every check here is about the SUBTRACTION rather than the
		// walk, because walking your own actions is the easy part and
		// getting these four wrong is what a hand-written line does.
		{
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(40, 10));
			auto *bar = new QMenuBar(&win);
			auto *file = bar->addMenu(QStringLiteral("&File"));
			auto *open = file->addAction(QStringLiteral("&Open..."));
			open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
			auto *save = file->addAction(QStringLiteral("&Save"));
			save->setShortcuts({QKeySequence(QStringLiteral("Ctrl+S")),
			                    QKeySequence(QStringLiteral("Ctrl+Shift+S"))});
			auto *greyed = file->addAction(QStringLiteral("&Revert"));
			greyed->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
			greyed->setEnabled(false);
			auto *amp = file->addAction(QStringLiteral("Fish && Chips"));
			amp->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
			auto *tb = new QToolBar(&win);
			tb->addAction(open);            // the same action, second owner
			auto *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")),
			                         &win, [] {});
			sc->setObjectName(QStringLiteral("Insert link"));
			win.show();
			QCoreApplication::processEvents();

			const QVector<QPair<QString, QString>> help = shortcut_help(&win);
			QStringList keys, labels;
			for (const auto &row : help) {
				keys.append(row.first);
				labels.append(row.second);
			}

			CHECK(!keys.contains(QStringLiteral("Ctrl+R")),
			      "a disabled action is left out of the help line, a key "
			      "that does nothing being worse there than an absent one");

			// GUARDED TWICE, and a sabotage run is what established
			// that rather than reading. `shortcut_claims()`
			// deduplicates per object, and `shortcut_help()` again per
			// (chord, label) -- and since one action reached twice
			// carries the same label both times, removing either leaves
			// the other covering this exactly. The entry written for
			// this check went green and was withdrawn.
			//
			// Both are still wanted: the first stops a second visit
			// bringing the same sequences again, the second folds two
			// DIFFERENT objects that agree on chord and label, which the
			// first cannot see. So this asserts the property a user
			// meets and not a mechanism, which is the honest thing for
			// it to say -- `evidence.md`'s two conditions each
			// independently saving the reported case.
			CHECK(keys.count(QStringLiteral("Ctrl+O")) == 1,
			      "an action owned by both a menu and a toolbar is one "
			      "row, not two, which is the walk an application writes "
			      "for itself getting it wrong");

			CHECK(keys.contains(QStringLiteral("Ctrl+S"))
			          && keys.contains(QStringLiteral("Ctrl+Shift+S")),
			      "while ONE action carrying two sequences is two rows, "
			      "since both answer and a user needs to know both");

			CHECK(labels.contains(QStringLiteral("Open...")),
			      "a mnemonic's ampersand is taken out of the label, "
			      "which would otherwise be drawn literally in a status "
			      "line");
			CHECK(labels.contains(QStringLiteral("Fish & Chips")),
			      "and a doubled one is a real ampersand and survives, "
			      "which is the case a plain remove() gets wrong");

			CHECK(labels.contains(QStringLiteral("Insert link")),
			      "a QShortcut is named by its objectName, so an "
			      "application that names one gets a line worth reading");

			// THE ORDER, because a hints line that reshuffles between
			// frames is one nobody can learn -- and because nothing else
			// here would notice if it did.
			CHECK(keys.indexOf(QStringLiteral("Ctrl+O"))
			          < keys.indexOf(QStringLiteral("Ctrl+S")),
			      "and the rows come in the order the application "
			      "declared its actions, which is usually its menu order");

			CHECK(shortcut_help(nullptr).isEmpty(),
			      "and a null scope answers empty rather than reaching "
			      "through it, as the other questions do");
		}

		// THE AUDIT SET ON A MIRRORED FORM, which the right-to-left
		// section claims was measured and which nothing here measured.
		// Its sentence read "all nine questions ... answer empty", and
		// that cannot be true of a form with two fields in it:
		// keyboard_reachable() returns what Tab visits, so an empty
		// answer there would contradict the clause beside it saying Tab
		// visits the fields in reading order.
		//
		// The suite DOES carry a check worded almost identically, a few
		// hundred lines up -- and it is about a null scope and an empty
		// window, where every one of the nine is correctly empty. One
		// sentence, two fixtures, and the guide took the wrong one.
		{
			QWidget form;
			form.setAttribute(Qt::WA_DontShowOnScreen);
			form.setLayoutDirection(Qt::RightToLeft);
			form.resize(GridMetrics::cells(40, 8));
			auto *lay = new QFormLayout(&form);
			auto *name = new QLineEdit;
			auto *city = new QLineEdit;
			auto *name_label = new QLabel(QStringLiteral("&Name"));
			auto *city_label = new QLabel(QStringLiteral("&City"));
			name_label->setBuddy(name);
			city_label->setBuddy(city);
			lay->addRow(name_label, name);
			lay->addRow(city_label, city);
			form.show();
			InputRouter fr(&form);
			QCoreApplication::processEvents();

			const QVector<QWidget *> reach = keyboard_reachable(&form);
			CHECK(reach.size() == 2 && reach.at(0) == name
			          && reach.at(1) == city,
			      "a mirrored form's fields are both reachable and in "
			      "reading order, which is the claim the guide makes and "
			      "the one an empty answer would deny");

			CHECK(pointer_only(&form).isEmpty()
			          && mnemonic_conflicts(&form).isEmpty()
			          && shortcut_conflicts(&form).isEmpty()
			          && conventions_shadowed(&form).isEmpty()
			          && tab_order_anomalies(&form).isEmpty()
			          && hover_only(&form).isEmpty()
			          && focus_invisible(&form).isEmpty()
			          && sheet_styled(&form).isEmpty(),
			      "and the other eight answer empty about it, mirroring "
			      "changing what is drawn and not what a key reaches");

			// And the mnemonic, which is the other half of that
			// paragraph: a buddy label's letter reaches its field with
			// the layout mirrored exactly as without.
			Qtty::test::mnemonic(fr, QLatin1Char('c'));
			QCoreApplication::processEvents();
			CHECK(Qtty::focusWidget() == city,
			      "and Alt on a buddy label's letter reaches the field it "
			      "names, with the form mirrored");
		}

		// THE WHOLE AUDIT SET, not one function's population. 8.310 fixed
		// a paragraph that had lost a kind; this is the same drift one
		// level up -- the guide's table of questions is prose a reader
		// trusts to be complete, and a function added without a row in it
		// is a feature nobody finds.
		//
		// A CHECK ALREADY GUARDS PART OF THIS and this does not replace
		// it: further down, `the page lists exactly the audit questions
		// these checks call` compares the table against a literal 9 in
		// the suite, so adding a tenth of anything forces somebody to
		// bump that number deliberately. What it cannot see is a
		// function DECLARED and never called from here -- the count it
		// compares against is the suite's own, not the header's.
		//
		// BY NAME RATHER THAN BY COUNT, and the first version of this
		// counted. It read every `(QWidget *scope);` declaration as an
		// audit question, which is a true sentence about the header and
		// not a characterisation of the set: `shortcut_help()` takes a
		// scope and answers a question about the application rather than
		// reporting a fault in it, so adding it turned this check red
		// for a correct change. The population is named here instead --
		// one line to add when a scope function is not an audit
		// question, which is a deliberate act rather than an ignore
		// list, and comparing names catches a rename that a count
		// cannot.
		{
			QFile head(QStringLiteral(QTTY_SOURCE_DIR
			                          "/include/qtty/runtime.h"));
			QFile page_md(QStringLiteral(QTTY_SOURCE_DIR
			                             "/doc/keyboard-first.md"));
			QString doc, guide;
			if (head.open(QIODevice::ReadOnly | QIODevice::Text))
				doc = QString::fromUtf8(head.readAll());
			if (page_md.open(QIODevice::ReadOnly | QIODevice::Text))
				guide = QString::fromUtf8(page_md.readAll());
			CHECK(!doc.isEmpty() && !guide.isEmpty(),
			      "the header and the guide are both readable, an "
			      "unreadable one being a check that cannot fail");

			// Takes a scope and is NOT an audit question. Each needs a
			// reason, because the default is that it is one.
			QStringList not_audits;
			not_audits << QStringLiteral("shortcut_help")    // a help line
			           << QStringLiteral("audit");           // the others

			QStringList declared;
			for (const QString &line : doc.split(QLatin1Char('\n'))) {
				const int at = line.indexOf(
				    QStringLiteral("(QWidget *scope);"));
				if (at < 0) continue;
				int from = at;
				while (from > 0 && (line.at(from - 1).isLetterOrNumber()
				                    || line.at(from - 1) == QLatin1Char('_')))
					--from;
				const QString name = line.mid(from, at - from);
				if (!name.isEmpty() && !not_audits.contains(name))
					declared.append(name);
			}
			QStringList listed;
			for (const QString &line : guide.split(QLatin1Char('\n'))) {
				if (!line.startsWith(QStringLiteral("| `Qtty::"))) continue;
				const int open = line.indexOf(QStringLiteral("(scope)`"));
				if (open < 0) continue;
				listed.append(line.mid(9, open - 9));
			}
			declared.sort();
			listed.sort();
			CHECK(!declared.isEmpty() && !listed.isEmpty(),
			      "and both lists are non-empty, so neither pattern has "
			      "quietly stopped matching");
			if (declared != listed)
				fprintf(stderr, "audit set: header [%s] guide [%s]\n",
				        declared.join(QLatin1Char(' ')).toUtf8().constData(),
				        listed.join(QLatin1Char(' ')).toUtf8().constData());
			CHECK(declared == listed,
			      "and the header's audit questions are exactly the ones "
			      "the guide's table names, by name rather than by count");

			// AND THE README, which is the THIRD place the population is
			// written down and the one nobody was checking. It said eight
			// when the guide said nine -- stale from the day
			// sheet_styled() landed -- and adding a tenth made it staler,
			// because this check read the guide's table and not that one.
			// Fixing the copy in front of me would have left the next
			// reader exactly where I was.
			QFile readme(QStringLiteral(QTTY_SOURCE_DIR "/README.md"));
			QString intro;
			if (readme.open(QIODevice::ReadOnly | QIODevice::Text))
				intro = QString::fromUtf8(readme.readAll());
			QStringList advertised;
			for (const QString &line : intro.split(QLatin1Char('\n'))) {
				if (!line.startsWith(QStringLiteral("| `Qtty::"))) continue;
				const int shut = line.indexOf(QStringLiteral("()`"));
				if (shut < 0) continue;
				advertised.append(line.mid(9, shut - 9));
			}
			advertised.sort();
			CHECK(!advertised.isEmpty(),
			      "the README's table of questions is where this check "
			      "expects it");
			if (advertised != listed)
				fprintf(stderr, "README [%s] guide [%s]\n",
				        advertised.join(QLatin1Char(' '))
				            .toUtf8().constData(),
				        listed.join(QLatin1Char(' ')).toUtf8().constData());
			CHECK(advertised == listed,
			      "and names the same questions the guide's does, all "
			      "three copies of the population being held to one "
			      "another rather than two of them to each other");

			// AND THE SENTENCE ABOVE THE TABLE, which is the third copy
			// of the number and the one a reader meets first. The top of
			// that page used to carry a fourth and it was stale; that
			// one is gone rather than guarded, a count being safest
			// where it is written once. This is the once.
			QStringList words;
			words << QStringLiteral("Zero") << QStringLiteral("One")
			      << QStringLiteral("Two") << QStringLiteral("Three")
			      << QStringLiteral("Four") << QStringLiteral("Five")
			      << QStringLiteral("Six") << QStringLiteral("Seven")
			      << QStringLiteral("Eight") << QStringLiteral("Nine")
			      << QStringLiteral("Ten") << QStringLiteral("Eleven")
			      << QStringLiteral("Twelve");
			const int n = listed.size();
			const QString said =
			    n < words.size()
			        ? words.at(n)
			              + QStringLiteral(" questions the library will "
			                               "answer")
			        : QString();
			CHECK(!said.isEmpty() && guide.contains(said),
			      "and the sentence above that table counts them in the "
			      "same number the table holds");
		}

		// WHICH BUTTON ENTER FIRES, pinned because practice 2 now states
		// it and because every claim in that section is about Qt rather
		// than about this library -- so the day Qt changes, the guide is
		// wrong and nothing else here would notice.
		//
		// THESE ARE TRIPWIRES AND NOT GUARDED INVARIANTS, which is worth
		// saying because the rest of this suite carries a sabotage entry
		// per check and these mostly cannot: there is no line in this
		// tree to break that would make Qt stop marking the first button
		// as default. The one exception is the second check below, which
		// does depend on this library -- a dead window delivers no focus
		// events, so `autoDefault` moves here only because the router
		// synthesises them.
		//
		// The lens that produced this came back EMPTY, and the empty
		// result is the reason these exist. A `dialogs_without_default()`
		// was written to make practice 2 assertable and measured useless
		// before it shipped: Qt marks a default on show in every dialog
		// shape built for it, so the function could not report anything
		// and would have been a check that cannot fail. What was left
		// worth having is the behaviour itself, written down.
		{
			const auto marked = [](QDialog *d) {
				QStringList out;
				for (QPushButton *b : d->findChildren<QPushButton *>())
					if (b->isDefault()) out.append(b->text());
				return out.join(QLatin1Char('+'));
			};
			const auto focus_on = [](QDialog *d, QWidget *w) {
				w->setFocus();
				set_focus_widget(d->focusWidget());
				QCoreApplication::processEvents();
			};

			// NO setDefault() ANYWHERE, which is the case the guide's
			// first table is about: an application that never thought
			// about this still has a default, and it is the first button
			// it happened to construct.
			QDialog d;
			d.setAttribute(Qt::WA_DontShowOnScreen);
			d.resize(GridMetrics::cells(30, 8));
			auto *l = new QVBoxLayout(&d);
			auto *field = new QLineEdit;
			auto *one = new QPushButton(QStringLiteral("One"));
			auto *two = new QPushButton(QStringLiteral("Two"));
			l->addWidget(field);
			l->addWidget(one);
			l->addWidget(two);
			d.show();
			InputRouter dr(&d);
			QCoreApplication::processEvents();
			CHECK(marked(&d) == QStringLiteral("One"),
			      "a dialog nobody marked still has a default button, and "
			      "it is the first one constructed");

			focus_on(&d, two);
			CHECK(marked(&d) == QStringLiteral("Two"),
			      "and it moves to whichever button has the focus, which "
			      "is Qt's autoDefault working here because this library "
			      "delivers the focus events a dead window does not");

			focus_on(&d, field);
			CHECK(marked(&d) == QStringLiteral("One"),
			      "and moves back to the first when the focus leaves the "
			      "buttons -- the state a form starts in, so this is the "
			      "row that decides what Enter does in practice");

			// THE SAME DIALOG WITH A CHOICE MADE. Practice 2's remedy is
			// worth only what it survives, so all three positions are
			// asked again rather than just the first.
			QDialog e;
			e.setAttribute(Qt::WA_DontShowOnScreen);
			e.resize(GridMetrics::cells(30, 8));
			auto *el = new QVBoxLayout(&e);
			auto *efield = new QLineEdit;
			auto *first = new QPushButton(QStringLiteral("First"));
			auto *chosen = new QPushButton(QStringLiteral("Marked"));
			el->addWidget(efield);
			el->addWidget(first);
			el->addWidget(chosen);
			chosen->setDefault(true);
			e.show();
			InputRouter er(&e);
			QCoreApplication::processEvents();
			CHECK(marked(&e) == QStringLiteral("Marked"),
			      "setDefault() beats construction order");
			focus_on(&e, first);
			CHECK(marked(&e) == QStringLiteral("First"),
			      "and is suspended, not overruled, while another button "
			      "holds the focus");
			focus_on(&e, efield);
			CHECK(marked(&e) == QStringLiteral("Marked"),
			      "coming back the moment the focus leaves that button, "
			      "which is why the guide says to write it");

			// THE OTHER SPELLING, and the one the destructive case wants.
			QDialog f;
			f.setAttribute(Qt::WA_DontShowOnScreen);
			f.resize(GridMetrics::cells(30, 8));
			auto *fl = new QVBoxLayout(&f);
			auto *danger = new QPushButton(QStringLiteral("Delete"));
			danger->setAutoDefault(false);
			fl->addWidget(danger);
			fl->addWidget(new QPushButton(QStringLiteral("Cancel")));
			f.show();
			QCoreApplication::processEvents();
			CHECK(marked(&f) == QStringLiteral("Cancel"),
			      "and setAutoDefault(false) takes a destructive first "
			      "button out of the running, which is the whole of the "
			      "remedy practice 2 gives");

			// AND QT'S OWN MESSAGE BOX, where the guide makes a claim
			// about a DIFFERENT property -- QMessageBox::defaultButton()
			// -- and the two must not be read as one. Both are asserted
			// in the same breath precisely because they disagree.
			QMessageBox mb(QMessageBox::Warning, QStringLiteral("t"),
			               QStringLiteral("m"),
			               QMessageBox::Ok | QMessageBox::Cancel);
			mb.setAttribute(Qt::WA_DontShowOnScreen);
			mb.show();
			QCoreApplication::processEvents();
			auto *ok = qobject_cast<QPushButton *>(mb.button(QMessageBox::Ok));
			CHECK(mb.defaultButton() == nullptr && ok && ok->isDefault(),
			      "a QMessageBox reports no defaultButton() of its own "
			      "while Qt has still marked its Ok as the default push "
			      "button, so the guide's two sentences are about two "
			      "properties and not one");
		}

		// A LINE EDIT'S CLEAR BUTTON, which is the report's own noise
		// rather than an application's fault. Qt adds it whenever
		// setClearButtonEnabled(true) is called, so naming it put a
		// finding in every field that has one -- and the remedy it asks
		// for already exists, which is the shape that gets a whole report
		// ignored rather than acted on.
		//
		// Three checks, and the third is the one that keeps the exclusion
		// honest: it pins the REASON. If a field ever stops being
		// clearable by key, the justification for skipping its button has
		// gone and this says so.
		{
			QWidget form;
			form.setAttribute(Qt::WA_DontShowOnScreen);
			form.resize(GridMetrics::cells(40, 6));
			auto *field = new QLineEdit(&form);
			field->setClearButtonEnabled(true);
			field->setText(QStringLiteral("typed text"));
			field->setGeometry(0, 0, 30 * cw, ch);
			QAction *reveal = field->addAction(QIcon(),
			                                   QLineEdit::LeadingPosition);
			reveal->setObjectName(QStringLiteral("reveal"));
			form.show();
			QCoreApplication::processEvents();
			QWidget *clear_button = nullptr, *reveal_button = nullptr;
			for (QToolButton *b : field->findChildren<QToolButton *>()) {
				bool is_clear = false;
				for (const QAction *a : b->actions())
					is_clear = is_clear
					    || a->objectName()
					       == QLatin1String("_q_qlineeditclearaction");
				if (is_clear) clear_button = b;
				else reveal_button = b;
			}
			const QVector<QWidget *> named = pointer_only(&form);
			CHECK(clear_button && !named.contains(clear_button),
			      "a line edit's own clear button is not reported, Qt "
			      "adding one to every field that asks and the action it "
			      "performs having a key already");
			CHECK(reveal_button && named.contains(reveal_button),
			      "while an icon action the APPLICATION put in the same "
			      "field still is, which is the case the report exists "
			      "for -- a reveal toggle no key reaches");

			// THE REASON, in both modes, because the readline bundle is
			// only one of the two routes and an application may run
			// without it.
			InputRouter cr(&form);
			const bool had_conv = keyboard_conventions();
			set_keyboard_conventions(true);
			field->setFocus();
			set_focus_widget(field);
			field->setText(QStringLiteral("typed text"));
			cr.on_key({Qt::Key_U, QString(), true, false, false});
			QCoreApplication::processEvents();
			const bool killed_by_readline = field->text().isEmpty();
			set_keyboard_conventions(false);
			field->setText(QStringLiteral("typed again"));
			cr.on_key({Qt::Key_A, QString(), true, false, false});
			cr.on_key({Qt::Key_Delete, QString(), false, false, false});
			QCoreApplication::processEvents();
			const bool cleared_by_selection = field->text().isEmpty();
			set_keyboard_conventions(had_conv);
			CHECK(killed_by_readline && cleared_by_selection,
			      "and the reason it is skipped holds both ways: Ctrl+U "
			      "empties the field with the conventions on, Ctrl+A then "
			      "Delete with them off");
			GridGuard::reset();
		}

		// THE NARROW TERMINAL, which is this library's ordinary condition
		// rather than an edge: a toolbar with more actions than fit hides
		// the surplus behind a chevron only a pointer can open, and the
		// guide says so. The report agrees with the guide, and -- widened
		// -- goes quiet with eight buttons on the bar, every one of them
		// keyed by the letter on its own action. A report that named them
		// is one nobody would read twice.
		{
			QMainWindow win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.setCentralWidget(new QLineEdit);
			auto *bar = win.addToolBar(QStringLiteral("Main"));
			for (const char *t : { "&Open", "&Save", "&Print", "&Quit",
				                   "&Find", "&Replace", "&Zoom", "&Help" })
				bar->addAction(QString::fromLatin1(t));
			win.resize(GridMetrics::cells(60, 10));
			win.show();
			win.resize(GridMetrics::cells(8, 10));
			QCoreApplication::processEvents();

			const QVector<QWidget *> tight = pointer_only(&win);
			bool chevron = false;
			for (QWidget *w : tight)
				if (w->objectName()
				    == QStringLiteral("qt_toolbar_ext_button"))
					chevron = true;
			CHECK(chevron,
			      "a toolbar too narrow for its actions puts the chevron "
			      "in the report, which is the one thing the overflow "
			      "leaves that only a pointer opens");

			win.resize(GridMetrics::cells(60, 10));
			QCoreApplication::processEvents();
			const QVector<QWidget *> roomy = pointer_only(&win);
			printf("info: toolbar pointer-only, narrow %d, wide %d\n",
			       int(tight.size()), int(roomy.size()));
			CHECK(roomy.isEmpty(),
			      "and with room for all eight the report is empty, each "
			      "button keyed by the letter on its own action");
		}

		// THE THREE WAYS THIS CAN NAME A CONTROL A KEY DOES REACH, which
		// is the error direction that matters: a false name sends
		// somebody to fix the control that works. Two are subtracted --
		// the action behind a toolbar button, above, and a dialog's
		// default button here -- and the third cannot be, so it is
		// pinned instead.
		{
			QDialog dlg;
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			auto *dv = new QVBoxLayout(&dlg);
			auto *field = new QLineEdit;
			dv->addWidget(field);
			int accepted = 0;
			auto *ok = new QPushButton(QStringLiteral("OK"));
			ok->setDefault(true);
			ok->setFocusPolicy(Qt::NoFocus);
			QObject::connect(ok, &QPushButton::clicked, [&] { ++accepted; });
			dv->addWidget(ok);
			int wired = 0;
			auto *shortcut_only = new QPushButton(QStringLiteral("Wired"));
			shortcut_only->setFocusPolicy(Qt::NoFocus);
			QObject::connect(shortcut_only, &QPushButton::clicked,
			                 [&] { ++wired; });
			dv->addWidget(shortcut_only);
			auto *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")),
			                         &dlg);
			QObject::connect(sc, &QShortcut::activated, shortcut_only,
			                 &QPushButton::click);
			dlg.resize(GridMetrics::cells(30, 8));
			dlg.show();
			QCoreApplication::processEvents();

			const QVector<QWidget *> named = pointer_only(&dlg);
			CHECK(!named.contains(ok),
			      "a dialog's default button is not named, Enter reaching "
			      "it without its ever holding the focus");

			InputRouter dr(&dlg);
			field->setFocus();
			set_focus_widget(dlg.focusWidget());
			QCoreApplication::processEvents();
			dr.on_key({Qt::Key_Return, QStringLiteral("\r"), false, false,
				       false});
			QCoreApplication::processEvents();
			CHECK(accepted == 1,
			      "and Enter in the field really does fire it, which is "
			      "what makes leaving it out of the report right");

			// THE LIMIT, pinned rather than fixed. A QShortcut claims a
			// key and says nothing about what it activates; Qt publishes
			// no way to read a connection's other end. So this button is
			// named though Ctrl+K clicks it -- asserted in both halves,
			// because a check on the report alone would pass if the key
			// stopped working too.
			bool shortcut_named = named.contains(shortcut_only);
			dr.on_key({Qt::Key_K, QStringLiteral("k"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(shortcut_named && wired == 1,
			      "while a button reached only through a QShortcut's "
			      "connection IS named, the one route nothing in Qt can "
			      "be asked about");

			// And the population is what is INSIDE the scope: asking
			// about a button answers about its children, not about it.
			CHECK(pointer_only(shortcut_only).isEmpty(),
			      "asking about a button itself names nothing, the scope "
			      "being what the walk looks inside of");
		}

		// ---- practice 3, which had nothing to check it. Qt's tab order
		// is construction order, and that stops being the reading order
		// the moment somebody inserts a widget into a layout -- silently,
		// since nothing about that edit looks like it touched the
		// keyboard. On a terminal it is the only order a user has.
		{
			QWidget form;
			form.setAttribute(Qt::WA_DontShowOnScreen);
			form.resize(GridMetrics::cells(30, 8));
			auto *fl = new QFormLayout(&form);
			auto *host = new QLineEdit;
			auto *user = new QLineEdit;
			fl->addRow(QStringLiteral("Host"), host);
			fl->addRow(QStringLiteral("User"), user);
			form.show();
			QCoreApplication::processEvents();
			CHECK(tab_order_anomalies(&form).isEmpty(),
			      "a form built in the order it reads has no tab-order "
			      "anomaly, which is the answer a test asserts");

			// The edit nobody notices: a row inserted into the layout by
			// a widget constructed after the ones below it.
			auto *port = new QLineEdit;
			fl->insertRow(1, QStringLiteral("Port"), port);
			QCoreApplication::processEvents();
			const auto bad = tab_order_anomalies(&form);
			CHECK(bad.size() == 1 && bad[0].first == user
			          && bad[0].second == port,
			      "and a row inserted above the one built before it is "
			      "reported as the pair it is, which is the only way an "
			      "application learns its form now reads backwards");

			// THE RELATIONSHIP: Tab really does go that way. The report
			// reads keyboard_reachable(), and the suite already proves
			// that list is what Tab visits -- but the pair is the claim
			// here, so the pair is what gets pressed.
			InputRouter tr(&form);
			user->setFocus();
			set_focus_widget(form.focusWidget());
			QCoreApplication::processEvents();
			tr.on_key({Qt::Key_Tab, QStringLiteral("\t"), false, false,
				       false});
			QCoreApplication::processEvents();
			CHECK(form.focusWidget() == port,
			      "and Tab from the first of the pair really does land on "
			      "the second, a row above it");

			// A SIDE-BY-SIDE layout is walked down one column and up to
			// the top of the next, and that is not an anomaly. Qt's own
			// QFontDialog does it, and it was the only false report in
			// the corpus this rule was measured against.
			//
			// ONE PARENT, and that is the whole fixture. The first
			// version put each column in its own QGroupBox, so the jump
			// between columns crossed a parent boundary and was skipped
			// before the exception was ever consulted -- the check passed
			// because of a different rule than the one it names, and the
			// sabotage entry for the exception said so: the named check
			// PASSED against broken code.
			QWidget panels;
			panels.setAttribute(Qt::WA_DontShowOnScreen);
			panels.resize(GridMetrics::cells(40, 8));
			QVector<QLineEdit *> column;
			for (int i = 0; i < 6; ++i) {
				auto *e = new QLineEdit(&panels);
				e->setGeometry((i / 3) * 18 * GridMetrics::cw(),
				               (i % 3) * GridMetrics::ch(),
				               16 * GridMetrics::cw(), GridMetrics::ch());
				column.append(e);
			}
			for (int i = 0; i + 1 < column.size(); ++i)
				QWidget::setTabOrder(column[i], column[i + 1]);
			panels.show();
			QCoreApplication::processEvents();
			CHECK(tab_order_anomalies(&panels).isEmpty(),
			      "while going up and to the right is a new column rather "
			      "than a fault, which is how a side-by-side layout is "
			      "meant to be walked");

			// AND THE ROWS ARE CELLS, not pixels. Two fields a person
			// sees on one row can sit a few pixels apart, and a pixel
			// comparison then reads the right-hand one as LOWER -- so a
			// walk that goes right to left along a row, which is
			// backwards to the eye, measures as forwards and is not
			// reported at all.
			//
			// WITHOUT GridSnap, and that is the only way to ask. The
			// snapper rounds every geometry to whole cells, so under it
			// pixels and rows agree by construction and the distinction
			// cannot be observed -- measured: the entry that judges the
			// order in pixels left this check green until the snapper
			// was taken out of the fixture. Put back below, because
			// every case after this one runs under the library as an
			// application gets it.
			GridSnap::remove();
			QWidget row;
			row.setAttribute(Qt::WA_DontShowOnScreen);
			row.resize(GridMetrics::cells(40, 4));
			auto *right = new QLineEdit(&row);
			right->setGeometry(20 * GridMetrics::cw(), 0,
			                   10 * GridMetrics::cw(), GridMetrics::ch());
			auto *left = new QLineEdit(&row);
			left->setGeometry(2 * GridMetrics::cw(), GridMetrics::ch() / 3,
			                  10 * GridMetrics::cw(), GridMetrics::ch());
			QWidget::setTabOrder(right, left);
			row.show();
			QCoreApplication::processEvents();
			const auto sideways = tab_order_anomalies(&row);
			CHECK(sideways.size() == 1 && sideways[0].first == right
			          && sideways[0].second == left,
			      "and a walk right to left along one row is reported, the "
			      "row being a cell row rather than a pixel -- the two "
			      "fields are a few pixels apart and a person sees one "
			      "line");
			GridSnap::install(*qApp);
		}

		// ---- practice 7, from the side an application can check. A tool
		// tip is never shown here -- no QEvent::ToolTip is raised, even
		// after its timer -- so a sentence that lives only in one is a
		// sentence a terminal user cannot reach. A status tip can be
		// reached: with the conventions on it follows focus.
		{
			QMainWindow win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			auto *central = new QWidget(&win);
			win.setCentralWidget(central);
			auto *host = new QLineEdit(central);
			host->setGeometry(0, 0, 20 * GridMetrics::cw(), GridMetrics::ch());
			host->setToolTip(QStringLiteral("host:port, or a bare host"));
			auto *user = new QLineEdit(central);
			user->setGeometry(0, GridMetrics::ch(), 20 * GridMetrics::cw(),
			                  GridMetrics::ch());
			user->setToolTip(QStringLiteral("the user to log in as"));
			user->setStatusTip(QStringLiteral("the user to log in as"));
			// An icon-only button, whose tip this style draws as its label
			// (8.8). It hides nothing, and without the exclusion every
			// file dialog would report six of these.
			auto *icon = new QToolButton(central);
			icon->setGeometry(0, 2 * GridMetrics::ch(), 4 * GridMetrics::cw(),
			                  GridMetrics::ch());
			icon->setToolTip(QStringLiteral("Parent Directory"));
			win.resize(GridMetrics::cells(40, 10));
			win.show();
			QCoreApplication::processEvents();

			const QVector<QWidget *> hidden = hover_only(&win);
			CHECK(hidden.contains(host),
			      "a field whose format lives only in a tool tip is named, "
			      "there being no way to raise one here at all");
			CHECK(!hidden.contains(user),
			      "and one that says the same thing in a status tip is "
			      "not, that being the tip a keyboard can reach");
			CHECK(!hidden.contains(icon),
			      "nor an icon-only button, whose tool tip this style "
			      "draws as its label rather than hiding");

			// THE RELATIONSHIP, and it is the reason the report exists:
			// the status tip really does arrive on focus, and the tool
			// tip really does not arrive at all.
			// The state is RESTORED rather than set to off, because this
			// block sits inside a section that runs with the conventions
			// on: forcing them off here took Ctrl+PageUp/PageDown and a
			// tab's own letter away from three checks below, which is a
			// fixture reaching outside itself.
			const bool was_on = keyboard_conventions();
			set_keyboard_conventions(true);
			win.statusBar()->showMessage(QString());
			user->setFocus();
			set_focus_widget(win.focusWidget());
			QCoreApplication::processEvents();
			const QString shown = win.statusBar()->currentMessage();
			host->setFocus();
			set_focus_widget(win.focusWidget());
			QCoreApplication::processEvents();
			const QString none = win.statusBar()->currentMessage();
			set_keyboard_conventions(was_on);
			CHECK(shown == QStringLiteral("the user to log in as")
			          && none.isEmpty(),
			      "and the difference is what a user sees: the status tip "
			      "reaches the status bar on focus and the tool tip "
			      "reaches nothing");
		}

		// ---- practice 10, which is a trap rather than advice: hasFocus()
		// is permanently false here, so a custom widget that asks Qt
		// whether it has the focus draws nothing -- on the terminal only,
		// with no error anywhere. Every widget Qt ships passes this; the
		// ones that fail are the ones somebody wrote.
		{
			// Local classes, one asking each way. Neither needs moc: a
			// paintEvent override is a virtual, not a signal.
			struct Naive : QWidget {
				void paintEvent(QPaintEvent *) override {
					QPainter p(this);
					p.drawText(rect(), Qt::AlignLeft,
					           hasFocus() ? QStringLiteral("[me]")
					                      : QStringLiteral(" me "));
				}
			};
			struct Correct : QWidget {
				void paintEvent(QPaintEvent *) override {
					QPainter p(this);
					p.drawText(rect(), Qt::AlignLeft,
					           Qtty::has_focus(this)
					               ? QStringLiteral("[ok]")
					               : QStringLiteral(" ok "));
				}
			};
			// A custom widget that EDITS TEXT and draws no mark of its
			// own, which is the case the exclusion exists for. Qt's own
			// QLineEdit does not need it -- measured, its rendering
			// changes on focus anyway -- so an entry that removes the
			// exclusion left the line edit's check green and said so.
			struct Editor : QWidget {
				void paintEvent(QPaintEvent *) override {
					QPainter p(this);
					p.drawText(rect(), Qt::AlignLeft, QStringLiteral("text"));
				}
				QVariant inputMethodQuery(Qt::InputMethodQuery q) const override {
					if (q == Qt::ImCursorRectangle)
						return QRect(0, 0, 1, GridMetrics::ch());
					return QWidget::inputMethodQuery(q);
				}
			};

			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			auto *lay = new QVBoxLayout(&host);
			auto *button = new QPushButton(QStringLiteral("Push"));
			lay->addWidget(button);
			auto *field = new QLineEdit;
			lay->addWidget(field);
			auto *naive = new Naive;
			naive->setFocusPolicy(Qt::StrongFocus);
			naive->setMinimumHeight(2 * GridMetrics::ch());
			lay->addWidget(naive);
			auto *correct = new Correct;
			correct->setFocusPolicy(Qt::StrongFocus);
			correct->setMinimumHeight(2 * GridMetrics::ch());
			lay->addWidget(correct);
			auto *editor = new Editor;
			editor->setFocusPolicy(Qt::StrongFocus);
			editor->setAttribute(Qt::WA_InputMethodEnabled);
			editor->setMinimumHeight(GridMetrics::ch());
			lay->addWidget(editor);
			host.resize(GridMetrics::cells(30, 10));
			host.show();
			QCoreApplication::processEvents();

			const QVector<QWidget *> blind = focus_invisible(&host);
			CHECK(blind.contains(naive),
			      "a custom widget that asks hasFocus() is named, since "
			      "that answer is permanently false here and its focus "
			      "mark therefore never draws");
			CHECK(!blind.contains(correct),
			      "and one that asks Qtty::has_focus() is not, which is "
			      "the whole of practice 10");
			CHECK(!blind.contains(button) && !blind.contains(field),
			      "nor Qt's own controls: the button draws a mark and the "
			      "field shows focus with the terminal's cursor, which is "
			      "why a widget that takes text is not asked");

			CHECK(!blind.contains(editor),
			      "nor a custom widget that edits text and draws nothing "
			      "new, since the terminal's cursor is what shows its "
			      "focus -- the case practice 11 is about");

			// AND THE CURSOR REALLY GOES THERE, which is what makes that
			// exclusion right rather than merely convenient.
			//
			// The DIFFERENCE between two focuses rather than an absolute
			// row: the compositor composes a screen, and a window may sit
			// below a strip, so the cursor's row and a widget's row inside
			// its window are not in the same coordinates -- measured, an
			// editor at window row 7 put the cursor at screen row 8. The
			// difference cancels whatever the offset is and says the
			// stronger thing anyway, that the cursor FOLLOWS the focus.
			{
				InputRouter fr(&host);
				Compositor fc(&host, &fr);
				auto cursor_row = [&](QWidget *on) {
					on->setFocus();
					set_focus_widget(host.focusWidget());
					QCoreApplication::processEvents();
					CellBuffer fb(30, 12);
					fc.compose(fb);
					return fc.cursor_cell() ? fc.cursor_cell()->y() : -1;
				};
				const int at_field = cursor_row(field);
				const int at_editor = cursor_row(editor);
				const int rows_apart =
				    (editor->mapTo(&host, QPoint()).y()
				     - field->mapTo(&host, QPoint()).y()) / GridMetrics::ch();
				CHECK(at_field >= 0 && at_editor - at_field == rows_apart,
				      "the cursor follows the focus onto the custom text "
				      "widget, exactly as many rows down as the widget is, "
				      "which is the mark it draws instead of one of its "
				      "own");
			}

			// THE RELATIONSHIP, read off the screen rather than off the
			// report: the correct widget's cells change when it takes
			// focus and the naive one's do not.
			auto shot = [&](QWidget *on) {
				on->setFocus();
				set_focus_widget(host.focusWidget());
				QCoreApplication::processEvents();
				CellBuffer b(30, 10);
				render_once(host, b);
				return b.to_text();
			};
			const QString on_button = shot(button);
			const QString on_naive = shot(naive);
			const QString on_correct = shot(correct);
			CHECK(on_correct.contains(QStringLiteral("[ok]"))
			          && !on_button.contains(QStringLiteral("[ok]")),
			      "and the screen agrees: the widget that asks the library "
			      "draws its mark when focused and not otherwise");
			CHECK(!on_naive.contains(QStringLiteral("[me]")),
			      "while the one that asks Qt draws the same thing focused "
			      "as unfocused, which is the fault itself rather than the "
			      "report of it");
		}

		// ---- the eight questions as a POPULATION, rather than one at a
		// time. Each is asked about nothing at all and about a window with
		// nothing in it, and each has to answer empty rather than refuse,
		// crash, or invent a finding. The list is written out so that a
		// ninth question added without a line here is a ninth question
		// nobody asked this of -- which is the honest version of a rule
		// that cannot enumerate itself.
		{
			QWidget bare;
			bare.setAttribute(Qt::WA_DontShowOnScreen);
			bare.resize(GridMetrics::cells(20, 6));
			bare.show();
			QCoreApplication::processEvents();

			const bool null_quiet =
			    keyboard_reachable(nullptr).isEmpty()
			    && pointer_only(nullptr).isEmpty()
			    && mnemonic_conflicts(nullptr).isEmpty()
			    && shortcut_conflicts(nullptr).isEmpty()
			    && conventions_shadowed(nullptr).isEmpty()
			    && tab_order_anomalies(nullptr).isEmpty()
			    && hover_only(nullptr).isEmpty()
			    && focus_invisible(nullptr).isEmpty()
			    && sheet_styled(nullptr).isEmpty()
			    && ambiguous_chords(nullptr).isEmpty()
			    && mnemonic_missing(nullptr).isEmpty();
			CHECK(null_quiet,
			      "all eleven questions answer empty when asked about "
			      "nothing, rather than refusing or reaching through a "
			      "null scope");

			const bool bare_quiet =
			    keyboard_reachable(&bare).isEmpty()
			    && pointer_only(&bare).isEmpty()
			    && mnemonic_conflicts(&bare).isEmpty()
			    && shortcut_conflicts(&bare).isEmpty()
			    && conventions_shadowed(&bare).isEmpty()
			    && tab_order_anomalies(&bare).isEmpty()
			    && hover_only(&bare).isEmpty()
			    && focus_invisible(&bare).isEmpty()
			    && sheet_styled(&bare).isEmpty()
			    && ambiguous_chords(&bare).isEmpty()
			    && mnemonic_missing(&bare).isEmpty();
			CHECK(bare_quiet,
			      "and empty about a window with nothing in it, which is "
			      "the answer a report invents a finding to avoid giving");

			// THE KEY HELPERS, and the controls are the point of them. A
			// KeyEvent carries a key, a text and three flags, and which of
			// them decides is different per keystroke -- so every wrong
			// combination is a keystroke that arrives and does nothing, with
			// nothing said. Each helper is checked against the hand-built
			// form that FAILS, because "press moves the focus" alone would
			// pass for a library where the spelling did not matter.
			{
				const auto form = [](QWidget &host, QLineEdit *&a, QLineEdit *&b) {
					host.setAttribute(Qt::WA_DontShowOnScreen);
					host.resize(GridMetrics::cells(30, 6));
					auto *f = new QFormLayout(&host);
					a = new QLineEdit;
					b = new QLineEdit;
					f->addRow(QStringLiteral("&Name"), a);
					f->addRow(QStringLiteral("&Host"), b);
					host.show();
				};
				{
					QWidget host;
					QLineEdit *a = nullptr, *b = nullptr;
					form(host, a, b);
					InputRouter r(&host);
					QCoreApplication::processEvents();
					a->setFocus();
					QCoreApplication::processEvents();
					Qtty::test::press(r, Qt::Key_Tab);
					QCoreApplication::processEvents();
					CHECK(host.focusWidget() == b,
					      "test::press sends a named key, which is the one the "
					      "router reads from the key code");
				}
				{
					QWidget host;
					QLineEdit *a = nullptr, *b = nullptr;
					form(host, a, b);
					InputRouter r(&host);
					QCoreApplication::processEvents();
					a->setFocus();
					QCoreApplication::processEvents();
					Qtty::test::type(r, QStringLiteral("hi"));
					QCoreApplication::processEvents();
					CHECK(a->text() == QStringLiteral("hi"),
					      "and test::type puts characters in, which the router "
					      "reads from the text");
				}
				{
					QWidget host;
					QLineEdit *a = nullptr, *b = nullptr;
					form(host, a, b);
					InputRouter r(&host);
					QCoreApplication::processEvents();
					a->setFocus();
					QCoreApplication::processEvents();
					Qtty::test::mnemonic(r, QLatin1Char('h'));
					QCoreApplication::processEvents();
					CHECK(host.focusWidget() == b,
					      "and test::mnemonic reaches the field its label "
					      "names");
				}
				// THE CONTROLS: the spellings a reader would reasonably write
				// by hand, each of which does NOTHING. Without these the three
				// above pass for a library in which the fields did not matter,
				// and the helpers would be sugar rather than a guard.
				{
					QWidget host;
					QLineEdit *a = nullptr, *b = nullptr;
					form(host, a, b);
					InputRouter r(&host);
					QCoreApplication::processEvents();
					a->setFocus();
					QCoreApplication::processEvents();
					r.on_key({Qt::Key_H, QString(), false, true, false});
					r.on_key({Qt::Key_Z, QString(), false, false, false});
					r.on_key({0, QStringLiteral("\t"), false, false, false});
					QCoreApplication::processEvents();
					CHECK(host.focusWidget() == a && a->text().isEmpty(),
					      "while a mnemonic with no letter, a character with no "
					      "text and a tab with no key code all arrive and do "
					      "nothing, which is why the helpers exist");
				}
				// And a printable key through press(), which fills the text
				// so that the obvious call does the obvious thing.
				{
					QWidget host;
					QLineEdit *a = nullptr, *b = nullptr;
					form(host, a, b);
					InputRouter r(&host);
					QCoreApplication::processEvents();
					a->setFocus();
					QCoreApplication::processEvents();
					Qtty::test::press(r, Qt::Key_Z);
					Qtty::test::press(r, Qt::Key_Z, false, false, true);
					QCoreApplication::processEvents();
					CHECK(a->text() == QStringLiteral("zZ"),
					      "and press fills a printable key's text, shift and "
					      "all, so it types rather than doing nothing");
				}
			}

			// FOCUS HERE IS THE ROUTER'S, and QWidget::setFocus() does not
			// move it. That is not a subtlety of the implementation, it is
			// the first thing a test author does and it silently produces
			// a window with no focus mark anywhere -- measured twice in
			// one session, once with clearFocus() and once with setFocus(),
			// both times reading as an inert feature when the fixture was
			// the thing at fault.
			//
			// focusWidget() answers either way, which is what makes it
			// convincing: the widget really does hold Qt's focus. What it
			// does not hold is the record this library draws the mark
			// from.
			{
				const auto marks = [](QWidget &w, int cols, int rows) {
					CellBuffer b(cols, rows);
					render_once(w, b);
					int n = 0;
					for (int y = 0; y < rows; ++y)
						for (int x = 0; x < cols; ++x)
							if (b.at(x, y).attrs & Attr::Reverse) ++n;
					return n;
				};
				QWidget host;
				host.setAttribute(Qt::WA_DontShowOnScreen);
				host.resize(GridMetrics::cells(24, 4));
				auto *v = new QVBoxLayout(&host);
				auto *b = new QPushButton(QStringLiteral("Save"));
				v->addWidget(b);
				v->addWidget(new QCheckBox(QStringLiteral("Wrap")));
				host.show();
				InputRouter r(&host);
				QCoreApplication::processEvents();

				b->setFocus();
				QCoreApplication::processEvents();
				const int by_setfocus = marks(host, 24, 4);
				QWidget *const after_setfocus = host.focusWidget();
				// Tab ADVANCES from wherever Qt's focus sits, so the mark it
				// draws is on the NEXT stop rather than on the button --
				// asserting it landed on the button is what the first
				// version of this check got wrong.
				Qtty::test::press(r, Qt::Key_Tab);
				QCoreApplication::processEvents();
				const int by_tab = marks(host, 24, 4);
				CHECK(by_setfocus == 0 && after_setfocus == b && by_tab > 0,
				      "setFocus() leaves no focus mark and Tab draws one, "
				      "focus here being the router's -- and focusWidget() "
				      "answers after setFocus() either way, which is what "
				      "makes it convincing");

				// AND THE TERMINAL'S OWN FOCUS takes the mark away, which is
				// what qtty-replay's `focus off` now reproduces.
				r.on_focus_change(false);
				QCoreApplication::processEvents();
				const int unfocused = marks(host, 24, 4);
				r.on_focus_change(true);
				QCoreApplication::processEvents();
				CHECK(unfocused == 0 && marks(host, 24, 4) == by_tab,
				      "and a terminal that loses focus withholds the mark, "
				      "giving it back when it returns");
				GridGuard::reset();
			}

			// THE MOUSE HELPERS, which carry the same kind of trap one
			// field along. A MouseEvent is built by aggregate
			// initialisation at almost every call site here, and three
			// things about it are silent when wrong: button 0 means NO
			// button and reaches nothing, a press with no release is not a
			// click, and the wheel's sign is a guess unless it is measured.
			{
				QWidget host;
				host.setAttribute(Qt::WA_DontShowOnScreen);
				host.resize(GridMetrics::cells(20, 4));
				auto *b = new QPushButton(QStringLiteral("Save"), &host);
				b->setGeometry(0, 0, cw * 10, ch);
				int clicks = 0;
				QObject::connect(b, &QPushButton::clicked,
				                 [&clicks] { ++clicks; });
				auto *bar = new QScrollBar(Qt::Vertical, &host);
				bar->setGeometry(cw * 12, 0, cw, ch * 4);
				bar->setRange(0, 100);
				bar->setValue(50);
				host.show();
				InputRouter r(&host);
				QCoreApplication::processEvents();

				Qtty::test::click(r, QPoint(2, 0));
				QCoreApplication::processEvents();
				CHECK(clicks == 1,
				      "test::click presses and releases, which is what a "
				      "control needs to see a click");

				const int was = bar->value();
				Qtty::test::wheel(r, QPoint(12, 1), 1);
				QCoreApplication::processEvents();
				const int after_up = bar->value();
				Qtty::test::wheel(r, QPoint(12, 1), -1);
				QCoreApplication::processEvents();
				CHECK(after_up < was && bar->value() == was,
				      "and test::wheel scrolls up for a positive count and "
				      "back down for a negative one");

				// A MOVE IS NOT A SCROLL, which is the bug the first draft
				// of mouse_move() had: it passed the button into the sixth
				// positional field, which is the wheel, so a drag move
				// scrolled whatever was under it.
				const int before_move = bar->value();
				Qtty::test::mouse_move(r, QPoint(12, 1), 1);
				QCoreApplication::processEvents();
				CHECK(bar->value() == before_move,
				      "while a move with a button held is a drag and not a "
				      "scroll, whatever it passes over");

				// THE CONTROLS: the two spellings a reader writes by hand
				// that reach nothing at all.
				clicks = 0;
				r.on_mouse({QPoint(2, 0), 0, true, false, false, 0});
				r.on_mouse({QPoint(2, 0), 0, false, true, false, 0});
				r.on_mouse({QPoint(2, 0), 1, true, false, false, 0});
				QCoreApplication::processEvents();
				CHECK(clicks == 0,
				      "while a click with button 0 and a press with no "
				      "release both reach nothing, which is why the helpers "
				      "exist");
				Qtty::test::mouse_release(r, QPoint(2, 0));
				QCoreApplication::processEvents();

				// A DRAG THROUGH ALL THREE, which is what mouse_press()
				// exists for and what nothing called. A sweep for public
				// functions the suite never calls found it: five names,
				// four of them library functions with a production caller
				// and this one a helper shipped yesterday with no caller
				// anywhere. An interface is only as wired as its least-used
				// method, and a press that set `release` instead would have
				// gone unnoticed.
				QWidget dhost;
				dhost.setAttribute(Qt::WA_DontShowOnScreen);
				dhost.resize(GridMetrics::cells(8, 8));
				auto *dbar = new QScrollBar(Qt::Vertical, &dhost);
				dbar->setGeometry(0, 0, cw, ch * 8);
				dbar->setRange(0, 100);
				dbar->setValue(50);
				dhost.show();
				InputRouter dr(&dhost);
				QCoreApplication::processEvents();
				CellBuffer db(8, 8);
				render_once(dhost, db);
				int thumb = -1;
				for (int y = 0; y < 8 && thumb < 0; ++y)
					if (db.at(0, y).ch == QString(QChar(0x2588))) thumb = y;

				// THE CONTROL FIRST: a move with no press before it is a
				// hover, and moves nothing. Without it "the value changed"
				// would pass for a library in which the press did nothing
				// and the move did all the work.
				Qtty::test::mouse_move(dr, QPoint(0, thumb + 2), 1);
				QCoreApplication::processEvents();
				const int after_bare_move = dbar->value();

				Qtty::test::mouse_press(dr, QPoint(0, thumb));
				QCoreApplication::processEvents();
				const int after_press = dbar->value();
				Qtty::test::mouse_move(dr, QPoint(0, thumb + 2), 1);
				QCoreApplication::processEvents();
				const int after_move = dbar->value();
				Qtty::test::mouse_release(dr, QPoint(0, thumb + 2));
				QCoreApplication::processEvents();
				CHECK(thumb >= 0 && after_bare_move == 50 && after_press == 50
				      && after_move > 50 && dbar->value() == after_move,
				      "and a press, a move and a release drag a scroll bar's "
				      "thumb, where a move on its own is a hover and shifts "
				      "nothing");
				GridGuard::reset();
				// This fixture's host is off the grid for one event -- a
				// QWidget carries a default 300x214 until it is resized,
				// and the guard sees it. Disowned deliberately, which is
				// what reset() is for, and not left to print into the
				// middle of the next PASS line.
				GridGuard::reset();
			}

			// AND THE PAGE THAT PUBLISHES THEM NAMES THE SAME NUMBER.
			// The two checks above say "all ten", which is a claim
			// about a population rather than about a call -- and
			// `doc/keyboard-first.md` says "Eleven questions the library
			// will answer" over a table of them. A twelfth helper added
			// with no row, or a row with no call, leaves both sentences
			// quietly wrong; the quantifier is the thing to verify.
			//
			// Same coupling as the vocabulary table in suite_widgets,
			// and for the same reason: a count in prose about the tree's
			// own shape is the kind that rots.
			int listed = 0;
			QFile page(QStringLiteral(QTTY_SOURCE_DIR)
			           + QStringLiteral("/doc/keyboard-first.md"));
			if (page.open(QIODevice::ReadOnly | QIODevice::Text)) {
				const QStringList lines =
				    QString::fromUtf8(page.readAll()).split(QLatin1Char('\n'));
				for (const QString &line : lines)
					if (line.startsWith(QStringLiteral("| `Qtty::"))) ++listed;
			}
			printf("info: the page lists %d audit question(s); these checks "
			       "call 11\n", listed);
			CHECK(listed == 11,
			      "the page lists exactly the audit questions these checks "
			      "call, so an eleventh cannot be added to either alone");
		}

		// THE WIDGETS A STYLE SHEET IS DRAWING, which is the ninth
		// question and the one practice 12 asks. A sheet takes drawing
		// over from the application style, and qtty's cell drawing IS
		// the application style, so a styled push button keeps its
		// behaviour and loses every sign that it is a control.
		{
			QWidget win;
			auto *v = new QVBoxLayout(&win);
			auto *save = new QPushButton(QStringLiteral("Save"));
			auto *box = new QGroupBox(QStringLiteral("Box"));
			auto *inner = new QLineEdit;
			auto *bv = new QVBoxLayout(box);
			bv->addWidget(inner);
			v->addWidget(save);
			v->addWidget(box);
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(24, 8));
			win.show();
			QCoreApplication::processEvents();
			CHECK(sheet_styled(&win).isEmpty(),
			      "a window with no style sheet anywhere in it reports "
			      "none, which is the answer to assert");

			save->setStyleSheet(QStringLiteral("color: red"));
			QCoreApplication::processEvents();
			CHECK(sheet_styled(&win) == QVector<QWidget *>{save},
			      "a sheet on one control names that control and nothing "
			      "else");

			// THE CASCADE, which is why this asks style() rather than
			// styleSheet(): the line edit carries no sheet of its own
			// and is drawn by the box's.
			save->setStyleSheet(QString());
			box->setStyleSheet(QStringLiteral("background: blue"));
			QCoreApplication::processEvents();
			const QVector<QWidget *> cascaded = sheet_styled(&win);
			CHECK(cascaded.contains(inner) && inner->styleSheet().isEmpty(),
			      "and a sheet on a container names the controls inside "
			      "it, which carry no sheet of their own");

			// AND THE APPLICATION-WIDE ONE, which reaches the window
			// itself and is the case that used to crash.
			box->setStyleSheet(QString());
			QCoreApplication::processEvents();
			qApp->setStyleSheet(QStringLiteral("QPushButton { padding: 2px }"));
			QCoreApplication::processEvents();
			CHECK(sheet_styled(&win).contains(save),
			      "and an application-wide sheet names them too, however "
			      "far from the widget it was set");
			// Cleared, or every later check in this suite renders through
			// it -- and the empty answer afterwards is the control that
			// says the reports above were the sheet rather than something
			// permanent about these widgets.
			qApp->setStyleSheet(QString());
			QCoreApplication::processEvents();
			CHECK(sheet_styled(&win).isEmpty(),
			      "while clearing every sheet empties the list again");
		}

		// Ctrl+PageUp and Ctrl+PageDown between tabs. Qt gives a
		// QTabWidget Ctrl+Tab and Ctrl+Shift+Tab and not these, and these
		// are what somebody coming from a browser or an editor tries.
		{
			auto *tabs = new QTabWidget;
			for (const char *t : { "One", "Two", "Three" }) {
				auto *page = new QWidget;
				auto *pv = new QVBoxLayout(page);
				pv->addWidget(new QLineEdit(QString::fromLatin1(t)));
				tabs->addTab(page, QString::fromLatin1(t));
			}
			v->addWidget(tabs);
			QCoreApplication::processEvents();
			tabs->setCurrentIndex(0);
			// Focus INSIDE a page, which is where a user is once they have
			// started typing, and the case a check that focused the tab
			// bar would not reach.
			tabs->currentWidget()->findChild<QLineEdit *>()->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_PageDown, QString(), true, false, false});
			QCoreApplication::processEvents();
			const int forward = tabs->currentIndex();
			r.on_key({Qt::Key_PageUp, QString(), true, false, false});
			QCoreApplication::processEvents();
			CHECK(forward == 1 && tabs->currentIndex() == 0,
			      "Ctrl+PageDown and Ctrl+PageUp step through the tabs the "
			      "focused widget is inside");
			// And it wraps, because a person holding the key expects to
			// come round rather than stop at an end with no signal.
			r.on_key({Qt::Key_PageUp, QString(), true, false, false});
			QCoreApplication::processEvents();
			CHECK(tabs->currentIndex() == 2,
			      "and stepping back from the first tab wraps to the last");

			// A TAB's own letter, which 8.37 recorded as doing nothing.
			// Under the opt-in only: 0b still holds whether a terminal
			// should switch tabs this way by default, and an application
			// that asked for the conventions has answered it for itself.
			tabs->setTabText(1, QStringLiteral("&Two"));
			tabs->setCurrentIndex(0);
			tabs->currentWidget()->findChild<QLineEdit *>()->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_T, QStringLiteral("t"), false, true, false});
			QCoreApplication::processEvents();
			CHECK(tabs->currentIndex() == 1,
			      "and a tab's own letter reaches it, so a page is one "
			      "keystroke away rather than a count of steps");

			// CTRL+TAB, which is Qt's and not qtty's -- and which
			// nothing pinned. The suite asserted only that Ctrl+Tab does
			// NOT cycle focus, a lone negative that passes just as well
			// if the key never arrived. This is its positive control:
			// the carve-out exists so Qt can act on the key, so the
			// thing to assert is that Qt did.
			tabs->setCurrentIndex(0);
			tabs->currentWidget()->findChild<QLineEdit *>()->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_Tab, QStringLiteral("\t"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(tabs->currentIndex() == 1,
			      "Ctrl+Tab switches tabs from inside a page, the carve-out "
			      "leaving the key for Qt rather than spending it on focus");

			// Arrows on a FOCUSED TAB BAR, the guide's other unverified
			// "Qt's" row. It holds, and now says so.
			tabs->setCurrentIndex(0);
			tabs->tabBar()->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_Right, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(tabs->currentIndex() == 1,
			      "and Right on a focused tab bar moves to the next tab, "
			      "which is Qt's and needs no opt-in");

			// ENTER AND THE DEFAULT BUTTON, and the guide had this
			// wrong. It said Enter "fires the dialog's default button,
			// WHEREVER focus is", and practice 2 told implementers that
			// Enter then "commits from anywhere in the dialog". Neither
			// is true: a QPushButton in a dialog has autoDefault set, so
			// a FOCUSED button is the effective default and takes Enter
			// for itself. The designated default fires only when focus
			// is on something that is not a button.
			//
			// Both states are run because the opt-in convention also
			// claims Enter, and two things clicking on one key would be
			// a defect nobody had looked for. They do not collide: Qt
			// accepts the press, and the convention is gated on it not
			// having been accepted.
			for (int conv = 0; conv < 2; ++conv) {
				set_keyboard_conventions(conv != 0);
				QDialog dlg;
				dlg.setAttribute(Qt::WA_DontShowOnScreen);
				auto *dv = new QVBoxLayout(&dlg);
				auto *okb = new QPushButton(QStringLiteral("OK"));
				auto *oth = new QPushButton(QStringLiteral("Other"));
				okb->setDefault(true);
				dv->addWidget(okb);
				dv->addWidget(oth);
				int okf = 0, othf = 0;
				QObject::connect(okb, &QPushButton::clicked,
				                 [&] { ++okf; });
				QObject::connect(oth, &QPushButton::clicked,
				                 [&] { ++othf; });
				dlg.show();
				QCoreApplication::processEvents();
				InputRouter dr(&dlg);
				oth->setFocus();
				set_focus_widget(dlg.focusWidget());
				dr.on_key({Qt::Key_Return, QStringLiteral("\r"),
					       false, false, false});
				QCoreApplication::processEvents();
				CHECK(okf == 0 && othf == 1,
				      conv ? "with the conventions on, Enter on a focused "
				             "button fires that button and not the dialog's "
				             "default, and fires it once"
				           : "Enter on a focused button fires that button "
				             "and not the dialog's default, autoDefault "
				             "following the focus");

				// And with focus on something that is NOT a button,
				// which is the case the guide's row is really about.
				auto *fld = new QLineEdit;
				dv->addWidget(fld);
				QCoreApplication::processEvents();
				okf = othf = 0;
				fld->setFocus();
				set_focus_widget(dlg.focusWidget());
				dr.on_key({Qt::Key_Return, QStringLiteral("\r"),
					       false, false, false});
				QCoreApplication::processEvents();
				// THE MECHANISM, which reconciles two checks this
				// suite has held apart since 8.33. A focused button
				// in a PLAIN widget ignores Enter; a focused button
				// in a DIALOG takes it. Both are true and the tree
				// recorded them as though one contradicted the
				// other. autoDefault is the whole difference, and
				// pinning it means a Qt that changed the default
				// reddens the explanation rather than silently
				// moving the behaviour.
				// Says which pass it is, because the block runs
				// twice and the fact is the same both times: two
				// identical verdicts are one check named twice to
				// anything reading the log rather than the screen.
				CHECK(oth->autoDefault() && !apply->autoDefault(),
				      conv ? "autoDefault is set for a button in a dialog "
				             "and clear for one in a plain widget, which is "
				             "why Enter reaches the focused button in the "
				             "first and not the second (conventions on)"
				           : "autoDefault is set for a button in a dialog "
				             "and clear for one in a plain widget, which is "
				             "why Enter reaches the focused button in the "
				             "first and not the second (conventions off)");
				CHECK(okf == 1 && othf == 0,
				      conv ? "and with them on, Enter in a field still "
				             "commits the dialog through its default button"
				           : "while Enter in a field does fire the "
				             "designated default, which is the case the "
				             "guide's row is really about");
			}
			set_keyboard_conventions(true);
		}

		// AN UNCONSUMED ESCAPE REACHES THE FOCUSED WIDGET, which is
		// the mechanism practice 5 rests on and did not name. "Enter
		// goes in, Esc comes back" is the shape a terminal user
		// expects; Qt answers Esc for a menu and a modal, and a layer
		// of the application's own -- a QStackedWidget page, an inline
		// editor, a mode -- has to hear it itself.
		//
		// Pinned in both convention states, because the risk is a
		// LATER convention taking Esc for something plausible and
		// silently retiring every application's way back. qtty binds
		// nothing to it deliberately: "back" differs per application,
		// and a guess would be wrong somewhere and unremovable.
		{
			struct Ear : QObject {
				int esc = 0;
				bool eventFilter(QObject *, QEvent *e) override {
					if (e->type() == QEvent::KeyPress
					    && static_cast<QKeyEvent *>(e)->key()
					       == Qt::Key_Escape) ++esc;
					return false;
				}
			};
			Ear ear;
			auto *page = new QLineEdit;
			v->addWidget(page);
			QCoreApplication::processEvents();
			page->installEventFilter(&ear);
			page->setFocus();
			set_focus_widget(win.focusWidget());
			for (int conv = 0; conv < 2; ++conv) {
				set_keyboard_conventions(conv != 0);
				const int was = ear.esc;
				r.on_key({Qt::Key_Escape, QString(),
					      false, false, false});
				QCoreApplication::processEvents();
				CHECK(ear.esc - was == 1,
				      conv ? "and still reaches it with the conventions "
				             "on, which take no key an application needs "
				             "for going back"
				           : "an Escape nothing else consumed reaches the "
				             "focused widget, so a layer of the "
				             "application's own can be left by it");
			}
			set_keyboard_conventions(true);
			page->removeEventFilter(&ear);
		}

		// HOVER, which practice 7 denied outright. It said "a terminal
		// has no pointer to rest, and qtty does not send
		// QEvent::ToolTip today" -- and qtty DOES send
		// QEvent::MouseMove, so Qt sets WA_Hover, delivers Enter and
		// HoverEnter, and underMouse() is true. There is a pointer and
		// it does rest.
		//
		// The advice was right and the reason was wrong, which is the
		// worse half to get wrong: a custom widget that consults
		// underMouse() in paintEvent WILL see the pointer, and would
		// have been talked out of it. What is actually absent is a
		// TOOLTIP, and a GridStyle that never reads State_MouseOver --
		// so a standard widget draws the same hovered or not, which is
		// what makes the advice true for everything Qt ships.
		{
			struct Watch : QObject {
				int enter = 0, hover = 0, tip = 0;
				bool eventFilter(QObject *, QEvent *e) override {
					switch (e->type()) {
					case QEvent::Enter:      ++enter; break;
					case QEvent::HoverEnter: ++hover; break;
					case QEvent::HoverMove:  ++hover; break;
					case QEvent::ToolTip:    ++tip;   break;
					default: break;
					}
					return false;
				}
			};
			Watch w;
			auto *hb = new QPushButton(QStringLiteral("Hov"));
			hb->setToolTip(QStringLiteral("a tip"));
			v->addWidget(hb);
			QCoreApplication::processEvents();
			hb->installEventFilter(&w);
			// The WINDOW, not the button: render_once() on a child
			// draws nothing, which the denominator assertion below
			// caught the moment it was added -- the first version of
			// this check compared two empty buffers and passed.
			const int wc = win.width() / GridMetrics::cw();
			const int wr = win.height() / GridMetrics::ch();
			CellBuffer cold(wc > 0 ? wc : 1, wr > 0 ? wr : 1);
			render_once(win, cold);
			const QPoint c(hb->geometry().center().x()
			                   / GridMetrics::cw(),
			               hb->geometry().center().y()
			                   / GridMetrics::ch());
			r.on_mouse({c, 0, false, false, true, 0});
			QCoreApplication::processEvents();
			// Bounded: a tooltip is on a ~700ms timer, so give it one
			// second of event loop and no more.
			QElapsedTimer t; t.start();
			while (t.elapsed() < 1000 && w.tip == 0) {
				QCoreApplication::processEvents(
				    QEventLoop::AllEvents, 20);
			}
			CHECK(w.enter == 1 && w.hover >= 1
			      && hb->testAttribute(Qt::WA_Hover),
			      "a mouse move over a widget delivers Enter and hover, "
			      "so a custom widget CAN know the pointer is on it");
			CHECK(w.tip == 0,
			      "but no tooltip is raised even after its timer, so "
			      "anything only a tooltip says is unreachable");

			// And the reason the advice holds for everything Qt ships:
			// the cell style never asks. Rendered rather than grepped --
			// a check reading the source would pass on a style that
			// asked and drew the same anyway, and fail on one merely
			// spelled differently. The same button that just proved it
			// receives the pointer, so the fixture cannot be the reason
			// nothing changed.
			CellBuffer warm(wc > 0 ? wc : 1, wr > 0 ? wr : 1);
			render_once(win, warm);
			// The denominator, because "the two renders agree" is true
			// of two empty buffers -- 8.72's lesson applied to the check
			// being written rather than to one being read.
			CHECK(hb->underMouse() && !cold.to_text().trimmed().isEmpty()
			      && warm.to_text() == cold.to_text(),
			      "and a standard widget under the pointer draws what it "
			      "drew without one -- something, and the same something "
			      "-- the cell style never reading State_MouseOver");
			hb->removeEventFilter(&w);
		}

		// A CONTEXT MENU WITHOUT A MOUSE. qtty synthesises
		// QContextMenuEvent for a right press, because the platform
		// layer that normally does it is absent -- and the KEYBOARD
		// routes live in that same absent layer and were left open.
		// Measured before the fix: 0 events from either key, so every
		// action behind a right-click was reachable only by pointer,
		// in the library whose own guide forbids exactly that.
		{
			struct Ctx : QObject {
				int n = 0;
				bool eventFilter(QObject *, QEvent *e) override {
					if (e->type() == QEvent::ContextMenu) ++n;
					return false;
				}
			};
			Ctx cm;
			auto *cw = new QLineEdit;
			v->addWidget(cw);
			QCoreApplication::processEvents();
			cw->installEventFilter(&cm);
			cw->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_Menu, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(cm.n == 1 && !r.popups().isEmpty(),
			      "the Menu key opens a context menu, so an action behind "
			      "a right-click is reachable without a mouse");

			// Close it before asking again. The first version of this
			// check did not, read Shift+F10 as broken, and was wrong:
			// the Menu key had opened a REAL popup, so the next key went
			// to the menu, which is correct routing. The pointers in the
			// diagnostic differed and that was the whole story.
			r.on_key({Qt::Key_Escape, QString(), false, false, false});
			QCoreApplication::processEvents();
			cw->setFocus();
			set_focus_widget(win.focusWidget());
			const int after_esc = cm.n;
			r.on_key({Qt::Key_F10, QString(), false, false, true});
			QCoreApplication::processEvents();
			CHECK(cm.n == after_esc + 1,
			      "and Shift+F10 does too, the other key every desktop "
			      "binds to the same thing");

			// AND A WIDGET THAT WANTS THE KEY KEEPS IT, which is the
			// half practice 6 rests on: the guide says these two are not
			// a shortcut qtty bound, because the menu opens only where
			// nothing accepted the press. That was a claim with nothing
			// behind it until now -- the same shape 8.73 found in three
			// other rows of the same table, one of which was false.
			// Close the menu Shift+F10 just opened. Without this the
			// popup owns input, the key never reaches the widget below,
			// and the check fails for the fixture rather than the code --
			// measured, seen=0 with popups=1.
			r.on_key({Qt::Key_Escape, QString(), false, false, false});
			QCoreApplication::processEvents();
			struct Eater : QWidget {
				int seen = 0;
				void keyPressEvent(QKeyEvent *e) override {
					if (e->key() == Qt::Key_Menu) {
						++seen;
						e->accept();
						return;
					}
					QWidget::keyPressEvent(e);
				}
			};
			auto *eater = new Eater;
			eater->setFocusPolicy(Qt::StrongFocus);
			v->addWidget(eater);
			QCoreApplication::processEvents();
			Ctx eaten;
			eater->installEventFilter(&eaten);
			eater->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_Menu, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(eater->seen == 1 && eaten.n == 0 && r.popups().isEmpty(),
			      "but a widget that handles the Menu key keeps it, so this "
			      "is the platform's behaviour restored rather than a key "
			      "qtty took");
			eater->removeEventFilter(&eaten);

			// CTRL+Z IS AN ORDINARY KEY, which the guide's table says and
			// nothing held. The backend clears ISIG so the driver no longer
			// turns it into a suspend, and InputRouter binds nothing to it,
			// so it reaches the focused widget like any other chord. That is
			// what lets an application bind it and raise SIGTSTP itself when
			// it wants the conventional behaviour.
			//
			// Added because the guide gained the row first and its own rule
			// says a row arrives with the check that holds it. Testing that
			// rule found it broken by the person who wrote it.
			struct Zed : QObject {
				int z = 0;
				bool eventFilter(QObject *, QEvent *e) override {
					if (e->type() != QEvent::KeyPress) return false;
					auto *k = static_cast<QKeyEvent *>(e);
					if (k->key() == Qt::Key_Z
					    && (k->modifiers() & Qt::ControlModifier))
						++z;
					return false;
				}
			};
			Zed zed;
			auto *zf = new QLineEdit;
			v->addWidget(zf);
			QCoreApplication::processEvents();
			zf->installEventFilter(&zed);
			zf->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_Z, QStringLiteral("z"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(zed.z == 1,
			      "Ctrl+Z reaches the focused widget as an ordinary key, the "
			      "driver no longer making it a suspend, so an application "
			      "can bind it");

			// A TEXT FIELD WIDER THAN THE TERMINAL keeps its caret in
			// view. It did not: follow_rect() returned the whole widget,
			// and a widget wider than the view can only scroll to its
			// RIGHT edge, which puts the caret outside and makes the
			// compositor place no cursor at all. Measured before the fix
			// -- present at (1,1) in a 60-cell view, absent in a 20-cell
			// one -- so a person typed with nothing saying where.
			//
			// The control is the same field in a view with room for it:
			// asserting only that the narrow case has A cursor would pass
			// for a cursor parked anywhere, and the two agreeing is the
			// claim worth making.
			{
				QWidget host;
				host.setAttribute(Qt::WA_DontShowOnScreen);
				auto *wide = new QLineEdit(&host);
				wide->setMinimumWidth(60 * GridMetrics::cw());
				wide->setGeometry(0, 0, 60 * GridMetrics::cw(),
				                  GridMetrics::ch());
				host.resize(GridMetrics::cells(60, 2));
				host.show();
				QCoreApplication::processEvents();
				InputRouter wr(&host);
				Compositor wc(&host, &wr);
				wide->setFocus();
				set_focus_widget(host.focusWidget());
				CellBuffer wideview(60, 2);
				wc.compose(wideview);
				const auto ctl = wc.cursor_cell();
				CellBuffer narrow(20, 2);
				wc.compose(narrow);
				const auto cur = wc.cursor_cell();
				printf("info: a 60-cell field: cursor at (%d,%d) in a "
				       "60-cell view, (%d,%d) in a 20-cell one\n",
				       ctl ? ctl->x() : -1, ctl ? ctl->y() : -1,
				       cur ? cur->x() : -1, cur ? cur->y() : -1);
				CHECK(ctl && cur && *ctl == *cur,
				      "a text field wider than the terminal keeps its caret "
				      "in view -- the cursor lands in the same cell whether "
				      "the view has room for the whole field or not");

				// THE CARET AT THE FAR END, which is what actually
				// separates following the caret from following the widget.
				// The check above cannot: its field is empty, so the caret
				// sits at the left -- and a rect wider than the view shows
				// its LEFT edge under the rule added later, which puts the
				// caret on screen for the wrong reason. The full sabotage
				// run is what said so, reporting that reverting the caret
				// fix left that check green.
				//
				// With the caret at the end the two rules disagree: follow
				// the widget and the left edge is shown with the caret far
				// off it, and the compositor then places no cursor at all,
				// which is the symptom the original defect was reported as.
				wide->setText(QString(55, QLatin1Char('x')));
				wide->setCursorPosition(55);
				QCoreApplication::processEvents();
				CellBuffer endview(20, 2);
				wc.compose(endview);
				const auto at_end = wc.cursor_cell();
				printf("info: caret at 55 of 55 in a 20-cell view: cursor "
				       "%s\n", at_end ? "placed" : "ABSENT");
				CHECK(at_end && at_end->x() >= 0 && at_end->x() < 20,
				      "and with the caret at the far end of that field the "
				      "cursor is still placed, inside the view, rather than "
				      "left off the screen with nothing saying where typing "
				      "goes");

				// AND A RECT WIDER THAN THE VIEW SHOWS ITS LEFT EDGE.
				// Same fault one widget over, found the same way: a menu
				// thirty cells wide in a twenty-cell terminal drew its
				// shortcut column and nothing else, every item NAME off
				// the left, because the only branch that could fire
				// scrolled to the far edge.
				//
				// Asserted on the frame rather than on the scroll offset:
				// the offset is the mechanism and the text is what a
				// person sees, and a check on the offset would pass for a
				// rule that happened to compute the same number by a
				// route nobody meant.
				// A MENU, not a field, and the harness insisted. The
				// first version of this asserted a wide FIELD shows its
				// start -- which it does with the left-edge rule removed,
				// because follow_rect() returns the caret there and a
				// caret is never wider than the view. The check could not
				// fail, and the sabotage entry said so.
				//
				// A menu item's rect IS the menu's full width, so this is
				// where the rule bites: without it the view scrolls to the
				// item's right edge and draws the shortcut column alone.
				QMenu wide_menu(&host);
				wide_menu.addAction(
				    QStringLiteral("Undo the last thing you did"));
				wide_menu.addAction(
				    QStringLiteral("Redo the thing you just undid"));
				wide_menu.popup(QPoint(0, 0));
				QCoreApplication::processEvents();
				wide_menu.setActiveAction(wide_menu.actions().at(0));
				QCoreApplication::processEvents();
				CellBuffer left_edge(20, 4);
				wc.compose(left_edge);
				CHECK(left_edge.to_text().contains(QStringLiteral("Undo")),
				      "a menu wider than the terminal shows its item names "
				      "rather than scrolling past them to the far edge");
				wide_menu.close();
				QCoreApplication::processEvents();
			}
			zf->removeEventFilter(&zed);
			r.on_key({Qt::Key_Escape, QString(), false, false, false});
			QCoreApplication::processEvents();
			cw->removeEventFilter(&cm);
		}

		// THE ESCAPE ROW of the guide's "what already works" table, stated
		// as Qt's and asserted by nothing. Four rows in that table had no
		// check; three were measured in 8.73 and one of THOSE was wrong, so
		// an unverified row earns a fixture rather than a shrug.
		{
			QMenu m(&win);
			m.addAction(QStringLiteral("One"));
			m.popup(QPoint(0, 0));
			QCoreApplication::processEvents();
			const bool up = !r.popups().isEmpty();
			r.on_key({Qt::Key_Escape, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(up && r.popups().isEmpty() && !m.isVisible(),
			      "Escape closes an open menu -- asserted with the menu "
			      "proved open first, since an empty popup stack is empty "
			      "whether the key worked or the menu never opened");

			// NOT result() == QDialog::Rejected: Rejected is 0 and a dialog
			// that has done nothing returns 0, so the obvious spelling
			// passes without the key being delivered at all. The signal
			// fires once or it does not.
			QDialog d(&win);
			d.setAttribute(Qt::WA_DontShowOnScreen);
			d.setModal(true);
			int rejected = 0;
			QObject::connect(&d, &QDialog::rejected, [&] { ++rejected; });
			d.show();
			QCoreApplication::processEvents();
			r.on_key({Qt::Key_Escape, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(rejected == 1 && !d.isVisible(),
			      "and rejects a modal dialog, so a person who opened one by "
			      "accident is not shut inside it");
		}

		// hasFocus() IS PERMANENTLY FALSE, which practice 10 of the guide
		// warns about and nothing asserted. Under the offscreen platform no
		// window ever activates, so Qt never sets its own focus widget --
		// and a custom widget drawing `if (hasFocus()) drawFocusMark()`
		// therefore draws nothing on a terminal while working perfectly on
		// the desktop. Same binary, same widget, no error anywhere.
		//
		// The RELATIONSHIP is the check. Asserting `!hasFocus()` alone
		// passes before anything is focused at all, and asserting qtty's
		// answer alone says nothing about the trap. What is worth pinning
		// is that the two disagree at the same instant, because that
		// disagreement IS the reason Qtty::focusWidget() exists.
		{
			auto *fw = new QLineEdit;
			v->addWidget(fw);
			QCoreApplication::processEvents();
			fw->setFocus();
			set_focus_widget(win.focusWidget());
			CHECK(Qtty::focusWidget() == fw && win.focusWidget() == fw
			      && !fw->hasFocus()
			      && QApplication::focusWidget() == nullptr,
			      "qtty names the focused widget at the moment Qt's own "
			      "hasFocus() is false, which is why a custom widget must "
			      "ask Qtty::focusWidget() to draw a focus mark");

			// AND THE SAME TRAP FOR MODIFIERS, which this library cannot
			// repair. Qt fills QApplication::keyboardModifiers() from
			// PLATFORM events, and there is no public setter -- the header
			// has the getter and queryKeyboardModifiers() and nothing that
			// writes. Measured: a synthetic KEY event does update the
			// global, and a synthetic MOUSE event does not. So an
			// application asking it inside a clicked() slot, which is the
			// ordinary way to spell "Ctrl+click", reads no modifiers while
			// the event in its hand says Ctrl.
			//
			// Pinned as a DISAGREEMENT at one instant rather than as either
			// value alone, the way hasFocus() is pinned above: if Qt ever
			// starts tracking these, this check fails and says the limit has
			// lifted.
			{
				// ITS OWN WINDOW, because the shared one is crowded by
				// this point in the section and the probe landed on the
				// last row with the press reaching the widget above it --
				// seen=0, traced. A window of its own removes the layout
				// from the question, which is not what is under test.
				struct Probe : QPushButton {
					Qt::KeyboardModifiers on_event, on_app;
					int seen = 0;
					explicit Probe(QWidget *p)
					    : QPushButton(QStringLiteral("MODPROBE"), p) {}
					void mousePressEvent(QMouseEvent *e) override {
						++seen;
						on_event = e->modifiers();
						on_app = QApplication::keyboardModifiers();
						QPushButton::mousePressEvent(e);
					}
				};
				QWidget host;
				host.setAttribute(Qt::WA_DontShowOnScreen);
				host.setWindowTitle(QStringLiteral("Mods"));
				auto *hv = new QVBoxLayout(&host);
				hv->setContentsMargins(0, 0, 0, 0);
				hv->setSpacing(0);
				auto *probe = new Probe(&host);
				probe->setFixedHeight(ch);
				hv->addWidget(probe);
				host.resize(GridMetrics::cells(20, 3));
				host.show();
				QCoreApplication::processEvents();

				InputRouter hr(&host);
				Compositor hc(&host, &hr);
				Qtty::set_current_window(&host);
				QCoreApplication::processEvents();
				CellBuffer frame(20, 3);
				hc.compose(frame);
				int prow = -1;
				const QStringList prows =
				    frame.to_text().split(QLatin1Char('\n'));
				for (int i = 0; i < prows.size(); ++i)
					if (prows.at(i).contains(QStringLiteral("MODPROBE")))
						prow = i;
				CHECK(prow >= 0,
				      "the control: the modifier probe is on screen to be "
				      "clicked");
				hr.on_mouse({QPoint(1, prow), 1, true, false, false, 0, 0,
					         true, false, false});
				QCoreApplication::processEvents();
				CHECK(probe->seen >= 1
				      && (probe->on_event & Qt::ControlModifier)
				      && !(probe->on_app & Qt::ControlModifier),
				      "a mouse event carries its modifiers while "
				      "QApplication::keyboardModifiers() does not, so a "
				      "Ctrl+click is read from the event or not at all");
				host.hide();
				Qtty::set_current_window(&win);
				QCoreApplication::processEvents();
			}

			// AND IT FOLLOWS AN APPLICATION THAT MOVES FOCUS ITSELF.
			// `edit->setFocus()` in a slot is the commonest thing a Qt
			// program does, and it used to leave this record behind: qtty
			// went on naming the old widget until the next keystroke, so
			// the reverse-video mark, the widget-shortcut contexts and any
			// application asking this library who has focus all got the
			// widget that used to have it.
			//
			// There is no signal to connect to, measured rather than
			// assumed: in a window that never activates Qt emits no
			// focusChanged and delivers no FocusIn or FocusOut when
			// setFocus() is called. window()->focusWidget() simply changes.
			// So the record is re-read where it is consumed -- at the top
			// of compose() and at the top of on_key().
			auto *other = new QLineEdit;
			v->addWidget(other);
			QCoreApplication::processEvents();
			// THIS window current first, and the switch is what makes the
			// rest of this about compose() rather than about the switch.
			// The suite leaves other top-levels visible, so without it the
			// drawn window is somebody else's and the record correctly
			// names the focus in THAT one -- the first version of this
			// check failed for exactly that reason.
			Qtty::set_current_window(&win);
			QCoreApplication::processEvents();
			other->setFocus();                  // no key, no mouse, no switch
			QCoreApplication::processEvents();
			CHECK(win.focusWidget() == other && Qtty::focusWidget() != other,
			      "the control: Qt's focus has moved and nothing has told "
			      "qtty, which is the state this is about");

			CellBuffer refreshed(40, 16);
			comp.compose(refreshed);
			CHECK(Qtty::focusWidget() == other,
			      "composing a frame re-reads who has focus, so an "
			      "application that moved it in a slot is drawn with the "
			      "mark on the widget that has it");

			// The keyboard half: a widget-context shortcut is decided
			// against this record, and the repair used to happen after the
			// matching rather than before it -- so the FIRST key after a
			// programmatic move was judged against the old widget.
			set_focus_widget(fw);               // stale it again, deliberately
			other->setFocus();
			QCoreApplication::processEvents();
			int owned = 0;
			auto *osc = new QShortcut(
			    QKeySequence(QStringLiteral("Ctrl+Q")), other);
			osc->setContext(Qt::WidgetShortcut);
			QObject::connect(osc, &QShortcut::activated, [&] { ++owned; });
			r.on_key({Qt::Key_Q, QStringLiteral("q"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(owned == 1,
			      "and the first key after that move is judged against the "
			      "widget that has focus, so its own WidgetShortcut fires");
			delete osc;
		}

		// QSHORTCUT, which is not a QAction and so was invisible to the
		// router's table. `new QShortcut(QKeySequence("Ctrl+S"), this)` is
		// the commonest way a Qt application binds a key, and it worked in
		// the desktop build and did nothing at all on the terminal --
		// silently, since Qt's own map gates on an active window and none
		// activates here, so the key simply fell through.
		{
			int fired = 0, widget_fired = 0;
			auto *sc = new QShortcut(
			    QKeySequence(QStringLiteral("Ctrl+S")), &win);
			QObject::connect(sc, &QShortcut::activated, [&] { ++fired; });
			r.on_key({Qt::Key_S, QStringLiteral("s"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(fired == 1,
			      "a QShortcut fires, which is the commonest way a Qt "
			      "application binds a key and did nothing here at all");
			// EVERY SHORTCUT IN THIS SECTION IS DELETED WITH THE COUNTER IT
			// WRITES TO, and they were not. Parented to `win`, they outlived
			// the blocks their `[&]` lambdas capture, so anything that made
			// them fire later wrote to a dead stack frame. That is not
			// theoretical: a sabotage widening the cross-window search
			// segfaulted the suite -- the backtrace was a posted event
			// delivered inside on_key, a long way from the cause -- and the
			// check it was written for could not be defended at all until
			// this was fixed. A fixture that outlives what it reports into is
			// a trap for whoever next broadens shortcut matching.

			sc->setEnabled(false);
			r.on_key({Qt::Key_S, QStringLiteral("s"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(fired == 1,
			      "and a disabled one does not, so the enabled flag means on "
			      "a terminal what it means on a desktop");

			// CONTEXT, because firing a WidgetShortcut from anywhere would
			// give the terminal a binding the desktop does not have -- a
			// fault in the other direction, and the harder one to notice.
			auto *host = new QLineEdit;
			auto *other = new QLineEdit;
			v->addWidget(host);
			v->addWidget(other);
			QCoreApplication::processEvents();
			auto *wsc = new QShortcut(
			    QKeySequence(QStringLiteral("Ctrl+G")), host);
			wsc->setContext(Qt::WidgetShortcut);
			QObject::connect(wsc, &QShortcut::activated,
			                 [&] { ++widget_fired; });
			other->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_G, QStringLiteral("g"), true, false, false});
			QCoreApplication::processEvents();
			const int away = widget_fired;
			host->setFocus();
			set_focus_widget(win.focusWidget());
			r.on_key({Qt::Key_G, QStringLiteral("g"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(away == 0 && widget_fired == 1,
			      "a WidgetShortcut fires only while its own widget has "
			      "focus, so the terminal does not answer a key the desktop "
			      "would leave alone");
			delete sc;                    // see the note above: with its
			delete wsc;                   // counter, not with the window
		}

		// AND A SHORTCUT DOES NOT FIRE FROM BEHIND AN OPEN MENU, which
		// is the rule the QAction table already followed and which the
		// QShortcut loop was written to match -- `if (popup_owns_input)
		// return true` swallows the chord rather than firing it. Adding
		// the code without the check would have left the one branch of
		// the new loop that says NO untested, which is the half a
		// sabotage of the other half cannot reach.
		{
			int behind = 0;
			auto *bsc = new QShortcut(
			    QKeySequence(QStringLiteral("Ctrl+B")), &win);
			QObject::connect(bsc, &QShortcut::activated,
			                 [&] { ++behind; });
			QMenu blocker(&win);
			blocker.addAction(QStringLiteral("One"));
			blocker.popup(QPoint(0, 0));
			QCoreApplication::processEvents();
			const bool up = !r.popups().isEmpty();
			r.on_key({Qt::Key_B, QStringLiteral("b"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(up && behind == 0,
			      "a QShortcut does not fire from behind an open menu, the "
			      "chord being swallowed exactly as an action's is");
			r.on_key({Qt::Key_Escape, QString(), false, false, false});
			QCoreApplication::processEvents();
			r.on_key({Qt::Key_B, QStringLiteral("b"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(behind == 1,
			      "and fires once the menu is gone, so the swallow is the "
			      "menu's doing rather than the shortcut being broken");
			delete bsc;
		}

		// CONTEXT ACROSS WINDOWS, which 8.107 made reachable and broke in
		// the same stroke. input_scope() answers the CURRENT window now, so
		// a shortcut belonging to another window is out of the search -- and
		// that is right for Qt::WindowShortcut and wrong for
		// Qt::ApplicationShortcut, whose whole meaning is that it does not
		// care where you are. Before 8.107 an application-context shortcut
		// parented to the primary window fired from anywhere because every
		// key went to the primary window, which was the defect and not the
		// feature, so nothing here had ever asked the question.
		{
			// A compositor of its own, paired with THIS router. The switch
			// tells the router that the live compositor was built with, and
			// the one in this section is paired with another -- so without
			// this the current window moved and `r` never heard, which is
			// the very defect under test wearing a test bug's clothes.
			// Measured: the two context checks below failed that way first.
			//
			// It also takes the tab strip down with it (~Compositor), which
			// keeps `elsewhere` out of the registry for the sections after
			// this one. Left in, it put a strip in row 0 and shifted every
			// later click by a row -- four failures, none of them about
			// shortcuts.
			Compositor local(&win, &r);
			int app_fired = 0, win_fired = 0;
			// Ctrl+Y and Ctrl+K, chords no other block in this suite
			// binds. Ctrl+G was used first and is the WidgetShortcut of the
			// block above: a cross-window search widened by sabotage found
			// that one instead of this one, which made the sabotage's
			// verdict a fact about fixture order rather than about context.
			auto *asc = new QShortcut(
			    QKeySequence(QStringLiteral("Ctrl+Y")), &win);
			asc->setContext(Qt::ApplicationShortcut);
			QObject::connect(asc, &QShortcut::activated,
			                 [&] { ++app_fired; });
			// The control for the other direction, in the SAME window, so
			// the pair differs only in context. A search that reached out of
			// the scope for everything would fire this one too, and a
			// reader cannot tell those two mistakes apart from one check.
			auto *wsc2 = new QShortcut(
			    QKeySequence(QStringLiteral("Ctrl+K")), &win);
			wsc2->setContext(Qt::WindowShortcut);
			QObject::connect(wsc2, &QShortcut::activated,
			                 [&] { ++win_fired; });

			QWidget elsewhere;
			elsewhere.setAttribute(Qt::WA_DontShowOnScreen);
			auto *ev = new QVBoxLayout(&elsewhere);
			ev->addWidget(new QLineEdit(&elsewhere));
			elsewhere.resize(GridMetrics::cells(20, 2));
			elsewhere.show();
			QCoreApplication::processEvents();
			{
				CellBuffer reg(40, 16);
				local.compose(reg);
			}
			Qtty::set_current_window(&elsewhere);
			r.on_key({Qt::Key_Y, QStringLiteral("y"), true, false, false});
			r.on_key({Qt::Key_K, QStringLiteral("k"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(Qtty::current_window() == &elsewhere && app_fired == 1,
			      "an application-context shortcut fires from a window that "
			      "does not own it");
			CHECK(win_fired == 0,
			      "while a window-context shortcut in the window you left "
			      "stays quiet");

			// And back, where the window context is the one that applies.
			Qtty::set_current_window(&win);
			r.on_key({Qt::Key_K, QStringLiteral("k"), true, false, false});
			QCoreApplication::processEvents();
			CHECK(win_fired == 1,
			      "and answers again once its own window is current, so the "
			      "silence was the context and not a dead shortcut");
			// The arm that says NO. An application-context shortcut is
			// swallowed behind an open menu like every other, and the menu
			// has to be opened in the CURRENT window to ask the question at
			// all -- switching windows dismisses the one the old window had
			// (8.107), so a menu opened before the switch is gone by now.
			Qtty::set_current_window(&elsewhere);
			{
				QMenu over(&elsewhere);
				over.addAction(QStringLiteral("Item"));
				over.popup(QPoint(0, 0));
				QCoreApplication::processEvents();
				const bool up = !r.popups().isEmpty();
				const int before_menu = app_fired;
				r.on_key({Qt::Key_Y, QStringLiteral("y"), true, false,
					      false});
				QCoreApplication::processEvents();
				CHECK(up && app_fired == before_menu,
				      "and an application-context shortcut is swallowed "
				      "behind an open menu like any other");
				over.close();
				QCoreApplication::processEvents();
			}

			Qtty::set_current_window(&win);
			delete asc;
			delete wsc2;
			elsewhere.hide();
			QCoreApplication::processEvents();
		}

		// What an application SHOWS. A terminal user cannot find a binding
		// by looking for a button, so the guide asks every application to
		// put its keys on the screen -- and one that wrote "F6 window"
		// into its own status bar would keep a second copy of a fact this
		// library owns.
		{
			const auto help = keyboard_conventions_help();
			QStringList keys;
			for (const auto &row : help) keys << row.first;
			printf("info: the conventions describe themselves as [%s]\n",
			       qPrintable(keys.join(QStringLiteral(", "))));
			// Every binding the block above exercised has a line.
			//
			// This used to claim more than it does -- "a key added to one and
			// not the other fails here" -- and that was false, measured: six
			// readline chords went into the bundle and this stayed green,
			// because the list it compares against is written out by hand
			// here and drifts with the code rather than deriving from it.
			// `evidence.md` calls that a name claiming exhaustiveness, and
			// the honest version says which direction it covers.
			//
			// It covers this one: a key NAMED below must be in the help. The
			// other direction -- a binding with no line -- is not mechanically
			// checkable while the bindings live in on_key()'s branches rather
			// than in a table, so the row COUNT is pinned instead: adding to
			// the help without adding here, or the reverse, moves it.
			CHECK(keys.contains(QStringLiteral("Enter"))
			      && keys.contains(QStringLiteral("Up/Down"))
			      && keys.contains(QStringLiteral("Ctrl+PgUp/PgDn"))
			      && keys.contains(QStringLiteral("F6"))
			      && keys.contains(QStringLiteral("Alt+letter"))
			      && keys.contains(QStringLiteral("Ctrl+A/E"))
			      && keys.contains(QStringLiteral("Ctrl+K/U"))
			      && keys.contains(QStringLiteral("Ctrl+W/D"))
			      && keys.contains(QStringLiteral("Menu/Shift+F10")),
			      "the conventions can list what they bind, so an "
			      "application shows the keys without keeping its own copy "
			      "of them");
			for (const auto &row : help)
				if (row.second.isEmpty()) keys.clear();
			CHECK(!keys.isEmpty(),
			      "and every line says what its key does, a key with no "
			      "meaning beside it being no help at all");
			CHECK(help.size() == 10,
			      "and the list is exactly as long as this check knows about,"
			      " which is what stands in for deriving it from the"
			      " bindings");
		}

		// The GUIDE is the third writer of this list, and nothing held it
		// against the other two. `keyboard_conventions_help()` is what an
		// application puts on its status bar; doc/keyboard-first.md is what
		// somebody reads before writing one, and it keeps its own table of
		// the same chords. A binding added to the bundle and not to the
		// guide leaves the document describing a smaller library than the
		// one that shipped, and the reader who would notice is the one who
		// does not know the key exists.
		//
		// DERIVED from the bundle rather than listed beside it, which is
		// 8.150's lesson applied to a document: every row must have a
		// spelling here, so a row added with none fails as a message
		// addressed to whoever added it rather than being passed over. The
		// spellings differ on purpose -- the bundle is sized for a status
		// bar and writes `Ctrl+PgUp/PgDn` where prose writes `Ctrl+PageUp`
		// -- so what is asserted is that the guide DESCRIBES the chord, not
		// that the two agree letter for letter.
		{
			const auto bound = keyboard_conventions_help();
			QFile f(QStringLiteral(QTTY_SOURCE_DIR "/doc/keyboard-first.md"));
			const bool opened = f.open(QIODevice::ReadOnly | QIODevice::Text);
			CHECK(opened,
			      "the guide is where the suite can read it, an unreadable "
			      "guide being a check that cannot fail rather than one "
			      "that passes");
			const QString guide = opened ? QString::fromUtf8(f.readAll())
			                             : QString();
			// Every chord the bundle names, and a phrase the guide has
			// to carry for it. Two rows where one bundle line covers two
			// keys, so that losing half of a pair is a failure too.
			static const struct { const char *key, *in_guide; } spelled[] = {
				{"Enter",          "| `Enter` |"},
				{"Up/Down",        "| `Up`, `Down` |"},
				{"Ctrl+PgUp/PgDn", "`Ctrl+PageUp`"},
				{"Ctrl+PgUp/PgDn", "`Ctrl+PageDown`"},
				{"F6",             "`F6`"},
				{"F10",            "`F10`"},
				{"Alt+letter",     "`Alt` + a tab's letter"},
				{"Ctrl+A/E",       "`Ctrl+A`"},
				{"Ctrl+A/E",       "`Ctrl+E`"},
				{"Ctrl+K/U",       "`Ctrl+K`"},
				{"Ctrl+K/U",       "`Ctrl+U`"},
				{"Ctrl+W/D",       "`Ctrl+W`"},
				{"Ctrl+W/D",       "`Ctrl+D`"},
				{"Menu/Shift+F10", "`Menu`, `Shift+F10`"},
			};

			QStringList unknown, undescribed;
			for (const auto &row : bound) {
				int named = 0;
				for (const auto &s : spelled) {
					if (row.first != QLatin1String(s.key)) continue;
					++named;
					if (!guide.contains(QLatin1String(s.in_guide)))
						undescribed << row.first;
				}
				if (named == 0) unknown << row.first;
			}
			if (!unknown.isEmpty() || !undescribed.isEmpty())
				printf("info: unknown to this check [%s]; missing from the "
				       "guide [%s]\n",
				       qPrintable(unknown.join(QStringLiteral(", "))),
				       qPrintable(undescribed.join(QStringLiteral(", "))));
			CHECK(unknown.isEmpty(),
			      "every key the conventions list is one this check knows "
			      "where to look for, so a binding cannot enter the bundle "
			      "without the guide being asked about it");
			CHECK(undescribed.isEmpty(),
			      "and the guide describes every key they bind, an "
			      "application's help and the document it was written from "
			      "being two copies of one list");
		}
		set_keyboard_conventions(false);          // process-wide: put it back
		// NOT empty, since 8.77. The contract was never "empty when
		// off" -- it was never promise a key that does nothing, and the
		// context menu's keys work whether or not the conventions were
		// asked for. An application showing this list unconditionally
		// still gets exactly the keys that answer.
		const auto off = keyboard_conventions_help();
		CHECK(off.size() == 1
		      && off[0].first == QStringLiteral("Menu/Shift+F10"),
		      "and with the conventions off it lists only the keys that "
		      "work anyway, so the list is what answers rather than what "
		      "was opted into");
	}

	// ---- and which of those rows this window has taken back. The help
	// list has no scope to ask, so it promises what the LIBRARY answers to
	// -- and an application that binds Ctrl+K to Insert Link has removed
	// that convention from its own window while still showing the row to a
	// user. The list is then true about the library and false about the
	// window, which is the fault it exists to prevent arriving from the one
	// side it cannot see.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 8));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 20 * GridMetrics::cw(), GridMetrics::ch());
		int linked = 0, panels = 0;
		auto *link = new QAction(QStringLiteral("Insert &Link"), &host);
		link->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
		QObject::connect(link, &QAction::triggered, [&] { ++linked; });
		host.addAction(link);
		auto *six = new QAction(QStringLiteral("Panels"), &host);
		six->setShortcut(QKeySequence(Qt::Key_F6));
		QObject::connect(six, &QAction::triggered, [&] { ++panels; });
		host.addAction(six);
		host.show();
		QCoreApplication::processEvents();

		CHECK(conventions_shadowed(&host).isEmpty(),
		      "with the conventions off nothing is shadowed, the rows they "
		      "would have promised not being promised");

		// EXCEPT THE ONE ROW THAT ANSWERS ANYWAY, which is the other
		// direction and was checked by nothing. 8.77 made the context
		// menu answer whether or not the bundle was asked for, because it
		// is a platform behaviour this library restores rather than a
		// convention it offers -- so an application that takes Shift+F10
		// has taken something real, and must be told even with the
		// conventions off.
		//
		// The check above binds F6 and Ctrl+K, so it can only catch a row
		// wrongly reported while off. Nothing could catch this row
		// quietly ceasing to be -- which matters because the walk used to
		// decide it by comparing the row's DISPLAY TEXT against a
		// hand-written copy of itself, and display text gets reworded.
		{
			QWidget off_host;
			off_host.setAttribute(Qt::WA_DontShowOnScreen);
			off_host.resize(GridMetrics::cells(30, 6));
			auto *ctx = new QAction(QStringLiteral("Context"), &off_host);
			ctx->setShortcut(QKeySequence(QStringLiteral("Shift+F10")));
			ctx->setShortcutContext(Qt::WindowShortcut);
			ctx->setObjectName(QStringLiteral("the context claim"));
			off_host.addAction(ctx);
			off_host.show();
			QCoreApplication::processEvents();
			QStringList off_rows;
			for (const auto &t : conventions_shadowed(&off_host))
				off_rows << t.first;
			CHECK(off_rows == QStringList{QStringLiteral("Menu/Shift+F10")},
			      "while the context-menu row is reported even with the "
			      "conventions off, that one answering whether or not the "
			      "bundle was asked for");
			off_host.hide();
			QCoreApplication::processEvents();
		}

		set_keyboard_conventions(true);
		const auto taken = conventions_shadowed(&host);
		QStringList rows;
		for (const auto &t : taken) rows << t.first;
		printf("info: convention rows this window has taken back [%s]\n",
		       qPrintable(rows.join(QStringLiteral(", "))));
		CHECK(rows.contains(QStringLiteral("Ctrl+K/U"))
		          && rows.contains(QStringLiteral("F6")),
		      "the rows an application's own shortcuts have taken back are "
		      "named, which nothing else can tell it");

		// EVERY ROW NAMED IS A ROW THE HELP LIST SHOWS. Two lists of the
		// same bindings drift, and the drift is silent -- a row spelled
		// one way here and another there would leave a status bar unable
		// to match them up at all.
		const auto help = keyboard_conventions_help();
		QStringList shown;
		for (const auto &h : help) shown << h.first;
		bool all_shown = !taken.isEmpty();
		for (const auto &t : taken)
			if (!shown.contains(t.first)) all_shown = false;
		CHECK(all_shown,
		      "and every row it names is spelled exactly as the help list "
		      "spells it, so a status bar can strike out the row it was "
		      "about to show");

		// THE RELATIONSHIP, which is what makes the report worth reading:
		// the keys really are gone. Ctrl+K leaves the line whole and fires
		// the application's action instead of killing to end of line.
		InputRouter cr(&host);
		field->setText(QStringLiteral("hello brave world"));
		field->setCursorPosition(11);
		field->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		cr.on_key({Qt::Key_K, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(linked == 1
		          && field->text() == QStringLiteral("hello brave world"),
		      "and the key really is gone: Ctrl+K fires the application's "
		      "action and the line it would have killed is whole");
		cr.on_key({Qt::Key_F6, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(panels == 1,
		      "as is F6, which answers the application rather than moving "
		      "between windows");
		set_keyboard_conventions(false);
	}

	// ---- the partition: every help row has a chord, or a reason it has none
	//
	// 8.187 built conventions_shadowed() over a table of seven chords while
	// keyboard_conventions_help() returns ten rows, and the check above it
	// asserts ONE DIRECTION -- every row the report names is spelled as the
	// help spells it. Nothing asked the other way, so the three rows with no
	// chord behind them were invisible to it: a report that can never name
	// `Enter` passes that check exactly as loudly as one that can.
	//
	// What is asserted here is the PARTITION rather than two more rows.
	// Every row the help list returns is either PLACED -- it has a chord,
	// proved by binding that chord to a window and requiring the report to
	// name the row -- or it is a FAMILY, with the reason it cannot be a
	// chord recorded beside it. A row added to the help list and to neither
	// reddens `unplaced`, which is a message addressed to whoever added it
	// rather than a silence.
	{
		set_keyboard_conventions(true);
		// The chord a window binds to take each row back, two entries where
		// one row covers two keys so that losing half a pair fails too.
		// `why` is what a row carries INSTEAD of a chord.
		static const struct {
			const char *shown;
			int mod;
			int key;
			const char *why;
		} placed[] = {
			{ "Enter",          0,              Qt::Key_Return,   nullptr },
			{ "Enter",          0,              Qt::Key_Enter,    nullptr },
			{ "Up/Down",        0,              Qt::Key_Up,       nullptr },
			{ "Up/Down",        0,              Qt::Key_Down,     nullptr },
			{ "Ctrl+PgUp/PgDn", int(Qt::CTRL),  Qt::Key_PageUp,   nullptr },
			{ "Ctrl+PgUp/PgDn", int(Qt::CTRL),  Qt::Key_PageDown, nullptr },
			{ "F6",             0,              Qt::Key_F6,       nullptr },
			{ "F10",            0,              Qt::Key_F10,      nullptr },
			{ "Ctrl+A/E",       int(Qt::CTRL),  Qt::Key_A,        nullptr },
			{ "Ctrl+A/E",       int(Qt::CTRL),  Qt::Key_E,        nullptr },
			{ "Ctrl+K/U",       int(Qt::CTRL),  Qt::Key_K,        nullptr },
			{ "Ctrl+K/U",       int(Qt::CTRL),  Qt::Key_U,        nullptr },
			{ "Ctrl+W/D",       int(Qt::CTRL),  Qt::Key_W,        nullptr },
			{ "Ctrl+W/D",       int(Qt::CTRL),  Qt::Key_D,        nullptr },
			{ "Menu/Shift+F10", int(Qt::SHIFT), Qt::Key_F10,      nullptr },
			// THE ONE ROW THAT IS NOT A CHORD, and it is a family rather
			// than an omission: the letter is whatever a tab, a mnemonic
			// or a buddy label happens to carry, so there is no fixed
			// QKeySequence for a claim to be compared against.
			// Qtty::mnemonic_conflicts() answers it per letter, in more
			// detail than one row could hold.
			{ "Alt+letter",     0,              0,
			  "a family and not a chord -- mnemonic_conflicts() answers it" },
		};
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(24, 4));
		auto *sink = new QPushButton(QStringLiteral("Go"), &host);
		sink->setGeometry(0, 0, 8 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		// ONE CHORD AT A TIME, so the row that comes back is the row that
		// chord took and not whichever the walk reached first.
		const auto shadowed_by = [&host](int mod, int key) {
			auto *sc = new QShortcut(
			    QKeySequence(QKeyCombination(Qt::KeyboardModifiers(mod),
			                                 Qt::Key(key)).toCombined()),
			    &host);
			sc->setObjectName(QStringLiteral("the taker"));
			QCoreApplication::processEvents();
			QStringList rows;
			for (const auto &t : conventions_shadowed(&host)) rows << t.first;
			delete sc;
			QCoreApplication::processEvents();
			return rows;
		};
		QStringList unplaced, unreported, families;
		for (const auto &row : keyboard_conventions_help()) {
			int named = 0;
			for (const auto &p : placed) {
				if (row.first != QLatin1String(p.shown)) continue;
				++named;
				if (p.why) {
					if (!families.contains(row.first)) families << row.first;
					continue;
				}
				if (!shadowed_by(p.mod, p.key).contains(row.first))
					unreported << QStringLiteral("%1 (key %2)")
					                  .arg(row.first).arg(p.key);
			}
			if (named == 0) unplaced << row.first;
		}
		if (!unplaced.isEmpty() || !unreported.isEmpty())
			printf("info: rows this check cannot place [%s]; placed rows the "
			       "report did not name [%s]\n",
			       qPrintable(unplaced.join(QStringLiteral(", "))),
			       qPrintable(unreported.join(QStringLiteral(", "))));
		CHECK(unplaced.isEmpty(),
		      "every row the conventions list shows is placed -- a chord a "
		      "window can take back, or a recorded reason it has none");
		CHECK(unreported.isEmpty(),
		      "and a window binding a placed row's chord is told so, which "
		      "is what stops the table behind the report losing a row the "
		      "list still shows");
		QStringList by_family;
		by_family << QStringLiteral("Alt+letter");
		CHECK(families == by_family,
		      "and exactly one row is a family rather than a chord -- "
		      "Alt+letter, whose letter mnemonic_conflicts() answers for");
		set_keyboard_conventions(false);
	}

	// ---- Enter and Up/Down, the two rows the report could not see --------
	//
	// The reason recorded for leaving them out was that they "answer only
	// where the focused widget ignored the key, so a widget that wants them
	// is not shadowing a convention". True, and about a different mechanism:
	// conventions_shadowed() reports SHORTCUT CLAIMS, not widgets accepting
	// keys, and match_shortcut() runs before deliver_key() ever does. So an
	// application binding Return takes Enter away from the focused button
	// before the widget is offered anything -- exactly what binding Ctrl+K
	// does to the readline kill, and the same reason would have excluded
	// Ctrl+A/E, which answer only where a caret is and are reported.
	//
	// Asserted against the KEYS and not against the report, which is 8.187's
	// own rule: a check on the report alone would pass just as loudly if
	// nothing had been shadowed.
	{
		set_keyboard_conventions(true);
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(24, 6));
		auto *first = new QPushButton(QStringLiteral("One"), &host);
		first->setGeometry(0, 0, 8 * cw, ch);
		auto *second = new QPushButton(QStringLiteral("Two"), &host);
		second->setGeometry(0, ch, 8 * cw, ch);
		int sent = 0, pressed = 0, previous = 0;
		QObject::connect(first, &QPushButton::clicked, [&] { ++pressed; });
		auto *send = new QAction(QStringLiteral("Send"), &host);
		send->setShortcut(QKeySequence(Qt::Key_Return));
		QObject::connect(send, &QAction::triggered, [&] { ++sent; });
		host.addAction(send);
		auto *prev = new QAction(QStringLiteral("Previous"), &host);
		prev->setShortcut(QKeySequence(Qt::Key_Up));
		QObject::connect(prev, &QAction::triggered, [&] { ++previous; });
		host.addAction(prev);
		host.show();
		QCoreApplication::processEvents();

		QStringList rows;
		for (const auto &t : conventions_shadowed(&host)) rows << t.first;
		printf("info: with Return and Up bound, the report names [%s]\n",
		       qPrintable(rows.join(QStringLiteral(", "))));
		CHECK(rows.contains(QStringLiteral("Enter")),
		      "an application that binds a shortcut to Return is told it "
		      "has taken the Enter convention back");
		CHECK(rows.contains(QStringLiteral("Up/Down")),
		      "and one that binds Up is told the same about the row that "
		      "moves between controls");

		InputRouter er(&host);
		first->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		er.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(sent == 1 && pressed == 0,
		      "and the key really is gone: Enter fires the application's "
		      "action and the button that had focus is not clicked");
		QWidget *const before = host.focusWidget();
		er.on_key({Qt::Key_Up, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(previous == 1 && host.focusWidget() == before,
		      "as is Up, which answers the application rather than moving "
		      "the focus to another control");
		set_keyboard_conventions(false);
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
		// AND WHAT THE ARGUMENT DECIDES is at the top of this suite, not
		// here beside the sink it belongs to. Two of those assertions are
		// about the record before anything has written it, and this line
		// is a write -- so they had to go in front of it. 8.251.

		// The caret blink, which starts when a key is TYPED and not when a
		// field is merely focused. Qt flashes a caret every cursorFlashTime,
		// and on a terminal the caret is the TERMINAL's own cursor -- so
		// every repaint the blink causes is a frame nobody can see.
		//
		// Measured on the chat example, one keystroke and then nothing
		// touched for three seconds:
		//
		//     +   5.1 ms  234 bytes   the keystroke
		//     + 481.5 ms   33 bytes   ESC[23;3H ESC[0m SPACE ...
		//     + 953.1 ms   38 bytes   ESC[23;3H ESC[0m ESC[40m SPACE ...
		//     +1430.6 ms   33 bytes
		//
		// on for ever at half the flash interval, about 75 bytes a second.
		//
		// The control is the first assertion and it is the whole reason this
		// is three checks rather than one: it fires the blink deliberately,
		// so if Qt ever stops blinking here the pair goes red and says the
		// policy is no longer needed, instead of passing quietly for a
		// reason nobody meant.
		{
			struct Painting : QObject {
				int n = 0;
				bool eventFilter(QObject *, QEvent *e) override {
					if (e->type() == QEvent::UpdateRequest) ++n;
					return false;
				}
			};
			const auto spin = [](int ms) {
				QElapsedTimer t; t.start();
				while (t.elapsed() < ms) {
					QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
				}
			};
			const int qtty_set = QApplication::cursorFlashTime();

			QWidget box;
			box.setAttribute(Qt::WA_DontShowOnScreen);
			box.resize(GridMetrics::cells(20, 3));
			QLineEdit *field = new QLineEdit(&box);
			box.show();
			QCoreApplication::processEvents();
			field->setFocus();
			InputRouter typed(&box);

			QApplication::setCursorFlashTime(120);
			typed.on_key({Qt::Key_X, QStringLiteral("x"), false, false, false});
			spin(60);
			Painting flashing;
			qApp->installEventFilter(&flashing);
			spin(400);
			qApp->removeEventFilter(&flashing);

			QApplication::setCursorFlashTime(0);
			typed.on_key({Qt::Key_Y, QStringLiteral("y"), false, false, false});
			spin(60);
			Painting steady;
			qApp->installEventFilter(&steady);
			spin(400);
			qApp->removeEventFilter(&steady);

			QApplication::setCursorFlashTime(qtty_set);
			CHECK(flashing.n > 0,
			      "a flash time makes Qt repaint a typed-into field on its"
			      " own, which is the blink this policy is about");
			CHECK(steady.n == 0,
			      "and a flash time of zero stops it");
			CHECK(qtty_set == 0,
			      "so setup() pins it to zero, the caret on a terminal being"
			      " the terminal's own cursor");
		}

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


	// ---- where QCursor::pos() says the pointer is ----
	{
		// QCursor::pos() was not unanswered here, it was answered WRONG,
		// which is a worse failure than the absent Enter and Leave above:
		// nothing reports it, and the application looks broken rather than
		// unsupported. Qt's offscreen plugin installs a platform cursor
		// constructed at (10, 10), and QCursor::pos() returns the platform
		// cursor's position whenever there is one -- so it answered cell
		// (1, 0) for the life of every program, while the router knew
		// exactly where the terminal had reported a press.
		//
		// The cost is that two idiomatic spellings disagreed:
		// menu.exec(QCursor::pos()) opened in the terminal's top-left
		// corner and menu.exec(event->globalPos()) opened correctly, in
		// code a reader would call the same thing.
		struct Watch : QWidget {
			int moves = 0, menus = 0;
			QPoint ctx;
			using QWidget::QWidget;
			void mouseMoveEvent(QMouseEvent *) override { ++moves; }
			void contextMenuEvent(QContextMenuEvent *e) override {
				++menus;
				ctx = e->globalPos();
				e->accept();
			}
		};
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(40, 12));
		auto *w = new Watch(&win);
		w->setFocusPolicy(Qt::StrongFocus);
		w->setGeometry(GridMetrics::cw() * 4, GridMetrics::ch() * 2,
		               GridMetrics::cw() * 10, GridMetrics::ch() * 3);
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);

		// The starting constant, put back by hand and asserted -- so this
		// cannot pass because some earlier fixture happened to leave the
		// right number there, and so the next line is measuring a move
		// rather than a coincidence.
		QCursor::setPos(10, 10);
		CHECK(QCursor::pos() == QPoint(10, 10),
		      "the offscreen platform starts its cursor at (10, 10), which "
		      "is what every program used to read for ever");

		MouseEvent m;
		m.cell = QPoint(20, 5);
		m.button = 0;
		m.press = true;
		r.on_mouse(m);
		QCoreApplication::processEvents();
		const QPoint want(20 * GridMetrics::cw() + GridMetrics::cw() / 2,
		                  5 * GridMetrics::ch() + GridMetrics::ch() / 2);
		CHECK(QCursor::pos() == want,
		      "a mouse report moves the pointer QCursor::pos() reports, so "
		      "menu.exec(QCursor::pos()) opens where the user clicked");

		// THE RELATIONSHIP, which is the assertion that matters: the two
		// ways an application can ask must not give different answers. A
		// check pinning either value alone goes stale the moment the
		// synthesis changes; this one cannot, because it compares them.
		w->setFocus();
		set_focus_widget(win.focusWidget());
		w->menus = 0;
		w->moves = 0;
		r.on_key({Qt::Key_Menu, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(w->menus == 1 && w->ctx == QCursor::pos(),
		      "and a keyboard context menu leaves QCursor::pos() agreeing "
		      "with the globalPos() the event carried");

		// THE CONTROL, and it guards a real hazard rather than a
		// hypothetical one. QOffscreenCursor::setPos() synthesises an
		// enter, a leave and a MouseMove into whatever EXPOSED window
		// contains the point -- which would fight the router's own hover
		// bookkeeping. Nothing here is exposed, every top level carrying
		// WA_DontShowOnScreen, so nothing is found and nothing is sent.
		// Measured that way before the write was relied on; asserted here
		// so a later change that exposes a window cannot make the router
		// start inventing pointer motion in silence.
		CHECK(w->moves == 0,
		      "and recording the position raises no mouse motion of its "
		      "own, the window it would be delivered to being unexposed");
	}

	// ---- the keypad's Enter is an Enter ----
	{
		// The router has accepted Qt::Key_Enter beside Qt::Key_Return
		// since both branches were written -- in the button activation
		// and in navigates_a_list() -- and NOTHING PRODUCED IT. The
		// shipped decoder turns a carriage return into Key_Return and had
		// no mapping for the keypad at all, so the second spelling was an
		// accepted value no test exercised and no backend could send.
		//
		// That is an interface promise with nothing holding it: collapse
		// the two cases to one and every check stays green, while every
		// adopter's keypad Enter stops working. The decoder produces the
		// spelling now, and this is the half that says the router still
		// means it.
		struct Counting : QPushButton {
			int clicks = 0;
			using QPushButton::QPushButton;
		};
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(30, 6));
		auto *b = new Counting(&win);
		b->setText(QStringLiteral("Go"));
		b->setGeometry(0, 0, GridMetrics::cw() * 8, GridMetrics::ch());
		b->setFocusPolicy(Qt::StrongFocus);
		QObject::connect(b, &QPushButton::clicked, [b] { ++b->clicks; });
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);
		b->setFocus();
		set_focus_widget(win.focusWidget());

		// WITH THE CONVENTIONS ON, because activating the focused control
		// with Enter is one of them -- the guide lists it in the
		// convention table and not in the table of what Qt already does,
		// and Qt really does nothing here: a plain QPushButton outside a
		// dialog does not answer Return at all. The first version of this
		// fixture left them off and measured 0 clicks for both spellings,
		// which is the fixture failing to reach the branch rather than
		// the branch being wrong.
		const bool had_conventions = keyboard_conventions();
		set_keyboard_conventions(true);

		r.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		const int after_return = b->clicks;
		r.on_key({Qt::Key_Enter, QString(), false, false, false});
		QCoreApplication::processEvents();
		// The RELATIONSHIP rather than either count: what is being
		// asserted is that the two spellings do the same thing, which
		// stays true if the activation rule changes and would need
		// rewriting if it were pinned to a number.
		set_keyboard_conventions(had_conventions);
		CHECK(after_return == 1 && b->clicks == 2,
		      "the keypad's Enter activates the focused button exactly as"
		      " the main keyboard's Return does");
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

	// ---- and the limit that rule has, pinned so it cannot move unseen ----
	//
	// A QKeySequenceEdit -- the field a shortcut dialog asks you to type
	// into -- cannot record the quit chord. It carries no
	// WA_InputMethodEnabled, honestly, since it takes raw key presses
	// rather than input-method text, so the exemption above does not
	// cover it and Ctrl+C closes the window while the user is telling the
	// program which key to use.
	//
	// TWO FIXES WERE BUILT AND BOTH WERE REFUSED BY THIS SUITE, which is
	// why this is a pinned limit rather than a fix. Qt's own
	// QEvent::ShortcutOverride asks a widget "do you want this key", and
	// it separates the cases perfectly on paper -- a QKeySequenceEdit
	// wants Ctrl+C and Ctrl+Q, a QLineEdit wants only Ctrl+C, a button and
	// a list want neither. Asked before every interception it reddened
	// six checks, because qtty's readline conventions exist precisely to
	// beat a widget's own bindings: Ctrl+A is start-of-line here and a
	// field wants it for select-all. Asked only for the quit chord it
	// still reddened three, because a field wants every printable key --
	// so a quit key spelled `q` would be typed rather than obeyed -- and
	// because a read-only field accepts the copy override although the
	// check three lines above says it must quit.
	//
	// The remedy that works today is the application's and is measured
	// below: drop the quit keys while the recorder has focus. 8.269.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *keys = new QKeySequenceEdit;
		v->addWidget(keys);
		v->addWidget(new QPushButton(QStringLiteral("After")));
		win.resize(GridMetrics::cells(30, 4));
		win.show();
		QCoreApplication::processEvents();
		Qtty::InputRouter router(&win);
		const auto record_ctrl_c = [&] {
			keys->clear();
			keys->setFocus();
			Qtty::set_focus_widget(keys);
			QCoreApplication::processEvents();
			router.on_key({Qt::Key_C, QStringLiteral("c"), true, false, false});
			QCoreApplication::processEvents();
		};
		record_ctrl_c();
		CHECK(keys->keySequence().isEmpty() && !win.isVisible(),
		      "a shortcut recorder cannot record the quit chord: the "
		      "window closes instead, which is the limit rather than the "
		      "intention");
		win.show();
		QCoreApplication::processEvents();
		Qtty::set_quit_keys({});
		record_ctrl_c();
		CHECK(keys->keySequence().toString() == QStringLiteral("Ctrl+C")
		      && win.isVisible(),
		      "and dropping the quit keys is the remedy an application "
		      "has today, measured rather than suggested");
		// PUT THEM BACK, and prove it: this is process-wide state and
		// every check after this one would run without a quit key. The
		// third observation is the restore's own evidence.
		Qtty::set_quit_keys({{Qt::Key_C, QString(), true, false, false},
			                 {Qt::Key_D, QString(), true, false, false}});
		record_ctrl_c();
		CHECK(keys->keySequence().isEmpty() && !win.isVisible(),
		      "and the default pair is back, which is asserted rather than "
		      "assumed because the call before it is process-wide");
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

		// CTRL+TAB IS NOT QTTY'S, and the `!k.ctrl` in that condition is
		// what leaves it alone. Branch coverage said the condition had
		// never been false: every Tab this suite sent was unmodified, so
		// nothing pinned the carve-out.
		//
		// It matters because Ctrl+Tab is conventionally "the next tab" or
		// "the next window", and `windows.h` says qtty deliberately binds
		// no shortcut of its own and an application must bind its own
		// route. A qtty that moved focus on Ctrl+Tab would be taking a
		// key the application is entitled to -- and taking it invisibly,
		// since focus moving looks like the application doing something.
		b1->setFocus();
		Qtty::set_focus_widget(win.focusWidget());
		router.on_key({ Qt::Key_Tab, QStringLiteral("\t"), true, false, false });
		CHECK(win.focusWidget() == b1,
		      "Ctrl+Tab does not cycle focus, so an application can bind it "
		      "to whatever a Ctrl+Tab means there");
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

	// A menu TALLER than the terminal, which section 7 calls its hardest case
	// and which runtime.h used to call a trap.
	//
	// Qt will not paginate one: the offscreen QScreen is 800x800, so it never
	// decides the menu is too tall. Section 7's scrolling carries it instead,
	// and nothing checked that it did -- the popup layer's follow_focus() is
	// the only thing standing between a keyboard user and a menu whose bottom
	// half does not exist.
	//
	// Three assertions, because being able to SEE the selection, being able
	// to REACH the last item, and being able to GET OUT are three different
	// promises and a menu can keep any two.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(30, 24));
		auto *btn = new QPushButton(QStringLiteral("Menu"), &win);
		win.show();
		QCoreApplication::processEvents();
		InputRouter router(&win);
		Compositor comp(&win, &router);

		QMenu menu(&win);
		for (int i = 0; i < 40; ++i)
			menu.addAction(QStringLiteral("item-%1").arg(i, 2, 10, QChar('0')));
		menu.popup(btn->mapToGlobal(QPoint(0, 0)));
		QCoreApplication::processEvents();

		const auto rows_shown = [&] {
			CellBuffer b(30, 24);
			comp.compose(b);
			QStringList seen;
			for (int y = 0; y < b.rows(); ++y) {
				QString row;
				for (int x = 0; x < b.cols(); ++x) row += b.at(x, y).ch;
				const int at = row.indexOf(QStringLiteral("item-"));
				if (at >= 0) seen << row.mid(at, 7);
			}
			return seen;
		};
		const QStringList before = rows_shown();
		const auto press = [&](int n) {
			for (int i = 0; i < n; ++i) {
				router.on_key({Qt::Key_Down, QString(), false, false, false});
				QCoreApplication::processEvents();
			}
		};
		press(35);
		const QStringList mid = rows_shown();
		press(5);
		const QStringList end = rows_shown();

		CHECK(menu.height() > 24 * GridMetrics::ch() && !before.isEmpty(),
		      "a forty-item menu is taller than the terminal, Qt having no"
		      " screen small enough to paginate it against");
		CHECK(mid != before && !mid.contains(QStringLiteral("item-00")),
		      "and it scrolls to keep the selection on screen rather than"
		      " leaving the caret below the last row");
		CHECK(end.contains(QStringLiteral("item-39")),
		      "so the last item is reachable from the keyboard");
		router.on_key({Qt::Key_Escape, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(!menu.isVisible(),
		      "and Escape closes it, which is how a user leaves a menu --"
		      " Tab, which does not, is not the question");
		GridGuard::reset();
	}

	// The keys a terminal user presses in a list without thinking, delivered
	// through the router to a focused view.
	//
	// None of this is qtty's behaviour -- it is Qt's, and the point of the
	// check is that qtty's routing does not eat it. A library that
	// intercepts keys to implement its own conventions is one keystroke away
	// from swallowing Home, End, the paging keys or the letter that drives
	// type-ahead, and nothing else here would notice: the guide promises
	// these work unmodified.
	//
	// Focused with setFocus() rather than set_focus_widget(), which is what
	// an application does and what makes the record follow: qtty's focus
	// record is re-read from Qt's on the next key.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(24, 10));
		auto *list = new QListWidget(&win);
		list->setGeometry(0, 0, 24 * GridMetrics::cw(), 10 * GridMetrics::ch());
		const char *const names[] = {"alpha", "bravo", "charlie", "delta",
			                         "echo", "foxtrot", "golf", "hotel",
			                         "india", "juliet", "kilo", "lima",
			                         "mike", "november", "oscar"};
		for (const char *n : names) list->addItem(QString::fromLatin1(n));
		win.show();
		QCoreApplication::processEvents();
		InputRouter router(&win);
		list->setCurrentRow(0);
		list->setFocus();
		QCoreApplication::processEvents();
		const auto press = [&](int key, const QString &t = QString()) {
			router.on_key({key, t, false, false, false});
			QCoreApplication::processEvents();
			return list->currentRow();
		};
		const int down = press(Qt::Key_Down);
		const int end = press(Qt::Key_End);
		const int home = press(Qt::Key_Home);
		const int paged = press(Qt::Key_PageDown);
		const int typed = press(Qt::Key_K, QStringLiteral("k"));
		// A TREE as well, because the guide's rows say "list or tree" and a
		// check over a list alone leaves half of each row unheld -- which is
		// that page's own maintenance rule, not a nicety.
		QWidget twin;
		twin.setAttribute(Qt::WA_DontShowOnScreen);
		twin.resize(GridMetrics::cells(24, 10));
		auto *tree = new QTreeWidget(&twin);
		tree->setGeometry(0, 0, 24 * GridMetrics::cw(), 10 * GridMetrics::ch());
		tree->setColumnCount(1);
		tree->setHeaderHidden(true);
		for (const char *n : names)
			new QTreeWidgetItem(tree, QStringList{QString::fromLatin1(n)});
		twin.show();
		QCoreApplication::processEvents();
		InputRouter tr(&twin);
		tree->setCurrentItem(tree->topLevelItem(0));
		tree->setFocus();
		QCoreApplication::processEvents();
		const auto tpress = [&](int key, const QString &t = QString()) {
			tr.on_key({key, t, false, false, false});
			QCoreApplication::processEvents();
			return tree->indexOfTopLevelItem(tree->currentItem());
		};
		const int t_end = tpress(Qt::Key_End);
		const int t_home = tpress(Qt::Key_Home);
		const int t_typed = tpress(Qt::Key_K, QStringLiteral("k"));
		CHECK(t_end == 14 && t_home == 0 && t_typed == 10,
		      "and a tree answers the same keys, which the guide's rows claim"
		      " and a list-only check could not hold");
		CHECK(down == 1 && end == 14 && home == 0,
		      "Down, End and Home reach a focused list through the router");
		CHECK(paged > 1 && paged < 14,
		      "and PageDown moves a screenful rather than to an end");
		CHECK(typed == 10,
		      "and a typed letter still reaches the view's own type-ahead,"
		      " which is how a terminal user finds an item by name");
		GridGuard::reset();
	}

	// Readline editing in a text field, which is the opt-in bundle's newest
	// member and the one with a conflict in it.
	//
	// Ctrl+E, Ctrl+K, Ctrl+U and Ctrl+W are free -- Qt gives them no meaning
	// -- and Ctrl+A is not: it is Select All in every Qt program and
	// start-of-line in every shell, and a terminal application is both. The
	// answer is the Ctrl+C precedent's, decided PER WIDGET on the same
	// attribute: a text field gets the shell's meaning, everything else
	// keeps Qt's.
	//
	// Four assertions, because the feature is four claims and a fix could
	// satisfy any three: the chords edit, the conflict resolves the right
	// way in a text field, it resolves the OTHER way outside one, and the
	// whole thing stays behind the opt-in.
	{
		const auto field = [](bool conventions, int key) {
			set_keyboard_conventions(conventions);
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(24, 4));
			auto *edit = new QLineEdit(&win);
			edit->setGeometry(0, 0, 24 * GridMetrics::cw(), GridMetrics::ch());
			edit->setText(QStringLiteral("hello brave world"));
			win.show();
			QCoreApplication::processEvents();
			InputRouter r(&win);
			edit->setFocus();
			edit->setCursorPosition(11);
			QCoreApplication::processEvents();
			r.on_key({key, QString(), true, false, false});
			QCoreApplication::processEvents();
			return QStringList{edit->text(),
				               QString::number(edit->cursorPosition()),
				               edit->selectedText()};
		};
		const QStringList home = field(true, Qt::Key_A);
		const QStringList end = field(true, Qt::Key_E);
		const QStringList kill = field(true, Qt::Key_K);
		const QStringList back = field(true, Qt::Key_U);
		const QStringList word = field(true, Qt::Key_W);
		CHECK(home.value(1) == QStringLiteral("0")
		          && end.value(1) == QStringLiteral("17"),
		      "Ctrl+A and Ctrl+E move to the start and end of a line, as a"
		      " shell user's fingers expect");
		CHECK(kill.value(0) == QStringLiteral("hello brave")
		          && back.value(0) == QStringLiteral(" world")
		          && word.value(0) == QStringLiteral("hello  world"),
		      "and Ctrl+K, Ctrl+U and Ctrl+W kill forward, back and by word");
		// The conflict, both ways round.
		const QStringList off = field(false, Qt::Key_A);
		CHECK(off.value(2) == QStringLiteral("hello brave world"),
		      "while with the conventions off Ctrl+A is Qt's Select All,"
		      " the whole bundle being opt-in");
		{
			set_keyboard_conventions(true);
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(24, 8));
			auto *list = new QListWidget(&win);
			list->setGeometry(0, 0, 24 * GridMetrics::cw(), 8 * GridMetrics::ch());
			list->setSelectionMode(QAbstractItemView::ExtendedSelection);
			for (int i = 0; i < 5; ++i)
				list->addItem(QStringLiteral("row%1").arg(i));
			win.show();
			QCoreApplication::processEvents();
			InputRouter r(&win);
			list->setCurrentRow(0);
			list->setFocus();
			QCoreApplication::processEvents();
			r.on_key({Qt::Key_A, QString(), true, false, false});
			QCoreApplication::processEvents();
			CHECK(list->selectedItems().size() == 5,
			      "and outside a text field it is still Select All, which is"
			      " what deciding per widget buys");
		}
		// Ctrl+D, which is the chord three things claimed until the
		// terminal-lost seam took one of them away.
		//
		// With the conventions on and a caret in a field it deletes forward,
		// as readline's does. With them off it is a quit key and must NOT
		// delete -- a chord that neither quits nor deletes would be worse
		// than either, which is why the quit-key loop gives it up on exactly
		// the condition that brings it here.
		//
		// What this canNOT check is the quit itself: QCoreApplication::quit()
		// is a no-op with no main loop running, and the suite has none, so
		// aboutToQuit never fires -- measured. The four quit cases are in
		// project.md, taken from an application with a real exec().
		{
			// The widget is WATCHED rather than the text compared, and the
			// sabotage run is why. "The text did not change" cannot tell a
			// chord consumed by the quit-key loop from one delivered to a
			// QLineEdit that has no use for it -- both leave the text alone
			// -- so an entry that stopped the loop giving Ctrl+D up came back
			// "the named check PASSED against broken code". Counting what
			// ARRIVES separates them.
			struct Watch : QObject {
				int ctrl_d = 0;
				bool eventFilter(QObject *, QEvent *e) override {
					if (e->type() == QEvent::KeyPress) {
						auto *k = static_cast<QKeyEvent *>(e);
						if (k->key() == Qt::Key_D
						    && (k->modifiers() & Qt::ControlModifier))
							++ctrl_d;
					}
					return false;
				}
			};
			const auto after_ctrl_d = [](bool conventions, int *arrived) {
				set_keyboard_conventions(conventions);
				QWidget win;
				win.setAttribute(Qt::WA_DontShowOnScreen);
				win.resize(GridMetrics::cells(24, 4));
				auto *edit = new QLineEdit(&win);
				edit->setGeometry(0, 0, 24 * GridMetrics::cw(), GridMetrics::ch());
				edit->setText(QStringLiteral("hello brave world"));
				win.show();
				QCoreApplication::processEvents();
				InputRouter r(&win);
				Watch watch;
				edit->installEventFilter(&watch);
				edit->setFocus();
				edit->setCursorPosition(5);
				QCoreApplication::processEvents();
				r.on_key({Qt::Key_D, QString(), true, false, false});
				QCoreApplication::processEvents();
				*arrived = watch.ctrl_d;
				return edit->text();
			};
			int on_arrived = -1, off_arrived = -1;
			const QString on_text = after_ctrl_d(true, &on_arrived);
			const QString off_text = after_ctrl_d(false, &off_arrived);
			CHECK(on_text == QStringLiteral("hellobrave world") && on_arrived == 0,
			      "Ctrl+D deletes the character under the caret, which the"
			      " terminal-lost seam had to land before it could");
			CHECK(off_text == QStringLiteral("hello brave world")
			          && off_arrived == 0,
			      "and with the conventions off the quit-key loop still eats"
			      " it, rather than passing a chord that does nothing");
		}

		// And the same chords in a MULTI-LINE editor, where the motions they
		// are built from are line-relative. Home and End are the start and
		// end of the LINE in Qt, not of the document, so kill-to-end must
		// stop at the newline -- a version built on Ctrl+Shift+End would eat
		// the rest of the document and pass every single-line check.
		//
		// Both editors, because they are different classes reaching the same
		// motions, and the claim that these behave identically in all three
		// text widgets was written before it had been measured.
		{
			set_keyboard_conventions(true);
			const auto killed = [](bool plain) {
				QWidget win;
				win.setAttribute(Qt::WA_DontShowOnScreen);
				win.resize(GridMetrics::cells(30, 6));
				QWidget *ed = plain ? static_cast<QWidget *>(new QPlainTextEdit(&win))
				                    : static_cast<QWidget *>(new QTextEdit(&win));
				ed->setGeometry(0, 0, 30 * GridMetrics::cw(), 5 * GridMetrics::ch());
				const QString body =
				    QStringLiteral("first line\nsecond brave line\nthird");
				if (plain) static_cast<QPlainTextEdit *>(ed)->setPlainText(body);
				else static_cast<QTextEdit *>(ed)->setPlainText(body);
				win.show();
				QCoreApplication::processEvents();
				InputRouter r(&win);
				ed->setFocus();
				const int at = QStringLiteral("first line\nsecond brave").size();
				if (plain) {
					QTextCursor tc = static_cast<QPlainTextEdit *>(ed)->textCursor();
					tc.setPosition(at);
					static_cast<QPlainTextEdit *>(ed)->setTextCursor(tc);
				} else {
					QTextCursor tc = static_cast<QTextEdit *>(ed)->textCursor();
					tc.setPosition(at);
					static_cast<QTextEdit *>(ed)->setTextCursor(tc);
				}
				QCoreApplication::processEvents();
				r.on_key({Qt::Key_K, QString(), true, false, false});
				QCoreApplication::processEvents();
				return plain ? static_cast<QPlainTextEdit *>(ed)->toPlainText()
				             : static_cast<QTextEdit *>(ed)->toPlainText();
			};
			const QString want =
			    QStringLiteral("first line\nsecond brave\nthird");
			CHECK(killed(true) == want && killed(false) == want,
			      "kill-to-end stops at the end of the LINE in both"
			      " multi-line editors, leaving the rest of the document");
		}
		set_keyboard_conventions(false);
		GridGuard::reset();
	}

	// ---- focus after a widget that had it is hidden ------------------------
	//
	// Qt moves focus on when a focused widget is hidden, and cannot do it
	// here: QWidget::setVisible(false) and QAbstractItemView::closeEditor()
	// are both gated on hasFocus(), which reads Qt's own focus_widget and is
	// set only for an ACTIVE window. No qtty window activates. So the
	// runtime does what the platform layer would have done, and the third
	// check below is what keeps it from doing more than that.
	{
		const bool had_conv = keyboard_conventions();
		set_keyboard_conventions(true);
		QWidget win;
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		auto *field = new QLineEdit(&win);
		field->setObjectName(QStringLiteral("field"));
		auto *table = new QTableWidget(2, 2, &win);
		for (int row = 0; row < 2; ++row)
			for (int col = 0; col < 2; ++col)
				table->setItem(row, col, new QTableWidgetItem(
				    QStringLiteral("r%1c%2").arg(row).arg(col)));
		auto *stack = new QStackedWidget(&win);
		auto *page0 = new QLineEdit(QStringLiteral("page0"));
		auto *page1 = new QLineEdit(QStringLiteral("page1"));
		stack->addWidget(page0);
		stack->addWidget(page1);
		v->addWidget(field);
		v->addWidget(table);
		v->addWidget(stack);
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(40, 16));
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);

		// AN ITEM VIEW'S EDITOR. The cost of getting this wrong is one dead
		// keystroke after EVERY commit and every cancel: the value reaches
		// the model, the editor goes, the window is left with no focus
		// widget at all, and the next F2 reaches the window and does
		// nothing. Asserted as the user meets it -- a second F2 with no
		// key in between has to open an editor again.
		table->setFocus();
		set_focus_widget(table);
		table->setCurrentCell(0, 0);
		r.on_key({Qt::Key_F2, QString(), false, false, false});
		r.on_key({0, QStringLiteral("Z"), false, false, false});
		r.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		const bool committed = table->item(0, 0)->text() == QStringLiteral("Z");
		const bool back = Qtty::focusWidget() == table
		               && win.focusWidget() == table;
		CHECK(committed && back,
		      "committing an item view's edit hands focus back to the view,"
		      " which Qt cannot do here because no window activates");
		r.on_key({Qt::Key_F2, QString(), false, false, false});
		QCoreApplication::processEvents();
		const bool editing_again =
		    Qtty::focusWidget() && Qtty::focusWidget() != table
		    && table->isAncestorOf(Qtty::focusWidget());
		CHECK(editing_again,
		      "so the next F2 edits rather than being spent putting the focus"
		      " back");
		r.on_key({Qt::Key_Escape, QString(), false, false, false});
		QCoreApplication::processEvents();

		// A STACKED WIDGET, which is the control and is why the repair is
		// deferred to the end of the event rather than run inside the hide.
		// Qt hides the old page BEFORE showing the new one, so a repair that
		// acts immediately walks straight past the page about to appear and
		// lands on the first tab stop in the window -- measured, this check
		// read `field`, and a user changing page would find focus at the top
		// of the form. Asked once the event has finished, the condition is
		// the one actually meant: the window has no focus widget at all.
		page0->setFocus();
		set_focus_widget(page0);
		stack->setCurrentIndex(1);
		QCoreApplication::processEvents();
		CHECK(Qtty::focusWidget() == page1,
		      "changing a stacked page leaves focus on the page that arrived,"
		      " the repair standing aside where Qt has already chosen");
		set_keyboard_conventions(had_conv);
		GridGuard::reset();
	}

	// ---- quit keys an application using exec() can actually change --------
	//
	// InputRouter::set_quit_keys() has always existed and was reachable by
	// nobody: exec() builds the router on its own stack and hands it out
	// to no one, so an application could not change Ctrl-C and Ctrl-D
	// without reimplementing exec() entire. project.md 0e carried that as
	// the last of the adoption decisions.
	//
	// Qtty::set_quit_keys() is the free function, shaped after
	// capabilities() and shell_out(), with one difference those two do not
	// need: an application picks its quit keys while building its window,
	// long before any router exists, so this sets the default every router
	// starts from AND reaches the ones already running.
	{
		// MATCHED THE WAY THE EVENT IS SPELLED. A control chord arrives
		// with a key code; an ordinary letter arrives as TEXT with a code
		// of zero, which is what the backend produces and what this
		// block now sends. A watch that only knew key codes counted
		// nothing for the letters and read as two failures.
		struct Watch : QObject {
			int arrived = 0;
			int key = 0;
			QString text;
			bool eventFilter(QObject *, QEvent *e) override {
				if (e->type() != QEvent::KeyPress) return false;
				auto *k = static_cast<QKeyEvent *>(e);
				if (!text.isEmpty() ? k->text() == text : k->key() == key)
					++arrived;
				return false;
			}
		};
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(20, 4));
		auto *button = new QPushButton(QStringLiteral("Go"), &host);
		button->setGeometry(0, 0, 8 * cw, ch);
		host.show();
		QCoreApplication::processEvents();

		// BEFORE the router exists, which is when an application would
		// call it.
		const KeyEvent quit_q{0, QStringLiteral("q"), false, false, false};
		Qtty::set_quit_keys({quit_q});
		InputRouter qk(&host);
		Watch on_q, on_c;
		on_q.text = QStringLiteral("q");
		on_c.key = Qt::Key_C;
		button->installEventFilter(&on_q);
		button->installEventFilter(&on_c);
		button->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		// THE SHAPE A TERMINAL SENDS, which is text-only: the backend
		// decodes an ordinary character to qt_key 0 with the letter in
		// text. The first version of this check fed {Qt::Key_Q, "q"} and
		// passed against an event the backend never produces -- the
		// exec() check in suite_backend caught it, and matching on text
		// is what the router learned from it.
		qk.on_key({0, QStringLiteral("q"), false, false, false});
		qk.on_key({Qt::Key_C, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(on_q.arrived == 0 && on_c.arrived == 1,
		      "a router built after Qtty::set_quit_keys() quits on the key "
		      "the application named and lets Ctrl+C through, which no "
		      "application using exec() could ask for before");

		// AND ONLY THAT LETTER, which is the half a code-only match
		// cannot give. A terminal sends every ordinary character with a
		// key code of zero, so comparing codes alone makes a spec of
		// {0, "q"} equal to EVERY printable key -- the whole keyboard
		// quitting, quietly. Naming q must not do that.
		Watch on_z;
		on_z.text = QStringLiteral("z");
		button->installEventFilter(&on_z);
		qk.on_key({0, QStringLiteral("z"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(on_z.arrived == 1,
		      "and only that letter: every other printable key still "
		      "reaches the widget, which a match on key codes alone "
		      "cannot manage when a terminal sends them all as zero");

		// AND WHILE ONE IS RUNNING, for the application that changes them
		// from a slot.
		const KeyEvent quit_x{0, QStringLiteral("x"), false, false, false};
		Qtty::set_quit_keys({quit_x});
		Watch on_x;
		on_x.text = QStringLiteral("x");
		button->installEventFilter(&on_x);
		qk.on_key({0, QStringLiteral("x"), false, false, false});
		qk.on_key({0, QStringLiteral("q"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(on_x.arrived == 0 && on_q.arrived == 1,
		      "and the router already running follows it, so a program can "
		      "change them from a slot as well as before the run");

		// AN EMPTY LIST is a documented answer rather than an oversight:
		// no quit key at all, and the application owes its user a way out.
		Qtty::set_quit_keys({});
		qk.on_key({Qt::Key_C, QString(), true, false, false});
		qk.on_key({0, QStringLiteral("x"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(on_c.arrived == 2 && on_x.arrived == 1,
		      "and an empty list leaves nothing quitting at all, which the "
		      "header promises and an application with its own Quit item "
		      "wants");

		// PUT BACK, because this default is process-wide and every router
		// built after this block would inherit it -- the fixture reaching
		// outside itself, which this suite has been bitten by before.
		const KeyEvent ctrl_c{Qt::Key_C, QString(), true, false, false};
		const KeyEvent ctrl_d{Qt::Key_D, QString(), true, false, false};
		Qtty::set_quit_keys({ctrl_c, ctrl_d});
		QWidget after;
		after.setAttribute(Qt::WA_DontShowOnScreen);
		after.resize(GridMetrics::cells(20, 4));
		auto *later = new QPushButton(QStringLiteral("Go"), &after);
		later->setGeometry(0, 0, 8 * cw, ch);
		after.show();
		QCoreApplication::processEvents();
		InputRouter fresh(&after);
		Watch on_c2;
		on_c2.key = Qt::Key_C;
		later->installEventFilter(&on_c2);
		later->setFocus();
		set_focus_widget(after.focusWidget());
		QCoreApplication::processEvents();
		fresh.on_key({Qt::Key_C, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(on_c2.arrived == 0,
		      "and a router built afterwards is back to Ctrl+C and Ctrl+D, "
		      "so this check cannot leave the rest of the suite holding "
		      "its quit keys");
		GridGuard::reset();
	}

	// ---- and a quit key is a WHOLE chord, shift included ------------------
	//
	// KeyEvent carries three modifiers and the loop compared two. A quit
	// key given as a bare code therefore fired on the shifted chord as
	// well, and the pair that costs most is this library's own: an
	// application naming F10 also quit on Shift+F10, which is the
	// context-menu key qtty answers to everywhere else. Reachable from a
	// real terminal rather than only through the sink -- the backend
	// decodes CSI 21;2~ as F10 with the shift bit set.
	//
	// STRICT, rather than reading an unset `shift` as "either". KeyEvent
	// has one bool per modifier and no way to spell "unspecified", the
	// other two are already compared exactly, and match_shortcut() builds
	// its QKeySequence from all three -- so a spec is a whole chord to
	// every other reader of the type. The loose reading would also have to
	// answer for Ctrl+Shift+C, which on a terminal is copy.
	//
	// Observed as the key REACHING the widget, which is the same trick the
	// block above uses: a suite cannot watch the application quit, and a
	// chord the quit loop takes never reaches a widget at all.
	{
		struct Watch : QObject {
			int keys = 0, menus = 0;
			int key = 0;
			bool eventFilter(QObject *, QEvent *e) override {
				if (e->type() == QEvent::ContextMenu) { ++menus; return false; }
				if (e->type() != QEvent::KeyPress) return false;
				if (static_cast<QKeyEvent *>(e)->key() == key) ++keys;
				return false;
			}
		};
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(20, 4));
		auto *button = new QPushButton(QStringLiteral("Go"), &host);
		button->setGeometry(0, 0, 8 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		Qtty::set_quit_keys(
		    {KeyEvent{Qt::Key_F10, QString(), false, false, false}});
		InputRouter sk(&host);
		Watch on_f10;
		on_f10.key = Qt::Key_F10;
		button->installEventFilter(&on_f10);
		button->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		sk.on_key({Qt::Key_F10, QString(), false, false, true});
		QCoreApplication::processEvents();
		CHECK(on_f10.keys == 1 && on_f10.menus == 1,
		      "a quit key named by key code is not fired by the shifted "
		      "chord, so an application that quits on F10 still gets its "
		      "context menu from Shift+F10");
		// A DELTA, not the running total. Asserting `keys == 1` here would
		// have failed against the unfixed code for the check above's
		// reason rather than its own -- a control that goes red with the
		// thing it controls for says nothing.
		const int after_shift = on_f10.keys;
		sk.on_key({Qt::Key_F10, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(on_f10.keys == after_shift,
		      "while the unshifted key is still the quit key, the chord "
		      "being matched whole rather than merely loosened");

		// AND THE DEFAULT, which is what most programs will meet. Put back
		// first, so what is measured afterwards is the shipped pair.
		const KeyEvent ctrl_c{Qt::Key_C, QString(), true, false, false};
		const KeyEvent ctrl_d{Qt::Key_D, QString(), true, false, false};
		Qtty::set_quit_keys({ctrl_c, ctrl_d});
		Watch on_c;
		on_c.key = Qt::Key_C;
		button->installEventFilter(&on_c);
		sk.on_key({Qt::Key_C, QString(), true, false, true});
		QCoreApplication::processEvents();
		CHECK(on_c.keys == 1,
		      "the default quit key does not answer Ctrl+Shift+C either, "
		      "which is a terminal's copy chord and not a request to end "
		      "the program");
		const int after_ctrl_shift = on_c.keys;
		sk.on_key({Qt::Key_C, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(on_c.keys == after_ctrl_shift,
		      "and plain Ctrl+C still quits, so the strictness has taken "
		      "nothing away from the default");
		GridGuard::reset();
	}

	// ---- and so is a readline chord --------------------------------------
	//
	// readline_edit() read two of the three modifiers for the same reason
	// and with the same consequence: Ctrl+Shift+K killed to the end of the
	// line. The conventions bind Ctrl+K and the help list says Ctrl+K, and
	// a Ctrl+Shift chord on a terminal usually belongs to the terminal.
	//
	// It is in this change rather than the next because the quit fix would
	// otherwise break an invariant readline_edit() states about itself: the
	// quit loop gives Ctrl+D up on exactly the condition that brings it
	// here, so the two cannot disagree about who has the chord. With the
	// quit loop strict and this one loose, Ctrl+Shift+D would be nobody's
	// quit key and still readline's delete.
	{
		set_keyboard_conventions(true);
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 4));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 24 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter rr(&host);
		field->setText(QStringLiteral("hello brave world"));
		field->setCursorPosition(11);
		field->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		rr.on_key({Qt::Key_K, QString(), true, false, true});
		QCoreApplication::processEvents();
		CHECK(field->text() == QStringLiteral("hello brave world"),
		      "Ctrl+Shift+K is not the readline kill: the convention is "
		      "Ctrl+K, and a shifted control chord on a terminal is the "
		      "terminal's own");
		// RESET, so that the control below is answered by the chord and
		// not by there being nothing left to kill. Against the unfixed
		// code the line above is already "hello brave", and a kill at the
		// end of a line takes nothing -- the control would have passed
		// without the binding working at all.
		field->setText(QStringLiteral("hello brave world"));
		field->setCursorPosition(11);
		QCoreApplication::processEvents();
		rr.on_key({Qt::Key_K, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(field->text() == QStringLiteral("hello brave"),
		      "while Ctrl+K itself still kills to the end of the line, the "
		      "chord being read whole rather than narrowed to nothing");
		set_keyboard_conventions(false);
		GridGuard::reset();
	}

	// ---- Ctrl+A in a list, which the guide promises is Select All ----------
	//
	// The fourth reader of WA_InputMethodEnabled, and the one that broke a
	// promise the guide makes in as many words: Ctrl+A is decided per
	// widget, start of line where a caret is and Qt's Select All in a
	// list. For the commonest list in Qt it was neither -- a QListView
	// over QStringListModel has editable items, so it carries the
	// attribute, so the readline chords fired on it. Measured, five rows
	// with the current one at the bottom:
	//
	//     QListWidget, items not editable   Ctrl+A selected 5
	//     QListView + QStringListModel      Ctrl+A selected 1, current
	//                                       row jumped 4 -> 0
	//
	// The existing check for this promise uses a QListWidget, whose items
	// are NOT editable by default -- so it passed throughout, on the one
	// list shape where the attribute is absent.
	{
		const bool had_conv = keyboard_conventions();
		set_keyboard_conventions(true);
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 12));
		auto *view = new QListView(&host);
		auto *model = new QStringListModel(&host);
		model->setStringList({QStringLiteral("v0"), QStringLiteral("v1"),
			                  QStringLiteral("v2"), QStringLiteral("v3"),
			                  QStringLiteral("v4")});
		view->setModel(model);
		view->setSelectionMode(QAbstractItemView::ExtendedSelection);
		view->setGeometry(0, 0, 18 * cw, 5 * ch);
		auto *field = new QLineEdit(QStringLiteral("some text"), &host);
		field->setGeometry(0, 6 * ch, 18 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter sr(&host);
		view->setCurrentIndex(model->index(4, 0));
		view->clearSelection();
		view->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		sr.on_key({Qt::Key_A, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(view->selectionModel()->selectedIndexes().size() == 5
		      && view->currentIndex().row() == 4,
		      "Ctrl+A in a list over an ordinary editable model is Qt's "
		      "Select All, which the guide promises and the readline "
		      "chords were taking");
		field->setFocus();
		set_focus_widget(host.focusWidget());
		field->setCursorPosition(field->text().size());
		QCoreApplication::processEvents();
		sr.on_key({Qt::Key_A, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(field->cursorPosition() == 0,
		      "and in a field it is still start of line, the chord being "
		      "decided by where the caret is rather than by an attribute a "
		      "model sets");
		set_keyboard_conventions(had_conv);
		GridGuard::reset();
	}

	// ---- the quit key an item view was eating ------------------------------
	//
	// The quit-key loop gives Ctrl+C up where a caret sits in a field,
	// because that is where a user means copy, and its comment says a form
	// is mostly buttons, lists and tables and that Ctrl+C quits from all of
	// them. It did not: an item view acquires WA_InputMethodEnabled as soon
	// as its current item is editable -- the default for QStringListModel,
	// QStandardItemModel and every QTableWidget item -- so the hatch was
	// taken by the commonest list in Qt, and with no Copy action bound the
	// key did NOTHING. Measured with a real exec(), before the fix:
	//
	//     a push button       Ctrl+C quits
	//     a read-only list    Ctrl+C quits
	//     the DEFAULT list    nothing at all
	//     a line edit         copy, as intended
	//
	// The quit cannot be observed here -- QCoreApplication::quit() is a
	// no-op with no main loop and this suite has none -- so the check
	// counts what ARRIVES, which is the same instrument the Ctrl+D case
	// below uses and for the same reason: a key the loop consumed never
	// reaches the widget, and one it gave up does.
	{
		struct Watch : QObject {
			int ctrl_c = 0;
			bool eventFilter(QObject *, QEvent *e) override {
				if (e->type() == QEvent::KeyPress) {
					auto *k = static_cast<QKeyEvent *>(e);
					if (k->key() == Qt::Key_C
					    && (k->modifiers() & Qt::ControlModifier))
						++ctrl_c;
				}
				return false;
			}
		};
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 10));
		auto *list = new QListView(&host);
		auto *model = new QStringListModel(
		    {QStringLiteral("one"), QStringLiteral("two")}, &host);
		list->setModel(model);
		list->setGeometry(0, 0, 18 * cw, 4 * ch);
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 5 * ch, 18 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter qr(&host);
		Watch on_list, on_field;
		list->installEventFilter(&on_list);
		field->installEventFilter(&on_field);

		list->setCurrentIndex(model->index(0, 0));
		list->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		qr.on_key({Qt::Key_C, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(on_list.ctrl_c == 0,
		      "Ctrl+C on a list over an ordinary editable model is taken by "
		      "the quit keys, which the hatch for a caret in a field had "
		      "been swallowing");
		// AND THE WINDOW IS SHUT, which is new: a quit key asks the
		// window to close now (8.227) rather than ending the loop
		// behind its back. With no main loop the old qApp->quit() did
		// nothing at all and this fixture never noticed; close() does
		// something, so the rest of the block has to put the window
		// back before using it again.
		CHECK(!host.isVisible(),
		      "and the window it asked to close is shut, a quit key being "
		      "the close gesture a terminal has rather than a way round "
		      "the application");
		host.show();
		QCoreApplication::processEvents();

		field->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		qr.on_key({Qt::Key_C, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(on_field.ctrl_c == 1,
		      "while a caret in a field still takes it, which is the whole "
		      "of what that hatch is for");

		// AND WHILE THE LIST IS EDITING, where there IS a caret: the editor
		// is the key target and carries the attribute on its own account,
		// so copy wins again without the view ever being asked.
		list->setFocus();
		set_focus_widget(host.focusWidget());
		list->edit(model->index(0, 0));
		QCoreApplication::processEvents();
		set_focus_widget(host.focusWidget());
		QWidget *editor = nullptr;
		for (QWidget *c : list->viewport()->findChildren<QWidget *>())
			if (c->isVisible()) editor = c;
		Watch on_editor;
		if (editor) editor->installEventFilter(&on_editor);
		qr.on_key({Qt::Key_C, QString(), true, false, false});
		QCoreApplication::processEvents();
		CHECK(editor && on_editor.ctrl_c == 1,
		      "and an open editor in that same list takes it too, a caret "
		      "being the test rather than the widget it sits in");
		qr.on_key({Qt::Key_Escape, QString(), false, false, false});
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- What's This, which works and nothing said so ----------------------
	//
	// Practice 7 says a tool tip's words cannot be got at from a keyboard
	// here, and that is true and measured: no QEvent::ToolTip is ever
	// raised. What nobody had tried is the other half of Qt's help
	// machinery. `QWhatsThis` works end to end -- Shift+F1 opens What's
	// This mode, the FOCUSED widget's whatsThis() appears in a Qt::ToolTip
	// window, which is a window kind this library draws, and Escape or the
	// next key closes it again.
	//
	// That makes setWhatsThis() the remedy practice 7 was missing, and it
	// is worth pinning because nothing else in the tree exercises Qt's
	// help mode at all.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 12));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 20 * cw, ch);
		field->setToolTip(QStringLiteral("host name"));
		field->setWhatsThis(QStringLiteral("FIELDHELP the host to reach"));
		auto *label = new QLabel(QStringLiteral("Server"), &host);
		label->setGeometry(0, 2 * ch, 20 * cw, ch);
		label->setToolTip(QStringLiteral("which server"));
		label->setWhatsThis(QStringLiteral("LABELHELP not reachable"));
		host.show();
		QCoreApplication::processEvents();
		InputRouter hr(&host);
		Compositor hc(&host, &hr);
		field->setFocus();
		set_focus_widget(field);
		const auto frame = [&] {
			QCoreApplication::processEvents();
			CellBuffer b(40, 12);
			hc.compose(b);
			return b.to_text();
		};
		const bool quiet_at_rest =
		    !frame().contains(QStringLiteral("FIELDHELP"));
		hr.on_key({Qt::Key_F1, QString(), false, false, true});
		const QString shown = frame();
		CHECK(quiet_at_rest && shown.contains(QStringLiteral("FIELDHELP")),
		      "Shift+F1 shows the focused widget's whatsThis, which is a "
		      "keyboard route to an explanation and the one practice 7 "
		      "was missing");
		CHECK(!shown.contains(QStringLiteral("LABELHELP")),
		      "and only the focused one's, so a label's whatsThis is as "
		      "far out of reach as its tool tip");
		hr.on_key({Qt::Key_Escape, QString(), false, false, false});
		CHECK(!frame().contains(QStringLiteral("FIELDHELP"))
		      && hr.popups().isEmpty(),
		      "and Escape takes it away again, which every layer here "
		      "owes a user");

		// THE REPORT FOLLOWS THE ROUTE. A tip whose words Shift+F1 can
		// reach is no longer the only way to them; one on a widget no key
		// can focus still is.
		const QVector<QWidget *> tips = Qtty::hover_only(&host);
		CHECK(!tips.contains(field) && tips.contains(label),
		      "so hover_only() spares a focusable widget that also has a "
		      "whatsThis and still names one that cannot be focused at "
		      "all");
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- the cursor belongs to a caret, not to a focus ---------------------
	//
	// An item view acquires WA_InputMethodEnabled as soon as its current
	// item is editable, which QStringListModel, QStandardItemModel and
	// every QTableWidget item are by default. Two things keyed off that
	// attribute and both were wrong for a view: the compositor placed the
	// terminal cursor on it -- measured, cell (4,2) inside the first row
	// of a focused list, with no caret anywhere -- and focus_invisible()
	// exempted it, letting the commonest list in Qt out of the report.
	//
	// Nothing is lost by refusing the view the cursor, and that is the
	// pair this checks: while the view IS editing, the editor is a child
	// and the focus widget, so it takes the cursor on its own account.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 10));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 12 * cw, ch);
		auto *list = new QListView(&host);
		auto *model = new QStringListModel(
		    {QStringLiteral("one"), QStringLiteral("two")}, &host);
		list->setModel(model);
		list->setGeometry(0, 2 * ch, 12 * cw, 3 * ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter ir(&host);
		Compositor ic(&host, &ir);
		list->setCurrentIndex(model->index(0, 0));
		list->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		CellBuffer ib(30, 10);
		ic.compose(ib);
		const bool none_on_the_list = !ic.cursor_cell().has_value();
		list->edit(model->index(0, 0));
		QCoreApplication::processEvents();
		set_focus_widget(host.focusWidget());
		ic.compose(ib);
		const auto editing = ic.cursor_cell();
		const bool on_the_editor =
		    editing.has_value()
		    && qobject_cast<QLineEdit *>(Qtty::focusWidget()) != nullptr
		    && Qtty::focusWidget() != field;
		CHECK(none_on_the_list && on_the_editor,
		      "a focused list carries no terminal cursor and its editor "
		      "does, a caret being a place to type rather than a way to "
		      "say what has the focus");
		ir.on_key({Qt::Key_Escape, QString(), false, false, false});
		QCoreApplication::processEvents();
		set_focus_widget(host.focusWidget());
		ic.compose(ib);
		CHECK(!ic.cursor_cell().has_value(),
		      "and it goes away again when the editor closes, rather than "
		      "staying where a caret used to be");

		// AND THE REPORT EXAMINES SUCH A LIST NOW, which has to be
		// asked with a list that WOULD be named -- the first version
		// asserted that an examined list came out clean, and a list the
		// report skips also comes out clean, so it passed with the
		// exemption widened back. The harness said so.
		//
		// So: frameless, an editable model, and a delegate that paints
		// straight over its rect and draws no panel. Nothing about it
		// changes when the focus arrives, and the only question left is
		// whether the report looks.
		struct Bare : QStyledItemDelegate {
			using QStyledItemDelegate::QStyledItemDelegate;
			void paint(QPainter *p, const QStyleOptionViewItem &o,
			           const QModelIndex &ix) const override {
				p->drawText(o.rect.x(),
				            o.rect.y() + QFontMetrics(o.font).ascent(),
				            ix.data().toString());
			}
		};
		list->setFrameShape(QFrame::NoFrame);
		list->setItemDelegate(new Bare(&host));
		field->setFocus();
		set_focus_widget(host.focusWidget());
		QCoreApplication::processEvents();
		CHECK(Qtty::focus_invisible(&host).contains(list),
		      "and a list over an editable model is examined by "
		      "focus_invisible rather than exempted for an attribute "
		      "meant for text widgets");
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- the project's own example, held to its own guide ------------------
	//
	// The guide tells an application to assert seven of the eight reports
	// empty. Nothing had ever asked them of the application THIS project
	// ships, and the answer was not empty: `focus_invisible()` named the
	// chat window's message list. Measured -- the frame with the focus on
	// the list and the frame with it in the input box were byte-identical,
	// so a keyboard user tabbing into the conversation had nothing on the
	// screen telling them they had arrived.
	//
	// The cause is general and is worth more than the example: a custom
	// QStyledItemDelegate that paints its own rows draws no panel, and the
	// panel is where selection and the focus mark live. The remedy is one
	// line of ordinary Qt -- ask the style to draw the panel, which is what
	// Qt's own documentation tells a delegate to do -- and the example
	// carries it now, still with zero qtty types in it.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		ChatWindow chat;
		chat.setAttribute(Qt::WA_DontShowOnScreen);
		chat.show();
		QCoreApplication::processEvents();
		InputRouter chat_router(&chat);
		QCoreApplication::processEvents();
		CHECK(Qtty::focus_invisible(&chat).isEmpty(),
		      "the project's own example shows where the focus is, which "
		      "it did not until the report was asked of it");
		// THE AGGREGATE, and this block is why it exists. It used to
		// write the list out, and the list went stale: sheet_styled()
		// was added to the page as its ninth question and this block was
		// not grown with it, so the example asserted eight of nine while
		// the page said nine. The tenth would have done it again.
		//
		// audit() cannot drift that way -- it enumerates in the file a
		// new question is added to anyway -- so the one program this
		// project ships as the worked answer now asserts what the guide
		// tells an application to assert, in the spelling the guide
		// gives it.
		const QVector<QPair<QString, QString>> report = Qtty::audit(&chat);
		if (!report.isEmpty())
			for (const auto &row : report)
				fprintf(stderr, "chat audit: %s -- %s\n",
				        row.first.toUtf8().constData(),
				        row.second.toUtf8().constData());
		CHECK(report.isEmpty(),
		      "and passes every report the guide tells an application to "
		      "assert empty, which is the guide run against the program "
		      "this project ships rather than read");
		CHECK(!Qtty::keyboard_reachable(&chat).isEmpty(),
		      "with something reachable to begin with, an empty window "
		      "passing every report by having no controls at all");
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- a custom delegate's focus mark ------------------------------------
	//
	// The library half of the finding above. A delegate that draws its
	// panel through the style gets the row's marks; one that paints
	// straight over the rect gets nothing, and on a frameless view -- which
	// a grid-disciplined application wants, a frame costing a row and a
	// column -- there is then nothing that changes when the focus arrives.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		struct Bare : QStyledItemDelegate {
			bool ask_the_style = true;
			using QStyledItemDelegate::QStyledItemDelegate;
			void paint(QPainter *p, const QStyleOptionViewItem &o,
			           const QModelIndex &ix) const override {
				if (ask_the_style) {
					QStyle *st = o.widget ? o.widget->style()
					                      : QApplication::style();
					st->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, p,
					                  o.widget);
				}
				p->drawText(o.rect.x(),
				            o.rect.y() + QFontMetrics(o.font).ascent(),
				            ix.data().toString());
			}
		};
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 10));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 12 * cw, ch);
		auto *view = new QListView(&host);
		view->setFrameShape(QFrame::NoFrame);
		// A READ-ONLY model, and that is the fixture's load-bearing
		// choice. QStringListModel's items are editable by default, and
		// an item view whose current item can be edited acquires
		// WA_InputMethodEnabled -- which focus_invisible() skips, on the
		// grounds that a widget taking text shows focus with the
		// terminal's cursor. So the first version of this control asked
		// the report about a widget the report deliberately does not
		// examine, and read the silence as a pass. Recorded as a
		// separate finding in project.md 8.220; here the model simply
		// refuses editing, which is what a message list is anyway.
		struct ReadOnly : QStringListModel {
			using QStringListModel::QStringListModel;
			Qt::ItemFlags flags(const QModelIndex &ix) const override {
				return QStringListModel::flags(ix) & ~Qt::ItemIsEditable;
			}
		};
		auto *model = new ReadOnly(
		    QStringList{QStringLiteral("one"), QStringLiteral("two")}, &host);
		view->setModel(model);
		auto *mine = new Bare(&host);
		view->setItemDelegate(mine);
		view->setGeometry(0, 2 * ch, 12 * cw, 4 * ch);
		view->setCurrentIndex(model->index(0, 0));
		host.show();
		QCoreApplication::processEvents();
		InputRouter dr(&host);
		QCoreApplication::processEvents();
		CHECK(Qtty::focus_invisible(&host).isEmpty(),
		      "a custom delegate that draws its panel through the style "
		      "shows the focus on a frameless view, the style knowing what "
		      "the delegate cannot");
		mine->ask_the_style = false;
		QCoreApplication::processEvents();
		CHECK(Qtty::focus_invisible(&host).contains(view),
		      "and one that paints straight over the rect is named, which "
		      "is the report earning its place on the very widget kind it "
		      "could not see before");
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- a long-lived program ---------------------------------------------
	//
	// Every fixture here opens a layer, asks one question and exits. A
	// terminal program runs for an afternoon and opens thousands, and
	// this library keeps registries keyed by window -- the strip's
	// remembered order, the popup stack, the modal placements, the focus
	// pointer. A prune forgotten in any of them is invisible in a suite
	// that opens one dialog and unbounded in a program that opens ten
	// thousand.
	//
	// Measured while writing this, over 200 dialogs, 200 menus and 200
	// secondary windows: RSS 38.6 MB at startup and 40.7 MB at the end,
	// flat between 50 and 200 dialogs, with every registry back to
	// empty. The state is asserted rather than the memory, because a
	// byte count is a machine's answer and a registry that is not empty
	// is the fault itself.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 12));
		auto *field = new QLineEdit(&host);
		field->setGeometry(0, 0, 20 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter cr(&host);
		Compositor cc(&host, &cr);
		field->setFocus();
		set_focus_widget(field);

		for (int i = 0; i < 60; ++i) {
			QDialog dlg(&host);
			dlg.setWindowTitle(QStringLiteral("D%1").arg(i));
			dlg.setModal(true);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(16, 4));
			dlg.show();
			QCoreApplication::processEvents();
			CellBuffer b(40, 12);
			cc.compose(b);
			dlg.close();
			QCoreApplication::processEvents();

			QMenu m(&host);
			m.addAction(QStringLiteral("Item"));
			m.popup(QPoint(0, 0));
			QCoreApplication::processEvents();
			cc.compose(b);
			m.close();
			QCoreApplication::processEvents();

			auto *extra = new QWidget;
			extra->setAttribute(Qt::WA_DontShowOnScreen);
			extra->setWindowTitle(QStringLiteral("W%1").arg(i));
			extra->resize(GridMetrics::cells(8, 3));
			extra->show();
			QCoreApplication::processEvents();
			cc.compose(b);
			delete extra;
			QCoreApplication::processEvents();
		}
		CellBuffer last(40, 12);
		cc.compose(last);
		CHECK(Qtty::window_tabs().isEmpty() && cr.popups().isEmpty()
		      && Qtty::focusWidget() == field,
		      "sixty rounds of a dialog, a menu and a window leave every "
		      "registry empty and the focus where it was, which is what a "
		      "program running all afternoon asks of them");
		CHECK(!last.to_text().contains(QStringLiteral("D59"))
		      && !last.to_text().contains(QStringLiteral("W59")),
		      "and the frame afterwards holds none of them, so a layer "
		      "that closed is drawn nowhere");
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- arrangements, rather than parts ----------------------------------
	//
	// 8.216 found that a change to how every modal is drawn moved no
	// fixture at all, because not one of them draws a modal over a
	// window. The fixtures here are made of the PARTS -- a line edit, a
	// button, a list -- and what an application builds is arrangements of
	// them. These are the three commonest that nothing had composed, and
	// all three work; they are pinned rather than reported, since an
	// arrangement that works today is one a layering change can break
	// tomorrow without touching any part.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(60, 16));
		auto *behind = new QLabel(QStringLiteral("BEHIND"), &host);
		behind->setGeometry(0, 12 * ch, 10 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter ar(&host);
		Compositor ac(&host, &ar);

		// A COMBO BOX'S DROPDOWN INSIDE A MODAL, which is a popup over a
		// modal over a window: three layers, and the popup has to draw
		// above the dialog AND take the keys from it.
		QDialog dlg(&host);
		dlg.setWindowTitle(QStringLiteral("Settings"));
		auto *combo = new QComboBox(&dlg);
		combo->addItems({QStringLiteral("Light"), QStringLiteral("Dark")});
		combo->setGeometry(0, 2 * ch, 16 * cw, ch);
		dlg.setModal(true);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		dlg.resize(GridMetrics::cells(24, 5));
		dlg.move(2 * cw, 2 * ch);
		dlg.show();
		QCoreApplication::processEvents();
		combo->setFocus();
		set_focus_widget(dlg.focusWidget());
		combo->showPopup();
		QCoreApplication::processEvents();
		CellBuffer ab(60, 16);
		ac.compose(ab);
		const QString three = ab.to_text();
		CHECK(three.contains(QStringLiteral("Dark"))
		      && three.contains(QStringLiteral("┌─ Settings"))
		      && three.contains(QStringLiteral("BEHIND")),
		      "a combo box's dropdown inside a modal draws above the "
		      "dialog and the dialog above the window, three layers in "
		      "one frame");
		ar.on_key({Qt::Key_Down, QString(), false, false, false});
		ar.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(combo->currentText() == QStringLiteral("Dark")
		      && ar.popups().isEmpty(),
		      "and the dropdown owns the keys while it is up, so Down and "
		      "Return choose in it rather than in the dialog behind");

		// A MODAL OVER A MODAL, which every application with a confirm
		// step builds and nothing here had.
		QDialog second(&dlg);
		second.setWindowTitle(QStringLiteral("Confirm"));
		auto *yes = new QPushButton(QStringLiteral("Yes"), &second);
		yes->setGeometry(0, ch, 8 * cw, ch);
		second.setModal(true);
		second.setAttribute(Qt::WA_DontShowOnScreen);
		second.resize(GridMetrics::cells(18, 4));
		// PLACED, so the inner box does not land on the outer's title
		// row -- which it did in the first version of this check, and
		// the failure was the fixture rather than the layering. A
		// parented dialog keeps its own geometry: the compositor
		// centres only the ones nobody placed.
		second.move(8 * cw, 8 * ch);
		second.show();
		QCoreApplication::processEvents();
		int said_yes = 0;
		QObject::connect(yes, &QPushButton::clicked, [&] { ++said_yes; });
		CellBuffer nb(60, 16);
		ac.compose(nb);
		const QString nested = nb.to_text();
		CHECK(nested.contains(QStringLiteral("┌─ Confirm"))
		      && nested.contains(QStringLiteral("┌─ Settings")),
		      "a modal opened from a modal draws over it, each with a box "
		      "of its own");
		yes->setFocus();
		set_focus_widget(second.focusWidget());
		ar.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(said_yes == 1,
		      "and the inner one owns the keyboard, which is what a modal "
		      "over a modal is for");
		second.close();
		dlg.close();
		QCoreApplication::processEvents();

		// A WHOLE QMainWindow: menu bar, toolbar, dock, central widget
		// and status bar in one frame. Every part is tested; the
		// arrangement is what an application actually is.
		//
		// The host goes away first. A second top-level is a strip window
		// and only the current one is drawn -- 8.209's own behaviour,
		// which the first version of this check walked straight into and
		// read as a QMainWindow that would not compose.
		host.hide();
		QCoreApplication::processEvents();
		QMainWindow full;
		full.setAttribute(Qt::WA_DontShowOnScreen);
		full.resize(GridMetrics::cells(60, 16));
		full.menuBar()->addMenu(QStringLiteral("&File"))
		    ->addAction(QStringLiteral("&Open"));
		full.addToolBar(QStringLiteral("Main"))
		    ->addAction(QStringLiteral("&Save"));
		auto *dock = new QDockWidget(QStringLiteral("Outline"), &full);
		auto *tree = new QTreeWidget;
		tree->setHeaderLabel(QStringLiteral("Sections"));
		tree->addTopLevelItem(new QTreeWidgetItem(
		    QStringList(QStringLiteral("part 0"))));
		dock->setWidget(tree);
		full.addDockWidget(Qt::LeftDockWidgetArea, dock);
		full.setCentralWidget(new QPlainTextEdit(
		    QStringLiteral("the document")));
		full.statusBar()->showMessage(QStringLiteral("Ready"));
		full.show();
		QCoreApplication::processEvents();
		InputRouter fr2(&full);
		Compositor fc2(&full, &fr2);
		CellBuffer fb2(60, 16);
		fc2.compose(fb2);
		const QString whole = fb2.to_text();
		CHECK(whole.contains(QStringLiteral("File"))
		      && whole.contains(QStringLiteral("Save"))
		      && whole.contains(QStringLiteral("part 0"))
		      && whole.contains(QStringLiteral("the document"))
		      && whole.contains(QStringLiteral("Ready")),
		      "and a whole QMainWindow puts its menu bar, toolbar, dock, "
		      "central widget and status bar in one frame");
		full.hide();
		QCoreApplication::processEvents();
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- the box around a modal ------------------------------------------
	//
	// A desktop's window manager draws a dialog's frame and title; this
	// platform has nobody to draw either, and before this a modal's
	// contents read as extra columns of the window behind it -- measured,
	// "window row 1  Theme:" on one line, with no way to tell which half
	// was the dialog. setWindowTitle() was written nowhere at all.
	//
	// Only when it fits, which is the decision this implements: the ring
	// needs a row above and below and a column either side, and a
	// terminal without them is one where section 7 is already dropping
	// content to fit the dialog.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(50, 12));
		auto *behind = new QLabel(QStringLiteral("BEHIND"), &host);
		behind->setGeometry(0, 6 * ch, 10 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter br(&host);
		Compositor bc(&host, &br);

		QDialog dlg(&host);
		dlg.setWindowTitle(QStringLiteral("Preferences"));
		auto *inside = new QLabel(QStringLiteral("Theme:"), &dlg);
		inside->setGeometry(0, 0, 10 * cw, ch);
		dlg.setModal(true);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		dlg.resize(GridMetrics::cells(20, 4));
		dlg.show();
		QCoreApplication::processEvents();
		CellBuffer bb(50, 12);
		bc.compose(bb);
		const QString framed = bb.to_text();
		CHECK(framed.contains(QStringLiteral("┌─ Preferences "))
		      && framed.contains(QStringLiteral("│"))
		      && framed.contains(QStringLiteral("└")),
		      "a modal gets a box with its title in the top rule, which a "
		      "window manager draws on a desktop and nobody draws here");
		CHECK(framed.contains(QStringLiteral("Theme:"))
		      && framed.contains(QStringLiteral("BEHIND")),
		      "and the box costs the dialog none of its contents, nor the "
		      "window behind any of its own");

		// THE CRAMPED TERMINAL, where the ring does not fit. The dialog
		// still has to be drawn whole: chrome losing to content is the
		// whole point of deciding it this way round.
		//
		// TWO CELLS WIDER THAN THE DIALOG AND NO TALLER, which took a
		// failed sabotage to get right. With a buffer exactly the
		// dialog's size every ring cell falls outside it and writable()
		// clips them all, so the check passed with the fit test deleted
		// -- it was measuring the buffer's bounds rather than the test.
		// Here the ring's sides WOULD land inside and only the fit test
		// keeps them out, which is the discrimination the pair needs.
		CellBuffer tight(22, 4);
		bc.compose(tight);
		const QString small = tight.to_text();
		CHECK(!small.contains(QStringLiteral("│"))
		      && !small.contains(QStringLiteral("┌"))
		      && small.contains(QStringLiteral("Theme:")),
		      "while a terminal with no room for the ring gets no box and "
		      "keeps the dialog's own contents");

		// A DIALOG WITH NO TITLE still gets the box: the frame says where
		// the dialog is, which is worth having without a name on it.
		QDialog plain(&host);
		auto *word = new QLabel(QStringLiteral("Body"), &plain);
		word->setGeometry(0, 0, 8 * cw, ch);
		plain.setModal(true);
		plain.setAttribute(Qt::WA_DontShowOnScreen);
		plain.resize(GridMetrics::cells(12, 3));
		dlg.close();
		plain.show();
		QCoreApplication::processEvents();
		CellBuffer nb(50, 12);
		bc.compose(nb);
		CHECK(nb.to_text().contains(QStringLiteral("┌──"))
		      && nb.to_text().contains(QStringLiteral("Body")),
		      "and a dialog with no title gets the box without one, the "
		      "frame being what says where it is");
		plain.close();
		QCoreApplication::processEvents();
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- a file dialog, end to end, by keyboard alone ---------------------
	//
	// The biggest composite widget an ordinary application opens, and
	// nothing in this tree had ever opened one: a sidebar, a tree view
	// with a sortable header, a filename field, a filter combo and two
	// buttons, all inside a dialog. If it did not work here, every
	// application with an Open item would be unusable, and nobody would
	// have found out from a fixture built out of line edits and buttons.
	//
	// Driven the way a person drives it -- type a name, press Return --
	// and asserted on what the application receives, not on what the
	// widget looks like.
	{
		QTemporaryDir tmp;
		if (!tmp.isValid()) {
			printf("FAIL: could not make a directory for the file dialog\n");
			++fails;
		} else {
			for (const char *name : { "alpha.txt", "beta.txt" }) {
				QFile f(tmp.filePath(QString::fromLatin1(name)));
				f.open(QIODevice::WriteOnly);
				f.close();
			}
			QWidget host;
			host.setAttribute(Qt::WA_DontShowOnScreen);
			host.resize(GridMetrics::cells(80, 24));
			host.show();
			QCoreApplication::processEvents();
			InputRouter fr(&host);
			QFileDialog dlg(&host, QStringLiteral("Open"), tmp.path());
			dlg.setOption(QFileDialog::DontUseNativeDialog, true);
			dlg.setFileMode(QFileDialog::ExistingFile);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(60, 18));
			bool listed = false;
			QTimer::singleShot(0, &dlg, [&] {
				QCoreApplication::processEvents();
				Compositor fc(&host, &fr);
				// WAITED FOR, because QFileSystemModel fills itself on
				// another thread and a frame composed before it has
				// finished holds an empty list. The first version of
				// this check composed once and passed -- from THIS
				// directory, by the timing it happened to get; run from
				// elsewhere the same check failed, which is a fixture
				// that works for a reason it does not state. Bounded,
				// so a dialog that never lists anything fails rather
				// than hanging.
				for (int spin = 0; spin < 200 && !listed; ++spin) {
					QCoreApplication::processEvents();
					QThread::msleep(5);
					QCoreApplication::processEvents();
					CellBuffer probe(80, 24);
					fc.compose(probe);
					const QString seen = probe.to_text();
					listed = seen.contains(QStringLiteral("alpha.txt"))
					      && seen.contains(QStringLiteral("beta.txt"));
				}
				for (QChar c : QStringLiteral("alpha.txt"))
					fr.on_key({0, QString(c), false, false, false});
				QCoreApplication::processEvents();
				fr.on_key({Qt::Key_Return, QString(), false, false, false});
				QCoreApplication::processEvents();
				if (dlg.isVisible()) dlg.reject();
			});
			const int answer = dlg.exec();
			const QStringList picked = dlg.selectedFiles();
			CHECK(listed,
			      "a file dialog draws the directory it was opened on, "
			      "names and all -- the largest composite widget an "
			      "application opens, and the one nothing here had tried");
			CHECK(answer == QDialog::Accepted && picked.size() == 1
			      && picked.first().endsWith(QStringLiteral("alpha.txt")),
			      "and a name typed into it and accepted with Return "
			      "reaches the application, so Open works with no mouse "
			      "anywhere in it");
			GridGuard::reset();
		}
	}

	// ---- F10 into the menu bar -------------------------------------------
	//
	// A QMenuBar is reached by Alt, and Alt needs a mnemonic -- so a bar
	// whose titles carry no `&` had no keyboard route at all: measured,
	// F10 did nothing, the bar is Qt::NoFocus and no tab stop, and its
	// menus were reachable only by a pointer. In a library about the user
	// without one, that is the whole subject failing quietly.
	{
		const bool had_conv = keyboard_conventions();
		QMainWindow win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(40, 10));
		auto *field = new QLineEdit;
		win.setCentralWidget(field);
		// NO mnemonics anywhere, which is the case with no other way in.
		QMenu *file = win.menuBar()->addMenu(QStringLiteral("File"));
		QMenu *edit = win.menuBar()->addMenu(QStringLiteral("Edit"));
		int opened = 0, pasted = 0;
		QObject::connect(file->addAction(QStringLiteral("Open")),
		                 &QAction::triggered, [&] { ++opened; });
		QObject::connect(edit->addAction(QStringLiteral("Paste")),
		                 &QAction::triggered, [&] { ++pasted; });
		win.show();
		QCoreApplication::processEvents();
		InputRouter mr(&win);
		field->setFocus();
		set_focus_widget(field);

		set_keyboard_conventions(false);
		mr.on_key({Qt::Key_F10, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(mr.popups().isEmpty(),
		      "F10 does nothing with the conventions off, this library "
		      "binding no key of its own by default");

		set_keyboard_conventions(true);
		mr.on_key({Qt::Key_F10, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(mr.popups().size() == 1,
		      "and opens the first menu with them on, which is the only "
		      "way into a menu bar whose titles carry no mnemonic");
		mr.on_key({Qt::Key_Down, QString(), false, false, false});
		mr.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(opened == 1,
		      "so Down and Return reach the item, the open menu owning "
		      "the keys as any other popup does");

		// RIGHT walks the bar, which is what makes one key enough: the
		// first menu is a way in to all of them rather than to one.
		mr.on_key({Qt::Key_F10, QString(), false, false, false});
		QCoreApplication::processEvents();
		mr.on_key({Qt::Key_Right, QString(), false, false, false});
		QCoreApplication::processEvents();
		mr.on_key({Qt::Key_Down, QString(), false, false, false});
		mr.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(pasted == 1,
		      "and Right moves along the bar to the next menu, so one key "
		      "reaches every menu rather than the first");

		// Escape, the way back, which every layer in this library owes.
		mr.on_key({Qt::Key_F10, QString(), false, false, false});
		QCoreApplication::processEvents();
		const bool opened_again = mr.popups().size() == 1;
		mr.on_key({Qt::Key_Escape, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(opened_again && mr.popups().isEmpty(),
		      "and Escape closes it again, leaving the keys where they "
		      "were");
		// AND THE FOCUS COMES BACK, which it did not until this was
		// measured. Qt puts the bar into keyboard mode when a menu opens
		// from it and restores the previous focus when that mode ends --
		// reading QApplication::focusWidget(), which is null here, so it
		// restored nothing and the BAR kept the focus. Every later key
		// went there: measured, Shift+F10 after an Escape asked the menu
		// bar for a context menu instead of the field the user was in.
		CHECK(Qtty::focusWidget() == field,
		      "and the focus is back in the widget F10 took it from, "
		      "rather than left on the menu bar");

		// SHIFT+F10 is the context-menu key and must not have been
		// swallowed by the new branch -- the two differ by a modifier and
		// share a keycode, which is exactly how a binding eats its
		// neighbour.
		field->setContextMenuPolicy(Qt::CustomContextMenu);
		int asked = 0;
		QObject::connect(field, &QWidget::customContextMenuRequested,
		                 [&] { ++asked; });
		// KeyEvent spells its modifiers ctrl, alt, shift -- in that
		// order -- and the first version of this line put `true` in the
		// alt slot and asserted a context menu that was never asked for.
		// The code was right and the fixture was not, which is the
		// direction this suite catches most often.
		mr.on_key({Qt::Key_F10, QString(), false, false, true});
		QCoreApplication::processEvents();
		CHECK(asked == 1 && mr.popups().isEmpty(),
		      "while Shift+F10 still asks the focused widget for its "
		      "context menu, the two differing by one modifier");
		set_keyboard_conventions(had_conv);
		GridGuard::reset();
	}

	// ---- a keyboard grab, which an application asks for and this router
	// used to ignore. grabKeyboard() means every key goes to that widget
	// until it is released, and the offscreen plugin refuses the grab and
	// says so on stderr -- which is what made this look like a platform
	// limit. Qt records the grabber regardless, so the router can just ask.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 8));
		auto *focused = new QLineEdit(&host);
		focused->setGeometry(0, 0, 20 * cw, ch);
		auto *grabber = new QLineEdit(&host);
		grabber->setGeometry(0, 2 * ch, 20 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter gr(&host);
		focused->setFocus();
		set_focus_widget(focused);

		grabber->grabKeyboard();
		gr.on_key({0, QStringLiteral("k"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(grabber->text() == QStringLiteral("k") && focused->text().isEmpty(),
		      "a widget that grabbed the keyboard gets the keys, though "
		      "another widget has the focus");
		grabber->releaseKeyboard();
		gr.on_key({0, QStringLiteral("f"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(focused->text() == QStringLiteral("f")
		      && grabber->text() == QStringLiteral("k"),
		      "and releaseKeyboard() gives them back to the focused one");

		// PRECEDENCE, which is Qt's own: QApplication::notify() tests
		// popup mode BEFORE the grabber, so a menu opened by a program
		// that had grabbed the keyboard still answers its own keys. Get
		// this backwards and a grab taken for some other purpose makes
		// every menu in the program unusable.
		grabber->grabKeyboard();
		QMenu menu(&host);
		QAction *cut = menu.addAction(QStringLiteral("Cut"));
		int cuts = 0;
		QObject::connect(cut, &QAction::triggered, [&] { ++cuts; });
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		const QString before = grabber->text();
		gr.on_key({Qt::Key_Down, QString(), false, false, false});
		gr.on_key({Qt::Key_Return, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(cuts == 1 && grabber->text() == before,
		      "while an open menu still wins over a grab, which is the "
		      "order Qt itself applies");
		menu.close();
		grabber->releaseKeyboard();
		QCoreApplication::processEvents();

		// AND ONLY THIS ROUTER'S OWN. A grab taken in a window this
		// router does not own is not its business, the same ownership
		// rule the focus repairs follow.
		QWidget elsewhere;
		elsewhere.setAttribute(Qt::WA_DontShowOnScreen);
		elsewhere.resize(GridMetrics::cells(20, 4));
		auto *stranger = new QLineEdit(&elsewhere);
		stranger->setGeometry(0, 0, 10 * cw, ch);
		elsewhere.show();
		QCoreApplication::processEvents();
		stranger->grabKeyboard();
		focused->setText(QString());
		gr.on_key({0, QStringLiteral("m"), false, false, false});
		QCoreApplication::processEvents();
		CHECK(focused->text() == QStringLiteral("m")
		      && stranger->text().isEmpty(),
		      "and a grab in a window this router does not own takes "
		      "nothing from the window it does");
		stranger->releaseKeyboard();
		elsewhere.hide();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- a layer that opens with nothing focused ---------------------------
	//
	// Qt gives a window its first tab stop when the window ACTIVATES, and
	// none activates here. Measured before the repair: a QDialog holding
	// two fields and a default button came up with focusWidget() null,
	// key_target() the QDialog itself, and the keys a user types next
	// landing nowhere at all -- Tab rescued it and nothing else did.
	//
	// The two controls matter as much as the repair. A QMessageBox focuses
	// its own default button on the way up, which is exactly why a library
	// author testing with one would never see this; and an application
	// that called setFocus() before show() has said what it wants. Neither
	// may be overruled.
	{
		const bool had_conv = keyboard_conventions();
		set_keyboard_conventions(true);
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 12));
		auto *behind = new QLineEdit(&host);
		behind->setGeometry(0, 0, 20 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter dr(&host);
		behind->setFocus();
		set_focus_widget(behind);

		{
			QDialog dlg(&host);
			auto *first = new QLineEdit(&dlg);
			first->setGeometry(0, 0, 20 * cw, ch);
			auto *second = new QLineEdit(&dlg);
			second->setGeometry(0, 2 * ch, 20 * cw, ch);
			dlg.setModal(true);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(30, 6));
			dlg.show();
			QCoreApplication::processEvents();
			dr.on_key({0, QStringLiteral("a"), false, false, false});
			dr.on_key({0, QStringLiteral("b"), false, false, false});
			QCoreApplication::processEvents();
			CHECK(dlg.focusWidget() == first
			      && first->text() == QStringLiteral("ab")
			      && behind->text().isEmpty(),
			      "a modal dialog opens with its first field focused, so "
			      "what the user types next goes into the dialog rather "
			      "than nowhere");
			dlg.close();
			QCoreApplication::processEvents();
		}

		// AND BEFORE ANY KEY AT ALL, which is what the repair on Show
		// buys that the one at dispatch cannot. A terminal draws a frame
		// as soon as the dialog opens, and the cursor in that frame is
		// where the user is told they are -- a screen reader says it out
		// loud. Waiting for the first keystroke would place it correctly
		// one frame too late.
		{
			QVector<QWidget *> hidden;
			for (QWidget *t : QApplication::topLevelWidgets())
				if (t->isVisible()) { t->hide(); hidden.append(t); }
			QWidget owner;
			owner.setAttribute(Qt::WA_DontShowOnScreen);
			owner.resize(GridMetrics::cells(40, 12));
			owner.show();
			QCoreApplication::processEvents();
			InputRouter er(&owner);
			Compositor ec(&owner, &er);
			QDialog dlg(&owner);
			auto *field = new QLineEdit(&dlg);
			field->setGeometry(0, 0, 20 * cw, ch);
			dlg.setModal(true);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(30, 4));
			dlg.show();
			QCoreApplication::processEvents();
			CellBuffer eb(40, 12);
			ec.compose(eb);
			CHECK(dlg.focusWidget() == field && ec.cursor_cell().has_value(),
			      "and the very first frame of a dialog already has the "
			      "cursor in its field, no keystroke having been spent to "
			      "put it there");
			dlg.close();
			QCoreApplication::processEvents();
			for (QWidget *t : hidden) t->show();
			QCoreApplication::processEvents();
			GridGuard::reset();
		}

		// THROUGH exec(), which is how a dialog is actually opened, and
		// with the loop allowed to settle the way it has by the time a
		// key arrives from a terminal. The first version of this probe
		// asked before the queued repair had run and read a stale null,
		// which is a fixture measuring its own timing.
		{
			QDialog dlg(&host);
			auto *field = new QLineEdit(&dlg);
			field->setGeometry(0, 0, 20 * cw, ch);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(30, 4));
			bool typed_in = false;
			QTimer::singleShot(0, &dlg, [&] {
				QCoreApplication::processEvents();
				dr.on_key({0, QStringLiteral("z"), false, false, false});
				QCoreApplication::processEvents();
				typed_in = field->text() == QStringLiteral("z");
				dlg.accept();
			});
			dlg.exec();
			CHECK(typed_in,
			      "and the same through exec(), where a lost keystroke is "
			      "what an application would actually ship");
		}

		// CONTROL ONE: the application chose, and keeps its choice.
		{
			QDialog dlg(&host);
			auto *first = new QLineEdit(&dlg);
			first->setGeometry(0, 0, 20 * cw, ch);
			auto *chosen = new QLineEdit(&dlg);
			chosen->setGeometry(0, 2 * ch, 20 * cw, ch);
			dlg.setModal(true);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(30, 6));
			chosen->setFocus();
			dlg.show();
			QCoreApplication::processEvents();
			CHECK(dlg.focusWidget() == chosen,
			      "while a dialog whose application called setFocus() "
			      "before showing it keeps the widget it named");
			dlg.close();
			QCoreApplication::processEvents();
		}

		// CONTROL TWO: Qt chose, and keeps its choice. This is the case a
		// library author tests with, and the reason the defect survived.
		{
			QMessageBox box(QMessageBox::Question, QStringLiteral("Quit?"),
			                QStringLiteral("Save first?"),
			                QMessageBox::Save | QMessageBox::Cancel, &host);
			box.setAttribute(Qt::WA_DontShowOnScreen);
			box.show();
			QCoreApplication::processEvents();
			CHECK(qobject_cast<QPushButton *>(box.focusWidget()) != nullptr,
			      "and a QMessageBox keeps the default button Qt focused "
			      "itself, which this must not overrule");
			box.close();
			QCoreApplication::processEvents();
		}
		set_keyboard_conventions(had_conv);
		GridGuard::reset();
	}

	// ---- the cursor sits where the text being edited is ---------------------
	//
	// THE RELATIONSHIP, not a row number: compose the frame, find the row the
	// editor's own character was drawn on, and ask the compositor where it
	// put the terminal cursor. Pinning a number would say nothing about the
	// pair, and the pair is the whole property -- a screen reader announces
	// the cell the cursor is in.
	//
	// Both geometries, because only one of them was ever wrong. A view at
	// fixed geometry sits on a cell boundary and every reading agrees; a
	// view in a LAYOUT does not, and that is what every real application
	// does.
	{
		const bool had_conv = keyboard_conventions();
		set_keyboard_conventions(true);
		// Compositor::compose() walks EVERY top-level and the cases above
		// leave theirs alive and visible, so take the screen for the length
		// of these two and give it back -- the idiom the menu-click check
		// further up already needed, for the same reason. Without it the
		// frame searched here is somebody else's window and the cursor
		// belongs to a layer this fixture never built.
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		for (int in_layout = 0; in_layout < 2; ++in_layout) {
			QWidget win;
			auto *table = new QTableWidget(3, 2, &win);
			for (int row = 0; row < 3; ++row)
				for (int col = 0; col < 2; ++col)
					table->setItem(row, col, new QTableWidgetItem(
					    QStringLiteral("r%1c%2").arg(row).arg(col)));
			table->horizontalHeader()->hide();
			table->verticalHeader()->hide();
			if (in_layout) {
				auto *lay = new QVBoxLayout(&win);
				lay->setContentsMargins(0, 0, 0, 0);
				lay->setSpacing(0);
				lay->addWidget(table);
			} else {
				table->setGeometry(0, 0, cw * 30, ch * 6);
			}
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(40, 12));
			win.show();
			QCoreApplication::processEvents();
			InputRouter cr_router(&win);
			Compositor cr_comp(&win, &cr_router);
			table->setFocus();
			set_focus_widget(table);
			bool agreed = true, measured = false;
			for (int row = 0; row < 3; ++row) {
				table->setCurrentCell(row, 0);
				cr_router.on_key({Qt::Key_F2, QString(), false, false, false});
				cr_router.on_key({0, QStringLiteral("Q"), false, false, false});
				QCoreApplication::processEvents();
				CellBuffer frame(40, 12);
				cr_comp.compose(frame);
				int drawn = -1;
				for (int y = 0; y < 12 && drawn < 0; ++y)
					for (int x = 0; x < 40; ++x)
						if (frame.at(x, y).ch == QStringLiteral("Q")) {
							drawn = y;
							break;
						}
				const auto at = cr_comp.cursor_cell();
				if (drawn >= 0 && at) measured = true;
				if (drawn < 0 || !at || at->y() != drawn) agreed = false;
				cr_router.on_key({Qt::Key_Escape, QString(), false, false,
					              false});
				QCoreApplication::processEvents();
			}
			CHECK(measured && agreed,
			      in_layout
			          ? "the cursor lands on the row the edited text is drawn"
			            " on, with the view in a layout"
			          : "and the same with the view at fixed geometry, where"
			            " it agreed all along");
			GridGuard::reset();
		}
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		set_keyboard_conventions(had_conv);
		GridGuard::reset();
	}

	// ---- a router that outlives its own window ---------------------------
	//
	// DELETING A VISIBLE WIDGET SENDS QEvent::Hide -- measured, to the
	// QWidgetWindow, to the widget and to each visible child -- so the focus
	// repair in InputRouter::eventFilter() fires from inside the window's own
	// DESTRUCTOR and posts a queued call to the router. The router is a
	// separate object and is perfectly alive, so
	// QCoreApplication::removePostedEvents() has nothing to remove: it is
	// keyed on the RECEIVER, and what died is a widget the receiver points
	// at. The call then arrives on the next pass of the loop and asks
	// input_scope() for a window that is gone.
	//
	// Measured before the fix, on this exact shape, by the sanitized suite:
	//
	//     ERROR: AddressSanitizer: heap-use-after-free
	//     READ of size 8 ... src/runtime/input_router.cpp:406
	//     0 bytes inside of 40-byte region      <- sizeof(QWidget) is 40
	//     freed by ... delete of the window
	//
	// ALL THREE WERE WATCHED FAILING, with the library reverted and these
	// checks left in place, and the plain build said more than the sanitized
	// one. Sanitized, the process aborts here and prints nothing. At -Os with
	// no sanitizer:
	//
	//     FAIL: a router whose window has been destroyed answers no key ...
	//     FAIL: and does not begin answering for a window built after ...
	//     Segmentation fault
	//
	// -- two honest FAILs, then a crash on the third check's own action.
	//
	// The second line is why win_ is a QPointer rather than a null test
	// bolted onto one place. Qt reuses heap addresses, so a raw pointer to a
	// destroyed window compares EQUAL to a new window that lands on it, and
	// the dead router began answering for a window built after its own was
	// freed -- on the first run, glibc having handed the 40 bytes straight
	// back. grid_style.cpp makes that argument about its own focus record;
	// this is the measurement behind it. A crash is the loud version, and
	// answering for somebody else's window is the version nobody sees.
	{
		QVector<QWidget *> hidden;
		for (QWidget *t : QApplication::topLevelWidgets())
			if (t->isVisible()) { t->hide(); hidden.append(t); }
		{
			auto *gone = new QWidget;
			gone->setAttribute(Qt::WA_DontShowOnScreen);
			gone->resize(GridMetrics::cells(40, 12));
			auto *field = new QLineEdit(gone);
			field->setGeometry(0, 0, cw * 20, ch);
			gone->show();
			QCoreApplication::processEvents();
			InputRouter orphan(gone);
			field->setFocus();
			set_focus_widget(gone->focusWidget());
			QCoreApplication::processEvents();
			// BOTH LINES ARE THE FIXTURE. The delete is what posts the
			// repair, and only the processEvents delivers it -- without
			// the second, the call sits in the queue and nothing ever
			// reads the freed window.
			delete gone;
			QCoreApplication::processEvents();
			CHECK(orphan.key_target() == nullptr,
			      "a router whose window has been destroyed answers no key"
			      " target, rather than reading one out of the freed window");

			// A SECOND WINDOW, so the question is not only whether the
			// router refuses but whether it refuses the right thing. Two
			// fields, because a Tab that moves nothing would pass this
			// check for the wrong reason in a window with one.
			auto *after = new QWidget;
			after->setAttribute(Qt::WA_DontShowOnScreen);
			after->resize(GridMetrics::cells(40, 12));
			auto *one = new QLineEdit(after);
			one->setGeometry(0, 0, cw * 20, ch);
			auto *two = new QLineEdit(after);
			two->setGeometry(0, ch * 2, cw * 20, ch);
			after->show();
			QCoreApplication::processEvents();
			one->setFocus();
			QCoreApplication::processEvents();
			CHECK(orphan.key_target() == nullptr,
			      "and does not begin answering for a window built after"
			      " its own was freed, which is what a reused address"
			      " gives a raw pointer");
			orphan.on_key({Qt::Key_Tab, QString(), false, false, false});
			QCoreApplication::processEvents();
			CHECK(after->focusWidget() == one,
			      "and a key handed to it moves the focus in nobody's"
			      " window");
			delete after;
		}
		for (QWidget *t : hidden) t->show();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// ---- a drag whose source dies under it -------------------------------
	//
	// THE QDrag BELONGS TO THE WIDGET THE DRAG STARTED FROM, which is what
	// drag.h asks for: exec_drag() is a drop-in for QDrag::exec(), so what an
	// application brings to it is whatever it wrote for QDrag -- and the
	// spelling in Qt's own documentation is `new QDrag(this)` inside a mouse
	// handler. That parents the QDrag to the SOURCE WIDGET and ties its
	// lifetime to it, and exec_drag() then runs a nested event loop --
	// arbitrary application code, for as long as the button is held down.
	//
	// The sequence an application performs to reach it is ordinary. An item
	// is dragged out of a panel; the drop handler takes it; the panel is now
	// empty and closes itself, or the dialog it lived in is dismissed.
	// `delete panel` runs inside dropEvent(), the QDrag parented to it dies
	// with it, drag_drop_at() quits the loop, and exec_drag() resumes at
	// `drag->deleteLater()` holding freed memory. THAT IS THE LOUD ONE: a
	// heap-use-after-free on the QDrag, which the sanitized suite aborts on.
	//
	// The same death can arrive EARLIER, while the loop is still running, and
	// that half is why a null test at the end alone would not do. The
	// live-drag record keeps the QMimeData the QDrag OWNS and deletes, so
	// every move after the source died handed the target a freed payload --
	// and a drop target reads its payload, that being what a drop target is
	// for. The quiet version of that one is the worse: a QMimeData
	// reallocated where the old one stood answers text() with somebody
	// else's string and nothing crashes at all.
	//
	// Ending the drag is also the only answer that TERMINATES. A guard that
	// merely returned from drag_move_to() and drag_drop_at() would leave the
	// nested loop with nothing left to quit it, and a hang is worse than a
	// crash -- it produces neither a PASS nor a FAIL. `!rescued` is what
	// asserts the drag ended on its own, and the rescue timers are the ones
	// the drag checks above already carry, for that reason.
	{
		struct Taker : QWidget {
			int drops = 0;
			QString got;
			QWidget **kill = nullptr;
			explicit Taker(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
			void dragEnterEvent(QDragEnterEvent *e) override {
				e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dragMoveEvent(QDragMoveEvent *e) override {
				e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dropEvent(QDropEvent *e) override {
				++drops;
				got = e->mimeData()->text();
				e->setDropAction(Qt::MoveAction); e->accept();
				// The panel the item came out of closes behind it, which
				// is what takes the QDrag parented to it.
				if (kill && *kill) { delete *kill; *kill = nullptr; }
			}
		};
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *t = new Taker(&h);
		t->setGeometry(0, 0, cw * 10, ch * 2);
		auto *panel = new QWidget(&h);
		panel->setGeometry(0, ch * 2, cw * 10, ch);
		h.resize(GridMetrics::cells(12, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		auto *mime = new QMimeData;
		mime->setText(QStringLiteral("torn out"));
		auto *drag = new QDrag(panel);       // Qt's own `new QDrag(this)`
		drag->setMimeData(mime);
		QWidget *doomed = panel;
		t->kill = &doomed;
		QTimer::singleShot(0, [&] {
			r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
		});
		bool rescued = false;
		QTimer::singleShot(2000, [&] {
			if (Qtty::drag_active()) { rescued = true; Qtty::drag_cancel(); }
		});
		const Qt::DropAction took =
		    Qtty::exec_drag(drag, Qt::CopyAction | Qt::MoveAction);
		QCoreApplication::processEvents();
		printf("info: a drop that closed the drag source: drops=%d payload"
		       " \"%s\", the source is %s, exec_drag returned %d\n",
		       t->drops, qPrintable(t->got), doomed ? "alive" : "gone",
		       int(took));
		CHECK(!rescued && t->drops == 1 && doomed == nullptr
		      && took == Qt::MoveAction,
		      "a drop handler that closes the drag source, deleting the "
		      "QDrag parented to it, leaves exec_drag returning the action "
		      "the target took rather than deleting a QDrag already gone");

		// AND THE SAME DEATH DURING THE LOOP, which is the other half. A
		// dragMoveEvent handler that closes the source -- a panel that
		// collapses as its last item leaves it, a dialog dismissed while
		// the button is still down -- kills the QDrag in the MIDDLE of the
		// drag rather than at the end of it, and what the unfixed code
		// does next is hand the target another move, and then a drop,
		// carrying the QMimeData the QDrag deleted on its way out.
		//
		// Asserted as "nothing more was delivered" rather than by reading
		// the payload. A fixture that dereferenced the freed QMimeData
		// itself would be diagnosing its own line rather than the
		// library's, and under the sanitizers it would abort before any of
		// this could print.
		{
			struct Fragile : QWidget {
				int moves = 0, drops = 0, leaves = 0, after_death = 0;
				QWidget **kill = nullptr;
				explicit Fragile(QWidget *p) : QWidget(p) {
					setAcceptDrops(true);
				}
				void dragEnterEvent(QDragEnterEvent *e) override {
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					++moves;
					if (kill && *kill) { delete *kill; *kill = nullptr; }
					else ++after_death;
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
				void dropEvent(QDropEvent *e) override {
					++drops; e->setDropAction(Qt::MoveAction); e->accept();
				}
			};
			QWidget h2;
			h2.setAttribute(Qt::WA_DontShowOnScreen);
			auto *f = new Fragile(&h2);
			f->setGeometry(0, 0, cw * 10, ch * 2);
			auto *panel2 = new QWidget(&h2);
			panel2->setGeometry(0, ch * 2, cw * 10, ch);
			h2.resize(GridMetrics::cells(12, 4));
			h2.show();
			QCoreApplication::processEvents();
			InputRouter r2(&h2);

			auto *mime2 = new QMimeData;
			mime2->setText(QStringLiteral("half way"));
			auto *drag2 = new QDrag(panel2);
			drag2->setMimeData(mime2);
			QWidget *doomed2 = panel2;
			f->kill = &doomed2;
			// FOUR events, not three: the first motion is the enter, which
			// returns before any move is delivered, so the kill happens on
			// the second and the third is the one that would carry a freed
			// payload.
			QTimer::singleShot(0, [&] {
				r2.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
				r2.on_mouse({QPoint(5, 1), 1, false, false, true, 0});
				r2.on_mouse({QPoint(5, 1), 1, false, true, false, 0});
			});
			bool rescued2 = false;
			QTimer::singleShot(2000, [&] {
				if (Qtty::drag_active()) {
					rescued2 = true; Qtty::drag_cancel();
				}
			});
			const Qt::DropAction stopped =
			    Qtty::exec_drag(drag2, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: the source died mid-drag: moves=%d after_death=%d"
			       " leave=%d drop=%d, returned %d\n", f->moves,
			       f->after_death, f->leaves, f->drops, int(stopped));
			CHECK(!rescued2 && doomed2 == nullptr && f->after_death == 0
			      && f->drops == 0 && f->leaves >= 1
			      && stopped == Qt::IgnoreAction,
			      "a drag whose source is destroyed while the loop is "
			      "still running ends there and tells the target it was "
			      "left, rather than offering it another move and a drop "
			      "carrying a QMimeData the QDrag freed");

			// AND THE DEATH INSIDE THE MOVE THE RELEASE ITSELF SENDS,
			// which neither of the two above reaches. drag_drop_at()
			// delivers a move before the drop, and its own comment says
			// why: a target that has never seen this position has to
			// decide on it before being asked to take a drop there. So
			// the last handler run before every drop is one the
			// application wrote, and it can close the source just as the
			// drop handler can -- at which point the drop below it would
			// carry a QMimeData that no longer exists.
			//
			// A guard at the TOP of drag_drop_at() cannot see this: it
			// has already run. What catches it is the second look, after
			// the move and before the drop, and this is the check that
			// holds that one -- two events, so the only move in the whole
			// drag is the one the release sends.
			QWidget h_late;
			h_late.setAttribute(Qt::WA_DontShowOnScreen);
			auto *f_late = new Fragile(&h_late);
			f_late->setGeometry(0, 0, cw * 10, ch * 2);
			auto *panel_late = new QWidget(&h_late);
			panel_late->setGeometry(0, ch * 2, cw * 10, ch);
			h_late.resize(GridMetrics::cells(12, 4));
			h_late.show();
			QCoreApplication::processEvents();
			InputRouter r_late(&h_late);

			auto *mime_late = new QMimeData;
			mime_late->setText(QStringLiteral("on the way up"));
			auto *drag_late = new QDrag(panel_late);
			drag_late->setMimeData(mime_late);
			QWidget *doomed_late = panel_late;
			f_late->kill = &doomed_late;
			QTimer::singleShot(0, [&] {
				r_late.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r_late.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
			});
			bool rescued_late = false;
			QTimer::singleShot(2000, [&] {
				if (Qtty::drag_active()) {
					rescued_late = true; Qtty::drag_cancel();
				}
			});
			const Qt::DropAction ended =
			    Qtty::exec_drag(drag_late,
			                    Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: the source died in the release's own move:"
			       " moves=%d leave=%d drop=%d, returned %d\n",
			       f_late->moves, f_late->leaves, f_late->drops,
			       int(ended));
			CHECK(!rescued_late && doomed_late == nullptr
			      && f_late->moves == 1 && f_late->drops == 0
			      && f_late->leaves >= 1 && ended == Qt::IgnoreAction,
			      "and a source closed by the last move before the button "
			      "came up takes the drop with it, rather than being "
			      "handed one whose payload the QDrag has already freed");
		}
		GridGuard::reset();
	}

	// ---- focus handed to a widget the focus-out handler destroyed --------
	//
	// DELIVERING THE FocusOut IS THE WHOLE POINT OF THIS FUNCTION, and the
	// comment above s_focus in grid_style.cpp says what it buys: Qt sends a
	// QFocusEvent only for an ACTIVE window and no qtty window ever
	// activates, so QLineEdit::editingFinished() never fired and a form only
	// ever heard about the field the user pressed Return in. Sending it
	// means running the application's slot, and an editingFinished slot that
	// rebuilds or clears a form is ordinary Qt.
	//
	// So the sequence is one a person performs by pressing Tab: focus is in
	// field A, it moves to field B, A's slot rebuilds the form and deletes
	// the widgets in it -- B among them -- and set_focus_widget() then sends
	// FocusIn to a QWidget that is gone.
	//
	// THE DEFECT HAS TWO FACES AND THEY NEED DIFFERENT FIXTURES, which is
	// why there are two blocks below rather than one. If the bytes B stood
	// in are still free, the send reads them and the process dies -- that is
	// the loud half, and only a sanitizer makes it reliable. If something
	// has moved in, the send reaches a LIVE widget that never gained the
	// focus, which is the quiet half: nothing crashes, and a control in a
	// rebuilt form draws itself focused while Qtty::focusWidget() correctly
	// says nobody is. Measured here, running the same sabotage twice: the
	// first run crashed at this fixture and the second printed two FAILs
	// and the same address twice. The difference that can be named between
	// them is how stdout was buffered -- a pipe in one, an unbuffered file
	// in the other -- which is enough to move the heap and decide whether
	// anything lands on the freed bytes. So the quiet half is put beyond
	// the allocator's mood below rather than waited for.
	//
	// The guard this needs already existed A FEW LINES TOO LATE: the
	// status-tip block re-reads s_focus and compares it against `w`, so that
	// third use has been protected by accident since it was written. The fix
	// is the same re-read at the first use.
	{
		struct Field : QLineEdit {
			int in = 0;
			explicit Field(QWidget *p) : QLineEdit(p) {}
			void focusInEvent(QFocusEvent *e) override {
				++in;
				QLineEdit::focusInEvent(e);
			}
		};

		// THE QUIET HALF, with the address reuse made certain instead of
		// hoped for. The field the focus is moving to is constructed in a
		// block of THIS FUNCTION'S STACK, destroyed in place by the slot,
		// and the replacement is constructed in the same block -- so "a
		// widget built where the old one stood" is a fact of the fixture
		// rather than a favour from the allocator. 8.237 had that reuse by
		// luck and said so; this is the same event, arranged.
		{
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			h.resize(GridMetrics::cells(20, 6));
			auto *a = new Field(&h);
			a->setGeometry(0, 0, cw * 10, ch);
			alignas(Field) unsigned char slab[sizeof(Field)];
			auto *b = new (slab) Field(&h);
			b->setGeometry(0, ch * 2, cw * 10, ch);
			h.show();
			QCoreApplication::processEvents();
			set_focus_widget(a);
			QCoreApplication::processEvents();
			// TYPED INTO, and that is not decoration: Qt 6 gates
			// editingFinished on the field having actually been edited, so
			// an untouched one emits nothing on focus-out and this fixture
			// would reach the hazard by no path at all. The check above it
			// paid for that lesson first.
			QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
			                QStringLiteral("x"));
			QCoreApplication::sendEvent(a, &typed);

			Field *rebuilt = nullptr;
			QObject::connect(a, &QLineEdit::editingFinished, [&] {
				// The form is rebuilt from inside the slot, which is what
				// an application does here: the old fields go and new ones
				// are made. Destroyed in place and rebuilt in place, so
				// the replacement is at the old one's address by
				// construction.
				b->~Field();
				b = nullptr;
				rebuilt = new (slab) Field(&h);
				rebuilt->setGeometry(0, ch * 2, cw * 10, ch);
				rebuilt->show();
			});
			Field *const doomed = b;
			set_focus_widget(doomed);
			QCoreApplication::processEvents();
			printf("info: the field focus was moving to and the one that"
			       " replaced it share the address %p, and the replacement"
			       " has had %d FocusIn(s)\n",
			       static_cast<const void *>(slab),
			       rebuilt ? rebuilt->in : -1);
			CHECK(rebuilt && rebuilt->in == 0,
			      "a widget standing where the one focus was moving to was "
			      "destroyed is not told it has the focus, which is what a "
			      "raw pointer gives when the address is handed back");
			CHECK(Qtty::focusWidget() == nullptr,
			      "and the focus is left nowhere, rather than on a widget "
			      "the application destroyed on its way out of the field");
			set_focus_widget(rebuilt);
			CHECK(rebuilt && rebuilt->in == 1
			      && Qtty::focusWidget() == rebuilt,
			      "and the next focus move works, the destroyed one having "
			      "left no half-finished state behind");
			// FLUSHED, because the block below is allowed to take the
			// process down: this is the half a sabotage run has to be able
			// to read, and a FAIL line still in stdio's buffer when the
			// process dies is a FAIL line nobody ever sees.
			fflush(stdout);
			set_focus_widget(nullptr);
			// In place, as it was built. `h` must not be left a child it
			// would try to `delete`.
			if (rebuilt) rebuilt->~Field();
			QCoreApplication::processEvents();
			GridGuard::reset();
		}

		// AND THE LOUD HALF, which is the same sequence over the heap: the
		// slot destroys the field and puts nothing in its place, so the
		// FocusIn is sent to memory that is still free.
		//
		// THIS CHECK CANNOT FAIL IN AN ORDINARY BUILD, and saying so is the
		// point of it. Qtty::focusWidget() answers null either way, s_focus
		// being a QPointer already; what this block is for is to make the
		// library perform the send, so that the sanitized arm has something
		// to catch. Measured before the fix, `make test-sanitize`:
		//
		//     ERROR: AddressSanitizer: SEGV on unknown address 0x16a9b
		//       #0 QMetaObject::cast(QObject const*) const
		//       #1 QApplication::notify(QObject*, QEvent*)
		//       #3 Qtty::set_focus_widget(QWidget*, Qt::FocusReason)
		//
		// -- a SEGV rather than a heap-use-after-free, and the reason is
		// worth keeping: the read of the freed QWidget happens inside
		// libQt6Core, which is not instrumented, so the sanitizer never
		// sees it and reports only the wild pointer that comes back out.
		{
			QWidget h;
			h.setAttribute(Qt::WA_DontShowOnScreen);
			h.resize(GridMetrics::cells(20, 6));
			auto *a = new Field(&h);
			a->setGeometry(0, 0, cw * 10, ch);
			auto *b = new Field(&h);
			b->setGeometry(0, ch * 2, cw * 10, ch);
			h.show();
			QCoreApplication::processEvents();
			set_focus_widget(a);
			QCoreApplication::processEvents();
			QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
			                QStringLiteral("x"));
			QCoreApplication::sendEvent(a, &typed);

			Field *const doomed = b;
			bool cleared = false;
			QObject::connect(a, &QLineEdit::editingFinished, [&] {
				delete b;
				b = nullptr;
				cleared = true;
			});
			set_focus_widget(doomed);
			QCoreApplication::processEvents();
			CHECK(cleared && Qtty::focusWidget() == nullptr,
			      "and a form that clears the field focus was moving to, "
			      "rather than replacing it, leaves the focus nowhere and "
			      "sends nothing to the widget it freed");
			set_focus_widget(nullptr);
			GridGuard::reset();
		}
	}

	// DRAGGING THE CORNER OF A STATUS BAR MUST NOT SHRINK THE WINDOW OFF THE
	// TERMINAL. Qt's size grip resizes its top-level, and it worked here:
	// measured before the style took its cells away, one drag up and to the
	// left took a 30x7 window to 22x4 and left the bottom three rows of the
	// terminal blank, with nothing to restore them short of the terminal
	// itself changing size. The grip is invisible to a user -- an opaque
	// black cell under a theme that named no colour -- so this was a way to
	// break the screen by dragging something unreadable.
	//
	// The style's answer is to give a grip no cells, which is why the check
	// is on the window's SIZE rather than on what the corner draws: what
	// matters is that the drag reaches nothing.
	{
		QMainWindow win;
		win.setCentralWidget(new QLabel(QStringLiteral("body")));
		win.statusBar()->addWidget(new QLabel(QStringLiteral("ready")));
		win.setAttribute(Qt::WA_DontShowOnScreen);
		win.resize(GridMetrics::cells(30, 7));
		win.show();
		QCoreApplication::processEvents();
		// THE ESCAPE THE STYLE DOCUMENTS, asked for deliberately, because it
		// is what makes this check about the SIZE rule rather than about
		// polish(). GridStyle::polish() takes the grip out of a status bar
		// to get one cell back; an application that insists can ask again
		// after showing, and what it gets then is a grip with no cells that
		// no press can reach. That is the layering, and this is the half of
		// it nothing else asserts.
		win.statusBar()->setSizeGripEnabled(true);
		QCoreApplication::processEvents();
		const auto grips = win.statusBar()->findChildren<QSizeGrip *>();
		CHECK(grips.size() == 1 && grips.first()->size().isEmpty(),
		      "a status bar that asks for its size grip back gets one with "
		      "no cells");
		InputRouter r(&win);
		const auto drag_corner = [&r](int x, int y, int dx, int dy) {
			MouseEvent m{QPoint(x, y), 1, true, false, false, 0};
			r.on_mouse(m);
			QCoreApplication::processEvents();
			m.press = false; m.motion = true;
			m.cell = QPoint(x + dx, y + dy);
			r.on_mouse(m);
			QCoreApplication::processEvents();
			m.motion = false; m.release = true;
			r.on_mouse(m);
			QCoreApplication::processEvents();
		};
		const QSize before = win.size();
		drag_corner(29, 6, -8, -3);
		CHECK(win.size() == before,
		      "dragging the corner of a status bar does not resize the "
		      "window");
		// THE CONTROL, because the check above passes just as loudly if the
		// drag reached nothing for some reason of its own. A QSizeGrip given
		// a size by hand is still a working grip -- that is what the style
		// declines to hand out, not a capability the runtime lacks -- so the
		// same three events over one of those must resize its window. If
		// this stops firing, the check above has stopped meaning anything.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 7));
		auto *grip = new QSizeGrip(&host);
		grip->setFixedSize(GridMetrics::cw(), GridMetrics::ch());
		grip->move(29 * GridMetrics::cw(), 6 * GridMetrics::ch());
		host.show();
		QCoreApplication::processEvents();
		InputRouter r2(&host);
		const auto drag_host = [&r2](int x, int y, int dx, int dy) {
			MouseEvent m{QPoint(x, y), 1, true, false, false, 0};
			r2.on_mouse(m);
			QCoreApplication::processEvents();
			m.press = false; m.motion = true;
			m.cell = QPoint(x + dx, y + dy);
			r2.on_mouse(m);
			QCoreApplication::processEvents();
			m.motion = false; m.release = true;
			r2.on_mouse(m);
			QCoreApplication::processEvents();
		};
		const QSize host_before = host.size();
		drag_host(29, 6, -8, -3);
		CHECK(host.size() != host_before,
		      "and the control fires: a grip an application sized itself "
		      "still resizes its window under the same three events");
		GridGuard::reset();
	}

	return fails;
}
