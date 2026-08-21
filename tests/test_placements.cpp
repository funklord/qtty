// test_placements — §5.7 cell-anchored placements through the drawPixmap
// funnel: stable identity, scroll tracking, upload-once (§16.3).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using qtty::GridMetrics;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

class PixWidget : public QWidget {           // paints one pixmap at a cell offset
public:
    QPixmap pm;
    QPoint cellPos{3, 2};
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.drawPixmap(cellPos.x() * GridMetrics::cw(), cellPos.y() * GridMetrics::ch(), pm);
    }
};

int main(int argc, char **argv) {
    qtty::prepareEnvironment();
    QApplication app(argc, argv);
    qtty::setup(app);
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

    QPixmap sticker(10 * cw, 4 * ch);
    sticker.fill(Qt::red);

    PixWidget w;
    w.pm = sticker;
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.resize(GridMetrics::cells(40, 12));
    w.show();
    QCoreApplication::processEvents();

    qtty::CellBuffer buf(40, 12);
    QVector<qtty::CellImage> pl;
    qtty::renderOnce(w, buf, &pl);
    CHECK(pl.size() == 1, "one drawPixmap -> one placement");
    CHECK(!pl.isEmpty() && pl[0].cellRect == QRect(3, 2, 10, 4),
          "placement lands at the drawn cell rect");
    CHECK(!pl.isEmpty() && pl[0].key == sticker.cacheKey(),
          "placement identity is the pixmap cacheKey");

    // "scroll": move the anchor two rows up, identity stays, position tracks
    quint64 keyBefore = pl.isEmpty() ? 0 : pl[0].key;
    w.cellPos = QPoint(3, 0);
    QVector<qtty::CellImage> pl2;
    qtty::renderOnce(w, buf, &pl2);
    CHECK(!pl2.isEmpty() && pl2[0].cellRect.topLeft() == QPoint(3, 0),
          "placement tracks the new anchor after scroll");
    CHECK(!pl2.isEmpty() && pl2[0].key == keyBefore,
          "same image -> same key across frames (upload-once)");

    // tiny image stays a glyph, not a placement (§8.6)
    PixWidget tiny;
    tiny.pm = QPixmap(cw, ch);
    tiny.pm.fill(Qt::blue);
    tiny.setAttribute(Qt::WA_DontShowOnScreen);
    tiny.resize(GridMetrics::cells(10, 4));
    tiny.show();
    QCoreApplication::processEvents();
    qtty::CellBuffer tb(10, 4);
    QVector<qtty::CellImage> tp;
    qtty::renderOnce(tiny, tb, &tp);
    CHECK(tp.isEmpty(), "1-cell pixmap substitutes a glyph, no placement");

    printf(failures ? "%d FAILURE(S)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
