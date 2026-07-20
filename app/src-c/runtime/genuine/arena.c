/*
 * arena.c — Bump-pointer arena allocator for the Metalsharp C backend
 *
 * Linked-list-of-chunks design:
 *   - The arena owns a singly linked list of chunks, each malloc'd
 *     once at creation or when the current chunk fills.
 *   - arena_alloc bumps a pointer inside the head chunk. When the
 *     request would overflow, a new chunk is allocated and linked.
 *   - arena_reset rewinds every chunk to offset 0 without freeing
 *     memory, so the next burst of allocations reuses the same RAM.
 *   - arena_destroy frees every chunk and the arena itself.
 *
 * All allocations are 8-byte aligned and zero-initialized.
 */

#include "arena.h"

#include <stdint.h>
#include <string.h>

/* Default chunk size when the caller passes 0 to arena_create. */
#define ARENA_DEFAULT_CHUNK_SIZE (64 * 1024)

/* All allocations are aligned to this boundary. */
#define ARENA_ALIGNMENT 8

/*
 * Internal chunk header. The payload lives in the same allocation,
 * one byte past the header, so the chunk owns its bookkeeping.
 */
typedef struct ArenaChunk {
    struct ArenaChunk* next;
    size_t capacity; /* Total payload bytes available. */
    size_t used;     /* Payload bytes already handed out. */
    /* Payload bytes follow immediately after this struct. */
} ArenaChunk;

/*
 * Concrete Arena layout. Kept private so external callers cannot
 * reach in and corrupt the chunk list.
 */
struct Arena {
    ArenaChunk* head;    /* First chunk; reset rewinds here. */
    ArenaChunk* current; /* Chunk currently being bumped into. */
    size_t default_chunk_size;
};

/* Round x up to the next multiple of ARENA_ALIGNMENT. */
static size_t arena_align_up(size_t x) {
    const size_t mask = ARENA_ALIGNMENT - 1;
    return (x + mask) & ~mask;
}

/* Allocate one chunk of at least `payload_size` bytes and link it. */
static ArenaChunk* arena_chunk_create(size_t payload_size) {
    ArenaChunk* chunk = malloc(sizeof(ArenaChunk) + payload_size);
    if (chunk == NULL) {
        return NULL;
    }
    chunk->next = NULL;
    chunk->capacity = payload_size;
    chunk->used = 0;
    return chunk;
}

/*
 * Create a new arena whose first chunk is `chunk_size` bytes.
 * A chunk_size of 0 selects an internal default. Returns NULL
 * only if the initial chunk cannot be malloc'd.
 */
Arena* arena_create(size_t chunk_size) {
    if (chunk_size == 0) {
        chunk_size = ARENA_DEFAULT_CHUNK_SIZE;
    }

    Arena* arena = malloc(sizeof(Arena));
    if (arena == NULL) {
        return NULL;
    }

    ArenaChunk* chunk = arena_chunk_create(chunk_size);
    if (chunk == NULL) {
        free(arena);
        return NULL;
    }

    arena->head = chunk;
    arena->current = chunk;
    arena->default_chunk_size = chunk_size;
    return arena;
}

/*
 * Allocate `size` bytes from the arena. Memory is zero-initialized
 * and 8-byte aligned. Returns NULL when `arena` is NULL; otherwise
 * aborts on allocation failure.
 */
void* arena_alloc(Arena* arena, size_t size) {
    if (arena == NULL) {
        return NULL;
    }

    const size_t aligned = arena_align_up(size);

    /* Try to fit the request into the current chunk first. */
    if (arena->current->used + aligned <= arena->current->capacity) {
        char* base = (char*)(arena->current + 1);
        void* ptr = base + arena->current->used;
        arena->current->used += aligned;
        memset(ptr, 0, size);
        return ptr;
    }

    /* Otherwise, pick a chunk size: the default, or the requested
     * payload if it exceeds the default. Either way, round up. */
    size_t payload = arena->default_chunk_size;
    if (aligned > payload) {
        payload = aligned;
    }

    ArenaChunk* chunk = arena_chunk_create(payload);
    if (chunk == NULL) {
        abort();
    }

    /* Link the new chunk after the current one so reset still walks
     * the entire list in insertion order. */
    chunk->next = arena->current->next;
    arena->current->next = chunk;
    arena->current = chunk;

    char* base = (char*)(chunk + 1);
    void* ptr = base + chunk->used;
    chunk->used += aligned;
    memset(ptr, 0, size);
    return ptr;
}

/*
 * Duplicate `str` into the arena. Returns NULL if either argument
 * is NULL; otherwise aborts on allocation failure. The returned
 * pointer is owned by the arena and dies with it.
 */
char* arena_strdup(Arena* arena, const char* str) {
    if (arena == NULL || str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char* copy = arena_alloc(arena, len + 1);
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

/*
 * Rewind every chunk to offset 0 so the next round of allocations
 * reuses the same memory. The chunk list itself is preserved.
 */
void arena_reset(Arena* arena) {
    if (arena == NULL) {
        return;
    }

    ArenaChunk* chunk = arena->head;
    while (chunk != NULL) {
        chunk->used = 0;
        chunk = chunk->next;
    }
    arena->current = arena->head;
}

/*
 * Free every chunk in order, then free the arena itself. Safe with
 * a NULL pointer so callers can clean up defensively.
 */
void arena_destroy(Arena* arena) {
    if (arena == NULL) {
        return;
    }

    ArenaChunk* chunk = arena->head;
    while (chunk != NULL) {
        ArenaChunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
    free(arena);
}