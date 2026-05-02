#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "mscoree_unix.h"

typedef long NTSTATUS;
#define STATUS_SUCCESS ((NTSTATUS)0)

static NTSTATUS mscoree_unix_dispatch(unsigned int code, void *args);

typedef NTSTATUS (*unixlib_call_t)(unsigned int, void*);

__attribute__((visibility("default")))
__attribute__((used))
unixlib_call_t __wine_unix_call_funcs[2] = { mscoree_unix_dispatch, mscoree_unix_dispatch };

__attribute__((visibility("default")))
__attribute__((used))
unixlib_call_t __wine_unix_call_wow64_funcs[2] = { mscoree_unix_dispatch, mscoree_unix_dispatch };

#define WINE_MONO_LIB "@loader_path/libmonosgen-2.0.dylib"

static void *g_mono_handle = NULL;
static int g_mono_initialized = 0;

typedef struct _MonoDomain MonoDomain;
typedef struct _MonoAssembly MonoAssembly;
typedef struct _MonoImage MonoImage;

typedef enum {
    MONO_IMAGE_OK,
    MONO_IMAGE_ERROR_ERRNO,
    MONO_IMAGE_MISSING_ASSEMBLYREF,
    MONO_IMAGE_IMAGE_INVALID
} MonoImageOpenStatus;

static MonoDomain* (*p_mono_jit_init_version)(const char *domain_name, const char *runtime_version);
static int (*p_mono_jit_exec)(MonoDomain *domain, MonoAssembly *assembly, int argc, char *argv[]);
static MonoAssembly* (*p_mono_assembly_open)(const char *filename, MonoImageOpenStatus *status);
static MonoImage* (*p_mono_assembly_get_image)(MonoAssembly *assembly);
static MonoAssembly* (*p_mono_assembly_load_from)(MonoImage *image, const char *fname, MonoImageOpenStatus *status);
static MonoImage* (*p_mono_image_open)(const char *fname, MonoImageOpenStatus *status);
static void (*p_mono_set_dirs)(const char *assembly_dir, const char *config_dir);
static void (*p_mono_config_parse)(const char *filename);
static MonoDomain* (*p_mono_domain_get)(void);
static void (*p_mono_thread_attach)(MonoDomain *domain);
static void (*p_mono_runtime_quit)(void);
static void (*p_mono_thread_manage)(void);

#define LOAD_FUNC(name) \
    p_##name = dlsym(g_mono_handle, #name); \
    if (!p_##name) { \
        fprintf(stderr, "[mscoree-unix] FAILED to load " #name ": %s\n", dlerror()); \
        return -1; \
    }

static int load_mono(void) {
    if (g_mono_handle) return 0;

    fprintf(stderr, "[mscoree-unix] Loading mono from %s\n", WINE_MONO_LIB);
    g_mono_handle = dlopen(WINE_MONO_LIB, RTLD_NOW | RTLD_GLOBAL);
    if (!g_mono_handle) {
        fprintf(stderr, "[mscoree-unix] FAILED to load mono: %s\n", dlerror());
        return -1;
    }

    LOAD_FUNC(mono_jit_init_version);
    LOAD_FUNC(mono_jit_exec);
    LOAD_FUNC(mono_assembly_open);
    LOAD_FUNC(mono_assembly_get_image);
    LOAD_FUNC(mono_assembly_load_from);
    LOAD_FUNC(mono_image_open);
    LOAD_FUNC(mono_set_dirs);
    LOAD_FUNC(mono_config_parse);
    LOAD_FUNC(mono_domain_get);
    LOAD_FUNC(mono_thread_attach);
    LOAD_FUNC(mono_runtime_quit);
    LOAD_FUNC(mono_thread_manage);

    fprintf(stderr, "[mscoree-unix] All mono functions loaded\n");
    return 0;
}

#undef LOAD_FUNC

static int do_init(void *args) {
    (void)args;
    fprintf(stderr, "[mscoree-unix] INIT\n");
    if (load_mono() < 0) return -1;

    p_mono_set_dirs("/Users/alexmondello/.metalsharp/wine-dlls/mono-x86_64-lib/mono",
                     "/Users/alexmondello/.metalsharp/wine-dlls/mono-x86_64-etc/mono");
    p_mono_config_parse(NULL);

    fprintf(stderr, "[mscoree-unix] mono dirs configured\n");
    return 0;
}

static int do_cor_exe_main(void *args) {
    (void)args;
    fprintf(stderr, "[mscoree-unix] do_cor_exe_main ENTRY\n");
    fflush(stderr);

    if (load_mono() < 0) {
        fprintf(stderr, "[mscoree-unix] mono not loaded\n");
        return -1;
    }

    extern char **environ;
    char exe_path[1024] = {0};
    int found = 0;

    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], "WINELOADERNOEXEC=", 17) == 0) continue;
    }

    for (char **env = environ; *env; env++) {
        if (strncmp(*env, "_=", 2) == 0) {
            strncpy(exe_path, *env + 2, sizeof(exe_path) - 1);
            found = 1;
            break;
        }
    }

    if (!found) {
        char *path = getenv("PATH");
        (void)path;
        FILE *f = fopen("/proc/self/cmdline", "r");
        if (f) {
            fgets(exe_path, sizeof(exe_path), f);
            fclose(f);
            found = 1;
        }
    }

    fprintf(stderr, "[mscoree-unix] about to call mono_jit_init_version...\n");
    fflush(stderr);

    MonoDomain *domain = NULL;

    typedef MonoDomain* (*jit_init_t)(const char*);
    jit_init_t p_init = (jit_init_t)dlsym(g_mono_handle, "mono_jit_init");
    fprintf(stderr, "[mscoree-unix] mono_jit_init at %p\n", (void*)p_init);
    fflush(stderr);

    if (p_init) {
        fprintf(stderr, "[mscoree-unix] calling mono_jit_init...\n");
        fflush(stderr);
        domain = p_init("metalsharp");
        fprintf(stderr, "[mscoree-unix] mono_jit_init returned %p\n", (void*)domain);
        fflush(stderr);
    }

    if (!domain) {
        fprintf(stderr, "[mscoree-unix] mono_jit_init failed, trying mono_jit_init_version...\n");
        fflush(stderr);
        domain = p_mono_jit_init_version("metalsharp", "v4.0.30319");
        fprintf(stderr, "[mscoree-unix] mono_jit_init_version returned %p\n", (void*)domain);
        fflush(stderr);
    }
    if (!domain) {
        fprintf(stderr, "[mscoree-unix] mono_jit_init_version failed\n");
        return -1;
    }
    fprintf(stderr, "[mscoree-unix] mono runtime initialized\n");

    MonoImageOpenStatus status;

    const char *exe_to_load = found ? exe_path : getenv("_");
    if (!exe_to_load || !exe_to_load[0]) {
        fprintf(stderr, "[mscoree-unix] no exe path found\n");
        return -1;
    }

    char unix_path[1024];
    const char *src = exe_to_load;
    char *dst = unix_path;
    while (*src && dst < unix_path + sizeof(unix_path) - 1) {
        *dst++ = (*src == '\\') ? '/' : *src;
        src++;
    }
    *dst = 0;

    fprintf(stderr, "[mscoree-unix] opening image: %s\n", unix_path);
    MonoImage *image = p_mono_image_open(unix_path, &status);
    if (!image) {
        fprintf(stderr, "[mscoree-unix] mono_image_open failed: %d\n", status);
        return -1;
    }

    MonoAssembly *assembly = p_mono_assembly_load_from(image, unix_path, &status);
    if (!assembly) {
        fprintf(stderr, "[mscoree-unix] mono_assembly_load_from failed: %d\n", status);
        return -1;
    }
    fprintf(stderr, "[mscoree-unix] assembly loaded, calling mono_jit_exec\n");

    g_mono_initialized = 1;
    char *argv[] = { unix_path, NULL };
    int exit_code = p_mono_jit_exec(domain, assembly, 1, argv);
    fprintf(stderr, "[mscoree-unix] mono_jit_exec returned %d\n", exit_code);

    p_mono_thread_manage();
    p_mono_runtime_quit();

    return exit_code;
}

static int do_get_cor_version(void *args) {
    struct mscoree_cor_version_params *p = (struct mscoree_cor_version_params *)args;
    const char *ver = "v4.0.30319";
    strncpy(p->version, ver, sizeof(p->version) - 1);
    p->version_len = (int32_t)strlen(ver);
    return 0;
}

static int g_state = 0;

static NTSTATUS mscoree_unix_dispatch(unsigned int code, void *args) {
    fprintf(stderr, "[mscoree-unix] dispatch: code=%u args=%p state=%d\n", code, args, g_state);
    fflush(stderr);

    if (g_state == 0) {
        g_state = 1;
        return do_init(args);
    } else if (g_state == 1) {
        g_state = 2;
        return do_cor_exe_main(args);
    } else {
        return do_cor_exe_main(args);
    }
}
