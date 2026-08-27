// src/core/cell_buffer.cpp -- L2 implementation (sections 5.2, 6).
#include "qtty/cell.h"
#include <QTextBoundaryFinder>

namespace Qtty {

// ---- cluster width ---------------------------------------------------------
static bool isWideCodepoint(char32_t u) {
	// East Asian Wide/Fullwidth + common emoji blocks. Deliberately compact;
	// refined against terminals empirically (section 5.2).
	return (u >= 0x1100  && u <= 0x115F)   // Hangul Jamo
	    || (u >= 0x2E80  && u <= 0x303E)   // CJK Radicals..CJK Symbols
	    || (u >= 0x3041  && u <= 0x33FF)   // Kana..CJK Compat
	    || (u >= 0x3400  && u <= 0x4DBF)
	    || (u >= 0x4E00  && u <= 0x9FFF)   // CJK Unified
	    || (u >= 0xA000  && u <= 0xA4CF)   // Yi
	    || (u >= 0xAC00  && u <= 0xD7A3)   // Hangul Syllables
	    || (u >= 0xF900  && u <= 0xFAFF)
	    || (u >= 0xFE30  && u <= 0xFE4F)
	    || (u >= 0xFF00  && u <= 0xFF60)   // Fullwidth forms
	    || (u >= 0xFFE0  && u <= 0xFFE6)
	    || (u >= 0x1F300 && u <= 0x1FAFF)  // emoji blocks
	    || (u >= 0x20000 && u <= 0x3FFFD); // CJK Ext B+
}

int clusterWidth(QStringView cluster) {
	if (cluster.isEmpty()) return 1;
	char32_t first = cluster.at(0).unicode();
	if (cluster.size() >= 2 && cluster.at(0).isHighSurrogate() && cluster.at(1).isLowSurrogate())
		first = QChar::surrogateToUcs4(cluster.at(0), cluster.at(1));
	if (isWideCodepoint(first)) return 2;
	// VS16 forces emoji presentation -> wide
	for (QChar c : cluster) if (c.unicode() == 0xFE0F) return 2;
	return 1;
}

QVector<QString> toClusters(const QString &text) {
	QVector<QString> out;
	QTextBoundaryFinder f(QTextBoundaryFinder::Grapheme, text);
	int prev = 0;
	while (f.toNextBoundary() != -1) {
		int b = f.position();
		if (b > prev) out.append(text.mid(prev, b - prev));
		prev = b;
	}
	return out;
}

// ---- CellBuffer ------------------------------------------------------------
void CellBuffer::fill(const QRect &r, const Cell &v) {
	for (int y = r.top(); y <= r.bottom(); ++y)
		for (int x = r.left(); x <= r.right(); ++x) at(x, y) = v;
}

void CellBuffer::clearWidePartner(int x, int y) {
	if (x < 0 || y < 0 || x >= c_ || y >= r_) return;
	Cell &c = d_[y * c_ + x];
	if (c.width == 0 && x > 0) {                        // continuation: clear lead
		Cell &lead = d_[y * c_ + x - 1];
		if (lead.width == 2) lead = Cell{};
		c = Cell{};
	} else if (c.width == 2 && x + 1 < c_) {            // lead: clear continuation
		Cell &cont = d_[y * c_ + x + 1];
		if (cont.width == 0) cont = Cell{};
	}
}

void CellBuffer::putCluster(int x, int y, const QString &cluster,
                            Color fg, Color bg, Attrs attrs) {
	if (x < 0 || y < 0 || x >= c_ || y >= r_) return;
	const int w = clusterWidth(cluster);
	clearWidePartner(x, y);
	if (w == 2) clearWidePartner(x + 1, y);
	Cell &c = d_[y * c_ + x];
	// A Default bg means "no opinion": glyphs written over a highlight fill
	// keep it (selection rendering, section 17.2). Explicit backgrounds replace.
	if (bg.kind() == Color::Default) bg = c.bg;
	c.ch = cluster; c.fg = fg; c.bg = bg; c.attrs = attrs; c.width = quint8(w);
	if (w == 2 && x + 1 < c_) {
		Cell &cont = d_[y * c_ + x + 1];
		cont = Cell{}; cont.ch.clear(); cont.width = 0;
		cont.fg = fg; cont.bg = bg; cont.attrs = attrs;
	}
}

int CellBuffer::text(int x, int y, const QString &s, Color fg, Color bg, Attrs attrs) {
	int consumed = 0;
	for (const QString &cl : toClusters(s)) {
		putCluster(x + consumed, y, cl, fg, bg, attrs);
		consumed += clusterWidth(cl);
	}
	return consumed;
}

QRegion CellBuffer::diff(const CellBuffer &prev) const {
	if (prev.c_ != c_ || prev.r_ != r_) return QRegion(0, 0, c_, r_);
	QRegion damage;
	for (int y = 0; y < r_; ++y) {
		int runStart = -1;
		for (int x = 0; x <= c_; ++x) {
			const bool changed = x < c_ && d_[y * c_ + x] != prev.d_[y * c_ + x];
			if (changed && runStart < 0) runStart = x;
			if (!changed && runStart >= 0) {
				damage += QRect(runStart, y, x - runStart, 1);
				runStart = -1;
			}
		}
	}
	return damage;
}

int CellBuffer::diffCells(const CellBuffer &prev) const {
	if (prev.c_ != c_ || prev.r_ != r_) return c_ * r_;
	int n = 0;
	for (int i = 0; i < d_.size(); ++i) if (d_[i] != prev.d_[i]) ++n;
	return n;
}

QString CellBuffer::toText() const {
	QString out;
	for (int y = 0; y < r_; ++y) {
		QString line;
		for (int x = 0; x < c_; ++x) {
			const Cell &c = d_[y * c_ + x];
			if (c.width == 0) continue;                 // continuation: no glyph
			line += c.ch;
		}
		while (line.endsWith(QLatin1Char(' '))) line.chop(1);
		out += line + QLatin1Char('\n');
	}
	return out;
}

} // namespace Qtty
