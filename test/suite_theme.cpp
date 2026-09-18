// suite_theme -- section 6: quantisation, the ANSI-16 role table, emission,
// contrast, and the wiring that makes theme() the source rendering reads.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

int suite_theme() {
	fails = 0;

	CHECK(Color().to_xterm256() == -1, "Default maps to terminal default (-1)");
	CHECK(Color::indexed(196).to_xterm256() == 196, "Indexed passes through");
	CHECK(Color::rgb(qRgb(255, 0, 0)).to_xterm256() == 196, "pure red -> cube 196");
	CHECK(Color::rgb(qRgb(0, 0, 0)).to_xterm256() == 16, "black -> cube 16");
	CHECK(Color::rgb(qRgb(255, 255, 255)).to_xterm256() == 231, "white -> cube 231");
	int grey = Color::rgb(qRgb(128, 128, 128)).to_xterm256();
	// Which of the two, printed. The check accepts either because both are
	// defensible answers for a mid grey, but accepting either also means
	// nothing here notices if the answer changes.
	printf("info: mid grey resolves to xterm index %d\n", grey);
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

	// EVERY ATTRIBUTE, because only ONE of the six had ever been emitted.
	// Branch coverage over 32,440 calls to this function: Bold at 8%, and
	// Dim, Italic, Underline, Reverse and Strike at zero. This is the
	// function that turns a cell into the bytes a terminal receives, so a
	// wrong number in any of those five rows is a wrong rendering on every
	// terminal, for every application, with nothing in the tree to notice
	// -- and `Reverse` is how a selection is drawn.
	//
	// The codes are ECMA-48's and are not qtty's to choose: 1 bold, 2 faint,
	// 3 italic, 4 underline, 5 slow blink, 7 negative image, 9 crossed out.
	{
		const struct { Attr a; const char *code; const char *what; } sgr[] = {
			{ Attr::Bold,      "\033[1m", "bold"      },
			{ Attr::Dim,       "\033[2m", "dim"       },
			{ Attr::Italic,    "\033[3m", "italic"    },
			{ Attr::Underline, "\033[4m", "underline" },
			// 5 slow blink, added with the attribute in 8.238. It goes in
			// THIS table rather than in a check of its own on purpose: the
			// table is the population, and the two assertions below are
			// written over it, so a seventh attribute that emitted nothing
			// reddens them both. A separate check would have left this
			// table saying six and still passing.
			{ Attr::Blink,     "\033[5m", "blink"     },
			{ Attr::Reverse,   "\033[7m", "reverse"   },
			{ Attr::Strike,    "\033[9m", "strike"    },
		};
		QStringList missing;
		for (const auto &e : sgr) {
			const QByteArray out =
			    sgr_sequence(Color(), Color(), Attrs(e.a),
			                 Capabilities::TrueColor);
			if (!out.contains(e.code)) missing << QLatin1String(e.what);
		}
		if (!missing.isEmpty())
			printf("info: attributes with no SGR code on the wire: %s\n",
			       qPrintable(missing.join(QStringLiteral(", "))));
		CHECK(missing.isEmpty() && sizeof(sgr) / sizeof(sgr[0]) == 7,
		      "each of the seven attributes emits its own ECMA-48 code, so a "
		      "terminal is told about all of them and not only bold");

		// And together, in one cell, which is what a struck-through heading
		// in a selection actually is. Asserted as a set rather than as a
		// literal string, because the ORDER is this function's business and
		// pinning it would be pinning something nobody depends on.
		Attrs all;
		for (const auto &e : sgr) all |= e.a;
		const QByteArray both = sgr_sequence(Color(), Color(), all,
		                                     Capabilities::TrueColor);
		QStringList absent;
		for (const auto &e : sgr)
			if (!both.contains(e.code)) absent << QLatin1String(e.what);
		CHECK(absent.isEmpty(),
		      "and a cell carrying all seven emits all seven, rather than "
		      "the first one that matched");
	}
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

	// The terminal's own low sixteen, asked for with OSC 4 and used here. It
	// matters in exactly one place and that narrowness is worth stating:
	// to_xterm256() matches against indices 16..255 only, so a user's scheme
	// cannot affect 256-colour quantisation at all. to_ansi16() picks the
	// nearest of the sixteen for a colour with no authored role, and picking
	// "nearest" against the wrong sixteen is how a fallback lands somewhere
	// the user can see is wrong.
	{
		const QVector<QRgb> before = Qtty::terminal_palette();
		CHECK(before.isEmpty(), "no palette is assumed until the terminal answers");

		// A mid-blue that the xterm table matches to blue.
		const Color probe = Color::rgb(qRgb(40, 40, 200));
		const int xterm_answer = probe.to_ansi16();

		// A scheme in which index 1 -- normally red -- is that same blue. The
		// nearest of THOSE sixteen is index 1, and no amount of reasoning
		// about xterm's table would reach it.
		QVector<QRgb> scheme(16);
		for (int i = 0; i < 16; ++i) scheme[i] = qRgb(0, 0, 0);
		scheme[1] = qRgb(40, 40, 200);
		scheme[7] = qRgb(200, 200, 200);
		Qtty::set_terminal_palette(scheme);
		CHECK(Qtty::terminal_palette().size() == 16, "the answer is kept");

		const Color fresh = Color::rgb(qRgb(40, 40, 200));
		CHECK(fresh.to_ansi16() == 1,
		      "a colour matches against the terminal's sixteen, not xterm's");
		CHECK(fresh.to_ansi16() != xterm_answer,
		      "which is a different answer, or this proves nothing");

		// And luminance follows the same answer, which it did not. That
		// function kept its own table for indices below 16 -- so one
		// function honoured the reported palette and the other, applied to
		// its OUTPUT, contradicted it. It bites hardest at Ansi16, where
		// quantise() turns every colour into an index below 16 and the whole
		// contrast check therefore ran on a table the terminal had answered
		// against.
		//
		// In this scheme index 1 is that mid-blue and index 7 is near-white,
		// so the pair is legible; the built-in table calls index 1 red at 32
		// and index 7 at 192. The assertion is the RELATIONSHIP -- the
		// judgement follows the scheme the terminal stated -- rather than
		// either number.
		printf("info: with the scheme, index 1 reads %d and index 7 %d\n",
		       Color::indexed(1).luminance(false),
		       Color::indexed(7).luminance(true));
		CHECK(Color::indexed(1).luminance(false)
		          == Color::rgb(qRgb(40, 40, 200)).luminance(false)
		      && Color::indexed(7).luminance(true)
		          == Color::rgb(qRgb(200, 200, 200)).luminance(true),
		      "an indexed colour's luminance is the terminal's own, where it"
		      " said what it is");

		// Cleared, and the built-in table stands again -- which is what a
		// terminal that never answered gets, and what this assumed before it
		// could ask.
		Qtty::set_terminal_palette(QVector<QRgb>());
		CHECK(Qtty::terminal_palette().isEmpty() &&
		      Color::rgb(qRgb(40, 40, 200)).to_ansi16() == xterm_answer,
		      "and clearing it restores the built-in table");
		// Including for luminance, whose table is NOT arithmetic on the
		// built-in colours -- index 12 is listed at 96 where the formula on
		// 0x0000ff gives 29 -- so this also pins that the judgement, rather
		// than a computation, is what a terminal that never answered gets.
		CHECK(Color::indexed(12).luminance(false) == 96,
		      "and an unasked terminal keeps the judged table, not the"
		      " arithmetic");

		// A short answer is refused rather than padded: half a user's scheme
		// and half xterm's is a palette no terminal has.
		Qtty::set_terminal_palette(QVector<QRgb>{qRgb(1, 2, 3)});
		CHECK(Qtty::terminal_palette().isEmpty(),
		      "and a partial palette is refused, not mixed with the built-in one");
	}

	{
		// The palette roles nothing had asked for. A role that falls to the
		// default arm silently is how a themed widget ends up drawn in the
		// window's colours: the value is plausible, the widget is legible,
		// and it is simply the wrong colour -- which no rendering test
		// notices because nothing tells it what to expect.
		const CellTheme &t = theme();
		CHECK(t.foreground(QPalette::ButtonText) != t.foreground(QPalette::Text)
		      || t.button_text.value() == t.text.value(),
		      "ButtonText is its own role, or is deliberately the same colour");
		CHECK(t.foreground(QPalette::HighlightedText).value()
		      == t.highlighted_text.value(),
		      "HighlightedText comes from the theme's own field");
		CHECK(t.background(QPalette::Button).value() == t.button.value(),
		      "and Button likewise, on the background side");
	}

	// OQ-7, closed: the ANSI-16 fallback matches in RGB and stays that way.
	//
	// to_xterm256() was changed to match in CIELAB because RGB nearest turns a
	// saturated green into a grey, and the open question was whether the same
	// change should be made at sixteen. What blocked it was having no arbiter
	// -- "rendering a page of Channel B colours both ways in a real terminal
	// and looking". qtty has an arbiter that needs no screen:
	// has_minimum_contrast(), the section 6 rule it already treats as a theme
	// bug when violated.
	//
	// Measured over 4374 saturated colours (HSV saturation > 0.5), counting
	// those whose quantised result has no contrast against the background:
	//
	//     against black:  RGB 470   Lab 1018
	//     against white:  RGB 288   Lab  228
	//
	// Lab more than doubles the failures on a dark ground and gains a little
	// on a light one, and terminals are mostly dark. That is the opposite of
	// the 256-colour case and it is not a contradiction: at 240 candidates the
	// perceptually nearest entry is close in every respect, and at sixteen it
	// is often a dark chromatic one with no luminance left.
	//
	// rgb(0, 15, 195) is the case named rather than a threshold asserted: a
	// saturated blue, which RGB sends to bright blue and Lab to dark blue.
	{
		const int fallback = Color::rgb(qRgb(0, 15, 195)).to_ansi16();
		CHECK(fallback == 12,
		      "the ANSI-16 fallback matches in RGB, not in Lab (OQ-7)");
		// And why 12 is the better answer, which is the half that would
		// notice the contrast rule changing under the decision.
		CHECK(has_minimum_contrast(Color::indexed(12), Color::indexed(0))
		      && !has_minimum_contrast(Color::indexed(4), Color::indexed(0)),
		      "and the answer it gives stays visible where Lab's would not");
	}


	// `Color::authored_ansi16()` had no caller anywhere either. The setter is
	// exercised through the role table; the reader is the only way an
	// application or a backend can ask what a colour was authored as, and
	// nothing had ever asked.
	{
		const Color plain = Color::rgb(qRgb(200, 30, 30));
		CHECK(plain.authored_ansi16() == -1,
		      "a colour with no authored index says so rather than "
		      "claiming index 0");
		CHECK(plain.with_ansi16(9).authored_ansi16() == 9,
		      "and one given an authored index reads it back");
		// The header states both no-ops. Neither had ever been read back,
		// so neither was more than a comment.
		CHECK(plain.with_ansi16(16).authored_ansi16() == -1
		      && plain.with_ansi16(-1).authored_ansi16() == -1,
		      "an out-of-range index is refused, so a role with no entry "
		      "falls through to the nearest match");
		CHECK(Color().with_ansi16(3).authored_ansi16() == -1,
		      "and the terminal's own colour is not improved on by naming "
		      "one of the sixteen");
	}

	// ---- the authored table's foreground half (8.248) ---------------------
	//
	// ansi16_for_role() authors twenty roles and sorts them, in the source,
	// into foregrounds, backgrounds and bevel roles. Each of those three
	// sections is a list somewhere in the paint path: the surfaces are
	// bg_for()'s and brush_cell()'s, the bevels are line_for()'s, and the
	// foregrounds are text_style_for()'s. Only the surface half was
	// complete. The foreground list named four of the nine the table
	// spells, so BrightText, PlaceholderText, Link, LinkVisited and Accent
	// each carried a hand-authored index, a paragraph of reasoning beside
	// it, and no route to a cell -- and a colour no role explains is
	// carried out as the application's own 24-bit value and nearest-matched
	// at Ansi16, which is the outcome the note at the top of theme.cpp says
	// the table exists to prevent.
	//
	// THE PALETTE AS IT STANDS CANNOT SEPARATE THE ANSWERS, which is why
	// this fixture installs one. Fusion's Link is pure blue and the nearest
	// of the sixteen to pure blue IS the authored 12; its LinkVisited is
	// pure magenta and the nearest is the authored 13. A check reading the
	// emitted index on the palette as it stands would pass just as loudly
	// against a library that had never heard of either role. Every fixture
	// colour below is therefore chosen so that its nearest match is NOT its
	// authored index, and that separation is asserted first.
	//
	// A from_palette theme, because the table is observable in no other
	// regime: terminal_default() is Color::Default everywhere and
	// with_ansi16() refuses to name an index on a Default colour.
	{
		const QPalette saved_palette = QGuiApplication::palette();
		const CellTheme saved_theme = theme();

		// Teal for the link, deliberately: 0x0e6b6b nearest-matches to 6,
		// which is the same wrong answer the measurement at the top of
		// theme.cpp records for Fusion's own highlight. The hand-authored
		// answer is 12.
		const QColor fx_link(0x0e, 0x6b, 0x6b);          // nearest 6, authored 12
		const QColor fx_visited(0x2a, 0x6b, 0x0e);       // nearest 2, authored 13
		const QColor fx_bright(0x7a, 0x0e, 0x0e);        // nearest 1, authored 15
		QPalette fx = saved_palette;
		fx.setColor(QPalette::Link, fx_link);
		fx.setColor(QPalette::LinkVisited, fx_visited);
		fx.setColor(QPalette::BrightText, fx_bright);
		QGuiApplication::setPalette(fx);
		set_theme(CellTheme::from_palette(QGuiApplication::palette()));

		const QPalette live = QGuiApplication::palette();
		const QColor fx_accent = live.color(QPalette::Accent);
		// Each fixture colour must belong to exactly one of the roles the
		// foreground list carries, across all three colour groups, or the
		// lookup could reach it by a route other than the one under test
		// and the checks below would prove nothing.
		const QVector<QPalette::ColorRole> fg_roles{
			QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
			QPalette::HighlightedText, QPalette::BrightText,
			QPalette::PlaceholderText, QPalette::Link,
			QPalette::LinkVisited, QPalette::Accent};
		const QVector<QPalette::ColorGroup> groups{
			QPalette::Active, QPalette::Inactive, QPalette::Disabled};
		const auto owners = [&](const QColor &c) {
			int n = 0;
			for (QPalette::ColorRole r : fg_roles)
				for (QPalette::ColorGroup g : groups)
					if (live.color(g, r).rgba() == c.rgba()) { ++n; break; }
			return n;
		};
		CHECK(owners(fx_link) == 1 && owners(fx_visited) == 1
		      && owners(fx_bright) == 1 && owners(fx_accent) == 1,
		      "the fixture separates the four roles it reads: each of their "
		      "colours belongs to exactly one foreground role");
		CHECK(Color::rgb(fx_link).to_ansi16() == 6
		      && Color::rgb(fx_visited).to_ansi16() == 2
		      && Color::rgb(fx_bright).to_ansi16() == 1
		      && Color::rgb(fx_accent).to_ansi16() == 6,
		      "and not one of them nearest-matches to the index its role "
		      "authors, or these checks could not tell the two apart");

		// THE HEADLINE. Links are live in this library -- holds_a_link()
		// decides whether a label is offered to the pointer -- so a QLabel
		// carrying an anchor is a case a user reaches, and it was reaching
		// a cell as a literal 24-bit colour with no authored index at all.
		{
			QLabel lab(QStringLiteral(
			    "<a href=\"https://example.invalid\">link</a>"));
			lab.setAttribute(Qt::WA_DontShowOnScreen);
			lab.resize(GridMetrics::cells(10, 1));
			lab.show();
			QCoreApplication::processEvents();
			CellBuffer b(10, 1);
			render_once(lab, b);
			const Cell &c = b.at(0, 0);
			CHECK(c.ch == QStringLiteral("l") && c.fg.to_ansi16() == 12,
			      "a QLabel's link draws in the 12 the role table authors, "
			      "not the 6 its palette colour nearest-matches to");
			// And the 24-bit tier is unchanged, which is the half the fix
			// could easily have paid with: the theme NAMES Link now, so a
			// terminal that can say 0x0e6b6b still gets 0x0e6b6b. Attaching
			// only the authored index would have sent every link out in the
			// window's text colour on the two deeper tiers.
			CHECK(c.fg.kind() == Color::Rgb
			      && (c.fg.value() & 0xffffff) == 0x0e6b6bu,
			      "and carries the palette's own colour still, so the true-"
			      "colour tier did not pay for the sixteen-colour one");
		}

		// A QLineEdit's hint, the other case a user meets without the
		// application doing anything unusual. It came out at 7 -- BODY
		// TEXT -- so a field holding a hint was indistinguishable from one
		// holding a value.
		//
		// The alpha is why, and it is why this needed more than a longer
		// list. Fusion spells PlaceholderText 0x80000000, a half
		// transparent black, and the pen path asked role_of() with qRgb()
		// -- alpha discarded -- so the query was opaque black, which IS
		// WindowText's colour. The role could not have matched even once
		// it was listed, and the wrong one matched every time.
		{
			QLineEdit e;
			e.setPlaceholderText(QStringLiteral("hint"));
			e.setAttribute(Qt::WA_DontShowOnScreen);
			e.resize(GridMetrics::cells(10, 1));
			e.show();
			QCoreApplication::processEvents();
			CellBuffer b(10, 1);
			render_once(e, b);
			const Cell &c = b.at(1, 0);
			CHECK(c.ch == QStringLiteral("h") && c.fg.to_ansi16() == 8,
			      "a QLineEdit's placeholder draws at the authored 8, which "
			      "reads as dimmed, and not at body text's 7");
		}

		// The remaining three have no widget in Qt that paints in them
		// here: nothing sends a QEvent::ToolTip (section 7), QLabel keeps
		// no visited-link state, and GridStyle draws its own buttons rather
		// than asking for BrightText. What an application reaches them with
		// is a palette -- tinting a label with the product's own accent is
		// the ordinary way -- and role_of() keys on the colour, so that is
		// the whole of the path either way.
		{
			struct { QColor colour; int authored; const char *sentence; }
			tinted[] = {
				{fx_accent, 12,
				 "a label tinted with the palette's accent draws at the "
				 "authored 12, not the 6 that accent nearest-matches to"},
				{fx_visited, 13,
				 "and one tinted with LinkVisited at 13, not 2"},
				{fx_bright, 15,
				 "and one tinted with BrightText at 15, not 1"},
			};
			for (const auto &t : tinted) {
				QLabel lab(QStringLiteral("x"));
				QPalette lp = lab.palette();
				lp.setColor(QPalette::WindowText, t.colour);
				lab.setPalette(lp);
				lab.setAttribute(Qt::WA_DontShowOnScreen);
				lab.resize(GridMetrics::cells(6, 1));
				lab.show();
				QCoreApplication::processEvents();
				CellBuffer b(6, 1);
				render_once(lab, b);
				CHECK(b.at(0, 0).fg.to_ansi16() == t.authored, t.sentence);
			}
		}

		// THE CONTROL, and it is not a formality. The one way to make these
		// five roles reachable that would have been WRONG is to put a role
		// whose colour collides with body text's ahead of the roles that
		// already resolved -- ToolTipText is black here, exactly as
		// WindowText is -- and that mistake shows up in this check and in
		// no other.
		{
			QLabel lab(QStringLiteral("body"));
			lab.setAttribute(Qt::WA_DontShowOnScreen);
			lab.resize(GridMetrics::cells(8, 1));
			lab.show();
			QCoreApplication::processEvents();
			CellBuffer b(8, 1);
			render_once(lab, b);
			CHECK(b.at(0, 0).ch == QStringLiteral("b")
			      && b.at(0, 0).fg.to_ansi16() == 7,
			      "and ordinary body text still draws at 7, so the five new "
			      "roles took nothing from the four that already resolved");
		}

		set_theme(saved_theme);
		QGuiApplication::setPalette(saved_palette);
		CHECK(QGuiApplication::palette().color(QPalette::Link)
		          == saved_palette.color(QPalette::Link)
		      && theme().window_text == saved_theme.window_text
		      && theme().accent == saved_theme.accent,
		      "and both the palette and the theme are put back, so no later "
		      "check inherits this fixture");
	}

	// ---- the two roles that stay out of the foreground list, and why ------
	//
	// TOOLTIPTEXT IS THE ONE FOREGROUND STILL OMITTED, and the reason is a
	// collision rather than an oversight. role_of() keys on the colour, and
	// Fusion spells ToolTipText 0xff000000 -- the same black as WindowText,
	// Text and ButtonText. Wherever it sat in the list it would either be
	// shadowed by them or shadow them, and the second is the expensive
	// direction: every body glyph in the program would take the tooltip's
	// authored 0, black ink on a terminal whose ground is black. Section 7
	// records separately that no QEvent::ToolTip is ever sent, so nothing
	// draws in the role today either -- but that is a second reason, and
	// the exclusion would stand if tooltips started popping tomorrow.
	//
	// ACCENT IS IN THE FOREGROUND LIST AND IN NEITHER SURFACE LIST. The
	// table authors 12 for it, an ink index picked so that an accented
	// widget inside a selection is still visible, and 12 as a GROUND would
	// be a bright blue surface nobody asked for. The second half is the
	// measurement below: Accent and Highlight are one colour here, so an
	// Accent entry in a surface list could never win against the Highlight
	// entry already ahead of it -- a line that cannot execute.
	//
	// Both are asserted rather than merely written down, so that a Qt or a
	// platform theme separating either pair reddens a check instead of
	// leaving two paragraphs of reasoning quietly false.
	{
		const QPalette live = QGuiApplication::palette();
		CHECK(live.color(QPalette::ToolTipText).rgba()
		      == live.color(QPalette::WindowText).rgba(),
		      "ToolTipText and WindowText are one colour here, which is why "
		      "ToolTipText is not in the foreground list -- revisit the list "
		      "if this goes red");
		CHECK(live.color(QPalette::Accent).rgba()
		      == live.color(QPalette::Highlight).rgba(),
		      "and Accent and Highlight are one colour here, which is why "
		      "Accent is a foreground role only -- revisit the surface lists "
		      "if this goes red");
	}

	// ---- CellTheme::accent, which was dead at both ends (8.248) -----------
	//
	// The field was declared, from_palette() set its eight siblings and not
	// it, and neither foreground() nor background() had a case for it. An
	// application assigning theme.accent got a silent no-op, and the
	// authored 12 beside QPalette::Accent had no colour to attach itself to.
	//
	// Its initialiser went with the wiring. It was the one field in the
	// struct that did not default to Color::Default, so terminal_default()
	// -- whose whole contract is that it names no colour and lets the
	// terminal's own scheme stand -- was quietly naming a hard indexed blue
	// for one role. Nothing read it, so nothing noticed.
	{
		const CellTheme d = CellTheme::terminal_default();
		CHECK(d.accent == Color(),
		      "the default theme names no accent either, so every field of "
		      "it really is the terminal's own colour");

		QPalette pal = QGuiApplication::palette();
		pal.setColor(QPalette::Accent, QColor(0x8a, 0x1f, 0x5c));
		const CellTheme t = CellTheme::from_palette(pal);
		CHECK(t.accent == Color::rgb(qRgb(0x8a, 0x1f, 0x5c)),
		      "from_palette() captures the accent, which it did not");
		CHECK(t.foreground(QPalette::Accent).value() == qRgb(0x8a, 0x1f, 0x5c)
		      && t.foreground(QPalette::Accent).to_ansi16() == 12,
		      "and foreground() answers with it, carrying the authored 12");

		// The case the field exists for: an application's own theme, with
		// an accent of its own, reaching a cell.
		const CellTheme saved_theme = theme();
		CellTheme mine = CellTheme::from_palette(QGuiApplication::palette());
		mine.accent = Color::rgb(qRgb(0x8a, 0x1f, 0x5c));
		set_theme(mine);
		QLabel lab(QStringLiteral("x"));
		QPalette lp = lab.palette();
		lp.setColor(QPalette::WindowText,
		            QGuiApplication::palette().color(QPalette::Accent));
		lab.setPalette(lp);
		lab.setAttribute(Qt::WA_DontShowOnScreen);
		lab.resize(GridMetrics::cells(6, 1));
		lab.show();
		QCoreApplication::processEvents();
		CellBuffer b(6, 1);
		render_once(lab, b);
		set_theme(saved_theme);
		CHECK(b.at(0, 0).fg.kind() == Color::Rgb
		      && (b.at(0, 0).fg.value() & 0xffffff) == 0x8a1f5cu,
		      "and an application that sets theme.accent sees it on the "
		      "wire, where the field used to be a no-op");
	}

	return fails;
}
