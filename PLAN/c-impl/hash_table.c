/*
 * hash_table.c — String-keyed open-addressing hash table
 *
 * Implementation of the HashTable API declared in hash_table.h.
 * Uses linear probing, FNV-1a 64-bit hashing, and tombstone
 * deletion. Capacity is always a power of two so the slot index
 * can be computed as (hash & (capacity - 1)) without a modulo.
 * The load factor threshold is 0.75; whenever an insert would push
 * (size + tombstones + 1) above 75% of capacity, the table is
 * rehashed to double capacity. Tombstones are counted toward the
 * load factor so a churn-heavy workload does not degrade into a
 * slow linear scan.
 *
 * Keys are duplicated into heap memory on insert and released on
 * remove/destroy. Values are opaque caller-owned pointers and are
 * never freed by the table.
 */
#include "hash_table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Sentinel stored in keys[] where a real key used to live. The
 * pointer value is never dereferenced; it is only compared by
 * identity against NULL and HT_TOMBSTONE itself. Casting through
 * uintptr_t avoids implementation-defined warnings on platforms
 * that warn when converting an integer literal to a pointer.
 */
#define HT_TOMBSTONE ((char*)(uintptr_t)1)

/*
 * Minimum and default capacity. Powers of two keep the
 * (capacity - 1) bitmask trick valid for the probe sequence.
 */
#define HT_MIN_CAPACITY ((size_t)16)

/*
 * Load factor threshold expressed as numerator/denominator so the
 * comparison can stay in integer arithmetic. 0.75 == 3/4.
 */
#define HT_LOAD_NUM ((size_t)3)
#define HT_LOAD_DEN ((size_t)4)

/* Internal layout. Defined here so callers cannot reach into it. */
struct HashTable {
    char** keys;       /* NULL == empty, HT_TOMBSTONE == removed, else owned strdup */
    void** values;     /* meaningful only when keys[i] is a real strdup'd pointer */
    size_t capacity;   /* always a power of two */
    size_t size;       /* number of occupied slots */
    size_t tombstones; /* number of tombstone slots */
};

/* ---------- internal helpers ---------- */

/*
 * Round `v` up to the next power of two no smaller than the minimum
 * capacity. Used by ht_create to normalize initial_capacity and by
 * ht_rehash to double capacity safely.
 */
static size_t next_pow2(size_t v) {
    size_t p = HT_MIN_CAPACITY;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

/*
 * FNV-1a 64-bit hash. The offset basis and prime match the spec
 * exactly; iterating over the bytes of the NUL-terminated string
 * (cast through unsigned char so the sign-extension rule in C's
 * integer promotions cannot bite) yields the canonical FNV-1a
 * digest used everywhere else in the project.
 */
static uint64_t fnv1a(const char* s) {
    uint64_t h = 14695981039346656037ULL;
    for (const unsigned char* p = (const unsigned char*)s; *p != 0; p++) {
        h ^= (uint64_t)*p;
        h *= 1099511628211ULL;
    }
    return h;
}

/*
 * Local strdup so the implementation does not depend on POSIX
 * feature test macros for `strdup` (it is not in C11). Uses
 * malloc + memcpy so allocation failures are reported cleanly
 * through NULL.
 */
static char* ht_strdup(const char* s) {
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

/*
 * Walk the probe chain for `key`. Returns the slot index to use
 * for an insert (the first empty slot encountered, or the first
 * tombstone if one was encountered before reaching that empty
 * slot). If the key already exists, *found is set to true and the
 * index of that slot is returned. The caller guarantees `ht` and
 * `key` are non-NULL and that `ht` has at least one empty slot,
 * which is always true after the load-factor check in ht_insert.
 */
static size_t ht_probe(const HashTable* ht, const char* key, bool* found) {
    size_t mask = ht->capacity - 1;
    uint64_t start = fnv1a(key);
    size_t i = (size_t)(start & (uint64_t)mask);
    size_t tomb = SIZE_MAX;
    *found = false;
    for (;;) {
        char* slot = ht->keys[i];
        if (slot == NULL) {
            return tomb != SIZE_MAX ? tomb : i;
        }
        if (slot == HT_TOMBSTONE) {
            if (tomb == SIZE_MAX) {
                tomb = i;
            }
        } else if (strcmp(slot, key) == 0) {
            *found = true;
            return i;
        }
        i = (i + 1) & mask;
    }
}

/*
 * Rebuild the table into fresh arrays of `new_capacity` slots.
 * Owns the new arrays exclusively on success; on failure the
 * existing table is left untouched and false is returned.
 * Tombstones are dropped during the rebuild so the load factor
 * drops back toward size/new_capacity. Key string ownership is
 * transferred from the old arrays to the new ones; the old
 * arrays themselves are then freed.
 */
static bool ht_rehash(HashTable* ht, size_t new_capacity) {
    char** new_keys = calloc(new_capacity, sizeof(char*));
    void** new_values = calloc(new_capacity, sizeof(void*));
    if (new_keys == NULL || new_values == NULL) {
        free(new_keys);
        free(new_values);
        return false;
    }
    char** old_keys = ht->keys;
    void** old_values = ht->values;
    size_t old_capacity = ht->capacity;
    uint64_t new_mask = (uint64_t)(new_capacity - 1);

    ht->keys = new_keys;
    ht->values = new_values;
    ht->capacity = new_capacity;
    ht->size = 0;
    ht->tombstones = 0;

    for (size_t j = 0; j < old_capacity; j++) {
        char* k = old_keys[j];
        if (k != NULL && k != HT_TOMBSTONE) {
            uint64_t h = fnv1a(k);
            size_t i = (size_t)(h & new_mask);
            while (new_keys[i] != NULL) {
                i = (i + 1) & (new_capacity - 1);
            }
            new_keys[i] = k;
            new_values[i] = old_values[j];
            ht->size++;
        }
    }
    free(old_keys);
    free(old_values);
    return true;
}

/* ---------- public API ---------- */

HashTable* ht_create(size_t initial_capacity) {
    HashTable* ht = calloc(1, sizeof(HashTable));
    if (ht == NULL) {
        return NULL;
    }
    size_t cap = next_pow2(initial_capacity);
    ht->keys = calloc(cap, sizeof(char*));
    ht->values = calloc(cap, sizeof(void*));
    if (ht->keys == NULL || ht->values == NULL) {
        free(ht->keys);
        free(ht->values);
        free(ht);
        return NULL;
    }
    ht->capacity = cap;
    ht->size = 0;
    ht->tombstones = 0;
    return ht;
}

void ht_destroy(HashTable* ht) {
    if (ht == NULL) {
        return;
    }
    for (size_t i = 0; i < ht->capacity; i++) {
        char* k = ht->keys[i];
        if (k != NULL && k != HT_TOMBSTONE) {
            free(k);
        }
    }
    free(ht->keys);
    free(ht->values);
    free(ht);
}

bool ht_insert(HashTable* ht, const char* key, void* value) {
    if (ht == NULL || key == NULL) {
        return false;
    }

    bool found = false;
    size_t i = ht_probe(ht, key, &found);
    if (found) {
        ht->values[i] = value;
        return true;
    }

    /* Rehash first if this insert would push us above 0.75 load. */
    size_t load = ht->size + ht->tombstones + 1;
    if (load * HT_LOAD_DEN > ht->capacity * HT_LOAD_NUM) {
        if (!ht_rehash(ht, ht->capacity * 2)) {
            return false;
        }
        /* After rehash the table has no tombstones and no entry for
         * this key, so a fresh probe picks the correct slot. */
        i = ht_probe(ht, key, &found);
    }

    char* copy = ht_strdup(key);
    if (copy == NULL) {
        return false;
    }
    ht->keys[i] = copy;
    ht->values[i] = value;
    ht->size++;
    return true;
}

void* ht_get(const HashTable* ht, const char* key) {
    if (ht == NULL || key == NULL) {
        return NULL;
    }
    bool found = false;
    size_t i = ht_probe(ht, key, &found);
    return found ? ht->values[i] : NULL;
}

bool ht_remove(HashTable* ht, const char* key) {
    if (ht == NULL || key == NULL) {
        return false;
    }
    bool found = false;
    size_t i = ht_probe(ht, key, &found);
    if (!found) {
        return false;
    }
    free(ht->keys[i]);
    ht->keys[i] = HT_TOMBSTONE;
    ht->values[i] = NULL;
    ht->size--;
    ht->tombstones++;
    return true;
}

size_t ht_size(const HashTable* ht) {
    return ht == NULL ? 0 : ht->size;
}

void ht_iterate(HashTable* ht, HtIterFn fn, void* ctx) {
    if (ht == NULL || fn == NULL) {
        return;
    }
    for (size_t i = 0; i < ht->capacity; i++) {
        char* k = ht->keys[i];
        if (k != NULL && k != HT_TOMBSTONE) {
            fn(k, ht->values[i], ctx);
        }
    }
}
