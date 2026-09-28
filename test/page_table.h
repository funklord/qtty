// The rows of one markdown table on the guide's page, by their first cell.
//
// ONE PARSER, because a second copy is a second chance for two gates to
// disagree about what a row is -- and there are six gates now, in three
// suites. It started as a static in suite_router.cpp and moved here when the
// third suite wanted it: suite_backend binds the table of ambiguous chords
// against what its decoder actually does, and duplicating the reader to reach
// it would have defeated the reason the reader is shared.
//
// It answers about the source tree rather than the build, which is why it
// needs QTTY_SOURCE_DIR: the binary cannot find the page once the build has
// moved out of the tree.
#ifndef QTTY_TEST_PAGE_TABLE_H
#define QTTY_TEST_PAGE_TABLE_H

#include <QFile>
#include <QString>
#include <QStringList>

static QStringList page_table_rows(const QString &section,
                                   const QString &header) {
	QStringList out;
	QFile page(QStringLiteral(QTTY_SOURCE_DIR)
	           + QStringLiteral("/doc/keyboard-first.md"));
	if (!page.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
	const QStringList lines =
	    QString::fromUtf8(page.readAll()).split(QLatin1Char('\n'));
	bool in_section = false, in_table = false;
	for (const QString &line : lines) {
		if (line.startsWith(section)) {
			in_section = true;
			continue;
		}
		if (!in_section) continue;
		if (!in_table) {
			if (line.startsWith(header)) in_table = true;
			continue;
		}
		if (line.startsWith(QStringLiteral("|---"))) continue;
		if (!line.startsWith(QStringLiteral("| "))) break;
		out << line.mid(2).section(QStringLiteral(" |"), 0, 0);
	}
	return out;
}

#endif
