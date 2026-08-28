// qtty-negotiate -- what qtty makes of the terminal it is run inside.
//
// Run it in a terminal and it prints the negotiated capabilities. That is
// worth a tool rather than a debugging printf because the negotiation is the
// one part of qtty whose answer depends on somebody else's program: a suite
// can only check it against replies the suite itself wrote, which is one
// witness. Pointed at a real terminal emulator it is two.
//
// Writes to a FILE rather than to stdout when QTTY_NEGOTIATE_OUT is set,
// because stdout is the terminal under test -- and under a terminal launched
// with `-e` there may be nowhere for the output to be read from afterwards.
#include <qtty/qtty.h>
#include "src/backend/ansi/ansi_backend.h"

#include <QApplication>
#include <cstdio>
#include <cstdlib>

using namespace Qtty;

int main(int argc, char **argv) {
	prepare_environment();
	QApplication app(argc, argv);
	setup(app);

	Capabilities c;
	{
		AnsiBackend backend;                      // asks on construction
		c = backend.capabilities();
	}

	static const char *graphics[] = {"NoGraphics", "Halfblocks", "Sixel",
		                                 "ITerm2", "Kitty", "KittyAlpha"};
	static const char *depth[] = {"Mono", "Ansi16", "Xterm256", "TrueColor"};

	const char *path = getenv("QTTY_NEGOTIATE_OUT");
	FILE *out = path ? fopen(path, "w") : stdout;
	if (!out) return 1;
	fprintf(out, "graphics             %s\n", graphics[c.graphics]);
	fprintf(out, "colour               %s\n", depth[c.color]);
	fprintf(out, "unicode placements   %s\n", c.unicode_placements ? "yes" : "no");
	fprintf(out, "cell size            %s\n",
	        c.cell_px.isValid()
	            ? qPrintable(QStringLiteral("%1x%2").arg(c.cell_px.width())
	                             .arg(c.cell_px.height()))
	            : "not reported");
	fprintf(out, "background           %s\n",
	        c.background_known ? qPrintable(c.background.name()) : "not reported");
	fprintf(out, "mouse                %s\n", c.mouse ? "yes" : "no");
	fprintf(out, "bracketed paste      %s\n", c.bracketed_paste ? "yes" : "no");
	if (out != stdout) fclose(out);
	return 0;
}
