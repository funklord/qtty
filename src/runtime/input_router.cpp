// src/runtime/input_router.cpp -- L5 (section 5.5), rules measured in section 16:
//   F3: synthetic keys never reach QShortcutMap -- the router owns shortcuts.
//   F4: no window activates -- window->focusWidget() is the key target.
//   F7: internally-created top-levels don't inherit WA_DontShowOnScreen --
//       stamp every one from a global filter, popup and modal dialog alike,
//       and track the popups' z-order for the compositor.
//   section 8.3: input outside activeModalWidget() is dropped before dispatch.
#include "qtty/runtime.h"
#include "qtty/drag.h"
#include "qtty/windows.h"
#include "qtty/grid.h"
#include "qtty/application.h"
#include <QtWidgets>
#include <QTextDocument>
#include <QTextBlock>
#include <QShortcut>

namespace Qtty {

// Off by default: see runtime.h. Process-wide rather than per-router
// because an application using exec() never holds the router, which is
// the same reason set_wide_clusters() and set_terminal_palette() are.
static bool s_conventions = false;
void set_keyboard_conventions(bool on) { s_conventions = on; }
bool keyboard_conventions() { return s_conventions; }

QVector<QPair<QString, QString>> keyboard_conventions_help() {
	// Written once, here, beside the code that acts on them. The list and
	// the behaviour drifting apart is the whole failure this exists to
	// prevent, so a binding added below must be added here in the same
	// edit -- and a check reads both.
	//
	// It answers "what does qtty respond to RIGHT NOW", which stopped
	// being the same as the opt-in bundle in 8.77: the context menu's
	// keyboard route works whether or not an application asked for the
	// conventions. Returning nothing while that key worked would have sent
	// an application back to hand-writing the row, which is the one thing
	// the guide's practice 8 exists to prevent. The empty answer was never
	// the contract -- "never promise a key that does nothing" was, and a
	// key that always works belongs in the list always.
	//
	// The QUIT KEYS are deliberately absent, and the same rule puts them
	// there: set_quit_keys() changes them per router, and this is a free
	// function with no router to ask, so naming Ctrl+C would promise a key
	// an application may have taken away. The same fault, pointed the
	// other way.
	const QPair<QString, QString> menu = {
		QStringLiteral("Menu/Shift+F10"), QStringLiteral("context menu")
	};
	if (!s_conventions) return { menu };
	return {
		{ QStringLiteral("Enter"),          QStringLiteral("activate") },
		{ QStringLiteral("Up/Down"),        QStringLiteral("move") },
		{ QStringLiteral("Ctrl+PgUp/PgDn"), QStringLiteral("tab") },
		{ QStringLiteral("F6"),             QStringLiteral("window") },
		{ QStringLiteral("F10"),            QStringLiteral("menu bar") },
		{ QStringLiteral("Alt+letter"),     QStringLiteral("jump to") },
		// The readline chords, QUALIFIED, because they answer only where a
		// caret is. The comment above says why Ctrl+C is not named at all --
		// naming a key the list cannot promise is the fault this function
		// exists to avoid -- and the same principle applies here with a
		// qualifier rather than a silence: these are always available, just
		// not always applicable, and a user reading "in text" is told
		// exactly when to expect them.
		{ QStringLiteral("Ctrl+A/E"),       QStringLiteral("line start/end, in text") },
		{ QStringLiteral("Ctrl+K/U"),       QStringLiteral("kill to end/start, in text") },
		{ QStringLiteral("Ctrl+W/D"),       QStringLiteral("rub out word, delete, in text") },
		menu,
	};
}

// The chords behind the rows above, in the same file and the same edit as
// the list itself, for the reason that list states: two copies of a binding
// drift, and the drift is silent. A check reads both.
//
// EVERY ROW OF THAT LIST IS HERE BUT ONE, and the check is a partition
// rather than a spelling comparison: a row shown up there either has a
// chord down here -- proved by binding it and requiring the report to name
// the row -- or it is `Alt+letter`, which is a FAMILY and not a chord. The
// letter there is whatever a tab, a mnemonic or a buddy label carries, so
// there is no fixed QKeySequence for a claim to be compared against, and
// mnemonic_conflicts() answers it per letter instead.
//
// `Enter` and `Up/Down` were left out until 8.254, on the ground that they
// answer only where the focused widget ignored the key -- true, and about a
// mechanism this table is not for. conventions_shadowed() reads SHORTCUT
// CLAIMS, and a claim is matched in match_shortcut() before deliver_key()
// offers the widget anything, so a QShortcut on Return takes Enter away
// exactly as one on Ctrl+K takes the readline kill. The same argument would
// have excluded Ctrl+A/E, which answer only where a caret is and are here.
struct ConventionRow {
	const char *shown;
	Qt::Modifier mod;
	Qt::Key keys[2];
	// Whether the row answers even with the convention bundle switched
	// off. The context-menu row does (8.77), being a platform behaviour
	// this library restores rather than a convention it offers.
	//
	// A FIELD, because the walk used to ask
	// `qstrcmp(row.shown, "Menu/Shift+F10") == 0` -- a fourth hand-written
	// copy of a string the table and the help list already spell, and one
	// the compiler cannot connect to either. Respelling the row, which is
	// display text and so exactly the kind of thing that gets reworded,
	// would have left the comparison matching nothing and silently stopped
	// the one row `conventions_shadowed()` reports with the bundle off --
	// green suite, quiet loss. This tree has the same shape recorded twice
	// already in `tools-check` and in the conventions partition.
	bool always;
};

static const ConventionRow k_convention_rows[] = {
	{ "Enter",          Qt::Modifier(0), { Qt::Key_Return,   Qt::Key_Enter },   false },
	{ "Up/Down",        Qt::Modifier(0), { Qt::Key_Up,       Qt::Key_Down },    false },
	{ "F6",             Qt::Modifier(0), { Qt::Key_F6,       Qt::Key(0) },      false },
	{ "F10",            Qt::Modifier(0), { Qt::Key_F10,      Qt::Key(0) },      false },
	{ "Ctrl+PgUp/PgDn", Qt::CTRL,        { Qt::Key_PageUp,   Qt::Key_PageDown },false },
	{ "Ctrl+A/E",       Qt::CTRL,        { Qt::Key_A,        Qt::Key_E },       false },
	{ "Ctrl+K/U",       Qt::CTRL,        { Qt::Key_K,        Qt::Key_U },       false },
	{ "Ctrl+W/D",       Qt::CTRL,        { Qt::Key_W,        Qt::Key_D },       false },
	{ "Menu/Shift+F10", Qt::SHIFT,       { Qt::Key_F10,      Qt::Key(0) },      true  },
};


// The quit keys an application asked for before any router existed, and the
// routers that are running now. Both halves are needed and neither is
// enough: an application picks its quit keys while building its window,
// which is long before exec() constructs a router, and one that changes
// them from a slot is talking about the router already driving the screen.
//
// A LIST of live routers rather than one pointer. exec() builds one, and
// this library's own suite builds hundreds -- and "the quit keys" is a
// property of the process, not of whichever router happens to be last.
static QVector<KeyEvent> s_quit_default = {
	KeyEvent{Qt::Key_C, QString(), true, false, false},
	KeyEvent{Qt::Key_D, QString(), true, false, false},
};
static QVector<InputRouter *> s_live_routers;

void set_quit_keys(const QVector<KeyEvent> &keys) {
	s_quit_default = keys;
	for (InputRouter *r : std::as_const(s_live_routers))
		r->set_quit_keys(keys);
}

InputRouter::InputRouter(QWidget *window) : win_(window) {
	quit_keys_ = s_quit_default;
	s_live_routers.append(this);
	qApp->installEventFilter(this);
}

InputRouter::~InputRouter() {
	s_live_routers.removeAll(this);
	if (qApp) qApp->removeEventFilter(this);
}

void InputRouter::set_quit_keys(const QVector<KeyEvent> &k) { quit_keys_ = k; }

QVector<QWidget *> InputRouter::popups() const {
	QVector<QWidget *> out;
	for (const auto &p : popups_) if (p && p->isVisible()) out.append(p.data());
	return out;
}

// A layer that is DRAWN is not always a layer that takes keys, and reading
// the stack as though it were is a second lockout of 8.194's shape.
//
// A Qt::ToolTip top-level owns no input anywhere: on a desktop it is a label
// that appears and goes away, and Qt's own QWidget::keyPressEvent closes a
// popup on Escape only when `windowType() == Qt::Popup` -- a tooltip is
// 0xd and does not match, so Qt offers no way out of one either. Measured
// before this split: with a Qt::ToolTip window up, keys went to it, nothing
// happened, and Escape did not give them back. Every other window kind
// recovers.
//
// So the stack has two readers now. popups() is what the COMPOSITOR draws,
// and it still carries every layer. input_popups() is what owns keys,
// shortcuts and the modal-style suppression, and it leaves out the layers
// that cannot answer: a tooltip, and anything the application declared
// transparent for input.
QVector<QWidget *> InputRouter::input_popups() const {
	QVector<QWidget *> out;
	const auto drawn = popups();
	for (QWidget *w : drawn) {
		if (w->windowType() == Qt::ToolTip) continue;
		if (w->windowFlags().testFlag(Qt::WindowTransparentForInput)) continue;
		// A LAYER THAT DEFERS ITS KEYS, said by Qt rather than guessed at.
		// `QCompleter::setPopup()` points its popup's focus proxy at the
		// widget being edited; a QMenu has none. Both are Qt::NoFocus, so
		// the policy cannot tell them apart and the proxy can.
		//
		// It matters because the completer's own event filter hides the
		// popup when the edited widget does not have focus -- and
		// hasFocus() is permanently false here. With the popup owning the
		// keys, every second letter typed went to the popup, the filter
		// forwarded it and then hid itself, so the list flickered out on
		// alternate keystrokes and Down/Return after a two-letter prefix
		// reached nothing. Measured before this: typing a-l-p-h-a gave
		// visible, hidden, visible, hidden, visible.
		//
		// On a desktop the keys go to the line edit, which HAS focus, and
		// the completer forwards them to the list itself. Leaving such a
		// popup out of this list puts the keys back where Qt expects them.
		QWidget *const proxy = w->focusProxy();
		if (proxy && !w->isAncestorOf(proxy)) continue;
		out.append(w);
	}
	return out;
}

// THE MASKED TYPE, not the bits. Qt's window types are a bitfield in which
// the interesting ones are supersets of each other: Qt::Popup is 0x9, and
// Qt::Tool is 0xb and Qt::SplashScreen 0xf -- both of which CONTAIN it. So
// `(flags & Qt::Popup) == Qt::Popup`, which reads as "is this a popup", is
// true for a palette window and for a splash screen as well.
//
// What that cost is measured in 8.194 and it is a lockout. A Qt::Tool window
// went on the popup stack, so the top popup owned input: keys typed with the
// focus in the MAIN window landed nowhere at all, the strip carried nothing
// so F6 had nowhere to go, and Esc does not dismiss a tool window. An
// application that opens a palette had a terminal it could not type into and
// could not get back from.
//
// windowType() is the same flags masked to WindowType_Mask, which is how Qt
// itself asks the question.
bool InputRouter::is_popup_layer(const QWidget *w) {
	const Qt::WindowType t = w->windowType();
	return t == Qt::Popup || t == Qt::ToolTip;
}

// section 8.3: while a modal is up it is the whole of the input tree. Nothing
// outside it may be reached -- not by a key, not by a shortcut, not by the
// arrow-key scroll fallback -- because the window manager that enforces that
// on the desktop is not here and Qt's own modal blocking runs in the platform
// layer we bypass with synthetic events.
QWidget *InputRouter::input_scope() const {
	QWidget *m = QApplication::activeModalWidget();
	if (m) return m;
	// cur_, not win_, once a compositor has told us the drawn window moved.
	// win_ is the window this router was CONSTRUCTED with, and a router that
	// never looked past it sent every key into the primary window however
	// many windows the application had opened -- F6 moved the picture and
	// left input behind, so text typed at the second window appeared in the
	// first one, off screen.
	//
	// Told rather than read. Reading Qtty::current_window() here was tried
	// and is wrong: a router is per-window and a suite builds many of them
	// over their own windows, so the global names a window most routers do
	// not serve. Measured: nine checks in two sections failed that way,
	// every one a router that had been sent keys meant for its own window.
	//
	// Null once the window has been destroyed, which is a state this class
	// can reach while it is still running: it posts a queued focus repair
	// from inside the window's own destructor (runtime.h says why), and
	// the repair is delivered after the memory is gone. Every caller that
	// dereferences this either checks it or sits behind one of the guards
	// at the top of on_key(), on_mouse(), on_paste() and on_resize().
	return cur_ ? cur_.data() : win_.data();
}

void InputRouter::set_input_window(QWidget *w) {
	// Called by the Compositor that draws this router's windows, from
	// Qtty::set_current_window() and its neighbours. An application moves
	// windows with THOSE -- calling this one directly would move input
	// without moving what is drawn, which is the defect it exists to fix.
	cur_ = w;
}

// A drawn layer that defers its keys to the widget being edited -- today
// that is a QCompleter's list, which Qt marks by pointing the popup's focus
// proxy at the editor (see input_popups()). Null unless such a layer is up
// and its proxy is where the focus actually is.
//
// It exists because the keys have to be SPLIT, which neither arrangement
// alone gets right. Give the popup everything, as a desktop's popup grab
// does, and Qt's own completer filter hides it after any key the editor
// accepts, because that branch checks `widget->hasFocus()` and no window
// activates here: measured, typing a-l-p-h-a flickered the list out on
// alternate letters. Give the editor everything and the list never sees
// Down, so it cannot be navigated at all -- measured too, Down Down Return
// leaving the text as typed.
//
// Qt's filter handles the navigation keys explicitly and returns before the
// focus test; only its ordinary-key branch reaches the hide. So the split
// is exactly that: navigation to the list, everything else to the editor.
QWidget *InputRouter::deferring_layer() const {
	QWidget *const fw = focusWidget();
	if (!fw) return nullptr;
	const auto drawn = popups();
	for (QWidget *w : drawn) {
		QWidget *const proxy = w->focusProxy();
		if (!proxy || w->isAncestorOf(proxy)) continue;
		if (proxy == fw || proxy->isAncestorOf(fw)) return w;
	}
	return nullptr;
}

// The keys a deferring layer answers: the ones Qt's completer filter handles
// itself, and no others. Home and End are deliberately absent -- they move a
// caret in the editor, which is where the text is.
static bool navigates_a_list(const KeyEvent &k) {
	switch (k.qt_key) {
	case Qt::Key_Up: case Qt::Key_Down:
	case Qt::Key_PageUp: case Qt::Key_PageDown:
	case Qt::Key_Return: case Qt::Key_Enter:
	case Qt::Key_Escape:
		return true;
	default:
		return false;
	}
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
	const QVector<QWidget *> open = input_popups();
	if (!open.isEmpty()) {
		QWidget *p = open.last();                 // topmost
		return p->focusWidget() ? p->focusWidget() : p;
	}
	// A KEYBOARD GRAB, which an application asks for with grabKeyboard()
	// and which this router used to ignore entirely. Measured before:
	// with the focus on one field and grabKeyboard() called on another,
	// a typed letter went to the FOCUSED one -- the opposite of what the
	// call means and of what every desktop does.
	//
	// The offscreen plugin refuses the grab and says so on stderr, which
	// is what made this look unfixable; Qt records the grabber anyway, so
	// QWidget::keyboardGrabber() is an honest answer and the router can
	// simply ask. Measured, none of Qt's OWN layers sets one here -- a
	// QMenu, a modal dialog and a combo box's dropdown all leave it null
	// -- so a non-null answer means the application asked, and nothing
	// else does.
	//
	// AFTER the popup stack, which is Qt's own order in
	// QApplication::notify(): popup mode is tested before the grabber, so
	// a menu opened by a program that had grabbed the keyboard still
	// answers its own arrows. And only for a grabber inside this router's
	// window, the same ownership rule the focus repairs follow.
	if (QWidget *g = QWidget::keyboardGrabber()) {
		for (QWidget *p = g; p; p = p->parentWidget())
			if (p == win_) return g;
	}
	QWidget *scope = input_scope();
	// No window, no target. A router whose window has been destroyed is
	// answering for nothing, and saying so is the only honest answer -- the
	// alternative was reading the focus widget out of a freed QWidget.
	if (!scope) return nullptr;
	return scope->focusWidget() ? scope->focusWidget() : scope;
}

// Defined below, beside the Tab handling it was written for. Declared here
// because the hide repair in the filter asks the same question Tab does.
static bool move_focus(QWidget *scope, bool forward);

bool InputRouter::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Polish) {
		if (auto *w = qobject_cast<QWidget *>(o))
			if (w->isWindow() && w != win_)
				w->setAttribute(Qt::WA_DontShowOnScreen);
	}
	// A WIDGET THAT HAD FOCUS AND IS BEING HIDDEN HANDS IT BACK.
	//
	// Qt does this itself and cannot do it here, for the reason this class
	// exists: QWidget::setVisible(false) moves focus on only when the widget
	// hasFocus(), which reads Qt's own focus_widget and is set only for an
	// ACTIVE window -- and no qtty window activates. QAbstractItemView's
	// closeEditor() is gated on the same predicate and so does the same
	// nothing, where on a desktop it calls setFocus() on the view.
	//
	// What that cost is one dead keystroke after EVERY commit and every
	// cancel in an item view. Measured: F2, type, Return -- the value is in
	// the model, the editor is gone, and the window's focusWidget is null,
	// so the next F2 reaches the window and does nothing at all. With the
	// conventions on, Down or Tab silently spends itself putting focus back;
	// with them off, only Tab does. F2 pressed twice edits one cell.
	//
	// The nearest focusable ANCESTOR, which for an editor is the view -- the
	// same widget Qt would have chosen -- and is reached without knowing
	// anything about item views. The walk stops at an ancestor that is
	// itself invisible, which is what keeps a closing dialog from handing
	// focus to the window behind it: hiding a parent hides its children
	// first, so the chain above a field in a dialog is already invisible by
	// the time this runs.
	if (e->type() == QEvent::Hide) {
		if (auto *w = qobject_cast<QWidget *>(o)) {
			QWidget *const had = Qtty::focusWidget();
			// AND ONLY FOR A WIDGET THIS ROUTER OWNS. The filter is
			// installed on the application, so every router alive sees
			// every hide -- and a router whose own window is untouched
			// would answer by syncing the focus to ITS window, which is
			// to say by throwing away the answer the right one just
			// worked out. Two routers is the test suite's ordinary
			// state and it is what caught this; an application has one,
			// so the fault would have waited for the first program that
			// opened a second window.
			//
			// The walk uses parentWidget() rather than isAncestorOf(),
			// which stops at a window boundary: a dialog is a window
			// and is still this router's to answer for.
			bool mine = false;
			for (QWidget *p = w; p && !mine; p = p->parentWidget())
				mine = p == win_;
			if (mine && had && (had == w || w->isAncestorOf(had))) {
				// The nearest focusable ANCESTOR, captured NOW while the
				// chain is still whole -- for an item view's editor that
				// is the view, the same widget Qt would have chosen, and
				// it is reached without knowing anything about item
				// views. The walk stops at an ancestor that is itself
				// invisible, which is a dialog closing: its children are
				// hidden with it, so there is nothing to hand focus to
				// inside it.
				QPointer<QWidget> up = w->parentWidget();
				while (up && (up->focusPolicy() == Qt::NoFocus
				              || !up->isVisible() || !up->isEnabled()))
					up = up->isWindow() ? nullptr : up->parentWidget();
				// QUEUED, and that is the whole of what took three
				// measurements to get right. Acting inside the hide
				// repairs a focus Qt has not finished moving: a
				// QStackedWidget hides the old page BEFORE it shows the
				// new one, so a repair that runs here walks past the
				// page about to appear and lands on the first tab stop
				// in the window -- a user changing page would find
				// focus at the top of the form.
				//
				// Asked after the event completes, the condition is
				// exact and is the one actually meant: the window has
				// no focus widget at all. Where something else has
				// already chosen one, this does nothing.
				QMetaObject::invokeMethod(this, [this, up] {
					QWidget *const scope = input_scope();
					if (!scope || !scope->isVisible()) return;
					QWidget *const now = scope->focusWidget();
					if (now && now->isVisible()) {
						set_focus_widget(now);
						return;
					}
					if (up && up->isVisible() && up->isEnabled())
						up->setFocus(Qt::OtherFocusReason);
					else
						move_focus(scope, true);
					set_focus_widget(scope->focusWidget());
				}, Qt::QueuedConnection);
			}
		}
	}
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
				} else {
					// A LAYER THAT OPENS WITH NOTHING FOCUSED SWALLOWS
					// WHAT THE USER TYPES FIRST. Qt gives a window its
					// first tab stop when the window ACTIVATES, and none
					// activates here -- the same predicate behind the
					// repair in the Hide branch above.
					//
					// Measured, with a QDialog holding two fields and a
					// default button: `dlg.focusWidget()` is null,
					// key_target() is the QDialog itself, and the two
					// keys a user types next land nowhere at all. Tab
					// rescues it and nothing else does. Through exec(),
					// which is how dialogs are actually opened, the
					// answer is the same.
					//
					// A QMessageBox is FINE, because Qt focuses its own
					// default button on the way up -- so the case that
					// breaks is the ordinary application's own dialog,
					// and the case that works is the one a library
					// author would have tested with.
					//
					// Queued, and that is what keeps this from fighting
					// the widgets that do it themselves: by the time it
					// runs, a QMessageBox already has a focus widget and
					// this does nothing. It also declines to overrule an
					// application that called setFocus() before show().
					// AND ONLY A LAYER THIS ROUTER OWNS, which the
					// stamping above deliberately does NOT ask -- a
					// window must be stamped by whoever sees it first,
					// while a focus repair belongs to one router. The
					// suite caught this the same way it caught the
					// identical omission in the Hide branch: a fixture
					// whose window an EARLIER router repaired had its
					// paging key answered by a widget that should not
					// have had focus, and PageUp scrolled three rows
					// where the arrow fallback gives five.
					bool mine = false;
					for (QWidget *p = w; p && !mine; p = p->parentWidget())
						mine = p == win_;
					if (!mine) return false;
					QPointer<QWidget> layer = w;
					QMetaObject::invokeMethod(this, [this, layer] {
						if (!layer || !layer->isVisible()) return;
						if (layer->focusWidget()) return;
						move_focus(layer, true);
						if (input_scope() == layer)
							set_focus_widget(layer->focusWidget());
					}, Qt::QueuedConnection);
				}
			}
		}
	} else if (e->type() == QEvent::Hide) {
		if (auto *w = qobject_cast<QWidget *>(o)) {
			if (popups_.removeAll(QPointer<QWidget>(w)) && frame_requested)
				frame_requested();
			// The menu bar's keyboard mode is over when the last popup
			// goes, so give the focus back to whoever F10 took it from.
			// Only when it is the BAR that holds it: a menu item that
			// moved the focus itself has said where it wants it, and an
			// application closing its own menu is not asking for this.
			//
			// QUEUED, for the reason 8.210 records: asked inside the
			// hide, the bar has not taken the focus yet -- Qt moves it
			// on the way OUT of keyboard mode, after this event -- so
			// the condition read false and the repair did nothing.
			// Measured exactly that way before the deferral went in.
			if (popups_.isEmpty() && before_menu_bar_) {
				QPointer<QWidget> back = before_menu_bar_;
				before_menu_bar_.clear();
				QMetaObject::invokeMethod(this, [this, back] {
					if (!back || !back->isVisible()) return;
					QWidget *const scope = input_scope();
					if (!scope) return;
					if (!qobject_cast<QMenuBar *>(scope->focusWidget()))
						return;
					back->setFocus(Qt::OtherFocusReason);
					set_focus_widget(back);
				}, Qt::QueuedConnection);
			}
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
// Move focus one step through the layer's focus chain.
//
// NOT `static_cast<Probe *>(w)->focusNextPrevChild()`, which is what stood
// here: `focusNextPrevChild` is protected, and the trick to reach it is to
// declare a fake derived class and cast a widget that is not one to it.
// That is undefined behaviour, and UndefinedBehaviorSanitizer says so --
// `__ubsan_handle_dynamic_type_cache_miss`, a vptr that is not the type
// the cast claimed.
//
// It went unseen because nothing REACHED it: 8.39 measured the Tab
// fallback as never running, Qt's own `QWidget::event()` taking Tab first.
// The arrow conventions of 8.66 call it on every press, so a dormant piece
// of undefined behaviour became a live one, and the sanitizer found it
// within a day.
//
// `nextInFocusChain()` is public and defined. The chain is circular, so
// returning to where it started is the termination condition; the counter
// beside it bounds a chain corrupted by something else.
//
// The filter is `tab_stop` below rather than three lines here, because two
// things ask the same question: this, which moves to the next stop, and
// `keyboard_reachable`, which lists them all so a test can assert on them.
// An application walking `nextInFocusChain()` itself would be asserting on
// Qt's focus order rather than on the order qtty actually moves through,
// which is not the question the test means to ask.
static bool tab_stop(QWidget *scope, QWidget *w) {
	if (w != scope && !scope->isAncestorOf(w)) return false;
	if (!w->isVisible() || !w->isEnabled()) return false;
	return (w->focusPolicy() & Qt::TabFocus) != 0;
}

static bool move_focus(QWidget *scope, bool forward) {
	QWidget *const start = scope->focusWidget() ? scope->focusWidget() : scope;
	QWidget *w = start;
	for (int guard = 0; guard < 4096; ++guard) {
		w = forward ? w->nextInFocusChain() : w->previousInFocusChain();
		if (!w || w == start) return false;
		if (!tab_stop(scope, w)) continue;
		w->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
		return true;
	}
	return false;
}

// Every widget Tab reaches inside `scope`, in the order it reaches them.
//
// `doc/keyboard-first.md` asks an implementer to assert that every control
// is reachable by key, and said the loop takes ten lines. It does not: it
// is the traversal above, and the ten-line version an application writes
// for itself gets the filter wrong in the direction that hides the fault
// -- an invisible widget, one outside the scope, or one whose focus policy
// excludes Tab all appear in a raw walk of the chain and are not stops.
// A test built on that reports controls reachable that a user cannot get
// to, which is the one answer it exists to rule out.
//
// The complement was refused here for a while, on the grounds that naming
// what SHOULD have been reachable means deciding which widgets are
// controls. That is right about widgets in general and wrong about one
// population: `QAbstractButton` is Qt's own word for a thing that answers
// a click, so pointer_only() below asks the question over a set nobody
// has to judge. What that entry missed is the other half -- the
// subtraction needs the claim enumerations, and an application writing
// the complement itself gets it wrong in the direction that invents a
// fault, naming a toolbar button whose action a mnemonic already reaches.
QVector<QWidget *> keyboard_reachable(QWidget *scope) {
	QVector<QWidget *> out;
	if (!scope) return out;
	if (tab_stop(scope, scope)) out.append(scope);
	QWidget *w = scope;
	for (int guard = 0; guard < 4096; ++guard) {
		w = w->nextInFocusChain();
		if (!w || w == scope) break;
		if (tab_stop(scope, w)) out.append(w);
	}
	return out;
}

static QChar mnemonic_of(const QString &text) {
	for (int i = 0; i + 1 < text.size(); ++i) {
		if (text.at(i) != QLatin1Char('&')) continue;
		if (text.at(i + 1) == QLatin1Char('&')) { ++i; continue; }
		return text.at(i + 1).toLower();
	}
	return QChar();
}

// Everything in `scope` that claims an Alt+letter, in the ORDER THE ROUTER
// TRIES THEM: actions first, then buttons and buddy labels. One enumeration
// rather than two, because the matcher and mnemonic_conflicts() disagreeing
// about who claims what would make the report describe a different program
// from the one the keys reach -- and a report nobody can trust is worse than
// none.
//
// The filters are part of the claim rather than of the matching. A disabled
// action, a hidden button and a label whose buddy is hidden do not claim
// their letter at all: the router walks past them to whatever is next, so
// naming them in a conflict report would invent a collision that does not
// exist.
struct MnemonicClaim {
	QChar letter;
	QObject *who;
	QString text;
	int index = -1;               // the tab, where `who` is a QTabBar
	// WHICH POPULATION, because the order between them is a decision and the
	// order within one is an accident. 8.154 settled the first: a menu's
	// `&File` beats a button's `&Format`, the menu being the older meaning.
	// Nothing settled the second, so two buttons claiming one letter were
	// answered in findChildren order -- measured, with the focus in the
	// right-hand panel, Alt+S fired the LEFT panel's button. Same shape as
	// the chord ambiguity of 8.174, one mechanism along.
	int rank = 0;                 // 0 actions, 1 buttons and labels, 2 tabs
};

static QVector<MnemonicClaim> mnemonic_claims(QWidget *scope) {
	QVector<MnemonicClaim> out;
	if (!scope) return out;
	// ONE CLAIM PER OBJECT. An action reached by two routes is one action,
	// and counting it twice makes it collide with itself -- measured against
	// Qt's own `menus` example, where 32 actions enumerate to 30 distinct
	// ones and the report accused `&Edit` and `&Help` of claiming their own
	// letters twice. The matcher does not care, since it acts on the first
	// match either way; a report of collisions cares completely.
	QSet<const QObject *> counted;
	for (QAction *a : mnemonic_actions(scope)) {
		if (!a->isEnabled() || a->isSeparator()) continue;
		if (counted.contains(a)) continue;
		counted.insert(a);
		const QChar m = mnemonic_of(a->text());
		if (!m.isNull()) out.append({m, a, a->text(), -1, 0});
	}
	for (QWidget *w : scope->findChildren<QWidget *>()) {
		if (!w->isVisible() || !w->isEnabled()) continue;
		if (auto *b = qobject_cast<QAbstractButton *>(w)) {
			const QChar m = mnemonic_of(b->text());
			if (!m.isNull()) out.append({m, b, b->text(), -1, 1});
		} else if (auto *l = qobject_cast<QLabel *>(w)) {
			QWidget *buddy = l->buddy();
			if (!buddy || !buddy->isVisible() || !buddy->isEnabled()) continue;
			const QChar m = mnemonic_of(l->text());
			if (!m.isNull()) out.append({m, l, l->text(), -1, 1});
		}
	}
	// TABS LAST, and only with the conventions on, because both facts are
	// about where this claim is resolved. A tab's letter is not matched by
	// match_mnemonic() at all -- it is answered further down, after the key
	// has been delivered and refused, in the block the conventions gate. So
	// it loses to an action, a button or a buddy label claiming the same
	// letter, and the order here is what makes the report say so; and with
	// the conventions off it answers nothing, so listing it would invent a
	// collision rather than report one.
	if (keyboard_conventions()) {
		for (QTabBar *bar : scope->findChildren<QTabBar *>()) {
			if (!bar->isVisible()) continue;
			for (int i = 0; i < bar->count(); ++i) {
				if (!bar->isTabEnabled(i)) continue;
				const QChar m = mnemonic_of(bar->tabText(i));
				if (!m.isNull()) out.append({m, bar, bar->tabText(i), i, 2});
			}
		}
	}
	return out;
}

// How far a mnemonic claim is from the focus, by the same measure the chord
// matcher uses: steps from the focused widget up to the claim's own widget,
// or to a widget the claim's action belongs to. Far when it is not above the
// focus at all -- a menu bar's action, a button in another panel.
// Defined with the chord matcher below, and needed here: a claim's widgets
// are the same question for a letter as for a chord.
static QVector<const QWidget *> owners_of(const QAction *a);

static int mnemonic_distance(const MnemonicClaim &c, const QWidget *fw) {
	const int far = 1000;
	if (!fw) return far;
	QVector<const QWidget *> owners;
	if (const auto *a = qobject_cast<const QAction *>(c.who))
		owners = owners_of(a);
	else if (const auto *w = qobject_cast<const QWidget *>(c.who))
		owners.append(w);
	// CONTAINMENT, not identity, and the difference decides the case this
	// exists for. A button claiming a letter is a SIBLING of the focused
	// field rather than an ancestor of it, so an identity test scores every
	// button `far` and the tie goes back to construction order -- measured,
	// with both panels' buttons scoring far and the left one answering. What
	// separates them is which enclosing widget holds each: the focused
	// panel holds one of them at distance 1, the window holds the other at
	// 2.
	//
	// The chord matcher below measures identity and is right to: an action
	// is associated with the container itself, so containment buys it
	// nothing -- and its sabotage said so by reddening no check.
	int best = far;
	for (const QWidget *o : std::as_const(owners)) {
		int steps = 0;
		for (const QWidget *w = fw; w; w = w->parentWidget(), ++steps)
			if (w == o || w->isAncestorOf(o)) {
				best = qMin(best, steps);
				break;
			}
	}
	return best;
}

// The letters more than one control answers to, and who answers -- in the
// order above, so the first name in each list is the one that WINS and the
// rest are unreachable by that key.
//
// It exists because doc/keyboard-first.md's first and highest-value practice
// is "give every control a mnemonic", and the failure that practice creates
// scales with how well it is followed: the more letters an application
// claims, the likelier two claims collide, and the loser is silent. There is
// nothing to see on the screen and nothing in the log -- the key simply does
// the other thing, for ever.
//
// Empty is the answer an application's test asserts, which is the same shape
// as keyboard_reachable(): the library owns the traversal, the application
// owns the assertion.
QVector<QPair<QChar, QStringList>> mnemonic_conflicts(QWidget *scope) {
	QVector<QPair<QChar, QStringList>> out;
	QVector<QChar> seen;
	const QVector<MnemonicClaim> claims = mnemonic_claims(scope);
	for (const MnemonicClaim &c : claims) {
		if (seen.contains(c.letter)) continue;
		seen.append(c.letter);
		QStringList who;
		for (const MnemonicClaim &other : claims)
			if (other.letter == c.letter) who << other.text;
		if (who.size() > 1) out.append({c.letter, who});
	}
	return out;
}

bool InputRouter::match_mnemonic(const KeyEvent &k) {
	// A mnemonic arrives as Alt with text and no Qt::Key: the terminal sends
	// ESC then the letter, and there is no key code to be had. That is why
	// match_shortcut() cannot serve -- it returns false on the first line for
	// want of a qt_key.
	if (!k.alt || k.text.size() != 1) return false;
	const QChar want = k.text.at(0).toLower();
	if (!want.isLetterOrNumber()) return false;

	// ONE list, in the router's own order, and the same list the conflict
	// report reads. It used to be two loops here -- actions, then buttons and
	// buddy labels -- and a second copy of the enumeration would have meant a
	// report that could disagree with the keys.
	//
	// After the actions, not before: a menu's `&File` and a button's `&File`
	// in the same window is a collision the application made, and the menu is
	// the older meaning. mnemonic_conflicts() is how an application finds out
	// it made one.
	// BY POPULATION FIRST, THEN BY NEARNESS. The order between populations
	// is 8.154's decision and is kept: a menu's letter beats a button's. The
	// order WITHIN one was findChildren order, which is where the widgets
	// happen to have been built -- so two buttons claiming one letter
	// answered the same way wherever the focus was. Ties keep enumeration
	// order, so a letter with one claimant is untouched.
	const QVector<MnemonicClaim> claims = mnemonic_claims(input_scope());
	const MnemonicClaim *pick = nullptr;
	int pick_rank = 0, pick_distance = 0;
	for (const MnemonicClaim &claim : claims) {
		if (claim.letter != want) continue;
		const int d = mnemonic_distance(claim, focusWidget());
		if (!pick || claim.rank < pick_rank
		    || (claim.rank == pick_rank && d < pick_distance)) {
			pick = &claim;
			pick_rank = claim.rank;
			pick_distance = d;
		}
	}
	if (pick) {
		const MnemonicClaim &claim = *pick;
		if (auto *a = qobject_cast<QAction *>(claim.who)) {
			if (QMenu *sub = a->menu()) {
				// A menu opens rather than triggers.
				QWidget *owner = a->associatedObjects().isEmpty()
				    ? nullptr
				    : qobject_cast<QWidget *>(a->associatedObjects().first());
				if (auto *bar = qobject_cast<QMenuBar *>(owner)) {
					// The BAR opens it, rather than this function calling
					// popup() at a position it worked out itself. The
					// position was the visible half and the smaller half:
					// popup() leaves QMenuPrivate::causedPopup unset, so the
					// menu does not know which bar it belongs to and the bar
					// does not know it is open. QMenu::keyPressEvent's
					// menu-bar traversal tests
					// qobject_cast<QMenuBar *>(topCausedWidget()) and could
					// therefore never fire, and QMenu::hideEvent's matching
					// clean-up could not either.
					//
					// Measured, Alt-F against a bar holding File and Edit:
					// with popup(), activeAction() stayed null, the bar drew
					// "File" no differently from "Edit", and Right did
					// nothing. With setActiveAction() the bar reports &File,
					// draws it marked, and Right closes File and opens Edit.
					//
					// QMenuBarPrivate::popupAction() does the placing, under
					// the item the menu belongs to, which is the same
					// position the hand-computed one aimed at -- so this
					// drops code rather than adding it.
					bar->setActiveAction(a);
					return true;
				}
				// Anywhere else there is no bar to ask, so the widget's own
				// corner it is: a submenu at the terminal's origin -- which
				// is what a null owner gives -- appears with nothing beside
				// it to say what it belongs to.
				sub->popup(owner ? owner->mapToGlobal(QPoint(0, 0))
				                 : QPoint(0, 0));
				return true;
			}
			a->trigger();
			return true;
		}
		// BUTTONS AND BUDDIES, which are not actions and were therefore
		// unreachable before 8.31. On a desktop `&Apply` on a push button is
		// activated by Alt+A -- Qt registers a shortcut for the ampersand --
		// and `&Name:` on a label moves focus to the field it is the buddy
		// of. Both are how a person without a mouse reaches a control
		// DIRECTLY rather than tabbing to it, so on a terminal they matter
		// more than on the desktop.
		if (auto *b = qobject_cast<QAbstractButton *>(claim.who)) {
			// click() rather than animateClick(): a terminal has no animation
			// to wait for, and animateClick defers the signal by a timer an
			// application would have to spin for.
			b->click();
			return true;
		}
		if (auto *l = qobject_cast<QLabel *>(claim.who)) {
			// A buddy that has gone since the claim was made: the letter
			// answers nothing rather than reaching for a null. It cannot
			// fall through to another claimant here -- the pick above is
			// the claimant -- and a label whose buddy is missing claimed
			// nothing in the first place.
			if (QWidget *buddy = l->buddy()) {
				buddy->setFocus(Qt::ShortcutFocusReason);
				set_focus_widget(buddy, Qt::ShortcutFocusReason);
				return true;
			}
		}
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

// Emit QShortcut::activated().
//
// invokeMethod because activated() is a signal and a signal cannot be emitted
// from outside its class. The meta-object system can, which is the one
// supported way to do this; the return value is read rather than discarded,
// since a rename upstream would otherwise fail silently and leave every
// shortcut dead again for a new reason.
static bool fire(QShortcut *sc) {
	const bool sent = QMetaObject::invokeMethod(sc, "activated");
	Q_ASSERT_X(sent, "match_shortcut", "QShortcut::activated missing");
	return sent;
}

// The windows an application-context shortcut may be found in: every visible
// top-level except the scope, which has already been searched, and except the
// popups, whose keys the caller has already decided about.
static QVector<QWidget *> other_windows(const QWidget *scope) {
	QVector<QWidget *> out;
	// THE STRIP'S ORDER FIRST, which is the order the user sees. An
	// application-context claim in another window is `far` from the focus by
	// definition, so every one of them ties and the tie goes to this list --
	// and this list used to be QApplication::topLevelWidgets(), whose order
	// is Qt's bookkeeping. Measured, four windows in one program:
	//
	//     strip [root, b, a, c]      Qt [b, root, a, c]
	//
	// They disagree, so which window answered a chord was decided by
	// something no user can see. 8.177 gave the strip an order that does not
	// change; this is the other consumer of the one it replaced.
	for (QWidget *w : window_tabs()) {
		if (w == scope || !w->isVisible()) continue;
		if (InputRouter::is_popup_layer(w)) continue;
		out.append(w);
	}
	// Then whatever the strip does not carry: a modal is not on it, and
	// neither is anything while there is only one window. Qt's order for
	// these, because there is nothing better and they are the exception
	// rather than the rule.
	for (QWidget *w : QApplication::topLevelWidgets()) {
		if (w == scope || !w->isVisible()) continue;
		if (InputRouter::is_popup_layer(w)) continue;
		if (!out.contains(w)) out.append(w);
	}
	return out;
}

// Does a shortcut with this context apply right now?
//
// QAction::shortcutContext() was ignored entirely: the action arm collected
// everything in the scope and fired whatever matched, so an action asking for
// Qt::WidgetShortcut answered while its widget was not focused -- a key the
// desktop would have left alone, which is the direction that changes what an
// application does rather than merely failing to. The QShortcut arm honoured
// context from the day it was written, and the two arms disagreeing is the
// kind of gap nobody meets until their own binding misbehaves.
//
// THE WINDOW CASE IS THE ONE WITH A TRAP IN IT, and the first version of this
// fell in. A QMenu is a top-level widget carrying Qt::Popup, so "is it in this
// window" cannot be asked as w->window() == scope -- the answer is the menu
// itself -- AND IT CANNOT BE ASKED WITH QWidget::isAncestorOf() EITHER, which
// is what this tried first. Qt's implementation refuses at a window boundary:
//
//     while (child) { if (child == this) return true;
//                     if (child->isWindow()) return false;
//                     child = child->parentWidget(); }
//
// A popup is a window, so it returns false on the first step and every menu
// action's shortcut in every application stops working. The check written for
// this case caught it on the first run, which is the only reason the wrong
// version never left the tree: nothing here had ever bound a shortcut to a
// menu action, so the whole behaviour rested on a path with no assertion over
// it.
//
// So walk parentWidget() and do not stop at windows. That is the chain Qt
// itself uses to decide where a popup belongs.
static bool context_applies(Qt::ShortcutContext ctx, const QWidget *owner,
                            const QWidget *scope, const QWidget *fw) {
	switch (ctx) {
	case Qt::ApplicationShortcut:
		return true;
	case Qt::WidgetShortcut:
		return owner && owner == fw;
	case Qt::WidgetWithChildrenShortcut:
		return owner && fw && (owner == fw || owner->isAncestorOf(fw));
	case Qt::WindowShortcut:
	default:
		if (!owner || !scope) return false;
		for (const QWidget *w = owner; w; w = w->parentWidget())
			if (w == scope) return true;
		return false;
	}
}

// The widgets an action belongs to, for the purpose above: the ones it was
// added to, and its parent if it was added to nothing. Qt matches a
// widget-context shortcut against any of them, so this answers as a list
// rather than picking one.
static QVector<const QWidget *> owners_of(const QAction *a) {
	QVector<const QWidget *> out;
	const auto associated = a->associatedObjects();
	for (QObject *o : associated)
		if (auto *w = qobject_cast<QWidget *>(o)) out.append(w);
	if (out.isEmpty())
		if (auto *p = qobject_cast<QWidget *>(a->parent())) out.append(p);
	return out;
}

static bool action_context_applies(const QAction *a, const QWidget *scope,
                                   const QWidget *fw) {
	const Qt::ShortcutContext ctx = a->shortcutContext();
	if (ctx == Qt::ApplicationShortcut) return true;
	const QVector<const QWidget *> owners = owners_of(a);
	// An action belonging to no widget at all is left alone rather than
	// refused: there is nothing to scope it by, and refusing would silently
	// disable a binding that works today.
	if (owners.isEmpty()) return true;
	for (const QWidget *w : owners)
		if (context_applies(ctx, w, scope, fw)) return true;
	return false;
}

// Whether a QShortcut's context lets it answer with `fw` focused. Extracted
// so that the matcher and shortcut_conflicts() cannot disagree: the report
// exists to say which chord answers twice, and one built on its own copy of
// this rule would be describing a different program.
static bool shortcut_context_applies(const QShortcut *sc, const QWidget *fw) {
	const QWidget *const owner = qobject_cast<QWidget *>(sc->parent());
	if (sc->context() == Qt::WidgetShortcut)
		return owner && owner == fw;
	if (sc->context() == Qt::WidgetWithChildrenShortcut)
		return owner && fw && (owner == fw || owner->isAncestorOf(fw));
	return true;
}

// Everything that claims a chord, in THE ORDER match_shortcut() tries them:
// actions in the scope, application-context actions in other windows, the
// scope's QShortcuts, and application-context QShortcuts elsewhere. Four
// populations, named here once, because a report that knows three of them
// tells an application its chords are unique when they are not -- which is
// 8.155 exactly, and it cost a day the first time.
//
// A claim per (object, sequence): an action may carry several, and a chord
// claimed by one of them is claimed.
struct ShortcutClaim {
	QKeySequence key;
	QObject *who;
	QString text;
	bool application = false;     // reached from outside the scope
};

static QString claim_label(const QObject *who, const QKeySequence &key) {
	if (const auto *a = qobject_cast<const QAction *>(who)) {
		if (!a->text().isEmpty()) return a->text();
		if (!a->objectName().isEmpty()) return a->objectName();
		return QStringLiteral("a QAction");
	}
	if (!who->objectName().isEmpty()) return who->objectName();
	const QObject *const owner = who->parent();
	return QStringLiteral("a QShortcut(%1) on %2")
	    .arg(key.toString(),
	         owner ? QString::fromLatin1(owner->metaObject()->className())
	               : QStringLiteral("nothing"));
}

static QVector<ShortcutClaim> shortcut_claims(QWidget *scope) {
	QVector<ShortcutClaim> out;
	if (!scope) return out;
	// One claim per object, for the reason mnemonic_claims() gives: an action
	// added to a window and to a menu is reached twice by this walk and is
	// still one action. Kept by object rather than by (object, sequence),
	// since the second visit brings the same sequences as the first.
	QSet<const QObject *> counted;
	const auto add_action = [&out, &counted](QAction *a, bool app) {
		if (!a->isEnabled()) return;
		if (app && a->shortcutContext() != Qt::ApplicationShortcut) return;
		if (counted.contains(a)) return;
		counted.insert(a);
		const auto keys = a->shortcuts();
		for (const QKeySequence &s : keys)
			if (!s.isEmpty()) out.append({s, a, claim_label(a, s), app});
	};
	QList<QAction *> actions = scope->actions();
	for (QWidget *c : scope->findChildren<QWidget *>()) actions += c->actions();
	for (QAction *a : std::as_const(actions)) add_action(a, false);
	for (QWidget *w : other_windows(scope)) {
		QList<QAction *> theirs = w->actions();
		for (QWidget *c : w->findChildren<QWidget *>()) theirs += c->actions();
		for (QAction *a : std::as_const(theirs)) add_action(a, true);
	}
	const auto add_shortcut = [&out, &counted](QShortcut *sc, bool app) {
		if (!sc->isEnabled() || sc->key().isEmpty()) return;
		if (app && sc->context() != Qt::ApplicationShortcut) return;
		if (counted.contains(sc)) return;
		counted.insert(sc);
		out.append({sc->key(), sc, claim_label(sc, sc->key()), app});
	};
	for (QShortcut *sc : scope->findChildren<QShortcut *>())
		add_shortcut(sc, false);
	for (QWidget *w : other_windows(scope))
		for (QShortcut *sc : w->findChildren<QShortcut *>())
			add_shortcut(sc, true);
	return out;
}

// How far a claim's owner is from the focused widget, walking up. 0 when the
// owner IS the focus widget, 1 for its parent, and so on; a large number when
// the owner is not above the focus at all -- an application-context claim from
// another window, or an action belonging to no widget.
//
// This is what decides between claims that ALL apply. Qt's own MDI is the
// case that asked for it: every subwindow's system menu carries the same
// `&Close` on Ctrl+F4, with window context, in one window -- so the chord is
// ambiguous by construction, and answering the first in enumeration order
// closed a document the user was not in. The nearest owner is the subwindow
// the focus is inside, which is the one they meant.
//
// A desktop Qt does not decide this either: QShortcutMap reports an ambiguity
// and cycles between the claimants. Cycling needs a memory of what answered
// last and gives a user a chord that does something different each press;
// nearest-to-focus gives the same answer every time and the answer they
// expect.
static int claim_distance(const ShortcutClaim &c, const QWidget *fw) {
	const int far = 1000;
	if (!fw || c.application) return far;
	QVector<const QWidget *> owners;
	if (const auto *a = qobject_cast<const QAction *>(c.who))
		owners = owners_of(a);
	else if (const auto *sc = qobject_cast<const QShortcut *>(c.who)) {
		if (auto *pw = qobject_cast<QWidget *>(sc->parent())) owners.append(pw);
	}
	if (owners.isEmpty()) return far;
	// Identity, and only identity. A containment test was written here first
	// -- the nearest ancestor that IS the owner or CONTAINS it -- on the
	// theory that an action's owner is often a menu and a menu is nobody's
	// ancestor. It is not needed: Qt associates `&Close` with the SUBWINDOW
	// as well as with its system menu, so the subwindow the focus sits in
	// answers by identity alone.
	//
	// Removed rather than kept as insurance, because nothing could prove it:
	// its sabotage reddened no check, which is the harness saying a rule has
	// no defender. What had actually failed while it looked necessary was a
	// check asserting on a QPointer to a subwindow that QMdiArea deletes a
	// turn after closing.
	int best = far;
	for (const QWidget *o : std::as_const(owners)) {
		int steps = 0;
		for (const QWidget *w = fw; w; w = w->parentWidget(), ++steps)
			if (w == o) {
				best = qMin(best, steps);
				break;
			}
	}
	return best;
}

// Whether a claim answers with `fw` focused. An application-context claim
// from another window answers wherever you are -- that is what the context
// means -- and the rest are asked through the router's own predicates.
static bool claim_applies(const ShortcutClaim &c, QWidget *scope,
                          const QWidget *fw) {
	if (c.application) return true;
	if (const auto *a = qobject_cast<const QAction *>(c.who))
		return action_context_applies(a, scope, fw);
	if (const auto *sc = qobject_cast<const QShortcut *>(c.who))
		return shortcut_context_applies(sc, fw);
	return false;
}

// The chords more than one thing answers, and who answers -- first the one
// that wins, then the ones that never fire.
//
// Qt reports this on a desktop and cannot here: QShortcutMap is what detects
// an ambiguous binding, it gates on the window being active, and no window
// activates under qtty (section F4). So the toolkit's own answer to the
// question is gone, and this is what replaces it.
//
// CONTEXT DECIDES, which is what keeps the answer honest. Two
// WidgetShortcut claims on different widgets are not a conflict -- only one
// of them can ever be in play -- so the chord is reported only where some
// focus a user can reach makes two claims answer at once. The focus
// candidates are keyboard_reachable(), plus nothing focused at all, which is
// the state a window starts in.
// Every focus a user can reach in `scope`, which is what the report above
// promises and is NOT the tab chain. Three routes put focus somewhere here,
// and only the first is keyboard_reachable():
//
//   Tab and Backtab        the tab stops
//   a label's mnemonic     its buddy, whatever the buddy's focus policy --
//                          match_mnemonic() calls setFocus() on it, and
//                          Qt's setFocus() does not consult the policy
//   a click                anything not Qt::NoFocus, a terminal having a
//                          mouse like any other screen
//
// Measured before this existed: a `Qt::ClickFocus` line edit named by a
// `&Notes` label is no tab stop, `Alt+N` focuses it, and two WidgetShortcut
// claims on it both answer there -- an ambiguity a user reaches with one
// key, reported as no conflict at all. An under-report is the worse
// direction here: a false name wastes somebody's afternoon, and a missing
// one leaves the silent loser this function exists to find.
static QVector<QWidget *> focus_candidates(QWidget *scope) {
	QVector<QWidget *> out;
	if (!scope) return out;
	const auto add = [&out](QWidget *w) {
		if (w && !out.contains(w)) out.append(w);
	};
	const auto focusable = [](const QWidget *w) {
		return w->isVisible() && w->isEnabled()
		       && w->focusPolicy() != Qt::NoFocus;
	};
	for (QWidget *w : keyboard_reachable(scope)) add(w);
	if (focusable(scope)) add(scope);
	const auto children = scope->findChildren<QWidget *>();
	for (QWidget *w : children)
		if (focusable(w)) add(w);
	const QVector<MnemonicClaim> letters = mnemonic_claims(scope);
	for (const MnemonicClaim &c : letters)
		if (auto *l = qobject_cast<QLabel *>(c.who)) add(l->buddy());
	return out;
}

// Which rows of keyboard_conventions_help() this window has taken back. The
// TABLE lives beside that list, where the rule about two copies of a binding
// put it; the walk lives here, where the claim enumeration it has to read is
// finally in scope.
QVector<QPair<QString, QStringList>> conventions_shadowed(QWidget *scope) {
	QVector<QPair<QString, QStringList>> out;
	if (!scope) return out;
	const QVector<ShortcutClaim> claims = shortcut_claims(scope);
	for (const ConventionRow &row : k_convention_rows) {
		// The context-menu row answers whether or not the bundle was asked
		// for (8.77), so it is the only one to report while the rest are
		// off: reporting a row the library is not answering to would be
		// naming a shadow over nothing.
		if (!keyboard_conventions() && !row.always) continue;
		QStringList who;
		for (const Qt::Key k : row.keys) {
			if (k == 0) continue;
			const QKeySequence want(QKeyCombination(
			    Qt::KeyboardModifiers(int(row.mod)), k).toCombined());
			for (const ShortcutClaim &c : claims)
				if (c.key == want && !who.contains(c.text)) who << c.text;
		}
		if (!who.isEmpty())
			out.append({QString::fromLatin1(row.shown), who});
	}
	return out;
}

QVector<QPair<QKeySequence, QStringList>> shortcut_conflicts(QWidget *scope) {
	QVector<QPair<QKeySequence, QStringList>> out;
	if (!scope) return out;
	const QVector<ShortcutClaim> claims = shortcut_claims(scope);
	QVector<QWidget *> focuses = focus_candidates(scope);
	focuses.append(nullptr);
	QVector<QKeySequence> seen;
	for (const ShortcutClaim &c : claims) {
		if (seen.contains(c.key)) continue;
		seen.append(c.key);
		QStringList who;
		for (QWidget *fw : std::as_const(focuses)) {
			// Nearest first, because that is the order the matcher picks in
			// and the report promises the winner is named first. Sorted for
			// THIS focus, since which claim is nearest is a property of
			// where the focus is rather than of the claims.
			QVector<QPair<int, QString>> here;
			for (const ShortcutClaim &other : claims)
				if (other.key == c.key && claim_applies(other, scope, fw))
					here.append({claim_distance(other, fw), other.text});
			std::stable_sort(here.begin(), here.end(),
			                 [](const QPair<int, QString> &a,
			                    const QPair<int, QString> &b) {
				                 return a.first < b.first;
			                 });
			if (here.size() > who.size()) {
				who.clear();
				for (const auto &entry : std::as_const(here))
					who << entry.second;
			}
		}
		if (who.size() > 1) out.append({c.key, who});
	}
	return out;
}

// Every button in `scope` that no key reaches: the QAbstractButtons that are
// visible and enabled, less the ones something keyed can activate.
//
// Three routes key a button, and the last two are why this cannot live in an
// application. Tab reaches it, which is keyboard_reachable(). A mnemonic or
// a chord claims the button ITSELF -- Qt gives `&Lettered` the shortcut
// Alt+L whatever its focus policy. Or a claim names an ACTION the button
// carries: a toolbar's button is Qt::NoFocus and in no tab chain, and
// `&Save` on the action behind it reaches it perfectly well. Measured, a
// sweep of the focus chain alone calls that button pointer-only.
//
// A buddy label's claim keys the field it names rather than the label, which
// is what that mnemonic does. Kept because a buddy can be a button -- a
// check box a label names -- and the report would otherwise accuse a control
// the user can reach with one key.
//
// TWO LIMITS, pinned by checks so they cannot move unnoticed. A QShortcut
// claims a key and says nothing about what it activates, so nothing is keyed
// for one: measured, a button wired to `QShortcut::activated` is named here
// though Ctrl+K clicks it, and there is no fix -- Qt publishes no way to ask
// what a connection reaches. An application wanting the report quiet says the
// same thing in a form this can see, with a mnemonic or a QAction. And the
// scope itself is not examined, only what is inside it, so asking about a
// button answers about its children.
//
// Not filtered to the ones an application can fix. Qt's own closable-tab and
// dock-widget buttons are QAbstractButtons with Qt::NoFocus and no action
// (8.159), so they are named, and that is the finding rather than noise: the
// remedy is the application's, and it is the one practice 4 already asks for.
// Rendered twice per candidate and compared inside its own rectangle. The
// rectangle matters: focusing one widget usually takes the mark OFF another,
// so "the screen changed" is satisfied by the widget that LOST focus and
// would pass whatever the candidate did -- measured in this suite's own
// sweep, which restricts the comparison the same way and says why.
static QString focus_signature(QWidget *scope, const QRect &cells) {
	CellBuffer b(qMax(1, scope->width() / GridMetrics::cw()),
	             qMax(1, scope->height() / GridMetrics::ch()));
	render_once(*scope, b);
	QString sig;
	for (int y = cells.top(); y <= cells.bottom() && y < b.rows(); ++y) {
		if (y < 0) continue;
		for (int x = cells.left(); x <= cells.right() && x < b.cols(); ++x) {
			if (x < 0) continue;
			const Cell &c = b.at(x, y);
			sig += c.ch.isEmpty() ? QStringLiteral(".") : c.ch;
			sig += QString::number(int(c.attrs));
			sig += QString::number(c.fg.value());
			sig += QString::number(c.bg.value());
		}
	}
	return sig;
}

QVector<QWidget *> focus_invisible(QWidget *scope) {
	QVector<QWidget *> out;
	if (!scope) return out;
	const QVector<QWidget *> stops = keyboard_reachable(scope);
	if (stops.size() < 2) return out;
	QWidget *const had = focusWidget();
	for (QWidget *w : stops) {
		// A widget that takes text shows focus with the terminal's cursor,
		// which is placed on exactly this attribute. Nothing for it to draw.
		//
		// EXCEPT AN ITEM VIEW, which acquires the attribute as soon as its
		// current item is editable -- true of QStringListModel,
		// QStandardItemModel and QTableWidget by default -- and has no
		// caret to show focus with unless an editor is actually open, in
		// which case the editor holds the focus rather than the view. So
		// the exemption let the commonest list in Qt out of this report
		// entirely: measured, a frameless QListView with a custom delegate
		// showed nothing at all when focused and was never named, and a
		// control written to prove the report could speak read that
		// silence as a pass.
		if (w->testAttribute(Qt::WA_InputMethodEnabled)
		    && !qobject_cast<QAbstractItemView *>(w)) continue;
		QWidget *other = nullptr;
		for (QWidget *o : stops)
			if (o != w) { other = o; break; }
		if (!other) continue;
		const QPoint at = w->mapTo(scope, QPoint());
		const QRect cells(at.x() / GridMetrics::cw(), at.y() / GridMetrics::ch(),
		                  qMax(1, w->width() / GridMetrics::cw()),
		                  qMax(1, w->height() / GridMetrics::ch()));
		other->setFocus();
		set_focus_widget(scope->focusWidget());
		QCoreApplication::processEvents();
		const QString without = focus_signature(scope, cells);
		// NOTHING TO COMPARE is not the same finding as nothing to see. A
		// widget squeezed to no rows, or laid out past the bottom of the
		// window, has an empty signature both times -- and naming it would
		// report a focus mark missing from a control that is not on the
		// screen at all. Measured on a fixture where a list took the space
		// and left two widgets 38x0 cells at row 14 of a 14-row window:
		// both were named, and neither was visible to anybody.
		if (without.isEmpty()) continue;
		w->setFocus();
		set_focus_widget(scope->focusWidget());
		QCoreApplication::processEvents();
		const QString with = focus_signature(scope, cells);
		if (with == without) out.append(w);
	}
	if (had) {
		had->setFocus();
		set_focus_widget(had);
		QCoreApplication::processEvents();
	}
	return out;
}

// Every report that should be empty, asked at once: see runtime.h.
//
// The list is written out here, which is the thing it exists to stop an
// APPLICATION having to do -- one place that goes stale instead of one per
// test, and this one is the place a new question is added anyway.
QVector<QPair<QString, QString>> audit(QWidget *scope) {
	QVector<QPair<QString, QString>> out;
	if (!scope) return out;
	const auto name_of = [](const QWidget *w) {
		if (!w) return QStringLiteral("a widget");
		const QString n = w->objectName();
		return n.isEmpty()
		           ? QString::fromLatin1(w->metaObject()->className())
		           : n + QLatin1String(" (")
		                 + QString::fromLatin1(w->metaObject()->className())
		                 + QLatin1Char(')');
	};
	const auto widgets = [&out, &name_of](const QString &q,
	                                      const QVector<QWidget *> &found) {
		for (QWidget *w : found) out.append({q, name_of(w)});
	};

	widgets(QStringLiteral("pointer_only"), pointer_only(scope));
	widgets(QStringLiteral("hover_only"), hover_only(scope));
	widgets(QStringLiteral("focus_invisible"), focus_invisible(scope));
	widgets(QStringLiteral("sheet_styled"), sheet_styled(scope));

	for (const auto &c : mnemonic_conflicts(scope))
		out.append({QStringLiteral("mnemonic_conflicts"),
		            QStringLiteral("Alt+") + c.first + QStringLiteral(": ")
		                + c.second.join(QStringLiteral(", "))});
	for (const auto &c : shortcut_conflicts(scope))
		out.append({QStringLiteral("shortcut_conflicts"),
		            c.first.toString(QKeySequence::NativeText)
		                + QStringLiteral(": ")
		                + c.second.join(QStringLiteral(", "))});
	for (const auto &c : conventions_shadowed(scope))
		out.append({QStringLiteral("conventions_shadowed"),
		            c.first + QStringLiteral(": ")
		                + c.second.join(QStringLiteral(", "))});
	for (const auto &p : tab_order_anomalies(scope))
		out.append({QStringLiteral("tab_order_anomalies"),
		            name_of(p.first) + QStringLiteral(" -> ")
		                + name_of(p.second)});
	for (const auto &c : ambiguous_chords(scope))
		out.append({QStringLiteral("ambiguous_chords"),
		            c.first + QStringLiteral(": ") + c.second});
	return out;
}

// The chords in `scope` a terminal cannot deliver unambiguously: see
// runtime.h. Same enumeration as shortcut_help() and shortcut_conflicts(),
// for the reason those two share it -- three walks of one tree is three
// chances to describe different programs.
QVector<QPair<QString, QString>> ambiguous_chords(QWidget *scope) {
	QVector<QPair<QString, QString>> out;
	if (!scope) return out;
	QSet<QString> seen;
	const QVector<ShortcutClaim> claims = shortcut_claims(scope);
	for (const ShortcutClaim &c : claims) {
		for (int i = 0; i < c.key.count(); ++i) {
			const QKeyCombination combo = c.key[i];
			const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
			if (!(mods & Qt::ControlModifier)) continue;
			const int key = combo.key();
			// A SHIFTED control chord cannot be sent at all: the byte
			// carries no shift bit. Reported whatever the key is.
			bool bad = mods & Qt::ShiftModifier;
			// And these five ARE sent, as another key entirely, because
			// the control byte they produce is a key in its own right.
			// The arithmetic is ASCII's: Ctrl+letter is letter & 0x1f.
			if (!bad)
				bad = key == Qt::Key_I || key == Qt::Key_M
				      || key == Qt::Key_BracketLeft || key == Qt::Key_H
				      || key == Qt::Key_J;
			if (!bad) continue;
			const QString text =
			    QKeySequence(combo).toString(QKeySequence::NativeText);
			const QString row = text + QLatin1Char('\t') + c.text;
			if (seen.contains(row)) continue;
			seen.insert(row);
			QString label = c.text;
			label.replace(QLatin1String("&&"), QLatin1String("\1"));
			label.remove(QLatin1Char('&'));
			label.replace(QLatin1String("\1"), QLatin1String("&"));
			out.append({text, label});
		}
	}
	return out;
}

// The chords this library will answer inside `scope`, with a label each: see
// runtime.h for the rule. It is the same enumeration `shortcut_conflicts()`
// reports on, read for a different purpose -- so a help line and a conflict
// report can never describe different programs, which is the whole reason
// this is a view over that function rather than a second walk.
//
// Deduplicated by chord AND label, which the claim list is not: one action
// carrying two sequences is two rows here and should be, while the same
// action reached through a menu and a toolbar is already one claim.
QVector<QPair<QString, QString>> shortcut_help(QWidget *scope) {
	QVector<QPair<QString, QString>> out;
	if (!scope) return out;
	QSet<QString> seen;
	const QVector<ShortcutClaim> claims = shortcut_claims(scope);
	for (const ShortcutClaim &c : claims) {
		const QString key = c.key.toString(QKeySequence::NativeText);
		if (key.isEmpty()) continue;
		// The ampersand is a mnemonic marker in the text an action carries
		// and would be drawn literally in a status line. `&&` is a real one.
		QString label = c.text;
		label.replace(QLatin1String("&&"), QLatin1String("\1"));
		label.remove(QLatin1Char('&'));
		label.replace(QLatin1String("\1"), QLatin1String("&"));
		const QString row = key + QLatin1Char('\t') + label;
		if (seen.contains(row)) continue;
		seen.insert(row);
		out.append({key, label});
	}
	return out;
}

// The words a terminal user cannot reach: see runtime.h for the rule and the
// one exclusion. Actions are deliberately not walked -- Qt derives an
// action's tool tip from its own text when none is set, so asking the
// question of actions would name every action in the program -- and an
// explicit tip on one arrives here anyway, through the button that carries
// it.
QVector<QWidget *> hover_only(QWidget *scope) {
	QVector<QWidget *> out;
	if (!scope) return out;
	// NOR A WIDGET WHOSE WORDS SHIFT+F1 REACHES, which took measuring to
	// be sure of: `QWhatsThis` works here end to end, and nothing said so.
	// Qt's What's This mode opens on Shift+F1, shows the FOCUSED widget's
	// whatsThis() in a Qt::ToolTip window -- a window kind this library
	// draws -- and Escape or the next key closes it again. Measured, all
	// four steps, including that the key which dismisses is swallowed,
	// which is Qt's own behaviour on a desktop.
	//
	// So a tip is not the only way to those words when the same widget
	// carries a whatsThis AND a key can get to it. Both halves are
	// required: measured, Shift+F1 shows the focused widget's help and
	// nothing else's, so a QLabel's whatsThis is as unreachable as its
	// tool tip and it is still named.
	const QVector<QWidget *> reachable = keyboard_reachable(scope);
	const auto kids = scope->findChildren<QWidget *>();
	for (QWidget *w : kids) {
		if (!w->isVisible() || !w->isEnabled()) continue;
		if (w->toolTip().isEmpty() || !w->statusTip().isEmpty()) continue;
		if (!w->whatsThis().isEmpty() && reachable.contains(w)) continue;
		const auto *b = qobject_cast<QAbstractButton *>(w);
		if (b && b->text().isEmpty()) continue;
		out.append(w);
	}
	return out;
}

// Where Tab goes backwards against the reading order. The rule and the one
// exception are in runtime.h; what is worth saying beside the code is that
// the comparison is in CELL ROWS rather than pixels. Two widgets a few
// pixels apart are the same row on a terminal -- that is the whole of what
// a grid is -- so a pixel comparison would report an order a user cannot
// see, and the row is the unit they actually move through.
QVector<QPair<QWidget *, QWidget *>> tab_order_anomalies(QWidget *scope) {
	QVector<QPair<QWidget *, QWidget *>> out;
	if (!scope) return out;
	const QVector<QWidget *> stops = keyboard_reachable(scope);
	for (int i = 0; i + 1 < stops.size(); ++i) {
		QWidget *const a = stops[i];
		QWidget *const b = stops[i + 1];
		if (a->parentWidget() != b->parentWidget()) continue;
		const QPoint pa = a->mapTo(scope, QPoint());
		const QPoint pb = b->mapTo(scope, QPoint());
		const int ra = pa.y() / GridMetrics::ch();
		const int rb = pb.y() / GridMetrics::ch();
		const bool backwards = (rb < ra && pb.x() <= pa.x())
		                       || (rb == ra && pb.x() < pa.x());
		if (backwards) out.append(qMakePair(a, b));
	}
	return out;
}

// Does this label actually offer a link, as opposed to merely being allowed
// to? Asked of the DOCUMENT rather than of the string, because the answer
// depends on how the label reads its own text: Qt::PlainText showing the
// characters `<a href=...>` offers nothing, and Qt::AutoText -- the default
// -- decides with mightBeRichText(). Markdown is asked in its own spelling
// for the same reason.
static bool holds_a_link(const QLabel *l) {
	const QString text = l->text();
	if (text.isEmpty()) return false;
	const Qt::TextFormat fmt = l->textFormat();
	QTextDocument doc;
	if (fmt == Qt::MarkdownText) {
		doc.setMarkdown(text);
	} else if (fmt == Qt::RichText
	           || (fmt == Qt::AutoText && Qt::mightBeRichText(text))) {
		doc.setHtml(text);
	} else {
		return false;
	}
	for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
		for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
			const QTextCharFormat cf = it.fragment().charFormat();
			if (cf.isAnchor() && !cf.anchorHref().isEmpty()) return true;
		}
	return false;
}

// Every widget under `scope`, and `scope` itself, whose effective style is
// Qt's QStyleSheetStyle. See the header for why this asks style() rather
// than styleSheet(), and why nothing is excluded.
QVector<QWidget *> sheet_styled(QWidget *scope) {
	QVector<QWidget *> out;
	if (!scope) return out;
	const auto drawn_by_sheet = [](const QWidget *w) {
		const QStyle *s = w->style();
		return s && qstrcmp(s->metaObject()->className(), "QStyleSheetStyle") == 0;
	};
	if (drawn_by_sheet(scope)) out.append(scope);
	const auto kids = scope->findChildren<QWidget *>();
	for (QWidget *w : kids)
		if (drawn_by_sheet(w)) out.append(w);
	return out;
}

QVector<QWidget *> pointer_only(QWidget *scope) {
	QVector<QWidget *> out;
	if (!scope) return out;
	QSet<const QWidget *> keyed;
	const QVector<QWidget *> reach = keyboard_reachable(scope);
	for (QWidget *w : reach) keyed.insert(w);
	const auto claimed = [&keyed](QObject *who) {
		if (auto *l = qobject_cast<QLabel *>(who)) {
			if (l->buddy()) keyed.insert(l->buddy());
			return;
		}
		if (auto *w = qobject_cast<QWidget *>(who)) keyed.insert(w);
		if (auto *a = qobject_cast<QAction *>(who))
			for (const QWidget *o : owners_of(a)) keyed.insert(o);
	};
	const QVector<MnemonicClaim> mnemonics = mnemonic_claims(scope);
	for (const MnemonicClaim &c : mnemonics) claimed(c.who);
	const QVector<ShortcutClaim> shortcuts = shortcut_claims(scope);
	for (const ShortcutClaim &c : shortcuts) claimed(c.who);
	// A DIALOG's default button answers Enter without holding the focus --
	// QDialog::keyPressEvent goes looking for it -- so it is keyed however
	// its focus policy reads. Measured: a Qt::NoFocus default button fires
	// on Enter pressed in a field beside it, and without this the report
	// named it. That is the toolbar's false report again, one route along:
	// a key reaches the control and the walk this subtracts from cannot see
	// the route.
	const auto defaults = scope->findChildren<QPushButton *>();
	for (QPushButton *b : defaults)
		if (b->isDefault() && qobject_cast<QDialog *>(b->window()))
			keyed.insert(b);
	// A LINE EDIT'S CLEAR BUTTON IS NOT A FINDING, and leaving it in was
	// the kind of false positive that gets a whole report ignored: Qt adds
	// the button whenever an application calls setClearButtonEnabled(true),
	// so every such field produced one, and the remedy it asks for already
	// exists. Measured, on a field holding text:
	//
	//   conventions ON    Ctrl+U empties it (the readline kill-back)
	//   conventions OFF   Ctrl+A then Delete empties it (Qt's select-all)
	//
	// So the ACTION a click there performs has a keyboard route in both
	// modes, which is the question practice 4 asks -- unlike a splitter
	// handle or a sorting header, where it has none anywhere.
	//
	// Qt's own marker decides it, the way QCompleter::setPopup() marks a
	// deferring layer by its focus proxy: the clear button's default action
	// is named `_q_qlineeditclearaction`, while an action the APPLICATION
	// adds with QLineEdit::addAction() keeps the name the application gave
	// it and is still named here -- a reveal toggle in a password field is
	// exactly the pointer-only case this report exists for. If Qt ever
	// renames that action the button comes back into the report rather than
	// vanishing from it, and the check below fails loudly saying so.
	const auto buttons = scope->findChildren<QAbstractButton *>();
	for (QAbstractButton *b : buttons) {
		if (!b->isVisible() || !b->isEnabled()) continue;
		if (keyed.contains(b)) continue;
		bool qt_clear_button = false;
		for (const QAction *a : b->actions())
			qt_clear_button = qt_clear_button
			    || a->objectName()
			       == QLatin1String("_q_qlineeditclearaction");
		if (qt_clear_button) continue;
		// A CALENDAR'S OWN NAVIGATION, for the clear button's reason rather
		// than a new one. QCalendarWidget builds four buttons of its own --
		// a previous and a next month, a month menu and a year edit -- and
		// none of them is a tab stop, so every calendar in every
		// application produced four findings. Measured with the focus on
		// the calendar:
		//
		//   PageDown   2026-09-20 -> 2026-10-20, month shown 9 -> 10
		//   PageUp     back again
		//   Down       2026-09-20 -> 2026-09-27
		//   Right      one day on
		//
		// So the ACTION those buttons perform has a keyboard route, which
		// is the question practice 4 asks. The year is reachable the same
		// way and more slowly, twelve PageDowns to the year -- which is the
		// clear button's Ctrl+A-then-Delete again: a route, not a good one.
		//
		// Matched by ancestor rather than by class name, because the two
		// arrows are QtPrivate::QPrevNextCalButton and a report must not
		// key on a private Qt name -- one rename and the exclusion goes
		// silently, which is the failure this whole function exists to
		// avoid.
		bool in_calendar = false;
		for (const QWidget *a = b->parentWidget(); a; a = a->parentWidget())
			if (qobject_cast<const QCalendarWidget *>(a)) {
				in_calendar = true;
				break;
			}
		if (in_calendar) continue;
		// AND A TABLE'S CORNER BUTTON, which is the same case measured
		// exactly: it selects every cell, and so does Ctrl+A. On a 3x3
		// table with one cell current, the click and the key both left
		// nine selected. It appears on every table showing both headers,
		// so naming it is a finding per table that nobody can act on.
		//
		// Its parent decides it: the corner button is a child of the
		// QTableView itself, while a button an application puts inside a
		// table is a child of the VIEWPORT and is still named. Qt hides it
		// when either header is hidden, and an invisible button never
		// reaches this loop, so a table without both headers does not need
		// the exclusion and does not get it.
		//
		// AND IT MUST BE UNLABELLED, so that the exclusion errs towards
		// naming. A parent test alone would also silence a button an
		// application had parented to the view on purpose -- an overlay, a
		// corner action of its own -- and that is the direction this must
		// not fail in: the corner button carries no text, and anything the
		// application put there almost certainly does.
		if (qobject_cast<QTableView *>(b->parentWidget()) && b->text().isEmpty())
			continue;
		out.append(b);
	}
	// AND THE THING YOU DRAG. `QSplitterHandle` is Qt's own word for it, the
	// way QAbstractButton is Qt's word for a thing you click, so this is the
	// same population argument rather than a second judgement. Measured: the
	// handle is Qt::NoFocus, is no tab stop, and three Right presses with
	// the focus forced onto it move the split by nothing at all -- so unlike
	// a scroll bar, which answers arrows and merely cannot be reached, a
	// splitter has no keyboard route anywhere. The guide's remedy is an
	// action of the application's own that sets the sizes.
	const auto handles = scope->findChildren<QSplitterHandle *>();
	for (QSplitterHandle *h : handles) {
		if (!h->isVisible() || !h->isEnabled()) continue;
		if (keyed.contains(h)) continue;
		out.append(h);
	}
	// AND A HEADER THAT SORTS. This one is not a widget you click but a
	// widget whose SECTIONS a click acts on, and it is here because the
	// report was silent exactly where the practice was broken: measured,
	// a QTableView with sorting on has no key that changes the column or
	// the order -- 45 key combinations across this library and plain Qt
	// moved nothing -- and `QHeaderView` declares four mouse handlers and
	// no keyPressEvent at all. Qt never promised the practice; this
	// library's guide does, and its audience is the one without a mouse.
	//
	// `isSortIndicatorShown()` rather than `sectionsClickable()`, which is
	// true on every table header by default and would name two headers in
	// every application that has a table. The indicator is shown when
	// setSortingEnabled(true) was called, which is exactly the case where
	// a click does something a key cannot.
	//
	// Resizing and reordering columns are pointer-only in Qt too, and are
	// NOT named: they change how the data looks rather than which data you
	// are looking at, and a report that names every table's every header
	// is one nobody reads. The guide records them as limits instead.
	const auto headers = scope->findChildren<QHeaderView *>();
	for (QHeaderView *h : headers) {
		if (!h->isVisible() || !h->isEnabled()) continue;
		if (!h->isSortIndicatorShown()) continue;
		if (keyed.contains(h)) continue;
		out.append(h);
	}
	// AND A LINK NO KEY CAN FOLLOW. Same shape as the header above and
	// found in the same sweep: the thing clicked is not a widget, so a
	// report whose population is widgets passed straight over it.
	//
	// What makes it worse than the header is that nothing on the screen
	// tells the two apart. A link in a QLabel is drawn underlined and
	// coloured whether or not a key can reach it -- byte-identical cells --
	// so a user without a mouse sees an invitation and has no way to take
	// it, and the author sees a link that works.
	//
	// THE ANCHOR IS THE PREDICATE, NOT THE FLAG, and that had to be
	// measured. Qt gives EVERY QLabel Qt::LinksAccessibleByMouse:
	//
	//   default flags               focusPolicy 0    flags 0x04
	//   LinksAccessibleByMouse      focusPolicy 0    flags 0x04
	//   + LinksAccessibleByKeyboard focusPolicy 11   flags 0x0c
	//   plain words, same flags     focusPolicy 0    flags 0x04
	//
	// So testing the flag alone would name every label in every program --
	// the sectionsClickable() mistake the header above records, met again
	// one member later. A label carrying an anchor and lacking
	// LinksAccessibleByKeyboard is the case where a click does something
	// no key can; Qt gives the keyboard-accessible one StrongFocus, so it
	// is already a tab stop and is already in `keyed`.
	const auto labels = scope->findChildren<QLabel *>();
	for (QLabel *l : labels) {
		if (!l->isVisible() || !l->isEnabled()) continue;
		if (keyed.contains(l)) continue;
		const Qt::TextInteractionFlags f = l->textInteractionFlags();
		if (!(f & Qt::LinksAccessibleByMouse)) continue;
		if (f & Qt::LinksAccessibleByKeyboard) continue;
		if (!holds_a_link(l)) continue;
		out.append(l);
	}
	return out;
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
	const bool popup_owns_input = !input_popups().isEmpty();
	// ONE list, in this function's own order, and the same one
	// shortcut_conflicts() reads: actions here, application-context actions
	// in other windows, this scope's QShortcuts, then application-context
	// QShortcuts elsewhere. It was four loops, and a report built beside
	// four loops is a report that can describe a different program -- 8.155,
	// which is what taught this file to enumerate once.
	//
	// Qt::ApplicationShortcut is why two of the four reach out of the scope
	// at all: by definition it does not care which window you are in, so the
	// scope cannot find it. Deliberately narrow -- a WINDOW-context shortcut
	// in another window must not fire, and there is a check for each.
	// NEAREST THE FOCUS, not first in the list. Several claims can apply at
	// once -- Qt's own MDI gives every subwindow the same Ctrl+F4 -- and
	// enumeration order then answers with whichever the walk reached first,
	// which is not the one the user is in. Ties keep enumeration order, so
	// nothing that had exactly one claimant changes.
	const QVector<ShortcutClaim> claims = shortcut_claims(scope);
	const ShortcutClaim *best = nullptr;
	int best_distance = 0;
	for (const ShortcutClaim &claim : claims) {
		if (claim.key != pressed) continue;
		if (!claim_applies(claim, scope, focusWidget())) continue;
		const int d = claim_distance(claim, focusWidget());
		if (!best || d < best_distance) {
			best = &claim;
			best_distance = d;
		}
	}
	if (best) {
		const ShortcutClaim &claim = *best;
		if (auto *a = qobject_cast<QAction *>(claim.who)) {
			if (popup_owns_input) return true;   // swallowed, not fired
			a->trigger();
			return true;
		}
		if (auto *sc = qobject_cast<QShortcut *>(claim.who)) {
			// QShortcut is NOT a QAction and was invisible to the action
			// table until 8.86. An application writing the commonest Qt
			// idiom there is --
			//
			//     new QShortcut(QKeySequence("Ctrl+S"), this, ...)
			//
			// -- got a shortcut that worked in the desktop build and did
			// nothing at all on the terminal, with nothing reporting it:
			// Qt's own QShortcutMap gates on an active window and none
			// activates here, so the key fell through to the focused widget
			// and was ignored.
			if (popup_owns_input) return true;   // swallowed (a QShortcut)
			return fire(sc);
		}
	}
	return false;
}

// Readline editing, in a widget that takes text and nowhere else.
//
// A terminal user's fingers know Ctrl+A for the start of a line, Ctrl+E
// for the end, Ctrl+K and Ctrl+U to kill forward and back, and Ctrl+W to
// rub out a word. Measured before any of this existed: four did nothing
// at all and Ctrl+A did Qt's Select All -- four inert and one doing
// something DIFFERENT from what the muscle memory expects, which is the
// worse of the two.
//
// WA_InputMethodEnabled is the test, and it is the Ctrl+C precedent's
// own: that chord is quit EXCEPT where a caret sits in a field, and it is
// the same attribute that decides where the terminal's cursor goes. The
// alternative was a global answer, and a global answer to Ctrl+A cannot
// be right -- it is Select All in every Qt program and start-of-line in
// every shell, and a terminal application is both. Per widget, the two
// meanings stop competing: a list view keeps Qt's Select All, and a text
// field gets the shell's.
//
// BEFORE dispatch rather than after, unlike the arrow keys and the tab
// chords below, and Ctrl+A is why. Qt ACCEPTS it in a line edit, so a
// binding that waited for the widget to decline would never fire for the
// one chord that needed deciding. The other four are free -- Qt gives
// them no meaning -- but they are handled here too, because a rule split
// across two places by whether Qt happens to accept the key is a rule
// nobody can read.
//
// Synthesised as the motions Qt already has rather than edited directly:
// Home, End, Shift+End then Delete, Shift+Home then Delete, and Qt's own
// delete-previous-word. That works the same in QLineEdit, QTextEdit and
// QPlainTextEdit without this knowing which it has, and it goes through
// each widget's own undo stack instead of around it.
//
// AND SHIFT IS PART OF THE CHORD, which this read no more than the quit
// loop did: Ctrl+Shift+K killed to the end of the line. The list promises
// Ctrl+K, and a shifted control chord on a terminal usually belongs to the
// emulator rather than to the program inside it.
//
// It is the same edit as the quit loop's because leaving it would break
// the sentence Ctrl+D below rests on -- the quit loop gives that chord up
// on exactly the condition that brings it here, so the two cannot disagree
// about who has it. With the quit loop strict and this one loose,
// Ctrl+Shift+D would be nobody's quit key and still readline's delete.
bool InputRouter::readline_edit(const KeyEvent &k) {
	if (!s_conventions || !k.ctrl || k.alt || k.shift) return false;
	QWidget *const fw = key_target();
	if (!fw || !fw->testAttribute(Qt::WA_InputMethodEnabled)) return false;
	// AND NOT AN ITEM VIEW, the fourth place this attribute was asked a
	// question it does not answer. The guide promises that Ctrl+A is
	// decided per widget -- start of line where a caret is, Qt's Select
	// All in a list, a tree or a table -- and for the commonest list in
	// Qt that promise was false: a QListView over QStringListModel has
	// editable items, so it carries WA_InputMethodEnabled and these
	// chords fired on it. Measured, with five rows and the current one at
	// the bottom:
	//
	//     QListWidget, items not editable   Ctrl+A selected 5
	//     QListView + QStringListModel      Ctrl+A selected 1, and the
	//                                       current row jumped 4 -> 0
	//
	// Home wearing Select All's clothes. An open editor is a QLineEdit
	// and is the key target in its own right, so a real caret inside a
	// view keeps every chord.
	if (qobject_cast<QAbstractItemView *>(fw)) return false;
	const auto send = [fw](int key, Qt::KeyboardModifiers mods) {
		QKeyEvent down(QEvent::KeyPress, key, mods);
		QApplication::sendEvent(fw, &down);
		QKeyEvent up(QEvent::KeyRelease, key, mods);
		QApplication::sendEvent(fw, &up);
	};
	switch (k.qt_key) {
	case Qt::Key_A: send(Qt::Key_Home, Qt::NoModifier); break;
	case Qt::Key_E: send(Qt::Key_End, Qt::NoModifier); break;
	case Qt::Key_K:
		send(Qt::Key_End, Qt::ShiftModifier);
		send(Qt::Key_Delete, Qt::NoModifier);
		break;
	case Qt::Key_U:
		send(Qt::Key_Home, Qt::ShiftModifier);
		send(Qt::Key_Delete, Qt::NoModifier);
		break;
	case Qt::Key_W:
		send(Qt::Key_Backspace, Qt::ControlModifier);
		break;
	case Qt::Key_D:
		// Delete FORWARD, which is what readline's Ctrl+D does with a
		// character under the caret. The quit-key loop gives the chord up on
		// the same condition that brings it here, so the two cannot disagree
		// about who has it.
		send(Qt::Key_Delete, Qt::NoModifier);
		break;
	default:
		return false;
	}
	if (frame_requested) frame_requested();
	return true;
}

void InputRouter::deliver_key(QWidget *target, const KeyEvent &k) {
	const Qt::KeyboardModifiers mods = qt_modifiers(k.ctrl, k.alt, k.shift);
	// NO TEXT when Alt is held, which is what a desktop delivers and what
	// this did not. A terminal sends Alt+Z as ESC then 'z', so the decoder
	// sets alt with the letter still in `text` -- and a QLineEdit reads
	// text and TYPES it. Measured: Alt+Z on a field holding "abc" left
	// "abcz".
	//
	// That is worse than a missing binding. Alt+letter is how a terminal
	// user reaches a menu, so pressing Alt+F for a File menu that is not
	// there put an "f" into whatever they were typing -- silently, the key
	// doing nothing else to say it had missed.
	//
	// Alt here always means the ESC prefix and never AltGr: a terminal
	// delivers an AltGr'd character as the composed character with no
	// prefix, so nothing that legitimately types text arrives this way.
	//
	// Withheld from the widgets that TYPE, rather than from everything,
	// and that took a broken check to learn. A QMenu matches its items by
	// the event's text -- there is no QShortcutMap here to do it another
	// way -- so emptying the text for every target left an open menu deaf
	// to its own mnemonics. `WA_InputMethodEnabled` is how this file
	// already asks "does this widget take typing", for the Ctrl+C carve-
	// out above, and it is the same question.
	const bool types = target && target->testAttribute(Qt::WA_InputMethodEnabled);
	const QString text = (k.alt && types) ? QString() : k.text;
	// A HANDLER MAY DELETE THE TARGET, and everything below this line uses
	// it: the fabricated release, the context-menu branch, the scroll
	// fallback. An application that deletes a page in a slot -- Qt permits
	// it and recommends deleteLater() instead, which is not the same as
	// nobody doing it -- would have every one of those reading freed
	// memory. A QPointer costs one word and turns a crash into a return.
	//
	// The press is still read afterwards because it is a stack object: what
	// dies is the widget, not the event.
	QPointer<QWidget> alive(target);
	QKeyEvent press(QEvent::KeyPress, k.qt_key, mods, text);
	QApplication::sendEvent(target, &press);
	if (!alive) return;
	// Terminals have no key-release; fabricate one immediately (section 5.5).
	QKeyEvent release(QEvent::KeyRelease, k.qt_key, mods, text);
	QApplication::sendEvent(target, &release);
	if (!alive) return;

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
	// The terminal conventions, when an application has asked for them and
	// the focused widget did not want the key. Focus movement comes BEFORE
	// the scroll fallback below on purpose: follow_focus() scrolls the
	// layer to keep the focused widget visible, so moving focus scrolls as
	// a consequence and lands somewhere a person can type, where scrolling
	// alone moves the view and leaves focus behind it.
	// THE KEYBOARD ROUTE TO A CONTEXT MENU, which the platform normally
	// supplies and which this library had filled in for the mouse only.
	// on_mouse() synthesises QContextMenuEvent for a right press because
	// "there is no platform here"; the Menu key and Shift+F10 are the same
	// absence and were left open, so on a terminal a context menu was
	// reachable ONLY BY POINTER -- the one thing doc/keyboard-first.md
	// tells applications never to do, in the library that tells them.
	// Measured before the fix: 0 QContextMenuEvent from either key.
	//
	// NOT opt-in, unlike the conventions below, and this is the line the
	// distinction sits on. Practice 6 says qtty binds no shortcut of its
	// own, and this binds none: it restores what every Qt application
	// already has on every desktop, the way the MouseMove and right-press
	// synthesis do. QWidget::event() reads contextMenuPolicy from the
	// event, so NoContextMenu still yields nothing and a custom policy
	// still emits the application's own signal -- the application keeps
	// the decision, which a bound key would have taken from it.
	//
	// Gated on the press not being accepted, so a widget wanting F10 or
	// the Menu key for itself keeps it: the same order the Tab and arrow
	// paths use, and the guard 8.70 exists to defend.
	//
	// The centre of the widget, because a keyboard press has no position
	// and the menu has to appear somewhere the focused control is. Qt's
	// own platform code makes the same choice.
	if (!press.isAccepted() && target
	    && (k.qt_key == Qt::Key_Menu
	        || (k.qt_key == Qt::Key_F10 && k.shift))) {
		const QPoint local = target->rect().center();
		const QPoint global = target->mapToGlobal(local);
		// And the recorded pointer goes with it, because this event's global
		// position is one qtty INVENTED -- there was no pointer involved --
		// and an application reading QCursor::pos() instead of
		// event->globalPos() must not get a different answer from the same
		// keystroke. A desktop does not move the pointer for a keyboard
		// context menu, and a desktop also has a real one to leave alone;
		// here the alternative is not fidelity but a constant, and the two
		// spellings disagreeing is the fault being fixed above.
		QCursor::setPos(global);
		QContextMenuEvent menu(QContextMenuEvent::Keyboard, local, global);
		QApplication::sendEvent(target, &menu);
		return;
	}
	if (s_conventions && !press.isAccepted()) {
		QWidget *const scope = input_scope();
		if (k.qt_key == Qt::Key_Down || k.qt_key == Qt::Key_Up) {
			QWidget *before = scope->focusWidget();
			move_focus(scope, k.qt_key == Qt::Key_Down);
			if (scope->focusWidget() != before) {
				set_focus_widget(scope->focusWidget());
				if (frame_requested) frame_requested();
				return;
			}
		}
		// Ctrl+PageUp and Ctrl+PageDown between tabs. Qt gives a
		// QTabWidget Ctrl+Tab and Ctrl+Shift+Tab already and not these,
		// and these are what a person coming from a browser, an editor or
		// a terminal multiplexer reaches for first. The tab widget the
		// FOCUSED widget is inside, rather than the first one in the
		// layer: a page holding its own tabs is an ordinary arrangement
		// and the inner one is the one being used.
		if (k.ctrl && (k.qt_key == Qt::Key_PageUp
		               || k.qt_key == Qt::Key_PageDown)) {
			for (QWidget *w = key_target(); w; w = w->parentWidget()) {
				auto *tabs = qobject_cast<QTabWidget *>(w);
				if (!tabs || tabs->count() < 2) continue;
				const int step = k.qt_key == Qt::Key_PageDown ? 1 : -1;
				const int n = tabs->count();
				tabs->setCurrentIndex((tabs->currentIndex() + step + n) % n);
				set_focus_widget(scope->focusWidget());
				if (frame_requested) frame_requested();
				return;
			}
		}
		// Alt and a TAB's own letter, which 8.37 recorded as doing
		// nothing: the mnemonic search covers actions, buttons and label
		// buddies, and a tab is none of those. Here rather than in the
		// default search because 0b still holds the question of whether a
		// terminal should switch tabs this way at all -- an application
		// that asked for the terminal's conventions has answered it for
		// itself, and the default is untouched.
		//
		// Through the same enumeration the mnemonic matcher walks, so that
		// mnemonic_conflicts() cannot describe a program whose keys behave
		// differently. This scanned the tab bars itself until 8.155, which
		// made the tab bars the one population the collision report could
		// not see -- a report that names three of four claimants tells an
		// application its letters are unique when they are not.
		if (k.alt && k.text.size() == 1) {
			const QChar want = k.text.at(0).toLower();
			for (const MnemonicClaim &claim : mnemonic_claims(scope)) {
				if (claim.letter != want) continue;
				auto *bar = qobject_cast<QTabBar *>(claim.who);
				if (!bar) continue;
				bar->setCurrentIndex(claim.index);
				set_focus_widget(scope->focusWidget());
				if (frame_requested) frame_requested();
				return;
			}
		}
		// F6 between top-level windows, which had no key at all: this
		// library binds none of its own, so an application with two
		// windows was reachable only by whatever IT bound. F6 is the
		// conventional "next pane" and is rarely an application's own --
		// and it is offered only where the focused widget ignored it, so
		// one that does use F6 keeps it.
		if (k.qt_key == Qt::Key_F6) {
			if (window_tabs().size() > 1) {
				if (k.shift) previous_window(); else next_window();
				if (frame_requested) frame_requested();
				return;
			}
		}
		// F10 INTO THE MENU BAR, which is the desktop's way in and the
		// terminal's alike -- mc and nano put the menu on F9, a desktop
		// puts the focus on the bar with F10, and Qt offers neither here
		// because a menu bar is reached by Alt and Alt needs a mnemonic.
		//
		// Measured before this: a QMenuBar whose titles carry no `&` had
		// NO keyboard route at all. F10 did nothing, the bar is
		// Qt::NoFocus and no tab stop, and its menus were reachable only
		// by a pointer -- in a library whose whole subject is the user
		// without one.
		//
		// It OPENS the first menu rather than merely highlighting the
		// bar, which is where this parts company with a desktop on
		// purpose: highlighting is a state a terminal cannot show well,
		// and once a menu is open every key already works -- the popup
		// owns input, arrows walk it, Left and Right move between menus
		// through QMenuBar, Escape closes it.
		//
		// Shift+F10 is the context-menu key and is tested first, above;
		// this branch takes a bare F10 only.
		if (k.qt_key == Qt::Key_F10 && !k.shift && !k.ctrl && !k.alt) {
			if (QWidget *scope = input_scope()) {
				if (auto *bar = scope->findChild<QMenuBar *>()) {
					const QList<QAction *> acts = bar->actions();
					QAction *first = nullptr;
					for (QAction *a : acts)
						if (a->menu() && a->isVisible() && a->isEnabled()) {
							first = a;
							break;
						}
					if (first && bar->isVisible()) {
						// REMEMBER WHO HAD IT. Qt puts the bar into
						// keyboard mode when a menu opens from it and
						// restores the previous focus when that mode
						// ends -- reading QApplication::focusWidget(),
						// which is null here, so it saves nothing and
						// the bar keeps the focus after Escape.
						// Measured: F10 then Escape left every later
						// key going to the menu bar, and Shift+F10
						// asked the BAR for a context menu instead of
						// the field the user was in.
						before_menu_bar_ = scope->focusWidget();
						bar->setActiveAction(first);
						first->menu()->popup(bar->mapToGlobal(
						    bar->actionGeometry(first).bottomLeft()));
						if (frame_requested) frame_requested();
						return;
					}
				}
			}
		}
		if (k.qt_key == Qt::Key_Return || k.qt_key == Qt::Key_Enter) {
			// Enter on the control that has focus, which is what a
			// terminal user means by it. A button only: Enter inside a
			// text field is that field's business and it consumed the
			// key already if it wanted it.
			if (auto *b = qobject_cast<QAbstractButton *>(key_target())) {
				b->click();
				return;
			}
		}
	}

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

// A ROUTER WHOSE WINDOW IS GONE DOES NOTHING, and the four entry points say
// so once each rather than every line below testing for it.
//
// exec() gets the ordering right -- `InputRouter router(&win)` is a local
// declared after the window reference, so it goes down first -- and that is
// not the same as the ordering being guaranteed. The window is the CALLER's:
// `exec(QApplication &, QWidget &)` borrows it, and an application that sets
// Qt::WA_DeleteOnClose on that window, or that owns a second one, can have it
// destroyed while the loop is still running. The backend is holding the
// router as its event sink by then (application.cpp, `set_event_sink`), so
// bytes keep arriving and keep being routed at a window that is gone.
//
// Which is the general shape rather than the test's particular one: nothing
// tells this class its window died, so it has to ask, and the answer has to
// mean something. See win_ in runtime.h.
void InputRouter::on_key(const KeyEvent &k) {
	if (!input_scope()) return;
	// The record before the match, not only after it. An application that
	// moved focus in a slot left this stale, and a widget-context shortcut is
	// decided against it -- measured: after `b->setFocus()`, B's own
	// Qt::WidgetShortcut refused to fire, because the repair at the end of
	// this function happens after the matching.
	if (QWidget *scope = input_scope()) {
		// NOBODY HOME, so seat somebody before the key is spent. The
		// repair on Show does this eagerly, and eagerly is not a
		// guarantee: it runs from the event queue, and an application's
		// own zero-timer can reach the loop first. Measured exactly that
		// way -- a QDialog opened with exec(), a singleShot(0) that types
		// into it, and the queued repair arriving after the dialog had
		// already been accepted and hidden, so the keystroke was lost
		// anyway.
		//
		// Here there is no race to lose: a key is about to be delivered,
		// and a layer with no focus widget would swallow it. This is the
		// same answer Qt gives on activation and the same call the hide
		// repair makes, asked at the last moment instead of the first.
		//
		// NOT the primary window, and that limit is measured rather than
		// cautious. With the conventions on this library offers arrow and
		// paging keys as a FALLBACK for when nothing takes them, and the
		// window's own no-focus state is how that fallback is reached: a
		// window whose only child is a QScrollArea pages five rows that
		// way, where seating focus on the area first gives Qt's own three.
		// A dialog has no such fallback to lose and everything to lose by
		// swallowing a key, so the repair stops at the window that owns
		// the fallback.
		if (scope != win_ && !scope->focusWidget() && input_popups().isEmpty())
			move_focus(scope, true);
		if (scope->focusWidget() != focusWidget())
			set_focus_widget(scope->focusWidget());
	}
	// Escape cancels a drag, which is what it does on every desktop -- and
	// before this nothing called drag_cancel() at all. It was written,
	// exported and never wired: an interface is only as wired as its
	// least-used method, and this tree had already been bitten by exactly
	// that and recorded it.
	//
	// Ahead of the quit keys, because a drag is a MODE and a key pressed
	// during one belongs to the mode -- and ahead of everything else for the
	// same reason a drag takes the mouse over in on_mouse(). A drag that
	// cannot be abandoned is worse than one that cannot be started: the
	// pointer is captured and every widget under it is being offered
	// something the user has changed their mind about.
	if (drag_active() && k.qt_key == Qt::Key_Escape) {
		drag_cancel();
		return;
	}
	// THE TEXT COUNTS, and leaving it out made a letter unusable as a quit
	// key -- which is the first thing an application asks for. A terminal
	// decodes an ordinary character to `qt_key = 0` with the letter in
	// `text` (ansi_backend.cpp), so a spec of {Qt::Key_Q, "q"} matched
	// nothing at all: the event's qt_key is 0 and the spec's is Key_Q. The
	// same omission the other way round is worse -- a spec of {0, "q"}
	// compared equal to EVERY printable key, because only qt_key was
	// looked at and both are 0.
	//
	// So: a spec that names text is matched on its text, and one that
	// names only a key code on the code. Ctrl-C and Ctrl-D carry a code
	// and no text and are unaffected.
	//
	// Found by checking the new free function through a real exec() rather
	// than by driving on_key() directly -- the synthetic event carried a
	// qt_key that a terminal never sends, so the in-suite check passed
	// against a shape the backend does not produce.
	//
	// AND ALL THREE MODIFIERS, which this compared two of. KeyEvent carries
	// shift, the backend decodes it -- CSI 21;2~ is F10 with the shift bit
	// -- and leaving it out of the comparison made a code-spelled quit key
	// answer its shifted chord as well. The pair that cost most is this
	// library's own: an application naming F10 also quit on Shift+F10,
	// which is the context-menu key qtty answers to everywhere else.
	//
	// STRICT, rather than reading an unset `shift` as "unspecified". There
	// is no third state to read it as: KeyEvent has one bool per modifier,
	// the other two were already compared exactly, and match_shortcut()
	// builds its QKeySequence from all three -- so a spec is a whole chord
	// to every other reader of the type, and application.h now says so.
	// The loose reading would also have to answer for Ctrl+Shift+C, and on
	// a terminal that is copy.
	//
	// It costs a text-spelled spec nothing: a letter arrives with the
	// SHIFTED character in `text` and no shift bit, so "Q" and "q" were
	// already different quit keys and still are.
	for (const KeyEvent &q : std::as_const(quit_keys_)) {
		const bool same_key = q.text.isEmpty()
		    ? (q.qt_key != 0 && q.qt_key == k.qt_key)
		    : (q.text == k.text);
		if (same_key && q.ctrl == k.ctrl && q.alt == k.alt
		    && q.shift == k.shift) {
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
			//
			// AND NOT AN ITEM VIEW, which the sentence above says quits
			// and which did not. A list, tree or table acquires
			// WA_InputMethodEnabled as soon as its current item is
			// editable -- the default for QStringListModel,
			// QStandardItemModel and every QTableWidget item -- so the
			// escape hatch written for a caret in a field was taken by
			// the commonest list in Qt. Measured, with no Copy action
			// bound anywhere:
			//
			//     a push button          Ctrl+C quits
			//     a read-only list       Ctrl+C quits
			//     the DEFAULT list       nothing at all happened
			//     a line edit            copy, as intended
			//
			// Nothing at all is what makes it a defect rather than a
			// trade: the user pressed the key every terminal program
			// answers and got silence. While such a view IS editing the
			// editor is the key target and carries the attribute on its
			// own account, so a caret still takes the key away -- which
			// is the whole of what the hatch was for. Same root as
			// 8.221, one branch along.
			const QWidget *fw = key_target();
			const bool in_text =
			    fw && fw->testAttribute(Qt::WA_InputMethodEnabled)
			    && !qobject_cast<const QAbstractItemView *>(fw);
			if (k.qt_key == Qt::Key_C && k.ctrl && in_text)
				break;
			// And Ctrl+D, on the same test and for the same reason, but ONLY
			// where readline_edit() will take it -- with the conventions on.
			// Off, it stays a quit key, because a chord that neither quits
			// nor deletes is worse than either.
			//
			// This became safe when the terminal-lost seam landed and not
			// before. While a vanished terminal was reported as a synthesised
			// Ctrl+D it came through this very loop, so giving the chord to a
			// text field would have swallowed the signal that stops a program
			// whose terminal has closed -- exactly when a field had focus,
			// which in a TUI is most of the time.
			if (k.qt_key == Qt::Key_D && k.ctrl && in_text && s_conventions)
				break;
			// THROUGH THE WINDOW, which is what a desktop's close box
			// does and what this called nobody about. `qApp->quit()`
			// ends the loop without a QCloseEvent, so every Qt
			// application's "you have unsaved changes" -- a
			// closeEvent() that calls ignore() -- was skipped in
			// silence. Measured through a real exec() with a window
			// that refuses the first close: closeEvent asked 0 times,
			// exec returned 0, the work gone.
			//
			// close() returns false when the application refused, and
			// then nothing happens: the program stays up, having said
			// so, exactly as it would on a desktop. A program that
			// asks nothing is unaffected -- close() accepts by
			// default -- so this costs the ordinary case nothing.
			//
			// THE WINDOW THIS ROUTER OWNS, not every window: quitting
			// is about the program, and the program's own close
			// handler is where it keeps that decision. An application
			// that wants the old behaviour has an empty quit-key list
			// and its own binding.
			if (win_ && !win_->close()) return;
			qApp->quit();
			return;
		}
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
			// The same defined route as the arrow conventions use, and
			// for the same reason: the cast this used to make is
			// undefined behaviour that 8.68 caught the moment anything
			// executed it.
			move_focus(scope, !k.shift);
		}
		// WITH THE REASON, which is what makes a field's contents
		// selected when Tab arrives at it -- Qt's own behaviour, and
		// invisible here until a differential run compared the two
		// builds after the same script.
		set_focus_widget(scope->focusWidget(),
		                 k.shift ? Qt::BacktabFocusReason
		                         : Qt::TabFocusReason);
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
		const bool popup_owns_input = !input_popups().isEmpty();
		// After match_shortcut() and before deliver_key(). An application's
		// own Ctrl+K must win over qtty's readline binding -- a shortcut is
		// something the program asked for by name, and the binding is a
		// convention offered on its behalf. Placing this earlier swallowed
		// exactly that, and the window-context shortcut check said so.
		QWidget *const defers = deferring_layer();
		if (defers && navigates_a_list(k)) {
			// Straight to the list, and not through match_shortcut() or
			// the mnemonic table: a desktop's popup grab puts these keys
			// in front of everything else while the list is up.
			deliver_key(defers, k);
		} else if (!match_shortcut(k) && !readline_edit(k)
		           && (popup_owns_input || !match_mnemonic(k)))
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
	if (!input_scope()) return;                 // see on_key(), above
	const QPoint px(m.cell.x() * GridMetrics::cw() + GridMetrics::cw() / 2,
	                m.cell.y() * GridMetrics::ch() + GridMetrics::ch() / 2);
	// Where the pointer is, recorded where the rest of Qt looks for it.
	//
	// QCursor::pos() is not unanswered under this platform, it is answered
	// WRONG, which is what makes it worth a write on every event. Measured on
	// Qt 6 under the offscreen platform prepare_environment() pins:
	// QOffscreenCursor is constructed at (10, 10), and QCursor::pos() returns
	// the platform cursor's position whenever the platform has one -- so it
	// answers (10, 10) for the life of the program.
	//
	// That is cell (1, 0). An application writing the standard
	// menu.exec(QCursor::pos()) opened its context menu in the top-left
	// corner of the terminal on every click, while the same application
	// written against event->globalPos() was correct. Both spellings are
	// idiomatic Qt and they disagreed, with nothing reporting a failure: the
	// compositor faithfully anchors the popup to the position it was given
	// (section 5.5), so the program looks broken rather than unsupported.
	//
	// setPos() rather than QGuiApplicationPrivate::lastCursorPosition, which
	// is private -- and which the menu-motion comment below already records
	// as unreachable for the same reason.
	//
	// It moves no real pointer and raises nothing. QOffscreenCursor::setPos()
	// does synthesise an enter, a leave and a MouseMove into whatever exposed
	// window contains the point, which would fight update_hover() below --
	// but every top level here carries WA_DontShowOnScreen, so none is
	// exposed and none is found. Measured before relying on it: five setPos()
	// calls across a shown WA_DontShowOnScreen widget delivered 0 moves,
	// 0 enters and 0 leaves, and left pos() reading back what was written.
	QCursor::setPos(px);
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
			// WEAKLY, because close() runs the application's own code and
			// this list is a snapshot taken before any of it ran. A handler
			// that closes a menu and deletes the submenu under it -- or any
			// popup deleting another, which is supported Qt since neither is
			// the one being delivered to -- leaves the rest of this walk
			// holding freed memory. 8.161's rule, met in a list.
			QVector<QPointer<QWidget>> stack;
			for (QWidget *w : ps) stack.append(w);
			for (auto it = stack.rbegin(); it != stack.rend(); ++it)
				if (QWidget *w = it->data()) w->close();
			QCoreApplication::processEvents();
			if (frame_requested) frame_requested();
		}
		return;
	}
	if (!top) {
		// A modal with an EMPTY geometry is not on the screen -- compose()'s
		// is_compositable() refuses to draw it -- so it does not own the
		// pointer either. Without this test the two rules disagreed: one said
		// the layer was not drawn, the other said it owned every click, and
		// the rectangle below contains no point at all. Measured, every click
		// on every cell of the terminal was dropped and reached nothing.
		//
		// It is qtty's to fix rather than the application's. Qt does not
		// register such a window as modal when it has a platform window --
		// activeModalWidget() is null on the desktop and the clicks get
		// through -- and it does here only because every qtty window carries
		// WA_DontShowOnScreen. Falling through is what the same application
		// already does everywhere else.
		//
		// Deliberately the MOUSE only. key_target() has the same divergence,
		// and keys reaching the empty modal are what let Escape close a
		// QDialog; redirecting those too would take away the one way out.
		QWidget *const active = QApplication::activeModalWidget();
		if (QWidget *modal = (active && !active->geometry().isEmpty())
		                         ? active : nullptr) {
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
			top = win_.data();
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
	// The offset belongs to whichever window is being SHOWN, which with a
	// tab strip up is not necessarily win_. Before tabs the two were always
	// the same and the test could be written against win_ alone.
	QWidget *const base = current_window() ? current_window() : win_.data();
	const QPoint screen = (top == win_ || top == base)
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
			// 120 to a NOTCH, which is what the fourth argument means.
			// QWheelEvent's angleDelta is in eighths of a degree and every
			// consumer divides by 120 -- QAbstractSlider accumulates the
			// remainder and ignores the event until it reaches one. This
			// passed a cell height, so a notch claimed to be 19/120 of one:
			// measured on a QListView, one notch moved NOTHING and three
			// moved a single row.
			//
			// It read plausibly because the fixtures were all QScrollArea,
			// whose bar is in pixels and whose singleStep is large enough
			// that three notches moved it a little -- and the check asked
			// only that it moved. The unit was wrong for every widget; the
			// one class tested hid it best.
			//
			// pixelDelta stays empty on purpose. A terminal reports a
			// discrete notch, which is exactly what a wheel mouse sends and
			// what angleDelta alone describes; supplying a pixel count as
			// well would ask a per-ITEM scroll bar to move by pixels, which
			// is the confusion this is fixing one field along.
			QWheelEvent ev(QPointF(w->mapFromGlobal(screen)), QPointF(screen),
			               QPoint(),
			               QPoint(m.wheel_x * 120, m.wheel * 120),
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
		// A drag takes the mouse over. While one is up a move is a drag
		// move and not a hover, and the release is a drop and not a click --
		// so the ordinary path below is skipped entirely rather than run
		// alongside, which would send a widget a mouseMoveEvent and a
		// dragMoveEvent for the same pointer.
		//
		// The press is not handled here because a drag can only begin from
		// one that has already been delivered: exec_drag() is called from
		// inside the application's own mousePressEvent or mouseMoveEvent.
		// The window tab strip owns row 0 when there is more than one
		// window, and a press there belongs to no widget: it chooses which
		// window the frame shows. Asked before anything else, because the
		// widget underneath is the one being covered by the strip.
		if (m.press) {
			if (QWidget *pick = window_tab_at(m.cell)) {
				set_current_window(pick);
				if (frame_requested) frame_requested();
				return;
			}
		}
		if (drag_active()) {
			if (m.motion) drag_move_to(target, pos, screen);
			if (m.release) drag_drop_at(target, pos, screen);
			QCoreApplication::processEvents();
			return;
		}
		// The same lifetime guard as deliver_key()'s, and the likelier of
		// the two to be met: a press reaches a button whose slot closes the
		// dialog it is in, and the release, the context-menu event and the
		// hover update below all use the same raw pointer afterwards.
		// GUARDED, NOT RETURNED FROM: the tail of this function runs
		// processEvents() and asks for a frame, and a widget that has just
		// been destroyed is exactly when the screen needs redrawing. An
		// early return here would leave the deleted panel on the terminal
		// until the next event happened along.
		QPointer<QWidget> alive(target);
		update_hover(target, pos);
		if (alive && m.press) {
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
		if (alive && m.motion) {
			// Held-button state matters: a widget reads buttons() to tell a
			// drag from a hover, so a move sent with Qt::NoButton while
			// grabbed would arrive as the pointer merely passing over.
			const auto held = grab_.isNull() ? Qt::NoButton : Qt::MouseButtons(btn);
			QMouseEvent ev(QEvent::MouseMove, QPointF(pos), QPointF(screen),
			               Qt::NoButton, held, mods);
			QApplication::sendEvent(target, &ev);
		}
		if (alive && m.press && btn == Qt::RightButton) {
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
			if (alive) {
				QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos),
				               QPointF(screen), btn, Qt::NoButton, mods);
				QApplication::sendEvent(target, &ev);
			}
			// The grab goes whether or not the widget survived: a grab
			// pointing at a destroyed widget is the same fault one frame
			// later.
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
// QPointers, because the loops below send events and an application's
// leaveEvent may delete a widget that is still in one of these lists. That
// is not the unsupported case -- a widget deleting ITSELF while handling its
// own event -- it is the supported one: the pointer moves off A onto B, A's
// handler deletes B, and B is not on the delivery stack at all. Measured
// with a fixture that does exactly that, against raw pointers: a segmentation
// fault inside the second loop, in mapFrom() on a destroyed widget.
static QVector<QPointer<QWidget>> hover_chain(QWidget *w) {
	QVector<QPointer<QWidget>> c;
	for (QWidget *a = w; a; a = a->parentWidget()) {
		c.append(a);
		if (a->isWindow()) break;
	}
	return c;
}

// Membership by identity, since a QPointer that has gone null is not the
// widget it used to hold and must not match anything.
static bool chain_holds(const QVector<QPointer<QWidget>> &chain,
                        const QWidget *w) {
	for (const QPointer<QWidget> &p : chain)
		if (p.data() == w) return true;
	return false;
}

void InputRouter::update_hover(QWidget *now, const QPoint &window_pos) {
	if (now == hovered_) return;
	// The arriving widget, weakly, BEFORE any handler runs. The tail of this
	// function records it, and a leaveEvent below may have destroyed it by
	// then -- at which point `now` is a dangling raw pointer and assigning
	// it to a QPointer is not a null assignment but undefined behaviour:
	// QWeakPointer has to read the object's own bookkeeping to attach, so it
	// reads freed memory. Measured, with a fixture whose leaveEvent deletes
	// the widget the pointer is moving onto: a segmentation fault inside
	// QtSharedPointer::ExternalRefCountData::getAndRef, at the assignment.
	//
	// A QPointer taken here attaches while the widget is alive and goes null
	// on its own when it dies, which is the whole difference.
	//
	// RE-MEASURED 2026-09-17, and the fault is no longer observable here:
	// with this line changed back to the raw pointer the suite runs green,
	// and the SANITIZED build -- which reports a read of freed memory
	// deterministically where a segfault is luck -- says nothing either.
	// The sabotage entry that used to take the run out was removed rather
	// than kept as a green line, since an entry the harness cannot satisfy
	// is worse than none. The guard stays because taking a weak reference
	// while the object is alive is right, not because anything here can
	// still prove it; 8.199 records what changed and what was tried.
	const QPointer<QWidget> arriving(now);
	const QVector<QPointer<QWidget>> was =
	    hovered_ ? hover_chain(hovered_) : QVector<QPointer<QWidget>>();
	const QVector<QPointer<QWidget>> is =
	    now ? hover_chain(now) : QVector<QPointer<QWidget>>();
	for (const QPointer<QWidget> &p : was) {
		QWidget *const w = p.data();
		if (!w || chain_holds(is, w)) continue;
		QEvent leave(QEvent::Leave);
		QApplication::sendEvent(w, &leave);
	}
	for (const QPointer<QWidget> &p : is) {
		QWidget *const w = p.data();
		if (!w || chain_holds(was, w)) continue;
		const QPointF local = w->mapFrom(w->window(), window_pos);
		QEnterEvent enter(local, local, local);
		QApplication::sendEvent(w, &enter);
	}
	// And what is recorded is the weak reference, so a widget destroyed by
	// one of the handlers above is recorded as nothing -- rather than as
	// something the next move would send a Leave to.
	hovered_ = arriving;
}

void InputRouter::on_paste(const QString &text) {
	QWidget *target = key_target();
	if (!target) return;                        // see on_key(), above

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
	// The arriving text goes into QClipboard as well, unfolded, because Qt's
	// clipboard is the only store a widget reads and without this Ctrl+V
	// inside the application pastes whatever the program itself last copied
	// -- nothing at all, in a program that has copied nothing. Measured: a
	// terminal paste into a QLineEdit worked and Ctrl+V beside it inserted
	// an empty string, so the two ways of pasting disagreed about what the
	// clipboard held.
	//
	// The RAW text, not the folded payload below: the clipboard holds what
	// the terminal sent, and the fold is a property of the single-line field
	// the paste happened to land in rather than of the text.
	//
	// Marked with terminal_paste_format() so a backend forwarding QClipboard
	// to the terminal can tell this from a copy -- see backend.h, where the
	// echo it prevents is a middle-click overwriting the user's clipboard.
	//
	// NOT a query. OSC 52 can ask the terminal what it holds, which would
	// make Ctrl+V read the real clipboard rather than the last paste, and it
	// is declined on purpose: xterm refuses a read by default and says so in
	// its own documentation, because a program that can read the clipboard
	// can read every password the user has copied. The mirror needs no
	// permission and cannot be used to look.
	if (QClipboard *const board = QGuiApplication::clipboard()) {
		auto *const mime = new QMimeData;
		mime->setText(text);
		mime->setData(QLatin1String(terminal_paste_format()),
		              QByteArray("1"));
		board->setMimeData(mime);
	}

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
	const QSize px(cells.width() * GridMetrics::cw(),
	               cells.height() * GridMetrics::ch());
	if (win_) win_->resize(px);                 // see on_key(), above

	// EVERY window that takes a turn at the terminal, not only the one this
	// router was built with. A terminal is one rectangle and the windows take
	// turns filling it, so a window that missed a resize is drawn at the size
	// the terminal used to be: measured, growing an 80x24 terminal left the
	// second window 40x10, and switching to it showed two rows of content in
	// the top-left corner of an empty screen. Shrinking is the same fault
	// wearing section 7's clothes -- the window is too big, so the policy
	// scrolls it, which is the graceful handling of a size it should never
	// have had.
	//
	// Modals and popups are excluded because their geometry is theirs: a
	// dialog is centred and clamped (8.113) and a menu is placed at its
	// anchor, and resizing either to the whole terminal would be a different
	// bug. Hidden windows are resized too -- one shown later is drawn at the
	// terminal's size like any other, and nothing else would size it.
	for (QWidget *w : QApplication::topLevelWidgets()) {
		if (w == win_ || is_popup_layer(w) || w->isModal()) continue;
		if (w->windowType() == Qt::Desktop) continue;
		w->resize(px);
	}
	QCoreApplication::processEvents();
	if (frame_requested) frame_requested();
}

// The terminal window or tab gained or lost the keyboard focus (DEC 1004).
//
// THE ARGUMENT WAS DISCARDED HERE, and the frame this asked for was
// therefore identical to the one already on the screen: the backend
// requested focus reporting unconditionally, decoded both directions, and
// handed the answer to a body that could not tell them apart. Recorded as
// 8.249 item 2 and closed by 8.251.
//
// Recorded BEFORE the frame is asked for, which is the whole point -- the
// frame is what carries the change to the screen, so a record written
// afterwards would land one frame late.
void InputRouter::on_focus_change(bool focused) {
	set_terminal_focused(focused);
	if (frame_requested) frame_requested();
}

// The terminal has gone. Not routed, not matched against the quit keys and
// not offered to the focused widget: there is no screen left to draw on and
// no keyboard to read, so the only thing to do is stop.
//
// Unconditional, which is the whole reason the seam exists. While this
// arrived as a synthesised Ctrl+D it went through the quit-key loop with
// every other key, so anything that took that chord away -- set_quit_keys(),
// or a text field claiming it -- took this with it.
void InputRouter::on_terminal_lost() {
	if (qApp) qApp->quit();
}

} // namespace Qtty
