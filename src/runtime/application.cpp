// src/runtime/application.cpp -- L6 entry points (section 5.6), wired to the real
// section 5 architecture: AnsiBackend -> InputRouter -> Compositor -> FrameScheduler.
#include "qtty/application.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/runtime.h"
#include "qtty/theme.h"
#include "../backend/ansi/ansi_backend.h"
#include <QtWidgets>

namespace Qtty {

void prepareEnvironment() {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
}

void setup(QApplication &app) {
	// Bundled-font provisioning (section 5.3) is later Phase-2 work; DejaVu Sans
	// Mono is the interim source of integral metrics, asserted as designed.
	QFont f(QStringLiteral("DejaVu Sans Mono"));
	f.setPixelSize(16);

	// A hard startup error, not a rendering glitch (section 5.3, risk R3). The
	// check this replaced was a Q_ASSERT_X comparing the advance of 'i' with
	// that of 'M': it tested monospace-ness rather than integral metrics, and
	// being an assert it compiled out in release, so no shipping build carried
	// R3's mitigation at all. qFatal is deliberate -- every column the grid
	// computes from here is wrong, and failing at the point of cause is worth
	// more than a screen that degrades a little further with each column.
	if (const QString problem = grid_font_problem(f); !problem.isEmpty()) {
		qFatal("qtty: the grid needs a font with integral metrics: %s",
			   qPrintable(problem));
	}

	const QFontMetrics fm(f);
	GridMetrics::set(fm.horizontalAdvance(u'M'), fm.height());
	app.setFont(f);
	app.setStyle(new GridStyle);
	setTheme(CellTheme::terminalDefault());

	// The guard is installed in debug builds and compiled out of release, as
	// the design specifies. Tests install it explicitly whatever the build,
	// since section 9 asks for it to run as an assertion in every test.
#ifndef QT_NO_DEBUG
	GridGuard::install(app);
#endif
}

void renderOnce(QWidget &win, CellBuffer &buf, QVector<CellImage> *placements) {
	CellPaintDevice dev(buf);
	QPainter p(&dev);
	win.render(&p, QPoint(), QRegion(),
		       QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
	p.end();
	buf.images = dev.placements;
	if (placements) *placements = dev.placements;
}

static bool s_tuiActive = false;
bool isTuiActive() { return s_tuiActive; }

int exec(QApplication &app, QWidget &win, ITerminalBackend &backend) {
	s_tuiActive = true;

	const QSize cells = backend.size();
	win.setAttribute(Qt::WA_DontShowOnScreen);
	win.resize(cells.width() * GridMetrics::cw(), cells.height() * GridMetrics::ch());
	win.show();
	QCoreApplication::processEvents();
	setFocusWidget(win.focusWidget());

	InputRouter router(&win);
	Compositor compositor(&win, &router);
	FrameScheduler scheduler(&backend, &compositor, &win);
	backend.setEventSink(&router);
	router.frameRequested = [&scheduler] { scheduler.requestFrame(); };

	scheduler.renderNow();                      // initial frame
	const int rc = app.exec();
	s_tuiActive = false;
	return rc;
}

int exec(QApplication &app, QWidget &win) {
	// The built-in backend, owned for the duration of the run. Everything else
	// happens in the overload above, so the two paths cannot drift.
	AnsiBackend backend;
	return exec(app, win, backend);
}

} // namespace Qtty
