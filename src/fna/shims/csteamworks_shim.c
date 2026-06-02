/// @file csteamworks_shim.c
/// @brief CSteamworks API shim for FNA/XNA games.
///
/// Implements the CSteamworks (Steamworks C API) functions used by FNA games.
/// The default path is an offline shim so Windows Steam builds can run without a
/// native Steam client. Set METALSHARP_FNA_STEAM_PASSTHROUGH=1 to forward to
/// libsteam_api.dylib for debugging or future real-Steam integration.
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* g_api;
static void* g_steamApps;
static void* g_steamUserStats;
static void* g_steamUser;
static char g_fakeSteamApps;
static char g_fakeSteamUserStats;
static char g_fakeSteamUser;

typedef int (*InitFlat_t)(char*);
typedef void* (*GetISteamApps_t)(void*, int, const char*);
typedef void* (*GetISteamUserStats_t)(void*, int, const char*);
typedef int (*GetHSteamPipe_t)(void);
typedef int (*GetHSteamUser_t)(void);
typedef void* (*SteamAppsV_t)(void);
typedef void* (*SteamUserStatsV_t)(void);
typedef void* (*SteamUserV_t)(void);

static int steam_passthrough_enabled(void) {
    const char* value = getenv("METALSHARP_FNA_STEAM_PASSTHROUGH");
    return value && strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
}

__attribute__((constructor)) static void _ctor(void) {
    if (steam_passthrough_enabled())
        g_api = dlopen("@loader_path/libsteam_api.dylib", RTLD_NOW | RTLD_GLOBAL);
}

static void cache_interfaces(void) {
    if (g_steamApps && g_steamUserStats)
        return;
    if (!g_api)
        return;

    SteamAppsV_t getApps = (SteamAppsV_t)dlsym(g_api, "SteamAPI_SteamApps_v009");
    SteamUserStatsV_t getStats = (SteamUserStatsV_t)dlsym(g_api, "SteamAPI_SteamUserStats_v013");
    SteamUserV_t getUser = (SteamUserV_t)dlsym(g_api, "SteamAPI_SteamUser_v021");

    if (getApps)
        g_steamApps = getApps();
    if (getStats)
        g_steamUserStats = getStats();
    if (getUser)
        g_steamUser = getUser();
}

int RestartAppIfNecessary(void* appId) {
    return 0;
}

int SteamAPI_RestartAppIfNecessary(unsigned int appId) {
    return 0;
}

int Init(void) {
    if (!steam_passthrough_enabled())
        return 1;
    if (!g_api)
        return 1;
    InitFlat_t fn = (InitFlat_t)dlsym(g_api, "SteamAPI_InitFlat");
    if (!fn)
        return 1;
    char errMsg[1024] = {0};
    int result = fn(errMsg);
    if (result == 0)
        cache_interfaces();
    return result == 0;
}

int SteamAPI_Init(void) {
    return Init();
}

int SteamAPI_IsSteamRunning(void) {
    return 1;
}

int SteamAPI_GetHSteamPipe(void) {
    return 1;
}

int SteamAPI_GetHSteamUser(void) {
    return 1;
}

const char* SteamAPI_GetSteamInstallPath(void) {
    return "";
}

void Shutdown(void) {
    if (!steam_passthrough_enabled())
        return;
    if (!g_api)
        return;
    void (*fn)(void) = dlsym(g_api, "SteamAPI_Shutdown");
    if (fn)
        fn();
}

void SteamAPI_Shutdown(void) {
    Shutdown();
}

void RunCallbacks(void) {
    if (!steam_passthrough_enabled())
        return;
    if (!g_api)
        return;
    void (*fn)(void) = dlsym(g_api, "SteamAPI_RunCallbacks");
    if (fn)
        fn();
}

void SteamAPI_RunCallbacks(void) {
    RunCallbacks();
}

void SteamAPI_RegisterCallback(void* callback, int callbackId) {
    (void)callback;
    (void)callbackId;
}

void SteamAPI_UnregisterCallback(void* callback) {
    (void)callback;
}

void SteamAPI_RegisterCallResult(void* callback, unsigned long long apiCall) {
    (void)callback;
    (void)apiCall;
}

void SteamAPI_UnregisterCallResult(void* callback, unsigned long long apiCall) {
    (void)callback;
    (void)apiCall;
}

void SteamAPI_ReleaseCurrentThreadMemory(void) {}

/* ISteamApps wrappers */
typedef const char* (*Apps_GetCurrentGameLanguage_t)(void*);
typedef int (*Apps_BIsSubscribed_t)(void*);
typedef int (*Apps_BIsDlcInstalled_t)(void*, unsigned int);

void* SteamAPI_SteamApps_v009(void) {
    if (!steam_passthrough_enabled())
        return &g_fakeSteamApps;
    cache_interfaces();
    return g_steamApps ? g_steamApps : &g_fakeSteamApps;
}

void* ISteamApps_GetCurrentGameLanguage(void) {
    if (!steam_passthrough_enabled())
        return (void*)"english";
    if (!g_api || !g_steamApps)
        return NULL;
    Apps_GetCurrentGameLanguage_t fn =
        (Apps_GetCurrentGameLanguage_t)dlsym(g_api, "SteamAPI_ISteamApps_GetCurrentGameLanguage");
    return fn ? (void*)fn(g_steamApps) : NULL;
}

const char* SteamAPI_ISteamApps_GetCurrentGameLanguage(void* self) {
    (void)self;
    return (const char*)ISteamApps_GetCurrentGameLanguage();
}

int SteamAPI_ISteamApps_BIsSubscribed(void* self) {
    (void)self;
    return 1;
}

int SteamAPI_ISteamApps_BIsDlcInstalled(void* self, unsigned int appId) {
    (void)self;
    (void)appId;
    return 0;
}

/* ISteamUserStats wrappers */
typedef int (*Stats_RequestCurrentStats_t)(void*);
typedef int (*Stats_GetStat_t)(void*, const char*, int*);
typedef int (*Stats_SetStat_t)(void*, const char*, int);
typedef int (*Stats_GetAchievement_t)(void*, const char*, int*);
typedef int (*Stats_SetAchievement_t)(void*, const char*);
typedef int (*Stats_StoreStats_t)(void*);
typedef int (*Stats_RequestGlobalStats_t)(void*, int);
typedef int (*Stats_GetGlobalStatDouble_t)(void*, const char*, double*);
typedef int (*Stats_GetGlobalStatInt64_t)(void*, const char*, long long*);

void* SteamAPI_SteamUserStats_v013(void) {
    if (!steam_passthrough_enabled())
        return &g_fakeSteamUserStats;
    cache_interfaces();
    return g_steamUserStats ? g_steamUserStats : &g_fakeSteamUserStats;
}

int ISteamUserStats_RequestCurrentStats(void) {
    if (!steam_passthrough_enabled())
        return 1;
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_RequestCurrentStats_t fn =
        (Stats_RequestCurrentStats_t)dlsym(g_api, "SteamAPI_ISteamUserStats_RequestCurrentStats");
    return fn ? fn(g_steamUserStats) : 0;
}

int SteamAPI_ISteamUserStats_RequestCurrentStats(void* self) {
    (void)self;
    return ISteamUserStats_RequestCurrentStats();
}

int ISteamUserStats_GetStat(void* name, int* data) {
    if (!steam_passthrough_enabled()) {
        if (data)
            *data = 0;
        return 1;
    }
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_GetStat_t fn = (Stats_GetStat_t)dlsym(g_api, "SteamAPI_ISteamUserStats_GetStatInt32");
    return fn ? fn(g_steamUserStats, name, data) : 0;
}

int SteamAPI_ISteamUserStats_GetStatInt32(void* self, const char* name, int* data) {
    (void)self;
    return ISteamUserStats_GetStat((void*)name, data);
}

int SteamAPI_ISteamUserStats_GetStatFloat(void* self, const char* name, float* data) {
    (void)self;
    (void)name;
    if (data)
        *data = 0.0f;
    return 1;
}

int ISteamUserStats_SetStat(void* name, int data) {
    if (!steam_passthrough_enabled())
        return 1;
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_SetStat_t fn = (Stats_SetStat_t)dlsym(g_api, "SteamAPI_ISteamUserStats_SetStatInt32");
    return fn ? fn(g_steamUserStats, name, data) : 0;
}

int SteamAPI_ISteamUserStats_SetStatInt32(void* self, const char* name, int data) {
    (void)self;
    return ISteamUserStats_SetStat((void*)name, data);
}

int SteamAPI_ISteamUserStats_SetStatFloat(void* self, const char* name, float data) {
    (void)self;
    (void)name;
    (void)data;
    return 1;
}

int ISteamUserStats_GetAchievement(void* name, int* achieved) {
    if (!steam_passthrough_enabled()) {
        if (achieved)
            *achieved = 0;
        return 1;
    }
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_GetAchievement_t fn = (Stats_GetAchievement_t)dlsym(g_api, "SteamAPI_ISteamUserStats_GetAchievement");
    return fn ? fn(g_steamUserStats, name, achieved) : 0;
}

int SteamAPI_ISteamUserStats_GetAchievement(void* self, const char* name, int* achieved) {
    (void)self;
    return ISteamUserStats_GetAchievement((void*)name, achieved);
}

int ISteamUserStats_SetAchievement(void* name) {
    if (!steam_passthrough_enabled())
        return 1;
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_SetAchievement_t fn = (Stats_SetAchievement_t)dlsym(g_api, "SteamAPI_ISteamUserStats_SetAchievement");
    return fn ? fn(g_steamUserStats, name) : 0;
}

int SteamAPI_ISteamUserStats_SetAchievement(void* self, const char* name) {
    (void)self;
    return ISteamUserStats_SetAchievement((void*)name);
}

int SteamAPI_ISteamUserStats_ClearAchievement(void* self, const char* name) {
    (void)self;
    (void)name;
    return 1;
}

int ISteamUserStats_StoreStats(void) {
    if (!steam_passthrough_enabled())
        return 1;
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_StoreStats_t fn = (Stats_StoreStats_t)dlsym(g_api, "SteamAPI_ISteamUserStats_StoreStats");
    return fn ? fn(g_steamUserStats) : 0;
}

int SteamAPI_ISteamUserStats_StoreStats(void* self) {
    (void)self;
    return ISteamUserStats_StoreStats();
}

int ISteamUserStats_RequestGlobalStats(int historyDays) {
    if (!steam_passthrough_enabled())
        return 1;
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_RequestGlobalStats_t fn =
        (Stats_RequestGlobalStats_t)dlsym(g_api, "SteamAPI_ISteamUserStats_RequestGlobalStats");
    return fn ? fn(g_steamUserStats, historyDays) : 0;
}

int SteamAPI_ISteamUserStats_RequestGlobalStats(void* self, int historyDays) {
    (void)self;
    return ISteamUserStats_RequestGlobalStats(historyDays);
}

int ISteamUserStats_GetGlobalStat(void* name, double* data) {
    if (!steam_passthrough_enabled()) {
        if (data)
            *data = 0.0;
        return 1;
    }
    if (!g_api || !g_steamUserStats)
        return 0;
    Stats_GetGlobalStatDouble_t fn =
        (Stats_GetGlobalStatDouble_t)dlsym(g_api, "SteamAPI_ISteamUserStats_GetGlobalStatDouble");
    return fn ? fn(g_steamUserStats, name, data) : 0;
}

int SteamAPI_ISteamUserStats_GetGlobalStatDouble(void* self, const char* name, double* data) {
    (void)self;
    return ISteamUserStats_GetGlobalStat((void*)name, data);
}

int SteamAPI_ISteamUserStats_GetGlobalStatInt64(void* self, const char* name, long long* data) {
    (void)self;
    (void)name;
    if (data)
        *data = 0;
    return 1;
}

/* SteamClient / SteamUser for getting Steam ID */
void* SteamClient_(void) {
    if (!steam_passthrough_enabled())
        return NULL;
    if (!g_api)
        return NULL;
    void* (*fn)(void) = dlsym(g_api, "SteamClient");
    return fn ? fn() : NULL;
}

void* SteamAPI_SteamUser_v021(void) {
    if (!steam_passthrough_enabled())
        return &g_fakeSteamUser;
    cache_interfaces();
    return g_steamUser ? g_steamUser : &g_fakeSteamUser;
}

typedef void* (*GetSteamID_t)(void*);
void* ISteamUser_GetSteamID(void) {
    if (!steam_passthrough_enabled())
        return NULL;
    if (!g_api || !g_steamUser)
        return NULL;
    GetSteamID_t fn = (GetSteamID_t)dlsym(g_api, "SteamAPI_ISteamUser_GetSteamID");
    return fn ? fn(g_steamUser) : NULL;
}

unsigned long long SteamAPI_ISteamUser_GetSteamID(void* self) {
    (void)self;
    return 76561197960287930ULL;
}
