/// @file unix_steamclient.c
/// @brief Steam client bridge forwarding to native libsteam_api.
///
/// Implements the ISteamClient interface by loading libsteam_api.dylib and forwarding calls to the native Steam
/// runtime. Handles Steam API initialization, user authentication, and AppID registration so MetalSharp games can use
/// Steam features natively.
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wine/unixlib.h"
#include <winbase.h>
#include <windef.h>
#include <winternl.h>

WINE_DEFAULT_DEBUG_CHANNEL(steamclient);

static void* steamclient_handle;

static void* (*p_CreateInterface)(const char* name, int* return_code);
static int8_t (*p_Steam_BGetCallback)(int32_t, void*, int32_t*);
static int8_t (*p_Steam_GetAPICallResult)(int32_t, uint64_t, void*, int, int, int8_t*);
static int8_t (*p_Steam_FreeLastCallback)(int32_t);
static void (*p_Steam_ReleaseThreadLocalMemory)(int);

NTSTATUS steamclient_init(void* args) {
    char path[4096];

    if (steamclient_handle)
        return 0;

    const char* install_path = getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH");
    if (install_path)
        snprintf(path, sizeof(path), "%s/steamclient.dylib", install_path);
    else {
        const char* home = getenv("HOME");
        snprintf(path, sizeof(path),
                 "%s/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib",
                 home ? home : "/Users/alexmondello");
    }

    TRACE("Loading steamclient from %s\n", debugstr_a(path));

    if (!(steamclient_handle = dlopen(path, RTLD_NOW))) {
        ERR("unable to load native steamclient library: %s\n", dlerror());
        return -1;
    }

#define LOAD_FUNC(x)                                                                                                   \
    if (!(p_##x = (typeof(p_##x))dlsym(steamclient_handle, #x))) {                                                     \
        ERR("unable to load %s\n", #x);                                                                                \
        return -1;                                                                                                     \
    }

    LOAD_FUNC(CreateInterface);
    LOAD_FUNC(Steam_BGetCallback);
    LOAD_FUNC(Steam_GetAPICallResult);
    LOAD_FUNC(Steam_FreeLastCallback);
    LOAD_FUNC(Steam_ReleaseThreadLocalMemory);

    TRACE("Loaded host steamclient\n");
    return 0;
}

NTSTATUS steamclient_CreateInterface(void* args) {
    struct {
        void* ret;
        const char* name;
        int* return_code;
    }* params = args;
    params->ret = p_CreateInterface(params->name, params->return_code);
    return 0;
}

NTSTATUS steamclient_Steam_BGetCallback(void* args) {
    struct {
        int8_t ret;
        int32_t pipe;
        void* msg;
        int32_t* ignored;
    }* params = args;
    params->ret = p_Steam_BGetCallback(params->pipe, params->msg, params->ignored);
    return 0;
}

NTSTATUS steamclient_Steam_GetAPICallResult(void* args) {
    struct {
        int8_t ret;
        int32_t pipe;
        uint64_t call;
        void* callback;
        int len;
        int id;
        int8_t* failed;
    }* params = args;
    params->ret =
        p_Steam_GetAPICallResult(params->pipe, params->call, params->callback, params->len, params->id, params->failed);
    return 0;
}

NTSTATUS steamclient_Steam_FreeLastCallback(void* args) {
    struct {
        int8_t ret;
        int32_t pipe;
    }* params = args;
    params->ret = p_Steam_FreeLastCallback(params->pipe);
    return 0;
}

NTSTATUS steamclient_Steam_ReleaseThreadLocalMemory(void* args) {
    struct {
        int thread_exit;
    }* params = args;
    p_Steam_ReleaseThreadLocalMemory(params->thread_exit);
    return 0;
}

NTSTATUS steamclient_Steam_IsKnownInterface(void* args) {
    struct {
        int8_t ret;
        const char* version;
    }* params = args;
    params->ret = 1;
    return 0;
}

NTSTATUS steamclient_Steam_NotifyMissingInterface(void* args) {
    return 0;
}
