// What an ADOPTING PROJECT does, compiled against the INSTALLED tree rather
// than against this one: the installed headers, the installed archive, and
// pkg-config to find both.
//
// test-install pinned the installed file SET from both sides -- every named
// file arrives, nothing unnamed does, and uninstall leaves nothing -- and
// never compiled a line against it. Those are different claims: a correct
// set of files is not a working library, and the difference is a wrong
// `Libs:`, a missing `Requires:`, a header that needs one of its siblings
// included first, or a prefix the .pc cannot be relocated to. Every one of
// those blocks every adopter and none of them moves a file.
//
// project.md 0e records that a consumer was built against the library by hand
// on 2026-09-05. That is a fact with a shelf life and nothing was keeping it
// true; this is the same act with a status nobody has to remember to read.
//
// Not shipped and in no .pro, the way tool/screen-probe.cpp is not: a binary
// only a gate uses does not belong in an install.
//
// It must FAIL LOUDLY rather than print nothing, because the gate reads both
// its status and its marker -- a probe that exits 0 having drawn nothing is
// the vacuous pass this file exists to avoid.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

int main(int argc, char **argv) {
	Qtty::prepare_environment();
	QApplication app(argc, argv);
	Qtty::setup(app);

	QWidget win;
	win.setAttribute(Qt::WA_DontShowOnScreen);
	win.resize(Qtty::GridMetrics::cells(24, 3));
	auto *rows = new QVBoxLayout(&win);
	rows->addWidget(new QLabel(QStringLiteral("INSTALLED")));
	rows->addWidget(new QPushButton(QStringLiteral("&Go")));
	win.show();
	QCoreApplication::processEvents();

	Qtty::CellBuffer buf(24, 3);
	Qtty::render_once(win, buf);
	const QString text = buf.to_text();

	// The version and the attribution come from the installed header rather
	// than from a build define: QTTY_VERSION_STRING is this build's own and
	// an adopter never sees it, which is worth pinning here because reaching
	// for it is the mistake a consumer makes first.
	printf("install-probe: qtty %s, rendered [%s]\n", Qtty::version_string,
	       QString(text).trimmed().replace(QLatin1Char('\n'),
	                                       QLatin1Char('/'))
	           .toUtf8().constData());
	if (!text.contains(QStringLiteral("INSTALLED"))) {
		fprintf(stderr, "install-probe: the label did not render\n");
		return 1;
	}
	// The button's mnemonic is drawn as the terminal shows one, which says
	// the style came through the archive rather than just the headers.
	if (!text.contains(QStringLiteral("Go"))) {
		fprintf(stderr, "install-probe: the button did not render\n");
		return 1;
	}
	return 0;
}
