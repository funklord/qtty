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

void prepare_environment() {
	// Offscreen is FORCED and not merely preferred, and the difference
	// matters: a desktop session commonly exports QT_QPA_PLATFORM as xcb or
	// wayland, and honouring that would make a terminal program open a
	// window. So an ambient setting must not reach this.
	//
	// But forcing it unconditionally, which is what stood here, means the
	// library can never be exercised under any other platform -- and a single
	// platform is a single configuration. Several faults in this tree lived in
	// offscreen's particulars rather than in the code: QApplication::
	// activePopupWidget() is permanently null there, no window ever activates,
	// and a caret paints only under a selection. Nothing announced any of
	// those; they were found one at a time.
	//
	// The override is therefore a qtty-specific variable, which no desktop
	// sets by accident and no user has already exported for another reason.
	const QByteArray want = qgetenv("QTTY_QPA_PLATFORM");
	qputenv("QT_QPA_PLATFORM", want.isEmpty() ? QByteArray("offscreen") : want);

	// The platform THEME is pinned for the same reason and by the same rule,
	// and this is the fix rather than the three that preceded it. A theme
	// loads under the offscreen platform perfectly happily -- measured -- so
	// a desktop was reaching into a terminal program by a route nobody had
	// looked at. With QT_QPA_PLATFORMTHEME=gtk3, which distributions set
	// globally, it supplied per-class fonts (QPushButton, QLabel and QMenu
	// came back Noto Sans 13, NOT fixed pitch, advancing 12 against a
	// 10-pixel cell), 14 of the 22 palette roles, three style hints, and 20
	// of the 71 standard key bindings.
	//
	// The last of those is why pinning beats patching: fonts and hints can be
	// forced back one at a time, and key bindings cannot -- Qt exposes no way
	// to choose the keyboard scheme, so Ctrl+K, Ctrl+U, Ctrl+E and Ctrl+D
	// appear or do not appear according to the desktop. A terminal program
	// whose keys depend on the machine's desktop is wrong in a way no
	// per-symptom fix reaches.
	//
	// Empty selects Qt's generic theme, which is what these fixtures have
	// always been recorded against, so pinning makes real use match what is
	// tested rather than diverging from it.
	const QByteArray theme = qgetenv("QTTY_QPA_PLATFORMTHEME");
	qputenv("QT_QPA_PLATFORMTHEME", theme);

	qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
}

namespace {

// A platform theme sets fonts PER WIDGET CLASS, and those beat the
// application-wide font that setup() installs. Measured under xcb with the
// gtk3 theme: QApplication::font() was DejaVu Sans Mono 16 as asked, while
// QPushButton, QLabel and QMenu were handed Noto Sans 13, not fixed pitch,
// advancing 12 for 'M' and 3 for 'i' against a 10-pixel cell.
//
// That is exactly the failure grid_font_problem() exists to prevent, and the
// check could not see it: it is handed the font setup() built, which is the
// one font the theme does NOT override. A guard reading the wrong object
// reports success as loudly as a real pass.
//
// Forcing the family and size on every widget as it is polished needs no list
// of class names, which is the point -- a list is a thing Qt adds to. Weight,
// italic and underline are kept, because those are a widget's own and a
// terminal can carry all three.
class FontEnforcer : public QObject {
public:
	QFont base;
	bool eventFilter(QObject *o, QEvent *e) override {
		// Polish AND FontChange, because the theme's class font does not
		// arrive at polish time. Traced under gtk3: at Polish the button
		// still reported DejaVu Sans Mono, and a FontChange landed afterwards
		// leaving it Sans -- so a filter watching only Polish saw the right
		// font every time and corrected nothing. Reacting to the change is
		// what actually catches it.
		//
		// Setting the font here raises another FontChange, which terminates
		// because the test below is an equality: the second pass matches and
		// returns. Same idempotence argument as GridSnap.
		if (e->type() != QEvent::Polish && e->type() != QEvent::FontChange)
			return false;
		QWidget *w = qobject_cast<QWidget *>(o);
		if (!w) return false;
		const QFont had = w->font();
		if (had.family() == base.family() && had.pixelSize() == base.pixelSize())
			return false;                        // already ours: idempotent
		QFont want = base;
		want.setBold(had.bold());
		want.setItalic(had.italic());
		want.setUnderline(had.underline());
		w->setFont(want);
		return false;                            // never consume
	}
};

} // namespace

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
	auto *fonts = new FontEnforcer;
	fonts->base = f;
	fonts->setParent(&app);
	app.installEventFilter(fonts);
	app.setStyle(new GridStyle);
	// Lets an ICellPainted widget paint itself in cells instead of going
	// through Channel B (section 5.3, risk R5). Inert in a GUI build by
	// construction: with no cell device being rendered into, the filter stands
	// down and the widget paints normally.
	install_cell_paint_filter(app);
	set_theme(CellTheme::terminal_default());

	// The guard is installed in debug builds and compiled out of release, as
	// the design specifies. Tests install it explicitly whatever the build,
	// since section 9 asks for it to run as an assertion in every test.
#ifndef QT_NO_DEBUG
	GridGuard::install(app);
#endif
}

void render_once(QWidget &win, CellBuffer &buf, QVector<CellImage> *placements) {
	CellPaintDevice dev(buf);
	QPainter p(&dev);
	win.render(&p, QPoint(), QRegion(),
	           QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
	p.end();
	buf.images = dev.placements;
	if (placements) *placements = dev.placements;
}

static bool s_tuiActive = false;
bool is_tui_active() { return s_tuiActive; }

int exec(QApplication &app, QWidget &win, ITerminalBackend &backend) {
	s_tuiActive = true;

	const QSize cells = backend.size();
	win.setAttribute(Qt::WA_DontShowOnScreen);

	// The primary window is the whole terminal, and it is at the origin. Both
	// halves matter and only the first was being set. Compositor::compose()
	// draws win_ at QPoint() whatever its geometry says, while
	// InputRouter::on_mouse() maps a click through win_->mapFromGlobal(): the
	// two agree at (0,0) and nowhere else, and they were agreeing by accident
	// because that is where the offscreen platform happens to put a window.
	// Stating it here costs one call and makes the accident an invariant --
	// which is the only kind of assumption a test can be written against,
	// since a probe at the origin cannot express an origin bug.
	win.move(0, 0);
	win.resize(cells.width() * GridMetrics::cw(), cells.height() * GridMetrics::ch());
	win.show();
	QCoreApplication::processEvents();
	setFocusWidget(win.focusWidget());

	InputRouter router(&win);
	Compositor compositor(&win, &router);
	FrameScheduler scheduler(&backend, &compositor, &win);
	backend.set_event_sink(&router);
	router.frame_requested = [&scheduler] { scheduler.request_frame(); };

	scheduler.render_now();                      // initial frame
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
