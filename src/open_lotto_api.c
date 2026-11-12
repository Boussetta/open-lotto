/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file open_lotto_api.c
 * @brief Stable embedding API wrappers around the core draw engine.
 */

#include "open_lotto_api.h"

#ifndef OPEN_LOTTO_VERSION_STRING
#define OPEN_LOTTO_VERSION_STRING "unknown"
#endif

/**
 * @brief Check whether an inclusive numeric range can satisfy count unique picks.
 */
static int has_enough_numbers(int count, int min_value, int max_value)
{
    return ((int64_t)max_value - (int64_t)min_value + 1) >= (int64_t)count;
}

/**
 * @brief Validate user-facing draw specification before generation.
 */
int open_lotto_validate_spec(const OpenLottoDrawSpec *spec)
{
    if (!spec)
        return OPEN_LOTTO_API_ERR_INVALID_ARG;

    if (spec->main_count <= 0 || spec->main_count > MAX_MAIN_NUMBERS)
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (spec->extra_count < 0 || spec->extra_count > MAX_EXTRA_NUMBERS)
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (spec->main_min <= 0 || spec->main_min > spec->main_max ||
        spec->main_max > COMBOGEN_MAX_NUMBER)
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (!has_enough_numbers(spec->main_count, spec->main_min, spec->main_max))
        return OPEN_LOTTO_API_ERR_INVALID_SPEC;

    if (spec->extra_count > 0)
    {
        if (spec->extra_min < 0 || spec->extra_min > spec->extra_max ||
            spec->extra_max > COMBOGEN_MAX_NUMBER)
            return OPEN_LOTTO_API_ERR_INVALID_SPEC;

        if (!has_enough_numbers(spec->extra_count, spec->extra_min, spec->extra_max))
            return OPEN_LOTTO_API_ERR_INVALID_SPEC;
    }

    return OPEN_LOTTO_API_SUCCESS;
}

/**
 * @brief Generate one draw using validated specification and runtime entropy.
 */
int open_lotto_generate(const OpenLottoDrawSpec *spec, LotteryResult *out)
{
    if (!out)
        return OPEN_LOTTO_API_ERR_INVALID_ARG;

    int validation = open_lotto_validate_spec(spec);
    if (validation != OPEN_LOTTO_API_SUCCESS)
        return validation;

    generate_draw(spec->main_count, spec->main_min, spec->main_max, spec->extra_count,
                  spec->extra_min, spec->extra_max, out, NULL);

    if (out->main_count != spec->main_count)
        return OPEN_LOTTO_API_ERR_GENERATION;

    return OPEN_LOTTO_API_SUCCESS;
}

/**
 * @brief Generate one deterministic draw from validated specification and seed.
 */
int open_lotto_generate_seeded(const OpenLottoDrawSpec *spec, uint64_t seed, LotteryResult *out)
{
    if (!out)
        return OPEN_LOTTO_API_ERR_INVALID_ARG;

    int validation = open_lotto_validate_spec(spec);
    if (validation != OPEN_LOTTO_API_SUCCESS)
        return validation;

    generate_draw_seeded(spec->main_count, spec->main_min, spec->main_max, spec->extra_count,
                         spec->extra_min, spec->extra_max, seed, out, NULL);

    if (out->main_count != spec->main_count)
        return OPEN_LOTTO_API_ERR_GENERATION;

    return OPEN_LOTTO_API_SUCCESS;
}

static uint64_t api_splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

/**
 * @brief Derive per-draw deterministic seeds via SplitMix64 mixing.
 *
 * Draw 0 uses the base seed unchanged. Later draws use
 * splitmix64(splitmix64(base) + index): hashing the base first means nearby
 * base seeds (e.g. 4 and 5) do not share per-draw seeds, which the previous
 * splitmix64(base ^ index) scheme did.
 */
uint64_t open_lotto_derive_seed(uint64_t base_seed, uint64_t draw_index)
{
    if (draw_index == 0)
        return base_seed;

    return api_splitmix64(api_splitmix64(base_seed) + draw_index);
}

/**
 * @brief Return compile-time version string.
 */
const char *open_lotto_version(void)
{
    return OPEN_LOTTO_VERSION_STRING;
}
