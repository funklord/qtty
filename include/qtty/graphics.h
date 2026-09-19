// qtty/graphics.h -- L4.5 graphics plane primitives (sections 5.7, 17.3):
// terminal-mode negotiation, protocol encoders, the cell rasterizer for the
// software-composite path, and the colour half-block fallback.
#pragma once
#include <QImage>
#include <QTextDocument>
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
// Transmit and display one tile, with an image id and a PLACEMENT id, so a
// later tile at the same ids replaces it instead of adding to it.
// encode_kitty_image() sends no placement id, which is what makes repeated
// calls accumulate.
QByteArray encode_kitty_tile(quint32 id, quint32 placement, const QImage &img);

QByteArray kitty_delete_all();

// The tiles a damage region touches, in cells, aligned to a fixed grid and
// clipped to the screen.
//
// Why a fixed grid rather than the damage itself: replacing a kitty placement
// VACATES its old rectangle -- measured, two placements sharing an image and
// placement id leave 0 pixels of the first -- so a patch that lands somewhere
// new each frame erases the last one and reveals whatever is underneath.
// Reusing an id is only safe where the rectangle repeats, and a fixed tile
// grid is what makes it repeat. The placement count is then bounded by the
// tile count instead of by frames.
//
// `tile` trades wasted pixels per tile against placements per screen. Both
// are arithmetic; neither is terminal behaviour.
QVector<QRect> dirty_tiles(const QRegion &damage, const QSize &grid, int tile);

// iTerm2 inline image (OSC 1337), sized in cells.
QByteArray encode_iterm2(const QImage &img, int w_cells, int h_cells);

// ---- the terminal's own ground ---------------------------------------------
// What the compositing sites below paint against, resolved ONCE from what the
// terminal answered.
//
// It is a type rather than two or three loose colours because the RESOLUTION
// is what was wrong, not the painting. Capabilities::background has carried
// the OSC 11 reply since graphics negotiation needed something to composite
// alpha against, and three places in this library paint a ground: the
// CellImage mosaic, the overlay half-block fallback, and the rasteriser that
// feeds the pixel tiers. The mosaic read the capability through a ternary
// written at its own call site; the other two had no ternary to copy and
// painted constants. One rule written once cannot drift between three
// callers, and a rule spelled at each call site already had. section 8.250.
//
// Every function that paints a ground takes one of these and NONE of them
// defaults it. The default argument this replaces was deliberate, and it is
// exactly what failed: compose_halfblocks() said "a caller with no terminal
// to ask keeps the old behaviour by saying nothing", and the caller that said
// nothing was the compositor -- which had a terminal, and had asked it. A
// required parameter turns forgetting into a compile error, which is the only
// guard that reaches a caller nobody has written yet.
struct TerminalGround {
	// What a translucent pixel is composited against. The BACKGROUND alone
	// is a complete answer here, there being one colour in the question, so
	// a terminal that replied to OSC 11 and ignored OSC 10 still gets its
	// own ground rather than a guess. That case is not hypothetical: this
	// tree's own pty fixture answers exactly that way.
	QRgb composite_under = qRgb(16, 20, 24);

	// And the pair a Color::Default cell is painted with. These two abstain
	// TOGETHER -- both of the terminal's or neither -- because what makes a
	// cell legible is the RELATIONSHIP between them, and a real background
	// under a guessed foreground is the one combination that can come out
	// unreadable. A white terminal answering OSC 11 alone would otherwise
	// get this library's pale default ink on its own pale ground.
	//
	// Same asymmetry harmonization.md settles and section 8.243 applied to
	// the colour scheme: a wrong light guess leaves an application looking
	// plain, a wrong dark guess cannot be read, so there is no coin to toss.
	QRgb default_bg = qRgb(16, 20, 24);
	QRgb default_fg = qRgb(215, 218, 220);

	// The rule, in the one place it is written. A default-constructed
	// TerminalGround holds what this library painted with before it ever
	// asked a terminal anything, so silence is served by the member
	// initialisers above rather than by a branch somebody has to remember at
	// each site.
	static TerminalGround from(const Capabilities &caps);

	// Two names for states a caller may hold directly, so that a call site
	// can SAY which one it means. A bare TerminalGround{} at twenty call
	// sites is correct and says nothing; "unanswered" is the rule's own
	// word, and it is what a reader of a check needs to know is being
	// asserted. Neither is a second policy -- unanswered() is the default
	// construction and under() is from() with the background answered and
	// the foreground not, which is a real terminal this tree has one of.
	static TerminalGround unanswered() { return TerminalGround(); }
	static TerminalGround under(QRgb bg) {
		TerminalGround g;
		g.composite_under = bg;
		return g;
	}
};

// ---- software composite (section 5.7 middle tier) ---------------------------------
// Rasterise a cell frame to pixels with the given monospace font -- the image
// a sixel/iTerm2 terminal is sent after overlays are blended on top.
QImage rasterize(const CellBuffer &frame, const QFont &font,
                 const TerminalGround &ground);

// The same, painted into an image that already exists and only over the cells
// named. What it is for: the software-composite path rasterises the whole
// screen every frame, measured at 18.4 ms for 200x60 against section 11's 16 ms
// budget, so the render is the dominant cost there and damage-limiting the
// TRANSMISSION saved none of it.
//
// `cells` is expanded leftwards to the start of any wide cluster it cuts,
// because a continuation cell carries no glyph: a region beginning there would
// paint no character and leave the previous frame's pixels showing through.
// The same rule the text path needs, for the same reason.
//
// rasterize() is this function over the whole frame, so the two cannot
// disagree about what a cell looks like.
//
// Returns the rectangle it ACTUALLY painted, which is `cells` after that
// expansion. A caller that draws anything over the cells -- the frame loop
// paints placements and overlays -- has to clip to the same rectangle: clip
// to the narrower one and the expanded column gets fresh cell pixels with no
// overlay repainted over them, which is a hole in the overlay exactly one
// cell wide. Returning it is what stops the rule being written twice.
QRect rasterize_into(QImage &dst, const CellBuffer &frame, const QFont &font,
                     const QRect &cells, const TerminalGround &ground);

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

// How many cells an image of `image_px` pixels occupies on a terminal whose
// cell measures `cell_px`. design.md section 5.7 calls this one of the two
// GUI-invisible accommodations an application may use to size an image.
//
// It needs the cell size and cannot assume one, which is the whole point: a
// half-block pixel is one cell wide and half a cell tall, and treating a cell
// as square squashes every picture on a terminal whose cells are not 1:2.
// Capabilities::cell_px carries the answer once the terminal has given it,
// and is invalid until then -- so a caller with no answer gets an empty size
// back rather than a plausible wrong one.
//
// Rounded up. An image that needs four and a half cells is given five and
// leaves a margin; given four it would be cropped, and a picture missing its
// last row is worse than one with a gap under it.
QSize cells(QSize image_px, QSize cell_px);

// kitty's Unicode-placeholder mode: the path that survives tmux, and the one
// design.md section 5.7 calls stronger still.
//
// An image is transmitted once and given a VIRTUAL placement, then displayed
// by printing ordinary text -- U+10EEEE with combining diacritics encoding the
// row and column, and the image id carried in the foreground colour. Because
// the placement is text, anything Unicode-aware moves it correctly when it
// redraws: tmux, vim, weechat, and qtty's own diff machinery, with no special
// cases anywhere.
//
// The transmit is quiet (q=2) so the terminal sends no replies that would be
// read as input by whatever is in between.
//
// Both halves come from kitty's specification rather than from recall, and so
// does the diacritic table -- see src/graphics/kitty_diacritics.h. The
// placeholder character is U+10EEEE; it is easy to misremember, and a wrong
// one prints a private-use box in every cell.
QByteArray encode_kitty_virtual(quint32 id, const QImage &img, int cols, int rows);

// Write a virtual placement's placeholder cells into the buffer. One cell per
// grid position, each carrying U+10EEEE plus the row and column diacritics --
// a single grapheme cluster, which is exactly what a Cell holds -- with the id
// in the foreground colour.
//
// Ids of 2^24 or more need a third diacritic for the most significant byte,
// because the foreground colour carries only 24 bits.
void compose_kitty_placeholders(CellBuffer &frame, quint32 id, const QRect &cell_rect);

// The other accommodation design.md section 5.7 names: round every image in a
// QTextDocument up to a whole number of cells.
//
// An image embedded in the TEXT FLOW must have a cell-multiple size or every
// line after it leaves the cell rows -- the inverse of F8, and it compounds
// down the document rather than showing up as one wrong picture. Rounding up
// rather than down for the reason cells() does: an image given less room than
// it needs is cropped, and a picture missing its last row is worse than one
// with a gap under it.
//
// An image with no explicit size in its format has its natural size resolved
// from the document's own resources and written back explicitly, because the
// layout would otherwise use that natural size and undo the rounding. One
// whose size cannot be determined at all is left alone -- rounding an unknown
// would be inventing a number.
//
// Named align_text_document rather than design.md's alignTextDocument: the
// member rename pass (section 11) moved this project's own identifiers to
// snake_case, and the document still spells the pre-rename name.
//
// Returns the number of images it changed, so a caller can tell "nothing to
// do" from "nothing was done".
int align_text_document(QTextDocument *doc, QSize cell_px);

// `ground.composite_under` is what a translucent pixel is composited against:
// the terminal's own background where it answered for one, and the dark grey
// this assumed for its whole life where it did not. The assumption is wrong
// on a light terminal -- every partly-transparent edge gets a dark halo.
//
// It was a defaulted QRgb, and the default was the defect rather than a
// convenience: the sentence that stood here said a caller with no terminal to
// ask keeps the old behaviour by saying nothing, and the caller that said
// nothing was the compositor's overlay fallback, which had a terminal and had
// asked it. Nothing about an omitted argument is visible at a call site, so
// the parameter is required now, and the whole ground is passed rather than
// one colour of it -- a caller holding a TerminalGround cannot then pick the
// wrong field out of it. section 8.250.
void compose_halfblocks(CellBuffer &frame, const QImage &img, const QRect &cell_rect,
                        const TerminalGround &ground);

} // namespace Qtty
