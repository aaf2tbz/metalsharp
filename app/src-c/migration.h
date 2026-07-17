#ifndef METALSHARP_MIGRATION_H
#define METALSHARP_MIGRATION_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t discovered;
    size_t managed;
    size_t refreshed;
    size_t skipped;
    size_t failed;
} MetalsharpMigrationBottleSummary;

/*
 * Reconcile every preserved DXMT bottle against the currently installed
 * surface. Bottle manifests and prefixes remain in place; only the
 * profile-owned DLL set, deployment receipt, and health are refreshed.
 */
bool metalsharp_migration_refresh_bottles(const char* home, size_t home_len, MetalsharpMigrationBottleSummary* summary);

/* Narrow no-argument adapter used by the converted migration state machine. */
bool metalsharp_migration_refresh_saved_bottles(void);

#endif
