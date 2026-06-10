/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/combogen.h"
#include "../include/simulation_analytics_advanced.h"
#include "../include/simulation_analytics_core.h"
#include "test.h"

#define ITERATIONS 20
#define DRAWS_PER_ITERATION 200

static void no_cb(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

static void test_simulation_property_invariants(void)
{
    test_suite("Simulation Analytics Property Invariants");

    for (int it = 0; it < ITERATIONS; it++)
    {
        LotteryResult draws[DRAWS_PER_ITERATION];
        SimulationAnalyticsCoreReport core;
        SimulationAnalyticsAdvancedReport advanced;
        int total_counts = 0;

        for (int i = 0; i < DRAWS_PER_ITERATION; i++)
        {
            generate_draw_seeded(6, 1, 49, 0, 0, 0, (uint64_t)(0x1000 + it * 100 + i), &draws[i],
                                 no_cb);
        }

        assert_equals(
            simulation_analytics_core_aggregate(draws, DRAWS_PER_ITERATION, 1, 49, 6, &core), 0,
            "core aggregate succeeds");
        assert_equals(
            simulation_analytics_advanced_compute(draws, DRAWS_PER_ITERATION, 1, 49, 5, &advanced),
            0, "advanced compute succeeds");

        for (int n = 1; n <= 49; n++)
        {
            total_counts += core.counts[n];
            assert_true(core.counts[n] >= 0, "counts are non-negative");
            assert_true(advanced.max_gap[n] >= 0, "max gap non-negative");
            assert_true(advanced.longest_streak[n] >= 0, "streak non-negative");
        }

        assert_equals(total_counts, DRAWS_PER_ITERATION * 6, "sum(counts)=draws*picks");
        assert_true(advanced.entropy_normalized >= 0.0 && advanced.entropy_normalized <= 1.0,
                    "entropy_normalized in [0,1]");
    }
}

int main(void)
{
    test_simulation_property_invariants();
    test_summary();
}
