// suite_cells -- L2: clusters, wide cells, continuation rules, region diff (section 5.2).
#include <qtty/qtty.h>
#include "src/cell_geometry.h"
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_cells() {
	fails = 0;

	CHECK(cluster_width(u"a") == 1, "ascii is narrow");
	CHECK(cluster_width(u"あ") == 2, "hiragana is wide");
	CHECK(cluster_width(u"漢") == 2, "CJK is wide");
	CHECK(cluster_width(QStringLiteral("🎉")) == 2, "emoji is wide");
	CHECK(to_clusters(QStringLiteral("héllo")).size() == 5
	      || to_clusters(QStringLiteral("héllo")).size() == 6,   // the accented e may be composed
	      "grapheme clustering runs");
	// combining sequence stays one cluster
	const QString combining = QStringLiteral("é");        // e + COMBINING ACUTE
	CHECK(to_clusters(combining).size() == 1, "combining mark joins its base cluster");

	CellBuffer b(10, 2);
	b.put_cluster(2, 0, QStringLiteral("あ"));
	CHECK(b.at(2, 0).width == 2 && b.at(3, 0).width == 0,
	      "wide cluster claims lead + continuation");
	CHECK(b.to_text().startsWith(QStringLiteral("  あ")),
	      "to_text emits wide glyph once");

	// overwrite the continuation half: lead must clear (section 5.2 corruption rule)
	b.put_cluster(3, 0, QStringLiteral("x"));
	CHECK(b.at(2, 0).ch == QStringLiteral(" ") && b.at(3, 0).ch == QStringLiteral("x"),
	      "writing over continuation clears the wide lead");

	// overwrite the lead half: continuation must clear
	b.put_cluster(5, 0, QStringLiteral("あ"));
	b.put_cluster(5, 0, QStringLiteral("y"));
	CHECK(b.at(6, 0).width == 1 && b.at(6, 0).ch == QStringLiteral(" "),
	      "writing over lead clears the continuation");

	// diff: one changed cell -> one 1x1 region
	CellBuffer p1(10, 3), p2(10, 3);
	p2.put_cluster(4, 1, QStringLiteral("z"));
	QRegion d = p2.diff(p1);
	CHECK(d.boundingRect() == QRect(4, 1, 1, 1), "diff region is exactly the change");
	CHECK(p2.diff_cells(p1) == 1, "diff_cells counts one");

	// text() advances by cluster width
	CellBuffer t(10, 1);
	int consumed = t.text(0, 0, QStringLiteral("aあb"));
	CHECK(consumed == 4 && t.at(3, 0).ch == QStringLiteral("b"),
	      "text() advances 1+2+1 cells");


	// elide_to_cells: a wide cluster is two cells, and the marker needs one.
	//
	// This is the gap that let a wrong implementation live in GridStyle for
	// months: both versions of the rule handled ASCII identically, and neither
	// had a check that asked about a wide cluster. A differential run over the
	// two found them disagreeing on 9 cases of 143 -- every one involving a
	// wide cluster or a budget of 1 -- and the loser was chopping one QChar
	// where it meant one cluster, and reserving no cell for the marker.
	{
		const QString cjk = QString::fromUtf8("\u6f22\u5b57\u30c6\u30b9\u30c8");
		const auto width = [](const QString &s) {
			int n = 0;
			for (const QString &c : to_clusters(s)) n += cluster_width(c);
			return n;
		};
		CHECK(width(cjk) == 10, "five wide clusters are ten cells");
		CHECK(elide_to_cells(cjk, 10) == cjk, "text that fits is returned whole");
		CHECK(width(elide_to_cells(cjk, 3)) == 3,
		      "eliding to 3 cells uses all 3, not one");
		CHECK(width(elide_to_cells(cjk, 1)) == 1,
		      "eliding to 1 cell is the marker, never an empty string");
		CHECK(elide_to_cells(cjk, 1) == QString(QChar(0x2026)),
		      "and the marker is U+2026, not a truncated byte");
		CHECK(elide_to_cells(cjk, 0).isEmpty(), "a zero budget elides to nothing");
		// A surrogate pair must not be split: chopping a QChar would leave
		// half of one, which is an invalid string rather than a short one.
		const QString emoji = QString::fromUtf8("\U0001F389ok");
		const QString cut = elide_to_cells(emoji, 2);
		CHECK(!cut.isEmpty() && cut.at(cut.size() - 1) == QChar(0x2026)
		      && !cut.at(0).isLowSurrogate(),
		      "eliding never leaves half a surrogate pair");
	}

	// A width-2 cluster in the LAST column has no continuation cell to take,
	// and section 5.2's invariant is that it always has one. Nothing had ever
	// asked: every wide-cluster check in this suite had room to spare, which
	// is the same gap that hid the Latin-1 input decode and the elide fault.
	{
		const QString wide = QString::fromUtf8("\u6f22");
		CellBuffer b(4, 1);
		b.put_cluster(3, 0, wide);
		CHECK(b.at(3, 0).width == 1,
		      "a wide cluster with no room is not written as width 2");
		CHECK(b.to_text().trimmed().isEmpty(),
		      "it renders as a blank, not a glyph overflowing the row");

		// The row must never render wider than the buffer. That is the fault
		// the blank prevents: a terminal given one column too many wraps it
		// onto the next line or truncates it, and either way the frame after
		// it is misaligned.
		CellBuffer wide_row(4, 1);
		wide_row.text(0, 0, wide + wide);
		int rendered = 0;
		for (int x = 0; x < 4; ++x)
			rendered += wide_row.at(x, 0).width;
		CHECK(rendered <= 4, "a full row of wide clusters occupies exactly the row");
	}

	// text() reports what it wrote, not what it was handed. It used to add the
	// width of every cluster including ones put_cluster refused as out of
	// bounds, so it answered 6 for a 4-column buffer -- and a caller advancing
	// a cursor by that walked off the end of the row.
	{
		const QString wide = QString::fromUtf8("\u6f22");
		CellBuffer b(4, 1);
		CHECK(b.text(0, 0, wide + wide + wide) == 4,
		      "text() stops at the edge and reports the cells it filled");
		CellBuffer c(4, 1);
		CHECK(c.text(0, 0, QStringLiteral("abcdef")) == 4,
		      "and the same for narrow clusters");
		CellBuffer d(8, 1);
		CHECK(d.text(0, 0, QStringLiteral("ab") + wide) == 4,
		      "a mixed run still reports its true width");
	}
	return fails;
}
