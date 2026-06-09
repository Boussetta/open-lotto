/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef HISTORICAL_DB_H
#define HISTORICAL_DB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HISTORICAL_DB_MAX_MAIN_NUMBERS 7
#define HISTORICAL_DB_MAX_EXTRA_NUMBERS 3
#define HISTORICAL_DB_MAX_WINNING_CLASSES 16

#define HISTORICAL_DB_SYNC_UPDATED 0
#define HISTORICAL_DB_SYNC_UNCHANGED 1

#define HISTORICAL_DB_ERR_INVALID_ARG -1
#define HISTORICAL_DB_ERR_UNSUPPORTED_GAME -2
#define HISTORICAL_DB_ERR_NETWORK -3
#define HISTORICAL_DB_ERR_PARSE -4
#define HISTORICAL_DB_ERR_IO -5

    typedef struct
    {
        char class_id[32];
        char description[64];
        int winners;
        double payout;
    } HistoricalWinningClass;

    typedef struct
    {
        char game[64];
        char draw_date[16];
        char next_draw_date[16];
        char source_url[256];
        int main_count;
        int main_numbers[HISTORICAL_DB_MAX_MAIN_NUMBERS];
        int extra_count;
        int extra_numbers[HISTORICAL_DB_MAX_EXTRA_NUMBERS];
        int winning_class_count;
        HistoricalWinningClass winning_classes[HISTORICAL_DB_MAX_WINNING_CLASSES];
    } HistoricalDrawSnapshot;

    int historical_db_sync_latest(const char *game_name, const char *db_root,
                                  HistoricalDrawSnapshot *out_snapshot);

    int historical_db_load_latest(const char *game_name, const char *db_root,
                                  HistoricalDrawSnapshot *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif
