// src/runtime/placement_paint.h -- painting a frame's cell-anchored
// placements, z-ordered (design.md section 5.7).
//
// INTERNAL. Not shipped in include/qtty/, because an application needs
// nothing from it: it puts a frame's placements onto the picture the software
// composite tier hands to the terminal, and the frame loop is the only caller
// there will ever be.
//
// A FUNCTION RATHER THAN SIX LINES INSIDE render_now(). It was a two-line
// loop inside render_now() first, and nothing could reach it: the frame it
// paints comes from Compositor::compose(), which builds placements out of the
// widget tree, and no test can put a placement of its OWN choosing into that
// frame. `z` is the field that makes this bite -- two pictures at different
// depths is the whole question, and a check driving the real frame loop can
// only ever produce placements that agree about depth, because nothing in the
// tree sets z yet. So the assertion that matters -- these pixels belong to
// the higher one, not to the later one -- was unwritable, and the ordering
// could have been reversed, absent or unstable with every existing check
// still green.
//
// That is this project's most expensive recurring shape, and the remedy is
// the one title_keeper.h already records for it: a seam rather than a
// cleverer test. FrameScheduler::pixel_damage() is static and public for the
// same reason, and says so -- the union is the part that can be wrong and the
// wiring is one call. Here the ORDER is the part that can be wrong; the
// wiring is one call, and the software tier's existing checks fail if it goes
// away, because they assert that a placement reaches the composed picture at
// all.
#pragma once
#include <QVector>
#include "qtty/cell.h"

class QPainter;

namespace Qtty {

// Paint `images` onto `p` at the given cell size, in z order.
//
// The painter's clip, its device and its transform are the caller's business
// and are not touched: the software tier clips to the damaged rectangle
// before calling this, and a function that reset the clip would silently cost
// the whole screen per frame.
void paint_placements(QPainter &p, const QVector<CellImage> &images,
                      int cw, int ch);

} // namespace Qtty
