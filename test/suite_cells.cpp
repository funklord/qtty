// suite_cells -- L2: clusters, wide cells, continuation rules, region diff (section 5.2).
#include <qtty/qtty.h>
#include "src/cell_geometry.h"
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

int suite_cells() {
	fails = 0;

	CHECK(cluster_width(u"a") == 1, "ascii is narrow");
	CHECK(cluster_width(u"あ") == 2, "hiragana is wide");
	CHECK(cluster_width(u"漢") == 2, "CJK is wide");
	CHECK(cluster_width(QStringLiteral("🎉")) == 2, "emoji is wide");

	// `Capabilities::unicode_wide`, which the header promised and nothing
	// kept until this. A backend for a terminal that will not advance two
	// columns says so, and the width table has to stop saying 2 -- otherwise
	// qtty reserves a cell the terminal does not consume and everything to
	// the right of it slides one column left.
	{
		CHECK(wide_clusters(), "the width table honours wide clusters by "
		                       "default, as every terminal met so far does");
		set_wide_clusters(false);
		// Asserted on the WIDE cases, because the narrow ones answer 1 with
		// the flag ignored entirely: a check over "a" would pass against a
		// build that never read it.
		CHECK(cluster_width(u"あ") == 1 && cluster_width(u"漢") == 1
		      && cluster_width(QStringLiteral("🎉")) == 1,
		      "and a terminal that does not advance two columns is told so, "
		      "and gets one-cell clusters");
		// The capability names wcwidth-2 and nothing else. Answering the
		// zero-width question with the same flag would be inventing a second
		// capability out of this one.
		CHECK(cluster_width(QString(QChar(0x200b))) == 0,
		      "and a zero-width character is still zero, which is a "
		      "different question the flag does not answer");
		set_wide_clusters(true);              // process-wide: put it back
		CHECK(cluster_width(u"あ") == 2,
		      "and the table comes back when the flag does");
	}

	// EVERY RANGE IN THE WIDE TABLE, one character each. Branch coverage
	// said eight of its thirteen rows had never matched: the suite used
	// hiragana, a CJK ideograph and an emoji, so Hangul, Yi, the fullwidth
	// forms, the compatibility blocks and CJK Ext B were carried by nobody.
	//
	// A typo in a bound is not a wrong glyph. A character the table calls
	// narrow occupies one cell here and two on the terminal, so everything
	// after it on the line sits one column left of where qtty believes it
	// is -- and the diff, which repaints only what changed, leaves the rest
	// of the screen as it was. Whole writing systems ride on these bounds.
	{
		const struct { char32_t cp; const char *what; } wide_ranges[] = {
			{ 0x1100,  "Hangul Jamo"        },
			{ 0x2E80,  "CJK Radicals"       },
			{ 0x3042,  "Kana"               },
			{ 0x3400,  "CJK Ext A"          },
			{ 0x4E00,  "CJK Unified"        },
			{ 0xA000,  "Yi"                 },
			{ 0xAC00,  "Hangul Syllables"   },
			{ 0xF900,  "CJK Compatibility"  },
			{ 0xFE30,  "CJK Compat Forms"   },
			{ 0xFF01,  "Fullwidth forms"    },
			{ 0xFFE0,  "Fullwidth signs"    },
			{ 0x1F600, "emoji"              },
			{ 0x20000, "CJK Ext B"          },
		};
		QStringList narrow;
		for (const auto &r : wide_ranges)
			if (cluster_width(QString::fromUcs4(&r.cp, 1)) != 2)
				narrow << QString::fromLatin1(r.what);
		if (!narrow.isEmpty())
			printf("info: ranges the table called narrow: %s\n",
			       qPrintable(narrow.join(QStringLiteral(", "))));
		CHECK(narrow.isEmpty()
		      && sizeof(wide_ranges) / sizeof(wide_ranges[0]) == 13,
		      "one character from each of the thirteen wide ranges is two "
		      "cells, so no writing system is silently narrow");

		// The other side of the same table: a character just outside a
		// bound must stay narrow. Without this the check above passes
		// against a table that answers 2 for everything.
		const char32_t just_below = 0x10FF, just_above = 0x1160;
		CHECK(cluster_width(QString::fromUcs4(&just_below, 1)) == 1
		      && cluster_width(QString::fromUcs4(&just_above, 1)) == 1,
		      "and the characters on either side of a range are one, so the "
		      "table discriminates rather than answering two");
	}

	// A WIDE CLUSTER IN THE LAST COLUMN. `text()` stops before the edge
	// rather than walking past it, so nothing had ever put one there --
	// and `put_cluster` is public, so an application or a renderer can.
	//
	// What it must do is documented in the source and was not pinned: a
	// width-2 cell always has its partner, so in a column with no partner
	// to be had a BLANK is what fits. The alternative is a cell claiming
	// two columns in a one-column space, which `to_text()` emits as a row
	// one column wider than the buffer and a terminal either wraps onto
	// the next line or truncates.
	//
	// Asserted on the ROW WIDTH, which is what discriminates. The obvious
	// assertion -- that the next row is untouched -- passes against the
	// blank substitution being deleted, because a second guard further
	// down stops the out-of-bounds write on its own. Two guards, and only
	// one of them decides what the user sees.
	{
		CellBuffer b(4, 2);
		b.text(0, 1, QStringLiteral("keep"));
		b.put_cluster(3, 0, QStringLiteral("あ"));
		const QStringList rows = b.to_text().split(QLatin1Char('\n'));
		// Empty rather than four spaces: `to_text()` trims a row's trailing
		// blanks, so a blanked cell is the absence of anything. What
		// discriminates is that the GLYPH is not there -- with the blank
		// substitution deleted the row carries it and is one column wider
		// than the buffer.
		CHECK(rows.size() >= 2 && !rows.at(0).contains(QStringLiteral("あ"))
		      && rows.at(0).isEmpty(),
		      "a wide cluster in the last column becomes a blank, because a "
		      "cell claiming two columns in a one-column space is a row "
		      "wider than the buffer");
		CHECK(rows.size() >= 2 && rows.at(1) == QStringLiteral("keep"),
		      "and the next row is untouched, which in one flat array is "
		      "the cell its continuation would have been");
	}
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

		// The MODE, which every caller was getting as ElideRight whatever the
		// application asked. Measured on a tree column twelve cells wide
		// showing "/home/user/deep/dir/report.txt": all four modes rendered
		// "/home/user/d...", and an application sets ElideLeft on a path
		// column precisely because the END is the part worth seeing.
		//
		// Asserted on the SHAPE rather than on an exact string, so the rule
		// is what is pinned and not one budget's arithmetic: which end the
		// marker sits at, and that the kept text really comes from that end.
		const QString path = QStringLiteral("/home/user/deep/dir/report.txt");
		const QString right = elide_to_cells(path, 12, Qt::ElideRight);
		const QString left = elide_to_cells(path, 12, Qt::ElideLeft);
		const QString middle = elide_to_cells(path, 12, Qt::ElideMiddle);
		const QString none = elide_to_cells(path, 12, Qt::ElideNone);
		const QChar dots(0x2026);
		CHECK(right.endsWith(dots) && path.startsWith(right.chopped(1)),
		      "eliding right keeps the front and marks the end");
		CHECK(left.startsWith(dots) && path.endsWith(left.mid(1)),
		      "eliding left keeps the END, which is what a path column asks"
		      " for and never got");
		CHECK(middle.count(dots) == 1 && !middle.startsWith(dots)
		          && !middle.endsWith(dots)
		          && path.startsWith(middle.section(dots, 0, 0))
		          && path.endsWith(middle.section(dots, 1, 1)),
		      "and eliding in the middle keeps both ends");
		CHECK(!none.contains(dots) && path.startsWith(none)
		          && width(none) <= 12,
		      "while ElideNone truncates and marks nothing, an ellipsis being"
		      " the elision it was told not to make");
		CHECK(width(right) <= 12 && width(left) <= 12 && width(middle) <= 12,
		      "and none of them overruns the budget it was given");
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

	// The snapshot planes must line up with the glyph plane COLUMN for column,
	// which is the one thing the format promises a reader. A wide cluster is
	// the case that tests it: one glyph occupying two columns, so the glyph
	// plane emits one character where the attribute and colour planes must
	// emit two. Skipping the continuation cell in all three -- which is what
	// the glyph plane correctly does -- left the other two a character short
	// under every wide cluster, and no fixture had one.
	{
		const QString wide = QString::fromUtf8("\u6f22");
		CellBuffer b(6, 1);
		b.text(0, 0, QStringLiteral("a") + wide + QStringLiteral("b"));
		for (int x = 0; x < 4; ++x) b.at(x, 0).attrs = Attr::Reverse;

		const QStringList lines = b.to_snapshot().split(QLatin1Char('\n'));
		const QString glyphs = lines.value(0);
		const int attrs_row = lines.indexOf(QStringLiteral("--- attrs ---")) + 1;
		const QString attrs = lines.value(attrs_row);

		// Display columns, not QChars: that distinction IS the bug.
		int glyph_columns = 0;
		for (const QString &cl : to_clusters(glyphs)) glyph_columns += cluster_width(cl);
		CHECK(glyph_columns == 4, "the glyph plane spans four columns");
		CHECK(attrs.size() == 4,
		      "the attribute plane carries one character per cell, so the "
		      "planes are the same width");
		CHECK(glyphs.size() == 3,
		      "and the glyph plane still carries one per cluster, not per cell");
	}

	// diff() against a buffer of a different size. Correct as written -- a
	// resized frame shares nothing with its predecessor, so everything is
	// damage -- but nothing had ever asked, and this is the path a terminal
	// resize takes, which is now reachable for the first time since the
	// backend grew a SIGWINCH handler.
	{
		CellBuffer small(4, 2), big(8, 3);
		small.text(0, 0, QStringLiteral("ab"));
		big.text(0, 0, QStringLiteral("ab"));
		CHECK(big.diff(small) == QRegion(0, 0, 8, 3),
		      "a diff against a differently sized buffer damages everything");
		CHECK(big.diff_cells(small) == 24,
		      "and counts every cell, not the cells that happen to match");
		// Same size, same content: the paired probe, so the two above cannot
		// be satisfied by a diff that reports everything whatever it is given.
		CellBuffer other(8, 3);
		other.text(0, 0, QStringLiteral("ab"));
		CHECK(big.diff(other).isEmpty() && big.diff_cells(other) == 0,
		      "while an identical buffer of the same size damages nothing");
	}
	{
		// colour_name() for an INDEXED colour, and luminance for one. Both
		// are how a fixture and the contrast rule describe a palette colour,
		// and both had only ever been given RGB.
		const Color idx = Color::indexed(33);
		CellBuffer b(3, 1);
		b.at(0, 0).fg = idx;
		b.at(0, 0).ch = QStringLiteral("x");
		// Through to_snapshot(), which is what a fixture records: the legend
		// names an indexed colour by its INDEX rather than by the value it
		// resolves to, so a snapshot stays readable as what the code asked
		// for rather than as what the palette happened to be.
		CHECK(b.to_snapshot().contains(QStringLiteral("index:33")),
		      "a fixture names an indexed colour by its index");
		// Resolved through the 256-colour table rather than guessed: an
		// index carries no channels of its own, so a luminance that returned
		// zero for every index would satisfy any check that only asked for a
		// number.
		CHECK(idx.luminance(true) > 0 && idx.luminance(true) <= 255,
		      "and its luminance is measured from the colour it resolves to");
	}

	// ---- controls and zero-width characters (section 5.2) ----
	{
		// Measured, not assumed: a probe rendered a QLabel holding each of
		// these and printed the row. Qt's layout turned the carriage return
		// into a space by itself and passed NUL, BEL and ESC through as
		// themselves, and AnsiBackend writes Cell::ch to the wire unaltered
		// -- so an application string could put an escape introducer into the
		// terminal's stream and have the text after it read as a sequence.
		CellBuffer b(6, 1);
		b.text(0, 0, QStringLiteral("a\x1b" "b"), Color(), Color(), Attrs());
		CHECK(b.at(1, 0).ch == QStringLiteral(" "),
		      "an escape character reaches a cell as a space");
		// The column is still spent, because Qt advanced by one laying it
		// out. Dropping it here would put the buffer half a character out of
		// step with the pixel positions Channel B measures against.
		CHECK(b.at(2, 0).ch == QStringLiteral("b"),
		      "and still costs the column Qt's layout gave it");
		// Every control, not the one the probe happened to print: a check
		// naming only ESC would pass with NUL and BEL still going out raw.
		for (char32_t u : { char32_t(0x00), char32_t(0x07), char32_t(0x1b),
		                    char32_t(0x7f), char32_t(0x9b) }) {
			CellBuffer c(2, 1);
			c.put_cluster(0, 0, QString(QChar(u)), Color(), Color(), Attrs());
			if (c.at(0, 0).ch != QStringLiteral(" ")) {
				printf("FAIL: no C0, C1 or DEL character reaches a cell as"
				       " itself -- U+%04X does\n", unsigned(u));
				++fails;
			}
		}
		printf("PASS: no C0, C1 or DEL character reaches a cell as itself\n");
	}
	{
		// A zero-width character was given a whole cell, which pushed
		// everything after it one column right.
		CHECK(cluster_width(QString(QChar(0x200b))) == 0,
		      "a lone zero-width space occupies no column");
		CellBuffer b(4, 1);
		const int used = b.text(0, 0, QStringLiteral("a")
		                        + QChar(0x200b) + QStringLiteral("b"),
		                        Color(), Color(), Attrs());
		CHECK(used == 2 && b.at(1, 0).ch == QStringLiteral("b"),
		      "so the character after it keeps its column");
		// Untouched, rather than blanked: the cell the dropped cluster would
		// have landed on belongs to whatever was already there.
		CellBuffer c(2, 1);
		c.text(0, 0, QStringLiteral("z"), Color(), Color(), Attrs());
		c.put_cluster(0, 0, QString(QChar(0xfeff)), Color(), Color(), Attrs());
		CHECK(c.at(0, 0).ch == QStringLiteral("z"),
		      "and writing one erases nothing");
		// Only when the whole cluster is invisible. A joiner inside an emoji
		// sequence belongs to a cluster that draws, and a rule reading the
		// first character alone would have swallowed it.
		CHECK(cluster_width(QString(QChar(0x200d)) + QStringLiteral("a")) == 1,
		      "a joiner in front of a glyph does not make the glyph vanish");
	}

	{
		// How a snapshot spells a TRUE-COLOUR cell, which no fixture has ever
		// contained: coverage showed the branch with no caller in a whole
		// run. Snapshots are this tree's most-cited artefacts, so a legend
		// entry nobody has read is worth one check -- and the comment beside
		// the code makes a claim precise enough to hold: six digits and not
		// eight, because the alpha byte would print a constant 00 in every
		// fixture and read as colour.
		CellBuffer b(3, 1);
		b.text(0, 0, QStringLiteral("x"), Color::rgb(qRgb(255, 0, 0)),
		       Color::rgb(qRgb(0, 128, 255)), Attrs());
		const QString snap = b.to_snapshot();
		CHECK(snap.contains(QStringLiteral("#ff0000"))
		      && snap.contains(QStringLiteral("#0080ff")),
		      "a snapshot spells a true-colour cell as six hex digits");
		CHECK(!snap.contains(QStringLiteral("ffff0000")),
		      "and masks the alpha byte off rather than printing eight");
	}

	// THE ATTRIBUTE NAMES IN A SNAPSHOT, all six. Branch coverage said four
	// had never been written: bold and reverse had, and dim, italic,
	// underline and strike had not.
	//
	// Snapshots are this tree's most-cited artefacts and they are compared
	// against THEMSELVES, so a misspelled name is invisible by construction
	// -- record a fixture with "itallic" in it and every later run agrees.
	// The legend is only worth what its words are, and four of the six words
	// had never been printed.
	{
		const struct { Attr a; const char *name; } names[] = {
			{ Attr::Bold,      "bold"      },
			{ Attr::Dim,       "dim"       },
			{ Attr::Italic,    "italic"    },
			{ Attr::Underline, "underline" },
			{ Attr::Reverse,   "reverse"   },
			{ Attr::Strike,    "strike"    },
			// The seventh (8.238), in the same table for the same reason
			// the SGR one in suite_theme.cpp gives: the table is the
			// population the count below asserts over.
			{ Attr::Blink,     "blink"     },
		};
		// A DIFFERENTIAL against the same cell without the attribute,
		// rather than `contains(name)`. The plain form was vacuous for the
		// seventh: the legend carries a "blink:" line in every snapshot
		// whether or not anything blinks, so a snapshot with the name
		// removed from attr_names() still contained the word and the check
		// passed against broken code. The sabotage harness found that --
		// entry "a snapshot legend that cannot spell blink" applied cleanly
		// and reddened nothing -- which is exactly what that target is for,
		// and it is the second time in this one change that a check was
		// satisfied by the plane's own header rather than by its contents.
		//
		// Counting rather than testing presence is what makes one predicate
		// work for all seven: an attribute's name may legitimately appear in
		// a snapshot that does not carry it, but it appears MORE OFTEN in
		// one that does.
		QStringList missing;
		for (const auto &e : names) {
			CellBuffer one(2, 1), bare(2, 1);
			one.text(0, 0, QStringLiteral("x"), Color(), Color(), Attrs(e.a));
			bare.text(0, 0, QStringLiteral("x"), Color(), Color(), Attrs());
			const QString name = QLatin1String(e.name);
			if (one.to_snapshot().count(name) <= bare.to_snapshot().count(name))
				missing << name;
		}
		if (!missing.isEmpty())
			printf("info: attribute names a snapshot never spells: %s\n",
			       qPrintable(missing.join(QStringLiteral(", "))));
		CHECK(missing.isEmpty() && sizeof(names) / sizeof(names[0]) == 7,
		      "a snapshot spells each of the seven attributes by name, so a "
		      "fixture recording one is comparing against a word somebody "
		      "has read");
	}

	// THE MASK THAT WOULD HAVE SWALLOWED THE SEVENTH ATTRIBUTE.
	//
	// attr_char() read `int(a) & 0x3f` against a 64-entry table, which is
	// exactly right for six flags and silently drops a seventh. Nothing
	// would have overrun and nothing would have failed: a blinking cell
	// would have printed the character for its other six attributes, and
	// since fixtures are compared against THEMSELVES the missing attribute
	// would have agreed with every later run for ever.
	//
	// This is deliberately a separate question from whether the attribute
	// reaches the wire. Against a half-fix -- the enum value added, the SGR
	// row added, the mask left at 0x3f -- suite_theme's check PASSES and
	// this one fails, and that split is the whole reason it is worth
	// writing.
	//
	// The difference shows in the blink PLANE rather than in the attribute
	// character, and that is the encoding rather than a weaker assertion.
	// Seven flags need 127 distinct characters and printable ASCII has 94,
	// so a single character cannot separate all seven; cell_buffer.cpp
	// carries the reasoning. What must hold is that the recorded artefact
	// tells the two cells apart, and it is asserted here on the plane that
	// does it as well as on the whole snapshot.
	{
		const Attrs six = Attr::Bold | Attr::Dim | Attr::Italic
		                | Attr::Underline | Attr::Reverse | Attr::Strike;
		const Attrs seven = six | Attr::Blink;
		const auto shot = [](Attrs a) {
			CellBuffer b(2, 1);
			b.text(0, 0, QStringLiteral("x"), Color(), Color(), a);
			return b.to_snapshot();
		};
		const auto plane = [](const QString &snap, const QString &header) {
			const QStringList l = snap.split(QLatin1Char('\n'));
			const int at = l.indexOf(header);
			return at >= 0 && at + 1 < l.size() ? l.at(at + 1) : QString();
		};
		const QString s6 = shot(six), s7 = shot(seven);
		const QString h = QStringLiteral("--- blink ---");

		// The LEGEND ENTRY, not the word. The legend carries a "blink:"
		// line in every snapshot whether or not anything blinks, so
		// asserting the bare word would pass against a blink plane that had
		// been emptied -- a check whose passing condition includes the
		// failure it was written for.
		CHECK(s7.contains(QStringLiteral(", b blink"))
		      && !s6.contains(QStringLiteral(", b blink")),
		      "a snapshot of a cell carrying all seven attributes names "
		      "blink in its legend, and one without it does not");
		CHECK(plane(s7, h) == QStringLiteral("b")
		      && plane(s6, h) == QStringLiteral("(none)"),
		      "and the blink plane separates a seven-attribute cell from a "
		      "six-attribute one, which the attribute character cannot");
		CHECK(s6 != s7,
		      "so two cells differing only in blink do not record the same "
		      "snapshot, which is what a masked-off bit would have done");
	}

	// THE AUTHORED ANSI-16 INDEX, WHICH THE COLOUR PLANE COULD NOT SPELL.
	//
	// Color::operator== counts the authored index as part of identity, and
	// color.h says why: two colours with the same RGB and different
	// authored indices emit different bytes on a 16-colour terminal, so a
	// diff that called them equal would leave the wrong one on screen.
	// colour_name() printed the kind, the index or the RGB and nothing
	// else, and the colour plane is keyed on that name -- so the frame diff
	// called two such cells UNEQUAL and a recorded fixture gave them the
	// SAME letter.
	//
	// That is the Blink defect one plane over, and it is worth naming as
	// the same one: an artefact compared against ITSELF cannot report a
	// difference it has no way to spell, so it agrees with every later run
	// of every fixture for ever and there is no failing test at the end of
	// it. It was inert rather than absent -- both committed fixtures are
	// recorded under terminal_default(), where every role is Color::Default
	// and with_ansi16() refuses to name an index on one -- and the tree
	// gained five newly-reachable authored indices in 8.257, so the regime
	// where it bites is nearer than it was.
	//
	// The remedy is a suffix on the NAME rather than a plane of its own,
	// and the counting is in cell_buffer.cpp beside it: the attribute
	// plane's ceiling is a theorem about a fixed domain of 127 masks, while
	// the colour plane assigns letters to the pairs a frame actually holds
	// and so has no such domain to overflow.
	{
		const Color plain  = Color::rgb(qRgb(0, 0, 255));
		const Color blue   = plain.with_ansi16(4);
		const Color bright = plain.with_ansi16(12);
		const QString word = QStringLiteral("ansi16");
		const auto colour_plane = [](const CellBuffer &b) {
			const QStringList l = b.to_snapshot().split(QLatin1Char('\n'));
			const int at = l.indexOf(QStringLiteral("--- colours ---"));
			return at >= 0 && at + 1 < l.size() ? l.at(at + 1) : QString();
		};

		// The premise, asserted rather than assumed. Without it the check
		// below would be demanding that a snapshot invent a distinction
		// nothing else in the tree makes, and it would be the snapshot that
		// was wrong. This is what the frame diff acts on.
		CHECK(blue != bright && blue.value() == bright.value(),
		      "two colours with the same RGB and different authored ANSI-16 "
		      "indices are unequal, which is the difference a frame diff "
		      "acts on");

		CellBuffer b(3, 1);
		b.text(0, 0, QStringLiteral("x"), blue, Color(), Attrs());
		b.text(1, 0, QStringLiteral("y"), bright, Color(), Attrs());
		const QString p = colour_plane(b);
		CHECK(p.size() == 2 && p.at(0) != p.at(1),
		      "and a snapshot gives them different colour letters, so a "
		      "fixture records the difference rather than agreeing with "
		      "itself for ever");

		// THE LEGEND, DIFFERENTIALLY. A contains() would be vacuous the
		// moment anything printed the word unconditionally, which is
		// exactly how the blink legend's first version passed against code
		// that could not spell blink at all -- the sabotage entry applied
		// cleanly and reddened nothing. Counting separates the two: a name
		// may legitimately appear in a snapshot that does not carry the
		// thing, but it appears MORE OFTEN in one that does.
		//
		// Index 0 and not 4, deliberately. Zero is the boundary the
		// obvious `if (index > 0)` loses, and authored black is a real
		// entry in the role table rather than a contrived one; a check
		// written with index 4 would pass against that off-by-one and the
		// fixture would go on mis-spelling exactly one of the sixteen.
		CellBuffer with_index(2, 1), without_index(2, 1);
		with_index.text(0, 0, QStringLiteral("x"), plain.with_ansi16(0),
		                Color(), Attrs());
		without_index.text(0, 0, QStringLiteral("x"), plain, Color(), Attrs());
		CHECK(with_index.to_snapshot().count(word)
		      > without_index.to_snapshot().count(word),
		      "the legend names the authored index, and names it more often "
		      "for a cell carrying index 0 than for one carrying none");

		// THE CONTROL. Everything above is satisfied by a change that
		// simply made every cell distinct, so two colours that really are
		// equal have to go on sharing a letter.
		CellBuffer same(3, 1);
		same.text(0, 0, QStringLiteral("x"), blue, Color(), Attrs());
		same.text(1, 0, QStringLiteral("y"), plain.with_ansi16(4), Color(),
		          Attrs());
		const QString sp = colour_plane(same);
		CHECK(blue == plain.with_ansi16(4) && sp.size() == 2
		      && sp.at(0) == sp.at(1),
		      "while two equal colours still take the same letter, so the "
		      "plane did not become one letter per cell");

		// WHY THE TWO COMMITTED FIXTURES CANNOT MOVE, proved at the
		// mechanism rather than by re-measuring the files. suite_render's
		// check_snapshot compares the bytes themselves and would go red if
		// they had; what it cannot say is whether they held still by luck.
		// They hold still because the suffix is CONDITIONAL and a
		// Color::Default cannot carry one, which is the regime both
		// fixtures were recorded in. Offering an index to a Default here
		// is the point: with_ansi16() refuses it, so the name is plain.
		CellBuffer def(2, 1);
		def.text(0, 0, QStringLiteral("x"), Color().with_ansi16(4),
		         Color().with_ansi16(12), Attrs());
		CHECK(!def.to_snapshot().contains(word),
		      "and a snapshot of the terminal's own colours names no "
		      "authored index even when one was offered, which is why a "
		      "fixture recorded under terminal_default() cannot move");
	}

	// ---- the colour group Qt actually paints from -------------------------
	//
	// role_of() searched Active and Disabled. Qt paints every widget here
	// from the palette's INACTIVE group -- no window activates, so
	// QWidgetPrivate::colorGroup() never answers Active for a shown widget --
	// and a colour matching no role is carried out as a hard 24-bit sequence,
	// which is what section 6 exists to avoid on a sixteen-colour terminal.
	//
	// THE FIXTURE HAS TO MAKE THE GROUPS DIFFER. On this machine's palette
	// Active and Inactive are identical for every role these lookups ask
	// about -- measured over all 21 paintable roles by 8.251, which is
	// wider than the lists below and so covers them however they grow --
	// so the omission could not be observed here, and
	// a check written against the palette as it stands would pass with the
	// defect in place. This one installs a palette that separates them and
	// asserts the separation before asking anything else.
	{
		const QPalette saved = QGuiApplication::palette();
		const QColor odd(0x12, 0x34, 0x56);
		QPalette p = saved;
		p.setColor(QPalette::Inactive, QPalette::Text, odd);
		p.setColor(QPalette::Inactive, QPalette::Dark, odd);
		QGuiApplication::setPalette(p);

		// The partition: this colour must belong to no Active and no Disabled
		// role in either list, or the lookup would find it without reading
		// the Inactive group at all and the check would prove nothing.
		const QPalette &live = QGuiApplication::palette();
		// ASKED OF THE SAME LISTS THE LOOKUPS ASK, not of a copy. This
		// enumeration used to be typed out here, and when 8.248 added
		// five authored roles to the ink list the copy stayed at eleven
		// -- so the partition below covered eleven of the sixteen roles
		// the lookup really reads. A partition assertion over a short
		// population does not fail; it quietly stops covering, which is
		// the one way a check can rot without anybody seeing a red line.
		bool elsewhere = false;
		QVector<QPalette::ColorRole> asked = ink_roles();
		asked += furniture_roles();
		for (QPalette::ColorRole r : asked)
			if (live.color(QPalette::Active, r).rgba() == odd.rgba()
			    || live.color(QPalette::Disabled, r).rgba() == odd.rgba())
				elsewhere = true;
		CHECK(!elsewhere,
		      "the fixture separates the palette's groups: the inactive "
		      "colour belongs to no active or disabled role");

		// Text, through the shared helper both channels use.
		const TextStyle ts = text_style_for(odd.rgba());
		CHECK(ts.color.kind() != Color::Rgb && !(ts.attrs & Attr::Dim),
		      "a text colour from the group Qt actually paints with resolves "
		      "to its role rather than to a true colour");

		// And a stroke, because the frame furniture is the bigger population:
		// Qt shades every sunken border with pal.dark() and pal.light(), so a
		// theme whose inactive greys differ would carry a 24-bit colour for
		// every frame in the program.
		CHECK(line_for(odd.rgba()).kind() != Color::Rgb,
		      "and a border shaded from that group draws in the terminal's "
		      "own colour rather than as true colour");

		QGuiApplication::setPalette(saved);
		CHECK(QGuiApplication::palette().color(QPalette::Inactive,
		                                       QPalette::Text)
		      == saved.color(QPalette::Inactive, QPalette::Text),
		      "and the palette is put back, so no later check inherits this "
		      "fixture");
	}

	return fails;
}
