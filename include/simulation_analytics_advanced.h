/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef SIMULATION_ANALYTICS_ADVANCED_H
#define SIMULATION_ANALYTICS_ADVANCED_H

#include "combogen.h"

/**
 * @file simulation_analytics_advanced.h
 * @brief Advanced streak/gap/entropy metrics for simulated draws.
 */

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct SimulationHotColdEntry
    {
        int number;
        int count;
        double percentage;
    } SimulationHotColdEntry;

    typedef struct SimulationAnalyticsAdvancedReport
    {
        int draw_count;
        int number_min;
        int number_max;
        int top_n;
        double entropy_normalized;
        int current_gap[128];
        int max_gap[128];
        int longest_streak[128];
        int current_streak[128];
        SimulationHotColdEntry hot[128];
        SimulationHotColdEntry cold[128];
    } SimulationAnalyticsAdvancedReport;

    /**
     * @brief Compute entropy, gaps, streaks, and hot/cold rankings.
     */
    int simulation_analytics_advanced_compute(const LotteryResult *draws, int draw_count,
                                              int number_min, int number_max, int top_n,
                                              SimulationAnalyticsAdvancedReport *out_report);

#ifdef __cplusplus
}
#endif

#endif
