/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

typedef enum
{
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

typedef void (*LogLineObserver)(void);

void log_set_level(LogLevel level);
void log_enable_file_output(const char *filename);

/**
 * Open the log file at the OS-conventional location and redirect file output
 * there. Creates intermediate directories as needed. On Linux the path is
 * $XDG_STATE_HOME/open-lotto/open-lotto.log (defaulting to
 * ~/.local/state/open-lotto/open-lotto.log). On macOS it is
 * ~/Library/Logs/open-lotto/open-lotto.log. On Windows it is
 * %APPDATA%\open-lotto\logs\open-lotto.log.
 */
void log_init_default_file(void);
void log_set_line_observer(LogLineObserver observer);

void log_error(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#endif
