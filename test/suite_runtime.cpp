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

		// The guard above is about metrics, and a substitute satisfies it
		// whenever its own happen to be whole numbers. Measured with DejaVu
		// Sans Mono taken out of the font list: qtty ran on Noto Mono, same
		// 10x19 cell, and the whole suite passed without a word about it --
		// 828 checks that day, dated because it records a run rather than
		// describing this one. A
		// family cannot be pinned the way the platform, the theme, the scaling
		// and the hinting are -- a font that is not installed cannot be
		// conjured -- so it is announced.
		CHECK(grid_font_substitution(mono).isEmpty(),
		      "the grid font is the one that was asked for");
		QFont absent(QStringLiteral("No Such Family At All"));
		absent.setPixelSize(16);
		const QString said = grid_font_substitution(absent);
		CHECK(!said.isEmpty(),
		      "and a family Qt resolves elsewhere is reported, not accepted");
		CHECK(said.contains(QStringLiteral("No Such Family At All"))
		      && said.contains(QFontInfo(absent).family()),
		      "naming both what was asked for and what arrived");

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
		// setup() installs it, which is the decision section 7.8 records as
		// taken. Asserted through its EFFECT and not only through
		// installed(): a plain box layout, nothing explicit anywhere, must
		// land its children on the grid because the library put the filter
		// there. A check on installed() alone would pass against a filter
		// that was installed and did nothing.
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
		CHECK(GridSnap::installed() && off == 0,
		      "setup() installs GridSnap, so a plain layout lands on the grid");
	}
	{
		// Inert unless installed (section 10.1's rule, which this has to obey
		// as much as the paint filter does). setup() installs it now, so this
		// half has to take it away first -- and putting it back is not
		// optional, because every case after this one runs under the library
		// as an application gets it.
		GridSnap::remove();
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
		GridSnap::install(*qApp);      // as setup() left it, for what follows
	}
	GridGuard::reset();      // the fixed-width button above is a reported one

	// Layout margins: nothing above and below, one column either side. That
	// is the rule the spacings already state -- PM_LayoutVerticalSpacing is 0
	// and PM_LayoutHorizontalSpacing is cw -- and the margins used to say the
	// opposite, spending a whole row above the first widget and another below
	// the last.
	//
	// It cost more than the eight per cent of a 24-row screen: at 80x1 a
	// window with a plain QVBoxLayout rendered entirely blank, its first
	// widget one row below the only row there was. The pair is what says the
	// rule rather than "no margins": vertical zero AND horizontal one.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);            // Qt's defaults, untouched
		auto *bar = new QMenuBar(&win);
		bar->addMenu(QStringLiteral("File"));
		v->addWidget(bar);
		win.resize(GridMetrics::cells(80, 1));
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor comp(&win, &r);
		CellBuffer b(80, 1);
		comp.compose(b);
		const QString row = b.to_text().section(QLatin1Char('\n'), 0, 0);
		CHECK(row.contains(QStringLiteral("File")),
		      "a one-row terminal shows its first widget, not a margin");
		CHECK(v->contentsMargins().top() == 0 && v->contentsMargins().bottom() == 0
		      && v->contentsMargins().left() == GridMetrics::cw(),
		      "and the margins are nothing vertically, one column either side");
		GridGuard::reset();      // an 80x1 window cannot hold a menu bar on the grid
	}

	// A terminal with no cells. The sweep that found this rendered the whole
	// tier at sizes nothing had rendered at -- one cell, one row, one column,
	// three by three, two hundred by two -- and every one of them composed
	// without complaint. Zero was the one that did not: the buffer is empty,
	// rasterize() answers a null QImage, and the software composite path opens
	// a QPainter on it, which fails and then warns on every call into the
	// stderr that is the terminal.
	//
	// Paired with a size that DOES produce a frame, because "no frame" is also
	// what a scheduler that never presents anything produces.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *label = new QLabel(QStringLiteral("hello"), &win);
		label->setGeometry(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		win.resize(GridMetrics::cells(20, 4));
		win.show();
		QCoreApplication::processEvents();
		InputRouter router(&win);
		Compositor comp(&win, &router);

		NullBackend empty(QSize(0, 0));
		FrameScheduler none(&empty, &comp, &win);
		none.render_now();
		CHECK(empty.frame_count() == 0, "a terminal with no cells is given no frame");

		NullBackend sized(QSize(20, 4));
		FrameScheduler some(&sized, &comp, &win);
		some.render_now();
		CHECK(sized.frame_count() == 1, "and one with cells still is");
	}

	// The cell size the snapshot fixtures were recorded at, named rather than
	// assumed. Both fixtures record CELLS, and every position in them comes
	// from cw and ch, which setup() derives from the locally installed DejaVu
	// Sans Mono -- so a machine whose font differs invalidates them.
	//
	// How fragile that is, measured here rather than feared: of 102
	// fixed-pitch families installed, all 102 give integral metrics at pixel
	// size 16 once full hinting is asked for -- so integrality is no longer
	// the exposure -- but they give **eleven distinct cell sizes**, and only
	// seven families give the 10x19 these fixtures assume. 8x16 is the
	// commonest at 35.
	//
	// This check is what turns that from two unexplained snapshot diffs into
	// one sentence. It cannot be satisfied by a bundled font that nobody has
	// chosen (project.md 7.9 and 11) -- it is the guard for the machine that
	// has not got the same one.
	{
		CHECK(GridMetrics::cw() == 10 && GridMetrics::ch() == 19,
		      "the cell is 10x19, which is what the snapshot fixtures assume");
	}



	// A window whose layout minimum exceeds the terminal, which is the case
	// design.md section 7 measured and left unanswered. A layout refuses to
	// shrink below its minimum, so the window keeps its size and the frame
	// simply stops: measured on a nine-cell dialog in a six-row terminal,
	// which showed six fields and neither the last two nor the button that
	// closes it. Nothing scrolled, nothing said so, and Tab could move focus
	// to a widget nobody could see.
	//
	// The root scrolls to follow the focus now. Following the focus rather
	// than binding a key is what makes it free: arrow keys belong to the
	// focused widget and a chord would have to be learned, but Tab already
	// walks the form.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		for (int i = 0; i < 8; ++i) {
			auto *row = new QHBoxLayout;
			row->addWidget(new QLabel(QStringLiteral("Field %1").arg(i)));
			row->addWidget(new QLineEdit(QStringLiteral("value %1").arg(i)));
			v->addLayout(row);
		}
		auto *close = new QPushButton(QStringLiteral("&Close"));
		v->addWidget(close);
		win.show();
		win.resize(GridMetrics::cells(30, 6));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);

		CellBuffer top(30, 6);
		c.compose(top);
		const QString at_top = top.to_text();
		close->setFocus();
		QCoreApplication::processEvents();
		CellBuffer bottom(30, 6);
		c.compose(bottom);
		const QString at_bottom = bottom.to_text();
		// The pair, because either half alone is satisfied by a bug. "The
		// button is visible" would pass against a frame that showed
		// everything; "the first field is gone" would pass against a frame
		// that showed nothing at all.
		CHECK(!at_top.contains(QStringLiteral("Close"))
		      && at_bottom.contains(QStringLiteral("Close"))
		      && at_top.contains(QStringLiteral("Field 0"))
		      && !at_bottom.contains(QStringLiteral("Field 0")),
		      "a window taller than the terminal scrolls to its focus");
		GridGuard::reset();
	}

	// And the control: a window that FITS must not move, however the focus
	// travels. Without this the check above is satisfied by a compositor that
	// scrolls whenever it feels like it, and every ordinary dialog would
	// wander under the Tab key.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *first = new QLineEdit(QStringLiteral("one"));
		auto *last = new QPushButton(QStringLiteral("&Go"));
		v->addWidget(first);
		v->addWidget(last);
		win.show();
		win.resize(GridMetrics::cells(20, 8));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);
		first->setFocus();
		QCoreApplication::processEvents();
		CellBuffer a(20, 8);
		c.compose(a);
		last->setFocus();
		QCoreApplication::processEvents();
		CellBuffer b(20, 8);
		c.compose(b);
		CHECK(a.to_text() == b.to_text(),
		      "and a window that fits does not scroll at all");
		GridGuard::reset();
	}



	// design.md section 7's Tier-2 hint, and the half of the small-terminal
	// policy that needs the application: only it can say what a screen can
	// afford to lose. Carried as a property so it is a no-op in a GUI build.
	//
	// Four things are asserted because each fails on its own. A pass that
	// drops nothing, a pass that drops everything, a pass that never puts
	// anything back, and a pass that hides the widget holding focus are four
	// different bugs and only the first is caught by "the screen fits".
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *keep = new QLabel(QStringLiteral("Required"));
		v->addWidget(keep);
		QVector<QLabel *> optional;
		for (int i = 0; i < 6; ++i) {
			auto *l = new QLabel(QStringLiteral("Extra %1").arg(i));
			set_priority(l, Priority::Optional);
			optional.append(l);
			v->addWidget(l);
		}
		win.show();
		keep->setFocus();
		QCoreApplication::processEvents();

		InputRouter r(&win);
		Compositor c(&win, &r);

		win.resize(GridMetrics::cells(20, 12));
		QCoreApplication::processEvents();
		CellBuffer big(20, 12);
		c.compose(big);
		const bool none_dropped = keep->isVisible() && optional.first()->isVisible();

		win.resize(GridMetrics::cells(20, 3));
		QCoreApplication::processEvents();
		CellBuffer small(20, 3);
		c.compose(small);
		int hidden = 0;
		for (QLabel *l : std::as_const(optional))
			if (!l->isVisible()) ++hidden;
		const bool kept_required = keep->isVisible();

		win.resize(GridMetrics::cells(20, 12));
		QCoreApplication::processEvents();
		CellBuffer back(20, 12);
		c.compose(back);
		int restored = 0;
		for (QLabel *l : std::as_const(optional))
			if (l->isVisible()) ++restored;

		CHECK(none_dropped, "a terminal with room drops no optional widget");
		CHECK(hidden > 0, "and one without room drops them");
		CHECK(kept_required, "but never a required one");
		CHECK(restored == optional.size(), "and a terminal that grows back shows them again");
		GridGuard::reset();
	}

	// The focused widget is never dropped, whatever its priority: hiding the
	// widget that owns input moves focus somewhere the application did not
	// choose, and a terminal has no pointer to put it back with.
	//
	// Its own fixture, because the first version of this check was VACUOUS and
	// the sabotage said so. It marked a focused line edit optional among six
	// other optional labels, and the pass stops as soon as the screen fits --
	// so the edit survived because it was never reached, not because it held
	// the focus, and removing the focus rule changed nothing. Here the focused
	// widget is the ONLY optional one and the terminal is far too small, so
	// nothing but the rule can save it.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *edit = new QLineEdit(QStringLiteral("field"));
		set_priority(edit, Priority::Optional);
		v->addWidget(edit);
		for (int i = 0; i < 4; ++i)
			v->addWidget(new QLabel(QStringLiteral("Required %1").arg(i)));
		win.show();
		edit->setFocus();
		win.resize(GridMetrics::cells(20, 2));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);
		CellBuffer b(20, 2);
		c.compose(b);
		CHECK(edit->isVisible(),
		      "nor the one holding the focus, whatever its priority");
		GridGuard::reset();
	}



	// A group box's contents rectangle, which Fusion answers in pixels: 25 for
	// the contents top, and 25 is 1.3 cells. GridSnap rounds to NEAREST, so a
	// squeezed box rounded its first child DOWN onto the frame's own top row
	// and drew through it:
	//
	//     +[ ]-Option 0--------+   the frame's own top border
	//     |                    |
	//     +[ ]-Option 2--------+   and its bottom one
	//
	// This is the overlap section 7.8 named as the risk to measure before
	// writing any snapping, arriving in the wild. The answer is not to change
	// the rounding but to hand it rectangles it cannot round wrong: a titled
	// box spends one row on the title and one on the frame's top border, and
	// starts its contents on the row after.
	//
	// Asserted where the two collide rather than on a whole frame: the row
	// that carries the frame's top corner must carry nothing else. A check on
	// "the box renders" passes against the broken version, which is exactly
	// how this survived until a five-row terminal.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *gb = new QGroupBox(QStringLiteral("Advanced"));
		auto *gv = new QVBoxLayout(gb);
		for (int i = 0; i < 3; ++i)
			gv->addWidget(new QCheckBox(QStringLiteral("Option %1").arg(i)));
		v->addWidget(gb);
		win.resize(GridMetrics::cells(24, 5));
		win.show();
		QCoreApplication::processEvents();
		CellBuffer b(24, 5);
		render_once(win, b);
		QString top, bottom;
		for (const QString &line : b.to_text().split(QLatin1Char('\n'))) {
			if (line.contains(QStringLiteral("┌"))) top = line;
			if (line.contains(QStringLiteral("└"))) bottom = line;
		}
		CHECK(!top.isEmpty() && !top.contains(QLatin1Char('['))
		      && !bottom.contains(QLatin1Char('[')),
		      "a group box's frame carries no widget on its border rows");
		GridGuard::reset();
	}



	// A popup's frame, and the row its contents lost to it. PM_MenuPanelWidth
	// is one number and a cell is not square: `cw` is a whole column and
	// 10/19 of a row, so a combo box's popup came out 3.05 cells tall --
	// measured at 220x58 px with its list view at y=10 -- and the second item
	// drew ON the frame's bottom border. The same fault as the group box, in
	// another widget, found by opening a popup over a scrolled root.
	//
	// PM_MenuVMargin makes the vertical frame up to a whole row. Asserted as
	// the two things that were wrong: the popup is a whole number of rows, and
	// nothing but frame is on its border rows.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		auto *combo = new QComboBox;
		combo->addItems({QStringLiteral("alpha"), QStringLiteral("beta")});
		v->addWidget(combo);
		win.show();
		win.resize(GridMetrics::cells(24, 6));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);
		combo->showPopup();
		QCoreApplication::processEvents();
		CellBuffer b(24, 6);
		c.compose(b);
		const auto pops = r.popups();
		const int h = pops.isEmpty() ? 0 : pops.first()->height();
		QString top, bottom;
		for (const QString &line : b.to_text().split(QLatin1Char('\n'))) {
			if (line.contains(QStringLiteral("┌"))) top = line;
			if (line.contains(QStringLiteral("└"))) bottom = line;
		}
		CHECK(h > 0 && h % GridMetrics::ch() == 0,
		      "a popup is a whole number of rows tall");
		CHECK(!top.isEmpty() && !bottom.isEmpty()
		      && !bottom.contains(QStringLiteral("beta")),
		      "and its bottom border carries no item");
		combo->hidePopup();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}


	// Compositor::compose() walks EVERY visible top-level (section 5.4 step
	// 3), and the cases above leave theirs alive and visible -- so a frame
	// composed below carries widgets this fixture never built, and an
	// assertion about what is on the screen answers partly for somebody
	// else's window. Reaping the deferred deletes is not enough, because
	// nothing here is deleted; the other top-levels are HIDDEN for the length
	// of a case and shown again when it ends.
	struct OnlyTopLevel {
		QVector<QPointer<QWidget>> hid;
		explicit OnlyTopLevel(QWidget *keep) {
			const auto tops = QApplication::topLevelWidgets();
			for (QWidget *w : tops) {
				if (w == keep || !w->isVisible()) continue;
				w->hide();
				hid.append(w);
			}
		}
		~OnlyTopLevel() {
			for (const QPointer<QWidget> &w : std::as_const(hid))
				if (w) w->show();
		}
	};

	// Dropping the optional widgets lowers the layout minimum, and that is the
	// only thing in section 7's policy that can make a screen FIT -- scrolling
	// never does, it only makes the rest reachable. But the resize that
	// provoked the drop was REFUSED before anything was hidden, and nothing
	// re-issued it: InputRouter::on_resize() resizes and returns, and the drop
	// happens a frame later inside compose().
	//
	// Measured 2026-09-02 at a 20x3 terminal: the window stayed 200x133 px
	// against a minimum that the drop had brought down to 57, two of six
	// optional widgets were still up, and exactly one of the three rows
	// carried anything.
	//
	// Paired with the content, because "the window is small enough" is
	// satisfied by a window shrunk to nothing and by a frame with nothing in
	// it. The required label has to still be on the screen afterwards.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		auto *v = new QVBoxLayout(&win);
		auto *keep = new QLabel(QStringLiteral("Required"));
		v->addWidget(keep);
		for (int i = 0; i < 6; ++i) {
			auto *l = new QLabel(QStringLiteral("Extra %1").arg(i));
			set_priority(l, Priority::Optional);
			v->addWidget(l);
		}
		win.show();
		keep->setFocus();
		win.resize(GridMetrics::cells(20, 12));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);
		CellBuffer roomy(20, 12);
		c.compose(roomy);

		win.resize(GridMetrics::cells(20, 3));
		QCoreApplication::processEvents();
		CellBuffer three(20, 3);
		c.compose(three);
		printf("info: at a 20x3 terminal the window comes down to %d px"
		       " of a terminal's %d\n", win.height(), 3 * ch);
		CHECK(win.height() <= 3 * ch
		      && three.to_text().contains(QStringLiteral("Required")),
		      "dropping the optional widgets brings the window down to the terminal");

		// One row is the case a fits() test cannot serve: nothing makes a
		// 19-pixel screen hold a 19-pixel label inside nine-pixel margins, so
		// asking whether the window fits before re-issuing the resize declines
		// to ask at all and leaves 133 pixels of window on 19 pixels of
		// terminal -- which showed as an entirely blank frame.
		win.resize(GridMetrics::cells(20, 1));
		QCoreApplication::processEvents();
		CellBuffer one(20, 1);
		c.compose(one);
		printf("info: at a 20x1 terminal it comes down to %d px, and the"
		       " layout refuses below %d\n", win.height(),
		       win.minimumSizeHint().height());
		CHECK(one.to_text().contains(QStringLiteral("Required")),
		      "and a one-row terminal is not left blank");
		GridGuard::reset();
	}

	// The drop pass asked findChildren() for the widgets to consider, which is
	// recursive over the QObject TREE rather than over this window -- so a
	// dialog parented to the root arrived in the list along with everything
	// inside it. Hiding content in another top-level cannot make this one fit,
	// because fits() measures the ROOT's minimumSizeHint(): measured
	// 2026-09-02, a widget inside a child dialog went dark while the root's
	// minimum stayed at 95 px against a 57 px terminal.
	//
	// Paired, because "nothing was dropped anywhere" satisfies the half that
	// matters. The root's own optional widget has to be gone in the same pass
	// that leaves the dialog's alone.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		auto *v = new QVBoxLayout(&win);
		auto *keep = new QLabel(QStringLiteral("Required"));
		v->addWidget(keep);
		auto *root_extra = new QLabel(QStringLiteral("Root extra"));
		set_priority(root_extra, Priority::Optional);
		v->addWidget(root_extra);
		for (int i = 0; i < 4; ++i)
			v->addWidget(new QLabel(QStringLiteral("Pad %1").arg(i)));
		win.show();
		keep->setFocus();
		win.resize(GridMetrics::cells(20, 12));
		QCoreApplication::processEvents();

		QDialog dlg(&win);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		auto *dv = new QVBoxLayout(&dlg);
		auto *dialog_extra = new QLabel(QStringLiteral("Dialog extra"));
		set_priority(dialog_extra, Priority::Optional);
		dv->addWidget(dialog_extra);
		dlg.show();
		QCoreApplication::processEvents();

		InputRouter r(&win);
		Compositor c(&win, &r);
		win.resize(GridMetrics::cells(20, 3));
		QCoreApplication::processEvents();
		c.apply_priority(20, 3);
		CHECK(dialog_extra->isVisible() && !root_extra->isVisible(),
		      "the drop pass never reaches into another top-level");
		dlg.hide();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	{
		// What a layer hid must not outlive the layer. compose() drops a
		// modal's optional widgets on a terminal too small for them and puts
		// them back when the modal goes away -- and nothing exercised the
		// second half. Coverage is what said so: both restores, the modal's
		// and the popup's, had no caller in a whole run.
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&win);
		v->addWidget(new QLabel(QStringLiteral("Root")));
		win.resize(GridMetrics::cells(20, 6));
		win.show();
		QCoreApplication::processEvents();

		QDialog dlg(&win);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		dlg.setModal(true);
		auto *dv = new QVBoxLayout(&dlg);
		auto *keep = new QLabel(QStringLiteral("Required"));
		dv->addWidget(keep);
		auto *extra = new QLabel(QStringLiteral("Dialog extra"));
		set_priority(extra, Priority::Optional);
		dv->addWidget(extra);
		for (int i = 0; i < 6; ++i)
			dv->addWidget(new QLabel(QStringLiteral("Pad %1").arg(i)));
		dlg.resize(GridMetrics::cells(18, 10));
		dlg.show();
		keep->setFocus();
		QCoreApplication::processEvents();

		InputRouter r(&win);
		Compositor c(&win, &r);
		CellBuffer b(20, 4);
		c.compose(b);
		const bool dropped = !extra->isVisible();
		dlg.hide();
		QCoreApplication::processEvents();
		c.compose(b);                                // the restore happens here
		// isHidden(), not isVisible(): the widget's parent is a closed dialog,
		// so isVisible() is false for a reason that has nothing to do with the
		// restore. The first version of this check asked the wrong one and
		// reported a defect that was not there -- the compositor had put the
		// widget back correctly all along.
		const bool unhidden = !extra->isHidden();
		dlg.show();                                  // and the user's question
		QCoreApplication::processEvents();
		CHECK(dropped,
		      "a modal too big for the terminal drops its optional widgets");
		CHECK(unhidden && extra->isVisible(),
		      "and they are back when it opens again");
		win.hide();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// Section 7's policy belongs to the layer that OWNS INPUT, and a modal
	// owns input while it is up (section 8.3). It was only ever run on the
	// root, and the follow-the-focus scroll was root-only too, so a modal
	// bigger than the terminal was merely clamped inside it and the rest was
	// unreachable -- which is worse in a modal than in the root, because there
	// is no Tab away to a window that does scroll.
	//
	// Measured 2026-09-02 with a control that separates the layer from the
	// fixture: the same eight-field form built once in the root and once in a
	// modal. In the root "Accept" reached the frame at 30x6 and at 30x3; in
	// the modal it reached neither.
	//
	// The pair is the same one the root's check uses, because either half
	// alone is satisfied by a bug: "the button is visible" passes against a
	// frame showing everything, and "the first field is gone" passes against a
	// frame showing nothing at all.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		win.resize(GridMetrics::cells(30, 6));
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);

		QDialog dlg(&win);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&dlg);
		QVector<QLabel *> fields;
		for (int i = 0; i < 8; ++i) {
			auto *l = new QLabel(QStringLiteral("Field %1").arg(i));
			fields.append(l);
			v->addWidget(l);
		}
		auto *accept = new QPushButton(QStringLiteral("Accept"));
		v->addWidget(accept);
		dlg.setModal(true);
		dlg.show();
		dlg.resize(GridMetrics::cells(30, 9));
		QCoreApplication::processEvents();
		CHECK(QApplication::activeModalWidget() == &dlg,
		      "the dialog this case is about really is the active modal");

		fields.first()->setFocus();
		QCoreApplication::processEvents();
		CellBuffer top(30, 6);
		c.compose(top);
		const QString at_top = top.to_text();
		accept->setFocus();
		QCoreApplication::processEvents();
		CellBuffer bottom(30, 6);
		c.compose(bottom);
		const QString at_bottom = bottom.to_text();
		CHECK(at_top.contains(QStringLiteral("Field 0"))
		      && !at_top.contains(QStringLiteral("Accept"))
		      && at_bottom.contains(QStringLiteral("Accept"))
		      && !at_bottom.contains(QStringLiteral("Field 0")),
		      "a modal taller than the terminal scrolls to its focus");

		// The control, and it is the same one the root's scroll needed:
		// without it the check above is satisfied by a compositor that scrolls
		// whenever it likes, and every ordinary dialog would wander under the
		// Tab key. It has to be a modal that has ALREADY scrolled, though. A
		// dialog that never overflowed gives a clamp nothing to undo -- the
		// focus rule only moves the offset for a widget outside the viewport,
		// so a check on a dialog that always fitted passes against every
		// version of this code, sabotaged or not. Here the terminal grows
		// under the dialog that just scrolled three rows, and the frame has to
		// come back to the top of its own accord.
		//
		// Paired at both ends, so that neither a blank frame nor one showing
		// only the button satisfies it.
		CellBuffer roomy(30, 12);
		c.compose(roomy);
		const QString grown = roomy.to_text();
		CHECK(grown.contains(QStringLiteral("Field 0"))
		      && grown.contains(QStringLiteral("Accept")),
		      "and a modal that fits again scrolls by nothing");
		dlg.hide();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// And the other half of the policy on the same layer: a modal drops its
	// own optional widgets. Its own fixture rather than the one above, so that
	// the root is deliberately small enough to fit -- which makes the root's
	// optional label the control. It is not dropped by the root's pass,
	// because the root has room, and it must not be dropped by the modal's
	// either, because it is not the modal's to lose.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		auto *rv = new QVBoxLayout(&win);
		auto *root_extra = new QLabel(QStringLiteral("Root extra"));
		set_priority(root_extra, Priority::Optional);
		rv->addWidget(root_extra);
		win.resize(GridMetrics::cells(20, 8));
		win.show();
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);

		QDialog dlg(&win);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		auto *v = new QVBoxLayout(&dlg);
		auto *keep = new QLabel(QStringLiteral("Keep me"));
		v->addWidget(keep);
		QVector<QLabel *> optional;
		for (int i = 0; i < 6; ++i) {
			auto *l = new QLabel(QStringLiteral("Spare %1").arg(i));
			set_priority(l, Priority::Optional);
			optional.append(l);
			v->addWidget(l);
		}
		dlg.setModal(true);
		dlg.show();
		keep->setFocus();
		QCoreApplication::processEvents();

		CellBuffer b(20, 3);
		c.compose(b);
		int hidden = 0;
		for (QLabel *l : std::as_const(optional))
			if (!l->isVisible()) ++hidden;
		printf("info: the modal dropped %d of %d optional widgets\n",
		       hidden, int(optional.size()));
		CHECK(hidden > 0 && keep->isVisible() && root_extra->isVisible(),
		      "and a modal drops its own optional widgets, not the root's");
		dlg.hide();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// design.md section 7's policy on the POPUP layer, which is the layer it
	// never reached -- and a menu is the case with no way out of it at all.
	// There is no Tab away from an open menu the way there is from the root,
	// and Qt will not paginate one either: the offscreen QScreen is 800x800,
	// so nothing tells Qt how tall the terminal is.
	//
	// Measured 2026-09-02 before the fix: a 30-item menu is 608 px in a 190 px
	// terminal, and after twenty Down presses the active item was "Item 19"
	// while the frame was byte-identical to the one taken before them. Two
	// things were wrong at once. placed_at() clamps y to 0, so nothing below
	// the fold could ever be drawn; and follow_focus() tracked
	// layer->focusWidget(), which is null for every menu ever opened -- a
	// QMenu keeps its current item in QMenu::activeAction() and calls
	// setFocus() on nothing.
	//
	// The pair is the one the root's check and the modal's both use, because
	// either half alone is satisfied by a bug: "Item 19 is on screen" passes
	// against a frame showing everything, and "Item 0 is gone" passes against
	// a frame showing nothing at all.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		win.show();
		win.resize(GridMetrics::cells(40, 10));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);

		QMenu menu(&win);
		for (int i = 0; i < 30; ++i)
			menu.addAction(QStringLiteral("Item %1").arg(i));
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		CellBuffer before(40, 10);
		c.compose(before);
		for (int i = 0; i < 20; ++i)
			r.on_key({Qt::Key_Down, QString(), false, false, false});
		QCoreApplication::processEvents();
		CellBuffer after(40, 10);
		c.compose(after);
		printf("info: a %d px menu in a %d px terminal, active item '%s'\n",
		       menu.height(), 10 * ch,
		       menu.activeAction() ? qPrintable(menu.activeAction()->text())
		                           : "(none)");
		CHECK(before.to_text().contains(QStringLiteral("Item 0"))
		      && !before.to_text().contains(QStringLiteral("Item 19"))
		      && after.to_text().contains(QStringLiteral("Item 19"))
		      && !after.to_text().contains(QStringLiteral("Item 0")),
		      "a menu taller than the terminal scrolls to its active item");

		// The first control, and it is the modal's verbatim: the terminal grows
		// under a menu that has ALREADY scrolled, and the offset has to come
		// back to nothing of its own accord. It has to be a menu that scrolled,
		// because the follow rule only ever moves the offset for an item
		// outside the viewport -- on a menu that always fitted this passes
		// against every version of the code, sabotaged or not. Paired at both
		// ends, so that neither a blank frame nor one showing only the tail
		// satisfies it.
		CellBuffer roomy(40, 40);
		c.compose(roomy);
		CHECK(roomy.to_text().contains(QStringLiteral("Item 0"))
		      && roomy.to_text().contains(QStringLiteral("Item 29")),
		      "and a menu that fits again scrolls by nothing");
		menu.close();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// And the second control. It is NOT "a menu that fits does not scroll" --
	// that one was written first and REPLACED, because no sabotage could
	// redden it. The offset is clamped to what overflows and a menu that fits
	// overflows by nothing, so the sentence is true whatever the follow rule
	// does: it stayed green through every sabotage that reddened the check it
	// was supposed to be controlling, which is the whole of the argument
	// against it.
	//
	// What can go wrong is scrolling too FAR. A rule that puts the active item
	// at the top of the terminal rather than merely inside it walks the last
	// item of a menu to the top row with nine blank rows under it, and the
	// scrolling check passes against that -- the item it names is on the
	// screen either way. So the control is that the offset is exactly what
	// overflows: standing on the last item, the nine above it are on the
	// screen too.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		win.show();
		win.resize(GridMetrics::cells(40, 10));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);

		QMenu menu(&win);
		for (int i = 0; i < 30; ++i)
			menu.addAction(QStringLiteral("Item %1").arg(i));
		menu.popup(QPoint(0, 0));
		QCoreApplication::processEvents();
		for (int i = 0; i < 30; ++i)          // the first Down selects Item 0
			r.on_key({Qt::Key_Down, QString(), false, false, false});
		QCoreApplication::processEvents();
		CellBuffer b(40, 10);
		c.compose(b);
		printf("info: standing on '%s', the frame holds:\n%s",
		       menu.activeAction() ? qPrintable(menu.activeAction()->text())
		                           : "(none)",
		       qPrintable(b.to_text()));
		CHECK(b.to_text().contains(QStringLiteral("Item 29"))
		      && b.to_text().contains(QStringLiteral("Item 20"))
		      && !b.to_text().contains(QStringLiteral("Item 19")),
		      "and it scrolls by what overflows and no further");
		menu.close();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// A popup anchored inside the root has to move WITH the root. compose()
	// draws the root at -scroll rather than moving it, so a popup left at its
	// own geometry stays beside the widget that used to be at that screen row.
	//
	// Measured 2026-09-02 with a menu that fits exactly where it was opened,
	// so that neither placed_at()'s clamp nor its flip can explain the result:
	// the root scrolled and the menu did not, and it sat beside the wrong
	// widget.
	//
	// Read out of the FRAME rather than recomputed. The whole fault is that
	// window coordinates and screen coordinates have come apart, so a check
	// that derives the expected position the way the compositor does would
	// agree with the compositor whatever the compositor did.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		for (int i = 0; i < 13; ++i)
			v->addWidget(new QLabel(QStringLiteral("row%1").arg(i)));
		auto *bottom = new QPushButton(QStringLiteral("Bottom"));
		v->addWidget(bottom);
		win.show();
		win.resize(GridMetrics::cells(30, 14));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);
		bottom->setFocus();
		QCoreApplication::processEvents();

		// Opened beside "row5" and clear of its text, and small enough to fit
		// on the screen at its own unscrolled position too -- so the clamp
		// never fires and the only thing that can move it is the root.
		QMenu menu(&win);
		menu.addAction(QStringLiteral("Cut"));
		menu.addAction(QStringLiteral("Copy"));
		menu.popup(QPoint(15 * cw, 5 * ch));
		QCoreApplication::processEvents();
		CellBuffer b(30, 10);
		c.compose(b);
		const auto rows_of = [](const CellBuffer &buf, const QString &text) {
			const auto lines = buf.to_text().split(QLatin1Char('\n'));
			for (int i = 0; i < lines.size(); ++i)
				if (lines.at(i).contains(text)) return i;
			return -1;
		};
		const int row5 = rows_of(b, QStringLiteral("row5"));
		const int cut = rows_of(b, QStringLiteral("Cut"));
		printf("info: over a scrolled root, 'row5' is on frame row %d and the"
		       " menu's first item on %d\n", row5, cut);
		if (row5 < 0 || cut != row5 + 1)
			printf("info: the frame holds:\n%s", qPrintable(b.to_text()));
		// Paired with "the root really did scroll", because a root that never
		// moved satisfies the relation for the wrong reason -- and that is
		// exactly the state this check was written against.
		CHECK(!b.to_text().contains(QStringLiteral("row0"))
		      && row5 >= 0 && cut == row5 + 1,
		      "a popup anchored in the root is drawn beside the widget it was "
		      "opened at, over a root that has scrolled");

		// Composing again must not move it again. The popup is MOVED to where
		// it is drawn (the router hit-tests it against its own geometry), so
		// its geometry stops being the position it was opened at -- and an
		// implementation that subtracts the scroll from the geometry every
		// frame walks the menu off the top of the screen one frame at a time.
		CellBuffer twice(30, 10);
		c.compose(twice);
		CHECK(twice.to_text() == b.to_text(),
		      "and composing the same frame again does not move it further");
		menu.close();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	// The control for the pair above: the same fixture on a terminal big
	// enough that the root does not scroll at all. Without it, a compositor
	// that subtracts a fixed offset from every popup passes the check above --
	// the menu would be beside "row5" for a reason that has nothing to do with
	// the root's position.
	{
		QWidget win;
		win.setAttribute(Qt::WA_DontShowOnScreen);
		OnlyTopLevel only(&win);
		auto *v = new QVBoxLayout(&win);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		for (int i = 0; i < 13; ++i)
			v->addWidget(new QLabel(QStringLiteral("row%1").arg(i)));
		auto *bottom = new QPushButton(QStringLiteral("Bottom"));
		v->addWidget(bottom);
		win.show();
		win.resize(GridMetrics::cells(30, 14));
		QCoreApplication::processEvents();
		InputRouter r(&win);
		Compositor c(&win, &r);
		bottom->setFocus();
		QCoreApplication::processEvents();

		QMenu menu(&win);
		menu.addAction(QStringLiteral("Cut"));
		menu.addAction(QStringLiteral("Copy"));
		menu.popup(QPoint(15 * cw, 5 * ch));
		QCoreApplication::processEvents();
		CellBuffer b(30, 14);
		c.compose(b);
		const auto lines = b.to_text().split(QLatin1Char('\n'));
		int row5 = -1, cut = -1;
		for (int i = 0; i < lines.size(); ++i) {
			if (lines.at(i).contains(QStringLiteral("row5"))) row5 = i;
			if (lines.at(i).contains(QStringLiteral("Cut"))) cut = i;
		}
		printf("info: over an unscrolled root, 'row5' is on frame row %d and"
		       " the menu's first item on %d\n", row5, cut);
		CHECK(b.to_text().contains(QStringLiteral("row0"))
		      && row5 == 5 && cut == 6,
		      "and a popup over a root that has not scrolled stays exactly "
		      "where it was opened");
		menu.close();
		QCoreApplication::processEvents();
		GridGuard::reset();
	}

	return fails;
}
