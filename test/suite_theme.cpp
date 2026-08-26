// suite_theme -- section 6: quantisation and contrast.
#include <qtty/qtty.h>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_theme() {
	fails = 0;

	CHECK(Color().toXterm256() == -1, "Default maps to terminal default (-1)");
	CHECK(Color::indexed(196).toXterm256() == 196, "Indexed passes through");
	CHECK(Color::rgb(qRgb(255, 0, 0)).toXterm256() == 196, "pure red -> cube 196");
	CHECK(Color::rgb(qRgb(0, 0, 0)).toXterm256() == 16, "black -> cube 16");
	CHECK(Color::rgb(qRgb(255, 255, 255)).toXterm256() == 231, "white -> cube 231");
	int grey = Color::rgb(qRgb(128, 128, 128)).toXterm256();
	CHECK(grey >= 232 || grey == 102, "mid grey -> grey ramp or grey cube cell");

	CHECK(Color::rgb(qRgb(255, 0, 0)).toAnsi16() == 9, "red -> bright red in 16");
	CHECK(Color().toAnsi16() == -1, "Default stays default in 16");

	CHECK(hasMinimumContrast(Color(), Color()), "terminal default fg/bg contrast ok");
	CHECK(!hasMinimumContrast(Color::rgb(qRgb(120, 120, 120)),
	                          Color::rgb(qRgb(128, 128, 128))),
	      "near-identical greys flagged as low contrast");

	CellTheme t = CellTheme::terminalDefault();
	CHECK(t.text == Color() && t.window == Color(),
	      "default theme is all terminal-default");

	QPalette pal;
	pal.setColor(QPalette::Highlight, QColor(30, 90, 200));
	CellTheme p = CellTheme::fromPalette(pal);
	CHECK(p.highlight == Color::rgb(QColor(30, 90, 200)),
	      "fromPalette captures roles as Rgb");

	return fails;
}
