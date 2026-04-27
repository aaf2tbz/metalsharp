#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace metalsharp {
namespace win32 {

typedef int32_t LONG;
typedef uint32_t DWORD;
typedef uint8_t BYTE;
typedef void* HKEY;

const LONG ERROR_SUCCESS = 0;
const LONG ERROR_FILE_NOT_FOUND = 2;
const LONG ERROR_PATH_NOT_FOUND = 3;

struct RegistryValue {
    DWORD type;
    std::vector<BYTE> data;
};

class Registry {
public:
    static Registry& instance();

    void init(const std::string& prefix);

    LONG openKey(HKEY hKey, const std::string& subKey, HKEY* result);
    LONG openKeyEx(HKEY hKey, const std::string& subKey, DWORD ulOptions, DWORD samDesired, HKEY* result);
    LONG createKeyEx(HKEY hKey, const std::string& subKey, DWORD reserved, char* lpClass,
        DWORD dwOptions, DWORD samDesired, void* lpSecurityAttributes, HKEY* phkResult, void* lpdwDisposition);
    LONG closeKey(HKEY hKey);
    LONG queryValue(HKEY hKey, const std::string& valueName, DWORD* lpType, BYTE* lpData, DWORD* lpcbData);
    LONG setValue(HKEY hKey, const std::string& valueName, DWORD dwType, const BYTE* lpData, DWORD cbData);
    LONG deleteValue(HKEY hKey, const std::string& valueName);
    LONG deleteKey(HKEY hKey, const std::string& subKey);

    void saveToFile(const std::string& path);
    void loadFromFile(const std::string& path);

private:
    Registry();

    std::string normalizePath(HKEY hKey, const std::string& subKey);
    std::string keyToString(HKEY hKey);

    void seedSteam(const std::string& prefix);

    std::unordered_map<std::string, std::unordered_map<std::string, RegistryValue>> m_store;
    std::unordered_map<HKEY, std::string> m_openKeys;
    uintptr_t m_nextKey = 0xA000;
    std::mutex m_mutex;
};

}
}
