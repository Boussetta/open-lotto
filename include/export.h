/**
 * @file export.h
 * @brief Export lottery results to CSV and JSON formats
 */

#ifndef EXPORT_H
#define EXPORT_H

#include "combogen.h"
#include <stdio.h>

/**
 * @brief Export a single lottery result to CSV format
 *
 * Writes a lottery result as a CSV row to the provided file.
 * CSV format: draw_number,main_numbers (space-separated),extra_numbers
 * (space-separated)
 *
 * @param file Open file pointer for writing
 * @param game_name Name of the game
 * @param draw_num Draw number (1-based)
 * @param result The lottery result to export
 * @return 0 on success, -1 on error
 */
int export_result_csv(FILE *file, const char *game_name, int draw_num, const LotteryResult *result);

/**
 * @brief Export multiple lottery results to CSV file
 *
 * Writes a CSV file with header and all results as rows.
 *
 * @param filename Path to output CSV file
 * @param game_name Name of the game
 * @param results Array of LotteryResult structures
 * @param num_results Number of results in array
 * @return 0 on success, -1 on error
 */
int export_results_csv_file(const char *filename, const char *game_name,
                            const LotteryResult *results, int num_results);

/**
 * @brief Export multiple lottery results to JSON file
 *
 * Writes a JSON file with an array of all results.
 * Format: {"game": "name", "draws": [...]}
 *
 * @param filename Path to output JSON file
 * @param game_name Name of the game
 * @param results Array of LotteryResult structures
 * @param num_results Number of results in array
 * @return 0 on success, -1 on error
 */
int export_results_json_file(const char *filename, const char *game_name,
                             const LotteryResult *results, int num_results);

#endif
