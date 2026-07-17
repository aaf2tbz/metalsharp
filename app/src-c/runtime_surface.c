#include "runtime_surface.h"

#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char* path;
    off_t size;
    const char* sha256;
} SurfaceArtifact;

/* The isolated M12 payload from the newer graphics archive. */
static const SurfaceArtifact m12_artifacts[] = {
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

static bool path_join(char* output, size_t size, const char* left, const char* right) {
    const int written = snprintf(output, size, "%s/%s", left, right);
    return written >= 0 && (size_t)written < size;
}

static bool copy_slice(char* output, size_t size, const char* value, size_t length) {
    if (value == NULL || length == 0 || length >= size)
        return false;
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}

static bool file_sha256(const char* path, off_t expected_size, char output[65]) {
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size != expected_size)
        return false;
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0)
        return false;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    unsigned char buffer[1024 * 1024];
    ssize_t count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0)
        CC_SHA256_Update(&context, buffer, (CC_LONG)count);
    const int saved_errno = errno;
    close(descriptor);
    if (count < 0) {
        errno = saved_errno;
        return false;
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    for (size_t index = 0; index < sizeof(digest); ++index)
        snprintf(output + index * 2, 3, "%02x", digest[index]);
    output[64] = '\0';
    return true;
}

static bool verify_artifacts(const char* root, const SurfaceArtifact* artifacts, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        char artifact_path[PATH_MAX], digest[65];
        if (!path_join(artifact_path, sizeof(artifact_path), root, artifacts[index].path) ||
            !file_sha256(artifact_path, artifacts[index].size, digest) || strcmp(digest, artifacts[index].sha256) != 0)
            return false;
    }
    return true;
}

static bool m12_receipt_valid(const char* root) {
    char receipt_path[PATH_MAX], contents[2048];
    if (!path_join(receipt_path, sizeof(receipt_path), root, "metalsharp-dxmt-runtime.json"))
        return false;
    FILE* receipt = fopen(receipt_path, "rb");
    if (receipt == NULL)
        return false;
    const size_t count = fread(contents, 1, sizeof(contents) - 1, receipt);
    const bool read_ok = !ferror(receipt);
    fclose(receipt);
    contents[count] = '\0';
    return read_ok && strstr(contents, "\"schema\": \"metalsharp.dxmt-runtime.v2\"") != NULL &&
           strstr(contents, "\"surface_id\": \"" METALSHARP_DXMT_M12_SURFACE_ID "\"") != NULL;
}

static bool write_m12_receipt(const char* root) {
    char receipt_path[PATH_MAX], temporary_path[PATH_MAX];
    if (!path_join(receipt_path, sizeof(receipt_path), root, "metalsharp-dxmt-runtime.json") ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", receipt_path) >= (int)sizeof(temporary_path))
        return false;
    FILE* receipt = fopen(temporary_path, "wb");
    if (receipt == NULL)
        return false;
    const int result = fprintf(receipt,
                               "{\n  \"schema\": \"metalsharp.dxmt-runtime.v2\",\n"
                               "  \"surface_id\": \"%s\",\n  \"lane\": \"dxmt_m12\",\n"
                               "  \"source_bundle\": \"Dependency Bundles\",\n"
                               "  \"artifact_count\": %zu\n}\n",
                               METALSHARP_DXMT_M12_SURFACE_ID,
                               sizeof(m12_artifacts) / sizeof(m12_artifacts[0]));
    bool ok = result > 0 && fflush(receipt) == 0 && fsync(fileno(receipt)) == 0;
    if (fclose(receipt) != 0)
        ok = false;
    if (ok)
        ok = rename(temporary_path, receipt_path) == 0;
    if (!ok)
        unlink(temporary_path);
    return ok;
}

bool metalsharp_m12_dxmt_surface_current(const char* root_value, size_t root_len) {
    char root[PATH_MAX];
    return copy_slice(root, sizeof(root), root_value, root_len) &&
           verify_artifacts(root, m12_artifacts, sizeof(m12_artifacts) / sizeof(m12_artifacts[0])) && m12_receipt_valid(root);
}

/* Compatibility name retained for converted callers. It is M12-only. */
bool metalsharp_dxmt_surface_current(const char* root_value, size_t root_len) {
    return metalsharp_m12_dxmt_surface_current(root_value, root_len);
}

bool metalsharp_dxmt_artifact_current(const char* root_value, size_t root_len, const char* relative_value,
                                      size_t relative_len) {
    char root[PATH_MAX], relative[PATH_MAX], artifact_path[PATH_MAX], digest[65];
    if (!copy_slice(root, sizeof(root), root_value, root_len) ||
        !copy_slice(relative, sizeof(relative), relative_value, relative_len))
        return false;
    for (size_t index = 0; index < sizeof(m12_artifacts) / sizeof(m12_artifacts[0]); ++index) {
        if (strcmp(relative, m12_artifacts[index].path) == 0)
            return path_join(artifact_path, sizeof(artifact_path), root, relative) &&
                   file_sha256(artifact_path, m12_artifacts[index].size, digest) &&
                   strcmp(digest, m12_artifacts[index].sha256) == 0;
    }
    return false;
}

/* Reconciliation is intentionally receipt-only: the M12 lane may never alter
 * the legacy dxmt tree or Wine loader mirrors. */
bool metalsharp_reconcile_dxmt_surface(const char* root_value, size_t root_len) {
    char root[PATH_MAX];
    if (!copy_slice(root, sizeof(root), root_value, root_len) ||
        !verify_artifacts(root, m12_artifacts, sizeof(m12_artifacts) / sizeof(m12_artifacts[0])))
        return false;
    return m12_receipt_valid(root) || (write_m12_receipt(root) && m12_receipt_valid(root));
}
