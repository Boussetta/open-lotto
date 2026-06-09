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

typedef struct
{
    int total_draws;
    int number_min;
    int number_max;
    double expected_interval;
    int hit_counts[128];
    int observed_gaps[128];
    double factors[128];
} BarometerReport;

typedef struct
{
    int number;
    int count;
    double percentage;
} HotColdEntry;

typedef struct
{
    int total_draws;
    int top_n;
    HotColdEntry hot[128];
    HotColdEntry cold[128];
} HotColdReport;

int analytics_load_historical_csv(const char *csv_path, HistoricalDraw *out_draws, int max_draws,
                                  int *out_count, const LotteryInfo *rules);

int analytics_filter_period(const HistoricalDraw *draws, int draw_count, const char *from_date,
                            const char *to_date, HistoricalDraw *out_filtered,
                            int *out_filtered_count);

int analytics_compute_frequency(const HistoricalDraw *draws, int draw_count, int number_min,
                                int number_max, FrequencyReport *out_report);

int analytics_compute_barometer(const HistoricalDraw *draws, int draw_count, int number_min,
                                int number_max, int picks_per_draw, BarometerReport *out_report);

int analytics_compute_hot_cold(const HistoricalDraw *draws, int draw_count, int number_min,
                               int number_max, int top_n, HotColdReport *out_report);

void analytics_print_frequency_table(const FrequencyReport *report);
void analytics_print_frequency_csv(const FrequencyReport *report);
void analytics_print_frequency_json(const FrequencyReport *report);
void analytics_print_frequency_gui_2d(const FrequencyReport *report);
void analytics_print_frequency_gui_3d_matlab(const FrequencyReport *report);

void analytics_print_barometer_table(const BarometerReport *report);
void analytics_print_barometer_csv(const BarometerReport *report);
void analytics_print_barometer_json(const BarometerReport *report);
void analytics_print_barometer_gui_2d(const BarometerReport *report);
void analytics_print_barometer_gui_3d_matlab(const BarometerReport *report);

void analytics_print_hot_cold_table(const HotColdReport *report);
void analytics_print_hot_cold_csv(const HotColdReport *report);
void analytics_print_hot_cold_json(const HotColdReport *report);
void analytics_print_hot_cold_gui_2d(const HotColdReport *report);
void analytics_print_hot_cold_gui_3d_matlab(const HotColdReport *report);

#endif /* ANALYTICS_H */
