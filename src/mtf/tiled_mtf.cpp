#include "tiled_mtf.h"
#include "word_encoding.h" // kNotSeen, seenAt, initialWord: the word encoding, in one place

#include <algorithm>

void tileLastSeen(const uint8_t* in, size_t begin, size_t end, uint32_t* words)
{
    std::fill(words, words + 256, kNotSeen);

    // A later occurrence overwrites the word an earlier one wrote, so one pass in order
    // leaves each symbol's last position in the tile.
    for (size_t i = begin; i < end; i++)
    {
        words[in[i]] = seenAt(i);
    }
}

void tiledMtf(const uint8_t* in, uint8_t* out, size_t n, size_t tileSize, DepthHistogram* depth,
              std::vector<uint32_t>* startWords)
{
    const size_t tiles = (n + tileSize - 1) / tileSize;

    // Phase 1: every tile computes its own words independently, as the GPU workgroups
    // will. This array is the model's copy of the GPU state buffer: 1 KB per tile.
    std::vector<uint32_t> local(tiles * 256);
    for (size_t t = 0; t < tiles; t++)
    {
        const size_t begin = t * tileSize;
        const size_t end = std::min(begin + tileSize, n);
        tileLastSeen(in, begin, end, &local[t * 256]);
    }

    if (startWords != nullptr)
    {
        startWords->assign(tiles * 256, 0);
    }

    // Step 2: the running exclusive prefix, per symbol. It starts as tile -1's words and,
    // once tile t has taken its start state, absorbs tile t's own words by max. O(256)
    // per tile, the same answer as walking back over the earlier tiles.
    uint32_t carry[256];
    for (int s = 0; s < 256; s++)
    {
        carry[s] = initialWord(s);
    }

    // Phase 2: each tile's look-back, then its encode.
    for (size_t t = 0; t < tiles; t++)
    {
        const size_t begin = t * tileSize;
        const size_t end = std::min(begin + tileSize, n);

        uint32_t start[256]; // tile t's start state: what its look-back produces
        std::fill(start, start + 256, 0xABABABABu); // poisoned, so a symbol you skip shows up
        for (int s = 0; s < 256; s++)
        {
            start[s] = carry[s];                                // max over tiles -1..t-1
            carry[s] = std::max(carry[s], local[t * 256 + s]); // now -1..t, for tile t + 1
            // TODO step 4: if (depth != nullptr), record how far back tile t looks for s:
            //              depth->record(distance) or depth->recordStart(tilesWalked).
        }

        if (startWords != nullptr)
        {
            std::copy(start, start + 256, startWords->begin() + t * 256);
        }

        // TODO step 3: encode in[begin, end) into out[begin, end), using start[] and
        //              the tile's own bytes only.
        (void)depth;
        (void)out;
        (void)end;
    }
}