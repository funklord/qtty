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
	if (QString::fromLatin1(app.style()->metaObject()->className())
	        .contains(QStringLiteral("GridStyle"))) {
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
		failures += s.run();
		// The guard's own finding, reported per suite so it names which one
		// moved a widget off the grid rather than leaving a total nobody can
		// attribute. It is a real check and counts as one.
		const int off = Qtty::GridGuard::violations();
		if (off == 0) {
			printf("PASS: every widget geometry landed on the grid\n");
		} else {
			printf("FAIL: %d widget geometry/geometries off the grid "
			       "(see the qtty: warnings above)\n", off);
			++failures;
		}
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
