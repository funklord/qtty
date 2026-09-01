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

## 0a. State, 2026-08-31

706 checks, 0 failures, under three configurations: the offscreen
platform, xcb, and the hostile environment `make test-platforms` builds.
`make check` is green and now includes `version-check`, which had never
been part of it. The 627 and the `test-platforms` run are today's, taken
from a second account; **xcb is the previous session's measurement and
was not re-taken**, because that account has no display, and a
configuration nobody ran is not a configuration that passed.

**A fourth axis was added to that list, and it found a defect that
stopped the program from starting at all: the fontconfig hint style.**
The suite is now green under hintfull, hintslight and hintnone, where
before it ran only under hintfull -- which is to say, only for a user
whose own fontconfig had been changed from the packaged default. §7.9
carries the measurement. The one-line shape: the 10x19 cell this tree
derives everything from was an *account setting*, not a property of the
font, and `setup()` now asks for the hinting that makes it so rather than
inheriting whatever the desktop supplies.

Whole-tree line coverage **98.74%, 2199 of 2227**, re-taken with the
command in §0c into a build directory removed first.

**That number is not comparable to the 98.87% (2105 of 2129) recorded
this morning, and the difference is the denominator rather than the
work.** 76 executable lines appeared in a tree whose only C++ change
since was one line, so the two runs measured different file sets. This
one's is verified complete -- 13 `.cpp.gcov` for the 13 sources `make`
compiles, listed rather than assumed -- and the earlier one's cannot be
recovered. §0c now says to delete the build directory first, which is a
second lesson from the same re-take: run in a reused one, `.gcda` from
previous runs merge, and the same tree measured 2179 covered instead of
2175. Coverage is the one number here that a stale directory moves in the
flattering direction.

The residue is 28 lines. §7.9 accounts for most of it -- `D0` deleting
destructors for classes only ever stack-allocated, a `qFatal` that
aborts, an inert SIGWINCH failure path, and four font-guard branches
measured unreachable on this font engine -- and **four sites it does not
mention, which are gaps rather than unreachable code**: `cell_buffer`'s
`"?"` fallback, two `QPaintDevice` metric cases, `grid_style`'s
tool-button size fallthrough, and **two orientations nothing renders** --
the vertical indeterminate progress bar and the vertical slider, both of
whose horizontal twins are tested.

Those last two are the ones worth taking, and they are one shape: the
orientation nobody renders is the orientation the vertical-bar defect
§0a opens with was found in. **The half-block compositor's translucent
branch has left this list** -- the mosaic sweep in §7.3 reached it, which
is what closing a coverage gap by rendering rather than by aiming at line
numbers looks like.

**What the last stretch of work was, in one line each**, because the
pattern mattered more than any single fix: render widget configurations
nothing exercises, print what a terminal would show, and read it. Eleven
defects came out of five such runs, every one a state that existed in the
model and not on the screen -- a disabled control indistinguishable from an
enabled one, a tristate checkbox's middle state, item-view check
indicators, a vertical progress bar drawn horizontally, a side tab bar
elided to nothing, an indeterminate bar reading as stalled, a sort
indicator that was a shaded block, a checkable menu item with no mark, a
closable tab's close button, an arrow-type tool button drawing nothing,
and every framed scroll area losing the bottom rule of its own border.

The interaction sweep that followed found **nothing** wrong, which is
recorded as a result rather than a gap: space, arrows and clicks reach
every common widget and change what they should.

**The same method was then taken to the item-view roles**, which §0e named
as the largest unswept surface, and it paid: ten configurations rendered,
six already right, **two defects fixed** -- a disabled row whose label was
the one part of it not dim, and `Qt::FontRole` reaching nothing at all --
and **two findings recorded rather than fixed**, because each turns on a
decision rather than a line. §7.2 carries all four. The generalisation is
worth more than any of them: **text drawn through `QPainter` carries the
font's emphasis and text written straight into the buffer does not**, so
every site that writes into the buffer is a place a font stops meaning
anything. One instance outside the delegate is measured and open -- a bold
`QPushButton` renders plain -- **and is now fixed too**: `label_attrs()`
carries the state and the font together at every site `GridStyle` writes a
label itself, which is the button, the tab, the menu bar, a menu item, a
header, a tool button, a progress bar's percentage and the item view's own
path. Neither snapshot fixture moved, because neither has an emphasised
widget in it.

**And then the same method at what is drawn DURING a drag**, §0e's second
item. Eight configurations pressed, moved and rendered with the button
still down. Five tracked correctly, which is recorded as a result. Three
were **one fault seen three times** and are fixed: `GridStyle` carried the
option's state into every label it writes and into **no glyph it writes**,
so every control drawn with `put_cluster()` -- slider, scroll bar,
progress bar, splitter handle, combo and spin box furniture, separators,
tree expanders -- was stateless, and a disabled one was identical to a
working one. The progress bar is what proves it was a fault: its
percentage was dim and the bar under it was not. Two more are recorded as
the copyright holder's, and one disagreement with design.md came out of it
that is larger than either: **Channel A does not clip**, and a header
resized past its viewport puts a grid line seven cells outside the widget,
in a neighbour's rectangle. §8.7.

**And then the dialogs beyond the standard three**, which §0d named as the
next unswept surface. Seven rendered -- input, message, button box,
progress, error, wizard, font -- and **almost all of it was already
right**, including a wizard's Back button arriving dim on the first page.
The one defect was in none of them and reaches the whole library: a
mnemonic underline is a rule one cell long that starts a pixel early, so
every check box, radio button and group box carrying one drew a rule in
the gap between its indicator and its label. `CellPaintEngine::line()`
takes the cells a line **covers** now rather than the ones it touches,
which is the half-cell test `fill_rectf()` was already applying one
function away.

**And then `ICellPainted`**, where the interface itself came out sound --
a scrolled viewport, an off-edge widget and a sub-cell one are all handed
what they should be, and three of those are checks now -- and the two
things worth having were found beside it. **A `QScrollArea` does not clip
its content**, so anything scrolled out of view paints over its
neighbours, with an ordinary `QLabel` as the control: that is §8.7, and it
moves that entry from untidy to urgent. And **the build did not rebuild**:
an `#include` added to a library source is invisible to qmake's subdirs
template, whose sub-Makefiles are generated once and never rescanned, so
a sabotage of the header changed nothing and the check aimed at it passed.
§9.5 carries the fix and what it cost backwards.

**And then the graphics tier**, at the fallback a terminal with no
graphics protocol actually gets. Eight configurations composited and the
cells read: the crop shows the right part of the image, the half-covered
edge is right and had never been tested, the alpha thresholds behave as
written. **One defect, and no assertion about the cells could have found
it** -- a null image composited nothing, correctly, and printed
`QImage::pixel: coordinate (-1,-1) out of range` twice per cell while
doing it, into the stderr a TUI shares with the screen it is drawing.
§7.3 carries it.

**And then the sizes nothing had rendered at**, which was the last name on
§0d's list: one cell, one row, one column, three by three, six by sixty,
two hundred by two, a menu popped at the corner of a three-row screen, and
zero. Everything above zero composed without complaint. **Zero did not**,
and the sweep found the same asymmetry twice: `read_winch()` refused a
zero column count and accepted a zero row count, and so did the
constructor -- `stty rows 0` is enough, and it lands on a null `QImage`
that `QPainter` warns about once per call into the terminal. Both doors
refuse it now, `FrameScheduler` refuses an empty size for every backend,
and §7.8 gained the measurement that gives its open question a face: a
window with Qt's default layout margins **loses its top row**, which on an
80x1 terminal is the whole screen.

**And the second Qt version, which §0e has wanted since it was written.**
Qt 5.15.15 is installed beside 6.8.2, so the build was simply tried: the
public headers compile clean, and the library fails on **three usages in
two files**, named in §8.1. Writing those three conditionals is the
decision §8.1 records as open, so the axis is priced rather than taken.
**One of the five errors was a defect and not a version difference** --
`grid_style.cpp` uses `QAction` and never included `<QAction>`, compiling
by transitive luck on a class Qt 6 moved between modules. That include is
right on every version and is fixed.

**A layout's vertical margins are zero now**, which was the last thing
standing between an 80x1 terminal and a screen: a window with a plain
`QVBoxLayout` rendered entirely blank, its first widget one row below the
only row there was. That looked like taste and was not -- two lines below
the offending one, `PM_LayoutVerticalSpacing` is already 0 while
`PM_LayoutHorizontalSpacing` is `cw`, so the tree had stated the rule and
the margins contradicted it. Left and right keep their column. Both
fixtures moved, and only by losing a leading blank row.

**OQ-7 is closed and the fallback stays RGB**, decided by an arbiter this
project already had rather than by the screen the entry asked for.
`has_minimum_contrast()` is qtty's own rule, and over 4374 saturated
colours Lab-nearest leaves **1018** of them with no contrast against a
black background where RGB-nearest leaves 470 -- more than double, on the
ground terminals mostly have. The 256-colour answer does not transfer,
and that is not a contradiction: among 240 candidates the perceptually
nearest entry is close in every respect, and among sixteen it is often a
dark chromatic one with no luminance left.

**`setFocusWidget()` is `set_focus_widget()`**, and the tree's own rename
filter is what decided it: an identifier is Qt's if it appears in Qt's
headers, `focusWidget` does and keeps its spelling, `setFocusWidget`
appears in none and was carried along by association. The pairing was
doing harm too -- `setFocusWidget(scope->focusWidget())` reads as one
thing's setter and getter and is not, the argument being Qt's per-window
focus and the call qtty's process-wide one. §11 carries the proof the
rename ran under.

**Frame output stays ungated on `isatty(1)`**, and the tree had already
settled that by building on it: `qtty-replay --ansi > corpus` drives the
real backend through a redirect and the byte stream is the point -- 1683
bytes from a two-line script, measured. Gating `present()` would make a
shipped tool emit nothing. The line is between a terminal's **state**,
which a program that does not own it must not set, and its **content**,
which is what was asked for. The pipe test asserts both halves in one run
now.

**The tab pane's near-white block is gone**, which was §7.7's gradient
finding: Fusion fills a pane with a gradient, the engine matched its stop
colour against no role, and a true-colour background went behind every tab
page. The two other answers recorded there were measured out -- a
tolerance cannot separate a colour equidistant between two roles, and Qt
exposes no way to ask which role produced a brush -- so the third stands:
`PE_FrameTabWidget` was not in `GridStyle`'s list of frames, and adding it
means the base style never runs. Ten rows of the fixture became no colour
at all.

**A one-row `QLineEdit` is bracketed**, like the combo box and the spin box
whose rationale it borrows, so an empty field is no longer invisible. The
cost this was deferred over -- a second closing bracket inside those two --
was an artefact of the first attempt: `QLineEdit::hasFrame()` separates
them with no class list and no parent test, because a control that draws
its own boundary already says so.

**A text selection is reverse video now, like every other selection in the
program.** It was the desktop's `QPalette::Highlight` as a literal RGB,
which `qtty/theme.h`'s own rule forbids -- the default theme keeps every
role at `Color::Default` and marks emphasis with attrs, not colour -- and
which made the commonest highlight in a program depend on which desktop
launched it. That is the fifth §0b entry in a row whose answer was already
in the tree. The engine also learned what `CellItemDelegate` already knew:
**reverse is a property of the cell, not of whoever wrote it last**, so a
glyph landing on a reversed cell keeps it.

**`GridSnap` is on, and §7.8's decision is taken** -- design.md §7's Tier
1 promise ("the same layout compacts automatically") is true now rather
than nearly true. What settled it was measuring the effect on a real tree
instead of arguing the principle: with the filter installed for the whole
suite, every check passes and **both fixtures are unmoved**, and the only
two that change are the two written to assert the unsnapped state. The
precedent decided the rest, one file away -- `setup()` already forces the
font on every widget through a filter, for the reason written there: the
pin is policy, the filter is the guarantee.

**And a correction: the lost top row the odd-size sweep blamed on Qt's
default margins is qtty's own.** `GridStyle` answers `ch` for
`PM_LayoutTopMargin`, so every layout gets a one-row margin; nothing
rounds and `GridSnap` neither causes it nor fixes it. That is a margin
policy, it is now §0b's, and it is a smaller question than the one it was
hiding inside.

**Channel A clips, and §8.7 is closed.** It is design.md §432's fourth
item and was the one of the four never implemented: a `QPainter` told to
keep inside four cells filled twenty. **A `QScrollArea` no longer paints
its scrolled-out content over its neighbours**, and a child wider than its
parent is clipped to it -- which was the largest thing standing between
this library and an application anyone depends on.

**It took two goes, and the wrong turn is the part worth keeping.** With
the user clip honoured the overflow remained, and every draw reported
`hasClipping() == false`, so this document recorded "Qt sets no clip" and
handed the rest to the compositor. Qt sets a clip: the **system** clip,
which `QPaintEngine::systemClip()` carries and this engine had never asked
about, while `hasClipping()` answers about the user clip and was honestly
false. **A channel that answers is not evidence it is the right channel.**

**One rule came out of it worth carrying: clipping rounds outward.** A
cell is atomic, so a clip covering part of one either admits it or loses
content that was inside it. Rounding to the nearest cell instead -- which
is right for placing a rectangle -- made a `QLineEdit`'s text disappear,
and reddened three checks, none of which mentioned clipping.

**And one of §0b's open questions was not a question.**
`Qt::ForegroundRole` and `Qt::BackgroundRole` reached nothing, and this
document deferred that as OQ-7 arriving at the item views. It was not:
OQ-7 asks which metric quantises an unthemed colour to sixteen, and the
project had already settled what an unthemed colour DOES -- it passes
through as the application's own, which is why a `QLabel` given a red
palette comes out red. Three answers to one question in one program, and
only one of them was the rule. The rule is shared now and all three agree;
§7.2 carries it, and the entry has left §0b.

**The dragged tab was the third, and it went further: the option the entry
recorded does not exist.** `QMovableTabWidget` carries no `Q_OBJECT`, so
the widget Qt drags a tab in reports its class as `QWidget` -- there is no
name to match and never was. The symptom belonged to
`CellPaintEngine::drawPixmap()`, whose icon substitution marked **one**
cell for a picture 8 cells wide and left the other seven showing what was
underneath. It covers the image's cells now, which for the 1x1 icon the
rule was written for is the same thing: the whole suite draws two glyph
substitutions and both are 1x1.

**The table grid was the second, immediately after.** It was written up as
"what should a table's grid be on a cell grid", and the tree had answered
that four times already -- `CE_HeaderSection`, `PE_PanelToolBar`,
`PE_IndicatorToolBarHandle` and `draw_box()`'s two-cell refusal all drop
chrome a cell grid cannot represent, each with its reason beside it. A
rule that meets content is not drawn now, and the blast radius was
measured over the whole suite before taking it: of 426 partial horizontal
rules and 102 vertical ones, **every single one belongs to a table's
grid**. §7.2 carries that too, along with the question that IS real and
was hiding behind the other -- a table grid that works needs the buffer to
know a cell was written.

**One branch was measured and rejected rather than argued about.** Letting
the window take the terminal's size -- dropping the layout's minimum, so a
short terminal is not simply clipped -- produces overlapping garbage:
`atextel` where three widgets share a row, `<Go>` over a progress bar.
§6's Phase-0 policy note said small terminals need drop-optional-then-
scroll "rather than faith in layout compression", and this is what that
faith looks like rendered.

## 0b. Open questions, and who owns them

An index, because these are recorded where they were found -- scattered
through 3,400 lines -- and a question nobody can locate is one nobody
answers. **None is a defect and none blocks anything**; each is a decision
that was deliberately not taken while working on something else. The
sections named carry the measurement and the options.

Owned by the copyright holder:

| Question | Where |
|---|---|
| A message box's severity icon: whether a warning triangle should become a glyph. The mechanism has no open question, the mosaic it would replace is **faithful and still unreadable**, and the picture costs the dialog exactly **one row**. Cheaper to answer after the picture-rule entry below, which is the same question seen from the other end | *Qt's standard iconography* |
| `SH_Slider_AbsoluteSetButtons`: whether the LEFT button joins the middle one, which already sets a slider where the click landed. All four candidate behaviours are printed now -- and `Left\|Middle` **removes paging** rather than adding anything, unless `SH_Slider_PageSetButtons` moves it to the right button | *the interaction sweep* |
| Whether the "too small to be a picture" rule moves to the backend. Nothing left unmeasured: the backend's fallback tier **already** composes placements as half-blocks, so this is one condition in `drawPixmap()`; no widget icon reaches the branch today; and the cost is **1.4 KB once per distinct icon, 35 bytes a frame after** -- eight of them together less than the one 48x48 icon the library already uploads | §7.2 |
| The bundled font -- but **not for the reason the entry gave**. Measured across 18 families from 7x16 to 32x16: pin `GridMetrics` and every frame is identical; leave it to the font and every frame differs. The fixtures depend on the **cell**, not the font, so reproducibility needs no licensed asset. What a font would still settle is R3's integrality and the running application's appearance | §7.9, §11 |

Owned elsewhere, and signalled rather than fixed here:

- **The braced-initialiser lexer fault in `style_gate.py`** is
  `claude-guidelines`', signalled with three fixtures and the paren case
  that must not change. It is larger than it looked: a braced continuation
  may be tab-indented **or** column-aligned and both are legal, so the fix
  must accept both rather than choose. 764 findings across eleven trees
  turn on it.
- **§9.8's two compensating regions** in `src/graphics/graphics.cpp` -- the
  lambda body near 254-257 and the final `else` near 274-278 -- are bent
  around the still-open lambda fault and **must go red when it is fixed**.
  They are deliberate and are not to be tidied.

**The standing exposure recorded here -- that the repository has no
remote -- is closed, and was closed by measurement rather than by work.**
`git remote -v` names `origin` at `git@github.com:funklord/qtty.git`, and
`git log --oneline -1 origin/master` is HEAD, so every commit described in
this document exists somewhere other than this machine. The paragraph is
corrected rather than deleted because the claim was load-bearing: it was
the reason "everything in it exists on one machine" appeared in the
handover, and a reader who remembers that sentence needs to find out here
that it is no longer true.

**What I complied with rather than checked.** The style gate demanded an
extra tab on about six continuation lines I wrote this session, and I gave
it one each time without establishing whether the demand was correct.
Tab-then-space continuations are the tree-wide norm -- 51 in
`grid_style.cpp`, 34 in `term_caps.cpp`, 35 in the file whose regions are
bent on purpose -- so those lines are conventional and are not a special
list. But *suspect the check before the code* says to establish which it
was, and I did not. If the lexer fix changes which form is required, they
are candidates along with several hundred others.

## 0c. How to re-take what this document claims

Every number here was measured, and a measurement without its command is
one nobody can re-take. `make check`, `make test-platforms` and
`make coverage F=<file>` are in the Makefile and documented there. These
are the ones that were typed by hand:

**Whole-tree coverage.** `make coverage` answers for one file; this is the
tree. **Delete the build directory first** -- `.gcda` files accumulate
counts across runs, so a reused one reports the union of every suite that
ever ran there and reads as better coverage than the tree has. Measured
on the same tree the same afternoon: 2179 covered from a reused
directory, 2175 from a fresh one.

    rm -rf build-cov build-cov-test
    make test BUILD_DIR=build-cov \
      QMAKE_CONFIG='CONFIG+=release CONFIG-=debug \
                    QMAKE_CXXFLAGS+=--coverage QMAKE_LFLAGS+=--coverage'
    cd build-cov/src
    for f in *.gcno; do gcov "${f%.gcno}" >/dev/null 2>&1; done
    awk -F: '$1 ~ /^ *[0-9]+$/ {ex++} $1 ~ /^ *#####/ {un++} \
             END {printf "%d of %d, %.2f%%\n", ex, ex+un, 100.0*ex/(ex+un)}' *.cpp.gcov

Add `{print "  " $2 ": " $3}` to the `#####` branch to name the uncovered
lines. `gcov -f <file>.cpp` is what separates a `D0` destructor from a `D2`
and is why the residue is what it is.

**The third configuration.** `make test-platforms` covers offscreen and the
hostile environment; xcb is run directly, and needs the stack dump off
because QtTest forks gdb on a fatal signal:

    QTTY_QPA_PLATFORM=xcb QTEST_DISABLE_STACK_DUMP=1 ./build-test/qtty-tests

**The fontconfig hint style**, which is the axis §7.9's last finding
turned on and has no environment variable. It is varied with a
configuration file rather than a variable:

    cat > /tmp/hint.conf <<'EOF'
    <?xml version="1.0"?>
    <!DOCTYPE fontconfig SYSTEM "fonts.dtd">
    <fontconfig>
      <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
      <match target="font">
        <edit name="hinting" mode="assign"><bool>true</bool></edit>
        <edit name="hintstyle" mode="assign"><const>hintfull</const></edit>
      </match>
    </fontconfig>
    EOF
    FONTCONFIG_FILE=/tmp/hint.conf ./build-test/qtty-tests

`hintslight` and `hintnone` are the other two values worth running, and
`fc-match --verbose "DejaVu Sans Mono" | grep hintstyle` says which one
the account is on without changing anything -- `1` is slight, `3` is
full. What the suite reports on an unconfigured account is the packaged
default, so a green run there is the one that means something.

**The beerssh probe matrix**, whose results are in `doc/beerssh.md` along
with when to re-take them:

    QTTY_NEGOTIATE_OUT=/tmp/out.txt \
      beerssh --term-features=<spec> -e <path>/qtty-negotiate --probes

The output goes to a file because stdout is the terminal under test.
Reading it needs the raw byte count, not only the parsed lines: a reply
that shrank rather than vanished is still a reply.

## 0d. The method that found eleven defects, and how to run it again

Not a tool -- a habit, and the most transferable thing here. **Render a
widget configuration nothing exercises, print what a terminal would show,
and read it.**

Add a temporary block to `suite_widgets` that renders each candidate into a
`CellBuffer` and prints its rows; run `make test`; read the output; delete
the block. Candidates are configurations a test would not think to build:
a disabled control, a tristate checkbox's middle state, a vertical
progress bar, a west tab bar, an indeterminate range, a checkable item
view, a closable tab, an arrow-type tool button.

Three things it taught, which the next run should carry in:

- **Print, do not assert.** The value is in reading output nobody
  predicted. An assertion can only fail against what its author already
  suspected.
- **The probe has its own bugs, and they read as findings.** One probe
  used a widget after the host that owned it was destroyed and segfaulted;
  another rendered a `QMessageBox` and reported the severity icon missing
  when it had arrived as a 5x3-cell image placement that `to_text()` does
  not rasterise. **Check the instrument before believing the result** --
  three times in one day it was the instrument.

  **A fourth, and this one was in the suite rather than in the probe**:
  the `gallery snapshot` line reported the suite's running failure count
  instead of its own result, so sabotaging an unrelated check turned the
  snapshot red with the fixture matching to the cell. The instrument to
  suspect is not only the temporary block you just wrote; a check that has
  been green for weeks can be answering a different question than its name
  says, and a sabotage run somewhere else in the file is what surfaces it.

  **A ninth, in the tooling this session leaned on hardest: `make record
  R=<fixture>` records nothing and says OK.** The argument is a SUITE
  name, and `widgets_gallery` is a fixture -- so the filter matched no
  suite, none ran, the binary exited 0, and the snapshot it was asked to
  rewrite was left untouched. It reads exactly like "already up to date".
  `test/main.cpp` refuses a filter that matches nothing now, the same way
  the `test` target refuses a run over zero binaries and for the same
  reason. The example in the Makefile's help is `R=render`, which happens
  to be both a suite and a fixture, so the trap was invisible from the
  documentation.

  **An eighth, and the cheapest to fall for: the same command run twice
  does not measure twice.** The Qt 5 error list was collected with
  `make -k`, and the count taken by running the same pipeline again --
  which reported **zero errors**, because make had nothing left to redo
  and printed nothing to count. A second invocation of a build command is
  not a second measurement; it is a measurement of what changed since the
  first, which is usually nothing. Take the count from the run that did
  the work.

  **A seventh, and it is the one this document had already warned about
  in §7.1: a check written under a redirected descriptor asks the wrong
  terminal.** The odd-size sweep's zero-rows check was placed just below
  the line that puts descriptor 1 back on the real stdout, so `SIGWINCH`
  reached a backend reading the wrong window size, and the check **passed
  against a deliberately sabotaged guard**. The warning was on the page
  and did not prevent it. What caught it was the sabotage producing no
  red -- so the rule is not "remember the warning" but **run the sabotage
  and disbelieve a green**.

  **A sixth, and the worst of them, because it was not in the probe at
  all: the binary under test was not the one that had been built.** An
  `#include` added to a library source is invisible to qmake's subdirs
  template (§9.5), so a sabotage of the header changed nothing, 27 checks
  elsewhere went red and the one aimed at the sabotaged code passed. The
  reading that presents itself is "this check does not discriminate" and
  it is wrong. **`grep -c SABOTAGE` says the edit applied to the SOURCE;
  it says nothing about the object.** When a sabotage produces a result
  that makes no sense -- and a check passing while its own subject is
  broken is exactly that -- `touch` the `.cpp` and run it again before
  believing anything.

  **A fifth, and it is the one to carry into any probe that drives input:
  some of what a widget does needs TIME to pass, not events to be
  processed.** A tab dragged and dropped rendered wrongly afterwards --
  the moved tab missing, a bracket doubled -- and the model was right the
  whole time. Qt animates the tab back into place, and
  `processEvents()` does not advance an animation that is waiting on a
  timer. A bounded `QEventLoop` with a 400 ms `singleShot` quit, and the
  frame came out correct. Every "after the release" assertion in a drag
  probe has this hazard, and it reads exactly like a rendering defect.
- **A channel that answers is not evidence it is the right channel**, and
  the clipping work paid for it twice. `QPainter::hasClipping()` reported
  false on every draw of a scroll area's overflow, which is true and was
  taken as "Qt sets no clip". Qt sets the **system** clip, a different
  member of the same object, and honouring it fixed outright what had just
  been written up as somebody else's work. The trace was real and the
  number was right; the question was wrong. When a measurement says a
  mechanism cannot be happening and the symptom says it is, the next thing
  to doubt is which member you asked.
- **When the defect is in what a call SAYS rather than what it writes,
  assert on the saying.** A null image composited nothing, which is
  correct, and printed two warnings per cell while doing it -- into the
  stderr that a TUI shares with the screen. Every assertion about the
  resulting cells passes against that, because the cells are right. The
  check installs a Qt message handler and counts. The general form: a
  probe that only reads the artefact cannot see a fault in the noise made
  producing it, and stdout, stderr and the terminal are the same file
  descriptor often enough here to matter.
- **Check whether the project has already answered it before deferring
  it.** `Qt::ForegroundRole` was written down as an open question owned by
  the copyright holder, and the answer was in `cell_paint.cpp` with a
  comment explaining itself: a colour no palette role explains passes
  through as the application's own. A deferral costs nothing to write and
  is caught by nothing -- `working-practice.md` says so, and this document
  did it anyway, one section away from the code that had decided. **The
  search is for the shape, not the name**: something else in the tree
  choosing between the same two answers, whatever it is called.

  **Twice running, in two consecutive items**, which is why this is a rule
  and not an anecdote. Both entries had been written into §0b as the
  copyright holder's; both were answered in the tree, one by code that
  already implemented the rule and one by four comments stating the
  policy. The tell they share: the entry described a **general** question
  ("what should a colour become", "what should a grid be") where the
  actual situation offered only a broken answer and a plain one. **When
  the options are "wrong" and "nothing", it is not a decision.** The real
  decision was hiding behind each of them, and is smaller and sharper for
  being separated out.
- **A baseline nothing produced is not a baseline.** The scroll check
  first placed its widget below the viewport, where it is never painted:
  the rect it compared against was the one a default-constructed `QRect`
  carries, and the check failed for that reason and looked like a real
  finding. It asserts the widget was painted in both frames now. Same
  shape as the vacuous pass, in a difference rather than in a gate.
- **Vary one thing.** The first check-box probe made one item checked
  *and* mnemonic-marked and the other neither, so the rule it found could
  have come from either. It cost one more run to separate them, and the
  test that came out asserts against a neighbour differing in exactly one
  respect -- which is the same discipline as the controls below, applied
  inside a single frame rather than across two.
- **The controls are where the findings actually get settled.** Every one
  of the item-view sweep's four findings needed a second render to place
  it: the same model with no delegate installed said the disabled-item
  fault was the delegate's rather than the style's; a bold `QLabel` said
  the font fault was the buffer-writing sites' rather than the terminal's;
  the same table with `showGrid` off, and again with no delegate, said the
  dashes were the line rule's; and a titled `QGroupBox` said the guard
  that produces them protects nothing. **Render the variant that would
  exonerate the thing you suspect** -- it costs one more block and it is
  the difference between a finding and a theory.
- **Every one of the eleven was the same shape**: a state that existed in
  the model and not on the screen. That is the lens, and it is not spent.
  It has since been applied to the item-view roles, to what is drawn
  during a drag, and to the dialogs beyond the standard three -- §7.2
  carries all three, §7.5 the `ICellPainted` sweep, §7.3 the mosaic tier
  and §7.1 the odd terminal sizes. **The list §0d has been working through
  is finished.** What would come next is not another surface but another
  configuration: a second Qt version (§0e), and whatever a real terminal
  says that offscreen does not.

**The sabotage discipline that goes with it**, because a passing new test
is not evidence: break the code the test claims to defend, confirm the edit
actually applied (`grep -c SABOTAGE`), run, confirm the *named* check
fails, restore, confirm the count is zero. Two sabotages this session
silently failed to apply -- their anchors matched twice -- and the green
run that followed would have read as "the test does not discriminate" had
the count not been checked.

## 0e. What I would pick up next

In the order I would take them, and none is blocked:

1. ~~**Run the probe method at the item-view roles.**~~ and
   ~~**carry a font's emphasis to every site that writes into the
   buffer.**~~ **Both done**, and §7.2 records them: ten configurations,
   six already right, two defects fixed, two questions raised, and then
   the general form of the second defect fixed at every site `GridStyle`
   writes a label. The residue is the two questions -- what a literal
   `QColor` becomes, and what a table's grid is on a cell grid -- which
   are the copyright holder's and are in §0b.
2. ~~**Sweep what is drawn during a drag.**~~ **Done**, and §7.2 records
   it: eight configurations, five tracking correctly, three defects fixed
   as one fault, two questions raised, and §8.7 -- Channel A not clipping
   -- found on the way. The next unswept surface named by §0d is dialogs
   beyond the standard three.
3. ~~**The `QStyle::StandardPixmap` question**, once decided, closes the
   dock title buttons and the message-box severity together.~~ **Withdrawn:
   they are not the same cause and one table closes neither.** Measured --
   `standardIcon()` is consulted for both, and a glyph returned that way
   never reaches the cells because Qt rasterises an icon before the style
   draws it. The severity icon arrives as 48x48 pixels with no identity;
   the dock buttons arrive with a 0x0 icon area, which is a sizing fault.
   §7.2 carries both, each pinned by a check.
4. **A second account, which cost nothing and is already spent.** Running
   the suite as a different user on the same machine is the cheapest
   configuration axis in this list -- no second machine, no second Qt, no
   container -- and it found the hinting defect in §7.9 on the first run,
   as a program that would not start. Anything else derived from the
   *user's* configuration rather than the environment's is reachable the
   same way, and fontconfig is unlikely to be the only such thing.
5. **A second Qt version, and it is now priced rather than open.** Qt
   5.15.15 is installed beside 6.8.2 and the build was tried against it:
   the public headers compile clean, and the library needs **three
   conditionals in two files** -- §8.1 names them. Writing those three is
   the decision §8.1 records as open, so the axis is reachable and not
   free. It is also the wrong axis for what this item wanted: a version
   sweep finds assumptions at run time, and Qt 5 cannot run the suite
   without the port. **A second Qt 6 point release would answer it with
   no decision at all**, and this machine has only one -- which is what
   keeps the beerssh exposure (a probe that passed on 6.10 and failed on
   6.4.2) an unmeasured one here rather than a clean bill.
6. **`suite_budget` prints and does not assert**, deliberately, because the
   same binary rendered the same fixture in 1.35 ms and 2.41 ms minutes
   apart. If a stable assertion is ever wanted it has to be a
   *relationship* -- damage-limited work against full-redraw work, in cells
   rather than milliseconds -- not a threshold on a clock.

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

- ~~**OQ-7.** Which metric should the ANSI-16 *fallback* use?~~ **Closed:
  it stays RGB, and the arbiter turned out not to need a screen.** The
  primary route is the hand-authored role table and design.md section 6 is
  emphatic that it should be; the fallback exists only for a colour with
  no palette role behind it. It matches in RGB, while `to_xterm256()` was
  changed to CIELAB precisely because RGB nearest turns a saturated green
  into a grey.

  What blocked it was having nothing to arbitrate with: over 24389 sampled
  colours the two disagree on 44.1%, and changing that many answers on
  judgement would have been a guess wearing a measurement's clothes. This
  entry said what would settle it -- "rendering a page of Channel B
  colours both ways in a real terminal and looking".

  **qtty already had an arbiter that needs no terminal**:
  `has_minimum_contrast()`, the section 6 rule the project treats as a
  theme bug when violated. Over 4374 saturated colours (HSV saturation
  above 0.5), counting those whose quantised result has no contrast
  against the background:

  | background | RGB nearest | Lab nearest |
  |---|---|---|
  | black | 470 | 1018 |
  | white | 288 | 228 |

  **Lab more than doubles the failures on a dark ground** and gains a
  little on a light one, and terminals are mostly dark. So the answer is
  the opposite of the 256-colour case, and that is not a contradiction:
  among 240 candidates the perceptually nearest entry is close in every
  respect, and among sixteen it is often a dark chromatic one with no
  luminance left. The entry's own instinct -- that Lab "picks the dark red
  where RGB picks the bright one, which is arguably the worse answer for a
  foreground" -- was right, and is measured now rather than argued.

  Two checks name it rather than asserting a threshold, because a count of
  470 is this measurement's arithmetic and not a property to hold:
  `rgb(0, 15, 195)` is a saturated blue that RGB sends to bright blue and
  Lab to dark blue, and the second check says why that matters -- bright
  blue clears the contrast rule against black and dark blue does not.
  Sabotaged by rewriting the fallback to match in Lab, which is exactly
  what this entry proposed: the first check goes red.

  The Lab comparison was computed with the library's own palette --
  `xterm256_rgb()` is public -- and the copied formula was proved rather
  than trusted: all sixteen palette entries must quantise to themselves
  under both metrics, and they do.


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
rather than faith in layout compression. **The scrolling half of that is
built now** -- see §7.8. `QComboBox`'s internal popup is
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

  **And there was a third mouse fault in the same function, found the same
  way the first two were: by asking what in here nothing exercises.** With
  the coverage instrument fixed to name lines rather than report a
  percentage, `input_router.cpp` still had the wheel branch unrun. The
  wheel is delivered to `childAt()`, and in a `QScrollArea` that is the
  scrolled WIDGET -- which ignores the wheel. Qt propagates an ignored
  wheel event itself, but only one the platform delivered;
  `QApplication::sendEvent()` does not. **So the wheel did nothing at all
  in a scroll area.** Measured: sent to the child, not accepted and nothing
  moves; sent to its parent viewport, accepted and scrolls a row.

  It walks up the parent chain until something accepts now, stopping at
  the input layer so an event cannot escape it. Motion, the button and the
  wheel: every one of the three was correctly parsed and delivered
  somewhere that could not act on it.

  **`on_resize()` and `on_focus_change()` had never been called either** --
  two sinks a terminal drives, one of which resizes the window to the new
  cell count, so nothing had checked that a terminal resize resizes
  anything. They are covered now, along with the quit keys and the
  five-row page step.

  **And the two halves being right separately is not the same fact as the
  chain working.** The backend delivering a resize to its sink was checked
  in one suite, the router resizing its window in another, and nothing had
  ever run SIGWINCH through a real `InputRouter` to a real window. That is
  the "correct function, unwired feature" shape again, one level up:
  neither half was wrong and the connection was untested. It is driven end
  to end now, on the pty, and sabotaging the sink fails both the unit and
  the chain.

  **The chain test failed first, and the fault was the test's.**
  `read_winch()` asks `TIOCGWINSZ` on descriptor 1, and the preceding case
  had left descriptor 1 pointing at the real stdout rather than the pty --
  so it read the wrong terminal's size and returned early. Reported
  without checking, that would have been a library defect that was its own
  plumbing. Worth recording beside the flush note below: a pty test that
  redirects descriptors has two ways to lie, and both look like the code
  being wrong.

  **It lied a second time, in the same file, to a check written after that
  paragraph was.** The odd-size sweep added a case asserting that a
  terminal reporting zero ROWS is refused; it was written just below the
  line that puts descriptor 1 back on the real stdout, so the signal
  reached a backend looking at the wrong terminal and the check passed
  **against a deliberately sabotaged guard**. The pty had stored 100x0
  and the backend went on saying 100x30, which is the right answer for a
  reason that has nothing to do with the code under test. The warning
  above was already written and did not prevent it: what prevents it is
  the sabotage run, and only because the sabotage produced no red.

  **A terminal reporting zero rows is refused now, at both of the doors
  that read a size.** `read_winch()` tested `ws_col <= 0` and not
  `ws_row`, and the constructor tested `ws_col > 0` and not `ws_row` --
  the same asymmetry twice, in the two places a size arrives. Zero rows
  with a good column count is what `stty rows 0` produces, and it lands
  where zero columns would have: a frame with no cells, whose
  rasterisation is a null `QImage` that `QPainter` refuses to open and
  then warns about on every call, into the stderr that is the terminal.
  Both are checked, each against its own door, and the constructor's case
  needed a second backend built while the pty was already degenerate --
  a resize guard cannot answer for a program that started that way.

  **And `FrameScheduler::render_now()` says the same thing for every
  backend**, including one an application injects through `exec()`: a
  terminal with no cells is given no frame. Paired with a sized backend
  that still gets one, because "presented nothing" is also what a
  scheduler that never presents anything produces.

  Two things about writing those tests are worth keeping, because both
  made a test lie before it told the truth. `qApp->quit()` leaves the
  application in a state where a later `processEvents()` need not deliver,
  so a case that quits and then expects typing asserts the order it
  happens to have been written in. And a child widget created AFTER its
  parent is shown is not visible until shown itself -- which reads as "the
  key was not delivered" and is not.

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

  **And the same misfire was still there one layer down, hiding behind
  the fix.** `AnsiBackend` wrote `m.button = (b & 3) + 1`, and SGR's
  button word does not fit in two bits: **bit 128 marks buttons 8..11**.
  Masked off, `128` -- a back-button click -- became button 1 and so a
  LEFT press; `129`, forward, became a middle press; `130` became a
  **right** press, which now opens a context menu. The whole word,
  decoded and printed:

      word  what it is          button  motion press wheel
         0  left press               1       0     1     0
         2  right press              3       0     1     0
         3  low bits 3 (none)        4       0     1     0
         4  shift+left               1       0     1     0
        16  ctrl+left                1       0     1     0
        35  bare motion              4       1     0     0
        64  wheel up                 0       0     0     1
        66  wheel left               0       0     0     1
       128  button 8 (back)          1       0     1     0
       129  button 9 (fwd)           2       0     1     0
       130  button 10                3       0     1     0

  **This had been written up as an open question about the router's
  fallback**, on the strength of a probe that passed button 4 and got a
  left click. That attribution was wrong and is withdrawn: `qt_button()`'s
  "anything unrecognised is left" never saw an extended button, because
  the collapse happened in the decoder one layer earlier. The fallback was
  harmless; what it was defending was not. **Where the information is
  thrown away is where it has to be kept** -- the same sentence the
  clipping and the picture-rule entries arrived at, and the third time
  this document has had to say it.

  Fixed at the decoder: bit 128 gives 4..7, low bits 3 gives 0, and
  `qt_button()` carries those to `Qt::BackButton`, `Qt::ForwardButton`,
  the two extras and `Qt::NoButton`. Faithful delivery rather than
  dropping, which is what this tree already decided when it stopped
  sending `Qt::LeftButton` for everything -- a widget ignores a button it
  does not handle, and nothing activates spuriously.

  **Two more defects are in that table**, and each was taken as its own
  change rather than swept into the button fix.

  ~~**Modifiers are decoded and discarded**: 4, 16 and 20 are shift-,
  control- and control-shift-click, and all three come out as a plain
  left press.~~ **Fixed.** Bits 4, 8 and 16 are shift, meta and control;
  `MouseEvent` carries them the way `KeyEvent` already did, and every
  mouse event the router builds -- press, move, release, wheel and the
  synthesised `QContextMenuEvent` -- was built with `Qt::NoModifier` and
  now carries them too.

  What that was worth is best said as the behaviour rather than the
  bits: **an item view could not be extend-selected or toggle-selected
  from a terminal at all.** The check is the pair -- the same two clicks
  leave ONE row selected without control and TWO with it -- because a
  check on the second alone would pass against a view that never
  deselects anything.

  The modifier map had three copies by then, one per key path, and a
  fourth caller was the moment to stop: `qt_modifiers()` is one function
  now, for the reason section 0d records about the mnemonic stripper
  having grown three spellings of one rule before anyone counted.

  ~~**The horizontal wheel is delivered as a vertical one**: 66 and 67 are
  wheel-left and wheel-right and decode identically to 64 and 65, so
  scrolling sideways scrolls up and down.~~ **Fixed.** Bit 1 of the button
  word is the axis, and the decoder read bit 0 alone. `MouseEvent` gained
  `wheel_x` as a second field rather than `wheel` becoming a `QPoint`,
  because `wheel` is public API and additive is the change that keeps
  every existing caller compiling.

  The direction was measured rather than read off the documentation: a
  positive `angleDelta().x()` moves a horizontal scroll bar's value
  **down** by five, which is what makes SGR's wheel-left scroll left. The
  check is the pair -- the bar moves in opposite directions for the two
  horizontal reports, and **does not move at all** for a vertical one.
  The second half is the one the old code failed, and a check on "the bar
  moved" would have passed against it.

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

**The mode-list derivation is defended by a test now, not by my having
noticed.** `queried_modes()` moved into `term_caps.cpp` beside the query it
reads, which is where the knowledge belongs, and three assertions hold it
to the query: the count matches the number of `$p` requests -- arrived at
without walking the string the way the function does, so a derivation that
dropped entries would fail rather than return a plausible shorter list --
every mode returned appears in the query, and **1002 is named as the one
that must not be there**, since qtty sets it at startup and never asks
about it. Sabotaging the walk to keep one mode fails the count; injecting
1002 fails all three.

That is the fix for the fault below turned into something that cannot
silently come back. The report was wrong once because two lists of one
thing drifted; the test is what makes the single list stay single.

**Checked and found absent:** the beerssh session's container-cleanup fault
-- a shell trap cannot preempt a foreground command, so a `trap` around a
blocking `docker run` fires only on the path where the work already
finished -- has no counterpart here. This tree has no `trap` anywhere and
one shell script, the commit-msg hook, which spawns nothing. The bound on
the test suite is inside the binary rather than in a recipe, which is what
`running-code.md` prescribes and is why no trap was reached for.

**`qtty-negotiate --probes`: what the terminal ANSWERED, not what qtty
concluded.** Built because the beerssh session asked for the one check it
cannot make from inside its own tree -- that switching a feature off
silences the *answer* rather than only the behaviour, since a terminal that
still replies while ignoring the payload gets its lie cached. Its own test
asserts what it believes the probe looks like, which is one witness wearing
two hats.

The ordinary output cannot answer that and should not: under the asymmetry
rule an unverifiable signal may only say yes, so silence and a definite no
reach the same conclusion there on purpose. `--probes` reports each probe
separately and prints the raw reply, so a reply that shrank rather than
vanished is visible as bytes.

`collect_caps()` gained an optional `raw` out-param rather than the tool
doing its own I/O -- same query, same parser, one implementation.

**Two instrument errors, both caught before the results were reported**,
which is the part worth keeping:

- The first run said every probe was silent while the same run negotiated
  Kitty. The backend sets termios raw before it asks; a tool-level probe
  does not, so the replies sat in the line discipline waiting for a newline
  that no terminal sends after a query. **A report that says "silent" when
  the terminal answered is worse than no report.**
- The mode list was written out a second time in the tool and included
  1002, which `caps_query()` does not ask about. The report therefore said
  "silent" about a question nobody sent, and silence was the only possible
  answer -- one edit from being sent to the terminal's author as their
  defect. The list is derived from the query now: two lists of one thing
  drift, and this one had drifted before it was ever run.

The measurements are in `doc/beerssh.md`. Every announcement-style
capability goes silent when switched off; the DEC modes answer 0 instead,
and **that is correct** -- silence is the right answer for a claim, a
definite no for a question that has one.

**The beerssh interoperability table was re-taken, and it holds.** Every
row was measured against beerssh at `3525de0`; its HEAD is now `395f354`,
five commits later and including terminal-facing work. All five
negotiation rows reproduce, as do the background, the palette entries and
the cell size recorded beside them.

Worth doing because it cost a minute, and it cost a minute because
`doc/beerssh.md` writes down the command that produced it. **A
cross-implementation claim is the kind that ages silently**: it is a
statement about somebody else's program, so nothing in this tree fails when
it stops being true, and the tests cannot notice -- they check qtty against
replies qtty's own suite wrote, which is one witness. That is the whole
argument for `qtty-negotiate` existing, and re-running it is what keeps the
second witness current rather than historical.

**A snapshot is regenerated by the code it tests, and `--record` made that
invisible.** The `claude-guidelines` session's correction generalised to a
rule worth applying here: **the intervention and the measurement had the
same author**, so nothing in the process could distinguish "they already
agreed with me" from "they did what I asked". A fixture is that shape --
the code is the intervention and the fixture is the measurement, and
`--record` hands both to one person in one minute.

**Checked first, because the shape existing is not the same as having done
it.** Both fixtures were last written 2026-08-27, before any of this
session's rendering work, so they constrained the changes rather than being
regenerated by them -- and they did constrain: the `qMax(cw, ch)` frame
experiment failed against `prefs_dialog`, which is what a fixture is for.

What was missing is the review the Makefile already claims. Its comment
says "after a reviewed change" and the code truncated the file and printed
a path, so a regression could be promoted to the expected output by muscle
memory with nothing shown. `--record` prints the differing rows now, and
reports `unchanged` without rewriting when there is nothing to bless.
Printing does not change who decides; it changes whether they can see what
they are deciding, which is the whole of what "reviewed" can mean for a
fixture nobody reads in full.

**The first attempt to prove it proved nothing**, and the way it failed is
the reason to say so: a temporary change to the unchecked-checkbox glyph
produced `unchanged`, because the gallery contains no unchecked checkbox --
and the run was inspected by grepping for the word "recording", which
found nothing and was read as nothing to see. Capturing the whole output
instead showed `unchanged` in plain sight. The proof is a glyph the fixture
actually carries: the slider handle on row 3, which prints

    recording .../widgets_gallery.txt -- 1 line(s) differ:
        3 - ────────────●─────────────────────────────
          + ────────────O─────────────────────────────

**"61 files conform" asserted more than the gate checks, and I quoted it
all day.** The shared `style_gate.py` was corrected at its source: its
structural half compares each line's tab count against what the converter
would emit, and the converter never ADDS indentation -- so a line with too
few tabs and no alignment spaces is invisible to it. Over-indentation is
caught; **under-indentation is not**, and a file with no tabs at all passed
while printing that it conformed.

The copy here is synced, and the message now says what was verified:
`61 file(s) pass: whitespace, and indentation except under-indentation,
which is not checked`. Nothing else changed -- the source gate was run
against this tree before syncing, per the rule that a tool fix which
reveals findings is applied with the source first, and it reported the same
61 files with nothing new.

Worth recording as a correction rather than a sync, because the citation
was mine: every check-in this session quoted the old line as a pass. It was
a real gate reporting a wider fact than it established, which is the third
shape in `evidence.md`'s new entry on facts recorded without their method.

**The copyright holder is named, on the surfaces qtty actually has.**
Directive of 2026-08-29, relayed because `CLAUDE.md` is read once at session
start: every private project names the holder in its `--version` output, its
About window, and its README. It is **attribution, not licensing** --
naming the holder is factual and grants nothing -- and explicitly not a
per-file banner sweep.

qtty had none of the three literally: it is a library, with no `--version`
and no About window. What it has is `version_string`, the symbol a
consuming program prints, so the line lives beside it in
`include/qtty/version.h` and reaches a person through whatever prints the
version -- which is the surface that will end up in beerssh's output. The
README carries it in its own section, deliberately separate from the
*License* section, which says the licence is not yet chosen and is not
this directive's business or mine.

`qtty-negotiate` gains `--version`, which makes the first surface real
rather than notional: it is a program somebody runs, and it now says which
qtty produced the capability report. Handled before `QApplication` and
before `prepare_environment()`, so it works in a pipe, on a machine with no
terminal to negotiate with, and without sending the startup query.

**The two copies are checked against each other**, because two places
stating one fact is drift waiting to happen: `make version-check` extracts
the line from the header and refuses if the README does not carry it
verbatim, or if the header states none. Both failure modes were exercised
-- the README altered, and the constant emptied -- rather than assumed.

**And the gate was wired into `make check`, where it had never been.** It
was reachable only by typing its name, so the version consistency it
enforces was unguarded in practice too. A gate nobody runs is not a gate.

The example application is left alone: it is a demonstration of the
library rather than a program this project ships, and the directive names
the surfaces where a person looks for the project itself.

**The interaction sweep found nothing wrong, and that is the result.** The
mirror of the rendering probes: which user actions reach a widget and
change something? Space presses a focused button and ticks a checkbox,
arrow keys step a slider, a combo box and a spin box, a click toggles a
checkbox and selects a tab. All of it worked, and it is asserted now
rather than left as a sentence -- these are the paths every application
depends on and nothing covered them, the key tests stopping at a
`QLineEdit` and the mouse tests at a button. Each assertion pins the
DIRECTION as well as the change, since a widget that moved on any key
would satisfy "the value differs".

Two behaviours were checked and left alone, because they are Qt's and not
this style's, and naming them stops the next sweep re-investigating:

- **Enter on a focused push button does nothing.** Qt routes Return to a
  dialog's default button, not to whatever has focus; a bare button in a
  plain widget responds to Space alone. Matching the desktop is right.
- **A click on a slider's groove pages rather than jumping.** Qt's
  default adds `pageStep`, which on a 0..10 range with the default step
  lands on the maximum however near the low end the click was.

The second is a real question for a terminal, and it is still the
holder's: `SH_Slider_AbsoluteSetButtons` would make a click set the value
where it landed, which suits a device with no fine pointer control. It
changes what a click MEANS rather than what it looks like, and this tree
already draws that line: `SH_DialogButtonLayout` is pinned with the note
that "changing it would be the decision, and this is not that". Pinning a
hint so the desktop cannot move it is not the same act as moving it.

**What was missing was the measurement, and it is here now.** A 0..100
slider over a 20-cell groove, clicked at four positions:

| cell | left | middle | right |
|---|---|---|---|
| 2 | 0 | 6 | 0 |
| 5 | 10 | 24 | 0 |
| 10 | 10 | 54 | 0 |
| 15 | 10 | 83 | 0 |

Three things follow that the entry did not say.

**Absolute set already works, on the middle button.** Fusion answers
`Qt::MiddleButton` for the hint and qtty's mouse routing carries the
button, so 6, 24, 54 and 83 track the click across the groove. The
capability is not missing; only the button is in question. Nothing
exercised it until now, so a change to the hint could have taken it away
unnoticed -- it has a check now.

**A left click lands in the same place wherever it is clicked.** 10 at
cell 5, 10 at cell 10, 10 at cell 15: one `pageStep` from where the handle
was, and no further. On a terminal that is the whole of the interaction --
there is no repeat-on-hold here -- so a click at three quarters of the
groove sets a tenth.

**And the naive change costs the behaviour that works.** The hint is a
set of buttons: returning `Qt::LeftButton` alone removes middle-click
absolute set, which the two checks catch together.

That much was reasoned from the hint's semantics. **Applied and printed,
it is worse than reasoned and the recommended fix is not what it looked
like.** The same four clicks under each candidate, with the hint
temporarily answerable from the environment. Each cell is the value a
click leaves, as left / middle / right:

| cell | current | `LeftButton` | `Left\|Middle` | and `PageSetButtons` = right |
|---|---|---|---|---|
|  2 |  0 /  6 / 0 |  6 / 0 / 0 |  6 /  6 / 0 |  6 /  6 /  0 |
|  5 | 10 / 24 / 0 | 24 / 0 / 0 | 24 / 24 / 0 | 24 / 24 / 10 |
| 10 | 10 / 54 / 0 | 54 / 0 / 0 | 54 / 54 / 0 | 54 / 54 / 10 |
| 15 | 10 / 83 / 0 | 83 / 0 / 0 | 83 / 83 / 0 | 83 / 83 / 10 |

**`LeftButton` alone does not merely lose absolute set on the middle
button -- it makes the middle button inert.** Middle goes to 0 at every
cell: not paging, not setting, nothing. That is a stronger objection than
the one recorded here, and it is measured rather than inferred.

**And `Left|Middle` does not add a behaviour, it removes one.** Qt tests
the absolute set first, so putting the left button in it takes the left
button OUT of `SH_Slider_PageSetButtons` -- after which **no button pages
at all**. The phrase "wants `Left|Middle`, not a replacement" reads as
though everything is kept and something is gained; the third column is
what that actually costs.

**Which surfaces a third option the entry never named**, and it is the
one that makes the trade avoidable. `SH_Slider_PageSetButtons` is a
separate hint and can be answered separately: with it returning
`Qt::RightButton`, the fourth column keeps absolute set on both the left
and middle buttons AND keeps paging, on a button that currently does
nothing at all on a slider. Whether paging is worth keeping is a
different question -- this document already argues it is nearly useless
on a terminal, since a click at three quarters of the groove sets a tenth
and there is no repeat-on-hold -- but the holder should decide that
knowing it need not be given up.

Still the holder's. What has changed is that all four answers are
printed, so the choice is between measured behaviours rather than between
one measured and three imagined.

**One instrument note, because it nearly went into the table above.** The
first run of this measurement reported the right button paging like the
left, which would have been a routing defect. It was the probe: qtty
numbers buttons 1 left, 2 middle, **3 right**, and the probe passed 4.
`qt_button()` maps anything unrecognised to `Qt::LeftButton`, so the
probe invented a behaviour that does not exist and put it in a table;
only the previously recorded column disagreeing caught it.

**That was written up as an open question about the fallback, and the
attribution was wrong.** Following it found something larger and it is
in §7.1: the fallback was never the mechanism, because the *decoder*
folds the extended buttons onto the first three before the router sees
anything.

**Qt's standard iconography has no route to a glyph, and that is a
decision rather than a defect.** Three measurements, taken because a
dialog probe looked like it had found a missing icon:

- `QIcon::fromTheme("dialog-warning")` is **null with an empty name** here.
  qtty pins the platform theme off, so nothing resolves.
- `standardIcon(SP_MessageBoxWarning)` is **not null, has three sizes, and
  its name is empty**. Qt's built-in fallbacks are pixmaps with no
  identity.
- `glyph_for()` is keyed on `QIcon::name()`. So neither route reaches it.

**And the suspected defect was smaller than it looked.** A `QMessageBox`
severity icon is not missing: it arrives as a 5x3-cell **image placement**,
which a graphics-capable terminal draws as a picture and a text-only one
composes as half-blocks. `render_once()` collects placements without
rasterising them, so a probe reading `to_text()` sees a blank -- the probe's
limit, not the library's. Recorded because the first reading of that probe
was "the icon is missing", which is a mechanism, and it was wrong.

What is left is real but narrow, and it has now been **printed rather
than guessed**. The sentence here used to say a 5x3 mosaic of a warning
triangle was "unlikely to be legible", which was a prediction. It is
worse than the prediction, and for a reason worth having.

The pixmap is a perfectly good warning triangle -- 644 of 2304 pixels
opaque, dumped at half resolution:

    ...........##...........
    ..........####..........
    .........######.........
    ........###...##........
    .......####...###.......
    ......#####..#####......
    .....##############.....
    ....#######...######....
    ...##################...

The exclamation mark is the two columns of background inside the body.
Composed into the 5x3 cells the layout gives it, a text-only terminal
gets:

     ▄
    ▄ ▄
    ▀ ▀

**The mosaic is faithful and still unreadable**, which is the finding.
The composer is not losing anything -- it samples five columns by six
half-rows and reports what is there. What is there, at that sampling, is
a triangle whose distinguishing feature is a hole one cell wide, so the
hole eats the middle of every row it touches and the outline stops being
an outline. An icon can survive a coarse mosaic when its meaning is its
silhouette; this one's meaning is its interior.

The same question covers `QDockWidget`'s title buttons, which render as
two identical empty brackets -- a close and a float that cannot be told
apart, since neither carries text and neither icon has a name.

**The option recorded here was "one small table", and it was tried.** The
claim was that mapping `QStyle::StandardPixmap` values to glyphs would
reach every standard icon at once and close both symptoms together. It
closes neither, and they are not one cause.

`GridStyle::standardIcon()` **is** consulted -- 28 calls across the suite,
`SP_MessageBoxWarning` for the `QMessageBox` and two title-bar values for
the `QDockWidget` -- so the hook exists. Overriding it to return an icon
that paints a glyph changed nothing on screen, because **Qt rasterises an
icon to a `QPixmap` before the style draws it**. Traced at
`drawItemPixmap()`, which is where they arrive:

- The severity icon arrives as **48x48 pixels with no identity left**. A
  glyph cannot travel inside a `QIcon` to a cell renderer. Making one
  arrive would mean identity surviving the rasterisation -- a
  `QPixmap::cacheKey()` to glyph registry, minted where the icon is built
  and consulted in `drawItemPixmap()`. That is a mechanism, not a table.

  **Two halves of that sentence have since been measured, and they point
  opposite ways.** "Minted where the icon is built" was wrong about
  *where*: `standardIcon()` is not the mint point. One `QIcon` caches its
  own pixmap, so asking it twice gives one identity -- but a second
  `standardIcon()` call for the same value re-renders to a different one,
  a different requested size gives a third, and the identity the message
  box actually places matches **none** of them. A registry minted at
  `standardIcon()` would resolve nothing. The mint point has to be inside
  the icon: a `QIconEngine` that registers each pixmap it returns, which
  is the object `QIcon` asks once per size, mode and state and then
  caches. That relationship is pinned by a check now, and it goes red the
  day Qt gives standard icons a stable identity -- which is the day this
  is worth re-opening.

  **The "lifetime question in the render path" is answered, and it was
  already answered under another name.** `CellImage::key` *is* the
  pixmap's `cacheKey()`, carried from the placement to the backend, and
  `suite_placements` has asserted "identity is pixmap cacheKey" since the
  upload-once work -- so the channel a glyph registry would ride already
  exists and is held by a check. Measured on top of that: a pixmap minted
  by hand arrives at the placement with its key intact, a dead pixmap's
  key is not reused, successive keys differ by exactly 2^32 (a serial in
  the high word, so recycling would need the serial to wrap), and a
  shallow copy shares the key while a deep copy does not. A
  cacheKey-to-glyph map therefore cannot mis-resolve; it can only grow,
  bounded by icons times sizes times modes times states.

  So the decision is smaller than it was written: not "is this
  buildable", which is now a `QIconEngine` and a map with no unanswered
  mechanism, but **should a warning triangle become a glyph**. That is
  the holder's, and the legibility measurement above is what it should be
  taken against.

  **What it costs is measured too, and it is a row.** Sweeping
  `PM_MessageBoxIconSize` and printing the dialog at each size:

  | icon px | dialog, cells | placement | what shows |
  |---|---|---|---|
  | 57 (3 rows) | 37.4 x 4.0 | 6x3 | mosaic |
  | **48, Fusion's** | **36.5 x 3.5** | 5x3 | mosaic |
  | **38 (2 rows)** | **35.5 x 3.0** | 4x2 | mosaic |
  | 30 | 34.7 x 2.6 | 3x2 | mosaic |
  | 20 | 33.7 x 2.1 | none | `▒▒` |
  | 10 | 32.7 x 2.0 | none | `▒` |

  Three things fall out. **Fusion's 48 is 2.53 rows and 4.8 columns**, so
  the dialog asked for three and a half cells and the half was
  unusable -- `PM_MessageBoxIconSize` was the one metric in `GridStyle`'s
  switch that still answered in Fusion's pixels. It answers `2 * ch` now,
  which keeps the icon a picture (4x2 cells, above the placement rule's
  two-in-each-direction) and makes the dialog a whole 3.0 cells. That is
  the rounding every other metric here already does, and it is not the
  glyph decision.

  **The icon costs exactly one row**, then: 3.0 cells with it against 2.0
  without. That is the price a glyph would save, and it is worth knowing
  before choosing, because on an 80x24 terminal a dialog is a large
  object.

  **And a small picture is not on the menu.** At 20 px and below the icon
  stops being a placement and becomes the substitution block -- `▒▒`,
  then `▒`. So a glyph would not be competing with a picture at that
  size; it would be competing with a coloured smudge. Which is the same
  observation the entry above reaches from the other end: **this decision
  and the "too small to be a picture" one are one question seen twice.**
  A glyph chosen in `GridStyle` is unconditional and costs a graphics
  terminal its real icon; a glyph chosen where the terminal's capability
  is known costs nothing. Answering where the rule lives first makes this
  one cheap.
- ~~The dock buttons arrive as a **0x0 pixmap into a -2x-6 rectangle**.
  There is no icon area at all, so no iconography decision can put
  anything in one. That is a sizing fault and a different question.~~
  **Fixed, and it was not an iconography question at all.** Two things
  in that sentence were wrong and both mattered.

  The **-2x-6 was qtty's own arithmetic, not Qt's**. The button is 18x14
  pixels; `PM_ButtonMargin` is `cw`, which this style sets to 10; 18-20
  and 14-20 are the numbers that were read as evidence of a Qt sizing
  fault. Measured rather than inferred this time, and the path is not
  even taken -- `CC_ToolButton` is drawn whole here and returns before
  any of it.

  And **the identity was there the whole time**. Both buttons carry Qt's
  own object names, `qt_dockwidget_floatbutton` and
  `qt_dockwidget_closebutton`. No icon name, no text, no tool tip -- but
  a name on the widget, which is identity the style can read without a
  picture. That is the same shape as the `arrowType` case in
  `tool_button_label()`, which this tree already decided: **a button with
  no text and no reachable icon gets a glyph chosen by the style, when
  the widget itself says what it is.** The close mark is the one
  `PE_IndicatorTabClose` had already chosen, for its own recorded reason
  -- a button drawn as a pixmap that says nothing about what pressing it
  does.

  What was actually missing was **room**, and the fix is a rule the
  rendering side already states in the other axis. Qt sizes these buttons
  in pixels, at not quite two cells each; two cells hold `[]` and nothing
  else, so the entire budget went on chrome and the result was a pair of
  empty boxes. **A bracket goes where a bracket fits**: below three cells
  the brackets are dropped and the content keeps the cells. The
  one-row line edit keeps its two brackets for the opposite reason and
  the same principle -- an empty box is not visibly a control either.

      Panel                     ↗ ✕

  Both halves are held by a check: one that the two buttons are told
  apart, one that no empty bracket pair survives. **They do not separate
  cleanly, and the sabotage is what showed that rather than hid it.**
  Returning nothing from the object-name branch reddens the first alone,
  which isolates it. Restoring the always-bracket threshold reddens
  **both** -- because a bracketed two-cell button has no room left for
  the glyph either. That is not a weak check; it is the two halves being
  one fix, and the pair still says which half broke: only the first going
  red means the glyph, both going red means the room.

So the entry split, and the sentence in §0e that said the two symptoms
are the same cause is withdrawn. Both were pinned by checks as results
rather than gaps, with the note that either goes red the day somebody
fixes its half -- **and the dock half has since gone red and been
replaced**, which is the paragraph doing its job rather than a
regression. What remains pinned as a result is the severity icon: a
picture of at least two cells in each direction.

What has not changed is whose the decision is. **The copyright holder's**,
and the alternative measured and closed above -- returning themed icons so
the existing name-based registry resolves them -- still does not work.

~~Also observed, and left alone deliberately: an **empty `QLineEdit`
renders as nothing at all**.~~ **Answered, and by the rule the tree had
already stated twice.** A one-row line edit is bracketed now, the way the
combo box and the spin box are, for the reason written where those were
decided: the control has to be visibly a control, and at one row a frame
cannot say so. An empty field shows its boundary rather than nothing, so a
form of them is no longer a blank screen.

**The cost this was deferred over was an artefact of the first attempt.**
Bracketing every `PE_PanelLineEdit` gave an editable combo box and a spin
box a second closing bracket inside their own, because each contains a
`QLineEdit` that reaches the same primitive -- measured, and recorded here
as the price. `QLineEdit::hasFrame()` separates them, with **no class list
and no parent test**: a plain edit answers true, and those two answer
false because they draw the boundary themselves. Measured on all four:

    [plain edit                  ]
    [editable combo             ▾]
    [42                         ±]
    no frame

The last is `setFrame(false)`, obeyed. Its check is kept and marked as one
that **cannot fail against this code** -- Qt skips the primitive entirely
for an unframed edit, so the gate is never asked, confirmed by a sabotage
that reddened the double-bracket check and left this one green. It holds a
property rather than a mechanism: an application that asks for no frame
must not be given one, whichever layer keeps that promise.

**The dialog sweep sharpened this and it is worth reading with the above**
(§7.2): it is not only the empty case -- a one-row line edit has no
boundary with content either, and it is the only one of the four one-row
editables that has none. The bracket was implemented and measured rather
than argued about, and it costs a second closing bracket inside every
editable combo box and spin box, each of which contains a `QLineEdit` that
reaches the same primitive. That cost is what makes it a decision.

**The pixmap rule predicted the next two, which is the first time this
sweep stopped being a search.** *Anything the base style draws as a pixmap
arrives here as a shaded block* was written down after the sort indicator;
probing what else Qt draws that way found a **closable tab's close button**
offering a shaded block to click on, and -- beside it, unpredicted -- an
**arrow-type tool button rendering as an empty pair of brackets**. It has
no text and no icon, and nothing asked what kind of arrow it was, so the
scroll and navigation buttons Qt builds for itself drew nothing at all.

The arrow is answered in `tool_button_label()` rather than at the drawing
site, so that `sizeFromContents()` measures the same string that gets
drawn. The menu marker had to be taught that separately and this did not,
which is the earlier fault paying for itself.

**And the close button raised a guard question worth recording as a
choice.** `QTabBar` builds it as a private `CloseButton` sized 20x20 from
two pixel metrics nothing overrode, and places it at a pixel offset inside
the tab -- so it landed off the grid and the guard reported it, correctly,
as a widget the application cannot reach. The exemption list exists for
exactly that class.

It is answered rather than exempted, twice over: the metrics make it one
cell, which is the right size for a one-glyph button, and
`subElementRect()` snaps the rectangle Qt asks the style for. An exemption
would have silenced the report and left the button drawn half in one cell
and half in the next. **The list is for widgets nothing can place; this one
asks the style where to go**, so the style answers.

**A sort indicator was a shaded block, and a checkable menu item had no
mark at all.** Two more of the same family, from a fourth probe run.

The sort indicator fell through to the base style, which draws one as a
**pixmap** -- so it reached the cell painter as an image too small to place
and came out as the tiny-icon substitute. Ascending and descending carried
the same meaningless mark, which is worse than none: it reads as a
rendering fault rather than as information. **Anything the base style draws
as a pixmap arrives here as a shaded block**, which is the general shape to
look for and is how the tab-close button and the dock-widget handles will
present when somebody renders those.

**`SortDown` is ASCENDING**, measured rather than read off the enum:
`QHeaderView` sets `sortIndicator` to `SortDown` when the order is
`Qt::AscendingOrder`, so taking the name at face value drew an A-to-Z
column with a downward arrow -- confidently backwards, which is worse than
the block it replaced, because the block claimed nothing. The first version
did exactly that and the test caught it. Asserted as the two orders
**differing**, since a check that an arrow appears passes for a style that
draws the same one both ways.

`CE_MenuItem` ignored `checkType` and `checked` entirely. A menu is where a
toggle usually lives, and "Wrap" with no tick beside it says nothing about
whether it is on -- the state was in the action and nowhere on the screen.
One cell now, and the shape says which kind of toggle it is: a tick for an
independent one, a bullet for a member of an exclusive group, matching the
checkbox and the radio button this style already draws. The cell is
reserved whenever the item is checkable, so a run of checkable items
aligns. Asserted on all three cases -- ticked, checkable-but-unchecked, and
ordinary -- which is what makes it an assertion about the mark rather than
about a tick appearing somewhere.

**A tab bar down the side rendered as `[...`.** Qt hands a West or East
tab its contents size already rotated -- narrow and tall -- so taking that
width gave a tab two cells wide and a label elided to nothing. Measured
from the text instead, as the tool button already is, the bar becomes as
wide as its longest label and each tab is one row: a column of names beside
the pane, which is what a terminal application with side tabs looks like.
Asserted on both the whole label and the row it lands on -- a strip that
merely fitted would put both tabs on row 0, and a column of elided tabs
would stack correctly and say nothing.

**An indeterminate progress bar read as stalled.** `minimum == maximum` is
Qt's way of saying the length of the job is unknown, and it drew a bar at 0%
with "0%" written across it -- which is the one thing it is not. A distinct
shade and no number now, without inventing an animation a frame-diffing
renderer would repaint the screen for. Paired with a bar whose length is
known, so "shows no percentage" is not satisfied by a style that never
shows one.

These two were seen in the same probe run as the three below and reported
as "recorded rather than fixed" when they were neither -- they had been
mentioned in conversation and written nowhere. That is the gap this
document exists to close, so they are fixed and here.

**A disabled control looked exactly like an enabled one**, at every
control. Qt reports the state in every option it hands the style, and the
style tested `State_Enabled` **at no site at all** -- so a button nobody
can press drew the same characters in the same colours with no attribute, a
greyed menu item read as available, and the only way to find out was to
click and have nothing happen. Dim rather than a colour: a terminal's dim
is one SGR that composes with whatever the theme already chose, while a
grey would have to be picked against a background this style does not know.

Asserted on the **attribute**, which is the whole of the change --
`to_text()` shows characters, so a check on the rendered string passes
against the bug and would have gone on passing -- and in both directions,
since "the disabled one is dim" is satisfied by a style that dims
everything.

**A checkable item view drew no check at all.** An item view whose items
are checkable showed the text and nothing else, so the state a user opens
such a list to set was invisible, with no second place to read it from --
unlike a checkbox, which at least has a label beside it.

**A vertical progress bar was drawn as a horizontal one in its top row**,
leaving the rest of the widget blank: a meter reading nothing, in the
orientation an application picks precisely because it has a tall space to
fill. It fills upward now, which is what a column of liquid does and what a
bar drawn from the top gets exactly backwards -- and the test asserts the
direction, since a bar filling downward passes any check that only counts
solid cells.

All three came from the same pass as the two below: render widget
configurations nothing exercises, and read the output. **Five defects from
two probe runs**, none of them found by looking for a particular fault.

**Every framed scroll area lost the bottom rule of its own border**, and
the corners stayed -- which is why it read as a rendering quirk rather than
as the arithmetic fault it is. Found by rendering widget configurations
nothing exercises and reading the output, rather than by looking for it.

`to_cells()` rounded the **extent** rather than each edge, so it lost where
a rectangle actually sits. A scroll area's viewport is inset by one frame
width, which is a whole column but only half a row on a cell taller than it
is wide: `qRound(94 / 19)` is 5, so a viewport covering four whole rows and
47% of a fifth claimed all five, and its background fill erased the rule
the frame had just drawn. Text edit, plain text edit, list, table, tree --
every framed `QAbstractScrollArea` had it.

Rounding each edge and taking the extent from the two fixes it, and **costs
nothing**: the whole suite passes unchanged, snapshots included. Two other
candidates were measured first and both were worse. Returning `qMax(cw, ch)`
for `PM_DefaultFrameWidth` gives a correct border and takes a column from
each side, and a small editor's text stopped reaching the cells at all.
Returning 0 and dropping the frame costs the border entirely. Neither is
needed: the metric was not the thing that was wrong.

Asserted as the **whole rule** rather than a corner, because the corners
survived the bug -- a check for them passes against the broken frame -- and
paired with the top border, which never broke, so a frame that stopped
being drawn at all fails rather than passing by drawing nothing anywhere.

**A tristate checkbox's middle state was invisible.** `PartiallyChecked`
arrives as `State_NoChange`, which the style did not test for, so it drew
`[ ]` -- identical to unchecked. The state existed in the model and not on
the screen, and the only way to find it was to click and watch the box
cycle somewhere unexpected. It draws `[-]` now, and the assertion compares
all three states against each other, which is what cannot be satisfied by a
widget drawing one glyph for two of them.

**A horizontal scrollbar had never been drawn.** Every scrollbar test in
this tree is vertical, so the `else` arm of the one loop that places a
thumb had never run -- and the two arms are not symmetric, one walking rows
and the other columns, so a fault in either is invisible from the other.
Asserted by the thumb **moving** along the row between value 0 and value
100: merely drawing one satisfies a check for the glyph, and a horizontal
bar that placed its thumb by row would draw an identical row at every
value.

**The idle heartbeat had never ticked in a test.** Its comment says it
catches timer-driven updates, and that is a real class: the compositor
paints a widget by calling `render()` on it directly, so a widget whose
output changes without Qt posting an `UpdateRequest` -- a clock, a meter
reading a sensor, anything drawn from state rather than from a repaint --
produces a different frame with nothing to say so. Without the 100 ms tick
nothing asks for that frame and the screen sits still. Not starting the
timer fails it.

Also covered: a spin box tall enough to be framed rather than bracketed
(every other test builds the one-row form, which takes the other arm); a
mnemonic opening a submenu whose owner is **not** a menu bar, asserted on
where it opens rather than that it opens, since the origin is what a null
owner produces; the subcontrol rects a terminal answers for itself; and a
wide cluster through the rasteriser, whose background fill is twice as wide
as an ordinary cell's and had only ever been given narrow ones.

**Why the last lines will not close, measured rather than assumed.** The
residue is not laziness and not reachable behaviour:

- **`D0` destructors.** `gcov -f` reports two destructors per polymorphic
  class -- `D2`, the complete-object one, and `D0`, the deleting one that
  runs when an object is destroyed through a base pointer. For
  `InputRouter` D2 is 100% and D0 is 0%, because nothing heap-allocates a
  router and deletes it polymorphically. Both are attributed to the same
  source line, so the line reads as uncovered. Same for `Overlay` and
  `CellPaintDevice`. **Line coverage cannot reach 100% for a polymorphic
  class that is only ever stack-allocated**, which is worth knowing before
  anybody chases it.
- **`qFatal`**, which aborts, and the SIGWINCH pipe failure path, which is
  inert by construction -- `F_SETFL` does not fail on a descriptor `pipe()`
  has just returned.
- **The four font-guard branches**, measured unreachable on this engine and
  recorded above with everything that was tried.

98.45% before this round's additions, and what remains is the list above.

**A paste carrying an escape had never been sent**, which is the case
bracketed paste exists for. Everything between the brackets is text,
including bytes shaped like a control sequence -- a terminal that decoded
them would let anything a user pastes drive the application, which is why
the mode was invented. Both halves are asserted: that the text arrives
whole says the escape was kept, and that **no key arrives** says it was not
also obeyed. The second is the one that fails without the branch, because
then the paste is three fragments and an Up key.

**A terminal going away** is the other end of the same descriptor.
`read()` returns 0, and the backend turns that into Ctrl+D rather than
returning -- an EOF descriptor is permanently *readable*, so a notifier
over one fires for ever and a backend that merely returned would burn a
core doing nothing.

**Half-blocks blend over the terminal's own background**, and the first
draft of that assertion was wrong in a way worth keeping: it used a fully
transparent image, which is not painted at all. Correct behaviour, and the
frame came back blank and said so. With half alpha the discriminator is the
**shape** of the result rather than an exact byte -- the terminal's
background is a neutral grey, so a pure red blended over it leaves green
and blue equal, while the grey this used to guess, `(16, 20, 24)`, is not
neutral and no blend over it can produce equal channels. That holds
whichever way the arithmetic rounds.

**The terminal's own low sixteen are adopted only when it answered for all
of them**, and the refusal is the half worth asserting -- the half a test
written to the feature's name would skip. Fifteen out of sixteen is
refused; sixteen is adopted. Sabotaging the completeness check fails the
first, and never adopting fails the second.

**Four branches of the font guard cannot be reached here, and that is a
measurement.** `grid_font_problem()` refuses a fractional line height, a
fractional advance, and either at zero or less; only its fixed-pitch
message had ever been produced by a test. Every attempt to reach the rest
failed and the attempts are worth recording, because the next reader will
otherwise repeat them: letter spacing, which
`QFontMetricsF::horizontalAdvance(QChar)` ignores entirely -- it applies
when laying out a run, not to one character; stretch at 50, 62, 75 and 150;
stretch at 1, 3, 5 and 8, which the engine clamps so the advance never
falls below 1; and fractional point sizes of 10.5, 11.3 and 13.7. **Every
one gave a whole-number advance and a height of 19.**

They are not dead code. An engine with subpixel metrics or a fractional
device pixel ratio produces exactly what they refuse, which is the case
`prepare_environment()` pins `QT_SCALE_FACTOR` to avoid -- so the branches
guard the configuration this machine is deliberately not in. Left uncovered
and said so, rather than left as a silent gap.

**Four things that were reachable, and one of them a feature.** A toolbar
**separator** had never been drawn -- `QToolBar::addSeparator()` goes
through `PE_IndicatorToolBarSeparator`, and the alternative to drawing it
is a gap a user cannot tell from spacing. Asserted between the two actions
by index, not merely present, since a rule drawn anywhere satisfies "there
is a bar" and where it sits is the whole job.

The four arrow primitives are asserted as what they now are: **public style
API**, called through `drawPrimitive()` by an application's own widget,
since no widget in this style reaches them. All four into one buffer at
four columns, which is what catches an arrow ignoring the rect it was
given.

`Qtty::focusWidget()` has **no caller anywhere in src** -- the router and
the compositor use `QWidget::focusWidget()`, a different function with the
same name. It is public API in `grid.h`, the way an application asks who
the router considers focused while no window is ever activated, so it is
asserted as the round trip it promises rather than removed. Both
directions: "returns the last thing set" is satisfied by a function that
returns a pointer it never updates.

And the paint device's own metrics, which Qt asks for when it decides how
to scale -- a wrong answer there is a wrong decision made inside Qt where
nothing of ours can see it.

Whole tree 97.60%.

**The front door had no test at all.** `exec(app, win)` -- the
two-argument overload every application calls -- was uncovered entirely,
because everything tests the three-argument form with a `NullBackend`. The
overload that builds a real `AnsiBackend` and owns it for the run had never
been entered, and `frame_requested`, which it installs, had never been
called from a real run either.

**Why nobody had entered it is a finding rather than an excuse**, and it
took four wrong theories to reach. `exec()` writes a whole frame from the
same thread that must drain the pty, so a frame larger than the buffer
blocks in `fwrite` with its only reader stuck behind it. At 20x4 a frame is
a few hundred bytes and a drain on every timer tick keeps ahead of it.

The last theory was wrong too, and the instrument said so: the hang's
syscall was `read`, not `write`. `AnsiBackend::read_input()` does a
**blocking** `read(0)` when its notifier says stdin is ready, which is
correct for the one backend a program has -- two of them on one descriptor
both wake, the first takes the bytes, and the second blocks for ever.
`suite_backend` keeps a backend alive for its whole run, so this cannot
live there and has its own suite. qtty owning the terminal exclusively is
the design, so the test respects it rather than the product being widened
to a configuration it excludes.

Read from `/proc/<pid>/syscall` and `wchan` rather than under a debugger,
which is this workspace's own rule about crash handlers pointed at a hang.
The first attempt read the wrong process -- `pgrep` matched the shell
wrapper -- which is the instrument being wrong before the thing measured,
for the sixth time in this file.

**Two router branches nothing had taken**, both found by the same
measurement. A click landing INSIDE a popup: every mouse test here clicks
the window under one. And a popup CLOSING asking for a frame -- the showing
half is what a menu test naturally covers, because a menu that never
appears fails visibly, while a menu that never goes away is drawn from a
frame nobody asked for and stays on screen until something unrelated
triggers a redraw.

Both assertions were wrong before they were right, and differently:

- The click was to be asserted by the menu's action firing. It does not
  fire -- and **the control says the router is not what that measures**:
  sending the same press straight to the `QMenu`, router bypassed, does not
  fire it either, a `QMenu` under the offscreen platform having no popup
  grab. It is asserted now by what does NOT receive the click, paired with
  the same cell once the popup is gone, which must reach the widget
  underneath.
- The close was measured against the count taken when the menu opened, and
  **passed with the hide branch deleted**: `on_mouse()` ends by asking for a
  frame too, so the clicks had already moved the number. A counter several
  things increment says nothing unless it is read either side of the one
  under test. It then failed with the code correct, because the synthetic
  press had already dismissed the menu and closing it removed nothing --
  so the hide path gets a fresh menu that was never clicked.

Whole tree 97.04%; `application.cpp` 91.43% to 97.18%, its only remaining
line a `qFatal` that cannot be tested because it aborts.

**Every failure in this suite printed a sentence and nothing else.** The
beerssh session paid two container runs and three wrong theories for a
message that was accurate throughout -- "the underline cursor drew nothing"
could not distinguish an absent cursor from a differently-sized cell from a
probe looking in the wrong place, which were precisely the three hypotheses
it generated. **A message that cannot separate the hypotheses it will
generate guarantees the guessing.**

The `CHECK` macro, copied identically into ten suites, now prints the
condition that was false as well as the sentence. Mechanical, so it carries
a proof: the 552 PASS lines are byte-identical before and after, and the
failure path was then exercised deliberately, because a change that only
touches the failure branch is not tested by a green run.

The condition is not always enough, and the evidence for where is local:
**twice today an assertion here had to be diagnosed by adding a temporary
print.** That is the proof that what it printed was insufficient, so those
two carry their observed value now -- the rendered row against the size
hint, and which widget the focus actually reached. Both failures generate
competing hypotheses that the condition reports identically: a marker
absent, over the label, or outside the bracket; focus moved forward, or
never moved at all.

**A tool button with a menu looked exactly like one without.** Coverage
named the four `PE_IndicatorArrow*` cases as never drawn, and asking which
widget reaches them found that **none does**: the combo box, the spin box,
the scroll bar and the tool button are all drawn whole by this style, so
every obvious candidate answers for itself. The arrows are reachable only
by an application calling `drawPrimitive()` directly.

That is a coverage curiosity. The defect behind it is not: a dropdown had
no affordance at all, so the only way to discover one was to press the
button. `CC_ToolButton` drew brackets and a label; `CT_ToolButton` measured
brackets and a label. Both know about the menu now, and the pair is the
point -- a marker drawn without being measured takes a cell the elide then
pays for out of the label, which is the toolbar fault of section 17.2 in
the other order.

Asserted as the **exact row**, `[Go ▾]`, at the button's size hint. Position
is the whole of it: an arrow over the label or outside the closing bracket
satisfies any check that asks only whether the glyph is present. Sized wider
the label pads and the marker tracks the bracket, which is also correct and
is why the assertion needs the hint -- the first draft set a width of ten
cells and got `[Go     ▾]`, a probe assuming a geometry it had itself
overridden.

**And it found a `GridGuard` false positive, which is the second.** A
`QMenu` handed to a tool button sits at its construction-time 100x30 until
it is popped up, and the guard reported it -- so any application using
`QToolButton::setMenu()` gets a violation for a geometry nothing draws
from, whose only "fix" is to resize a menu that nothing reads. The
deforming-the-source failure, exactly.

Not an exemption by class, because a menu that IS on screen must be checked
like anything else. It is a question of **when**: a widget that has never
been shown has no drawn geometry. `QEvent::Show` joins the triggers so
nothing is lost -- a widget laid out while hidden and shown at an unchanged
geometry would otherwise escape, there being no resize to catch it -- and
adding it produced no new violations anywhere in the suite.

`drawPolygon` is covered too, driven at the engine rather than through a
widget, since the widgets that would reach it are the self-drawn ones above.

**The gate that decides whether to write at all was asserted on neither
side.** Every check in the backend suite asks *what* is written; `tty_out_`
decides *whether*, and gates `resume()`, `suspend()` and the geometry query.

It matters most in `suspend()`. A terminal left in mouse-reporting mode
writes an escape burst into the user's shell on every click for the rest of
that shell's life -- so the modes must go off, and must never have been set
for a stream that is not a terminal, where there is nothing to reset and
the bytes land in somebody's file.

Both sides are asserted now, and **the control is the point**: "wrote
nothing" is satisfied by a backend that writes nothing ever, so the same
two calls run down a pty first and are asserted to produce the alternate
screen and the reporting modes. One variable changes between the two runs.
Removing the guard fails exactly the two pipe checks.

**Settled, and the tree had already settled it by building on it.** The
frame output is deliberately not gated by that flag: `present()`,
`set_cursor()`, `present_pixels()`, `present_overlay()` and
`clear_overlay()` write whatever `isatty(1)` says, while setup, teardown
and the geometry query do not.

**The line is between a terminal's STATE and its CONTENT.** Setting modes
on a stream that is not a terminal changes something the program does not
own and cannot reset -- `suspend()` must never turn off a mode it never
turned on. Writing the frame is what was asked for.

**`qtty-replay --ansi` is the proof, and it is a shipped tool.** It drives
this backend with stdout redirected to a file, and the byte stream it
captures is the whole point -- doc/beerssh.md §4's parser corpus is made
that way. Measured here: a two-line script produces **1683 bytes** through
a redirect. Gating `present()` would make that tool emit nothing, so what
looked like the argument against -- "under `program > file` the frames are
escape soup in a file" -- is the use case rather than the objection. A
recording IS escape soup, and `qtty-replay` exists to make one.

`program | cat` follows from the same rule and was the other half of the
original doubt: the frames reach a terminal through `cat`, and the modes
are correctly absent because this process is not the one that owns that
terminal.

The pipe test asserts both halves together now -- a frame written and no
mode written, in one run -- so the day somebody tidies by adding the gate,
the check names what it costs. Sabotaged with `if (!tty_out_) return;` at
the top of `present()`: exactly that check goes red.

**Four methods whose whole job is to emit had no wire test at all.** Once
the keys were covered, `present_pixels()`, `present_overlay()`,
`clear_overlay()` and `set_cursor()` were every uncovered line left in the
backend -- each a public entry point reached only through callers no test
drove. The seam finding again, from the emission end.

`set_cursor()` is the one that matters most, because its **policy** is
asserted in the widget suite -- which cell, and whether a delegating widget
gets one -- and what it writes was asserted nowhere. The assertion is the
exact sequence rather than "some CUP", since the 1-based conversion is the
whole of what the function computes and an off-by-one satisfies any looser
check. Sabotaging the `+ 1`s fails it.

The three pixel tiers are asserted **together**, with a fourth check that
neither of the other two emits kitty's escape: a switch answering the same
way whatever the mode passes every individual check and fails that one.
Sabotaging sixel to emit a kitty image fails both, which is the pair
working. The overlay pair is asserted on its id arithmetic -- overlays live
in an id space above the placements, and a transmit and a delete that
disagreed would leave the picture on screen for ever.

97.03% in the backend after, from 86.70%. What remains is the palette
completion, the SIGWINCH pipe failure path (inert by construction), the
known-background half-block branch, one unmapped key and an escape arriving
inside a bracketed paste.

**Coverage named seven keys the decoder had never decoded.** Measured over
the whole library rather than guessed at: 94.5% of lines, concentrated
enough that `ansi_backend.cpp` at 86.70% held most of what was missing. Its
uncovered lines included `case 1`, `case 2`, `case 4`, `case 5`, `case 6`
and the finals `B`, `H`, `F`, `Z` -- **Home, Insert, End, PageUp, PageDown,
Down and back-tab.** Every existing test used Up, Right, Left or Delete, so
the rest had never run, in a library whose whole subject is editing text in
a terminal.

They are asserted through the chain rather than at the sink, because the
sink cannot tell a key that moved the cursor from one consumed and dropped:
Home and End by **where the next character lands**, in both the letter and
the `~` encoding since a terminal picks one and an application meets
whichever it picked; Down, PageDown and PageUp on a `QListWidget`, by the
row that ended up current; Insert at a widget that records the key it was
handed, since Qt gives it no standard effect. 88.92% after.

Back-tab needs **three** widgets. With two, forward and backward are the
same place, so the assertion would pass with `focusNextPrevChild(!k.shift)`
sabotaged to `true` -- and it does fail with three.

**The first draft asked Qt for the focus and got nil.** `hasFocus()` is
true only for the active window's focus widget, and nothing here is ever
activated -- which is exactly why the router keeps its own focus notion.
The window's own `focusWidget()` is the question this environment can
answer. A probe placed where the error cannot be expressed, again, and the
third time this suite has produced one.

**And fixing that gate turned on an unbounded upload.** The duty the
previous entry creates: a fix changes which code runs, so the path it
enables is the next thing to read.

`kitty_delete_all()` uses `d=a`, which drops **placements** and leaves the
image data -- correct, and the reason an unchanged picture is re-placed for
about thirty bytes rather than re-uploaded. But nothing ever freed the
data. A surface that animates uploads one image per distinct frame, and the
terminal kept every one of them, **in another process, for the life of the
session**; the backend's own key set grew alongside it and was never
emptied either.

It had been unreachable rather than absent. While the frame loop compared
placements by count, a picture that changed under unchanged cells was never
presented at all, so the upload path ran once per surface and the leak had
nothing to leak. **A leak whose feeder is broken measures as no leak.**

`retire_uploads()` frees by `d=I` -- uppercase, which releases the data;
the lowercase `d=i` deletes placements and leaves exactly what needs
releasing. It keeps a cap of 16 rather than freeing everything unreferenced
each frame, because a spinner cycling a handful of pictures would otherwise
delete and re-encode a full PNG for one it is about to want again -- and
animation is the only case that reaches here at all.

Asserted on the wire: twenty distinct pictures upload twenty times and free
four (the seventeenth is the first that can evict, and frames 17 to 20
evict one apiece), while four pictures cycled five times upload four times
and free none. Removing the retirement fails the first; removing the cap
fails all three.

The second assertion's first draft ran on the backend the animation had
just filled, so the frees it counted were the animation's dead keys --
correct behaviour arriving as a failure, because the assertion had measured
a shared cache rather than the property. It gets its own backend.

**A picture that changed was never sent, and both halves of the seam were
innocent.** The output mirror of the input gap below, found by asking the
same question of the other direction: every render test stops at a
`CellBuffer` and every wire test *starts* from a hand-built one, so
`FrameScheduler::render_now()` -- the code that decides whether anything
reaches the terminal at all -- sat between two exhaustively covered halves.

The gate reads

    const bool images_changed = !prev_ || frame.images.size() != prev_->images.size();

and `CellImage` carries `key`, a content-addressed upload-once identity:
**the one field that can tell two pictures apart, with the gate counting
them instead.** A `PixelSurface` repainted in place keeps its geometry, so
the cells under it diff to nothing and the count is unchanged -- the frame
is correct, `present()` would have written it correctly, and `present()` is
not called. A plot, a meter or a video still simply froze.

Neither half could see it. The compositor built the right frame; the
backend is right about writing one it was never handed. This is
*correctly computed, drawn somewhere nothing looks* with the omission in
the **gating condition** rather than the payload, which is why every test
of the payload passes.

Comparison is by content now, through a `CellImage::operator==` written
beside the fields in the manner of `Cell::operator==`. It deliberately
does not compare the pixmap: the key *is* the pixmap's identity, so
comparing the key compares the image at the cost of a `quint64` rather
than a per-frame pixel walk. Both fields are asserted by their own case --
pixels changing under unchanged cells, and a picture that **moves**, which
keeps its key because the key is the pixels. Sabotaging each field fails
exactly its own case.

**Nothing asserted that a byte on stdin becomes text in a widget.** The
decoder has 33 checks in `suite_backend` and the router has 20 of its own;
no test ran a byte through both. Every case on one side stops at a
recording sink, every case on the other starts from a hand-built event --
both halves exhaustively covered, the chain between them covered nowhere,
**in the one path a terminal library exists for.**

The exact mirror of what the beerssh session found on its side the same
hour: thirty-two router tests, a transport test whose comment called
itself "the seam the input router gets tested through", and nothing
asserting that pressing a key sends a byte.

The discriminators were chosen by the rule that afternoon produced -- the
name is the intent, the discriminator is the mechanism. A plain letter
proves almost nothing, because a path that forwarded each byte's character
would pass it with the decoder cut out entirely. An **arrow** is three
bytes that must move the cursor and insert nothing, and a **three-byte
character** must arrive as one keystroke rather than three, which its
length says and its glyph does not. Sabotaging the escape branch fails the
arrow; sabotaging the three-byte UTF-8 lead fails the character.

**A guard whose failure path was inert, found because the beerssh session
described theirs.** They fixed two the same day -- one that never fired,
one that fired and proceeded anyway -- and named the construction: *a
condition guarding an action, with nothing said when the condition cannot
be evaluated.*

qtty had the same shape and a degree worse, because there was no condition
at all:

    fcntl(s_winch_pipe[0], F_SETFL, O_NONBLOCK);   // result discarded

`read_winch()` drains that pipe with `while (read(...) > 0)` **on the GUI
thread**. A descriptor that stayed blocking does not degrade the resize
handling; it **freezes the application on the first SIGWINCH**, a long way
from here and with nothing pointing back. Carrying on without the flag
keeps exactly the hazard the line exists to remove.

The result is read now, and a failure closes the pipe and installs no
notifier: resizes are missed, which is a visible degradation rather than a
hang. Not by returning early -- that would skip `active_ = true` and leave
the backend in a state `suspend()` declines to undo, which would be a poor
way to take a lesson about inert failure paths.

No test: `F_SETFL` does not fail on a descriptor `pipe()` has just
returned, so the branch is unreachable from a suite and an entry would
report nothing for ever. Recorded in the source with that reason, which is
the third time in this document -- enough to be a category rather than
three exceptions.

**The search key, applied deliberately rather than opportunistically.**
It found two on the first two attempts, which is what makes it a key
rather than an anecdote.

The second: `suite_budget` asserts §9's damage invariant carefully -- an
unchanged tree diffs to nothing, paired with an everywhere-different frame
so that a `diff()` returning nothing whatever it was handed would fail. The
parse half, done well. **What nothing asserted is what the frame loop does
with that answer**: `FrameScheduler` skips `present()` entirely when the
damage is empty, and skipping is the whole point of computing it. A
`diff()` that always reported change would have cost a full repaint per
frame and failed no test in the tree.

Asserted now, and paired the same way `suite_budget` pairs its own --
because a scheduler that never presented anything would pass "an unchanged
tree is not presented again" and fail nothing. Both sabotages are caught:
presenting regardless fails the skip, and never presenting fails the pair.

**The search key, and a first-draft test that could not fail.** The
beerssh session turned this section's own observation into a key worth
keeping: *find the well-tested parser, then ask what consumes it and
whether anything asserts the consumption* -- because a passing test on the
easy half is what stops anyone looking at the hard one.

Pointed at the most thoroughly tested code in this tree, the cluster and
width model: **48 assertions in `suite_cells` about the parse, and none at
all about what `present()` does with a wide cluster.** Its entire handling
is one line, `if (c.width == 0) continue;`.

**The first test written for it was vacuous, and the sabotage said so.**
Removing that line left the suite green. A continuation cell's `ch` is
EMPTY, so emitting it appends no bytes at all, and with colours equal it
emits no SGR either -- the line is a no-op for everything the test was
looking at. What it actually protects is the COLOUR RUN: a continuation
carries default colours, so without the skip a coloured wide glyph is
followed by an SGR reset and then the next cell's colour again, breaking
the run in the middle of one character. Coloured, the test discriminates
and the same sabotage fails it.

That is the fourth time this session a check has had to be built from the
case where the two answers differ rather than from the case that confirms
the right one, and the first where the sabotage caught it rather than
review.

**And it happened again on the picture rule, in the plainest form yet.**
The claim to hold was "no widget degrades to a substitution block", and
the obvious check was that the widgets gallery contains no such block. It
passed. Sabotaging the threshold so that *every* image becomes a block
reddened **nine** checks elsewhere and left that one green: the gallery
contains no image at all, so no threshold could make a block appear in
it. A check on the absence of a mark, in a fixture that has nothing which
could produce that mark, is an assertion about the fixture rather than
about the code.

A replacement was written -- a bar of closable tabs and a sorted header,
widgets that DO have icons -- and it was then thrown away too, for a
second reason worth recording separately: **the property was already
held.** The tab-close pair a few hundred lines up
asserts the mark is present AND that the block is not, on a fixture that
has the icon, and removing the interception reddens it. The replacement
added only a "no placement either" clause that the same sabotage left
green, which is a clause with nothing to say. Two checks were written
before noticing that neither was needed; the census belongs in this
document, not in an assertion.

**And the modifier check failed first for a reason that was already
written down.** A control-click was supposed to add a second row to an
item view's selection; it selected one row with the modifier and one
without, which reads exactly like "the modifier never arrived" -- the
conclusion a session in a hurry writes into the record as a routing
defect. It was the fixture. A `QListWidget`'s default frame offsets the
viewport by `PM_DefaultFrameWidth` in **both** axes, which is a whole
cell here, so the click aimed at row 0 landed in the frame above it and
only the second click ever hit anything. §7.1 records that offset, from
the chat spike that had to set `QFrame::NoFrame` for the same reason.

The instrument lesson is not "read the document", which nobody
disagrees with. It is that **a fixture built out of widgets inherits
every one of their quirks**, and the ones this tree has already paid for
are exactly the ones a new fixture will hit again. Printing the selected
ROWS rather than their count is what separated the two hypotheses in one
run: `2` alone, versus `0 2`, says which click missed.

**And that check then went red for a reason that had nothing to do with
it, which is the better half of the story.** Adding `wheel_x` to
`MouseEvent` -- a field in the MIDDLE of the struct -- broke the
control-click check instantly, because the fixture built its events as a
**positional** aggregate list. Every argument after the new field
re-bound one place along, so the `ctrl` flag silently became the
horizontal wheel. Nothing warned: the list was still the right length
minus one, and the trailing field took its default.

Two things worth keeping. **A positional aggregate initializer in a test
is a hidden coupling to struct layout**, and the fix is to name the
fields; the fixtures do that now. And **the check paid for itself within
the hour** -- it was written for the modifiers and it caught an unrelated
edit to a struct it merely uses, which is the argument for a check that
goes through the real path rather than one that inspects a field.

**A guard is only testable through a value that would do damage if
obeyed**, and `qtty.cells` took two goes to learn it. The property is
ignored when its size is not positive, and the check for that was
written first as "the minimum is non-zero" -- which no `QWidget`'s
default `minimumSize()` is, so it demanded a property the fixture never
had and failed against correct code. Rewritten as "a `QSize(0, 0)`
leaves the widget as if unannotated", it passed -- and passed just as
happily with the guard removed, because `setMinimumSize(0, 0)` is what
the widget already had. Two wrong checks, both caught by the sabotage
rather than by review.

The value that works is `QSize(-1, 2)`. Qt clamps a negative minimum to
zero by itself, so the -1 is not the interesting part; what the guard
actually buys is that **the 2 is refused with it**, so a widget cannot
end up two cells tall because its width was misspelt. Writing the check
forced the guard's real purpose to be stated, which it had not been.

**And one about the instrument itself.** Two readings disagreed in the
same minute: `make test` reported one failure while running the binary
directly reported none, and a probe's `printf`s were absent from a
binary built from a file that contained them. The cause was not the
code -- **another session was building in the same tree**, rewriting
`build-test/` underneath. The tree's own `BUILD_DIR` is the answer
(`make BUILD_DIR=build-probe test`), and the general form is worth
having: in a shared checkout, a build directory is shared state, and a
measurement taken through one is only as trustworthy as the assumption
that nobody else is writing it.

**The menu bar drew no items at all, and the mnemonic fix had hidden two
more instances of its own cause.** Both came from looking deliberately,
after the beerssh session observed that a fix removing a symptom can hide
what caused it -- their own dead guard had survived exactly that way.

The cause behind the push-button ampersand was never the button: it is
that a style writing option text straight into cells never reaches
`drawItemText()`, which is where Qt strips the marker. Fixing the button
removed that symptom. Measured afterwards, **three spellings of one rule**
stood in this file: `strip_mnemonic()` on the button, an ad-hoc
`remove('&')` on the menu bar which turned `A && B` into `A  B`, and
nothing at all on the tab bar -- where `&General` rendered as
`[&Genera...`, the marker both visible AND stealing the cell that made the
label elide a character early. One spelling now.

**And `PM_MenuBarPanelWidth` was a horizontal cell used as a vertical
inset.** It shared a case with `PM_MenuPanelWidth` and returned `cw`, so
every menu bar item sat at y=10 in a bar 19 tall: each straddled two rows
and hung below the bar it belonged to, and **nothing was drawn where the
bar was**. A popup menu genuinely wants that border and keeps it; a menu
bar has none to draw on a grid. With it at zero the items land on the row
and `A && B` renders as `A & B`, which is the two fixes confirming each
other.

Same shape as `PM_ToolBarHandleExtent` being 9, and found the same way:
by rendering a widget nothing had ever rendered.

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

#### The item-view roles, swept with the probe method

§0e's first item, run. Ten configurations rendered through
`CellItemDelegate` and printed -- a disabled item, `Qt::FontRole` bold and
italic, foreground and background roles, a tristate item beside a selected
row, elision and centring in a table, an expanded tree, a decoration
supplied as a colour, a taller row asking for its text at the bottom --
each with a control run beside it wherever the answer needed one. **Six of
the ten were already right**: the tristate mark, the selection, the
elision, the tree's branch glyphs, the colour decoration falling back to
`▒`, and `AlignBottom` in a three-row item all rendered as they should.

**Two were defects, and both are fixed.**

**A disabled item's label was not dim, while the rest of its row was.**
The style fills the whole item through `with_state()` and the delegate
then wrote the label over that fill with no attributes at all, so one row
carried both answers -- measured as `2........22222222222` in the
attribute plane, dim everywhere except across the eight cells a user is
actually reading. The control settles which half was wrong: the same model
in a view with **no** delegate installed comes back dim across all twenty
cells, so the delegate was a regression against the style it defers to.
`with_state()` has moved out of `grid_style.cpp` into `cell_geometry.h`,
which is the header that exists for exactly this -- a rule both files need
and neither should keep a copy of.

**`Qt::FontRole` reached nothing.** A model marking a row bold is the
ordinary way an item view says one row differs from another, and it
arrived in the option as a font that nothing read. What makes this one
sharp is the control: a **bold `QLabel` comes out bold**, because that
text goes through `QPainter` and `CellPaintEngine` reads the painter's
font. So the rule is not "a terminal cannot do bold" but **text drawn
through `QPainter` carries the font's emphasis and text written straight
into the buffer does not**, and every site that writes into the buffer is
a place where a font stops meaning anything.

That had one measured instance outside the delegate -- a `QPushButton`
given a bold font rendered plain, because `CE_PushButtonLabel` writes the
string itself -- and it held wherever `GridStyle` writes a label rather
than passing it to `QPainter`. **It is fixed, at every such site**:
`label_attrs()` in `cell_geometry.h` takes the state and the font
together, and the push button, the tab, the menu bar, a menu item, a
header label, a tool button, a progress bar's percentage and the style's
own `CE_ItemViewItem` path all go through it. Neither snapshot fixture
moved, which is the expected result rather than a lucky one: neither has
an emphasised widget in it.

**Which font the answer comes from is the part that had to be measured,
and the menu bar settles it.** The obvious rule -- prefer the option's
font, since Qt resolves one per item -- is wrong: `QMenuBar` leaves
`QStyleOptionMenuItem::font` at the application font, so an italic menu
bar read as plain under it. The opposite rule is wrong too, because a
default menu action carries bold in the option and nowhere on the widget.
`label_attrs()` therefore takes the **union**, and the test that pins it
is the menu bar, whose option demonstrably does not carry the answer.

The fill is deliberately not included. A menu item and an item view both
fill their row with the state attributes, and adding a font's bold to a
run of spaces is nothing to look at and something to read wrongly in a
snapshot; the label carries both, the fill carries the state.

**One thing the sweep turned up is not a defect and is §7.8 arriving with
a face.** A probe put a `QMenuBar` above a framed `QTreeWidget` in a
layout and the tree lost the bottom rule of its own border, corners
intact. The cause is in the probe rather than the library: a menu bar's
height is not a cell multiple, the layout hands the widget below it a
height of 96 against a 19-pixel cell, and `GridGuard` says so by name in
the same run -- which is the instrument answering correctly and the
finding being the probe's. It is recorded because §7.8's open question
currently states the cost of `GridSnap` as a trade-off in the abstract,
and this is what the other side of it looks like on a screen: not a
widget one pixel out, but a border that is not there.

**Two more findings came out of the same sweep and are recorded rather
than fixed**, both because the fix is a decision rather than a line:

- ~~**`Qt::ForegroundRole` and `Qt::BackgroundRole` reach nothing.**~~
  **Fixed, and the deferral above was wrong.** It read as OQ-7 arriving at
  the item views -- what should a literal `QColor` become on a terminal --
  and that is not this question. OQ-7 asks which *metric* quantises an
  unthemed colour down to sixteen, and it applies equally to every literal
  colour already flowing through Channel B; nothing about an item view
  makes it harder or easier.

  **The project had already decided this one, under a different name.**
  `CellPaintEngine` matches a colour against the palette roles, asks the
  theme what that role looks like on a terminal, and passes anything with
  no role behind it through as the application's own -- which is why a
  `QLabel` given a red palette comes out red. Measured in one program:
  **three answers to one question**, the label red, the same red on a
  model row nothing, and the same row with no delegate installed nothing
  again. That is `working-practice.md`'s wiring gap wearing a design
  question's clothes, and this document deferred it.

  The rule is `fg_for()` and `bg_for()` in `cell_geometry.h` now, shared
  rather than copied, and `CellPaintEngine`'s own pen path calls it too.
  A background role **fills the row** rather than colouring the cells its
  label happens to occupy, which is checked at 24 cells because colouring
  the label alone passes any check taken at the label's position. Three
  checks, and the third is the one that matters: the style's own
  `CE_ItemViewItem` must answer the same as the delegate, so one program
  gives one answer whichever path a view happens to take.

  An unset role costs nothing, which is what makes it safe: the option's
  `Text` brush is then the application palette's own, it matches a role,
  and the theme answers `Color::Default` under the default theme -- so a
  plain row is still written with no colour at all.
- ~~**A table's grid lines eat the spaces inside its own labels.**~~
  **Fixed, and it was not the decision it was written up as either.** A
  `QTableView` with `showGrid` on -- Qt's default -- rendered `a label far
  wider than its column` with a rule in place of every space. Three
  controls placed it: with the grid off the text is clean, with the grid on
  and **no** delegate the rules are identical, so it is neither the
  delegate's nor the text's but the line rule's. The cause is that
  `CellPaintEngine::line()` writes only into a cell whose glyph is `" "`,
  and **cannot tell a space a label wrote from a cell nothing has
  touched**; a row one cell tall has no edge to put a horizontal rule on,
  so the rule lands in the row and fills the gaps in the sentence.

  **A rule that meets any content is not drawn at all now**, and that is
  this tree's own answer for chrome a cell grid cannot represent, applied
  where it had not reached. `CE_HeaderSection` draws no chrome and only its
  label; `PE_PanelToolBar` draws nothing; `PE_IndicatorToolBarHandle` draws
  nothing because its extent is nil; `draw_box()` refuses a rectangle under
  two cells, because a border needs a cell of its own. Four instances, each
  with its reason written beside it. A horizontal grid line between two
  one-cell rows has no cell of its own either.

  **The blast radius was measured over the whole suite rather than
  assumed**, which is what made this decidable: 510 horizontal rules land
  on entirely clear cells and are untouched; 8 land on entirely occupied
  ones and already drew nothing; and every one of the 426 that were partial
  belongs to a table's grid -- as do all 102 vertical rules, which run down
  columns already carrying the horizontal grid they crossed. So the change
  affects one widget's chrome and nothing else in the tree.

  What a table looks like now:

      1  a label far … two words   │
      2  mid           x           │

  -- the labels intact, and a vertical rule surviving where it has a clear
  column to live in. The lone right-hand rule is the rule being uniform
  while the content is not, which is inherent to a cell grid.

  **What is still open is a real question and a different one: a table
  grid that works.** That needs the buffer to know a cell was WRITTEN,
  which is a per-cell flag and a change to the model every tier reads.
  Nobody has asked for it, and the choice today was never "grid or no
  grid" but "a broken grid or none" -- which is not a choice. If a grid is
  ever wanted, the route is the written flag, not re-enabling this.

  Checked as a **difference**: the label's cells must be identical with the
  grid on and off, which is what says the grid changed nothing about the
  text. A check on the text alone passes against a table that failed to
  render its label. The positive control from the mnemonic fix guards the
  other side -- a rule spanning its widget must still draw every cell of
  it -- and dropping every rule reddens exactly that one.

**And the instrument was wrong for the third time, which §0d predicted.**
The suite's `gallery snapshot` line printed `FAIL` while the fixture
matched to the cell, because it read the suite's running failure count
rather than its own result: sabotaging a delegate check twenty lines above
it turned the snapshot red. A check that names one thing and answers about
another is worse than no check, because the reader it misdirects goes to
the fixture. It reads the delta now, which is what `suite_render`'s
snapshot always did.

#### What is drawn during a drag

§0e's second item, run with the same method and the same discipline: a
press, some motion, and **a frame rendered with the button still down**,
each beside a control frame taken before the press and after the release.
Everything a still frame cannot see.

**Five of the eight tracked correctly, and that is a result rather than a
gap.** A slider follows the pointer, a splitter handle moves and the
panes either side of it move with it, a scroll bar's thumb tracks, a
header section resizes live, and an item view's rubber band selects the
rows it passes over. The band's *rectangle* is not drawn and the
selection it produces is, which is the readable answer on a grid: an
outline over cells would fight the content it is over, and the thing the
user needs to see -- which rows are caught -- is already reverse video.

**Three were defects, and all three are fixed.** They are one fault seen
three times: **`GridStyle` carried the option's state into every label it
writes and into no glyph it writes.** Every control drawn with
`put_cluster()` rather than `text()` was therefore stateless, and the
progress bar is the one that proves it was a fault rather than a choice
-- disabled, its percentage was dim, because that goes through `text()`,
and the bar under the percentage was not. One widget showing both
answers, exactly as the disabled item view did.

- **A disabled slider, scroll bar and progress bar were identical to
  working ones.** So were the splitter handle, the combo and spin box
  brackets and markers, the tool button's menu arrow, the menu and
  toolbar separators, and a tree's expander. All carry `with_state()`
  now. This is the disabled-control fault that §7.2 records as fixed "at
  every control rather than one" -- and it was fixed at every control
  that writes its label through `text()`, which is not the same set.
- **A push button held down looked exactly like one at rest**, so
  pressing it gave no feedback at all until whatever it does happens.
  `State_Sunken` and `State_On` join focus in the reverse-video test,
  which is what the tool button one case down already did and this did
  not -- and a *checked* checkable button was equally invisible for the
  same reason.
- **A slider handle being dragged looked the same as one sitting where it
  was left.** Qt sets `State_Sunken` on a slider whose handle has been
  grabbed; the handle is reverse while held now. That spelling is not
  invented here: reverse *is* this style's word for pressed, at the tool
  button and the menu bar item, and the change is to stop two controls
  from being the only ones that say it.

**Two are recorded rather than fixed.**

- ~~**A dragged tab is a picture.**~~ **The option this recorded does not
  exist, and the symptom was somebody else's.** With `setMovable(true)`,
  Qt moves a tab by grabbing it into a `QPixmap` inside a private widget
  and hiding the original. The recorded choice was to suppress that widget
  in Channel A, "at the cost of matching a private Qt class by name" --
  and there is no name to match. Measured: during a drag the visible child
  of the `QTabBar` reports `metaObject()->className()` as **`QWidget`**,
  because `QMovableTabWidget` carries no `Q_OBJECT`. A class-name test
  would match every plain widget in every application.

  What produced the symptom was not the tab at all. The moving pixmap is
  82x19 px, which is **8 cells by 1**, so it fails
  `CellPaintEngine::drawPixmap()`'s "two cells or more in each direction"
  and takes the icon branch -- which marked **one** cell and left the other
  seven showing the tab bar underneath. A picture covering eight cells and
  leaving seven of them stale is not a judgement about tabs; it is the
  screen disagreeing with the widget tree.

  The substitution covers the cells the image occupies now. For the 1x1
  icon the rule was written for that is the same thing, which is why the
  blast radius is small. It was recorded as "exactly two glyph
  substitutions and both are 1x1"; counted again with the branch
  instrumented, the whole suite draws **seven**, and the shape of them
  matters more than the number:

      1 x  cells 8x1  px 80x19   the dragged tab            (render)
      6 x  cells 1x1  px 10x19   pixmaps built at one cell  (4 suites)

  Every one of the six is a test's own pixmap, made at exactly `cw x ch`.
  **No widget icon reaches this branch at all** -- not the tab close, not
  the sort indicator, not the branch expander, not the dock buttons --
  because `GridStyle` answers Qt's standard iconography with a glyph
  before a pixmap is ever drawn. The tab drag renders as eight shaded
  cells moving along the bar -- an honest "a picture is here" -- instead
  of a smear of stale label.

  **What is left open is real, and the measurement moves it rather than
  answering it.** Should a wide, short image be a *placement* rather than a
  glyph? The entry asked how to tell an icon from a picture. Measured, that
  is the wrong question to be asking here:

  - A 16x16 warning triangle is **2 cells by 1**. An 82x19 tab grab is
    **8 by 1**. Neither cell extent nor aspect separates them in the one
    dimension that matters, and both are one cell tall.
  - Composited as mosaics on a text-only terminal, the triangle is `▄▄`
    and the tab is eight `▀`. **Neither is legible as a shape.** What a
    mosaic carries at this size is colour, not content.
  - On a graphics terminal both would be real pixels and both would look
    right, which is how every terminal that can draw pictures shows a
    16x16 icon.

  So the classification the entry asks for cannot be made from the pixmap,
  and the question that CAN be answered is a different one: **can this
  terminal draw pictures at all** -- which `CellPaintEngine` does not know
  and the backend does. The rule is being applied where the information is
  not.

  **"A structural change" was the wrong size, and the code already said
  so.** The backend does not merely know whether the terminal can draw
  pictures; it already acts on it. `AnsiBackend` composes every placement
  as half-blocks whenever `!pixel_placements && !placeholders` -- the
  fallback tier, running today for every terminal without a graphics
  protocol. Nothing would have to be built. The change is one condition
  in `drawPixmap()`, after which the existing path carries a tiny icon
  the same way it carries a large one.

  **And what the two answers actually look like at icon size, printed
  rather than predicted.** A 16x16 status light, red and grey:

      red   block: one cell, rgb(203,9,9)
      red   mosaic: [▀ fg ff0000 bg ba0d0d] [▀ fg ba0d0d bg 000000]
      grey  block: one cell, rgb(106,106,106)
      grey  mosaic: [▀ fg 808080 bg 646464] [▀ fg 646464 bg 000000]

  Both tell red from grey, so the colour fix (below) already bought the
  thing that mattered most. The difference is resolution: the block
  spends one colour on the icon's two cells, the mosaic spends four and
  keeps the edge where the circle stops. On a graphics terminal the
  placement would be real pixels and there is no contest.

  ~~**What the suite cannot price is the cost.**~~ **Priced.** The
  sentence here said "every tiny icon becomes an upload" had no instance
  to measure, which was true of the widget suite and not of the encoders:
  they take an image and return bytes, so the bill can be read directly.
  Eight distinct 16x16 icons, the shape a toolbar would have:

      kitty: first frame  11200 bytes for 8 uploads   (1400 each)
             later frames   280 bytes for 8 re-places (  35 each)
      for scale, one 48x48 upload is 12342 bytes

  **The whole toolbar costs less than one icon the library already
  sends.** A message box's severity icon is 48x48 and is uploaded today;
  eight tiny ones together come to 11.2 KB against its 12.3. And they
  amortise: 35 bytes each per frame afterwards, which independently
  reproduces the "about 30 bytes on the kitty protocol" §7.1 recorded
  from the chat spike, on a different fixture.

  The text tier costs something too, and less. Composed as mosaics those
  eight icons make **17 colour changes** across sixteen cells against the
  blocks' **9** -- roughly double the SGR, on sixteen cells. Neither is a
  shape at that size: the mosaic row is `▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀` and the block
  row `▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒`, so what the change buys there is colour
  resolution, four samples an icon against one.

  So the cost objection that made this "a decision with a real cost" is
  measured and it is small: **1.4 KB once per distinct icon on a graphics
  terminal, 35 bytes a frame after that, and about double the SGR on a
  text one.** The choice stays the holder's; what is gone is the
  unpriced half of it.

  It stays **the copyright holder's**, and it is now a smaller question
  than it was written as: not where to build a mechanism, but whether to
  relax one condition, given that the tier below it already works.

  **One thing that was not a decision is fixed.** The substitution threw
  the image's colour away: a red status light and a grey one both drew a
  default-coloured block, so a row of them said nothing, while the mosaic
  tier -- the other path the same content takes -- carries colour. The
  block takes the image's alpha-weighted average now, which is the
  application's own colour passing through, the rule `fg_for()` applies
  everywhere else. And a wholly transparent pixmap draws nothing at all,
  rather than a block claiming a picture is there.
- ~~**A text selection is the desktop's colour, and an item view's is
  not.**~~ **Fixed, and `qtty/theme.h` had already said so.** Dragging
  across a `QLineEdit` highlighted with `bg=#308cc6` -- measured, and
  equal to `QPalette::Highlight` on this machine -- while a selected item
  view row was reverse video. Two visual languages for one concept,
  confirmed side by side in a single frame.

  The rule was written down before the defect: the default theme keeps
  every role at `Color::Default` and **marks emphasis with attrs, not
  colour**. `GridStyle` obeys it at the item view, the menu item and the
  tab; one line in `fill_rectf()` did not. **A `Highlight` the theme has
  not coloured is reverse video now.**

  The fallback that produced the colour is right for what it was written
  for, and this was not it. It exists for a colour with **no palette role
  behind it** -- Channel B output, something the application coloured
  itself -- and `Highlight` is a role, matched, whose themed answer was
  "the terminal's own scheme". Taking the desktop's literal RGB overrode
  the theme rather than standing in for it.

  **The second half is the engine learning what the delegate already
  knew.** Qt fills a selection and then draws the text over it as two
  unrelated calls, so the fill's attribute was set and the text's write
  replaced it -- the selection came out with no colour and no attribute at
  all. `CellItemDelegate` carries the same rule for the row it draws and
  says why: text written over a reverse-video cell must carry the
  attribute too. `drawTextItem()` writes cluster by cluster now and keeps
  a reverse that is already on the cell, because **reverse is a property
  of the cell rather than of whoever wrote it last**.

  Checked as an agreement rather than as two facts: both selections in one
  frame, and their cells must match. Separately, either side could drift
  and both checks would stay green.


The sweep also found the clipping disagreement now recorded as §8.7,
which is neither of the above: a header resized past its viewport puts a
grid line seven cells outside the widget.

#### Dialogs beyond the standard three

design.md §17.2 scopes "QDialog/QMessageBox/QDialogButtonBox", and those
are what the fixtures and the routing tests exercise. Seven dialogs
outside that line were rendered and read: `QInputDialog`, a `QMessageBox`
carrying informative text, detailed text, a check box and three standard
buttons, a `QDialogButtonBox` with five roles at once, `QProgressDialog`,
`QErrorMessage`, `QWizard`, and `QFontDialog`.

**Almost all of it was already right, and that is the result.** A wizard
draws its page title in bold, its separator rule, and its Back button
**dim on the first page**, which is the disabled state arriving from a
widget nobody had rendered. A message box lays out four buttons with the
default one reversed and its check box below the informative text. The
button box puts five roles in order. A progress dialog draws its bar with
the percentage over it and its Abort button. An error message frames its
text and underlines the mnemonic in "Show this message again". A font
dialog -- the largest thing rendered here -- comes out legible, with the
off-grid symptoms §7.8 already owns and nothing new.

**One defect, and it was in none of the dialogs.** `QErrorMessage` drew a
rule between its check box's indicator and the first letter of its label,
and the cause reaches every check box, radio button and group box that
carries a mnemonic anywhere in the library. Qt underlines the marked
letter with a line one cell long that **starts a pixel early** -- traced
as (39.00,16.50) to (49.00,16.50) against `cw` = 10 -- and
`CellPaintEngine::line()` flooring both ends gave cells 3 **and** 4. Cell
4 held the letter and was skipped by the blank-cell test; cell 3 was the
gap, and got the rule.

The fix is the rule the same file already applies one function away:
`fill_rectf()` refuses a rect thinner than half a cell because it does not
cover the cell it would colour, and `line()` now refuses a cell it
overlaps by less than half for the same reason. **Coverage, not
touching.** The letter keeps its underline attribute either way -- that
arrives through the font, not through the line -- so the mnemonic still
reads as one.

It is checked in both directions, because the one-directional version is
satisfied by an engine that has stopped drawing rules: a rule spanning its
widget must still draw **every** cell of it, asserted against the widget's
own width. Confirmed by sabotage each way. The first version of the probe
was worse than the check: it varied the mnemonic and the check state
together and could not have said which produced the rule.

**And one measurement that belongs to an open question rather than to a
fix.** §0b asks whether an empty `QLineEdit` should show anything. Rendered
beside its neighbours, the question is sharper than "empty":

    [plain edit                  ]     <- what a bracket would give
    [editable combo            ]▾]     <- and what it costs
    [42                        ]±]
    [fixed combo                ▾]

A one-row line edit has no boundary **with or without content** -- it is a
run of text floating where the other three one-row editables are bracketed
-- and `PE_PanelLineEdit` falls through to `draw_box()`, which returns
without drawing anything below two rows. The project has already answered
this question twice, for the combo box and the spin box, in a comment that
states the principle: the control has to be visibly a control, and at one
row a frame cannot say so.

What stops it being a wiring gap is measured rather than assumed: **the
bracket was implemented and rendered.** A bare line edit gets its
boundary; an editable combo and a spin box get a second closing bracket
inside their own, because each contains a `QLineEdit` that reaches the
same primitive. So the fix is not the four lines it looks like -- it needs
the panel to know it is not inside a control that has already bracketed
itself, which is a parent-class test, which is the shape this tree
distrusts. Recorded here so the decision is taken once with the cost
visible. **Owned by the copyright holder.**

Worth one more line, because it says something about the fixtures: the
experimental bracket broke **no existing check**, both snapshots included.
Neither fixture contains a one-row line edit, an editable combo or a spin
box in a position that shows it.

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

  **Swept with the probe method afterwards**, at the tier a terminal with
  no graphics protocol actually gets: eight configurations composited into
  a buffer and the cells read one by one. Most of it was already right,
  and two of those are worth having as checks rather than as beliefs.

  **A placement clipped at the top-left shows the bottom-right of its
  image**, which is the half of cropping that is easy to get wrong in the
  other direction -- moving the picture instead of cropping it looks
  identical under a flat fill. Composited whole and again shifted two
  cells off-screen, the surviving cells equal the corresponding cells of
  the unclipped frame, with a gradient so the two answers differ.
  Sabotaged by sampling from frame coordinates instead of
  placement-relative ones, which is exactly the mistake, and the check
  goes red.

  **The half-covered edge is right, and until now it was not tested at
  all** -- it is part of §7.9's coverage residue, reached by this sweep
  rather than by aiming at the line numbers. A cell whose two vertical
  samples disagree takes the block of the covered half, in the colour of
  that half, and **leaves the background alone** so what is behind the
  uncovered half shows through. All three are asserted, the third because
  the opaque branch one line above does set a background and a
  half-covered cell falling into it would still look plausible.

  The probe needed two attempts to reach that branch, which is worth
  keeping: painting the top HALF of the image covers cell row 0 entirely
  and takes the opaque branch. The edge has to fall **between a cell's two
  samples**, not between two cells.

  Sound, and recorded as results rather than gaps: the alpha thresholds
  behave as written at 39, 40, 200 and 201; a one-pixel image stretched
  over eight cells and a 64x64 image squeezed into one both composite
  without sampling out of range; and two translucent placements over each
  other blend in order, the second reading the background the first left.

  **One defect, and no assertion about the cells could have found it.** A
  null image drew nothing -- correctly -- and printed
  `QImage::pixel: coordinate (-1,-1) out of range` **twice per cell** while
  doing it. The sampling clamps to `width() - 1`, which is -1 when there
  is no width, and Qt answers an out-of-range coordinate with a warning
  and the value 12345, whose alpha is zero, so every cell was skipped and
  the buffer came out exactly as it should. In a TUI that stderr is the
  terminal being drawn: a 40x20 placement puts 1600 lines through the
  screen.

  `compose_halfblocks()` returns on a null image now. The library's own
  routes were already guarded -- the overlay registry skips a null image
  and the pixel-surface harvest skips a zero-size widget -- so this is the
  **public entry point** answering for itself, which it has to, because
  `qtty/graphics.h` declares it and an application may also append a
  `CellImage` of its own to `CellBuffer::images`.

  The check counts Qt's messages around the call, because that is the only
  place the defect exists.
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

**Both measurements are consumed, not merely exposed.** A field nobody reads
is §7.4's own fault, and plumbing the cell size and the background without
using them would have created two more:

- **The background reaches the half-block compositor**, which had
  `qRgb(16, 20, 24)` hardcoded for its whole life. On a light terminal that
  haloes every translucent edge darkly, and the real value was always
  askable. The test asserts that two different backgrounds give two
  different cells rather than pinning a colour, because what matters is that
  the argument is consulted -- pinning one value would pass with the
  parameter ignored if the expectation happened to be the old constant.
- **`Qtty::cells()` is implemented**, one of the two accommodations
  design.md §5.7 offers for image sizing, and it takes the cell size rather
  than assuming one. With no measurement it returns an invalid size rather
  than a plausible wrong one. Its discriminating check is that the same
  image on two terminals with different cells gets different footprints,
  which is the entire reason the cell is asked for.

**tmux is detected, the query is wrapped through it, and the pixel tiers
are refused inside it.** `fuzzypickles` names this problem -- "$TERM lies
both ways (a tmux inside kitty...)" -- and does not solve it, so this part
is qtty's own.

`$TMUX` is set by tmux for its own children and is not inherited across ssh
into somewhere else, which makes it the one environment variable in this
whole area that does not lie; `$TERM` starting with screen or tmux is
accepted too, but only ever to say yes.

The query is wrapped as `DCS tmux ; <payload, every ESC doubled> ST`,
because unwrapped it is answered by **tmux itself** -- tmux is a terminal
too, and it knows nothing about what it is sitting in. That answer would
arrive with the fence attached, and the "a terminal that answered is
believed completely" rule would then believe it. Without
`allow-passthrough on` the wrapper is simply eaten, nothing answers, and
the negotiation concludes no graphics -- which is the right answer for a
tmux that will not pass them.

**The pixel tiers are refused inside tmux however capable the outer
terminal proves to be, and the reason is placement rather than
capability.** Passthrough carries the image to the outer terminal, but the
cursor it lands at is that terminal's, not the one tmux is drawing with, so
the picture arrives in the wrong place. Kitty's Unicode-placeholder mode is
the fix precisely because it makes a placement a run of ordinary text cells
that tmux moves like any other text. Until that exists, half-blocks are the
honest answer, being text already.

That is what qtty did before by accident -- `$TERM` reads as screen inside
tmux, so it fell to half-blocks without knowing why. What changed is that
the query still goes out, wrapped, so the colour depth and the background
are learned from the real terminal instead of guessed.

**The negotiation has now been run against a real, independent terminal.**
`beerssh` grew a `--term-features` switch, so it can be told which
extensions to speak, and `tool/negotiate` reports what qtty makes of
whatever terminal it is run inside. Together they are the second witness
this could not otherwise have: qtty's suite checks the negotiation against
replies qtty's suite wrote, which is one witness however many cases it
covers.

Measured with `beerssh --term-features=<spec> -e qtty-negotiate`, the
answers tracked the switches: all features gave the kitty tier at true
colour, `-kitty-graphics` dropped to half-blocks, `-xtgettcap` left the
tier alone and took the colour depth back to what `$TERM` claims, `none`
gave half-blocks at `$TERM`'s depth, and `none,+kitty-graphics` brought
the kitty tier back on its own. A raw capture -- a C program sharing no
code with qtty, so that the instrument is not the thing under test --
showed the wire form: `ESC _ G i=31 ; OK`, an XTGETTCAP success for RGB,
a failure for Tc, and `ESC [ ? 1 ; 2 c`.

**The first pass was taken while that tree was being rebuilt under it, and
one of its findings was wrong.** Partway through the matrix the answers
changed; `beerssh/build/beerssh` had been rebuilt at the same second as the
check, with five modified files including the two that answer the kitty
query. `running-code.md` names a concurrent build as a *candidate*
explanation for a changing result -- here it was a confirmed one, so the
numbers were recorded as provisional and nothing was reported to that
project.

**Re-taken against a settled build, and the withdrawn finding is the
interesting one.** The first pass had `none,+sixel` yielding half-blocks --
sixel enabled and not advertised in the device attributes. It reproduces no
longer: beerssh's `3525de0`, "answer DA1 and XTVERSION for this terminal,
not for libvterm", lands attribute 4, and qtty now negotiates the whole
tier ladder correctly -- `-kitty-graphics` falls to sixel, `none,+sixel`
gives sixel, `none,+kitty-graphics` brings kitty back on its own.

So the provisional finding was real in substance and unreportable in
timing, and holding it back cost nothing: it was fixed in that tree while
this one was waiting to be sure. The full inventory, and what beerssh does
not answer, is in `doc/beerssh.md` -- which is qtty's own document about
the integration, and the right place for it, since a sibling's tree is not
this project's to edit.

**The same lens found that none of it was reachable.** "A value produced
that nothing consumes" is the shape that gave up the mouse button and the
motion flag; turned on the capability struct it gives up something worse.
`Capabilities` could be obtained **only** through `ITerminalBackend`, and
the convenience `exec(app, win)` constructs its backend internally -- so
from the seat an application actually sits in, every field on that struct
was declared and unreachable. That is §7.4's own fault, and the three
fields the negotiation added had made it three instances worse.

`Qtty::capabilities()` answers for the running session now, beside
`is_tui_active()`. It is taken from the backend when a run starts and
cleared when it ends rather than kept as a pointer, because a stale answer
is worse than none: a caller cannot tell one from a current one.

It is not a convenience. `Qtty::cells()` needs `cell_px` to size an image
without squashing it, and the tiers below kitty need the background to
composite against -- both are things an application must do, and until now
neither could ask.

The test gives the injected backend deliberately odd values, so that what
comes back is shown to be the backend's rather than a default that happens
to look plausible.

**Coverage measured across the library, and the lowest file gave up a
whole untested feature.** The lens is the one the PM and SOS framing
handed over: code that exists and nothing exercises. Per-file line
coverage ranks `compositor.cpp` last at **78% of 132 lines**, and most of
that gap was one block -- design.md §5.7's **three overlay delivery
strategies, none of which had a test.**

`Overlay` itself was covered: the registry, the opacity, the z-order. What
was not covered is the compositor path that CONSUMES that registry, which
is the same shape as every other fault in this section -- a correct class
and an unexercised connection. All three tiers are driven now, against a
recording backend that implements both `ITerminalBackend` and
`IGraphicsOutput`: `KittyAlpha` ships the image to the terminal
unrasterised with its own cell and z, sixel/iTerm2/plain-kitty composite
in software into one full-terminal frame, and half-blocks ship no pixels
at all, being cells already. Removing the software-composite condition
fails exactly the three middle cases.

**And measuring it exposed a fault in the measuring instrument, which is
this section's own theme arriving from the other side.** `make coverage`
reported `term_caps.cpp` at "100.00% of 150", then at "99.44% of 177"
after one `QHash<int, int>` was added -- without a line of its own going
uncovered. `gcov -n` reports the TRANSLATION UNIT, and a translation unit
carries every header inlined into it: the shortfall was Qt's
`QHashPrivate`, instantiated into the object and attributed to the file.
Chasing it would have meant writing tests for Qt's hash.

A number that moves when a header is included is not a number about the
file. The target counts the per-file `.gcov` listing now, which carries
only that file's own lines, and **prints every uncovered line rather than
a percentage** -- a percentage says how much and never which. That change
is what turned the next two gaps from invisible into obvious.

With them closed, `compositor.cpp` goes **78.03% to 98.39%**. The two
lines left are inside the idle-repaint timer's lambda, whose only trigger
is elapsed wall-clock time; a test that waits for a timer is a test that
sleeps, and a sleeping test is flaky on a loaded machine. Left uncovered
deliberately and recorded, rather than covered badly.

**It also found a false positive in `GridGuard`, which is a guard
reporting something nobody can fix.** `Overlay` builds a top-level
`TwinWidget` for the GUI path, guarded by `is_tui_active()` -- so any test
that composites WITHOUT going through `exec()` gets one, at Qt's default
640x480, and the guard reported it. The twin is sized in pixels
deliberately, because in a GUI build the grid does not govern it, and the
application never constructs it and cannot size it. That is precisely the
guard's own test for what it must not report, so the twin is named and
exempted with the reason attached rather than the suite being taught to
look away.

**And it caught a test asserting its own environment.** The pty case
inherited `$TERM`, which on this machine is `screen`; `inside_tmux()` reads
that correctly, so the test began failing for the right reason. It sets
`$TERM` explicitly now. A test that inherits the variable it is reasoning
about passes or fails by where it was run, which is the fault this whole
section has been about.

Still not done from this list:

- **Unicode-placeholder mode** -- the kitty path that survives tmux, and
  the one design.md §5.7 calls stronger still because a placement becomes
  a run of ordinary text cells that the existing diff machinery moves
  with no special cases. Everything it needs is now in place: tmux is
  detected, the query is wrapped through it, and the cell size is known.

  **The encoder and the table are done; the wiring is not.** The table was
  blocked on data rather than work -- 297 specific code points, none of
  them ours to choose, and not on this machine. It came from kitty's own
  `gen/rowcolumn-diacritics.txt`, and the protocol from kitty's
  `docs/graphics-protocol.rst`, rather than from recall.

  **Fetching it caught an error that recall would have made.** The
  placeholder character is **U+10EEEE**; U+10EFFF is what memory offered,
  and it is a plausible neighbour in the same private-use block. A wrong
  one prints a box in every cell of every image. Sabotaging the constant
  back to U+10EFFF fails seven checks, and swapping two diacritics fails
  six -- so the vectors discriminate.

  What makes those checks worth anything is where they come from: they are
  the **worked examples in kitty's own specification**, not a round trip
  through a decoder written here. A decoder built from the same memory as
  the encoder agrees with itself and is wrong together; the specification
  is the one witness this tree cannot supply for itself.

  The generator carries a proof and refuses to write without it: exactly
  297 entries, index 0 is U+0305 with 1 and 2 the two the specification's
  examples pin, no duplicates, every value a code point, and a
  re-extraction from the source text that must agree.

  **The sabotage reported zero failures the first time, and that was the
  build rather than the test.** `kitty_diacritics.h` was not in `src.pro`'s
  `HEADERS`, and qmake writes its dependency list once -- so the edited
  header never reached the compiler and a green suite meant nothing. It is
  registered now, and §9.4's staleness note has one more instance: the
  check that a sabotage landed has to be that the OBJECT rebuilt, not that
  the text changed.

  **Wired.** `use_placeholders()` says yes only where they are both needed
  and safe: inside a host application that would otherwise move a
  placement without knowing it had, on a terminal **proven** to speak
  kitty, at a depth that can carry the id. Outside tmux a real placement
  is cheaper and exact, so placeholders there would be a downgrade.

  The depth condition is not a detail. The id travels in the **foreground
  colour**, so at 256 colours it would be quantised to a palette index and
  the terminal would look up the wrong image or none at all. True colour
  carries all 24 bits, and this is why the XTGETTCAP question in the
  startup query earns its place: `$COLORTERM` guessing wrong here does not
  cost an approximated colour, it costs the picture.

  The transmission is wrapped through tmux; the placeholder **cells are
  deliberately not**, because they are text and tmux is meant to see them
  and move them. That asymmetry is the whole mechanism in one line.

  **So the tmux refusal recorded above is lifted, conditionally.** It was
  standing in for this the whole time.

  Two things the end-to-end check needed, both of which had made it lie
  first. Each backend runs its own startup query, so the terminal's answer
  had to be re-armed before the second one -- otherwise it measured a
  silent terminal and correctly concluded there was no kitty. And a mosaic
  must not be composed when placeholders are carrying the images, or it
  overwrites the very text that displays them.
- ~~The roughly 100 ms scroll-settle debounce for slow links.~~ **Done.**
  Sixel and iTerm2 images have no placement handles, so moving one means
  re-emitting it -- on a slow link that is the whole frame budget spent on
  a picture about to move again. While placements are moving they degrade
  to the half-block mosaic, which is cells and diffs like any other text,
  and the real pixels come back once scrolling settles.

  **Kitty is excluded deliberately**, because a placement there has a
  handle and moving it is one short escape with no re-upload -- degrading
  would trade a cheap correct picture for a coarse one and buy nothing.
  design.md scopes the policy to the two tiers that pay for movement.

  Only a placement that MOVED counts. One appearing or vanishing is a
  picture arriving or leaving, and counting it would degrade the first
  frame of every image -- the one moment the pixels are most wanted.

  The clock is a parameter rather than a timer read inside, for the reason
  the capability parser takes bytes rather than a descriptor: a
  hundred-millisecond debounce tested against the real clock is a test that
  sleeps, and a test that sleeps is flaky on a loaded machine. Eleven
  checks drive the policy directly, including that the wait restarts from
  the LAST move rather than the first.

  **Two things had to be checked separately, and one of them was a claim
  with nothing behind it.** The policy being correct and the backend
  consulting it are different facts, so the pty case drives `present()` at
  the sixel tier and looks for the sixel introducer in what actually
  reaches the terminal. And "kitty is excluded" was a comment until a
  sabotage that made kitty degrade too failed to turn anything red; there
  is a check for it now, and that sabotage fails it at once.
- ~~`Qtty::PixelSurface`.~~ **Done**, and it is the mirror of
  `ICellPainted`: that interface is for a widget which knows how to draw
  itself in CELLS, this is for one whose content is genuinely pixels -- a
  plot, a meter, a video still -- which Channel B would mangle by snapping
  every primitive in it to the grid. An application paints into it with
  QPainter exactly as on the desktop; qtty harvests the result and hands it
  to the graphics plane with the widget's cell geometry.

  A base class rather than an interface, because there is nothing for the
  application to implement: painting is the whole contract and it already
  knows how. Detected by `dynamic_cast` for the reason `ICellPainted` is --
  a `qobject_cast` would need `Q_INTERFACES` and therefore moc in the
  application.

  **The key is content-addressed, and finding out why took a failed
  sabotage.** A key taken from the widget would tell the kitty tier the
  image had not changed and it would show the first frame for ever; a fresh
  key every repaint would re-upload an unchanged plot every frame. Hashing
  the pixels is what makes upload-once mean what it says for a surface that
  is repainted rather than cached.

  The test originally asserted only that an unchanged surface keeps its
  key -- and a widget-pointer key **passes that**, which the sabotage run
  proved by not going red. What discriminates is the other half: changing
  the content must change the key. Both halves are asserted now, and the
  same sabotage fails the second immediately. One more instance of a check
  that has to be built from the case where the two answers differ, not from
  the case that confirms the right one.
- ~~`qtty::cells()` and `alignTextDocument()`, the two GUI-invisible
  accommodations design.md §5.7 offers for cell-multiple image sizing.~~
  **Both done.** `align_text_document()` rounds every image in a
  `QTextDocument` up to whole cells, because an image in the TEXT FLOW that
  is not a cell multiple pushes every line after it off the cell rows --
  which compounds down the document rather than showing up as one wrong
  picture.

  Three cases, and the middle one is the reason it is not a two-line
  function. An image with an explicit size is rounded. An image with **no**
  size in its format has its natural size resolved from the document's own
  resources and written back explicitly, because the layout would otherwise
  use that natural size and undo the rounding -- skipping it would look
  like it worked. An image whose size cannot be determined at all is left
  exactly as it was, since rounding an unknown is inventing a number.

  It is idempotent and reports how many images it changed, which is what
  makes it safe to run over every document once wherever they are built,
  and lets a caller tell "nothing to do" from "nothing was done".

  Spelled `align_text_document`, not design.md's `alignTextDocument`: the
  member rename in §11 moved this project's own identifiers to snake_case
  and the document still carries the pre-rename name.
- ~~The `qtty.glyph` / `QIcon::name()` icon substitution registry
  (design.md §8.6).~~ **Done.** A terminal cannot draw a 16-pixel icon in
  one cell -- there is nothing to see at that size, which is why
  `drawPixmap()` has always stamped a placeholder block there. The registry
  is how an application says what an icon MEANS, chosen by whoever knows the
  icon set rather than guessed by the library, and `CC_ToolButton` is its
  first consumer.

  **The route that works is the ACTION's property, not the icon's name**,
  and that was measured rather than designed. `QIcon::name()` is empty
  unless an icon THEME resolved the icon, and on this machine nothing
  resolves -- `QIcon::fromTheme("edit-cut")` comes back null with an empty
  name under no theme, under `hicolor` and under `Adwaita` with the search
  path set. qtty pins the platform theme off, so that is the normal case
  rather than this machine being odd. The property route also fits how
  applications are written: a toolbar's `QToolButton` is built by Qt from a
  `QAction`, so requiring the property on the button would require it on a
  widget the application never sees.

  `glyph_for()` is split in two for that reason. There is no way to give a
  `QIcon` a name by hand, so the `QIcon` overload cannot be driven past its
  first line here; the name overload carries every rule and is exercised in
  full. Saying which is which is better than a coverage figure that hides
  it.

  The measurement and the drawing share one label builder, because a label
  measured without its glyph is drawn into a box a cell too narrow and the
  elide then eats the last letter rather than the thing that did not fit.
  The test asserts the button's WIDTH as well as the row, which is what
  catches that.

  Not done from §8.6: the debug-versus-release placeholder rule -- a single
  cell in debug and nothing in release. It needs a way to exercise both
  builds, and the suite runs in one.

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
  measures width itself.

  **`mouse` and `bracketedPaste` are no longer taken from the tty**, which
  was a fact about the local terminal device that said nothing about what
  the terminal understands. They are asked with DECRQM now (§7.4), and raw
  mode is demoted to the assumption used when the terminal says nothing.

  **`synchronisedOutput` is implemented and true when earned.** DEC 2026
  is asked in the startup query and `present()` brackets its frames when
  the terminal confirmed the mode, so the field still means "qtty uses
  synchronised output" rather than "the terminal has it" -- what changed
  is that qtty now does.

  Confirmed only, with the assumption **false**, which is the opposite
  default to mouse and paste and deliberately so. Those are about input
  that would otherwise be mishandled, so silence leaves the working
  assumption alone; this is an optimisation worth nothing on a terminal
  that lacks it, and DECRQM is the discovery mechanism the
  synchronised-output specification itself names. A terminal that says
  nothing has declined to be asked and gets unbracketed frames.

  One function decides, because `present()` and `capabilities()` must not
  be able to disagree: a field claiming synchronised output over frames
  that go out bare is exactly the defect shape this negotiation exists to
  stop, and it would be invisible from inside the process. The tests
  assert the claim and both brackets together, and bracketing
  unconditionally fails the terminal-answered-0 case.

  Verified against beerssh, which can be told to lack it: all features
  gives `2026;2` and the frames are bracketed, `-synchronised-output`
  gives `2026;0` and they are not.

  `title` is still false -- there is no OSC emitter.
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

  **Swept with the probe method afterwards**, in the configurations those
  four checks do not reach, and the interface came out sound. A widget
  inside a scrolled viewport is handed a rect in **window** cells that
  moves with the scroll; one hanging three cells off the left edge is
  handed a negative rect and what falls outside is dropped rather than
  wrapping onto the row above; a child widget paints over the cell
  painting, which is what a child should do; and a widget four pixels
  across is handed one whole cell, because the alternative to rounding it
  up is handing it nothing to draw in. Three of those are checks now.

  **Two things the sweep found are contract rather than code, and both are
  written into `qtty/paint.h` where an implementer will meet them.** The
  buffer handed to `paint_cells()` is the **whole frame** and `cells` is
  where the widget is rather than a boundary anything enforces -- measured
  by writing outside it, which lands on the neighbours with nothing
  noticing. And **a class inheriting both `ICellPainted` and
  `PixelSurface` gets the pixel path**: the filter tests for a surface
  first, so `paint_cells()` is never called and the widget is harvested as
  an image with no warning. That is now documented, and pinned by a check
  so it cannot change silently in either direction. Which of the two
  *should* win is not a question this raises -- the interfaces answer
  opposite questions, and a class claiming both has answered neither.

  The sweep's other two findings are elsewhere, because neither is the
  interface's: a scroll area not clipping is §8.7, and a build that did
  not rebuild is §9.5.
- **design.md §7's Tier-2 hint system** -- what remains is the CI check
  banning `setContentsMargins`, `setSpacing`, `setFixedSize` and
  `setFixedWidth` in shared UI code. The hints themselves are done:
  `GridSnap` does what `CompactionPass` was for, `set_priority()` carries
  `Priority::Optional`, `"qtty.cells"` sets a widget's minimum in cells,
  and `setCompact` is **deliberately not built** -- §8.8 carries the
  argument, that its effect is unconditional on a terminal, so the hint
  would switch nothing. The rest is built: `GridSnap` does
  what `CompactionPass` was for, `set_priority()` carries
  `Priority::Optional`, and §7.8 records the small-terminal policy
  working in the order design.md names -- drop the optional widgets,
  then scroll the root.
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
having built it. **Fixed, by the third of the three answers recorded here,
and the other two were measured out rather than argued out.**

- **Match roles with a tolerance** cannot work for the case that motivated
  it. `#fbfbfb` is *equidistant* from `Base` (`#ffffff`) and
  `AlternateBase` (`#f7f7f7`) -- four of 255 either way -- so no tolerance
  chooses between them except by tie-break, and a tie-break here is a coin
  toss about what a pane is.
- **Ask the style for the role** has no route. Qt exposes no way to ask
  which role produced a brush, and the paint engine sees only the painter.
- **A gradient brush cannot be detected either**, which was the first
  thing tried. Measured: the fill arrives with `Qt::SolidPattern`, not a
  gradient pattern -- Fusion has already flattened its gradient to a stop
  colour by the time the engine sees it. Three fills in the suite do carry
  a real gradient brush, and their colour is black, which is its own
  question and not this one.

So: **suppress the fill for a region Channel A has already drawn as a
box**, which needed no new mechanism at all. `PE_FrameTabWidget` was
simply not in `GridStyle`'s list of frames -- it drew the tabs itself and
let the base style draw the pane -- and adding the case label means Fusion
never runs and there is no fill to classify.

The fixture change is the measurement: **ten rows of `bg=#fbfbfb` across
the whole tab pane become no colour at all**, the legend loses its
near-white entry, and the glyph and attribute planes are untouched. The
check is on the cells rather than on the fixture, because a fixture says
what it looks like and a check says why.

Exact-colour matching remains the shared cause and is still fragile for
any style that paints a gradient. What has changed is that the one place
it bit is no longer reached, and the next place will be a frame this style
does not draw either -- which is a smaller and more findable thing than a
matching rule.

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

**A window larger than the terminal scrolls to its focus.** This is the
oldest unanswered finding in the document: Phase 0.5 measured that a
resize below `minimumSizeHint()` is refused and content overflows, and
design.md §7 named the policy -- drop the `Priority::Optional` widgets,
then scroll the root -- and neither half was built. Printed rather than
described, an eight-field dialog whose minimum is nine cells, in a
six-row terminal:

    | Field 0[value 0            ]|      | Field 3[value 3            ]|
    | Field 1[value 1            ]|      | Field 4[value 4            ]|
    | Field 2[value 2            ]|      | Field 5[value 5            ]|
    | Field 3[value 3            ]|  ->  | Field 6[value 6            ]|
    | Field 4[value 4            ]|      | Field 7[value 7            ]|
    | Field 5[value 5            ]|      | <Close>|

The left is what it showed: six fields, and neither the last two nor the
button that closes the dialog. Nothing scrolled and nothing said so, and
Tab would happily move focus to a widget nobody could see. The right is
the same dialog after Tab reaches the button.

**Following the focus is what makes it free.** The alternative was a key
binding, and there is no key to spare: the arrows belong to the focused
widget and a chord has to be learned. Tab already walks the form, so
keeping the focused widget inside the terminal makes every widget
reachable with the keys the application already answers -- no new
gesture, no annotation, nothing for the application to call.

The offset is clamped to what actually overflows, so **a window that fits
scrolls by nothing** and this is invisible to every dialog that was
working before. That is a check of its own rather than an assurance:
without it the first check is satisfied by a compositor that scrolls
whenever it likes, and every ordinary dialog would wander under the Tab
key.

**And the other half is built too, in the order design.md names it.**
`set_priority(w, Priority::Optional)` marks what a screen can afford to
lose, and `Compositor::apply_priority()` hides those widgets when the
window's layout minimum does not fit the terminal. Dropping comes first
and scrolling second, because dropping can make a screen *fit* and
scrolling never does -- it only makes the rest reachable.

Four properties, each asserted because each fails on its own: a pass that
drops nothing, one that drops everything, one that never puts anything
back, and one that hides the widget holding focus are four different
bugs, and only the first is caught by "the screen fits".

- **It is re-evaluated every frame rather than latched.** Put back first,
  then measure, then drop again: a terminal that grows brings the content
  back with no separate path to get wrong, and the hysteresis is the
  whole of it.
- **It hides only what it hid.** `dropped_` records this pass's own
  work, so a widget the application hid for its own reasons stays hidden.
- **It never drops the focused widget, nor an ancestor of it**, whatever
  its priority. Hiding the widget that owns input moves focus somewhere
  the application did not choose, and a terminal has no pointer to put it
  back with. **The first check written for this was vacuous and the
  sabotage said so** -- it marked a focused line edit optional among six
  other optional labels, and since the pass stops as soon as the screen
  fits, the edit survived because it was never reached. Removing the
  focus rule changed nothing. Its own fixture now: the focused widget is
  the only optional one and the terminal is far too small, so nothing but
  the rule can save it.

  The two sabotages also had to be run **separately**, which is the other
  half of the lesson. Applied together, "never drop anything" masks "drop
  the focused one too" completely -- one failure appeared where two were
  expected, and the missing one was read as the check being weak before
  it was read as the sabotages colliding.
- **It stops as soon as the screen fits**, rather than dropping
  everything optional.

The hint is the dynamic property `"qtty.priority"`, which is what makes
it a no-op in a GUI build the way design.md asks: nothing reads it there,
and shared code neither links qtty nor branches on target to set it.

**One thing this fixture showed that is NOT this feature's**, and the
first reading of it was wrong. It looked like a `QGroupBox`'s children
landing off the grid -- `194x17+13+25` for a check box inside one, with
the indicators drawing *into* the frame:

    ┌[ ]─Option 0────────┐
    │[ ] Option 1        │

Measured properly, **the children are on the grid**: `200x19+10+38`,
aligned, after `GridSnap` corrected them. `GridGuard` warns about the
geometry a layout ASKS for, and the snap then fixes it -- which is the
two working as designed, not a defect. The `194x17` in the warning was
never on screen.

**The real fault was one layer up, and it is the one §7.8 said to watch
for.** Fusion answers a 25-pixel contents top for a titled group box, and
25 is 1.3 cells; `GridSnap` rounds to NEAREST, so a squeezed box rounded
its first child **down onto the frame's own top row**. At six rows it
rendered correctly and at five it collapsed, which is why it took a small
terminal to see at all.

That is exactly the risk this document named before any snapping was
written -- "whether closing a layout's gap can overlap two widgets; one
safe case is not a proof" -- arriving in the wild, and it says something
about the shape of the answer. **The fix is not to change the rounding
but to hand it rectangles it cannot round wrong.** `subControlRect()`
answers `CC_GroupBox` in cells now: a titled box spends one row on the
title and one on the frame's top border, and its contents start on the
row after. Rounding to nearest is still the policy; what changed is that
nothing is offered to it at 1.3 cells.

The check is on the collision rather than the frame -- the row carrying
the frame's top corner must carry nothing else -- because "the box
renders" passes against the broken version, which is how this survived
until a five-row terminal.

**The decision is taken: `GridSnap` is installed by `Qtty::setup()`.**
design.md §7's Tier 1 promise -- "style metrics differ, so the same layout
compacts automatically" -- is true now rather than nearly true.

The two answers that were open:

- **Snap child geometry after layout activation**, which is what §7's
  `CompactionPass` is for, making Tier 1 genuinely free.
- **Leave it to the application and say so**, narrowing §7 Tier 1 and
  documenting what an application must do instead.

**What settled it was measuring the effect on a real tree rather than
arguing the principle.** With `GridSnap` installed for the whole suite:
662 checks pass, **both snapshot fixtures included**, and the only checks
that change are the two written to assert the UNSNAPPED state. Nothing
else moved. The feared cost was not small, it was absent.

The precedent decided the rest, and it is one file away. `setup()`
already forces the font family and size on every widget through an event
filter, and the reason written there is this one: **the pin is policy and
can be overridden, the filter is the guarantee.** An invariant the grid
depends on is the library's to hold, not a paragraph for every
application to obey. `setup()` is also the call that makes a program a
terminal program -- it installs `GridStyle` and restyles everything -- so
a GUI build that never calls it is untouched, which is §10.1's inertness
rule working rather than being bypassed.

**Two limits, both measured and both the application's to fix.** A widget
with a **fixed size** off the grid resists snapping, because Qt clamps
`setGeometry` to size constraints. And a `QSplitter` assigns its panes'
geometry itself and re-asserts it, so the snap is fought rather than
missed: measured with the filter installed, its panes are still 205, 215
and 225 pixels wide against a 10-pixel cell. `setSizes()` in cell
multiples is the caller's, and the suite says so rather than leaving it
to be found.


**What off-grid geometry actually costs has now been measured, and it is
worse than untidiness.** The lens came from two bugs of one shape -- a
caret erasing the glyph under it, and a toolbar's border drawn in the row
below itself -- both a coordinate mapped to the nearest cell boundary
instead of to the cell containing it. `line()` was wrong that way and is
fixed; `to_cells()` and `drawTextItem()` still round, so the question was
whether they are wrong too.

They are not. Instrumented across the whole suite, rounding and flooring
disagree 35 times, and **every one of the 35 has an off-grid coordinate.
With cell-aligned input they never disagree at all** -- zero cases. So
the rounding rule is sound and there is no third instance of that bug.

What the measurement says instead belongs here: when geometry is off the
grid, content is not placed slightly wrong, it is placed **a whole cell
away** -- 35 times in this suite. That is the cost of the slack this
section is about, and it is a fact for whoever decides the question rather
than an argument for either answer. **What the decision was waiting on has now been
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

**`GridSnap` is `GridGuard`'s other half** -- same event, same exemptions,
same install-once shape. `setup()` installs it; the guard stays a debug
build's, so a release program is corrected without being reported at.

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

**A correction to what the odd-size sweep recorded here, because it named
the wrong cause and the right one is a different question.** That sweep
found a window laid out by a plain `QVBoxLayout` losing its top row, and
wrote it up as Qt's default nine-pixel margins rounding badly. Measured
since: the margin is **19 pixels**, exactly one cell, because
`GridStyle::pixelMetric()` answers `ch` for `PM_LayoutTopMargin` and
`PM_LayoutBottomMargin`. Nothing rounds and nothing is lost to slack --
the first widget starts on row 1 because **qtty gives every layout a
one-row margin**, and `GridSnap` neither causes that nor fixes it, which
was checked both ways.

**Taken: the vertical margins are zero and the horizontal ones stay a
column.** It looked like a matter of taste and it is not -- the tree had
already stated the rule two lines below the one at fault.
`PM_LayoutVerticalSpacing` is **0** and `PM_LayoutHorizontalSpacing` is
`cw`: vertical space is precious and horizontal space is not. The margins
said the opposite, spending a whole row above the first widget and another
below the last, on a screen with twenty-four of them.

The cost was more than the eight per cent. At 80x1 a window with a plain
`QVBoxLayout` rendered **entirely blank**, its first widget one row below
the only row there was; it reads `File` now. Left and right keep their
column, because a column of eighty is cheap where a row of twenty-four is
not, and the indent is what stops text touching the screen edge.

Both fixtures moved and the change is legible in the diff: the leading
blank row goes, and the row it frees is redistributed by the layout --
`prefs_dialog`'s button row descends by one, the gallery's tab pane grows
by one. No content appears or disappears. The checks are a pair, because
"no margins" is a different rule from this one: the first widget must
reach row 0 **and** the left margin must still be a column.

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

**And then the fourth lever, which was not an environment variable and so
was not pinned with the other three: hinting.** Found the way the theme
findings were found -- by running the suite somewhere it had never run,
in this case from a second account on the same machine, same font file,
same Qt, same everything the first three pins cover. It did not fail a
check. `setup()` aborted before the first one:

    qtty: the grid needs a font with integral metrics:
          line height is 18.6406 px, which is not a whole number

That is the number §7.9 already records for `QT_SCALE_FACTOR=2`, arriving
with no scale factor set. Measured with the preference varied and nothing
else:

| Hinting preference | advance of `M` | line height |
|---|---|---|
| `PreferFullHinting` | 10.0000 | 19.0000 |
| default / `PreferNoHinting` / `PreferVerticalHinting` | 9.6250 | 18.6406 |

Default means fontconfig's, and **fontconfig's packaged answer is
hintslight** -- `/etc/fonts/conf.d/10-hinting-slight.conf`, present on a
stock Debian. So the second row is what a user gets, and against those
metrics `grid_font_problem()` refuses and `qFatal()` aborts. **Every qtty
program failed to start for anyone who had not turned full hinting on in
their own fontconfig**, which is a per-user desktop setting nobody would
connect to a terminal program refusing to run.

The sharper part is what it says about this tree's own numbers. **The
10x19 cell was the first account's fontconfig, not a property of the
font.** design.md §16 measured it, both snapshot fixtures were recorded
against it, and every column here is derived from it -- and none of that
was reproducible on the machine it was measured on, from a different
login.

`setup()` now asks for `PreferFullHinting` on the font it builds. That is
the same argument as the theme pin -- a terminal program must not inherit
the desktop's configuration -- with a sharper edge, because this lever
does not change how qtty looks, it decides whether qtty runs. And it
moves nothing already recorded: full hinting **is** 10x19, so the pin
makes the fixtures independent of whose account renders them rather than
re-taking them. Measured under all three hint styles, driven by
`FONTCONFIG_FILE`, the suite is green in each: hintfull, hintslight and
hintnone.

Guarded two ways, and the division is the one §7.9 already draws between
a check that discriminates and one that only looks like it does:

- **The request is read back off the installed font**, which fails on any
  account the moment the line is deleted.
- **The enforcer must carry it**, checked in `suite_grid` beside the
  class-font simulation, whose intruder now asks for no hinting the way a
  theme's freshly built font does. `FontEnforcer` copies its base
  wholesale today; a future edit that copies field by field, as it
  already does for weight, italic and underline, is what this notices.

Both were confirmed by sabotage, and the second one was **written in the
wrong place first**: a plain `QWidget` probe inherits the application
font untouched, the enforcer returns early on it, and the check passed
with the enforcer sabotaged. It reported on inheritance and was moved to
the only place a widget's font is actually rebuilt.

The honesty note that goes with it, the same one the style-hint pin
carries: **on an account whose fontconfig already selects hintfull, an
assertion on the metrics cannot fail.** Sabotaged and run under such a
configuration, `the grid font passes the integral-metrics check` passes
while the two hinting checks fail -- which is the measurement that says
the numbers are the wrong thing to assert here and the request is the
right one.

**One check was wrong rather than merely weak, and this is what exposed
it.** The font-provisioning test built its own `DejaVu Sans Mono` at
pixel size 16 and asked `grid_font_problem()` about that. A second font
built to the same recipe is not the grid font; it is a copy that drops
whatever the recipe grows and nobody remembers to repeat -- which is
exactly what happened the moment the recipe grew a hinting request. It
reads `QApplication::font()` now, which is the grid font by construction.

**What this does not close is D7.** Hinting was one of the two levers
that decide the cell, and it is now pinned; the font *version* and the
rasterizer are the other, and only a bundled font settles those. The
exposure is smaller than §7.9 opened with -- a different login no longer
changes the answer -- and a different machine still can.

**So how large is what is left? Measured, not feared.** The bundled font
is a licensed third-party asset and choosing one is the copyright
holder's, not a thing to settle while doing something else -- but *how
much rides on it* needs no licensing decision, and it had never been
counted. A probe walked `QFontDatabase::families()` at pixel size 16 with
full hinting and asked each fixed-pitch family for its cell:

    102 fixed-pitch families, 102 with integral metrics
      14x16: 1     9x14: 7     8x16: 35    10x19: 7    16x16: 24
      11x12: 2     5x12: 2    32x16: 2     18x16: 1     8x14: 5
      10x20: 1
    this build uses 10x19

Two things fall out, and they point opposite ways. **Integrality is no
longer the exposure**: every one of the 102 gives whole-pixel metrics
once hinting is pinned, which is the hinting fix (above) being worth
more than it looked. R3's risk, "font metrics are not integral on some
backend", survived 102 chances to fire on this machine -- which is
evidence about this rasterizer and not about another one, and it is the
most that can be said without a bundled font. **The cell size is**: eleven
distinct values across the 102, and only **seven** families give the
10x19 the fixtures were recorded at. The commonest cell on this machine
is 8x16, at 35 families, and it is not ours.

**What that buys is a guard, which is the part that needs no licence.**
A machine with a different font does not get two inscrutable fixture
diffs any more. It gets one check that says what is wrong --

    FAIL: the cell is 10x19, which is what the snapshot fixtures assume

-- and, if the fixtures are compared anyway, a mismatch line that names
the cell it actually measured, `snapshot 'widgets_gallery' mismatch (cell
10x20)`. Both were verified by moving the pixel size to 17 and reading
the two failures, which is also why the numbers above are the second
thing this session measured rather than the first: the probe was written
to answer "how fragile", and the answer changed which defect was worth
building against.

The check cannot substitute for the font. It is deliberately the
opposite: it converts an unowned assumption into a named one, so that
whoever does choose a font is choosing against a stated requirement
instead of chasing a diff.

**And then the exposure turned out to be narrower still, in the one way
that matters for the entry.** The reason given for bundling a font is
that it "would make the fixtures reproducible". Measured, **the font is
not what the fixtures depend on -- the cell size is, and nothing else.**

Eighteen fixed-pitch families were sampled, one per distinct natural
cell, from a 7x16 to a 32x16. Each was made the application font, and a
deliberately width-sensitive dialog -- an eliding label, a combo box
sized to its longest item, a tab bar, a table with a wide column header
-- was rendered and compared frame for frame:

    GridMetrics pinned to 10x19   17 of 17 comparisons identical
    GridMetrics left to the font  17 of 17 comparisons differ

(Eighteen families, seventeen comparisons: the first is the reference.
The control is the half that makes the result mean anything -- without
it, "a font that never took effect" would look exactly like "the pin
works".)

The reason is structural rather than lucky. `sizeFromContents()` returns
cell multiples and `elide_to_cells()` counts cells, so **no layout
decision in this tree ever consults a glyph advance except through the
cell**. A font with a 32-pixel advance lays a dialog out identically to
one with 7, provided the cell is the same.

So the entry narrows sharply. A bundled font would still settle R3's
integrality and would fix what the *running application* looks like --
but for **fixture reproducibility, which is the reason the entry gives,
pinning `GridMetrics` in the test binary would do it, with no licensed
asset involved at all.**

That is left as a costed option rather than done, because it is a change
to what the two fixtures MEAN and they are this tree's most-cited
artefacts. The cost is real and is the argument against: a pinned cell
makes the fixtures stop being an end-to-end check of the metric
pipeline, so a machine whose font gives fractional metrics would render
fixtures that pass. The tree has two other checks for exactly that --
`grid_font_problem()` and the cell-size guard above -- which is what
makes the option arguable rather than obviously wrong. **The holder's,
and now a much smaller question than "which font".**

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

**The cost half of that question is measured now, and it is smaller than
it reads.** Qt 5.15.15 is installed on this machine beside 6.8.2, so the
Makefile's own `QMAKE=` override is all it takes:

    make BUILD_DIR=build-qt5 QMAKE=/usr/bin/qmake
    make -k -C build-qt5/src 2>&1 | grep error:

**The public headers are clean.** A translation unit that includes
`qtty/qtty.h` and nothing else compiles under Qt 5.15 with no errors, so
an application on Qt 5 could include qtty today. What does not compile is
three usages in two implementation files:

| Usage | Where | Qt 5 equivalent |
|---|---|---|
| `QPalette::Accent` | `src/core/theme.cpp` | absent before Qt 6.6 |
| `QAction::associatedObjects()` | `src/runtime/input_router.cpp`, twice | `associatedWidgets()` |
| `QKeyCombination` | `src/runtime/input_router.cpp` | the older combined `int` |

That is the whole of it for the library. **None of it is fixed here**,
because writing those three conditionals *is* the decision this section
records as open -- §8.1 says the code has "no version conditionals
anywhere", and adding the first three would settle by hand what belongs
to the copyright holder.

**One of the five errors was a defect rather than a version difference,
and it is fixed.** `src/grid/grid_style.cpp` uses `QAction` through
`QToolButton::defaultAction()` and had never included `<QAction>`; it
compiled because something else drags the header in. That is luck rather
than a fact about the file, and Qt 6 **moved `QAction` from QtWidgets to
QtGui**, which is exactly the reorganisation that changes what a header
brings with it. The include is right on every version and is not a
position on which versions are supported.

**What this does not measure is the thing §0e wanted a second version
for.** A version axis finds assumptions at *run* time -- the beerssh
session's probe that passed on 6.10 and failed on 6.4.2 -- and reaching
that here means compiling the suite, which means the three conditionals,
which is the decision. So the exposure §0e names is unchanged: it is now
priced rather than closed. A second Qt **6** would answer it without any
decision at all, and this machine has only one.

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

### 8.7 Channel A clips, and the last thing this section said was wrong

design.md §432 lists the paint engine's entry point as
`updateState(const QPaintEngineState &) override; // pen/brush/font/clip →
Attrs`. Three of those four were implemented. **The fourth is implemented
now and the section is closed**, but it took two goes and the wrong turn
is the part worth keeping.

**An application's own `setClipRect()` was ignored outright**: a
`QPainter` told to keep inside four cells filled twenty. Fills, text,
rules, boxes and placements consult the clip now.

**Clipping rounds outward, and that is a rule rather than a detail.** A
cell is atomic: a clip covering part of one either admits that cell or
loses content that was inside it. `to_cells()` rounds each edge to the
nearest cell, which is right for placing a rectangle and wrong for
admitting one -- used here it made a clip eight pixels tall against a
nineteen-pixel cell round to nothing, and a `QLineEdit`'s text
disappeared. Three checks went red on that and none of them said "clip".

**Then the wrong turn.** With the user clip honoured, a `QScrollArea`
still painted its scrolled-out content over its neighbours, and every
draw in that case reported `hasClipping() == false`. The conclusion
written here was "Qt sets no clip when `QWidget::render()` walks
children", and the remaining work was recorded as the compositor's.

**Qt sets a clip. It is the SYSTEM clip, and this engine had never asked
about it.** `QPaintEngine::systemClip()` is the channel Qt uses for widget
rendering; `QPainter::hasClipping()` answers about the *user* clip and was
honestly false the whole time. Measured across the suite: the system clip
is set on essentially every widget draw, sometimes as four rectangles.
Honouring it -- in device coordinates, so it does not go through the
transform -- fixes both cases outright, with no compositor change at all:
a scroll area's content stops at its viewport, and a child wider than its
parent is clipped to it.

So **a channel that answers is not evidence it is the right channel**,
which `evidence.md` says in as many words and this section had to learn
twice. The trace was real, the number was right, and the question was
wrong.

### 8.8 The Tier-2 hint names, and the two hints that are not hints

design.md §7 writes three Tier-2 calls. The code answers all three, and
disagrees with the document about two of them in the same way.

**`qtty::setPriority()` is `set_priority()`.** A name qtty INTRODUCES is
`snake_case` (§10); only a Qt name or a reimplemented Qt virtual keeps
Qt's spelling. This is the same call the `focusWidget`/`set_focus_widget`
rename made, and it is recorded rather than resolved silently because
design.md is the design document and the spelling rule is the tree's.

**`qtty::setCompact(toolbar, Compact::IconsToLetters)` is not built, and
the reason is that its effect is unconditional here.** A hint exists to
let shared code ask for behaviour the GUI must not get. Measured, a
terminal has no choice to offer:

- `SH_ToolButtonStyle` is pinned to `Qt::ToolButtonTextOnly` for the
  whole style, because a terminal draws no icon. Every toolbar is already
  letters, and an application that sets its own tool button style still
  wins -- which is the hint mechanism Qt already provides.
- What was actually missing was the case the document's name points at:
  an action with an icon and **no text** had nothing to fall back to and
  drew nothing at all. Four actions, measured:

      before   [Cut]    [Find]
      after    [Cut][Copy][][Find]

  The tool tip is where such an action already keeps its words -- what Qt
  shows on hover and what a screen reader announces -- so that is what
  the label falls back to. A word beats a letter, and it costs the
  application nothing new.

- **And this found a regression I had put there two commits earlier.**
  The bracket-dropping rule written for the dock widget's title buttons
  -- drop the brackets below three cells, so the content keeps the cells
  -- turned a wordless two-cell action from `[]` into two blank cells: an
  invisible button. Brackets are dropped to buy room for content, so with
  **no** content there is nothing to buy, and they stay. Its own check,
  and the sabotage was run on its own after the priority pass showed that
  two sabotages applied together can mask each other.

So `setCompact` would be an enum with one value that switches nothing.
**Building it would be writing API for a document rather than for a
need**, which is the opposite of what §7's hints are for. If a second
compaction mode ever appears that a GUI genuinely must not get, the hint
is worth adding then and this paragraph is the argument for it.

`w->setProperty("qtty.cells", QSize(20, 1))` is the third, and it is
built now -- **but not where design.md says it is read**, and the reason
is a fact about Qt rather than a preference.

§5.1 says the style reads it: "the style receives the `QWidget*`, so
attached state is read there". It does -- for the widgets Qt asks it
about. `QStyle::ContentsType` has **twenty-four values** and not one of
them is a label, a text edit, a view, or an application's own `QWidget`
subclass. There is no `CT_Label`. So a style-side reader would silently
do nothing for most of a tree, including for the document's own example
line, which is worse than not having the property at all -- an
application would set it, see nothing, and have no way to tell whether
it had spelled it wrong.

It is read in `GridSnap`'s event filter instead, which sees every
widget: on `Polish` for a property set at construction and on
`DynamicPropertyChange` for one set later, so there is no moment where
an application has asked and been ignored.

**Applied as a minimum rather than a fixed size.** That is the
non-destructive reading of "this field needs twenty columns" -- fewer
makes it useless and more is fine -- so it composes with stretch instead
of fighting it, and it feeds §7.8's small-terminal policy rather than
bypassing it: a layout that cannot honour its minimums is exactly what
makes the compositor drop and scroll.

A non-positive size is ignored rather than obeyed, so a typo cannot pin
a widget to nothing -- the same direction `priority_of()` takes for an
out-of-range value.

**The width is a floor and the height is exact**, and that asymmetry was
not the first answer. A minimum alone honours the number and loses the
shape, which the document's own example makes visible: a `QLineEdit`
asked for 20x1 came out 38x1, because its vertical policy is `Fixed` and
held on its own -- but a `QLabel` asked for the same came out
**38x11**. Its policy is `Preferred`, so the layout stretched a one-row
annotation over eleven rows and floated the text in the middle of an
empty box. The floor was honoured and the annotation still did not mean
what it said.

The rule that fixes it is one the tree had already written down for
every control `sizeFromContents()` sizes: *"a single-line control is one
cell tall by construction. The width still snaps up, because a width is
a count of characters and rounding one down truncates text."* The hint
follows it -- minimum size in both axes, and a maximum height as well --
so the same sentence governs a widget qtty sizes and a widget the
application sizes.

Two checks, both pairs, because "nothing drawn" passes any assertion about
content failing to appear where it should not: the scrolled-out label must
be visible when it is in view and absent from the row above the area when
it is not, and a child eighteen cells wide inside a six-cell parent must
arrive clipped rather than whole.


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

**And that mitigation did not reach the library, which was found the same
way a month later.** It is not only a header that can be new: an
**`#include`** can be. `src/render/cell_paint.cpp` gained one for
`src/cell_geometry.h`; `make test` reported success; `cell_paint.o` was
not rebuilt, because its dependency list had been written before the
include existed. A sabotage of the header then failed to change the
binary -- 27 checks in other files went red and **the one check aimed at
the sabotaged code passed**, which reads exactly like a check that does
not discriminate. `touch`ing the `.cpp` is what said otherwise.

The cause is qmake's subdirs template, and `$(HEADERS)` cannot reach it.
The generated top-level Makefile recurses like this:

    cd src/ && ( test -e Makefile || qmake -o Makefile ... ) && make -f Makefile

**`test -e Makefile` means a sub-Makefile is generated once.** Re-running
the top-level qmake regenerates the top-level Makefile and leaves every
sub-Makefile exactly as it was, still carrying the scan taken when the
build directory was first configured. `build-test/` escaped this by
accident of shape: it is a plain app project with its own rule, and that
rule re-runs qmake unconditionally when it is out of date.

So the `$(BUILD_DIR)/Makefile` rule now removes the sub-Makefiles before
re-running qmake. They are **named rather than found** -- the list is
derived from the `.pro` files already in `$(PROFILES)`, so it cannot
drift from the set qmake recurses into and no wildcard decides what gets
deleted:

    SUBDIR_MAKEFILES = $(addsuffix Makefile, \
                         $(addprefix $(BUILD_DIR)/, \
                           $(dir $(filter-out qtty.pro qtty.pri,$(PROFILES)))))

Confirmed by doing it again with the fix in place: sabotaging
`cell_geometry.h` alone now rebuilds `cell_paint.o` and reddens the check
aimed at it, with nothing touched but the header. It costs one qmake run
per subdirectory on the configure step and nothing per build.

**What that costs backwards is worth stating plainly.** The refactor that
introduced the include was verified against a stale object: the "every
check and both fixtures unchanged" proof was taken from a binary that did
not contain the change. It was re-verified afterwards. The other commits
of that session were checked for the same exposure and are clean -- none
of them added an `#include` to a library source, and the test build
regenerates its own scan.

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

**And `8da1c99` is not that fix, in case its subject reads like it.**
That commit aligned brace continuations upstream, which touches the same
lexer and the same file; whether it happened to close the lambda case was
worth ten minutes rather than an assumption. Both compensating regions
were un-bent to plain tabs and the gate re-run: **9 violations**, the
same shape as before. The lambda case is open, and these two regions stay
bent.

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

1. ~~**Decide the layout-slack question (§7.8).**~~ **Taken**: `setup()`
   installs `GridSnap`, and §7.8 records what settled it -- the whole
   suite green with the filter on, both fixtures unmoved. What follows
   was the reasoning while it was open: Either qtty snaps child
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
   §7.4. `synchronisedOutput` is done too: DEC 2026 is asked for and the
   frames are bracketed when the terminal confirms it. What is left of
   that section is a title emitter, which is an addition rather than a
   gap.
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
and `setFocusWidget()` were exempt for a different reason -- they
deliberately mirror the `QApplication` call they replace (F4).

**Half of that was wrong, and the tree's own filter says which half.** The
rule is "an identifier is Qt's if it appears anywhere in Qt's headers".
Checked mechanically: `focusWidget` **is** there -- `QApplication`,
`QWidget` and `QGraphicsWidget` all declare it -- so it keeps Qt's
spelling and the exemption falls out of the filter rather than being a
special case. `setFocusWidget` appears in **no Qt header at all**. It was
carried along by association with the getter, and it is qtty's own name
like the other 117. It is `set_focus_widget()` now.

**The pairing was doing harm as well as being inconsistent.**
`setFocusWidget(scope->focusWidget())` reads as the setter and getter of
one thing and is not: the argument is Qt's **per-window** focus and the
call is qtty's **process-wide** one. Four sites in the library read that
way, and the rename makes them stop.

The rename carried its own proof, which is why a global replace was safe
here and would not have been for the getter: **the string
`setFocusWidget` occurs in no Qt header**, so no Qt call could be caught
by it. 25 occurrences before, 0 after, 25 of the new name, and no suite's
`CHECK` count changed.

Worth knowing about the getter, because it makes the exemption cheaper
than it looks: **`Qtty::focusWidget()` has no caller in `src` at all.**
The router and the compositor use `QWidget::focusWidget()`, and GridStyle
reads the variable directly. It exists for applications and for the one
check that exercises it, which is exactly the case its Qt spelling is
for.

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

What is left there is now **the bundled font**, which is what would make
the fixtures reproducible (§7.9), and it is a decision before it is work:
a font is somebody's licensed asset, and which one to carry is not a
question to answer while doing something else. What *has* been done is
the half that needs no licence -- the exposure is counted (7 of 102
installed families give the 10x19 the fixtures assume; all 102 give
integral metrics, so the hinting pin holds R3 off on this rasterizer) and
a check now names
the assumption instead of letting it surface as two unexplained fixture
diffs. §7.9 has the figures.

The rest of that list has been overtaken and the entries are corrected
here rather than left to be re-read as gaps. `CellItemDelegate` and the
item-view roles, `ICellPainted`, `PixelSurface`, submenus, mnemonics and
an editable combo box are all built and covered -- §7.5 records the two
that carried decisions worth keeping. The `QTextEdit` interaction layer
is exercised end to end: a plain text editor takes typing, Return and
wide clusters, and a `QTextEdit` takes them too.

**And the §11 benchmark exists**, as `suite_budget`. The sentence that
stood here said the 0.16 ms and 3.8 ms figures were spike measurements
nothing held, and that was true when it was written. What the fixture
does is deliberate and is the part worth knowing: it PRINTS its durations
for a human to read against §11's 16 ms and 50 ms budgets, and asserts
the damage behaviour instead. The reason is measured and is in that
file's own header -- the same binary rendered the same fixture in 1.35 ms
and 2.41 ms minutes apart, so a wall-clock assertion on a shared machine
is a coin toss wearing a threshold. A stale roadmap entry is worse than
none, because it sends somebody to build what is already there.
