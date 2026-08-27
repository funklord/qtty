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
Capabilities::GraphicsMode detect_graphics_mode();

// ---- encoders --------------------------------------------------------------
// Each returns complete escape-sequence bytes ready to write to the tty.

// DEC SIXEL. P2=1 (untouched pixels transparent); palette quantised through
// the xterm-256 cube; pixels with alpha < 128 are omitted (transparent).
QByteArray encode_sixel(const QImage &img);

// kitty graphics protocol: transmit-and-display an RGBA image with id `id`
// at the current cursor cell. Chunked at 4096 base64 bytes. z: stacking
// (positive = above text, the alpha-over-text path).
QByteArray encode_kitty_image(quint32 id, const QImage &img, int z = 0);
// Re-display an already-transmitted image at the cursor (upload-once, section 5.7).
// Place an already-uploaded image. `source`, when non-empty, selects a
// rectangle of it in IMAGE PIXELS -- kitty's a=p understands x/y/w/h, so a
// partly-visible placement is cropped at display time and the upload stays
// whole. That is what keeps upload-once working across a crop: cropping the
// image instead would put different pixels under the same cache key, and the
// next full sighting would show the cropped one.
QByteArray kitty_place(quint32 id, int z = 0, const QRect &source = QRect());
// Delete all visible kitty placements (start-of-frame reset).
QByteArray kitty_delete_all();

// iTerm2 inline image (OSC 1337), sized in cells.
QByteArray encode_iterm2(const QImage &img, int w_cells, int h_cells);

// ---- software composite (section 5.7 middle tier) ---------------------------------
// Rasterise a cell frame to pixels with the given monospace font -- the image
// a sixel/iTerm2 terminal is sent after overlays are blended on top.
QImage rasterize(const CellBuffer &frame, const QFont &font);

// ---- fallback tier ---------------------------------------------------------
// Composite an alpha image into cells at cell_rect (colour half-blocks, two
// vertical samples per cell). Translucent regions tint the cell background
// and leave existing glyphs readable; opaque regions become upper/lower/full block pixels.
// What of a placement is actually on screen (section 16.3). A sticker scrolled
// half out of the viewport has a cell_rect extending past the grid, and every
// pixel tier placed it at its full size regardless: kitty and sixel drew
// outside the terminal, and a placement scrolled off the top was positioned at
// a negative row. Only the mosaic tier was safe, and only because it composites
// into the CellBuffer, which clips by construction.
//
// `cells` is where to draw and is empty when the placement is wholly off
// screen. `source` is the matching rectangle of the image in pixels, derived
// through the grid, and equals the whole image when nothing was cropped.
struct CroppedPlacement {
	QRect cells;
	QRect source;
};
CroppedPlacement crop_placement(const QRect &cell_rect, QSize image, QSize grid);

void compose_halfblocks(CellBuffer &frame, const QImage &img, const QRect &cell_rect);

} // namespace Qtty
