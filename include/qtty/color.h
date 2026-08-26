// qtty/color.h -- L2 colour model and quantisation (section 6).
#pragma once
#include <QColor>
#include <QFlags>

namespace Qtty {

// A terminal colour: the terminal's defaults, a palette index, or true colour.
// Quantisation to the backend's depth happens at present time (section 6), so L2-L4
// carry full-fidelity colour.
//
// A colour may also carry the ANSI-16 index authored for the palette role it
// came from. That is the primary 16-colour route (section 6: an explicit
// hand-authored table, never a nearest match); CellTheme attaches it in
// theme.cpp, where the roles live. The index rides with the colour so that the
// backend can emit it without knowing anything about QPalette, and so that
// this header does not gain a QtGui palette type -- project.md section 7.1
// counts every GUI type reachable from cell.h against extracting L2.
class Color {
public:
	enum Kind : quint8 { Default, Indexed, Rgb };

	constexpr Color() = default;
	static constexpr Color indexed(quint8 i) { return Color(Indexed, i, 0); }
	static Color rgb(QRgb c) { return Color(Rgb, 0, c); }
	static Color rgb(const QColor &c) { return rgb(c.rgba()); }

	Kind kind() const { return kind_; }
	quint8 index() const { return index_; }
	QRgb value() const { return rgb_; }

	// A copy carrying `index` (0..15) as this colour's authored ANSI-16
	// spelling. Out-of-range values and Color::Default are no-ops: "the
	// terminal's own colour" is not improved on by naming one of the sixteen,
	// and a role with no authored entry must fall through to the nearest match
	// rather than silently take index 0.
	Color with_ansi16(int index) const {
		Color c = *this;
		if (kind_ != Default && index >= 0 && index < 16) c.ansi16_ = qint8(index);
		return c;
	}
	int authored_ansi16() const { return ansi16_; }        // -1 when none

	// The authored index is part of identity. Two colours with the same RGB
	// and different authored indices emit different bytes on a 16-colour
	// terminal, so a diff that called them equal would leave the wrong one on
	// screen.
	bool operator==(const Color &o) const {
		return kind_ == o.kind_ && index_ == o.index_ && ansi16_ == o.ansi16_
			&& (kind_ != Rgb || rgb_ == o.rgb_);
	}
	bool operator!=(const Color &o) const { return !(*this == o); }

	// Nearest xterm-256 index (16..255: 6x6x6 cube + grey ramp), matched in
	// CIELAB rather than in RGB as design.md section 6 requires. Memoised.
	int toXterm256() const;
	// Ansi16 (section 6). Default passes through; an authored index -- attached by
	// CellTheme from the hand-written role table -- is the primary route;
	// nearest-of-16 is the fallback for colours that arrive with no role at
	// all, which is what Channel B output is.
	int toAnsi16() const;

	// Perceived luminance 0..255 (Default fg assumed light, bg dark).
	int luminance(bool isForeground) const;

private:
	constexpr Color(Kind k, quint8 i, QRgb c) : kind_(k), index_(i), rgb_(c) {}
	Kind kind_ = Default;
	quint8 index_ = 0;
	qint8 ansi16_ = -1;                 // authored ANSI-16 index, -1 for none
	QRgb rgb_ = 0;
};

// The RGB an xterm-256 palette index stands for (all 256 of them: the 16
// system colours, the 6x6x6 cube, the 24-step grey ramp). Shared because the
// Lab candidate table, the luminance calculation and the nearest-of-16
// fallback all need the same answer.
QRgb xterm256_rgb(int index);

enum class Attr : quint8 {
	None      = 0x00,
	Bold      = 0x01,
	Dim       = 0x02,
	Italic    = 0x04,
	Underline = 0x08,
	Reverse   = 0x10,
	Strike    = 0x20,
};
Q_DECLARE_FLAGS(Attrs, Attr)
Q_DECLARE_OPERATORS_FOR_FLAGS(Attrs)

// section 6 contrast rule: minimum luminance delta between fg and bg of a cell.
// Violations are a theme bug; debug builds log them at present time.
bool hasMinimumContrast(const Color &fg, const Color &bg, int minDelta = 48);

} // namespace Qtty
