#include <stddef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static void *g_api;
static void *g_steamApps;
static void *g_steamUserStats;
static void *g_steamUser;

typedef int (*InitFlat_t)(char *);
typedef void *(*GetISteamApps_t)(void *, int, const char *);
typedef void *(*GetISteamUserStats_t)(void *, int, const char *);
typedef int (*GetHSteamPipe_t)(void);
typedef int (*GetHSteamUser_t)(void);
typedef void *(*SteamAppsV_t)(void);
typedef void *(*SteamUserStatsV_t)(void);
typedef void *(*SteamUserV_t)(void);

__attribute__((constructor)) static void _ctor(void) {
    g_api = dlopen("@loader_path/libsteam_api.dylib", RTLD_NOW | RTLD_GLOBAL);
}

static void cache_interfaces(void) {
    if (g_steamApps && g_steamUserStats) return;
    if (!g_api) return;
    
    SteamAppsV_t getApps = (SteamAppsV_t)dlsym(g_api, "SteamAPI_SteamApps_v009");
    SteamUserStatsV_t getStats = (SteamUserStatsV_t)dlsym(g_api, "SteamAPI_SteamUserStats_v013");
    SteamUserV_t getUser = (SteamUserV_t)dlsym(g_api, "SteamAPI_SteamUser_v021");
    
    if (getApps) g_steamApps = getApps();
    if (getStats) g_steamUserStats = getStats();
    if (getUser) g_steamUser = getUser();
}

int RestartAppIfNecessary(void *appId) { return 0; }

int Init(void) {
    if (!g_api) return 0;
    InitFlat_t fn = (InitFlat_t)dlsym(g_api, "SteamAPI_InitFlat");
    if (!fn) return 0;
    char errMsg[1024] = {0};
    int result = fn(errMsg);
    if (result == 0) cache_interfaces();
    return result == 0;
}

void Shutdown(void) {
    if (!g_api) return;
    void (*fn)(void) = dlsym(g_api, "SteamAPI_Shutdown");
    if (fn) fn();
}

void RunCallbacks(void) {
    if (!g_api) return;
    void (*fn)(void) = dlsym(g_api, "SteamAPI_RunCallbacks");
    if (fn) fn();
}

/* ISteamApps wrappers */
typedef const char *(*Apps_GetCurrentGameLanguage_t)(void *);
typedef int (*Apps_BIsSubscribed_t)(void *);
typedef int (*Apps_BIsDlcInstalled_t)(void *, unsigned int);

void *ISteamApps_GetCurrentGameLanguage(void) {
    if (!g_api || !g_steamApps) return NULL;
    Apps_GetCurrentGameLanguage_t fn = (Apps_GetCurrentGameLanguage_t)dlsym(g_api, "SteamAPI_ISteamApps_GetCurrentGameLanguage");
    return fn ? fn(g_steamApps) : NULL;
}

/* ISteamUserStats wrappers */
typedef int (*Stats_RequestCurrentStats_t)(void *);
typedef int (*Stats_GetStat_t)(void *, const char *, int *);
typedef int (*Stats_SetStat_t)(void *, const char *, int);
typedef int (*Stats_GetAchievement_t)(void *, const char *, int *);
typedef int (*Stats_SetAchievement_t)(void *, const char *);
typedef int (*Stats_StoreStats_t)(void *);
typedef int (*Stats_RequestGlobalStats_t)(void *, int);
typedef int (*Stats_GetGlobalStatDouble_t)(void *, const char *, double *);
typedef int (*Stats_GetGlobalStatInt64_t)(void *, const char *, long long *);

int ISteamUserStats_RequestCurrentStats(void) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_RequestCurrentStats_t fn = (Stats_RequestCurrentStats_t)dlsym(g_api, "SteamAPI_ISteamUserStats_RequestCurrentStats");
    return fn ? fn(g_steamUserStats) : 0;
}

int ISteamUserStats_GetStat(void *name, int *data) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_GetStat_t fn = (Stats_GetStat_t)dlsym(g_api, "SteamAPI_ISteamUserStats_GetStatInt32");
    return fn ? fn(g_steamUserStats, name, data) : 0;
}

int ISteamUserStats_SetStat(void *name, int data) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_SetStat_t fn = (Stats_SetStat_t)dlsym(g_api, "SteamAPI_ISteamUserStats_SetStatInt32");
    return fn ? fn(g_steamUserStats, name, data) : 0;
}

int ISteamUserStats_GetAchievement(void *name, int *achieved) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_GetAchievement_t fn = (Stats_GetAchievement_t)dlsym(g_api, "SteamAPI_ISteamUserStats_GetAchievement");
    return fn ? fn(g_steamUserStats, name, achieved) : 0;
}

int ISteamUserStats_SetAchievement(void *name) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_SetAchievement_t fn = (Stats_SetAchievement_t)dlsym(g_api, "SteamAPI_ISteamUserStats_SetAchievement");
    return fn ? fn(g_steamUserStats, name) : 0;
}

int ISteamUserStats_StoreStats(void) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_StoreStats_t fn = (Stats_StoreStats_t)dlsym(g_api, "SteamAPI_ISteamUserStats_StoreStats");
    return fn ? fn(g_steamUserStats) : 0;
}

int ISteamUserStats_RequestGlobalStats(int historyDays) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_RequestGlobalStats_t fn = (Stats_RequestGlobalStats_t)dlsym(g_api, "SteamAPI_ISteamUserStats_RequestGlobalStats");
    return fn ? fn(g_steamUserStats, historyDays) : 0;
}

int ISteamUserStats_GetGlobalStat(void *name, double *data) {
    if (!g_api || !g_steamUserStats) return 0;
    Stats_GetGlobalStatDouble_t fn = (Stats_GetGlobalStatDouble_t)dlsym(g_api, "SteamAPI_ISteamUserStats_GetGlobalStatDouble");
    return fn ? fn(g_steamUserStats, name, data) : 0;
}

/* SteamClient / SteamUser for getting Steam ID */
void *SteamClient_(void) {
    if (!g_api) return NULL;
    void *(*fn)(void) = dlsym(g_api, "SteamClient");
    return fn ? fn() : NULL;
}

typedef void *(*GetSteamID_t)(void *);
void *ISteamUser_GetSteamID(void) {
    if (!g_api || !g_steamUser) return NULL;
    GetSteamID_t fn = (GetSteamID_t)dlsym(g_api, "SteamAPI_ISteamUser_GetSteamID");
    return fn ? fn(g_steamUser) : NULL;
}
