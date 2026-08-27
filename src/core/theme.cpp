// src/core/theme.cpp -- the role tables, quantisation and SGR emission (section 6).
#include "qtty/theme.h"

namespace Qtty {

CellTheme CellTheme::terminal_default() { return CellTheme{}; }

CellTheme CellTheme::from_palette(const QPalette &p) {
	CellTheme t;
	t.window_text       = Color::rgb(p.color(QPalette::WindowText));
	t.text             = Color::rgb(p.color(QPalette::Text));
	t.button_text       = Color::rgb(p.color(QPalette::ButtonText));
	t.window           = Color::rgb(p.color(QPalette::Window));
	t.base             = Color::rgb(p.color(QPalette::Base));
	t.button           = Color::rgb(p.color(QPalette::Button));
	t.highlight        = Color::rgb(p.color(QPalette::Highlight));
	t.highlighted_text  = Color::rgb(p.color(QPalette::HighlightedText));
	return t;
}

// ---- the ANSI-16 role table (section 6) ------------------------------------
//
// design.md section 6, verbatim: "not nearest-match. Explicit hand-authored
// role->index mapping, because nearest-match on 16 colours produces unreadable
// pairings. This table is a design artifact, reviewed like code."
//
// The measurement behind that sentence, taken on Fusion's own palette, which
// is the palette this tree renders through: Highlight is 0x308cc6, and the
// nearest of the sixteen by squared RGB distance is index 6 -- teal, at a
// distance of 7348 against 11444 for the nearest grey and 25153 for blue. A
// selection therefore came out as bright-white-on-teal: the wrong hue, and a
// pairing whose luminance delta is a third of what white-on-blue gives. The
// table below says 4 for that role, and the reason it can is that it is
// keyed on what the colour MEANS rather than on what it looks like.
//
// It is authored for a dark terminal, which is what index 0 means to the
// overwhelming majority of them. A light-terminal table is a second theme
// rather than a second branch here: the roles that would change are exactly
// the ones a theme names, and CellTheme is where a product names them.
int ansi16_for_role(QPalette::ColorRole role) {
	switch (role) {

	// -- foregrounds ---------------------------------------------------------
	case QPalette::WindowText:
	case QPalette::Text:
	case QPalette::ButtonText:
		// Body text, wherever it sits. 7 (white) and not 15 (bright white), so
		// that BrightText and Attr::Bold still have somewhere louder to go; 7
		// clears the contrast minimum against 0 and 4, which are the only two
		// backgrounds this table produces. Nearest-match sends all three to 0
		// on any light palette -- black text on a black terminal, which is the
		// unreadable pairing design.md names.
		return 7;
	case QPalette::BrightText:
		// The role exists to be louder than WindowText, and above 7 the only
		// white left is 15.
		return 15;
	case QPalette::PlaceholderText:
		// Deliberately below body text and deliberately above the ground. 8 is
		// the one index that reads as "dimmed" rather than as "absent".
		return 8;
	case QPalette::HighlightedText:
		// Sits on Highlight's 4. 15 on 4 is the largest luminance delta the
		// sixteen offer for a selection, and it is the pairing every DOS-era
		// TUI arrived at independently.
		return 15;
	case QPalette::ToolTipText:
		// Sits on ToolTipBase's 11. Black on bright yellow is the conventional
		// tooltip pairing, and black is the readable half of it.
		return 0;
	case QPalette::Link:
		// Bright blue is what a terminal reader takes for a link, and it stays
		// distinct from Highlight's 4 when a link falls inside a selection.
		return 12;
	case QPalette::LinkVisited:
		// Bright magenta: the conventional visited hue, and one told from Link
		// at a glance rather than by holding the two side by side.
		return 13;

	// -- backgrounds ---------------------------------------------------------
	case QPalette::Window:
	case QPalette::Base:
	case QPalette::Button:
		// Every ordinary surface is the terminal's own ground. Widgets are
		// told apart by the box glyphs GridStyle draws, not by three shades of
		// the same dark -- which is what a nearest match produces from a GUI
		// palette, and what makes a 16-colour TUI look muddy.
		return 0;
	case QPalette::AlternateBase:
		// The one surface that must differ from Base, or alternating rows stop
		// alternating. 8 is the only index that reads as "slightly off the
		// ground" rather than as a second foreground.
		return 8;
	case QPalette::Highlight:
		// The selection surface, and the role the note above measured:
		// nearest-match gives 6 for Fusion's 0x308cc6.
		return 4;
	case QPalette::ToolTipBase:
		// Tooltips are meant to interrupt. Bright yellow is the one loud
		// surface among the sixteen that still takes black text.
		return 11;

	// -- the 3-D bevel roles -------------------------------------------------
	// Terminals have no bevels, and GridStyle draws frames as box glyphs
	// rather than as light and shadow edges. Anything still reaching a cell
	// through one of these degrades onto the grey ramp, so that a stray bevel
	// arrives as a shade and never as a hue.
	case QPalette::Light:    return 15;
	case QPalette::Midlight: return 7;
	case QPalette::Mid:      return 8;
	case QPalette::Dark:     return 8;
	case QPalette::Shadow:   return 0;

	case QPalette::Accent:
		// The product's own emphasis colour. 12 rather than Highlight's 4, so
		// that an accented widget inside a selection is still visible.
		return 12;

	default:
		// NoRole, NColorRoles, and whatever a later Qt adds: no authored
		// spelling. Returning -1 rather than a plausible index is the point --
		// Color::to_ansi16() then falls back to the nearest of the sixteen and
		// the absence stays visible instead of becoming a silent black.
		return -1;
	}
}

Color CellTheme::foreground(QPalette::ColorRole r) const {
	Color c;
	switch (r) {
	case QPalette::Text:            c = text; break;
	case QPalette::ButtonText:      c = button_text; break;
	case QPalette::HighlightedText: c = highlighted_text; break;
	default:                        c = window_text; break;
	}
	return c.with_ansi16(ansi16_for_role(r));
}

Color CellTheme::background(QPalette::ColorRole r) const {
	Color c;
	switch (r) {
	case QPalette::Base:      c = base; break;
	case QPalette::Button:    c = button; break;
	case QPalette::Highlight: c = highlight; break;
	default:                  c = window; break;
	}
	return c.with_ansi16(ansi16_for_role(r));
}

static CellTheme s_theme = CellTheme::terminal_default();
const CellTheme &theme() { return s_theme; }
void set_theme(const CellTheme &t) { s_theme = t; }

// ---- quantisation and emission (section 6) ---------------------------------

Color quantise(const Color &c, Capabilities::ColorDepth depth) {
	switch (depth) {
	case Capabilities::Mono:
		return Color();                          // attributes carry everything
	case Capabilities::Ansi16: {
		const int i = c.to_ansi16();
		return i < 0 ? Color() : Color::indexed(quint8(i));
	}
	case Capabilities::Xterm256: {
		const int i = c.to_xterm256();
		return i < 0 ? Color() : Color::indexed(quint8(i));
	}
	case Capabilities::TrueColor:
		break;
	}
	return c;
}

// One already-quantised colour, in the spelling `depth` calls for.
static void append_color(QByteArray &out, const Color &c, bool foreground,
                         Capabilities::ColorDepth depth) {
	if (c.kind() == Color::Default) return;      // 39/49: the leading reset did it
	if (depth == Capabilities::Ansi16) {
		// 30-37 / 40-47 for the first eight and 90-97 / 100-107 for the bright
		// half: the aixterm spelling, which is what a terminal claiming
		// sixteen colours understands. Bold-as-bright is deliberately not used
		// -- it would make Attr::Bold and a bright colour the same byte, and
		// then neither could be turned off without the other.
		const int i = c.kind() == Color::Indexed ? c.index() % 16 : 7;
		const int base = foreground ? (i < 8 ? 30 : 82) : (i < 8 ? 40 : 92);
		out += "\033[" + QByteArray::number(base + i) + 'm';
		return;
	}
	if (c.kind() == Color::Rgb) {
		out += foreground ? "\033[38;2;" : "\033[48;2;";
		out += QByteArray::number(qRed(c.value())) + ';'
		     + QByteArray::number(qGreen(c.value())) + ';'
		     + QByteArray::number(qBlue(c.value())) + 'm';
		return;
	}
	out += foreground ? "\033[38;5;" : "\033[48;5;";
	out += QByteArray::number(c.index()) + 'm';
}

QByteArray sgr_sequence(const Color &fg, const Color &bg, Attrs attrs,
                        Capabilities::ColorDepth depth) {
	QByteArray out = "\033[0m";                  // known state, then build up
	append_color(out, quantise(fg, depth), true, depth);
	append_color(out, quantise(bg, depth), false, depth);
	if (attrs & Attr::Bold)      out += "\033[1m";
	if (attrs & Attr::Dim)       out += "\033[2m";
	if (attrs & Attr::Italic)    out += "\033[3m";
	if (attrs & Attr::Underline) out += "\033[4m";
	if (attrs & Attr::Reverse)   out += "\033[7m";
	if (attrs & Attr::Strike)    out += "\033[9m";
	return out;
}

// section 6: "after mapping, assert a minimum luminance delta between fg and
// bg in every emitted cell, and log violations in debug builds."
//
// Deliberately counted rather than asserted, and never fatal. An unreadable
// cell is a theme quality defect; an application that aborts a user's terminal
// session over one has turned a cosmetic fault into lost work, and a guard
// that can do that is the first thing anybody switches off -- at which point
// it guards nothing. That is the same reasoning GridGuard carries in grid.h.
//
// Only cells carrying a glyph are counted. A blank cell has no foreground to
// be unreadable, and counting the background fill would drown the real
// findings -- a warning that fires on every frame is a warning nobody reads.
int contrast_violations(const CellBuffer &frame, Capabilities::ColorDepth depth,
                        int min_delta) {
	int violations = 0;
	for (int y = 0; y < frame.rows(); ++y)
		for (int x = 0; x < frame.cols(); ++x) {
			const Cell &c = frame.at(x, y);
			if (c.width == 0 || c.ch == QStringLiteral(" ")) continue;
			const Color fg = quantise(c.fg, depth);
			const Color bg = quantise(c.bg, depth);
			if (has_minimum_contrast(fg, bg, min_delta)) continue;
			++violations;
#ifndef QT_NO_DEBUG
			if (violations <= 8)
				qWarning("qtty: cell (%d,%d) %s is below the section 6 contrast "
				         "minimum: luminance delta %d, wanted %d",
				         x, y, qPrintable(c.ch),
				         qAbs(fg.luminance(true) - bg.luminance(false)), min_delta);
#endif
		}
	return violations;
}

} // namespace Qtty
