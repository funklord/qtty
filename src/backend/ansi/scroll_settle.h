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

namespace Qtty {

class ScrollSettle {
public:
	explicit ScrollSettle(int debounce_ms = 100) : debounce_(debounce_ms) {}

	// True when the real pixels should be emitted this frame. Call once per
	// frame, in order: it remembers where each placement was.
	bool update(const QVector<CellImage> &images, qint64 now_ms) {
		bool moved = false;
		QHash<quint64, QRect> now;
		now.reserve(images.size());
		for (const CellImage &ci : images) {
			now.insert(ci.key, ci.cell_rect);
			const auto it = last_.constFind(ci.key);
			// Only a placement that MOVED counts. One that appeared or
			// vanished is a picture arriving or leaving, not a scroll, and
			// treating it as one would degrade the first frame of every image
			// to a mosaic -- the case where the pixels are most wanted.
			if (it != last_.constEnd() && *it != ci.cell_rect) moved = true;
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
	QHash<quint64, QRect> last_;
	qint64 moved_at_ = 0;
	bool settling_ = false;
	int debounce_;
};

} // namespace Qtty

#endif // QTTY_SCROLL_SETTLE_H
