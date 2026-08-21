// qtty test runner — one binary, all suites (`make check`).
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

int main(int argc, char **argv) {
    qtty::prepareEnvironment();
    QApplication app(argc, argv);

    // §10.1 inertness gate runs BEFORE setup() by necessity.
    int failures = 0;
    if (QString::fromLatin1(app.style()->metaObject()->className())
            .contains(QStringLiteral("GridStyle"))) {
        printf("FAIL: library not inert before setup()\n");
        ++failures;
    } else printf("PASS: inert before setup()\n");

    qtty::setup(app);

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
    };
    for (auto &s : suites) {
        if (!only.isEmpty() && only != QLatin1String(s.name)) continue;
        printf("\n== %s ==\n", s.name);
        failures += s.run();
    }
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
