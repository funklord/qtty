// main.cpp -- frontend selection: the packaging best-practices half of the example.
//
// Three ways to ship a Qtty application, all demonstrated by this one file
// plus chat.pro next to it. (It said "the CMakeLists next to it"; CMake was
// removed in 73fdee6 and this tree builds with qmake.)
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
#include <functional>        // the refit the status line is reinstalled by
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
	if (tui) {
		// PRACTICE 8 of doc/keyboard-first.md -- say what the keys are --
		// demonstrated where such a thing BELONGS: in the frontend. The
		// window offers a status line and does not know what a terminal
		// is; this branch is the only code that does.
		//
		// The list is ASKED FOR rather than written out. An application
		// that typed "Menu context menu" into a label would keep a
		// second copy of a fact the library owns, and the copy is wrong
		// the day the binding moves -- which it did, twice, in one day.
		//
		// The conventions are deliberately NOT turned on here. The bar
		// then holds one entry and fits a 52-column window; asking for
		// them adds five more, and an application that does should give
		// the bar the rows to show them.
		//
		// AND THE KEY A TERMINAL USER REACHES FOR when something else
		// has written to the screen. THE CONVENTIONS BIND Ctrl+L AS OF
		// 8.493 and this branch does not turn them on, so the library
		// binds nothing here and the application's own shortcut is what
		// answers -- which is also what the guide tells an application
		// that leaves the bundle off to do.
		//
		// It was written when qtty bound the key nowhere at all, and the
		// comment said so. That claim went stale the day the convention
		// landed, two commits before this one: an application turning
		// the conventions on would now have two bindings for one key,
		// its own winning because match_shortcut() runs first, and
		// conventions_shadowed() reporting the row as taken. Left as it
		// is because the bundle is off here, and said rather than left
		// for the next reader to find.
		//
		// Qtty::redraw() is a no-op when nothing is driving a screen, so
		// the same connection would be safe in the GUI branch too; it
		// lives here because the status line does, and both are things
		// only the frontend knows about.
		//
		// NAMED, because shortcut_help() labels a QShortcut by its
		// objectName and an unnamed one describes itself rather than
		// what it does -- which would then be what the status line said.
		auto *again = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")),
		                            &win);
		again->setObjectName(QStringLiteral("redraw"));
		QObject::connect(again, &QShortcut::activated,
		                 [] { Qtty::redraw(); });

		QStringList hints;
		for (const auto &row : Qtty::keyboard_conventions_help())
			hints << row.first + QStringLiteral(" ") + row.second;
		// THE OTHER HALF of the same practice, and the same argument: an
		// application's own keys move when somebody edits them, so they
		// are asked for rather than typed in beside the library's.
		for (const auto &row : Qtty::shortcut_help(&win))
			hints << row.first + QStringLiteral(" ") + row.second;
		// AS MANY WHOLE HINTS AS FIT, REFITTED ON EVERY RESIZE.
		//
		// Joining them and handing the line over fits a wide terminal and
		// is CUT on a narrow one -- measured 2026-10-22 by rendering this
		// program at four pty sizes, "Menu/Shift+F10 context m" at
		// twenty-four columns, a key hint cut mid-word. A QLabel does not
		// elide, so nothing below here was going to, and half a chord is
		// not a shorter chord: the unit that yields is a whole hint. The
		// library's own rule, for a tab and for a menu item, is that "a
		// name cut without an ellipsis reads as a different class".
		//
		// THE TERMINAL'S WIDTH AND NOT THE WINDOW'S, which the first
		// attempt at this got backwards. The window is deliberately
		// LARGER than the terminal when the terminal is small: section
		// 7's policy drops what is optional and then SCROLLS rather than
		// clipping, so the window keeps its minimum and the terminal is a
		// view onto it. Measured 2026-10-22 at five pty sizes, the window
		// stopping at 42 cells while the terminal went to 16:
		//
		//     terminal 80  window 80      fits, and expands to fill
		//     terminal 40  window 42
		//     terminal 24  window 42      a view onto 42 cells
		//     terminal 16  window 42
		//
		// Fitting to the window therefore fits nothing: at 40 columns it
		// measured 42 and kept both hints, which is the output that
		// started this.
		//
		// AND WHAT THAT CANNOT DO, said rather than left implied: which
		// 24 of those 42 cells are on screen is the scroll's business and
		// follows the FOCUS, so an application cannot know where its
		// status row is being viewed from. Fitting to the terminal's
		// width keeps the hints in the leftmost cells, which is where the
		// view sits while the focused widget starts at the left edge --
		// the ordinary case, and the best an application can do from
		// outside.
		//
		// AN EVENT FILTER FROM THE FRONTEND, rather than anything in
		// chat.h. That window is a plain Qt window that does not know
		// what a terminal is -- which is the demonstration, not an
		// accident -- so the terminal-shaped decision stays in the only
		// code that knows there is one. An application with its own
		// window class would put this in a resizeEvent; one that wants
		// its window to stay portable does what this does.
		//
		// The filter is for a terminal RESIZED DURING THE RUN, which is
		// the only thing that moves the window afterwards: measured, a
		// run at a fixed size calls this once and never again.
		//
		// IN CELLS, for the reason project.md 8.483 to 8.487 record: a
		// QString's size counts code units, and a hint carrying a wide
		// character is twice that on screen. Qtty::text_cells() is what
		// the library measures its own text with, and this file summed
		// the clusters itself until 8.500 made it public -- the one
		// place in this tree that had to, and the evidence that an
		// application needs it.
		const auto fit = [&win, hints] {
			// Already answered here: setup() reads the terminal before
			// this branch runs, so this is the real width and not a
			// zero waiting for exec().
			const int room = Qtty::terminal_cells().width();
			QStringList fits;
			int used = 0;
			for (const QString &hint : hints) {
				const int cost = Qtty::text_cells(hint)
				               + (fits.isEmpty() ? 0 : 2);
				if (room > 0 && used + cost > room) break;
				fits << hint;
				used += cost;
			}
			// Never nothing: a terminal too narrow for the first hint
			// still gets it, cut by the label as before, because a
			// status line saying nothing is worse than one saying too
			// much.
			if (fits.isEmpty() && !hints.isEmpty())
				fits = hints.mid(0, 1);
			win.setStatus(fits.join(QStringLiteral("  ")));
		};
		fit();
		struct Refit : QObject {
			std::function<void()> again;
			bool eventFilter(QObject *o, QEvent *e) override {
				if (e->type() == QEvent::Resize) again();
				return QObject::eventFilter(o, e);
			}
		};
		auto *refit = new Refit;
		refit->again = fit;
		refit->setParent(&win);
		win.installEventFilter(refit);
		return Qtty::exec(app, win);
	}
#endif
	win.show();
	return app.exec();
}
