/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/combogen.h"
#include <stdint.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *data, size_t size, size_t offset)
{
    uint32_t out = 0;
    for (size_t i = 0; i < 4 && offset + i < size; i++)
        out |= ((uint32_t)data[offset + i]) << (i * 8U);
    return out;
}

static uint64_t read_u64(const uint8_t *data, size_t size, size_t offset)
{
    uint64_t out = 0;
    for (size_t i = 0; i < 8 && offset + i < size; i++)
        out |= ((uint64_t)data[offset + i]) << (i * 8U);
    return out;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data)
        return 0;

    int main_count = (int)(read_u32(data, size, 0) % 10);
    int main_min = (int)(read_u32(data, size, 4) % 60) - 5;
    int main_max = (int)(read_u32(data, size, 8) % 80) - 5;
    int extra_count = (int)(read_u32(data, size, 12) % 6);
    int extra_min = (int)(read_u32(data, size, 16) % 25) - 2;
    int extra_max = (int)(read_u32(data, size, 20) % 30) - 2;

    uint64_t seed = read_u64(data, size, 24);

    LotteryResult result;
    memset(&result, 0, sizeof(result));

    generate_draw(main_count, main_min, main_max, extra_count, extra_min, extra_max, &result, NULL);
    generate_draw_seeded(main_count, main_min, main_max, extra_count, extra_min, extra_max, seed,
                         &result, NULL);

    return 0;
}
