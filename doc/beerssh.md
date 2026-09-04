# qtty ↔ beerssh integration contract (draft)

beerssh is the other end of the pipe: a terminal/ssh program with modern
feature support, developed alongside qtty. Owning both ends changes several
design calculations; this document is the working contract between the
projects. Items marked **[Q]** are questions beerssh's side needs to answer;
everything else is what qtty already provides or proposes.

## 1. Shared L2 — one width table, zero misalignment

The classic unfixable TUI bug is the application and the terminal disagreeing
about Unicode display width (emoji, ambiguous-width CJK, ZWJ clusters).
Neither side can fix it alone.

**Proposal:** beerssh links (or vendors) qtty's L2 — `Qtty::CellBuffer`,
`Qtty::clusterWidth()`, `Qtty::toClusters()` (`include/qtty/cell.h`,
`src/core/cellbuffer.cpp`, no GUI dependencies beyond QtGui's QPixmap, which
can be compiled out if needed). Both ends then compute width from the same
table and disagreement is impossible *by construction*. Divergence becomes a
version-skew problem, handled by the handshake below.

- **[Q]** Is beerssh Qt-based / can it take a QtCore dependency, or should
  L2 be extractable as a plain-C++ core?

## 2. Capability negotiation

Today (implemented):

- `QTTY_GRAPHICS` env override: `none | halfblocks | sixel | iterm2 | kitty
  | kitty-alpha`. A cooperating terminal exports this and qtty obeys —
  exact, no sniffing.
- ~~`TERM` containing `beerssh` is recognised and provisionally assumed
  `kitty-alpha`.~~ **Removed**, and the removal is the point of the
  measurement below. That special case was a guess at a sibling's
  capability set made before either end could ask; now that beerssh answers
  the standard kitty query, the measured path decides and the guess is
  never reached. Where it *would* still be reached -- a beerssh that
  answered nothing -- it was actively dangerous: it said yes to kitty on
  behalf of a terminal that had just proved silent, and it assumed
  **alpha over text**, which beerssh has never claimed and the protocol has
  no query for. A silent beerssh gets half-blocks now.

Proposed next step: a query handshake so capabilities survive ssh hops where
env vars may not (`SendEnv`/`AcceptEnv` friction):

    qtty -> beerssh:  DCS  > q t t y ; 1 ST        (version 1 hello)
    beerssh -> qtty:  DCS  > q t t y ; 1 ; <caps> ST

with `<caps>` a semicolon list: `g=kitty-alpha`, `sync=2026`, `mouse=1006`,
`paste=2004`, `l2=<width-table-version>`. Unanswered within ~50 ms → fall
back to env heuristics. The `l2` field lets both ends detect width-table
skew and degrade to ASCII-safe rendering rather than misalign.

### Measured, 2026-08-28

The handshake above was drafted before qtty asked anything. It now asks the
**standard** questions — a batched kitty query, XTGETTCAP, OSC 11, the
window-op size reports and device attributes, with DA1 last as the fence —
and beerssh answers three of them. So most of the proposal is unnecessary:
a bespoke `DCS >qtty` exchange would buy nothing the existing protocols do
not already carry, and it would only work against one terminal.

Measured with `beerssh --term-features=<spec> -e qtty-negotiate` against
beerssh at `3525de0`, with a raw capture from a C program sharing no code
with qtty so that the instrument is not the thing under test.

**When to re-take this, agreed with the beerssh session 2026-08-31:** when
something touches the emulator, the negotiation, or the vendored libvterm
patches -- not on every commit. Build tooling and CI lifecycle work cannot
move a byte of these tables, and re-running against them spends a
measurement to confirm no change. A method without a trigger gets re-run
either too often or never, and never is the one that leaves a stale table
looking current.

## Silence, and the one place it is the wrong answer

**Measured 2026-08-31 with `qtty-negotiate --probes`**, which reports each
probe as answered or silent rather than reporting what qtty concluded.

**Both ends must be a terminal to re-take this**, and since 2026-09-04 the
tool enforces it rather than trying: `--probes` refuses, and says so on
stderr, unless stdout is a terminal and stdin can be put in raw mode. So
`-e qtty-negotiate --probes` inside beerssh is the way to run it and
`... > capture.txt` is not -- redirecting stdout used to put the whole
query into the file and then wait 200 ms for a reply that could not come.
Capture the report with `QTTY_NEGOTIATE_OUT=<path>`, which writes it to a
file while the probes still go to the terminal. The
ordinary output cannot answer this question: under the asymmetry rule an
unverifiable signal may only say yes, so "no reply" and "a reply saying no"
reach the same conclusion there, deliberately -- and they are exactly the
two a terminal implementer needs told apart.

Every announcement-style capability goes **silent** when switched off, which
is the behaviour asked for. The byte counts are the evidence, since a reply
that shrank rather than vanished would still be a reply:

| spec | raw reply | what stopped |
|---|---|---|
| *(all on)* | 568 B | -- |
| `-kitty-graphics` | 556 B | the `a=q` OK, 12 bytes, gone entirely |
| `-sixel` | 566 B | DA1 `?1;2;4c` becomes `?1;2c` |
| `-xtgettcap` | 533 B | both DCS replies |
| `-window-size` | 547 B | `CSI 14t` and `CSI 16t` |
| `-colour-query` | 121 B | OSC 11 and all sixteen OSC 4 replies |
| `none` | 51 B | everything but the fence and the mode replies |

**The DEC modes are the exception, and answering is right there.** With
`-synchronised-output`, `-sgr-mouse`, `-bracketed-paste` or
`-focus-reporting`, DECRQM answers **0, not recognised** -- and the reply
stays 568 bytes. That is not answering-but-inert; it is the protocol's own
way of saying no, and it is strictly better than silence. `mode_usable()`
here treats silence as "learned nothing, keep your assumption" and 0 as a
definite no, so a terminal that went quiet would leave qtty believing
whatever it believed before, while one that answers 0 turns the capability
off. **Silence is the right answer for a claim; a definite no is the right
answer for a question that has one.**

**Re-taken 2026-08-30, and again on 08-31 with `--probes`.** The version
measured is stated by its method, because the first attempt at this line
named a commit that was inferred rather than known: what was run is the
`beerssh` binary built 2026-08-30 17:35:28, which **postdates `395f354`
(08-29 19:46) and predates `71eefea` (08-30 18:17)**, so `395f354` is the
newest commit it can contain unless it was built from uncommitted work,
which nothing here can tell. The four probe fixes it exercises landed
earlier still, in `3525de0`.

What that does NOT cover is anything after `71eefea`, `a8b4187` included --
the binary on this machine is older than that commit by eight hours, so no
measurement here says anything about it. Every row below reproduces, along
with the background `#000000`, the palette entries `0=#000000 1=#aa0000`
and the 9x20 cell size recorded further down. The point of re-running it is
that the fact carries its method: a cross-implementation claim ages
silently, because it is a statement about somebody else's program that
nothing here would notice going stale, and the command that produced it is
what makes the check cost a minute rather than a reconstruction:

| beerssh spec | qtty negotiates |
|---|---|
| *(all features)* | Kitty, TrueColor |
| `-kitty-graphics` | Sixel, TrueColor |
| `none` | Halfblocks, Xterm256 |
| `none,+sixel` | Sixel, Xterm256 |
| `none,+kitty-graphics` | Kitty, Xterm256 |

The wire reply, all features on:

    ESC _ G i=31 ; OK ESC \     kitty graphics: yes
    ESC P 1 + r 524742=382F382F38 ESC \   XTGETTCAP RGB: yes, "8/8/8"
    ESC P 0 + r 5463 ESC \      XTGETTCAP Tc: no
    ESC [ ? 1 ; 2 ; 4 c         device attributes, 4 = sixel

**[Q1] is answered in part**: kitty graphics and sixel, both discoverable by the
conventional probes. Whether kitty alpha-over-text works is NOT answered —
the protocol has no query for it, which is why qtty picks that variant from
`$TERM` and treats a wrong guess as costing appearance rather than
correctness.

**[Q3] is partly answered.** Direct colour is confirmed by XTGETTCAP. SGR
mouse, bracketed paste and DEC 2026 are **not** measured here: qtty reports
its own mouse and paste flags from whether it got raw mode, not from
anything the terminal said, so this document must not quote them as
beerssh's.

### Re-measured 09:31, after both ends moved

Both gaps below are **closed**, and qtty now asks four more questions. What
beerssh answers today, all features on:

    ESC _ G i=31;OK                       kitty
    ESC P 1+r 524742=382F382F38           RGB, "8/8/8"
    ESC P 1+r 5463                        Tc -- now yes, was 0+r
    ESC ] 11 ; rgb:0000/0000/0000         background -- new
    ESC [ 4 ; 432 ; 720 t                 text area -- new
    ESC [ 6 ; 18 ; 9 t                    cell 9x18 -- new
    ESC [ ? 1006 ; 1 $ y                  SGR mouse: set
    ESC [ ? 1004 ; 1 $ y                  focus: set
    ESC [ ? 2004 ; 1 $ y                  bracketed paste: set
    ESC [ ? 2026 ; 2 $ y                  synchronised output: reset
    ESC [ ? 1 ; 2 ; 4 c                   DA1, 4 = sixel

`--term-features=-synchronised-output` turns that `2026;2` into `2026;0`,
and nothing else changes -- so the switch is honest and qtty reads the
difference. That is the first capability qtty has verified against a
terminal that can be told to lack it, rather than against its own double.

**A discrepancy, and it is qtty's.** beerssh reports a cell of **9x18**
while qtty's own `GridMetrics` derives **10x19** from DejaVu Sans Mono at
16px. Both are right about different things: qtty's is the arithmetic its
layout runs on, beerssh's is what a cell measures on screen.

**The first version of this paragraph blamed the wrong function, and the
correction is worth more than the finding.** It said
`compose_halfblocks()` samples against the internal ratio. It does not --
it references `GridMetrics` nowhere at all, fits the image to the
`cell_rect` it is handed, and carries a comment saying exactly that. A
reader sent there would have found innocent code and no fault.

The mechanism is the LAYOUT, not the sampler. A pixmap a widget draws
reaches `CellPaintEngine::to_cells()`, which converts pixels to cells at
qtty's internal 10x19; the terminal then draws each of those cells at
9x18. So a WxH image occupies `ceil(W/10) x ceil(H/19)` cells and is shown
at `(cells_w * 9) x (cells_h * 18)` pixels -- an aspect of
`(W/H) * (9/10) * (19/18)`, about five per cent narrow. The figure was
right and the cause was not.

It is not image-specific in origin and not a bug in any one function:
qtty's notion of a pixel is simply not the terminal's, and only things
whose appearance depends on pixel ASPECT notice. Text and box drawing do
not, being cells. What an application should do is size images through
`Qtty::cells()` with `Capabilities::cell_px`, which is the terminal's
figure and correct -- which is what that pair is for.

**Both ends were moving during this, twice.** An earlier run of the tool
reported the background as unanswered while a raw capture minutes later
showed it arriving; the parser was fine on the exact bytes, and beerssh had
simply gained OSC 11 in between -- its binary was rebuilt twenty seconds
before the run that finally saw it. Every figure in this file is worth a
timestamp and the other binary's mtime, which is why both are given.

### The negotiation falsified, 10:11, beerssh c1ebf5b

beerssh's `--term-features` now switches all twelve, and the three mode
features answer `0` rather than `2` -- the veto reaches `request_dec_mode`
as well as `set_dec_mode`, so the mode is never recorded and DECRQM never
claims it. That distinction was the one qtty asked for, and it is the whole
reason this table means anything:

| beerssh spec | qtty reports |
|---|---|
| *(all)* | mouse yes, paste yes |
| `-sgr-mouse` | **mouse no**, paste yes |
| `-bracketed-paste` | mouse yes, **paste no** |
| both | **mouse no**, **paste no** |

**Before the DECRQM work qtty reported `yes` in every one of those rows**,
because it took the answer from whether it got raw mode -- a fact about the
local tty device that says nothing about the terminal. This is the first
capability in this tree to be **falsified** by a terminal that genuinely
lacks it, rather than confirmed by qtty's own test double, and the two
switches move independently.

The mouse is deliberately one switch on their side -- 1000, 1002, 1003,
1005, 1006 and 1015 together -- because gating the SGR encoding alone would
produce a terminal with tracking and no usable encoding, which is stranger
than either having a mouse or not. Worth knowing before anyone reads
`-sgr-mouse` as "SGR only".

### OSC 4, and a claim of qtty's own that was too broad

beerssh answers `ESC ] 4 ; <n> ; ? ST` now, one reply per index, several
pairs per query. qtty asks for **0..15 only**: 16..255 are a formula every
terminal shares, so asking would be 240 round trips to learn what is
already known.

**qtty's own request overstated the case, and the correction belongs
here.** It was put as "every quantised colour is computed against the wrong
table". It is not. `Color::to_xterm256()` builds its candidate list from
indices **16..255 only** -- the low sixteen are deliberately excluded from
256-colour matching -- so a user's scheme cannot affect that path at all.
What it affects is `to_ansi16()`, which picks the nearest of the sixteen
for a colour carrying no authored role, and that is a documented last
resort rather than the primary route.

Narrower, then, but real: picking "nearest" against the wrong sixteen
colours is how a fallback lands somewhere the user can see is wrong.

Measured 10:53, beerssh binary 10:48:54 before the run **and after it**:

    palette   16 low entries, 0=#000000 1=#aa0000

qtty's built-in table has index 1 at **#800000**. So beerssh's default
scheme differs from the table qtty would otherwise have assumed, on the
very first index -- exactly as beerssh predicted when it said a default
build ships a scheme and the difference would show on a normal run rather
than an exotic one.

A partial answer is refused rather than mixed: half a user's scheme and
half xterm's is a palette no terminal has, and matching against it would
be worse than matching against either.

### Superseded: two things beerssh did not answer

- **OSC 11**, the background colour. Every tier below kitty composites an
  image's alpha against it; without an answer qtty uses a dark grey, which
  haloes translucent edges on a light terminal.
- **`CSI 14 t` / `CSI 16 t`**, the text area and cell size in pixels.
  Without the cell size an image's aspect ratio cannot be preserved: a
  half-block pixel is one cell wide and half a cell tall, and assuming 1:2
  squashes every picture on a terminal whose cells are not.

Neither is a defect in beerssh — they are not required of a terminal — but
both are cheap to answer and each unlocks a specific correctness in the
client. Recorded here rather than written into beerssh's own tree, which is
not this project's to edit.

**One earlier measurement was wrong and is withdrawn.** A first pass found
`none,+sixel` yielding half-blocks, i.e. sixel enabled but not advertised in
DA1. It was taken while that tree was being rebuilt under it, so it was not
a measurement at all; beerssh's `3525de0` ("answer DA1 and XTVERSION for
this terminal, not for libvterm") lands attribute 4, and the table above is
against a settled build.

- **[Q2]** Preferred identification: `TERM=beerssh`? `TERM=xterm-beerssh`
  (ncurses-compat)? An exported env? The DCS handshake? Still open, and
  less pressing now that the standard queries answer: identification only
  decides the kitty variant.

## 3. A cooperative wire profile (future, high value over ssh)

Because both ends are ours, qtty need not limit itself to standard escapes.
Candidate extensions, in rough order of value:

1. **Cell-diff frames.** qtty already computes exact damage
   (`CellBuffer::diff()` → QRegion). A compact DCS carrying only changed
   runs (row, col, run of styled clusters) beats full-screen SGR streams
   over slow links, and beerssh applies them without parsing ambiguity.
2. **Placement lifecycle.** qtty's kitty-tier already does upload-once /
   re-place-by-id; a cooperative profile could add explicit eviction hints
   and placement cropping at viewport edges (§16.3's half-visible sticker).
3. **Semantic annotations.** Clickable regions (widget under cell), focus
   ring location, window title/role — enabling beerssh-side mouse cursor
   shaping, screen-reader hooks, and click-through without qtty round-trips.
4. **Pixel-exact resize.** beerssh reports cell size in px on resize
   (like XTWINOPS 14/16), letting qtty pick CW×CH to match the real font
   for pixel-perfect placements.

Rule: every extension degrades cleanly — qtty must render correctly on
terminals that answer none of this.

## 4. Shared test harness

- `qtty-replay` drives real screens from scripted input and can emit either
  text frames (for qtty's own snapshots) or the raw ANSI/graphics byte
  stream (`--ansi`) — the latter is a deterministic corpus for beerssh's
  parser tests.
- **[Q]** Does beerssh have (or want) a headless mode — escape stream in,
  grid state out (text or JSON)? With that, CI runs the full loop:
  `qtty-replay --ansi script | beerssh --headless | diff fixture` — qtty
  encoder bugs and beerssh parser bugs surface in one harness, attributable
  by which fixture moved.

## 5. Non-goals

qtty stays terminal-agnostic: beerssh is the first-class partner, never a
requirement. The §5.7 tier table and this contract's fallback rules are the
guarantee.
