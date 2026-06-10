/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "simulation_analytics_core.h"
#include <string.h>

int simulation_analytics_core_aggregate(const LotteryResult *draws, int draw_count, int number_min,
                                        int number_max, int picks_per_draw,
                                        SimulationAnalyticsCoreReport *out_report)
{
    int population;
    int i;
    int n;
    int total = 0;
    double mean = 0.0;
    double var_acc = 0.0;

    if (!draws || !out_report || draw_count <= 0 || number_min < 0 || number_max >= 128 ||
        number_min > number_max || picks_per_draw <= 0)
    {
        return -1;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->draw_count = draw_count;
    out_report->number_min = number_min;
    out_report->number_max = number_max;
    out_report->picks_per_draw = picks_per_draw;

    for (i = 0; i < draw_count; i++)
    {
        for (n = 0; n < draws[i].main_count; n++)
        {
            int value = draws[i].main_numbers[n];
            if (value >= number_min && value <= number_max)
            {
                out_report->counts[value]++;
                total++;
            }
        }
    }

    out_report->total_hits = total;

    population = number_max - number_min + 1;
    mean = (double)total / (double)population;
    out_report->mean_hits_per_number = mean;

    for (n = number_min; n <= number_max; n++)
    {
        double d = (double)out_report->counts[n] - mean;
        var_acc += d * d;
    }

    out_report->variance_hits_per_number = var_acc / (double)population;

    return 0;
}
