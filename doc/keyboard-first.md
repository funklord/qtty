# Keyboard-first: writing a Qt application that a terminal user can drive

qtty renders an unmodified Qt Widgets application on a character-cell
terminal. Rendering is the easy half. The half that decides whether the
result is *usable* is the keyboard, because a terminal user often has no
mouse at all -- over ssh, in a console, on a server -- and a control that
can only be clicked is a control that does not exist.

This is the guide for that. Where a behaviour is Qt's own it says so, and
where it is qtty's it says which.

**If you came here with a job**, rather than to read it through:

- *writing a widget of your own* -- **If you are writing a custom widget**
  has the six things a standard one gets and yours does not; practices 9
  to 11 explain the first three, the fourth is in *Copy and paste*, and
  the fifth is the grid's own limit rather than a price;
- *your control does not look like a control* -- practice 12, on style
  sheets;
- *your form is bigger than the terminal* -- **When the terminal is
  smaller than the form**;
- *deciding what to bind* -- **What already works, unmodified** first, so
  you do not bind a key that already answers;
- *your debug output is landing on the screen* -- **Where your output
  goes**;
- *a user says your program hangs and `Ctrl+C` does nothing* -- **Never
  block the event loop**, which on a terminal is a lockout rather than a
  slow window;
- *a user cannot select text with the mouse any more* -- **Copy and
  paste**, which names the modifier that still works;
- *you want a test rather than an opinion* -- **Checking it without a
  terminal**, which lists the questions this library will answer about
  a window, and what to assert about each.

That is organised by task rather than mirroring every heading below --
though it does name some of them, so it is a smaller copy rather than
none, and worth checking if you rename a section.

**Every row below is held by at least one check in the suite**, so a
behaviour that changes reddens something rather than quietly making this
file wrong. **When you add a row here, add the check that holds it** --
that is the trigger, and it is the whole maintenance rule for this page.
The mapping was enumerated on 2026-09-09 and found one row with nothing
behind it, `Esc`; it has two checks now.
That is worth saying plainly because the sentence here before it claimed
that everything had been measured, and several rows had not been. One of
them said `Enter` fires a dialog's **default** button wherever focus is,
which Qt does not do; another said a terminal has no pointer to rest,
which contradicted a measurement already recorded in `project.md`. (This
sentence said "three" until a fourth turned up an hour later, which is
the argument for not counting in prose at all.) **A
sentence claiming rigour is what stops anybody checking**, so this one
says what to do instead: if a row here disagrees with what you observe,
the row is the first suspect, and `project.md` holds the measurement.

## The model a terminal user already has

A terminal interface is a stack of layers rather than a plane of windows,
and people navigate it with three ideas:

- **Enter goes IN.** It opens the thing that has focus, accepts the
  dialog, commits the field.
- **Escape comes BACK.** It closes the menu, cancels the dialog, leaves
  the layer. Pressed enough times it reaches the top.
- **Tab and the arrows MOVE.** Within a layer, between the things that
  can be typed into.

An application that answers those three consistently feels native on a
terminal whatever else it does. One that requires a pointer for any step
does not, however good it looks.

**Those are what a user expects, not a list of what you get.** Some of it
is already true -- Qt closes a menu on `Esc` and fires a dialog's default
button on `Enter` -- some arrives with the opt-in conventions, and some
is yours to arrange: a layer of your own answers `Esc` only because you
made it. The tables below say which is which, and the practices say what
is left to you.

## What already works, unmodified

None of this needs a change to an application. It is here so that nobody
reimplements it:

| Key | What happens | Whose |
|---|---|---|
| `Tab`, `Shift+Tab` | Move focus forward and back through the layer | Qt's |
| `Space` | Activates the focused button, toggles the focused check box | Qt's |
| Arrows | Work inside a control that wants them -- a slider moves, a list selects, a spin box steps | Qt's |
| `Esc` | Closes an open menu; rejects a modal dialog | Qt's |
| `Enter` | Fires the focused button if focus is on one; otherwise the dialog's **default** button | Qt's |
| `Alt` + letter | Reaches a menu, a toolbar action, a **button**, or the field a **label** is the buddy of | qtty's |
| `Alt` + a letter that matches nothing | Nothing. It does not type the letter into whatever has focus | qtty's |
| A `QAction` shortcut, or a `QShortcut` | Fires, and its **context** is honoured either way: a `WidgetShortcut` needs its own widget focused, an `ApplicationShortcut` fires from any window. Neither fires from behind an open menu | qtty's |
| `Menu`, `Shift+F10` | Opens the focused widget's context menu, honouring its `contextMenuPolicy` | qtty's |
| `Home`, `End` | Jump to the first or last item of a focused list or tree | Qt's |
| `PageUp`, `PageDown` | Move a screenful within it | Qt's |
| A letter, typed into a focused list or tree | Jumps to the next item beginning with it -- type-ahead, which a terminal user reaches for and which nothing here has to implement | Qt's |
| `Ctrl+C`, `Ctrl+D` | Quit -- except where a **caret** is, in a widget that takes text, where `Ctrl+C` is left for copy. A list, tree or table quits like anything else, and its open editor does not. Change them with `Qtty::set_quit_keys()`, before your run or during it | qtty's |
| `Ctrl+Z` | An ordinary key, **not** a suspend -- see *Never block the event loop* for why, and how to get the conventional behaviour back | qtty's |
| `F2` | Opens the editor on the current cell or item of an editable view, and `Enter` commits, `Escape` cancels, `Tab` moves to the next cell's editor | Qt's |
| `Left`, `Right` in a focused tree | Close and open the current branch, and the `▸` or `▾` mark turns over with it -- which is the whole of how a user with no pointer sees that the key did anything | Qt's |

**Editing in a table or a tree works, and one thing about it is Qt's
behaviour rather than this library's.** Typing a printable character
straight into a `QTableView` starts an edit and the character lands in
the editor; typing into a `QTreeView` does **not** -- the letter goes to
type-ahead search, which is the row above. Measured against plain Qt on
a desktop, where it behaves the same way, so an application wanting
type-to-edit in a tree has to ask for it there too.

The editor is a real widget while it is open: it takes the keys,
`Qtty::keyboard_reachable()` grows by one and names it, and the cell
underneath it is erased, so what the user types is what the user reads.

**And it opens in the cells the item's own text was drawn in.** Qt places an
inline editor at `SE_ItemViewItemText` and this library answers it in whole
cells, so the editor lands *after* a tree's branch mark and after a check
box rather than on top of them, and the name does not jump a column sideways
as the rename begins and back as it ends. Nothing to call: it is the style
answering, and it is there whether or not you install
`Qtty::CellItemDelegate`.
**Focus comes back to the view when the edit ends**, which is qtty's
doing rather than Qt's -- Qt returns it only for an active window, and
none activates here, so without that a keystroke after every commit and
every cancel would be spent putting the focus back.

**`grabKeyboard()` works, and it did not until it was measured.** If your
program takes the keyboard for a widget -- a custom overlay, a
key-capture field -- every key goes there until `releaseKeyboard()`,
exactly as on a desktop. The platform prints *"This plugin does not
support grabbing the keyboard"* on stderr and refuses the real grab, which
is what made this look like something no library could fix; Qt records the
grabber anyway, so qtty asks it. **An open menu still wins over a grab**,
which is Qt's own order and matters: get it the other way round and a grab
taken for something else makes every menu in the program unusable.

**Your shortcuts are matched by the router, not by Qt.** Qt's shortcut
map gates on the window being *active* and none activates here, so
neither a `QAction`'s shortcut nor a `QShortcut` would ever fire if qtty
did not resolve them itself. It does, and **all four contexts mean what
they mean on the desktop, for a `QAction` and a `QShortcut` alike**:

| `context()` / `shortcutContext()` | fires |
|---|---|
| `Qt::WidgetShortcut` | only while its own widget has focus |
| `Qt::WidgetWithChildrenShortcut` | while that widget or a descendant has it |
| `Qt::WindowShortcut` (the default) | anywhere in the window that owns it |
| `Qt::ApplicationShortcut` | from any window, including ones that do not own it |

Getting the narrow ones wrong in the other direction would give the
terminal a binding the desktop does not answer, which is worse than
missing one: it changes what your program does rather than what it
offers. A shortcut on a **menu's** action fires with the menu closed, as
it does on the desktop -- worth stating because a `QMenu` is a top-level
window of its own here, and "is this action in the current window" is not
the obvious question it looks like.

**With more than one window, the context is what decides where a key
lands.** A `WindowShortcut` in the window you are not in stays quiet --
that is the point of it -- so if you want a binding to work everywhere,
say `Qt::ApplicationShortcut` and it will.

The `Alt` rows are qtty's because a terminal delivers keys as bytes and
nothing here ever reaches Qt's shortcut map -- it gates on the window
being *active*, and no window activates under this platform. The router
matches mnemonics itself, and for the same reason it matches your
`QAction` shortcuts and your `QShortcut` objects: if it did not, neither
would fire at all. Menus and actions worked from the start; buttons and label buddies
were added later, and an application written for the desktop gets them
without knowing.

The second row is there because it was once false. A terminal sends
`Alt+Z` as `ESC` then `z`, so the letter arrives in the event's text and a
text field **typed it** -- pressing `Alt+F` for a File menu that did not
exist left an `f` in whatever you were editing. The text is now withheld
from widgets that take typing, and only from those: an open menu still
matches its items by the letter, because with no shortcut map that is how
it finds them.

**A table is not a list, and the difference is Qt's rather than qtty's.**
In a `QTableWidget`, `Home` and `End` move within the ROW -- to the first
and last column -- and type-ahead does not jump. Measured against plain
Qt with no qtty in the process, which does exactly the same, so it is
worth knowing before you file it: `Ctrl+Home` and `Ctrl+End` are the
first and last cell.

**Focus a widget the way you would in any Qt program: `w->setFocus()`.**
`Qtty::focusWidget()` reads qtty's own record of who has focus, which is
process-wide where Qt's is per-window, and it is refreshed FROM Qt's --
on the next key and on the next frame. So `setFocus()` is what an
application calls and the record follows it. `Qtty::set_focus_widget()`
is the other side of that pair: it writes the record, which the next
refresh overwrites, and it exists for the runtime rather than for you.

## What the marks mean

**The whole visual vocabulary, in one place.** A terminal has no bevels
and no icons, so a control says what it is with a few characters; these
are them. Read it as a user reading a screen, and as an author of a
custom widget deciding what to draw -- **use these rather than inventing
a third spelling**, which is the other half of practice 10.

| the cells | what it is |
|---|---|
| `<Save>` | a push button |
| `<Save▾>` | one with a menu on it |
| `[ ] Wrap` | a check box, clear |
| `[x] Wrap` | a check box, ticked |
| `[-] Wrap` | a check box, partially ticked |
| `( ) One` | a radio button, unchosen |
| `(o) One` | a radio button, chosen |
| `[text          ]` | a line edit |
| `[●●●●●●        ]` | a password field |
| `[query        ✕]` | a field with a clear button, which shows only while there is something to clear |
| `[One          ▾]` | a combo box |
| `[3           ▴▾]` | a spin box, the two arrows being two cells so both can be clicked |
| `[Cut          ▾]` | a tool button with a menu |
| `───────●────────` | a slider |
| `◀░░█░░░░░░░░░░░▶` | a scroll bar |
| `████████40%░░░░` | a progress bar |
| `[One      ][Two      ]` | tabs |
| `[One     ✕][Two     ✕]` | tabs you can close |
| `✓ Word wrap` | a ticked menu item |
| `• By name` | the chosen one of a menu's exclusive group |
| `Recent     ▸` | a submenu |
| `▸ Folder` | a tree row that opens |
| `▾ Folder` | a tree row that is open |
| a box with a class name in it | a widget whose content is out of reach -- a `QGraphicsView`, a web view |
| `▒` | a picture reduced to one cell, which is all this library can say about it |

**Two of these are near neighbours on purpose and one is a trap worth
knowing.** `✕` is close-or-clear and appears on a tab, a dock title and
a clear button; `(o)` is a chosen radio. A mark of your own spelled
`(X)` therefore reads as a radio button, and `[!]` as a check box --
which is why the brackets are not free to reuse.

**Every row above is rendered by a check** rather than copied from the
source, so a mark that changes in the code makes this page go red. The
check also counts the rows in this table and compares them with its own
list, so a row added here without a check -- or a check without a row --
fails too.

## Moving between pages and windows

A terminal interface is layers, so getting BETWEEN them is half of using
it. Measured:

| Key | What happens | Whose |
|---|---|---|
| `Ctrl+Tab`, `Ctrl+Shift+Tab` | Move between a `QTabWidget`'s tabs, from anywhere inside a page | Qt's |
| Arrows on a focused tab bar | Move between tabs | Qt's |
| `Ctrl+PageDown`, `Ctrl+PageUp` | Move between tabs, and wrap at the ends | qtty's, opt-in |
| `F6`, `Shift+F6` | Move between top-level windows | qtty's, opt-in |
| `F10` | Opens the window's menu bar, the only key that reaches one whose titles have no mnemonic | qtty's, opt-in |

The last two are part of the opt-in below. `Ctrl+PageUp`/`PageDown` is
what somebody coming from a browser, an editor or a multiplexer reaches
for, and Qt does not give it to a tab widget. `F6` matters more: multiple
top-level windows are reachable through `Qtty::next_window()` and
`Qtty::previous_window()`, and **until the conventions are asked for
nothing binds a key to them at all** -- a second window could be on the
screen and unreachable without a mouse.

**A completer's list is a layer, not a window**, and it works: type into a
`QLineEdit` with a `QCompleter` and the candidates appear, `Up`/`Down`
walk them, `Enter` takes one, `Escape` dismisses the list and leaves the
field. Qt marks such a popup by pointing its focus proxy at the field
being edited, which is how this library knows to draw it without handing
it the keyboard -- the text keeps going to your field and only the
navigation keys reach the list. Getting that split wrong is visible
immediately: give the list everything and Qt's own filter hides it after
every letter, give the field everything and the list cannot be walked.

**Not every top-level is a window in that sense**, and which ones are was
measured over every kind Qt has:

| your window's type | in the `F6` strip | takes the keys | Escape gives them back |
|---|---|---|---|
| `Window`, `Dialog`, `Sheet`, `Drawer`, `SubWindow` | yes | no | -- |
| `Tool` (a palette), `SplashScreen` | yes | no | -- |
| `Popup` (a menu) | no | **yes**, while it is up | yes |
| `ToolTip` | no | no | -- |

**What that table costs you, in the one case where it bites.** A strip
window that is not the current one is *not drawn* -- the strip is a row
of names and one view at a time -- so anything you put in a second
window is invisible until the user presses `F6`. Two ordinary Qt idioms
land on that:

- **A non-modal `QProgressDialog` shows a tab label and no bar.**
  Measured: with one up, the frame carries `[Your Window] QProgressDialog`
  in the strip and the main window underneath, and the words *Copying
  files* appear nowhere. A user watching a long operation sees nothing
  happening. **Make it modal** -- `setWindowModality(Qt::ApplicationModal)`
  -- and the same dialog draws over the window, bar and all, which is
  also what you want on a desktop while an operation blocks.
- **A `QSplashScreen` is a strip window too**, so its artwork and
  `showMessage()` are never seen and the strip appears and disappears
  around startup. If the splash is telling the user something, tell them
  in the window instead.

So a palette you open with `Qt::Tool` is a window like any other here:
`F6` reaches it and the window you were in keeps its keys. A menu owns
the keyboard while it is open, which is what a menu is for, and `Escape`
closes it. A tooltip window is drawn and takes nothing. **Both of the
first two were once wrong** -- a palette and a tooltip each deafened the
application with no way back -- which is why the table is in the guide
rather than in somebody's head.

**A tab's mnemonic works only with the conventions on.** `Alt+S` on a tab
labelled `&Second` switches to it once `set_keyboard_conventions(true)`
has been called, and does nothing otherwise -- whether a terminal should
switch tabs that way *by default* is still an open question in
`project.md` §0b. **The tab is not marked either way**, and that is
deliberate: a selected tab is already underlined to show the tab bar has
focus, so underlining one letter of it would say two things at once. Put
the key in your own help text if your users need to find it.

## When the terminal is smaller than the form

Two things happen, in this order, and neither needs anything from you at
the time.

**First, what you marked optional is dropped.** `Qtty::set_priority(w,
Priority::Optional)` says a widget may go when there is not enough room.
The compositor hides them one at a time, stopping the moment the layer
fits, and **never hides the widget that owns focus**. It is carried as a
dynamic property, so it is a no-op in the GUI build and can be set from a
`.ui` file without linking qtty at all.

**Then what is still too big is scrolled, not clipped**, and the scroll
follows the focused widget: move focus with `Tab` and the view comes with
it. A layer that already fits scrolls by nothing, so on a roomy terminal
none of this is visible. The reasoning, from the compositor:

> Follow the focus rather than binding a key: arrow keys belong to the
> focused widget and a chord would have to be learned, but `Tab` already
> walks the form -- so keeping the focused widget inside the terminal
> makes every widget reachable with the keys the application already
> answers.

Two refinements are worth knowing, because they decide what you actually
see. **A text widget is followed by its caret**, not by its rectangle, so
a field wider than the terminal keeps the place you are typing in view
rather than showing you its far end. And **anything wider than the view
shows its left edge**, anything taller its top: a menu thirty cells wide
in a twenty-cell terminal draws its item names rather than the shortcut
column, which is what it did before the rule and is no use to anybody.

So **practice 4's rule survives a small screen**: if every control is on
the tab chain, every control can be got to at 80x24, or at 40x10. It
works per layer -- the window, a modal and a popup each scroll on their
own -- and a menu follows its *current item*, having no focus widget to
follow.

Two keys move the view without moving focus. An arrow the focused widget
**ignores** falls through to the scroll area that widget is inside, and
`PageUp`/`PageDown` page the same area -- asking its scroll *mode*, so a
list that scrolls per item moves by visible rows rather than by a
pixel count that would mean nothing to it.

**With two scroll areas it is the one you are in**, measured on a split
window with the focus inside the right-hand pane: the right one moves
and the left does not. That is Qt propagating an unhandled key up the
parent chain rather than anything this library arranges; qtty's own
fallback runs only when the focused widget is inside no scroll area at
all, and then takes the first one in the window. Worth knowing if you
ever wonder which pane a key will move.

**A scroll area needs nothing focusable inside it to be reachable.** A
page of text -- a licence, a log, a help screen -- is a tab stop in its
own right, so `Tab` lands on it and `PageDown` reads it. That is Qt's
focus policy for `QAbstractScrollArea` rather than a convention here,
which is why it holds with the conventions off.

**One transient worth knowing**: a resize does not re-scroll a list to
its current item, because Qt does not, so after a sharp shrink the item
the keys would act on can be off-view until the next keystroke brings it
back. The item is not lost -- measured, it is exactly where it was -- and
if that matters in your application, `scrollTo(currentIndex())` on a
resize event settles it.

What none of this does is reflow your layout. A form that needs eighty
columns still needs them; it is reached by scrolling rather than by
becoming narrower. Marking the parts that can go is the lever you have,
and it is worth setting before somebody meets the form on a phone-sized
terminal rather than after.

**If you would rather reflow it yourself, ask how big the terminal is.**
`Qtty::terminal_cells()` answers in cells while a program is running, and
an empty `QSize` when none is -- so a narrow terminal can collapse a
sidebar, choose a one-column form, or shorten a table, the way the same
application would against a small window on a desktop. It asks the live
backend each time rather than handing back something measured at startup,
because a user resizes a terminal whenever they like.

**What it is not is the room you have to draw in**, and the difference
catches people. The window bar takes a row off the top when it is shown,
and a layout that refuses to shrink leaves the window LARGER than the
terminal -- so neither this nor `win.height() / GridMetrics::ch()` tells
you how many rows your widget got. For that, ask the widget. What this
answers is how big the screen is, which is the question behind *is this
terminal narrow enough that the sidebar should go*.

**And whether the user is still looking at it.**
`Qtty::terminal_focused()` answers false while the terminal window or tab
qtty is drawing into does not have the keyboard focus, and true the rest
of the time -- including on a terminal that never reports focus at all,
where saying nothing has to mean "focused" rather than dim every session
for ever.

You will usually not need to ask, because the library already acts on
it: the control that owns the focus stops being drawn as owning it while
the terminal does not have it, exactly as a widget in a deactivated
desktop window loses its focus rectangle. Selections stay, which is also
what a deactivated window does. Ask when your application has something
of its own to stop -- an animation, a poll, a cursor of your own making
-- the way you would use `QEvent::WindowDeactivate` on a desktop. That
event is not available here and cannot be: no qtty window ever
activates, so `isActiveWindow()` is permanently false and the event
would have nothing behind it.

## Right to left

**Set the direction and the form mirrors. There is nothing else to do.**

    QApplication::setLayoutDirection(Qt::RightToLeft);

Qt's layouts mirror themselves, and qtty follows for the handful of
controls it positions rather than lays out: a progress bar fills from the
right, a scroll bar's thumb and a spin box's step arrows change side, and
a tool button's menu arrow moves to the left of its label. A `QLabel` is
**not** mirrored -- that is Qt's own behaviour, measured against plain Qt
with Fusion, and not a gap here.

**Everything the keyboard does is unchanged**, which is the part worth
saying because it is the part an implementer would otherwise go and
check. Measured on a mirrored two-field form: `Alt` and a letter reaches
the field it names, `Tab` visits the fields in reading order, and of the
questions under *Checking it without a terminal*, the eight that should
be empty are -- while `Qtty::keyboard_reachable()` names both fields, in
reading order, which is the one answer mirroring could plausibly have
disturbed and did not.

This sentence used to say that **all nine** answered empty, which cannot
be true of a form with fields in it: `keyboard_reachable()` returns what
`Tab` visits, so an empty answer there would deny the clause beside it.
The suite carries a check worded almost the same way and it is about a
null scope and an empty window, where all nine are correctly empty. One
sentence, two fixtures. There is a check on the mirrored form now.

### Bidirectional text is not handled

**A string mixing scripts does not survive.** Measured, a `QLabel`
holding four Hebrew letters, a space and `abc` -- eight code points:

    asked for   U+05E9 U+05DC U+05D5 U+05DD U+0020 U+0061 U+0062 U+0063
    reached     U+0061 U+0062 U+0063 U+0020 U+05E9 U+05DC U+05D5
    the cells   abc שלו

Seven, not eight: the runs are reordered and the last letter is gone.

This is not the layout direction and setting it changes nothing here. A
terminal decides for itself whether it reorders what it is sent, and
qtty does not yet take a position on which order to send -- so an
application whose *text* is bidirectional is not one this library can
carry today. An application whose text is left-to-right in a mirrored
layout is fine, and that is the case the section above is about.

The measurement is pinned by a check, so that whoever changes it finds
this page rather than leaving it saying something untrue.

## The terminal's own keys, and how to ask for them

One line turns on the habits a terminal user has and Qt does not:

    Qtty::set_keyboard_conventions(true);

| Key | What it does |
|---|---|
| `Enter` | Activates the control that has focus |
| `Up`, `Down` | Move between controls |
| `Ctrl+PageUp`, `Ctrl+PageDown` | Move between tabs, wrapping at the ends |
| `F6`, `Shift+F6` | Move between top-level windows |
| `F10` | Open the window's menu bar |
| `Alt` + a tab's letter | Switch to that tab |
| `Ctrl+A`, `Ctrl+E` | Start and end of the line, **in a widget that takes text** |
| `Ctrl+K`, `Ctrl+U` | Kill to the end of the line, and to the start |
| `Ctrl+W` | Rub out the word before the caret |
| `Ctrl+D` | Delete the character under the caret -- a quit key everywhere else |

**These are the chords as written, `Shift` included.** Adding a shift to
one does not reach the binding: `Ctrl+Shift+K` is not the kill, and a
shifted control chord on a terminal usually belongs to the terminal
emulator rather than to the program inside it.

**And whether a shifted control chord reaches you at all depends on the
terminal.** A control byte is one of thirty-two values and carries no
shift bit, so on a legacy terminal `Ctrl+Shift+C` and `Ctrl+C` are the
same three bits on the wire -- an application binding the first is
binding a chord nothing can send. Terminals that speak the **kitty
keyboard protocol** can say it, and qtty asks: it sends `CSI ? u` with
its other startup probes, pushes the disambiguating flag when the
terminal answers, and pops it on the way out. Such a key then arrives as
`CSI <code>;<modifiers>u` with the shift in the modifiers.

Ask your terminal before you rely on it:

    qtty-negotiate --probes | grep keyboard

`Qtty::Capabilities::keyboard_protocol` is the same answer in code. Where
it is false, treat a shifted control chord as unavailable rather than as
unbound: nothing an application does will make it arrive, and a binding
nobody can reach is worse than no binding, because the menu entry beside
it says the key exists.

**What you can do is find out which of yours are affected**, before a
user does. `Qtty::ambiguous_chords()` names them -- the shifted chords
here and the five below -- from the window rather than from the
terminal, so it answers in a headless test where there is no terminal
to ask.

The same flag is what makes a lone `Escape` immediate rather than a
chord waiting on a timer, which is what "disambiguate escape codes"
means.

**And five chords are not unsendable but AMBIGUOUS, which is the worse
half.** A shifted control chord is silently *unbound* -- you press it
and nothing happens, which at least looks like a bug. These five are
sendable and mean something else, because the control byte an ASCII
keyboard produces for them is a key in its own right:

| Your binding | The byte | What arrives without the protocol |
|---|---|---|
| `Ctrl+I` | `0x09` | `Tab` -- the focus moves |
| `Ctrl+M` | `0x0d` | `Return` -- a dialog's default fires |
| `Ctrl+[` | `0x1b` | `Escape` -- a dialog rejects |
| `Ctrl+H` | `0x08` | `Backspace` -- a character is deleted |
| `Ctrl+J` | `0x0a` | a line feed |

Measured through the decoder: `Ctrl+I` arrives as `Key_Tab` **with no
ctrl on it at all**, so nothing downstream can tell it from a real
`Tab`, and `Qtty::shortcut_conflicts()` cannot see the collision either
-- to Qt those are two different key sequences and they are, right up
until the wire. With the protocol the same keypress arrives as `Key_I`
with ctrl.

**You can assert that you have none**, which is the tenth question under
*Checking it without a terminal*:

    QVERIFY(Qtty::ambiguous_chords(&window).isEmpty());

It names both halves -- the five above, and any shifted control chord --
with the action's own text beside each, and it leaves an ordinary
`Ctrl+O` and a function key alone. It does **not** ask your terminal:
the report is for a headless test, where there is none to ask and the
answer would be about the machine the test ran on rather than about
your program.

Every one of the five is a chord an application might reasonably pick:
`Ctrl+I` for italic, `Ctrl+M` for a mark, `Ctrl+H` for help. **Prefer
another letter.** Where you cannot -- because your users know that
chord from the desktop build -- bind the other key as well and say in
your help which terminals answer it, since `Ctrl+I` doing nothing is a
fault a user reports and `Ctrl+I` moving the focus is one they blame
themselves for.

**`F10` is the way into a menu bar whose titles carry no mnemonic**, and
without it there is none: a `QMenuBar` is `Qt::NoFocus`, is in no tab
chain, and Qt reaches it by `Alt` -- which needs a `&` in the title. So a
program whose menus read *File*, *Edit*, *View* had no keyboard route to
any of them at all. Measured before it was added: `F10` did nothing and
the menus were reachable only by a pointer.

It **opens** the first menu rather than merely highlighting the bar,
which is where this parts company with a desktop deliberately -- a
highlight is a state a terminal shows poorly, and once a menu is open
everything already works: the popup owns the keys, `Up`/`Down` walk it,
`Left`/`Right` move along the bar to the next menu, `Enter` triggers and
`Escape` closes. Focus goes back to the widget you were in, which Qt
would normally restore itself and cannot here.

`Shift+F10` is unaffected and still asks the focused widget for its
context menu -- the two differ by one modifier, and a check holds them
apart.

One thing in the bundle is not a key. **A status tip follows focus**, which
is what Qt does when the mouse rests on a control and what a terminal user
can never ask for -- there is no pointer to rest anywhere, so every
`setStatusTip()` an application has written explains a control to nobody.
With the conventions on, moving focus sends the tip exactly as a hover
would, and a `QMainWindow` puts it in its status bar with no code at all.
The parent chain is searched, so a tip on a group box explains the fields
inside it. A message the application wrote itself survives tabbing past
controls that explain nothing: qtty takes back only tips it put up.

    hostEdit->setStatusTip("the host to connect to");   // already yours

**Your menu items' tips already work**, with or without the conventions,
because that half is Qt's own: a `QMenu` sends the highlighted action's
status tip to whoever opened it, and qtty opens menus through the bar that
owns them precisely so that chain survives. Arrow through a menu and the
status bar follows.

The first two rows are the ones that **differ** from Qt rather than merely
adding to it, and they are why the whole set is opt-in. On a desktop a
focused button answers to `Space` and not to `Enter` -- except inside a
dialog, where `autoDefault` makes a focused button take `Enter` for
itself and the designated default fires only when focus is elsewhere; and
arrows move *within* a control, never between. Both are reasonable when a
mouse is always there and neither is when it is not.

**Every one of them fires only where the focused widget ignored the key.**
A text field keeps its own `Enter`, a list keeps its own arrows, an
application that already uses `F6` keeps `F6`. So turning them on cannot
take a key away from a control that wanted it.

**"Ignored the key" is per keystroke, not per widget, and that is the
part a user feels.** A list keeps its arrows while it has somewhere to
go; at its first item `Up` is one it does not want, so focus leaves for
the control above. Measured: three `Down`s put the current item two rows
in and `Up` moves it back within the list, while `Up` at the top returns
focus to the field. So the arrows walk *inside* a control until it runs
out and then *between* controls -- which is how a person expects to get
out of a list without reaching for `Tab`.

It is off by default because this library's promise is that an unmodified
application renders faithfully, and one that has bound `Enter` or `Down`
itself must keep them. If you are writing for a terminal, turn it on: it
is one line, and it is what makes a form walkable.

**And ask it what it bound** rather than writing the table above into your
own status bar -- see practice 8.

## Practices

**Most are advice**, roughly in the order they pay off: they turn an
application a terminal user can operate into one they can operate
comfortably. **Practices 9 to 12 are traps rather than advice** -- each
breaks something silently, and breaks it only on the terminal, so the
desktop build hides every one. Nine to eleven concern a widget of your
own; twelve concerns styling a standard one. They sit near the end because
that is where the material belongs, not because they matter least --
thirteen and fourteen, which are ordinary advice again, come after them. If you are
writing a custom widget, read 9 to 11 first: they are collected, with a
fourth from *Copy and paste*, in the table under *If you are writing a
custom widget*.

**1. Give every control a mnemonic, and every field a labelled buddy.**

    auto *apply = new QPushButton("&Apply");
    auto *label = new QLabel("&Host");
    label->setBuddy(hostEdit);

This is the single highest-value thing in this document. A mnemonic turns
"tab past four fields" into one keystroke, and it is the only way to reach
a control *directly*. It costs one character.

**The buddy is worth a row when the mnemonic is**, and not otherwise. A
label exists here to carry a letter you can jump to; on a terminal it also
costs a whole row, which is a real price on twenty-four of them. So a form
with several fields wants labels and mnemonics, and a single input that
`Tab` reaches immediately is better served by a placeholder naming it --
which is what `example/chat` does, and why it does not contradict this
page. **Count the keystrokes you are saving before you spend the row.**

**And the failure this practice creates is silent, so ask about it.** Two
controls claiming one letter is not drawn differently and is not logged:
the first one the router reaches answers, and the other never does. The
more of this page you follow, the likelier it is.

    for (const auto &clash : Qtty::mnemonic_conflicts(&window))
        qWarning() << clash.first << "is claimed by" << clash.second;

Empty is the answer to assert in the test this page asks you to write. The
list names every claimant, and **which one answers depends on where the
focus is**: menu actions come before buttons and buddy labels, so `&File` on
a menu beats `&Format` on a button wherever you are -- and between two
buttons, the one in the panel you are in wins. So a letter shared by two
panels is not the bug a letter shared by a menu and a button is;
a **tab** comes last of all, because its letter is answered only after the
key has been delivered and refused, and only with the conventions on. With
them off a tab's letter is not a claim at all, and the list says so.

**And the same question for your chords.** Qt reports an ambiguous
shortcut on a desktop; here it cannot, because `QShortcutMap` gates on the
window being active and no window activates. Two things claiming `Ctrl+S`
means the first one the router reaches answers and the other never does.

    for (const auto &clash : Qtty::shortcut_conflicts(&window))
        qWarning() << clash.first.toString() << "is answered by" << clash.second;

It covers `QAction` and `QShortcut` alike, including the
`ApplicationShortcut` ones in your other windows, since those answer here by
definition. The list names the **nearest** claimant first, which is the one
that answers: where several apply at once, the one owned by the widget the
focus is in -- or by the nearest thing around it -- wins. Qt's own MDI is
why: every subwindow carries the same `Ctrl+F4`, so without that rule the
chord closes whichever subwindow the search happened to reach first. **Context decides**, so this is not a list of sequences used
twice: two `WidgetShortcut` claims on different widgets are not a collision,
only one of them ever being in play, and a chord is reported where some
focus a user can reach makes two of them answer at once.

**2. Give every dialog a default button.**

    buttons->button(QDialogButtonBox::Ok)->setDefault(true);

`Enter` then commits from anywhere in the dialog **except another
button**, and it works with no opt-in.

**But Qt marks one whether or not you ask, and that is the part to
know.** Reading `QPushButton::isDefault()` right after `show()`, with
nothing of this library involved in the choice:

    hand-rolled buttons, no setDefault()   the FIRST one is default
    QDialogButtonBox                       Ok
    QMessageBox                            Ok
    QInputDialog                           Ok
    QProgressDialog                        Cancel, its only button

So a dialog never lacks a default. What it lacks is a default anybody
CHOSE, and the one it has came from the order you happened to construct
the buttons in.

**It then moves with the focus and moves back.** Measured on a dialog
holding a field and two buttons, `One` constructed before `Two`:

    focus in the field          One is default
    focus on Two                Two is default
    focus back in the field     One is default again

and with `Marked->setDefault(true)` on the second of them:

    focus in the field          Marked is default
    focus on First              First takes it, autoDefault winning
    focus back in the field     Marked is default again

So `setDefault(true)` is not overruled, only suspended: it holds
whenever the focus is not sitting on some other button, which is most
of the time in a form.

**The hazard is the first line of that first table.** A dialog whose
first constructed button is destructive, with the focus in a field --
which is where a form starts -- commits destruction on `Enter`, and
nothing asked anybody. qtty draws the default bold, so a user can see
WHICH button it is; what neither they nor you can see is that nobody
picked it.

Two ways to say it, and write one of them:

```cpp
deleteButton->setAutoDefault(false);   // keep Enter off this one
okButton->setDefault(true);            // and say which one it is for
```

The second is worth writing even when the order happens to be right
today, because it survives somebody adding a button above it.

That exception is Qt's `autoDefault`, which is on for a `QPushButton` in a
dialog: a button that has focus becomes the effective default and takes
`Enter` for itself. Measured, both with the opt-in conventions and
without. It is the behaviour you want -- `Enter` on a highlighted
*Cancel* should cancel -- but it means a default button is not a promise
that `Enter` always commits, and a form whose first `Tab` lands on a
button will commit somewhere else than you drew it.

**Qt's own message boxes set no default at all**, which is worth knowing
before you copy their shape. Measured with plain Qt and nothing of this
library in it: `QMessageBox` with `Ok`, with `Ok|Cancel`, with `Yes|No`
and with `Save|Discard|Cancel` all report `defaultButton()` as null,
before showing and after. What answers `Enter` there is the button that
happens to have focus, through the same `autoDefault`. So for a message
box the question is which button is focused, and for a dialog of your own
it is which one you marked -- and only the second is something you
control.

**3. Make the tab order the reading order.** Qt's default is construction
order, which is usually right and silently is not after a refactor.
`QWidget::setTabOrder()` fixes it. On a terminal this is the *only*
ordering a user experiences -- there is no glancing across to the field
they wanted.

**And this one you can check.** `Qtty::tab_order_anomalies()` returns the
consecutive tab stops that go backwards against the reading order -- the
pair where the second sits above the first, or level with it and to its
left:

    QVERIFY(Qtty::tab_order_anomalies(&window).isEmpty());

Going up *and to the right* is not reported, because that is a new column
and how a side-by-side layout is meant to be walked; Qt's own
`QFontDialog` does exactly that. Measured against five of Qt's dialogs, a
wizard, a tab widget, two panels and a form: all clean, while a form whose
second row was inserted after its third is reported as the pair it is.
That insert is the failure this practice is about, and nothing about
making it looks like it touched the keyboard.

**4. Never let an action be reachable only by pointer.** A right-click
menu, a hover reveal, a drag: each needs a keyboard route beside it. Put
the same action in a menu, give it a shortcut, or both. `QAction` in a
`QMenu` gets you a mnemonic and a shortcut at once.

**A narrow terminal loses toolbar BUTTONS and keeps the commands.** When a
`QToolBar` has more actions than fit, Qt hides the surplus behind an
extension chevron -- which only a pointer can open. Their actions are still
there, so **their mnemonics still answer**: measured, `Alt+Q` on a toolbar
too narrow to show *&Quit* triggers it anyway. That is a reason to give
every toolbar action a letter, rather than trusting the button to be there.
An action you hide yourself answers nothing, as it should: Qt reports an
invisible action as disabled.

**Qt ships several of these itself**, which is worth knowing before you
audit your own code for them. Two were measured first, and a third is
named below. The `x` on a closable tab and a dock widget's
close button have **no keyboard route at all** -- not in qtty, and not on
the desktop either. Measured with plain Qt and no qtty in it: `Ctrl+W`,
`Ctrl+F4` and `Delete` reach a closable `QTabWidget` and `tabCloseRequested`
never fires; `Ctrl+W` and `Esc` reach a closable `QDockWidget` and it stays
visible. On a desktop that is a mouse away. Here it may be nothing away at
all, so if your application uses either, give the same action a menu entry
or a shortcut of your own -- this practice applied to Qt's furniture rather
than to yours. The tab bar itself is fine: it takes `Tab` focus and the
arrows move between pages.

There is a third, and the check below found it where the audit above had
not: a dock widget's **float** button sits beside its close button, is
`Qt::NoFocus`, carries no action, and nothing claims a key for it either.
A fourth is not a button at all -- a `QSplitter`'s handle, which practice
13 measures: no focus, no tab stop, and no answer to the arrows even with
the focus forced onto it.

**A fifth is not a widget at all: the header of a table you have made
sortable.** `setSortingEnabled(true)` puts a sort on a click, and there
is no key anywhere that changes the sort column or reverses the order --
measured, 45 combinations in this library and in plain Qt moved neither,
and `QHeaderView` declares four mouse handlers and no `keyPressEvent`.
So a table whose sorting is the point of it offers a keyboard user one
fixed order. Give the sorts you care about a menu entry or a shortcut of
your own, calling `sortByColumn()`; that is the same remedy as the
toolbar chevron above, and it is the only one available.

**A line edit's clear button is deliberately not named either**, and for
a different reason worth knowing: the little `x` Qt adds when you call
`setClearButtonEnabled(true)` has no key of its own, but *what it does*
has one in both modes -- `Ctrl+U` empties the field with the conventions
on, `Ctrl+A` then `Delete` with them off. Practice 4 asks whether the
**action** is reachable, not whether the widget is, so a finding there
would be one you could not act on. An icon action **you** add to a field
with `QLineEdit::addAction()` is named, because that one does something
of your own that no key reaches -- a reveal toggle in a password field
is the case to have in mind.

**Resizing and reordering columns are pointer-only too, and are
deliberately NOT named** -- they change how the data looks rather than
which data you are shown, and a report that flagged every header in
every program is one nobody would read. If either matters in your
application, the remedy is the same: an action with a key on it.

**A sixth is the one a user is most likely to blame themselves for: a
link in a `QLabel`.** Qt gives every label `Qt::LinksAccessibleByMouse`,
so a link in rich text is clickable the moment you write it -- and the
label is `Qt::NoFocus`, in no tab chain, and reachable by no key. What
makes it worse than the header is that **nothing on the screen tells the
two apart**: a link a key can follow and a link it cannot are drawn
underlined and coloured in exactly the same cells. The remedy is one
line, and it is Qt's:

```cpp
label->setTextInteractionFlags(Qt::LinksAccessibleByMouse
                               | Qt::LinksAccessibleByKeyboard);
```

Qt then gives the label `Qt::StrongFocus` by itself, so it becomes a tab
stop and `Enter` follows the link. `Qtty::pointer_only()` names a label
that carries an anchor and lacks that flag, and stays quiet about one
that has it -- and about a label with no link in it, whose interaction
flags are identical.

**A seventh is a whole container: a `QToolBox`.** Its section headers are
`Qt::NoFocus` and in nobody's tab chain, so a section that is shut cannot
be opened by any key -- measured, `Tab` through a window holding one walks
the button before it, the page's scroll area, the field inside and the
button after, and never a header.

**The remedy is one character: put a mnemonic in the title.**

```cpp
box->addItem(page, "&Network");
```

Qt registers a shortcut for the ampersand on the header button, so
`Alt+N` opens that section. Measured on a box titled *&One*, *&Two* and
*T&hree*: each letter opened its own section and `Qtty::pointer_only()`
named nothing at all. qtty underlines the letter for you, which matters
here more than on a menu item -- it is the only key into a shut section,
so an unmarked one is a key nobody finds.

If you would rather `Tab` reached the headers as well, give them a focus
policy; with that in place `Space` opens the focused section:

```cpp
for (QAbstractButton *b : box->findChildren<QAbstractButton *>())
    b->setFocusPolicy(Qt::TabFocus);
```

Do that again after each `addItem()`, which builds a new header. The
mnemonic needs no such care, which is why it is the one to reach for.

**Two things Qt builds are deliberately NOT named, for the clear button's
reason.** A `QCalendarWidget`'s four navigation buttons are not, because
the keys reach what they do -- `PageDown` and `PageUp` step the month,
the arrows step the day, and a year is twelve `PageDown`s away, which is
a route rather than a good one. A table's corner button is not either:
it selects every cell, and measured on a 3x3 table both the click and
`Ctrl+A` left nine selected. Naming them would put four findings in every
calendar and one in every table, and none of them would be a finding
anybody could act on.

`Qtty::pointer_only()` names the seven, and the section on testing says
how to read that list.

A right-click menu is the exception you get for free: `Menu` and
`Shift+F10` open it, and your `contextMenuPolicy` is honoured exactly as
on a desktop. That was not true until the library was measured against
this very practice -- the mouse route had been supplied and the keyboard
one had not. A hover reveal and a drag still need a route you provide.

**If your application records shortcuts, drop the quit keys while it
does.** A `QKeySequenceEdit` is the field that asks the user to press the
chord they want, and under qtty the quit key is tested before anything is
dispatched -- so `Ctrl+C`, the default, closes the window while they are
pressing it. The field is drawn as focused and reads back nothing.

```cpp
Qtty::set_quit_keys({});                       // while the recorder has focus
...
Qtty::set_quit_keys({{Qt::Key_C, {}, true, false, false},
                     {Qt::Key_D, {}, true, false, false}});   // put them back
```

Measured both ways: with them dropped the field records `Ctrl+C`, with
them back the window closes. Two of qtty's other layers take chords from
such a field as well -- a menu mnemonic (`Alt+F`) opens the menu and a
readline convention (`Ctrl+U`) is the kill, both before the field sees
them -- so a recorder here cannot capture those either. That is the same
trade as practice 6's: a convention a terminal user expects is a chord
your widget does not get.

**5. Leave a way back from every layer.** Modal dialogs and menus answer
`Esc` already. A layer of your own -- a page in a `QStackedWidget`, an
inline editor, a mode -- does not, and a user who cannot get back is
stuck in a way a mouse user never is.

An `Esc` that nothing else consumed arrives at the focused widget as an
ordinary key event, with the conventions or without them, so the way back
is the usual one:

    void page::keyPressEvent(QKeyEvent *e) {
        if (e->key() == Qt::Key_Escape) { emit back(); return; }
        QWidget::keyPressEvent(e);
    }

qtty binds nothing to `Esc` itself, deliberately: "back" means something
different in every application, and a default that guessed would be wrong
somewhere and impossible to remove. What it does promise is that the key
reaches you, and a check asserts that in both convention states -- so a
convention added later cannot quietly take it.

**6. Reach every window.** qtty binds **no** shortcut of its own beyond
the quit keys, because any key it took by default would be one an
application could not use. (`Menu` and `Shift+F10` are not a counter-
example: they are the platform's own behaviour, restored, and a widget
that wants either key keeps it -- the context menu opens only when
nothing accepted the press.) So a second top-level window is unreachable
until either you bind `Qtty::next_window()` yourself or you turn on the
conventions, which put it on `F6`. Do one of the two: a window nobody can
get to is worse than one that was never opened.

**Closing the window you are in hands you its neighbour** on the strip,
not the first one -- the same thing a browser, an editor or a multiplexer
does, and the same thing your users will expect without being able to say
why.

**What you get for free, once there is more than one window**: qtty draws
a window strip along the top row, every window named, the current one in
brackets -- so a person can see which one they are in and click a tab to
change it, as well as pressing `F6`. This is the terminal's answer to a
task bar, and it is better than the window title for the purpose, since
it names all of them rather than only the one you are looking at.

**A subwindow is not a window here.** If your application uses `QMdiArea`,
its subwindows are children rather than top-levels, so the strip does not
name them and `F6` does not reach them. Each wears its name on its top
border instead, since a terminal has no row to spare for a title bar.

Measured, rather than assumed, in case you are counting on either: **`Ctrl+Tab`
does not switch subwindows here** -- Qt implements that in a filter the key
never reaches, the focused editor having taken it -- and **`Ctrl+F4` closes
one but not always the one you are in.** Every subwindow's system menu
carries the same `&Close` on `Ctrl+F4` and on `Ctrl+W`, so the chord is
ambiguous by construction: `Qtty::shortcut_conflicts()` reports it as
`Ctrl+F4 answered by &Close / &Close`, and the first claimant answers.

So consider whether you want MDI at all on a terminal: it is a window
manager inside a window, the strip is already one, and the keyboard route
Qt gives it is ambiguous before your application has done anything.

**It costs a row, and that is worth knowing before it surprises you.**
The strip takes the top row and everything below moves down by one, so a
layout that exactly filled the terminal loses its last row the moment a
second window opens. With one window there is no strip and no cost.

**The switch carries input, focus, the open menu and the terminal's title
with it, and none of that was free.** `Qtty::set_current_window()`,
`next_window()` and `previous_window()` move where keys go as well as
what is drawn; they give a window that has never had focus its first tab
stop; they dismiss a menu the window being left had open; and the
terminal takes its title from the window you arrive in, so a tabbed
terminal names the window you are actually looking at. Those are four
things a desktop gets from window *activation*, which never happens here
-- so each had to be done by hand, and each was missing at one point.
Nothing is asked of you except that you move windows with these
functions: a window made current by any other route leaves input in the
window the user can no longer see.

**7. Do not depend on hover or tooltips.** Information a user needs must
be visible or reachable by key.

**Write them anyway, and that is not a contradiction.** qtty *reads* a
tool tip: a `QToolButton` with an icon and no text is labelled from its
tool tip, or from its default action's, because a terminal cannot draw
the icon and a word is what the action already has. Measured before that
existed, two icon-only actions **occupied four cells between them and
drew nothing**. So do not depend on a tool tip *appearing* -- it never
will -- and do write one, or your toolbar is a row of blank cells.

**The same goes for a status tip, and it is read more literally.** With
the conventions on, `setStatusTip()` follows FOCUS rather than the pointer
-- see *The terminal's own keys* -- so the sentence you wrote for a mouse
that will never hover is shown to a user who tabs onto the control. A
`QMainWindow` puts it in its status bar with no code at all.

**And `setWhatsThis()` has a key: `Shift+F1`.** Qt's What's This mode
works here, which is worth saying because nothing suggests it would:
`Shift+F1` opens it, the **focused** widget's `whatsThis()` appears in a
window this library draws, and `Escape` -- or the next key you press --
takes it away. Measured, all four steps. So where a tool tip's words
cannot be reached at all, a `whatsThis()` on the same control can be, and
it is the right home for the sentence that does not fit on the screen:

```cpp
field->setToolTip(tr("host name"));          // read, never shown
field->setWhatsThis(tr("the host to connect to, with an "
                       "optional :port"));   // Shift+F1 shows this
```

**Only the focused widget's, though.** A `QLabel` is `Qt::NoFocus`, so
`Shift+F1` cannot be aimed at it and its `whatsThis()` is as far out of
reach as its tool tip. `Qtty::hover_only()` follows exactly that line: it
spares a focusable control that carries both, and still names one nothing
can focus.

The reason is not that a terminal has no pointer -- this guide said that
and it was wrong. qtty sends `QEvent::MouseMove`, so Qt sets `WA_Hover`,
delivers `Enter` and `HoverEnter`, and `underMouse()` answers true: a
custom widget consulting it in `paintEvent` **will** see the pointer.
What is absent is the two things anyone would build on. No
`QEvent::ToolTip` is raised, even after its timer, so whatever only a
tooltip says cannot be got at. And the cell style never reads
`State_MouseOver`, so every widget Qt ships draws the same hovered or
not.

Hover is therefore available and invisible -- the worst pair to depend
on, since it works in the widget you wrote and nowhere else. A keyboard
user never produces it at all. Whether either should change is an open
question in `project.md`, not a gap.

**So find the sentences that live only in a tool tip.**
`Qtty::hover_only()` returns the widgets that have one and no status tip:

    QVERIFY(Qtty::hover_only(&window).isEmpty());

The remedy is usually one line -- `setStatusTip()` with the same words,
which the conventions then show on focus. Icon-only buttons are not
named, since this library already draws their tip as their label; without
that exclusion every `QFileDialog` would report its six navigation
buttons. And where the widget cannot take focus at all, a status tip on
*it* will not show either: put the words in its label, or on the control
the user will actually be standing on.

**8. Say what the keys are.** A status bar line, a `?` page, a footer of
hints -- a terminal user cannot discover a binding by looking for a
button. This costs one `QLabel` and is the difference between an
application people can use and one they can use *if somebody tells them*.

Do not hand-write the ones qtty binds. Ask for them:

    QStringList hints;
    for (const auto &[key, what] : Qtty::keyboard_conventions_help())
        hints << key + " " + what;
    status->setText(hints.join("  \u00b7  "));

It lists **what qtty answers right now** -- the conventions when
they are on, and `Menu/Shift+F10` either way, since the context menu's
keys do not need the opt-in. So the same code is right in both states and
never promises a key that does nothing. Writing `"F6 window"` into your
own status bar instead keeps a second copy of a fact this library owns,
and the copy is wrong the day the binding moves -- which it did: the
context-menu row appeared after this guide was first written, and an
application asking for the list got it without an edit.

**And take back the rows you have taken.** That list has no window to
look at, so it promises what the library answers to -- while your own
`Ctrl+K` for Insert Link, or `F6` for a panel, has quietly removed that
convention from your window. Measured: the action fires and the line the
readline kill would have taken is whole. `Qtty::conventions_shadowed()`
names the rows your bindings have taken, spelled exactly as the help list
spells them:

    const auto taken = Qtty::conventions_shadowed(&window);
    for (const auto &[row, who] : taken)
        hints.removeAll(rowText(row));   // or strike it through

Empty is the usual answer and the one to assert in a test. What it
cannot see is the reverse -- a key you bound that the conventions do not
name -- because that one is yours and was never promised.

**And your own keys are the other half of that line.** The argument
against hand-writing this library's rows is that a second copy of a fact
is wrong the day the fact moves -- and it applies just as well to your
own `Ctrl+O`, which moves when somebody edits the menu.
`Qtty::shortcut_help()` returns them the same shape, so the two
concatenate:

```cpp
QStringList hints;
for (const auto &[key, what] : Qtty::keyboard_conventions_help())
    hints << key + " " + what;
for (const auto &[key, what] : Qtty::shortcut_help(&window))
    hints << key + " " + what;
```

It is a view over the same enumeration `Qtty::shortcut_conflicts()`
reports on, which matters more than it sounds: a help line and a
conflict report that walked your tree separately could describe
different programs, and the line is the one your user believes.

Four things it does that a walk of your own actions would have to
remember, and each is a row you would otherwise print wrongly:

| | |
|---|---|
| a **disabled** action or shortcut is left out | a key that does nothing is worse in a hints line than an absent one |
| one action owned by a **menu and a toolbar** is one row | the naive walk reaches it twice |
| one action carrying **two sequences** is two rows | both answer, so a user needs both |
| another window's `Qt::ApplicationShortcut` is **in** | it answers here, whatever window it was declared in |

**Ask `Qtty::ambiguous_chords()` as well, because this line will print
a key your terminal cannot send.** The two answer different questions:
this one lists what you BOUND, and that one says which of those a
terminal cannot deliver. Neither is wrong on its own -- your `Ctrl+I`
works wherever the keyboard protocol does, and dropping it from the
line would hide a key some terminals deliver -- but a program that
prints the line and never runs the audit shows a user *Ctrl+I Italic*
on a screen where pressing it moves the focus. Run the audit, fix the
binding, and then the line is right.

Mnemonics are deliberately not in it: they are drawn underlined on the
control itself, so a list of them is a second copy of what the screen
already says. And a `QShortcut` is labelled by its `objectName()` -- an
unnamed one still gets a row, because a working key missing from the
list is the worse failure, but the row says what it is rather than what
it does. Name it, or hang it on a `QAction`.

**Every row it lists is covered but one.** `Enter` and `Up`/`Down` are
in it too: a shortcut is matched before the focused widget is offered
anything, so a `QShortcut` on `Return` takes the `Enter` convention away
just as one on `Ctrl+K` takes the kill. The exception is `Alt` + a
letter, which is a family rather than a chord -- the letter is whatever
a tab or a label carries -- and `Qtty::mnemonic_conflicts()` answers it
per letter instead. A check in this library asserts that split, so a row
cannot appear in the list with nothing watching it.

**9. In a custom widget, ignore keys that carry `Alt`.** qtty withholds
the letter from widgets Qt marks as taking text, which covers every
standard input widget. A widget of your own that reads
`QKeyEvent::text()` directly is outside that, and will see the `z` of an
`Alt+Z` that was meant for a menu. One line:

    if (event->modifiers() & Qt::AltModifier) { event->ignore(); return; }

That line is right for a widget with no `Alt` bindings of its own, which
is most of them. **If yours has some, handle those first and ignore the
rest** -- the hazard is narrower than the line suggests. What must not
happen is inserting `event->text()` while `Alt` is held, and what must
happen is that an `Alt`+letter you do not want reaches the mnemonic
matcher instead of being swallowed. Your own `Alt+Left` is neither, and
taking the line literally would cost you it.

**10. In a custom widget, draw your own focus mark -- and do not ask
`hasFocus()`.** This is the one that bites hardest, because the desktop
build hides it.

qtty shows focus as reverse video on the control's own glyph: a push
button's brackets, a check box's brackets, a slider's handle, a scroll
bar's thumb. A line edit is deliberately left alone, having a real caret,
which says where typing goes as well as that it goes here.

**In an item view the vocabulary is different, and deliberately.**
`reverse` already means *selected* there, so it cannot also mean *focused*
-- which item the keys would act on is a different fact from which items
are chosen. So a list, table or tree with the keys shows its **current
item underlined**, and an item that is current *and* selected is
underlined **and** reversed. The tab bar set that precedent.

Worth telling your users, since it is the opposite of the desktop habit:
in a list on a terminal, **the underline is where the keys will act** and
the highlight is what is already chosen.

**A menu is the simple case, because it has no selection.** Nothing in a
menu is "chosen" while you are moving through it, so `reverse` is free to
mean *current* there: the item the keys would act on is highlighted, its
mnemonic letter is underlined within that highlight, and its shortcut
label is dimmed. The rule underneath both is one rule -- **reverse means
current unless something else already needs it**, and in a view something
does. **The style
can only mark the controls it knows how to draw**, so a widget that
paints itself gets no mark from anybody.

Worse, the obvious way to draw one does not work:

    void paintEvent(QPaintEvent *) {
        if (hasFocus()) drawFocusMark();      // NEVER true under qtty
    }

Under the offscreen platform no window ever becomes active, so Qt never
sets its own focus widget: `QWidget::hasFocus()` is permanently false,
`QApplication::focusWidget()` permanently null, and `State_HasFocus`
never set. That code works on the desktop and silently draws nothing on
the terminal -- the same binary, the same widget, no error anywhere. Ask
qtty instead:

    if (Qtty::has_focus(this)) drawFocusMark();

**That used to read `Qtty::focusWidget() == this`, and a pointer test
gets two things wrong.** Both are silent and both only on the terminal,
which is the pair this practice exists to prevent:

- **A focus proxy.** `hasFocus()` walks the proxy chain before comparing
  and a pointer test does not, so a composite widget that delegates its
  focus to an inner editor -- `setFocusProxy()`, the ordinary way to
  build one -- is answered **no**, where a desktop `hasFocus()` says
  yes. Measured on exactly that arrangement. It is not a rare shape: a
  `QKeySequenceEdit`, an editable `QComboBox`, a `QSpinBox`, a
  `QDateTimeEdit` and a `QFontComboBox` each hold an inner `QLineEdit`
  whose proxy is the outer widget.
- **The terminal's own focus.** Every control this library draws
  withholds its mark while the terminal is not focused -- a frame saying
  *type here* at a window receiving nothing typed is worse than one
  saying nothing -- and a pointer test knows nothing about that.
  Measured: with the terminal unfocused it still says draw, so your
  widget would keep its mark alone on a screen where every standard one
  had dropped it.

`Qtty::has_focus()` is the function the style itself asks, so your widget
and a standard one cannot disagree about who is focused.

**If you write an item delegate, draw its panel through the style.** This
is ordinary Qt -- the documentation asks for it and a desktop needs it
for selection -- and on a terminal it is what puts the focus mark on your
rows at all:

```cpp
void paint(QPainter *p, const QStyleOptionViewItem &o,
           const QModelIndex &ix) const override {
    QStyle *st = o.widget ? o.widget->style() : QApplication::style();
    st->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, p, o.widget);
    ...                                   // then your own drawing
}
```

A delegate that paints straight over its rect draws no panel, so it gets
no selection and no focus mark -- and on a view with `NoFrame`, which a
grid-disciplined layout wants because a frame costs a row and a column,
**nothing at all changes when the focus arrives.** This project's own
chat example had exactly that, and `focus_invisible()` named its message
list until the line above was added; it is one line of vanilla Qt and the
example still contains no qtty types.

**And you can check that you did it.** `Qtty::focus_invisible()` renders
your window once with the focus on each control and once without, and
names the ones that come out identical inside their own rectangle:

    QVERIFY(Qtty::focus_invisible(&window).isEmpty());

The ten Qt controls this library's own suite sweeps all pass -- button,
line edit, check box, radio, combo, spin box, slider, list, tab widget,
scroll bar -- and the ones that fail are the ones somebody wrote. A widget that edits text is not asked, because the terminal's
cursor is its mark -- that is the same `WA_InputMethodEnabled` practice 11
is about, and the check confirms the cursor really does follow the focus
onto it.

**A list, a tree or a table IS asked, even though it carries that same
attribute.** Qt sets `WA_InputMethodEnabled` on an item view as soon as
its current item is editable, which is the default for
`QStringListModel`, `QStandardItemModel` and every `QTableWidget` item --
and such a view has no caret to show focus with unless an editor is
actually open, in which case the editor holds the focus rather than the
view. **The terminal's cursor stays off it for the same reason**: a caret
means *type here*, and measured, a focused list used to park one at a
cell where nothing was being typed. So an item view has to draw its own
mark, and the report checks that it does. It moves the focus while it looks and puts it back, so it belongs
in a test rather than in a frame loop.

On a terminal this matters more than on a desktop. There is no pointer
hovering near the thing a person is about to use, and no window manager
drawing a focus ring: **the only way to know where a keystroke will land
is the mark on the screen.** A control that cannot show it is a control a
keyboard user has to find by trial.

**And it follows `setFocus()` when you call it yourself**, which is worth
saying because nothing in *Qt* makes that true here: a window that never
activates emits no `focusChanged` signal and delivers no `FocusIn` or
`FocusOut` event of its own, so `edit->setFocus()` in one of your slots
moves Qt's focus and Qt tells nobody. qtty re-reads the window's focus
widget when it composes a frame and again when a key arrives, so the mark,
the widget-shortcut contexts and `Qtty::focusWidget()` all agree with where
your keystroke is actually going. You do not have to tell it, and there is
nothing to call.

**Tabbing into a field selects its contents**, as on a desktop, because
the focus event this library synthesises carries the REASON Qt would
have carried -- `Qt::TabFocusReason` going forward, `Backtab` coming
back, `ShortcutFocusReason` when a mnemonic put you there. `QLineEdit`
selects on exactly those and leaves the text alone otherwise, so a
custom widget of yours that reads `e->reason()` gets the same answer it
would on a desktop.

**Your `focusInEvent` and `focusOutEvent` do run** -- this library sends
them when it re-reads the focus, so what Qt builds on them works too:
`QLineEdit::editingFinished` fires when a user tabs out of a field they
edited, which is the signal a form actually depends on. The timing is the
part to know: focus you move yourself is announced **at the next frame**,
not inside your `setFocus()` call. Measured -- immediately after the call
the old field has had no `focusOutEvent` and the new one no
`focusInEvent`; after one composed frame, both have, and
`Qtty::focusWidget()` agrees.

**`QApplication::focusChanged` is the one that never comes**, and no
library can emit it on Qt's behalf -- it belongs to the activation path
that does not run here. A program that watches focus centrally should
watch the *events*, with an event filter on `QEvent::FocusIn`, rather than
connecting to that signal and hearing nothing. A check pins both halves,
so the day Qt starts emitting it the suite says so.

**The same trap has one more member, and this one qtty cannot repair.**
`QApplication::keyboardModifiers()` is filled by Qt from *platform* events,
and there is no public setter -- so it is right after a key and **wrong
during a mouse event**. Measured: inside a `mousePressEvent` for a
Ctrl+click, the event says `Ctrl` and the application-wide answer says
nothing at all; and if the last key you pressed was `Ctrl+S`, a plain click
after it reads `Ctrl` that nobody is holding. So:

    void MyWidget::mousePressEvent(QMouseEvent *e) {
        if (e->modifiers() & Qt::ControlModifier) ...   // right
        if (QApplication::keyboardModifiers() & ...)    // wrong here
    }

**Ask the event.** That is good practice on the desktop for other reasons
and is the only thing that works here. A check pins the disagreement, so if
Qt ever starts tracking these the suite says the limit has lifted.

**11. If your widget edits text, set `WA_InputMethodEnabled`.** Qt sets
it on `QLineEdit`, `QTextEdit` and `QPlainTextEdit`. A text editor of
your own is outside that, and **two things go wrong at once, silently,
and only on the terminal**:

    setAttribute(Qt::WA_InputMethodEnabled);

Without it, **`Ctrl+C` quits the application** instead of copying. The
quit keys stand down for a widget that says it takes text, because a
caret in a field is the one place a person means *copy* rather than
*interrupt* -- so an editor that never said so gets the interrupt.

And **the terminal cursor is not placed**: the compositor puts it on the
focused widget only when that attribute is set, so nothing on screen says
where typing goes.

The desktop build has a real caret and no quit key, so a widget missing
the attribute behaves perfectly there and fails only where you are not
looking. (Qt *clears* the attribute on a read-only line edit, which is
right: nothing to copy, and `Ctrl+C` should still quit.)

**12. Do not put a style sheet on a control.** Measured on a push
button, at three settings:

    no sheet                       <Hi>
    background/border/padding      Hi      -- the brackets are gone
    color: red                             -- nothing at all

The brackets are how a terminal user knows a thing is a button. A styled
one still takes `Enter` and still shows focus, and **nothing on screen
says it is a control** -- so the affordance is gone while the behaviour
stays, which is the worst way round. A colour-only sheet is worse again:
the button draws nothing. A `QLabel` with the same rule is unharmed, so
this is about controls rather than about drawing.

Qt's style-sheet machinery takes drawing over from the application style,
which is what it is for; qtty's cell drawing IS the application style, so
a sheet replaces it. If you style for the desktop, set the sheet only
there -- and you can ask from anywhere, not just from `main()`:

    if (!Qtty::is_tui_active()) setStyleSheet(...);

`Qtty::is_tui_active()` is true while a terminal session is being driven --
by `exec()`, or by an application's own frame loop -- so a widget deep in a
tree can branch without being told which frontend built it. That is the general escape for anything a desktop
wants and a terminal cannot use.

**And you can assert that none is in force**, which is the ninth question
under *Checking it without a terminal*:

    QCOMPARE(Qtty::sheet_styled(&window).size(), 0);

It asks each widget's `style()` rather than its `styleSheet()`, so it sees
a sheet set on a container or on the application as well as one set on the
control -- a widget those reach reports an empty sheet of its own. It
excludes no class, because which widgets a sheet costs their drawing is
not predictable from the class: measured under one `padding: 1px` rule, a
push button, a spin box, a progress bar and a group box all drew
differently while a check box, a radio button, a line edit, a combo box, a
slider, a label and a frame did not -- and the group box changed for the
better. The remedy is the same for every row, so over-reporting costs
nothing: the `is_tui_active()` line above empties the list in one edit.

**13. Prefer stepping to dragging.** The three controls people drag are
not one case, and this practice used to say they were. Measured, with the
focus put on each by hand:

| control | focus policy | a `Tab` stop | arrows when focused |
|---|---|---|---|
| `QSlider` | `StrongFocus` | yes | 50 → 53 |
| `QScrollBar` | `NoFocus` | no | 50 → 47 |
| `QSplitter`'s handle | `NoFocus` | no | **nothing at all** |

So a slider is fine as it stands. A scroll bar answers arrows and cannot
be reached -- which costs nothing, because the view it scrolls takes focus
and scrolls with the same keys. **A splitter has no keyboard route
anywhere in Qt**: not merely unreachable, unanswering, so no focus policy
you set will help. Give the split a menu action of your own that sets the
sizes; "drag the handle" is not available to everybody.

`Qtty::pointer_only()` names splitter handles for exactly this reason,
beside the buttons.

**And dragging BETWEEN widgets does not happen at all.** `QDrag::exec()`
is the platform's half of drag and drop, and the offscreen platform has
none: measured, it returns `Qt::IgnoreAction` in under a millisecond and
no target hears a thing. Plain Qt offscreen does the same, so this is the
platform rather than the library -- but what your user sees is worth
knowing, because it is not "nothing":

    a QListWidget set to InternalMove, dragged      order unchanged
    the same gesture, what actually happened        the item under the
                                                    pointer got SELECTED

Qt enters `startDrag()`, it returns instantly, the view falls back to
rubber-band selection, and the reorder silently becomes a selection
change. `Qtty::exec_drag()` in `qtty/drag.h` is the spelling that works
here -- the library owns the pointer, so it can carry the drag Qt cannot
-- and it needs a release or an `Escape` to end it, which on a terminal
means a user with no mouse needs the `Escape`. Give the reorder a
keyboard route regardless: that is this practice.

**14. If you pick your own colours, ask the terminal which way round it
is.** An application that leaves the default `CellTheme` alone needs
none of this: it renders in the terminal's own colours, whatever they
are, and a dark terminal and a light one both come out right. The one
that breaks is the application that chooses a palette of its own -- and
choosing one without asking is choosing correctly on about half of all
terminals, with nothing on screen to say which half you are on.

The spelling you already know does not work here, and it fails silently:

    qApp->styleHints()->colorScheme()      Qt::ColorScheme::Unknown, always

`Qtty::prepare_environment()` pins the platform theme empty on purpose --
a desktop theme reaching into a terminal program supplied proportional
fonts and twenty of Qt's seventy-one key bindings -- and Qt's generic
theme reports no scheme at all. So ask qtty, which asked the terminal:

    switch (Qtty::color_scheme()) {
    case Qt::ColorScheme::Dark:  apply_dark();  break;
    case Qt::ColorScheme::Light: apply_light(); break;
    case Qt::ColorScheme::Unknown: break;       // keep your own default
    }

`Qtty::color_scheme()` compares the background the terminal reported
against the foreground it reported, and darker-than-its-own-foreground
is dark. Not against a threshold: the two colours a terminal reports are
a pair somebody reads text with, so a legible scheme keeps them well
apart, while a single colour weighed against a midpoint is decided by
the midpoint. A mid-grey ground is where the two answers part company,
and mid-grey grounds ship on real desktops.

**And you can colour your own TEXT, which is a different question and
the one a log reader actually asks.** Practice 14 above is about
choosing a palette; this is about a red line in a list of green ones.
All three of the spellings you already know reach the terminal, and the
colour arrives exactly as given:

```cpp
label->setText("<span style=\"color:#ff0000\">ERROR</span> ok");  // rich text

QTextCharFormat red;                                              // a char format
red.setForeground(QColor(255, 0, 0));
cursor.insertText("ERROR", red);

QPalette pal = label->palette();                                  // a palette
pal.setColor(QPalette::WindowText, QColor(255, 0, 0));
label->setPalette(pal);
```

Measured on all three: five cells carrying `fg=#ff0000` and the rest
the terminal's own default. Note what is NOT happening -- the colour is
not matched against this library's theme and replaced by the nearest
role. What you asked for is what the cell holds.

**What the terminal does with it depends on the terminal**, and
`Qtty::quantise()` is where that is decided: true colour is emitted
unchanged, a 256- or 16-colour terminal gets the nearest index, and a
monochrome one gets no colour at all. So a colour is a hint that
degrades rather than a guarantee -- **do not let it be the only thing
carrying the meaning.** Prefix the line, or mark it, so the log still
reads on a terminal that dropped the red.

And the caution from practice 14 applies here too: a colour you chose
against your own idea of the ground may not clear a contrast floor on
somebody else's terminal, which is why the default `CellTheme` renders
in the terminal's own colours and asks nothing of you.

**Handle `Unknown` by doing nothing.** It is what you get before a run,
from a terminal that answered neither query, and from one that answered
only one of them -- and it is the common case rather than the exotic
one, because plenty of terminals answer nothing. The two wrong answers
are not worth the same: guessing light when the terminal is dark leaves
your application looking plain, and guessing dark when it is light puts
pale text on a pale ground, which cannot be read at all. So there is no
coin to toss, and qtty does not toss one on your behalf.

## The shape of the caret

**Overwrite mode changes the caret, and you get that by setting the mode
rather than by asking for a shape.** A `QTextEdit` or `QPlainTextEdit`
whose `overwriteMode()` is true is given the terminal's block caret;
everything else keeps a bar. That is the convention every terminal editor
already follows, and it is the one thing about a caret a terminal user
reads without being told -- a bar sits *between* two characters, which is
where an inserted one goes, and a block sits *on* one, which is the
character about to be replaced.

    edit->setOverwriteMode(true);     // the caret becomes a block
    edit->setOverwriteMode(false);    // and a bar again

Nothing else is needed and there is nothing else to call. The shape is
recomputed with each frame from the widget that has focus, so an
application that toggles the mode from an `Insert` key gets the caret
change with it.

**There is deliberately no way to ask for a shape directly.** The shapes
`Qtty::CursorShape` names are the backend's vocabulary, not an
application-facing control: a second way to say "block" would be a second
thing to keep in step with the mode, and the two would drift. If you have
a case that wants a caret shape the mode cannot express, that is worth
raising rather than working around.

**Your caret does not outlive your program.** When qtty gives the terminal
back -- on exit, on `Ctrl+Z`, on a crash -- it restores the caret the user
configured, the same way it puts back the title it pushed. A terminal that
does not implement caret shapes at all ignores the whole exchange; the
request is consumed rather than printed, measured on xterm, GNU screen,
tmux and kitty.

## Copy and paste

**Copy needs no code.** The ANSI backend watches `QClipboard`, so an
ordinary `QClipboard::setText()` reaches the terminal's own clipboard as
OSC 52 and the application never learns a backend exists.

**A copy is written whole or not at all.** Past a size limit nothing goes
out, deliberately -- a truncated copy the user believes went out is worse
than a refused one. The same holds when stdout is not a terminal, and
while the terminal is suspended.

**And a copy with no TEXT in it does not go out either -- which your
application wants, whatever it may look like.** A clipboard holding only
an image or only HTML answers empty to `QClipboard::text()`, and an OSC
52 carrying an empty payload does not copy nothing: it tells the terminal
to CLEAR the clipboard. So a *Copy chart* button would have thrown away
whatever the user had, with nothing in your program having asked to copy
any text at all. The watcher now forwards a change only when there is a
text half to forward. An application setting the text to the empty
string is a different thing and is still carried out: that one said what
it wanted.

The limit of it is worth knowing, since it is your program that will meet
it: **a terminal clipboard holds text, so the picture is not going to
arrive by this route.** If copying an image matters to your application,
offer *Copy as text* -- a path, a table, a URL -- beside it, and the
keyboard user gets something they can paste.

**Your application cannot tell that it was refused; the user eventually
can.** The `QClipboard` watcher discards the result, and neither the
limit nor the writing call is in an installed header, so you cannot ask
before offering a Copy of something large or branch on the answer after.
qtty does not stay silent about it, though: a refused copy logs a message
naming the size refused and the bound it broke, held with the other
diagnostics and printed when the terminal is given back. So a person
learns why their copy did not arrive -- **after they have stopped
needing to know.** Whether the limit belongs in the public API is an open
question in `project.md`.

**Paste needs no code either, and `Ctrl+V` agrees with it.** A terminal
paste arrives as one event carrying the whole text -- that is what
bracketed paste is for, so the newlines in it do not fire your default
button -- and the arriving text also goes into `QClipboard`. That second
half is what makes `Ctrl+V` work: Qt's clipboard is the only store a
widget reads, so without it your program's own paste shortcut inserted
whatever your program had last copied, which in a program that has
copied nothing is nothing at all. The two ways of pasting now give the
same answer.

**What it is not is a read of the terminal's clipboard.** OSC 52 can ask
a terminal what it holds, and qtty does not ask -- xterm refuses the
question by default and says why: a program that can read the clipboard
can read every password its user has copied. So `Ctrl+V` pastes the last
thing the *user* pasted into your program, not whatever is on their
clipboard now. For a terminal application that is the whole of what is
knowable without asking permission nobody grants.

**And a paste is not a copy.** The mirrored text carries
`Qtty::terminal_paste_format()` on the clipboard data so the backend can
tell it from a copy and does not send it back out. That matters because a
middle click pastes the PRIMARY selection while the clipboard holds
something else, and echoing the paste would replace what the user had
with something they never copied. If you write your own
`ITerminalBackend` and forward `QClipboard` to the terminal, skip a
change carrying that format -- `backend.h` says so beside the name.

**If you are wondering which graphics tier your program is on**, three
things in the environment decide it before any of your code runs, and two
are easy to be inside without noticing. `QTTY_GRAPHICS` names a tier
outright. `TMUX` puts qtty on the Unicode-placeholder path, because a
direct placement would land where the outer terminal's cursor is rather
than where tmux is drawing. And `TERM` beginning `screen` or `tmux` does
the same on its own, deliberately -- a terminal calling itself screen is
treated as one even when `TMUX` is unset. `qtty-negotiate --probes` run
in the terminal you care about reports what was actually measured there;
run it over a pipe and it says so rather than guessing.

**Readline editing works in a text field, with the conventions on.**
`Ctrl+A` and `Ctrl+E` go to the start and end of the line, `Ctrl+K` and
`Ctrl+U` kill forward and back, and `Ctrl+W` rubs out a word -- the
chords a shell user's fingers already know. Four of them are free, Qt
giving them no meaning at all.

**`Ctrl+A` is the one that had to be decided, and it is decided per
widget.** It means Select All in every Qt program and start-of-line in
every shell, and a terminal application is both. So it follows `Ctrl+C`:
the answer depends on whether the focused widget takes text. In a line
edit it is start-of-line; in a list, a tree or a table it is still Qt's
Select All, and nothing there changes.

**Your own shortcut wins.** A `QShortcut` or `QAction` bound to one of
these fires as it always did, even with a text field focused: a shortcut
is something your program asked for by name, and these are a convention
offered on its behalf.

**You can choose the font, and a user can override you.** qtty lays its
grid on DejaVu Sans Mono at 16 pixels; `Qtty::set_font(family, pixels)`
before `Qtty::setup()` names another, and `QTTY_FONT` and
`QTTY_FONT_SIZE` let whoever runs your program name one instead. That
last is the one that matters on a strange machine: the cell is whatever
the font advances, `setup()` refuses a font whose metrics are not whole
numbers, and until this existed the refusal named a font nobody could
change. The hinting is qtty's either way -- it decides whether the
metrics are integral at all rather than how the text looks.

**And a font need not be installed.** `Qtty::add_font_file(path)`
registers a file with Qt and returns the family it holds, or an empty
string when the file could not be read:

```cpp
const QString family = Qtty::add_font_file(":/fonts/grid.ttf");
if (!family.isEmpty()) Qtty::set_font(family, 16);   // before setup()
```

`QTTY_FONT_FILE` is the same thing for whoever runs the program, and
with it set the file supplies the family when nobody named one. Use a
file rather than a family name when it matters which font you get: **a
family Qt cannot resolve is substituted silently**, which is how this
library once ran a whole suite on Noto Mono with nothing anywhere
saying so, and a file either registers or it does not.

Registering a file and choosing the grid's font are separate acts.
`add_font_file()` does the first and hands you the family; the variable
does both. Ship the file yourself -- qtty bundles none, a font carrying
its own licence terms not being this library's to choose for you.

**You can choose how often the screen is repainted.** qtty coalesces a
burst of damage before it sends a frame -- 16 ms locally, 50 ms where
`SSH_CONNECTION` or `SSH_TTY` says the terminal is at the other end of a
link, which are the two numbers the design measured.
`Qtty::set_frame_interval(ms)` names another, and `QTTY_FRAME_MS` lets
whoever runs your program name one instead:

```cpp
Qtty::set_frame_interval(100);     // a dashboard nobody types into
```

It reaches a run already going, so you can raise it while a long
operation paints and put it back afterwards. `0` means *as soon as the
event loop comes back*; a negative interval is refused rather than
treated as 0, because that would paint as fast as your loop allows and
on a slow link that is the thing the budget exists to prevent.

**A quit key asks your window to close, and takes no for an answer.**
`Ctrl+C` is the close gesture a terminal has -- there is no title bar to
click -- so it goes through `QWidget::close()` and your `closeEvent()`
runs exactly as it does on a desktop:

```cpp
void MainWindow::closeEvent(QCloseEvent *e) {
    if (!dirty || askToSave() == QMessageBox::Discard) e->accept();
    else e->ignore();                 // and the program stays up
}
```

Until this was measured it did not: the key ended the event loop behind
the window's back, so an unsaved-changes prompt was skipped in silence.
A program that asks nothing is unaffected -- a close nobody refuses is
accepted.

**You can change the quit keys, and an application using `exec()` could
not until it was asked for.** `Qtty::set_quit_keys()` takes the list:
name a letter, name a chord, or pass an empty list for no quit key at
all -- and then your program owes its user a way out of its own.

```cpp
Qtty::set_quit_keys({{0, "q", false, false, false}});   // q quits
Qtty::set_quit_keys({});                                // nothing does
```

**A letter is spelled as text, which is how a terminal sends one.** An
ordinary character arrives with no key code at all and the letter in
`text`, so `{0, "q"}` is the quit key *q*; a control chord carries a key
code and no text, which is how `Ctrl+C` is written. Call it before
`Qtty::exec()` or from a slot during the run -- both take effect, and a
router built afterwards starts from it.

**The chord you name is the chord that quits, `Shift` included.** All
three modifiers are compared, so `{Qt::Key_C, {}, true, false, false}` is
`Ctrl+C` and is not `Ctrl+Shift+C` -- which on most terminals is copy,
and is nobody's request to end a program. There is no spelling for
"shift unspecified": `Qtty::KeyEvent` has one flag per modifier and no
third state.

Until this was measured only `Ctrl` and `Alt` were compared, and the
price was paid by the keys this library binds itself: an application
naming `F10` as its quit key also quit on `Shift+F10`, which is the
context-menu chord. The readline bindings read the whole chord for the
same reason -- `Ctrl+Shift+K` is not the kill.

**`Ctrl+D` deletes forward too**, on the same terms: with the
conventions on, in a widget that takes text. Elsewhere, and with them
off, it is still a quit key -- so a chord that neither quits nor deletes
never exists.

That needed one thing to land first. qtty used to report a vanished
terminal by **synthesising `Ctrl+D`** into the same sink a keystroke
arrives on, so giving the chord to a text field would have swallowed the
signal that stops a program whose terminal has closed -- exactly when a
field had focus, which in a TUI is most of the time. The backend says it
on `ITerminalEventSink::on_terminal_lost()` now, which nothing routes and
no binding can take.

PRIMARY -- what a middle click pastes -- is unreachable through Qt here.
Under the offscreen platform `QClipboard::supportsSelection()` is false
and Qt refuses `setText(.., QClipboard::Selection)` outright.

**Your user's habitual drag-to-select stops working, and there is an
escape you should tell them about.** qtty turns on mouse reporting --
`1006` and `1002`, so drags reach the application -- which means the
terminal no longer treats a drag as a selection. By convention most
terminals bypass reporting while **Shift** is held (xterm and the VTE
family do), so `Shift`-drag still selects and the terminal's own copy
still works.

That last is stated as convention rather than measurement, and qtty is
structurally unable to check it: the whole point is that those events
never reach the application. It is worth a line in your own help, because
a user who cannot select text usually concludes the program is broken
rather than that a modifier is missing.

**Paste arrives as text, not as typing.** That is what bracketed paste is
for: delivering the newlines as `Return` would fire a dialog's default
button halfway through a paste. Newlines are folded to spaces for
`QLineEdit` and `QAbstractSpinBox`, which cannot hold one.

**A single-line editor of your own gets the raw newlines.** The test is
by type, Qt exposing no generic "accepts a newline" query -- so if you
write one, fold them yourself. This is the fourth thing on this page a
custom widget must do that a standard one gets free, with practices 9,
10 and 11; they are worth reading together before writing one.

## Opening a link

**`QDesktopServices::openUrl()` works, and your program keeps its usual
spelling.** A *Help -> Website* action, a link in an About box, a
`QLabel::linkActivated`, a `QTextBrowser::anchorClicked` -- all of them
reach the handler `Qtty::setup()` registers, which puts the URL on the
clipboard and then shows a box naming it. Nothing in your application
changes, which is what separates this from the tray and the drag.

Before it did, the call returned false and Qt warned -- into the deferred
diagnostics, so your user pressed Enter on a link, nothing on the screen
moved, and the explanation reached them at the shell prompt after your
program had exited.

**It does not launch a browser, deliberately.** `xdg-open` is wrong in
both of the places a terminal program usually runs: over ssh it opens a
browser on the machine your program is on rather than the one your user
is sitting at, and on a headless host it opens one nobody can see. The
clipboard goes the other way -- the write leaves as OSC 52 and the
terminal *emulator* performs it, on the user's own machine -- so the link
lands where the browser is.

**The box is queued rather than synchronous.** `openUrl()` returns at
once, as it does on a desktop, and the dialog goes up on the next turn of
the event loop. A link activated from inside one of your slots does not
re-enter it.

**A link longer than the terminal is folded, not elided.** The box is
sized to the terminal rather than to the link -- a URL is one word, so a
wrapping label would leave it whole and make the dialog wider than the
screen. The clipboard has the link exactly; the screen has all of it,
across as many rows as it takes.

**`http` and `https` only.** `mailto:` and `file:` fall through and
return false, which is the honest answer: neither has a single right
meaning on a terminal, and a false is something your application can see
and act on.

**You can take a scheme over, and there is no qtty API for it.**
`Qtty::setup()` registers before your widgets exist, and Qt keeps one
handler per scheme with the last registration winning -- so your own
handler, registered any time afterwards, is the one that gets the link:

```cpp
Qtty::setup(app);
QDesktopServices::setUrlHandler(QStringLiteral("https"), &opener, "open");
```

**A keyboard-reachable link is not named by `Qtty::pointer_only()`, and
that was the sharp end of this.** That report lists controls only a mouse
can reach, so it clears a label that is a tab stop and activates on
Enter -- which was exactly the link whose activation did nothing. The
audit was right and the thing it cleared was broken; it is worth knowing
that a clean report means reachable, not that the control does something.

## Getting the user's attention

**`Qtty::bell()` rings the terminal's bell, and your application has to
call it.** BEL is the one attention signal a terminal has, and it is
worth more than a noise: most emulators map it to the window-urgency
hint -- the thing that marks a background tab -- so one call covers both
of the things a desktop keeps apart, the sound and the flashing taskbar
entry.

It does nothing when no backend is driving, which is the same "nothing
was measured" answer `Qtty::capabilities()` and `Qtty::terminal_cells()`
give outside a run. There is no terminal the library was given, and
ringing your process's controlling one behind its back would be a write
to a screen qtty does not own.

**`QApplication::beep()` and `QApplication::alert()` do nothing under the
offscreen platform qtty pins, and neither of them starts working.** That is the shortfall `Qtty::exec_drag()`
and `Qtty::SystemTrayIcon` already state in their own headers, and it is
stated here for the same reason: an adopter should find out from the
documentation rather than from a user. Both calls are static and
non-virtual, and both end at the platform:

- `beep()` is one line, `QPlatformIntegration::beep()`, whose base
  implementation is an empty function body. The offscreen platform
  `Qtty::prepare_environment()` pins does not override it, so there is
  no seat anywhere between your call and nothing happening.
- `alert(widget)` reaches `QWindow::alert()`, which returns immediately
  when the window has no platform window -- and under
  `Qt::WA_DontShowOnScreen`, which qtty sets on every top level, there
  never is one. Even given one, `QPlatformWindow::setAlertState()` is
  empty and `isAlertState()` answers false.

This is the opposite case from **Opening a link**, where Qt publishes a
handler in front of the platform, `Qtty::setup()` registers one, and
`QDesktopServices::openUrl()` keeps its ordinary spelling with nothing in
your application changed. There is no such hook for either of these two.
So change the call site, or nothing rings:

```cpp
// QApplication::beep();          // silent under qtty
// QApplication::alert(this);     // silent under qtty
Qtty::bell();
```

**The backend decides whether anything is written, and two cases write
nothing.** A stream that is not a terminal is sent no bell -- nothing
downstream of a pipe rings, and the byte would only land in somebody's
captured output as a control character to step over. And nothing is rung
while `Qtty::shell_out()` has handed the screen to an editor or a pager:
the terminal is that program's for the duration, and a BEL arriving then
rings for it rather than for you.

**And the desktop's own notification is available to a program with no
display**, which is `QSystemTrayIcon::showMessage()` and reaches the
same bubble a windowed build would put up:

```cpp
if (Qtty::SystemTrayIcon::messages_available())
    tray.show_message("Backup", "finished", "drive-harddisk");
```

It is a **second** service rather than part of the tray -- a desktop's
bubbles come from `org.freedesktop.Notifications` and the
StatusNotifierItem specification has no notification method -- so ask
about it separately: a machine can have one and not the other, and a
server usually has neither. `show_message()` returns whether the desktop
took it, which is the answer to act on.

Successive messages **replace** each other rather than stacking, as a
desktop application's single balloon does, so a program reporting
progress leaves no column behind it.

`message_clicked()` exists and fires only where the notification daemon
supports actions -- qtty asks it, and sends none where it does not. So
do not read the absence of that signal as the absence of a click:
`show_message()` returning true is what says the user was told.

## Modal dialogs and `exec()`

`if (dialog.exec() == QDialog::Accepted)` works, and the terminal goes on
drawing while the dialog is up. That is worth stating because it is not
obvious: `exec()` runs a **nested event loop**, and a library that owned
a read loop of its own would freeze the screen inside one. qtty owns
none -- input arrives on a `QSocketNotifier`, frames on a timer -- so a
nested loop pumps both exactly as the outer one does. A check pins it, by
reading the frame from inside the nested loop and asserting the dialog is
in it.

`Esc` rejects such a dialog and its default button answers `Enter`, both
Qt's own; see the first table.

**`QFileDialog` works, and you need no option to make it.** The whole
widget arrives -- the places sidebar, the file list with its columns, the
name field, the filter combo, Open and Cancel -- and a person can drive
it with no pointer at all: type a name, press `Return`, and your
`selectedFiles()` holds the path. You do **not** need
`DontUseNativeDialog`: this platform offers no native dialog, so Qt uses
its own without being asked. Measured end to end, with and without the
option.

**A modal gets a box, and its title goes in the top rule.** A desktop's
window manager draws a dialog's frame and name; a terminal has no window
manager, so qtty draws them -- without it a dialog's contents read as
extra columns of the window behind, which is what they did until this
was measured:

```
             ┌─ Preferences ──────┐
 window row 0│                    │
 window row 1│ Theme:             │
             │ [ ] Dark           │
 window row 2│ <OK>               │
             └────────────────────┘
 window row 3 ...............
```

So `setWindowTitle()` on a dialog is worth setting: it is the only place
that name is shown. **The box is drawn only when there is room for it**
-- a row above and below, a column either side -- and on a terminal
without them the dialog is drawn bare rather than losing a field to its
own chrome. Nothing is asked of you either way.

**A dialog opens with its first field focused, and that one is not Qt's
here.** On a desktop Qt seats focus when the window *activates*, and no
window activates under this platform -- so a dialog used to come up with
nothing focused at all, and the first keys a user typed went nowhere.
Measured, before the fix: a two-field dialog with a default button,
`focusWidget()` null, `ab` typed into it, and both fields still empty.
A `QMessageBox` was unaffected, because Qt focuses its own default
button -- which is precisely why the fault survived being tested with
one.

You need do nothing for this, and there is one thing you can do with it:
**call `setFocus()` on the field you want before showing the dialog and
your choice is kept.** The runtime only seats focus where nobody else
has.

**Where a dialog lands, and the one case worth knowing about.** Give a
dialog a parent and Qt centres it over that parent, which is inside the
terminal, and qtty leaves it there. Give it **no** parent -- which is what
`QMessageBox::information(nullptr, ...)` does -- and Qt centres it on the
primary screen, which under this platform is a fiction reported as
800x800 whatever your terminal is: it asked for `+326+325` in every
terminal measured, and the result was a dialog jammed into the corner. So
qtty centres a dialog nobody placed, and keeps it centred across a
resize.

If you position a dialog yourself with `move()`, that is obeyed and
nothing here second-guesses it. You are not asked to do anything
differently; this is written down because a dialog appearing somewhere
you did not choose is exactly the kind of thing that reads as a bug in
your own layout code.

## Never block the event loop

Everywhere else this is advice about responsiveness. Here it is about
whether the user can get out at all.

A full-screen program owns its keyboard, so qtty clears `ISIG` and
`IXON`: the terminal driver no longer turns `Ctrl+C` into `SIGINT`, and
`Ctrl+S` no longer means flow control. **Both keys arrive as bytes**,
read by the event loop like any other -- which is the only way
`InputRouter`'s quit keys can see `Ctrl+C` at all, the only way
`set_quit_keys()` can change them, and the only way a text field can
keep `Ctrl+C` for copy.

The cost lands on exactly one case. **If your handler blocks the event
loop, nothing reads those bytes, so `Ctrl+C` does nothing** -- no signal,
no quit, no redraw, no input. On a desktop a frozen window can still be
closed by the window manager, and `Ctrl+C` in the launching terminal
still signals; here neither is true, and the only way out is a `kill`
from another terminal.

So a long operation goes on a thread, or breaks itself up:

    QtConcurrent::run([this] { ... });   // and a signal when it lands

`QCoreApplication::processEvents()` inside a loop works too and is the
blunter instrument -- it re-enters, so anything reachable from the event
loop can run underneath you, which is the same hazard a nested `exec()`
has and worth the same care.

The upside of the same decision is worth knowing: because `IXON` is
cleared, a user who types `Ctrl+S` out of habit does **not** freeze your
screen with no way to know why. That key reaches you like any other.

**`Ctrl+Z` is a key here too, and a terminal user will expect a
suspend.** Clearing `ISIG` takes that from the driver along with the
other two. A real `SIGTSTP` -- `kill -TSTP` from another window -- is
handled properly: qtty gives the terminal back, stops for real, and
restores raw mode, the cursor and mouse reporting when `SIGCONT` arrives.
What no longer happens by itself is the keystroke. If you want it, bind
the key and ask for the signal:

    if (chord == Ctrl+Z) ::raise(SIGTSTP);

which lands in the same handler and behaves the same way. That is a
deliberate choice to leave with you rather than take: a full-screen
editor usually does NOT want `Ctrl+Z` suspending it, having its own use
for the chord.

**`Ctrl+L` is the other key a terminal user reaches for, and it is
yours for the same reason.** A screen can be corrupted by something
this library did not do and cannot see -- another process writing to
the same tty, a sequence dropped over a slow link. What qtty remembers
about the screen then describes one that no longer exists, so every
cell diffs to nothing and frames go out saying nothing at all:

    if (chord == Ctrl+L) Qtty::redraw();

`Qtty::redraw()` forgets what the terminal is showing and draws the
next frame whole, placements included. It is a no-op when nothing is
driving a screen, so the same slot is safe in a desktop build.

It is deliberately **not** bound for you. Binding it is a change to
what the opt-in conventions mean rather than a capability, and that is
the copyright holder's; until it is settled, bind it yourself -- and
practice 8 says to list what you bound.

### Running an editor, a pager, or anything else that wants the screen

A TUI usually has one thing it cannot do itself, and reaches for a
program that can. `Qtty::shell_out()` hands the terminal over for the
duration and takes it back:

    Qtty::shell_out([&] {
        QProcess::execute(qEnvironmentVariable("EDITOR", "vi"), {path});
    });

The terminal is the child's while the body runs: qtty leaves the
alternate screen, puts the line discipline back as it found it, turns off
mouse reporting and shows the cursor, so the child sees an ordinary
terminal and its output lands where the user can scroll back to it.
Afterwards qtty takes the screen again and puts back what the handover
cost -- the window title, the cell size if the window was resized while
the child had it, and the whole screen, since leaving the alternate
screen clears it.

Three things are worth knowing.

**Use the body, not a pair of calls.** The terminal comes back even if
the body throws, and there is no second call to forget. That is the only
form offered for the same reason.

**It blocks, and that is the point.** Everything in *Never block the
event loop* still applies -- timers do not fire, keys are not read, and
the frame on the screen is the child's. Use it for something the user is
waiting for anyway, not for background work.

**It returns whether there was a terminal to hand over.** A program whose
output is a pipe has nothing to suspend; the body still runs, and the
result is `false`. Ignore it unless you want to behave differently in
that case.


## Where your output goes

A TUI owns the screen, so anything printed into it lands in the middle of
a frame -- and nothing repaints over it, because the cell plane never
changed.

**Qt's own logging is handled for you.** `setup()` installs a message
handler that buffers `qDebug()`, `qWarning()` and the rest while stderr
is a terminal, and writes them out when the backend gives the terminal
back, with a count of anything it held. An application that takes the
screen some other way can flush them itself:

    Qtty::flush_deferred_messages();

**`qFatal()` is handled the other way round, deliberately.** The screen
is given back *first* and the message printed after, because a fatal
message is the process's last words and the alternate screen dies with
the process -- taking them with it. Measured before that was so: with a
frame up, 2746 bytes of screen reached the terminal and not one sentence
of the diagnostic.

**A raw `printf`, `std::cout` or `write(1, ...)` is not interceptable.**
It goes straight to the terminal, so nothing here is asked and nothing
here knows: the bytes land in your frame, and the next frame is as
quiet as any other, because what qtty remembers about the screen is
still what it last drew. Use Qt's logging and this is taken care of. In
a dual-frontend application it is the easiest trap on this page to fall
into, because the identical line is harmless in the GUI build -- which
is the same reason every trap here is a trap.

**The frame is recoverable, though your output is not.**
`Qtty::redraw()` forgets what the terminal is showing and draws the
whole thing again, over the top of whatever landed there:

    Qtty::redraw();      // after something wrote to the terminal

Measured on a live terminal -- twelve stray bytes written to the tty,
an ordinary frame after them writing nothing at all, and the redraw
putting the window back. It is worth binding to a key for the same
reason `Ctrl+L` exists everywhere else, since the person who can see
the mess is the user rather than the program; *Running an editor, a
pager, or anything else that wants the screen* has the one-liner.

## If you are writing a custom widget

Six things a standard Qt widget gets and yours does not. **Each fails
silently, and each fails only on the terminal** -- the desktop build hides
all six, which is what makes them worth collecting in one place rather
than leaving scattered above. The first four are one line each; the last
two are facts about the grid.

| Do this | Or else |
|---|---|
| `event->ignore()` for a key carrying `Alt` (practice 9) | you eat the `z` of an `Alt+Z` that was meant for a menu |
| Draw a focus mark, asking `Qtty::focusWidget()` (practice 10) | nothing marks you, and `hasFocus()` is permanently false here |
| `setAttribute(Qt::WA_InputMethodEnabled)` if you edit text (practice 11) | `Ctrl+C` quits instead of copying, and no cursor is placed on you |
| Fold pasted newlines if you are single-line (*Copy and paste*) | you get the raw ones: the fold is by type and your type is not on the list |
| Put your text lines at least `Qtty::GridMetrics::ch()` apart | two lines closer than a cell row share one, and the later one wins -- measured in Qt's own `QCommandLinkButton`, whose title and description sit 14 pixels apart and whose title therefore vanishes |
| Measure your text in COLUMNS, with `Qtty::to_clusters()` and `Qtty::cluster_width()` | `QString::size()` counts the wrong thing for every emoji, every CJK character and every combining mark, so your own truncation lands in the middle of a glyph |

**The sixth is where a custom widget most often goes wrong quietly**,
because on a desktop you measure text in pixels and here you measure it in
cells, and `QString` counts neither. The rules, each measured:

- A CJK character and most emoji are **two** columns.
- A combining mark is **none** -- `e` and a combining acute are one
  cluster and one column, and a mark on its own is zero, not one.
- A flag is two regional indicators, **one** cluster and two columns,
  which no per-character table can tell you.
- A cell holds a base and at most **thirty** marks, after which the rest
  are dropped: UAX-15's stream-safe limit, so that one cell cannot cost
  the wire ten thousand bytes.

`to_clusters()` splits a string the way the grid does and
`cluster_width()` prices each piece. Between them they are what
`elide_to_cells()` uses, and a widget that truncates its own text wants
the same pair rather than `left(n)`.

**And two ways to draw content the grid cannot infer, which this page
has never mentioned and which are the reason to write a custom widget
at all.** Everything above is about a widget that draws roughly what a
standard one draws. These are for one whose content is its own:

| If your content is | Do this | What happens |
|---|---|---|
| **cells** -- a terminal, a hex view, a board | inherit `Qtty::ICellPainted` and implement `paint_cells(CellBuffer &, const QRect &cells)` | you are handed the frame and your own rectangle in it, and you write glyphs and attributes straight in |
| **pixels** -- a plot, a meter, a still | inherit `Qtty::PixelSurface` and paint with `QPainter` as you always would | qtty harvests the result and hands it to the graphics plane with your cell geometry |

Both are inert in a GUI build: `PixelSurface` is an ordinary `QWidget`
there and `paint_cells()` is never called, so the same class serves
both frontends.

**Never claim both.** It compiles, and the pixel path wins silently --
qtty tests for a surface first, so `paint_cells()` is never called and
your widget is harvested as an image with no warning anywhere.
Measured, and pinned by a check so the consequence cannot quietly
change in either direction. The two interfaces answer opposite
questions, and a class claiming both has answered neither.

**`cells` is where you are, not a fence.** The buffer handed to
`paint_cells()` is the whole frame; your rectangle already carries the
compositor's origin, so draw at `cells.left()` and `cells.top()` and
map nothing yourself. Write outside it and you write over your
neighbours, and nothing notices -- measured, not inferred. A cell
outside the buffer is dropped rather than wrapped, so a widget
positioned partly off-screen may draw its whole rect and let the edges
fall away.

The fifth is the one that is a limitation rather than a price: a cell row
is the unit, so a widget laying its own lines out in pixels can ask for the
same row twice. Qt's own command link button does exactly that, which is
worth knowing before you conclude your own painting is at fault.

**A font you set keeps its style and loses its face.** The grid rests on
one glyph per cell, so the library replaces the family and the size on
every widget and keeps what an application MEANS by a font -- bold,
italic, underline. Measured, asking for bold italic underlined Courier
10:

    asked for          Courier         bold italic underline
    the widget kept    the grid's face bold italic underline

The consequence worth knowing is at the other end: `QFontDialog` returns
its sample widget's font, so a font chooser hands you the weight the user
picked and the grid's face. If your application needs the user's FACE --
for export, for a document property, for printing elsewhere -- do not
read it out of the returned `QFont`, because on a terminal there is one
face and it is the cell's.

**Blink is the one emphasis a `QFont` cannot carry**, so it is asked for
with a property instead:

    alert->setProperty("qtty.blink", true);

Every glyph that widget draws is then sent with SGR 5. Like
`qtty.priority` it is inert in a desktop build -- nothing reads it there
-- so the same source runs both ways, and it can be set from a `.ui` file
by a program that does not link qtty.

**Use it sparingly, and never as the only thing that carries the
meaning.** Three reasons, and the last is the one that decides it:

- **A terminal may simply not do it.** Plenty ignore SGR 5 deliberately,
  and others render it as bright rather than blinking. Nothing reports
  back, so your application cannot know which it got.
- **It cannot be rasterised.** On a terminal drawing pictures rather than
  text -- sixel, iTerm2, the half-block tiers -- a frame is one still
  image, and there is no image of blinking text. Those tiers show the
  text unblinking.
- **Blinking text is an accessibility hazard**, and a genuine one: it is
  a known trigger for photosensitive seizures, and it is hard to read for
  anyone who needs longer on a line. A user who has turned it off at the
  terminal has turned off your only signal.

So mark an alert with a word, a colour or a position, and let blink be
the thing on top of that -- the same rule as practice 7's about hover.
If removing the blink would leave the screen saying nothing, it was
carrying the information rather than emphasising it.

**And call `Qtty::setup()` before you build anything**, which matters for
one reason rather than the general one. A window of ordinary widgets built
before that call renders byte-for-byte the same as one built after -- the
library puts the grid font back on every widget, and layouts measure at
layout time. What it cannot put back is a number your constructor already
wrote down: a widget doing
`setFixedHeight(QFontMetrics(font()).height() * 2)` before `setup()`
measured 17 pixels a row where the grid's is 19, fixed itself at 34, and
no later font change moved it. Measure in a constructor and you are
measuring whatever font the application had at that moment.

The others are not limitations of the library so much as the price of Qt
having no way to ask a widget what it is. Where a question could be put
to the widget, qtty puts it -- `WA_InputMethodEnabled` is exactly that,
and it is why the list is four items rather than a class list nobody
could keep current.

## Checking it without a terminal

The whole keyboard path is testable headlessly, with no tty and no
terminal emulator, which is how this library tests its own:

    Qtty::InputRouter router(&window);
    Qtty::test::press(router, Qt::Key_Tab);
    QCoreApplication::processEvents();
    // ... assert on window.focusWidget()

**Three helpers, and they are a guard rather than sugar.** A `KeyEvent`
carries a key, a text and three modifier flags, and *which of them
decides* is different per keystroke:

| what you send | decides | ignored |
|---|---|---|
| a named key -- `Tab`, `Escape`, an arrow | the key code | the text |
| a chord -- `Ctrl+S` | the key code and the modifier | the text |
| a character you typed | the text | the key code |
| a mnemonic -- `Alt`+letter | the text and `alt` | the key code |

**Every wrong combination is silent.** Measured on a two-field form:
`{Qt::Key_H, QString(), alt}` reaches no mnemonic, `{Qt::Key_Z,
QString()}` types nothing, and `{0, "\t"}` moves no focus -- each
returns as though delivered, and nothing says otherwise.

    Qtty::test::press(router, Qt::Key_Tab);           // a named key
    Qtty::test::press(router, Qt::Key_S, true);       // Ctrl+S
    Qtty::test::type(router, "hello");                // what a user types
    Qtty::test::mnemonic(router, 'h');                // Alt+H

`press()` fills the text for a printable key with no `Ctrl` or `Alt`, so
`press(router, Qt::Key_Z)` types a `z` rather than doing nothing, and
leaves it empty under a modifier, which is what stops a chord also typing
its letter. You can still build the struct by hand -- the table above is
what you need if you do.

**And the mouse, which has the same shape of trap one field along.**

    Qtty::test::click(router, QPoint(2, 0));          // press and release
    Qtty::test::mouse_press(router, cell);            // the halves, for a drag
    Qtty::test::mouse_move(router, cell, 1);          // with a button: a drag
    Qtty::test::mouse_release(router, cell);
    Qtty::test::wheel(router, cell, 1);               // positive is up

Three things about a `MouseEvent` are silent when wrong, all measured
against a button and a scroll bar: **button 0 means *no* button** and
reaches nothing, a press with no release is not a click, and the wheel's
sign is a guess until somebody measures it -- `+1` took a scroll bar from
50 to 47, so positive is up. `button` is not an SGR number: the decoder
writes `1 + (b & 3)`, so the left button is `1`.

`QWidget::focusWidget()` on the window does answer, which is worth saying
straight after practice 10: it is `hasFocus()` on the widget, and
`QApplication::focusWidget()`, that are dead here. Qt keeps the window's
own idea of its focus widget; what it never sets is the ACTIVE window,
which is what the other two read.

**Move the focus with a key, not with `setFocus()`.** Focus here is the
router's, and `setFocus()` does not move it: the widget really does take
Qt's focus -- `focusWidget()` names it -- and **no focus mark is drawn
anywhere**, because the mark comes from the router's record. Measured on
a two-control form: `setFocus()` on the button gives zero reversed cells,
one `Tab` gives three. `clearFocus()` is the same trap pointed the other
way. A test that sets focus directly and then asserts on the screen is
asserting about a window nobody is in.

**And the terminal's own focus withholds the mark.** When the emulator
reports that it has lost focus, qtty stops drawing where the keystrokes
would go -- because they would be going somewhere else. Measured: the
same three reversed cells become none, and come back when it returns.
`qtty-replay` spells it `focus off` and `focus on`, so a report about a
missing mark is reproducible.

**Ten questions the library will answer about your window**, so that a
test can assert on them rather than a person noticing:

| call | what it returns | what to assert |
|---|---|---|
| `Qtty::keyboard_reachable(scope)` | the widgets `Tab` visits, in order | your controls are in it |
| `Qtty::pointer_only(scope)` | the controls no key reaches | empty (Qt's own furniture aside) |
| `Qtty::mnemonic_conflicts(scope)` | the `Alt`+letters two controls claim | empty |
| `Qtty::shortcut_conflicts(scope)` | the chords two things answer at once | empty |
| `Qtty::conventions_shadowed(scope)` | the convention rows your own keys took | empty, or drop those rows |
| `Qtty::tab_order_anomalies(scope)` | the tab steps that read backwards | empty |
| `Qtty::hover_only(scope)` | the words only a hover would reveal | empty |
| `Qtty::focus_invisible(scope)` | the controls that look the same focused | empty |
| `Qtty::sheet_styled(scope)` | the widgets a style sheet is drawing | empty |
| `Qtty::ambiguous_chords(scope)` | the chords a terminal cannot deliver | empty |

**Nine of the ten are asserted EMPTY, and that is the property worth
having.** A list you check by name needs updating every time the window
grows a control; an empty assertion needs nothing, and goes red the day
somebody adds one that collides or that only a mouse can press. Each of
them walks the router's own tables rather than a second copy, so what
they report is what the keys will do.

**Ask all nine of those in one line, and do not write the list
yourself:**

```cpp
QVERIFY(Qtty::audit(&window).isEmpty());
```

`Qtty::audit()` returns every row the nine empty reports would, each
named with the question that produced it, so a failure says which one
without your test enumerating any.

**Before you have written a test, print it.** The rows carry the
question and the finding, so the first run of a window you are still
building tells you what it is missing:

```cpp
for (const auto &[question, found] : Qtty::audit(&window))
    qWarning("%s: %s", qPrintable(question), qPrintable(found));
```

That is the same call the assertion makes, so what you fix at the
keyboard is what the test will stop reporting -- and `qWarning` is the
right spelling here rather than `printf`, which would land in your
frame. `keyboard_reachable()` is left out
because it is the one asserted *non*-empty -- a window with no controls
would otherwise pass by having nothing to report.

**The reason to prefer it is drift, and it is measured rather than
supposed.** This project's own example asserted eight of these while
this page said nine, the ninth having been added to the page and not to
the block; and adding the tenth meant editing every place that had
written the list out. Your test is the same shape with nobody to
notice. The table above is how you learn what is being asked; the one
line is what goes in the test.

**Two more the same test wants, shaped differently.** They are not in the
table because neither takes a scope: one is a process-wide counter and the
other reads a rendered frame.

**Did every widget land on the character grid?** A widget whose geometry
is not a whole number of cells renders smeared across the boundary, and
the guard that notices reports where it happens rather than several
layers away.

    QVERIFY(Qtty::GridGuard::installed());          // the guard is watching
    QCOMPARE(Qtty::GridGuard::violations(), 0);     // and saw nothing

**Both lines, and the first is not ceremony.** `violations()` returns a
count, and zero means *either* that every geometry was on the grid *or*
that nothing was looking -- `Qtty::setup()` installs the guard inside
`#ifndef QT_NO_DEBUG`, so a release build has none. Measured on one: a
window with a deliberately off-grid child reported **0**, and the same
binary with `GridGuard::install(app)` called by hand reported **2**. Call
`install()` yourself in a test if you build it in release.

**Do your colours stay legible at the terminal's depth?** Contrast is
decided after the mapping down to whatever the terminal has, so a pair
that clears the floor in true colour can fail once quantised to sixteen.

    Qtty::CellBuffer frame(40, 12);
    Qtty::render_once(win, frame);
    QCOMPARE(Qtty::contrast_violations(frame, Qtty::capabilities().color), 0);

It counts the cells whose foreground and background are closer in
luminance than the floor. Taking the depth from `Qtty::capabilities()`
rather than naming one asks about the terminal you are actually on, and
outside a run it answers the sixteen-colour default -- which is the
strict case, so a test gets the pessimistic answer for free.

A window drawn in the default theme answers 0 at every depth, because
the theme leaves the terminal's own colours alone; it is the colours
*you* pick, which practice 14 is about, that this measures.

**There is a snapshot harness for what the screen shows.**
`qtty/testing.h` ships with every install:

    #include <qtty/testing.h>

    const QString got = Qtty::test::snapshot_of(win, 40, 12);
    QVERIFY(!Qtty::test::check_snapshot(MY_SOURCE_DIR, "login", got));

`MY_SOURCE_DIR` is yours to supply -- a compile define pointing at your
source tree, the way this project's own build passes `QTTY_SOURCE_DIR`.
The fixture then lives at `<root>/test/snapshot/login.txt`, which is
where `check_snapshot()` looks and what it rewrites when you record.

`snapshot_of()` renders a widget to text in one call -- **glyphs,
attributes, colours and the pictures a frame carries**, not glyphs
alone, which matters because a frame that stopped drawing a selection
compares equal to one that drew it if only the characters are kept. The
pictures are recorded as geometry -- where each one sits and how big it
is -- so an icon that vanished, moved or changed size shows up; a
*different* picture of the same size in the same place does not, because
the image's identity is a hash of its pixels and that changes with the Qt
version and the icon theme rather than with your code.

**For a screen with a layer on it, use `snapshot_of_screen()` instead.**

    const QString got = Qtty::test::snapshot_of_screen(win, router, 40, 12);

`snapshot_of()` renders the widget it is given, which is right for a
control and wrong for everything a layer covers. Measured with a `QMenu`
popped over a window: it shows the button the menu is sitting *on* and no
menu at all, so a fixture taken that way is a picture nobody sees -- and a
menu is the commonest thing to get wrong. `snapshot_of_screen()` composes
the window, then the menus, drop-downs, tooltips and modal dialogs on top
of it, and applies the small-terminal policy the way a frame loop does.

**The router is an argument because it is not optional.** The popup stack
is the router's, collected from the layers' own show events, so a
compositor built without one draws the window alone -- and neither answer
reports anything wrong. It is the same router your key-driving test
already has.

It also records **where the caret is**, which the cells cannot say -- the
cell and the shape, or `hidden`. A frame that lost its caret, or put it in
the wrong cell, or showed a block where it had shown a bar, compares equal
to one that did not if only the cells are kept.

Two things to know. It composes the **screen**, not the window you pass:
a second visible top level puts a tab strip in the frame and leaves the
first one current, so close or scope the windows you are not
snapshotting.

And it takes the **window tab record** down as it goes, which two things
depend on: a press in row 0 is no longer read as a tab selection, and
`F6` has no window list to move within, so it does nothing and reads as
a broken feature. Measured: two windows with the conventions on, `F6`
between snapshots, and the current window never changed; with one
compositor kept alive across the presses -- as a frame loop keeps one --
`F6` moved and `Shift+F6` came back. Keep a `Compositor` alive across
keys that depend on the window set, or call
`Qtty::set_current_window()` to say which window you mean. `check_snapshot()` compares that against a
fixture under `<root>/test/snapshot/`, prints both sides on a mismatch,
and rewrites the fixture when you pass `record = true` -- so capturing a
screen before you change it is one call and a flag.

### One test that uses them together

Assembled rather than left as fragments, since each answers a different
question and an application wants all of them:

    void LoginTest::terminal()
    {
        LoginWindow win;                       // your ordinary Qt window
        win.setAttribute(Qt::WA_DontShowOnScreen);
        win.show();
        QCoreApplication::processEvents();

        // 1. Every control is reachable without a mouse (practice 4).
        const auto reach = Qtty::keyboard_reachable(&win);
        QVERIFY(reach.contains(win.user()));
        QVERIFY(reach.contains(win.password()));
        QVERIFY(reach.contains(win.okButton()));

        // 2. The keys do what you think: Tab from the user field lands
        //    on the password field rather than somewhere else.
        Qtty::InputRouter router(&win);
        win.user()->setFocus();
        router.on_key({Qt::Key_Tab, "\t", false, false, false});
        QCoreApplication::processEvents();
        QCOMPARE(win.focusWidget(), win.password());

        // 3. No two controls claim one key (practice 1). Empty is the
        //    assertion; the lists are for the day it is not.
        QVERIFY(Qtty::mnemonic_conflicts(&win).isEmpty());
        QVERIFY(Qtty::shortcut_conflicts(&win).isEmpty());

        // 4. And nothing is left that only a mouse could press
        //    (practice 4 from the other side). A window with a closable
        //    tab or a dock widget will name Qt's own buttons here until
        //    you give those actions a key of your own.
        QVERIFY(Qtty::pointer_only(&win).isEmpty());

        // 5. And the rest of the window's questions, each of them empty
        //    and each staying empty as the window grows.
        QVERIFY(Qtty::tab_order_anomalies(&win).isEmpty());
        QVERIFY(Qtty::hover_only(&win).isEmpty());
        QVERIFY(Qtty::focus_invisible(&win).isEmpty());
        QVERIFY(Qtty::conventions_shadowed(&win).isEmpty());

        // 6. And it still LOOKS right, attributes included.
        const QString got = Qtty::test::snapshot_of(win, 40, 12);
        QVERIFY(!Qtty::test::check_snapshot(MY_SOURCE_DIR, "login", got));
    }

**The first two are the ones people skip and the ones that rot.** Tab
order follows construction order until somebody inserts a widget, and
nothing about that edit looks like it touched the keyboard; a mnemonic
collision arrives the day a second `&S` is typed, and the control that
stops answering is not the one that was edited. The snapshot fails loudly
when it breaks. A control quietly leaving the tab chain, or quietly losing
its letter, does not.

`Qtty::NullBackend` captures frames the same way for what the screen
*shows*. Between them an application can assert that every control it
owns is reachable by key, in a unit test, on a machine with no terminal.

**The check worth writing first**: assert that every control you own can
be reached without a mouse.

    const QVector<QWidget *> reach = Qtty::keyboard_reachable(&window);
    QVERIFY(reach.contains(apply_button));

`keyboard_reachable()` returns the widgets `Tab` reaches inside a scope,
in the order it reaches them. This paragraph used to say the loop takes
ten lines and to write it yourself; that was wrong, and wrong in the
direction that hides the fault. Walking `nextInFocusChain()` by hand
lists widgets that are hidden, disabled, outside the window, or whose
focus policy excludes `Tab` -- none of them stops. A test built on that
walk reports a control reachable that a person cannot get to, which is
the one answer it exists to rule out. The function is the traversal the
router itself moves through, so what it lists is what will happen.

**The complement asks a different question, and one control type can
answer it.** `Qtty::pointer_only()` returns the buttons in a scope that
no key reaches -- visible, enabled, and claimed by nothing: not `Tab`,
not a mnemonic, not a chord, and not any of those on an action the button
carries.

    for (QWidget *w : Qtty::pointer_only(&window))
        qWarning("no key reaches %s", w->metaObject()->className());

The population is four kinds, and each is Qt's own word for something
rather than a judgement about which of your widgets matter -- that is why
this one is offered and a general `unreachable_controls()` is not:

| Kind | What a pointer acts on |
|---|---|
| `QAbstractButton` | the control you click |
| `QSplitterHandle` | the one you drag, and it answers no key anywhere |
| `QHeaderView` showing a sort indicator | a **section**, not a widget |
| `QLabel` holding an anchor, without `LinksAccessibleByKeyboard` | an **anchor**, not a widget |

The last two are why the list is worth stating: what a click acts on there
is not a widget at all, so a report whose population is widgets answers
empty on a window whose only link or only sort a keyboard user cannot
reach. Practice 4 above walks the seven instances Qt ships and is the
place to read for what each one looks like; this table is only the
population, and the two are held to the same four by a check. The subtraction is the part you cannot write yourself: a toolbar's
button is `Qt::NoFocus` and in no tab chain, and `&Save` on the action
behind it reaches it perfectly well, so a sweep of the focus chain alone
reports a fault that is not there.

Empty is the assertion -- **unless you use a closable tab or a dock
widget**, in which case Qt's own buttons are in the list until you give
the same action a key. That is not noise: it is practice 4's own note
about Qt's furniture, arriving as a list you can act on.
