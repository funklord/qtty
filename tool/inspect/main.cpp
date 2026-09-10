// qtty-inspect -- dump a widget tree with cell geometry beside its rendering (section 9).
// Currently inspects a built-in sample; Phase 2 adds loading .ui files.
#include <qtty/qtty.h>
#include <qtty/version.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

// What the file's own header says, reachable from the program. Every one of
// these tools documented itself thoroughly in a comment nobody running it
// could see, and answered --help by doing its ordinary work -- which reads as
// if the flag had been understood.
static const char *const usage =
    "qtty-inspect -- dump a widget tree with cell geometry beside its"
    " rendering.\n"
    "\n"
    "usage: qtty-inspect [--attrs] [--help] [--version]\n"
    "\n"
    "Prints every widget's position and size in cells, says whether each\n"
    "lands on the character grid, and then the frame they compose to. It\n"
    "inspects a built-in sample dialog; loading a .ui file is Phase 2.\n"
    "\n"
    "  --attrs            show the rendering with attributes and colours\n"
    "                     instead of glyphs alone, which is the only way to\n"
    "                     see focus: a list's current item is underlined and\n"
    "                     a focused button reversed, and neither is a glyph\n";

int main(int argc, char **argv) {
	// Before QApplication, so both answer in a pipe and on a machine with no
	// terminal, which is where somebody reads --help.
	for (int i = 1; i < argc; ++i) {
		if (!qstrcmp(argv[i], "--help") || !qstrcmp(argv[i], "-h")) {
			printf("%s", usage);
			return 0;
		}
		if (!qstrcmp(argv[i], "--version") || !qstrcmp(argv[i], "-V")) {
			printf("qtty-inspect %s\n%s\n", Qtty::version_string,
			       Qtty::copyright);
			return 0;
		}
	}
	bool attrs = false;
	for (int i = 1; i < argc; ++i)
		if (!qstrcmp(argv[i], "--attrs")) attrs = true;
	Qtty::prepare_environment();
	QApplication app(argc, argv);
	// An unrecognised option was IGNORED, and the tool then did its
	// ordinary work and exited 0 -- so `--probs` for `--probes` reads as
	// "the probes are off" rather than as a typo, in a program whose whole
	// job is telling somebody what is really happening.
	//
	// AFTER QApplication, deliberately: Qt removes the arguments it
	// recognises (-platform, -style and the rest) from argv, so what is
	// left is this program's to judge. Doing it before would refuse
	// perfectly good Qt options.
	for (int i = 1; i < argc; ++i) {
		static const char *const known[] = { "--help", "-h", "--version", "-V", "--attrs" };
		if (argv[i][0] != '-') continue;
		bool ok = false;
		for (const char *k : known) ok = ok || !qstrcmp(argv[i], k);
		if (ok) continue;
		fprintf(stderr, "qtty-inspect: unknown option '%s'\n"
		        "try 'qtty-inspect --help'\n", argv[i]);
		return 2;
	}
	Qtty::setup(app);
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	QDialog dlg;
	auto *v = new QVBoxLayout(&dlg);
	auto *chk = new QCheckBox("Enable telemetry", &dlg);
	chk->setChecked(true);
	v->addWidget(chk);
	auto *edit = new QLineEdit("status: connected", &dlg);
	v->addWidget(edit);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	v->addWidget(bb);
	dlg.setAttribute(Qt::WA_DontShowOnScreen);
	dlg.resize(GridMetrics::cells(48, 10));
	dlg.show();
	QCoreApplication::processEvents();

	printf("widget tree (cell geometry, CW=%d CH=%d):\n", cw, ch);
	for (QWidget *c : dlg.findChildren<QWidget *>()) {
		if (c->geometry().isNull() || !c->isVisible()) continue;
		QRect g = c->geometry();
		printf("  %-24s cells %3d,%2d %3dx%-2d  %s\n",
		       c->metaObject()->className(),
		       g.x() / cw, g.y() / ch, g.width() / cw, g.height() / ch,
		       GridMetrics::is_aligned(g) ? "aligned" : "MISALIGNED");
	}
	Qtty::CellBuffer buf(52, 12);
	Qtty::render_once(dlg, buf);
	// GLYPHS by default and ATTRIBUTES on request. The guide tells an
	// implementer that a list's current item is underlined and a focused
	// button is reversed -- and this tool printed neither, so somebody
	// debugging "why does my focus not show" could not see the answer in
	// the one program built to show them their own dialog. qtty-replay
	// already has `snapshot` for the same reason; the buffer has carried
	// to_snapshot() all along.
	if (attrs)
		printf("\nrendering (attributes shown):\n%s",
		       qPrintable(buf.to_snapshot()));
	else
		printf("\nrendering:\n%s", qPrintable(buf.to_text()));
	return 0;
}
