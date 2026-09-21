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
// It takes the window tab strip down as it goes, because ~Compositor
// does: the strip is a property of a composed frame and its record is
// read on every press. So a press in row 0 sent between this call and
// the next compose is not read as a tab selection. Compose again, or
// snapshot after the press rather than before it.
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
