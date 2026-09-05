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

Or with [fmake](../fmake), which needs no build file and nothing beyond the
Python standard library:

    python3 ~/src/fmake/fmake    # the four tools, the probe and the example

**Not `/usr/bin/fmake`.** The packaged one predates `$root` and leaves the
reference in the value as text, so the build succeeds and the binary
carries the literal string. Measured: `--version` printing
the reference text. Run the one in fmake's own tree until a newer package is
installed.

`fmake.toml` beside it says three things fmake cannot read off the tree:
that the tests find their snapshot fixtures through `QTTY_SOURCE_DIR`,
which qmake spells `$$QTTY_ROOT` and fmake spells `$root`; that `spike/`
is the Phase 0 record rather than something to build, the same exemption
`.style-gate.toml` already makes; and the tools' names, since fmake calls
a program after its root TU and these ship with a `qtty-` prefix.

It writes its objects into `.fmake/` and leaves the programs at the
repository root -- `chat`, `qtty-inspect`, `qtty-negotiate`, `qtty-replay`
and `screen-probe`. All five are in `.gitignore`; the list was four until
`screen-probe` arrived with the screen gate, which is why the line above
no longer counts them.

`make` remains the entry point: fmake builds the programs and not the
library, and none of the gates.
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

## Using it from another project

    make install PREFIX=/usr/local     # headers, libqtty.a, qtty.pc, tools

Testing without a terminal, which is what `NullBackend` is for:

```cpp
Qtty::NullBackend backend({80, 24});
Qtty::exec(app, win, backend);          // the seam
QCOMPARE(backend.last_frame().contains("Ready"), true);
```

Then, in a qmake project:

    CONFIG += link_pkgconfig
    PKGCONFIG += qtty

or anywhere else:

    g++ myapp.cpp $(pkg-config --cflags --libs qtty)

`qtty.pc` names the include path, the C++ standard and `Qt6Widgets`, so
nothing else has to be repeated. `make test-consume` builds and runs a
program this way against a temporary prefix, so the recipe above is checked
rather than described.

The call sequence, which is the one sharp edge:

```cpp
#include <qtty/qtty.h>

int main(int argc, char **argv) {
    Qtty::prepare_environment();          // BEFORE QApplication
    QApplication app(argc, argv);
    Qtty::setup(app);                     // BEFORE any widget: font + style
    MyWindow win;                         // ordinary Qt widgets
    return Qtty::exec(app, win);
}
```

`prepare_environment()` pins the platform to `offscreen` and turns High-DPI
scaling off; `setup()` installs the font, the style, the theme and the grid
snapper. Both must run in that order and before the widgets exist —
`include/qtty/application.h` says why, and `example/chat/` is a complete
program that does it.

Nothing else is required. A widget tree written against ordinary Qt renders
into the terminal as it is; `example/chat/chat.h` is deliberately free of
qtty types to show that.

What an application may ask for on top, none of it automatic:
`Qtty::set_priority()` for what to drop first on a small terminal,
`Qtty::CellItemDelegate` for item views with check states and icons,
`Qtty::set_icon_glyph()` to name a glyph for an icon, `Qtty::Overlay` for a
picture over the cells, and the `qtty.cells` widget property to state a size
in cells. Each is documented in its own header.

**Known limits, so they are not a surprise.** The library is a static
`libqtty.a`; there is no shared build and no CMake package file. The font is
resolved by `setup()` and must be monospace with integral metrics — see the
build requirements above. A progress bar cannot be made one cell tall by the
style alone, so an application that wants one calls
`setFixedHeight(GridMetrics::ch())` itself. A click on a slider sets the
value where it landed rather than paging — deliberate, because a cell is a
coarse target and a terminal slider has few of them, but it differs from
desktop Qt, where that is the middle button's job. And the API is pre-alpha: see the
status note at the top.

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

**One caution about `doc/design.md`.** Its API chapter (section 5.6)
describes a `qtty::Application` class that was never built: the shipped
surface is the free functions in `include/qtty/application.h`, in namespace
`Qtty`. Read design.md for the reasoning and the headers for the API.
`project.md` section 8.2 records the disagreement and whose decision it is.

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
