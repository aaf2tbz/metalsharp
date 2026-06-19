#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m12core.h"

static bool probe_verbose(void) {
  const char *v = getenv("M12_PROBE_VERBOSE");
  return v && v[0] && strcmp(v, "0") != 0;
}

#define VLOG(...) do { if (probe_verbose()) fprintf(stderr, __VA_ARGS__); } while (0)

static bool normalize_hash(const char *input, char out[17]) {
  if (!input)
    return false;
  while (isspace((unsigned char)*input))
    input++;
  if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))
    input += 2;
  char tmp[17] = {0};
  size_t n = 0;
  while (*input && !isspace((unsigned char)*input) && n < sizeof(tmp) - 1) {
    if (!isxdigit((unsigned char)*input))
      return false;
    tmp[n++] = (char)tolower((unsigned char)*input++);
  }
  if (n == 0 || n > 16)
    return false;
  memset(out, '0', 16 - n);
  memcpy(out + (16 - n), tmp, n);
  out[16] = '\0';
  return true;
}

static bool read_file(const char *path, void **out_data, uint64_t *out_size) {
  *out_data = NULL;
  *out_size = 0;
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long size = ftell(f);
  if (size <= 0) {
    fclose(f);
    return false;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }
  void *data = malloc((size_t)size);
  if (!data) {
    fclose(f);
    return false;
  }
  size_t got = fread(data, 1, (size_t)size, f);
  fclose(f);
  if (got != (size_t)size) {
    free(data);
    return false;
  }
  *out_data = data;
  *out_size = (uint64_t)size;
  return true;
}

static int probe_one(id<MTLDevice> device, const char *cache_dir, const char *hash_text) {
  VLOG("probe_one begin hash_text=%s\n", hash_text ? hash_text : "(null)");
  char hash[17];
  if (!normalize_hash(hash_text, hash)) {
    fprintf(stderr, "invalid hash: %s\n", hash_text ? hash_text : "(null)");
    return 1;
  }

  char path[4096];
  snprintf(path, sizeof(path), "%s/%s.metallib", cache_dir, hash);

  void *data = NULL;
  uint64_t size = 0;
  if (!read_file(path, &data, &size)) {
    fprintf(stderr, "missing/unreadable metallib hash=%s path=%s errno=%d\n", hash, path, errno);
    return 1;
  }
  VLOG("read metallib hash=%s bytes=%llu\n", hash, (unsigned long long)size);

  uint64_t shader_hash = strtoull(hash, NULL, 16);
  M12CoreShaderFunctionDesc desc;
  memset(&desc, 0, sizeof(desc));
  desc.abi_version = M12CORE_ABI_VERSION;
  desc.stage = M12CORE_SHADER_STAGE_UNKNOWN;
  desc.input_kind = M12CORE_SHADER_FUNCTION_INPUT_METALLIB;
  desc.shader_hash = shader_hash;
  desc.device_handle = (uint64_t)(uintptr_t)device;
  desc.input_data = data;
  desc.input_size = size;

  M12CoreShaderFunctionResult first;
  memset(&first, 0, sizeof(first));
  VLOG("calling m12core_create_shader_function first hash=%s\n", hash);
  int rc = m12core_create_shader_function(&desc, &first);
  VLOG("returned first hash=%s rc=%d status=%u cache_hit=%u func=%llu\n",
       hash, rc, first.status, first.cache_hit, (unsigned long long)first.function_handle);
  /* m12core's METALLIB path wraps input_data in dispatch_data_create with
   * DISPATCH_DATA_DESTRUCTOR_DEFAULT, so a successful miss consumes/frees the
   * malloc buffer. Do not reuse or free it after this call unless the function
   * cache hit before dispatch_data_create.
   */
  bool first_consumed_input = !first.cache_hit;
  if (rc != 0 || first.status != M12CORE_SHADER_FUNCTION_STATUS_OK || !first.function_handle) {
    fprintf(stderr, "metallib load failed hash=%s rc=%d status=%u error=%" PRIu64 " path=%s\n",
            hash, rc, first.status, first.error_handle, path);
    if (!first_consumed_input)
      free(data);
    return 1;
  }
  if (!first_consumed_input)
    free(data);

  unsigned char dummy = 0;
  desc.input_data = &dummy;
  desc.input_size = 1;
  M12CoreShaderFunctionResult second;
  memset(&second, 0, sizeof(second));
  VLOG("calling m12core_create_shader_function second hash=%s\n", hash);
  rc = m12core_create_shader_function(&desc, &second);
  VLOG("returned second hash=%s rc=%d status=%u cache_hit=%u func=%llu\n",
       hash, rc, second.status, second.cache_hit, (unsigned long long)second.function_handle);
  if (rc != 0 || second.status != M12CORE_SHADER_FUNCTION_STATUS_OK || !second.function_handle || !second.cache_hit) {
    fprintf(stderr, "metallib cache re-load failed hash=%s rc=%d status=%u cache_hit=%u error=%" PRIu64 " path=%s\n",
            hash, rc, second.status, second.cache_hit, second.error_handle, path);
    if (first.function_handle)
      [(id)(uintptr_t)first.function_handle release];
    return 1;
  }

  printf("ok hash=%s bytes=%" PRIu64 " first_cache_hit=%u second_cache_hit=%u entry=%s\n",
         hash, size, first.cache_hit, second.cache_hit,
         second.selected_entry[0] ? second.selected_entry : first.selected_entry);

  if (first.function_handle)
    [(id)(uintptr_t)first.function_handle release];
  if (second.function_handle)
    [(id)(uintptr_t)second.function_handle release];
  VLOG("probe_one end hash=%s\n", hash);
  return 0;
}

int main(int argc, char **argv) {
  const char *cache_dir = NULL;
  const char *hash_file = NULL;
  const char *single_hash = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--cache-dir") && i + 1 < argc) {
      cache_dir = argv[++i];
    } else if (!strcmp(argv[i], "--hash-file") && i + 1 < argc) {
      hash_file = argv[++i];
    } else if (!strcmp(argv[i], "--hash") && i + 1 < argc) {
      single_hash = argv[++i];
    } else {
      fprintf(stderr, "usage: %s --cache-dir DIR (--hash HASH | --hash-file FILE)\n", argv[0]);
      return 2;
    }
  }
  if (!cache_dir || (!hash_file && !single_hash)) {
    fprintf(stderr, "usage: %s --cache-dir DIR (--hash HASH | --hash-file FILE)\n", argv[0]);
    return 2;
  }

  @autoreleasepool {
    VLOG("creating default MTLDevice\n");
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
      fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n");
      return 1;
    }

    int failures = 0;
    int total = 0;
    if (single_hash) {
      total++;
      failures += probe_one(device, cache_dir, single_hash) ? 1 : 0;
    }
    if (hash_file) {
      FILE *f = fopen(hash_file, "r");
      if (!f) {
        fprintf(stderr, "failed to open hash file: %s\n", hash_file);
        return 1;
      }
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p))
          p++;
        if (!*p || *p == '#')
          continue;
        total++;
        failures += probe_one(device, cache_dir, p) ? 1 : 0;
      }
      fclose(f);
    }
    printf("summary total=%d failures=%d\n", total, failures);
    return failures ? 1 : 0;
  }
}
