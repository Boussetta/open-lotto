/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "simulation_analytics_advanced.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int hot_cmp(const void *a, const void *b)
{
    const SimulationHotColdEntry *x = (const SimulationHotColdEntry *)a;
    const SimulationHotColdEntry *y = (const SimulationHotColdEntry *)b;
    if (x->count != y->count)
        return y->count - x->count;
    return x->number - y->number;
}

static int cold_cmp(const void *a, const void *b)
{
    const SimulationHotColdEntry *x = (const SimulationHotColdEntry *)a;
    const SimulationHotColdEntry *y = (const SimulationHotColdEntry *)b;
    if (x->count != y->count)
        return x->count - y->count;
    return x->number - y->number;
}

int simulation_analytics_advanced_compute(const LotteryResult *draws, int draw_count,
                                          int number_min, int number_max, int top_n,
                                          SimulationAnalyticsAdvancedReport *out_report)
{
    int i;
    int n;
    int population;
    int counts[128] = {0};
    int streak[128] = {0};
    int seen_in_draw[128] = {0};
    int total_hits = 0;
    double entropy = 0.0;
    SimulationHotColdEntry all[128] = {{0}};
    int all_count = 0;

    if (!draws || !out_report || draw_count <= 0 || number_min < 0 || number_max >= 128 ||
        number_min > number_max || top_n <= 0)
    {
        return -1;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->draw_count = draw_count;
    out_report->number_min = number_min;
    out_report->number_max = number_max;

    population = number_max - number_min + 1;

    for (i = 0; i < draw_count; i++)
    {
        memset(seen_in_draw, 0, sizeof(seen_in_draw));

        for (n = 0; n < draws[i].main_count; n++)
        {
            int value = draws[i].main_numbers[n];
            if (value >= number_min && value <= number_max)
            {
                seen_in_draw[value] = 1;
            }
        }

        for (n = number_min; n <= number_max; n++)
        {
            if (seen_in_draw[n])
            {
                counts[n]++;
                streak[n]++;
                if (streak[n] > out_report->longest_streak[n])
                {
                    out_report->longest_streak[n] = streak[n];
                }
                out_report->current_gap[n] = 0;
            }
            else
            {
                streak[n] = 0;
                out_report->current_gap[n]++;
                if (out_report->current_gap[n] > out_report->max_gap[n])
                {
                    out_report->max_gap[n] = out_report->current_gap[n];
                }
            }

            out_report->current_streak[n] = streak[n];
        }
    }

    for (n = number_min; n <= number_max; n++)
    {
        total_hits += counts[n];
    }

    if (total_hits > 0)
    {
        for (n = number_min; n <= number_max; n++)
        {
            double p = (double)counts[n] / (double)total_hits;
            if (p > 0.0)
            {
                entropy += -p * (log(p) / log(2.0));
            }
        }
    }

    out_report->entropy_normalized =
        (population > 1) ? entropy / (log((double)population) / log(2.0)) : 0.0;

    for (n = number_min; n <= number_max; n++)
    {
        all[all_count].number = n;
        all[all_count].count = counts[n];
        all[all_count].percentage =
            (draw_count > 0) ? (100.0 * counts[n] / (double)draw_count) : 0.0;
        all_count++;
    }

    out_report->top_n = (top_n < all_count) ? top_n : all_count;

    qsort(all, (size_t)all_count, sizeof(all[0]), hot_cmp);
    for (i = 0; i < out_report->top_n; i++)
    {
        out_report->hot[i] = all[i];
    }

    qsort(all, (size_t)all_count, sizeof(all[0]), cold_cmp);
    for (i = 0; i < out_report->top_n; i++)
    {
        out_report->cold[i] = all[i];
    }

    return 0;
}
