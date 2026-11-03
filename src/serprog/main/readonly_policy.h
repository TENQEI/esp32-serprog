/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef READONLY_POLICY_H
#define READONLY_POLICY_H
#include <stdbool.h>
#include <stdint.h>
/* Every SPI transaction is independently validated; no state-changing opcode. */
static inline bool reader_read_allowed(const uint8_t *tx, uint32_t w, uint32_t r)
{
    if (!tx || !w || !r || w > 8192 || r > 8192) return false;
    switch (tx[0]) {
    case 0x9f: return w == 1 && r <= 3; /* JEDEC ID */
    case 0x05: case 0x35: case 0x15:
        return w == 1 && r == 1;       /* SR1/2/3 */
    case 0x03: case 0x0b: case 0x5a: {
        uint32_t header = tx[0] == 0x03 ? 4 : 5;
        if (w != header) return false;
        uint32_t address = ((uint32_t)tx[1] << 16) |
                           ((uint32_t)tx[2] << 8) | tx[3];
        /* 24-bit addressing only. Prevent wrap at the end of 16 MiB. */
        return r <= 0x1000000u - address;
    }
    default: return false;
    }
}
static inline uint32_t reader_clamp_frequency(uint32_t hz, uint32_t ceiling)
{
    /* Exact APB / integer-divider steps only; never round upwards. */
    const uint32_t steps[] = {4000000, 2000000, 1000000, 500000, 250000, 100000};
    if (hz > ceiling) hz = ceiling;
    for (unsigned i = 0; i < sizeof(steps)/sizeof(steps[0]); ++i)
        if (hz >= steps[i]) return steps[i];
    return 0;
}
#endif
