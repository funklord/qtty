// qtty/cell.h — L2 cell model (§5.2).
// Spike-fidelity subset: colour, attrs beyond rev/bold, and grapheme-cluster
// width handling are Phase 2 work (§17.1) and extend this type in place.
#pragma once
#include <QString>
#include <QVector>
#include <QRect>
#include <QPixmap>

namespace qtty {

struct Cell {
    QString ch = QStringLiteral(" ");   // one grapheme cluster (§5.2)
    bool rev = false;
    bool bold = false;
    bool operator==(const Cell &o) const { return ch == o.ch && rev == o.rev && bold == o.bold; }
    bool operator!=(const Cell &o) const { return !(*this == o); }
};

// §5.7 cell-anchored placement: a pixel image riding the cell grid.
struct CellImage {
    quint64 key = 0;        // QPixmap::cacheKey() → upload-once identity
    QRect   cellRect;       // anchor + span, in cells
    QPixmap pixmap;         // pixel source (backends encode from this)
};

class CellBuffer {
public:
    CellBuffer(int cols, int rows) : c_(cols), r_(rows), d_(cols * rows) {}
    int cols() const { return c_; }
    int rows() const { return r_; }

    Cell &at(int x, int y) {
        static Cell junk;
        if (x < 0 || y < 0 || x >= c_ || y >= r_) { junk = Cell{}; return junk; }
        return d_[y * c_ + x];
    }
    const Cell &at(int x, int y) const { return const_cast<CellBuffer *>(this)->at(x, y); }

    void fill(const QRect &r, const Cell &v) {
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) at(x, y) = v;
    }
    void text(int x, int y, const QString &s, bool rev = false, bool bold = false) {
        for (int i = 0; i < s.size(); ++i) {
            Cell &c = at(x + i, y);
            c.ch = QString(s[i]); c.rev = rev; c.bold = bold;
        }
    }
    int diffCells(const CellBuffer &prev) const {
        if (prev.c_ != c_ || prev.r_ != r_) return c_ * r_;
        int n = 0;
        for (int i = 0; i < d_.size(); ++i) if (d_[i] != prev.d_[i]) ++n;
        return n;
    }
    QString toText() const {
        QString out;
        for (int y = 0; y < r_; ++y) {
            QString line;
            for (int x = 0; x < c_; ++x) line += d_[y * c_ + x].ch;
            while (line.endsWith(QLatin1Char(' '))) line.chop(1);
            out += line + QLatin1Char('\n');
        }
        return out;
    }

private:
    int c_, r_;
    QVector<Cell> d_;
};

} // namespace qtty
