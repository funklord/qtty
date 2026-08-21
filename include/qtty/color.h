// qtty/color.h — L2 colour model and quantisation (§6).
#pragma once
#include <QColor>
#include <QFlags>

namespace Qtty {

// A terminal colour: the terminal's defaults, a palette index, or true colour.
// Quantisation to the backend's depth happens at present time (§6), so L2-L4
// carry full-fidelity colour.
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

    bool operator==(const Color &o) const {
        return kind_ == o.kind_ && index_ == o.index_ && (kind_ != Rgb || rgb_ == o.rgb_);
    }
    bool operator!=(const Color &o) const { return !(*this == o); }

    // Nearest xterm-256 index (16..255: 6x6x6 cube + grey ramp).
    int toXterm256() const;
    // Ansi16: Default passes through; otherwise nearest of the 16 (a curated
    // map, §6 — nearest-match on 16 colours is a fallback, themes should map
    // roles explicitly).
    int toAnsi16() const;

    // Perceived luminance 0..255 (Default fg assumed light, bg dark).
    int luminance(bool isForeground) const;

private:
    constexpr Color(Kind k, quint8 i, QRgb c) : kind_(k), index_(i), rgb_(c) {}
    Kind kind_ = Default;
    quint8 index_ = 0;
    QRgb rgb_ = 0;
};

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

// §6 contrast rule: minimum luminance delta between fg and bg of a cell.
// Violations are a theme bug; debug builds log them at present time.
bool hasMinimumContrast(const Color &fg, const Color &bg, int minDelta = 48);

} // namespace Qtty
