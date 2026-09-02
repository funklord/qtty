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

// The actions a mnemonic may reach right now. A popup owns the keyboard while
// it is up, so Alt-O inside an open File menu must find that menu's Open and
// not a like-lettered action on the window behind it.
static QList<QAction *> mnemonic_actions(QWidget *scope,
                                        const InputRouter *router) {
	// The router's stack, for the reason key_target() gives: Qt's
	// activePopupWidget() is null for a stamped popup.
	if (router && !router->popups().isEmpty())
		scope = router->popups().last();
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

	for (QAction *a : mnemonic_actions(input_scope(), this)) {
		if (!a->isEnabled() || a->isSeparator()) continue;
		if (mnemonic_of(a->text()) != want) continue;
		if (QMenu *sub = a->menu()) {
			// A menu opens rather than triggers. Positioned under the item it
			// belongs to when that item is in a menu bar, which is where a
			// reader expects it; anywhere else, at the widget's own corner.
			QWidget *owner = a->associatedObjects().isEmpty()
			    ? nullptr
			    : qobject_cast<QWidget *>(a->associatedObjects().first());
			QPoint at(0, 0);
			if (auto *bar = qobject_cast<QMenuBar *>(owner)) {
				const QRect g = bar->actionGeometry(a);
				at = bar->mapToGlobal(QPoint(g.left(), g.bottom() + 1));
			} else if (owner) {
				at = owner->mapToGlobal(QPoint(0, 0));
			}
			sub->popup(at);
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
	for (QAction *a : std::as_const(actions)) {
		if (!a->isEnabled()) continue;
		const auto shortcuts = a->shortcuts();
		for (const QKeySequence &s : shortcuts)
			if (!s.isEmpty() && s == pressed) { a->trigger(); return true; }
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

	// Arrow keys a focus widget ignored fall back to scrolling the nearest
	// scroll area -- the TUI convention (section 5.5).
	if (!press.isAccepted() && (k.qt_key == Qt::Key_Up || k.qt_key == Qt::Key_Down
	                            || k.qt_key == Qt::Key_PageUp || k.qt_key == Qt::Key_PageDown)) {
		if (auto *area = input_scope()->findChild<QAbstractScrollArea *>()) {
			int step = GridMetrics::ch();
			if (k.qt_key == Qt::Key_PageUp || k.qt_key == Qt::Key_PageDown)
				step *= 5;
			const int dir = (k.qt_key == Qt::Key_Up || k.qt_key == Qt::Key_PageUp) ? -1 : 1;
			area->verticalScrollBar()->setValue(
			    area->verticalScrollBar()->value() + dir * step);
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
	} else if (!match_shortcut(k) && !match_mnemonic(k)) {
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
	const QPoint local = top->mapFromGlobal(px);   // offscreen: global == root coords
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

	const QPoint pos = target->mapFromGlobal(px);

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
			QWheelEvent ev(QPointF(w->mapFromGlobal(px)), QPointF(px), QPoint(),
			               QPoint(m.wheel_x * GridMetrics::cw(),
			                      m.wheel * GridMetrics::ch()),
			               Qt::NoButton, mods, Qt::NoScrollPhase, false);
			QApplication::sendEvent(w, &ev);
			if (ev.isAccepted()) break;
			if (w == top) break;              // do not escape the input layer
		}
	} else {
		const auto btn = qt_button(m.button);
		if (m.press) {
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
			               QPointF(pos), QPointF(px), btn, btn, mods);
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
		update_hover(target, pos);
		if (m.motion) {
			// Held-button state matters: a widget reads buttons() to tell a
			// drag from a hover, so a move sent with Qt::NoButton while
			// grabbed would arrive as the pointer merely passing over.
			const auto held = grab_.isNull() ? Qt::NoButton : Qt::MouseButtons(btn);
			QMouseEvent ev(QEvent::MouseMove, QPointF(pos), QPointF(px),
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
			QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, px, mods);
			QApplication::sendEvent(target, &ev);
		}
		if (m.release) {
			QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos), QPointF(px),
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
