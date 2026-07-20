/*
 * database.h — SQLite wrapper for the Metalsharp C backend
 *
 * WHAT
 *   Thin, thread-safe wrapper around the SQLite C API used by every
 *   backend module that needs durable storage. Hides sqlite3's
 *   connection handle, owns the on-disk path that was used to open
 *   the database, and serialises every operation through a single
 *   pthread mutex so callers do not have to coordinate access. The
 *   wrapper handles journal mode and foreign-key pragmas on open so
 *   every connection boots into the same durable, referentially
 *   consistent state.
 *
 * IMPORTS
 *   "server.h"   Shared error vocabulary and PATH_MAX fallback
 *   <stdbool.h>  bool, true, false
 *   <stddef.h>   size_t, NULL
 *
 * EXPORTS
 *   Database        Opaque database handle
 *   DbRowCallback   Row callback signature used by db_query
 *   db_open         Open or create a SQLite database at the given path
 *   db_close        Close the database and release every resource
 *   db_exec         Execute SQL that produces no result rows
 *   db_query        Execute a SELECT and invoke a callback per row
 *   db_last_insert_id  Return the rowid of the last successful INSERT
 *   db_changes      Return the number of rows changed by the last statement
 *   db_path         Return the on-disk path the handle was opened with
 *
 * SCHEMA
 *   Database is opaque; callers interact with instances only through
 *   the db_* functions declared below. Each call into the handle
 *   acquires an internal pthread mutex so concurrent threads see a
 *   strictly serialised stream of statements and never observe a
 *   half-applied transaction. On open the handle runs PRAGMA
 *   journal_mode=WAL and PRAGMA foreign_keys=ON so every connection
 *   participates in write-ahead logging and enforces referential
 *   integrity. db_exec and db_query return bool: true on success and
 *   false on failure. On failure the out-parameter `*error` is set
 *   to a heap-allocated, NUL-terminated string describing the
 *   problem; the caller owns that string and must release it with
 *   free(). When the call succeeds the implementation leaves
 *   `*error` unchanged. db_query invokes the supplied callback once
 *   per result row; the callback's return value is forwarded to
 *   SQLite unchanged, so a non-zero return aborts the statement and
 *   surfaces back through db_query as a failure. db_last_insert_id
 *   and db_changes take the same mutex so the values they return
 *   are consistent with the most recently completed statement.
 */
#ifndef METALSHARP_DATABASE_H
#define METALSHARP_DATABASE_H

#include "server.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Opaque database handle. The concrete layout lives in database.c
 * so the sqlite3 connection, mutex, and owned path cannot leak into
 * other modules.
 */
typedef struct Database Database;

/*
 * Per-row callback signature used by db_query. The callback is
 * invoked once for every result row with the column count, an
 * array of NUL-terminated column values (NULL where the column
 * was SQL NULL), and an array of NUL-terminated column names.
 * Returning 0 continues iteration; returning a non-zero value
 * aborts the running statement and propagates back through
 * db_query as a failure.
 */
typedef int (*DbRowCallback)(void* ctx, int ncols, char** values, char** names);

/*
 * Open or create a SQLite database at `path`. The path is copied
 * into Database-owned storage, so the caller may free or reuse its
 * buffer once db_open returns. The handle enables WAL journal mode
 * and foreign-key enforcement before returning. Returns NULL on
 * failure (NULL path, allocation failure, sqlite3_open_v2 failure,
 * or any PRAGMA that could not be applied). Every successful open
 * produces a handle that must eventually be released with db_close.
 */
Database* db_open(const char* path);

/*
 * Close the database and release every resource owned by the
 * handle, including the sqlite3 connection, the internal mutex,
 * and the path copy recorded at open time. Safe to call with NULL.
 * After db_close the pointer is invalid and must not be reused.
 */
void db_close(Database* db);

/*
 * Execute `sql` against the database without producing any result
 * rows. On success returns true and leaves `*error` untouched.
 * On failure returns false and, when `error` is non-NULL, writes
 * a heap-allocated, NUL-terminated diagnostic string through
 * `*error` that the caller must release with free(). Passing a
 * NULL `db` or NULL `sql` is reported as a failure.
 */
bool db_exec(Database* db, const char* sql, char** error);

/*
 * Execute `sql` (typically a SELECT) and invoke `cb` once per
 * result row, with `ctx` forwarded as the callback's first
 * argument. `cb` may be NULL, in which case rows are still read
 * and discarded. The callback's return value is forwarded to
 * SQLite, so returning a non-zero value aborts the statement and
 * turns into a db_query failure. On success returns true; on
 * failure returns false and, when `error` is non-NULL, writes a
 * heap-allocated, NUL-terminated diagnostic through `*error` that
 * the caller must release with free().
 */
bool db_query(Database* db, const char* sql, DbRowCallback cb, void* ctx, char** error);

/*
 * Return the rowid of the most recent successful INSERT (or 0 if
 * no insert has ever run on this handle). Takes the database mutex
 * so the returned value is consistent with the last completed
 * statement. Returns 0 when `db` is NULL.
 */
int64_t db_last_insert_id(Database* db);

/*
 * Return the number of rows changed, inserted, or deleted by the
 * last completed INSERT, UPDATE, or DELETE statement. Takes the
 * database mutex so the returned value is consistent with the
 * last completed statement. Returns 0 when `db` is NULL.
 */
int db_changes(Database* db);

/*
 * Return the on-disk path the handle was opened with. The pointer
 * remains owned by the database and stays valid until db_close;
 * callers must not free or modify it. Returns NULL when `db` is
 * NULL.
 */
const char* db_path(const Database* db);

#endif /* METALSHARP_DATABASE_H */