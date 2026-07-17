#ifndef METALSHARP_INSTALLER_H
#define METALSHARP_INSTALLER_H

#include <stdbool.h>
#include <stddef.h>

/*
 * C-owned runtime validation and migration used by the shipped backend.
 * The frozen PR-230 surface is pinned by identity, size, and SHA-256; a
 * successful check also proves both installed lanes and Wine mirrors agree.
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

/*
 * Converted slice-window searches are prohibitively slow on large PE images.
 * Keep the byte scan and the Agility opt-in decision in maintained C.
 */
bool metalsharp_find_bytes(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len,
                           size_t* offset);
bool metalsharp_game_requires_agility(const char* game_path, size_t game_path_len);

#endif
