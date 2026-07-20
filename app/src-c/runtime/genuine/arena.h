/*
 * arena.h — Bump-pointer arena allocator for the Metalsharp C backend
 *
 * WHAT
 *   Declares the Arena type and its lifecycle operations. An arena
 *   hands out zero-initialized memory in O(1) by bumping a pointer
 *   inside a chunk; when a chunk fills up, a fresh chunk is malloc'd
 *   and linked. arena_reset reuses the original chunks without
 *   freeing them; arena_destroy releases everything.
 *
 * IMPORTS
 *   server.h    MetalsharpResponse and shared error vocabulary
 *               (kept transitive so callers only need arena.h)
 *   <stddef.h>  size_t, NULL
 *   <stdlib.h>  malloc, free
 *
 * EXPORTS
 *   Arena             Opaque arena handle
 *   arena_create      Build a new arena whose first chunk has the
 *                     given size (0 picks a sensible default).
 *   arena_alloc       Allocate `size` bytes, zero-initialized.
 *   arena_strdup      Duplicate a NUL-terminated string into the arena.
 *   arena_reset       Rewind to the start of the first chunk; memory
 *                     is reused, not freed.
 *   arena_destroy     Free every chunk and the arena itself.
 *
 * SCHEMA
 *   Arena is opaque; callers must use the accessors above. All
 *   allocations are 8-byte aligned so the arena plays well with
 *   any C struct. arena_alloc and arena_strdup abort on allocation
 *   failure (long-lived process, no recovery path); callers should
 *   not assume allocations can return NULL.
 */
#ifndef METALSHARP_ARENA_H
#define METALSHARP_ARENA_H

#include "server.h"
#include <stddef.h>
#include <stdlib.h>

/*
 * Opaque arena type. The internal layout lives in arena.c so the
 * bump-pointer and chunk list cannot leak into other modules.
 */
typedef struct Arena Arena;

/*
 * Create a new arena whose first chunk is `chunk_size` bytes.
 * A chunk_size of 0 selects an internal default (64 KiB). Returns
 * NULL only when the initial chunk cannot be malloc'd.
 */
Arena* arena_create(size_t chunk_size);

/*
 * Allocate `size` bytes from the arena. Memory is zero-initialized.
 * Returns NULL if `arena` is NULL; otherwise aborts on failure.
 */
void* arena_alloc(Arena* arena, size_t size);

/*
 * Duplicate the NUL-terminated string `str` into the arena. Returns
 * NULL when either argument is NULL; otherwise aborts on failure.
 */
char* arena_strdup(Arena* arena, const char* str);

/*
 * Rewind every chunk to its starting offset so subsequent
 * allocations reuse the same memory. O(chunk_count).
 */
void arena_reset(Arena* arena);

/*
 * Free every chunk and the arena itself. Safe to call with NULL.
 */
void arena_destroy(Arena* arena);

#endif /* METALSHARP_ARENA_H */