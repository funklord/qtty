# qtty

Render an **unmodified Qt Widgets application** on a character-cell terminal.
One `QWidget` codebase produces both the desktop GUI and the TUI — no parallel
view layer, no Qt fork, no custom QPA plugin. Built on four public seams:
`QStyle` metrics on a cell grid, `Qt::WA_DontShowOnScreen` widget lifecycle,
a custom `QPaintDevice`/`QPaintEngine`, and a terminal-backend interface that
existing TUI codebases adapt to.

**Status: pre-alpha.** The architecture is spike-validated (see
`docs/design.md` §16: rendering, popups, input, focus, resize, placements,
graphics compositing — all measured, not assumed); the library itself is the
Phase-2 build-out of those spikes. Expect API movement.

## Highlights

- Vanilla Qt code is the API: `drawPixmap` in a delegate becomes a
  cell-anchored image placement; `QTextDocument`, item views, menus, and
  popups render through their normal paths.
- Graphics tiers per terminal: kitty protocol (pixels that scroll with text),
  sixel/iTerm2 software composite, half-block/mosaic fallback — declared in
  `Capabilities`, invisible to app code.
- Deterministic snapshot testing with no tty (`NullBackend`, text fixtures).
- Strict hygiene contract (`docs/design.md` §10.1): everything in
  `namespace qtty`, no public macros, inert in GUI builds.

## Build

    cmake -S . -B build && cmake --build build -j
    ctest --test-dir build            # snapshot + invariant tests
    ./build/examples/chat/chat --tui  # run the example in your terminal

Requires Qt 6 (Qt 5.15 support planned), CMake ≥ 3.16, a monospace font with
integral metrics (DejaVu Sans Mono for now; bundled font planned).

## Layout

    include/qtty/     public headers (§10)
    src/core|grid|render|runtime/
    src/backends/     ansi (built-in), null (CI), termpaint + legacy (planned)
    examples/chat/    canonical dual-frontend example (§16.4)
    tests/            CTest suite + snapshot fixtures
    tools/            qtty-inspect (qtty-replay planned)
    docs/design.md    the design document — read this first
    spikes/           the Phase-0 spikes exactly as run (§16); standalone

## Naming

The project is **qtty** — lowercase in every machine-facing context: namespace,
CMake target, library, headers, package and binary names. Env vars and macros
are `QTTY_*` by their own conventions. In prose, "Qtty" at sentence and title
positions is fine (the Git/git pattern). Never `QTty` or Q-class styling —
that convention belongs to Qt's classes. Descriptively: "qtty — terminal
rendering for Qt applications" (the trademark fair-use form).

qtty is not affiliated with or endorsed by The Qt Company. "Qt" is a
trademark of The Qt Company Ltd.

## License

Not yet chosen (design intends a permissive license; note Qt's own LGPLv3/GPL
terms apply to linking Qt).
