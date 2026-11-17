#include "serial_mtf.h"

void serialMtf(const uint8_t* in, uint8_t* out, size_t n)
{
    // Keep it simple and obviously correct: everything else is checked
    // against it, and it is also the "serial CPU MTF" timing baseline in RQ2.

    uint8_t list[256];
    for (int i = 0; i < 256; i++)
    {
        list[i] = uint8_t(i);
    }

    for (size_t i = 0; i < n; i++)
    {
        const uint8_t symbol = in[i];

        int j = 0;
        while (list[j] != symbol)
        {
            j++;
        }
        out[i] = uint8_t(j);

        // Move to front: shift list[0..j-1] right by one, put the symbol at 0.
        for (int k = j; k > 0; k--)
        {
            list[k] = list[k - 1];
        }
        list[0] = symbol;
    }
}