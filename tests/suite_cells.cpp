// suite_cells — L2: clusters, wide cells, continuation rules, region diff (§5.2).
#include <qtty/qtty.h>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_cells() {
    fails = 0;

    CHECK(clusterWidth(u"a") == 1, "ascii is narrow");
    CHECK(clusterWidth(u"あ") == 2, "hiragana is wide");
    CHECK(clusterWidth(u"漢") == 2, "CJK is wide");
    CHECK(clusterWidth(QStringLiteral("🎉")) == 2, "emoji is wide");
    CHECK(toClusters(QStringLiteral("héllo")).size() == 5
          || toClusters(QStringLiteral("héllo")).size() == 6,   // é may be composed
          "grapheme clustering runs");
    // combining sequence stays one cluster
    const QString combining = QStringLiteral("é");        // e + COMBINING ACUTE
    CHECK(toClusters(combining).size() == 1, "combining mark joins its base cluster");

    CellBuffer b(10, 2);
    b.putCluster(2, 0, QStringLiteral("あ"));
    CHECK(b.at(2, 0).width == 2 && b.at(3, 0).width == 0,
          "wide cluster claims lead + continuation");
    CHECK(b.toText().startsWith(QStringLiteral("  あ")),
          "toText emits wide glyph once");

    // overwrite the continuation half: lead must clear (§5.2 corruption rule)
    b.putCluster(3, 0, QStringLiteral("x"));
    CHECK(b.at(2, 0).ch == QStringLiteral(" ") && b.at(3, 0).ch == QStringLiteral("x"),
          "writing over continuation clears the wide lead");

    // overwrite the lead half: continuation must clear
    b.putCluster(5, 0, QStringLiteral("あ"));
    b.putCluster(5, 0, QStringLiteral("y"));
    CHECK(b.at(6, 0).width == 1 && b.at(6, 0).ch == QStringLiteral(" "),
          "writing over lead clears the continuation");

    // diff: one changed cell → one 1x1 region
    CellBuffer p1(10, 3), p2(10, 3);
    p2.putCluster(4, 1, QStringLiteral("z"));
    QRegion d = p2.diff(p1);
    CHECK(d.boundingRect() == QRect(4, 1, 1, 1), "diff region is exactly the change");
    CHECK(p2.diffCells(p1) == 1, "diffCells counts one");

    // text() advances by cluster width
    CellBuffer t(10, 1);
    int consumed = t.text(0, 0, QStringLiteral("aあb"));
    CHECK(consumed == 4 && t.at(3, 0).ch == QStringLiteral("b"),
          "text() advances 1+2+1 cells");

    return fails;
}
