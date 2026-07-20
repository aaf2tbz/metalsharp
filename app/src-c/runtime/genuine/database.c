/*
 * database.c — SQLite wrapper for the Metalsharp C backend
 *
 * Implementation notes:
 *   - The wrapper holds a sqlite3 connection, an owned copy of the
 *     path used at open time, and a pthread mutex. Every public
 *     function takes the mutex so concurrent callers observe a
 *     strictly serialised stream of statements. Because we already
 *     serialise externally, the handle is opened with SQLite's
 *     default serialised threading mode left untouched; we use
 *     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE so the file is
 *     created on first use and the same flags survive every boot.
 *   - On a successful open the wrapper issues
 *       PRAGMA journal_mode=WAL
 *       PRAGMA foreign_keys=ON
 *     so every connection boots into write-ahead logging and
 *     enforces referential integrity. Failure to apply either
 *     pragma rolls back the open and returns NULL so callers never
 *     see a handle whose pragmas silently disagree with the rest
 *     of the backend.
 *   - db_query uses sqlite3_exec together with a static row
 *     callback that forwards to the caller-supplied callback. The
 *     indirection captures the user callback and its opaque
 *     context inside a small heap struct so SQLite's "first
 *     argument" slot can carry them both without exposing the
 *     wrapper's plumbing to callers.
 *   - Errors raised by sqlite3_exec are returned via the caller-
 *     supplied `char **error` out-parameter as a strdup'd,
 *     NUL-terminated string. SQLite's own error buffer is released
 *     with sqlite3_free immediately after copying. Callers release
 *     the strdup'd copy with the standard free(). This keeps the
 *     error contract uniform with the rest of the C backend while
 *     staying compatible with SQLite's allocation strategy.
 *   - Function depth stays well below the 8-frame budget: the
 *     deepest path through db_open reaches four indentation levels
 *     (function -> if -> if -> if) when chaining the
 *     calloc/strdup/mutex_init/sqlite3_open_v2 failure checks, and
 *     db_query's row wrapper runs at depth 2 at most.
 */

#include "database.h"

#include <pthread.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Per-query plumbing handed to sqlite3_exec. The wrapper stores
 * the caller's callback and opaque context here so the static
 * shim below can forward them to db_query's user without exposing
 * its internals to other modules.
 */
typedef struct {
    DbRowCallback user_cb;
    void* user_ctx;
} QueryCtx;

/*
 * Concrete Database layout. Kept private so external callers
 * cannot reach in and bypass the mutex that serialises every
 * sqlite3 call.
 */
struct Database {
    sqlite3* handle;       /* Underlying SQLite connection. */
    char* path;            /* Owned copy of the open path. */
    pthread_mutex_t mutex; /* Serialises every db_* call. */
};

/*
 * Replace `*error` with a heap-allocated, NUL-terminated copy of
 * `msg`. The caller already supplied a non-NULL `error` pointer;
 * this routine only writes through it and never reads the prior
 * contents. Aborts on allocation failure because the surrounding
 * process is long-lived and there is no recovery path.
 */
static void db_set_error(char** error, const char* msg) {
    size_t len = strlen(msg);
    char* copy = malloc(len + 1);
    if (copy == NULL) {
        abort();
    }
    memcpy(copy, msg, len);
    copy[len] = '\0';
    *error = copy;
}

/*
 * Bridge sqlite3_exec's row callback to the caller's DbRowCallback.
 * Returning the inner callback's value verbatim means a non-zero
 * return from user code aborts the statement and propagates back
 * through sqlite3_exec as a failure, exactly as documented. A
 * missing wrapper context or callback is treated as "continue
 * iterating" so db_query can run a SELECT without supplying a
 * callback.
 */
static int db_query_row_shim(void* raw, int ncols, char** values, char** names) {
    QueryCtx* qctx = raw;
    if (qctx == NULL || qctx->user_cb == NULL) {
        return 0;
    }
    return qctx->user_cb(qctx->user_ctx, ncols, values, names);
}

/*
 * Open or create the SQLite database at `path`. On success the
 * handle has WAL journaling and foreign keys enabled. Returns
 * NULL on any failure path; the caller has nothing to release in
 * that case because all partially-initialised state is unwound
 * before the function returns.
 */
Database* db_open(const char* path) {
    if (path == NULL) {
        return NULL;
    }

    Database* db = calloc(1, sizeof(Database));
    if (db == NULL) {
        return NULL;
    }

    size_t path_len = strlen(path);
    db->path = malloc(path_len + 1);
    if (db->path == NULL) {
        free(db);
        return NULL;
    }
    memcpy(db->path, path, path_len);
    db->path[path_len] = '\0';

    if (pthread_mutex_init(&db->mutex, NULL) != 0) {
        free(db->path);
        free(db);
        return NULL;
    }

    int rc = sqlite3_open_v2(db->path, &db->handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        const char* msg = (db->handle != NULL) ? sqlite3_errmsg(db->handle) : "unable to open sqlite database";
        /* Keep a local copy because sqlite3_close below may
         * invalidate the buffer the errmsg points into. */
        char fallback[256];
        size_t msg_len = strlen(msg);
        if (msg_len >= sizeof(fallback)) {
            msg_len = sizeof(fallback) - 1;
        }
        memcpy(fallback, msg, msg_len);
        fallback[msg_len] = '\0';

        if (db->handle != NULL) {
            sqlite3_close(db->handle);
        }
        pthread_mutex_destroy(&db->mutex);
        free(db->path);
        free(db);
        /* The caller cannot see this message because db_open
         * reports failure by returning NULL; keep it in the log
         * stream so support engineers can diagnose the cause. */
        fprintf(stderr, "[database] db_open failed: %s\n", fallback);
        return NULL;
    }

    /*
     * Apply the project's mandatory pragmas. Both are issued as
     * plain sqlite3_exec calls so the open path does not depend
     * on db_exec's NULL-safe wrapper. Any failure tears the
     * handle down so callers never observe a connection whose
     * pragmas silently disagree with the rest of the backend.
     */
    char* pragma_err = NULL;
    rc = sqlite3_exec(db->handle, "PRAGMA journal_mode=WAL", NULL, NULL, &pragma_err);
    if (rc != SQLITE_OK) {
        const char* msg = (pragma_err != NULL) ? pragma_err : sqlite3_errmsg(db->handle);
        fprintf(stderr, "[database] PRAGMA journal_mode=WAL failed: %s\n", msg);
        if (pragma_err != NULL) {
            sqlite3_free(pragma_err);
        }
        sqlite3_close(db->handle);
        pthread_mutex_destroy(&db->mutex);
        free(db->path);
        free(db);
        return NULL;
    }
    sqlite3_free(pragma_err);
    pragma_err = NULL;

    rc = sqlite3_exec(db->handle, "PRAGMA foreign_keys=ON", NULL, NULL, &pragma_err);
    if (rc != SQLITE_OK) {
        const char* msg = (pragma_err != NULL) ? pragma_err : sqlite3_errmsg(db->handle);
        fprintf(stderr, "[database] PRAGMA foreign_keys=ON failed: %s\n", msg);
        if (pragma_err != NULL) {
            sqlite3_free(pragma_err);
        }
        sqlite3_close(db->handle);
        pthread_mutex_destroy(&db->mutex);
        free(db->path);
        free(db);
        return NULL;
    }
    sqlite3_free(pragma_err);

    return db;
}

/*
 * Close the handle. Safe with NULL so callers can clean up
 * defensively. Releases the mutex, closes the sqlite3 connection,
 * frees the path copy, and frees the Database struct itself.
 */
void db_close(Database* db) {
    if (db == NULL) {
        return;
    }

    pthread_mutex_destroy(&db->mutex);
    if (db->handle != NULL) {
        sqlite3_close(db->handle);
    }
    free(db->path);
    free(db);
}

/*
 * Execute `sql` without producing any result rows. Holds the
 * mutex for the duration of the statement so concurrent writers
 * cannot interleave with the same connection. On failure the
 * sqlite3-allocated error buffer is copied into a caller-owned
 * heap string and the original is released with sqlite3_free.
 */
bool db_exec(Database* db, const char* sql, char** error) {
    if (db == NULL || sql == NULL) {
        if (error != NULL) {
            db_set_error(error, "db_exec: NULL database or SQL");
        }
        return false;
    }

    pthread_mutex_lock(&db->mutex);
    char* sqlite_err = NULL;
    int rc = sqlite3_exec(db->handle, sql, NULL, NULL, &sqlite_err);
    bool ok = (rc == SQLITE_OK);
    if (!ok && error != NULL) {
        const char* msg = (sqlite_err != NULL) ? sqlite_err : sqlite3_errmsg(db->handle);
        db_set_error(error, msg);
    }
    if (sqlite_err != NULL) {
        sqlite3_free(sqlite_err);
    }
    pthread_mutex_unlock(&db->mutex);
    return ok;
}

/*
 * Execute a SELECT (or any statement with rows) and forward each
 * row to the caller-supplied callback. The QueryCtx shim couples
 * the user's callback and opaque context together so SQLite's
 * "first argument" slot can carry both through a single void *.
 */
bool db_query(Database* db, const char* sql, DbRowCallback cb, void* ctx, char** error) {
    if (db == NULL || sql == NULL) {
        if (error != NULL) {
            db_set_error(error, "db_query: NULL database or SQL");
        }
        return false;
    }

    QueryCtx qctx;
    qctx.user_cb = cb;
    qctx.user_ctx = ctx;

    pthread_mutex_lock(&db->mutex);
    char* sqlite_err = NULL;
    int rc = sqlite3_exec(db->handle, sql, db_query_row_shim, &qctx, &sqlite_err);
    bool ok = (rc == SQLITE_OK);
    if (!ok && error != NULL) {
        const char* msg = (sqlite_err != NULL) ? sqlite_err : sqlite3_errmsg(db->handle);
        db_set_error(error, msg);
    }
    if (sqlite_err != NULL) {
        sqlite3_free(sqlite_err);
    }
    pthread_mutex_unlock(&db->mutex);
    return ok;
}

/*
 * Return the rowid of the most recent successful INSERT. The
 * mutex guarantees the value matches the last completed statement
 * observed through this handle.
 */
int64_t db_last_insert_id(Database* db) {
    if (db == NULL) {
        return 0;
    }

    pthread_mutex_lock(&db->mutex);
    int64_t rowid = sqlite3_last_insert_rowid(db->handle);
    pthread_mutex_unlock(&db->mutex);
    return rowid;
}

/*
 * Return the number of rows changed by the most recent INSERT,
 * UPDATE, or DELETE. The mutex guarantees the value matches the
 * last completed statement observed through this handle.
 */
int db_changes(Database* db) {
    if (db == NULL) {
        return 0;
    }

    pthread_mutex_lock(&db->mutex);
    int changes = sqlite3_changes(db->handle);
    pthread_mutex_unlock(&db->mutex);
    return changes;
}

/*
 * Return the on-disk path recorded at open time. The pointer
 * belongs to the handle and stays valid until db_close.
 */
const char* db_path(const Database* db) {
    if (db == NULL) {
        return NULL;
    }
    return db->path;
}