# Keyboard-first: writing a Qt application that a terminal user can drive

qtty renders an unmodified Qt Widgets application on a character-cell
terminal. Rendering is the easy half. The half that decides whether the
result is *usable* is the keyboard, because a terminal user often has no
mouse at all -- over ssh, in a console, on a server -- and a control that
can only be clicked is a control that does not exist.

This is the guide for that. Everything in it was measured against this
library rather than assumed; where a behaviour is Qt's own it says so, and
where it is qtty's it says which.

## The model a terminal user already has

A terminal interface is a stack of layers rather than a plane of windows,
and people navigate it with three ideas:

- **Enter goes IN.** It opens the thing under the cursor, accepts the
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
| `Enter` | Fires the dialog's **default** button, wherever focus is | Qt's |
| `Alt` + letter | Reaches a menu, a toolbar action, a **button**, or the field a **label** is the buddy of | qtty's |
| `Ctrl+C`, `Ctrl+D` | Quit. Change them with `InputRouter::set_quit_keys()` | qtty's |

The `Alt` row is qtty's because a terminal delivers keys as bytes and
nothing here ever reaches Qt's shortcut map: the router matches mnemonics
itself. Menus and actions worked from the start; buttons and label buddies
were added later, and an application written for the desktop gets them
without knowing.

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

**A tab's mnemonic does not switch to it.** `Alt+S` on a tab labelled
`&Second` does nothing: the mnemonic search covers menus, actions,
buttons and label buddies, and a tab is none of those. Whether it should
is an open question in `project.md` §0b; until it is answered, do not
rely on tab mnemonics.

## The two conventions that differ, and how to ask for them

Two terminal habits are not Qt's behaviour, so qtty does **not** turn them
on by itself:

    Qtty::set_keyboard_conventions(true);

- **Enter activates the control that has focus.** On a desktop Enter
  fires the *default* button wherever focus is, and a focused button
  answers to `Space`. On a terminal Enter is the "do it" key.
- **Up and Down move between controls.** On a desktop they move *within*
  one, and between controls only `Tab` does -- which is reasonable when a
  mouse is always available and unreasonable when it is not.

It also binds the two navigation keys in the table above, `Ctrl+PageUp`
and `Ctrl+PageDown` for tabs and `F6` for windows.

**Both fire only where the focused widget ignored the key.** A text field
keeps its own `Enter`, a list keeps its own arrows, a slider keeps its
own. So turning them on cannot take a key away from a control that wanted
it.

It is off by default because this library's promise is that an unmodified
application renders faithfully, and an application that has bound `Enter`
or `Down` itself must keep them. If you are writing for a terminal,
turn it on: it is one line, and it is what makes a form walkable.

## Practices, in the order they matter

**1. Give every control a mnemonic, and every field a labelled buddy.**

    auto *apply = new QPushButton("&Apply");
    auto *label = new QLabel("&Host");
    label->setBuddy(hostEdit);

This is the single highest-value thing in this document. A mnemonic turns
"tab past four fields" into one keystroke, and it is the only way to reach
a control *directly*. It costs one character.

**2. Give every dialog a default button.**

    buttons->button(QDialogButtonBox::Ok)->setDefault(true);

`Enter` then commits from anywhere in the dialog, which is what a person
expects, and it works with no opt-in.

**3. Make the tab order the reading order.** Qt's default is construction
order, which is usually right and silently is not after a refactor.
`QWidget::setTabOrder()` fixes it. On a terminal this is the *only*
ordering a user experiences -- there is no glancing across to the field
they wanted.

**4. Never let an action be reachable only by pointer.** A right-click
menu, a hover reveal, a drag: each needs a keyboard route beside it. Put
the same action in a menu, give it a shortcut, or both. `QAction` in a
`QMenu` gets you a mnemonic and a shortcut at once.

**5. Leave a way back from every layer.** Modal dialogs and menus answer
`Esc` already. A layer of your own -- a page in a `QStackedWidget`, an
inline editor, a mode -- does not, and a user who cannot get back is
stuck in a way a mouse user never is.

**6. Reach every window.** qtty binds **no** shortcut of its own beyond
the quit keys, because any key it took by default would be one an
application could not use. So a second top-level window is unreachable
until either you bind `Qtty::next_window()` yourself or you turn on the
conventions, which put it on `F6`. Do one of the two: a window nobody can
get to is worse than one that was never opened.

**7. Do not depend on hover or tooltips.** A terminal has no pointer to
rest, and qtty does not send `QEvent::ToolTip` today. Information a user
needs must be visible or reachable by key.

**8. Say what the keys are.** A status bar line, a `?` page, a footer of
hints -- a terminal user cannot discover a binding by looking for a
button. This costs one `QLabel` and is the difference between an
application people can use and one they can use *if somebody tells them*.

**9. Prefer stepping to dragging.** A splitter, a slider and a scroll bar
all respond to arrows when focused, but only if a user can reach them.
Give a splitter a keyboard route or a menu action that sets the split;
"drag the handle" is not available to everybody.

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

**The check worth writing first**: focus every control in turn with `Tab`
and assert you reach them all. It is a loop, it takes ten lines, and it
fails the day somebody adds a widget that cannot be reached.
