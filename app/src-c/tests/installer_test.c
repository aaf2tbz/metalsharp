#include "../installer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* const required[] = {
    "x86_64-windows/d3d10core.dll", "x86_64-windows/d3d11.dll",      "x86_64-windows/d3d12.dll",
    "x86_64-windows/dxgi.dll",      "x86_64-windows/dxgi_dxmt.dll",  "x86_64-windows/winemetal.dll",
    "x86_64-windows/nvapi64.dll",   "x86_64-windows/nvngx.dll",      "x86_64-unix/winemetal.so",
    "x86_64-unix/libc++.1.dylib",   "x86_64-unix/libc++abi.1.dylib", "x86_64-unix/libunwind.1.dylib",
};

static void mkdirs(const char* path) {
    char buffer[4096];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char* cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            mkdir(buffer, 0700);
            *cursor = '/';
        }
    }
    mkdir(buffer, 0700);
}

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--check-home") == 0) {
        const bool ready = metalsharp_graphics_runtimes_complete(argv[2], strlen(argv[2]));
        printf("graphics runtimes: %s\n", ready ? "ready" : "not ready");
        return ready ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "--check-lane") == 0) {
        const bool ready = metalsharp_graphics_runtime_lane_complete(argv[2], strlen(argv[2]));
        printf("graphics runtime lane: %s\n", ready ? "ready" : "not ready");
        return ready ? 0 : 1;
    }
    char temp[] = "/tmp/metalsharp-installer-c.XXXXXX";
    char command[8192];
    assert(mkdtemp(temp) != NULL);

    const unsigned char scan_fixture[] = {'x', 'x', 'D', '3', 'D', '1', '2', 'S', 'D',
                                          'K', 'V', 'e', 'r', 's', 'i', 'o', 'n', 'x'};
    size_t marker_offset = 0;
    assert(metalsharp_find_bytes(scan_fixture, sizeof(scan_fixture), "D3D12SDKVersion", 15, &marker_offset));
    assert(marker_offset == 2);
    assert(!metalsharp_find_bytes(scan_fixture, sizeof(scan_fixture), "missing", 7, NULL));
    assert(!metalsharp_find_bytes(scan_fixture, sizeof(scan_fixture), "", 0, NULL));

    for (size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", temp, required[index]);
        char parent[4096];
        snprintf(parent, sizeof(parent), "%s", path);
        *strrchr(parent, '/') = '\0';
        mkdirs(parent);
        FILE* file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite("rebuilt", 1, 7, file) == 7);
        assert(fclose(file) == 0);
    }

    /* Presence alone must never authenticate a graphics surface. */
    assert(!metalsharp_m12_runtime_complete(temp, strlen(temp)));
    assert(!metalsharp_graphics_runtimes_complete(temp, strlen(temp)));
    assert(setenv("METALSHARP_HOME", temp, 1) == 0);
    assert(!metalsharp_graphics_runtime_lane_complete("/legacy/runtime/dxmt", 20));
    assert(unsetenv("METALSHARP_HOME") == 0);
    char missing[4096];
    snprintf(missing, sizeof(missing), "%s/%s", temp, required[0]);
    assert(unlink(missing) == 0);
    assert(!metalsharp_m12_runtime_complete(temp, strlen(temp)));
    assert(!metalsharp_m12_runtime_artifact_valid(temp, strlen(temp), "../../escape", 12));

    char fixture[4096];
    char fixture_bin[4096];
    char archive[4096];
    char agility[4096];
    char extracting[4096];
    snprintf(fixture, sizeof(fixture), "%s-fixture", temp);
    snprintf(fixture_bin, sizeof(fixture_bin), "%s/build/native/bin/x64", fixture);
    snprintf(archive, sizeof(archive), "%s-agility.nupkg", temp);
    snprintf(agility, sizeof(agility), "%s-agility", temp);
    snprintf(extracting, sizeof(extracting), "%s.extracting", agility);
    mkdirs(fixture_bin);
    const char* const agility_files[] = {"D3D12Core.dll", "d3d12SDKLayers.dll"};
    for (size_t index = 0; index < sizeof(agility_files) / sizeof(agility_files[0]); ++index) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", fixture_bin, agility_files[index]);
        FILE* file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite("agility", 1, 7, file) == 7);
        assert(fclose(file) == 0);
    }
    mkdirs(extracting);
    char stale[4096];
    snprintf(stale, sizeof(stale), "%s/stale", extracting);
    FILE* stale_file = fopen(stale, "wb");
    assert(stale_file != NULL);
    assert(fwrite("stale", 1, 5, stale_file) == 5);
    assert(fclose(stale_file) == 0);

    snprintf(command, sizeof(command), "cd '%s' && /usr/bin/zip -qr '%s' build", fixture, archive);
    assert(system(command) == 0);
    assert(metalsharp_extract_agility_package(archive, strlen(archive), agility, strlen(agility)));
    char installed[4096];
    snprintf(installed, sizeof(installed), "%s/build/native/bin/x64/D3D12Core.dll", agility);
    assert(access(installed, R_OK) == 0);
    assert(access(stale, F_OK) != 0);
    assert(!metalsharp_extract_agility_package("/missing", 8, agility, strlen(agility)));

    char ordinary_game[4096];
    char agility_game[4096];
    snprintf(ordinary_game, sizeof(ordinary_game), "%s/ordinary.exe", temp);
    snprintf(agility_game, sizeof(agility_game), "%s/agility.exe", temp);
    FILE* ordinary = fopen(ordinary_game, "wb");
    assert(ordinary != NULL);
    assert(fwrite("ordinary D3D12 game", 1, 19, ordinary) == 19);
    assert(fclose(ordinary) == 0);
    assert(!metalsharp_game_requires_agility(ordinary_game, strlen(ordinary_game)));
    FILE* agility_executable = fopen(agility_game, "wb");
    assert(agility_executable != NULL);
    assert(fwrite(scan_fixture, 1, sizeof(scan_fixture), agility_executable) == sizeof(scan_fixture));
    assert(fclose(agility_executable) == 0);
    assert(metalsharp_game_requires_agility(agility_game, strlen(agility_game)));
    assert(metalsharp_game_requires_agility(temp, strlen(temp)));

    snprintf(command, sizeof(command), "/bin/rm -rf -- '%s' '%s' '%s' '%s'", temp, fixture, archive, agility);
    assert(system(command) == 0);
    puts("C installer runtime validation passed.");
    return 0;
}
