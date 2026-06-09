/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef ANALYTICS_H
#define ANALYTICS_H

#include "combogen.h"
#include "validate.h"

#define ANALYTICS_MAX_DRAWS 100000

typedef struct
{
    char draw_date[11]; /* YYYY-MM-DD */
    LotteryResult result;
} HistoricalDraw;

typedef struct
{
    int total_draws;
    int number_min;
    int number_max;
    int counts[128];
} FrequencyReport;

int analytics_load_historical_csv(const char *csv_path, HistoricalDraw *out_draws, int max_draws,
                                  int *out_count, const LotteryInfo *rules);

int analytics_filter_period(const HistoricalDraw *draws, int draw_count, const char *from_date,
                            const char *to_date, HistoricalDraw *out_filtered,
                            int *out_filtered_count);

int analytics_compute_frequency(const HistoricalDraw *draws, int draw_count, int number_min,
                                int number_max, FrequencyReport *out_report);

void analytics_print_frequency_table(const FrequencyReport *report);
void analytics_print_frequency_csv(const FrequencyReport *report);
void analytics_print_frequency_json(const FrequencyReport *report);
void analytics_print_frequency_gui_2d(const FrequencyReport *report);
void analytics_print_frequency_gui_3d_matlab(const FrequencyReport *report);

#endif /* ANALYTICS_H */
