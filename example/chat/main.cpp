// main.cpp -- frontend selection: the packaging best-practices half of the example.
//
// Three ways to ship a Qtty application, all demonstrated by this one file
// plus the CMakeLists next to it:
//
//   1. ONE DUAL BINARY (recommended).  `chat` runs the GUI under a desktop
//      session and the TUI on a bare terminal, decided at runtime. GUI-first
//      apps already link QtWidgets, so the TUI adds no dependencies -- there
//      is usually no reason to ship two binaries.
//
//   2. EXPLICIT FLAGS / NAMES.  `chat --tui`, `chat --gui` override the
//      autodetection; installing a `chat-tui` symlink gives users a
//      terminal-only command name (argv[0] is honoured).
//
//   3. COMPILE-TIME VARIANTS.  -DQTTY_NO_TUI / -DQTTY_NO_GUI strip one
//      frontend for policy reasons (a kiosk build, a server package).
//      Dependencies are identical either way; this trims code paths, not
//      libraries.
//
// Order matters and is the one sharp edge:
//      decide frontend  ->  Qtty::prepare_environment()   (sets QT_QPA_PLATFORM)
//                       ->  QApplication ctor
//                       ->  Qtty::setup(app)             (font + style)
//                       ->  construct shared widgets     (they read app font)
//                       ->  Qtty::exec / app.exec
// prepare_environment() must precede QApplication, and setup() must precede
// widget construction so the shared UI derives its metrics from the final font.

#include <QtWidgets>
#include <unistd.h>          // isatty, for frontend autodetection
#include "chat.h"

#ifndef QTTY_NO_TUI
#  include <qtty/qtty.h>
#endif

static bool wantTui(int argc, char **argv) {
#if defined(QTTY_NO_TUI)
	(void)argc; (void)argv; return false;
#elif defined(QTTY_NO_GUI)
	(void)argc; (void)argv; return true;
#else
	for (int i = 1; i < argc; ++i) {                     // 2a: explicit flag wins
		if (!qstrcmp(argv[i], "--tui")) return true;
		if (!qstrcmp(argv[i], "--gui")) return false;
	}
	const QString name = QFileInfo(argv[0]).fileName();  // 2b: invoked as chat-tui?
	if (name.endsWith(QStringLiteral("-tui"))) return true;
	if (name.endsWith(QStringLiteral("-gui"))) return false;
	// 1: autodetect -- no display session and stdout is a terminal -> TUI
	const bool haveDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
	                      || qEnvironmentVariableIsSet("DISPLAY");
	return !haveDisplay && isatty(1);
#endif
}

int main(int argc, char **argv) {
	const bool tui = wantTui(argc, argv);

#ifndef QTTY_NO_TUI
	if (tui) Qtty::prepare_environment();       // BEFORE QApplication
#endif
	QApplication app(argc, argv);
#ifndef QTTY_NO_TUI
	if (tui) Qtty::setup(app);                 // BEFORE any widget
#endif

	ChatWindow win;                            // identical in both frontends

	if (qEnvironmentVariableIsSet("QTTY_SMOKE"))          // CI hook: exit clean
		QTimer::singleShot(300, &app, &QCoreApplication::quit);

#ifndef QTTY_NO_TUI
	if (tui) return Qtty::exec(app, win);
#endif
	win.show();
	return app.exec();
}
