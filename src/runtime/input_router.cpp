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

bool InputRouter::match_shortcut(const KeyEvent &k) {
	if (!k.qt_key || k.qt_key == Qt::Key_unknown) return false;
	Qt::KeyboardModifiers mods;
	if (k.ctrl) mods |= Qt::ControlModifier;
	if (k.alt) mods |= Qt::AltModifier;
	if (k.shift) mods |= Qt::ShiftModifier;
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
	Qt::KeyboardModifiers mods;
	if (k.ctrl) mods |= Qt::ControlModifier;
	if (k.alt) mods |= Qt::AltModifier;
	if (k.shift) mods |= Qt::ShiftModifier;
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
			qApp->quit();
			return;
		}
	if (k.qt_key == Qt::Key_Tab && !k.ctrl) {
		// Focus chain works without an active window (F4); drive it directly.
		QWidget *scope = input_scope();
		struct Probe : QWidget { using QWidget::focusNextPrevChild; };
		static_cast<Probe *>(scope)->focusNextPrevChild(!k.shift);
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
// Anything unrecognised is left, which is what the previous behaviour was for
// every button and is the harmless direction to be wrong in -- a wheel event
// leaves button at 0 and does not reach here.
static Qt::MouseButton qt_button(int button) {
	switch (button) {
	case 2:  return Qt::MiddleButton;
	case 3:  return Qt::RightButton;
	default: return Qt::LeftButton;
	}
}

void InputRouter::on_mouse(const MouseEvent &m) {
	const QPoint px(m.cell.x() * GridMetrics::cw() + GridMetrics::cw() / 2,
	                m.cell.y() * GridMetrics::ch() + GridMetrics::ch() / 2);
	// Popups first (top of stack down), then the modal, then the window
	// (section 5.5 routing order).
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

	if (m.wheel) {
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
			               QPoint(0, m.wheel * GridMetrics::ch()),
			               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
			QApplication::sendEvent(w, &ev);
			if (ev.isAccepted()) break;
			if (w == top) break;              // do not escape the input layer
		}
	} else {
		const auto btn = qt_button(m.button);
		if (m.press) {
			grab_ = target;
			QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), QPointF(px),
			               btn, btn, Qt::NoModifier);
			QApplication::sendEvent(target, &ev);
		}
		if (m.motion) {
			// Held-button state matters: a widget reads buttons() to tell a
			// drag from a hover, so a move sent with Qt::NoButton while
			// grabbed would arrive as the pointer merely passing over.
			const auto held = grab_.isNull() ? Qt::NoButton : Qt::MouseButtons(btn);
			QMouseEvent ev(QEvent::MouseMove, QPointF(pos), QPointF(px),
			               Qt::NoButton, held, Qt::NoModifier);
			QApplication::sendEvent(target, &ev);
		}
		if (m.press && btn == Qt::RightButton) {
			// The platform layer is what normally turns a right press into a
			// context menu event; there is no platform here, so nothing ever
			// asked for one. QWidget::event() reads contextMenuPolicy from
			// here, so every policy -- default, custom, actions -- starts
			// working at once. On the press rather than the release, which is
			// the X11 convention and so the one a terminal user expects.
			QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, px, Qt::NoModifier);
			QApplication::sendEvent(target, &ev);
		}
		if (m.release) {
			QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos), QPointF(px),
			               btn, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(target, &ev);
			grab_ = nullptr;
		}
		set_focus_widget(input_scope()->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
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
