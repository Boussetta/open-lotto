/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef SEED_CALIBRATION_H
#define SEED_CALIBRATION_H

#include "analytics.h"
#include "combogen.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SEED_CALIBRATION_MAX_TOP_K 64

    typedef enum
    {
        SEED_CALIBRATION_OK = 0,
        SEED_CALIBRATION_ERR_INVALID_ARGUMENT = 1,
        SEED_CALIBRATION_ERR_EMPTY_PERIOD = 2,
        SEED_CALIBRATION_ERR_CALLBACK_FAILED = 3
    } SeedCalibrationStatus;

    typedef struct
    {
        uint64_t seed;
        double total_score;
        double frequency_score;
        double gap_score;
        double rank_score;
    } SeedCalibrationCandidate;

    typedef int (*SeedCalibrationDrawFn)(void *ctx, uint64_t seed, int draw_index,
                                         LotteryResult *out_result);

    typedef struct
    {
        const HistoricalDraw *historical_draws;
        int historical_draw_count;
        int number_min;
        int number_max;
        int expected_main_count;
        const uint64_t *seed_list;
        int seed_list_count;
        uint64_t seed_start;
        uint64_t seed_end;
        int max_evals;
        int threads;
        int top_k;
        double weight_frequency;
        double weight_gap;
        double weight_rank;
        SeedCalibrationDrawFn draw_for_seed;
        void *draw_ctx;
    } SeedCalibrationRequest;

    typedef struct
    {
        SeedCalibrationStatus status;
        int evaluated_seeds;
        SeedCalibrationCandidate best;
        SeedCalibrationCandidate top_candidates[SEED_CALIBRATION_MAX_TOP_K];
        int top_candidate_count;
        double score_gap;
        double elapsed_ms;
        double seeds_per_second;
    } SeedCalibrationResult;

    int seed_calibration_find_closest(const SeedCalibrationRequest *req,
                                      SeedCalibrationResult *out);

#ifdef __cplusplus
}
#endif

#endif /* SEED_CALIBRATION_H */
