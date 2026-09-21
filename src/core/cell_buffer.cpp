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
	    // THE WIDE EMOJI BELOW 0x1F300, which the single range under this
	    // one missed. Every code point here is East Asian Width W, so a
	    // terminal advances two columns for it and this library advanced
	    // one -- and a width disagreement is not a narrow glyph, it is
	    // every column after it on the row being wrong.
	    //
	    // Measured before they were added: U+1F004 the mahjong tile,
	    // U+1F0CF the joker, U+1F18E, U+1F191..1F19A the squared
	    // letters, and the squared-CJK blocks from U+1F200 all answered
	    // 1. Taken from EastAsianWidth.txt rather than from a guess at
	    // what looks like an emoji: 1F000..1F003 and 1F005..1F02B are
	    // narrow in the same block, so a range over the whole block
	    // would be wrong in the other direction.
	    || u == 0x1F004                    // mahjong red dragon
	    || u == 0x1F0CF                    // playing card black joker
	    || u == 0x1F18E                    // negative squared AB
	    || (u >= 0x1F191 && u <= 0x1F19A)  // squared CL..VS
	    || (u >= 0x1F200 && u <= 0x1F202)
	    || (u >= 0x1F210 && u <= 0x1F23B)
	    || (u >= 0x1F240 && u <= 0x1F248)
	    || (u >= 0x1F250 && u <= 0x1F251)
	    || (u >= 0x1F260 && u <= 0x1F265)
	    || (u >= 0x1F300 && u <= 0x1FAFF)  // emoji blocks
	    || (u >= 0x20000 && u <= 0x3FFFD); // CJK Ext B+
}

// A character nothing can display and nothing should: the C0 controls, DEL,
// and the C1 range. Qt's own text layout already turns a carriage return into
// a space and leaves NUL, BEL and ESC as themselves, so three of them reached
// Cell::ch verbatim -- and AnsiBackend writes Cell::ch to the wire unaltered.
// An application putting an escape character in a QLabel was therefore
// writing an escape introducer into the terminal's input stream, and whatever
// text followed it was read as a control sequence rather than as text.
//
// A space is what the carriage return already became: one column, nothing
// shown, and no meaning on the wire.
static bool is_control(char32_t u) {
	// THE LINE AND PARAGRAPH SEPARATORS, which are exactly what the
	// paragraph above describes -- a character nothing can display and
	// nothing should -- and which are not C0, C1 or DEL, so the ranges
	// missed them. Measured straight into a buffer, past Qt's layout:
	// ESC and U+0085 became a space and U+2028 and U+2029 reached the
	// cell verbatim.
	//
	// It is not only that they draw as a box. A terminal is entitled to
	// read U+2028 as a line break, and one arriving in the middle of a
	// row would take the rest of the frame's geometry with it -- the
	// same argument as the escape introducer in the paragraph above, one
	// character class along. They turn up in text pasted out of a word
	// processor or a PDF, and paste is a path this library carries.
	//
	// Separator_Space is NOT here: a no-break space is a space and
	// belongs in a cell as itself.
	const QChar::Category cat = QChar::category(u);
	if (cat == QChar::Separator_Line || cat == QChar::Separator_Paragraph)
		return true;
	return u < 0x20 || u == 0x7f || (u >= 0x80 && u <= 0x9f);
}

// Characters meant to occupy no column at all. A lone one arrives as its own
// grapheme cluster and was given a whole cell, which shifted every character
// after it one column to the right. Inside an emoji sequence a joiner belongs
// to its cluster and never reaches here alone, so this only ever sees the
// stray ones.
//
// The bidi embedding, override and isolate controls are in the list for the
// same reason as the rest -- they are zero width -- and dropping them also
// takes away the display-spoofing trick that reorders text a reader trusts.
static bool is_zero_width(char32_t u) {
	// COMBINING MARKS, asked of Qt rather than listed. The paragraph above
	// describes this defect exactly -- a lone one is its own cluster and
	// took a whole cell -- and the list below fixed it for the FORMAT
	// characters only, leaving every combining mark in Unicode out.
	//
	// Measured before this: a lone combining acute and a lone cedilla each
	// took one column, and a lone VARIATION SELECTOR took **two**, because
	// the emoji-presentation rule fired on a cluster that is nothing but
	// the selector. Two columns for a character with no glyph at all is
	// the worst of the three.
	//
	// By category and not by range: Mn is Nonspacing_Mark and Me is
	// Enclosing_Mark, both zero-width by definition, and between them they
	// carry every combining mark and every variation selector without a
	// table to keep current. Mc, Spacing_Combining_Mark, is deliberately
	// NOT here -- those do take a column.
	const QChar::Category cat = QChar::category(u);
	if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_Enclosing)
		return true;
	return u == 0x00ad                     // soft hyphen
	    || (u >= 0x200b && u <= 0x200f)    // ZWSP, ZWNJ, ZWJ, LRM, RLM
	    || (u >= 0x202a && u <= 0x202e)    // bidi embedding and override
	    || (u >= 0x2060 && u <= 0x2064)    // word joiner, invisible operators
	    || (u >= 0x2066 && u <= 0x2069)    // bidi isolates
	    || u == 0xfeff;                    // zero width no-break space
}

// Default true, which is what every terminal this project has met does and
// what `Capabilities::unicode_wide` itself defaults to. A tree that read the
// flag correctly and defaulted it false would render every CJK document
// wrongly on the terminals that are fine.
static bool s_wide_clusters = true;

void set_wide_clusters(bool honoured) { s_wide_clusters = honoured; }
bool wide_clusters() { return s_wide_clusters; }

int cluster_width(QStringView cluster) {
	if (cluster.isEmpty()) return 1;
	char32_t first = cluster.at(0).unicode();
	if (cluster.size() >= 2 && cluster.at(0).isHighSurrogate() && cluster.at(1).isLowSurrogate())
		first = QChar::surrogateToUcs4(cluster.at(0), cluster.at(1));
	// Every character zero width, and the cluster takes no column. One that
	// merely starts with such a character does not qualify: a stray joiner
	// followed by a real glyph is still a glyph.
	bool all_zero = true;
	for (QChar c : cluster) if (!is_zero_width(c.unicode())) { all_zero = false; break; }
	if (all_zero) return 0;
	// Zero width is not affected and is asked first, above: the capability
	// names wcwidth-2, and a terminal that will not advance two columns for
	// a wide cluster says nothing about whether it advances none for a
	// combining mark. Answering both with one flag would be inventing a
	// second capability out of this one.
	if (!s_wide_clusters) return 1;
	if (is_wide_codepoint(first)) return 2;
	// A FLAG, which is two regional indicators and one cluster. They are
	// East Asian Width NEUTRAL individually, so the range test above
	// cannot catch them and should not: a lone indicator is a letter in a
	// box and takes one column. A PAIR is a flag, and a terminal draws it
	// in two -- both by the modern cluster rule and by the old one of
	// adding wcwidth per code point, which gives 1 + 1.
	//
	// Measured before this: a Swedish flag came back as one cell, so
	// everything after it on the row sat one column left of where the
	// terminal put it.
	int indicators = 0;
	for (int i = 0; i + 1 < cluster.size(); ++i) {
		if (!cluster.at(i).isHighSurrogate()
		    || !cluster.at(i + 1).isLowSurrogate()) continue;
		const char32_t u = QChar::surrogateToUcs4(cluster.at(i),
		                                          cluster.at(i + 1));
		if (u >= 0x1F1E6 && u <= 0x1F1FF) ++indicators;
	}
	if (indicators >= 2) return 2;
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
		for (int x = r.left(); x <= r.right(); ++x)
			if (writable(x, y)) at(x, y) = v;
}

void CellBuffer::clear_wide_partner(int x, int y) {
	// Clipped like any other write. Without this a wide cluster landing on
	// the clip's edge would reach past it to clear the partner cell -- which
	// is the one thing the clip exists to stop, arriving by the one path that
	// does not look like a write.
	if (!writable(x, y)) return;
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
	if (!writable(x, y)) return;
	const int w = cluster_width(cluster);
	// Nothing to occupy: leave the cell exactly as it was, partner and all.
	if (w == 0) return;
	// A control never reaches a cell as itself. Substituted here rather than
	// in the backend because to_text() and the snapshot fixtures read the
	// buffer directly, and a buffer holding an escape character is already
	// wrong whoever writes it out.
	QString glyph = cluster;
	if (!glyph.isEmpty() && is_control(glyph.at(0).unicode()))
		glyph = QStringLiteral(" ");
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
	// The same rule for the clip's right edge as for the buffer's: there is
	// no continuation cell to be had, so a blank is what fits. A lead written
	// without its partner is the corruption section 5.2 is built to prevent,
	// and a clip boundary is a boundary like any other.
	if (w == 2 && !writable(x + 1, y)) {
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
	c.ch = glyph; c.fg = fg; c.bg = bg; c.attrs = attrs; c.width = quint8(w);
	if (w == 2 && writable(x + 1, y)) {
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

// One printable character per attribute mask. The low SIX flags live here,
// so 64 combinations, and a plane that showed only the first set flag would
// go green when a second one stopped being drawn. The whole of those six is
// encoded.
//
// '.' for none, so the common case reads as background and a set attribute
// stands out. The rest are digits then letters, which keeps a plane
// diffable and greppable -- a fixture is read by people.
//
// SIX and not seven, with the seventh on a plane of its own below. That
// split is forced rather than preferred. One character per cell is what
// makes the three planes line up in columns, which is the one thing this
// format promises; and an injective map from the 127 non-empty masks of
// seven flags onto single characters DOES NOT EXIST in printable ASCII,
// which has 94 non-space characters and 93 once '.' is spent on the empty
// mask. The two ways to keep one plane were a wider alphabet -- accented
// letters, in an artefact whose stated virtue is that people read and grep
// it -- or letting two masks share a character, which is precisely the lie
// this encoding exists to prevent.
QChar attr_char(Attrs a) {
	const int mask = int(a) & 0x3f;
	if (mask == 0) return QLatin1Char('.');
	static const char *const table =
	    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+=";
	return QLatin1Char(table[mask]);
}

// The seventh flag, on its own plane, one character per cell so its columns
// line up with the other three.
//
// '.' and 'b' rather than a second base-64 digit: with a single flag up here
// a digit would print '1', and '1' one plane above means Bold. A reader
// comparing two planes column by column would meet one character meaning two
// things, which is the cost the separate plane was supposed to buy off.
//
// An EIGHTH attribute does not extend this by adding a third plane. It needs
// a decision here, and the honest one is likely to make this a real digit
// plane carrying bits 6 and up, with the legend saying so. Whoever arrives
// with one should know what this plane is for: `& 0x3f` used to drop Blink
// on the floor, and an attribute a snapshot cannot show agrees with every
// later run of every fixture for ever -- there is no failing test at the end
// of that, only a plane that quietly stopped describing the frame.
QChar blink_char(Attrs a) {
	return (a & Attr::Blink) ? QLatin1Char('b') : QLatin1Char('.');
}

QString attr_names(Attrs a) {
	QStringList on;
	if (a & Attr::Bold)      on << QStringLiteral("bold");
	if (a & Attr::Dim)       on << QStringLiteral("dim");
	if (a & Attr::Italic)    on << QStringLiteral("italic");
	if (a & Attr::Underline) on << QStringLiteral("underline");
	if (a & Attr::Reverse)   on << QStringLiteral("reverse");
	if (a & Attr::Strike)    on << QStringLiteral("strike");
	// Last, in the enum's bit order like the six above it, rather than in
	// the reading order design.md section 5.2 uses. The list is generated
	// per mask and read beside the mask's own character, so bit order is
	// what lets a reader check one against the other.
	if (a & Attr::Blink)     on << QStringLiteral("blink");
	return on.join(QLatin1Char('+'));
}

// What a colour's KIND spells, which is what this printed in full before
// the authored index arrived. Split out rather than given a `break` per
// case so that the three lines below stay one line each and keep their
// columns: a switch whose arms are single returns is what this was, and a
// suffix is no reason to reflow it.
QString colour_kind_name(const Color &c) {
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

// The name a colour takes in the plane's key and in the legend, which are
// the same string by construction -- see letter_for() below, which keys on
// what this returns, and the legend loop, which prints it back. That is
// deliberate rather than incidental: the blink legend's first version wrote
// its name as a literal beside the encoding, which made attr_names()'s own
// row dead code and let a sabotage that deleted it apply cleanly and redden
// nothing. One function, read by both, cannot drift from itself.
QString colour_name(const Color &c) {
	QString name = colour_kind_name(c);

	// THE AUTHORED ANSI-16 INDEX, WHICH THE THREE SPELLINGS ABOVE CANNOT
	// SHOW. Color::operator== counts it as part of identity and color.h
	// says why: two colours with the same RGB and different authored
	// indices emit different bytes on a 16-colour terminal, so a diff that
	// called them equal would leave the wrong one on screen. Without this
	// suffix the frame diff called two such cells UNEQUAL and a recorded
	// fixture gave them the SAME letter -- an artefact compared against
	// itself, unable to spell a difference, agreeing with every later run
	// of every fixture for ever. That is the Blink defect of 8.238 in the
	// plane next door, and it was inert only because both committed
	// fixtures were recorded under terminal_default(), where every role is
	// Color::Default and with_ansi16() refuses to name an index on one.
	//
	// A SUFFIX AND NOT A PLANE OF ITS OWN, and the counting is the reason
	// rather than the convenience. Blink went to its own plane because the
	// attribute character is a TOTAL map over a FIXED domain: seven flags
	// give 127 non-empty masks, printable ASCII has 94 non-space characters
	// and 93 once '.' is spent, and 127 > 93 means no injection exists. No
	// such theorem applies here. The colour plane never mapped a value
	// space at all -- 2^24 RGB values per side is (2^24)^2 pairs against 63
	// alphabet slots, and always was -- it hands a letter to each PAIR A
	// FRAME ACTUALLY HOLDS, on first sight, with 61 non-default slots and an
	// out-loud '?' when they run out. Widening the key cannot overflow a
	// domain the encoding never enumerated; it adds a slot only when a frame
	// really does hold two colours differing in nothing else, which is
	// exactly the case that was being told as the same letter.
	//
	// And the plane route fails the same arithmetic that forced Blink's.
	// A cell carries TWO colours, each with 17 possible authored states
	// (none, plus 0..15), so one character per cell would have to separate
	// 17*17 = 289 of them, 288 after '.' takes the both-none case. 288 > 93:
	// no injection, so it is not one extra plane but two -- both emitted
	// always, by the argument above blink's plane that an optional one makes
	// "absent" and "recorded before this existed" the same two bytes. Two
	// planes move both committed fixtures, and each needs a legend line
	// written beside the encoding rather than derived from it, which is the
	// trap this function's header describes. The suffix costs neither.
	//
	// `>= 0` and not `> 0`: -1 is "no authored index" and 0 is authored
	// black, a real entry in theme.cpp's role table. The off-by-one would
	// mis-spell exactly one of the sixteen and nothing else, which is the
	// shape of defect a fixture is least able to report.
	//
	// No space in the suffix, which is a constraint and not a style: the
	// plane's key is fg + ' ' + bg and the legend splits it back on that
	// space, so a name containing one would tear the legend in half.
	if (c.authored_ansi16() >= 0)
		name += QStringLiteral("/ansi16:%1").arg(c.authored_ansi16());
	return name;
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

	QString glyphs, attrs, blinks, colours;
	for (int y = 0; y < r_; ++y) {
		QString g, a, bl, k;
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
			bl += blink_char(c.attrs);
			k += letter_for(c);
		}
		// Trailing default cells carry nothing and only make a diff noisier.
		// All three planes are trimmed on the same rule so the columns stay
		// readable straight down.
		while (g.endsWith(QLatin1Char(' '))) g.chop(1);
		while (a.endsWith(QLatin1Char('.'))) a.chop(1);
		while (bl.endsWith(QLatin1Char('.'))) bl.chop(1);
		while (k.endsWith(QLatin1Char('.'))) k.chop(1);
		glyphs  += g + QLatin1Char('\n');
		attrs   += a + QLatin1Char('\n');
		blinks  += bl + QLatin1Char('\n');
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
	// Beside the attribute plane it completes, and before the colours, so a
	// reader meeting an unfamiliar character in one of them finds the other
	// half of the mask in the next block rather than past the colours.
	//
	// Emitted always, collapsing to "(none)" in the overwhelming majority of
	// frames, rather than appearing only when something blinks. An optional
	// plane would make "no blink here" and "recorded before this plane
	// existed" the same two bytes of absence, which is the shape of every
	// defect in section 8 worth having.
	out += plane(QStringLiteral("blink"), blinks, QLatin1Char('.'));
	out += plane(QStringLiteral("colours"), colours, QLatin1Char('.'));
	// THE PICTURES, which a snapshot could not see at all. A frame's
	// images are carried beside its cells, not in them, so a frame holding
	// one and a frame holding none compared EQUAL -- measured on one
	// buffer, by appending an image to it and snapshotting twice. That is
	// the fault the attribute planes were added for, one channel along: a
	// message box whose severity icon stopped being drawn, or moved, or
	// changed size, went past every fixture in this tree.
	//
	// Emitted always and collapsing to "(none)", for the reason the blink
	// plane gives above: an optional section makes "no picture here" and
	// "recorded before this section existed" the same absence.
	//
	// GEOMETRY AND STACKING RATHER THAN CONTENT, and the limit is worth
	// stating because it is not obvious. The cell rectangle, the pixmap's
	// size and z are pinned by the grid and by the caller, and are the
	// same on any machine.
	//
	// The image's own `key` is NOT recorded. It is an upload identity
	// rather than a description -- a content hash of the pixels from one
	// producer and QPixmap::cacheKey() from the other -- so a different
	// Qt, a different icon theme or a differently-constructed pixmap
	// changes it with nothing wrong, and a fixture carrying it would go
	// red for the toolchain.
	//
	// Z IS RECORDED although nothing in this tree sets it yet, and the
	// struct's own comment is the reason: the frame loop decides whether
	// to present on `frame.images != prev_->images`, which compares z, so
	// two pictures swapping which is on top is a change. A fixture blind
	// to z would stop covering that the day somebody starts using it, and
	// would stop covering it silently.
	//
	// What this catches is a picture that vanished, moved, changed size or
	// changed stacking. What it does not catch is a DIFFERENT picture of
	// the same size in the same place.
	//
	// Sorted, because the order images are appended in is the order
	// widgets happened to paint and is not a property of the frame.
	out += QStringLiteral("--- images ---\n");
	if (images.isEmpty()) {
		out += QStringLiteral("(none)\n");
	} else {
		QVector<QString> lines;
		lines.reserve(images.size());
		for (const CellImage &im : images)
			lines.append(QStringLiteral("%1,%2 %3x%4 z%5 (%6x%7 px)")
			             .arg(im.cell_rect.left()).arg(im.cell_rect.top())
			             .arg(im.cell_rect.width()).arg(im.cell_rect.height())
			             .arg(im.z)
			             .arg(im.pixmap.width()).arg(im.pixmap.height()));
		std::sort(lines.begin(), lines.end());
		for (const QString &l : lines) out += l + QLatin1Char('\n');
	}
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
	// Its own line, because its plane is its own. Named even though the
	// plane has one value, so that a fixture carrying a blink says the word
	// somewhere a reader and a grep can both find it -- the attribute is
	// invisible in the glyph plane by definition, which is how it went
	// missing in the first place.
	out += QStringLiteral("blink: . none");
	// The NAME comes from attr_names() like every other attribute's, rather
	// than being written out here. A literal "blink" beside the one function
	// whose job is to spell attributes is a second copy of the same fact,
	// and the copy cannot be reached by anything that checks the first --
	// which is not hypothetical: the first version of this line did carry
	// the literal, and it made attr_names()'s own blink row dead code. The
	// sabotage entry that deletes that row applied cleanly and reddened
	// nothing, because nothing was calling it.
	if (blinks.contains(QLatin1Char('b')))
		out += QStringLiteral(", b %1").arg(attr_names(Attrs(Attr::Blink)));
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
