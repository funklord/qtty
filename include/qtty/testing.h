// qtty/testing.h -- snapshot harness (section 9). Fixtures are human-readable text;
// characterisation-first is the migration rule (section 12 Phase 3): capture the
// existing behaviour before porting a screen, then hold it.
#pragma once
#include <QString>
#include <QFile>
#include <cstdio>
#include "cell.h"
#include "application.h"

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

} // namespace test
} // namespace Qtty
