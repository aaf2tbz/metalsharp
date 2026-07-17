#ifndef METALSHARP_RUNTIME_SURFACE_H
#define METALSHARP_RUNTIME_SURFACE_H

#include <stdbool.h>
#include <stddef.h>

#define METALSHARP_DXMT_LEGACY_SURFACE_ID "metalsharp-dxmt-0.55.1-v1"
#define METALSHARP_DXMT_M12_SURFACE_ID "metalsharp-dxmt-m12-pr230-v2"
#define METALSHARP_DXMT_SURFACE_ID METALSHARP_DXMT_M12_SURFACE_ID

/*
 * The M12 v2 contract is independent from baseline DXMT. Reconciliation writes
 * only the v2 receipt; it must never promote M12 files into legacy dxmt.
 */
bool metalsharp_m12_dxmt_surface_current(const char* root, size_t root_len);
bool metalsharp_dxmt_surface_current(const char* m12_root, size_t m12_root_len);
bool metalsharp_reconcile_dxmt_surface(const char* m12_root, size_t m12_root_len);
bool metalsharp_dxmt_artifact_current(const char* m12_root, size_t m12_root_len, const char* relative_path,
                                      size_t relative_path_len);

#endif
