/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static LogLevel current_level = LOG_INFO;
static FILE *log_file = NULL;

// ANSI colors
#define COLOR_RESET "\033[0m"
#define COLOR_ERROR "\033[31m"
#define COLOR_WARN "\033[33m"
#define COLOR_INFO "\033[32m"
#define COLOR_DEBUG "\033[36m"

static const char *level_to_string(LogLevel level)
{
    switch (level)
    {
    case LOG_ERROR:
        return "ERROR";
    case LOG_WARN:
        return "WARN";
    case LOG_INFO:
        return "INFO";
    case LOG_DEBUG:
        return "DEBUG";
    default:
        return "LOG";
    }
}


static const char *level_to_color(LogLevel level)
{
    switch (level)
    {
    case LOG_ERROR:
        return COLOR_ERROR;
    case LOG_WARN:
        return COLOR_WARN;
    case LOG_INFO:
        return COLOR_INFO;
    case LOG_DEBUG:
        return COLOR_DEBUG;
    default:
        return COLOR_RESET;
    }
}

void log_set_level(LogLevel level)
{
    current_level = level;
}

void log_enable_file_output(const char *filename)
{
    log_file = fopen(filename, "a");
    if (!log_file)
    {
        fprintf(stderr, "ERROR: Cannot open log file: %s\n", filename);
    }
}

static void log_write(LogLevel level, const char *fmt, va_list args)
{
    if (level > current_level)
        return;

    // Timestamp
    char timebuf[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Console output (colored)
    fprintf(stderr, "%s[%s] %s: ", level_to_color(level), timebuf, level_to_string(level));
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "%s\n", COLOR_RESET);

    // File output (no colors)
    if (log_file)
    {
        fprintf(log_file, "[%s] %s: ", timebuf, level_to_string(level));
        vfprintf(log_file, fmt, args);
        fprintf(log_file, "\n");
        fflush(log_file);
    }
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_ERROR, fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_WARN, fmt, args);
    va_end(args);
}

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_INFO, fmt, args);
    va_end(args);
}

void log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_DEBUG, fmt, args);
    va_end(args);
}
