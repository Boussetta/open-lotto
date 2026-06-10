/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/simulation_analytics_core.h"
#include "test.h"

static LotteryResult make_draw(int a, int b, int c)
{
    LotteryResult r;
    r.main_numbers[0] = a;
    r.main_numbers[1] = b;
    r.main_numbers[2] = c;
    r.main_count = 3;
    r.extra_count = 0;
    return r;
}

static void test_core_aggregation(void)
{
    test_suite("Simulation Analytics Core Aggregation");

    LotteryResult draws[4];
    SimulationAnalyticsCoreReport report;

    draws[0] = make_draw(1, 2, 3);
    draws[1] = make_draw(1, 2, 4);
    draws[2] = make_draw(1, 3, 4);
    draws[3] = make_draw(2, 3, 4);

    assert_equals(simulation_analytics_core_aggregate(draws, 4, 1, 4, 3, &report), 0,
                  "aggregate returns 0");

    assert_equals(report.draw_count, 4, "draw_count stored");
    assert_equals(report.total_hits, 12, "total_hits equals draws * picks");
    assert_equals(report.counts[1], 3, "count number 1");
    assert_equals(report.counts[2], 3, "count number 2");
    assert_equals(report.counts[3], 3, "count number 3");
    assert_equals(report.counts[4], 3, "count number 4");
    assert_true(report.mean_hits_per_number > 2.99 && report.mean_hits_per_number < 3.01,
                "mean around expected value");
    assert_true(report.variance_hits_per_number >= 0.0, "variance is non-negative");
}

static void test_core_invalid_inputs(void)
{
    LotteryResult draws[1];
    SimulationAnalyticsCoreReport report;

    draws[0] = make_draw(1, 2, 3);

    assert_equals(simulation_analytics_core_aggregate(NULL, 1, 1, 49, 6, &report), -1,
                  "reject NULL draws");
    assert_equals(simulation_analytics_core_aggregate(draws, 0, 1, 49, 6, &report), -1,
                  "reject non-positive draw_count");
    assert_equals(simulation_analytics_core_aggregate(draws, 1, 60, 49, 6, &report), -1,
                  "reject invalid range");
    assert_equals(simulation_analytics_core_aggregate(draws, 1, 1, 49, 0, &report), -1,
                  "reject non-positive picks_per_draw");
}

int main(void)
{
    test_core_aggregation();
    test_core_invalid_inputs();
    test_summary();
}
