#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace metalsharp {

struct LoadedModule {
    std::string name;
    uint8_t* base = nullptr;
    uint32_t size = 0;
    uint8_t* entryPoint = nullptr;
    bool isPE = false;

    uint8_t* rvaToPtr(uint32_t rva) const {
        if (!base || rva >= size) return nullptr;
        return base + rva;
    }
};

using ExportedFunction = std::function<void*()>;

struct ShimLibrary {
    std::string name;
    std::unordered_map<std::string, ExportedFunction> functions;
    std::unordered_map<uint16_t, ExportedFunction> ordinals;
};

class PELoader {
public:
    PELoader();
    ~PELoader();

    bool load(const std::string& path);
    bool loadFromMemory(const uint8_t* data, size_t size);

    LoadedModule* getModule(const std::string& name);
    LoadedModule* getMainModule() { return &m_mainModule; }

    void registerShim(const std::string& dllName, ShimLibrary&& shim);
    void* resolveFunction(const std::string& dllName, const std::string& funcName);

    uint8_t* getBase() const { return m_mainModule.base; }

private:
    bool parsePE(LoadedModule& module, const uint8_t* rawData, size_t rawSize);
    bool mapSections(LoadedModule& module, const uint8_t* rawData, size_t rawSize);
    bool processRelocations(LoadedModule& module);
    bool resolveImports(LoadedModule& module);
    bool loadDependency(const std::string& dllName, LoadedModule& outModule);

    void* resolveImport(const std::string& dllName, const std::string& funcName, uint16_t ordinal);
    void* getExportAddress(LoadedModule& module, const std::string& funcName, uint16_t ordinal = 0xFFFF);

    uint64_t alignUp(uint64_t value, uint64_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    LoadedModule m_mainModule;
    std::unordered_map<std::string, LoadedModule> m_loadedDLLs;
    std::unordered_map<std::string, ShimLibrary> m_shims;

    uint64_t m_imageBase = 0;
    uint32_t m_sectionAlignment = 0;
    uint32_t m_fileAlignment = 0;
    uint64_t m_delta = 0;
};

}
