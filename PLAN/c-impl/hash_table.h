/*
 * hash_table.h — String-keyed hash table for the Metalsharp C backend
 *
 * WHAT
 *   Declares the opaque HashTable type and its lifecycle, query,
 *   mutation, and iteration entry points. Backed by open addressing
 *   with linear probing, FNV-1a hashing, and tombstone-based
 *   deletion. Keys are NUL-terminated C strings that the table
 *   duplicates on insert and releases on remove/destroy. Values are
 *   opaque caller-owned pointers that the table stores but never
 *   frees; the caller is responsible for value lifetime.
 *
 * IMPORTS
 *   server.h     Shared Metalsharp types and error vocabulary
 *                (transitive so callers can include only this header).
 *   <stdbool.h>  bool, true, false
 *   <stddef.h>   size_t, NULL
 *   <stdint.h>   Fixed-width integer types
 *
 * EXPORTS
 *   HashTable     Opaque handle to a string-keyed hash table.
 *   ht_create     Build a new table with the requested initial capacity.
 *   ht_destroy    Free a table and every key it owns.
 *   ht_insert     Insert or replace a (key, value) pair; the key is
 *                 duplicated into heap memory owned by the table.
 *   ht_get        Look up the value associated with a key (NULL if absent).
 *   ht_remove     Remove an entry from the table.
 *   ht_size       Number of live (non-tombstoned) entries.
 *   HtIterFn      Callback signature used by ht_iterate.
 *   ht_iterate    Visit every live entry in implementation-defined order.
 *
 * SCHEMA
 *   The table is opaque; callers cannot inspect its internal layout.
 *   Capacity is always a power of two so the probe index can be a
 *   bitmask instead of a modulo. The table guarantees:
 *     - Inserting a key that already exists replaces the value and
 *       returns true without re-allocating the key (the existing
 *       strdup'd copy is reused).
 *     - Removing a missing key returns false and is a no-op.
 *     - Values are never freed by the table; the caller retains
 *       ownership. To release values before destroying the table,
 *       walk the entries with ht_iterate.
 *     - Destroying the table frees every still-present key but
 *       leaves values untouched.
 */
#ifndef METALSHARP_HASH_TABLE_H
#define METALSHARP_HASH_TABLE_H

#include "server.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Opaque handle to a string-keyed hash table. The full layout lives
 * in hash_table.c so the slot arrays, probe mask, and tombstone
 * bookkeeping cannot leak into other modules.
 */
typedef struct HashTable HashTable;

/*
 * Create a new hash table with at least `initial_capacity` slots.
 * A value of 0 (or any non-power-of-two) selects an internal power
 * of two no smaller than 16. Returns NULL only when the initial
 * bookkeeping cannot be allocated; once ht_create returns a non-NULL
 * table, subsequent operations either succeed or return a clean
 * error value.
 */
HashTable* ht_create(size_t initial_capacity);

/*
 * Free every key still present in the table, then the slot arrays,
 * then the table itself. Values are not freed. Safe to call with NULL.
 */
void ht_destroy(HashTable* ht);

/*
 * Insert `value` under `key`. The key is duplicated into heap memory
 * owned by the table. If the key already exists, the previous value
 * is replaced and the existing key (a strdup'd copy from the first
 * insert) is reused; no new key allocation happens. Returns true on
 * success and false on invalid arguments, allocation failure, or
 * rehash failure.
 */
bool ht_insert(HashTable* ht, const char* key, void* value);

/*
 * Look up the value for `key`. Returns the stored pointer or NULL
 * if the key is absent. The pointer remains owned by the caller.
 */
void* ht_get(const HashTable* ht, const char* key);

/*
 * Remove the entry for `key`. Frees the strdup'd key copy and leaves
 * a tombstone so probe sequences that cross the slot stay intact.
 * Returns true if a key was removed, false if the key was not found
 * or arguments are invalid.
 */
bool ht_remove(HashTable* ht, const char* key);

/*
 * Number of live (non-tombstoned) entries currently in the table.
 * Returns 0 when called with NULL.
 */
size_t ht_size(const HashTable* ht);

/*
 * Iteration callback. Receives the key (NUL-terminated, owned by
 * the table, valid only for the duration of the call), the stored
 * value pointer, and the user-supplied context.
 */
typedef void (*HtIterFn)(const char* key, void* value, void* ctx);

/*
 * Walk every live entry and invoke `fn`. Iteration order is
 * implementation-defined. Inserting or removing entries during
 * iteration is not supported.
 */
void ht_iterate(HashTable* ht, HtIterFn fn, void* ctx);

#endif /* METALSHARP_HASH_TABLE_H */
