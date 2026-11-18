// Look-back depth statistics for the tiled MTF model: how many tiles back a tile has
// to look for a symbol before it finds an earlier tile that contains it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// One histogram per (input, tile size). A tile's look-back for a symbol starts at the
// previous tile, so distances are 1 or more and go into power-of-two buckets:
//   1, 2, 3-4, 5-8, 9-16, ...
// plus a separate "start" bucket for a look-back that found no earlier tile
// containing the symbol and ended at the start of the input. (Bucket 0, distance 0,
// exists but stays empty.)
//
// Separately, `present` counts the (tile, symbol) pairs where the symbol is in the
// tile itself: those words the tile can pass on to later tiles without any look-back.
struct DepthHistogram
{
    static constexpr int kBuckets = 33; // bucket 32 ends at 2^31 tiles; inputs stay below 2^30 bytes

    uint64_t counts[kBuckets] = {};
    uint64_t start = 0;        // look-backs that reached the start
    uint64_t total = 0;        // everything recorded, "start" included
    uint64_t sumFound = 0;     // sum of distances over the found ones
    uint64_t sumStartWalk = 0; // sum of tiles walked by the ones that reached the start
    uint64_t maxFound = 0;     // largest distance among the found ones
    uint64_t present = 0;      // pairs whose symbol is in the tile itself (not part of total)

    // The symbol was found `distance` tiles back (0: in the tile itself).
    void record(uint64_t distance);

    // No earlier tile contains the symbol; the look-back walked `tilesWalked` tiles
    // before it got to the start.
    void recordStart(uint64_t tilesWalked);

    // The symbol is in the tile itself. Call it in addition to record or recordStart.
    void notePresent();

    // Bucket b holds the distances lo..hi.
    static int bucketOf(uint64_t distance);
    static void bucketRange(int bucket, uint64_t& lo, uint64_t& hi);
};

// One block per histogram: a line per bucket from the first to the last non-empty one,
// then "start", then the share of pairs present in their own tile.
void printDepthHistogram(const char* input, size_t tileSize, size_t tiles, const DepthHistogram& h);

// Long-format CSV for the plot scripts: one row per non-empty bucket.
//   input,bytes,tile_size,tiles,bucket,lo,hi,count
// "start" and "present" rows have bucket = start or present and empty lo and hi.
// "present" counts are not part of the distance total.
void writeDepthCsvHeader(FILE* f);
void writeDepthCsvRows(FILE* f, const char* input, size_t bytes, size_t tileSize, size_t tiles,
                       const DepthHistogram& h);
