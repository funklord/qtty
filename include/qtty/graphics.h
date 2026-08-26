// qtty/graphics.h -- L4.5 graphics plane primitives (sections 5.7, 17.3):
// terminal-mode negotiation, protocol encoders, the cell rasterizer for the
// software-composite path, and the colour half-block fallback.
#pragma once
#include <QImage>
#include <QByteArray>
#include <QFont>
#include "cell.h"
#include "backend.h"

namespace Qtty {

// Best pixel-graphics mode for the current terminal, from environment
// heuristics (KITTY_WINDOW_ID, TERM, TERM_PROGRAM). DA1 query-based
// detection is a later refinement; env covers the mainstream terminals.
Capabilities::GraphicsMode detectGraphicsMode();

// ---- encoders --------------------------------------------------------------
// Each returns complete escape-sequence bytes ready to write to the tty.

// DEC SIXEL. P2=1 (untouched pixels transparent); palette quantised through
// the xterm-256 cube; pixels with alpha < 128 are omitted (transparent).
QByteArray encodeSixel(const QImage &img);

// kitty graphics protocol: transmit-and-display an RGBA image with id `id`
// at the current cursor cell. Chunked at 4096 base64 bytes. z: stacking
// (positive = above text, the alpha-over-text path).
QByteArray encodeKittyImage(quint32 id, const QImage &img, int z = 0);
// Re-display an already-transmitted image at the cursor (upload-once, section 5.7).
QByteArray kittyPlace(quint32 id, int z = 0);
// Delete all visible kitty placements (start-of-frame reset).
QByteArray kittyDeleteAll();

// iTerm2 inline image (OSC 1337), sized in cells.
QByteArray encodeITerm2(const QImage &img, int wCells, int hCells);

// ---- software composite (section 5.7 middle tier) ---------------------------------
// Rasterise a cell frame to pixels with the given monospace font -- the image
// a sixel/iTerm2 terminal is sent after overlays are blended on top.
QImage rasterize(const CellBuffer &frame, const QFont &font);

// ---- fallback tier ---------------------------------------------------------
// Composite an alpha image into cells at cellRect (colour half-blocks, two
// vertical samples per cell). Translucent regions tint the cell background
// and leave existing glyphs readable; opaque regions become upper/lower/full block pixels.
void composeHalfblocks(CellBuffer &frame, const QImage &img, const QRect &cellRect);

} // namespace Qtty
