#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

void log_set_level(LogLevel level);
void log_enable_file_output(const char *filename);

void log_error(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#endif
