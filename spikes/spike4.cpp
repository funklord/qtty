// Qtty Phase 0.7 — chat view with scrolling stickers.
//
// Vanilla Qt chat: QListView + QAbstractListModel + QStyledItemDelegate.
// The delegate draws text with QPainter::drawText and stickers with
// QPainter::drawPixmap — NO qtty types in the "app" code. The engine's
// drawPixmap funnel turns each sticker into a CellImage placement (§5.7).
//
// Demonstrated per scroll position:
//   - cell dump with mosaic fallback (NoGraphics tier)
//   - placement table (what the kitty tier ships: image key + cell rect)
//   - PNG composite (what a sixel/kitty user actually sees)

#include "qtty_core.h"
#include <QAbstractListModel>
#include <QStyledItemDelegate>

// --------------------------------------------------------------- "app" code
struct Msg { QString author, text; QPixmap sticker; };

static QPixmap makeSticker(int wCells, int hCells, int kind) {
    QPixmap pm(wCells*CW, hCells*CH);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = pm.rect().adjusted(4, 4, -4, -4);
    if (kind == 0) {                              // smiley
        p.setBrush(QColor(255, 205, 60)); p.setPen(QPen(QColor(120, 90, 0), 3));
        p.drawEllipse(r);
        p.setBrush(QColor(60, 40, 0)); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(r.center().x()-r.width()*0.18, r.center().y()-r.height()*0.15), 5, 7);
        p.drawEllipse(QPointF(r.center().x()+r.width()*0.18, r.center().y()-r.height()*0.15), 5, 7);
        p.setPen(QPen(QColor(60, 40, 0), 4)); p.setBrush(Qt::NoBrush);
        p.drawArc(r.adjusted(r.width()*0.22, r.height()*0.25, -r.width()*0.22, -r.height()*0.12), 200*16, 140*16);
    } else {                                      // heart
        p.setPen(QPen(QColor(140, 0, 30), 3)); p.setBrush(QColor(230, 40, 70));
        QPainterPath path;
        qreal w = r.width(), h = r.height();
        path.moveTo(r.center().x(), r.bottom());
        path.cubicTo(r.left()-w*0.15, r.top()+h*0.45, r.left()+w*0.22, r.top()-h*0.25, r.center().x(), r.top()+h*0.28);
        path.cubicTo(r.right()-w*0.22, r.top()-h*0.25, r.right()+w*0.15, r.top()+h*0.45, r.center().x(), r.bottom());
        p.drawPath(path);
    }
    p.end();
    return pm;
}

class ChatModel : public QAbstractListModel {
public:
    QVector<Msg> msgs;
    int rowCount(const QModelIndex &) const override { return msgs.size(); }
    QVariant data(const QModelIndex &ix, int role) const override {
        if (role == Qt::UserRole) return QVariant::fromValue(ix.row());
        return {};
    }
};

class ChatDelegate : public QStyledItemDelegate {
public:
    ChatModel *m;
    ChatDelegate(ChatModel *mm) : m(mm) {}
    QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &ix) const override {
        const Msg &msg = m->msgs[ix.row()];
        int rows = msg.sticker.isNull() ? 3                       // author + text + gap
                 : 2 + msg.sticker.height()/CH;                   // author + sticker + gap
        return QSize(o.rect.width(), rows * CH);                  // cell-multiple: free
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &ix) const override {
        const Msg &msg = m->msgs[ix.row()];
        QFont bold = o.font; bold.setBold(true);
        p->setFont(bold);
        p->drawText(o.rect.x() + CW, o.rect.y() + QFontMetrics(bold).ascent(),
                    msg.author + ":");
        p->setFont(o.font);
        if (msg.sticker.isNull())
            p->drawText(o.rect.x() + 3*CW, o.rect.y() + CH + QFontMetrics(o.font).ascent(),
                        msg.text);
        else
            p->drawPixmap(o.rect.x() + 3*CW, o.rect.y() + CH, msg.sticker);   // ← vanilla Qt
    }
};

// ------------------------------------------------------------ spike harness
static void mosaic(CellBuffer &buf, const QVector<CellImage> &pl) {
    static const char *shades[] = {" ", "░", "▒", "▓", "█"};
    for (const CellImage &ci : pl) {
        QImage img = ci.pixmap.toImage();
        for (int cy = 0; cy < ci.cellRect.height(); ++cy)
            for (int cx = 0; cx < ci.cellRect.width(); ++cx) {
                int tx = cx * img.width()  / ci.cellRect.width()  + img.width() /(2*ci.cellRect.width());
                int ty = cy * img.height() / ci.cellRect.height() + img.height()/(2*ci.cellRect.height());
                QRgb px = img.pixel(qMin(tx, img.width()-1), qMin(ty, img.height()-1));
                int a = qAlpha(px);
                if (a < 40) continue;
                int lum = qGray(px);
                int idx = a < 128 ? 1 : qBound(1, 4 - lum/64, 4);
                buf.at(ci.cellRect.x()+cx, ci.cellRect.y()+cy).ch = QString::fromUtf8(shades[idx]);
            }
    }
}

static QImage compositePng(CellBuffer &buf, const QVector<CellImage> &pl, const QFont &f) {
    QImage img(buf.cols()*CW, buf.rows()*CH, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(16, 20, 24));
    QPainter p(&img);
    p.setFont(f);
    QFontMetrics fm(f);
    p.setPen(QColor(215, 218, 220));
    for (int y = 0; y < buf.rows(); ++y)
        for (int x = 0; x < buf.cols(); ++x)
            if (buf.at(x,y).ch != QStringLiteral(" "))
                p.drawText(x*CW, y*CH + fm.ascent(), buf.at(x,y).ch);
    for (const CellImage &ci : pl)                       // pixels at cell anchors
        p.drawPixmap(ci.cellRect.x()*CW, ci.cellRect.y()*CH, ci.pixmap);
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

    ChatModel model;
    QPixmap smiley = makeSticker(10, 4, 0);
    QPixmap heart  = makeSticker(10, 4, 1);
    model.msgs = {
        {"dave",  "hey, did the release build pass?", {}},
        {"mika",  "yes! all four products green ☺", {}},
        {"dave",  "", smiley},
        {"mika",  "shipping the TUI beta tonight ★", {}},
        {"dave",  "stickers scroll with the text now", {}},
        {"mika",  "", heart},
        {"dave",  "even over ssh — upload once, re-place by id", {}},
        {"mika",  "vanilla drawPixmap, no qtty types ⚡", {}},
        {"dave",  "", smiley},                        // same pixmap → same key
        {"mika",  "ok that is actually seamless", {}},
    };

    QListView view;
    ChatDelegate delegate(&model);
    view.setModel(&model);
    view.setItemDelegate(&delegate);
    view.setFrameShape(QFrame::NoFrame);              // frame offset would break row alignment
    view.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(52*CW, 16*CH);
    view.show();
    QCoreApplication::processEvents();
    view.verticalScrollBar()->setSingleStep(CH);       // scroll in whole rows

    QSet<quint64> uploaded;
    int frameNo = 0;
    for (int scrollRows : {0, 6, 12}) {
        view.verticalScrollBar()->setValue(scrollRows * CH);
        QCoreApplication::processEvents();

        CellBuffer buf(52, 16);
        CellPaintDevice dev(buf);
        { QPainter p(&dev);
          view.render(&p, QPoint(), QRegion(),
                      QWidget::RenderFlags(QWidget::DrawWindowBackground|QWidget::DrawChildren));
          p.end(); }

        printf("\n\033[1m=== FRAME %d — scrolled %d rows ===\033[0m\n", frameNo, scrollRows);
        printf("placements (what the kitty tier ships):\n");
        for (const CellImage &ci : dev.placements) {
            bool first = !uploaded.contains(ci.key);
            if (first) uploaded.insert(ci.key);
            printf("  image %llx  at cells (%d,%d) %dx%d   %s\n",
                   (unsigned long long)ci.key,
                   ci.cellRect.x(), ci.cellRect.y(), ci.cellRect.width(), ci.cellRect.height(),
                   first ? "UPLOAD pixels + place" : "re-place by id (~30 bytes)");
        }

        CellBuffer fallback = buf;
        mosaic(fallback, dev.placements);
        printf("--- NoGraphics tier (mosaic) ---\n%s", qPrintable(fallback.toText()));

        compositePng(buf, dev.placements, f)
            .save(QString("chat_scroll_%1.png").arg(scrollRows));
        ++frameNo;
    }
    printf("\nunique images uploaded across all frames: %d (of %d placements seen)\n",
           int(uploaded.size()), 0);
    return 0;
}
