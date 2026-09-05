// src/runtime/input_router.cpp -- L5 (section 5.5), rules measured in section 16:
//   F3: synthetic keys never reach QShortcutMap -- the router owns shortcuts.
//   F4: no window activates -- window->focusWidget() is the key target.
//   F7: internally-created top-levels don't inherit WA_DontShowOnScreen --
//       stamp every one from a global filter, popup and modal dialog alike,
//       and track the popups' z-order for the compositor.
//   section 8.3: input outside activeModalWidget() is dropped before dispatch.
#include "qtty/runtime.h"
#include "qtty/grid.h"
#include <QtWidgets>

namespace Qtty {

InputRouter::InputRouter(QWidget *window) : win_(window) {
	quit_keys_ = { KeyEvent{Qt::Key_C, QString(), true, false, false},
		          KeyEvent{Qt::Key_D, QString(), true, false, false} };
	qApp->installEventFilter(this);
}

InputRouter::~InputRouter() {
	if (qApp) qApp->removeEventFilter(this);
}

void InputRouter::set_quit_keys(const QVector<KeyEvent> &k) { quit_keys_ = k; }

QVector<QWidget *> InputRouter::popups() const {
	QVector<QWidget *> out;
	for (const auto &p : popups_) if (p && p->isVisible()) out.append(p.data());
	return out;
}

bool InputRouter::is_popup_layer(const QWidget *w) {
	const Qt::WindowFlags f = w->windowFlags();
	return (f & Qt::Popup) == Qt::Popup || f.testFlag(Qt::ToolTip);
}

// section 8.3: while a modal is up it is the whole of the input tree. Nothing
// outside it may be reached -- not by a key, not by a shortcut, not by the
// arrow-key scroll fallback -- because the window manager that enforces that
// on the desktop is not here and Qt's own modal blocking runs in the platform
// layer we bypass with synthetic events.
QWidget *InputRouter::input_scope() const {
	QWidget *m = QApplication::activeModalWidget();
	return m ? m : win_;
}

QWidget *InputRouter::key_target() const {
	// popup > modal > window focus. A key carries no position, so preferring
	// the modal IS the section 8.3 rule for keys: anything outside it is
	// unreachable rather than dropped. on_mouse() has to do the dropping
	// itself, because a click does carry one.
	// The router's OWN stack, not QApplication::activePopupWidget(). That
	// function returns null for every popup here, and the reason is this
	// class: the stamping filter sets WA_DontShowOnScreen on a popup as it is
	// shown (F7), the platform never maps it, and Qt's open-popup list is
	// driven by that mapping. So the branch that used to stand here could not
	// fire, and keys typed at an open menu went to the widget behind it --
	// Down and Return did nothing, and the menu drew perfectly throughout
	// because the compositor reads this same stack rather than Qt's.
	//
	// design.md section 16's gate 2 passed because it clicked. A synthetic
	// mouse press on actionGeometry().center() triggers the action without
	// consulting key_target() at all, so the keyboard path was never
	// exercised by the measurement that declared popups working.
	//
	// activeModalWidget() below is NOT affected: Qt tracks a modal through
	// setWindowModality and show, which stamping does not disturb.
	const QVector<QWidget *> open = popups();
	if (!open.isEmpty()) {
		QWidget *p = open.last();                 // topmost
		return p->focusWidget() ? p->focusWidget() : p;
	}
	QWidget *scope = input_scope();
	return scope->focusWidget() ? scope->focusWidget() : scope;
}

bool InputRouter::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Show) {
		if (auto *w = qobject_cast<QWidget *>(o)) {
			if (w->isWindow() && w != win_) {
				// F7 stamping, and it is not only for popups. Any top-level
				// created after exec() started -- a QComboBox's internal
				// container, a QMenu, or a QDialog the application opens --
				// arrives with the attribute unset and would be mapped to a
				// real screen on a platform that has one. Modals included:
				// this runtime draws them itself (section 8.1), so the
				// platform must never try to.
				w->setAttribute(Qt::WA_DontShowOnScreen);
				if (is_popup_layer(w)) {
					popups_.removeAll(QPointer<QWidget>(w));
					popups_.append(w);                             // top of stack
					if (frame_requested) frame_requested();
				}
			}
		}
	} else if (e->type() == QEvent::Hide) {
		if (auto *w = qobject_cast<QWidget *>(o)) {
			if (popups_.removeAll(QPointer<QWidget>(w)) && frame_requested)
				frame_requested();
		}
	}
	return false;                                                  // observe only
}

// The actions a mnemonic may reach right now.
//
// It used to take the router as well, and prefer the topmost popup when one
// was open -- so that Alt-O inside an open File menu found that menu's Open
// rather than a like-lettered action on the window behind it. That branch is
// gone because it became unreachable: on_key() no longer consults this table
// at all while a popup owns input, the menu answering Alt-O itself through
// QMenu::keyPressEvent, which also closes it. Coverage is what said so -- the
// line had no caller in a whole run -- and a branch whose comment describes a
// policy the code has moved elsewhere is worse than no branch.
static QList<QAction *> mnemonic_actions(QWidget *scope) {
	QList<QAction *> actions = scope->actions();
	const auto children = scope->findChildren<QWidget *>();
	for (QWidget *c : children) actions += c->actions();
	return actions;
}

// The letter a `&` marks, or a null QChar. Qt spells a literal ampersand
// "&&", which marks nothing.
static QChar mnemonic_of(const QString &text) {
	for (int i = 0; i + 1 < text.size(); ++i) {
		if (text.at(i) != QLatin1Char('&')) continue;
		if (text.at(i + 1) == QLatin1Char('&')) { ++i; continue; }
		return text.at(i + 1).toLower();
	}
	return QChar();
}

bool InputRouter::match_mnemonic(const KeyEvent &k) {
	// A mnemonic arrives as Alt with text and no Qt::Key: the terminal sends
	// ESC then the letter, and there is no key code to be had. That is why
	// match_shortcut() cannot serve -- it returns false on the first line for
	// want of a qt_key.
	if (!k.alt || k.text.size() != 1) return false;
	const QChar want = k.text.at(0).toLower();
	if (!want.isLetterOrNumber()) return false;

	for (QAction *a : mnemonic_actions(input_scope())) {
		if (!a->isEnabled() || a->isSeparator()) continue;
		if (mnemonic_of(a->text()) != want) continue;
		if (QMenu *sub = a->menu()) {
			// A menu opens rather than triggers.
			QWidget *owner = a->associatedObjects().isEmpty()
			    ? nullptr
			    : qobject_cast<QWidget *>(a->associatedObjects().first());
			if (auto *bar = qobject_cast<QMenuBar *>(owner)) {
				// The BAR opens it, rather than this function calling
				// popup() at a position it worked out itself. The position
				// was the visible half and the smaller half: popup() leaves
				// QMenuPrivate::causedPopup unset, so the menu does not know
				// which bar it belongs to and the bar does not know it is
				// open. QMenu::keyPressEvent's menu-bar traversal tests
				// qobject_cast<QMenuBar *>(topCausedWidget()) and could
				// therefore never fire, and QMenu::hideEvent's matching
				// clean-up could not either.
				//
				// Measured, Alt-F against a bar holding File and Edit:
				// with popup(), activeAction() stayed null, the bar drew
				// "File" no differently from "Edit", and Right did nothing.
				// With setActiveAction() the bar reports &File, draws it
				// marked, and Right closes File and opens Edit.
				//
				// QMenuBarPrivate::popupAction() does the placing, under the
				// item the menu belongs to, which is the same position the
				// hand-computed one aimed at -- so this drops code rather
				// than adding it.
				bar->setActiveAction(a);
				return true;
			}
			// Anywhere else there is no bar to ask, so the widget's own
			// corner it is: a submenu at the terminal's origin -- which is
			// what a null owner gives -- appears with nothing beside it to
			// say what it belongs to.
			sub->popup(owner ? owner->mapToGlobal(QPoint(0, 0)) : QPoint(0, 0));
			return true;
		}
		a->trigger();
		return true;
	}
	return false;
}

// One spelling, because the mouse now needs it too and three copies of a rule
// is how the mnemonic stripper came to have three (section 0d).
static Qt::KeyboardModifiers qt_modifiers(bool ctrl, bool alt, bool shift) {
	Qt::KeyboardModifiers mods;
	if (ctrl) mods |= Qt::ControlModifier;
	if (alt) mods |= Qt::AltModifier;
	if (shift) mods |= Qt::ShiftModifier;
	return mods;
}

bool InputRouter::match_shortcut(const KeyEvent &k) {
	if (!k.qt_key || k.qt_key == Qt::Key_unknown) return false;
	const Qt::KeyboardModifiers mods = qt_modifiers(k.ctrl, k.alt, k.shift);
	const QKeySequence pressed(QKeyCombination(mods, Qt::Key(k.qt_key)).toCombined());

	// Collect actions from the input scope, all its children, and menus
	// (rebuilt per press: correctness first, the table is small; section 5.5).
	// The scope is the modal while one is up, so a main-window shortcut cannot
	// fire behind a dialog that is blocking it (section 8.3).
	QWidget *const scope = input_scope();
	QList<QAction *> actions = scope->actions();
	const auto children = scope->findChildren<QWidget *>();
	for (QWidget *c : children) actions += c->actions();
	// A popup owns input while it is up (section 5.5), and on the desktop a
	// shortcut does not fire from behind an open menu. Measured against Qt
	// itself, with a real popup and no router involved:
	//
	//     menu closed, Ctrl+S to the window   the action triggered
	//     menu open,   Ctrl+S to the menu     nothing, and NOT accepted
	//     menu open,   bare 's' to the menu   triggered it and closed the menu
	//
	// So the chord is SWALLOWED here rather than passed on, which is the
	// middle row: Qt answers such a key by doing nothing and not accepting it.
	//
	// Swallowing rather than merely standing down is deliberate, and the
	// reason is one condition away from being invisible. A first version of
	// this comment claimed that delivering the chord onward would let Qt's own
	// QShortcutMap fire it -- measured in a probe, and the probe was wrong
	// about the runtime: its window was an ordinary one. Qt's map gates on the
	// widget's window being ACTIVE, and no window activates here because every
	// one carries WA_DontShowOnScreen. The same program, same keys:
	//
	//     ordinary window       Ctrl+S sent to it triggered the action
	//     WA_DontShowOnScreen   nothing, and activeWindow() is null
	//
	// So passing the chord on would be harmless only for as long as that
	// holds, and this does not depend on it.
	//
	// Only a chord that MATCHES is swallowed. A bare letter matches no
	// shortcut, falls through, and reaches QMenu::keyPressEvent, which is
	// where the desktop answers it from.
	const bool popup_owns_input = !popups().isEmpty();
	for (QAction *a : std::as_const(actions)) {
		if (!a->isEnabled()) continue;
		const auto shortcuts = a->shortcuts();
		for (const QKeySequence &s : shortcuts)
			if (!s.isEmpty() && s == pressed) {
				if (popup_owns_input) return true;   // swallowed, not fired
				a->trigger();
				return true;
			}
	}
	return false;
}

void InputRouter::deliver_key(QWidget *target, const KeyEvent &k) {
	const Qt::KeyboardModifiers mods = qt_modifiers(k.ctrl, k.alt, k.shift);
	QKeyEvent press(QEvent::KeyPress, k.qt_key, mods, k.text);
	QApplication::sendEvent(target, &press);
	// Terminals have no key-release; fabricate one immediately (section 5.5).
	QKeyEvent release(QEvent::KeyRelease, k.qt_key, mods, k.text);
	QApplication::sendEvent(target, &release);

	// Arrow keys nothing wanted fall back to scrolling a scroll area -- the
	// TUI convention (section 5.5).
	//
	// "The nearest" is what this used to claim, and it is not what happens.
	// Measured with two scroll areas and the focus on a widget INSIDE the
	// second that ignores every key: the second scrolled and this code never
	// ran, because Qt propagates an unaccepted key press up the parent chain
	// and the enclosing QScrollArea took it. findChild() returns the FIRST
	// area in the scope, which would have been the wrong one -- so the wrong
	// lookup was harmless only because the case it would get wrong is the
	// case that never reaches it.
	//
	// What is left for this branch is a focus widget with no scroll area
	// above it at all, and then there is no "nearest" to speak of: the
	// scope's first is as good an answer as any, and picking it is the
	// convention rather than a resolution of ambiguity.
	if (!press.isAccepted() && (k.qt_key == Qt::Key_Up || k.qt_key == Qt::Key_Down
	                            || k.qt_key == Qt::Key_PageUp || k.qt_key == Qt::Key_PageDown)) {
		if (auto *area = input_scope()->findChild<QAbstractScrollArea *>()) {
			// A scroll bar's value is not always pixels, and this asked for
			// a cell height regardless. QAbstractItemView defaults to
			// ScrollPerItem, where the value is an ITEM INDEX -- so one
			// arrow key scrolled nineteen rows and a page key ninety-five,
			// on any list, table or tree that had not been switched to
			// pixel scrolling. Ask the mode rather than assume the unit.
			const bool page = k.qt_key == Qt::Key_PageUp
			               || k.qt_key == Qt::Key_PageDown;
			QScrollBar *bar = area->verticalScrollBar();
			bool per_item = false;
			if (auto *view = qobject_cast<QAbstractItemView *>(area))
				per_item = view->verticalScrollMode()
				         == QAbstractItemView::ScrollPerItem;
			// In item units the bar already knows what a page is -- it is
			// the number of visible rows, which is what a reader expects
			// PageDown to move. In pixels there is nothing to ask, so the
			// cell height stands and five of them make a page.
			int step;
			if (per_item) step = page ? qMax(1, bar->pageStep()) : 1;
			else          step = page ? GridMetrics::ch() * 5 : GridMetrics::ch();
			const int dir = (k.qt_key == Qt::Key_Up || k.qt_key == Qt::Key_PageUp) ? -1 : 1;
			bar->setValue(bar->value() + dir * step);
		}
	}
}

void InputRouter::on_key(const KeyEvent &k) {
	for (const KeyEvent &q : std::as_const(quit_keys_))
		if (q.qt_key == k.qt_key && q.ctrl == k.ctrl && q.alt == k.alt) {
			// ...unless a text field has focus, where the same chord is copy
			// on every desktop there is. Measured: with the whole of a
			// QLineEdit selected, Ctrl+X cut it and put it on the clipboard,
			// Ctrl+V pasted, Ctrl+A selected all -- and Ctrl+C reached
			// nothing, because this loop is the first thing in the function.
			// Cut and paste worked and copy ended the application: the one
			// clipboard operation that changes nothing was the one that
			// destroyed the most.
			//
			// WA_InputMethodEnabled is the test, the same one that decides
			// where the terminal's cursor goes: it is set by exactly the
			// widgets that edit text, and Qt clears it on a read-only line
			// edit, which has nothing to copy from and should still quit.
			// A class list would have to name QLineEdit, QTextEdit,
			// QPlainTextEdit and every application's own editor, and would be
			// wrong about the last one.
			//
			// The escape hatch survives where it matters. A form is mostly
			// buttons, lists and tables, and Ctrl+C quits from all of them;
			// only a caret sitting in a field takes the key away, which is
			// the one place a user means copy. An application that wants the
			// old behaviour has set_quit_keys(), and one that wants no quit
			// key at all passes an empty list.
			const QWidget *fw = key_target();
			if (k.qt_key == Qt::Key_C && k.ctrl && fw
			    && fw->testAttribute(Qt::WA_InputMethodEnabled))
				break;
			qApp->quit();
			return;
		}
	if (k.qt_key == Qt::Key_Tab && !k.ctrl) {
		// The widget first, and only then the focus chain. This drove the
		// chain unconditionally, so every widget that WANTS a tab lost it:
		// measured, a QTextEdit reports tabChangesFocus() false -- Qt saying
		// it wants the key -- and a tab typed into one moved focus to the
		// next button instead, while a 2x2 QTableWidget's current cell stayed
		// at 0,0 where Tab should have moved it. The same shape as the quit
		// key above: an interception before dispatch takes a key from the one
		// widget that had a use for it.
		//
		// Qt's own arrangement is this order -- QWidget::event() offers a Tab
		// to keyPressEvent() and only calls focusNextPrevChild() if nothing
		// accepted it -- and deliver_key() already uses it for the arrow
		// keys, falling back to scrolling a scroll area when the focus widget
		// did not want them.
		//
		// The focus widget is compared as well as the accepted flag, because
		// Qt's own default handler may move focus AND accept: driving the
		// chain again on top of that would skip a widget.
		QWidget *scope = input_scope();
		QWidget *before = scope->focusWidget();
		const Qt::KeyboardModifiers mods = qt_modifiers(k.ctrl, k.alt, k.shift);
		QKeyEvent press(QEvent::KeyPress, k.qt_key, mods, k.text);
		QWidget *target = key_target();
		if (target) QApplication::sendEvent(target, &press);
		// A QKeyEvent starts ACCEPTED, so with no target to offer it to the
		// event would read as handled and the chain would never move. The
		// no-target case is tested for directly rather than by calling
		// ignore() first: that was the first fix, and it broke three focus
		// checks that had been passing -- Qt's own QWidget::event() reaches
		// its Tab branch by a path the flag's starting value takes part in,
		// and the measurement is the authority over the reasoning.
		if ((!target || !press.isAccepted()) && scope->focusWidget() == before) {
			// Focus chain works without an active window (F4); drive it
			// directly.
			struct Probe : QWidget { using QWidget::focusNextPrevChild; };
			static_cast<Probe *>(scope)->focusNextPrevChild(!k.shift);
		}
		set_focus_widget(scope->focusWidget());
	} else {
		// The tables do not run while a popup owns input. Section 5.5's order
		// is popup > modal > window, and key_target() already applies it to
		// keys; a shortcut firing from behind an open menu breaks the rule
		// input_scope() refuses to break for a modal, one layer up.
		//
		// Measured with a File menu open, before this:
		//
		//     Ctrl+W   triggered a WINDOW action
		//     Ctrl+S   triggered the menu's own Save, menu still on screen
		//     Alt+O    triggered Open, menu still on screen
		//
		// The last is the one a user sees: an item fired and the menu it came
		// from stayed up, because nothing in the mnemonic path knows a menu is
		// involved. The key goes to the popup instead, which is where the
		// desktop answers from -- a bare letter already reaches
		// QMenu::keyPressEvent, which triggers the item AND closes the menu,
		// measured as the popup stack going 1 to 0.
		//
		// The same predicate key_target() uses, deliberately: whatever owns
		// keys owns shortcuts, so the two cannot disagree about who is on top.
		const bool popup_owns_input = !popups().isEmpty();
		if (!match_shortcut(k) && (popup_owns_input || !match_mnemonic(k)))
			deliver_key(key_target(), k);
		set_focus_widget(input_scope()->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

// SGR 1006 reports the button in the low two bits and the backend hands it on
// as 1 left, 2 middle, 3 right. Nothing read it: on_mouse() sent Qt::LeftButton
// whatever arrived, so a right-click ACTIVATED a button rather than doing
// nothing, and no context menu could ever be asked for. Measured: a right
// click on a QPushButton emitted clicked().
//
// The extended buttons arrive as 4..7 and 0 means "no button", both since the
// backend stopped folding them onto the first three. The fallback below is
// still left, and the sentence that used to stand here called that "the
// harmless direction to be wrong in" -- which was true of the fallback and
// false of the case it was defending. A back-button click never reached this
// switch as an unknown: the DECODER had already made it a left press. Where
// the information is thrown away is where it has to be kept, and that is one
// layer earlier.
static Qt::MouseButton qt_button(int button) {
	switch (button) {
	case 0:  return Qt::NoButton;          // the protocol's "no button"
	case 2:  return Qt::MiddleButton;
	case 3:  return Qt::RightButton;
	case 4:  return Qt::BackButton;
	case 5:  return Qt::ForwardButton;
	case 6:  return Qt::ExtraButton1;
	case 7:  return Qt::ExtraButton2;
	default: return Qt::LeftButton;
	}
}

void InputRouter::set_root_scroll(QPoint cells) { root_scroll_ = cells; }

// The motion a pointer would have made crossing a menu on its way to the item
// it clicks. A terminal never makes it: the backend asks for \033[?1002h,
// which reports presses, releases and drags and NOT bare motion, so a real
// click on a menu item arrives with nothing in front of it.
//
// QMenu refuses such a click. QMenuPrivate::hasMouseMoved() gates
// mousePressEvent(), mouseMoveEvent() and mouseReleaseEvent() alike, and its
// two halves are `motions > 6` and a distance from the cursor position the
// menu was popped at. The distance half is dead here -- measured false
// throughout, because it is compared against
// QGuiApplicationPrivate::lastCursorPosition, which only the platform's own
// mouse events update -- so the motion count is the only half a terminal can
// satisfy. A press that fails the gate calls hideUpToMenuBar(): the menu
// vanishes and the item does not fire, which is exactly what a user saw.
//
// Measured, sending the motion straight to the menu and then clicking "Cut":
//
//     motions   0..6      nothing highlights, the press dismisses the menu
//     motions   7         the item highlights and the press lands
//
// and QMenu::enterEvent() sets motions to -1 -- "ignore the move the platform
// generates on entry" -- so a menu the pointer has just entered needs EIGHT.
// That is where the count below comes from; it is Qt's constant plus the
// entry, not a number raised until something passed.
//
// The alternative was to resolve QMenu::actionAt() and trigger the action
// directly, and it is worse for the reason this file already gives for Enter,
// Leave and QContextMenuEvent: the missing piece is the platform's, not the
// menu's. Triggering by hand would have to re-implement the submenu opening,
// the checkable toggle, the sync action and the close of the whole caused-by
// chain -- all of which QMenu does correctly the moment the press lands.
static void prime_menu_motion(QWidget *target, const QPoint &screen,
                              Qt::KeyboardModifiers mods) {
	QWidget *w = target;
	while (w && !qobject_cast<QMenu *>(w)) w = w->parentWidget();
	QMenu *menu = qobject_cast<QMenu *>(w);
	if (!menu) return;
	const QPointF local(menu->mapFromGlobal(screen));
	// Unconditional, rather than stopping as soon as the item highlights. A
	// menu whose current item was set by the KEYBOARD is already highlighting
	// the one about to be clicked, so an early exit would send nothing, leave
	// the count at -1 and dismiss the menu -- the very fault this is here to
	// remove, surviving in the one case a user reaches by typing first.
	for (int i = 0; i < 8; ++i) {
		QMouseEvent ev(QEvent::MouseMove, local, QPointF(screen), Qt::NoButton,
		               Qt::NoButton, mods);
		QApplication::sendEvent(menu, &ev);
	}
}

void InputRouter::on_mouse(const MouseEvent &m) {
	const QPoint px(m.cell.x() * GridMetrics::cw() + GridMetrics::cw() / 2,
	                m.cell.y() * GridMetrics::ch() + GridMetrics::ch() / 2);
	// Popups first (top of stack down), then the modal, then the window
	// (section 5.5 routing order).
	const Qt::KeyboardModifiers mods = qt_modifiers(m.ctrl, m.alt, m.shift);
	QWidget *top = nullptr;
	const auto ps = popups();
	for (auto it = ps.rbegin(); it != ps.rend(); ++it)
		if ((*it)->geometry().contains(px)) { top = *it; break; }
	if (!top && !ps.isEmpty() && grab_.isNull()) {
		// A popup owns the pointer for as long as it is up, the same way
		// section 8.3's modal owns it and for the same reason: the grab that
		// enforces that on the desktop lives in the platform layer this
		// runtime bypasses. Without the rule the hit test above simply failed
		// to find a popup and control fell through to the window branch, so
		// the click was delivered THROUGH the open popup.
		//
		// Measured twice before this existed. A catcher widget under an open
		// QMenu received the press; and a QPushButton behind one emitted
		// clicked() with the menu still visible, still the only entry in
		// popups(), and still what key_target() named. The menu kept the
		// keyboard while the window behind it took the mouse.
		//
		// Dropped rather than redirected, which is the modal branch's rule
		// below verbatim -- a click on what a popup is covering means
		// nothing, and handing it to the popup would invent a press. The one
		// thing a popup does that a modal does not is go away, so a PRESS
		// also closes the stack, from the top down: a submenu must not
		// outlive the menu that opened it.
		//
		// grab_ is the exception and not an oversight. A press that landed
		// INSIDE a popup owns everything up to its release (section 5.5), and
		// a slider dragged off the edge of the menu it sits in would lose the
		// rest of the drag to this rule.
		if (m.press) {
			for (auto it = ps.rbegin(); it != ps.rend(); ++it) (*it)->close();
			QCoreApplication::processEvents();
			if (frame_requested) frame_requested();
		}
		return;
	}
	if (!top) {
		if (QWidget *modal = QApplication::activeModalWidget()) {
			// section 8.3: input outside activeModalWidget() is dropped before
			// dispatch. key_target() already gives keys to the modal, but a
			// click carries a position and had no such rule, so it went to
			// whatever sat under the dialog -- a button pressed through a
			// modal that was there to block it. Dropped, not redirected: a
			// click on the blocked window means nothing, and delivering it to
			// the dialog instead would invent a press the user never made.
			if (!modal->geometry().contains(px)) return;
			top = modal;
		} else {
			top = win_;
		}
	}
	// The ROOT is drawn at -scroll cells when the terminal is too small for
	// the window (section 7's policy), and nothing shared that offset with
	// this function: a click on the button the user could SEE was delivered
	// to whatever sat at the same screen cell in an unscrolled window.
	// Measured on a 30x4 terminal scrolled four rows -- the visible button
	// was hit as the QLabel four rows above it.
	//
	// Only the root. A popup, a modal and a plain top-level are drawn at
	// their own geometry, and the hit test above already compares against
	// that geometry, so shifting them would break what works.
	//
	// A popup anchored inside the root DOES move with the root, and the
	// reason this function needs no case for it is that the Compositor
	// translates such a popup by the root's scroll and MOVES it there, so
	// its geometry is already a screen position by the time the hit test
	// above reads it. This comment said that was an open fault until
	// 2026-09-04; it was closed the day it was written, in the same pass
	// that taught follow_focus() to steer a menu by its active action.
	const QPoint screen = top == win_
	    ? px + QPoint(root_scroll_.x() * GridMetrics::cw(),
	                  root_scroll_.y() * GridMetrics::ch())
	    : px;
	const QPoint local = top->mapFromGlobal(screen); // offscreen: global == root coords
	QWidget *child = top->childAt(local);
	QWidget *target = child ? child : top;

	// A drag belongs to the widget the press landed on, for as long as the
	// button is down. Without that, every event re-runs the hit test above and
	// the drag is handed to whatever the pointer is over -- which for the two
	// things a terminal user actually drags is fatal: a splitter handle is one
	// cell wide, and a slider's groove ends. Both were dead (section 7.2), and
	// motion was the larger half of why: the backend parses it (SGR 1002
	// reports drags) and MouseEvent has carried a `motion` flag all along,
	// which this function read for the first time here.
	//
	// Qt calls this a mouse grab and would normally set it from the platform.
	// There is no platform here, so the router keeps it: the press records the
	// target, motion and release go to it wherever the pointer is, and the
	// release clears it.
	if (!grab_.isNull() && (m.motion || m.release)) target = grab_;

	// From `screen`, not from `px`. The first version of the scroll fix
	// corrected the hit test and left this line alone, so the right widget
	// received an event whose own position was five rows above itself --
	// QAbstractButton checks rect().contains() on the release before it
	// emits clicked(), so the press landed and the click did not. Two
	// derivations of one position, and only one of them was fixed.
	const QPoint pos = target->mapFromGlobal(screen);

	if (m.wheel || m.wheel_x) {
		// Up the parent chain until something takes it. Qt propagates an
		// ignored wheel event itself, but only for one the PLATFORM
		// delivered; QApplication::sendEvent() does not, so a synthetic one
		// stopped at whatever childAt() found.
		//
		// Measured: in a QScrollArea childAt() returns the scrolled WIDGET,
		// which ignores the wheel and does not scroll, while its parent --
		// the viewport -- accepts and scrolls by a row. So the wheel did
		// nothing at all in a scroll area, which is the third thing wrong
		// with this function's mouse handling after motion and the button,
		// and the same shape as both: an event correctly parsed, delivered
		// somewhere that cannot act on it.
		for (QWidget *w = target; w; w = w->parentWidget()) {
			// From `screen`, for the same reason `pos` above is. The comment
			// there says "two derivations of one position, and only one of
			// them was fixed" -- this was a THIRD, and it was still reading
			// the unscrolled cell. The target is found through `screen`, so
			// the right widget received an event whose own position was the
			// root's scroll away from itself, and anything acting on
			// QWheelEvent::position() -- a plot picking a point, a view
			// zooming about the cursor -- acted on the wrong one.
			QWheelEvent ev(QPointF(w->mapFromGlobal(screen)), QPointF(screen),
			               QPoint(),
			               QPoint(m.wheel_x * GridMetrics::cw(),
			                      m.wheel * GridMetrics::ch()),
			               Qt::NoButton, mods, Qt::NoScrollPhase, false);
			QApplication::sendEvent(w, &ev);
			if (ev.isAccepted()) break;
			if (w == top) break;              // do not escape the input layer
		}
	} else {
		const auto btn = qt_button(m.button);
		// BEFORE the press, not after it, which is where this line used to
		// stand. A pointer arrives at a widget and only then presses, and the
		// order was not cosmetic: QMenu::enterEvent() resets the motion count
		// that prime_menu_motion() above is entirely about, so an Enter sent
		// AFTER a press undid what let the press land, and the release then
		// dismissed the menu instead of firing its item. Measured -- with the
		// enter arriving after the press, the item highlighted, the menu
		// stayed up, and the release fired nothing at all.
		update_hover(target, pos);
		if (m.press) {
			prime_menu_motion(target, screen, mods);
			grab_ = target;
			// A second press on the same cell with the same button, inside
			// the interval Qt itself publishes, is a double click -- and it
			// REPLACES the press rather than following it. That is Qt's own
			// arrangement: QWidget::mouseDoubleClickEvent() forwards to
			// mousePressEvent() by default, which is why QAbstractButton
			// needs no override and QAbstractItemView has one. Sending both
			// would make a button count the second click twice.
			//
			// The cell rather than a pixel radius: a terminal reports a
			// position in cells, so "did not move" can only mean the same
			// cell. Measured before this existed: two clicks
			// gave two presses and no double click at all, so
			// itemDoubleClicked, a line edit selecting a word and a tree
			// expanding on double click were all dead.
			const bool again = since_press_.isValid()
			                && since_press_.elapsed() < QApplication::doubleClickInterval()
			                && m.cell == last_press_cell_
			                && m.button == last_press_button_;
			QMouseEvent ev(again ? QEvent::MouseButtonDblClick
			                     : QEvent::MouseButtonPress,
			               QPointF(pos), QPointF(screen), btn, btn, mods);
			QApplication::sendEvent(target, &ev);
			// A third click starts again rather than chaining into another
			// double, which is what a platform does.
			if (again) {
				since_press_.invalidate();
				last_press_cell_ = QPoint(-1, -1);
				last_press_button_ = 0;
			} else {
				since_press_.start();
				last_press_cell_ = m.cell;
				last_press_button_ = m.button;
			}
		}
		if (m.motion) {
			// Held-button state matters: a widget reads buttons() to tell a
			// drag from a hover, so a move sent with Qt::NoButton while
			// grabbed would arrive as the pointer merely passing over.
			const auto held = grab_.isNull() ? Qt::NoButton : Qt::MouseButtons(btn);
			QMouseEvent ev(QEvent::MouseMove, QPointF(pos), QPointF(screen),
			               Qt::NoButton, held, mods);
			QApplication::sendEvent(target, &ev);
		}
		if (m.press && btn == Qt::RightButton) {
			// The platform layer is what normally turns a right press into a
			// context menu event; there is no platform here, so nothing ever
			// asked for one. QWidget::event() reads contextMenuPolicy from
			// here, so every policy -- default, custom, actions -- starts
			// working at once. On the press rather than the release, which is
			// the X11 convention and so the one a terminal user expects.
			QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, screen, mods);
			QApplication::sendEvent(target, &ev);
		}
		if (m.release) {
			QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos), QPointF(screen),
			               btn, Qt::NoButton, mods);
			QApplication::sendEvent(target, &ev);
			grab_ = nullptr;
		}
		set_focus_widget(input_scope()->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

// Enter and Leave, which nothing was sending. QApplicationPrivate does this
// from the platform's mouse events; there is no platform here, so the same
// three things that were missing for the context menu are missing for hover:
// an application's enterEvent() and leaveEvent() overrides never ran,
// QWidget::underMouse() was permanently false, and QStyle::State_MouseOver
// could not be set on any option.
//
// Measured before the fix, sweeping the pointer over every cell of a form:
// underMouse() false on every widget, while Qt had set WA_Hover on the push
// button -- so Qt was prepared to repaint for a hover that could never
// arrive.
//
// The ancestor chains, not just the two widgets. underMouse() is true for a
// container while the pointer is over its child, and a widget that stays
// under the pointer must not be told it was left and re-entered: only the
// difference between the two chains gets an event.
//
// The events alone are enough: Qt maintains WA_UnderMouse itself when Enter
// and Leave arrive through QApplication::sendEvent(), so underMouse() answers
// correctly without this function touching the attribute. That was not the
// first version -- it set and cleared WA_UnderMouse explicitly, and a
// sabotage of each line in turn changed nothing, which is what says a line
// is not doing the work it claims. The set difference below is what does.
//
// The early return is an optimisation, not the mechanism. Without it the two
// chains are equal and every widget is skipped anyway; with it, a move
// within one widget does not build them.
static QVector<QWidget *> hover_chain(QWidget *w) {
	QVector<QWidget *> c;
	for (QWidget *a = w; a; a = a->parentWidget()) {
		c.append(a);
		if (a->isWindow()) break;
	}
	return c;
}

void InputRouter::update_hover(QWidget *now, const QPoint &window_pos) {
	if (now == hovered_) return;
	const QVector<QWidget *> was = hovered_ ? hover_chain(hovered_)
	                                        : QVector<QWidget *>();
	const QVector<QWidget *> is = now ? hover_chain(now) : QVector<QWidget *>();
	for (QWidget *w : was) {
		if (is.contains(w)) continue;
		QEvent leave(QEvent::Leave);
		QApplication::sendEvent(w, &leave);
	}
	for (QWidget *w : is) {
		if (was.contains(w)) continue;
		const QPointF local = w->mapFrom(w->window(), window_pos);
		QEnterEvent enter(local, local, local);
		QApplication::sendEvent(w, &enter);
	}
	hovered_ = now;
}

void InputRouter::on_paste(const QString &text) {
	QWidget *target = key_target();

	// A paste is text, not typing, which is the whole reason bracketed paste
	// exists: delivering the newlines as Return would fire the default button
	// and submit a dialog halfway through the paste. So the text goes in as
	// one event carrying the lot.
	//
	// But a single-line editor cannot hold a newline, and it accepts one
	// anyway through this path -- measured: pasting two lines into a QLineEdit
	// left its text() containing a literal newline, a state no user can type
	// and one nothing downstream expects. Folded to spaces for those targets,
	// which is what a clipboard paste into the same field does.
	//
	// The test is by type because Qt exposes no generic "accepts a newline"
	// query -- ImhMultiLine is a hint an application sets, not something
	// QLineEdit reports. QLineEdit is the single-line text entry in Qt
	// Widgets, and QAbstractSpinBox embeds one, so focus lands on a QLineEdit
	// for both. A third-party single-line editor would still get the raw
	// newlines, and that is a known edge rather than a hidden one.
	QString payload = text;
	if (qobject_cast<QLineEdit *>(target)) {
		payload.replace(QLatin1Char('\r'), QLatin1Char(' '));
		payload.replace(QLatin1Char('\n'), QLatin1Char(' '));
	}

	QKeyEvent ev(QEvent::KeyPress, 0, Qt::NoModifier, payload);
	QApplication::sendEvent(target, &ev);
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

void InputRouter::on_resize(QSize cells) {
	win_->resize(cells.width() * GridMetrics::cw(), cells.height() * GridMetrics::ch());
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

void InputRouter::on_focus_change(bool) { if (frame_requested) frame_requested(); }

} // namespace Qtty
