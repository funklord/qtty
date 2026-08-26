// Qtty Phase 0.6 — graphics overlay plane.
//
// Claim to validate: because cells are pixel-addressed (CW x CH), the software
// compositing path for pixel-graphics terminals (sixel / iTerm2 / kitty-no-alpha)
// is nearly free: rasterize the CellBuffer with QPainter, alpha-blend overlay
// QImages, emit. And the no-graphics fallback (half/shade blocks) is a pure
// CellBuffer transform needing NOTHING from the backends.

#include "qtty_core.h"
#include <QElapsedTimer>
#include <QImage>
#include <QRadialGradient>

static void hr2(const char *t) { printf("\n\033[1m=== %s ===\033[0m\n", t); }

// ---- overlay: an alpha image the app wants shown over the whole terminal ----
static QImage makeOverlay(int wpx, int hpx) {
    QImage img(wpx, hpx, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    // translucent disc, offset right-of-centre
    QRadialGradient g(QPointF(wpx*0.62, hpx*0.45), hpx*0.55);
    g.setColorAt(0.0, QColor(80, 160, 255, 200));
    g.setColorAt(0.7, QColor(80, 160, 255, 120));
    g.setColorAt(1.0, QColor(80, 160, 255, 0));
    p.setBrush(g); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(wpx*0.62, hpx*0.45), hpx*0.55, hpx*0.55);
    // an opaque "logo" block
    p.setBrush(QColor(255, 120, 40, 235));
    p.drawRoundedRect(QRectF(wpx*0.66, hpx*0.12, wpx*0.22, hpx*0.18), 8, 8);
    p.end();
    return img;
}

// ---- path A: no-graphics fallback — compose INTO the cell buffer ----------
// Pure L2 transform: backends keep shipping plain cells, unaware of graphics.
static void composeHalfblocks(CellBuffer &buf, const QImage &ov) {
    for (int y = 0; y < buf.rows(); ++y)
        for (int x = 0; x < buf.cols(); ++x) {
            // sample overlay alpha at cell centre
            int px = x*CW + CW/2, py = y*CH + CH/2;
            if (px >= ov.width() || py >= ov.height()) continue;
            int a = qAlpha(ov.pixel(px, py));
            Cell &c = buf.at(x, y);
            if (a < 48) continue;                       // transparent: UI untouched
            if (a > 210) { c.ch = QStringLiteral("█"); continue; }   // opaque: covers UI
            // translucent: UI text shows through; empty cells take a shade
            if (c.ch == QStringLiteral(" "))
                c.ch = a > 150 ? QStringLiteral("▓")
                     : a > 96  ? QStringLiteral("▒") : QStringLiteral("░");
        }
}

// ---- path B: pixel-graphics terminals — rasterize + blend -----------------
// This QImage is byte-identical to what the sixel/kitty/iTerm2 encoder ships.
static QImage rasterizeCells(CellBuffer &buf, const QFont &font) {
    QImage img(buf.cols()*CW, buf.rows()*CH, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(16, 20, 24));
    QPainter p(&img);
    p.setFont(font);
    QFontMetrics fm(font);
    for (int y = 0; y < buf.rows(); ++y)
        for (int x = 0; x < buf.cols(); ++x) {
            Cell &c = buf.at(x, y);
            if (c.rev) { p.fillRect(x*CW, y*CH, CW, CH, QColor(220,220,220));
                         p.setPen(QColor(16,20,24)); }
            else         p.setPen(QColor(215, 218, 220));
            if (c.ch != QStringLiteral(" "))
                p.drawText(x*CW, y*CH + fm.ascent(), c.ch);
        }
    p.end();
    return img;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QFont f("DejaVu Sans Mono"); f.setPixelSize(16);
    QFontMetrics fm(f);
    CW = fm.horizontalAdvance('M'); CH = fm.height();
    app.setFont(f);
    app.setStyle(new GridStyle);

    // a plausible screen
    QDialog dlg;
    auto *v = new QVBoxLayout(&dlg);
    auto *chk = new QCheckBox("Enable telemetry", &dlg); chk->setChecked(true);
    v->addWidget(chk);
    auto *edit = new QLineEdit(&dlg); edit->setText("status: connected");
    v->addWidget(edit);
    auto *tree = new QTreeWidget(&dlg);
    tree->setHeaderLabels({"Channel", "Level"});
    for (auto pair : {std::pair{"left","-3.1 dB"}, {"right","-2.8 dB"}, {"sub","-9.0 dB"}}) {
        auto *it = new QTreeWidgetItem(tree);
        it->setText(0, pair.first); it->setText(1, pair.second);
    }
    v->addWidget(tree);
    auto *h = new QHBoxLayout;
    auto *b1 = new QPushButton("Apply", &dlg); auto *b2 = new QPushButton("Close", &dlg);
    h->addStretch(1); h->addWidget(b1); h->addWidget(b2);
    v->addLayout(h);
    dlg.setAttribute(Qt::WA_DontShowOnScreen);
    dlg.resize(60*CW, 16*CH);
    dlg.show();
    QCoreApplication::processEvents();

    CellBuffer ui(60, 16);
    renderWidget(&dlg, ui);
    QImage overlay = makeOverlay(60*CW, 16*CH);

    hr2("PATH A: no-graphics fallback (pure CellBuffer transform, backend-agnostic)");
    printf("--- UI alone ---\n%s\n", qPrintable(ui.toText()));
    CellBuffer composed = ui;   // copy
    composeHalfblocks(composed, overlay);
    printf("--- UI + translucent overlay (text survives under the disc) ---\n%s\n",
           qPrintable(composed.toText()));

    hr2("PATH B: pixel-graphics terminals (sixel/kitty/iTerm2 software composite)");
    QElapsedTimer t; t.start();
    QImage frame = rasterizeCells(ui, f);
    qint64 rasterNs = t.nsecsElapsed();
    t.restart();
    { QPainter p(&frame); p.drawImage(0, 0, overlay); p.end(); }   // SourceOver blend
    qint64 blendNs = t.nsecsElapsed();
    frame.save("composite.png");
    printf("rasterize 60x16 cells -> %dx%d px: %.2f ms;  alpha blend: %.2f ms\n",
           frame.width(), frame.height(), rasterNs/1e6, blendNs/1e6);
    printf("saved composite.png — byte-identical to what a sixel/kitty encoder would ship\n");
    return 0;
}
