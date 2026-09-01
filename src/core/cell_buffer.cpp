// src/core/cell_buffer.cpp -- L2 implementation (sections 5.2, 6).
#include "qtty/cell.h"
#include <QHash>
#include <QStringList>
#include <algorithm>
#include <QTextBoundaryFinder>

namespace Qtty {

// ---- cluster width ---------------------------------------------------------
static bool is_wide_codepoint(char32_t u) {
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

int cluster_width(QStringView cluster) {
	if (cluster.isEmpty()) return 1;
	char32_t first = cluster.at(0).unicode();
	if (cluster.size() >= 2 && cluster.at(0).isHighSurrogate() && cluster.at(1).isLowSurrogate())
		first = QChar::surrogateToUcs4(cluster.at(0), cluster.at(1));
	if (is_wide_codepoint(first)) return 2;
	// VS16 forces emoji presentation -> wide
	for (QChar c : cluster) if (c.unicode() == 0xFE0F) return 2;
	return 1;
}

QVector<QString> to_clusters(const QString &text) {
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

void CellBuffer::clear_wide_partner(int x, int y) {
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

void CellBuffer::put_cluster(int x, int y, const QString &cluster,
                            Color fg, Color bg, Attrs attrs) {
	if (x < 0 || y < 0 || x >= c_ || y >= r_) return;
	const int w = cluster_width(cluster);
	// A width-2 cluster is a lead plus a continuation cell (section 5.2), and
	// in the last column there is no continuation to have. Writing it anyway
	// produced a cell claiming two columns in a one-column space: to_text()
	// emitted the glyph, the row rendered one column wider than the buffer,
	// and a terminal either wrapped it onto the next line or truncated it.
	//
	// A blank is what fits. Every terminal that lays out wide text does the
	// same, and it keeps the invariant the whole cell model rests on -- a
	// width-2 cell always has its partner -- instead of breaking it at exactly
	// the edge nothing had tested.
	if (w == 2 && x + 1 >= c_) {
		clear_wide_partner(x, y);
		Cell &edge = d_[y * c_ + x];
		edge = Cell{};
		edge.fg = fg;
		edge.bg = bg.kind() == Color::Default ? edge.bg : bg;
		edge.attrs = attrs;
		return;
	}
	clear_wide_partner(x, y);
	if (w == 2) clear_wide_partner(x + 1, y);
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
	for (const QString &cl : to_clusters(s)) {
		const int w = cluster_width(cl);
		// Stop at the edge rather than walking past it. This used to add the
		// width of every cluster it was given, including ones put_cluster then
		// refused for being out of bounds, so it reported 6 for a 4-column
		// buffer -- and a caller advancing a cursor by the return value
		// carried on off the end of the row.
		if (x + consumed + w > c_) break;
		put_cluster(x + consumed, y, cl, fg, bg, attrs);
		consumed += w;
	}
	return consumed;
}

QRegion CellBuffer::diff(const CellBuffer &prev) const {
	if (prev.c_ != c_ || prev.r_ != r_) return QRegion(0, 0, c_, r_);
	QRegion damage;
	for (int y = 0; y < r_; ++y) {
		int run_start = -1;
		for (int x = 0; x <= c_; ++x) {
			const bool changed = x < c_ && d_[y * c_ + x] != prev.d_[y * c_ + x];
			if (changed && run_start < 0) run_start = x;
			if (!changed && run_start >= 0) {
				damage += QRect(run_start, y, x - run_start, 1);
				run_start = -1;
			}
		}
	}
	return damage;
}

int CellBuffer::diff_cells(const CellBuffer &prev) const {
	if (prev.c_ != c_ || prev.r_ != r_) return c_ * r_;
	int n = 0;
	for (int i = 0; i < d_.size(); ++i) if (d_[i] != prev.d_[i]) ++n;
	return n;
}

namespace {

// One printable character per attribute mask. Attrs is six flags, so 64
// combinations, and a plane that showed only the first set flag would go
// green when a second one stopped being drawn. The whole mask is encoded.
//
// '.' for none, so the common case reads as background and a set attribute
// stands out. The rest are digits then letters, which keeps a plane
// diffable and greppable -- a fixture is read by people.
QChar attr_char(Attrs a) {
	const int mask = int(a) & 0x3f;
	if (mask == 0) return QLatin1Char('.');
	static const char *const table =
	    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+=";
	return QLatin1Char(table[mask]);
}

QString attr_names(Attrs a) {
	QStringList on;
	if (a & Attr::Bold)      on << QStringLiteral("bold");
	if (a & Attr::Dim)       on << QStringLiteral("dim");
	if (a & Attr::Italic)    on << QStringLiteral("italic");
	if (a & Attr::Underline) on << QStringLiteral("underline");
	if (a & Attr::Reverse)   on << QStringLiteral("reverse");
	if (a & Attr::Strike)    on << QStringLiteral("strike");
	return on.join(QLatin1Char('+'));
}

QString colour_name(const Color &c) {
	switch (c.kind()) {
	case Color::Default: return QStringLiteral("default");
	case Color::Indexed: return QStringLiteral("index:%1").arg(c.index());
	// Six digits, not eight: the alpha byte is masked off, so an eighth
	// pair would print a constant 00 in every fixture and read as colour.
	case Color::Rgb:     return QStringLiteral("#%1").arg(c.value() & 0xffffffu,
	                                                      6, 16, QLatin1Char('0'));
	}
	return QStringLiteral("?");
}

} // namespace

QString CellBuffer::to_snapshot() const {
	// A colour PAIR gets a letter, rather than each colour getting one. A
	// terminal frame uses a handful of pairs -- the ground, a selection, a
	// highlight -- so the plane stays short and the legend stays readable.
	// Keyed on the printed names so two colours that emit the same bytes
	// share a letter, which is what a reader comparing two fixtures cares
	// about.
	QVector<QString> order;
	QHash<QString, QChar> letters;
	static const char *const alphabet =
	    ".abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	// '.' is the default pair, always, whatever order the cells arrive in --
	// so the ordinary ground reads as background in every fixture. Everything
	// else takes the next letter in first-seen order, which means a frame's
	// legend reads top-left to bottom-right.
	int next = 1;                                   // index 0 is '.', reserved
	const auto letter_for = [&](const Cell &c) {
		const QString key = colour_name(c.fg) + QLatin1Char(' ') + colour_name(c.bg);
		const auto it = letters.constFind(key);
		if (it != letters.constEnd()) return it.value();
		const bool plain = c.fg.kind() == Color::Default
		                && c.bg.kind() == Color::Default;
		// Running out of letters would silently merge two pairs into one and
		// make the plane lie, so it is said out loud instead.
		const QChar ch = plain ? QLatin1Char('.')
		               : QLatin1Char(next < 62 ? alphabet[next++] : '?');
		letters.insert(key, ch);
		order.append(key);
		return ch;
	};

	QString glyphs, attrs, colours;
	for (int y = 0; y < r_; ++y) {
		QString g, a, k;
		for (int x = 0; x < c_; ++x) {
			const Cell &c = d_[y * c_ + x];
			// The glyph plane skips a continuation cell, because the wide
			// cluster in the lead already occupies both columns. The other
			// two planes must NOT skip it, or they come up a character short
			// under every wide cluster and the columns stop lining up -- which
			// is the one thing this format promises. One character per CELL
			// there, one per CLUSTER here, and all three planes end the same
			// number of display columns wide.
			if (c.width != 0) g += c.ch;
			a += attr_char(c.attrs);
			k += letter_for(c);
		}
		// Trailing default cells carry nothing and only make a diff noisier.
		// All three planes are trimmed on the same rule so the columns stay
		// readable straight down.
		while (g.endsWith(QLatin1Char(' '))) g.chop(1);
		while (a.endsWith(QLatin1Char('.'))) a.chop(1);
		while (k.endsWith(QLatin1Char('.'))) k.chop(1);
		glyphs  += g + QLatin1Char('\n');
		attrs   += a + QLatin1Char('\n');
		colours += k + QLatin1Char('\n');
	}

	// A plane with nothing in it collapses to one line. A frame drawn entirely
	// in the terminal's own colours is the common case, and fifteen blank rows
	// twice over buries the glyph plane a reader came for. The marker is still
	// a value: an attribute appearing anywhere replaces the line with a plane,
	// which is exactly as loud a diff as a changed row would be.
	const auto plane = [](const QString &name, const QString &body,
	                      QChar empty) {
		QString flat = body;
		flat.remove(QLatin1Char('\n'));
		flat.remove(empty);
		return QStringLiteral("--- %1 ---\n").arg(name)
		     + (flat.isEmpty() ? QStringLiteral("(none)\n") : body);
	};

	QString out = glyphs;
	out += plane(QStringLiteral("attrs"), attrs, QLatin1Char('.'));
	out += plane(QStringLiteral("colours"), colours, QLatin1Char('.'));
	out += QStringLiteral("--- legend ---\n");
	// Named in the order the letters were handed out, so the legend reads
	// top-left to bottom-right of the frame.
	QVector<QPair<QChar, QString>> rows;
	for (const QString &key : order) rows.append({letters.value(key), key});
	std::sort(rows.begin(), rows.end(),
	          [](const auto &l, const auto &r) { return l.first < r.first; });
	for (const auto &row : rows)
		out += QStringLiteral("%1 fg=%2 bg=%3\n")
		       .arg(row.first)
		       .arg(row.second.section(QLatin1Char(' '), 0, 0))
		       .arg(row.second.section(QLatin1Char(' '), 1, 1));
	// The attribute legend is fixed rather than per-frame: a reader meeting a
	// letter needs to look it up whether or not this frame used it.
	out += QStringLiteral("attrs: . none");
	for (int mask = 1; mask < 64; ++mask) {
		const Attrs a = Attrs(QFlag(mask));
		if (attrs.contains(attr_char(a)))
			out += QStringLiteral(", %1 %2").arg(attr_char(a)).arg(attr_names(a));
	}
	out += QLatin1Char('\n');
	return out;
}

QString CellBuffer::to_text() const {
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
