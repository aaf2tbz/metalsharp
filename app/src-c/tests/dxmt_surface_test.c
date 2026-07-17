#include "../runtime_surface.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void corrupt_dxmt(const char* m12_root) {
    char path[4096];
    snprintf(path, sizeof(path), "%.*s/dxmt/x86_64-windows/dxgi.dll", (int)(strlen(m12_root) - strlen("/dxmt_m12")),
             m12_root);
    int fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0);
    assert(write(fd, "stale", 5) == 5);
    assert(close(fd) == 0);
}

static void assert_sentinel(const char* path) {
    char value[32] = {0};
    FILE* marker = fopen(path, "r");
    assert(marker != NULL);
    assert(fread(value, 1, sizeof(value) - 1, marker) == strlen("preserve-me"));
    assert(fclose(marker) == 0);
    assert(strcmp(value, "preserve-me") == 0);
}

static void write_legacy_v1_receipt(const char* root) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/metalsharp-dxmt-runtime.json", root);
    FILE* receipt = fopen(path, "w");
    assert(receipt != NULL);
    assert(fputs("{\n  \"schema\": \"metalsharp.dxmt-runtime.v1\",\n"
                 "  \"version\": \"0.56.0-m12-isolated-surface-v1\"\n}\n",
                 receipt) >= 0);
    assert(fclose(receipt) == 0);
}

static void assert_v2_receipt(const char* root) {
    char path[4096], contents[2048] = {0};
    snprintf(path, sizeof(path), "%s/metalsharp-dxmt-runtime.json", root);
    FILE* receipt = fopen(path, "r");
    assert(receipt != NULL);
    assert(fread(contents, 1, sizeof(contents) - 1, receipt) > 0);
    assert(fclose(receipt) == 0);
    assert(strstr(contents, "\"schema\": \"metalsharp.dxmt-runtime.v2\"") != NULL);
    assert(strstr(contents, "\"version\": \"" METALSHARP_DXMT_SURFACE_VERSION "\"") != NULL);
    assert(strstr(contents, "isolated-surface-v1") == NULL);
}

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--repair") == 0) {
        const bool repaired = metalsharp_reconcile_dxmt_surface(argv[2], strlen(argv[2]));
        fprintf(stderr, "%s: %s\n", METALSHARP_DXMT_SURFACE_ID, repaired ? "reconciled" : "failed");
        return repaired ? 0 : 1;
    }
    assert(argc == 2);
    const char* m12 = argv[1];
    const size_t m12_len = strlen(m12);
    /* The archive may carry an older receipt; promotion upgrades identity. */
    assert(metalsharp_reconcile_dxmt_surface(m12, m12_len));
    assert(metalsharp_dxmt_surface_current(m12, m12_len));

    /* A preserved v1 receipt is stale even when its files exist. Migration
       replaces the active surface and receipt before reporting readiness. */
    write_legacy_v1_receipt(m12);
    assert(!metalsharp_dxmt_surface_current(m12, m12_len));
    assert(metalsharp_reconcile_dxmt_surface(m12, m12_len));
    assert(metalsharp_dxmt_surface_current(m12, m12_len));
    assert_v2_receipt(m12);

    char sentinel[4096];
    snprintf(sentinel, sizeof(sentinel), "%.*s/dxmt/i386-windows/m1132-preserved.sentinel",
             (int)(m12_len - strlen("/dxmt_m12")), m12);
    FILE* marker = fopen(sentinel, "w");
    assert(marker != NULL);
    assert(fputs("preserve-me", marker) >= 0);
    assert(fclose(marker) == 0);

    corrupt_dxmt(m12);
    assert(!metalsharp_dxmt_surface_current(m12, m12_len));
    assert(metalsharp_reconcile_dxmt_surface(m12, m12_len));
    assert(metalsharp_dxmt_surface_current(m12, m12_len));
    assert_sentinel(sentinel);

    corrupt_dxmt(m12);
    assert(setenv("METALSHARP_TEST_DXMT_FAIL_AFTER_ROOT_PROMOTION", "1", 1) == 0);
    assert(!metalsharp_reconcile_dxmt_surface(m12, m12_len));
    assert(unsetenv("METALSHARP_TEST_DXMT_FAIL_AFTER_ROOT_PROMOTION") == 0);
    assert(!metalsharp_dxmt_surface_current(m12, m12_len));
    assert_sentinel(sentinel);
    assert(metalsharp_reconcile_dxmt_surface(m12, m12_len));
    assert(metalsharp_dxmt_surface_current(m12, m12_len));
    assert_sentinel(sentinel);

    puts("DXMT surface promotion, i386 preservation, and rollback passed.");
    return 0;
}
