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
  has the five things a standard one gets and yours does not; practices 9
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
  terminal**, which lists the eight questions this library will answer
  about a window, seven of them asserted empty.

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
that is where the material belongs, not because they matter least -- only
thirteen, which is ordinary advice again, comes after them. If you are
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

`Qtty::pointer_only()` names all six, and the section on testing says
how to read that list.

A right-click menu is the exception you get for free: `Menu` and
`Shift+F10` open it, and your `contextMenuPolicy` is honoured exactly as
on a desktop. That was not true until the library was measured against
this very practice -- the mouse route had been supplied and the keyboard
one had not. A hover reveal and a drag still need a route you provide.

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

It lists **what qtty answers right now** -- the five conventions when
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

    if (Qtty::focusWidget() == this) drawFocusMark();

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
It will land in your frame and stay there. Use Qt's logging and it is
taken care of; write to stdout yourself and nothing can help you. In a
dual-frontend application this is the easiest trap here to fall into,
because the identical line is harmless in the GUI build -- which is the
same reason every trap on this page is a trap.

## If you are writing a custom widget

Five things a standard Qt widget gets and yours does not. **Each fails
silently, and each fails only on the terminal** -- the desktop build hides
all five, which is what makes them worth collecting in one place rather
than leaving scattered above. The first four are one line each; the fifth
is a fact about the grid.

| Do this | Or else |
|---|---|
| `event->ignore()` for a key carrying `Alt` (practice 9) | you eat the `z` of an `Alt+Z` that was meant for a menu |
| Draw a focus mark, asking `Qtty::focusWidget()` (practice 10) | nothing marks you, and `hasFocus()` is permanently false here |
| `setAttribute(Qt::WA_InputMethodEnabled)` if you edit text (practice 11) | `Ctrl+C` quits instead of copying, and no cursor is placed on you |
| Fold pasted newlines if you are single-line (*Copy and paste*) | you get the raw ones: the fold is by type and your type is not on the list |
| Put your text lines at least `Qtty::GridMetrics::ch()` apart | two lines closer than a cell row share one, and the later one wins -- measured in Qt's own `QCommandLinkButton`, whose title and description sit 14 pixels apart and whose title therefore vanishes |

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
    router.on_key({Qt::Key_Tab, "\t", false, false, false});
    QCoreApplication::processEvents();
    // ... assert on window.focusWidget()

`QWidget::focusWidget()` on the window does answer, which is worth saying
straight after practice 10: it is `hasFocus()` on the widget, and
`QApplication::focusWidget()`, that are dead here. Qt keeps the window's
own idea of its focus widget; what it never sets is the ACTIVE window,
which is what the other two read.

**Eight questions the library will answer about your window**, so that a
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

**Seven of the eight are asserted EMPTY, and that is the property worth
having.** A list you check by name needs updating every time the window
grows a control; an empty assertion needs nothing, and goes red the day
somebody adds one that collides or that only a mouse can press. Each of
them walks the router's own tables rather than a second copy, so what
they report is what the keys will do.

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
attributes and colours**, not glyphs alone, which matters because a frame
that stopped drawing a selection compares equal to one that drew it if
only the characters are kept. `check_snapshot()` compares that against a
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

The population is `QAbstractButton`, which is Qt's own word for a control
that answers a click rather than a judgement about which of your widgets
matter -- that is why this one is offered and a general
`unreachable_controls()` is not. The subtraction is the part you cannot
write yourself: a toolbar's button is `Qt::NoFocus` and in no tab chain,
and `&Save` on the action behind it reaches it perfectly well, so a sweep
of the focus chain alone reports a fault that is not there.

Empty is the assertion -- **unless you use a closable tab or a dock
widget**, in which case Qt's own buttons are in the list until you give
the same action a key. That is not noise: it is practice 4's own note
about Qt's furniture, arriving as a list you can act on.
