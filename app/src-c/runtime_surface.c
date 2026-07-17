#include "runtime_surface.h"

#include <CommonCrypto/CommonDigest.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

typedef struct {
    const char* path;
    off_t size;
    const char* sha256;
} SurfaceArtifact;

static const SurfaceArtifact x64_artifacts[] = {
    {"x86_64-windows/d3d10core.dll", 14733671, "cc4f9a99a53bc6e67747d4c00eb7b3932b4338f54d03205280ffb51bac7654cd"},
    {"x86_64-windows/d3d11.dll", 72646279, "41c8eed15dc4e92ed1457645e5122f45afa42c10ec5dfdcd269f54884833aefb"},
    {"x86_64-windows/d3d12.dll", 53311968, "f021efbe95e99c58b3d44d8dfe6277f22e50794de6e260d690da9f638e923805"},
    {"x86_64-windows/dxgi.dll", 67527, "1b3460d6857b93c3105d7cacb3bc3a8ef0cc62914de758c860558a4a8ad243a0"},
    {"x86_64-windows/dxgi_dxmt.dll", 20411737, "be295d8e0fc143365e769b8a2d25775e68d2d2ac5dcce10f4636b77b0c0875f3"},
    {"x86_64-windows/nvapi64.dll", 16447624, "8c42f56450c28316af78ebaa28cc0847b998b47b5530ab0a089a44dd5b7f0054"},
    {"x86_64-windows/nvngx.dll", 16377711, "e5021f37c8ac502e04d8a59f8171f7d508a422f00cc1cfeda11ae6d540916c31"},
    {"x86_64-windows/winemetal.dll", 280952, "ea4ec713d48963caa93deabf1073d13fd3ebed396dbe193fa059fa76bea89ced"},
    {"x86_64-unix/winemetal.so", 46848096, "92228116d30dd187f92a77fbb2316f0cc692de0ac60d4ec8cc664f7b9984e855"},
    {"x86_64-unix/libc++.1.dylib", 1052800, "3f0da0b4025c6fb5e50fc23c8a1feea67c839b40df93baff3b2781089b42ad35"},
    {"x86_64-unix/libc++abi.1.dylib", 303856, "9a95b4ce2be40951b688c394db99f79b7e0b81fa2372e5e49615319869e72e49"},
    {"x86_64-unix/libunwind.1.dylib", 96040, "964d4e5d6242163e4e8099efd08ba75540f253257b834bf5b7a45f8c84b4ea78"},
};

static const SurfaceArtifact i386_artifacts[] = {
    {"i386-unix/winemetal.so", 31805040, "3d50d7f39c64778c71d0af2fce1cde818d09ffbce7c4f7b8ae24ae1df567c0ca"},
    {"i386-windows/d3d10core.dll", 1506849, "c5a26310d14a30c3e1700a4d0c9865be28a11351a5c4dc5b83ab1bd8fcf7662f"},
    {"i386-windows/d3d11.dll", 5689830, "9afc2b3419818618c4c87274435a28935b0df002caa4b9a8d3a88d3dc846b17d"},
    {"i386-windows/dxgi.dll", 1901340, "7df8cdf66e12a108410002abc77b01f70aa59b8a36a70fa0bc6ae3b056841319"},
    {"i386-windows/dxgi_dxmt.dll", 1901340, "7df8cdf66e12a108410002abc77b01f70aa59b8a36a70fa0bc6ae3b056841319"},
    {"i386-windows/winemetal.dll", 69299, "20a6865facebdaac92b6c06fadf37d2efb5a242b22ca1c349bb57d7ad43df8e3"},
};

static bool path_join(char* out, size_t size, const char* left, const char* right) {
    const int written = snprintf(out, size, "%s/%s", left, right);
    return written >= 0 && (size_t)written < size;
}

static bool copy_slice(char* out, size_t size, const char* value, size_t length) {
    if (value == NULL || length == 0 || length >= size)
        return false;
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

static bool run(const char* executable, char* const argv[]) {
    pid_t child = 0;
    if (posix_spawn(&child, executable, NULL, NULL, argv, environ) != 0)
        return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool remove_tree(const char* path) {
    char* const argv[] = {"rm", "-rf", "--", (char*)path, NULL};
    return run("/bin/rm", argv);
}

static bool copy_tree(const char* source, const char* destination) {
    char* const argv[] = {"ditto", "--noqtn", (char*)source, (char*)destination, NULL};
    return run("/usr/bin/ditto", argv);
}

static bool mkdir_if_needed(const char* path) {
    struct stat info;
    if (stat(path, &info) == 0)
        return S_ISDIR(info.st_mode);
    return mkdir(path, 0700) == 0;
}

static bool file_sha256(const char* path, off_t expected_size, char output[65]) {
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size != expected_size)
        return false;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    unsigned char buffer[1024 * 1024];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0)
        CC_SHA256_Update(&context, buffer, (CC_LONG)count);
    const int saved_errno = errno;
    close(fd);
    if (count < 0) {
        errno = saved_errno;
        return false;
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    for (size_t i = 0; i < sizeof(digest); ++i)
        snprintf(output + i * 2, 3, "%02x", digest[i]);
    output[64] = '\0';
    return true;
}

static bool verify_artifacts(const char* root, const SurfaceArtifact* artifacts, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        char path[PATH_MAX], digest[65];
        if (!path_join(path, sizeof(path), root, artifacts[i].path) || !file_sha256(path, artifacts[i].size, digest) ||
            strcmp(digest, artifacts[i].sha256) != 0)
            return false;
    }
    return true;
}

static bool write_receipt(const char* root) {
    char path[PATH_MAX], temporary[PATH_MAX];
    if (!path_join(path, sizeof(path), root, "metalsharp-dxmt-runtime.json") ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
        return false;
    FILE* file = fopen(temporary, "w");
    if (file == NULL)
        return false;
    const int result =
        fprintf(file,
                "{\n  \"schema\": \"metalsharp.dxmt-runtime.v2\",\n  \"surface_id\": \"%s\",\n"
                "  \"source_pr\": 230,\n  \"source_commit\": \"d53ddf32f6f2729476382b8ea11716b90649ca20\",\n"
                "  \"x64_artifact_count\": %zu,\n  \"i386_policy\": \"preserve-and-verify\"\n}\n",
                METALSHARP_DXMT_SURFACE_ID, sizeof(x64_artifacts) / sizeof(x64_artifacts[0]));
    bool ok = result > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0)
        ok = false;
    ok = ok && rename(temporary, path) == 0;
    if (!ok)
        unlink(temporary);
    return ok;
}

static bool receipt_valid(const char* root) {
    char path[PATH_MAX], buffer[2048];
    if (!path_join(path, sizeof(path), root, "metalsharp-dxmt-runtime.json"))
        return false;
    FILE* file = fopen(path, "r");
    if (file == NULL)
        return false;
    const size_t count = fread(buffer, 1, sizeof(buffer) - 1, file);
    const bool read_ok = !ferror(file);
    fclose(file);
    buffer[count] = '\0';
    return read_ok && strstr(buffer, "\"schema\": \"metalsharp.dxmt-runtime.v2\"") != NULL &&
           strstr(buffer, "\"surface_id\": \"" METALSHARP_DXMT_SURFACE_ID "\"") != NULL;
}

static bool derive_paths(const char* m12_root, char lib[PATH_MAX], char dxmt[PATH_MAX]) {
    const char suffix[] = "/dxmt_m12";
    const size_t length = strlen(m12_root), suffix_length = sizeof(suffix) - 1;
    if (length <= suffix_length || strcmp(m12_root + length - suffix_length, suffix) != 0)
        return false;
    memcpy(lib, m12_root, length - suffix_length);
    lib[length - suffix_length] = '\0';
    return path_join(dxmt, PATH_MAX, lib, "dxmt");
}

static bool verify_complete(const char* m12_root) {
    char lib[PATH_MAX], dxmt[PATH_MAX], mirror[PATH_MAX];
    if (!derive_paths(m12_root, lib, dxmt) ||
        !verify_artifacts(m12_root, x64_artifacts, sizeof(x64_artifacts) / sizeof(x64_artifacts[0])) ||
        !verify_artifacts(dxmt, x64_artifacts, sizeof(x64_artifacts) / sizeof(x64_artifacts[0])) ||
        !verify_artifacts(dxmt, i386_artifacts, sizeof(i386_artifacts) / sizeof(i386_artifacts[0])) ||
        !receipt_valid(m12_root) || !receipt_valid(dxmt))
        return false;
    if (!path_join(mirror, sizeof(mirror), lib, "wine") || !verify_artifacts(mirror, &x64_artifacts[8], 4))
        return false;
    if (!verify_artifacts(mirror, &x64_artifacts[7], 1))
        return false;
    return true;
}

bool metalsharp_dxmt_surface_current(const char* m12_root, size_t m12_root_len) {
    char root[PATH_MAX];
    return copy_slice(root, sizeof(root), m12_root, m12_root_len) && verify_complete(root);
}

bool metalsharp_dxmt_artifact_current(const char* m12_root, size_t m12_root_len, const char* relative_path,
                                      size_t relative_path_len) {
    char root[PATH_MAX], relative[PATH_MAX], path[PATH_MAX], digest[65];
    if (!copy_slice(root, sizeof(root), m12_root, m12_root_len) ||
        !copy_slice(relative, sizeof(relative), relative_path, relative_path_len))
        return false;
    for (size_t i = 0; i < sizeof(x64_artifacts) / sizeof(x64_artifacts[0]); ++i) {
        if (strcmp(relative, x64_artifacts[i].path) != 0)
            continue;
        return path_join(path, sizeof(path), root, relative) && file_sha256(path, x64_artifacts[i].size, digest) &&
               strcmp(digest, x64_artifacts[i].sha256) == 0;
    }
    return false;
}

static bool copy_lane(const char* source_root, const char* lane, const char* target_root) {
    char source[PATH_MAX], target[PATH_MAX];
    return path_join(source, sizeof(source), source_root, lane) &&
           path_join(target, sizeof(target), target_root, lane) && copy_tree(source, target);
}

static void rollback_root(const char* target, const char* backup, bool had_target) {
    remove_tree(target);
    if (had_target)
        rename(backup, target);
}

static void rollback_file(const char* target, const char* backup, bool had_target) {
    unlink(target);
    if (had_target)
        copy_tree(backup, target);
}

bool metalsharp_reconcile_dxmt_surface(const char* m12_root_value, size_t m12_root_len) {
    char m12_root[PATH_MAX], lib[PATH_MAX], dxmt[PATH_MAX];
    if (!copy_slice(m12_root, sizeof(m12_root), m12_root_value, m12_root_len) || !derive_paths(m12_root, lib, dxmt) ||
        !verify_artifacts(m12_root, x64_artifacts, sizeof(x64_artifacts) / sizeof(x64_artifacts[0])) ||
        !verify_artifacts(dxmt, i386_artifacts, sizeof(i386_artifacts) / sizeof(i386_artifacts[0])))
        return false;
    if (verify_complete(m12_root))
        return true;

    char transactions[PATH_MAX], stage[PATH_MAX], backup_parent[PATH_MAX];
    if (!path_join(transactions, sizeof(transactions), lib, ".metalsharp-dxmt-transactions") ||
        !mkdir_if_needed(transactions) ||
        snprintf(stage, sizeof(stage), "%s/%s-stage-XXXXXX", transactions, METALSHARP_DXMT_SURFACE_ID) >=
            (int)sizeof(stage) ||
        mkdtemp(stage) == NULL || !path_join(backup_parent, sizeof(backup_parent), lib, ".metalsharp-dxmt-backups") ||
        !mkdir_if_needed(backup_parent))
        return false;

    char stage_dxmt[PATH_MAX], stage_m12[PATH_MAX];
    bool ok = path_join(stage_dxmt, sizeof(stage_dxmt), stage, "dxmt") &&
              path_join(stage_m12, sizeof(stage_m12), stage, "dxmt_m12") && mkdir_if_needed(stage_dxmt) &&
              mkdir_if_needed(stage_m12);
    const char* x64_lanes[] = {"x86_64-windows", "x86_64-unix"};
    for (size_t i = 0; ok && i < 2; ++i) {
        ok = copy_lane(m12_root, x64_lanes[i], stage_dxmt) && copy_lane(m12_root, x64_lanes[i], stage_m12);
    }
    const char* i386_lanes[] = {"i386-windows", "i386-unix"};
    for (size_t i = 0; ok && i < 2; ++i)
        ok = copy_lane(dxmt, i386_lanes[i], stage_dxmt);
    ok = ok && write_receipt(stage_dxmt) && write_receipt(stage_m12) &&
         verify_artifacts(stage_dxmt, x64_artifacts, sizeof(x64_artifacts) / sizeof(x64_artifacts[0])) &&
         verify_artifacts(stage_m12, x64_artifacts, sizeof(x64_artifacts) / sizeof(x64_artifacts[0])) &&
         verify_artifacts(stage_dxmt, i386_artifacts, sizeof(i386_artifacts) / sizeof(i386_artifacts[0]));
    if (!ok) {
        remove_tree(stage);
        return false;
    }

    char backup[PATH_MAX], backup_dxmt[PATH_MAX], backup_m12[PATH_MAX];
    if (snprintf(backup, sizeof(backup), "%s/%s-backup-XXXXXX", backup_parent, METALSHARP_DXMT_SURFACE_ID) >=
            (int)sizeof(backup) ||
        mkdtemp(backup) == NULL || !path_join(backup_dxmt, sizeof(backup_dxmt), backup, "dxmt") ||
        !path_join(backup_m12, sizeof(backup_m12), backup, "dxmt_m12")) {
        remove_tree(stage);
        return false;
    }

    static const char* const mirror_relatives[] = {
        "x86_64-unix/winemetal.so",      "x86_64-windows/winemetal.dll",  "x86_64-unix/libc++.1.dylib",
        "x86_64-unix/libc++abi.1.dylib", "x86_64-unix/libunwind.1.dylib",
    };
    char wine_root[PATH_MAX], mirror_targets[5][PATH_MAX], mirror_stages[5][PATH_MAX], mirror_backups[5][PATH_MAX];
    bool had_mirror[5] = {false};
    ok = path_join(wine_root, sizeof(wine_root), lib, "wine");
    for (size_t i = 0; ok && i < 5; ++i) {
        ok = path_join(mirror_targets[i], sizeof(mirror_targets[i]), wine_root, mirror_relatives[i]) &&
             snprintf(mirror_stages[i], sizeof(mirror_stages[i]), "%s.stage-%d", mirror_targets[i], (int)getpid()) <
                 (int)sizeof(mirror_stages[i]) &&
             snprintf(mirror_backups[i], sizeof(mirror_backups[i]), "%s/mirror-%zu", backup, i) <
                 (int)sizeof(mirror_backups[i]);
        had_mirror[i] = ok && access(mirror_targets[i], F_OK) == 0;
        if (ok && had_mirror[i])
            ok = copy_tree(mirror_targets[i], mirror_backups[i]);
    }
    if (!ok) {
        remove_tree(stage);
        remove_tree(backup);
        return false;
    }
    const bool had_dxmt = access(dxmt, F_OK) == 0, had_m12 = access(m12_root, F_OK) == 0;
    if ((had_dxmt && rename(dxmt, backup_dxmt) != 0) || (had_m12 && rename(m12_root, backup_m12) != 0)) {
        if (had_dxmt && access(dxmt, F_OK) != 0)
            rename(backup_dxmt, dxmt);
        remove_tree(stage);
        return false;
    }
    if (rename(stage_dxmt, dxmt) != 0 || rename(stage_m12, m12_root) != 0) {
        rollback_root(dxmt, backup_dxmt, had_dxmt);
        rollback_root(m12_root, backup_m12, had_m12);
        remove_tree(stage);
        return false;
    }

    if (getenv("METALSHARP_TEST_DXMT_FAIL_AFTER_ROOT_PROMOTION") != NULL) {
        rollback_root(dxmt, backup_dxmt, had_dxmt);
        rollback_root(m12_root, backup_m12, had_m12);
        remove_tree(stage);
        return false;
    }

    char mirror_sources[5][PATH_MAX];
    for (size_t i = 0; ok && i < 5; ++i) {
        ok = path_join(mirror_sources[i], sizeof(mirror_sources[i]), m12_root, mirror_relatives[i]) &&
             copy_tree(mirror_sources[i], mirror_stages[i]);
    }
    for (size_t i = 0; ok && i < 5; ++i)
        ok = rename(mirror_stages[i], mirror_targets[i]) == 0;
    ok = ok && verify_complete(m12_root);
    if (!ok) {
        for (size_t i = 0; i < 5; ++i) {
            unlink(mirror_stages[i]);
            rollback_file(mirror_targets[i], mirror_backups[i], had_mirror[i]);
        }
        rollback_root(dxmt, backup_dxmt, had_dxmt);
        rollback_root(m12_root, backup_m12, had_m12);
        remove_tree(stage);
        return false;
    }
    remove_tree(stage);
    return true;
}
