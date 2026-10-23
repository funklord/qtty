// src/control_chars.h -- the one rule for "a character that must not reach
// the wire as itself".
//
// INTERNAL. Not shipped in include/qtty/: nothing outside the library needs
// it, because every public write path -- CellBuffer::put_cluster(), text(),
// set_glyph() -- already applies it.
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

} // namespace Qtty
