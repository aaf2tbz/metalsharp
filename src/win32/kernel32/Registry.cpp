#include <metalsharp/Registry.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace metalsharp {
namespace win32 {

Registry::Registry() {}

Registry& Registry::instance() {
    static Registry reg;
    return reg;
}

std::string Registry::keyToString(HKEY hKey) {
    if (hKey == reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000000))) return "HKEY_CLASSES_ROOT";
    if (hKey == reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000001))) return "HKEY_CURRENT_USER";
    if (hKey == reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000002))) return "HKEY_LOCAL_MACHINE";
    if (hKey == reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000003))) return "HKEY_USERS";
    if (hKey == reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000005))) return "HKEY_CURRENT_CONFIG";

    auto it = m_openKeys.find(hKey);
    if (it != m_openKeys.end()) return it->second;
    return "";
}

std::string Registry::normalizePath(HKEY hKey, const std::string& subKey) {
    std::string root = keyToString(hKey);
    if (root.empty()) return "";

    std::string path = subKey;
    for (auto& c : path) {
        if (c == '\\') c = '/';
    }

    while (!path.empty() && path[0] == '/') path.erase(0, 1);
    while (!path.empty() && path.back() == '/') path.pop_back();

    std::string result = root + "/" + path;
    std::string lower;
    for (auto c : result) lower += tolower(c);
    return lower;
}

void Registry::seedSteam(const std::string& prefix) {
    auto set = [&](const std::string& key, const std::string& name, DWORD type, const std::vector<BYTE>& data) {
        m_store[key][name] = {type, data};
    };

    auto strVal = [](const std::string& s) -> std::vector<BYTE> {
        std::vector<BYTE> v(s.begin(), s.end());
        v.push_back(0);
        v.push_back(0);
        return v;
    };

    auto dwordVal = [](DWORD v) -> std::vector<BYTE> {
        std::vector<BYTE> d(4);
        memcpy(d.data(), &v, 4);
        return d;
    };

    std::string steamPath = "C:\\Program Files (x86)\\Steam";
    std::string steamExe = steamPath + "\\steam.exe";

    std::string hklmSteam = "hkey_local_machine/software/valve/steam";
    set(hklmSteam, "InstallPath", 1, strVal(steamPath));
    set(hklmSteam, "SteamPath", 1, strVal(steamPath));
    set(hklmSteam, "SteamExe", 1, strVal(steamExe));
    set(hklmSteam, "Language", 1, strVal("english"));
    set(hklmSteam, "ActiveProcess", 4, dwordVal(1));
    set(hklmSteam, "pid", 4, dwordVal(1337));
    set(hklmSteam, "SteamClientDll", 1, strVal(steamPath + "\\steamclient.dll"));
    set(hklmSteam, "SteamClientDll64", 1, strVal(steamPath + "\\steamclient64.dll"));
    set(hklmSteam, "Universe", 1, strVal("Public"));

    std::string hkcuSteam = "hkey_current_user/software/valve/steam";
    set(hkcuSteam, "Language", 1, strVal("english"));
    set(hkcuSteam, "AutoLoginUser", 1, strVal(""));
    set(hkcuSteam, "RememberPassword", 4, dwordVal(1));
    set(hkcuSteam, "AlreadyRetriedOfflineLogin", 4, dwordVal(0));
    set(hkcuSteam, "SteamPSM", 1, strVal(""));

    std::string hklmSteamApps = "hkey_local_machine/software/valve/steam/apps";
    set(hklmSteamApps, "", 1, strVal(""));

    std::string hklmWinCurVer = "hkey_local_machine/software/microsoft/windows nt/currentversion";
    set(hklmWinCurVer, "ProductName", 1, strVal("Windows 10 Pro"));
    set(hklmWinCurVer, "CurrentVersion", 1, strVal("6.3"));
    set(hklmWinCurVer, "CurrentBuildNumber", 1, strVal("19041"));
    set(hklmWinCurVer, "CurrentMajorVersionNumber", 4, dwordVal(10));
    set(hklmWinCurVer, "CurrentMinorVersionNumber", 4, dwordVal(0));
    set(hklmWinCurVer, "InstallationType", 1, strVal("Client"));
    set(hklmWinCurVer, "ProductId", 1, strVal("00000-00000-00000-AA000"));
    set(hklmWinCurVer, "BuildLabEx", 1, strVal("19041.1.amd64fre.vb_release.191206-1406"));
    set(hklmWinCurVer, "RegisteredOwner", 1, strVal("user"));
    set(hklmWinCurVer, "RegisteredOrganization", 1, strVal(""));

    std::string hklmWinRun = "hkey_local_machine/software/microsoft/windows/currentversion/run";
    set(hklmWinRun, "", 1, strVal(""));

    std::string hklmPolicies = "hkey_local_machine/software/policies";
    set(hklmPolicies, "", 1, strVal(""));

    std::string hklmSessionMgr = "hkey_local_machine/system/currentcontrolset/control/session manager";
    set(hklmSessionMgr, "SafeDllSearchMode", 4, dwordVal(1));

    std::string hkcuEnv = "hkey_current_user/environment";
    set(hkcuEnv, "TEMP", 1, strVal("C:\\Users\\user\\AppData\\Local\\Temp"));
    set(hkcuEnv, "TMP", 1, strVal("C:\\Users\\user\\AppData\\Local\\Temp"));
    set(hkcuEnv, "PATH", 1, strVal("C:\\Users\\user\\AppData\\Local\\Microsoft\\WindowsApps"));

    std::string hklmSysEnv = "hkey_local_machine/system/currentcontrolset/control/session manager/environment";
    set(hklmSysEnv, "ComSpec", 1, strVal("C:\\Windows\\system32\\cmd.exe"));
    set(hklmSysEnv, "OS", 1, strVal("Windows_NT"));
    set(hklmSysEnv, "PATH", 1, strVal("C:\\Windows\\system32;C:\\Windows;C:\\Windows\\System32\\Wbem"));
    set(hklmSysEnv, "PATHEXT", 1, strVal(".COM;.EXE;.BAT;.CMD;.VBS;.JS"));
    set(hklmSysEnv, "PROCESSOR_ARCHITECTURE", 1, strVal("AMD64"));
    set(hklmSysEnv, "SystemDrive", 1, strVal("C:"));
    set(hklmSysEnv, "SystemRoot", 1, strVal("C:\\Windows"));
    set(hklmSysEnv, "windir", 1, strVal("C:\\Windows"));
    set(hklmSysEnv, "TEMP", 1, strVal("C:\\Windows\\TEMP"));
    set(hklmSysEnv, "TMP", 1, strVal("C:\\Windows\\TEMP"));

    MS_INFO("Registry: Seeded Steam + Windows registry keys");
}

void Registry::init(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store.clear();
    m_openKeys.clear();
    m_nextKey = 0xA000;
    seedSteam(prefix);

    std::string regPath = prefix + "/registry.json";
    loadFromFile(regPath);
}

LONG Registry::openKey(HKEY hKey, const std::string& subKey, HKEY* result) {
    return openKeyEx(hKey, subKey, 0, 0, result);
}

LONG Registry::openKeyEx(HKEY hKey, const std::string& subKey, DWORD, DWORD, HKEY* result) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = normalizePath(hKey, subKey);
    if (path.empty()) return ERROR_PATH_NOT_FOUND;

    MS_INFO("Registry: OpenKey(\"%s\")", path.c_str());

    HKEY newKey = reinterpret_cast<HKEY>(m_nextKey);
    m_nextKey += 4;
    if (m_nextKey < 0xA000) m_nextKey = 0xA000;

    m_openKeys[newKey] = path;

    if (m_store.find(path) == m_store.end()) {
        m_store[path] = {};
    }

    if (result) *result = newKey;
    return ERROR_SUCCESS;
}

LONG Registry::createKeyEx(HKEY hKey, const std::string& subKey, DWORD, char*, DWORD, DWORD, void*, HKEY* phkResult, void*) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = normalizePath(hKey, subKey);
    if (path.empty()) return ERROR_PATH_NOT_FOUND;

    MS_INFO("Registry: CreateKey(\"%s\")", path.c_str());

    if (m_store.find(path) == m_store.end()) {
        m_store[path] = {};
    }

    HKEY newKey = reinterpret_cast<HKEY>(m_nextKey);
    m_nextKey += 4;
    if (m_nextKey < 0xA000) m_nextKey = 0xA000;

    m_openKeys[newKey] = path;
    if (phkResult) *phkResult = newKey;
    return ERROR_SUCCESS;
}

LONG Registry::closeKey(HKEY hKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_openKeys.erase(hKey);
    return ERROR_SUCCESS;
}

LONG Registry::queryValue(HKEY hKey, const std::string& valueName, DWORD* lpType, BYTE* lpData, DWORD* lpcbData) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = keyToString(hKey);
    if (path.empty()) return ERROR_PATH_NOT_FOUND;

    std::string lowerPath;
    for (auto c : path) lowerPath += tolower(c);

    auto it = m_store.find(lowerPath);
    if (it == m_store.end()) {
        MS_INFO("Registry: QueryValue key not found: \"%s\"", lowerPath.c_str());
        return ERROR_FILE_NOT_FOUND;
    }

    std::string lookupName = valueName;
    for (auto& c : lookupName) c = tolower(c);

    auto vit = it->second.find(lookupName);
    if (vit == it->second.end()) {
        if (lookupName.empty()) {
            RegistryValue empty = {1, {0, 0}};
            if (lpType) *lpType = empty.type;
            if (lpcbData) {
                if (lpData && *lpcbData >= empty.data.size()) {
                    memcpy(lpData, empty.data.data(), empty.data.size());
                }
                *lpcbData = static_cast<DWORD>(empty.data.size());
            }
            return ERROR_SUCCESS;
        }
        MS_INFO("Registry: QueryValue \"%s\" not found in \"%s\"", lookupName.c_str(), lowerPath.c_str());
        return ERROR_FILE_NOT_FOUND;
    }

    const RegistryValue& val = vit->second;
    MS_INFO("Registry: QueryValue(\"%s\\%s\") -> type=%u, size=%zu",
        lowerPath.c_str(), lookupName.c_str(), val.type, val.data.size());

    if (lpType) *lpType = val.type;
    if (lpcbData) {
        DWORD needed = static_cast<DWORD>(val.data.size());
        if (lpData && *lpcbData >= needed) {
            memcpy(lpData, val.data.data(), needed);
        } else if (lpData && *lpcbData < needed) {
            *lpcbData = needed;
            return 234;
        }
        *lpcbData = needed;
    }
    return ERROR_SUCCESS;
}

LONG Registry::setValue(HKEY hKey, const std::string& valueName, DWORD dwType, const BYTE* lpData, DWORD cbData) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = keyToString(hKey);
    if (path.empty()) return ERROR_PATH_NOT_FOUND;

    std::string lowerPath;
    for (auto c : path) lowerPath += tolower(c);

    std::string lookupName = valueName;
    for (auto& c : lookupName) c = tolower(c);

    RegistryValue val;
    val.type = dwType;
    val.data.resize(cbData);
    if (lpData && cbData > 0) {
        memcpy(val.data.data(), lpData, cbData);
    }

    m_store[lowerPath][lookupName] = std::move(val);

    MS_INFO("Registry: SetValue(\"%s\\%s\", type=%u, size=%u)",
        lowerPath.c_str(), lookupName.c_str(), dwType, cbData);
    return ERROR_SUCCESS;
}

LONG Registry::deleteValue(HKEY hKey, const std::string& valueName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = keyToString(hKey);
    if (path.empty()) return ERROR_PATH_NOT_FOUND;

    std::string lowerPath;
    for (auto c : path) lowerPath += tolower(c);

    std::string lookupName = valueName;
    for (auto& c : lookupName) c = tolower(c);

    auto it = m_store.find(lowerPath);
    if (it == m_store.end()) return ERROR_FILE_NOT_FOUND;
    it->second.erase(lookupName);
    return ERROR_SUCCESS;
}

LONG Registry::deleteKey(HKEY hKey, const std::string& subKey) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = normalizePath(hKey, subKey);
    if (path.empty()) return ERROR_PATH_NOT_FOUND;

    std::string lowerPath;
    for (auto c : path) lowerPath += tolower(c);

    m_store.erase(lowerPath);
    return ERROR_SUCCESS;
}

void Registry::saveToFile(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n";
    bool firstKey = true;
    for (auto& [keyPath, values] : m_store) {
        if (!firstKey) out << ",\n";
        firstKey = false;
        out << "  \"" << keyPath << "\": {\n";
        bool firstVal = true;
        for (auto& [name, val] : values) {
            if (!firstVal) out << ",\n";
            firstVal = false;
            out << "    \"" << name << "\": {\"type\": " << val.type << ", \"data\": [";
            for (size_t i = 0; i < val.data.size(); i++) {
                if (i > 0) out << ", ";
                out << static_cast<int>(val.data[i]);
            }
            out << "]}";
        }
        out << "\n  }";
    }
    out << "\n}\n";
    out.close();
    MS_INFO("Registry: Saved to %s", path.c_str());
}

void Registry::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    size_t pos = 0;
    auto skipWS = [&]() {
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\n' || content[pos] == '\r' || content[pos] == '\t')) pos++;
    };
    auto expect = [&](char c) -> bool {
        skipWS();
        if (pos < content.size() && content[pos] == c) { pos++; return true; }
        return false;
    };
    auto readString = [&]() -> std::string {
        skipWS();
        if (pos >= content.size() || content[pos] != '"') return "";
        pos++;
        std::string result;
        while (pos < content.size() && content[pos] != '"') {
            if (content[pos] == '\\' && pos + 1 < content.size()) { pos++; result += content[pos]; }
            else result += content[pos];
            pos++;
        }
        if (pos < content.size()) pos++;
        return result;
    };
    auto readNumber = [&]() -> int {
        skipWS();
        int n = 0;
        bool neg = false;
        if (pos < content.size() && content[pos] == '-') { neg = true; pos++; }
        while (pos < content.size() && content[pos] >= '0' && content[pos] <= '9') {
            n = n * 10 + (content[pos] - '0');
            pos++;
        }
        return neg ? -n : n;
    };

    if (!expect('{')) return;

    while (pos < content.size()) {
        skipWS();
        if (content[pos] == '}') break;

        std::string keyPath = readString();
        if (keyPath.empty()) break;

        if (!expect(':')) break;
        if (!expect('{')) break;

        std::unordered_map<std::string, RegistryValue> values;

        while (pos < content.size()) {
            skipWS();
            if (content[pos] == '}') { pos++; break; }
            if (content[pos] == ',') { pos++; continue; }

            std::string valName = readString();
            if (valName.empty()) break;

            if (!expect(':')) break;
            if (!expect('{')) break;

            RegistryValue rv;

            while (pos < content.size()) {
                skipWS();
                if (content[pos] == '}') { pos++; break; }
                if (content[pos] == ',') { pos++; continue; }

                std::string fieldName = readString();
                if (!expect(':')) break;

                if (fieldName == "type") {
                    rv.type = static_cast<DWORD>(readNumber());
                } else if (fieldName == "data") {
                    if (!expect('[')) break;
                    while (pos < content.size()) {
                        skipWS();
                        if (content[pos] == ']') { pos++; break; }
                        if (content[pos] == ',') { pos++; continue; }
                        rv.data.push_back(static_cast<BYTE>(readNumber()));
                    }
                }
            }

            values[valName] = std::move(rv);
        }

        m_store[keyPath] = std::move(values);
        skipWS();
        if (pos < content.size() && content[pos] == ',') pos++;
    }

    MS_INFO("Registry: Loaded from %s", path.c_str());
}

}
}
