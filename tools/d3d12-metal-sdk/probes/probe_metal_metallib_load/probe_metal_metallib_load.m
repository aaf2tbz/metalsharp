#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool normalize_hash(const char *input, char out[17]) {
  if (!input) return false;
  while (isspace((unsigned char)*input)) input++;
  if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) input += 2;
  char tmp[17] = {0};
  size_t n = 0;
  while (*input && !isspace((unsigned char)*input) && n < sizeof(tmp) - 1) {
    if (!isxdigit((unsigned char)*input)) return false;
    tmp[n++] = (char)tolower((unsigned char)*input++);
  }
  if (n == 0 || n > 16) return false;
  memset(out, '0', 16 - n);
  memcpy(out + (16 - n), tmp, n);
  out[16] = 0;
  return true;
}

static NSData *read_data(const char *path) {
  NSString *s = [[NSString alloc] initWithUTF8String:path];
  NSData *data = [NSData dataWithContentsOfFile:s];
  [s release];
  return data;
}

static id<MTLFunction> lookup(id<MTLLibrary> lib) {
  const char *names[] = {"main", "cs_main", "vs_main", "ps_main", NULL};
  for (int i = 0; names[i]; i++) {
    NSString *name = [[NSString alloc] initWithUTF8String:names[i]];
    id<MTLFunction> fn = [lib newFunctionWithName:name];
    [name release];
    if (fn) return fn;
  }
  return nil;
}

static int probe_one(id<MTLDevice> device, const char *cache_dir, const char *hash_text) {
  char hash[17];
  if (!normalize_hash(hash_text, hash)) {
    fprintf(stderr, "invalid hash: %s\n", hash_text ? hash_text : "(null)");
    return 1;
  }
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s.metallib", cache_dir, hash);
  NSData *data = read_data(path);
  if (!data || [data length] == 0) {
    fprintf(stderr, "missing/unreadable metallib hash=%s path=%s errno=%d\n", hash, path, errno);
    return 1;
  }
  NSError *error = nil;
  dispatch_data_t dispatchData = dispatch_data_create([data bytes], [data length], NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  id<MTLLibrary> lib = [device newLibraryWithData:dispatchData error:&error];
  dispatch_release(dispatchData);
  if (!lib) {
    fprintf(stderr, "newLibraryWithData failed hash=%s error=%s path=%s\n", hash,
            error ? [[error localizedDescription] UTF8String] : "unknown", path);
    return 1;
  }
  id<MTLFunction> fn = lookup(lib);
  if (!fn) {
    fprintf(stderr, "function lookup failed hash=%s path=%s\n", hash, path);
    [lib release];
    return 1;
  }
  printf("ok hash=%s bytes=%llu function=%s\n", hash, (unsigned long long)[data length], [[fn name] UTF8String]);
  [fn release];
  [lib release];
  return 0;
}

int main(int argc, char **argv) {
  const char *cache_dir = NULL, *hash_file = NULL, *single_hash = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--cache-dir") && i + 1 < argc) cache_dir = argv[++i];
    else if (!strcmp(argv[i], "--hash-file") && i + 1 < argc) hash_file = argv[++i];
    else if (!strcmp(argv[i], "--hash") && i + 1 < argc) single_hash = argv[++i];
    else { fprintf(stderr, "usage: %s --cache-dir DIR (--hash HASH | --hash-file FILE)\n", argv[0]); return 2; }
  }
  if (!cache_dir || (!hash_file && !single_hash)) {
    fprintf(stderr, "usage: %s --cache-dir DIR (--hash HASH | --hash-file FILE)\n", argv[0]);
    return 2;
  }
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n"); return 1; }
    int total = 0, failures = 0;
    if (single_hash) { total++; failures += probe_one(device, cache_dir, single_hash) ? 1 : 0; }
    if (hash_file) {
      FILE *f = fopen(hash_file, "r");
      if (!f) { fprintf(stderr, "failed to open hash file: %s\n", hash_file); return 1; }
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!*p || *p == '#') continue;
        total++;
        failures += probe_one(device, cache_dir, p) ? 1 : 0;
      }
      fclose(f);
    }
    printf("summary total=%d failures=%d\n", total, failures);
    return failures ? 1 : 0;
  }
}
