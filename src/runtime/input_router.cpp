// src/runtime/input_router.cpp -- L5 (section 5.5), rules measured in section 16:
//   F3: synthetic keys never reach QShortcutMap -- the router owns shortcuts.
//   F4: no window activates -- window->focusWidget() is the key target.
//   F7: internally-created popups don't inherit WA_DontShowOnScreen -- stamp
//       them from a global filter, and track their z-order for the compositor.
#include "qtty/runtime.h"
#include "qtty/grid.h"
#include <QtWidgets>

namespace Qtty {

InputRouter::InputRouter(QWidget *window) : win_(window) {
	quitKeys_ = { KeyEvent{Qt::Key_C, QString(), true, false, false},
			      KeyEvent{Qt::Key_D, QString(), true, false, false} };
	qApp->installEventFilter(this);
}

InputRouter::~InputRouter() {
	if (qApp) qApp->removeEventFilter(this);
}

void InputRouter::setQuitKeys(const QVector<KeyEvent> &k) { quitKeys_ = k; }

QVector<QWidget *> InputRouter::popups() const {
	QVector<QWidget *> out;
	for (const auto &p : popups_) if (p && p->isVisible()) out.append(p.data());
	return out;
}

QWidget *InputRouter::keyTarget() const {
	if (QWidget *p = QApplication::activePopupWidget())
		return p->focusWidget() ? p->focusWidget() : p;
	if (QWidget *m = QApplication::activeModalWidget())
		return m->focusWidget() ? m->focusWidget() : m;
	return win_->focusWidget() ? win_->focusWidget() : win_;
}

bool InputRouter::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Show) {
		if (auto *w = qobject_cast<QWidget *>(o)) {
			if (w->isWindow() && w != win_) {
				w->setAttribute(Qt::WA_DontShowOnScreen);          // F7 stamping
				if ((w->windowFlags() & Qt::Popup) == Qt::Popup
					|| w->windowFlags().testFlag(Qt::ToolTip)) {
					popups_.removeAll(QPointer<QWidget>(w));
					popups_.append(w);                             // top of stack
					if (frameRequested) frameRequested();
				}
			}
		}
	} else if (e->type() == QEvent::Hide) {
		if (auto *w = qobject_cast<QWidget *>(o)) {
			if (popups_.removeAll(QPointer<QWidget>(w)) && frameRequested)
				frameRequested();
		}
	}
	return false;                                                  // observe only
}

bool InputRouter::matchShortcut(const KeyEvent &k) {
	if (!k.qtKey || k.qtKey == Qt::Key_unknown) return false;
	Qt::KeyboardModifiers mods;
	if (k.ctrl) mods |= Qt::ControlModifier;
	if (k.alt) mods |= Qt::AltModifier;
	if (k.shift) mods |= Qt::ShiftModifier;
	const QKeySequence pressed(QKeyCombination(mods, Qt::Key(k.qtKey)).toCombined());

	// Collect actions from the window, all children, and menus (rebuilt per
	// press: correctness first, the table is small; section 5.5).
	QList<QAction *> actions = win_->actions();
	const auto children = win_->findChildren<QWidget *>();
	for (QWidget *c : children) actions += c->actions();
	for (QAction *a : std::as_const(actions)) {
		if (!a->isEnabled()) continue;
		const auto shortcuts = a->shortcuts();
		for (const QKeySequence &s : shortcuts)
			if (!s.isEmpty() && s == pressed) { a->trigger(); return true; }
	}
	return false;
}

void InputRouter::deliverKey(QWidget *target, const KeyEvent &k) {
	Qt::KeyboardModifiers mods;
	if (k.ctrl) mods |= Qt::ControlModifier;
	if (k.alt) mods |= Qt::AltModifier;
	if (k.shift) mods |= Qt::ShiftModifier;
	QKeyEvent press(QEvent::KeyPress, k.qtKey, mods, k.text);
	QApplication::sendEvent(target, &press);
	// Terminals have no key-release; fabricate one immediately (section 5.5).
	QKeyEvent release(QEvent::KeyRelease, k.qtKey, mods, k.text);
	QApplication::sendEvent(target, &release);

	// Arrow keys a focus widget ignored fall back to scrolling the nearest
	// scroll area -- the TUI convention (section 5.5).
	if (!press.isAccepted() && (k.qtKey == Qt::Key_Up || k.qtKey == Qt::Key_Down
		                        || k.qtKey == Qt::Key_PageUp || k.qtKey == Qt::Key_PageDown)) {
		if (auto *area = win_->findChild<QAbstractScrollArea *>()) {
			int step = GridMetrics::ch();
			if (k.qtKey == Qt::Key_PageUp || k.qtKey == Qt::Key_PageDown)
				step *= 5;
			const int dir = (k.qtKey == Qt::Key_Up || k.qtKey == Qt::Key_PageUp) ? -1 : 1;
			area->verticalScrollBar()->setValue(
				area->verticalScrollBar()->value() + dir * step);
		}
	}
}

void InputRouter::onKey(const KeyEvent &k) {
	for (const KeyEvent &q : std::as_const(quitKeys_))
		if (q.qtKey == k.qtKey && q.ctrl == k.ctrl && q.alt == k.alt) {
			qApp->quit();
			return;
		}
	if (k.qtKey == Qt::Key_Tab && !k.ctrl) {
		// Focus chain works without an active window (F4); drive it directly.
		QWidget *scope = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : win_;
		struct Probe : QWidget { using QWidget::focusNextPrevChild; };
		static_cast<Probe *>(scope)->focusNextPrevChild(!k.shift);
		setFocusWidget(scope->focusWidget());
	} else if (!matchShortcut(k)) {
		deliverKey(keyTarget(), k);
		setFocusWidget(win_->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frameRequested) frameRequested();
}

void InputRouter::onMouse(const MouseEvent &m) {
	const QPoint px(m.cell.x() * GridMetrics::cw() + GridMetrics::cw() / 2,
		            m.cell.y() * GridMetrics::ch() + GridMetrics::ch() / 2);
	// Popups first (top of stack down), then the window (section 5.5 routing order).
	QWidget *top = win_;
	const auto ps = popups();
	for (auto it = ps.rbegin(); it != ps.rend(); ++it)
		if ((*it)->geometry().contains(px)) { top = *it; break; }
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
		setFocusWidget(win_->focusWidget());
	}
	QCoreApplication::processEvents();
	if (frameRequested) frameRequested();
}

void InputRouter::onPaste(const QString &text) {
	QKeyEvent ev(QEvent::KeyPress, 0, Qt::NoModifier, text);
	QApplication::sendEvent(keyTarget(), &ev);
	if (frameRequested) frameRequested();
}

void InputRouter::onResize(QSize cells) {
	win_->resize(cells.width() * GridMetrics::cw(), cells.height() * GridMetrics::ch());
	QCoreApplication::processEvents();
	if (frameRequested) frameRequested();
}

void InputRouter::onFocusChange(bool) { if (frameRequested) frameRequested(); }

} // namespace Qtty
