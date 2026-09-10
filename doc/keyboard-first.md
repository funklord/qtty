# Keyboard-first: writing a Qt application that a terminal user can drive

qtty renders an unmodified Qt Widgets application on a character-cell
terminal. Rendering is the easy half. The half that decides whether the
result is *usable* is the keyboard, because a terminal user often has no
mouse at all -- over ssh, in a console, on a server -- and a control that
can only be clicked is a control that does not exist.

This is the guide for that. Where a behaviour is Qt's own it says so, and
where it is qtty's it says which.

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
| `Menu`, `Shift+F10` | Opens the focused widget's context menu, honouring its `contextMenuPolicy` | qtty's |
| `Ctrl+C`, `Ctrl+D` | Quit -- except in a widget that takes text, where `Ctrl+C` is left for copy. Change them with `InputRouter::set_quit_keys()` | qtty's |

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

## Moving between pages and windows

A terminal interface is layers, so getting BETWEEN them is half of using
it. Measured:

| Key | What happens | Whose |
|---|---|---|
| `Ctrl+Tab`, `Ctrl+Shift+Tab` | Move between a `QTabWidget`'s tabs, from anywhere inside a page | Qt's |
| Arrows on a focused tab bar | Move between tabs | Qt's |
| `Ctrl+PageDown`, `Ctrl+PageUp` | Move between tabs, and wrap at the ends | qtty's, opt-in |
| `F6`, `Shift+F6` | Move between top-level windows | qtty's, opt-in |

The last two are part of the opt-in below. `Ctrl+PageUp`/`PageDown` is
what somebody coming from a browser, an editor or a multiplexer reaches
for, and Qt does not give it to a tab widget. `F6` matters more: multiple
top-level windows are reachable through `Qtty::next_window()` and
`Qtty::previous_window()`, and **until the conventions are asked for
nothing binds a key to them at all** -- a second window could be on the
screen and unreachable without a mouse.

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
| `Alt` + a tab's letter | Switch to that tab |

The first two are the ones that **differ** from Qt rather than merely
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
own; twelve concerns styling a standard one. They sit last because that
is where the material belongs, not because they matter least. If you are
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

**3. Make the tab order the reading order.** Qt's default is construction
order, which is usually right and silently is not after a refactor.
`QWidget::setTabOrder()` fixes it. On a terminal this is the *only*
ordering a user experiences -- there is no glancing across to the field
they wanted.

**4. Never let an action be reachable only by pointer.** A right-click
menu, a hover reveal, a drag: each needs a keyboard route beside it. Put
the same action in a menu, give it a shortcut, or both. `QAction` in a
`QMenu` gets you a mnemonic and a shortcut at once.

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

**What you get for free, once there is more than one window**: qtty draws
a window strip along the top row, every window named, the current one in
brackets -- so a person can see which one they are in and click a tab to
change it, as well as pressing `F6`. This is the terminal's answer to a
task bar, and it is better than the window title for the purpose, since
it names all of them rather than only the one you are looking at.

**It costs a row, and that is worth knowing before it surprises you.**
The strip takes the top row and everything below moves down by one, so a
layout that exactly filled the terminal loses its last row the moment a
second window opens. With one window there is no strip and no cost.

**7. Do not depend on hover or tooltips.** Information a user needs must
be visible or reachable by key.

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

**9. In a custom widget, ignore keys that carry `Alt`.** qtty withholds
the letter from widgets Qt marks as taking text, which covers every
standard input widget. A widget of your own that reads
`QKeyEvent::text()` directly is outside that, and will see the `z` of an
`Alt+Z` that was meant for a menu. One line:

    if (event->modifiers() & Qt::AltModifier) { event->ignore(); return; }

**10. In a custom widget, draw your own focus mark -- and do not ask
`hasFocus()`.** This is the one that bites hardest, because the desktop
build hides it.

qtty shows focus as reverse video on the control's own glyph: a push
button's brackets, a check box's brackets, a slider's handle, a scroll
bar's thumb. A line edit is deliberately left alone, having a real caret,
which says where typing goes as well as that it goes here. **The style
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

On a terminal this matters more than on a desktop. There is no pointer
hovering near the thing a person is about to use, and no window manager
drawing a focus ring: **the only way to know where a keystroke will land
is the mark on the screen.** A control that cannot show it is a control a
keyboard user has to find by trial.

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
a sheet replaces it. If you style for the desktop, set the sheet in the
GUI branch of your frontend -- the same place the terminal-only pieces
go, one section down.

**13. Prefer stepping to dragging.** A splitter, a slider and a scroll bar
all respond to arrows when focused, but only if a user can reach them.
Give a splitter a keyboard route or a menu action that sets the split;
"drag the handle" is not available to everybody.

## Copy and paste

**Copy needs no code.** The ANSI backend watches `QClipboard`, so an
ordinary `QClipboard::setText()` reaches the terminal's own clipboard as
OSC 52 and the application never learns a backend exists.

**A copy is written whole or not at all.** Past a size limit nothing goes
out, deliberately -- a truncated copy the user believes went out is worse
than a refused one. The same holds when stdout is not a terminal, and
while the terminal is suspended.

**Today you cannot tell that it was refused.** The watcher discards the
result, and neither the limit nor the writing call is in an installed
header, so an application cannot ask before offering a Copy of something
large. That is a gap rather than a design: `project.md` 0b carries the
question of whether the limit belongs in the public API.

PRIMARY -- what a middle click pastes -- is unreachable through Qt here.
Under the offscreen platform `QClipboard::supportsSelection()` is false
and Qt refuses `setText(.., QClipboard::Selection)` outright.

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

Four things a standard Qt widget gets and yours does not. **Each is one
line, each fails silently, and each fails only on the terminal** -- the
desktop build hides all four, which is what makes them worth collecting
in one place rather than leaving scattered above.

| Do this | Or else |
|---|---|
| `event->ignore()` for a key carrying `Alt` (practice 9) | you eat the `z` of an `Alt+Z` that was meant for a menu |
| Draw a focus mark, asking `Qtty::focusWidget()` (practice 10) | nothing marks you, and `hasFocus()` is permanently false here |
| `setAttribute(Qt::WA_InputMethodEnabled)` if you edit text (practice 11) | `Ctrl+C` quits instead of copying, and no cursor is placed on you |
| Fold pasted newlines if you are single-line (*Copy and paste*) | you get the raw ones: the fold is by type and your type is not on the list |

None of these is a limitation of the library so much as the price of Qt
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

There is deliberately no `unreachable_controls()`. Naming what *should*
have been reachable means deciding which widgets are controls, and that
is a guess; a list of what *is* reachable is a measurement. You know
which of your widgets matter, so assert on those.
