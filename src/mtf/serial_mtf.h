// Serial move-to-front: the reference every other MTF version is checked against.
#pragma once

#include <cstddef>
#include <cstdint>

// The list starts as [0, 1, ..., 255]. out[i] is the position of in[i] in the
// list just before in[i] is moved to the front. in and out must not overlap.
void serialMtf(const uint8_t* in, uint8_t* out, size_t n);
