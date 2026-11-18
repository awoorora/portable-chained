// How a per-symbol word stores a position. Kept in one place: the tiled model writes
// these words, the input size limit is derived from them, and the GPU kernel's words
// (without their flag bits) are checked against them.
#pragma once

#include <cstddef>
#include <cstdint>

// The GPU keeps its flag bits above the payload (0x40000000 is READY), so every word
// the model writes must fit in the low 30 bits.
constexpr int kPayloadBits = 30;
constexpr uint32_t kMaxWord = (uint32_t(1) << kPayloadBits) - 1;

// Tile -1. MTF's initial list [0, 1, ..., 255] is the list it leaves after the 256
// bytes 255, 254, ..., 1, 0, so pretend those bytes sit just before position 0:
// symbol s at position -1 - s. Every symbol is in tile -1, and its words are final.
//
// Words are position + 257: tile -1's positions -256..-1 get words 1..256, real
// positions start at 257 and beat all of them, and 0 is left for "not seen". Tiles
// combine by a per-symbol max, so "not seen" must be the max's identity, 0.
constexpr uint32_t kNotSeen = 0;
constexpr uint32_t kWordOffset = 256 + 1; // tile -1's 256 positions, plus 0 kept for "not seen"

constexpr uint32_t seenAt(size_t pos)
{
    return uint32_t(pos + kWordOffset);
}

// Tile -1's word for symbol s, which sits at position -1 - s: 256 for symbol 0 down
// to 1 for symbol 255.
constexpr uint32_t initialWord(int s)
{
    return kWordOffset - 1 - uint32_t(s);
}

static_assert(kNotSeen < initialWord(255) && initialWord(0) < seenAt(0),
              "order: not seen < every tile -1 word < every real position");