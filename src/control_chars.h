// src/control_chars.h -- the one rule for "a character that must not reach
// the wire as itself".
//
// INTERNAL. Not shipped in include/qtty/: nothing outside the library needs
// it, because the library applies it on the way in and on the way out.
//
// THE "EVERY PUBLIC WRITE PATH" CLAIM THIS CARRIED WAS FALSE, and it named
// the three paths that are one: text() and set_glyph() both call
// put_cluster(), so there was one sanitising entry, not three. What it left
// out are the two writers that take a value -- CellBuffer::fill(), which
// wrote a Cell verbatim, and the mutable CellBuffer::at() reference, whose
// writes cannot be enumerated at all, which is why no list of entry points
// could ever have been complete. Measured on the wire 2026-10-26, an ESC, a
// C1 CSI, a U+2028 and a BEL all reached the terminal as themselves. 8.578.
//
// So the entry guards keep the BUFFER clean -- which is what to_text() and
// the snapshot fixtures read, and the reason the substitution is not only
// in the backend -- and one guard at the single wire exit covers at(). Two
// sites for two different properties, and neither is a clamp repeated per
// caller.
//
// IT LIVES HERE BECAUSE IT HAD TWO SPELLINGS AND ONE WAS NARROWER. The cell
// path has always had the full rule; AnsiBackend::set_title() open-coded
// `c >= 0x20 && c != 0x7f`, which is C0 and DEL and nothing else -- so a
// window title carrying a C1 control or a line separator went out inside an
// OSC string. Measured on the wire: U+009C, U+009B and U+2028 all reached
// it, with the surrounding text intact. 8.510.
//
// A title is where it costs most rather than least. U+009C IS ST, the string
// terminator, and U+009B is CSI: a terminal that decodes C1 from UTF-8 reads
// them as controls, so the OSC ends early and what follows is executed rather
// than displayed. A cell's glyph is written as text; a title is written as
// part of an escape sequence.
#pragma once
#include <QChar>

namespace Qtty {

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
inline bool is_control(char32_t u) {
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

// THE RULE APPLIED, not merely named, because having one predicate and two
// applications of it is how the narrower spelling survived 8.510. Both
// callers want "this string with nothing in it that must not reach the
// wire", and they had written that out twice: the title path scanned every
// character and the cell path read only the first one.
//
// A GLYPH IS A GRAPHEME CLUSTER, so position 0 is not where a control has
// to be. Measured on the wire 2026-10-26: put_cluster() with "a" followed
// by U+009B emitted `61 c2 9b`, a base and then C1 CSI, past the guard --
// and the bytes after an injected CSI are read as its parameters. The
// cheap check was not a smaller version of the rule, it was a different
// rule that happened to catch the common input.
//
// Returns the string itself when there is nothing to do, which is every
// ordinary glyph, so the common path copies no characters.
inline QString without_controls(const QString &s) {
	int i = 0;
	while (i < s.size() && !is_control(s.at(i).unicode())) ++i;
	if (i == s.size()) return s;
	QString out = s.left(i);
	out.reserve(s.size());
	for (++i; i < s.size(); ++i)
		if (!is_control(s.at(i).unicode())) out += s.at(i);
	return out;
}

// A CELL SUBSTITUTES WHERE A TITLE STRIPS, and the difference is columns.
// Dropping a glyph's only character leaves a cell with nothing to emit, so
// every cell after it on the row moves one left -- a corrupted frame rather
// than a corrupted character. A space is what a carriage return already
// became: one column, nothing shown, no meaning on the wire.
//
// An EMPTY cluster is left empty rather than becoming a space, because an
// empty one means "no glyph here" to the emitters and a space does not.
inline QString safe_glyph(const QString &cluster) {
	if (cluster.isEmpty()) return cluster;
	const QString g = without_controls(cluster);
	return g.isEmpty() ? QStringLiteral(" ") : g;
}

} // namespace Qtty
