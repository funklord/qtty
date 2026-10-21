// qtty/src/standard_icons.h -- which standard icon a placed pixmap came from.
//
// Qt rasterises a QIcon to a QPixmap before a style draws it, so a severity
// icon reaches drawItemPixmap() as pixels with no identity left. That is what
// section 0b's iconography row is blocked on: a glyph cannot travel inside a
// QIcon to a cell renderer.
//
// The mint point is NOT standardIcon(). Measured in 8.466: one QIcon caches
// its own pixmap, so asking it twice gives one identity -- but a second
// standardIcon() call for the same value re-renders to a different one, a
// different requested size gives a third, and the identity a message box
// actually places matches none of them. A registry minted at standardIcon()
// would resolve nothing.
//
// It has to be inside the icon, in a QIconEngine: that is the object QIcon
// asks once per size, mode and state and then caches, so every pixmap that
// escapes passes through it exactly once. GridStyle::standardIcon() returns
// icons built on one, and the engine notes each pixmap it returns here.
//
// WHAT THIS DOES NOT DO: choose a glyph. Whether a warning triangle should
// become one is section 0b's decision and belongs to the copyright holder;
// this is the mechanism that decision was waiting on, and nothing more. The
// channel it rides already exists -- CellImage::key IS the pixmap's
// cacheKey(), carried from the placement to the backend and asserted by
// suite_placements -- so a lookup here reaches the render path with no new
// plumbing the day a glyph is wanted.
#pragma once

#include <QtGlobal>

namespace Qtty {

// Record that `key` -- a QPixmap::cacheKey(), which is what a placement
// carries -- is a pixmap GridStyle's icon engine rendered for the
// QStyle::StandardPixmap value `which`. Taken as an int so this header does
// not drag QStyle in.
void note_standard_icon(quint64 key, int which);

// Which StandardPixmap a placed pixmap came from, or -1 for a pixmap this
// library did not mint -- an application's own icon, a grab of a widget, or
// one minted before the registry last turned over.
int standard_icon_of(quint64 key);

// How many pixmaps the registry is holding, and how many it has forgotten by
// turning over. Exposed because a cache nobody can measure is one nobody can
// say is bounded, which is the shape this project keeps meeting.
int standard_icons_noted();
int standard_icons_forgotten();

} // namespace Qtty
