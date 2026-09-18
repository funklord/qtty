// qtty/runtime.h -- L5/L6 runtime tier (sections 5.5, 5.6, 17.1):
// InputRouter, Compositor, FrameScheduler. Wired together by Qtty::exec();
// exposed for custom frame loops and tests.
#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QImage>
#include <QRegion>
#include <QVector>
#include <QKeySequence>          // shortcut_conflicts(), below
#include <QElapsedTimer>
#include <functional>
#include <QTimer>
#include <memory>
#include "cell.h"
#include "backend.h"

class QWidget;
class QApplication;

namespace Qtty {

// ------------------------------------------------- terminal keyboard habits
//
// A terminal has no mouse, and the conventions a person expects there are
// not the desktop's. Two of them differ from Qt outright:
//
//   Enter activates the control that HAS focus. On a desktop a focused
//   button answers to Space and not to Enter -- except inside a dialog,
//   where autoDefault makes a focused button take Enter for itself, and
//   the designated default fires only when focus is on something that is
//   not a button.
//
//   That sentence used to end "the DEFAULT button instead, wherever
//   focus is", which is the fixture's condition dropped. 8.79 found five
//   copies of the claim across this tree and only one kept the
//   condition -- the comment beside the measurement itself.
//
//   Up and Down move between controls. On a desktop they move within one
//   -- a list, a spin box -- and do nothing between them, because Tab is
//   the way and a mouse is always there.
//
// OPT-IN, and off by default, because this library's premise is that an
// unmodified application renders faithfully: an application that binds
// Enter or Down itself must keep them, and a default that quietly took
// them would break exactly the applications that had thought hardest
// about their keys. Both fire only when the focused widget IGNORED the
// key, so a list still takes its own arrows and a text field still takes
// its own Enter.
//
// `doc/keyboard-first.md` is the guide this belongs to, and says what to
// do instead where an application would rather do it itself.
void set_keyboard_conventions(bool on);
bool keyboard_conventions();

// What the conventions bind, as key/meaning pairs, so an application can
// SHOW them.
//
// A terminal user cannot discover a binding by looking for a button, so
// `doc/keyboard-first.md` asks every application to put its keys on the
// screen -- and an application that wrote "F6 window" into its own status
// bar would be keeping a second copy of a fact this library owns, which
// goes stale the day the binding moves. This hands over the list instead.
//
// EMPTY when the conventions are off, rather than a list of what they
// would be. An application can then render it unconditionally and be
// right either way, which is the only version that cannot lie to a user.
QVector<QPair<QString, QString>> keyboard_conventions_help();

// ------------------------------------------- eight questions about a window
//
// Each takes the window (or any container in it) and answers something an
// application has no other way to find out, because the answer lives in this
// library's own tables or in what a frame actually drew. Seven of the eight
// are asserted EMPTY, which is the assertion that keeps working as a window
// grows: a list checked by name needs editing every time a control arrives,
// and an empty one goes red the day somebody adds a control that collides,
// reads backwards, hides its words, shows no focus mark or cannot be
// reached at all.
//
// They are diagnostics rather than frame-loop calls -- one of them renders
// the window twice per control and moves the focus while it looks -- so they
// belong in a test. Each answers empty for a null scope rather than
// refusing.

// Every widget Tab reaches inside `scope`, in the order it reaches them,
// using the router's own traversal rather than a second copy of it. For
// the test `doc/keyboard-first.md` asks every application to write: that
// each control it owns can be reached without a mouse.
QVector<QWidget *> keyboard_reachable(QWidget *scope);

// The Alt+letters more than one control in `scope` answers to, with the text
// of each claimant. Which one answers depends on where the focus is: a menu
// action beats a button or a buddy label wherever it sits, and between two of
// the same kind the one nearest the focus wins (8.175). Empty when every
// mnemonic is unique, which is what a test asserts.
//
// The first practice in `doc/keyboard-first.md` is "give every control a
// mnemonic", and the failure it creates scales with how well it is followed:
// two controls claiming one letter is silent. Nothing is drawn differently,
// nothing is logged, and the key does the other thing for ever. This is the
// only way an application can be told, and it reads the router's own
// enumeration rather than a second copy of it.
QVector<QPair<QChar, QStringList>> mnemonic_conflicts(QWidget *scope);

// And the same question for chords: the key sequences more than one thing in
// `scope` answers, with the winner named first -- which is the NEAREST
// claimant to the focus rather than the first the walk finds, since that is
// how the router chooses (8.174). Actions and QShortcuts both,
// including the application-context ones in other windows, because those
// answer here by definition.
//
// Qt reports this on a desktop and cannot here. QShortcutMap is what detects
// an ambiguous binding, it gates on the window being active, and no window
// activates under qtty -- so the toolkit's own answer is gone and this
// replaces it.
//
// CONTEXT DECIDES, so the answer is not a list of repeated sequences. Two
// Qt::WidgetShortcut claims on different widgets are not a conflict, only
// one of them ever being in play; a chord is reported where some focus a
// user can reach makes two claims answer at once.
//
// "A focus a user can reach" is wider than the tab chain, and saying so is
// not pedantry: a `Qt::ClickFocus` field named by a `&Notes` label is no tab
// stop, `Alt+N` focuses it, and claims that answer only there were invisible
// to this until 8.184. Tab stops, a click, and the buddy of every mnemonic,
// whatever its focus policy.
QVector<QPair<QKeySequence, QStringList>> shortcut_conflicts(QWidget *scope);

// Which of those rows this window has taken back, and who took them.
//
// `keyboard_conventions_help()` has no scope to ask, so it promises what the
// library answers to -- and an application that binds `Ctrl+K` to Insert Link
// or `F6` to a panel of its own has removed that convention from its own
// window while still showing the row. Measured, with the conventions on: a
// `Ctrl+K` action fires and the line is left whole, and an `F6` action fires
// instead of the window moving. So the list is a true statement about the
// library and a false one about that window, which is the failure the help
// function exists to prevent, arriving from the side it cannot see.
//
// Keyed by the row as `keyboard_conventions_help()` spells it, so a status
// bar can strike out or drop exactly the row it would otherwise show. Empty
// when nothing is shadowed, which is what a test asserts.
//
// ONLY THE CHORD-SHAPED ROWS. `Enter`, `Up/Down` and the arrows answer only
// where the focused widget ignored the key, so a widget that wants them is
// not shadowing a convention -- it is the case the conventions were built to
// yield to. `Alt+letter` is `mnemonic_conflicts()`'s question and is
// reported there, in far more detail than a row could carry.
QVector<QPair<QString, QStringList>> conventions_shadowed(QWidget *scope);

// The buttons in `scope` that only a pointer can press: visible, enabled,
// and reached by no key at all -- not by Tab, not by a mnemonic, and not by
// any chord that would fire the action they carry.
//
// The population is `QAbstractButton` and `QSplitterHandle` -- Qt's own words
// for a control you click and one you drag, rather than a judgement about
// which widgets matter -- plus a `QHeaderView` that is showing a sort
// indicator, which is the one case where the thing a pointer acts on is a
// SECTION rather than a widget. Nothing sorts a column from the keyboard,
// in this library or in Qt: `QHeaderView` has four mouse handlers and no
// `keyPressEvent` (8.203). Resizing and reordering columns are pointer-only
// too and are deliberately not named -- they change how the data looks
// rather than which data is shown, and naming every header in every
// application makes a report nobody reads.
// The subtraction is the part an application cannot write for itself: a
// toolbar button holds an action whose mnemonic and shortcut reach it while
// the button itself is `Qt::NoFocus` and in nobody's tab chain, so a sweep
// over the focus chain alone reports it pointer-only and is wrong.
//
// TWO LIMITS. A `QShortcut` wired straight to a button's `click()` is
// invisible -- Qt publishes no way to ask what a connection reaches -- so
// that button is named although a key does reach it; a mnemonic or a
// `QAction` says the same thing in a form this can see. And the scope itself
// is not examined, only what is inside it.
//
// Practice 4 of `doc/keyboard-first.md` is "never let an action be reachable
// only by pointer", and this is how an application asserts it. Empty is the
// answer to assert -- with the caveat that Qt breaks the practice on the
// application's behalf in two places (8.159), so a closable tab or a dock
// widget puts Qt's own buttons in the list until the application gives the
// same action a key.
QVector<QWidget *> pointer_only(QWidget *scope);

// Consecutive tab stops that go BACKWARDS against the reading order: each
// pair is a widget and the one Tab reaches after it, where the second sits
// above the first, or level with it and to its left.
//
// Practice 3 of `doc/keyboard-first.md` is "make the tab order the reading
// order", and it is the one piece of advice there with nothing to check it:
// Qt's order is construction order, which is usually right and silently
// stops being right when somebody inserts a widget into a layout. On a
// terminal that order is the only one a user experiences -- there is no
// glancing across to the field they wanted.
//
// GOING UP AND TO THE RIGHT IS NOT AN ANOMALY, because that is how a
// side-by-side layout is meant to be walked: down one column, then up to the
// top of the next. Qt's own `QFontDialog` does exactly that, and without the
// exception it was the only false report in the corpus this was measured
// against -- five of Qt's dialogs, a wizard, two panels, a tab widget and a
// form, all clean, with a form whose second row was inserted after the third
// reported correctly.
//
// Only pairs that share an immediate parent, since a walk leaving one
// container for the next says nothing about either one's order.
QVector<QPair<QWidget *, QWidget *>> tab_order_anomalies(QWidget *scope);

// The widgets whose words only a hover reveals: a tool tip, no status tip.
//
// Practice 7 of `doc/keyboard-first.md` is "do not depend on hover or
// tooltips", and the reason is measured rather than assumed -- no
// `QEvent::ToolTip` is ever raised here, even after its timer, so whatever a
// tool tip says cannot be got at by any means a keyboard offers. A status
// tip can: with the conventions on it follows FOCUS, and a `QMainWindow`
// shows it with no code at all.
//
// NOT the icon-only buttons, whose tip this library already draws AS the
// label (8.8) -- those hide nothing. Measured, that exclusion is what keeps
// the answer usable: `QFileDialog`'s six navigation buttons are all tip-only
// and all of them are labels here, so without it every file dialog would
// report six findings nobody can act on.
//
// A tool tip that merely repeats a label is still named, because the library
// cannot tell a repeat from an elaboration and the remedy is one line
// either way. And where the widget cannot take focus at all, a status tip on
// IT will not show -- the words belong in its label, or on the control the
// user will be standing on.
QVector<QWidget *> hover_only(QWidget *scope);

// The controls that look the same focused and unfocused: rendered once with
// the focus on them and once without, and identical inside their own
// rectangle -- glyphs, attributes and colours, since focus here is usually
// spelled with an attribute rather than a glyph.
//
// Practice 10 of `doc/keyboard-first.md` is "in a custom widget, draw your
// own focus mark", and it is a trap rather than advice: `hasFocus()` is
// permanently false here, so a widget that asks Qt whether it has focus
// draws nothing, on the terminal only, with no error anywhere. The ten Qt
// controls this library's own suite sweeps all pass -- button, line edit,
// check box, radio, combo, spin box, slider, list, tab widget, scroll bar
// -- and the ones that fail are the ones somebody wrote.
//
// WIDGETS THAT EDIT TEXT ARE NOT NAMED. They show focus with the terminal's
// own cursor, which is placed on whatever sets `WA_InputMethodEnabled` --
// the same attribute practice 11 asks a custom text widget to set, and the
// same one this library already keys the quit keys on. A line edit draws no
// mark and needs none.
//
// IT MOVES THE FOCUS while it looks, and puts back the widget and the
// library's own record of it. An application whose focus handlers do work
// will see that work happen, which is why this belongs in a test rather than
// in a frame loop.
QVector<QWidget *> focus_invisible(QWidget *scope);

// ---------------------------------------------------------------- InputRouter
// Owns everything Qt's platform layer would normally own (measured F3/F4):
// the shortcut table (synthetic keys never reach QShortcutMap), focus
// (no window ever activates), popup/modal routing, and popup attribute
// stamping via a global event filter.
class InputRouter : public QObject, public ITerminalEventSink {
public:
	explicit InputRouter(QWidget *window);
	~InputRouter() override;

	// ITerminalEventSink
	void on_key(const KeyEvent &) override;
	void on_mouse(const MouseEvent &) override;
	void on_paste(const QString &) override;
	void on_resize(QSize cells) override;
	void on_focus_change(bool) override;
	// The terminal has gone, so there is nothing left to draw on and nothing
	// to type into. Quits unconditionally: it is NOT a quit key, and none of
	// the things that can take a quit key away -- set_quit_keys(), a text
	// field holding Ctrl+C, the readline binding holding Ctrl+D -- may take
	// this, because what it reports is not a chord anybody pressed.
	void on_terminal_lost() override;

	// Keys that quit the application (default: Ctrl-C, Ctrl-D).
	void set_quit_keys(const QVector<KeyEvent> &);

	// Visible popup stack in z-order, maintained by the stamping filter.
	QVector<QWidget *> popups() const;
	// The subset of those that can own keys: not a tooltip, not a window
	// the application declared transparent for input. The compositor draws
	// popups(); input asks this one (8.195).
	QVector<QWidget *> input_popups() const;
	// The drawn layer, if any, that defers its keys to the widget being
	// edited -- a QCompleter's list. Navigation goes to it, text to the
	// editor; see the implementation for why neither alone works.
	QWidget *deferring_layer() const;

	// The widget key events target right now (popup > modal > window focus).
	// Nothing outside the active modal is ever returned: section 8.3 requires
	// input outside activeModalWidget() to be dropped before dispatch, and
	// there is no window manager to enforce it.
	QWidget *key_target() const;

	// True for a layer the popup stack owns -- Qt::Popup or Qt::ToolTip. The
	// Compositor asks the same question, so it is answered in one place: a
	// window the router tracks must not also be drawn by the top-level walk.
	static bool is_popup_layer(const QWidget *);

	bool eventFilter(QObject *, QEvent *) override;   // popup stamping + tracking

	// Set by FrameScheduler: called after each handled input batch.
	std::function<void()> frame_requested;

	// Where the Compositor has scrolled the ROOT layer, in cells. Set by
	// compose(); a click carries a screen position and the root may not be
	// drawn at the screen's origin, so the two have to agree about the offset
	// or every press lands on the wrong widget.
	void set_root_scroll(QPoint cells);

	// Which window keys and clicks go to, when the application has more than
	// one. Set by the Compositor this router was handed to, from
	// Qtty::set_current_window() and next_window()/previous_window().
	//
	// AN APPLICATION CALLS THOSE, NOT THIS. Moving input without moving what
	// is drawn is precisely the defect this exists to fix -- keys arriving in
	// a window the user cannot see -- and calling this directly reintroduces
	// it from the other side.
	void set_input_window(QWidget *);

private:
	bool match_shortcut(const KeyEvent &);
	bool readline_edit(const KeyEvent &);
	// Alt-<letter> against the `&` markers in action text (section 17.2). A
	// separate matcher because a mnemonic is not a shortcut: it carries no
	// Qt::Key at all -- a terminal sends ESC then the letter -- and it opens
	// a menu where a shortcut triggers an action.
	bool match_mnemonic(const KeyEvent &);
	void deliver_key(QWidget *target, const KeyEvent &);
	// The widget tree input is allowed to reach: the active modal if there is
	// one, else the window (section 8.3).
	QWidget *input_scope() const;

	// A QPointer, because THIS ROUTER OUTLIVES ITS WINDOW AND POSTS WORK
	// FROM INSIDE THAT WINDOW'S OWN DESTRUCTOR.
	//
	// Deleting a visible widget sends QEvent::Hide -- measured, to the
	// QWidgetWindow, the widget and each visible child -- so the focus
	// repair in eventFilter() fires while the window is being destroyed
	// and posts a queued call to this router. The call is delivered on
	// the next pass of the event loop, by which time the window's memory
	// is gone, and `input_scope()` handed the lambda the freed pointer:
	// heap-use-after-free at input_router.cpp:406, on the 40 bytes of a
	// QWidget, caught by the sanitized suite.
	//
	// QCoreApplication::removePostedEvents() does not reach it, and the
	// reason is worth stating rather than rediscovering: it is keyed on
	// the RECEIVER, and the receiver is this router, which is perfectly
	// alive. What died is a widget the receiver points at, and Qt has no
	// way to know that. The lambda's own captures were QPointers already;
	// the one pointer it did not have to capture, because it came in
	// through `this`, is the one that dangled.
	//
	// The quieter half is the reason every other widget this class
	// remembers is a QPointer too, and is the argument grid_style.cpp
	// makes about its own focus record: Qt reuses heap addresses, so a
	// new window landing where the old one was compares EQUAL to `win_`
	// in the ownership tests scattered through input_router.cpp -- the
	// `p == win_` walks that decide which router answers a Hide or a
	// Show -- and a dead router would start answering for somebody
	// else's window. A crash is the loud version of that; the silent
	// version is worse, and the regression fixture caught it happening
	// on the first run.
	//
	// So a router whose window is gone has no input scope, and everything
	// that needs one does nothing. See input_scope() and the guards at
	// the top of on_key(), on_mouse(), on_paste() and on_resize().
	QPointer<QWidget> win_;
	// The window the compositor is drawing, once it has said so. Null until
	// then, which means win_: a router with no compositor -- a test's, over
	// its own window -- keeps the window it was built with.
	QPointer<QWidget> cur_;
	QVector<QPointer<QWidget>> popups_;
	// Who had the focus before F10 put the menu bar into keyboard mode.
	// Qt remembers this itself and restores it when the bar leaves that
	// mode -- from QApplication::focusWidget(), which is permanently null
	// here, so it remembers nothing and the bar keeps the focus after the
	// menu closes. See the F10 branch in on_key().
	QPointer<QWidget> before_menu_bar_;
	QVector<KeyEvent> quit_keys_;
	// The widget a press grabbed, held until the release (section 5.5). A
	// QPointer because a press can destroy its own target -- a button that
	// closes a dialog -- and the release then arrives for a widget that is
	// gone.
	QPointer<QWidget> grab_;
	// The widget the pointer is over, so that Enter and Leave can be sent
	// when it changes. The platform layer normally does this -- the same
	// reason the right-press context menu is synthesised here -- and without
	// it QWidget::underMouse() is permanently false and no application's
	// enterEvent() or leaveEvent() ever runs.
	QPointer<QWidget> hovered_;
	QPoint root_scroll_;
	void update_hover(QWidget *now, const QPoint &window_pos);
	// The last press, for recognising a double click. The platform layer
	// does this too, from QApplication::doubleClickInterval(); with no
	// platform, QWidget::mouseDoubleClickEvent() never ran anywhere.
	QElapsedTimer since_press_;
	QPoint last_press_cell_ = QPoint(-1, -1);
	int last_press_button_ = 0;
};

// ----------------------------------------------------------------- Compositor
// Walks QApplication::topLevelWidgets() in z-order into one CellBuffer
// (section 5.4 step 3), then stacks activeModalWidget() and the popups on top
// as section 8.1 asks -- explicitly, rather than trusting window flags. Layers
// are kept inside the terminal rectangle: an anchored one (menu, drop-down,
// tooltip) flips to the other side of its anchor, everything else slides.
// Placements and the cursor position (section 5.5) are collected as it goes.
class Compositor {
public:
	Compositor(QWidget *window, InputRouter *router);
	// Takes the window tab strip down with it. The strip is a property of a
	// composed FRAME, and its record is a file static that InputRouter reads
	// on every press -- so without this it outlived the compositor that drew
	// it and a later press in row 0, in a test or a second phase of a
	// program, was read as a tab selection for windows nobody is showing.
	~Compositor();
	void compose(CellBuffer &out);                        // fills out + out.images
	// design.md section 7's small-terminal policy, first half, on the ROOT.
	// Public so the behaviour can be exercised without a terminal; compose()
	// calls it, and calls it again for whichever layer owns input.
	void apply_priority(int cols, int rows);
	std::optional<QPoint> cursor_cell() const;             // after compose()

private:
	// What section 7's policy remembers about ONE layer. It belongs to the
	// layer that owns input rather than to the root: a modal owns input while
	// it is up (section 8.3), and two layers cannot share either half of this
	// without one of them scrolling to the other's focus or showing widgets
	// the other hid.
	struct Layer {
		// Where the layer is scrolled to, in cells. A layout refuses to
		// shrink below its minimum, so on a terminal smaller than that the
		// layer keeps its size and the frame simply stops -- measured on a
		// nine-cell dialog in a six-row terminal, which showed six fields and
		// neither the last two nor the button that closes it. design.md
		// section 7 names the policy: drop the optional widgets, then scroll.
		// This is the second half, which needs no annotation from the
		// application.
		QPoint scroll;
		// The widgets THIS pass hid, so that growing the terminal back shows
		// exactly those and no others. A widget the application hid for its
		// own reasons must stay hidden, and it is not in here.
		QVector<QPointer<QWidget>> dropped;
	};

	void apply_priority(QWidget *layer, Layer &state, int cols, int rows);
	// Section 7's second half for one layer: move `state.scroll` so that the
	// layer's focused widget is inside a cols x rows terminal.
	void follow_focus(QWidget *layer, Layer &state, int cols, int rows);

	// Where a popup asked to be, and where compose() last put it. A popup is
	// MOVED to the position it is drawn at rather than drawn at an offset --
	// InputRouter::on_mouse() hit-tests it against its own geometry() -- so
	// its geometry stops being the position it was opened at, and an offset
	// applied to that geometry a second time would compound rather than
	// replace. `anchor` is the answer to "where does this belong"; `placed` is
	// how a popup that moved ITSELF is told apart from one compose() moved.
	struct PopupPlace { QPoint anchor, placed; };

	Layer root_;
	// Which window root_ describes. The drawn window can change under a
	// window switch, and a layer's scroll and dropped-widget list are that
	// layer's own -- see the same pair for modals and popups below.
	QPointer<QWidget> root_layer_;
	// The modal that owns input, and the state the policy keeps for it. Reset
	// when the layer changes, because what one layer hid must not outlive the
	// reason for hiding it.
	QPointer<QWidget> input_layer_;
	Layer input_;
	// The same pair for the popup stack, whose top owns input above any modal
	// (section 5.5's routing order).
	//
	// A menu taller than the terminal is section 7's hardest case and is NOT
	// a trap, which this said it was. Qt will not paginate one -- the
	// offscreen QScreen is 800x800, so it never decides the menu is too tall
	// -- and section 7's scrolling carries it instead. Measured on a
	// forty-item menu forty-two cells tall in a twenty-four row terminal:
	//
	//     before any key   item-00 .. item-22
	//     after 35 Down    item-11 .. item-34
	//     after 40 Down    item-16 .. item-39
	//
	// so the selection stays on screen, the last item is reachable, and
	// Escape closes it.
	//
	// What this used to say was that such a menu "cannot be Tabbed away
	// from". That is TRUE -- Qt menus ignore Tab, and the measurement above
	// confirms the menu is still open after one -- and it is the wrong
	// sentence, because Tab is not how anybody leaves a menu and the two
	// facts that decide whether a user is stuck were both absent. A true
	// statement that characterises the situation wrongly reads as a
	// limitation nobody should try to fix.
	QPointer<QWidget> popup_layer_;
	Layer popup_;
	QHash<QWidget *, PopupPlace> popup_place_;
	// The modals this compositor placed itself, and where it put them. A
	// dialog Qt positioned against its fictional 800x800 screen is centred in
	// the terminal instead; one the application moved is left alone, and an
	// entry disappears the moment its dialog's geometry stops matching what
	// was written here.
	QHash<QWidget *, QPoint> modal_place_;
	QWidget *win_;
	InputRouter *router_;
	std::optional<QPoint> cursor_;
};

// ------------------------------------------------------------- FrameScheduler
// Coalesces frame production (section 5.4): renders through the Compositor at most
// once per interval, diffs, presents. Frames are requested by the router
// after input and by a global UpdateRequest watcher; a coarse idle tick
// catches timer-driven model updates.
class FrameScheduler : public QObject {
public:
	FrameScheduler(ITerminalBackend *backend, Compositor *compositor, QWidget *window);
	// What the PIXEL path has to repaint, which is not the cell diff.
	// Anything composited OVER the cells -- an overlay, a placement -- that
	// MOVES changes pixels under cells that did not change, so the region is
	// the diff united with those rectangles from before and after. Passing
	// the cell diff straight through looks correct, compiles, and leaves a
	// ghost wherever one of them moved.
	//
	// `was` and `now` are both kinds together. Overlays needed new state
	// (`prev_overlays_`); placements did not, because `prev_` is the previous
	// CellBuffer and carries its own images.
	//
	// Static and public so it can be tested without a frame loop: the union
	// is the part that can be wrong, and the wiring is one call.
	static QRegion pixel_damage(const QRegion &cells,
	                            const QVector<QRect> &was,
	                            const QVector<QRect> &now);

	void request_frame();                                   // coalesced
	void render_now();                                      // immediate (initial frame)
	bool eventFilter(QObject *, QEvent *) override;        // UpdateRequest watcher

	// How long a burst of damage is coalesced before a frame goes out, in
	// milliseconds. design.md section 5.4 has described this as
	// "configurable" since it was written, and section 11 goes further --
	// "16 ms local, 50 ms over ssh; FrameScheduler coalesces to whichever
	// applies" -- while the number was a bare literal in one expression with
	// nothing able to reach it.
	//
	// 0 is legal and means "as soon as the event loop comes back", which is
	// what an application driving its own loop may want; the coalescing is
	// then only what the loop itself merges. Negative is refused rather than
	// clamped, because a caller passing one has made a mistake and a silent
	// 0 would hide it.
	void set_frame_interval(int ms);
	int frame_interval() const { return frame_ms_; }

private:
	ITerminalBackend *backend_;
	Compositor *comp_;
	QWidget *win_;
	// Declared after win_ so the initialiser list runs in declaration order;
	// -Wreorder is right that the two disagreeing is a trap waiting for
	// whoever adds a member that reads another.
	int frame_ms_;
	QTimer coalesce_;
	QTimer idle_;
	QElapsedTimer since_last_;
	std::unique_ptr<CellBuffer> prev_;
	// Where the overlays were last frame, in cells. Kept because nothing
	// else remembers it and pixel_damage() cannot be computed without it.
	QVector<QRect> prev_overlays_;
	// How many overlay ids the terminal is currently holding. Overlay ids
	// are indices into the z-sorted visible list, so when that list shrinks
	// the tail has to be deleted explicitly -- nothing about a smaller list
	// removes the placements the terminal already has.
	int live_overlay_ids_ = 0;
	// Handovers already acted on. Compared rather than cleared, so that this
	// scheduler reading the news cannot hide it from anything else that also
	// keeps a copy of the screen.
	int seen_handovers_ = 0;
	// The composited picture, kept between frames so only the damaged cells
	// have to be rasterised into it. Rasterising 200x60 measures 18.4 ms
	// against section 11's 16 ms budget, so on the software-composite path
	// the render is the cost and damage-limiting the TRANSMISSION saved none
	// of it. Discarded when the grid resizes, which is the one thing that
	// makes every pixel in it wrong at once.
	QImage pixels_;
};

} // namespace Qtty
