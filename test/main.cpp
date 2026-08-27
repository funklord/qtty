// qtty test runner -- one binary, all suites (`make check`).
// --record rewrites snapshot fixtures; a suite name as argv runs just it.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>
#include <cstring>

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

int main(int argc, char **argv) {
	Qtty::prepareEnvironment();
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
	};
	for (auto &s : suites) {
		if (!only.isEmpty() && only != QLatin1String(s.name)) continue;
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
	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
