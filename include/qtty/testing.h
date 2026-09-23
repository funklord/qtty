// qtty/testing.h -- snapshot harness (section 9). Fixtures are human-readable text;
// characterisation-first is the migration rule (section 12 Phase 3): capture the
// existing behaviour before porting a screen, then hold it.
#pragma once
#include <QString>
#include <QFile>
#include <cstdio>
#include "cell.h"
#include "application.h"
#include "grid.h"
#include "runtime.h"

namespace Qtty {
namespace test {

// Compare `got` with the named fixture under <root>/test/snapshot/.
// record=true (re)writes the fixture instead. Returns 0 on match/record,
// 1 on mismatch (printing both sides).
inline int check_snapshot(const QString &root, const QString &name,
                         const QString &got, bool record = false) {
	const QString path = root + QStringLiteral("/test/snapshot/") + name
	                   + QStringLiteral(".txt");
	if (record) {
		// Show what is about to be blessed. Re-recording is how a snapshot
		// stops being evidence: the fixture is the measurement and the code
		// is the intervention, and --record hands both to the same person in
		// the same minute, so a regression can be promoted to the expected
		// output by muscle memory. The Makefile calls this "after a reviewed
		// change" and nothing here made a review possible -- it truncated
		// the file and printed a path.
		//
		// Printing the differing rows does not change who is deciding. It
		// changes whether they can see what they are deciding, which is the
		// whole of what "reviewed" can mean for a fixture nobody reads in
		// full.
		QString before;
		{
			QFile old(path);
			if (old.open(QIODevice::ReadOnly))
				before = QString::fromUtf8(old.readAll());
		}
		if (!before.isEmpty() && before == got) {
			printf("unchanged %s\n", qPrintable(path));
			return 0;
		}
		if (before.isEmpty()) {
			printf("new fixture %s\n", qPrintable(path));
		} else {
			const QStringList a = before.split(QLatin1Char('\n'));
			const QStringList b = got.split(QLatin1Char('\n'));
			int differing = 0;
			for (int i = 0; i < qMax(a.size(), b.size()); ++i)
				if (a.value(i) != b.value(i)) ++differing;
			printf("recording %s -- %d line(s) differ:\n",
			       qPrintable(path), differing);
			for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
				if (a.value(i) == b.value(i)) continue;
				printf("  %3d -%s\n      +%s\n", i,
				       qPrintable(a.value(i)), qPrintable(b.value(i)));
			}
		}
		// Both results read, and that is not pedantry: with open()'s dropped
		// the write returned -1 on an unwritable path, this printed "new
		// fixture <path>" and returned 0, and the reader who had just been
		// told to "run with --record" got the same failure next run with a
		// success message in between.
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
		    || f.write(got.toUtf8()) < 0) {
			fprintf(stderr, "FAIL: fixture %s could not be written: %s\n",
			        qPrintable(path), qPrintable(f.errorString()));
			return 1;
		}
		return 0;
	}
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		// "missing" was a cause this never tested. open() fails just as
		// readily for a mode-000 file or a directory at that path, and
		// --record is no remedy for either -- it would fail to write for the
		// same reason. So existence is asked about, and the advice is given
		// only to the case it is advice for.
		if (!QFile::exists(path))
			fprintf(stderr, "FAIL: fixture %s does not exist;"
			        " run with --record\n", qPrintable(path));
		else
			fprintf(stderr, "FAIL: fixture %s exists but could not be read:"
			        " %s\n", qPrintable(path), qPrintable(f.errorString()));
		return 1;
	}
	const QString want = QString::fromUtf8(f.readAll());
	if (got != want) {
		// The cell size, because it is the likeliest reason a fixture fails
		// on a machine that changed nothing. A snapshot is recorded in CELLS
		// and every cell position in it comes from cw and ch, which
		// Qtty::setup() derives from the locally installed font -- measured
		// on this machine, 102 fixed-pitch families give eleven distinct cell
		// sizes and only seven of them give the 10x19 these fixtures were
		// taken at. Without this line that arrives as two unexplained diffs.
		fprintf(stderr, "FAIL: snapshot '%s' mismatch (cell %dx%d)\n"
		        "--- want ---\n%s--- got ---\n%s",
		        qPrintable(name), GridMetrics::cw(), GridMetrics::ch(),
		        qPrintable(want), qPrintable(got));
		return 1;
	}
	return 0;
}

// -- DRIVING THE KEYBOARD, with the fields filled in correctly.
//
// A KeyEvent carries a key, a text and three modifier flags, and WHICH of
// them decides is different per keystroke -- measured, on a two-field form
// with mnemonics:
//
//     what you send          decides            ignored
//     a named key (Tab)      qt_key             text
//     a chord (Ctrl+S)       qt_key + modifier  text
//     a typed character      text               qt_key
//     a mnemonic (Alt+H)     text + alt         qt_key
//
// Every wrong combination is SILENT. `{Qt::Key_H, QString(), alt}` reaches
// no mnemonic, `{Qt::Key_Z, QString()}` types nothing, and `{0, "\t"}` moves
// no focus -- each returns as if delivered and nothing says otherwise. The
// three below fill the right field, so a test says what it means.
//
// They are here rather than on InputRouter because they are a test's
// vocabulary: a program has a terminal sending it real bytes.

// A named key or a chord: Tab, Escape, Return, an arrow, Ctrl+S.
//
// A printable key with no ctrl and no alt is a CHARACTER, so its text is
// filled in and press(r, Qt::Key_Z) types a z rather than doing nothing.
// Under ctrl or alt it is left empty, which is what the router reads and
// what stops a chord also typing its letter.
inline void press(InputRouter &router, int qt_key, bool ctrl = false,
                  bool alt = false, bool shift = false) {
	QString text;
	if (!ctrl && !alt) {
		if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) {
			const QChar c(char16_t(u'a' + (qt_key - Qt::Key_A)));
			text = shift ? QString(c.toUpper()) : QString(c);
		} else if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) {
			text = QString(QChar(char16_t(u'0' + (qt_key - Qt::Key_0))));
		} else if (qt_key == Qt::Key_Space) {
			text = QStringLiteral(" ");
		}
	}
	router.on_key({qt_key, text, ctrl, alt, shift});
}

// What a user types, one key event per character. The key code is left at
// zero because the text is what the router reads -- with ONE exception, and
// it is the decoder's rather than this helper's.
//
// A space arrives from a terminal carrying Qt::Key_Space as well as its
// text, because QAbstractButton activates on the KEY and a space that was
// text alone did nothing. A helper that sent text alone would be a stand-in
// reproducing the half of the real thing its author happened to have met,
// and every test written with it would be typing a space no terminal sends.
inline void type(InputRouter &router, const QString &text) {
	for (const QChar &c : text) {
		const int key = c == QLatin1Char(' ') ? int(Qt::Key_Space) : 0;
		router.on_key({Qt::Key(key), QString(c), false, false, false});
	}
}

// Alt and a letter, which is how a mnemonic is reached. The LETTER is what
// matches, not the key code -- so this is not press(r, Qt::Key_H, ...) with
// alt, which reaches nothing.
inline void mnemonic(InputRouter &router, QChar letter) {
	router.on_key({0, QString(letter), false, true, false});
}

// -- AND THE MOUSE, which carries the same kind of trap one field along.
//
// A MouseEvent is built by aggregate initialisation at almost every call
// site in this project -- `{cell, 1, false, false, true, 0}` -- where the
// second field is the button and the three bools are press, release and
// motion in that order. Three things about it are silent when wrong, all
// measured against a button and a scroll bar:
//
//     button 1, press then release    the button is clicked
//     button 0, press then release    NOTHING -- 0 means "no button"
//     button 1, press with no release NOTHING -- a click needs both
//     wheel +1 / -1 on a scroll bar   50 -> 47 -> 50, so + is UP
//
// `button` is not an SGR number. The decoder writes `1 + (b & 3)`, so the
// left button is 1 and zero means no button is down. A probe written with
// 0 cost a session its drag results once: every one invalid, and the
// control dead beside them, with nothing saying so.

// BY NAMED FIELD AND NOT BY POSITION, which is the other half of the
// trap and caught the author of these four: the first draft of
// mouse_move() read `{cell, button, false, false, true, button}` and the
// sixth field is the WHEEL, so a drag move also scrolled. Aggregate
// initialisation is what every call site in this project uses and it is
// exactly what puts a value in the wrong slot -- so the helpers that
// exist to stop that do not use it either.

// A click at one cell: press and release, which is what a control needs.
// Button 1 is the left button.
inline void click(InputRouter &router, const QPoint &cell, int button = 1) {
	MouseEvent down;
	down.cell = cell;
	down.button = button;
	down.press = true;
	router.on_mouse(down);
	MouseEvent up;
	up.cell = cell;
	up.button = button;
	up.release = true;
	router.on_mouse(up);
}

// The halves of a click, for a drag: press, then move, then release.
inline void mouse_press(InputRouter &router, const QPoint &cell,
                        int button = 1) {
	MouseEvent e;
	e.cell = cell;
	e.button = button;
	e.press = true;
	router.on_mouse(e);
}
inline void mouse_release(InputRouter &router, const QPoint &cell,
                          int button = 1) {
	MouseEvent e;
	e.cell = cell;
	e.button = button;
	e.release = true;
	router.on_mouse(e);
}

// Moving the pointer. With no button that is a hover; with one it is a
// drag, which is why the button is an argument rather than assumed.
inline void mouse_move(InputRouter &router, const QPoint &cell,
                       int button = 0) {
	MouseEvent e;
	e.cell = cell;
	e.button = button;
	e.motion = true;
	router.on_mouse(e);
}

// The wheel. Positive is UP -- measured, +1 took a scroll bar from 50 to
// 47 -- and `right` is the horizontal wheel, which SGR reports separately
// and which this library carries in its own field.
inline void wheel(InputRouter &router, const QPoint &cell, int up,
                  int right = 0) {
	MouseEvent e;
	e.cell = cell;
	e.wheel = up;
	e.wheel_x = right;
	router.on_mouse(e);
}

// Render a widget to a section 9 snapshot in one call -- glyphs, attributes
// and colours. It used to return glyphs alone, which meant a fixture could
// not see the reverse video, bold and dim that most of the Channel A work
// produces: a frame that stopped drawing a selection compared equal to one
// that drew it.
inline QString snapshot_of(QWidget &w, int cols, int rows) {
	CellBuffer buf(cols, rows);
	render_once(w, buf);
	return buf.to_snapshot();
}

// The whole SCREEN rather than one widget: the window, then the menus,
// drop-downs, tooltips and modal dialogs stacked on top of it, and the
// small-terminal policy applied the way a frame loop applies it.
//
// snapshot_of() above renders one widget, which is right for a control
// and wrong for everything a layer covers. Measured, with a QMenu popped
// over a window: render_once() shows the button the menu is sitting on
// and no menu at all, so a fixture taken that way is a picture nobody
// sees. The menu is the commonest thing to get wrong and was the one
// thing a snapshot could not hold.
//
// THE ROUTER IS NOT OPTIONAL and that is why it is an argument rather
// than a default. The popup stack is the router's -- it collects layers
// from their Show events -- so a compositor built without one draws the
// window alone. Measured, same fixture: with a router the menu is there,
// with nullptr it is not, and neither reports anything wrong. A test that
// drives keys already has a router, which is the same one to pass.
//
// It takes the WINDOW TAB RECORD down as it goes, because ~Compositor
// does: the strip is a property of a composed frame, and a record that
// outlived its compositor would have a later press read as a tab
// selection for windows nobody is showing.
//
// TWO THINGS DEPEND ON THAT RECORD, and this note first named only one.
// A press in row 0 is the obvious one -- it is not read as a tab
// selection afterwards. The other is **F6**, which moves within the
// window list the compositor recorded: with a transient compositor the
// list is empty by the time the key arrives, so F6 does nothing at all
// and reads as a broken feature.
//
// Measured while writing this tree's own probes: two windows, the
// conventions on, F6 sent between snapshots -- the current window never
// changed. With one compositor kept alive across the presses, as a
// frame loop keeps one, F6 moved to the second window and Shift+F6 came
// back.
//
// So a test that drives keys depending on the window set does one of
// two things: keeps a Compositor alive across them, or calls
// Qtty::set_current_window() to say which window it means, which is
// what this project's own F6 checks do.
//
// AND IT COMPOSES THE SCREEN, not the window you pass. A second visible
// top level puts a window tab strip in the frame and leaves the FIRST
// one current, so the cells carry both and the caret reported is that
// other window's. Measured while writing this tree's own checks: a
// focused field's frame carried its own text AND a button from a window
// left up by the fixture before it, and reported the caret hidden. Close
// or scope the windows you are not snapshotting.
inline QString snapshot_of_screen(QWidget &window, InputRouter &router,
                                  int cols, int rows) {
	CellBuffer buf(cols, rows);
	QString where = QStringLiteral("hidden");
	{
		Compositor c(&window, &router);
		c.compose(buf);
		// THE CARET, which no fixture could see. It is the thing a
		// terminal user's eye follows and it is not in the buffer: the
		// cells say what is written and the compositor says where the
		// terminal's own cursor goes, so a frame that lost its caret, or
		// put it in the wrong cell, or showed a block where it had shown
		// a bar, compared equal to one that did not. Same argument as
		// the images plane, one channel further out.
		//
		// Recorded HERE rather than in CellBuffer::to_snapshot(), because
		// only a composed frame has an answer. A widget rendered on its
		// own has no caret to report, and a section saying "hidden" in
		// that case would be a claim rather than an absence -- a focused
		// line edit really does show one when a frame is composed around
		// it.
		if (const std::optional<QPoint> at = c.cursor_cell()) {
			const char *shape = "block";
			switch (c.cursor_shape()) {
			case CursorShape::Underline: shape = "underline"; break;
			case CursorShape::Bar:       shape = "bar"; break;
			case CursorShape::Hidden:    shape = "hidden"; break;
			case CursorShape::Block:     break;
			}
			where = QStringLiteral("%1,%2 %3")
			        .arg(at->x()).arg(at->y())
			        .arg(QLatin1String(shape));
		}
	}
	return buf.to_snapshot() + QStringLiteral("--- cursor ---\n")
	     + where + QLatin1Char('\n');
}

} // namespace test
} // namespace Qtty
