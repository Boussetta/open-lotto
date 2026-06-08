/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef OPEN_LOTTO_API_H
#define OPEN_LOTTO_API_H

#include "combogen.h"
#include "historical_db.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OPEN_LOTTO_API_SUCCESS 0
#define OPEN_LOTTO_API_SYNC_UNCHANGED 10
#define OPEN_LOTTO_API_ERR_INVALID_ARG 1
#define OPEN_LOTTO_API_ERR_INVALID_SPEC 2
#define OPEN_LOTTO_API_ERR_UNSUPPORTED_GAME 3
#define OPEN_LOTTO_API_ERR_NETWORK 4
#define OPEN_LOTTO_API_ERR_PARSE 5
#define OPEN_LOTTO_API_ERR_IO 6

    typedef struct
    {
        int main_count;
        int main_min;
        int main_max;
        int extra_count;
        int extra_min;
        int extra_max;
    } OpenLottoDrawSpec;

    int open_lotto_validate_spec(const OpenLottoDrawSpec *spec);

    int open_lotto_generate(const OpenLottoDrawSpec *spec, LotteryResult *out);

    int open_lotto_generate_seeded(const OpenLottoDrawSpec *spec, uint64_t seed,
                                   LotteryResult *out);

    uint64_t open_lotto_derive_seed(uint64_t base_seed, uint64_t draw_index);

    int open_lotto_sync_historical_latest(const char *game_name, const char *db_root,
                                          HistoricalDrawSnapshot *out_snapshot);

    int open_lotto_load_historical_latest(const char *game_name, const char *db_root,
                                          HistoricalDrawSnapshot *out_snapshot);

    const char *open_lotto_version(void);

#ifdef __cplusplus
}
#endif

#endif
