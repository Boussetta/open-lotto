/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/analytics.h"
#include "test.h"
#include <string.h>

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

static void test_frequency_pipeline(void)
{
    test_suite("Analytics frequency pipeline");

    HistoricalDraw draws[32];
    HistoricalDraw filtered[32];
    int draw_count = 0;
    int filtered_count = 0;
    FrequencyReport report;

    LotteryInfo rules = lotto_rules();
    int rc = analytics_load_historical_csv("tests/fixtures/historical_lotto_small.csv", draws, 32,
                                           &draw_count, &rules);
    assert_equals(rc, VALIDATE_OK, "CSV loads successfully");
    assert_equals(draw_count, 4, "all fixture draws are loaded");

    rc = analytics_filter_period(draws, draw_count, "2025-01-01", "2025-01-31", filtered,
                                 &filtered_count);
    assert_equals(rc, VALIDATE_OK, "period filtering succeeds");
    assert_equals(filtered_count, 3, "date filtering keeps 3 january draws");

    rc = analytics_compute_frequency(filtered, filtered_count, 1, 49, &report);
    assert_equals(rc, VALIDATE_OK, "frequency computation succeeds");

    assert_equals(report.total_draws, 3, "report stores filtered draw count");
    assert_equals(report.counts[1], 3, "number 1 appears in all january draws");
    assert_equals(report.counts[2], 2, "number 2 appears in two january draws");
    assert_equals(report.counts[11], 1, "number 11 appears once");
    assert_equals(report.counts[20], 0, "number 20 appears zero times");
}

int main(void)
{
    test_frequency_pipeline();
    test_summary();
}
