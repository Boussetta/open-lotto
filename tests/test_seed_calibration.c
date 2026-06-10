/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "seed_calibration.h"
#include "test.h"
#include <string.h>

static void fill_draw_from_seed(uint64_t seed, int draw_index, LotteryResult *out)
{
    memset(out, 0, sizeof(*out));
    out->main_count = 3;
    out->extra_count = 0;

    int base = (int)((seed + (uint64_t)(draw_index * 17)) % 10ULL) + 1;
    int a = base;
    int b = ((base + 3) % 10) + 1;
    int c = ((base + 6) % 10) + 1;

    out->main_numbers[0] = a;
    out->main_numbers[1] = b;
    out->main_numbers[2] = c;
}

static int draw_callback(void *ctx, uint64_t seed, int draw_index, LotteryResult *out_result)
{
    (void)ctx;
    fill_draw_from_seed(seed, draw_index, out_result);
    return 0;
}

static int draw_callback_ignore_seed(void *ctx, uint64_t seed, int draw_index,
                                     LotteryResult *out_result)
{
    (void)ctx;
    (void)seed;
    fill_draw_from_seed(0, draw_index, out_result);
    return 0;
}

static void build_historical(HistoricalDraw *draws, int draw_count, uint64_t seed)
{
    for (int i = 0; i < draw_count; i++)
    {
        LotteryResult result;
        fill_draw_from_seed(seed, i, &result);

        int day = (i % 28) + 1;
        snprintf(draws[i].draw_date, sizeof(draws[i].draw_date), "2026-01-%02d", day);
        draws[i].result = result;
    }
}

static void test_invalid_arguments(void)
{
    SeedCalibrationResult out;
    int rc = seed_calibration_find_closest(NULL, &out);
    assert_equals(rc, SEED_CALIBRATION_ERR_INVALID_ARGUMENT,
                  "Rejects NULL request in seed calibration");
}

static void test_exact_seed_is_best_fit(void)
{
    HistoricalDraw historical[8];
    build_historical(historical, 8, 42);

    SeedCalibrationRequest req;
    memset(&req, 0, sizeof(req));
    req.historical_draws = historical;
    req.historical_draw_count = 8;
    req.number_min = 1;
    req.number_max = 10;
    req.expected_main_count = 3;
    req.seed_start = 40;
    req.seed_end = 44;
    req.max_evals = 0;
    req.top_k = 3;
    req.weight_frequency = 1.0;
    req.weight_gap = 1.0;
    req.weight_rank = 1.0;
    req.draw_for_seed = draw_callback;

    SeedCalibrationResult out;
    int rc = seed_calibration_find_closest(&req, &out);

    assert_equals(rc, SEED_CALIBRATION_OK, "Closest-seed request succeeds");
    assert_equals((int)out.best.seed, 42, "Exact historical seed is selected as best candidate");
    assert_true(out.best.total_score <= out.top_candidates[1].total_score,
                "Best candidate score is not greater than runner-up");
}

static void test_tie_break_prefers_lower_seed(void)
{
    HistoricalDraw historical[6];
    build_historical(historical, 6, 0);

    SeedCalibrationRequest req;
    memset(&req, 0, sizeof(req));
    req.historical_draws = historical;
    req.historical_draw_count = 6;
    req.number_min = 1;
    req.number_max = 10;
    req.expected_main_count = 3;
    req.seed_start = 100;
    req.seed_end = 103;
    req.top_k = 4;
    req.weight_frequency = 1.0;
    req.weight_gap = 1.0;
    req.weight_rank = 1.0;
    req.draw_for_seed = draw_callback_ignore_seed;

    SeedCalibrationResult out;
    int rc = seed_calibration_find_closest(&req, &out);

    assert_equals(rc, SEED_CALIBRATION_OK, "Tie-break scenario request succeeds");
    assert_equals((int)out.best.seed, 100, "Tie-break prefers lower seed value");
}

static void test_max_evals_caps_search(void)
{
    HistoricalDraw historical[5];
    build_historical(historical, 5, 7);

    SeedCalibrationRequest req;
    memset(&req, 0, sizeof(req));
    req.historical_draws = historical;
    req.historical_draw_count = 5;
    req.number_min = 1;
    req.number_max = 10;
    req.expected_main_count = 3;
    req.seed_start = 1;
    req.seed_end = 100;
    req.max_evals = 3;
    req.top_k = 2;
    req.weight_frequency = 1.0;
    req.weight_gap = 1.0;
    req.weight_rank = 1.0;
    req.draw_for_seed = draw_callback;

    SeedCalibrationResult out;
    int rc = seed_calibration_find_closest(&req, &out);

    assert_equals(rc, SEED_CALIBRATION_OK, "Search with max-evals succeeds");
    assert_equals(out.evaluated_seeds, 3, "Search stops after max-evals seeds");
}

int main(void)
{
    test_suite("Seed Calibration Unit Tests");

    test_invalid_arguments();
    test_exact_seed_is_best_fit();
    test_tie_break_prefers_lower_seed();
    test_max_evals_caps_search();

    test_summary();
}
