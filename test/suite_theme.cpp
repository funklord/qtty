// suite_theme -- section 6: quantisation, the ANSI-16 role table, emission,
// contrast, and the wiring that makes theme() the source rendering reads.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_theme() {
	fails = 0;

	CHECK(Color().to_xterm256() == -1, "Default maps to terminal default (-1)");
	CHECK(Color::indexed(196).to_xterm256() == 196, "Indexed passes through");
	CHECK(Color::rgb(qRgb(255, 0, 0)).to_xterm256() == 196, "pure red -> cube 196");
	CHECK(Color::rgb(qRgb(0, 0, 0)).to_xterm256() == 16, "black -> cube 16");
	CHECK(Color::rgb(qRgb(255, 255, 255)).to_xterm256() == 231, "white -> cube 231");
	int grey = Color::rgb(qRgb(128, 128, 128)).to_xterm256();
	CHECK(grey >= 232 || grey == 102, "mid grey -> grey ramp or grey cube cell");

	// section 6 requires the xterm-256 match to be made in CIELAB, not in RGB, and
	// this is the colour that shows the difference. By squared RGB distance
	// the mid green 0x287832 sits 3812 from the grey-ramp entry 238 and 3850
	// from the nearest cube entry, so the RGB match quantised a saturated
	// green to a GREY, by a margin of 38 parts in 3850. In Lab it is 22 --
	// rgb(0,95,0), a green -- and not marginally: 13.9 against 19.0 for the
	// runner-up.
	CHECK(Color::rgb(qRgb(40, 120, 50)).to_xterm256() == 22,
	      "CIELAB puts a mid green on green 22 (RGB nearest gave grey 238)");
	// Memoisation must not change the answer, only how often it is computed.
	CHECK(Color::rgb(qRgb(40, 120, 50)).to_xterm256() == 22,
	      "and the memoised second lookup agrees with the first");

	CHECK(Color::rgb(qRgb(255, 0, 0)).to_ansi16() == 9, "red -> bright red in 16");
	CHECK(Color().to_ansi16() == -1, "Default stays default in 16");

	// -- the hand-authored ANSI-16 role table (section 6) ----------------------
	//
	// design.md forbids nearest-match at this depth. Fusion's Highlight is
	// 0x308cc6, and the nearest of the sixteen by squared RGB distance is 6 --
	// teal, at 7348 against 11444 for the nearest grey and 25153 for blue. A
	// selection therefore came out bright-white-on-teal: the wrong hue, and a
	// pairing with a third of the luminance delta white-on-blue gives. The
	// first check below is what the fallback still produces for a colour that
	// arrives with no role; the second is what the table says instead.
	CHECK(Color::rgb(qRgb(48, 140, 198)).to_ansi16() == 6,
	      "role-less nearest-match lands Fusion's highlight blue on 6 (teal)");
	CHECK(ansi16_for_role(QPalette::Highlight) == 4,
	      "authored table maps Highlight to 4 (blue), not the nearest-match 6");
	CHECK(ansi16_for_role(QPalette::HighlightedText) == 15,
	      "HighlightedText is 15: white on blue, the widest delta of the sixteen");
	CHECK(ansi16_for_role(QPalette::WindowText) == 7
	      && ansi16_for_role(QPalette::Text) == 7
	      && ansi16_for_role(QPalette::ButtonText) == 7,
	      "the three body-text roles are all 7, leaving 15 for BrightText");
	CHECK(ansi16_for_role(QPalette::BrightText) == 15, "BrightText is 15");
	CHECK(ansi16_for_role(QPalette::Window) == 0
	      && ansi16_for_role(QPalette::Base) == 0
	      && ansi16_for_role(QPalette::Button) == 0,
	      "the ordinary surfaces are all 0, told apart by glyphs not by shade");
	CHECK(ansi16_for_role(QPalette::AlternateBase) == 8,
	      "AlternateBase is 8, the one index that reads as off-the-ground");
	CHECK(ansi16_for_role(QPalette::ToolTipBase) == 11
	      && ansi16_for_role(QPalette::ToolTipText) == 0,
	      "tooltips are black on bright yellow");
	CHECK(ansi16_for_role(QPalette::NoRole) == -1,
	      "a role with no authored entry says so rather than returning 0");

	// The table reaches a colour through the theme, which is the only route
	// rendering has to it.
	{
		QPalette fusion;
		fusion.setColor(QPalette::Highlight, QColor(48, 140, 198));
		const CellTheme t = CellTheme::from_palette(fusion);
		CHECK(t.background(QPalette::Highlight).to_ansi16() == 4,
		      "a themed Highlight emits the authored 4");
		CHECK(t.highlight.to_ansi16() == 6,
		      "the same colour with no role attached still nearest-matches to 6");
	}

	// -- emission: one escape per colour depth (section 6) ---------------------
	//
	// AnsiBackend::present emitted 38;5; and 48;5; whatever the terminal could
	// do. All three depths now have a path, and sgr_sequence is where they
	// differ, so it is where they are checked.
	CHECK(sgr_sequence(Color::rgb(qRgb(255, 0, 0)), Color(), {},
	                   Capabilities::TrueColor)
	      == QByteArray("\033[0m\033[38;2;255;0;0m"),
	      "truecolor foreground -> 38;2;r;g;b");
	CHECK(sgr_sequence(Color(), Color::rgb(qRgb(0, 32, 64)), {},
	                   Capabilities::TrueColor)
	      == QByteArray("\033[0m\033[48;2;0;32;64m"),
	      "truecolor background -> 48;2;r;g;b");
	CHECK(sgr_sequence(Color::rgb(qRgb(255, 0, 0)), Color(), {},
	                   Capabilities::Xterm256)
	      == QByteArray("\033[0m\033[38;5;196m"),
	      "xterm-256 foreground -> 38;5;196");
	CHECK(sgr_sequence(Color::indexed(9), Color::indexed(4), {},
	                   Capabilities::Ansi16)
	      == QByteArray("\033[0m\033[91m\033[44m"),
	      "ansi-16 emits 91 and 44, never 38;5;");
	CHECK(sgr_sequence(Color::indexed(7), Color::indexed(0), {},
	                   Capabilities::Ansi16)
	      == QByteArray("\033[0m\033[37m\033[40m"),
	      "ansi-16 uses the 30-37/40-47 half for indices under 8");
	CHECK(sgr_sequence(Color::indexed(15), Color(), Attrs(Attr::Bold),
	                   Capabilities::Ansi16)
	      == QByteArray("\033[0m\033[97m\033[1m"),
	      "a bright colour and Attr::Bold stay separate bytes");
	CHECK(sgr_sequence(Color::rgb(qRgb(255, 0, 0)), Color::rgb(qRgb(0, 0, 255)),
	                   {}, Capabilities::Mono)
	      == QByteArray("\033[0m"),
	      "mono emits no colour at all");
	// The theme's authored index is what an Ansi16 terminal gets, which is the
	// whole point of carrying it on the colour.
	{
		QPalette fusion;
		fusion.setColor(QPalette::Highlight, QColor(48, 140, 198));
		const CellTheme t = CellTheme::from_palette(fusion);
		CHECK(sgr_sequence(Color(), t.background(QPalette::Highlight), {},
		                   Capabilities::Ansi16)
		      == QByteArray("\033[0m\033[44m"),
		      "a themed selection background emits 44 (blue), not 46 (teal)");
	}

	// -- contrast (section 6), the rule the emission path now applies ----------
	CHECK(has_minimum_contrast(Color(), Color()), "terminal default fg/bg contrast ok");
	CHECK(!has_minimum_contrast(Color::rgb(qRgb(120, 120, 120)),
	                          Color::rgb(qRgb(128, 128, 128))),
	      "near-identical greys flagged as low contrast");
	{
		CellBuffer low(4, 1);
		low.text(0, 0, QStringLiteral("ab"),
		         Color::rgb(qRgb(120, 120, 120)), Color::rgb(qRgb(128, 128, 128)));
		CHECK(contrast_violations(low, Capabilities::TrueColor) == 2,
		      "both glyph cells of a low-contrast pairing are counted");
		CHECK(contrast_violations(low, Capabilities::Ansi16) == 2,
		      "and still counted after mapping to the sixteen");
		CellBuffer readable(4, 1);
		readable.text(0, 0, QStringLiteral("ab"),
		              Color::rgb(qRgb(230, 230, 230)), Color::rgb(qRgb(10, 10, 10)));
		CHECK(contrast_violations(readable, Capabilities::TrueColor) == 0,
		      "a legible pairing is not counted");
		CellBuffer blank(4, 1);
		CHECK(contrast_violations(blank, Capabilities::TrueColor) == 0,
		      "a blank frame reports nothing -- no glyph, no unreadable pairing");
	}

	CellTheme t = CellTheme::terminal_default();
	CHECK(t.text == Color() && t.window == Color(),
	      "default theme is all terminal-default");

	QPalette pal;
	pal.setColor(QPalette::Highlight, QColor(30, 90, 200));
	CellTheme p = CellTheme::from_palette(pal);
	CHECK(p.highlight == Color::rgb(QColor(30, 90, 200)),
	      "from_palette captures roles as Rgb");

	// -- the wiring (project.md section 11 item 3) ----------------------------
	//
	// theme() has to be the single source rendering resolves colours through,
	// or nothing else in section 6 has anywhere to land. Before this,
	// CellPaintEngine read QGuiApplication::palette() directly: set_theme()
	// could not reach a cell, and the two renders below came out identical.
	{
		QWidget w;
		w.setAutoFillBackground(true);
		w.setAttribute(Qt::WA_DontShowOnScreen);
		w.resize(GridMetrics::cells(8, 3));
		w.show();
		QCoreApplication::processEvents();

		CellBuffer plain(8, 3);
		set_theme(CellTheme::terminal_default());
		render_once(w, plain);

		QPalette themed_palette = QGuiApplication::palette();
		themed_palette.setColor(QPalette::Window, QColor(20, 40, 60));
		CellBuffer themed(8, 3);
		set_theme(CellTheme::from_palette(themed_palette));
		render_once(w, themed);
		set_theme(CellTheme::terminal_default());     // leave the process as found

		CHECK(plain.at(1, 1).bg.kind() == Color::Default,
		      "terminal-default theme leaves the window ground to the terminal");
		CHECK(themed.at(1, 1).bg.kind() == Color::Rgb
		      && themed.at(1, 1).bg.value() == qRgb(20, 40, 60),
		      "set_theme() changes what render_once draws");
		CHECK(themed.at(1, 1).bg.to_ansi16() == 0,
		      "and the drawn colour carries Window's authored ANSI-16 index");
		CHECK(plain.diff_cells(themed) > 0,
		      "the two frames differ, which is the defect this closes");
	}

	return fails;
}
