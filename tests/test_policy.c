/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../src/serprog/main/readonly_policy.h"
int main(void)
{
    uint8_t tx[8193] = {0};
    for (unsigned op = 0; op < 256; ++op) {
        tx[0] = op;
        for (unsigned w = 0; w <= 8; ++w)
            for (unsigned r = 0; r <= 8; ++r) {
                bool expected =
                    (op == 0x9f && w == 1 && r >= 1 && r <= 3) ||
                    ((op == 5 || op == 0x35 || op == 0x15) && w == 1 && r == 1) ||
                    (op == 3 && w == 4 && r >= 1) ||
                    ((op == 0xb || op == 0x5a) && w == 5 && r >= 1);
                assert(reader_read_allowed(tx, w, r) == expected);
            }
        assert(!reader_read_allowed(tx, 8193, 1));
        assert(!reader_read_allowed(tx, 1, 8193));
        assert(!reader_read_allowed(tx, 1, 0));
    }
    assert(!reader_read_allowed(NULL, 1, 1));
    tx[0] = 3; tx[1] = tx[2] = tx[3] = 255;
    assert(reader_read_allowed(tx, 4, 1));
    assert(!reader_read_allowed(tx, 4, 2));
    tx[1] = tx[2] = tx[3] = 0;
    assert(reader_read_allowed(tx, 4, 8192));
    for (uint32_t max = 500000; max <= 4000000; max *= 2)
        for (uint32_t hz = 1; hz < 10000000; hz += 997) {
            uint32_t result = reader_clamp_frequency(hz, max);
            assert(result <= hz && result <= max);
            assert(!result || 80000000u % result == 0);
        }
    puts("PASS: all 256 opcodes, framing, boundaries and frequency limits");
}
