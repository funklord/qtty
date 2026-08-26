// suite_runtime -- L6 (section 5.6): the backend seam, the font provisioning
// check (section 5.3, risk R3), and GridGuard (section 5.3, section 9).
#include <qtty/qtty.h>
#include "src/backend/null/null_backend.h"
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_runtime() {
	fails = 0;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	// ---------------------------------------------- the backend seam (5.1/5.6)
	//
	// Before exec() took a backend, it constructed an AnsiBackend on its own
	// stack: the runtime could only ever be driven by the built-in terminal
	// backend, so NullBackend was compiled into the library and reachable from
	// nothing. This is the test that the seam is real.
	{
		NullBackend backend(QSize(40, 12));
		QWidget win;
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		auto *label = new QLabel(QStringLiteral("seam"), &win);
		v->addWidget(label);
		v->addStretch();

		win.move(7 * cw, 3 * ch);              // exec() must put this back
		CHECK(backend.sink() == nullptr, "backend has no sink before exec()");

		// A repeating timer, not singleShot(0). exec() calls processEvents()
		// after show() and before app.exec(), so a zero-timer fires while
		// there is still no event loop to quit -- quit() is a no-op then, and
		// the test hangs forever in app.exec(). A timer that keeps firing
		// quits whenever the loop actually starts, which is the property
		// wanted rather than a guess at how long setup takes.
		QTimer quitter;
		quitter.setInterval(10);
		QObject::connect(&quitter, &QTimer::timeout, qApp, &QCoreApplication::quit);
		quitter.start();
		const int rc = exec(*qApp, win, backend);
		quitter.stop();

		CHECK(rc == 0, "exec() on an injected backend returns cleanly");
		CHECK(backend.frameCount() > 0, "the injected backend received a frame");
		CHECK(backend.sink() != nullptr, "exec() wired the router into the backend");
		CHECK(backend.lastFrame().contains(QStringLiteral("seam")),
		      "the frame the backend received is the widget tree");
		CHECK(win.size() == QSize(40 * cw, 12 * ch),
		      "the window was sized from the backend's cell size");
		// Deliberately moved off the origin before exec(), because that is the
		// only way this check can fail: the compositor draws the primary
		// window at QPoint() and the router maps clicks through
		// mapFromGlobal(), so the two agree at (0,0) whether or not anything
		// enforces it. A probe that starts there cannot tell the difference.
		CHECK(win.pos() == QPoint(0, 0),
		      "exec() puts the primary window at the origin, which the "
		      "compositor and the router both assume");
		CHECK(!isTuiActive(), "exec() clears the TUI flag on the way out");
	}

	// --------------------------------------- font provisioning (5.3, risk R3)
	{
		QFont mono(QStringLiteral("DejaVu Sans Mono"));
		mono.setPixelSize(16);
		CHECK(grid_font_problem(mono).isEmpty(),
		      "the grid font passes the integral-metrics check");

		// The check this replaced compared the advance of 'i' with that of
		// 'M', which a proportional font fails on the same pair -- but it was
		// an assert, so it did nothing in a release build. This one is a
		// value, and is read in both.
		QFont prop(QStringLiteral("DejaVu Sans"));
		prop.setPixelSize(16);
		if (QFontInfo(prop).fixedPitch()) {
			printf("SKIP: no proportional font resolved, cannot test rejection\n");
		} else {
			CHECK(!grid_font_problem(prop).isEmpty(),
			      "a proportional font is rejected");
			CHECK(grid_font_problem(prop).contains(QStringLiteral("fixed pitch")),
			      "the rejection says what was wrong with it");
		}
	}

	// ------------------------------------------------------ GridGuard (5.3/9)
	{
		GridGuard::install(*qApp);

		// The widgets have to be shown, and that is not a detail. QWidget::resize()
		// on a hidden widget sets the geometry and defers the QResizeEvent until
		// the widget is shown, so a guard driven by resize events sees nothing
		// from one that never appears. The first version of this test resized two
		// hidden widgets: the misaligned one was not caught, and the aligned one
		// "raised nothing" because nothing was ever delivered to raise. One half
		// failed honestly and the other passed vacuously, off the same mistake.
		QWidget aligned;
		aligned.setAttribute(Qt::WA_DontShowOnScreen);
		aligned.show();
		QCoreApplication::processEvents();
		GridGuard::reset();                       // ignore whatever show() sized it to
		CHECK(GridGuard::violations() == 0, "guard starts clean");

		aligned.resize(GridMetrics::cells(10, 4));
		aligned.move(cw * 2, ch * 3);
		QCoreApplication::processEvents();
		CHECK(GridGuard::violations() == 0,
		      "a cell-multiple geometry raises nothing");

		QWidget off;
		off.setAttribute(Qt::WA_DontShowOnScreen);
		off.show();
		QCoreApplication::processEvents();
		GridGuard::reset();
		off.resize(10 * cw + 1, 4 * ch);
		QCoreApplication::processEvents();
		CHECK(GridGuard::violations() > 0,
		      "a width one pixel off the grid is caught");

		// F5: QHeaderView and QScrollBar self-size and land off the grid however
		// the style is written. They are exempt by name so that the guard does not
		// fire on every item view -- which is how a guard becomes noise and then
		// gets switched off.
		QScrollBar bar;
		CHECK(GridGuard::is_exempt(&bar), "QScrollBar is exempt (F5)");
		QTreeWidget tree;
		CHECK(GridGuard::is_exempt(tree.header()), "QHeaderView is exempt (F5)");
		CHECK(!GridGuard::is_exempt(&off), "a plain widget is not exempt");

		GridGuard::reset();
		CHECK(GridGuard::violations() == 0, "reset clears the count");
	}

	return fails;
}
