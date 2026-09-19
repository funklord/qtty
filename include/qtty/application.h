// qtty/application.h -- L6 runtime entry points (section 5.6).
//
// Canonical usage (see example/chat/main.cpp):
//     decide frontend
//       -> Qtty::prepare_environment()   BEFORE QApplication
//       -> QApplication ctor
//       -> Qtty::setup(app)             BEFORE any widget (font + style)
//       -> construct shared widgets
//       -> Qtty::exec(app, win)  /  win.show(); app.exec()
#pragma once
#include <QWidget>
#include <functional>
#include "cell.h"
#include "backend.h"

class QApplication;

namespace Qtty {

// Selects the offscreen platform and pins scaling. MUST precede QApplication.
void prepare_environment();

// Installs the cell-metric font and GridStyle. MUST precede widget
// construction: the shared UI derives its metrics from the application font.
//
// WHAT "MUST" MEANS HERE, measured rather than asserted, because the font
// enforcer (8.180) covers more of this than the rule implies. A window of
// ordinary widgets built BEFORE this call renders byte-for-byte the same as
// one built after: the enforcer puts the grid font back on every widget, and
// a layout measures at layout time, not at construction.
//
// What it cannot put back is a number somebody already wrote down. A widget
// that measures in its CONSTRUCTOR --
// `setFixedHeight(QFontMetrics(font()).height() * 2)` -- gets whatever font
// the application had at that moment. Measured: 17 px a row before this call
// against the grid's 19, so the widget fixes itself at 34 px where two rows
// are 38, and no later font change moves it. The rule is therefore real and
// its reason is narrow: call this first, and a constructor that measures is
// measuring the grid.
void setup(QApplication &app);

// Write out any diagnostics that were held back while qtty owned the terminal,
// and stop holding them. setup() installs a message handler that buffers
// instead of writing when stderr is a terminal, because that terminal is the
// one the frame is on: a qWarning lands in the middle of the drawing, and
// nothing repaints over it because the cell plane never changed.
//
// It is called for you when the backend gives the terminal back. An
// application that takes the screen some other way can call it itself.
void flush_deferred_messages();

// Run `win` on `backend` until quit. The backend is L1's seam (section 5.1):
// a legacy adapter, termpaint, or NullBackend under test all drive the same
// runtime through it. This overload is what makes the seam real -- until it
// existed, exec() constructed an AnsiBackend on its own stack and no other
// backend could reach the runtime at all, which blocked Phase 1 and left the
// section 9 harness unable to use the backend it was specified in terms of.
int exec(QApplication &app, QWidget &win, ITerminalBackend &backend);

// Run `win` full-screen on the controlling terminal until quit. The
// convenience form of the above, on the built-in AnsiBackend.
int exec(QApplication &app, QWidget &win);

// Hand the terminal back for the duration of `body`, and take it again
// afterwards -- running an editor, a pager, or anything else that wants the
// screen. Returns false when there was no terminal to hand over, in which
// case `body` has still run: a program whose output is a pipe has nothing to
// suspend and its work should happen anyway.
//
// It exists because the pair it wraps could not be reached. backend.h has
// named this case from the start -- suspend() is documented as being for
// "SIGTSTP / shelling out" -- and both halves are pure virtuals on
// ITerminalBackend, which the public headers only ever CONSUME: exec() takes
// a backend and nothing hands one out, and the only concrete implementation
// an installed program gets is NullBackend, whose suspend() and resume() are
// empty. So the feature was implemented, carefully maintained, and callable
// by nobody.
//
// That is the same fault Qtty::capabilities() was added for, and this follows
// its answer rather than inventing one: a free function rather than a handle
// nobody is given.
//
// WHICH backend it acts on is where the two part company, and the difference
// is deliberate. This one asks who currently OWNS the terminal, because its
// return value is a claim that a screen was handed over and only ownership
// supports that claim -- a backend with no screen would make it a false one.
// capabilities() asks which backend is DRIVING the session first, and falls
// back to ownership; a terminal's cell size does not stop being true while a
// child has the screen. See 8.153: the two were one sentence in this comment
// for a while, and they are not one fact.
//
// Scoped rather than a suspend()/resume() pair, because the pair has a wrong
// way to use it and this does not -- the terminal comes back if `body`
// throws, and there is no way to forget the second call.
bool shell_out(const std::function<void()> &body);

// What the terminal qtty is driving turned out to be, as negotiated (section
// 5.7). Valid while a backend is driving -- under exec(), and equally in a
// frame loop an application runs itself, which has no exec() to ask and was
// answered "nothing known" until 8.153. A default-constructed Capabilities
// when neither is true, which reads as "nothing known" rather than as a
// claim.
//
// It exists because an application had no way to ask. Capabilities were
// reachable only through ITerminalBackend, and the convenience exec() builds
// its backend internally -- so every field on that struct was declared and
// unreachable from the seat an application sits in, which is section 7.4's
// own fault. The three fields the negotiation added made it three instances
// worse before this closed it.
//
// What an application does with the answer is real work rather than
// curiosity: Qtty::cells() needs cell_px to size an image without squashing
// it, and a lower graphics tier needs the background to composite against.
Capabilities capabilities();

// How big the terminal is, in cells, while a backend is driving. An empty
// QSize when none is -- the same "nothing was measured" the empty
// Capabilities above means, and for the same reason: a stale answer is
// worse than none, because a caller cannot tell one from a current one.
//
// It exists because the number was measured, used to size the window, and
// then withheld. ITerminalBackend::size() returns cells and the convenience
// exec() builds its backend internally, so this was the one arrangement
// where the value an application most obviously wants -- how many columns
// have I got -- could not be reached from any installed header.
//
// NOT a field on Capabilities, deliberately. Capabilities is a snapshot of
// what was NEGOTIATED, and a terminal is resized by the user at any moment;
// a cached copy of the size would be the one field on that struct capable
// of being confidently wrong. This asks the live backend every time, so it
// cannot go stale and no backend has to remember to fill it in.
//
// AND NOT THE USABLE AREA, which is the distinction to hold on to. This is
// the terminal's extent. The window bar takes a row off the top when it is
// shown, and a window whose layout refuses to shrink can be LARGER than the
// terminal, so neither this nor win.height() / GridMetrics::ch() answers
// "how many rows may I draw in" -- for that, ask the widget you are laying
// out. What this answers is how big the screen is, which is the question
// behind "is this terminal narrow enough that the sidebar should go".
QSize terminal_cells();

// Whether the terminal window or tab qtty is drawing into has the keyboard
// focus, the way QWidget::isActiveWindow() answers for a desktop window.
// True until something says otherwise.
//
// THE TERMINAL HAS ALWAYS REPORTED IT AND NOTHING LEARNED IT. Focus
// reporting (DEC 1004) is in the startup string unconditionally, both
// directions decode in the backend, and the sink they reached discarded the
// argument -- so `ESC[O` and `ESC[I` did the same thing, which was to ask
// for a frame identical to the one already on the screen.
//
// WHY IT IS A RECORD OF OUR OWN RATHER THAN QT'S ACTIVATION. On a desktop
// Qt carries this in the window's activation, and two things follow from a
// deactivation -- measured under xcb on a real display, one window
// activated away from another:
//
//     QApplication::focusWidget()   the edit    ->  null
//     QWidget::hasFocus()           1           ->  0
//     State_HasFocus, State_Active  1           ->  0
//     palette colour group          Active      ->  Inactive
//     QLineEdit::hasSelectedText()  1           ->  1        (kept)
//
// Neither reaches this library. No qtty window ever activates -- every top
// level carries Qt::WA_DontShowOnScreen -- so the first three are already
// nailed to the unfocused answer and cannot move, and
// QWidgetPrivate::colorGroup() has every visible widget permanently in the
// Inactive group. Nor is the group worth driving: measured under the theme
// prepare_environment() pins, ALL 21 palette roles are byte-identical
// between Active and Inactive, so Qt's own mechanism for saying "inactive"
// changes no colour here at all. And sending QEvent::WindowDeactivate by
// hand moves none of the five -- also measured -- so it would be an event
// with nothing behind it.
//
// So the state is qtty's to keep, and this is where an application asks for
// it. GridStyle consults it for the same reason it consults
// Qtty::focusWidget() rather than QWidget::hasFocus().
//
// WHAT CHANGES WHEN IT IS FALSE is the focus MARK and nothing else, which
// is what the measurement above licenses: the control that owns the focus
// stops being drawn as owning it, exactly as a desktop widget loses its
// focus rectangle, and a selection stays exactly as it was, exactly as
// QLineEdit::hasSelectedText() does. Qtty::focusWidget() is unchanged and
// still answers who the keys go to -- that is Qt's per-window record, which
// a deactivation does not clear either.
//
// TRUE IS THE ABSTENTION, and the asymmetry is color_scheme()'s. A terminal
// with no focus reporting says nothing, for ever; taking silence for "not
// focused" would unmark the focused control in every such session, which is
// a real loss of information for no report. Taking it for "focused" costs
// nothing anybody can see.
bool terminal_focused();

// Report that the terminal gained or lost the focus. exec() wires this to
// the backend's own decoder and an application needs it only when it drives
// a backend through its own frame loop, which backend.h supports and which
// has no exec() to do the wiring -- the same seat capabilities() and
// set_quit_keys() were opened for.
//
// Process-wide, like set_keyboard_conventions() and set_focus_widget(), and
// reset to focused at both ends of exec() so that a run cannot inherit the
// last state of the one before it.
void set_terminal_focused(bool focused);

// Ring the terminal's bell, which is what a terminal has instead of a beep
// and instead of a taskbar entry that flashes. Nothing happens when no
// backend is driving -- the same "nothing was measured" the empty
// Capabilities above means, and for the same reason: there is no terminal to
// ring, and ringing the process's controlling one behind the library's back
// would be a write to a screen qtty does not own.
//
// It is worth more than a noise. Most terminal emulators map BEL to the
// window-urgency hint -- the thing that marks a background tab -- so one
// signal covers what QApplication::beep() means and what
// QApplication::alert() means, which on a desktop are two different
// mechanisms.
//
// AN APPLICATION HAS TO CALL THIS, and that is a real shortfall stated
// rather than hidden. It is the same one Qtty::exec_drag() and
// Qtty::SystemTrayIcon state in drag.h and tray.h, and it has the same
// cause -- Qt asks the platform, and the platform is a stub -- but it is
// worse here, because BOTH of the ordinary spellings are closed:
//
//   QApplication::beep()      is static and non-virtual, and its whole body
//                             is QPlatformIntegration::beep(), whose base
//                             implementation is an empty function the
//                             offscreen plugin does not override. Measured
//                             in Qt 6.12.0's sources: there is no seat
//                             between the call and the empty body.
//   QApplication::alert(w)    is static too, and reaches QWindow::alert(),
//                             which is not virtual either and returns at
//                             once when the window has no platform window.
//                             qtty sets Qt::WA_DontShowOnScreen on every top
//                             level, so QWidgetPrivate::show_sys() returns
//                             before anything is created and there never is
//                             one. Even given one, QPlatformWindow's
//                             setAlertState() is empty and isAlertState()
//                             answers false, and the offscreen plugin
//                             overrides neither.
//
// So neither is interceptable, and NEITHER STARTS WORKING because this
// exists. This is not QDesktopServices::openUrl(), where Qt publishes a
// handler in front of the platform and qtty registers one, so the standard
// spelling keeps its meaning with no application change. Here the call site
// changes or nothing rings.
void bell();

// Whether the terminal is dark, light, or has not said. The same two
// records again, asked in the same order and for the same reason: which
// backend is driving this program.
//
// IT EXISTS BECAUSE QT'S OWN ANSWER IS PERMANENTLY UNKNOWN HERE.
// qApp->styleHints()->colorScheme() is the spelling an application already
// knows, and under qtty it never returns anything else: prepare_environment()
// pins QT_QPA_PLATFORMTHEME empty on purpose -- a desktop theme reaching
// into a terminal program supplied proportional fonts and 20 of the 71 key
// bindings -- and Qt's generic theme reports no scheme at all. Measured, and
// the pin is not going away, so the answer had to come from the terminal.
//
// The terminal was ALREADY ASKED. OSC 11 has been in the startup query since
// the graphics negotiation needed something to composite alpha against, and
// OSC 10 is beside it now for this. An application keeping the default
// CellTheme never needed either -- it renders in the terminal's own colours,
// whatever they are. The one that breaks is the application that picks its
// own dark or light palette, and it picks wrong on half of all terminals
// with nothing to tell it.
//
// HOW IT DECIDES, which is the workspace rule in harmonization.md and not
// this library's invention: compare the luminance of the background against
// the luminance of ITS OWN FOREGROUND, and a background darker than its
// foreground means dark. Not against a midpoint -- the pair the terminal
// reports is a pair somebody has to read, so a legible scheme keeps the two
// well apart, and that floor is what makes the comparison safe. A colour
// weighed against a constant has no floor between its operands, so a
// mid-grey terminal is decided by the constant rather than by the terminal.
// Qtty::Color::luminance() does the arithmetic, which is the one this
// library already uses for its own contrast rule.
//
// UNKNOWN IS AN ANSWER AND IS THE IMPORTANT ONE. No backend, a terminal that
// answered neither query, a terminal that answered only one, or two colours
// of equal luminance: all Unknown, and a caller that gets it keeps its own
// default. The two errors are not symmetric -- a wrong LIGHT leaves an
// application looking plain, a wrong DARK puts pale text on a pale ground
// and it cannot be read -- so there is no coin to toss here.
Qt::ColorScheme color_scheme();

// The font qtty lays its grid on, chosen before setup() installs it.
// DejaVu Sans Mono at 16 pixels otherwise.
//
// A terminal cell is one glyph, so the font decides the cell: the grid is
// sound only when one cell is a whole number of pixels and every glyph
// advances by exactly that, which setup() checks and refuses. Until this
// existed the family and the size were hardcoded, so an application that
// wanted another mono font could not have one, and a user whose machine
// could not carry the default had a fatal message naming a font they had no
// way to change. `QTTY_FONT` and `QTTY_FONT_SIZE` are the user's end of the
// same lever, and this beats them -- a program that names a font has
// usually measured something against it.
//
// The hinting is qtty's either way and is not offered here: it decides
// whether the metrics are integral at all rather than how the text looks,
// and setup() records the measurement.
void set_font(const QString &family, int pixel_size);

// The keys that quit, for an application that uses exec() and therefore
// never sees the router. Ctrl-C and Ctrl-D by default; an empty list means
// no quit key at all, and the application is then responsible for offering
// a way out of its own.
//
// It exists because there was NO way to change them. exec() builds the
// router on its own stack and hands it to nobody, so
// InputRouter::set_quit_keys() -- which has always existed -- was reachable
// only by reimplementing the whole of exec(). project.md 0e carried that as
// the last of the adoption decisions, and its shape is taken from the
// sibling that was closed the same way: capabilities() and shell_out() are
// free functions that reach what exec() owns.
//
// PROCESS-WIDE AND BEFORE THE RUN, which is the difference from those two.
// An application decides its quit keys while building its window, long
// before any router exists, so this sets the default every router starts
// from rather than poking a live one -- and it also reaches the routers
// already running, so a call from inside a slot takes effect at once.
// InputRouter::set_quit_keys() still overrides it for one router.
//
// THE HAZARD THAT MADE THIS DELICATE IS GONE. While a vanished terminal was
// reported by synthesising Ctrl-D, an application able to redefine the quit
// keys could also stop being told its terminal had closed; 8.148 gave that
// its own seam, on_terminal_lost(), which nothing routes and no quit key
// can take away.
void set_quit_keys(const QVector<KeyEvent> &keys);

// True while a terminal session is being driven -- by exec(), or by an
// application's own frame loop, which is the seat 8.153 found answering
// false beside a backend holding the alternate screen. Overlay uses this to
// pick its rendering path (section 5.7); apps can branch on it for
// target-specific polish.
bool is_tui_active();

// Render one frame of `win` into `buf` (and collect section 5.7 placements when
// `placements` is non-null). Used by tests, tools, and custom frame loops.
void render_once(QWidget &win, CellBuffer &buf,
                QVector<CellImage> *placements = nullptr);

} // namespace Qtty
