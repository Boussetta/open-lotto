/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/analytics.h"
#include "test.h"

static LotteryInfo lotto_rules(void)
{
    LotteryInfo info;
    info.main_count = 6;
    info.main_min = 1;
    info.main_max = 49;
    info.extra_count = 1;
    info.extra_min = 0;
    info.extra_max = 9;
    return info;
}

static void test_barometer_pipeline(void)
{
    test_suite("Analytics barometer pipeline");

    HistoricalDraw draws[32];
    HistoricalDraw filtered[32];
    int draw_count = 0;
    int filtered_count = 0;
    BarometerReport report;

    LotteryInfo rules = lotto_rules();
    int rc = analytics_load_historical_csv("tests/fixtures/historical_lotto_small.csv", draws, 32,
                                           &draw_count, &rules);
    assert_equals(rc, VALIDATE_OK, "CSV loads for barometer");

    rc = analytics_filter_period(draws, draw_count, "2025-01-01", "2025-02-28", filtered,
                                 &filtered_count);
    assert_equals(rc, VALIDATE_OK, "period filtering succeeds");
    assert_equals(filtered_count, 4, "all fixture rows are in selected period");

    rc = analytics_compute_barometer(filtered, filtered_count, 1, 49, 6, &report);
    assert_equals(rc, VALIDATE_OK, "barometer computation succeeds");

    assert_true(report.expected_interval > 0.0, "expected interval is positive");
    assert_equals(report.hit_counts[1], 3, "number 1 hit count is tracked");
    assert_true(report.factors[1] >= 0.0, "factor is non-negative");

    /* Number 49 never appears in fixture, should be highly overdue. */
    assert_equals(report.hit_counts[49], 0, "never-hit number has zero hits");
    assert_true(report.factors[49] > 1.0, "never-hit number is overdue");
}

int main(void)
{
    test_barometer_pipeline();
    test_summary();
}
