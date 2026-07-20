/*
 * logger.c — Structured logging implementation
 *
 * Implementation notes:
 *   - All log writes are serialized by a single pthread mutex so
 *     that records from concurrent threads never interleave on the
 *     same line. The mutex also guards reconfiguration through
 *     logger_init and logger_set_level so that a stream swap is
 *     observed atomically with respect to in-flight writes.
 *   - Timestamps come from clock_gettime(CLOCK_REALTIME) and are
 *     rendered as UTC ISO 8601 with millisecond precision. The
 *     clock_gettime call runs inside the mutex because the gmtime
 *     family and the FILE stream both rely on shared mutable state.
 *   - The issue counter is a process-lifetime atomic uint64_t.
 *     Access uses the __atomic_fetch_add GCC/Clang builtin with
 *     __ATOMIC_RELAXED because no other state needs to synchronize
 *     with the increment; the returned ID is unique by construction.
 *   - logger_get_level performs an unlocked read of a word-sized
 *     value. On every supported platform (arm64 macOS, x86_64) the
 *     read is naturally atomic and the worst observable value is
 *     one update behind, which is acceptable for a diagnostic API.
 *   - Function depth stays well below the 8-frame budget: the
 *     deepest path through logger_log reaches gmtime_r or strftime
 *     (depth 2) at most, with the message formatting delegated to
 *     vfprintf (depth 2) for the variadic payload.
 */

#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Module-level state. g_output and g_level are guarded by g_mutex. */
static FILE* g_output = NULL;
static LogLevel g_level = LOG_INFO;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Issue counter. Accessed exclusively through __atomic_fetch_add
 * and __atomic_load so it does not need to participate in the log
 * mutex. Initial value 0 means the first logger_next_id call
 * returns 1, matching the documented contract.
 */
static uint64_t g_issue_counter = 0;

/*
 * Map a LogLevel to the short tag printed in the formatted record.
 * Returning "UNKNOWN" for any value outside the documented enum
 * keeps the formatter total and prevents a stray record from
 * crashing the logger itself if a future caller passes a bogus
 * integer cast.
 */
static const char* level_name(LogLevel level) {
    switch (level) {
    case LOG_DEBUG:
        return "DEBUG";
    case LOG_INFO:
        return "INFO";
    case LOG_WARN:
        return "WARN";
    case LOG_ERROR:
        return "ERROR";
    }
    return "UNKNOWN";
}

/*
 * Return a pointer to the final path component of `path`. The
 * logger uses this to keep formatted records compact even when
 * __FILE__ points deep into the source tree. Falls back to the
 * original pointer when no slash is present so a bare filename is
 * preserved unchanged.
 */
static const char* path_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    if (slash != NULL && slash[1] != '\0') {
        return slash + 1;
    }
    return path;
}

void logger_init(FILE* output) {
    pthread_mutex_lock(&g_mutex);
    g_output = (output != NULL) ? output : stderr;
    g_level = LOG_INFO;
    pthread_mutex_unlock(&g_mutex);
}

void logger_set_level(LogLevel level) {
    pthread_mutex_lock(&g_mutex);
    g_level = level;
    pthread_mutex_unlock(&g_mutex);
}

LogLevel logger_get_level(void) {
    /* Single-word reads are atomic on every supported platform, so
     * taking the mutex here would only add contention without
     * changing the value any caller could observe. */
    return g_level;
}

void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (level < g_level) {
        return;
    }

    pthread_mutex_lock(&g_mutex);

    if (g_output == NULL) {
        /* Defensive default: a caller who never invoked logger_init
         * still gets visible output rather than a silent drop. */
        g_output = stderr;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        fprintf(g_output, "[0000-00-00T00:00:00.000Z] ");
    } else {
        struct tm tm_buf;
        gmtime_r(&ts.tv_sec, &tm_buf);
        char stamp[32];
        if (strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tm_buf) == 0) {
            fprintf(g_output, "[0000-00-00T00:00:00.000Z] ");
        } else {
            fprintf(g_output, "[%s.%03ldZ] ", stamp, (long)(ts.tv_nsec / 1000000L));
        }
    }

    const char* display_file = (file != NULL) ? file : "?";
    fprintf(g_output, "[%s] [%s:%d] ", level_name(level), path_basename(display_file), line);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_output, fmt, args);
    va_end(args);

    fputc('\n', g_output);
    fflush(g_output);

    pthread_mutex_unlock(&g_mutex);
}

uint64_t logger_next_id(void) {
    /* __atomic_fetch_add returns the previous value, then increments.
     * Adding one yields the post-increment value so the first call
     * returns 1, matching the documented monotonic sequence. */
    uint64_t previous = __atomic_fetch_add(&g_issue_counter, 1, __ATOMIC_RELAXED);
    return previous + 1u;
}