// src/backend/ansi/scroll_settle.h -- design.md section 5.7's scroll-settle
// policy, kept apart from the backend so it can be driven by a test clock.
//
// Sixel and iTerm2 images have no handles: they paint into the text flow and
// moving one means RE-EMITTING it, which on a slow link is the whole frame
// budget spent on a picture that is about to move again. So while placements
// are moving they degrade to the half-block mosaic -- which is cells, and
// costs a diff like any other text -- and the real pixels are drawn once
// scrolling settles.
//
// It does NOT apply to kitty. There a placement has a handle and moving it is
// one short escape with no re-upload, so degrading would trade a cheap
// correct picture for a coarse one and buy nothing. design.md scopes the
// policy to the two tiers that pay for movement, and so does the caller.
//
// The clock is a parameter rather than a call to a timer inside, for the
// reason the capability parser takes bytes rather than a descriptor: a
// hundred-millisecond debounce tested against the real clock is a test that
// sleeps, and one that sleeps is a test that is flaky on a loaded machine.
#ifndef QTTY_SCROLL_SETTLE_H
#define QTTY_SCROLL_SETTLE_H

#include "qtty/cell.h"
#include <QHash>
#include <QRect>
#include <QVector>
#include <algorithm>

namespace Qtty {

class ScrollSettle {
public:
	explicit ScrollSettle(int debounce_ms = 100) : debounce_(debounce_ms) {}

	// True when the real pixels should be emitted this frame. Call once per
	// frame, in order: it remembers where each placement was.
	//
	// A key maps to ALL the rectangles that picture occupies, not to one.
	// This held a single QRect per key, and a frame carrying one pixmap
	// twice -- the same emoji in two places, one bullet icon per row,
	// repeated avatars -- collapsed to whichever rectangle went in last. The
	// next frame then compared the other copy against it, called that a
	// move, and did so again on every frame afterwards: settling_ was
	// refreshed for ever and sixel and iTerm2 stayed on the half-block
	// mosaic for the life of the program. Nothing had moved at any point.
	//
	// SORTED, so that the comparison is of the set of places the picture
	// occupies rather than of the order the painter happened to emit them
	// in. Two identical pictures swapping position in the list is not
	// something a viewer can see, and calling it a scroll would put the
	// latch back by a narrower route.
	bool update(const QVector<CellImage> &images, qint64 now_ms) {
		bool moved = false;
		QHash<quint64, QVector<QRect>> now;
		now.reserve(images.size());
		for (const CellImage &ci : images) now[ci.key].append(ci.cell_rect);
		for (auto it = now.begin(); it != now.end(); ++it) {
			std::sort(it->begin(), it->end(),
			          [](const QRect &a, const QRect &b) {
				          if (a.y() != b.y()) return a.y() < b.y();
				          if (a.x() != b.x()) return a.x() < b.x();
				          if (a.width() != b.width())
					          return a.width() < b.width();
				          return a.height() < b.height();
			          });
			const auto was = last_.constFind(it.key());
			// Only a placement that MOVED counts. One that appeared or
			// vanished is a picture arriving or leaving, not a scroll, and
			// treating it as one would degrade the first frame of every image
			// to a mosaic -- the case where the pixels are most wanted.
			if (was != last_.constEnd() && *was != *it) moved = true;
		}
		last_ = now;
		if (moved) {
			moved_at_ = now_ms;
			settling_ = true;
			return false;
		}
		if (!settling_) return true;              // nothing has ever moved
		if (now_ms - moved_at_ >= debounce_) {
			settling_ = false;
			return true;
		}
		return false;
	}

	int debounce_ms() const { return debounce_; }

private:
	QHash<quint64, QVector<QRect>> last_;
	qint64 moved_at_ = 0;
	bool settling_ = false;
	int debounce_;
};

} // namespace Qtty

#endif // QTTY_SCROLL_SETTLE_H
