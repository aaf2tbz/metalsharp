#include "../bottles.h"
#include "../runtime_surface.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_required(const char* path) {
    assert(mkdir(path, 0700) == 0 || access(path, F_OK) == 0);
}

static void write_text(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static void read_text(const char* path, char* output, size_t size) {
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    const size_t count = fread(output, 1, size - 1, file);
    assert(!ferror(file));
    output[count] = '\0';
    assert(fclose(file) == 0);
}

static void save_manifest(const char* bottles, const char* profile, const char* prefix, const char* game,
                          char manifest[PATH_MAX]) {
    char directory[PATH_MAX], json[PATH_MAX * 3];
    snprintf(directory, sizeof(directory), "%s/test_%s", bottles, profile);
    mkdir_required(directory);
    snprintf(manifest, PATH_MAX, "%s/bottle.json", directory);
    const int count = snprintf(
        json, sizeof(json),
        "{\n  \"id\": \"test_%s\",\n  \"steam_app_id\": 1245620,\n  \"prefix_path\": \"%s\",\n"
        "  \"runtime_profile\": \"%s\",\n  \"game_install_path\": %s%s%s,\n  \"health\": \"ready\"\n}\n",
        profile, prefix, profile, game == NULL ? "" : "\"", game == NULL ? "null" : game, game == NULL ? "" : "\"");
    assert(count > 0 && count < (int)sizeof(json));
    write_text(manifest, json);
}

static void assert_contains(const char* path, const char* expected) {
    char contents[8192];
    read_text(path, contents, sizeof(contents));
    assert(strstr(contents, expected) != NULL);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const char* home = argv[1];
    const char* m12 = argv[2];
    assert(setenv("METALSHARP_HOME", home, 1) == 0);
    assert(metalsharp_reconcile_dxmt_surface(m12, strlen(m12)));

    char bottles[PATH_MAX], prefix[PATH_MAX], system32[PATH_MAX], syswow64[PATH_MAX], game[PATH_MAX];
    snprintf(bottles, sizeof(bottles), "%s/bottles", home);
    snprintf(prefix, sizeof(prefix), "%s/test-prefix", home);
    snprintf(system32, sizeof(system32), "%s/drive_c/windows/system32", prefix);
    snprintf(syswow64, sizeof(syswow64), "%s/drive_c/windows/syswow64", prefix);
    snprintf(game, sizeof(game), "%s/test-game", home);
    mkdir_required(bottles);
    mkdir_required(prefix);
    mkdir_required(game);

    char manifest[PATH_MAX], path[PATH_MAX];
    save_manifest(bottles, "m12", prefix, game, manifest);
    assert(metalsharp_reconcile_bottle_manifest(manifest, strlen(manifest)));
    snprintf(path, sizeof(path), "%s/d3d12.dll", system32);
    assert(access(path, R_OK) == 0);
    snprintf(path, sizeof(path), "%s/d3d12.dll", game);
    assert(access(path, R_OK) == 0);
    snprintf(path, sizeof(path), "%s/steam_appid.txt", game);
    assert_contains(path, "1245620");
    snprintf(path, sizeof(path), "%s/test_m12/dxmt-deployment.json", bottles);
    assert_contains(path, "\"surface_id\": \"" METALSHARP_DXMT_SURFACE_ID "\"");
    assert_contains(path, "\"status\": \"ready\"");

    save_manifest(bottles, "m11", prefix, game, manifest);
    assert(metalsharp_reconcile_bottle_manifest(manifest, strlen(manifest)));
    snprintf(path, sizeof(path), "%s/d3d12.dll", system32);
    assert(access(path, F_OK) != 0);
    snprintf(path, sizeof(path), "%s/d3d12.dll", game);
    assert(access(path, F_OK) != 0);

    const char* remaining[] = {"m10", "m11_32", "m10_32"};
    for (size_t i = 0; i < sizeof(remaining) / sizeof(remaining[0]); ++i) {
        save_manifest(bottles, remaining[i], prefix, NULL, manifest);
        assert(metalsharp_reconcile_bottle_manifest(manifest, strlen(manifest)));
        snprintf(path, sizeof(path), "%s/test_%s/dxmt-deployment.json", bottles, remaining[i]);
        assert_contains(path, "\"status\": \"ready\"");
    }
    snprintf(path, sizeof(path), "%s/d3d11.dll", syswow64);
    assert(access(path, R_OK) == 0);

    save_manifest(bottles, "m11", prefix, game, manifest);
    snprintf(path, sizeof(path), "%s/d3d11.dll", system32);
    write_text(path, "preserve-me");
    assert(setenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION", "1", 1) == 0);
    assert(!metalsharp_reconcile_bottle_manifest(manifest, strlen(manifest)));
    assert(unsetenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION") == 0);
    assert_contains(path, "preserve-me");
    assert_contains(manifest, "\"health\": \"needs_repair\"");

    puts("DXMT bottle deployment, receipt, profile subset, Steam staging, and rollback tests passed.");
    return 0;
}
