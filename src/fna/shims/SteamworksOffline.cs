using System;

[assembly: System.Reflection.AssemblyVersion("10.0.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("10.0.0.0")]

namespace Steamworks
{
    public struct AppId_t
    {
        public uint m_AppId;
        public AppId_t(uint value) { m_AppId = value; }
        public static explicit operator AppId_t(uint value) { return new AppId_t(value); }
        public static explicit operator uint(AppId_t value) { return value.m_AppId; }
    }

    public struct SteamAPICall_t
    {
        public ulong m_SteamAPICall;
        public SteamAPICall_t(ulong value) { m_SteamAPICall = value; }
        public static explicit operator SteamAPICall_t(ulong value) { return new SteamAPICall_t(value); }
        public static explicit operator ulong(SteamAPICall_t value) { return value.m_SteamAPICall; }
    }

    public static class SteamAPI
    {
        public static bool Init() { return true; }
        public static bool InitSafe() { return true; }
        public static void Shutdown() {}
        public static void RunCallbacks() {}
        public static bool RestartAppIfNecessary(AppId_t appId) { return false; }
    }

    public static class SteamApps
    {
        public static string GetCurrentGameLanguage() { return "english"; }
    }

    public static class SteamUserStats
    {
        public static bool RequestCurrentStats() { return true; }
        public static SteamAPICall_t RequestGlobalStats(int historyDays) { return (SteamAPICall_t)1UL; }
        public static bool GetStat(string name, out int data) { data = 0; return true; }
        public static bool SetStat(string name, int data) { return true; }
        public static bool GetGlobalStat(string name, out double data) { data = 0.0; return true; }
        public static bool GetAchievement(string name, out bool achieved) { achieved = false; return true; }
        public static bool SetAchievement(string name) { return true; }
        public static bool StoreStats() { return true; }
    }
}
