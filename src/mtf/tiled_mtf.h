// CPU model of the per-symbol tiled MTF design. It runs the tiles one after another,
// but each tile only uses what the GPU version would have: its own bytes and the
// words its predecessors posted. Its output must equal serialMtf's.
#pragma once

#include "depth_histogram.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Step 1. One tile's own state, computed from in[begin, end) alone: for each of the
// 256 symbols, one 32-bit word saying where in the whole input the symbol was last
// seen inside the tile, or that it wasn't seen there. words must hold 256 entries.
// These are the words the GPU kernel will post (without its flag bits) and will be
// checked against.
void tileLastSeen(const uint8_t* in, size_t begin, size_t end, uint32_t* words);

// Steps 2-4. Writes out[0, n). If depth is not null, records one look-back distance
// per (tile, symbol). If startWords is not null, it is resized to tiles * 256 and
// receives each tile's start state (step 2), for checks and for the GPU comparison.
void tiledMtf(const uint8_t* in, uint8_t* out, size_t n, size_t tileSize, DepthHistogram* depth = nullptr,
              std::vector<uint32_t>* startWords = nullptr);
