#include "../bottles.h"
#include "../launcher.h"
#include "../migration.h"
#include "../runtime_surface.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char* profile;
    unsigned char pipeline;
    unsigned int appid;
    const char* executable;
    const char* route;
    const char* required_dll;
    bool includes_d3d12;
} ProfileCase;

static const ProfileCase profiles[] = {
    {"m12", 6, 1245620, "start_protected_game.exe", "system32", "d3d12.dll", true},
    {"m11", 4, 312520, "RainWorld.exe", "system32", "d3d11.dll", false},
    {"m10", 2, 910010, "m10-test.exe", "system32", "d3d10core.dll", false},
    {"m11_32", 5, 9101132, "m11-32-test.exe", "syswow64", "d3d11.dll", false},
    {"m10_32", 3, 9101032, "m10-32-test.exe", "syswow64", "d3d10core.dll", false},
};

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

static void copy_file(const char* source, const char* target) {
    FILE* input = fopen(source, "rb");
    FILE* output = fopen(target, "wb");
    char buffer[65536];
    assert(input != NULL && output != NULL);
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0)
        assert(fwrite(buffer, 1, count, output) == count);
    assert(!ferror(input));
    assert(fclose(input) == 0);
    assert(fclose(output) == 0);
}

static void replace_text(const char* path, const char* before, const char* after) {
    char contents[16384], updated[16384];
    read_text(path, contents, sizeof(contents));
    char* match = strstr(contents, before);
    assert(match != NULL);
    *match = '\0';
    const int count = snprintf(updated, sizeof(updated), "%s%s%s", contents, after, match + strlen(before));
    assert(count > 0 && count < (int)sizeof(updated));
    write_text(path, updated);
}

static void save_manifest(const char* bottles, const ProfileCase* profile, const char* prefix, const char* game,
                          char manifest[PATH_MAX]) {
    char directory[PATH_MAX], json[PATH_MAX * 3];
    snprintf(directory, sizeof(directory), "%s/steam_%u", bottles, profile->appid);
    mkdir_required(directory);
    snprintf(manifest, PATH_MAX, "%s/bottle.json", directory);
    const int count =
        snprintf(json, sizeof(json),
                 "{\n  \"id\": \"steam_%u\",\n  \"steam_app_id\": %u,\n  \"prefix_path\": \"%s\",\n"
                 "  \"runtime_profile\": \"%s\",\n  \"game_install_path\": \"%s\",\n  \"health\": \"ready\"\n}\n",
                 profile->appid, profile->appid, prefix, profile->profile, game);
    assert(count > 0 && count < (int)sizeof(json));
    write_text(manifest, json);
}

static void assert_contains(const char* path, const char* expected) {
    char contents[16384];
    read_text(path, contents, sizeof(contents));
    assert(strstr(contents, expected) != NULL);
}

static bool preflight(const ProfileCase* profile, const char* prefix, const char* game) {
    return metalsharp_launcher_preflight(profile->appid, profile->pipeline, prefix, strlen(prefix), game, strlen(game),
                                         profile->executable, strlen(profile->executable));
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const char* home = argv[1];
    const char* m12 = argv[2];
    assert(setenv("METALSHARP_HOME", home, 1) == 0);
    assert(metalsharp_reconcile_dxmt_surface(m12, strlen(m12)));

    char bottles[PATH_MAX], wine_dir[PATH_MAX], wine[PATH_MAX];
    snprintf(bottles, sizeof(bottles), "%s/bottles", home);
    snprintf(wine_dir, sizeof(wine_dir), "%s/runtime/wine/bin", home);
    snprintf(wine, sizeof(wine), "%s/metalsharp-wine", wine_dir);
    mkdir_required(bottles);
    mkdir_required(wine_dir);
    write_text(wine, "#!/bin/sh\n");
    assert(chmod(wine, 0700) == 0);

    char manifests[sizeof(profiles) / sizeof(profiles[0])][PATH_MAX];
    char prefixes[sizeof(profiles) / sizeof(profiles[0])][PATH_MAX];
    char games[sizeof(profiles) / sizeof(profiles[0])][PATH_MAX];
    char path[PATH_MAX];

    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        const ProfileCase* profile = &profiles[i];
        snprintf(prefixes[i], PATH_MAX, "%s/prefix-%s", home, profile->profile);
        snprintf(games[i], PATH_MAX, "%s/game-%s", home, profile->profile);
        mkdir_required(prefixes[i]);
        mkdir_required(games[i]);
        snprintf(path, sizeof(path), "%s/%s", games[i], profile->executable);
        write_text(path, "direct-launch-test");
        if (strcmp(profile->profile, "m12") == 0) {
            snprintf(path, sizeof(path), "%s/eldenring.exe", games[i]);
            write_text(path, "elden-ring");
        }
        save_manifest(bottles, profile, prefixes[i], games[i], manifests[i]);
        assert(metalsharp_reconcile_bottle_manifest(manifests[i], strlen(manifests[i])));

        snprintf(path, sizeof(path), "%s/drive_c/windows/%s/%s", prefixes[i], profile->route, profile->required_dll);
        assert(access(path, R_OK) == 0);
        snprintf(path, sizeof(path), "%s/%s", games[i], profile->required_dll);
        assert(access(path, R_OK) == 0);
        snprintf(path, sizeof(path), "%s/steam_%u/dxmt-deployment.json", bottles, profile->appid);
        assert_contains(path, "\"status\": \"ready\"");
        if (strcmp(profile->profile, "m12") == 0) {
            snprintf(path, sizeof(path), "%s/steam_%u/m12-dry-run.json", bottles, profile->appid);
            assert_contains(path, "\"dry_run\": true");
            assert_contains(path, "\"unix_sidecars\"");
            assert_contains(path, "\"winemetal.so\"");
            /* Match PR #230 launch ordering: substitute the protected
               launcher before validating its direct-executable launch. */
            assert(metalsharp_launcher_prepare_eac(profile->appid, games[i], strlen(games[i])));
        }
        assert(preflight(profile, prefixes[i], games[i]));
        snprintf(path, sizeof(path), "%s/steam_%u/dxmt-launch-preflight.json", bottles, profile->appid);
        assert_contains(path, "\"launch_mode\": \"direct_executable\"");
        assert_contains(path, "\"steam_client\": \"background\"");

        snprintf(path, sizeof(path), "%s/drive_c/windows/%s/d3d12.dll", prefixes[i], profile->route);
        assert((access(path, F_OK) == 0) == profile->includes_d3d12);
        snprintf(path, sizeof(path), "%s/d3d12.dll", games[i]);
        assert((access(path, F_OK) == 0) == profile->includes_d3d12);
    }

    /* Non-M12 bottles must reject the M12 payload and restore from baseline
       dxmt; their save path must stay usable if the isolated lane is absent. */
    char m12_d3d11[PATH_MAX];
    snprintf(m12_d3d11, sizeof(m12_d3d11), "%s/x86_64-windows/d3d11.dll", m12);
    char m12_d3d11_backup[PATH_MAX];
    snprintf(m12_d3d11_backup, sizeof(m12_d3d11_backup), "%s.baseline-test", m12_d3d11);
    snprintf(path, sizeof(path), "%s/d3d11.dll", games[1]);
    copy_file(m12_d3d11, path);
    assert(!preflight(&profiles[1], prefixes[1], games[1]));
    assert(metalsharp_reconcile_bottle_manifest(manifests[1], strlen(manifests[1])));
    assert(preflight(&profiles[1], prefixes[1], games[1]));

    assert(rename(m12_d3d11, m12_d3d11_backup) == 0);
    assert(metalsharp_reconcile_bottle_manifest(manifests[2], strlen(manifests[2])));
    assert(!metalsharp_reconcile_bottle_manifest(manifests[0], strlen(manifests[0])));
    assert(rename(m12_d3d11_backup, m12_d3d11) == 0);
    assert(metalsharp_reconcile_bottle_manifest(manifests[0], strlen(manifests[0])));

    /* Elden Ring's EAC title contract is a direct executable substitution, never a Steam launch. */
    assert(metalsharp_launcher_prepare_eac(1245620, games[0], strlen(games[0])));
    snprintf(path, sizeof(path), "%s/start_protected_game.old", games[0]);
    assert_contains(path, "direct-launch-test");
    snprintf(path, sizeof(path), "%s/start_protected_game.exe", games[0]);
    assert_contains(path, "elden-ring");
    assert(preflight(&profiles[0], prefixes[0], games[0]));
    char protected_path[PATH_MAX], protected_old[PATH_MAX], protected_real[PATH_MAX];
    snprintf(protected_path, sizeof(protected_path), "%s/start_protected_game.exe", games[0]);
    snprintf(protected_old, sizeof(protected_old), "%s/start_protected_game.old", games[0]);
    snprintf(protected_real, sizeof(protected_real), "%s/eldenring.exe", games[0]);
    copy_file(protected_old, protected_path);
    assert(!preflight(&profiles[0], prefixes[0], games[0]));
    copy_file(protected_real, protected_path);
    assert(preflight(&profiles[0], prefixes[0], games[0]));
    assert(!metalsharp_launcher_preflight(1245620, 4, prefixes[0], strlen(prefixes[0]), games[0], strlen(games[0]),
                                          profiles[0].executable, strlen(profiles[0].executable)));

    /* Partial/corrupt game copies must fail until bottle reconciliation restores exact bytes. */
    snprintf(path, sizeof(path), "%s/d3d12.dll", games[0]);
    write_text(path, "partial-copy");
    assert(!preflight(&profiles[0], prefixes[0], games[0]));
    assert(metalsharp_reconcile_bottle_manifest(manifests[0], strlen(manifests[0])));
    assert(preflight(&profiles[0], prefixes[0], games[0]));

    /* A cross-architecture DLL cannot authorize a 32-bit launch. */
    char x64_d3d11[PATH_MAX];
    snprintf(x64_d3d11, sizeof(x64_d3d11), "%s/runtime/wine/lib/dxmt/x86_64-windows/d3d11.dll", home);
    snprintf(path, sizeof(path), "%s/d3d11.dll", games[3]);
    copy_file(x64_d3d11, path);
    assert(!preflight(&profiles[3], prefixes[3], games[3]));
    assert(metalsharp_reconcile_bottle_manifest(manifests[3], strlen(manifests[3])));
    assert(preflight(&profiles[3], prefixes[3], games[3]));

    /* A cross-architecture receipt cannot authorize launch either. */
    snprintf(path, sizeof(path), "%s/steam_%u/dxmt-deployment.json", bottles, profiles[3].appid);
    replace_text(path, "\"architecture\": \"i386\"", "\"architecture\": \"x86_64\"");
    assert(!preflight(&profiles[3], prefixes[3], games[3]));
    assert(metalsharp_reconcile_bottle_manifest(manifests[3], strlen(manifests[3])));
    assert(preflight(&profiles[3], prefixes[3], games[3]));

    /* A stale receipt cannot authorize launch. */
    snprintf(path, sizeof(path), "%s/steam_%u/dxmt-deployment.json", bottles, profiles[1].appid);
    replace_text(path, "\"status\": \"ready\"", "\"status\": \"stale\"");
    assert(!preflight(&profiles[1], prefixes[1], games[1]));
    assert(metalsharp_reconcile_bottle_manifest(manifests[1], strlen(manifests[1])));
    assert(preflight(&profiles[1], prefixes[1], games[1]));

    /* Mixed PE/Unix payloads and missing sidecars invalidate every launch shape. */
    char unix_bridge[PATH_MAX], unix_backup[PATH_MAX], pe_bridge[PATH_MAX], sidecar[PATH_MAX], sidecar_backup[PATH_MAX];
    snprintf(unix_bridge, sizeof(unix_bridge), "%s/x86_64-unix/winemetal.so", m12);
    snprintf(unix_backup, sizeof(unix_backup), "%s.negative-backup", unix_bridge);
    snprintf(pe_bridge, sizeof(pe_bridge), "%s/x86_64-windows/winemetal.dll", m12);
    assert(rename(unix_bridge, unix_backup) == 0);
    copy_file(pe_bridge, unix_bridge);
    assert(!preflight(&profiles[0], prefixes[0], games[0]));
    assert(unlink(unix_bridge) == 0);
    assert(rename(unix_backup, unix_bridge) == 0);
    assert(preflight(&profiles[0], prefixes[0], games[0]));

    snprintf(sidecar, sizeof(sidecar), "%s/x86_64-unix/libc++abi.1.dylib", m12);
    snprintf(sidecar_backup, sizeof(sidecar_backup), "%s.negative-backup", sidecar);
    assert(rename(sidecar, sidecar_backup) == 0);
    assert(!preflight(&profiles[0], prefixes[0], games[0]));
    assert(rename(sidecar_backup, sidecar) == 0);
    assert(preflight(&profiles[0], prefixes[0], games[0]));

    /* A failed promotion must preserve prior bytes and mark the manifest for repair. */
    snprintf(path, sizeof(path), "%s/drive_c/windows/system32/d3d11.dll", prefixes[1]);
    write_text(path, "preserve-me");
    assert(setenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION", "1", 1) == 0);
    assert(!metalsharp_reconcile_bottle_manifest(manifests[1], strlen(manifests[1])));
    assert(unsetenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION") == 0);
    assert_contains(path, "preserve-me");
    assert_contains(manifests[1], "\"health\": \"needs_repair\"");

    /* Saving M12 over a title whose catalog default is M11 must select the full
       generic M12 deployment and direct-launch shape without applying Elden's
       app-specific EAC executable substitution. */
    const ProfileCase rain_world_m12 = {"m12", 6, 312520, "RainWorld.exe", "system32", "d3d12.dll", true};
    replace_text(manifests[1], "\"runtime_profile\": \"m11\"", "\"runtime_profile\": \"m12\"");
    assert(metalsharp_reconcile_bottle_manifest(manifests[1], strlen(manifests[1])));
    snprintf(path, sizeof(path), "%s/drive_c/windows/system32/d3d12.dll", prefixes[1]);
    assert(access(path, R_OK) == 0);
    snprintf(path, sizeof(path), "%s/d3d12.dll", games[1]);
    assert(access(path, R_OK) == 0);
    assert(preflight(&rain_world_m12, prefixes[1], games[1]));
    snprintf(path, sizeof(path), "%s/steam_%u/dxmt-launch-preflight.json", bottles, rain_world_m12.appid);
    char expected_surface[512];
    const MetalsharpBottlePolicy* m12_policy = metalsharp_bottle_policy("m12");
    assert(m12_policy != NULL);
    snprintf(expected_surface, sizeof(expected_surface), "\"surface_id\": \"%s\"", m12_policy->surface_id);
    assert_contains(path, "\"pipeline\": \"m12\"");
    assert_contains(path, expected_surface);
    assert_contains(path, "\"graphics_backend\": \"dxmt\"");
    assert_contains(path, "\"executable\": \"RainWorld.exe\"");
    assert_contains(path, "\"launch_mode\": \"direct_executable\"");
    assert_contains(path, "\"steam_client\": \"background\"");
    snprintf(path, sizeof(path), "%s/steam_%u/m12-dry-run.json", bottles, rain_world_m12.appid);
    assert_contains(path, "\"dry_run\": true");
    assert_contains(path, "\"unix_sidecars\"");

    /* Update migration preserves bottle manifests and prefixes, then refreshes
       every managed DLL set from the newly installed surface. */
    snprintf(path, sizeof(path), "%s/drive_c/windows/system32/d3d12.dll", prefixes[0]);
    write_text(path, "old-prefix-surface");
    snprintf(path, sizeof(path), "%s/d3d12.dll", games[1]);
    write_text(path, "old-game-surface");
    snprintf(path, sizeof(path), "%s/steam_%u/dxmt-deployment.json", bottles, profiles[0].appid);
    replace_text(path, m12_policy->surface_id, "stale-surface");

    char unmanaged[PATH_MAX], unmanaged_manifest[PATH_MAX];
    snprintf(unmanaged, sizeof(unmanaged), "%s/custom-gptk", bottles);
    mkdir_required(unmanaged);
    snprintf(unmanaged_manifest, sizeof(unmanaged_manifest), "%s/bottle.json", unmanaged);
    write_text(unmanaged_manifest, "{\"runtime_profile\":\"d3dmetal\"}\n");

    MetalsharpMigrationBottleSummary migration = {0};
    assert(setenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION", "1", 1) == 0);
    assert(!metalsharp_migration_refresh_bottles(home, strlen(home), &migration));
    assert(migration.failed == 5);
    snprintf(path, sizeof(path), "%s/logs/migration-bottle-refresh-latest.json", home);
    assert_contains(path, "\"status\": \"needs_repair\"");
    assert(unsetenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION") == 0);

    migration = (MetalsharpMigrationBottleSummary){0};
    assert(metalsharp_migration_refresh_bottles(home, strlen(home), &migration));
    assert(migration.discovered == 6);
    assert(migration.managed == 5);
    assert(migration.refreshed == 5);
    assert(migration.skipped == 1);
    assert(migration.failed == 0);
    snprintf(path, sizeof(path), "%s/steam_%u/dxmt-deployment.json", bottles, profiles[0].appid);
    assert_contains(path, expected_surface);
    snprintf(path, sizeof(path), "%s/drive_c/windows/system32/d3d12.dll", prefixes[0]);
    char refreshed[16384];
    read_text(path, refreshed, sizeof(refreshed));
    assert(strstr(refreshed, "old-prefix-surface") == NULL);
    snprintf(path, sizeof(path), "%s/d3d12.dll", games[1]);
    read_text(path, refreshed, sizeof(refreshed));
    assert(strstr(refreshed, "old-game-surface") == NULL);
    snprintf(path, sizeof(path), "%s/logs/migration-bottle-refresh-latest.json", home);
    assert_contains(path, "\"status\": \"ready\"");
    assert_contains(path, "\"refreshed\": 5");

    /* Migration explicitly validates v2, but it never re-routes a baseline
       bottle through dxmt_m12. */
    assert(rename(m12_d3d11, m12_d3d11_backup) == 0);
    migration = (MetalsharpMigrationBottleSummary){0};
    assert(!metalsharp_migration_refresh_bottles(home, strlen(home), &migration));
    assert(migration.failed == 1);
    assert(rename(m12_d3d11_backup, m12_d3d11) == 0);
    assert(metalsharp_migration_refresh_bottles(home, strlen(home), &migration));

    puts("DXMT M12, M11, M10, M11(32), and M10(32) deployment/launch conformance passed, including saved "
         "M11-to-M12 promotion, migration-wide bottle refresh, mixed PE/Unix, cross-architecture, missing-sidecar, "
         "partial-copy, stale-receipt, EAC, and rollback negatives.");
    return 0;
}
