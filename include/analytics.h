/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef ANALYTICS_H
#define ANALYTICS_H

#include "combogen.h"
#include "validate.h"

#define ANALYTICS_MAX_DRAWS 100000

/**
 * @file analytics.h
 * @brief Historical-draw analytics API (frequency, barometer, hot/cold).
 */

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
    char from_date[11]; /* YYYY-MM-DD */
    char to_date[11];   /* YYYY-MM-DD */
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
    char from_date[11]; /* YYYY-MM-DD */
    char to_date[11];   /* YYYY-MM-DD */
} BarometerReport;

typedef struct
{
    int number;

    /**
     * @brief Load historical draws from CSV into normalized records.
     */
    int count;
    double percentage;
} HotColdEntry;
    /**
     * @brief Load historical draws from the local snapshot database.
     */

typedef struct
{
    int total_draws;
    /**
     * @brief Filter historical draws by inclusive date interval.
     */
    int top_n;
    HotColdEntry hot[128];
    HotColdEntry cold[128];
    char from_date[11]; /* YYYY-MM-DD */
    /**
     * @brief Compute per-number hit frequencies across filtered draws.
     */
    char to_date[11];   /* YYYY-MM-DD */
} HotColdReport;

    /**
     * @brief Compute overdue-factor barometer metrics for each number.
     */
int analytics_load_historical_csv(const char *csv_path, HistoricalDraw *out_draws, int max_draws,
                                  int *out_count, const LotteryInfo *rules);

    /**
     * @brief Compute top-N hot/cold number statistics.
     */
int analytics_load_historical_db_snapshot(const char *game_name, const char *db_root,
                                          HistoricalDraw *out_draws, int max_draws, int *out_count,
                                          const LotteryInfo *rules);
    /** @brief Print frequency report in human-readable table format. */

    /** @brief Print frequency report as CSV to stdout. */
int analytics_filter_period(const HistoricalDraw *draws, int draw_count, const char *from_date,
    /** @brief Print frequency report as JSON to stdout. */
                            const char *to_date, HistoricalDraw *out_filtered,
    /** @brief Render frequency report in 2D GUI mode. */
                            int *out_filtered_count);
    /** @brief Render frequency report in 3D GUI mode. */

int analytics_compute_frequency(const HistoricalDraw *draws, int draw_count, int number_min,
    /** @brief Print barometer report in human-readable table format. */
                                int number_max, FrequencyReport *out_report);
    /** @brief Print barometer report as CSV to stdout. */

    /** @brief Print barometer report as JSON to stdout. */
int analytics_compute_barometer(const HistoricalDraw *draws, int draw_count, int number_min,
    /** @brief Render barometer report in 2D GUI mode. */
                                int number_max, int picks_per_draw, BarometerReport *out_report);
    /** @brief Render barometer report in 3D GUI mode. */

int analytics_compute_hot_cold(const HistoricalDraw *draws, int draw_count, int number_min,
    /** @brief Print hot/cold report in human-readable table format. */
                               int number_max, int top_n, HotColdReport *out_report);
    /** @brief Print hot/cold report as CSV to stdout. */

    /** @brief Print hot/cold report as JSON to stdout. */
void analytics_print_frequency_table(const FrequencyReport *report);
    /** @brief Render hot/cold report in 2D GUI mode. */
void analytics_print_frequency_csv(const FrequencyReport *report);
    /** @brief Render hot/cold report in 3D GUI mode. */
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
