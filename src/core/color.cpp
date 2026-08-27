// src/core/color.cpp -- quantisation and contrast (section 6).
#include "qtty/color.h"
#include <QHash>
#include <QVector>
#include <climits>
#include <cmath>

namespace Qtty {

// xterm 256: 16 system + 6x6x6 cube (16..231) + grey ramp (232..255).
//
// The inverse of this -- an RGB component to its cube level -- used to sit
// beside it and is gone with the RGB nearest match it served. Matching in
// CIELAB compares whole colours against the candidate table rather than
// choosing a level per channel, so there is nothing left to invert.
static int cubeValue(int level) { return level ? 55 + level * 40 : 0; }

// The 16 system colours, as the xterm defaults spell them. Terminals are free
// to re-map these, which is exactly why the Ansi16 route is a role table and
// not a distance calculation -- see theme.cpp.
static const QRgb system16[16] = {
	0xFF000000, 0xFF800000, 0xFF008000, 0xFF808000,
	0xFF000080, 0xFF800080, 0xFF008080, 0xFFC0C0C0,
	0xFF808080, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
	0xFF0000FF, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF };

QRgb xterm256_rgb(int index) {
	if (index < 0 || index > 255) return 0xFF000000;
	if (index < 16) return system16[index];
	if (index >= 232) { const int v = 8 + (index - 232) * 10; return qRgb(v, v, v); }
	const int i = index - 16;
	return qRgb(cubeValue(i / 36), cubeValue((i / 6) % 6), cubeValue(i % 6));
}

// ---- CIELAB (section 6) ----------------------------------------------------
//
// design.md section 6 requires the xterm-256 match to be made in CIELAB rather
// than in RGB, and one colour shows why. The mid-green 0x287832 sits 3812
// squared RGB units from the grey-ramp entry 238 and 3850 from the cube's
// nearest entry, so an RGB match quantises a saturated green to a grey by a
// margin of 38 parts in 3850. In Lab the same colour lands on index 22, a
// green, and it is not close: 13.9 against 19.0 for the runner-up.
struct Lab { double l = 0, a = 0, b = 0; };

static double srgb_to_linear(int v) {
	const double c = v / 255.0;
	return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

static Lab to_lab(QRgb c) {
	const double r = srgb_to_linear(qRed(c));
	const double g = srgb_to_linear(qGreen(c));
	const double b = srgb_to_linear(qBlue(c));
	// sRGB -> CIE XYZ, the sRGB specification's D65 matrix.
	const double x = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
	const double y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
	const double z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;
	// D65 white point, and the CIE 1976 nonlinearity with the exact (6/29)^3
	// knee rather than the rounded 0.008856 that predates the 2004 correction.
	const double xn = 0.95047, yn = 1.0, zn = 1.08883;
	auto f = [](double t) {
		return t > 216.0 / 24389.0 ? std::cbrt(t) : (841.0 / 108.0) * t + 4.0 / 29.0;
	};
	const double fx = f(x / xn), fy = f(y / yn), fz = f(z / zn);
	return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

// The 240 addressable xterm-256 colours (cube plus grey ramp) in Lab, built
// once. The candidates never change and the conversion is the expensive half,
// so precomputing them turns each match into 240 subtractions.
static const QVector<Lab> &candidate_lab() {
	static const QVector<Lab> table = [] {
		QVector<Lab> t;
		t.reserve(240);
		for (int i = 16; i < 256; ++i) t.append(to_lab(xterm256_rgb(i)));
		return t;
	}();
	return table;
}

int Color::toXterm256() const {
	if (kind_ == Indexed) return index_;
	if (kind_ == Default) return -1;                     // caller emits 39/49
	// Memoised on the colour itself. A frame holds a handful of distinct
	// colours however many cells it has, so the hit rate is close to one and
	// the match runs once per distinct colour rather than once per cell.
	// Single-threaded by construction: rendering and present() are both on the
	// GUI thread (design.md section 5.4).
	static QHash<QRgb, int> memo;
	const QRgb key = rgb_ | 0xFF000000;
	const auto hit = memo.constFind(key);
	if (hit != memo.constEnd()) return *hit;

	const Lab want = to_lab(key);
	const QVector<Lab> &cands = candidate_lab();
	int best = 16;
	double best_distance = 1e18;
	for (int i = 0; i < cands.size(); ++i) {
		const double dl = want.l - cands[i].l;
		const double da = want.a - cands[i].a;
		const double db = want.b - cands[i].b;
		const double d = dl * dl + da * da + db * db;
		if (d < best_distance) { best_distance = d; best = 16 + i; }
	}
	// Bounded. A Channel B image can present millions of distinct colours, and
	// a memo that only ever grows is a leak wearing a cache's costume. 4096 is
	// far more than a themed frame of text holds, and dropping the table
	// wholesale costs one re-match per surviving colour.
	if (memo.size() >= 4096) memo.clear();
	memo.insert(key, best);
	return best;
}

int Color::toAnsi16() const {
	if (kind_ == Default) return -1;
	// Primary route (section 6): the hand-authored role table in theme.cpp,
	// attached to the colour by CellTheme when it resolved the role.
	if (ansi16_ >= 0) return ansi16_;
	if (kind_ == Indexed && index_ < 16) return index_;
	// Fallback, and only that: a colour with no palette role behind it --
	// Channel B output, or a QColor the application chose itself -- has no
	// authored spelling, so the nearest of the sixteen is the best available.
	// design.md section 6 rejects this as the *primary* route, not as a
	// last resort.
	const QRgb c = kind_ == Rgb ? rgb_ : xterm256_rgb(index_);
	int best = 7, best_distance = INT_MAX;
	for (int i = 0; i < 16; ++i) {
		const int dr = qRed(system16[i]) - qRed(c);
		const int dg = qGreen(system16[i]) - qGreen(c);
		const int db = qBlue(system16[i]) - qBlue(c);
		const int d = dr * dr + dg * dg + db * db;
		if (d < best_distance) { best_distance = d; best = i; }
	}
	return best;
}

int Color::luminance(bool isForeground) const {
	switch (kind_) {
	case Default: return isForeground ? 210 : 20;        // conventional dark theme
	case Rgb:     return (qRed(rgb_) * 299 + qGreen(rgb_) * 587 + qBlue(rgb_) * 114) / 1000;
	case Indexed: {
		if (index_ < 16) {
			// The system colours are the terminal's to re-map, so their
			// luminance is a judgement about what terminals actually show
			// rather than arithmetic on the table above.
			static const int sysLum[16] = {0,32,80,96,32,48,80,192,
						                   128,96,180,220,96,150,200,255};
			return sysLum[index_];
		}
		const QRgb v = xterm256_rgb(index_);
		return (qRed(v) * 299 + qGreen(v) * 587 + qBlue(v) * 114) / 1000;
	}}
	return 128;
}

bool hasMinimumContrast(const Color &fg, const Color &bg, int minDelta) {
	return qAbs(fg.luminance(true) - bg.luminance(false)) >= minDelta;
}

} // namespace Qtty
