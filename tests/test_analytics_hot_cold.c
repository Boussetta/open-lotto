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

static void test_hot_cold_pipeline(void)
{
    test_suite("Analytics hot/cold pipeline");

    HistoricalDraw draws[32];
    HistoricalDraw filtered[32];
    int draw_count = 0;
    int filtered_count = 0;
    HotColdReport report;

    LotteryInfo rules = lotto_rules();
    int rc = analytics_load_historical_csv("tests/fixtures/historical_lotto_small.csv", draws, 32,
                                           &draw_count, &rules);
    assert_equals(rc, VALIDATE_OK, "CSV loads for hot/cold");

    rc = analytics_filter_period(draws, draw_count, "2025-01-01", "2025-02-28", filtered,
                                 &filtered_count);
    assert_equals(rc, VALIDATE_OK, "period filtering succeeds");

    rc = analytics_compute_hot_cold(filtered, filtered_count, 1, 49, 5, &report);
    assert_equals(rc, VALIDATE_OK, "hot/cold computation succeeds");
    assert_equals(report.top_n, 5, "top N is respected");

    assert_equals(report.hot[0].number, 1, "number 1 is hottest in fixture");
    assert_true(report.hot[0].count >= report.hot[1].count, "hot ranking is sorted descending");
    assert_true(report.cold[0].count <= report.cold[1].count, "cold ranking is sorted ascending");
}

int main(void)
{
    test_hot_cold_pipeline();
    test_summary();
}
