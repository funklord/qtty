# qtty

Read `~/.claude/CLAUDE.md` for prime directives.

This document is authoritative. Where it and the code disagree, the
document wins -- but the disagreement is raised rather than silently
resolved in either direction. §8 is the list of disagreements standing
today; none of them is settled there, and none should be settled in
passing.

`doc/design.md` is the design in full: 1300 lines, self-consistent, and
deliberately not duplicated here. **This file is the project record** --
what qtty is, what has been decided, what is built, what is not, what is
open, and how to work in the tree. Where design.md holds the detail, this
document states the substance in a sentence or two and cites the section
number rather than restating it.

## 0. What this is

**Render an unmodified Qt Widgets application on a character-cell
terminal.** The same `QWidget` tree that produces the desktop GUI produces
the TUI; there is one view codebase, not two dialects of one.

It does that without forking Qt and without a custom QPA platform plugin.
Four public seams carry the whole design (design.md §1):

| Concern | Mechanism | API stability |
|---|---|---|
| Geometry on a cell grid | a `QStyle` subclass returning cell-multiple metrics | public |
| Widget lifecycle without a window | `Qt::WA_DontShowOnScreen` plus the in-box `offscreen` platform plugin | public API, *undocumented behaviour* -- design.md §5.6 |
| Rendering to cells | a custom `QPaintDevice` / `QPaintEngine` | public |
| Terminal I/O | `ITerminalBackend`, ours | ours |

The secondary purpose is consolidation: four products each carry an
independently written TUI, and the terminal-level work in them is done
four times over. What is missing in all four is the layer above it --
widget hierarchy, layout, focus, event propagation -- which is exactly
what Qt already implements. design.md §3 records the alternative that was
weighed and rejected as the primary path (adopting Tui Widgets or
terminalgui), and design.md §13.2 keeps it as the documented fallback.

### 0.1 Identity

- **Name:** `qtty`, lowercase in every file-system context -- repository,
  library, include path, package, binary. In prose, "Qtty" (the Git/git
  pattern).
- **C++ namespace:** `Qtty::`. Capitalised, matching the Qt-ecosystem
  mould (`Qt::`, `KIO::`, `QXlsx::`) and the KDE repo/namespace split.
  Never `QTty`, and never Q-class styling -- that spelling belongs to
  Qt's own classes.
- **Macros and environment variables:** `QTTY_*` (`QTTY_GRAPHICS`,
  `QTTY_VERSION_STRING`, `QTTY_NO_TUI`).
- **String namespaces:** `"qtty.*"` for dynamic properties and settings
  keys (`"qtty.cells"`, `"qtty.glyph"`); `"org.qtty.*"` for
  `Q_DECLARE_INTERFACE` identifiers.
- **Version:** one place, the `VERSION` file, currently **0.1.0**.
  `qtty.pri` reads it and `make version-check` asserts that it still
  does, because a second copy of a version number is how the two drift.

**qtty is not affiliated with or endorsed by The Qt Company. "Qt" is a
trademark of The Qt Company Ltd.** The descriptive form -- "qtty:
terminal rendering for Qt applications" -- is the one to use, and is the
trademark fair-use shape.

### 0.2 Goals

- **G1.** A `QWidget`-based screen compiles once and runs as both GUI and
  TUI.
- **G2.** The GUI remains the primary target. No GUI regression is
  acceptable as the price of terminal support; qtty must be inert when
  not active.
- **G3.** The four existing terminal implementations consolidate behind
  one backend interface.
- **G4.** Deterministic, snapshot-testable rendering in CI with no tty
  attached.
- **G5.** No Qt private headers and no patched Qt. One documented
  exception: `WA_DontShowOnScreen`'s lifecycle behaviour is public API
  but undocumented behaviour (design.md §5.6).

### 0.3 Non-goals

- **N1.** Qt Quick / QML. The cell model has no meaningful mapping to a
  scene graph.
- **N2.** Pixel fidelity. The TUI is a legible reinterpretation, not a
  screenshot.
- **N3.** Automatic beauty. A screen designed for 1920x1080 needs
  adaptation work (design.md §7); qtty makes that work small, not zero.
- **N4.** Arbitrary third-party widgets. Coverage is declared by an
  explicit support matrix (design.md §8.4), not discovered.

## 1. Architecture in brief

design.md §4 carries the diagram; §5.1 to design.md §5.7 carry the layer
specifications. The summary:

- **L1 -- terminal backend** (design.md §5.1). Owns the tty and nothing
  else: no diffing, no layout, no widget knowledge. `ITerminalBackend`
  plus `Capabilities`, `ITerminalEventSink`, and the optional
  `IGraphicsOutput` for terminals that can accept pixels. Escape decoding
  deliberately stays *inside* each backend, because that is where the
  four implementations differ most and where their accumulated bug fixes
  live.
- **L2 -- cell model** (design.md §5.2). `Cell`, `Attr`, `Color`,
  `CellBuffer`, and `diff()`. A cell holds a **grapheme cluster**, not a
  code point, because a user-visible character may be a base plus
  combining marks or a ZWJ emoji sequence. Diffing lives here so that all
  backends inherit one optimizer.
- **L3 -- grid metrics** (design.md §5.3). One cell is exactly `cw x ch`
  device pixels, so `geometry().x() / cw` *is* the column index. Rounding
  happens once, at the style, on the way in -- never at read time.
- **L4 -- style and paint** (design.md §5.4). `GridStyle : QProxyStyle`
  grids the layout engine and the widgets together, because `QStyle`
  supplies layout margins and spacing as well as widget metrics.
  `CellPaintDevice` / `CellPaintEngine` catch everything that reaches
  `QPainter` without passing a style hook.
- **L4.5 -- GraphicsPlane** (design.md §5.7). Pixel overlays over the
  cell UI, and cell-anchored image placements that scroll with text.
  Three delivery strategies chosen by `Capabilities::graphics`; the
  fallback tier is a pure L2 transform, so it reaches every backend
  through the ordinary `present()` with no backend changes.
- **L5 -- input** (design.md §5.5). `InputRouter` owns the shortcut
  table, focus, and modal/popup routing.
- **L6 -- runtime** (design.md §5.6). Environment preparation,
  `Compositor`, `FrameScheduler`, and the exec loop.

**Layers are strictly stacked, and this is a rule rather than a
description:** L1 knows nothing of Qt, L2 knows nothing of widgets, L3
and L4 know nothing of the tty. Only L6 sees everything. The stacking is
what makes the beerssh proposal in `doc/beerssh.md` §1 possible at all --
sharing L2 so that both ends of the pipe compute Unicode width from one
table and disagreement becomes impossible by construction.

### 1.1 The two-channel rendering model

design.md §5.4 calls this the central idea of the design, and it is.

**Channel A -- semantic, and where quality comes from.** `GridStyle`
knows *what* it is drawing. When `drawPrimitive(PE_FrameWindow, ...)` is
called, a window frame is wanted, and the style can emit box-drawing
characters straight into the cell buffer with correct corners, correct
joins, and correct title placement. Same for a checkbox (`[x]`), a
scrollbar (`^ # . v`), a menu item, a tab.

**Channel B -- generic, and the safety net.** Anything that reaches
`QPainter` without passing through a style hook -- a custom
`paintEvent()`, a third-party widget, `QTextDocument` output -- lands in
`CellPaintEngine` and is snapped to cells. It guarantees that *something*
legible appears for any widget; it guarantees nothing about how good it
looks.

So **the widget support matrix is really a statement about which widgets
have Channel A coverage** (design.md §8.4). That is the sentence to keep
in mind when reading §7 below.

Two details of Channel A are measured and both are counter-intuitive
(design.md §16, F1 and F2), and getting either wrong fails silently --
Channel A simply never fires and everything falls quietly through to
Channel B:

- **Detect the cell target through the paint *engine*, not the paint
  device.** Inside a `paintEvent` the painter's device is the `QWidget`;
  the redirection is invisible at that level.
- **Take position from the widget, not the painter.** Neither
  `QPainter::transform()` nor `combinedTransform()` carries the offset
  `QWidget::render()` applies. `w->mapTo(w->window(), ...)` is the way
  back to screen space.

And one more, from F4: **`State_HasFocus` is never set** in the TUI
build, because no window ever becomes active. Channel A takes focus from
`InputRouter` instead -- the one place shared style code legitimately
needs a target-specific branch.

The frame loop is deliberately explicit rather than backingstore-driven
(decision D5): the compositor calls `QWidget::render()` -- public API --
into one `CellPaintDevice`, then `CellBuffer::diff()` against the
previous frame yields the damage that `present()` ships.

### 1.2 The grid invariant

`GridMetrics::cw()` and `ch()` are set once, by `Qtty::setup()`, from the
measured application font. Everything in the Qt half of the system is
then expressed in pixels that are exact multiples of them. A fractional
device pixel ratio destroys that invariant silently, which is why the
protection is an assertion rather than an environment variable
(design.md §5.3: `QT_SCALE_FACTOR=1` is a *multiplier* on the native
ratio, so it forces nothing; the offscreen plugin reports DPR 1
regardless).

The component design.md §5.3 calls "the highest value-per-line component
in the project" is `GridGuard`: a global event filter asserting on every
resize and move that the widget's geometry is cell-aligned, converting a
class of subtle rendering bugs into a stack trace at the point of origin.
**It does not exist yet** (§7.1).

## 2. The hygiene contract

design.md §10.1, stated in brief. The reason it is a contract and not a
preference: qtty is a library that will be linked into applications that
already have their own `Cell`, `Overlay` or `Style`, and the entire clash
surface should be the frontend shim in `main.cpp`.

- **Everything public lives in `namespace Qtty`.** No types, functions or
  globals at global scope; no `using namespace` in any public header.
  (The spikes deliberately break this for brevity -- `CW`, `CH`,
  `g_qttyFocus`, an unqualified `Cell` -- and the library wraps all of
  it.)
- **No public macros** except include guards and, where genuinely
  needed, `QTTY_`-prefixed ones. Macros are the one C++ clash no
  namespace can fix.
- **Prefixed string namespaces** everywhere a string acts as an
  identifier: `"qtty.cells"`, `"qtty.glyph"`, `"org.qtty.*"` interface
  ids, settings keys.
- **Qt singleton ownership is declared, not grabbed.** In TUI mode qtty
  owns the application style, the application font, the QPA and scaling
  environment variables, and one global event filter. An application that
  must also touch those goes through qtty's API rather than
  `QApplication` directly. Note the wording on the style: an app's custom
  style is *not* lost, it becomes `GridStyle`'s proxy base -- which the
  code does not yet honour (§7.1).
- **In GUI mode qtty touches none of the above.** G2's inertness,
  restated as a testable rule: **a GUI build with the library linked but
  inactive must be byte-identical in behaviour to one without it.**

## 3. Decision record

design.md §15, reproduced because it is exactly the "record the options
rejected, not just the one chosen" rule and it belongs in the project
record.

| ID | Decision | Rejected alternative | Why |
|---|---|---|---|
| D1 | Reuse real QtWidgets classes | A parallel Qt-like TUI toolkit | One view codebase; sharing at 100%, not 80% |
| D2 | The in-box `offscreen` platform plugin | A custom curses QPA plugin | QPA has no source or binary compatibility guarantee; per-release maintenance |
| D3 | Semantic style channel plus paint-engine fallback | Paint engine only | The style knows *what* it draws; box-drawing and controls need that |
| D4 | Style-driven gridding | Subclass every widget | The style reaches Qt-internal children we never construct |
| D5 | Explicit `render()` on a damage schedule | Backingstore paint events | Public API, controls the frame rate, preserves Channel A -- at the cost of double painting |
| D6 | A backend interface with four adapters | Adopt termpaint immediately | Preserves accumulated bug fixes; consolidation becomes optional |
| D7 | One cell = `cw x ch` px with a bundled fixed-metric font | Sub-pixel metrics with rounding | Rounding once at the style beats rounding everywhere |
| D8 | No Qt fork | A patched qtbase | LGPLv3 publication duty plus a rebuild per release across four products |

D4 deserves the reason spelled out, because it is the decision the whole
library rests on: Qt constructs widgets internally that the application
never sees. `QComboBox` builds its own popup view and line edit,
`QAbstractItemView` builds editors through the delegate, `QTabWidget`
builds a `QTabBar`, `QScrollArea` builds scrollbars. A `GridButton`
subclass is never instantiated on any of those paths. The style is
consulted on all of them.

## 4. Open questions

Two of design.md §14's five are closed, by measurement rather than by
argument:

- **OQ-1 -- does a synthetic `QKeyEvent` participate in Qt's shortcut
  map? CLOSED, negative.** Measured across all three shortcut contexts
  and both delivery targets: **zero activations** (design.md §16, F3).
  Because no window is active, `QShortcutMap` never matches.
  `InputRouter` therefore owns its own table, built by walking `QAction`s
  from the active window and its children. Manual `QAction::trigger()` on
  a match works correctly, and the silver lining is that shortcut
  precedence becomes explicit and testable rather than depending on Qt's
  context rules.
- **OQ-2 -- does the offscreen plugin create backingstores for
  `WA_DontShowOnScreen` top-levels? CLOSED, affirmative.** It does, and
  that is the mechanism by which paint events keep flowing. Roughly 6 MB
  per top-level at 200x60 cells, plus one per popup and dialog, and
  widgets are painted twice per frame -- once into the backingstore Qt
  insists on, once into the `CellPaintDevice`. F9 then measured the cost
  and found it irrelevant (§6).

Open:

- **OQ-3.** Qt 5.15 and Qt 6 in one codebase, or Qt 6 only with the two
  laggard products upgrading first? Affects Phase 3 sequencing. See §8.1
  -- the tree has already been built one way while design.md says the
  other, which makes this the most urgent of the three.
- **OQ-4.** Do any of the four products need the TUI where QtWidgets
  cannot be linked (embedded, serial console)? If yes, that product goes
  to the design.md §13.2 fallback permanently and Phase 4's scope changes.
- **OQ-5.** Accessibility: is a `QAccessible` bridge worth it, given that
  the terminal already exposes text to screen readers when the cursor is
  placed correctly (design.md §5.5)?

And one raised by the tree rather than by the design:

- **OQ-7.** Which metric should the ANSI-16 *fallback* use? The primary
  route is the hand-authored role table and design.md section 6 is
  emphatic that it should be; the fallback exists only for a colour with
  no palette role behind it, which is Channel B output or a colour an
  application chose itself. It matches in **RGB**, while `to_xterm256()`
  was changed to match in **CIELAB** precisely because RGB nearest turns
  a saturated green into a grey.

  Measured before asking: over 24389 sampled colours the two metrics
  disagree on **44.1%** of them, so this is not a rounding difference.
  But which is *better* at sixteen colours is not obvious and the
  measurement does not settle it -- Lab is the standard for perceptual
  nearest, and on rgb(200,60,60) it picks the dark red where RGB picks
  the bright one, which is arguably the worse answer for a foreground.
  With no visual reference to arbitrate, changing 44% of fallback answers
  on my judgement would be a guess wearing a measurement's clothes.

  Recorded rather than changed. What would settle it: rendering a page of
  Channel B colours both ways in a real terminal and looking.

- ~~**OQ-6.** Are PascalCase type names inside `namespace Qtty` a settled
  exception to the global style rule?~~ **Closed 2026-08-26 by the
  copyright holder: yes, for type names and nothing wider.** See §8.5.
  It opened a follow-on that is not an open question but a piece of work:
  the members are still `camelCase` and the rule says they should not be.
  §11 carries it.

## 5. Risks

design.md §13, reproduced. L is likelihood, I is impact.

| # | Risk | L | I | Mitigation | Trigger to act |
|---|---|---|---|---|---|
| R1 | Popups do not composite acceptably | M | H | design.md §8.1; fall back to §13.2 | Phase 0 gate 2 -- **passed** |
| R2 | The offscreen plugin is documented as "only fully supported on X11" | H | H | §5.1 below; a scope decision is required before Phase 2 | Now |
| R2b | The offscreen plugin behaves differently across Qt versions, and is absent from some packaged builds | M | M | A CI matrix; vendor-check the plugin's presence at startup | Any version-specific snapshot diff |
| R3 | Font metrics are not integral on some backend | L | H | Startup assert; bundled font; a documented hard failure | The assert fires |
| R4 | Shared screens degrade the GUI to suit the TUI | M | H | G2 is a review rule; GUI snapshot tests; Tier 3 is encouraged | Any GUI regression traced to a Tier 2 hint |
| R5 | Channel B output is too poor for a heavily custom-painted product | M | M | Widen Channel A, or `ICellPainted` per widget | More than 30% of a screen's cells from Channel B |
| R6 | The effort exceeds the value of unifying four TUIs | M | M | Phase 1 ships value standalone; the phases are independently abandonable | Phase 2 slips more than 50% |

**R2b is live right now**, and nothing has been done about it: the Phase 0
measurements were taken on Qt 6.4.2 and this machine is **Qt 6.8.2**. The
suite passes here, but no snapshot in the tree has ever been compared
across two Qt versions, and there is no CI matrix to do it.

R3's mitigation is likewise not in place -- see §7.5.

### 5.1 Platform scope, and it must be decided before Phase 2

design.md §13.3. Qt's own documentation states the offscreen plugin is
*"only fully supported on X11"*. That is a material constraint on a
design whose foundation it is, and it forces an explicit scope decision:

- **If the TUI is a Linux/server feature** -- the likely case, since TUIs
  exist for ssh and headless operation -- this costs nothing. Declare
  Linux the supported target for the *terminal* build; GUI builds are
  unaffected on every platform.
- **If any product needs a Windows or macOS terminal build**, offscreen
  is not a safe foundation there, and that product goes to the fallback
  or gets a per-platform spike.

The constraint is on the plugin's *completeness*, not on Qt: it bounds
where the TUI runs, never where the GUI runs.

### 5.2 The fallback

design.md §13.2. If gate 2 had failed, or if R5 comes to dominate: adopt
**Tui Widgets** as the terminal view engine behind a thin facade, sharing
at the model/controller layer instead of the view layer. Phase 1's
backend work is not wasted -- Tui Widgets accepts a custom terminal
connection, so the backends still plug in. Gate 2 passed, so this is
insurance rather than a plan.

## 6. Phase 0: what was measured

Four spikes, roughly 700 lines, about three working days. They are kept
in `spike/` **exactly as run**, which is what makes them evidence for the
numbers design.md cites; the style gate exempts the directory for that
reason. design.md §16, §16.1, §16.2, §16.3 and design.md §16.4 are the record.

**Environment: Qt 6.4.2, `-platform offscreen`, a Linux container with no
X11, DejaVu Sans Mono at 16 px -- measured advance 10 px, height 19 px,
so cw=10 and ch=19.** Every number below belongs to that machine. **This
machine is Qt 6.8.2**, which is what makes R2b live rather than
theoretical.

**Gate 1, render a real dialog: PASS.** A `QDialog` with a `QCheckBox`,
two `QRadioButton`s, a `QComboBox`, a `QTreeWidget` with a header and a
scrollbar, and a `QDialogButtonBox`. Layouts activated normally, and
**11 of 13 child widgets came out exactly cell-aligned** with only the
style overrides listed in design.md §5.3. The two that did not are
`QHeaderView` and `QScrollBar` internals, which self-size (F5).

**Gate 2, popups: PASS, and easier than feared.** `QMenu::popup()` under
`WA_DontShowOnScreen` produced a live popup that
`QApplication::activePopupWidget()` reported correctly, that appeared in
`topLevelWidgets()`, and that composited over the dialog. A synthetic
press/release pair on `menu.actionGeometry(action).center()` triggered
the action. design.md §8.1's "highest risk" rating is over-stated for
menus; the residual risk is tooltips, nested submenus and off-screen
clamping.

**The five corrections.** F1: the painter's device inside a `paintEvent`
is the `QWidget`, so Channel A must detect through the paint engine. F2:
neither transform carries `render()`'s redirection offset. F3: synthetic
key events never reach `QShortcutMap` -- 0 hits. F4: no window activates,
so `qApp->focusWidget()` is null and `hasFocus()` false forever, but
`window->focusWidget()` and `focusNextPrevChild()` work perfectly. F5:
`QHeaderView` and `QScrollBar` ignore some style metrics.

**Confirmed as designed.** `QPaintEngine(AllFeatures)` prevents
text-to-path emulation: **11 `drawTextItem` calls against 1 `drawPath`**
on a full dialog. Declaring features narrowly would have gutted Channel
B's text handling, exactly as predicted. `devicePixelRatio() == 1` under
offscreen, unconditionally. Channel A reached **14 style-drawn elements**
on one dialog once F1 and F2 were fixed.

**Phase 0.5, F6 to F10.** Resize and reflow pass -- after resizes to
64x14, 30x8 and 80x24 cells all children re-land aligned and stretch
factors redistribute correctly. The boundary found is the layout
*minimum*: a resize below `minimumSizeHint()` is refused and content
overflows, so small terminals need a drop-optional-then-scroll policy
rather than faith in layout compression. `QComboBox`'s internal popup is
discoverable and drivable, but does **not** inherit
`WA_DontShowOnScreen`, so the runtime stamps it. `QTextEdit` through
Channel B came out **better than designed for**: with the document font's
line height equal to ch, plain text renders perfectly, which downgraded
it from "replace wholesale" to "replace the interaction layer if editing
proves bad". Frame cost: **a full `render()` of a dialog into 80x24 costs
0.16 ms**, and **one keystroke in a `QLineEdit` dirties exactly 1 cell of
400** -- two orders of magnitude inside the 16 ms budget, which is why
the frame loop tracks no damage regions at all and simply diffs full
frames. Focus injection works in a few lines.

**Phase 0.6, the graphics plane.** Rasterizing a 60x16 `CellBuffer` to a
600x304 px image with QPainter costs **3.8 ms**; `SourceOver`-blending an
overlay costs **0.2 ms**. Both non-native delivery paths run today in
`spike/spike3.cpp`, and the composited PNG is the exact frame a
sixel/kitty/iTerm2 encoder would ship.

**Phase 0.7, the chat view with scrolling stickers.** A vanilla
`QListView` + `QAbstractListModel` + `QStyledItemDelegate`, with **zero
`qtty::` types in the app-side code**. Placements track the text flow
exactly. Upload-once works as designed: **two unique
`QPixmap::cacheKey()`s across three frames and five sticker sightings**,
every subsequent sighting a re-place by id at **about 30 bytes on the
kitty protocol** rather than pixel retransmission. `QFrame::NoFrame` was
needed on the view, because the default frame offsets the viewport by
`PM_DefaultFrameWidth` in *both* axes and breaks row alignment.

**Phase 0.8, the shippable example.** The chat spike was promoted to
`example/chat/` and extended with the frontend-selection story:
`--tui`/`--gui`, then an `argv[0]` name suffix, then autodetection. The
one sharp edge is ordering, and it is documented in the file header:
`prepareEnvironment()` then `QApplication` then `setup()` then the shared
widgets then `exec()`.

**Verdict: proceed.** No gate failed, and the two genuine surprises (F3,
F4) cost a bounded amount of `InputRouter` code rather than threatening
the architecture.

**What Phase 0 did not test**, and still has not been tested: colour
quantisation against a real terminal, real terminal I/O, and
macOS/Windows offscreen behaviour. Vertical text centring within
multi-cell widgets is visibly off by one row in the gate 1 dump and needs
a baseline calibration pass that has not happened.

## 7. What is built, and what is not

Measured against the source on 2026-08-26. design.md §17 sets out three
tiers; this is where each one actually stands. **The tree builds clean and
the suite reports 108 PASS, 0 failures** on Qt 6.8.2 with the offscreen
plugin.

The one-sentence summary: **the graphics tier is the most complete, the
widget tier is broad but shallow, and the infrastructure tier -- which
gates both -- has four holes that block everything downstream.**

### 7.1 Infrastructure tier (design.md §17.1)

**L2 cell buffer, colour, damage -- done.** `src/core/cell_buffer.cpp`
and `src/core/color.cpp`. Grapheme clusters via `QTextBoundaryFinder`,
wide-cell lead and continuation with partner clearing in **both**
directions (the corruption case design.md §5.2 names), and a run-based
`diff()`. Three gaps:

- Quantisation is **RGB nearest, not CIELAB**, which design.md §6
  requires.
- `Attr` has no `Blink`, though design.md §5.2 lists it.
- `include/qtty/cell.h` includes `QPixmap`, so **L2 is not GUI-free.**
  That matters beyond tidiness: `doc/beerssh.md` §1 proposes beerssh link
  or vendor L2 so both ends compute Unicode width from one table, and the
  open question there is whether L2 can be taken with a QtCore dependency
  or has to be extractable as plain C++. Every GUI type reachable from
  `cell.h` makes that answer worse.

**InputRouter -- mostly done.** `src/runtime/input_router.cpp`: the
shortcut table (F3's mitigation), Tab and Backtab through
`focusNextPrevChild()` (F4's), popup attribute stamping (F7's), and a
synthetic press with a fabricated release. Missing:

~~Modal input is never dropped.~~ **Done.** `input_scope()` is the
active modal if there is one and the window otherwise, and every path
that used to reach for `win_` -- the shortcut table, the Tab chain, the
arrow-scroll fallback, the focus write-back -- goes through it. A click
outside the modal is now **dropped rather than redirected**, because a
click on a blocked window means nothing and delivering it to the dialog
would invent a press the user never made. It also stamps
`WA_DontShowOnScreen` on every late-created top-level rather than only
popups, which F7 measured for `QComboBox`'s internal container and which
applies equally to a dialog the application opens.

Still missing:

- ~~`onPaste` fabricates a key event with `key=0`.~~ **Examined and
  fixed**, and the key=0 event turns out to be right: a paste is text,
  not typing, which is the whole reason bracketed paste exists.
  Delivering the newlines as Return would fire the default button and
  submit a dialog halfway through a paste.

  What was wrong is what a single-line editor then held. Measured:
  pasting two lines into a `QLineEdit` left its `text()` containing a
  literal newline -- a state no user can type, that nothing downstream
  expects, and that renders as a stray glyph in a cell. Newlines fold to
  spaces for such a target, which is what a clipboard paste into the same
  field does, and a multi-line editor keeps the newline it can hold. The
  test is by type because Qt exposes no generic "accepts a newline"
  query; a third-party single-line editor still gets the raw text, which
  is a known edge rather than a hidden one.

  The path had no test at all and could not have had one until the
  backend learned to decode bracketed paste -- an implemented sink that
  nothing called, which is §7.4's theme exactly.
- ~~No mnemonic handling.~~ **Done**, and it could not have been built
  before the backend learned to decode Alt at all. A mnemonic cannot use
  the shortcut matcher: it arrives with text and **no `Qt::Key`**, since
  a terminal sends ESC then the letter, and `match_shortcut()` returns
  false on its first line for want of one. Alt with a menu's mnemonic
  opens that menu; with an item's, triggers it; `&&` marks nothing,
  because that is Qt's spelling for a literal ampersand.
- **Keys did not reach an open menu at all, and that is what building
  mnemonics turned up.** `key_target()`'s first branch asked
  `QApplication::activePopupWidget()`, which returns **null for every
  popup in this runtime**: the stamping filter sets
  `WA_DontShowOnScreen` as a popup is shown (F7), the platform never maps
  it, and Qt's open-popup list is driven by that mapping. So the branch
  could not fire, and Down and Return went to the widget behind the menu.

  Nothing looked wrong, which is the part worth keeping. The menu drew
  perfectly the whole time, because `Compositor` reads the **router's**
  popup stack rather than Qt's -- so the half of the system that was
  right disguised the half that was not.

  And design.md §16's gate 2 signed popups off as working: it sent a
  synthetic mouse press at `actionGeometry().center()`, which triggers an
  action without consulting `key_target()` at all. The measurement was
  sound and the conclusion drawn from it was wider than what it
  exercised -- the same shape as this project's own "nothing in that tree
  is bent" claim recorded in §9.8.

  `key_target()` reads the router's stack now. `activeModalWidget()` is
  unaffected and stays: Qt tracks a modal through `setWindowModality` and
  show, which stamping does not disturb.
- ~~No grab-widget branch.~~ **Done**, and it was half of a larger
  absence: `on_mouse()` handled the wheel, presses and releases and
  **never read `m.motion` at all**. The backend had been parsing it
  correctly the whole time -- SGR 1002 is enabled precisely because it
  reports drags -- and `MouseEvent` has carried the flag since the seam
  was written, so every drag event arrived at the router and was
  discarded. Nothing that needs a drag worked, which is why this entry
  and §7.2's splitter entry were one defect.

  A press now records its target and motion and release go to that
  widget wherever the pointer is. Qt calls this a mouse grab and takes it
  from the platform; there is no platform here, so the router keeps it,
  in a `QPointer` because a press can destroy its own target -- a button
  that closes a dialog -- and the release then arrives for a widget that
  is gone.

  The two halves fail separately, and that was measured rather than
  assumed. Removing motion delivery takes both a slider drag and a
  splitter drag red; removing only the grab leaves the slider passing and
  fails the splitter alone. A slider drag never leaves the slider, so
  moves are all it needs; a splitter handle is one cell wide and the
  pointer is off it after the first move.

  A third check was written and removed: a stray release far from the
  handle, which passes with the grab gone because the cell it names holds
  a `QLabel` and a release on a `QLabel` does nothing either way.

  **The same lens found a second field one along, and that one was worse
  than an absence.** `m.button` was parsed by the backend -- SGR 1006
  carries it and `AnsiBackend` writes 1 left, 2 middle, 3 right -- and
  `on_mouse()` sent `Qt::LeftButton` whatever arrived. So a right click
  did not fail to open a context menu; it **activated whatever it landed
  on**. Measured: a right click on a `QPushButton` emitted `clicked()`.

  The buttons are mapped now, and a right press also raises the
  `QContextMenuEvent` that a platform layer would normally synthesise --
  there is no platform here, so nothing had ever asked for one.
  `QWidget::event()` reads `contextMenuPolicy` from that event, so
  default, custom and actions policies all start working at once, and a
  menu opened from one is stamped and composited like any other popup.

  Worth keeping about the tests: the check that matters is
  **`hits == 0`**, not the one asking for a context menu. A suite that
  only tested for the menu would have read this as a feature nobody had
  written, when what was actually there was a misfire.

**Compositor and FrameScheduler -- done.** This was the largest
correctness gap in the tree: `compose()` rendered the tracked window and
the router's popup list, walked no top-levels, and drew no modal. Since
the tracking filter only noticed `Qt::Popup` and `Qt::ToolTip`, **a modal
`QDialog` was stamped `WA_DontShowOnScreen` and then drawn by nobody** --
it disappeared while still holding input, which is the worst of both.

`compose()` now walks `QApplication::topLevelWidgets()` as design.md
design.md §5.4 step 3 specifies, then stacks modals and popups explicitly on top,
which is §8.1's mitigation and is deliberately not a reading of window
flags -- there is no window manager here, so there is no z-order to read.
The cursor follows the layer that owns input, so a modal's text field
places the terminal cursor rather than the blocked window's.

Popups now **flip** rather than slide. §8.1's example is a menu opening
at x=78 that must flip left; `qBound` alone slid it along the edge
instead, which detaches a menu from the item it was opened from and can
cover that item. Flipping puts the far edge on the anchor; sliding stays
as the fallback for a layer too big to fit either side.

**GridStyle hardening -- partial.** `src/grid/grid_style.cpp`. The
`pixelMetric` audit is thorough: 35 or more metrics answered explicitly,
the rest snapped. Missing:

- ~~`sizeFromContents` is a blanket snap-up, not per-`ContentsType`.~~
  **Done, and it was the largest visible quality defect in the tree.**
  Snapping the proxy style's answer up is wrong in the direction that
  matters: Fusion sizes a control for a mouse. Measured at cw=10 ch=19
  before the fix -- `QCheckBox` and `QRadioButton` came out **38 px, two
  cells**; `QPushButton`, `QLineEdit` and `QComboBox` came out **57 px,
  three cells**. Only `QLabel` was right, and only because its height
  already equals the line height. So every dialog was two to three times
  taller than it needed to be, with blank rows between its controls.

  A single-line control is one cell tall by construction now, and the
  width still snaps up because a width is a count of characters and
  rounding one down truncates text. The types that genuinely carry more
  than a line -- an item view row, a group box, a whole menu -- keep the
  snap-up, which is the only rule that cannot clip.
- ~~`subControlRect` is neither declared nor overridden.~~ **Done for
  `CC_ComboBox`**, which is where it was costing something. An editable
  combo's internal `QLineEdit` is placed by that call, and with the proxy
  style answering it the edit sat at Fusion's pixel offsets inside a
  cell-sized combo -- measured at 377x15+2+2 in a one-cell combo. No
  application could correct it, because no application constructed that
  widget; only the style can reach it, which is design.md §5.3's whole
  argument for the style over widget subclasses.

  `GridGuard` is what found it, on a test written to cover something
  else entirely, and removing the override reddens it again. The other
  complex controls still take the proxy's answer, and `subElementRect`
  is still not overridden -- neither has yet cost anything measurable,
  and this one was done because it had.
- ~~The off-by-one vertical-centring baseline calibration that §16 called
  for is not done.~~ **Explained, and it needed no calibration.** §16
  recorded the symptom -- "vertical text centring within multi-cell
  widgets is visibly off by one row" -- and could not place the cause.
  There was none to place: the widgets were **two cells tall when they
  should have been one**, and a control with two rows to fill puts its
  indicator on one and its text on the other. `[x]` and `Enable
  telemetry` landed on different rows for that reason and no other.
  With single-line controls one cell tall the two are on the same row by
  construction. The recorded fixture shows it: the prefs dialog went from
  controls separated by blank rows to one row per control.

  Worth keeping as a shape rather than as a fact about this bug. The
  symptom was described as a *rendering* fault -- a baseline, a centring
  calculation -- and it was a *geometry* fault two layers up. A design
  note that names the symptom and guesses at the layer is the kind that
  sends the next reader to the wrong file.
- ~~`GridGuard` does not exist at all.~~ **Done.** It is an event filter
  checking every widget geometry against the grid as it is assigned, so a
  misalignment is reported where it happens rather than as a smeared
  frame several layers away. It **reports rather than aborts**: a
  misaligned widget is a quality defect, and a guard that takes the
  application down is one somebody switches off, at which point it guards
  nothing. `QHeaderView` and `QScrollBar` are exempt by name, because F5
  measured that they self-size and land off the grid however the style is
  written -- an exemption that is reviewable, rather than a guard that
  fires on every item view and is then disabled. Installed automatically
  in debug builds and by tests explicitly, which is design.md §9's "runs
  as an assertion in every test".
- `GridMetrics` has `cw`, `ch`, `set`, `cells` and `isAligned`, but not
  `toCells()` or `snapUp()`.
- The proxy base is hardcoded to Fusion, so design.md §10.1's promise that an
  application's custom style *becomes* `GridStyle`'s proxy base is not
  honoured.

**Theming -- wired.** `Qtty::theme()` is the single source rendering
resolves colour through. `CellPaintEngine` still consults
`QGuiApplication::palette()`, but for one thing only -- recovering *which
role* produced a pen or brush -- and what that role looks like on a
terminal is the theme's to say. `setTheme()` therefore changes what gets
drawn, which it previously did not.

- **The hand-authored Ansi16 role table exists** (`ansi16_for_role()`),
  and it is a design artifact with a stated reason per entry, as §6 asks.
  Nearest-of-16 survives only as the fallback for colours with no role
  behind them, which is what Channel B output is. The measured case is
  `Highlight`: Fusion's 0x308cc6 nearest-matches to 6 (teal) -- wrong hue,
  and a third the luminance delta under white text -- against the
  authored 4.
- **Quantisation is CIELAB**, as §6 requires. Measured against the RGB
  version it replaced: rgb(40,120,50) went to grey 238 and now goes to
  green 22, an error of 38 parts in 3850 squared-RGB units being enough
  to turn a saturated green into a grey. Memoised, and the memo is
  cleared wholesale past 4096 entries because a photographic Channel B
  image can present millions of distinct colours and a cache that only
  grows is a leak wearing a cache's costume.
- **All three SGR paths exist** and are chosen by `Capabilities::color`,
  which `AnsiBackend` now negotiates rather than hardcoding to
  Xterm256: `QTTY_COLOR` overrides, then `COLORTERM`, then a `-direct` or
  `256color` `TERM`, with the sixteen colours as the documented floor.
- **The contrast check runs at present time**, counting in all builds and
  logging in debug, capped. Never fatal: a contrast violation is a
  quality defect, not a corruption, and aborting a user's terminal
  application over one would be wrong.

**Snapshot harness -- done.** `test/snapshot/prefs_dialog.txt` and
`test/snapshot/widgets_gallery.txt` are the fixtures, and
`make record R=<fixture>` rewrites one after a reviewed change.
`CellBuffer::to_snapshot()` emits the three planes design.md §9 asks for
-- glyphs, attributes and colours over the same grid, plus a legend --
where `toText()` emitted glyphs alone and **no test could snapshot colour
or attributes at all**.

Proved rather than assumed. Removing the reverse video from the selected
tab leaves the **glyph plane byte-identical** and changes the attribute
plane, so the old format went green on a lost selection and the new one
does not. That is the regression class most of the Channel A work
produces, and it was invisible.

Three decisions in the format, each for a reason. The attribute plane
encodes the **whole six-bit mask** as one character rather than the first
flag set, because a plane showing only the first would go green when a
second stopped being drawn. A colour **pair** gets a letter rather than
each colour, since a terminal frame uses a handful -- the ground, a
selection, a highlight -- and the legend stays readable. And a plane with
nothing in it collapses to `(none)`: a frame drawn entirely in the
terminal's own colours is the common case, and thirty blank rows bury the
glyph plane a reader came for, while an attribute appearing anywhere
still replaces the line with a plane. `qtty-replay` (`tool/replay/`) drives a built-in sample UI
only; the characterisation runner design.md §9 makes the backbone of the
migration does not exist. `src/backend/null/null_backend.h` is now reachable and
exercised -- see the backend seam below -- but it is still not what the
snapshot tests are built on; they call `renderOnce()` directly.

### 7.2 Widget tier (design.md §17.2)

**Done, with tests:** the basic controls (`QLabel`, `QPushButton`,
`QCheckBox`, `QRadioButton`, `QGroupBox`, `QFrame`), menus and the menu
bar, scrollbars, tabs, the progress bar and the slider. Exercised by
`test/suite_widgets.cpp` and `test/suite_render.cpp`.

**`QToolBar` works now**, and it needed three things that had to arrive
together. It rendered as an empty strip: two actions laid out correctly
at 60x19 and 70x19, and not one glyph on the screen.

- **`QToolBar` defaults to `Qt::ToolButtonIconOnly`** and a terminal
  draws no icon, so the base style measured each button for an icon --
  about two cells -- and the label had nowhere to go. `CT_ToolButton` is
  measured from its text here instead. `SH_ToolButtonStyle` is pinned to
  text-only as well, but that reaches only widgets set to follow the
  style, which is why the measurement had to move rather than the hint
  alone. It is the same judgement as pinning the dialog-button icon hint:
  no space is reserved for what cannot be drawn.
- **Nothing drew a tool button's label.** `CC_ToolButton` does, bracketed
  like a tab -- the nearest thing already in this style, being a row of
  adjacent labels one of which may be current. A push button's angle
  brackets would read as a dialog button sitting in a toolbar.
- **`PM_ToolBarHandleExtent` was 9**, most of a cell, pushing every
  button off the grid. A terminal toolbar cannot be dragged, so it is
  nothing; the separator and the overflow arrow get a cell each, being
  things that are drawn.

The test uses the DEFAULT tool button style deliberately. With
`setToolButtonStyle(Qt::ToolButtonTextOnly)` it passes with the sizing
left broken, and the default is what an application gets.

**And it uncovered a general fault in Channel B: a rule on the last
pixel row of a widget was drawn in the row below it.** `line()` mapped a
coordinate with `qRound`, so a toolbar 19 pixels tall drawing its bottom
border at y=17 and y=18 put it in row 1 -- a row the toolbar does not
occupy -- and it came out as a full-width rule across the central
widget, measured as `body` followed by `──────`. A line belongs to the
cell it is **in**, not the boundary it is nearest, so it floors now.

The same edit fixed the span's far end, which ran one column past the
buffer: a line to x=399 asked for column `qRound(39.9)` = 40 on a
40-column buffer. Nothing was corrupted, because `CellBuffer::at()`
returns a scratch cell out of range -- which is why it had never been
noticed rather than why it was safe.

**The `widgets_gallery` fixture recorded the defect and was
re-recorded.** It carried a full-width rule one row below the tab pane;
the trace says that rule is `LINE (10,284)-(429,284)`, and with a 19-pixel
cell row 14 spans y=266 to 284, so y=284 is the last pixel of the row the
pane's `└───┘` is already in. Exactly one line of the fixture changed,
which is the whole check on a re-record: a fixture rewritten because the
code changed is only honest if the diff is the change.

**Partial:** `QLineEdit` and the item views.

**Thin or absent:**

- **`QSpinBox` has tests now**, and they found two things. Its keys work:
  Up and Down step the value. Its internal `QLineEdit` was at
  `280x13+3+3` inside a one-cell spin box -- the same missing
  `subControlRect` as the combo, fixed with it and now on the grid.

  **The value not rendering was found and fixed, and the entry that stood
  here described it wrongly.** It said "the value does not render", and
  listed eliminations under that heading. The value renders perfectly in
  the ordinary case; what the failing test did, and the entry did not
  say, was give the spin box focus and send it keys. Every elimination
  below it was gathered with that condition present and unnoticed, which
  is why none of them discriminated -- they were all varying the spin box
  while the variable was somewhere else entirely. **A defect recorded
  without the conditions that produce it sends the next reader to vary
  the wrong thing**, and that cost more here than a wrong guess at the
  cause would have.

  The bisect that broke it open varied one thing at a time and is worth
  keeping, because each step killed a plausible cause:

  * `stepUp()` renders. A raw `QKeyEvent` press renders. Press **and**
    release does not -- so it is not stepping, and not the key.
  * Qt focus alone renders; qtty's `setFocusWidget()` alone renders; both
    together render. So focus is not it either, and neither is the
    router's `setFocusWidget(input_scope()->focusWidget())`, which for a
    `QSpinBox` returns the spin box rather than its inner edit.
  * The release is what makes `QAbstractSpinBox` select its text, and
    `deselect()` afterwards brings the value straight back. But a plain
    focused `QLineEdit` with `selectAll()` renders fine, so "selected
    text does not render" was wrong too.

  What settled it was tracing the paint engine rather than reasoning
  about the widget. The failing render emits, in order: a `Highlight`
  fill at the value's cell, the glyph, and then **a second fill at the
  same cell in the `Text` colour, after the glyph** -- `1.0x19.0` pixels,
  one pixel wide and one cell tall. That is `QLineEdit`'s text caret, and
  `CellPaintEngine::to_cells()` rounds every extent up to at least a
  whole cell, so a one-pixel caret became a full-cell fill and
  `fill_rectf()` blanked the glyph under it.

  **So it was never a spin box defect.** It is the general rule that a
  rect thinner than half a cell cannot stand for that cell's background,
  and `fill_rectf()` now honours it: a thin fill colours a cell that is
  empty and leaves a cell that is not. Nothing is lost by dropping the
  caret, because the caret is already carried by the terminal's own
  cursor -- `Compositor::compose()` places it from the focus widget and
  `ITerminalBackend::set_cursor()` emits it.

  The spin box reached it first only because `QAbstractSpinBox` selects
  on step, and a selection is what makes Qt paint a caret in an offscreen
  widget at all. Any focused editor showing a caret had the same fault.

  Two measurements taken while fixing it, both worth having:

  * **The whole suite emits no sub-cell fill at all** -- instrumented and
    counted at zero across 290 checks, with the probe shown to report a
    positive on the known caret case before the zero was believed. So
    nothing existing depended on the old behaviour, and the caret was
    untested, which is why it survived.
  * **The terminal cursor was landing a cell to the left of the caret**,
    and that one is fixed too. `QAbstractSpinBox` does forward
    `ImCursorRectangle` -- verbatim, so the rect comes back in the inner
    edit's coordinates while `compose()` mapped it from the spin box,
    dropping the edit's offset. `compose()` now finds the widget that
    actually returned the rect and maps from there.

    Two things about the *checks* are worth more than the fix. The first
    probe compared `cursor_cell()` against a truth computed by the same
    `mapTo()` the fix had just changed -- it was the fix checked against
    itself, and it said "ok" four times. The invariant that means
    something is that the same editor at the same place reports the same
    cell whether it is standalone or nested, and the first attempt at
    *that* failed for a reason of its own: a standalone `QLineEdit` has a
    frame and a spin box's inner edit does not, so the two were not the
    same editor at the same place and the disagreement was the probe's.
    The second is that at caret position 0 both mappings round to the
    same cell, so a test written at position 0 -- which is where a
    stepped spin box leaves it -- passes with the bug present. The suite
    check uses positions 1 and 2.

    And the stale-binary trap caught this one on the way past. The
    sabotage run rebuilt the library and reported "agree", which read as
    the fix making no difference; the probe was statically linked and had
    not been relinked. Relinked, it disagrees exactly where it should.
    `make test` does not have this problem, which is the argument for
    putting the check in the suite rather than leaving it in a probe.
- ~~`QSplitter` drag is unimplemented.~~ **Done** -- it was the motion
  and grab absence in §7.1, not anything about splitters, and it needed
  no splitter-specific code.

  **What it surfaced is that a `QSplitter` lays its panes off the grid,
  and always has.** `PM_SplitterWidth` already answers one cell, so the
  handle is fine; the panes are not. A 300px splitter with a 10px handle
  splits evenly into 145/145, which is off the grid before any input is
  involved -- so this is not a consequence of the drag, it is what having
  a splitter test at all revealed. `GridGuard` reported 36 violations the
  moment one existed.

  It is not fixed here, because fixing it means snapping child geometry,
  which is exactly §7.8's open question and the copyright holder's to
  decide. What is done instead is to **assert the count**: the suite
  checks that nothing before the splitter left the grid, then that the
  splitter block does. The day snapping lands, that second check goes red
  and this entry has to be brought up to date -- which is better than a
  paragraph nobody re-reads. Aligned splits do exist for this geometry
  (140/150 sums to the same 290), so the decision is not blocked by
  arithmetic.
- ~~The `QTextEdit` interaction layer is absent.~~ **It works, and
  needed no code** -- which makes it the fourth gap in this document that
  was an obstruction rather than an absence. F8 had already measured that
  display is free when the document font's line height equals the cell
  height; what was missing was keys reaching the widget, which is the
  routing fixed in §7.1. `QPlainTextEdit` and `QTextEdit` both take
  typing, Return and wide clusters now, and what is typed reaches the
  cells. `src/widget/`'s README still describes a replacement layer, and
  §17.2's nought-to-four-day estimate was for the case where display had
  not come free. **Selecting text with the mouse works**, and is
  covered since the grab landed (§7.1): it needed no text-widget code at
  all, only motion events that carry the held button. What is genuinely
  untested is partial-line scrolling.
- **The editable `QComboBox` takes typed text**, non-ASCII included. It
  was untested rather than unhandled. Testing it is what found the
  missing `subControlRect` in §7.1: the combo's internal `QLineEdit` sat
  at the proxy style's pixel offsets, and `GridGuard` reported it on a
  test written to cover something else.
- ~~`QDialog`/`QMessageBox` is blocked by the compositor gap.~~
  **Unblocked** (§7.1): a modal is composited, holds input exclusively,
  and takes the cursor. What is still untested is `QMessageBox`
  specifically, and a dialog's own layout under the grid.
- **No `CellItemDelegate` class exists**, so item views have no Channel A
  role coverage; `QTableView` is never exercised at all.
- **`ICellPainted` and its `Q_DECLARE_INTERFACE` do not exist**, though
  that pair is R5's stated mitigation and F5's suggested remedy.
- ~~Menus draw a submenu indicator but nothing opens or routes a
  submenu, and there are no mnemonics.~~ **Both done.** Mnemonics needed
  building (§7.1). **Submenus needed no code at all**: Qt's own
  `QMenu::keyPressEvent` opens one on Right, and it had simply never run,
  because keys were not reaching the menu -- the dead `activePopupWidget()`
  branch recorded in §7.1. Fixing that made submenus work, and the
  compositor already drew them, since it walks the router's popup stack
  and a submenu is just another entry on it.

  Worth keeping as a shape: a feature recorded as absent was a feature
  **obstructed**, and the obstruction was two layers away in key routing.
  Ten checks now cover menus and submenus, and putting `key_target()` back
  on Qt's tracking reddens all ten -- so the suite says which layer owns
  them. ~~The compositor clamps rather than
  flipping.~~ **Flipping is done** (§7.1) -- the discriminating check is
  that the popup's far edge lands on its anchor, since "fully inside the
  terminal" is true of a clamped popup too.

### 7.3 Graphics tier (design.md §17.3)

The most complete tier. `Overlay`, the half-block colour upgrade, the
sixel encoder, the kitty protocol and iTerm2 inline images are all
implemented in `src/graphics/graphics.cpp`, `src/graphics/overlay.cpp`
and `src/backend/ansi/ansi_backend.cpp`, and structurally tested by
`test/suite_graphics.cpp` and `test/suite_placements.cpp`.

**But the encoders are tested for byte structure only.** No test decodes
anything, and there is no round-trip. A sixel stream that is
well-formed and wrong passes.

Missing:

- ~~Viewport cropping for half-clipped kitty placements.~~ **Done, and it
  was every pixel tier rather than kitty's alone.** design.md §16.3 named
  it as the kitty remainder after the chat spike -- the heart entering at
  cell (3,13) half-clipped by the viewport. Measured while fixing it:
  kitty, sixel and iTerm2 all placed a `CellImage` at its full size at
  `cell_rect.topLeft()` whatever the grid was, so a sticker scrolled out
  of view drew past the terminal, and one scrolled off the top was
  positioned at a **negative row**. Only the mosaic tier was safe, and
  only because it composites into the `CellBuffer`, which clips by
  construction -- which is why design.md §16.3 saw it as a kitty problem.

  `crop_placement()` answers all three: what of a placement is on screen,
  in cells, and the matching rectangle of the image in pixels.

  **kitty carries the crop as a source rectangle, not as different
  pixels**, and that is the part worth remembering. `a=p` takes
  `x/y/w/h` selecting a region of the *stored* image, so the upload stays
  whole and upload-once survives a crop. Uploading the cropped pixels
  instead would file them under the full image's cache key, and the next
  unclipped sighting of that sticker would show the crop -- a cache
  poisoned by a scroll. Sixel and iTerm2 have no such mechanism and crop
  the image itself, which is safe because neither is cached. iTerm2 needs
  both halves together: OSC 1337 sizes the image in cells, so cropping
  the pixels without the cell count squeezes the whole picture into the
  visible rows instead of hiding the rest.

  Nine checks, and removing the clip makes seven of them fail.
**qtty asks the terminal now, and that closed two gaps at once.** It used to
decide its colour depth and graphics tier from `$TERM`, `$TERM_PROGRAM`,
`$COLORTERM` and `$KITTY_WINDOW_ID` alone -- every one of which is inherited
across ssh and su, so all of them are wrong in both directions. The lessons
are `fuzzypickles`' `tui/term_gfx.c`, which had solved this; what follows
credits it because the reasons matter more than the code.

**It could not have asked before, and the reason is the second gap.** The
decoder had no branch for OSC, DCS or APC at all, so a reply was typed into
the application: measured, one OSC 11 background reply arrived as **23 fake
keystrokes**, an XTGETTCAP reply as 14 and a kitty reply as 10. Querying and
key handling are the same defect from two sides, which is why the terminal
had never been asked anything.

What is implemented, and why each part is shaped as it is:

- **One batched query with device attributes LAST as the fence.** Five
  sequential probes cost five round trips before the first frame, and over
  ssh that is the whole of a visible startup delay. Every terminal answers
  DA1, so its arrival means a missing kitty reply is a real "no" rather than
  a slow one.
- **The kitty probe is safe to send blind** -- a terminal without the
  protocol ignores the APC string entirely.
- **XTGETTCAP instead of `$COLORTERM`** for direct colour, and the cell size
  and background asked rather than assumed.
- **Polled in slices and rescanned after each chunk**, because replies are
  not guaranteed to arrive together and over ssh routinely do not. **A
  timeout is not a failure**: whatever arrived still counts.
- **The parser is split from the I/O and is additive**, which is what makes
  a terminal that answers the graphics query but not the colour one, or
  answers out of order, or splits a reply across two reads, into a line of
  test rather than a flaky experiment.

**The rule the whole thing turns on is an asymmetry.** A signal that cannot
be verified may only ever say YES; only a measurement may say no. A guess
that says yes to kitty costs a screenful of escape sequences, and one that
says no costs half-blocks. So: an explicit override wins, then a terminal
that ANSWERED is believed completely -- including its silences -- and a
terminal that answered nothing has told us nothing, so `$TERM` is read as
before. Dropping to the floor there would regress every terminal behind a
multiplexer that eats the query.

**Resizes come on stdin too, which is why graphics is tied to input.**
`read_winch()` had an early return when the cell count was unchanged,
commented "SIGWINCH also fires for pixel-only changes" -- which is exactly
the case where the cell PIXEL size moved and every pixel measurement qtty
holds went stale. It re-asks before that return now, and the answer arrives
through the decoder like everything else the terminal says. `CSI 8 t`, a
resize the terminal reports rather than signals, is handled on the same
path.

**Two faults found while building it, neither of them in the plan.**
`SIGPIPE` killed the whole suite with signal 13 and no message when the
query was written to a socket whose peer had closed -- which would equally
kill any qtty program whose output is a pipe the reader has finished with.
`AnsiBackend` ignores it now, so the write fails and is reported. And the
first pty test hung, because a pty master read blocks once the buffer is
empty and "read until empty" is bounded only if the descriptor says so.

**Coverage is measured, not asserted: `term_caps.cpp` is at 100% of 120
lines**, and `make coverage F=term_caps` is there so the claim can be
re-checked rather than believed. The backend rose from 66.77% to 70.77% with
it. The suite grew a pty, because the startup query, raw mode and the
SIGWINCH path all require stdin and stdout to BE a terminal -- against a
socketpair they prove the parser and leave the wiring untested.

Still not done from this list:

- **Unicode-placeholder mode** -- the kitty path that survives tmux, and
  the one design.md §5.7 calls stronger still because a placement becomes
  a run of ordinary text cells that the existing diff machinery moves
  with no special cases.
- The roughly 100 ms scroll-settle debounce for slow links.
- `Qtty::PixelSurface`.
- `qtty::cells()` and `alignTextDocument()`, the two GUI-invisible
  accommodations design.md §5.7 offers for cell-multiple image sizing.
- The `qtty.glyph` / `QIcon::name()` icon substitution registry
  (design.md §8.6).

### 7.4 Declared but unreachable -- mostly closed

This was a theme rather than a list: a surface that read as implemented
and could not be reached from a real terminal. Sinks existed, the router
implemented them, and the backend called none of them. Most of it is
closed now; what each item was is kept, because the shape is the useful
part. A declaration is not a feature, and a test that drives the sink
directly -- as this tree's did -- proves the sink and says nothing about
whether anything ever calls it.

- ~~`onResize`, `onPaste` and `onFocusChange` are implemented on the
  router and never called by the backend.~~ **Done.** `resume()` asks the
  terminal for SGR 1006 mouse, 1002 press/release/drag, 2004 bracketed
  paste and 1004 focus reporting, and `suspend()` turns each off again --
  which matters more than the screen restore, because a terminal left in
  mouse mode writes an escape burst into the user's shell on every click
  for the rest of that shell's life. `SIGWINCH` is handled through a
  self-pipe, so **dragging a terminal's edge does something at last.**
- ~~Mouse input is unreachable.~~ **Done**, and the decoder had to be
  rewritten for it. It read a fixed three bytes and switched on the
  third, which works for the arrow keys and nothing else: an SGR mouse
  report is `ESC [ < 0 ; 34 ; 12 M` and a paste opens `ESC [ 2 0 0 ~`.
  Both were read as an unknown key, three bytes at a time, which then
  desynchronised everything after them. It parses a real CSI now --
  private prefix, parameters, final byte -- and returns for more bytes
  when the sequence is incomplete, which is the case the old one could
  not represent at all.
- **`Capabilities` answers for four of its fields now** -- `mouse` and
  `bracketedPaste` from whether input is a tty, `unicodeWide` because L2
  measures width itself. `synchronisedOutput` is deliberately still
  false: section 11 wants DEC 2026 to eliminate tearing and nothing emits
  the brackets yet, so claiming it would describe an intention rather
  than the backend. `title` likewise -- there is no OSC emitter.
- **`test/suite_backend.cpp` covers the decoder**, which had no test at
  all -- which is how the fixed-width reader survived long after the
  runtime grew sinks it could not feed.
- **Non-ASCII input was corrupted, and the suite could not see it.** The
  decoder built a key event's text as `QString(QChar(c))` from one byte,
  which is Latin-1. Typing an e-acute sends `0xC3 0xA9` and produced
  **two** key events carrying two wrong characters; a CJK character
  produced three; an emoji four. Every non-ASCII keystroke was mangled,
  in a library whose entire cell model is grapheme clusters -- the input
  path could not deliver even one.

  It survived because the suite fed it only ASCII, which is exactly where
  a one-byte Latin-1 decode and a UTF-8 decode agree. That is the same
  shape as the `elide` fault in §7.6, found the same day by the same
  question: **which hard case does nothing test?** Both rules handled
  ASCII correctly and neither had ever been shown anything else.

  Sequences are decoded whole now, and an incomplete one waits exactly as
  a half-arrived CSI does -- a terminal splits input at any byte, and a
  read() boundary mid-character is ordinary. A stray continuation byte is
  dropped rather than delivered, since it is not a character. The Alt
  path had the identical fault one byte later and is fixed with it.
  Seven checks go red against the old decode. It drives the real code path by
  making stdin a pipe rather than by reaching past `readInput()` into a
  helper written for the test, and it checks the case the old decoder
  could not express: a sequence split across two reads.
- **Terminal control now goes only to a terminal.** `resume()` emitted
  the alternate-screen switch and the mode sets unconditionally, so a run
  whose output was redirected wrote them into the file. Found by the new
  suite: its first PASS line came out appended to a mode-setting
  sequence. Guarded on `isatty(1)`, and asked separately from the input
  side, because output can be a pipe while input is still a keyboard.
- ~~`ITerminalBackend` is not injectable.~~ **Done, and it was the
  blocker under the others.** `Qtty::exec()` constructed an `AnsiBackend`
  on its own stack, so the runtime could only ever be driven by the
  built-in terminal backend: `NullBackend` was compiled into the library
  and reachable from nothing, a legacy adapter had nowhere to plug in,
  and Phase 1 could not start. `exec(app, win, backend)` takes the
  backend now and `exec(app, win)` supplies `AnsiBackend` by calling it,
  so the two paths cannot drift. `test/suite_runtime.cpp` drives a real
  session on `NullBackend` and checks that the frame arriving at the
  backend is the widget tree.

  Deciding §8.2 -- whether L6 becomes the `Application` class design.md
  design.md §5.6 specifies -- is still open. This does the smaller thing the
  blocker needed without pre-empting that answer.

**The font check is a hard startup error now.** design.md §5.3 asks for
the advance and the line height to be asserted at startup, and R3's
mitigation is "startup assert; bundled font; documented hard failure".
What was there was a `Q_ASSERT_X` comparing the advance of `'i'` with
that of `'M'` -- which tests monospace-ness rather than integrality, and
which compiles out in release, so **no shipping build carried the
mitigation at all**. `grid_font_problem()` returns a diagnostic rather
than asserting, and `setup()` reads it in every build and calls `qFatal`
with whatever failed. It checks fixed pitch, that the line height and the
advance are whole numbers, and that a spread of twelve characters --
thin, wide, punctuation, digits -- all advance identically, which is
where a font that is monospace for Latin and proportional elsewhere gives
itself away. Measured here: DejaVu Sans Mono at 16px is exactly 10x19,
which is design.md §16's figure.

### 7.5 Absent entirely

- ~~`GridGuard` in every suite.~~ **Done, and it paid for itself
  immediately.** It is installed in `test/main.cpp` and reset per suite,
  so a suite written later is covered by having been written rather than
  by remembering to opt in. It reported **83 misaligned geometries across
  five suites** on its first run, and sorting those out is where the
  `sizeFromContents` fix above came from -- see §7.8.
- ~~`ICellPainted`.~~ **Done**, and with it risk R5's stated mitigation and
  F5's suggested remedy: a widget that knows how to draw itself in cells
  says so, and its ordinary painting is skipped rather than drawn first
  and overwritten -- Channel B output underneath would show through
  wherever the cell painting left a cell alone.

  Two decisions worth keeping. The interface is **not** a QObject,
  because a widget already inherits one and two QObject bases are
  illegal; it is a plain abstract class the widget inherits second.
  And qtty dispatches with `dynamic_cast` rather than the `qobject_cast`
  design.md §5.3 shows: `qobject_cast` across an interface needs
  `Q_INTERFACES` on the widget and therefore moc, while `dynamic_cast`
  needs neither and accepts a widget that merely inherits the class.
  `Q_DECLARE_INTERFACE` is declared anyway so an application can use
  either. `IGraphicsOutput` was already dispatched this way.

  Replacing a widget's painting means consuming its paint event, which
  is the only hook Qt offers -- so the dispatch is a filter, and
  `CellPaintDevice::active()` is what tells it whether a cell render is
  running at all. That is design.md §10.1's inertness rule made mechanical: in a
  GUI build there is no active cell device, the filter stands down, and
  the widget paints exactly as it would with qtty absent.

  Four checks, each proved able to fail against a *different* wrong
  implementation: not installing the filter reddens four; painting cells
  without consuming the event reddens the two that say the ordinary
  painting was skipped -- which is the discriminating pair, since the
  first two pass while the widget is still wrong; and consulting the
  interface outside a render reddens the inertness check alone.
- **The whole of design.md §7's Tier-2 hint system** -- `setPriority`,
  `setCompact`, `CompactionPass`, the `"qtty.cells"` property -- and the
  CI check banning `setContentsMargins`, `setSpacing`, `setFixedSize` and
  `setFixedWidth` in shared UI code. Tier 1 is free and works; Tier 3 is
  a convention; Tier 2 is the part that needed building and was not
  built.
- **The bundled font.** The startup check is in place (§7.4), but it
  checks a font the *machine* happens to provide. design.md §5.3 wants
  the font bundled and installed with `QFontDatabase::addApplicationFont`
  so the grid does not depend on what is installed -- which is also what
  would make the snapshot fixtures reproducible (§7.9).
- `TermpaintBackend`, and the four legacy adapters.
- Qt 5.15 support.
- ~~The design.md §11 benchmark.~~ **Done**, and the budget holds with
  room. Measured on a 200x60 grid with a 5000-row table, Qt 6.8.2:
  **1.4 ms** to render a frame against a 16 ms local budget, 0.15 ms to
  diff it. The 80x24 dialog comes out at **0.15 ms**, which reproduces
  F9's 0.16 ms on a different Qt and a different machine -- independent
  corroboration of a spike figure, which is worth more than the figure.

  **What it asserts is deliberately not the timing.** This machine runs
  several concurrent builds, and the same binary measured 1.35 ms and
  2.41 ms minutes apart; a threshold tight enough to mean something is
  one that goes red for reasons that are not the code's, and a test that
  fails for unrelated reasons is one somebody disables. So it asserts
  what is load-independent -- render-twice-diffs-to-nothing, which is
  design.md §9's named invariant and did not exist; that one keystroke
  dirties 1 cell of 12000 and that the damage stays inside the widget
  that changed; that damage is proportional to the change rather than to
  the grid; that rows past the bottom of a 5000-row model are never
  painted, which is why such a model is affordable at all -- and it
  *reports* the three durations beside the budget for a human to read.
  One duration is asserted, at ten times the whole budget, so only an
  order-of-magnitude regression trips it.

  It carries its own paired probe: a frame that differs everywhere must
  report every cell damaged. Without it, a `diff()` that returned nothing
  whatever it was handed would satisfy every empty-diff check in the file
  and fail none.
- The design.md §9 differential test (the same model driven through GUI
  and TUI builds must reach the same observable state), and the
  render-twice-diff-must-be-empty invariant.

### 7.6 One question found three bugs: which hard case does nothing test?

Three faults in one session, none related in the code and all identical
in shape. Each rule was correct for the easy case, nothing had ever shown
it the hard one, and the suite was green throughout.

- **`elide` counted QChars where it meant clusters.** Correct on ASCII,
  where the two are the same. A five-character CJK string elided to three
  cells returned a lone ellipsis using one; at a budget of one it
  returned empty.
- **Key input was decoded as Latin-1, one byte at a time.** Correct on
  ASCII, which is exactly the range where Latin-1 and UTF-8 agree. Every
  other character arrived as two, three or four wrong ones.
- **A width-2 cluster in the last column was written as width 2 with no
  continuation cell**, so the row rendered one column wider than the
  buffer. Every wide-cluster check in the suite had room to spare.
  `text()` had the matching fault, reporting 6 cells written into a
  4-column row.

What makes this a lens rather than three anecdotes is that the second and
third were found by *asking the question*, not by meeting a symptom. The
first was found by accident -- two copies of one rule disagreeing -- and
the question is what it generalised to.

**The failure mode they share is a test suite selected to confirm correct
behaviour.** Every check was written by someone who knew what the code
should do and picked an input that showed it doing so. None was chosen
because a plausible wrong implementation would pass it. That is the same
principle the graphics round-trips measured from the other end: four of
five deliberately broken encoders left every pre-existing structural
check passing.

Where to point it next, in this tree: anything that takes a QString and
counts, anything that takes bytes and decodes, and anything with an edge
the fixtures never reach. `to_snapshot()`'s planes against a wide cluster
and `InputRouter::on_paste` were both unexamined. **Both have since been
looked at and both were wrong**, which makes the lens four for four in
one session.

The paste path is in §7.1. `to_snapshot()` promised that "the planes line
up with the glyph plane column for column, so a column can be read
straight down across all three" -- and under a wide cluster they did not,
because all three planes skipped the continuation cell. The glyph plane
is right to: a wide cluster is one glyph occupying two columns. The other
two were then a character short, so every plane below a wide glyph was
displaced by one and the fixture's one promise to its reader was false.
Neither recorded fixture contains a wide character, so nothing had ever
shown it the case.

Both targets named here have since been examined, and **neither was a
bug** -- which is worth recording, because a lens that always finds
something is one that is being made to. `diff()` across differently sized
buffers is correct as written and is now covered. `Color::to_ansi16`'s
fallback is correct but matches in RGB where `to_xterm256` matches in
CIELAB; the two disagree on 44.1% of sampled colours and which is better
at sixteen colours is a design question, recorded as OQ-7 rather than
decided.

So the lens is four for six. The two that came up empty cost a
measurement each and bought coverage and an open question -- which is
the ordinary outcome, and the four that paid out are what makes it worth
running rather than what makes it reliable.

### 7.7 Latent bugs, found by working around them

None of these was introduced by the work in this section. Each was found
by editing or measuring the code around it, which is the only reason they
are known at all -- they are quiet faults that no test was asking about.
The first is closed; the other two are recorded rather than fixed,
because each wants a decision or a place to live rather than a patch.

~~The primary window's origin is assumed twice, differently.~~ **Closed
by stating it.** `Compositor::compose()` draws `win_` at the origin
whatever its geometry says, while `InputRouter::onMouse()` maps a click
through `win_->mapFromGlobal(px)`. The two agree only at (0,0) -- and
`exec()` sized the window without ever positioning it, so they were
agreeing because that is where the offscreen platform happens to put a
window, not because anything required it.

`exec()` moves it there now. The assumption was not wrong, it was
unstated, and an unstated one cannot be tested: a probe that starts at
the origin cannot express an origin bug. The check moves the window away
before calling `exec()`, which is what makes it capable of failing --
verified by removing the one line and watching it go red.

**The sixel encoder carried a second copy of the xterm-256 palette**,
identical to `Qtty::xterm256_rgb()` in the public header -- verified
index by index, 0 of 256 differing, before it was removed. Two copies of
one table is the parallel-copy hazard `code-style.md` names, and the
sixel encoder is exactly the path it is worst in: nothing but a sixel
terminal reads its output, so a correction to one copy would leave the
other wrong silently. Closed. Its `c < 16` branch and a `c < 0` clamp
were dead with it, since `toXterm256()` returns -1 only for a `Default`
colour and this call site constructs `Color::rgb()`; the clamp would
have written white into a pixel rather than reporting, had it ever
fired.

Still open in the same function, and not a correctness fault: the
per-band colour scan is `for each of 256 registers, rescan up to 6*w
pixels`. It short-circuits, so a text frame with a handful of colours is
cheap, but the cost scales with distinct colours times area per band --
worth knowing before the sixel path carries a full-screen Channel B
image at frame rate.

~~The Channel A coordinate rules were copied rather than shared.~~
**Closed, and the copy was hiding a bug.** `cell_target()` and
`cells_of()` -- findings F1 and F2, which are *measurements* rather than
preferences -- were file-static in `grid_style.cpp` and were copied into
`cell_item_delegate.cpp` when it arrived. They live in
`src/cell_geometry.h` now, an internal header that does not leave the
tree.

The third copied helper is the finding. Both files had an `elide`, and
they were not the same rule: a differential run over 143 cases had them
disagreeing on 9, every one involving a wide cluster or a budget of one,
and `GridStyle`'s was wrong on all nine. It ended with `out.chop(1)`,
which removes one **QChar** where it meant one **cluster**, and it
reserved no cell for the marker -- so a five-character CJK string elided
to three cells came back as a lone ellipsis using one of them, and at a
budget of one it came back **empty**, rendering a truncated string as
nothing. Chopping a QChar would also split a surrogate pair, which is an
invalid string rather than a short one.

Neither implementation had a test that asked about a wide cluster, which
is why it survived: both handled ASCII identically, and ASCII was all
anything checked. Two implementations of one rule disagreeing is what
surfaced it -- the corroboration was independent because the two were
written by different hands for different callers.

**A gradient fill lands as a literal RGB background.** `fillRectF`
recovers which palette role produced a brush by comparing the brush
colour against each role's colour for **exact equality**, and Fusion
paints gradients. Measured: the tab pane's interior arrives as `#fbfbfb`,
which is no role at all -- it sits midway between `Base` (`#ffffff`) and
`AlternateBase` (`#f7f7f7`), a gradient stop. Matching nothing, it falls
through to a true-colour background, so on a terminal the whole tab pane
carries a near-white block behind its contents, and on a dark terminal
that is exactly as bad as it sounds.

The colour plane found this on its first run, which is the argument for
having built it. The fix is a design question rather than a patch: match
roles with a tolerance, ask the style for the role instead of
reverse-engineering it from a colour, or suppress the fill for regions
Channel A has already drawn as a box. Exact-colour matching is the shared
cause and it is fragile for any style that paints a gradient, which is
most of them.

**`QTabWidget` paints one row below its own geometry.** Isolated with a
probe: a window containing only a tab widget reproduces it. The widget
ends at y=285 and a rule is drawn at y=285..304, in the window's bottom
margin and one cell wider than the pane box above it. Pre-existing and
byte-identical in the fixture before and after the size change, so
re-recording froze nothing new -- but a control drawing outside its own
rectangle is a real fault in `src/grid/grid_style.cpp`, and on a real
screen it would overwrite whatever sat beneath it.

**A flipped popup does not remember its anchor.** The flip is computed
from the geometry the popup has when it is composed, so a popup that is
resized after being flipped does not re-derive the flip from the point it
was opened at. It stays inside the terminal either way, so the symptom is
a menu that is on the correct side for its old size.

### 7.8 What the guard found, and what an application still has to do

Installing `GridGuard` in the runner reported 83 misaligned geometries
across five suites. Sorting them turned out to be the most productive
hour in the tree, and the categories are worth recording because two of
them are the library's fault and two are not.

**Qt's own internal children -- 62 of the 83, and not the application's
to fix.** A combo box's popup container, scroller and list view; a
splitter handle; a tab widget's stacked page area; a tab bar's two
scroll buttons; a scroll area's viewport and its scrollbar containers.
F5 named two of these (`QHeaderView`, `QScrollBar`) and the class is
much larger. The guard exempts them by a **principle rather than a
list**: an `objectName` beginning `qt_`, or a class name carrying
`Private`, or descent from such a widget. A list of nine class names is
one somebody adds a tenth to without deciding anything, and that is how
an exemption grows until the guard reports nothing.

**A top-level's position, which nothing reads.** Qt centres a dialog
over its parent and assigns it a y that is not a cell multiple. In qtty
that coordinate means nothing: there is no window manager, and
`Compositor::compose()` decides where each top-level is drawn and snaps
it (§8.1). The guard asks a window about its **size** and not its
position. Checking the position reported every dialog in the suite for a
number nothing consults.

**Control heights -- the library's fault, and fixed.** See §7.1: single-
line controls were two and three cells tall. This was the bulk of the
visible damage and the cause of §16's off-by-one centring.

**Layout slack -- the library's fault, and NOT fixed. This is the open
one.** `QBoxLayout` hands leftover space to its items when nothing can
absorb it, in shares that are not cell multiples. Measured: a dialog 12
cells tall whose content needs 9 put its three rows at y = 33, 85 and
137 against ch=19; adding a trailing stretch, or sizing the dialog to
its content, gave zero misalignments. `QSplitter` does the same thing
horizontally, dividing by size hints rather than by cells.

**A second mechanism, and the style cannot reach it.**
`QProgressBar::minimumSizeHint()` does not go through the style at all --
it answers 110x**21** where `sizeHint()` correctly answers 110x19 -- and a
layout honours the minimum over the hint. Those two pixels moved every
widget below it: the slider to y=59, the tab widget to y=78, and left the
tab widget 207 tall. It was mistaken for leftover-space distribution
until somebody measured both hints separately, which is the only way to
tell the two apart, since they present identically as "a widget is a few
pixels too tall".

The consequence is worth stating plainly: **`GridStyle` cannot make a
progress bar one cell tall.** Every application must call
`setFixedHeight(GridMetrics::ch())` itself, or qtty must grow a
widget-side fix -- an event filter or an `ICellPainted` implementation --
because there is no style hook on that path. `QSlider` and `QComboBox`
answer 19 for both hints, so as measured this is the progress bar's
alone; nothing has audited the rest of the widget set for it.

That matters more than the arithmetic, because **design.md §7 promises
Tier 1 is free** -- "style metrics differ, so the same layout compacts
automatically". It is not free for any layout with slack in it, which is
most of them. Today an application must add a stretch and call
`setSizes()` in cell multiples, and nothing tells it so except the guard.

The two candidate answers, neither taken:

- **Snap child geometry after layout activation**, which is what §7's
  unbuilt `CompactionPass` is for. It would make Tier 1 genuinely free.
  The risk is real and must be measured before it is written: snapping a
  child's origin down can close a gap the layout meant to keep, and in
  the worst case overlap two widgets. In the measured case it was safe --
  33, 85, 137 became 19, 76, 133, each still a clear cell apart -- but
  one case is not a proof.
- **Leave it to the application and say so**, which means §7 Tier 1 is
  narrower than the design claims and the document should say what an
  application must do rather than promising it need do nothing.

Which of the two is right is a design decision and is recorded here
rather than taken. **What the decision was waiting on has now been
measured**, and the answer is sharper than "it was safe in one case".

The risk is not a property of snapping. It is a property of **which
snapping policy**, and the two obvious ones differ absolutely:

- **Floor the origin and ceil the size** closes any gap narrower than a
  cell, and overlaps the neighbour when it does. Measured: a 1px gap
  between two widgets overlaps, and so does a sub-cell-wide widget beside
  its neighbour.
- **Round each edge to the nearest cell** cannot do it at all, and this
  is a proof rather than a sample. Rounding is monotonic, so if
  `a.right + 1 <= b.left` then the rounded edges keep that order: two
  disjoint siblings stay disjoint, becoming adjacent at worst.

There is exactly one case where rounding does overlap, and it is not a
defect in the policy: **two widgets that both live inside a single
cell.** A cell renderer cannot draw those whatever it does with their
geometry, so the overlap is the grid speaking rather than the snap.

Two further measurements bear on the cost. Across eight real layout
arrangements -- box, grid, form, nested, mixed size policies, a splitter
-- **no sub-cell gap arose at all**, because `GridStyle` already returns
cell-multiple layout spacing; the sub-cell case has to be reached by
absolute geometry or by an explicit `setSpacing()` under a cell. And
four of those eight still had off-grid *children*, which is the slack
this section is about, so the population needing a snap is real while the
population at risk from one is not.

**`GridSnap` is built, and it is off by default.** It is `GridGuard`'s
other half -- same event, same exemptions, same install-once shape --
and neither is installed by `Qtty::setup()`, so a program can have the
report without the correction and a GUI build does nothing at all unless
asked (§10.1's inertness rule). Building the mechanism is not taking the
decision: whether §7 Tier 1 becomes free by turning it on is still the
copyright holder's, and nothing turns it on.

`GridSnap::snap()` is public because the property that makes it safe is
worth asserting on rectangles rather than only through a widget, and the
suite asserts both halves directly: that snapping a snapped rectangle
changes nothing, and that no two disjoint rectangles are made to
overlap. The first is not decoration -- **idempotence is what makes the
filter terminate**, since it sets geometry only when the snapped rect
differs, and a policy that moved twice would set geometry from inside
its own resize for ever.

**A widget with a fixed size off the grid cannot be snapped**, and the
suite says so rather than leaving it to be found. Qt clamps
`setGeometry` to a widget's size constraints, so `setFixedWidth(23)`
survives every pass and `GridGuard` goes on reporting it; a fixed width
of two cells snaps like anything else. That is the right division of
labour -- 23 is the application's number and only the application can
change it -- but it is the sort of boundary that is discovered painfully
if nobody writes it down.

**The probe's first version reported zero overlaps for both policies and
was wrong.** Its control -- two widgets three pixels apart, which no
gap-closing policy can leave disjoint -- came back clean, because the
numbers picked happened to abut: 47 ceils to 50 and the neighbour started
at 50. Without the control the run would have read as a clean bill for
floor/ceil, which is the policy that is actually unsafe. That is the
whole argument for a control that lives inside the probe.

### 7.9 The fixtures are machine-dependent

Worth recording on its own, because it will bite whoever changes machine
first. `Qtty::setup()` derives the cell size from the **locally installed**
DejaVu Sans Mono at pixel size 16. design.md §16 measured 10x19 on the
spike machine. A different font version, or a different rasterizer,
changes cw and ch and invalidates both snapshot fixtures.

That is exactly what the bundled font (D7) was meant to prevent, and it
is not bundled (§7.5). The fixtures do pass here, on Qt 6.8.2, today.

**And the suite could not run under any other platform at all, which was
found by trying.** `prepare_environment()` called
`qputenv("QT_QPA_PLATFORM", "offscreen")` unconditionally, so it
overwrote whatever the environment said -- including a deliberate
override. The first attempt to run the suite elsewhere therefore
reported a clean pass under `minimal` that was really offscreen wearing
another name, and the `make test-platforms` target built on it could not
fail: given a platform that does not exist, it printed `ok`.

Forcing offscreen is right and stays. A desktop session commonly exports
`QT_QPA_PLATFORM` as `xcb` or `wayland`, and honouring an ambient
setting would make a terminal program open a window. What was wrong is
that there was no way past it, so the override is now a qtty-specific
`QTTY_QPA_PLATFORM`, which nothing sets by accident.

**A single platform is a single configuration, and this tree has paid
for that repeatedly.** `QApplication::activePopupWidget()` is
permanently null under offscreen, which is why keys typed at an open
menu went to the widget behind it; no window ever activates, which is
why `focusWidget()` had to be reimplemented; and a caret paints only
under a selection, which is why the caret fault in §7.2 survived until a
spin box happened to select its own text. None of those announced
itself.

What a second platform costs, measured: **`minimal` cannot host the
suite.** It ships no font database, so `DejaVu Sans Mono` resolves to
`''`, and `grid_font_problem()` refuses at startup -- correctly, since a
grid needs integral metrics and there is no font to measure. `vnc` opens
a listening socket and `linuxfb` writes to the console framebuffer, so
neither belongs in a target that runs unattended. That leaves `xcb`,
which needs a display and puts windows on it, so `TEST_PLATFORMS`
defaults to `offscreen` alone and names `xcb` as the deliberate second
run. A target claiming two platforms while the second aborts would be
the vacuous pass again, one level up.

**The `xcb` run paid for itself immediately, which is the argument for
the whole exercise.** The suite came up with exactly one failure -- the
`prefs_dialog` snapshot, whose button row sat two cells left of the
fixture -- and the cause was not the fixture. `GridStyle` is a
`QProxyStyle`, and a proxy passes style hints straight through;
`SH_DialogButtonBox_ButtonsHaveIcons` comes from the **platform theme**,
answering 0 under offscreen and 1 under xcb. So a `Cancel` button grew
from 90 to 110 pixels to reserve room for an icon a cell renderer cannot
draw, and **a terminal program's layout depended on the desktop it
happened to be launched from.**

The eliminations are worth keeping because each killed an obvious
suspect: the base style is `QFusionStyle` under both; the dialog is
explicitly sized and `WA_DontShowOnScreen`, so its geometry is fixed;
`DejaVu Sans Mono` resolves identically with the same advance, and a
standalone `QPushButton("Cancel")` hints 90x19 under both. Only inside a
`QDialogButtonBox` did the width move.

`GridStyle::styleHint()` pins the hint to 0 now, and the whole suite
passes under both platforms. **The scope of the exposure was measured
rather than assumed**: all 121 style hints and all 96 pixel metrics were
compared across the two platforms, and that hint is the only
disagreement in 217.

One honesty note about the test guarding it. Asserting the hint is 0
**cannot fail under offscreen**, whose theme answers 0 regardless, so
under the default platform it is a check that inspects nothing. The test
says so and names the command that does discriminate. That command was
run: red before the override, green after.

**Going one step further -- a real desktop theme -- found three more,
and one of them was serious.** `QT_QPA_PLATFORMTHEME=gtk3` loads a
platform theme proper rather than the generic Unix one, and it moves
much more than a style hint. All three were invisible until something
ran under it.

- **`SH_DialogButtonLayout` and `SH_LineEdit_PasswordCharacter` are the
  same class as the icon hint**, so they are pinned with it. The first
  changes button ORDER -- gtk3 answers GnomeLayout and the dialog came
  back as `<Cancel><OK>` -- and the second changes the character a
  password field shows, U+25CF against U+2022. Pinning them to the
  values this library has always produced is not a choice about button
  order or bullet glyphs; it is a refusal to let either change
  underneath a program. It matters more here than on a desktop because a
  terminal program is routinely run over ssh, where the machine holding
  the theme is not the machine anybody is looking at.
- **A mnemonic marker was being drawn.** `CE_PushButtonLabel` wrote
  `QStyleOptionButton::text` unchanged, so a `QPushButton("&Save")`
  rendered `<&Save>`. This one is not platform-dependent at all and was
  merely *found* here, because gtk3 supplies its standard button text as
  `&OK` and `&Cancel` where the generic theme does not. The check box
  and the group box were already right, and only because `GridStyle`
  does not override their labels: they fall through to a base style that
  draws through `Qt::TextShowMnemonic`. The sharper reason it had to go
  is internal -- `InputRouter::match_mnemonic()` reads that marker to
  route Alt-s, so **the library asks applications to write `&Save` and
  was then drawing the ampersand.**
- **The serious one: a platform theme sets fonts per widget class, and
  those beat the application font.** Measured under gtk3,
  `QApplication::font()` was DejaVu Sans Mono 16 as asked, while
  `QPushButton`, `QLabel` and `QMenu` were handed **Noto Sans 13, not
  fixed pitch**, advancing 12 for `M` and 3 for `i` against a 10-pixel
  cell. Every column such a widget computes is wrong, which is precisely
  what R3 and `grid_font_problem()` exist to prevent -- and the check
  could not see it, because it is handed the font `setup()` built, the
  one font a theme does not override. **A guard reading the wrong object
  reports success exactly as loudly as a real pass.**

  `setup()` now forces the family and size on every widget through an
  event filter, which needs no list of class names. It watches `Polish`
  **and `FontChange`**, and the second is the one that matters: traced
  under gtk3, the button still reported DejaVu Sans Mono at polish time
  and a `FontChange` arrived afterwards leaving it Sans, so a filter
  watching only `Polish` saw the right font every time and corrected
  nothing.

  Its test simulates a theme rather than waiting for one, by registering
  a class font the way a theme does, and varies the SIZE rather than the
  family so it does not depend on which fonts are installed. That is
  what lets it fail under the default platform. Confirmed by sabotage:
  reduced to watching `Polish` alone, it goes red -- so the simulation
  reproduces the ordering and not merely the symptom.

**And then the question nobody had asked: does a platform theme load
under the OFFSCREEN platform?** It does. That turns every finding above
from an xcb curiosity into something qtty shipped, because
`QT_QPA_PLATFORMTHEME=gtk3` is set globally by distributions and
`prepare_environment()` pinned only the platform. Measured in that
configuration -- offscreen, the platform qtty forces, with the theme
ambient -- `QApplication::font("QPushButton")` came back **Noto Sans 13,
not fixed pitch, advancing 12 against a 10-pixel cell.** Every column
such a widget computed was wrong, on any desktop that sets the variable.

So the fix is to pin the theme, not to keep patching what it sets.
`prepare_environment()` now does to `QT_QPA_PLATFORMTHEME` exactly what
it does to `QT_QPA_PLATFORM`, and for the argument already written
there: a terminal program must not inherit the desktop's look, and it
must not inherit its behaviour either. An empty value selects Qt's
generic theme, which is what every fixture here has always been recorded
against, so pinning makes real use match what is tested instead of
diverging from it. `QTTY_QPA_PLATFORMTHEME` overrides it deliberately,
the same escape the platform has.

**What settles pinning over patching is the key bindings.** A theme
supplies those too, and **20 of the 71 standard keys differ** under
gtk3: `Delete` gains `Ctrl+D`, `DeleteEndOfLine` and
`DeleteCompleteLine` appear as `Ctrl+K` and `Ctrl+U`, `MoveToEndOfLine`
gains `Ctrl+E`, `Quit` gains `Ctrl+Q`, `Redo` loses two of its three.
Fonts and hints can each be forced back one at a time; **key bindings
cannot, because Qt exposes no way to choose the keyboard scheme.** A
terminal program whose keys depend on the machine's desktop is wrong in
a way no per-symptom fix reaches, and it is worse over ssh, where that
desktop is not even the one the user is sitting at.

The per-symptom fixes stay anyway, and the division is worth stating:
**the pin is policy and can be overridden, the font filter is the
guarantee.** An application may register a class font itself, and a
future Qt may route one differently; the filter holds the invariant the
grid actually depends on either way.

`make test-platforms` runs the suite once more with the hostile theme
ambient, which is the only thing that would notice the pin being
deleted -- confirmed by deleting it and watching the target go red. It
**refuses rather than passes** when the theme plugin is not installed,
because an unknown theme name is ignored silently and looks exactly like
a pin that works.

**Scaling is the third lever, and disabling high-DPI scaling was not
enough on its own.** `QT_SCALE_FACTOR` and `QT_SCREEN_SCALE_FACTORS`
override it, and a HiDPI desktop commonly sets one. With either at 2 the
line height came out **18.6406 px** and `setup()` refused to start --
`grid_font_problem()` doing exactly its job, on a cause that was the
environment rather than the font. The visible consequence was not a
misrendering but a program that **would not run at all** on such a
desktop.

A cell grid has no device pixel ratio to honour: the terminal decides how
big a cell is on screen, and qtty needs the metrics to be whole numbers
and nothing else. So both are pinned neutral with the platform and the
theme, and the guard stays for the case it was written for. Its positive
control is already in the suite -- a proportional font is rejected, and
the message says "fixed pitch" -- so what changed is the cause, not the
check.

Swept with them, and clean: `QT_FONT_DPI=192`,
`QT_ENABLE_HIGHDPI_SCALING=1` and `QT_STYLE_OVERRIDE=Windows` all leave
`cw`, `ch` and the rendering untouched. The last is worth knowing rather
than assuming -- `setup()` calls `setStyle()` explicitly, so the override
never gets a say.

The pins are guarded two ways, and both were confirmed by removing the
line and watching them go red. The suite asserts the values directly,
which discriminates under a clean environment precisely because nothing
else sets them: an empty value means the pin is gone, not that the
machine is tidy. And `make test-platforms` runs the whole suite once
more under a **hostile environment** -- scaling and theme together --
which is the end-to-end form: without the pin that run does not fail an
assertion, it fails to start.

**What remains failing under a DELIBERATE gtk3 override is §7.7's, and
is not this section's to fix.** The `widgets_gallery` fixture differs in one legend entry:
`bg=#fbfbfb` against `bg=#ffffff`. That is the tab pane's gradient
landing as a literal RGB because it matches no palette role -- already
recorded in §7.7 as awaiting a decision. The measurement adds something
to it, though: the defect is not only that a role was lost, it is that
**the colour then depends on which desktop the program was launched
from**, since 14 of the 22 palette roles differ under gtk3.

## 8. Where the document and the code disagree

`~/.claude/guidelines/working-practice.md` is explicit: where the document
and the code contradict each other, **flag it -- do not silently resolve
it in either direction.** Which one is wrong is a real question with a
real answer, and the person who knows it is usually not the one who
noticed. Each item below states what design.md claims and what the code
does, and stops there.

### 8.1 Build system and Qt version

**design.md §10 says:** "CMake, C++17, Qt 5.15 and Qt 6 in parallel (the
four products are not on one version; the style and paint-engine APIs
used here are stable across both)."

**The code is:** qmake only, Qt 6 only, with no version conditionals
anywhere. CMake was removed in commit `73fdee6`.

This is entangled with OQ-3, which is still open. Note the design's
parenthesis is a claim about the *products*, not about qtty -- so the
question is not only "which build system" but "does the Qt 5.15 half of
the requirement still stand".

### 8.2 The L6 API shape

**design.md §5.6 specifies:** `class Qtty::Application`, constructed from
a `QApplication` and a `std::unique_ptr<ITerminalBackend>`, with
`prepareEnvironment()`, `setTheme()` and `exec()`.

**The code has:** free functions `prepareEnvironment`, `setup`, `exec`,
`isTuiActive` and `renderOnce`, with the backend hardwired inside
`exec()`.

The free-function shape is what `example/chat/main.cpp` was written
against and what design.md §16.4 calls "the intended `qtty::Application`
API", so the two halves of the document do not agree with each other
either. Whichever way this settles, §11's first item depends on it.

### 8.3 The termpaint and backend READMEs

`src/backend/termpaint/README.md` and `include/qtty/backend.h` both still
describe the in-tree backend as **`AnsiRuntime`**, which "currently drives
the tty directly" and "will be rehosted behind `ITerminalBackend` in
Phase 2".

That rehosting happened in commit `73fdee6`, and the class is
`AnsiBackend`.

### 8.4 The example's build instructions

`example/chat/main.cpp` refers to "the CMakeLists next to it", and
design.md §16.4 describes `examples/chat/CMakeLists.txt` and
`qtty_mini.h` -- three targets from one source set, and a minimal
stand-in runtime so the example runs in a real terminal today.

Both files were removed when the example moved to linking the real
library. The three-variant packaging story (`chat`, `chat-gui`,
`chat-tui`) has no build rule behind it any more.

### 8.5 The namespace, and the global style rule -- settled

Commit `e099862` moved `qtty::` to `Qtty::` for Qt-ecosystem consistency,
and `README.md` documents the choice and the reasoning.

`~/.claude/guidelines/code-style.md` says `snake_case` for "functions,
variables, type names and fields". qtty's public types are `PascalCase`
inside `namespace Qtty` -- `CellBuffer`, `GridStyle`, `InputRouter`,
`CellPaintEngine`.

**Decided by the copyright holder, 2026-08-26: the exception stands, and
it is exactly type names.** The reason is this library's position rather
than a preference. Its entire public surface is Qt's -- the types are
`QProxyStyle`, `QPaintDevice` and `QPaintEngine` subclasses, and a caller
writes `Qtty::GridStyle` in the same expression as
`QStyle::PE_FrameWindow`. Spelling it `qtty::grid_style` would make this
one library read unlike every library it appears beside and unlike the
toolkit it exists to serve. `code-style.md` carries the full form.

The exception is qtty's, argued from qtty's position, and no sibling
inherits it.

**What it leaves behind is a piece of work rather than a question.**
Deciding that only type names are exempt makes the tree's `camelCase`
*members* -- `putCluster`, `cellRect`, `setEventSink`, `frameRequested`
-- a real divergence from the rule, not an ambiguity in it. They are
pre-existing and there are many; converting them is a mechanical rename
across every header and call site and it needs a proof, so it is its own
pass and it is listed in §11. New code written since the decision uses
`snake_case` for members, which means the tree is mixed until that pass
runs. That is stated here so the next reader knows the mixture is a
known state with a scheduled end, not drift.

Two members are worth calling out as *not* part of that pass, because
they are the foreign-API carve-out rather than the divergence:
`focusWidget()` and `setFocusWidget()` deliberately mirror
`QApplication::focusWidget()`, which they replace (measured F4). Where
qtty stands in for a Qt call, keeping Qt's spelling is the rule working,
not an exception to it.

### 8.6 `diff()` has no early row skip

design.md §11 says `CellBuffer::diff` is "the only full-screen scan per
frame; O(cells) with early row skip". The O(cells) half is right. There
is no early row skip: `src/core/cell_buffer.cpp` compares every cell of
every row, with no per-row fast path that would let an unchanged row be
skipped without examining it.

Flagged rather than resolved either way, because both readings are
defensible and the choice is a design decision. The document may be
describing an optimisation that was intended and never written -- in
which case a row-level memcmp before the per-cell loop is a small change
that would earn its place at 200x60. Or the phrase may have been
describing the run-collapsing that IS there, which skips no work but does
keep the output proportional to the change rather than to the row.

Measured while writing the §11 benchmark, so the cost is known rather
than guessed: `diff()` over a 200x60 frame is 0.154 ms against a 16 ms
budget. Whichever way this is settled, it is not urgent.

## 9. Build and repository conventions

A harmonization pass ran on **2026-08-26** and moved the tree onto the
shape the sibling projects use. What follows is the state after it.

### 9.1 The layout

```
Makefile              the single entry point
qtty.pri              settings shared by every .pro file
qtty.pro              the subdirs project
VERSION               0.1.0, and the only place a version number lives
code-style.md         copied from ~/.claude/guidelines/code-style.md
.style-gate.toml      which files in this tree the gate reads
project.md            this file
README.md
include/qtty/         public headers
src/core/             CellBuffer, Cell, Color, CellTheme
src/grid/             GridMetrics, GridStyle
src/render/           CellPaintDevice, CellPaintEngine
src/runtime/          the exec loop, Compositor, InputRouter
src/graphics/         encoders, placements, Overlay
src/backend/          ansi/ (built-in), null/ (CI), termpaint/ and legacy/ (READMEs)
src/widget/           replaced widgets (a README today)
test/                 the suite; text fixtures in test/snapshot/
tool/inspect/         qtty-inspect
tool/replay/          qtty-replay
tool/style_gate.py    the shared style gate, copied from ~/.claude/tool/
tool/hooks/commit-msg the shared commit-msg hook, likewise
example/chat/         the canonical dual-frontend example
spike/                the Phase 0 spikes exactly as run
doc/design.md         the design document -- read this first
doc/beerssh.md        the integration contract with beerssh
```

Directories were renamed to the canonical singular names in that pass:
`docs` to `doc`, `tests` to `test`, `tools` to `tool`, `examples` to
`example`, `spikes` to `spike`, `src/backends` to `src/backend`,
`src/widgets` to `src/widget`. Compound C++ basenames went to
`snake_case`: `cellbuffer.cpp` to `cell_buffer.cpp`, `gridstyle.cpp` to
`grid_style.cpp`, `cellpaint.cpp` to `cell_paint.cpp`, `inputrouter.cpp`
to `input_router.cpp`, `ansibackend` to `ansi_backend`, `nullbackend` to
`null_backend`.

### 9.2 The build does not write into the source tree

**It used to.** Every `.pro` file said `DESTDIR = $$PWD/../lib`, so
`lib/libqtty.a` was a **tracked** file that every build rewrote -- a
clean checkout went dirty on `make`, and even a shadow build dirtied the
repository, because the `DESTDIR` was unconditional. `qtty.pri` derives
the library directory with qmake's `$$shadowed()` now, the artifact is
untracked, and `lib/` is ignored.

A stray `qtty-repo.tar.gz` -- an empty gzip that nothing referenced --
was removed in the same pass.

### 9.3 -Os, and why the order matters

`qtty.pri` sets `-Os`, not qmake's `-O2` release default, per the global
build guidelines. It **removes** `-O2` before adding `-Os`, rather than
appending: two `-O` flags on one command line leave the last one winning,
which would make the optimisation level depend on where in the line qmake
happened to put it. Debug builds get `-Og` rather than qmake's `-O0`, for
the same reason `-Og` exists.

### 9.4 Tests are built by the test target, and only by it

`test/` is deliberately not a `SUBDIRS` entry in `qtty.pro`. A plain
`make` builds the library, the two tools and the example; `make test`
runs qmake on `test/test.pro` into `$(BUILD_DIR)-test` and builds the
suite there. That is `build-and-commit.md`'s rule, and it was paid for
here within an hour of the Makefile being written.

The first version had `test` in `SUBDIRS` and gave the library rule an
explicit prerequisite list:

    $(LIB): $(BUILD_DIR)/Makefile $(SOURCES) $(HEADERS)
            $(MAKE) -C $(BUILD_DIR)

`SOURCES` is `src/` only. So editing a **test** file left `$(LIB)` up to
date, make printed `Nothing to be done for 'all'`, the sub-make never
ran, and the test binary that then got executed was the previous one. It
reported the previous answer -- which is indistinguishable from the new
code being correct. That cost a real detour: a `GridGuard` check was
diagnosed as a bug in the guard, probed in isolation twice where it
worked perfectly, and was a stale object the whole time.

Two things fix it and both are kept. The rule now depends on `FORCE`,
because a prerequisite list here is a claim that this file knows what the
sub-make depends on and it does not -- only the generated Makefile knows,
and it tracks headers through `-MMD` that no wildcard here can see.
Recursing every time costs a no-op sub-make. And tests leaving the
default build removes the question from that path entirely.

The two rebuild triggers are checked rather than assumed: touching a test
source recompiles its object, and touching a public header recompiles the
library objects that include it.

### 9.5 qmake's dependency list is a snapshot, and goes stale on an ADD

qmake does not emit `-MMD` dependency files. It writes a **static** list
into the generated Makefile, scanned once when qmake ran:

    cell_buffer.o: ../../src/core/cell_buffer.cpp ../../include/qtty/cell.h

That tracks an edited header correctly and knows nothing about a header
that did not exist when the scan happened. So **adding** a header and
then editing it rebuilds nothing, in either the library build or the test
build.

Measured the expensive way. A new internal header was sabotaged to prove
a set of checks could fail; `make` reported success, no object was
recompiled, and the binary that ran was the previous one -- so the checks
passed and the conclusion drawn was "these checks do not discriminate".
They discriminate perfectly. A sabotage that changes nothing and a check
that cannot fail are indistinguishable from the output, and this is the
second time in one session that pair has cost a wrong conclusion.

**§9.4's proof was real and incomplete, which is the part worth keeping.**
It showed that touching a public header recompiles the library objects
that include it -- and that header was already in qmake's list, so it
proved *edit* tracking and said nothing about *add* tracking. A proof
demonstrates the case it exercises and no other, and choosing the case is
where the thinking is.

Both qmake targets take `$(HEADERS)` as a prerequisite now, so any header
change re-runs qmake and regenerates the snapshot. `$(HEADERS)` also
gained `src/*.h`, which it had never matched -- the internal header at
the top of `src/` was invisible to it, which is how the gap surfaced.

### 9.6 The Makefile interface

The same interface every sibling project presents. qmake does the build
because moc does not fit hand-written pattern rules; the Makefile is the
wrapper that makes the two look like one thing.

| Target | What it does |
|---|---|
| `make` | build the library, the tools and the example |
| `make test` | build and run the suite |
| `make check` | style plus test -- what must pass before committing |
| `make style` | the shared source gate, and the project.md checks |
| `make hooks` | install `tool/hooks/commit-msg` into `.git/hooks` |
| `make version-check` | VERSION, `qtty.pri` and `version.h` still agree |
| `make record R=<fixture>` | rewrite a snapshot fixture after a reviewed change |
| `make run` | build and run the chat example in TUI mode |
| `make install` | headers, library, tools and example, honouring `DESTDIR` |
| `make clean` / `veryclean` / `distclean` | the build's own output, each removal printed; the build directory is guarded against being empty, absolute or escaping the tree |
| `make help` | the list above |

`BUILD_DIR` defaults to `build`. `DEBUG=1` gives `-Og`; `SANITIZE=1` adds
ASan and UBSan. Both are tested with `ifdef` and deliberately have no
`?=` default, because `DEBUG ?= 0` would make them permanently set.

Two things in the test target are there because of incidents recorded in
the global guidelines, and neither should be removed:

- **The suite runs with `QTEST_DISABLE_STACK_DUMP` and
  `QTEST_DISABLE_CORE_DUMP` unless `QTTY_TEST_STACK_DUMP` asks
  otherwise.** QtTest runs `gdb --batch --pid <self>` on a fatal signal,
  and a traced process that stops is neither reaped nor killable with
  SIGTERM. Five of them held 15 GB resident in a sibling project.
- **A run over zero binaries fails rather than passing.** A loop that
  finds no test binary exits 0 and reads exactly like a pass.

### 9.7 Indentation was converted, and the spikes were not

The tree was 4-space indented and was converted to tabs by
`tool/style_gate.py fix`: **2372 violations across 48 files**. `spike/`
is excluded, and the exclusion has a reason rather than being a
convenience -- the four spikes and the focus probe are the Phase 0 record
exactly as they were run, which is what makes them evidence for the
numbers §6 cites. Reindenting them would edit the record. Nothing in the
library builds them.

### 9.8 Two mixed indents are deliberate, and must not be "fixed"

`src/graphics/graphics.cpp` has two regions indented with tabs followed
by spaces -- the lambda body around lines 254-257 and the final `else`
branch around 274-278. That is the mixed structural indent
`code-style.md` rule 2 forbids, and it is left alone on purpose.

They compensate for a fault in `tool/style_gate.py` that is still open. A
lambda inside a **braceless** loop's body makes the gate believe the
lines around it sit one level shallower than they do, and the spaces make
up the difference. Correcting them to plain tabs -- which is what the
rule asks for -- makes the gate report them as violations.

Reduced to a fixture, and the reduction is the actionable part: a lambda
inside a **braced** loop conforms, and the identical lambda inside a
braceless loop's body does not, so the trigger is the braceless level.

**The mechanism is the lambda's opening brace, not its closing one**, and
this paragraph said the opposite until claude-guidelines corrected it
(`961d01a`). `convert_c` decrements the pending braceless-body count on
any `{`, on the reasoning that the brace takes the level the braceless
body would have had -- but a lambda's brace opens an *expression*, and
the enclosing block had already taken that body. So the lambda eats the
outer loop's level, which is why the body and the closing `};` both come
out a tab too deep. The reproduction was right and the attribution was
wrong, which is worth leaving visible: a reduction that fires reliably
says where to look, and says nothing about why.

**The obvious fix was written, measured and reverted**, so nobody spends
the afternoon rediscovering it. Conditioning the decrement on
`await_body` -- consume only a brace that IS the awaited body -- fixes
this case exactly and breaks aligned string continuations, sending a
continued argument inside a following block to a level too deep. That is
a different trade rather than a refinement, and it would swap one class
of false finding for another across sixteen trees. A correct fix probably
has to recognise a lambda introducer by its preceding token -- a `]` or
the `)` of a parameter list -- rather than reason from `await_body`.

**So this is the anti-pattern deliberately left in place rather than
propagated.** Nine other lines in these two files were the same
compensation and are corrected, because the `9e2dcbf` fix made the gate
right about them. These two regions wait for the lambda case. Whoever
fixes the gate should expect them to go red, and that is the signal to
correct them -- not a regression.

### 9.9 What was measured on this machine

Qt **6.8.2**, `qmake6` present, the offscreen platform plugin present,
DejaVu Sans Mono present. The tree builds clean and the suite reports
**108 PASS, 0 failures**.

## 10. Code style

Three rules -- `snake_case`, tabs to indent and spaces to align,
lowercase filenames -- with the detail in `code-style.md` at the repo
root.

Qt's own API is called exactly as it is spelled (`setParent`,
`sizeHint`, `drawPrimitive`), and a reimplemented Qt virtual keeps Qt's
name because it is Qt's name and not ours. Names qtty introduces stay
`snake_case`.

The one settled exception is type names: qtty's public types are
`PascalCase` inside `namespace Qtty`, decided 2026-08-26 and recorded
with its reason in §8.5 and in `code-style.md`. It covers type names and
nothing wider -- a method is `put_cluster`, not `putCluster`. The tree's
existing members do not yet follow that; see §8.5 and §11.

## 11. What is next, in order

The four items that used to head this list -- backend injection, the
compositor's top-level walk, theme wiring, and `GridGuard` with a real
startup check -- **are done**, and §7 records what each of them was and
what it now is. What follows is what they unblocked, and what they did
not touch.

Dependency-ordered:

1. **Decide the layout-slack question (§7.8).** Either qtty snaps child
   geometry after layout activation -- design.md §7's unbuilt
   `CompactionPass`, which would make its Tier 1 promise true -- or §7 is
   narrowed to say what an application must do instead. It is first
   because it is a decision rather than work, everything below moves
   widget geometry, and the guard now reports the consequences of getting
   it wrong. Whichever way it goes, the risk to measure before writing
   any snapping is whether closing a layout's gap can overlap two
   widgets; one safe case is not a proof.
2. ~~The attribute plane in `CellBuffer::toText()`.~~ **Done** -- see
   §7.1. What it immediately found is in §7.7: a gradient fill landing as
   a literal RGB background behind the whole tab pane.
3. ~~Decode or round-trip coverage for the graphics encoders.~~ **Done.**
   Each encoder is decoded independently and compared against the source
   -- exactly for kitty and iTerm2, within a measured 3/255 for sixel,
   which is the truncation residual of DEC's percent-valued colour
   registers and not a fudge. The measured argument for having done it:
   of five deliberate sabotages, **four leave every pre-existing
   structural check in the file passing**.
4. ~~The declared-but-unreachable surface in §7.4.~~ **Done** -- see
   §7.4. What is left of that section is `synchronisedOutput` (DEC 2026,
   which §11's frame budget wants) and a title emitter, both of which are
   additions rather than gaps.
5. **§8.2**: whether L6 becomes the `Application` class design.md §5.6
   specifies. The backend seam no longer waits on it, so this is now a
   design decision taken on its merits rather than a blocker.

The **member rename** is done. Closing OQ-6 in favour of PascalCase
*type* names made the tree's `camelCase` members a plain divergence from
the rule rather than an ambiguity in it, and 117 names moved:
`putCluster` -> `put_cluster`, `setEventSink` -> `set_event_sink`,
`cellRect` -> `cell_rect`, `frameRequested` -> `frame_requested` and
their neighbours.

**What made it safe was deciding what NOT to rename, mechanically.** An
identifier is Qt's if it appears anywhere in Qt's headers, so the filter
skipped every one that did -- conservative by construction, since
ambiguity left a name alone. That protected the whole override surface
without anybody listing it: `drawPrimitive`, `pixelMetric`,
`sizeFromContents`, `eventFilter`, `paintEngine`, `updateState` and the
rest keep Qt's spelling because they *are* Qt's names. `focusWidget()`
and `setFocusWidget()` are exempt for a different reason -- they
deliberately mirror the `QApplication` call they replace (F4).

**The filter's cost is a half-renamed struct, and that had to be looked
for rather than assumed.** A name of ours that Qt happens to use too was
skipped, so `CellTheme` nearly ended with `window_text` beside
`buttonText`, and `version.h` actually did end with `version_patch`
beside `versionMajor` -- caught because `make version-check` greps for
those names and went red. Ten such names were added back by hand after
checking each overrides nothing.

**The proof is reversibility.** Applying the inverse map to the result
must reproduce every original file byte for byte, which catches a target
colliding with an existing name, a substitution that is not one-to-one,
and a rewrite inside a token that merely looked like one -- none of which
a passing suite would necessarily show. It fired: `minDelta` converges
onto a `min_delta` that already existed, so the inverse could not be
exact, and that one name was done as its own pass and read by eye. The
first attempt also reverted only the files that failed rather than all of
them, which left a half-renamed tree and made the next run's proof
meaningless; an all-or-nothing revert is part of the shape.

The behavioural invariant held throughout: 201 tests, 0 failures, zero
build warnings, and the example and both tools still link.

Then the rest of design.md §17. The two items that used to head this
paragraph -- the attribute plane and the encoder round-trips -- are done
and are recorded in §7.1 and §7.3; both were chosen because they unblock
testing rather than features, and both immediately found a defect that
had been invisible, which is the argument for picking that kind of work
first.

What is left there, in no particular order and none of it blocked:
`CellItemDelegate` and the item-view roles; `ICellPainted`, which is
risk R5's stated mitigation and F5's suggested remedy for the widgets
that self-size; the `QTextEdit` interaction layer; submenus, mnemonics
and an editable combo box; `PixelSurface`; the
§11 benchmark, without which the 0.16 ms and 3.8 ms figures are spike
measurements nothing holds; and the bundled font, which is what would
make the fixtures reproducible (§7.9).
