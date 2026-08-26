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
- `TERM` containing `beerssh` is recognised and provisionally assumed
  `kitty-alpha` (the modern bar). Correct via `QTTY_GRAPHICS` until [Q1] is
  answered.

Proposed next step: a query handshake so capabilities survive ssh hops where
env vars may not (`SendEnv`/`AcceptEnv` friction):

    qtty -> beerssh:  DCS  > q t t y ; 1 ST        (version 1 hello)
    beerssh -> qtty:  DCS  > q t t y ; 1 ; <caps> ST

with `<caps>` a semicolon list: `g=kitty-alpha`, `sync=2026`, `mouse=1006`,
`paste=2004`, `l2=<width-table-version>`. Unanswered within ~50 ms → fall
back to env heuristics. The `l2` field lets both ends detect width-table
skew and degrade to ASCII-safe rendering rather than misalign.

- **[Q1]** Which graphics protocol(s) does beerssh implement or plan:
  kitty (with alpha-over-text?), sixel, iTerm2, something of its own?
- **[Q2]** Preferred identification: `TERM=beerssh`? `TERM=xterm-beerssh`
  (ncurses-compat)? An exported env? The DCS handshake?
- **[Q3]** DEC 2026 synchronised output supported? SGR 1006 mouse?
  Bracketed paste? Focus events (1004)?

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
