/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef OPEN_LOTTO_API_H
#define OPEN_LOTTO_API_H

#include "combogen.h"
#include <stdint.h>

/**
 * @file open_lotto_api.h
 * @brief Stable C API for embedding open-lotto draw generation.
 */

#ifdef __cplusplus
extern "C"
{
#endif

#define OPEN_LOTTO_API_SUCCESS 0
#define OPEN_LOTTO_API_ERR_INVALID_ARG 1
#define OPEN_LOTTO_API_ERR_INVALID_SPEC 2

    typedef struct
    {
        int main_count;
        int main_min;
        int main_max;
        int extra_count;
        int extra_min;
        int extra_max;
    } OpenLottoDrawSpec;

    /** @brief Validate draw specification fields and ranges. */
    int open_lotto_validate_spec(const OpenLottoDrawSpec *spec);

    /** @brief Generate one draw using runtime entropy. */
    int open_lotto_generate(const OpenLottoDrawSpec *spec, LotteryResult *out);

    /** @brief Generate one deterministic draw using explicit seed. */
    int open_lotto_generate_seeded(const OpenLottoDrawSpec *spec, uint64_t seed,
                                   LotteryResult *out);

    /** @brief Derive deterministic per-draw seeds from base seed and index. */
    uint64_t open_lotto_derive_seed(uint64_t base_seed, uint64_t draw_index);

    /** @brief Return semantic version string of the embedded API/runtime. */
    const char *open_lotto_version(void);

#ifdef __cplusplus
}
#endif

#endif
