#ifndef MASK_FMT_H
#define MASK_FMT_H

#include "game.h"

// Format a mask as a binary string with underscore separators every 8 bits.
// Uses a rotating buffer (4 slots) so up to 4 calls can appear in one OSReport.
// Example: MaskBits(0x1A3, 16) -> "00000001_10100011"
static inline const char *MaskBits(u32 val, int bits)
{
    // 32 bits + 3 underscores + null = 36 max
    static char bufs[4][36];
    static int idx;
    char *buf = bufs[idx++ & 3];

    int pos = 0;
    for (int i = bits - 1; i >= 0; i--)
    {
        buf[pos++] = (val & (1u << i)) ? '1' : '0';
        if (i > 0 && (i % 8) == 0)
            buf[pos++] = '_';
    }
    buf[pos] = '\0';
    return buf;
}

#endif // MASK_FMT_H
