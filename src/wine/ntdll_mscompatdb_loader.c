/*
 * MetalSharp mscompatdb autoload helper for Wine ntdll's Unix loader.
 *
 * This is a source template for the patched Wine tree. It should be folded
 * into dlls/ntdll/unix/loader.c and called after load_ntdll() during process
 * startup so mscompatdb can patch the syscall table through the exported
 * MetalSharp hook contract.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void load_metalsharp_mscompatdb_from_dir(const char *dir)
{
    char path[4096];

    if (!dir || !*dir) return;
    if (snprintf(path, sizeof(path), "%s/mscompatdb.so", dir) <= 0) return;
    if (access(path, R_OK)) return;
    if (dlopen(path, RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "mscompatdb: loaded from %s\n", path);
    else
        fprintf(stderr, "mscompatdb: failed to load %s: %s\n", path, dlerror());
}

static void load_metalsharp_mscompatdb(void)
{
    const char *root = getenv("MS_ROOT");
    char path[4096];

    load_metalsharp_mscompatdb_from_dir(ntdll_dir);
    if (!root || !*root) return;

    if (snprintf(path, sizeof(path), "%s/lib/wine/x86_64-unix", root) > 0)
        load_metalsharp_mscompatdb_from_dir(path);
}
