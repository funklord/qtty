# Phase-0 spikes (§16)

The validation spikes exactly as run, kept reproducible. Standalone build —
they predate the library and use the un-namespaced `qtty_core.h`:

    cmake -S . -B build && cmake --build build
    ./build/spike && ./build/spike2 && ./build/spike3 && ./build/spike4

spike:  gate 1+2 (render, menus), shortcut map, backingstore    -> §16
spike2: resize, combo popup, focus injection, damage, QTextEdit -> §16.1
spike3: graphics overlay compositing paths                      -> §16.2
spike4: chat view with scrolling sticker placements             -> §16.3
