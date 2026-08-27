// qtty/theme.h -- palette-role -> terminal-colour mapping (section 6).
#pragma once
#include <QPalette>
#include <QByteArray>
#include "color.h"
#include "cell.h"
#include "backend.h"

namespace Qtty {

// The theme maps Qt palette roles to terminal colours. The default theme
// keeps everything Color::Default (the terminal's own scheme) -- the most
// legible choice on unknown terminals -- and marks emphasis with attrs, not
// colour. Products install richer themes; Ansi16 role tables are authored by
// hand per section 6, never nearest-matched.
//
// theme() is the single source rendering resolves colours through
// (project.md section 11 item 3). CellPaintEngine recovers the palette role
// behind a pen or brush and asks the theme what that role looks like on a
// terminal; it does not read QGuiApplication::palette() for the answer.
struct CellTheme {
	Color windowText, text, buttonText;         // foregrounds
	Color window, base, button;                 // backgrounds
	Color highlight, highlightedText;
	Color accent = Color::indexed(4);

	static CellTheme terminalDefault();          // all Default (recommended)
	static CellTheme fromPalette(const QPalette &p);   // true-colour capture

	// Resolve a role. The returned colour carries the role's hand-authored
	// ANSI-16 index (see ansi16_for_role below), so a 16-colour terminal gets
	// the authored spelling rather than a nearest match. Roles the theme does
	// not name fall back to windowText / window, which is the theme saying
	// "this is ordinary text on the ordinary surface".
	Color foreground(QPalette::ColorRole r) const;
	Color background(QPalette::ColorRole r) const;
};

// Process-wide active theme (installed by Qtty::setup, replaceable).
const CellTheme &theme();
void setTheme(const CellTheme &);

// The hand-authored role -> ANSI-16 index table (section 6). design.md rejects
// a nearest match at this depth outright, because sixteen colours are too few
// for a distance metric to keep a pairing readable; the table is a design
// artifact and is reviewed like code. Returns 0..15, or -1 for a role with no
// authored spelling (which is what sends a colour to the nearest-match
// fallback in Color::toAnsi16).
int ansi16_for_role(QPalette::ColorRole role);

// A colour as it will actually be emitted at `depth` (section 6). Ansi16 and
// Xterm256 come back as Color::Indexed, TrueColor unchanged, Mono as
// Color::Default -- at that depth there is no colour to carry.
Color quantise(const Color &c, Capabilities::ColorDepth depth);

// The SGR sequence putting `fg`, `bg` and `attrs` into effect at `depth`,
// starting from a reset. The three colour depths differ only here, which is
// why this sits with the mapping rather than inside a backend -- and why it
// can be tested without a tty.
QByteArray sgr_sequence(const Color &fg, const Color &bg, Attrs attrs,
                        Capabilities::ColorDepth depth);

// section 6's contrast rule, applied after mapping: the number of cells in
// `frame` whose foreground and background fail to clear `min_delta` of
// luminance once quantised to `depth`. Debug builds log the first few.
// Never fatal -- theme.cpp says why.
int contrast_violations(const CellBuffer &frame, Capabilities::ColorDepth depth,
                        int min_delta = 48);

} // namespace Qtty
