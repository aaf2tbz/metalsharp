#ifndef METALSHARP_RUNTIME_SURFACE_H
#define METALSHARP_RUNTIME_SURFACE_H

#include <stdbool.h>
#include <stddef.h>

#define METALSHARP_DXMT_SURFACE_ID "metalsharp-dxmt-pr230-20260716-v1"

/*
 * Verify the frozen PR-230 x64 surface and, when requested, promote it from
 * dxmt_m12 into both x64 lanes and Wine's loader mirrors. The i386 lane is
 * copied from the installed dxmt tree and verified before and after commit.
 */
bool metalsharp_dxmt_surface_current(const char* m12_root, size_t m12_root_len);
bool metalsharp_reconcile_dxmt_surface(const char* m12_root, size_t m12_root_len);
bool metalsharp_dxmt_artifact_current(const char* m12_root, size_t m12_root_len, const char* relative_path,
                                      size_t relative_path_len);

#endif
