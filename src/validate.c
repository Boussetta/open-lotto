/**
 * @file validate.c
 * @brief Implementation of input validation utilities.
 *
 * SPDX-License-Identifier: MIT
 */

#include "validate.h"
#include "log.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* =====================================================================
   Game Name Validation
   ===================================================================== */

int validate_game_name(const char *game_name, PluginRegistry *registry)
{
    if (!game_name || game_name[0] == '\0')
    {
        fprintf(stderr, "Error: Game name is empty or not provided.\n");
        fprintf(stderr, "Hint: Use --list-games to see available games.\n");
        return VALIDATE_ERR_EMPTY;
    }

    size_t len = strlen(game_name);
    if (len > 256)
    {
        fprintf(stderr, "Error: Game name is too long (%zu characters, max 256).\n", len);
        return VALIDATE_ERR_TOO_LONG;
    }

    /* Check if game exists in registry */
    LoadedPlugin *found = registry_find_plugin(registry, game_name);
    if (!found)
    {
        fprintf(stderr, "Error: Game '%s' not found.\n", game_name);
        fprintf(stderr, "Hint: Use --list-games to see available games:\n");
        fprintf(stderr, "  ./open-lotto --list-games\n");
        return VALIDATE_ERR_NOT_FOUND;
    }

    return VALIDATE_OK;
}

/* =====================================================================
   Draw Count Validation
   ===================================================================== */

int validate_draw_count(const char *draws_str, int *out_draws)
{
    if (!draws_str || draws_str[0] == '\0')
    {
        fprintf(stderr, "Error: Draw count is empty.\n");
        fprintf(stderr, "Hint: Use --draws N where N is a positive integer.\n");
        fprintf(stderr, "Example: --draws 10\n");
        return VALIDATE_ERR_EMPTY;
    }

    char *endptr;
    errno = 0;
    long val = strtol(draws_str, &endptr, 10);

    /* Check for parsing errors */
    if (errno == ERANGE)
    {
        fprintf(stderr, "Error: Draw count '%s' is out of range.\n", draws_str);
        fprintf(stderr, "Hint: Value must be between 1 and %d.\n", INT_MAX);
        return VALIDATE_ERR_OUT_OF_RANGE;
    }

    if (*endptr != '\0')
    {
        fprintf(stderr, "Error: Draw count '%s' is not a valid integer.\n", draws_str);
        fprintf(stderr, "Hint: Use --draws N where N contains only digits (e.g., 10, not 10x).\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    if (val < 1 || val > INT_MAX)
    {
        fprintf(stderr, "Error: Draw count must be between 1 and %d, got %ld.\n", INT_MAX, val);
        fprintf(stderr, "Hint: Use --draws N with a positive integer.\n");
        return VALIDATE_ERR_OUT_OF_RANGE;
    }

    *out_draws = (int)val;
    return VALIDATE_OK;
}

/* =====================================================================
   Export Format Validation
   ===================================================================== */

int validate_export_format(const char *format)
{
    if (!format || format[0] == '\0')
    {
        fprintf(stderr, "Error: Export format is empty.\n");
        fprintf(stderr, "Hint: Use --export FORMAT where FORMAT is 'csv' or 'json'.\n");
        fprintf(stderr, "Example: --export csv\n");
        return VALIDATE_ERR_EMPTY;
    }

    if (strcmp(format, "csv") != 0 && strcmp(format, "json") != 0)
    {
        fprintf(stderr, "Error: Export format '%s' is not supported.\n", format);
        fprintf(stderr, "Hint: Use one of: csv, json\n");
        fprintf(stderr, "Example: --export csv\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    return VALIDATE_OK;
}

/* =====================================================================
   Export Filename Validation
   ===================================================================== */

int validate_export_filename(const char *filename)
{
    if (!filename || filename[0] == '\0')
    {
        fprintf(stderr, "Error: Export filename is empty.\n");
        fprintf(stderr, "Hint: Use --output FILE where FILE is a valid file path.\n");
        fprintf(stderr, "Example: --output results.csv\n");
        return VALIDATE_ERR_EMPTY;
    }

    size_t len = strlen(filename);
    if (len > 512)
    {
        fprintf(stderr, "Error: Export filename is too long (%zu characters, max 512).\n", len);
        return VALIDATE_ERR_TOO_LONG;
    }

    /* Check for obviously invalid characters */
    if (filename[len - 1] == '/')
    {
        fprintf(stderr, "Error: Export filename '%s' is invalid.\n", filename);
        fprintf(stderr, "Hint: Provide a valid file path (not a directory).\n");
        return VALIDATE_ERR_FILE_INVALID;
    }

    /* Check if parent directory is writable (if filename contains a path) */
    const char *last_slash = strrchr(filename, '/');
    if (last_slash)
    {
        /* Extract directory path */
        size_t dir_len = (size_t)(last_slash - filename);
        char dirpath[513];
        strncpy(dirpath, filename, dir_len);
        dirpath[dir_len] = '\0';

        struct stat st;
        if (stat(dirpath, &st) == 0 && S_ISDIR(st.st_mode))
        {
            if (access(dirpath, W_OK) != 0)
            {
                fprintf(stderr, "Error: No write permission for directory '%s'.\n", dirpath);
                return VALIDATE_ERR_PERMISSION;
            }
        }
        else if (strcmp(dirpath, "") != 0)
        {
            fprintf(stderr, "Warning: Directory '%s' does not exist; will attempt to create it.\n",
                    dirpath);
        }
    }
    else
    {
        /* File in current directory; check write permission on cwd */
        if (access(".", W_OK) != 0)
        {
            fprintf(stderr, "Error: No write permission for current directory.\n");
            return VALIDATE_ERR_PERMISSION;
        }
    }

    return VALIDATE_OK;
}

/* =====================================================================
   Log Level Validation
   ===================================================================== */

int validate_log_level(const char *level)
{
    if (!level || level[0] == '\0')
    {
        fprintf(stderr, "Error: Log level is empty.\n");
        fprintf(stderr, "Hint: Use --verbose LEVEL where LEVEL is one of: ERROR, WARN, INFO, "
                        "DEBUG.\n");
        fprintf(stderr, "Example: --verbose DEBUG\n");
        return VALIDATE_ERR_EMPTY;
    }

    if (strcasecmp(level, "ERROR") != 0 && strcasecmp(level, "WARN") != 0 &&
        strcasecmp(level, "INFO") != 0 && strcasecmp(level, "DEBUG") != 0)
    {
        fprintf(stderr, "Error: Log level '%s' is not recognized.\n", level);
        fprintf(stderr, "Hint: Use one of: ERROR, WARN, INFO, DEBUG\n");
        fprintf(stderr, "Example: --verbose DEBUG\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    return VALIDATE_OK;
}

/* =====================================================================
   GUI Mode Validation
   ===================================================================== */

int validate_gui_mode(const char *mode)
{
    /* NULL mode is valid (defaults to 2D) */
    if (!mode)
    {
        return VALIDATE_OK;
    }

    if (strcmp(mode, "2D") != 0 && strcmp(mode, "3D") != 0)
    {
        fprintf(stderr, "Error: GUI mode '%s' is not supported.\n", mode);
        fprintf(stderr, "Hint: Use one of: 2D, 3D (or omit for default 2D).\n");
        fprintf(stderr, "Example: --gui 3D\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    return VALIDATE_OK;
}

/* =====================================================================
   Option Pair Validation
   ===================================================================== */

int validate_export_pair(const char *export_format, const char *export_filename)
{
    int has_format = (export_format != NULL);
    int has_filename = (export_filename != NULL);

    if (has_format && !has_filename)
    {
        fprintf(stderr, "Error: --export requires --output filename.\n");
        fprintf(stderr, "Hint: Provide both --export FORMAT and --output FILE.\n");
        fprintf(stderr, "Example: --export csv --output results.csv\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    if (!has_format && has_filename)
    {
        fprintf(stderr, "Error: --output requires --export format.\n");
        fprintf(stderr, "Hint: Provide both --export FORMAT and --output FILE.\n");
        fprintf(stderr, "Example: --export json --output results.json\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    return VALIDATE_OK;
}

/* =====================================================================
   Option Conflict Validation
   ===================================================================== */

int validate_option_conflicts(int animate, int gui, const char *export_format)
{
    if (animate && gui)
    {
        fprintf(stderr, "Error: --animate and --gui cannot be used together.\n");
        fprintf(stderr, "Hint: Choose one mode:\n");
        fprintf(stderr, "  - Animated CLI: --animate (shows spinner animation)\n");
        fprintf(stderr, "  - GUI mode: --gui [2D|3D] (interactive window)\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  ./open-lotto --game \"Lotto 6aus49\" --animate\n");
        fprintf(stderr, "  ./open-lotto --game \"Lotto 6aus49\" --gui 3D\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    if (export_format && gui)
    {
        fprintf(stderr, "Error: --export and --gui cannot be used together.\n");
        fprintf(stderr, "Hint: Choose one mode:\n");
        fprintf(stderr, "  - Export to file: --export FORMAT --output FILE\n");
        fprintf(stderr, "  - GUI mode: --gui [2D|3D]\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  ./open-lotto --game \"Lotto 6aus49\" --export csv --output "
                        "results.csv\n");
        fprintf(stderr, "  ./open-lotto --game \"Lotto 6aus49\" --gui 2D\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    if (export_format && animate)
    {
        fprintf(stderr, "Error: --export and --animate cannot be used together.\n");
        fprintf(stderr, "Hint: Choose one mode:\n");
        fprintf(stderr, "  - Animated CLI: --animate (shows spinner animation)\n");
        fprintf(stderr, "  - Export to file: --export FORMAT --output FILE\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  ./open-lotto --game \"Lotto 6aus49\" --animate\n");
        fprintf(stderr, "  ./open-lotto --game \"Lotto 6aus49\" --export csv --output "
                        "results.csv\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    return VALIDATE_OK;
}

/* =====================================================================
   Error Hint Messages
   ===================================================================== */

const char *validate_error_hint(ValidateError error_code, const char *context)
{
    (void)context; /* unused for now, but available for future context-specific hints */

    switch (error_code)
    {
    case VALIDATE_OK:
        return "No error.";
    case VALIDATE_ERR_EMPTY:
        return "A required value is empty or missing. Check your command syntax.";
    case VALIDATE_ERR_TOO_LONG:
        return "A value exceeds its maximum allowed length.";
    case VALIDATE_ERR_INVALID_FORMAT:
        return "A value has an invalid format. Check the examples in --help.";
    case VALIDATE_ERR_OUT_OF_RANGE:
        return "A numeric value is outside the allowed range.";
    case VALIDATE_ERR_NOT_FOUND:
        return "A game name or resource was not found. Use --list-games to see available "
               "games.";
    case VALIDATE_ERR_FILE_INVALID:
        return "A file path is invalid or points to a directory instead of a file.";
    case VALIDATE_ERR_PERMISSION:
        return "You do not have permission to write to the specified location.";
    default:
        return "An unknown validation error occurred.";
    }
}
