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
	// The section's own heading level, so that the scan can END at the next
	// heading of that level or above. It did not: `in_section` stayed true to
	// the end of the file, so a table that had been renamed or removed was
	// answered by the next table BELOW carrying the same header.
	//
	// `| Key |` heads a table in three sections, which is what makes that
	// reachable. Measured against the page with "## What already works"'s
	// table header deleted: the reader returned the FIVE rows of "## Moving
	// between pages and windows" -- `Ctrl+Tab`, the tab-bar arrows,
	// `Ctrl+PageDown` -- and its gate failed saying the page promises five
	// key rows where the check drives sixteen. Loud, and pointing at the
	// wrong table. Bounded, it returns nothing and the gate says the page
	// promises none, which points at the table that went.
	//
	// No caller could be given the wrong ROWS today: no two tables on the
	// page share a first-column list, and no `##` heading is a prefix of
	// another. Both measured, and both are properties of the page rather
	// than of this reader, which is why the reader carries the bound.
	int level = 0;
	while (level < section.size() && section.at(level) == QLatin1Char('#'))
		++level;
	bool in_section = false, in_table = false;
	for (const QString &line : lines) {
		if (line.startsWith(section)) {
			in_section = true;
			continue;
		}
		if (!in_section) continue;
		if (!in_table) {
			int depth = 0;
			while (depth < line.size() && line.at(depth) == QLatin1Char('#'))
				++depth;
			if (depth > 0 && depth <= level) break;      // out of the section
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
