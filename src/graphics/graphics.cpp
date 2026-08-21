// src/graphics/graphics.cpp — encoders, negotiation, rasterizer, halfblocks.
#include "qtty/graphics.h"
#include "qtty/grid.h"
#include <QFontMetrics>
#include <QPainter>
#include <QBuffer>

namespace Qtty {

// ---- negotiation -----------------------------------------------------------
Capabilities::GraphicsMode detectGraphicsMode() {
    const QByteArray term = qgetenv("TERM").toLower();
    const QByteArray prog = qgetenv("TERM_PROGRAM").toLower();

    if (!qgetenv("KITTY_WINDOW_ID").isEmpty() || term.contains("kitty")
        || term.contains("ghostty"))
        return Capabilities::KittyAlpha;          // kitty protocol, alpha over text
    if (prog.contains("wezterm"))
        return Capabilities::Kitty;               // kitty protocol, no alpha-over-text
    if (prog.contains("iterm"))
        return Capabilities::ITerm2;
    if (term.contains("sixel") || term.contains("mlterm") || term.contains("foot")
        || prog.contains("mintty"))
        return Capabilities::Sixel;
    return Capabilities::Halfblocks;              // every colour terminal
}

// ---- sixel -----------------------------------------------------------------
QByteArray encodeSixel(const QImage &src) {
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();

    // Quantise via the xterm-256 cube; collect used registers.
    QVector<int> idx(w * h, -1);                  // -1 = transparent
    bool used[256] = {};
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(line[x]) < 128) continue;
            int c = Color::rgb(line[x]).toXterm256();
            if (c < 0) c = 15;
            idx[y * w + x] = c;
            used[c] = true;
        }
    }

    QByteArray out = "\033P0;1;0q";               // P2=1: untouched -> transparent
    out += "\"1;1;" + QByteArray::number(w) + ';' + QByteArray::number(h);

    auto reg = [](int c) -> QRgb {                // xterm-256 index -> rgb
        if (c < 16) {
            static const QRgb basic[16] = {
                0xFF000000,0xFF800000,0xFF008000,0xFF808000,0xFF000080,0xFF800080,
                0xFF008080,0xFFC0C0C0,0xFF808080,0xFFFF0000,0xFF00FF00,0xFFFFFF00,
                0xFF0000FF,0xFFFF00FF,0xFF00FFFF,0xFFFFFFFF };
            return basic[c];
        }
        if (c >= 232) { int v = 8 + (c - 232) * 10; return qRgb(v, v, v); }
        int i = c - 16;
        auto lv = [](int l) { return l ? 55 + l * 40 : 0; };
        return qRgb(lv(i / 36), lv((i / 6) % 6), lv(i % 6));
    };
    for (int c = 0; c < 256; ++c)
        if (used[c]) {
            QRgb v = reg(c);
            out += '#' + QByteArray::number(c) + ";2;"
                 + QByteArray::number(qRed(v)   * 100 / 255) + ';'
                 + QByteArray::number(qGreen(v) * 100 / 255) + ';'
                 + QByteArray::number(qBlue(v)  * 100 / 255);
        }

    for (int band = 0; band < h; band += 6) {
        bool firstColor = true;
        for (int c = 0; c < 256; ++c) {
            if (!used[c]) continue;
            // does this colour appear in the band?
            bool present = false;
            for (int y = band; y < qMin(band + 6, h) && !present; ++y)
                for (int x = 0; x < w; ++x)
                    if (idx[y * w + x] == c) { present = true; break; }
            if (!present) continue;
            if (!firstColor) out += '$';          // carriage return within band
            firstColor = false;
            out += '#' + QByteArray::number(c);
            int runChar = -1, runLen = 0;
            auto flush = [&] {
                if (runLen <= 0) return;
                if (runLen > 3) out += '!' + QByteArray::number(runLen) + char(runChar);
                else for (int i = 0; i < runLen; ++i) out += char(runChar);
            };
            for (int x = 0; x < w; ++x) {
                int bits = 0;
                for (int dy = 0; dy < 6 && band + dy < h; ++dy)
                    if (idx[(band + dy) * w + x] == c) bits |= 1 << dy;
                const int ch = 63 + bits;         // '?' + bits
                if (ch == runChar) ++runLen;
                else { flush(); runChar = ch; runLen = 1; }
            }
            flush();
        }
        out += '-';                               // next band
    }
    out += "\033\\";
    return out;
}

// ---- kitty -----------------------------------------------------------------
static QByteArray kittyChunks(const QByteArray &ctrl, const QByteArray &payload) {
    QByteArray out;
    const int N = 4096;
    for (int off = 0; off < payload.size(); off += N) {
        const bool last = off + N >= payload.size();
        out += "\033_G";
        if (off == 0) out += ctrl + (payload.size() > N ? ",m=1" : "");
        else out += QByteArray("m=") + (last ? "0" : "1");
        out += ';';
        out += payload.mid(off, N);
        out += "\033\\";
    }
    if (payload.isEmpty()) out += "\033_G" + ctrl + ";\033\\";
    return out;
}

QByteArray encodeKittyImage(quint32 id, const QImage &src, int z) {
    const QImage img = src.convertToFormat(QImage::Format_RGBA8888);
    QByteArray raw(reinterpret_cast<const char *>(img.constBits()),
                   int(img.sizeInBytes()));
    QByteArray ctrl = "a=T,f=32,q=2"
        ",i=" + QByteArray::number(id)
        + ",s=" + QByteArray::number(img.width())
        + ",v=" + QByteArray::number(img.height());
    if (z) ctrl += ",z=" + QByteArray::number(z);
    return kittyChunks(ctrl, raw.toBase64());
}

QByteArray kittyPlace(quint32 id, int z) {
    QByteArray ctrl = "a=p,q=2,i=" + QByteArray::number(id);
    if (z) ctrl += ",z=" + QByteArray::number(z);
    return "\033_G" + ctrl + ";\033\\";
}

QByteArray kittyDeleteAll() { return "\033_Ga=d,d=a,q=2;\033\\"; }

// ---- iTerm2 ----------------------------------------------------------------
QByteArray encodeITerm2(const QImage &img, int wCells, int hCells) {
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return "\033]1337;File=inline=1;width=" + QByteArray::number(wCells)
         + ";height=" + QByteArray::number(hCells)
         + ";preserveAspectRatio=0:" + png.toBase64() + "\a";
}

// ---- rasterizer ------------------------------------------------------------
QImage rasterize(const CellBuffer &frame, const QFont &font) {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    QImage img(frame.cols() * cw, frame.rows() * ch,
               QImage::Format_ARGB32_Premultiplied);
    const QRgb defBg = qRgb(16, 20, 24), defFg = qRgb(215, 218, 220);
    img.fill(defBg);
    QPainter p(&img);
    QFontMetrics fm(font);
    for (int y = 0; y < frame.rows(); ++y)
        for (int x = 0; x < frame.cols(); ++x) {
            const Cell &c = frame.at(x, y);
            if (c.width == 0) continue;
            QRgb fg = c.fg.kind() == Color::Rgb ? c.fg.value() : defFg;
            QRgb bg = c.bg.kind() == Color::Rgb ? c.bg.value() : defBg;
            if (c.attrs & Attr::Reverse) std::swap(fg, bg);
            if (bg != defBg || (c.attrs & Attr::Reverse))
                p.fillRect(x * cw, y * ch, cw * c.width, ch, QColor::fromRgb(bg));
            if (c.ch != QStringLiteral(" ")) {
                QFont f = font;
                f.setBold(c.attrs & Attr::Bold);
                f.setItalic(c.attrs & Attr::Italic);
                f.setUnderline(c.attrs & Attr::Underline);
                p.setFont(f);
                p.setPen(QColor::fromRgb(fg));
                p.drawText(x * cw, y * ch + fm.ascent(), c.ch);
            }
        }
    p.end();
    return img;
}

// ---- halfblock fallback ----------------------------------------------------
static QRgb blend(QRgb over, int a, QRgb under) {
    return qRgb((qRed(over)   * a + qRed(under)   * (255 - a)) / 255,
                (qGreen(over) * a + qGreen(under) * (255 - a)) / 255,
                (qBlue(over)  * a + qBlue(under)  * (255 - a)) / 255);
}

void composeHalfblocks(CellBuffer &frame, const QImage &src, const QRect &cellRect) {
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const QRgb underDefault = qRgb(16, 20, 24);
    for (int cy = 0; cy < cellRect.height(); ++cy)
        for (int cx = 0; cx < cellRect.width(); ++cx) {
            const int X = cellRect.x() + cx, Y = cellRect.y() + cy;
            if (X < 0 || Y < 0 || X >= frame.cols() || Y >= frame.rows()) continue;
            // two vertical samples per cell (2x vertical resolution)
            auto sample = [&](double fy) -> QRgb {
                int sx = qMin(int((cx + 0.5) * img.width() / cellRect.width()), img.width() - 1);
                int sy = qMin(int((cy + fy) * img.height() / cellRect.height()), img.height() - 1);
                return img.pixel(sx, sy);
            };
            const QRgb top = sample(0.25), bot = sample(0.75);
            const int aT = qAlpha(top), aB = qAlpha(bot);
            if (aT < 40 && aB < 40) continue;                  // transparent: untouched
            Cell &cell = frame.at(X, Y);
            if (aT > 200 && aB > 200) {                        // opaque: 2 pixels
                cell.ch = QStringLiteral("▀");
                cell.fg = Color::rgb(QRgb(top | 0xFF000000));
                cell.bg = Color::rgb(QRgb(bot | 0xFF000000));
                cell.attrs = {}; cell.width = 1;
            } else if (aT > 200 || aB > 200) {                 // half-covered edge
                const bool topHalf = aT > 200;
                cell.ch = topHalf ? QStringLiteral("▀") : QStringLiteral("▄");
                cell.fg = Color::rgb(QRgb((topHalf ? top : bot) | 0xFF000000));
                // keep whatever bg is behind the uncovered half
                cell.attrs = {}; cell.width = 1;
            } else {                                           // translucent: tint bg,
                const int a = qMax(aT, aB);                    // glyph stays readable
                const QRgb under = cell.bg.kind() == Color::Rgb ? cell.bg.value()
                                                                : underDefault;
                cell.bg = Color::rgb(blend(top, a, under));
            }
        }
}

} // namespace Qtty
