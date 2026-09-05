// src/runtime/application.cpp -- L6 entry points (section 5.6), wired to the real
// section 5 architecture: AnsiBackend -> InputRouter -> Compositor -> FrameScheduler.
#include "qtty/application.h"
#include "terminal_owner.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/runtime.h"
#include "qtty/theme.h"
#include "../backend/ansi/ansi_backend.h"
#include <QtWidgets>
#include <cstdlib>
#include <unistd.h>

namespace Qtty {

// ---- diagnostics do not go on the screen the frame is on -------------------
// Nothing installed a message handler, and qtty emits qWarning of its own --
// the grid guard once per off-grid widget, the section 6 contrast check once
// per offending cell, the font substitution named below, and the SIGWINCH
// pipe. Listed rather than counted: a count here was right and its list was
// wrong for a day, and the list is the half a reader can act on. Qt adds its own: a resize below the layout minimum produced over a
// hundred "This plugin does not support propagateSizeHints()" lines in a
// single run. All of it goes to stderr, and stderr is the terminal the frame
// is being drawn on.
//
// Measured: with the backend running and stderr on a pseudo-terminal, a
// qWarning put its text on that terminal -- in the middle of the frame, where
// nothing will repaint over it, because the cell plane never changed and the
// next diff has nothing to say about a region qtty did not write.
//
// The rule is the one that needs no coupling to the backend's state: hold
// them while stderr IS a terminal, pass them straight through when it is not.
// A redirected stderr corrupts nothing, and that is also the case every test
// run and every `2>log` invocation takes, so this changes nothing for either.
//
// Bounded, because a resize storm is exactly when this fires: after the cap
// the messages are counted rather than kept, and the count is reported when
// they are flushed. An unbounded buffer would turn a screenful of noise into
// a memory leak that only shows up on a bad day.
namespace {

constexpr int kMaxDeferred = 256;
// Distinct messages, each with how many times it arrived. A repeated message
// is the normal case rather than the exception here: the section 6 contrast
// check runs on EVERY frame and warns for up to eight cells each time, so a
// static screen with one bad colour pair emits the same sentence sixty times
// a second. Storing them flat filled the buffer in under a second and turned
// everything after it into "and N further messages" -- including the ones
// worth reading, the SIGWINCH pipe failing or a widget off the grid.
//
// So the bound is on DISTINCT messages, and a repeat costs a counter. That is
// also the more useful report: "(x420)" beside a contrast warning says the
// colour pair is wrong on every frame, which the flat list said only by
// filling up.
struct Held { QString text; int count = 1; };
QVector<Held> g_deferred;
int g_dropped = 0;
QtMessageHandler g_previous = nullptr;
// The backend that has the screen. Set by the backend itself, in resume() and
// suspend() -- see terminal_owner.h for why those two and not exec(). A fatal
// message has to be printed where somebody can read it, and the alternate
// screen is not that place: see below.
ITerminalBackend *g_backend = nullptr;         // the top of the stack below
QVector<ITerminalBackend *> g_owners;

void deferring_handler(QtMsgType type, const QMessageLogContext &ctx,
                       const QString &text) {
	// A fatal message is the process's LAST words. qFatal() aborts as soon as
	// this returns, so there is no later flush to hold it for, and holding it
	// is the same as deleting it. Measured on a pseudo-terminal, before this:
	//
	//     stderr a pipe        the font refusal printed, exit 134
	//     stderr a terminal    NOTHING printed, exit 134
	//     with a frame up      2746 bytes of screen, no sentence in them
	//
	// which is to say the one diagnostic that explains why a program will not
	// start was invisible to everybody who runs it in a terminal -- and a TUI
	// is run in a terminal. That is section 7.9's own failure with its
	// explanation removed: `grid_font_problem()` refuses correctly and says so
	// to nobody.
	//
	// The screen goes back FIRST where a backend has it, because a message
	// printed onto the alternate screen dies with the alternate screen: the
	// SIGABRT that follows runs qtty_fatal_handler(), which leaves it. That is
	// what the 2746-byte measurement is -- the frame, and then the switch
	// back, taking the sentence with it. suspend() is the call a Ctrl+Z takes
	// and it flushes on its way out, so the held messages go too.
	//
	// g_previous is Qt's own handler and is measured non-null: Qt 6 installs
	// qDefaultMessageHandler explicitly rather than leaving the pointer empty,
	// so qInstallMessageHandler() returns something to fall back to. The guard
	// matches the one below rather than asserting that.
	if (type == QtFatalMsg) {
		if (g_backend) g_backend->suspend();
		flush_deferred_messages();
		if (g_previous) g_previous(type, ctx, text);
		return;
	}
	if (!isatty(2)) {                       // nothing to protect
		if (g_previous) g_previous(type, ctx, text);
		return;
	}
	for (Held &h : g_deferred)
		if (h.text == text) { ++h.count; return; }
	if (g_deferred.size() < kMaxDeferred) g_deferred.append(Held{text, 1});
	else ++g_dropped;
}

} // namespace

// terminal_owner.h: the backend says when it has the screen and when it does
// not. Defined here because the handler that asks is here.
void take_terminal(ITerminalBackend *owner) {
	if (owner && !g_owners.contains(owner)) g_owners.append(owner);
	g_backend = g_owners.isEmpty() ? nullptr : g_owners.last();
}

void release_terminal(ITerminalBackend *owner) {
	g_owners.removeAll(owner);
	g_backend = g_owners.isEmpty() ? nullptr : g_owners.last();
}

void flush_deferred_messages() {
	const QVector<Held> held = g_deferred;
	const int dropped = g_dropped;
	g_deferred.clear();
	g_dropped = 0;
	for (const Held &h : held) {
		if (h.count > 1)
			fprintf(stderr, "%s (x%d)\n", qPrintable(h.text), h.count);
		else
			fprintf(stderr, "%s\n", qPrintable(h.text));
	}
	if (dropped)
		fprintf(stderr, "qtty: and %d further distinct message(s) while the"
		                " terminal was in use\n", dropped);
	fflush(stderr);
}


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

	// Scaling is the third ambient lever, and disabling high-DPI scaling was
	// not enough on its own: QT_SCALE_FACTOR and QT_SCREEN_SCALE_FACTORS
	// override it, and a HiDPI desktop commonly sets one. With either at 2 the
	// line height came out 18.6406 px and setup() refused to start at all --
	// grid_font_problem() doing its job, but the cause was the environment
	// rather than the font, so the program simply would not run there.
	//
	// A cell grid has no device pixel ratio to honour. The terminal decides
	// how big a cell is on screen; qtty needs the metrics to be whole numbers
	// and nothing else. So these are pinned neutral with the other two, and
	// the guard stays for the case it was written for -- a font that genuinely
	// cannot carry the grid.
	qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
	qputenv("QT_SCALE_FACTOR", "1");
	qputenv("QT_SCREEN_SCALE_FACTORS", "");
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
	// Held from here on, and released by the backend when it gives the
	// terminal back. Installed in setup() rather than in the backend because
	// a warning emitted before the first frame is on the same screen.
	g_previous = qInstallMessageHandler(deferring_handler);
	// And said at exit, because a program that never takes a screen never
	// calls suspend() and would otherwise drop them. Measured: a program that
	// calls setup(), warns about a substituted font and returns from main
	// printed nothing at all -- the warning was held for a terminal it never
	// took, and the deferral has no other end.
	//
	// atexit rather than qAddPostRoutine, because the flush has to happen for
	// a plain exit(3) too and not only for QCoreApplication teardown.
	// g_deferred is constructed before this runs, so it is destroyed after --
	// the handler cannot be reading a dead vector. _exit(2) still skips it,
	// which is the abrupt path and is what the control check asserts.
	std::atexit(flush_deferred_messages);
	// Bundled-font provisioning (section 5.3) is later Phase-2 work; DejaVu Sans
	// Mono is the interim source of integral metrics, asserted as designed.
	QFont f(QStringLiteral("DejaVu Sans Mono"));
	f.setPixelSize(16);

	// Hinting is the fourth ambient lever, and it was the one nothing pinned.
	// The first three -- platform, platform theme, scaling -- are environment
	// variables and are pinned in prepare_environment(); this one arrives from
	// the user's fontconfig, which no variable overrides, so it has to be
	// asked for on the font itself.
	//
	// It decides whether the metrics are whole numbers at all. Measured on
	// this machine, same font file, same Qt, DejaVu Sans Mono at pixel size
	// 16, varying only the hinting preference:
	//
	//     PreferFullHinting                  advance 10.0000  height 19.0000
	//     Default/NoHinting/VerticalHinting  advance  9.6250  height 18.6406
	//
	// The second row is what a STOCK Debian gives, because
	// /etc/fonts/conf.d/10-hinting-slight.conf makes hintslight the packaged
	// default -- and against those metrics grid_font_problem() refuses and
	// qFatal() aborts. So every qtty program failed to start for any user who
	// had not, somewhere in their own fontconfig, turned full hinting on.
	// Found by running the suite from a second account on the machine that
	// has always passed it: the tree's 10x19 cell was that account's
	// fontconfig, not a property of the font.
	//
	// Asking for full hinting here is therefore not a rendering preference.
	// It is the same argument as the theme pin one function up -- a terminal
	// program must not inherit the desktop's configuration -- with a sharper
	// edge, because this lever does not change how qtty looks, it decides
	// whether qtty runs. It also costs nothing to what is already recorded:
	// 10x19 is exactly what design.md section 16 measured and what both
	// snapshot fixtures were taken against, so the pin makes the fixtures
	// independent of whose account renders them rather than moving them.
	//
	// The guard below stays, and now guards the case it was written for: a
	// font that cannot carry the grid even when asked properly.
	f.setHintingPreference(QFont::PreferFullHinting);

	// A hard startup error, not a rendering glitch (section 5.3, risk R3). The
	// check this replaced was a Q_ASSERT_X comparing the advance of 'i' with
	// that of 'M': it tested monospace-ness rather than integral metrics, and
	// being an assert it compiled out in release, so no shipping build carried
	// R3's mitigation at all. qFatal is deliberate -- every column the grid
	// computes from here is wrong, and failing at the point of cause is worth
	// more than a screen that degrades a little further with each column.
	// Said BEFORE the refusal below, and that order is the point: a font the
	// grid rejects is often not the font that was asked for, and a message
	// naming the requested family sends the reader to fix something that is
	// not wrong. Both sentences arrive now that a fatal message flushes what
	// was held.
	if (const QString subst = grid_font_substitution(f); !subst.isEmpty())
		qWarning("qtty: %s", qPrintable(subst));
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

	// design.md section 7 promises Tier 1 is free -- "style metrics differ, so the
	// same layout compacts automatically" -- and it was not free for any
	// layout with slack in it, which is most of them. QBoxLayout hands
	// leftover space to its items in shares that are not cell multiples, and
	// what that costs is not untidiness: measured across the suite, content
	// under an off-grid coordinate lands a WHOLE CELL from where it belongs,
	// 35 times.
	//
	// The policy is rounding each edge to the nearest cell, and that it
	// cannot overlap two disjoint siblings is a proof rather than a sample:
	// rounding is monotonic, so a.right + 1 <= b.left survives it. The one
	// case where it does overlap is two widgets inside a single cell, which a
	// cell renderer cannot draw whatever their geometry says.
	//
	// Turning it on was held open as a decision until the effect on a real
	// tree was measured rather than argued. Measured: the whole suite passes
	// with it installed, both snapshot fixtures included, and the only two
	// checks that change are the two written to assert the UNSNAPPED state.
	// Nothing else in 662 moved.
	//
	// It goes here rather than being left to applications for the reason the
	// font enforcer above gives for itself: an invariant the grid depends on
	// is the library's to hold, not a paragraph for every application to
	// obey. setup() is already the call that makes a program a terminal
	// program -- it installs GridStyle and restyles everything -- so a GUI
	// build that never calls it is unaffected, which is section 10.1's
	// inertness rule doing its job rather than being bypassed.
	GridSnap::install(app);
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

// Asked of the backend while a run is in progress, and nothing outside one.
//
// This was a COPY taken at the top of exec(), on the reasoning that a stale
// answer after exec() returns is worse than none. The second half of that is
// right and is why the pointer is cleared below; the first half made the
// answer stale DURING the run as well, which is the case the reasoning was
// not about. `cell_px` is re-read on every SIGWINCH -- read_winch() asks for
// the geometry unconditionally, because a font-size change moves the cell
// without moving the cell COUNT -- so a snapshot taken before the loop
// answered with the size the terminal had at startup for the rest of the
// session, while the transmit path two files away used the new one.
//
// A caller still cannot be handed a stale answer: outside a run the pointer
// is null and the answer is the empty Capabilities, which is what "nothing
// was measured" means everywhere else.
static ITerminalBackend *s_backend = nullptr;
Capabilities capabilities() {
	return s_backend ? s_backend->capabilities() : Capabilities{};
}

int exec(QApplication &app, QWidget &win, ITerminalBackend &backend) {
	s_tuiActive = true;
	s_backend = &backend;

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
	set_focus_widget(win.focusWidget());

	InputRouter router(&win);
	Compositor compositor(&win, &router);
	FrameScheduler scheduler(&backend, &compositor, &win);
	backend.set_event_sink(&router);
	router.frame_requested = [&scheduler] { scheduler.request_frame(); };

	scheduler.render_now();                      // initial frame
	const int rc = app.exec();
	s_tuiActive = false;
	s_backend = nullptr;
	return rc;
}

int exec(QApplication &app, QWidget &win) {
	// The built-in backend, owned for the duration of the run. Everything else
	// happens in the overload above, so the two paths cannot drift.
	AnsiBackend backend;
	return exec(app, win, backend);
}

} // namespace Qtty
