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
		CHECK(backend.frame_count() > 0, "the injected backend received a frame");
		CHECK(backend.sink() != nullptr, "exec() wired the router into the backend");
		CHECK(backend.last_frame().contains(QStringLiteral("seam")),
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
		CHECK(!is_tui_active(), "exec() clears the TUI flag on the way out");
	}

	// --------------------------------------- font provisioning (5.3, risk R3)
	{
		QFont mono(QStringLiteral("DejaVu Sans Mono"));
		mono.setPixelSize(16);
		// The ambient scaling levers are pinned by prepare_environment(), and
		// this is what notices if that stops happening. It discriminates under
		// a clean environment precisely because nothing else sets these: an
		// empty value means the pin is gone, not that the machine is tidy.
		//
		// It is not cosmetic. With QT_SCALE_FACTOR=2 the line height came out
		// 18.6406 px and setup() refused to start -- the font guard working
		// correctly on a cause that was the environment, so every qtty program
		// simply would not run on a HiDPI desktop that sets it.
		CHECK(qgetenv("QT_SCALE_FACTOR") == QByteArray("1"),
		      "prepare_environment() pins QT_SCALE_FACTOR");
		CHECK(qgetenv("QT_SCREEN_SCALE_FACTORS").isEmpty(),
		      "and QT_SCREEN_SCALE_FACTORS");

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

	// ------------------------------------------------- section 7.8: GridSnap
	// The guard's other half. Its policy is a proof rather than a preference,
	// so the proof is asserted directly on rectangles before any widget is
	// involved -- a widget test can only sample, and the property is universal.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const QRect samples[] = {
			QRect(0, 0, 45, ch), QRect(46, 0, 45, ch), QRect(3, 7, 3, 3),
			QRect(10, 19, 88, 19), QRect(-4, 0, 27, 40), QRect(0, 0, 0, 0),
		};
		bool idempotent = true;
		for (const QRect &r : samples)
			idempotent = idempotent && GridSnap::snap(GridSnap::snap(r)) == GridSnap::snap(r);
		// Not decoration: idempotence is what makes the filter terminate. It
		// sets geometry only when the snapped rect differs, so a snap that
		// moved twice would set geometry from inside its own resize for ever.
		CHECK(idempotent, "snapping a snapped rectangle changes nothing");

		bool disjoint_stays = true;
		for (const QRect &a : samples)
			for (const QRect &b : samples)
				if (!a.intersects(b) && GridSnap::snap(a).intersects(GridSnap::snap(b)))
					disjoint_stays = false;
		CHECK(disjoint_stays, "snapping never overlaps two disjoint rectangles");

		// The control for the check above, and the reason the policy is
		// round-to-nearest: the rejected policy fails this exact pair. A 1px
		// gap survives rounding and does not survive floor-origin/ceil-size,
		// so a run where the check above passes vacuously is one where these
		// two rectangles were never disjoint to begin with.
		CHECK(!QRect(0, 0, 45, ch).intersects(QRect(46, 0, 45, ch)),
		      "the 1px-gap pair really is disjoint before snapping");
	}
	{
		// Inert unless installed (section 10.1's rule, which this has to obey
		// as much as the paint filter does).
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *l = new QHBoxLayout(&h);
		for (int i = 0; i < 4; ++i)
			l->addWidget(new QPushButton(QStringLiteral("b%1").arg(i)));
		h.resize(GridMetrics::cells(40, 12));
		h.show();
		QCoreApplication::processEvents();
		l->activate();
		QCoreApplication::processEvents();
		int off = 0;
		for (QObject *o : h.children())
			if (auto *w = qobject_cast<QWidget *>(o))
				if (!GridMetrics::is_aligned(w->geometry())) ++off;
		CHECK(!GridSnap::installed() && off > 0,
		      "without GridSnap a box layout leaves its children off the grid");
	}
	{
		GridSnap::install(*qApp);
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *l = new QHBoxLayout(&h);
		for (int i = 0; i < 4; ++i)
			l->addWidget(new QPushButton(QStringLiteral("b%1").arg(i)));
		h.resize(GridMetrics::cells(40, 12));
		h.show();
		QCoreApplication::processEvents();
		l->activate();
		QCoreApplication::processEvents();
		int off = 0, overlaps = 0;
		QVector<QRect> rs;
		for (QObject *o : h.children())
			if (auto *w = qobject_cast<QWidget *>(o)) {
				rs.append(w->geometry());
				if (!GridMetrics::is_aligned(w->geometry())) ++off;
			}
		for (int i = 0; i < rs.size(); ++i)
			for (int j = i + 1; j < rs.size(); ++j)
				if (rs[i].intersects(rs[j])) ++overlaps;
		CHECK(off == 0, "with GridSnap the same layout lands on the grid");
		CHECK(overlaps == 0, "and no two children overlap");

		// The boundary, asserted rather than left to be discovered: Qt clamps
		// setGeometry to a widget's size constraints, so a fixed size that is
		// not a cell multiple cannot be snapped and the guard goes on
		// reporting it. That is the right division of labour -- it is the
		// application's number -- but only if it is written down. The pair
		// discriminates: 23 resists, 20 does not, so the check is about the
		// constraint and not about fixed widths in general.
		auto *stubborn = new QPushButton(QStringLiteral("x"), &h);
		stubborn->setFixedWidth(23);
		stubborn->setGeometry(0, 0, 23, ch);
		auto *willing = new QPushButton(QStringLiteral("y"), &h);
		willing->setFixedWidth(2 * cw);
		willing->setGeometry(0, 2 * ch, 2 * cw, ch);
		QCoreApplication::processEvents();
		CHECK(stubborn->width() == 23,
		      "a fixed size off the grid resists snapping (the application's to fix)");
		CHECK(GridMetrics::is_aligned(willing->geometry()),
		      "a fixed size on the grid does not");

		GridSnap::remove();
		CHECK(!GridSnap::installed(), "remove() uninstalls it");
	}
	GridGuard::reset();      // the fixed-width button above is a reported one

	return fails;
}
