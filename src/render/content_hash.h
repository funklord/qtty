// src/render/content_hash.h -- the 64-bit key that means "this picture".
//
// INTERNAL. Not shipped in include/qtty/, because nothing outside the library
// needs it: what leaves the tree is the quint64 in CellImage::key, not the
// function that produced it.
//
// harvest() content-addresses a PixelSurface, because a surface is repainted
// rather than cached: a key taken from the widget would tell the kitty tier
// the image had not changed and it would keep showing the first frame. So the
// key must read every pixel, and this is the hot loop that does it -- once per
// placement per frame, over 8.6 MB for a full-screen surface.
//
// WHY NOT qHashBits, WHICH THIS REPLACES. It was the obvious spelling and it
// is wrong on Qt 5 in two ways that compound, both measured rather than read
// off the source:
//
//   - It is `h = 31 * h + byte`, the classic small-multiplier polynomial.
//     Reproduced bit-for-bit against Qt 5.15.15 on five inputs, and it has
//     CONSTRUCTIBLE collisions: raise one byte by 1, lower the next by 31,
//     and the hash is unchanged, because 31 * 31^k == 31^(k+1) exactly. For
//     one ARGB32 pixel that is dG = +1 with dR = -31 -- an ordinary colour
//     step, not a contrived one. 200000 frames differing in a single pixel
//     gave 27875 distinct keys: 172125 collisions where chance predicts 4.66.
//     Frames differing in eight pixels or a whole column collided 4 and 3
//     times, at chance, so the weakness sits exactly where an animated
//     readout lives -- in the small change.
//
//   - It returns uint on Qt 5 and size_t on Qt 6, so CellImage::key carried
//     32 bits on one arm and 64 on the other while being a quint64 on both.
//
// A collision is not cosmetic here. AnsiBackend::wire_id_for() is a registry:
// a key it already holds returns the stored wire id with minted = false and
// NOTHING IS RE-UPLOADED, so the terminal keeps displaying the picture that
// claimed the key first. That is the stale frame the content hash exists to
// prevent, reached through the hash instead of around it.
//
// WHAT THIS IS. Four independent lanes of multiply-xor-rotate over 8-byte
// words, folded and finished with murmur3's fmix64 avalanche. Each step is a
// bijection in its lane -- xor, odd-constant multiply and rotate all are --
// so no step can lose information, which is the property a run of identical
// pixels needs: a uniform region is where an accumulate-only hash collapses,
// and a 200000-frame sweep over a buffer built half of uniform runs collided
// zero times on both arms. One flipped bit moves 32.1 of the 64 output bits,
// against an ideal of 32.
//
// The lanes are there for throughput: a single chain is limited by multiply
// latency, four independent ones are limited by memory instead. Measured on
// the 8.6 MB full-screen harvest, both hashes interleaved in one process so
// the comparison cannot straddle a change in machine load, on an otherwise
// idle machine:
//
//                       Qt 5        Qt 6
//     qHashBits        11.76 ms     3.08 ms     (32 bits / 64 bits)
//     this             1.25 ms      1.25 ms     (64 bits on both)
//
// so 9.4x on Qt 5 and 2.5x on Qt 6, with change detection strictly better
// rather than traded away. Note that the two figures for this one agree to
// three digits, which is what the same code on both arms looks like: it runs
// at 7.2 GB/s and is bound by memory rather than by either Qt.
//
// Those `qHashBits` figures reproduce 8.600's 10.96 and 2.98, measured
// separately, which is the reason to trust the rest of the row. Taken under
// load the whole table inflates and unevenly -- at load ~155 it read 17.41
// and 4.45 against 2.20 and 2.41 -- because a memory-bound loop barely moves
// while a byte-at-a-time one swings with the CPU it is contending for. So
// never compare a row here against a row measured on another day. The alternative on the table was to SAMPLE the
// pixels rather than hash them all, which buys time by giving up exactly the
// property the key exists for -- a plot that alters only where the sampler
// did not look keeps its key, and the kitty tier then shows the previous
// frame for ever. Reading every byte faster costs nothing in detection.
// 8.616.
//
// Deterministic, and deliberately unseeded: keys are compared within one
// process and never persisted, so there is nothing for a per-process seed to
// defend against, and determinism is what lets the suite assert on the
// function at all.
#pragma once
#include <QtGlobal>
#include <cstring>

namespace Qtty {

// memcpy rather than a cast: the image's bits are only 4-byte aligned, and an
// 8-byte load through a reinterpret_cast is undefined behaviour that the
// sanitizer build in `make test-sanitize` is there to catch. Every compiler
// here turns this into one instruction.
inline quint64 read_word(const uchar *p) {
	quint64 v;
	memcpy(&v, p, sizeof v);
	return v;
}

inline quint64 rotate_left(quint64 x, int r) {
	return (x << r) | (x >> (64 - r));
}

// Odd 64-bit constants with well-mixed bit patterns. Each lane multiplies by
// a different one so the lanes cannot stay in step, and each is odd so the
// multiply is invertible mod 2^64.
inline quint64 content_hash(const void *p, size_t n) {
	const quint64 k1 = 0x9e3779b185ebca87ULL;
	const quint64 k2 = 0xc2b2ae3d27d4eb4fULL;
	const quint64 k3 = 0x165667b19e3779f9ULL;
	const quint64 k4 = 0x27d4eb2f165667c5ULL;
	const uchar *s = static_cast<const uchar *>(p);
	quint64 a = k1, b = k2, c = k3, d = k4;
	size_t i = 0;
	for (; i + 32 <= n; i += 32) {
		a = rotate_left(a ^ (read_word(s + i +  0) * k2), 31) * k1;
		b = rotate_left(b ^ (read_word(s + i +  8) * k3), 31) * k2;
		c = rotate_left(c ^ (read_word(s + i + 16) * k4), 31) * k3;
		d = rotate_left(d ^ (read_word(s + i + 24) * k1), 31) * k4;
	}
	// Folded with different rotations, so two lanes that happen to hold the
	// same value do not cancel on the way out.
	quint64 h = rotate_left(a, 1) + rotate_left(b, 7)
	          + rotate_left(c, 13) + rotate_left(d, 19);
	for (; i + 8 <= n; i += 8)
		h = rotate_left(h ^ (read_word(s + i) * k2), 31) * k1;
	quint64 tail = 0;
	for (; i < n; ++i) tail = (tail << 8) | s[i];
	h ^= tail * k3;
	// The length, so that the size of the picture is part of its identity
	// rather than only the bytes in it.
	h += quint64(n) * k4;
	// murmur3's fmix64: the avalanche that makes a one-bit input change move
	// half the output bits.
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	h ^= h >> 33;
	return h;
}

}  // namespace Qtty
