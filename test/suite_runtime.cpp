// suite_runtime -- L6 (section 5.6): the backend seam, the font provisioning
// check (section 5.3, risk R3), and GridGuard (section 5.3, section 9).
#include <qtty/qtty.h>
#include "src/backend/null/null_backend.h"
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

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
		// Reports something distinctive, so that what an application reads
		// back can be shown to have come from the BACKEND rather than from a
		// default that happens to look plausible.
		struct TellingBackend : NullBackend {
			using NullBackend::NullBackend;
			Capabilities capabilities() const override {
				Capabilities c = NullBackend::capabilities();
				c.cell_px = QSize(7, 13);
				c.background_known = true;
				c.background = QColor(1, 2, 3);
				c.color = Capabilities::TrueColor;
				return c;
			}
		};
		TellingBackend backend(QSize(40, 12));
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
		// Read from inside the run, which is the only place an application
		// could read it: Qtty::capabilities() answers for the session, and a
		// session is what exec() is.
		Capabilities seen;
		bool asked_during = false;
		QTimer quitter;
		quitter.setInterval(10);
		QObject::connect(&quitter, &QTimer::timeout, qApp, [&] {
			if (!asked_during) { seen = Qtty::capabilities(); asked_during = true; }
			QCoreApplication::quit();
		});
		quitter.start();
		CHECK(!Qtty::capabilities().cell_px.isValid(),
		      "before a run there is nothing to know, and it says so");
		const int rc = exec(*qApp, win, backend);
		quitter.stop();

		// The gap this closes: Capabilities were reachable only through
		// ITerminalBackend, and the convenience exec() builds its backend
		// internally -- so every field on that struct was declared and
		// unreachable from the seat an application sits in.
		CHECK(asked_during && seen.cell_px == QSize(7, 13)
		      && seen.background_known && seen.background == QColor(1, 2, 3),
		      "an application can read the negotiated capabilities during a run");
		CHECK(seen.color == Capabilities::TrueColor,
		      "and they are the backend's, not a plausible default");
		// Cleared afterwards, because a stale answer is worse than none: a
		// caller cannot tell one from a current one.
		CHECK(!Qtty::capabilities().cell_px.isValid(),
		      "and afterwards it goes back to knowing nothing");

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
		// The font setup() installed, rather than a second one built here to
		// the same recipe. The two parted company the moment the recipe grew
		// a hinting request: reconstructing it drops whatever the copy does
		// not remember to repeat, and this check then reports on a font no
		// program uses. Read from the application and it is the grid font by
		// construction.
		const QFont mono = QApplication::font();
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

		// The fourth lever, and the one that is not an environment variable:
		// hinting comes from the user's fontconfig. setup() asks for full
		// hinting on the font it builds, and this is what notices if that
		// stops happening.
		//
		// It discriminates everywhere, which the end-to-end form does not.
		// On an account whose fontconfig already selects hintfull -- the
		// account this tree was written on -- the metrics are 10x19 with the
		// request and 10x19 without it, so an assertion on the numbers
		// inspects nothing there. Reading the request back off the installed
		// font fails the moment the line is deleted, on any account.
		//
		// The command that discriminates end-to-end, on a machine whose
		// fontconfig leaves the packaged hintslight default in place, is to
		// run the suite at all: without the request the metrics come back
		// 9.625 x 18.6406 and setup() aborts before the first check.
		CHECK(QApplication::font().hintingPreference() == QFont::PreferFullHinting,
		      "setup() installed a font that asks for full hinting");
		// That the ENFORCER carries it too is checked in suite_grid, beside
		// the class-font simulation, because that is the only place a widget's
		// font is actually rebuilt. A plain widget here inherits the
		// application font untouched -- the enforcer returns early on it --
		// so a probe placed at this end would report on inheritance and pass
		// whatever the enforcer does. Confirmed by sabotage: it did.

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


		// The guard's NUMERIC branches are unreachable here, and this is a
		// measurement rather than an assumption. grid_font_problem() refuses
		// a fractional line height, a fractional advance, and either of them
		// at zero or less; only the fixed-pitch message above has ever been
		// produced by a test.
		//
		// Tried, all giving integral metrics on this font engine: letter
		// spacing (which QFontMetricsF::horizontalAdvance(QChar) ignores
		// entirely -- it is applied when laying out a run, not to a single
		// character), stretch at 50, 62, 75 and 150, stretch at 1, 3, 5 and 8
		// which the engine clamps so the advance never falls below 1, and
		// fractional point sizes of 10.5, 11.3 and 13.7. Every one produced a
		// whole-number advance and a height of 19.
		//
		// They are not dead code: an engine with subpixel metrics or a
		// fractional device pixel ratio produces exactly what they refuse,
		// which is the case qtty pins QT_SCALE_FACTOR to avoid. Left
		// uncovered deliberately, said here rather than left as a silent gap
		// for somebody to rediscover.
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

	// The other half of section 9's damage invariant. suite_budget asserts
	// that an unchanged tree diffs to NOTHING, with a paired everywhere-
	// different frame so a diff() returning nothing whatever it was handed
	// would fail -- the parse half, done carefully. What nothing asserted is
	// what the frame loop DOES with that answer: FrameScheduler skips
	// present() entirely when the damage is empty, and skipping is the entire
	// point of computing it.
	//
	// Found with the beerssh session's search key -- find the well-tested
	// parser, then ask what consumes it and whether anything asserts the
	// consumption.
	{
		NullBackend backend(QSize(30, 8));
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *label = new QLabel(QStringLiteral("steady"), &win);
		label->setGeometry(0, 0, cw * 10, ch);
		win.resize(GridMetrics::cells(30, 8));
		win.show();
		QCoreApplication::processEvents();

		InputRouter router(&win);
		Compositor comp(&win, &router);
		FrameScheduler sched(&backend, &comp, &win);

		sched.render_now();
		const int first = backend.frame_count();
		CHECK(first > 0, "the first frame is always presented, having no predecessor");

		sched.render_now();
		CHECK(backend.frame_count() == first,
		      "and an unchanged tree is not presented again");

		// Paired with a frame that DID change, for the reason suite_budget
		// pairs its empty-diff checks: a scheduler that never presented
		// anything would pass the check above and fail nothing.
		label->setText(QStringLiteral("moved"));
		QCoreApplication::processEvents();
		sched.render_now();
		CHECK(backend.frame_count() > first,
		      "while a tree that changed is");
	}
	{
		// The OTHER branch of that same gate, which the check above cannot
		// reach and which no widget test can see: `images_changed` compares
		// frame.images.SIZE. CellImage carries a content-addressed key --
		// the upload-once identity, and the only field that can tell two
		// pictures apart -- and the gate counts them instead.
		//
		// So a picture that changes without changing the CELLS under it
		// diffs to nothing, counts the same, and is never presented. Both
		// halves of the seam stay innocent: the compositor built the right
		// frame, and present() would have written it correctly had it been
		// called.
		struct Plot : PixelSurface {
			using PixelSurface::PixelSurface;
			QColor c = QColor(200, 40, 40);
			void paintEvent(QPaintEvent *) override {
				QPainter p(this);
				p.fillRect(rect(), c);
			}
		};
		NullBackend backend(QSize(30, 8));
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *plot = new Plot(&win);
		plot->setGeometry(0, 0, cw * 4, ch * 2);
		win.resize(GridMetrics::cells(30, 8));
		win.show();
		QCoreApplication::processEvents();

		InputRouter router(&win);
		Compositor comp(&win, &router);
		FrameScheduler sched(&backend, &comp, &win);
		sched.render_now();
		const int before = backend.frame_count();
		sched.render_now();
		CHECK(backend.frame_count() == before,
		      "an unchanged picture is not presented again either");

		// Only the PIXELS move. The widget keeps its geometry, so the cells
		// it occupies are identical and the placement count is identical --
		// which is exactly the case a size comparison cannot express.
		plot->c = QColor(40, 200, 40);
		plot->update();
		QCoreApplication::processEvents();
		sched.render_now();
		CHECK(backend.frame_count() > before,
		      "and a picture whose pixels changed under unchanged cells is");

		// The second field of that comparison, which the first case cannot
		// reach: a picture that MOVES keeps its key, because the key is the
		// pixels. Comparing keys alone would leave it drawn where it was.
		const int moved_from = backend.frame_count();
		plot->move(cw * 6, ch * 3);
		QCoreApplication::processEvents();
		sched.render_now();
		CHECK(backend.frame_count() > moved_from,
		      "and a picture that moved with the same pixels is too");
	}

	{
		// The idle heartbeat, which nothing had exercised. Its comment says
		// it catches timer-driven updates, and that is a real class: the
		// compositor paints a widget by calling render() on it directly, so
		// a widget whose output changes without Qt ever posting an
		// UpdateRequest -- a clock, a meter reading a sensor, anything drawn
		// from state rather than from a repaint -- produces a different frame
		// with nothing to say so. Without the 100 ms tick nothing would ask
		// for that frame and the screen would sit still.
		struct Ticking : QWidget {
			using QWidget::QWidget;
			mutable int paints = 0;
			void paintEvent(QPaintEvent *) override {
				QPainter p(this);
				p.drawText(rect(), Qt::AlignLeft, QString::number(++paints));
			}
		};
		NullBackend backend(QSize(30, 8));
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *tick = new Ticking(&win);
		tick->setGeometry(0, 0, cw * 8, ch);
		win.resize(GridMetrics::cells(30, 8));
		win.show();
		QCoreApplication::processEvents();

		InputRouter router(&win);
		Compositor comp(&win, &router);
		FrameScheduler sched(&backend, &comp, &win);
		sched.render_now();
		const int settled = backend.frame_count();

		// Nothing touches the widget from here: no event is posted, no
		// update() is called, and the loop only turns. Bounded by wall clock
		// as well as by the answer, so a scheduler that never fires ends the
		// test rather than the suite's watchdog.
		QElapsedTimer clock;
		clock.start();
		while (backend.frame_count() <= settled && clock.elapsed() < 1000)
			QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents,
			                                20);
		CHECK(backend.frame_count() > settled,
		      "an idle tick redraws a widget that changed with no event to say so");
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
