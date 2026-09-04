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
	fprintf(stderr, "screen-probe: grid %dx%d, marker at cell 22,5;"
	                " tile origin x=180, so 198 if sized in the terminal's"
	                " cells and 200 if in qtty's\n", cw, ch);
	fflush(stderr);
	Qtty::AnsiBackend backend;
	backend.present_pixels(px, QRegion());
	::fflush(stdout);
	::sleep(4);                       // the capture happens in here
	backend.suspend();
	return 0;
}
