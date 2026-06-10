/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/simulation_analytics_advanced.h"
#include "test.h"

static LotteryResult make_draw3(int a, int b, int c)
{
    LotteryResult r;
    r.main_numbers[0] = a;
    r.main_numbers[1] = b;
    r.main_numbers[2] = c;
    r.main_count = 3;
    r.extra_count = 0;
    return r;
}

static void test_advanced_metrics(void)
{
    test_suite("Simulation Analytics Advanced Metrics");

    LotteryResult draws[5];
    SimulationAnalyticsAdvancedReport report;

    draws[0] = make_draw3(1, 2, 3);
    draws[1] = make_draw3(1, 2, 4);
    draws[2] = make_draw3(1, 3, 4);
    draws[3] = make_draw3(2, 3, 4);
    draws[4] = make_draw3(1, 2, 4);

    assert_equals(simulation_analytics_advanced_compute(draws, 5, 1, 4, 2, &report), 0,
                  "advanced compute returns 0");
    assert_equals(report.top_n, 2, "top_n applied");
    assert_true(report.entropy_normalized > 0.0 && report.entropy_normalized <= 1.0,
                "entropy_normalized in [0,1]");

    assert_true(report.hot[0].count >= report.hot[1].count, "hot list sorted desc");
    assert_true(report.cold[0].count <= report.cold[1].count, "cold list sorted asc");

    assert_true(report.longest_streak[1] >= 1, "longest streak tracked");
    assert_true(report.max_gap[3] >= 0, "max gap tracked");
}

static void test_advanced_invalid(void)
{
    LotteryResult draw = make_draw3(1, 2, 3);
    SimulationAnalyticsAdvancedReport report;

    assert_equals(simulation_analytics_advanced_compute(NULL, 1, 1, 49, 5, &report), -1,
                  "reject null draws");
    assert_equals(simulation_analytics_advanced_compute(&draw, 0, 1, 49, 5, &report), -1,
                  "reject zero draw_count");
    assert_equals(simulation_analytics_advanced_compute(&draw, 1, 60, 49, 5, &report), -1,
                  "reject invalid number range");
    assert_equals(simulation_analytics_advanced_compute(&draw, 1, 1, 49, 0, &report), -1,
                  "reject zero top_n");
}

int main(void)
{
    test_advanced_metrics();
    test_advanced_invalid();
    test_summary();
}
