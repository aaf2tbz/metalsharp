#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m12core.h"

static uint64_t parse_hash(const char* text) {
    if (!text)
        return 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    return strtoull(text, NULL, 16);
}

static const char* json_bool(uint32_t v) {
    return v ? "true" : "false";
}

int main(int argc, char** argv) {
    const char* cache_dir = NULL;
    const char* hash_text = NULL;
    const char* output_path = NULL;
    uint32_t force_source = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cache-dir") && i + 1 < argc) {
            cache_dir = argv[++i];
        } else if (!strcmp(argv[i], "--hash") && i + 1 < argc) {
            hash_text = argv[++i];
        } else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (!strcmp(argv[i], "--force-source") && i + 1 < argc) {
            force_source = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "usage: %s --cache-dir DIR --hash HASH --output FILE [--force-source 0|1]\n", argv[0]);
            return 2;
        }
    }
    if (!cache_dir || !hash_text || !output_path) {
        fprintf(stderr, "usage: %s --cache-dir DIR --hash HASH --output FILE [--force-source 0|1]\n", argv[0]);
        return 2;
    }

    M12CoreShaderCacheLookup lookup;
    memset(&lookup, 0, sizeof(lookup));
    uint64_t hash = parse_hash(hash_text);
    int rc = m12core_probe_shader_cache(cache_dir, hash, force_source, &lookup);

    FILE* f = fopen(output_path, "w");
    if (!f) {
        fprintf(stderr, "failed to open output %s errno=%d\n", output_path, errno);
        return 1;
    }
    fprintf(f,
            "{\n"
            "  \"rc\": %d,\n"
            "  \"abi_version\": %u,\n"
            "  \"hash\": \"%016" PRIx64 "\",\n"
            "  \"force_source_compile\": %s,\n"
            "  \"metallib_exists\": %s,\n"
            "  \"metallib_available\": %s,\n"
            "  \"metallib_path\": \"%s\",\n"
            "  \"metallib_error_path\": \"%s\"\n"
            "}\n",
            rc, lookup.abi_version, hash, json_bool(lookup.force_source_compile), json_bool(lookup.metallib_exists),
            json_bool(lookup.metallib_available), lookup.paths.metallib_path, lookup.paths.metallib_error_path);
    fclose(f);
    return rc == 0 ? 0 : 1;
}
