// qtty/theme.h -- palette-role -> terminal-colour mapping (section 6).
#pragma once
#include <QPalette>
#include "color.h"

namespace Qtty {

// The theme maps Qt palette roles to terminal colours. The default theme
// keeps everything Color::Default (the terminal's own scheme) -- the most
// legible choice on unknown terminals -- and marks emphasis with attrs, not
// colour. Products install richer themes; Ansi16 role tables are authored by
// hand per section 6, never nearest-matched.
struct CellTheme {
	Color windowText, text, buttonText;         // foregrounds
	Color window, base, button;                 // backgrounds
	Color highlight, highlightedText;
	Color accent = Color::indexed(4);

	static CellTheme terminalDefault();          // all Default (recommended)
	static CellTheme fromPalette(const QPalette &p);   // true-colour capture

	Color foreground(QPalette::ColorRole r) const;
	Color background(QPalette::ColorRole r) const;
};

// Process-wide active theme (installed by Qtty::setup, replaceable).
const CellTheme &theme();
void setTheme(const CellTheme &);

} // namespace Qtty
