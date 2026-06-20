/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

/**
 * @file log.h
 * @brief Central logging facade with configurable level and sink behavior.
 */

typedef enum
{
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

typedef void (*LogLineObserver)(void);

void log_set_level(LogLevel level);

/**
 * Open the log file at the OS-conventional location and redirect file output
 * there. Creates intermediate directories as needed. On Linux the path is
 * $XDG_STATE_HOME/open-lotto/open-lotto.log (defaulting to
 * ~/.local/state/open-lotto/open-lotto.log). On macOS it is
 * ~/Library/Logs/open-lotto/open-lotto.log. On Windows it is
 * %APPDATA%\open-lotto\logs\open-lotto.log.
 */
void log_init_default_file(void);
/** @brief Register callback invoked after each emitted log line. */
void log_set_line_observer(LogLineObserver observer);

/** @brief Emit an error-level log line. */
void log_error(const char *fmt, ...);
/** @brief Emit a warning-level log line. */
void log_warn(const char *fmt, ...);
/** @brief Emit an info-level log line. */
void log_info(const char *fmt, ...);
/** @brief Emit a debug-level log line. */
void log_debug(const char *fmt, ...);

#endif
