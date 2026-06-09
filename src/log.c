/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static LogLevel current_level = LOG_INFO;
static FILE *log_file = NULL;
static LogLineObserver line_observer = NULL;

static int mkdir_compat(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

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

static void log_enable_file_output(const char *filename)
{
    log_file = fopen(filename, "a");
    if (!log_file)
    {
        fprintf(stderr, "ERROR: Cannot open log file: %s\n", filename);
    }
}

/* Create every component of `path` that does not already exist. */
static void log_mkdir_p(const char *path)
{
    char tmp[768];
    if (!path || strlen(path) == 0 || strlen(path) >= sizeof(tmp))
        return;
    size_t len = strlen(path);

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (size_t i = 1; i < len; i++)
    {
#ifdef _WIN32
        if (tmp[i] == '\\' || tmp[i] == '/')
#else
        if (tmp[i] == '/')
#endif
        {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (mkdir_compat(tmp) != 0 && errno != EEXIST)
            {
                return;
            }
            tmp[i] = saved;
        }
    }
    mkdir_compat(tmp); /* last component */
}

void log_init_default_file(void)
{
    char dir[768];
    char path[800];

#if defined(_WIN32)
    /* Windows: %APPDATA%\open-lotto\logs\ */
    const char *appdata = getenv("APPDATA");
    if (!appdata || appdata[0] == '\0')
        return;
    snprintf(dir, sizeof(dir), "%s\\open-lotto\\logs", appdata);
    snprintf(path, sizeof(path), "%s\\open-lotto.log", dir);
    /* normalise separators for fopen */
    for (char *p = dir; *p; p++)
        if (*p == '\\')
            *p = '/';
    for (char *p = path; *p; p++)
        if (*p == '\\')
            *p = '/';

#elif defined(__APPLE__)
    /* macOS: ~/Library/Logs/open-lotto/ */
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        home = ".";
    snprintf(dir, sizeof(dir), "%s/Library/Logs/open-lotto", home);
    snprintf(path, sizeof(path), "%s/open-lotto.log", dir);

#else
    /* Linux / other POSIX: $XDG_STATE_HOME/open-lotto/ */
    const char *state_home = getenv("XDG_STATE_HOME");
    if (state_home && state_home[0] != '\0')
    {
        snprintf(dir, sizeof(dir), "%s/open-lotto", state_home);
    }
    else
    {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0')
            home = ".";
        snprintf(dir, sizeof(dir), "%s/.local/state/open-lotto", home);
    }
    snprintf(path, sizeof(path), "%s/open-lotto.log", dir);
#endif

    log_mkdir_p(dir);
    log_enable_file_output(path);
}

void log_set_line_observer(LogLineObserver observer)
{
    line_observer = observer;
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

    if (line_observer)
        line_observer();

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
