// qtty/testing.h — snapshot harness (§9). Fixtures are human-readable text;
// characterisation-first is the migration rule (§12 Phase 3): capture the
// existing behaviour before porting a screen, then hold it.
#pragma once
#include <QString>
#include <QFile>
#include <cstdio>
#include "cell.h"
#include "application.h"

namespace qtty {
namespace test {

// Compare `got` with the named fixture under <root>/tests/snapshots/.
// record=true (re)writes the fixture instead. Returns 0 on match/record,
// 1 on mismatch (printing both sides).
inline int checkSnapshot(const QString &root, const QString &name,
                         const QString &got, bool record = false) {
    const QString path = root + QStringLiteral("/tests/snapshots/") + name
                       + QStringLiteral(".txt");
    if (record) {
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(got.toUtf8());
        printf("recorded %s\n", qPrintable(path));
        return 0;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "FAIL: fixture %s missing; run with --record\n", qPrintable(path));
        return 1;
    }
    const QString want = QString::fromUtf8(f.readAll());
    if (got != want) {
        fprintf(stderr, "FAIL: snapshot '%s' mismatch\n--- want ---\n%s--- got ---\n%s",
                qPrintable(name), qPrintable(want), qPrintable(got));
        return 1;
    }
    return 0;
}

// Render a widget to snapshot text in one call.
inline QString snapshotOf(QWidget &w, int cols, int rows) {
    CellBuffer buf(cols, rows);
    renderOnce(w, buf);
    return buf.toText();
}

} // namespace test
} // namespace qtty
