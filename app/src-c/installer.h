#ifndef METALSHARP_INSTALLER_H
#define METALSHARP_INSTALLER_H

#include <stdbool.h>
#include <stddef.h>

/*
 * C-owned runtime validation used by the shipped backend. Release payloads
 * are intentionally not pinned to compiler-output hashes: CI rebuilds M12,
 * while the bundle manifest authenticates the archive as a whole.
 */
bool metalsharp_m12_runtime_complete(const char* root, size_t root_len);
bool metalsharp_m12_runtime_artifact_valid(const char* home, size_t home_len, const char* relative_path,
                                           size_t relative_path_len);

/*
 * Extract the Direct3D Agility SDK x64 payload outside the converted runtime.
 * The destination is replaced only after both required DLLs are present.
 */
bool metalsharp_extract_agility_package(const char* archive, size_t archive_len, const char* destination,
                                        size_t destination_len);

#endif
