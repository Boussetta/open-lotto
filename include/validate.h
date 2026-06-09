/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file validate.h
 * @brief Input validation utilities for CLI arguments and configuration.
 *
 * This module provides comprehensive validation functions for:
 * - Game names and availability
 * - Draw counts and ranges
 * - Export formats and file paths
 * - Log levels
 * - Configuration values
 *
 * Each validation function returns an error code and provides helpful
 * error messages via stderr.
 */

#ifndef VALIDATE_H
#define VALIDATE_H

#include "plugin_registry.h"
#include <stdint.h>

/**
 * @brief Validation error codes.
 */
typedef enum
{
    VALIDATE_OK = 0,                 /**< Validation passed */
    VALIDATE_ERR_EMPTY = 1,          /**< Value is empty or NULL */
    VALIDATE_ERR_TOO_LONG = 2,       /**< Value exceeds max length */
    VALIDATE_ERR_INVALID_FORMAT = 3, /**< Invalid format */
    VALIDATE_ERR_OUT_OF_RANGE = 4,   /**< Value out of allowed range */
    VALIDATE_ERR_NOT_FOUND = 5,      /**< Game or resource not found */
    VALIDATE_ERR_FILE_INVALID = 6,   /**< File path invalid or inaccessible */
    VALIDATE_ERR_PERMISSION = 7,     /**< Permission denied */
} ValidateError;

/**
 * @brief Validate a game name against available plugins.
 *
 * Checks that:
 * - game_name is not NULL or empty
 * - game_name is not too long (max 256 characters)
 * - game_name exists in the registry
 *
 * @param game_name The game name to validate
 * @param registry The plugin registry to check against
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_game_name(const char *game_name, PluginRegistry *registry);

/**
 * @brief Validate a draw count string.
 *
 * Checks that:
 * - draws_str is a valid integer
 * - Value is >= 1 and <= INT_MAX
 *
 * @param draws_str The draw count string to parse
 * @param out_draws Pointer to write validated integer value
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_draw_count(const char *draws_str, int *out_draws);

/**
 * @brief Validate an export format string.
 *
 * Checks that:
 * - format is one of: "csv", "json"
 *
 * @param format The format string to validate
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_export_format(const char *format);

/**
 * @brief Validate an export output filename.
 *
 * Checks that:
 * - filename is not NULL or empty
 * - filename is not too long (max 512 characters)
 * - directory is writable (if parent exists)
 *
 * @param filename The output filename to validate
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_export_filename(const char *filename);

/**
 * @brief Validate a log level string.
 *
 * Checks that:
 * - level is one of: "ERROR", "WARN", "INFO", "DEBUG"
 *
 * @param level The log level string to validate
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_log_level(const char *level);

/**
 * @brief Validate a GUI mode string.
 *
 * Checks that:
 * - mode is NULL (default 2D) or one of: "2D", "3D"
 *
 * @param mode The GUI mode string to validate (can be NULL)
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_gui_mode(const char *mode);

/**
 * @brief Check if --export and --output are both provided (or both missing).
 *
 * Returns error if export format is set but output filename is not provided,
 * or vice versa.
 *
 * @param export_format Export format string (can be NULL)
 * @param export_filename Output filename (can be NULL)
 * @return VALIDATE_OK if both are set or both are NULL, error otherwise
 */
int validate_export_pair(const char *export_format, const char *export_filename);

/**
 * @brief Check for conflicting CLI flags.
 *
 * Returns error if mutually exclusive flags are used together:
 * - --animate and --gui
 * - --export and --gui
 * - --export and --animate
 *
 * @param animate 1 if --animate is set
 * @param gui 1 if --gui is set
 * @param export_format Export format (can be NULL if not set)
 * @return VALIDATE_OK if no conflicts, error otherwise
 */
int validate_option_conflicts(int animate, int gui, const char *export_format);

/**
 * @brief Validate a strict ISO date string in YYYY-MM-DD format.
 *
 * Checks that:
 * - date_str is non-empty
 * - format is exactly YYYY-MM-DD
 * - month/day values are valid (including leap years)
 *
 * @param date_str The date string to validate
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_iso_date(const char *date_str);

/**
 * @brief Validate an analytics period and optional available-data bounds.
 *
 * Checks that:
 * - both from_date and to_date are provided
 * - both are valid ISO dates
 * - from_date <= to_date
 * - when available_from/available_to are provided, the requested period intersects
 *   the available range
 *
 * @param from_date Inclusive start date (YYYY-MM-DD)
 * @param to_date Inclusive end date (YYYY-MM-DD)
 * @param available_from Optional earliest available date (YYYY-MM-DD), or NULL
 * @param available_to Optional latest available date (YYYY-MM-DD), or NULL
 * @return VALIDATE_OK on success, or ValidateError code with error message on stderr
 */
int validate_analytics_period(const char *from_date, const char *to_date,
                              const char *available_from, const char *available_to);

/**
 * @brief Get a human-readable error hint for common validation mistakes.
 *
 * @param error_code The ValidateError code
 * @param context Additional context (e.g., the invalid value)
 * @return A statically allocated hint string
 */
const char *validate_error_hint(ValidateError error_code, const char *context);

#endif /* VALIDATE_H */
