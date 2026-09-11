// qtty-replay -- scripted input -> text frames (section 9). Makes bug reports
// reproducible: a script drives the UI through the real InputRouter and each
// `frame` line emits the composed cell buffer.
//
// Script (stdin or file argument), one command per line:
//   text <string>     type characters
//   key <spec>        a key with optional ctrl+/alt+/shift+ modifiers;
//                     see key_map() below and `qtty-replay --help`
//   ctrl <letter>     e.g. "ctrl s"
//   click <col> <row> mouse press+release at cell
//   resize <c> <r>    resize the terminal through on_resize()
//   conventions on|off  the opt-in terminal key habits
//   frame             print the composed frame between markers
//   snapshot          the same, with attributes -- what `frame` cannot show
//
// --ansi: emit the raw ANSI/graphics byte stream through the real
// AnsiBackend instead of text frames -- a deterministic corpus for terminal
// parser testing (doc/beerssh.md section 4). Combine with QTTY_GRAPHICS to force a
// graphics tier into the stream.
//
// Drives the built-in sample UI; applications link libqtty and reuse
// InputRouter/Compositor the same way for their own screens.
//
// The sample carries a menu bar, a tab widget, a button, a line edit and a
// list, and `window` opens a second top-level -- one target for each key the
// library answers, so a report about any of them can be reproduced here:
//
//   key alt+f              the File menu, by mnemonic
//   conventions on         then key return on the focused Send button
//   key ctrl+pagedown      across the tabs
//   window, key f6         between the windows
//
// Both actions change something a frame can see: Send moves the field's text
// into the list, and the menu items clear and fill the field.
#include <qtty/qtty.h>
#include <qtty/version.h>
#include "../../src/backend/ansi/ansi_backend.h"
#include <QtWidgets>
#include <QTextStream>
#include <cstdio>
#include <memory>

using namespace Qtty;

// Lifted out of key_by_name() so --help can ASK it. The header used to list
// the names in a comment and the help repeated them, and both were wrong the
// same way: neither mentioned "enter", and the comment predated pageup and
// pagedown. A list the program owns and prose repeats is a copy waiting to be
// wrong -- which is what this guide tells applications about key hints, so a
// tool of ours keeping its own second copy was poor advertising.
static const QHash<QString, int> &key_map() {
	static const QHash<QString, int> map = {
		{"tab", Qt::Key_Tab}, {"return", Qt::Key_Return}, {"enter", Qt::Key_Return},
		{"backspace", Qt::Key_Backspace}, {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
		{"left", Qt::Key_Left}, {"right", Qt::Key_Right},
		{"pageup", Qt::Key_PageUp}, {"pagedown", Qt::Key_PageDown},
		// Added because the library answers all of these and a script
		// could not send one of them: Escape is the way back out of a
		// layer, F6 moves between windows, Menu opens a context menu,
		// and none could be reproduced in a bug report.
		{"escape", Qt::Key_Escape}, {"esc", Qt::Key_Escape},
		{"menu", Qt::Key_Menu}, {"home", Qt::Key_Home},
		{"end", Qt::Key_End}, {"delete", Qt::Key_Delete},
		{"insert", Qt::Key_Insert}, {"space", Qt::Key_Space},
		{"f1", Qt::Key_F1}, {"f2", Qt::Key_F2}, {"f3", Qt::Key_F3},
		{"f4", Qt::Key_F4}, {"f5", Qt::Key_F5}, {"f6", Qt::Key_F6},
		{"f7", Qt::Key_F7}, {"f8", Qt::Key_F8}, {"f9", Qt::Key_F9},
		{"f10", Qt::Key_F10}, {"f11", Qt::Key_F11},
		{"f12", Qt::Key_F12},
	};
	return map;
}

static int key_by_name(const QString &n) {
	return key_map().value(n.toLower(), 0);
}

// `key ctrl+pagedown`, `key shift+tab`, `key alt+f`. The modifiers were
// missing entirely and the library answers all three: Alt reaches a
// mnemonic, Shift+Tab walks backwards, Ctrl+PageDown steps a tab. A tool
// for reproducible bug reports could not reproduce any report about them.
//
// A single letter after the modifiers becomes its key AND its text,
// because that is what a terminal delivers and what the router's mnemonic
// matching reads -- Alt+F arrives as ESC then 'f', so the letter is in the
// event. Withholding it from widgets that type is the router's job, not
// this tool's.
static Qtty::KeyEvent key_from_spec(const QString &spec) {
	Qtty::KeyEvent k;
	QStringList parts = spec.toLower().split(QLatin1Char('+'));
	const QString name = parts.takeLast();
	for (const QString &m : parts) {
		if (m == QLatin1String("ctrl"))  k.ctrl = true;
		else if (m == QLatin1String("alt"))   k.alt = true;
		else if (m == QLatin1String("shift")) k.shift = true;
	}
	if (const int mapped = key_by_name(name)) {
		k.qt_key = mapped;
	} else if (name.size() == 1 && name.at(0).isLetter()) {
		k.qt_key = Qt::Key_A + (name.at(0).unicode() - 'a');
		k.text = name;
	}
	return k;
}

static const char *const usage =
    "qtty-replay -- scripted input to text frames, so a bug report is"
    " reproducible.\n"
    "\n"
    "usage: qtty-replay [--ansi] [--help] [--version] [script]\n"
    "\n"
    "Reads the script from a file argument or stdin, one command per line,\n"
    "and drives the built-in sample UI through the real InputRouter:\n"
    "\n"
    "  text <string>      type characters\n"
    "  key <spec>         a key, with optional modifiers joined by +:\n"
    "                     key tab, key shift+tab, key ctrl+pagedown,\n"
    "                     key alt+f, key escape, key f6, key menu.\n"
    "                     A single letter becomes its key and its text,\n"
    "                     which is what a terminal delivers. Names below\n"
    "  ctrl <letter>      e.g. \"ctrl s\"\n"
    "  click <col> <row>  mouse press and release at a cell\n"    "  window             open a second top-level window, so F6 has\n"
    "                     somewhere to go. It is not open at the start,\n"
    "                     because two windows put a strip in row 0 of\n"
    "                     every frame and that is every script's output\n"
    "  conventions on|off  turn the terminal keyboard conventions on, as\n"
    "                     an application does with\n"
    "                     Qtty::set_keyboard_conventions(). They are off\n"
    "                     by default here, as they are in the library\n"
    "  resize <cols> <rows>  resize the terminal, through the same sink a\n"
    "                     SIGWINCH reaches, so optional widgets drop and\n"
    "                     the view re-follows the focus as they would\n"
    "  frame              print the composed frame between markers\n"
    "  snapshot           the same with attributes, which frame cannot show\n"
    "\n"
    "  --ansi             emit the raw ANSI and graphics byte stream through\n"
    "                     the real backend instead of text frames, which is a\n"
    "                     deterministic corpus for terminal parser testing.\n"
    "                     Set QTTY_GRAPHICS to force a graphics tier into it.\n";

int main(int argc, char **argv) {
	// Before QApplication, for the reason the same block in qtty-negotiate
	// gives: these must answer in a pipe and with no terminal present.
	for (int i = 1; i < argc; ++i) {
		if (!qstrcmp(argv[i], "--help") || !qstrcmp(argv[i], "-h")) {
			printf("%s", usage);
			QStringList names = key_map().keys();
			names.sort();
			printf("\nkey names: %s\n",
			       qPrintable(names.join(QLatin1Char(' '))));
			return 0;
		}
		if (!qstrcmp(argv[i], "--version") || !qstrcmp(argv[i], "-V")) {
			printf("qtty-replay %s\n%s\n", version_string, copyright);
			return 0;
		}
	}
	prepare_environment();
	QApplication app(argc, argv);
	// An unrecognised option was IGNORED, and the tool then did its
	// ordinary work and exited 0 -- so `--probs` for `--probes` reads as
	// "the probes are off" rather than as a typo, in a program whose whole
	// job is telling somebody what is really happening.
	//
	// AFTER QApplication, deliberately: Qt removes the arguments it
	// recognises (-platform, -style and the rest) from argv, so what is
	// left is this program's to judge. Doing it before would refuse
	// perfectly good Qt options.
	for (int i = 1; i < argc; ++i) {
		static const char *const known[] = { "--help", "-h", "--version", "-V", "--ansi" };
		if (argv[i][0] != '-') continue;
		bool ok = false;
		for (const char *k : known) ok = ok || !qstrcmp(argv[i], k);
		if (ok) continue;
		fprintf(stderr, "qtty-replay: unknown option '%s'\n"
		        "try 'qtty-replay --help'\n", argv[i]);
		return 2;
	}
	setup(app);

	// The sample UI. Replace it with your screen when you link the library;
	// what it is FOR here is giving every key the library answers something
	// to aim at, which it did not.
	//
	// It was a line edit and a list, and four of the six opt-in conventions
	// could not be driven from a script at all for want of a target: Enter on
	// a focused BUTTON, Ctrl+PageUp and PageDown across a TAB WIDGET, F6
	// between two WINDOWS, and Alt+letter against a MENU BAR. A tool for
	// reproducible bug reports could not reproduce a report about any of
	// them.
	//
	// The line edit keeps its place and its focus deliberately: the replay
	// gate asserts that `text hi` reaches a frame and that `ctrl a` changes
	// the snapshot without changing the glyphs, which are claims about THIS
	// widget being focused at the start. It lives in the first tab page now,
	// which is visible from the first frame, so those hold.
	QWidget win;
	auto *v = new QVBoxLayout(&win);
	v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);

	// A menu bar, for Alt+letter. Its items carry mnemonics too, so a bare
	// letter in the open menu reaches them the way a desktop's does.
	auto *bar = new QMenuBar(&win);
	QMenu *file_menu = bar->addMenu(QStringLiteral("&File"));
	auto *edit_menu = bar->addMenu(QStringLiteral("&Edit"));
	v->addWidget(bar);

	auto *tabs = new QTabWidget(&win);
	tabs->setDocumentMode(true);
	auto *page_one = new QWidget;
	auto *pv = new QVBoxLayout(page_one);
	pv->setContentsMargins(0, 0, 0, 0); pv->setSpacing(0);
	auto *edit = new QLineEdit(page_one);
	edit->setPlaceholderText("type here");
	auto *send = new QPushButton(QStringLiteral("&Send"), page_one);
	pv->addWidget(edit); pv->addWidget(send);
	tabs->addTab(page_one, QStringLiteral("&One"));
	auto *page_two = new QWidget;
	auto *pv2 = new QVBoxLayout(page_two);
	pv2->addWidget(new QLabel(QStringLiteral("second page"), page_two));
	tabs->addTab(page_two, QStringLiteral("&Two"));
	v->addWidget(tabs);

	auto *list = new QListView(&win);
	auto *model = new QStringListModel(&win);
	QStringList rows;
	for (int i = 0; i < 40; ++i) rows << QStringLiteral("item %1").arg(i);
	model->setStringList(rows);
	list->setModel(model);
	list->setFrameShape(QFrame::NoFrame);
	list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	v->addWidget(list, 1);

	// Both actions change something a frame can SEE, because a script that
	// fires one has to be able to tell that it did: an action whose only
	// evidence is that no error appeared proves nothing.
	QObject::connect(send, &QPushButton::clicked, [&] {
		if (edit->text().isEmpty()) return;
		QStringList now = model->stringList();
		now.prepend(edit->text());
		model->setStringList(now);
		edit->clear();
	});
	QAction *clear = file_menu->addAction(QStringLiteral("&Clear the field"));
	QObject::connect(clear, &QAction::triggered, [&] { edit->clear(); });
	QAction *fill = edit_menu->addAction(QStringLiteral("&Fill the field"));
	QObject::connect(fill, &QAction::triggered,
	                 [&] { edit->setText(QStringLiteral("filled")); });
	// Titled, because the window strip names each window and an untitled one
	// comes out as "QWidget 1" -- which is Qt's fallback and reads like a
	// fault in the strip rather than a window nobody named.
	win.setWindowTitle(QStringLiteral("replay"));
	win.setAttribute(Qt::WA_DontShowOnScreen);
	// The script's current terminal size. Fixed at 48x14 until now, so a
	// report about a layout that breaks when the terminal is resized could
	// not be reproduced with this tool at all -- and resize is where the
	// small-terminal behaviour lives: optional widgets are dropped, and
	// what is left scrolls to follow the focus.
	int term_cols = 48, term_rows = 14;
	win.resize(GridMetrics::cells(term_cols, term_rows));
	win.show();
	edit->setFocus();
	QCoreApplication::processEvents();

	InputRouter router(&win);
	Compositor comp(&win, &router);
	std::unique_ptr<QWidget> second;          // the `window` command builds it

	bool ansi = false;
	QString script_path;
	for (int i = 1; i < argc; ++i) {
		if (!qstrcmp(argv[i], "--ansi")) ansi = true;
		else script_path = QString::fromLocal8Bit(argv[i]);
	}
	std::unique_ptr<AnsiBackend> backend;
	if (ansi) backend = std::make_unique<AnsiBackend>();

	QFile file;
	if (!script_path.isEmpty()) { file.setFileName(script_path); file.open(QIODevice::ReadOnly); }
	else file.open(stdin, QIODevice::ReadOnly);
	QTextStream in(&file);

	int frame_no = 0;
	while (!in.atEnd()) {
		const QString line_raw = in.readLine();
		const QString line = line_raw.trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
		const QStringList parts = line.split(QLatin1Char(' '));
		const QString cmd = parts[0].toLower();
		if (cmd == QLatin1String("text") && parts.size() > 1) {
			const QString t = line_raw.mid(line_raw.indexOf(QLatin1Char(' ')) + 1);
			for (const QString &cl : to_clusters(t))
				router.on_key({0, cl, false, false, false});
		} else if (cmd == QLatin1String("key") && parts.size() == 2) {
			router.on_key(key_from_spec(parts[1]));
		} else if (cmd == QLatin1String("ctrl") && parts.size() == 2) {
			router.on_key({Qt::Key_A + (parts[1].at(0).toLower().unicode() - 'a'),
				          QString(), true, false, false});
		} else if (cmd == QLatin1String("conventions")
		           && parts.size() == 2) {
			// The terminal habits are OPT-IN, so an application that
			// asked for them and an application that did not answer
			// different keys -- and this tool could only ever be the
			// second. A report that Down does not move focus, or that
			// F6 does not change window, was unreproducible with it for
			// exactly the applications those reports come from.
			Qtty::set_keyboard_conventions(
			    parts[1].compare(QLatin1String("off"),
			                     Qt::CaseInsensitive) != 0);
		} else if (cmd == QLatin1String("window")) {
			// A SECOND top-level, on demand rather than at startup. F6
			// moves between windows and there was only ever one, so that
			// convention could not be driven at all -- and creating it
			// up front would put the window strip in row 0 of every
			// frame this tool has ever printed, which is a change to
			// every existing script's output for the sake of one key.
			if (!second) {
				second = std::make_unique<QWidget>();
				second->setAttribute(Qt::WA_DontShowOnScreen);
				second->setWindowTitle(QStringLiteral("second"));
				auto *sv = new QVBoxLayout(second.get());
				sv->addWidget(new QLineEdit(QStringLiteral("in the second"),
				                            second.get()));
				second->resize(GridMetrics::cells(term_cols, term_rows));
				second->show();
				sv->activate();
				QCoreApplication::processEvents();
				// Compose once into a buffer nobody prints. The window
				// registry the strip and F6 read is built DURING
				// compose(), so without this a script that opens a
				// window and presses F6 finds nothing to switch to --
				// and would have to know to put a `frame` between them,
				// which is a hidden step and therefore a trap. A real
				// application composes every frame and never meets it.
				CellBuffer settle(term_cols, term_rows);
				comp.compose(settle);
			}
		} else if (cmd == QLatin1String("resize") && parts.size() == 3) {
			// Through the SINK, not by resizing the widget: on_resize is
			// what a real SIGWINCH reaches, and it is the path that
			// drops optional widgets and re-follows the focus. Resizing
			// the window directly would skip exactly what a resize bug
			// is about.
			term_cols = qMax(1, parts[1].toInt());
			term_rows = qMax(1, parts[2].toInt());
			router.on_resize(QSize(term_cols, term_rows));
		} else if (cmd == QLatin1String("click") && parts.size() == 3) {
			QPoint cell(parts[1].toInt(), parts[2].toInt());
			router.on_mouse({cell, 1, true, false, false, 0});
			router.on_mouse({cell, 1, false, true, false, 0});
		} else if (cmd == QLatin1String("frame")) {
			CellBuffer buf(term_cols, term_rows);
			comp.compose(buf);
			if (backend) {
				backend->present(buf, QRegion(0, 0, buf.cols(), buf.rows()));
				++frame_no;
			} else {
				printf("--- frame %d ---\n%s--- end ---\n", frame_no++, qPrintable(buf.to_text()));
			}
		} else if (cmd == QLatin1String("snapshot")) {
			// The same frame with its ATTRIBUTES, which `frame` cannot show.
			// to_text() is glyphs only, so a selection, a focus mark and a
			// highlight are all invisible in it -- measured here: `text hi`
			// then `ctrl a` then two frames come out byte-identical, and the
			// second one has the whole field in reverse video.
			//
			// That matters for the one thing this tool is for. A bug report
			// built from `frame` output omits exactly the states its own
			// `click`, `ctrl` and `key Tab` commands produce. The suite hit
			// this and fixed it the same way: section 9's snapshots carry
			// attributes because "a frame that stopped drawing a selection
			// compared equal to one that drew it".
			//
			// Added rather than swapped: `frame` keeps its format, because
			// something may be reading it.
			CellBuffer buf(term_cols, term_rows);
			comp.compose(buf);
			printf("--- snapshot %d ---\n%s--- end ---\n",
			       frame_no++, qPrintable(buf.to_snapshot()));
		} else {
			fprintf(stderr, "qtty-replay: unknown command '%s'\n", qPrintable(cmd));
		}
	}
	return 0;
}
