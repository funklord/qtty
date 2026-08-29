# qtty

Render an **unmodified Qt Widgets application** on a character-cell terminal.
One `QWidget` codebase produces both the desktop GUI and the TUI — no parallel
view layer, no Qt fork, no custom QPA plugin. Built on four public seams:
`QStyle` metrics on a cell grid, `Qt::WA_DontShowOnScreen` widget lifecycle,
a custom `QPaintDevice`/`QPaintEngine`, and a terminal-backend interface that
existing TUI codebases adapt to.

**Status: pre-alpha.** The architecture is spike-validated (see
`doc/design.md` §16: rendering, popups, input, focus, resize, placements,
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
- Strict hygiene contract (`doc/design.md` §10.1): everything in
  `namespace Qtty`, no public macros, inert in GUI builds.

## Build

The `Makefile` at the root is the entry point:

    make                # build the library, the tools and the example
    make test           # build and run the test suite
    make check          # style + test — what must pass before committing
    make style          # the shared source gate
    make hooks          # install the commit-msg hook
    make help           # every target

`make` drives qmake into `BUILD_DIR` (default `build/`), so a build never
writes into the source tree. `DEBUG=1` gives `-Og`; the default is `-Os`.
`SANITIZE=1` adds ASan/UBSan. Running qmake by hand in a directory of your
own still works, and is what the Makefile does:

    mkdir build && cd build && qmake6 ../qtty.pro && make -j

Requires Qt 6 — built here against 6.8.2 — and a monospace font with
integral metrics (DejaVu Sans Mono for now; bundled font planned). Fixture
maintenance: `make record R=render` rewrites the render snapshot after a
reviewed change. The example runs with `make run`, or directly as
`build/example/chat/chat --tui`.

## Layout

    include/qtty/     public headers (§10)
    src/core|grid|render|runtime|graphics/
    src/backend/      ansi (built-in), null (CI), termpaint + legacy (planned)
    src/widget/       replaced widgets (planned)
    example/chat/     canonical dual-frontend example (§16.4)
    test/             the suite; text fixtures in test/snapshot/
    tool/             qtty-inspect, qtty-replay, style_gate.py, hooks/
    doc/design.md     the design document — read this first
    doc/beerssh.md    integration contract with beerssh (the terminal end)
    spike/            the Phase-0 spikes exactly as run (§16); standalone
    Makefile          the entry point; qtty.pro and qtty.pri are the qmake side
    VERSION           the one place the version number is stated
    project.md        design and intent; code-style.md, .style-gate.toml

## Naming

The project is **qtty** lowercase in file-system contexts — repo, library,
headers/include path, package and binary names — with the C++ namespace
**`Qtty::`**, matching the Qt-ecosystem mould (`Qt::`, `KIO::`, `QXlsx::`;
the KDE pattern of repo `kio` / namespace `KIO`). Env vars and macros are
`QTTY_*`. In prose, "Qtty" (the Git/git pattern). Never `QTty` or Q-class
styling — that convention belongs to Qt's classes. Descriptively: "qtty —
terminal rendering for Qt applications" (the trademark fair-use form).

qtty is not affiliated with or endorsed by The Qt Company. "Qt" is a
trademark of The Qt Company Ltd.

## Copyright

Copyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>

## License

Not yet chosen (design intends a permissive license; note Qt's own LGPLv3/GPL
terms apply to linking Qt).
