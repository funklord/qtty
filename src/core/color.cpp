// src/core/color.cpp — quantisation and contrast (§6).
#include "qtty/color.h"

namespace qtty {

// xterm 256: 16 system + 6x6x6 cube (16..231) + grey ramp (232..255).
static int cubeLevel(int v) {                 // 0..255 → 0..5 (cube levels 0,95,135,175,215,255)
    if (v < 48) return 0;
    if (v < 115) return 1;
    return (v - 35) / 40;
}
static int cubeValue(int level) { return level ? 55 + level * 40 : 0; }

int Color::toXterm256() const {
    if (kind_ == Indexed) return index_;
    if (kind_ == Default) return -1;                     // caller emits 39/49
    const int r = qRed(rgb_), g = qGreen(rgb_), b = qBlue(rgb_);
    // candidate 1: cube
    const int cr = cubeLevel(r), cg = cubeLevel(g), cb = cubeLevel(b);
    const int cubeIdx = 16 + 36 * cr + 6 * cg + cb;
    const int cd = (cubeValue(cr)-r)*(cubeValue(cr)-r)
                 + (cubeValue(cg)-g)*(cubeValue(cg)-g)
                 + (cubeValue(cb)-b)*(cubeValue(cb)-b);
    // candidate 2: grey ramp (8,18,...,238)
    const int grey = (r + g + b) / 3;
    int gl = qBound(0, (grey - 8) / 10, 23);
    const int gv = 8 + gl * 10;
    const int gd = (gv-r)*(gv-r) + (gv-g)*(gv-g) + (gv-b)*(gv-b);
    return gd < cd ? 232 + gl : cubeIdx;
}

int Color::toAnsi16() const {
    if (kind_ == Default) return -1;
    if (kind_ == Indexed && index_ < 16) return index_;
    // fallback nearest of the 16 (themes should map roles explicitly — §6)
    static const QRgb basic[16] = {
        0xFF000000, 0xFF800000, 0xFF008000, 0xFF808000,
        0xFF000080, 0xFF800080, 0xFF008080, 0xFFC0C0C0,
        0xFF808080, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
        0xFF0000FF, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF };
    QRgb c = kind_ == Rgb ? rgb_ : 0xFF000000;
    int best = 7, bestD = INT_MAX;
    for (int i = 0; i < 16; ++i) {
        int d = (qRed(basic[i])-qRed(c))*(qRed(basic[i])-qRed(c))
              + (qGreen(basic[i])-qGreen(c))*(qGreen(basic[i])-qGreen(c))
              + (qBlue(basic[i])-qBlue(c))*(qBlue(basic[i])-qBlue(c));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int Color::luminance(bool isForeground) const {
    switch (kind_) {
    case Default: return isForeground ? 210 : 20;        // conventional dark theme
    case Rgb:     return (qRed(rgb_) * 299 + qGreen(rgb_) * 587 + qBlue(rgb_) * 114) / 1000;
    case Indexed: {
        if (index_ >= 232) return 8 + (index_ - 232) * 10;
        if (index_ >= 16) {
            int i = index_ - 16;
            int r = cubeValue(i / 36), g = cubeValue((i / 6) % 6), b = cubeValue(i % 6);
            return (r * 299 + g * 587 + b * 114) / 1000;
        }
        static const int sysLum[16] = {0,32,80,96,32,48,80,192,
                                       128,96,180,220,96,150,200,255};
        return sysLum[index_];
    }}
    return 128;
}

bool hasMinimumContrast(const Color &fg, const Color &bg, int minDelta) {
    return qAbs(fg.luminance(true) - bg.luminance(false)) >= minDelta;
}

} // namespace qtty
