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
#include <qtty/version.h>
#include "src/backend/ansi/ansi_backend.h"

#include <QApplication>
#include <QtGlobal>
#include <cstdio>
#include <cstdlib>

using namespace Qtty;

int main(int argc, char **argv) {
	// Before anything else, and before QApplication: --version must work in a
	// pipe, on a machine with no terminal to negotiate with, and without the
	// startup query this tool otherwise sends.
	for (int i = 1; i < argc; ++i)
		if (!qstrcmp(argv[i], "--version") || !qstrcmp(argv[i], "-V")) {
			printf("qtty-negotiate %s\n%s\n", version_string, copyright);
			return 0;
		}
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
	// The palette is not on Capabilities: it is consumed by the colour model
	// rather than reported to an application, so it is read from there.
	const QVector<QRgb> pal = terminal_palette();
	fprintf(out, "palette              %s\n",
	        pal.isEmpty() ? "not reported (xterm table assumed)"
	                      : qPrintable(QStringLiteral("%1 low entries, 0=%2 1=%3")
	                            .arg(pal.size())
	                            .arg(QColor(pal[0]).name(), QColor(pal[1]).name())));
	fprintf(out, "mouse                %s\n", c.mouse ? "yes" : "no");
	fprintf(out, "bracketed paste      %s\n", c.bracketed_paste ? "yes" : "no");
	if (out != stdout) fclose(out);
	return 0;
}
