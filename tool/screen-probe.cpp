// Drives AnsiBackend's PIXEL path inside a real terminal, so that a capture
// can check what was drawn rather than what qtty emitted. Every tier is
// verified by its bytes -- sizes, addresses, placement ids -- and none by a
// picture, which is the gap tool/screen-check exists to close.
//
// Not shipped and not in any .pro: tool/screen-check compiles it when the
// library is built, because a binary that only a screen test uses does not
// belong in the install.
//
// It draws a green rectangle at a known place, presents it, and waits long
// enough for the capture, then gives the terminal back.
#include <qtty/qtty.h>
#include "src/backend/ansi/ansi_backend.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <unistd.h>
#include <cstdio>

int main(int argc, char **argv) {
	Qtty::prepare_environment();
	QApplication app(argc, argv);
	Qtty::setup(app);
	const int cw = Qtty::GridMetrics::cw(), ch = Qtty::GridMetrics::ch();
	const int cols = 40, rows = 12;

	QImage px(cols * cw, rows * ch, QImage::Format_ARGB32_Premultiplied);
	px.fill(QColor(0, 0, 0));
	{
		// Four cells at (2,1), which is one whole tile at the tile size the
		// kitty path uses, so a tiled present and a whole one put the same
		// pixels in the same place and the check does not depend on which
		// ran.
		QPainter p(&px);
		p.fillRect(22 * cw, 5 * ch, 4 * cw, 4 * ch, QColor(40, 200, 90));
	}
	// The expected count, said out loud, because the checker cannot know the
	// grid metrics: they come from the font this build resolved.
	// Cell 22 is two cells INTO the tile that starts at 20, so the marker's
	// left edge is the tile origin plus an offset that differs between the
	// two sizings -- 2*9 against 2*10. On a tile boundary both agree and the
	// measurement cannot tell them apart, which is how the first version of
	// this probe failed to discriminate.
	Qtty::AnsiBackend backend;
	// The prediction, computed HERE because this is the only place that knows
	// both cell sizes: qtty's from the font and the terminal's from CSI 16t. A
	// checker outside cannot derive it, and duplicating the arithmetic there
	// would be the parallel copy this tree keeps meeting.
	//
	// Cell 22 is two cells INTO the tile starting at 20, and that offset is
	// the whole discriminator: on a tile boundary the two sizings agree,
	// because the left edge is then the cursor address and that is in cells.
	const QSize probed = backend.capabilities().cell_px;
	const QSize term = probed.isValid() && probed.width() > 0
	                 ? probed : QSize(cw, ch);
	fprintf(stderr, "screen-probe: qtty %dx%d terminal %dx%d"
	                " predict left=%d width=%d\n",
	        cw, ch, term.width(), term.height(),
	        22 * term.width(), 4 * term.width());
	fflush(stderr);
	// Two paths, because they are two different pieces of code and only one
	// of them has a pixel frame at all. present_pixels() serves the tiers
	// that transmit an image -- sixel, iTerm2, kitty -- and returns without
	// drawing on the others, which is correct rather than a gap: halfblocks
	// composites a PLACEMENT into cells inside present(), so the tier a
	// terminal with no graphics protocol falls back to is reached only
	// through the cell path. Asking present_pixels() for it draws nothing
	// and looks exactly like a broken renderer.
	if (qgetenv("QTTY_SCREEN_PATH") == "overlay") {
		// The KittyAlpha overlay path, which is a different function from
		// the pixel one and had never had a picture taken of it. The base
		// frame is black so that the only colour on the screen is the
		// overlay, and the marker's geometry is therefore the overlay's.
		QImage base(cols * cw, rows * ch, QImage::Format_ARGB32_Premultiplied);
		base.fill(QColor(0, 0, 0));
		{
			// A control in the same capture: if this blue appears and the
			// overlay's green does not, the pixel path drew and the overlay
			// path did not. Without it a blank screen cannot tell a broken
			// overlay from a probe that never ran.
			QPainter bp(&base);
			bp.fillRect(4 * cw, 5 * ch, 4 * cw, 4 * ch, QColor(60, 90, 220));
		}
		backend.present_pixels(base, QRegion());
		backend.present_overlay(1, px.copy(22 * cw, 5 * ch, 4 * cw, 4 * ch),
		                        QPoint(22, 5), 1);
	} else if (qgetenv("QTTY_SCREEN_PATH") == "cells") {
		Qtty::CellBuffer frame(cols, rows);
		Qtty::CellImage place;
		QPixmap pm = QPixmap::fromImage(
		    px.copy(22 * cw, 5 * ch, 4 * cw, 4 * ch));
		place.key = quint64(pm.cacheKey());
		place.cell_rect = QRect(22, 5, 4, 4);
		place.pixmap = pm;
		frame.images.append(place);
		backend.present(frame, QRegion());
	} else {
		backend.present_pixels(px, QRegion());
	}
	::fflush(stdout);
	::sleep(4);                       // the capture happens in here
	backend.suspend();
	return 0;
}
