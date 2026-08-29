#pragma once
namespace Qtty {
inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;
inline constexpr const char *version_string = "0.1.0";

// Attribution, not licensing: naming the holder says who wrote this and
// grants nothing. qtty is a library and has no --version of its own, so this
// is the surface that reaches a person -- a consuming program prints it
// beside the version, and qtty-negotiate does.
inline constexpr const char *copyright =
    "Copyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>";
}
