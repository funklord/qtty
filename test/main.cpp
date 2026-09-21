// qtty test runner -- one binary, all suites (`make check`).
// --record rewrites snapshot fixtures; a suite name as argv runs just it.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>

int suite_cells();
int suite_theme();
int suite_render(bool record);
int suite_grid();
int suite_placements();
int suite_router();
int suite_widgets_entry(bool record);
int suite_graphics();
int suite_runtime();
int suite_backend();
int suite_exec();
int suite_budget();

namespace {

// The suite bounds its own running time, and the bound is here rather than
// only in the Makefile recipe on purpose. A recipe's `timeout` protects
// `make test` and protects nobody debugging -- and debugging is exactly when
// the binary gets run directly. This tree paid for that: a suite that called
// exec() waited on an event loop that never started, and running it by hand
// to find out why hung until it was killed from another terminal.
//
// SIGALRM is the whole mechanism. A handler may call almost nothing, so it
// writes a fixed string with write(2) and leaves by _exit -- neither printf
// nor exit is legal here, and a watchdog that crashes instead of reporting is
// worse than none. QTTY_TEST_TIMEOUT raises it for a deliberately slow run.
constexpr unsigned default_timeout_seconds = 300;

extern "C" void qtty_test_watchdog(int) {
	static const char msg[] =
	    "\nFAILED: the suite exceeded its own time limit and was stopped.\n"
	    "That is a hang, not a slow machine -- raise QTTY_TEST_TIMEOUT only\n"
	    "once you know which suite is waiting and on what.\n";
	const ssize_t ignored = ::write(2, msg, sizeof msg - 1);
	(void)ignored;
	::_exit(2);
}

} // namespace


int main(int argc, char **argv) {
	unsigned limit = default_timeout_seconds;
	if (const QByteArray env = qgetenv("QTTY_TEST_TIMEOUT"); !env.isEmpty()) {
		bool ok = false;
		const unsigned v = env.toUInt(&ok);
		if (ok) limit = v;
	}
	if (limit > 0) {
		struct sigaction sa{};
		sa.sa_handler = qtty_test_watchdog;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGALRM, &sa, nullptr);
		alarm(limit);
	}

	Qtty::prepare_environment();
	QApplication app(argc, argv);

	// section 10.1 inertness gate runs BEFORE setup() by necessity.
	int failures = 0;
	// dynamic_cast, not the class NAME. GridStyle carries no Q_OBJECT, so
	// metaObject()->className() answers "QProxyStyle" for every instance of
	// it -- measured, before setup() and after -- and this gate's condition
	// could never be true. It has reported PASS since it was written without
	// once being able to report anything else, which is the vacuous pass
	// this project spends its time hunting, sitting in the check that guards
	// section 10.1.
	//
	// Proved by positive control: with setup(app) moved ABOVE this gate --
	// the exact condition it exists to detect -- the old spelling still
	// printed PASS, and this one fails.
	if (dynamic_cast<Qtty::GridStyle *>(app.style())) {
		printf("FAIL: library not inert before setup()\n");
		++failures;
	} else printf("PASS: inert before setup()\n");

	Qtty::setup(app);

	// section 9: "GridGuard runs as an assertion in every test". Installed
	// here rather than in each suite, so a suite added later is covered by
	// having been written rather than by remembering to opt in -- which is
	// the failure mode a per-suite install has and this does not.
	Qtty::GridGuard::install(app);

	bool record = false;
	QString only;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--record")) record = true;
		else only = QString::fromLatin1(argv[i]);
	}
	struct { const char *name; std::function<int()> run; } suites[] = {
		{"cells",      [&] { return suite_cells(); }},
		{"theme",      [&] { return suite_theme(); }},
		{"render",     [&] { return suite_render(record); }},
		{"grid",       [&] { return suite_grid(); }},
		{"placements", [&] { return suite_placements(); }},
		{"router",     [&] { return suite_router(); }},
		{"widgets",    [&] { return suite_widgets_entry(record); }},
		{"graphics",   [&] { return suite_graphics(); }},
		{"runtime",    [&] { return suite_runtime(); }},
		{"backend",    [&] { return suite_backend(); }},
		{"exec",       [&] { return suite_exec(); }},
		{"budget",     [&] { return suite_budget(); }},
	};
	int ran = 0;
	for (auto &s : suites) {
		if (!only.isEmpty() && only != QLatin1String(s.name)) continue;
		++ran;
		printf("\n== %s ==\n", s.name);
		Qtty::GridGuard::reset();
		// A sweep between suites -- hide every window the last one left
		// showing -- was written here and REMOVED again, because it changed
		// nothing. Measured both ways: zero visible top-levels at the start
		// of all twelve suites, with the sweep and without it. Nothing leaks
		// ACROSS a suite boundary, so a boundary sweep buys nothing and
		// would only look like protection.
		//
		// What does leak is within a suite: a QTableView shown for one check
		// stays visible through thirty-three later ones in the same file.
		// That is where the fifteen checks recorded in section 7 are fragile,
		// and a fix belongs at those blocks rather than here.
		failures += s.run();
		// The guard's own finding, reported per suite so it names which one
		// moved a widget off the grid rather than leaving a total nobody can
		// attribute. It is a real check and counts as one.
		const int off = Qtty::GridGuard::violations();
		if (off == 0) {
			// NAMED, which this comment claimed before the line did. The
			// suite's own header sits above it, so a person reading the
			// run in order always knew which one -- and a person reading
			// a FAIL line, or grepping for it, did not, and twelve
			// identical PASS lines are what the sabotage harness counts
			// as one check named twelve times. An entry naming this
			// assertion was refused for matching twelve passing checks
			// rather than one, so the strongest guard in the tree could
			// not defend the guard.
			printf("PASS: every widget geometry landed on the grid (%s)\n",
			       s.name);
		} else {
			// THE SAME SENTENCE as the pass, with the count after it. The
			// two used to word the claim differently -- "24 widget
			// geometry/geometries off the grid in render" against "every
			// widget geometry landed on the grid" -- and a check whose
			// failure does not contain its own name cannot be named by a
			// sabotage entry at all: the harness looks for the check's
			// text inside a FAIL line, finds nothing, and reports that the
			// suite finished without running it. Measured while writing
			// the entry that defends this.
			printf("FAIL: every widget geometry landed on the grid (%s)"
			       " -- %d did not (see the qtty: warnings above)\n",
			       s.name, off);
			++failures;
		}

		// And what the suites DISOWNED, which is the number that was
		// silently doing the work. GridGuard::reset() is called 59 times
		// across these suites so a fixture can ignore an off-grid geometry
		// it made on purpose, and everything before the last call was gone
		// by the time the tally above was read: measured, a run emitting 136
		// off-grid warnings reported zero violations and passed -- though
		// those 136 turned out to come from the FORKED CHILDREN this suite
		// runs, which count in their own address space and were never in
		// this tally at all. The parent discards nothing today, which is
		// what makes zero the useful number rather than a weak one.
		//
		// Pinned, the way this project already pins its check count, and for
		// the same reason -- an off-grid widget appearing ANYWHERE moves this
		// number whatever the resets do, so a regression cannot hide behind
		// one. Update it deliberately, and only after reading the warnings
		// the run printed.
		// What reset() threw away, REPORTED and not asserted -- and the
		// reason is worth more than the assertion would have been.
		//
		// reset() lets a fixture disown an off-grid geometry it made on
		// purpose, and it is called 59 times here, so everything before the
		// last call is gone by the time the tally above is read. The same
		// deliberate violation passes or fails depending only on which side
		// of an unrelated later reset() it sits. GridGuard::forgiven() now
		// keeps the discarded count, and it works: proved standalone, where
		// a resize to 37x23, a move to (3,7) and an odd-sized child give 4,
		// 5 and 6 violations and forgiven() reads 6 after a reset.
		//
		// It is not asserted because this suite FORKS -- it is how a
		// terminal library watches a process die -- and a child inherits the
		// counter and the tail of main() with it. One run prints this line
		// nine times with rising values, one per fork point, and a getpid()
		// guard did not suppress them. Pinning a number that arrives nine
		// times with four different values would be pinning the fork
		// pattern, not the grid.
		//
		// THE FIGURE IS NOT WRITTEN HERE, and that is the correction.
		// This comment used to say "the parent's own figure is 136";
		// measured 2026-09-21 it is 235, and nothing about the grid had
		// changed -- the suites had grown fixtures, and every fixture
		// that shows a widget at a size the grid does not divide adds to
		// it. A number that moves with the tests rather than with the
		// code cannot be pinned in a comment, and pinning it made the
		// sentence wrong within a fortnight.
		//
		// The finding survives the number: these suites disown off-grid
		// geometries by the hundred, every one invisible to the check
		// above, and the line below is the live count. Watch that until
		// somebody separates the parent's from its children's.
		printf("info: reset() has disowned %d off-grid geometries in this"
		       " process\n", Qtty::GridGuard::forgiven());
	}
	// A name that matches no suite ran nothing, and a run over zero suites
	// exits 0 and reads exactly like a pass -- the same shape the `test`
	// target refuses for zero binaries. It cost an hour here:
	// `make record R=widgets_gallery` names a FIXTURE where the argument is a
	// SUITE, so it recorded nothing, printed OK, and left the snapshot it was
	// asked to rewrite untouched.
	if (!only.isEmpty() && ran == 0) {
		printf("FAIL: '%s' names no suite, so nothing ran\n      suites:",
		       qPrintable(only));
		for (auto &s : suites) printf(" %s", s.name);
		printf("\n");
		++failures;
	}
	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
