/*
 * logger.h — Structured logging for the Metalsharp C backend
 *
 * WHAT
 *   Lightweight, thread-safe logger that emits timestamped, level-
 *   tagged records to a caller-supplied FILE stream (typically
 *   stderr). Each record carries the source file basename and line
 *   number so support engineers can correlate a log entry with the
 *   exact code that produced it. A monotonic issue counter hands
 *   out unique IDs so callers can attach a stable identifier to a
 *   record and reference it later in bug reports or telemetry.
 *
 * IMPORTS
 *   server.h     Metalsharp shared types (PATH_MAX, error codes)
 *   <stdbool.h>  bool, true, false
 *   <stddef.h>   size_t, NULL
 *   <stdio.h>    FILE
 *
 * EXPORTS
 *   LogLevel              Enumerated severity (DEBUG, INFO, WARN, ERROR)
 *   logger_init           Configure output stream; defaults to stderr/INFO
 *   logger_set_level      Adjust the minimum emitted severity
 *   logger_get_level      Read the current minimum emitted severity
 *   logger_log            Emit one record at an explicit severity
 *   logger_next_id        Atomically mint the next issue counter value
 *   LOG_DEBUG             Convenience macro for logger_log(LOG_DEBUG, ...)
 *   LOG_INFO              Convenience macro for logger_log(LOG_INFO, ...)
 *   LOG_WARN              Convenience macro for logger_log(LOG_WARN, ...)
 *   LOG_ERROR             Convenience macro for logger_log(LOG_ERROR, ...)
 *   (Each macro takes a printf-style format string plus zero or
 *    more substitution arguments, e.g. LOG_INFO("x=%d", 7).)
 *
 * SCHEMA
 *   Each emitted record is a single line of the form:
 *     [YYYY-MM-DDTHH:MM:SS.mmmZ] [LEVEL] [basename:line] message...
 *   Timestamps are UTC, with millisecond resolution produced via
 *   clock_gettime(CLOCK_REALTIME). Records strictly below the
 *   configured minimum level are dropped without formatting.
 *   logger_log is safe to call from multiple threads concurrently;
 *   the underlying writes are serialized with a pthread mutex so
 *   that records never interleave on the same line. logger_next_id
 *   uses a relaxed atomic fetch-and-add and is therefore safe to
 *   call from any thread without holding the log mutex.
 */
#ifndef METALSHARP_LOGGER_H
#define METALSHARP_LOGGER_H

#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Severity levels ordered from most verbose to most severe. Records
 * with level < logger_get_level() are dropped before any formatting
 * work is performed, so callers may freely embed expensive format
 * arguments inside LOG_DEBUG without paying for them when debug
 * logging is disabled.
 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
} LogLevel;

/*
 * Configure the logger. `output` becomes the destination FILE *
 * for every subsequent log record; passing NULL selects stderr.
 * The minimum level is reset to LOG_INFO. Safe to call multiple
 * times; later calls reconfigure the destination atomically under
 * the same mutex used for log writes.
 */
void logger_init(FILE* output);

/*
 * Set the minimum severity emitted by logger_log. Records with a
 * level strictly less than `level` are dropped silently. Levels
 * are compared by their enumerator value (DEBUG < INFO < WARN <
 * ERROR), so callers may rely on ordering without referring to
 * numeric values directly.
 */
void logger_set_level(LogLevel level);

/*
 * Return the current minimum severity. Intended for diagnostics
 * and tests; the returned value reflects the last successful
 * logger_init or logger_set_level call.
 */
LogLevel logger_get_level(void);

/*
 * Emit one structured log record at the given severity. `file` is
 * typically __FILE__ and `line` __LINE__; only the basename of
 * `file` appears in the formatted output so that records stay
 * readable regardless of the source tree layout. `fmt` is a
 * printf-style format string consumed by vfprintf together with
 * the trailing variadic arguments.
 */
void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...);

/*
 * Atomically increment the issue counter and return its new value.
 * The first call returns 1, then 2, 3, and so on. The counter is
 * monotonic for the lifetime of the process; callers may use the
 * returned ID as a stable correlation token in bug reports and
 * telemetry without coordinating with other threads.
 */
uint64_t logger_next_id(void);

/*
 * Convenience macros. Each expands to a logger_log call site with
 * __FILE__ and __LINE__ filled in automatically. Callers invoke
 * them with a printf-style format string followed by zero or more
 * substitution arguments, e.g. LOG_INFO("hello") or LOG_INFO("x=%d",
 * 7). The macros consume all arguments through the variadic slot
 * rather than splitting the format string into a named parameter,
 * because clang's -pedantic mode rejects both `, ##__VA_ARGS__` and
 * empty-variadic invocations of a (fmt, ...)-style macro.
 */
#define LOG_DEBUG(...) logger_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* METALSHARP_LOGGER_H */