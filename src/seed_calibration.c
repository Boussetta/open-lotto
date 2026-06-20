/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file seed_calibration.c
 * @brief Parallel search for seeds whose simulated statistics best match history.
 */

#include "seed_calibration.h"
#include <limits.h>
#include <math.h>
#include <omp.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SEED_CALIBRATION_MAX_NUMBER 127

typedef struct
{
    int count[SEED_CALIBRATION_MAX_NUMBER + 1];
    double mean_gap[SEED_CALIBRATION_MAX_NUMBER + 1];
    int rank[SEED_CALIBRATION_MAX_NUMBER + 1];
} SeedCalibrationStats;

typedef struct
{
    int number;
    int count;
} NumberCount;

/**
 * @brief Sort helper for descending hit count, then ascending number.
 */
static int compare_number_count(const void *a, const void *b)
{
    const NumberCount *lhs = (const NumberCount *)a;
    const NumberCount *rhs = (const NumberCount *)b;

    if (lhs->count != rhs->count)
        return rhs->count - lhs->count;
    return lhs->number - rhs->number;
}

/**
 * @brief Total ordering for candidate ranking.
 *
 * Lower total score wins; ties break on frequency score then seed value.
 */
static int candidate_better(const SeedCalibrationCandidate *lhs,
                            const SeedCalibrationCandidate *rhs)
{
    if (lhs->total_score < rhs->total_score)
        return 1;
    if (lhs->total_score > rhs->total_score)
        return 0;

    if (lhs->frequency_score < rhs->frequency_score)
        return 1;
    if (lhs->frequency_score > rhs->frequency_score)
        return 0;

    return lhs->seed < rhs->seed;
}

/**
 * @brief Keep a small candidate buffer sorted by quality.
 */
static void sort_candidates(SeedCalibrationCandidate *arr, int count)
{
    for (int i = 0; i < count; i++)
    {
        int best_idx = i;
        for (int j = i + 1; j < count; j++)
        {
            if (candidate_better(&arr[j], &arr[best_idx]))
                best_idx = j;
        }
        if (best_idx != i)
        {
            SeedCalibrationCandidate tmp = arr[i];
            arr[i] = arr[best_idx];
            arr[best_idx] = tmp;
        }
    }
}

/**
 * @brief Validate calibration request structure before launching work.
 */
static int validate_request(const SeedCalibrationRequest *req)
{
    if (!req || !req->historical_draws || !req->draw_for_seed)
        return 0;
    if (req->historical_draw_count <= 0)
        return 0;
    if (req->number_min < 0 || req->number_max > SEED_CALIBRATION_MAX_NUMBER)
        return 0;
    if (req->number_min > req->number_max)
        return 0;
    if (req->expected_main_count <= 0 || req->expected_main_count > MAX_MAIN_NUMBERS)
        return 0;
    if (req->seed_list_count < 0)
        return 0;
    if (req->seed_list_count > 0 && !req->seed_list)
        return 0;
    if (req->seed_list_count == 0 && req->seed_start > req->seed_end)
        return 0;
    if (req->threads <= 0)
        return 0;
    if (req->top_k <= 0 || req->top_k > SEED_CALIBRATION_MAX_TOP_K)
        return 0;
    if (req->weight_frequency < 0.0 || req->weight_gap < 0.0 || req->weight_rank < 0.0)
        return 0;

    return 1;
}

/**
 * @brief Build reference frequency/gap/rank statistics from historical draws.
 */
static int build_stats_from_history(const SeedCalibrationRequest *req, SeedCalibrationStats *stats)
{
    memset(stats, 0, sizeof(*stats));

    int min = req->number_min;
    int max = req->number_max;
    int draw_count = req->historical_draw_count;
    double gap_sum[SEED_CALIBRATION_MAX_NUMBER + 1] = {0.0};
    int gap_count[SEED_CALIBRATION_MAX_NUMBER + 1] = {0};
    int last_hit[SEED_CALIBRATION_MAX_NUMBER + 1];
    for (int n = min; n <= max; n++)
        last_hit[n] = -1;

    for (int i = 0; i < draw_count; i++)
    {
        const LotteryResult *draw = &req->historical_draws[i].result;
        if (draw->main_count != req->expected_main_count)
            return 0;

        for (int j = 0; j < draw->main_count; j++)
        {
            int number = draw->main_numbers[j];
            if (number < min || number > max)
                return 0;

            stats->count[number]++;
            if (last_hit[number] >= 0)
            {
                gap_sum[number] += (double)(i - last_hit[number]);
                gap_count[number]++;
            }
            last_hit[number] = i;
        }
    }

    NumberCount sorted[SEED_CALIBRATION_MAX_NUMBER + 1];
    int sorted_count = 0;
    for (int number = min; number <= max; number++)
    {
        if (gap_count[number] > 0)
            stats->mean_gap[number] = gap_sum[number] / (double)gap_count[number];
        else
            stats->mean_gap[number] = (double)draw_count + 1.0;

        sorted[sorted_count].number = number;
        sorted[sorted_count].count = stats->count[number];
        sorted_count++;
    }

    qsort(sorted, (size_t)sorted_count, sizeof(sorted[0]), compare_number_count);
    for (int i = 0; i < sorted_count; i++)
        stats->rank[sorted[i].number] = i + 1;

    return 1;
}

/**
 * @brief Build comparable statistics from simulated draws for one candidate seed.
 */
static int build_stats_from_seed(const SeedCalibrationRequest *req, uint64_t seed,
                                 SeedCalibrationStats *stats)
{
    memset(stats, 0, sizeof(*stats));

    int min = req->number_min;
    int max = req->number_max;
    int draw_count = req->historical_draw_count;
    double gap_sum[SEED_CALIBRATION_MAX_NUMBER + 1] = {0.0};
    int gap_count[SEED_CALIBRATION_MAX_NUMBER + 1] = {0};
    int last_hit[SEED_CALIBRATION_MAX_NUMBER + 1];
    for (int n = min; n <= max; n++)
        last_hit[n] = -1;

    for (int i = 0; i < draw_count; i++)
    {
        LotteryResult draw;
        memset(&draw, 0, sizeof(draw));

        if (req->draw_for_seed(req->draw_ctx, seed, i, &draw) != 0)
            return 0;
        if (draw.main_count != req->expected_main_count)
            return 0;

        for (int j = 0; j < draw.main_count; j++)
        {
            int number = draw.main_numbers[j];
            if (number < min || number > max)
                return 0;

            stats->count[number]++;
            if (last_hit[number] >= 0)
            {
                gap_sum[number] += (double)(i - last_hit[number]);
                gap_count[number]++;
            }
            last_hit[number] = i;
        }
    }

    NumberCount sorted[SEED_CALIBRATION_MAX_NUMBER + 1];
    int sorted_count = 0;
    for (int number = min; number <= max; number++)
    {
        if (gap_count[number] > 0)
            stats->mean_gap[number] = gap_sum[number] / (double)gap_count[number];
        else
            stats->mean_gap[number] = (double)draw_count + 1.0;

        sorted[sorted_count].number = number;
        sorted[sorted_count].count = stats->count[number];
        sorted_count++;
    }

    qsort(sorted, (size_t)sorted_count, sizeof(sorted[0]), compare_number_count);
    for (int i = 0; i < sorted_count; i++)
        stats->rank[sorted[i].number] = i + 1;

    return 1;
}

/**
 * @brief Score one candidate seed against historical reference stats.
 *
 * Each component is normalized to [0, 1]-like scales and combined using the
 * user-provided weights.
 */
static SeedCalibrationCandidate score_seed(const SeedCalibrationRequest *req,
                                           const SeedCalibrationStats *historical,
                                           const SeedCalibrationStats *simulated, uint64_t seed)
{
    SeedCalibrationCandidate candidate;
    candidate.seed = seed;
    candidate.total_score = 0.0;
    candidate.frequency_score = 0.0;
    candidate.gap_score = 0.0;
    candidate.rank_score = 0.0;

    const int range = req->number_max - req->number_min + 1;
    const double normalizer_frequency =
        (double)req->historical_draw_count * (double)req->expected_main_count;
    const double normalizer_gap = (double)req->historical_draw_count + 1.0;
    const double normalizer_rank = (range > 1) ? (double)(range - 1) : 1.0;

    for (int number = req->number_min; number <= req->number_max; number++)
    {
        double f_hist = (double)historical->count[number] / normalizer_frequency;
        double f_sim = (double)simulated->count[number] / normalizer_frequency;
        candidate.frequency_score += fabs(f_hist - f_sim);

        double g_hist = historical->mean_gap[number] / normalizer_gap;
        double g_sim = simulated->mean_gap[number] / normalizer_gap;
        candidate.gap_score += fabs(g_hist - g_sim);

        double r_hist = (double)historical->rank[number] / normalizer_rank;
        double r_sim = (double)simulated->rank[number] / normalizer_rank;
        candidate.rank_score += fabs(r_hist - r_sim);
    }

    candidate.frequency_score /= (double)range;
    candidate.gap_score /= (double)range;
    candidate.rank_score /= (double)range;

    candidate.total_score = req->weight_frequency * candidate.frequency_score +
                            req->weight_gap * candidate.gap_score +
                            req->weight_rank * candidate.rank_score;

    return candidate;
}

/**
 * @brief Evaluate candidate seeds in parallel and keep the top-K matches.
 */
int seed_calibration_find_closest(const SeedCalibrationRequest *req, SeedCalibrationResult *out)
{
    if (!out)
        return SEED_CALIBRATION_ERR_INVALID_ARGUMENT;

    memset(out, 0, sizeof(*out));

    if (!validate_request(req))
    {
        out->status = SEED_CALIBRATION_ERR_INVALID_ARGUMENT;
        return out->status;
    }

    SeedCalibrationStats historical_stats;
    if (!build_stats_from_history(req, &historical_stats))
    {
        out->status = SEED_CALIBRATION_ERR_EMPTY_PERIOD;
        return out->status;
    }

    uint64_t eval_target_u64 = 0;
    if (req->seed_list_count > 0)
    {
        eval_target_u64 = (uint64_t)req->seed_list_count;
    }
    else
    {
        uint64_t domain_size_u64 = req->seed_end - req->seed_start + 1ULL;
        eval_target_u64 = domain_size_u64;
        if (req->max_evals > 0 && (uint64_t)req->max_evals < eval_target_u64)
            eval_target_u64 = (uint64_t)req->max_evals;
    }

    if (eval_target_u64 == 0)
    {
        out->status = SEED_CALIBRATION_ERR_EMPTY_PERIOD;
        return out->status;
    }

    int eval_target = (eval_target_u64 > (uint64_t)INT_MAX) ? INT_MAX : (int)eval_target_u64;
    int thread_count = req->threads;
    if (thread_count > eval_target)
        thread_count = eval_target;

    SeedCalibrationCandidate *thread_candidates = (SeedCalibrationCandidate *)calloc(
        (size_t)thread_count * (size_t)req->top_k, sizeof(SeedCalibrationCandidate));
    int *thread_kept = (int *)calloc((size_t)thread_count, sizeof(int));
    int *thread_done = (int *)calloc((size_t)thread_count, sizeof(int));
    int *thread_utilization_pct = (int *)calloc((size_t)thread_count, sizeof(int));
    double *thread_started_at = (double *)calloc((size_t)thread_count, sizeof(double));
    double *thread_finished_at = (double *)calloc((size_t)thread_count, sizeof(double));
    if (!thread_candidates || !thread_kept || !thread_done || !thread_utilization_pct ||
        !thread_started_at || !thread_finished_at)
    {
        free(thread_candidates);
        free(thread_kept);
        free(thread_done);
        free(thread_utilization_pct);
        free(thread_started_at);
        free(thread_finished_at);
        out->status = SEED_CALIBRATION_ERR_INVALID_ARGUMENT;
        return out->status;
    }

    if (req->progress_fn)
        req->progress_fn(req->progress_ctx, 0, eval_target, thread_count, thread_done,
                         thread_utilization_pct);

    int callback_failed = 0;
    double t0 = omp_get_wtime();

#pragma omp parallel num_threads(thread_count)
    {
        int tid = omp_get_thread_num();
        SeedCalibrationCandidate *local = &thread_candidates[(size_t)tid * (size_t)req->top_k];
        int local_kept = 0;
        int local_done = 0;
        double thread_start = omp_get_wtime();
        thread_started_at[tid] = thread_start;

#pragma omp for schedule(static)
        for (int i = 0; i < eval_target; i++)
        {
            if (callback_failed)
                continue;

            uint64_t seed =
                (req->seed_list_count > 0) ? req->seed_list[i] : (req->seed_start + (uint64_t)i);
            SeedCalibrationStats simulated_stats;
            if (!build_stats_from_seed(req, seed, &simulated_stats))
            {
#pragma omp atomic write
                callback_failed = 1;
                continue;
            }

            SeedCalibrationCandidate candidate =
                score_seed(req, &historical_stats, &simulated_stats, seed);

            if (local_kept < req->top_k)
            {
                local[local_kept++] = candidate;
                sort_candidates(local, local_kept);
            }
            else if (candidate_better(&candidate, &local[local_kept - 1]))
            {
                local[local_kept - 1] = candidate;
                sort_candidates(local, local_kept);
            }

            local_done++;
            if (req->progress_fn && ((local_done % 64) == 0))
            {
#pragma omp critical(seed_calibration_progress)
                {
                    thread_done[tid] = local_done;

                    double now = omp_get_wtime();
                    double elapsed = now - t0;
                    if (elapsed <= 0.0)
                        elapsed = 1e-9;

                    for (int t = 0; t < thread_count; t++)
                    {
                        if (thread_started_at[t] <= 0.0)
                        {
                            thread_utilization_pct[t] = 0;
                            continue;
                        }

                        double active_until =
                            (thread_finished_at[t] > 0.0) ? thread_finished_at[t] : now;
                        double active_elapsed = active_until - thread_started_at[t];
                        if (active_elapsed < 0.0)
                            active_elapsed = 0.0;

                        int util_pct = (int)((active_elapsed / elapsed) * 100.0);
                        if (util_pct > 100)
                            util_pct = 100;
                        thread_utilization_pct[t] = util_pct;
                    }

                    int done = 0;
                    for (int t = 0; t < thread_count; t++)
                        done += thread_done[t];
                    if (done > eval_target)
                        done = eval_target;

                    req->progress_fn(req->progress_ctx, done, eval_target, thread_count,
                                     thread_done, thread_utilization_pct);
                }
            }
        }

#pragma omp critical(seed_calibration_progress)
        {
            thread_done[tid] = local_done;
            thread_finished_at[tid] = omp_get_wtime();

            if (req->progress_fn)
            {
                double now = omp_get_wtime();
                double elapsed = now - t0;
                if (elapsed <= 0.0)
                    elapsed = 1e-9;

                for (int t = 0; t < thread_count; t++)
                {
                    if (thread_started_at[t] <= 0.0)
                    {
                        thread_utilization_pct[t] = 0;
                        continue;
                    }

                    double active_until =
                        (thread_finished_at[t] > 0.0) ? thread_finished_at[t] : now;
                    double active_elapsed = active_until - thread_started_at[t];
                    if (active_elapsed < 0.0)
                        active_elapsed = 0.0;

                    int util_pct = (int)((active_elapsed / elapsed) * 100.0);
                    if (util_pct > 100)
                        util_pct = 100;
                    thread_utilization_pct[t] = util_pct;
                }

                int done = 0;
                for (int t = 0; t < thread_count; t++)
                    done += thread_done[t];
                if (done > eval_target)
                    done = eval_target;

                req->progress_fn(req->progress_ctx, done, eval_target, thread_count, thread_done,
                                 thread_utilization_pct);
            }
        }

        thread_kept[tid] = local_kept;
    }

    double t1 = omp_get_wtime();
    if (callback_failed)
    {
        free(thread_candidates);
        free(thread_kept);
        free(thread_done);
        free(thread_utilization_pct);
        free(thread_started_at);
        free(thread_finished_at);
        out->status = SEED_CALIBRATION_ERR_CALLBACK_FAILED;
        return out->status;
    }

    if (req->progress_fn)
        req->progress_fn(req->progress_ctx, eval_target, eval_target, thread_count, thread_done,
                         thread_utilization_pct);

    int kept = 0;
    for (int t = 0; t < thread_count; t++)
    {
        SeedCalibrationCandidate *local = &thread_candidates[(size_t)t * (size_t)req->top_k];
        for (int i = 0; i < thread_kept[t]; i++)
        {
            SeedCalibrationCandidate candidate = local[i];
            if (kept < req->top_k)
            {
                out->top_candidates[kept++] = candidate;
                sort_candidates(out->top_candidates, kept);
            }
            else if (candidate_better(&candidate, &out->top_candidates[kept - 1]))
            {
                out->top_candidates[kept - 1] = candidate;
                sort_candidates(out->top_candidates, kept);
            }
        }
    }

    free(thread_candidates);
    free(thread_kept);
    free(thread_done);
    free(thread_utilization_pct);
    free(thread_started_at);
    free(thread_finished_at);

    if (kept == 0)
    {
        out->status = SEED_CALIBRATION_ERR_EMPTY_PERIOD;
        return out->status;
    }

    out->top_candidate_count = kept;
    out->evaluated_seeds = eval_target;
    out->best = out->top_candidates[0];
    out->score_gap =
        (kept >= 2) ? (out->top_candidates[1].total_score - out->best.total_score) : 0.0;
    out->elapsed_ms = (t1 - t0) * 1000.0;
    out->seeds_per_second =
        (out->elapsed_ms > 0.0) ? ((double)out->evaluated_seeds / (out->elapsed_ms / 1000.0)) : 0.0;
    out->status = SEED_CALIBRATION_OK;
    return out->status;
}
