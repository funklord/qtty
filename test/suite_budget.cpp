// suite_budget -- the design.md section 11 benchmark fixture: a 200x60 grid
// carrying a 5000-row table.
//
// section 11 says the frame budget is "enforced by a benchmark test on a
// 200x60 grid with a 5000-row table", and project.md section 7.5 records that
// no such test exists -- so F9's 0.16 ms and section 16.2's 3.8 ms are spike
// measurements that nothing in the tree holds to. This suite builds the
// fixture the design names and holds the tree to the parts of the budget that
// are properties of the code rather than of the machine.
//
// The split between what is asserted and what is merely printed is the whole
// design of this file. A wall-clock threshold is a flaky test: this machine
// routinely runs concurrent builds from other sessions, and a duration that
// passes on an idle box fails on a busy one -- at which point somebody
// disables the suite and the real checks go with it. The spread is not
// hypothetical, it was measured while writing this: the same render measured
// 1.35 ms and 2.41 ms in two runs minutes apart, on the same binary.
//
// So durations are PRINTED, for a human to read against section 11's 16 ms
// local and 50 ms ssh budgets, and what is ASSERTED is the damage behaviour
// the frame loop rests on, which does not move with load:
//
//   - rendering the same tree twice produces identical buffers, so an
//     unchanged frame diffs to nothing. section 9 names this invariant --
//     "render twice, diff must be empty" -- and section 7.5 of project.md
//     records that it did not exist;
//   - one keystroke dirties a small bounded number of cells, and dirties them
//     inside the widget that changed rather than across the screen. F9
//     measured 1 cell of 400 at 80x24; it is 1 of 12000 here;
//   - diff()'s output is proportional to what changed and not to how big the
//     grid is, which is section 11's damage-driven claim written as a count
//     rather than as a duration.
//
// Each empty-diff assertion is paired with a frame that differs everywhere,
// because a diff() that returned nothing whatever it was handed would pass
// every check in the first group and fail nothing.
//
// The fixture is built once and reused by every check. At 200x60 the window
// is 2000x1140 px -- the section 5.6 backing store the design puts at roughly
// 6 MB -- and the model is 5000 rows; paying for either per check would
// dominate the very numbers this suite exists to report.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <QElapsedTimer>
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

namespace {

constexpr int grid_cols = 200;
constexpr int grid_rows = 60;
constexpr int table_rows = 5000;
constexpr int table_columns = 4;

// The section 11 table. Cells are generated rather than stored: 5000 rows of
// QStandardItem would measure the item model's allocator rather than the frame
// loop, and a view only ever asks for the rows it can show -- which is itself
// one of the things asserted below.
class Rows : public QAbstractTableModel {
public:
	using QAbstractTableModel::QAbstractTableModel;
	int rowCount(const QModelIndex &parent) const override {
		return parent.isValid() ? 0 : table_rows;
	}
	int columnCount(const QModelIndex &parent) const override {
		return parent.isValid() ? 0 : table_columns;
	}
	QVariant data(const QModelIndex &ix, int role) const override {
		if (role != Qt::DisplayRole || !ix.isValid()) return {};
		return QStringLiteral("r%1c%2").arg(ix.row()).arg(ix.column());
	}
};

// Minimum over `iterations` runs, in milliseconds. The minimum rather than a
// mean on purpose: a mean taken on a loaded box measures the other sessions as
// much as it measures this code, while the minimum is the closest an
// interrupted process can get to reporting its own cost. Iterations are capped
// rather than the time, so this cannot grow into a benchmark that runs for a
// while -- the suite carries a wall-clock watchdog and the whole of it runs in
// a couple of seconds.
template <typename F>
double best_milliseconds(int iterations, F &&run) {
	double best = 0.0;
	for (int i = 0; i < iterations; ++i) {
		QElapsedTimer timer;
		timer.start();
		run();
		const double ms = double(timer.nsecsElapsed()) / 1e6;
		if (i == 0 || ms < best) best = ms;
	}
	return best;
}

bool buffer_contains(const CellBuffer &b, const QString &s) {
	for (int y = 0; y < b.rows(); ++y) {
		for (int x = 0; x + s.size() <= b.cols(); ++x) {
			bool ok = true;
			for (int i = 0; i < s.size(); ++i) {
				if (b.at(x + i, y).ch != QString(s[i])) { ok = false; break; }
			}
			if (ok) return true;
		}
	}
	return false;
}

} // namespace

int suite_budget() {
	fails = 0;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	// ------------------------------------------------------------ fixture
	// Margins and spacing are zeroed and the table takes the stretch, which
	// is what keeps every child on the grid -- GridGuard is watching, and the
	// slack a box layout hands out in non-cell shares is what puts rows off
	// it (the same reason suite_render carries a stretch).
	QWidget win;
	auto *v = new QVBoxLayout(&win);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto *edit = new QLineEdit(&win);
	auto *table = new QTableView(&win);
	Rows model;
	table->setModel(&model);
	table->setFrameShape(QFrame::NoFrame);
	table->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
	v->addWidget(edit);
	v->addWidget(table, 1);
	win.setAttribute(Qt::WA_DontShowOnScreen);
	win.resize(GridMetrics::cells(grid_cols, grid_rows));
	win.show();
	QCoreApplication::processEvents();

	// ------------------------------- section 9: render twice, diff is empty
	CellBuffer first(grid_cols, grid_rows), second(grid_cols, grid_rows);
	render_once(win, first);
	render_once(win, second);
	CHECK(second.diff_cells(first) == 0 && second.diff(first).isEmpty(),
	      "render twice: the second frame diffs to nothing (section 9)");

	// The paired probe. Without it, every empty-diff check above and below is
	// satisfied by a diff() that reports nothing whatever it is handed --
	// which is the shape of check that passes loudest while testing least.
	CellBuffer everywhere(grid_cols, grid_rows);
	Cell odd;
	odd.ch = QStringLiteral("#");
	odd.attrs = Attr::Bold;
	everywhere.fill(QRect(0, 0, grid_cols, grid_rows), odd);
	const QRegion all_damage = everywhere.diff(first);
	CHECK(everywhere.diff_cells(first) == grid_cols * grid_rows
	      && all_damage.boundingRect() == QRect(0, 0, grid_cols, grid_rows),
	      "a frame that differs everywhere reports every cell damaged");

	// ------------------------------------------- F9: one keystroke, one cell
	// The line edit takes focus before the baseline frame, so focus is not
	// part of what changes between the two renders. Measured while writing
	// this: focusing the field changes 0 cells, because the text cursor is
	// the compositor's (Compositor::cursor_cell) and is never painted into
	// the cell plane -- so Qt's cursor blink timer cannot reach these
	// numbers, and this check does not race a 1000 ms flash interval.
	edit->setFocus(Qt::OtherFocusReason);
	// And the view's delayed layout is run before the baseline rather than
	// left pending. QAbstractItemView schedules relayout on a one-shot timer,
	// so a fixture that renders twice and asserts the keystroke is the only
	// difference has a second thing in the window that can change between
	// them. The reasoning above lists only focus, and this is what it missed.
	//
	// Not offered as the explanation of anything: this check was seen to fail
	// once in fifteen runs and then did not reproduce in sixty-three more,
	// eighteen of them with six suites running at once. A pending timer in a
	// fixture that compares two frames is wrong on its own terms, and the
	// diagnostic below is what will name the cause if it happens again.
	table->doItemsLayout();                       // the public form; execute... is protected
	QCoreApplication::processEvents();
	CellBuffer before(grid_cols, grid_rows), after(grid_cols, grid_rows);
	render_once(win, before);

	InputRouter router(&win);
	router.on_key({0, QStringLiteral("a"), false, false, false});
	QCoreApplication::processEvents();
	render_once(win, after);

	const int typed_cells = after.diff_cells(before);
	const QRegion typed_damage = after.diff(before);
	printf("info: one keystroke dirties %d of %d cells\n",
	       typed_cells, grid_cols * grid_rows);
	// The ceiling is eight rather than the one this measures, so that a
	// second cell for a wide cluster, or a field that scrolls its text by a
	// column, is not a failure. What the check is about is the order of
	// magnitude: a keystroke that repaints a screen costs 12000 cells here,
	// and no headroom short of that hides one.
	CHECK(typed_cells >= 1 && typed_cells <= 8,
	      "one keystroke dirties a bounded handful of cells (F9)");

	const QRect edit_cells(edit->geometry().x() / cw, edit->geometry().y() / ch,
	                       edit->geometry().width() / cw, edit->geometry().height() / ch);
	// Named, not just counted. This check failed about one run in fifteen
	// and the condition alone said only that a rectangle was not inside
	// another one -- which is the shape of failure that costs an afternoon
	// of hypotheses, and suite_cells' CHECK macro carries the same lesson.
	if (!edit_cells.contains(typed_damage.boundingRect())) {
		printf("info: damage %d,%d %dx%d is not inside edit %d,%d %dx%d\n",
		       typed_damage.boundingRect().x(), typed_damage.boundingRect().y(),
		       typed_damage.boundingRect().width(),
		       typed_damage.boundingRect().height(),
		       edit_cells.x(), edit_cells.y(),
		       edit_cells.width(), edit_cells.height());
		int shown = 0;
		for (int y = 0; y < grid_rows && shown < 8; ++y)
			for (int x = 0; x < grid_cols && shown < 8; ++x) {
				if (edit_cells.contains(QPoint(x, y))) continue;
				const Cell &a = after.at(x, y), &b = before.at(x, y);
				if (a.ch == b.ch && a.attrs == b.attrs && a.fg == b.fg
				    && a.bg == b.bg)
					continue;
				printf("info:   cell %d,%d was '%s' now '%s'\n", x, y,
				       qPrintable(b.ch), qPrintable(a.ch));
				++shown;
			}
	}
	CHECK(edit_cells.contains(typed_damage.boundingRect()),
	      "keystroke damage stays inside the widget that changed (section 11)");

	// ------------------- damage is proportional to change, not to grid size
	// section 11's cost claim cannot be asserted as a duration without the
	// flakiness this file exists to avoid, and CellBuffer exposes no counter
	// of cells examined -- the scan is O(cells) as the design says. What is
	// assertable, and what actually decides what a frame costs a terminal, is
	// the size of the damage handed to the backend: it must follow what
	// changed and must not grow with the grid.
	CellBuffer one_changed(grid_cols, grid_rows);
	render_once(win, one_changed);
	one_changed.put_cluster(7, 3, QStringLiteral("X"));
	const QRegion one_damage = one_changed.diff(after);
	CHECK(one_damage.rectCount() == 1 && one_changed.diff_cells(after) == 1
	      && one_damage.boundingRect() == QRect(7, 3, 1, 1),
	      "one changed cell in a 200x60 frame yields one one-cell rect");

	CellBuffer small_a(20, 6), small_b(20, 6);
	small_b.put_cluster(7, 3, QStringLiteral("X"));
	const QRegion small_damage = small_b.diff(small_a);
	CHECK(small_damage.rectCount() == one_damage.rectCount()
	      && small_b.diff_cells(small_a) == one_changed.diff_cells(after)
	      && small_damage.boundingRect() == one_damage.boundingRect(),
	      "the same change in a 20x6 frame yields the identical damage");

	CellBuffer run_a(grid_cols, grid_rows), run_b(grid_cols, grid_rows);
	run_b.text(30, 12, QStringLiteral("hello"));
	const QRegion run_damage = run_b.diff(run_a);
	CHECK(run_damage.rectCount() == 1
	      && run_damage.boundingRect() == QRect(30, 12, 5, 1),
	      "a changed run is one rect spanning the run, not the row");

	// ---------------------------------------- the 5000 rows are not painted
	// Why a 5000-row model is affordable at all: the view asks the model only
	// for the rows that fit. If that ever stops being true the render cost
	// starts tracking the model rather than the grid, and the number printed
	// below stops meaning anything.
	CHECK(buffer_contains(first, QStringLiteral("r0c0")),
	      "the table's first row is painted");
	CHECK(!buffer_contains(first, QStringLiteral("r400c0")),
	      "rows past the bottom of the grid are not painted");

	// ----------------------------------------------------- reported numbers
	CellBuffer scratch(grid_cols, grid_rows);
	const double render_ms = best_milliseconds(20, [&] { render_once(win, scratch); });
	const double diff_ms = best_milliseconds(50, [&] { (void)scratch.diff(first); });

	// The 80x24 dialog is here to anchor the number above to the one
	// design.md actually published: F9's 0.16 ms was measured on a dialog at
	// 80x24, on Qt 6.4 under offscreen. Without it the 200x60 figure is a
	// number with nothing to compare against, on a different Qt and a
	// different machine.
	QDialog dialog;
	auto *dv = new QVBoxLayout(&dialog);
	dv->setContentsMargins(0, 0, 0, 0);
	dv->setSpacing(0);
	dv->addWidget(new QCheckBox(QStringLiteral("Enable telemetry"), &dialog));
	dv->addWidget(new QRadioButton(QStringLiteral("Daily"), &dialog));
	dv->addWidget(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
	                                   &dialog));
	dv->addStretch();
	dialog.setAttribute(Qt::WA_DontShowOnScreen);
	dialog.resize(GridMetrics::cells(80, 24));
	dialog.show();
	QCoreApplication::processEvents();
	CellBuffer dialog_frame(80, 24);
	const double dialog_ms = best_milliseconds(20, [&] { render_once(dialog, dialog_frame); });

	printf("info: render 200x60 with %d table rows: %.3f ms (best of 20)\n",
	       table_rows, render_ms);
	printf("info: diff  200x60: %.3f ms (best of 50)\n", diff_ms);
	printf("info: render 80x24 dialog: %.3f ms (best of 20; design.md F9 says 0.16)\n",
	       dialog_ms);
	printf("info: section 11 budget is 16 ms local, 50 ms over ssh -- read the "
	       "three numbers above against it\n");

	// The one duration that is asserted, and the ceiling is deliberately
	// absurd: ten times the whole 16 ms local frame budget, and roughly two
	// orders of magnitude above what this measures. A machine running four
	// other builds stretches a 1.3 ms render to 2.4 ms -- measured, not
	// guessed -- so nothing short of a real order-of-magnitude regression,
	// the kind that would put a full-frame render outside the budget on any
	// machine, can trip it. A tighter number would be a truer statement of
	// the budget and a test that goes red for reasons that are not the code's.
	CHECK(render_ms < 160.0,
	      "a full 200x60 frame renders inside ten times the local budget");

	return fails;
}
