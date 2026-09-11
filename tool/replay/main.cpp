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
    "  click <col> <row>  mouse press and release at a cell\n"    "  resize <cols> <rows>  resize the terminal, through the same sink a\n"
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

	// sample UI: form + scrolling list (replace with your screen when linking)
	QWidget win;
	auto *v = new QVBoxLayout(&win);
	v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
	auto *edit = new QLineEdit(&win);
	edit->setPlaceholderText("type here");
	auto *list = new QListView(&win);
	auto *model = new QStringListModel(&win);
	QStringList rows;
	for (int i = 0; i < 40; ++i) rows << QStringLiteral("item %1").arg(i);
	model->setStringList(rows);
	list->setModel(model);
	list->setFrameShape(QFrame::NoFrame);
	list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	v->addWidget(edit); v->addWidget(list, 1);
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
