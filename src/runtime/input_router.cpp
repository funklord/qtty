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
	if (QWidget *p = QApplication::activePopupWidget())
		return p->focusWidget() ? p->focusWidget() : p;
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
		setFocusWidget(scope->focusWidget());
	} else if (!match_shortcut(k)) {
		deliver_key(key_target(), k);
		setFocusWidget(input_scope()->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
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
	const QPoint pos = target->mapFrom(top, local);

	if (m.wheel) {
		QWheelEvent ev(QPointF(pos), QPointF(px), QPoint(),
		               QPoint(0, m.wheel * GridMetrics::ch()),
		               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(target, &ev);
	} else {
		const auto btn = Qt::LeftButton;
		if (m.press) {
			QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), QPointF(px),
			               btn, btn, Qt::NoModifier);
			QApplication::sendEvent(target, &ev);
		}
		if (m.release) {
			QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos), QPointF(px),
			               btn, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(target, &ev);
		}
		setFocusWidget(input_scope()->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

void InputRouter::on_paste(const QString &text) {
	QKeyEvent ev(QEvent::KeyPress, 0, Qt::NoModifier, text);
	QApplication::sendEvent(key_target(), &ev);
	if (frame_requested) frame_requested();
}

void InputRouter::on_resize(QSize cells) {
	win_->resize(cells.width() * GridMetrics::cw(), cells.height() * GridMetrics::ch());
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

void InputRouter::on_focus_change(bool) { if (frame_requested) frame_requested(); }

} // namespace Qtty
