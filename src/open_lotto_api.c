/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "open_lotto_api.h"

#ifndef OPEN_LOTTO_VERSION_STRING
#define OPEN_LOTTO_VERSION_STRING "unknown"
#endif

static int has_enough_numbers(int count, int min_value, int max_value)
{
    return (max_value - min_value + 1) >= count;
}

int open_lotto_validate_spec(const OpenLottoDrawSpec *spec)
{
    if (!spec)
        return OPEN_LOTTO_API_ERR_INVALID_ARG;

    if (spec->main_count <= 0 || spec->main_count > MAX_MAIN_NUMBERS)
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (spec->extra_count < 0 || spec->extra_count > MAX_EXTRA_NUMBERS)
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (spec->main_min <= 0 || spec->main_min > spec->main_max)
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (!has_enough_numbers(spec->main_count, spec->main_min, spec->main_max))
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (spec->extra_count > 0)
    {
        if (spec->extra_min < 0 || spec->extra_min > spec->extra_max)
            return OPEN_LOTTO_API_ERR_INVALID_SPEC;

        if (!has_enough_numbers(spec->extra_count, spec->extra_min, spec->extra_max))
            return OPEN_LOTTO_API_ERR_INVALID_SPEC;
    }

    return OPEN_LOTTO_API_SUCCESS;
}

int open_lotto_generate(const OpenLottoDrawSpec *spec, LotteryResult *out)
{
    if (!out)
        return OPEN_LOTTO_API_ERR_INVALID_ARG;

    int validation = open_lotto_validate_spec(spec);
    if (validation != OPEN_LOTTO_API_SUCCESS)
        return validation;

    generate_draw(spec->main_count, spec->main_min, spec->main_max, spec->extra_count,
                  spec->extra_min, spec->extra_max, out, NULL);

    return OPEN_LOTTO_API_SUCCESS;
}

int open_lotto_generate_seeded(const OpenLottoDrawSpec *spec, uint64_t seed, LotteryResult *out)
{
    if (!out)
        return OPEN_LOTTO_API_ERR_INVALID_ARG;

    int validation = open_lotto_validate_spec(spec);
    if (validation != OPEN_LOTTO_API_SUCCESS)
        return validation;

    generate_draw_seeded(spec->main_count, spec->main_min, spec->main_max, spec->extra_count,
                         spec->extra_min, spec->extra_max, seed, out, NULL);

    return OPEN_LOTTO_API_SUCCESS;
}

uint64_t open_lotto_derive_seed(uint64_t base_seed, uint64_t draw_index)
{
    if (draw_index == 0)
        return base_seed;

    uint64_t x = base_seed ^ draw_index;
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

const char *open_lotto_version(void)
{
    return OPEN_LOTTO_VERSION_STRING;
}
