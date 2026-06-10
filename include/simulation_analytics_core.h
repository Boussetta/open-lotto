/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef SIMULATION_ANALYTICS_CORE_H
#define SIMULATION_ANALYTICS_CORE_H

#include "combogen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SimulationAnalyticsCoreReport
{
    int draw_count;
    int number_min;
    int number_max;
    int picks_per_draw;
    int total_hits;
    double mean_hits_per_number;
    double variance_hits_per_number;
    int counts[128];
} SimulationAnalyticsCoreReport;

int simulation_analytics_core_aggregate(const LotteryResult *draws,
                                        int draw_count,
                                        int number_min,
                                        int number_max,
                                        int picks_per_draw,
                                        SimulationAnalyticsCoreReport *out_report);

#ifdef __cplusplus
}
#endif

#endif
