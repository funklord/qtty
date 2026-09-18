// src/runtime/application.cpp -- L6 entry points (section 5.6), wired to the real
// section 5 architecture: AnsiBackend -> InputRouter -> Compositor -> FrameScheduler.
#include "qtty/application.h"
#include "terminal_owner.h"
#include "title_keeper.h"
#include "url_opener.h"
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
// How many times the offscreen platform's size-hint notice was suppressed.
// Reported by flush_deferred_messages(), so the count survives the silence.
int g_offscreen_noise = 0;
QtMessageHandler g_previous = nullptr;
// The backend that has the screen. Set by the backend itself, in resume() and
// suspend() -- see terminal_owner.h for why those two and not exec(). A fatal
// message has to be printed where somebody can read it, and the alternate
// screen is not that place: see below.
ITerminalBackend *g_backend = nullptr;         // the top of the stack below
QVector<ITerminalBackend *> g_owners;
// And the backend exec() was handed, declared here because the handler below
// needs it and defined with capabilities(), where what it is for is written
// out. The two are different questions -- who HAS the screen, and which
// backend is driving this session -- and each answers where the other cannot.
ITerminalBackend *g_session = nullptr;

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
		// Whoever can put the screen back: the backend that HAS it, and
		// failing that the one exec() was handed. Only qtty's own
		// AnsiBackend can register ownership -- take_terminal() is internal
		// and does not ship -- so a backend an application wrote reached
		// this line as a null pointer, and the message it exists to rescue
		// was printed onto the alternate screen after all. That is the
		// measured 2746-byte failure above, available again by the one
		// route the fix did not cover.
		//
		// Suspending a backend with no screen costs nothing: suspend() is
		// the interface's own "give it back" call, NullBackend's is empty,
		// and the alternative is a program's last words going missing.
		if (ITerminalBackend *owner = g_backend ? g_backend : g_session)
			owner->suspend();
		flush_deferred_messages();
		if (g_previous) g_previous(type, ctx, text);
		return;
	}
	// The offscreen platform's own noise, which qtty CAUSES BY DESIGN and
	// which no caller can act on. It says the plugin does not propagate size
	// hints; qtty chose that plugin precisely because there is no window
	// manager to propagate them to, and section 7's policy resizes layouts
	// below their minimum as a matter of routine -- which is what emits it.
	//
	// Counted and explained once rather than repeated. On a TERMINAL the
	// branch below already coalesces duplicates, so a hundred of these became
	// one held line; with stderr REDIRECTED every one passed through, and
	// that is the case every measurement takes. It has cost this project two
	// wrong check counts -- 744 and 745 from one binary, the warning landing
	// mid-line and cutting a PASS in half -- and the remedy so far has been
	// `2>/dev/null` repeated in count-check, in the sabotage harness and in
	// section 0c. Fixing it here retires all three.
	//
	// Not silently: the first one is printed with a note that the rest are
	// counted, and the total is reported when messages are flushed. A
	// diagnostic that vanishes without trace is the opposite defect.
	if (text == QLatin1String("This plugin does not support "
	                          "propagateSizeHints()")) {
		if (g_offscreen_noise++ == 0 && g_previous)
			g_previous(type, ctx,
			           QStringLiteral("qtty: the offscreen platform does not "
			                          "propagate size hints, which it says "
			                          "whenever a layout is asked to shrink "
			                          "below its minimum. Further identical "
			                          "lines are counted, not printed."));
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
	// The suppressed platform notice, reported as a number so that silencing
	// it does not also lose it. Reset with the rest, so a later flush counts
	// only what happened since this one.
	if (g_offscreen_noise > 1)
		fprintf(stderr, "qtty: the offscreen size-hint notice was emitted"
		                " %d time(s)\n", g_offscreen_noise);
	g_offscreen_noise = 0;
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
	QByteArray platform = want.isEmpty() ? QByteArray("offscreen") : want;

	// And the offscreen platform is given a SCREEN nothing can exceed.
	//
	// Its default is 800x800, which at a 10x19 cell is 80 columns by 42
	// rows -- and Qt clamps a menu, a completer popup and a modal dialog's
	// centring into screen()->availableGeometry(). A maximised terminal on
	// an ordinary display is around 200x55, so the clamp is not an edge
	// case, it is where most users are. Measured: a menu asked for column
	// 150 was placed at column 66, and the compositor anchors a popup at
	// its own geometry, so it was DRAWN detached from the item that opened
	// it. Nothing in this library saw the move; Qt had already made it.
	//
	// A screen nothing can exceed puts every placement decision back here,
	// which is where the flipping and pagination already live and the
	// regime they were measured in.
	//
	// Attached whenever the platform IS offscreen, which is the condition
	// that matters -- not whether this library chose it. QTTY_QPA_PLATFORM
	// is the escape hatch for running under xcb, where an offscreen option
	// would be nonsense; but the suite sets that same variable to
	// "offscreen", and a fix the suite cannot reach is one nothing holds.
	// A value carrying options of its own is left exactly as given.
	//
	// The plugin qFATALS on a configfile it cannot read, so the file is
	// written first and the option appended only if that worked -- a
	// temporary file this process cannot create must degrade to the small
	// screen, not take the program down. It is a function-local static so
	// it lives as long as the process: the plugin reads it inside the
	// QApplication constructor, which has not run yet.
	if (platform == "offscreen") {
		static QTemporaryFile *config = [] () -> QTemporaryFile * {
			auto *f = new QTemporaryFile(
			    QDir::tempPath() + QStringLiteral("/qtty-screen-XXXXXX.json"));
			if (!f->open()) { delete f; return nullptr; }
			f->write("{\"screens\":[{\"name\":\"qtty\",\"x\":0,\"y\":0,"
			         "\"width\":10000,\"height\":10000,"
			         "\"logicalDpi\":96,\"logicalBaseDpi\":96,\"dpr\":1}]}");
			if (!f->flush()) { delete f; return nullptr; }
			return f;
		}();
		if (config)
			platform += ":configfile=" + QFile::encodeName(config->fileName());
	}
	qputenv("QT_QPA_PLATFORM", platform);

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

// Keeps GridStyle on top of whatever style the application installs. See the
// installEventFilter call in setup() for why this exists rather than a note
// in the documentation.
namespace {
class StyleKeeper : public QObject {
public:
	explicit StyleKeeper(QObject *parent) : QObject(parent) {}
	bool eventFilter(QObject *o, QEvent *e) override {
		if (e->type() != QEvent::StyleChange || rewrapping_)
			return QObject::eventFilter(o, e);
		QStyle *const now = QApplication::style();
		// dynamic_cast rather than qobject_cast: GridStyle carries no
		// Q_OBJECT, and this tree already detects PixelSurface the same way
		// and for the same reason -- a Q_OBJECT here would put moc in the
		// path of a class an application subclasses.
		if (!now || dynamic_cast<GridStyle *>(now))
			return QObject::eventFilter(o, e);
		// Wrap what the application chose. setStyle() adopts the argument
		// and deletes the previous style, so the base has to be one Qt is
		// not about to delete: take a fresh instance of the same key rather
		// than the live pointer, which setStyle() would destroy underneath
		// the proxy that had just been given it.
		rewrapping_ = true;
		QStyle *const base = QStyleFactory::create(now->name());
		QApplication::setStyle(new GridStyle(
		    base ? base : QStyleFactory::create(QStringLiteral("Fusion"))));
		rewrapping_ = false;
		return QObject::eventFilter(o, e);
	}
private:
	bool rewrapping_ = false;
};
} // namespace

// ---- a link the application offers, and a terminal with no browser --------
//
// QDesktopServices::openUrl() is what an ordinary Qt program calls from a
// Help -> Website action, from an About box, from QLabel::linkActivated and
// from QTextBrowser::anchorClicked. Measured with a standalone program under
// the offscreen platform prepare_environment() pins, before any of this was
// written:
//
//     openUrl("https://...")              false, and a qWarning
//     openUrl("mailto:...")               false, and a qWarning
//     with a setUrlHandler registered     TRUE, and the handler got the QUrl
//     a scheme with no handler            still false
//
// The warning is Qt's own -- "This plugin does not support
// QPlatformServices::openUrl() for '<url>'" -- and qtty makes it QUIETER
// than bare offscreen would. The deferring handler at the top of this file
// holds it while stderr is the terminal and flushes it at exit, which is
// right for every other diagnostic and is exactly wrong for this one: the
// user presses Enter on a link, no frame changes, nothing happens, and the
// only explanation arrives at the shell prompt after the program has gone.
//
// That the registry intercepts at all is the whole reason this is fixed
// here rather than application by application: an unmodified program keeps
// the standard spelling and starts working. Compare the tray and the drag,
// where an adopter has to write something.
//
// WHY setup() AND NOT exec(). The documented call order is
// prepare_environment() -> QApplication -> setup() -> build the widgets ->
// exec(), so a handler installed here is in place BEFORE any application
// code that might install one of its own -- and Qt keeps one handler per
// scheme, the last registration winning. An application that wants
// something else therefore gets it by writing ordinary Qt, with no qtty API
// to learn and nothing to switch off. Installing from exec() would reverse
// that and silently override the application.
//
// WHY http AND https ONLY. Those two have an answer that is right on every
// terminal, because a link is text and text is what a terminal carries.
// mailto: and file: have no such answer -- a mail composer is a different
// program, and a file: URL on a remote host names a file the user is not
// looking at -- so they fall through to Qt's false, which an application
// can see and act on. A guess made on its behalf is worse than a refusal it
// can read.
//
// WHY NOT LAUNCH A BROWSER. shell_out() plus xdg-open is the obvious answer
// and it is wrong in both of the situations a terminal program is actually
// in: over ssh it opens a browser on the WRONG MACHINE, and on a headless
// host it opens one nobody can see. qtty cannot tell either state from the
// inside -- DISPLAY says whether this machine has a display, not whether
// the terminal is on it -- so the choice is not between a good mechanism
// and a safe one. It is between a mechanism that is right everywhere and
// one that is right where its author happened to be sitting.
//
// WHY THE CLIPBOARD. It is the only mechanism available here with that
// property. AnsiBackend::watch_clipboard() forwards a QClipboard change as
// OSC 52, and the terminal EMULATOR executes it -- on the user's own
// machine, at the far end of the ssh connection -- so the link lands where
// the user's browser is rather than where the process is. That path is
// already measured through tmux and over ssh.
//
// WHY A DIALOG, AND WHY IT IS NOT INTRUSIVE. On a desktop this same call
// opens an ENTIRE BROWSER WINDOW over whatever the user was doing. A box
// naming the link is less than the platform contract rather than more --
// and since the defect being fixed is an activation that changes nothing on
// screen, silence is the one answer that cannot be right.
namespace {

// The object QDesktopServices hands the link to. A named class with a slot
// rather than a lambda, because the registration has only one shape:
// setUrlHandler() takes a receiver and a method NAME and invokes it through
// the meta-object, so the receiver has to be moc'd. That is what the .moc
// at the bottom of this file is for -- the arrangement tray.cpp already
// uses, and the reason this file has one at all.
class UrlOpener : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
public slots:
	void open_url(const QUrl &url);
};

// How wide the box's text may be, in characters.
//
// Measured rather than chosen: the first version of this dialog handed
// QMessageBox one 104-character line and came out 1115 pixels -- 111 cells,
// wider than the 80-column terminal it would have been drawn on, with the
// difference clipped. GridGuard printed the geometry; nothing else would
// have said a word, because the box is correct on a desktop and correct on
// a wide terminal.
//
// The terminal's own width when a backend is driving, which is the case
// that matters, and 80 otherwise -- the width a terminal has when nobody
// has said. Ten cells come off for what is not text: the icon, the frame
// and the margins, which measured about seven and a half above.
int message_columns() {
	const QSize cells = terminal_cells();
	return qMax(16, (cells.width() > 0 ? cells.width() : 80) - 10);
}

// Fold `text` to `cols` columns, breaking at a space where it can and
// through a word where it cannot.
//
// The second half is the reason this is here rather than QLabel's own word
// wrap: a URL is ONE WORD, so a wrapping label leaves it whole and makes
// the box as wide as the link. Breaking it looks worse than eliding and is
// the better answer here, because the clipboard already holds the URL
// exactly -- what is on the screen is for reading, and all of it is
// readable folded. An elision would be the only copy of the link the user
// can see, with the middle missing.
QString fold_to_columns(const QString &text, int cols) {
	const int width = qMax(8, cols);
	QStringList out;
	// Paragraphs first, so the blank line the message asks for survives.
	const QStringList paragraphs = text.split(QLatin1Char('\n'));
	for (const QString &para : paragraphs) {
		const QStringList words =
		    para.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		if (words.isEmpty()) { out.append(QString()); continue; }
		QString line;
		for (QString word : words) {
			while (word.size() > width) {
				if (!line.isEmpty()) { out.append(line); line.clear(); }
				out.append(word.left(width));
				word = word.mid(width);
			}
			if (line.isEmpty()) line = word;
			else if (line.size() + 1 + word.size() <= width)
				line += QLatin1Char(' ') + word;
			else { out.append(line); line = word; }
		}
		if (!line.isEmpty()) out.append(line);
	}
	return out.join(QLatin1Char('\n'));
}

void UrlOpener::open_url(const QUrl &url) {
	// toString(), not toDisplayString(): what goes on the clipboard has to
	// be what the user pastes into a browser, so the percent-encoding stays
	// and nothing is prettified out of it.
	const QString text = url.toString();
	if (QClipboard *board = QGuiApplication::clipboard())
		board->setText(text);
	// QUEUED, and this is the load-bearing half of the design rather than a
	// tidy-up. openUrl() returns immediately on every desktop platform and
	// an application is entitled to code written against that: it may
	// activate a link from inside a slot that is part-way through
	// something, or from a nested loop of its own. Running a modal's nested
	// event loop inside this function would change what openUrl() promises
	// its caller and can re-enter whatever called it. So the handler copies,
	// posts, and returns at once; the box goes up on the next turn of
	// whichever loop is running.
	//
	// Captured by VALUE. The QUrl belongs to the caller and the box is built
	// on a later turn, by which time a reference would name whatever has
	// since replaced it on the caller's stack.
	QTimer::singleShot(0, qApp, [text] {
		// Parented to the modal that is up, where there is one, so a link
		// inside an About box does not put its answer behind the box the
		// user clicked it in. Read when the timer fires rather than when
		// the link was activated: the dialog may have closed in between,
		// and the right parent is then nobody -- which the compositor
		// already handles by centring an unparented dialog in the terminal.
		const QString body = fold_to_columns(
		    text + QStringLiteral("\n\nCopied to the clipboard. A terminal"
		                          " has no browser of its own, so paste it"
		                          " wherever you want it opened."),
		    message_columns());
		auto *box = new QMessageBox(
		    QMessageBox::Information, QStringLiteral("Link"), body,
		    QMessageBox::Ok, QApplication::activeModalWidget());
		box->setAttribute(Qt::WA_DeleteOnClose);
		// show() with the modality set rather than exec(), for the reason
		// the queue exists: exec() would run a nested event loop here and
		// block whatever code the timer interrupted. Enter and Escape both
		// dismiss it without a line of key handling -- QMessageBox makes a
		// lone Ok button both the default and the escape button -- and the
		// suite asserts that rather than this comment asserting it.
		box->setWindowModality(Qt::ApplicationModal);
		box->show();
	});
}

} // namespace

void install_url_handlers() {
	// One object for the life of the application, and parented to it.
	// QDesktopServices keeps the receiver as a plain pointer and drops the
	// registration when it is destroyed, so a handler whose receiver has
	// gone is a scheme that silently stopped working -- the reason this is
	// neither a local nor a leak.
	static QPointer<UrlOpener> opener;
	if (!opener) opener = new UrlOpener(qApp);
	QDesktopServices::setUrlHandler(QStringLiteral("http"), opener, "open_url");
	QDesktopServices::setUrlHandler(QStringLiteral("https"), opener, "open_url");
}

bool shell_out(const std::function<void()> &body) {
	// The backend that currently has the screen, which is the same thing the
	// fatal path asks for a few lines above. A STACK rather than a pointer
	// because one backend may be constructed inside another's lifetime, and
	// the top is the one drawing -- terminal_owner.h says why at length.
	ITerminalBackend *owner = g_backend;
	if (owner) owner->suspend();
	// Restored by a destructor rather than by a line after the call. `body`
	// is the caller's code and may throw; a terminal left on the alternate
	// screen in raw mode with the cursor hidden is the damage this library
	// takes most trouble to prevent, and it would be reintroduced here by
	// the one path nobody tests.
	struct Retake {
		ITerminalBackend *b;
		~Retake() { if (b) b->resume(); }
	} retake{owner};
	// Run whatever happened above: a program whose output is a pipe has no
	// terminal to hand over and its work should still be done. That is what
	// the false return is for -- it reports what was handed over, not
	// whether the body ran.
	body();
	return owner != nullptr;
}

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
	// Mono is the interim source of integral metrics, asserted as designed --
	// and now the default rather than the only answer. grid_font_request()
	// reads the application's set_font() and the user's QTTY_FONT, in that
	// order, and says why in grid.h.
	QFont f = grid_font_request();

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
	// Asked for in grid_font_request() now, so that a font named by an
	// application or by the environment is hinted the same way the default
	// is -- the lever decides whether metrics are integral, and a font
	// chosen elsewhere needs it at least as much. Repeated here costs
	// nothing and keeps this function readable on its own.
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
	// Said before the refusal below for the same reason the substitution is:
	// a font that is about to be refused should not also be reported for its
	// leading, and a font that is merely odd should say so while it still can.
	if (const QString lead = grid_font_leading(f); !lead.isEmpty())
		qWarning("qtty: %s", qPrintable(lead));
	if (const QString problem = grid_font_problem(f); !problem.isEmpty()) {
		qFatal("qtty: the grid needs a font with integral metrics: %s",
		       qPrintable(problem));
	}

	// The fifth ambient lever, and it is read from the user's desktop rather
	// than from anything qtty pins. Qt flashes a text caret every
	// cursorFlashTime -- 1000 ms on a stock desktop -- and on a terminal the
	// caret IS the terminal's own cursor, which the backend places with
	// ESC[?25h. So every repaint the blink causes is a frame nobody can see:
	// the widget is asked to paint, the compositor composes, the diff finds
	// the one caret cell changed, and the terminal is told to paint a block
	// under a cursor it is already drawing.
	//
	// Measured on the chat example, one keystroke and then nothing touched:
	//
	//     +   5.1 ms  234 bytes   the keystroke
	//     + 481.5 ms   33 bytes   the caret cell cleared
	//     + 953.1 ms   38 bytes   and painted again
	//     +1430.6 ms   33 bytes
	//
	// on for ever at half the flash interval, about 75 bytes a second, for a
	// program sitting untouched. Over ssh that is a link that can never go
	// idle after the user has typed one character.
	//
	// It starts on the KEYSTROKE and not on focus, which is why this was
	// missed: section 8.112 measured a focused QLineEdit over three idle
	// seconds, found zero frames, and concluded that the blink cannot start
	// because Qt only blinks for a widget in an ACTIVE window and no window
	// activates here. The first half of that is true and the conclusion is
	// not -- the probe never typed into the field, so it never reached the
	// state that starts the timer.
	//
	// Zero rather than a smaller number: Qt documents 0 as "do not flash",
	// and a caret that is drawn steadily is right here, since the terminal's
	// cursor is what the user actually sees.
	QApplication::setCursorFlashTime(0);

	const QFontMetrics fm(f);
	GridMetrics::set(fm.horizontalAdvance(u'M'), fm.height());
	app.setFont(f);
	auto *fonts = new FontEnforcer;
	fonts->base = f;
	fonts->setParent(&app);
	app.installEventFilter(fonts);
	app.setStyle(new GridStyle);
	// And put it back if the application installs one of its own.
	//
	// QApplication::setStyle() REPLACES, so a program that sets a style
	// after setup() -- to supply toolbar icons, say, which is an ordinary
	// thing for a Qt program to want -- deleted GridStyle and with it every
	// Channel A drawing in the program. Silently, everywhere at once, and
	// with nothing to attribute it to: a surveyed sibling does exactly this
	// today.
	//
	// GridStyle is a QProxyStyle, so the answer is to WRAP rather than to
	// refuse: the application's style becomes the base, GridStyle answers
	// what it knows about cells, and everything else falls through to the
	// style the application asked for. It keeps its icons and its hints and
	// the terminal keeps its drawing.
	//
	// Watched through QEvent::StyleChange, which Qt sends to every widget
	// when the application style changes -- there is no application-level
	// signal for it. The guard is not optional: setting the style inside the
	// handler sends another round of the same event.
	app.installEventFilter(new StyleKeeper(&app));
	// Lets an ICellPainted widget paint itself in cells instead of going
	// through Channel B (section 5.3, risk R5). Inert in a GUI build by
	// construction: with no cell device being rendered into, the filter stands
	// down and the widget paints normally.
	install_cell_paint_filter(app);
	set_theme(CellTheme::terminal_default());

	// A link the application offers is answered rather than dropped on the
	// floor. What that answer is, and why it is installed from HERE rather
	// than from exec(), is written out above UrlOpener -- in short, an
	// application registering a handler of its own afterwards wins, and
	// that ordering is the escape hatch.
	install_url_handlers();

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
// True while a backend is driving, which is NOT the same question as whether
// exec() is on the stack. backend.h supports an application running its own
// frame loop -- qtty-replay --ansi is one, and an adopted TUI codebase is the
// case the interface exists for -- and until this fell back to the ownership
// record, such a program answered false while a real AnsiBackend had the
// alternate screen. Overlay reads this to decide whether the runtime is
// compositing (overlay.cpp), so the false answer built a GUI twin window for
// a session whose frames qtty was already drawing.
bool is_tui_active() { return s_tuiActive || g_backend != nullptr; }

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
// And the same fall-back, for the same seat. This asked exec()'s pointer
// alone, so an application driving its own loop -- the one arrangement that
// has no exec() to ask -- got the empty Capabilities, which reads as "nothing
// was negotiated" while the backend beside it had negotiated everything. The
// two records answer different questions and both are needed: exec() knows
// which backend is DRIVING THIS SESSION and goes on knowing it while the
// screen is handed to a child, and the ownership stack knows who HAS THE
// SCREEN when no exec() was involved.
Capabilities capabilities() {
	const ITerminalBackend *b = g_session ? g_session : g_backend;
	return b ? b->capabilities() : Capabilities{};
}

// The same two records, asked the same way and in the same order, because
// the question is the same one: which backend is driving this program. What
// differs is only that the answer is read live rather than out of a
// negotiated snapshot -- see application.h for why it is not a field on
// Capabilities.
QSize terminal_cells() {
	const ITerminalBackend *b = g_session ? g_session : g_backend;
	return b ? b->size() : QSize();
}

int exec(QApplication &app, QWidget &win, ITerminalBackend &backend) {
	s_tuiActive = true;
	g_session = &backend;

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

	// The window's title becomes the terminal's, now and whenever it
	// changes. An unmodified Qt application already calls
	// setWindowTitle() -- a QMainWindow does it for the document it has
	// open -- so this asks nothing new of one, which is the whole premise
	// of the library.
	TitleKeeper titles(win, backend);

	scheduler.render_now();                      // initial frame
	const int rc = app.exec();
	s_tuiActive = false;
	g_session = nullptr;
	return rc;
}

int exec(QApplication &app, QWidget &win) {
	// The built-in backend, owned for the duration of the run. Everything else
	// happens in the overload above, so the two paths cannot drift.
	AnsiBackend backend;
	return exec(app, win, backend);
}

} // namespace Qtty

// UrlOpener's meta-object. QDesktopServices invokes a handler by method
// NAME, so the receiver has to be moc'd, and a class declared in a .cpp is
// moc'd by including the output here -- the arrangement tray.cpp uses.
#include "application.moc"
