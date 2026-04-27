#include <metalsharp/PELoader.h>
#include <metalsharp/PEHeader.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <fstream>
#include <sys/mman.h>

namespace metalsharp {

PELoader* PELoader::s_instance = nullptr;

PELoader::PELoader() { s_instance = this; }
PELoader::~PELoader() {
    if (m_mainModule.base) {
        munmap(m_mainModule.base, m_mainModule.size);
    }
    for (auto& [name, mod] : m_loadedDLLs) {
        if (mod.base && mod.isPE) {
            munmap(mod.base, mod.size);
        }
    }
    s_instance = nullptr;
}

void PELoader::registerShim(const std::string& dllName, ShimLibrary&& shim) {
    std::string lower = dllName;
    for (auto& c : lower) c = tolower(c);
    m_shims[lower] = std::move(shim);
}

void* PELoader::resolveFunction(const std::string& dllName, const std::string& funcName) {
    return resolveImport(dllName, funcName, 0xFFFF);
}

 bool PELoader::load(const std::string& path) {
     MS_INFO("PELoader: loading %s", path.c_str());
 
     m_mainModule.name = path;

     std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MS_INFO("PELoader: failed to open %s", path.c_str());
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        MS_INFO("PELoader: failed to read %s", path.c_str());
        return false;
    }

    if (!parsePE(m_mainModule, data.data(), fileSize)) return false;
    if (!mapSections(m_mainModule, data.data(), fileSize)) return false;
    if (!processRelocations(m_mainModule)) return false;
    if (!resolveImports(m_mainModule)) return false;

    MS_INFO("PELoader: loaded %s at %p, entry %p, size %u",
            path.c_str(), m_mainModule.base, m_mainModule.entryPoint, m_mainModule.size);
    return true;
}

bool PELoader::loadFromMemory(const uint8_t* data, size_t size) {
    if (!parsePE(m_mainModule, data, size)) return false;
    if (!mapSections(m_mainModule, data, size)) return false;
    if (!processRelocations(m_mainModule)) return false;
    if (!resolveImports(m_mainModule)) return false;
    return true;
}

bool PELoader::parsePE(LoadedModule& module, const uint8_t* rawData, size_t rawSize) {
    if (rawSize < sizeof(IMAGE_DOS_HEADER)) {
        MS_INFO("PELoader: file too small for DOS header");
        return false;
    }

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(rawData);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        MS_INFO("PELoader: not a valid PE file (bad DOS magic: 0x%04X)", dos->e_magic);
        return false;
    }

    if (dos->e_lfanew < 0 || (size_t)dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) > rawSize) {
        MS_INFO("PELoader: invalid e_lfanew offset");
        return false;
    }

    auto* peSig = reinterpret_cast<const uint32_t*>(rawData + dos->e_lfanew);
    if (*peSig != IMAGE_PE_SIGNATURE) {
        MS_INFO("PELoader: bad PE signature: 0x%08X", *peSig);
        return false;
    }

    auto* fileHeader = reinterpret_cast<const IMAGE_FILE_HEADER*>(
        rawData + dos->e_lfanew + 4);

    if (fileHeader->Machine != IMAGE_FILE_MACHINE_AMD64) {
        MS_INFO("PELoader: unsupported machine type: 0x%04X (only AMD64 supported)", fileHeader->Machine);
        return false;
    }

    if (fileHeader->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)) {
        MS_INFO("PELoader: optional header too small");
        return false;
    }

    auto* optHeader = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
        rawData + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));

    if (optHeader->Magic != IMAGE_OPTIONAL_MAGIC_PE32PLUS) {
        MS_INFO("PELoader: not PE32+ (magic: 0x%04X)", optHeader->Magic);
        return false;
    }

    m_imageBase = optHeader->ImageBase;
    m_sectionAlignment = optHeader->SectionAlignment;
    m_fileAlignment = optHeader->FileAlignment;

    module.size = optHeader->SizeOfImage;
    module.isPE = true;

    MS_INFO("PELoader: PE32+ image, %u sections, image base 0x%llX, size %u",
            fileHeader->NumberOfSections,
            (unsigned long long)optHeader->ImageBase,
            optHeader->SizeOfImage);

    return true;
}

bool PELoader::mapSections(LoadedModule& module, const uint8_t* rawData, size_t rawSize) {
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(rawData);
    auto* fileHeader = reinterpret_cast<const IMAGE_FILE_HEADER*>(
        rawData + dos->e_lfanew + 4);
    auto* optHeader = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
        rawData + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));
    auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        rawData + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + fileHeader->SizeOfOptionalHeader);

    uint32_t imageSize = alignUp(optHeader->SizeOfImage, 0x1000);

    uint8_t* mem = reinterpret_cast<uint8_t*>(mmap(
        nullptr, imageSize,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0));

    if (mem == MAP_FAILED) {
        MS_INFO("PELoader: mmap failed for image (%u bytes), errno=%d", imageSize, errno);
        return false;
    }

    memset(mem, 0, imageSize);

    uint32_t headerSize = alignUp(optHeader->SizeOfHeaders, m_fileAlignment);
    if (headerSize > rawSize) headerSize = rawSize;
    memcpy(mem, rawData, headerSize);

    for (uint16_t i = 0; i < fileHeader->NumberOfSections; i++) {
        const auto& sec = sections[i];
        if (sec.SizeOfRawData == 0) continue;

        uint32_t dstOffset = sec.VirtualAddress;
        uint32_t srcOffset = sec.PointerToRawData;
        uint32_t copySize = sec.SizeOfRawData;

        if (srcOffset + copySize > rawSize) {
            copySize = rawSize - srcOffset;
        }
        if (dstOffset + copySize > imageSize) {
            copySize = imageSize - dstOffset;
        }

        if (copySize > 0) {
            memcpy(mem + dstOffset, rawData + srcOffset, copySize);
        }

        const char* name = reinterpret_cast<const char*>(sec.Name);
        MS_INFO("PELoader: mapped section %.8s at RVA 0x%X (0x%X bytes, raw 0x%X)",
                name, sec.VirtualAddress, sec.VirtualSize, sec.SizeOfRawData);
    }

    for (uint16_t i = 0; i < fileHeader->NumberOfSections; i++) {
        const auto& sec = sections[i];
        if (sec.VirtualSize == 0) continue;

        uint32_t secSize = alignUp(sec.VirtualSize, 0x1000);
        if (sec.VirtualAddress + secSize <= imageSize) {
            mprotect(mem + sec.VirtualAddress, secSize, PROT_READ | PROT_WRITE | PROT_EXEC);
        }
    }

    module.base = mem;
    if (optHeader->AddressOfEntryPoint) {
        module.entryPoint = mem + optHeader->AddressOfEntryPoint;
    }

    m_delta = reinterpret_cast<uint64_t>(mem) - m_imageBase;

    return true;
}

bool PELoader::processRelocations(LoadedModule& module) {
    if (!module.base) return false;

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
    auto* fileHeader = reinterpret_cast<const IMAGE_FILE_HEADER*>(
        module.base + dos->e_lfanew + 4);
    auto* optHeader = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
        module.base + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));

    if (optHeader->DataDirectory[DIRECTORY_BASERELOC].Size == 0) {
        MS_INFO("PELoader: no relocations needed");
        return true;
    }

    uint32_t relocRVA = optHeader->DataDirectory[DIRECTORY_BASERELOC].VirtualAddress;
    uint32_t relocSize = optHeader->DataDirectory[DIRECTORY_BASERELOC].Size;

    auto* reloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(module.base + relocRVA);
    auto* relocEnd = reinterpret_cast<IMAGE_BASE_RELOCATION*>(module.base + relocRVA + relocSize);

    int relocCount = 0;

    while (reloc < relocEnd && reloc->SizeOfBlock > 0) {
        uint32_t pageRVA = reloc->VirtualAddress;
        uint32_t numEntries = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
        auto* entries = reinterpret_cast<uint16_t*>(reloc + 1);

        for (uint32_t i = 0; i < numEntries; i++) {
            uint16_t entry = entries[i];
            uint32_t type = entry >> 12;
            uint32_t offset = entry & 0xFFF;

            if (type == IMAGE_REL_BASED_DIR64) {
                uint64_t* target = reinterpret_cast<uint64_t*>(module.base + pageRVA + offset);
                *target += m_delta;
                relocCount++;
            } else if (type == IMAGE_REL_BASED_ABSOLUTE) {
                // skip
            } else {
                MS_INFO("PELoader: unsupported relocation type %u at 0x%X", type, pageRVA + offset);
            }
        }

        reloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
            reinterpret_cast<uint8_t*>(reloc) + reloc->SizeOfBlock);
    }

    MS_INFO("PELoader: processed %d relocations (delta 0x%llX)", relocCount, (unsigned long long)m_delta);
    return true;
}

bool PELoader::resolveImports(LoadedModule& module) {
    if (!module.base) return false;

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
    auto* optHeader = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
        module.base + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));

    if (optHeader->DataDirectory[DIRECTORY_IMPORT].Size == 0) {
        MS_INFO("PELoader: no imports");
        return true;
    }

    uint32_t importRVA = optHeader->DataDirectory[DIRECTORY_IMPORT].VirtualAddress;
    auto* importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module.base + importRVA);

    while (importDesc->Name && importDesc->FirstThunk) {
        const char* dllName = reinterpret_cast<const char*>(module.base + importDesc->Name);

        MS_INFO("PELoader: resolving imports from %s", dllName);

        auto* iat = reinterpret_cast<uint64_t*>(module.base + importDesc->FirstThunk);

        uint32_t thunkRVA = importDesc->OriginalFirstThunk
            ? importDesc->OriginalFirstThunk
            : importDesc->FirstThunk;

        auto* thunk = reinterpret_cast<uint64_t*>(module.base + thunkRVA);

        int resolved = 0;
        int failed = 0;

        while (*thunk) {
            uint64_t entry = *thunk;
            void* funcPtr = nullptr;
            const char* missingName = "";
            uint16_t missingOrdinal = 0;
            bool isOrdinal = false;

            if (entry & (1ULL << 63)) {
                uint16_t ordinal = static_cast<uint16_t>(entry & 0xFFFF);
                missingOrdinal = ordinal;
                isOrdinal = true;
                funcPtr = resolveImport(dllName, "", ordinal);
            } else {
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    module.base + (entry & 0x7FFFFFFF));
                missingName = reinterpret_cast<const char*>(ibn->Name);
                funcPtr = resolveImport(dllName, missingName, 0xFFFF);
            }

            if (funcPtr) {
                *iat = reinterpret_cast<uint64_t>(funcPtr);
                resolved++;
            } else {
                if (!isOrdinal) {
                    MS_INFO("PELoader:   MISSING %s!%s", dllName, missingName);
                } else {
                    MS_INFO("PELoader:   MISSING %s!ordinal_%u", dllName, missingOrdinal);
                }
                failed++;
                *iat = 0;
            }

            thunk++;
            iat++;
        }

        MS_INFO("PELoader: %s — %d resolved, %d failed", dllName, resolved, failed);
        importDesc++;
    }

    return true;
}

void* PELoader::resolveImport(const std::string& dllName, const std::string& funcName, uint16_t ordinal) {
    std::string lower = dllName;
    for (auto& c : lower) c = tolower(c);

    auto shimIt = m_shims.find(lower);
    if (shimIt != m_shims.end()) {
        if (!funcName.empty()) {
            auto it = shimIt->second.functions.find(funcName);
            if (it != shimIt->second.functions.end()) {
                return it->second();
            }
        }
        if (ordinal != 0xFFFF) {
            auto it = shimIt->second.ordinals.find(ordinal);
            if (it != shimIt->second.ordinals.end()) {
                return it->second();
            }
        }
    }

    auto dllIt = m_loadedDLLs.find(lower);
    if (dllIt != m_loadedDLLs.end()) {
        return getExportAddress(dllIt->second, funcName, ordinal);
    }

    LoadedModule depModule;
    if (loadDependency(lower, depModule)) {
        m_loadedDLLs[lower] = std::move(depModule);
        auto& stored = m_loadedDLLs[lower];
        return getExportAddress(stored, funcName, ordinal);
    }

    return nullptr;
}

void* PELoader::getExportAddress(LoadedModule& module, const std::string& funcName, uint16_t ordinal) {
    if (!module.base) return nullptr;

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
    auto* optHeader = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
        module.base + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));

    if (optHeader->DataDirectory[DIRECTORY_EXPORT].Size == 0) return nullptr;

    uint32_t exportRVA = optHeader->DataDirectory[DIRECTORY_EXPORT].VirtualAddress;
    auto* exportDir = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(module.base + exportRVA);

    auto* names = reinterpret_cast<uint32_t*>(module.base + exportDir->AddressOfNames);
    auto* functions = reinterpret_cast<uint32_t*>(module.base + exportDir->AddressOfFunctions);
    auto* ordinals = reinterpret_cast<uint16_t*>(module.base + exportDir->AddressOfNameOrdinals);

    if (!funcName.empty()) {
        for (uint32_t i = 0; i < exportDir->NumberOfNames; i++) {
            const char* name = reinterpret_cast<const char*>(module.base + names[i]);
            if (strcmp(name, funcName.c_str()) == 0) {
                uint16_t idx = ordinals[i];
                return module.base + functions[idx];
            }
        }
    }

    if (ordinal != 0xFFFF && ordinal >= exportDir->Base) {
        uint32_t idx = ordinal - exportDir->Base;
        if (idx < exportDir->NumberOfFunctions) {
            return module.base + functions[idx];
        }
    }

    return nullptr;
}

void* PELoader::lookupFunctionEntry(uint64_t controlPc, uint64_t* outImageBase) {
    struct RuntimeFunction {
        uint32_t BeginAddress;
        uint32_t EndAddress;
        uint32_t UnwindData;
    };

    for (auto& [name, mod] : m_loadedDLLs) {
        if (!mod.base || !mod.isPE) continue;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod.base);
        auto* opt = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
            mod.base + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));
        if (opt->DataDirectory[DIRECTORY_EXCEPTION].Size == 0) continue;

        uint64_t modBase = reinterpret_cast<uint64_t>(mod.base);
        uint64_t modEnd = modBase + mod.size;
        if (controlPc < modBase || controlPc >= modEnd) continue;

        uint32_t exceptRva = opt->DataDirectory[DIRECTORY_EXCEPTION].VirtualAddress;
        uint32_t exceptSize = opt->DataDirectory[DIRECTORY_EXCEPTION].Size;
        auto* funcs = reinterpret_cast<const RuntimeFunction*>(mod.base + exceptRva);
        size_t count = exceptSize / sizeof(RuntimeFunction);

        uint32_t rva = static_cast<uint32_t>(controlPc - modBase);
        for (size_t i = 0; i < count; i++) {
            if (rva >= funcs[i].BeginAddress && rva < funcs[i].EndAddress) {
                if (outImageBase) *outImageBase = modBase;
                MS_INFO("PELoader: lookupFunctionEntry(0x%llX) found in %s at RVA 0x%X",
                    (unsigned long long)controlPc, name.c_str(), rva);
                return const_cast<RuntimeFunction*>(&funcs[i]);
            }
        }
    }

    if (m_mainModule.base && m_mainModule.isPE) {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m_mainModule.base);
        auto* opt = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
            m_mainModule.base + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));
        if (opt->DataDirectory[DIRECTORY_EXCEPTION].Size > 0) {
            uint64_t modBase = reinterpret_cast<uint64_t>(m_mainModule.base);
            uint32_t exceptRva = opt->DataDirectory[DIRECTORY_EXCEPTION].VirtualAddress;
            uint32_t exceptSize = opt->DataDirectory[DIRECTORY_EXCEPTION].Size;
            auto* funcs = reinterpret_cast<const RuntimeFunction*>(m_mainModule.base + exceptRva);
            size_t count = exceptSize / sizeof(RuntimeFunction);

            uint32_t rva = static_cast<uint32_t>(controlPc - modBase);
            for (size_t i = 0; i < count; i++) {
                if (rva >= funcs[i].BeginAddress && rva < funcs[i].EndAddress) {
                    if (outImageBase) *outImageBase = modBase;
                    MS_INFO("PELoader: lookupFunctionEntry(0x%llX) found in main module at RVA 0x%X",
                        (unsigned long long)controlPc, rva);
                    return const_cast<RuntimeFunction*>(&funcs[i]);
                }
            }
        }
    }

    if (outImageBase) *outImageBase = 0;
    return nullptr;
}

bool PELoader::loadDependency(const std::string& dllName, LoadedModule& outModule) {
    std::vector<std::string> paths = m_searchPaths;
    if (!m_mainModule.name.empty()) {
        auto lastSlash = m_mainModule.name.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            paths.push_back(m_mainModule.name.substr(0, lastSlash));
        }
    }

    for (const auto& dir : paths) {
        std::string path = dir + "/" + dllName;
        std::ifstream f(path);
        if (f.is_open()) {
            f.close();
            return loadDLL(path, dllName);
        }
    }

    return false;
}

LoadedModule* PELoader::getModule(const std::string& name) {
    std::string lower = name;
    for (auto& c : lower) c = tolower(c);

    if (m_loadedDLLs.count(lower)) return &m_loadedDLLs[lower];
    return nullptr;
}

PELoader* PELoader::instance() { return s_instance; }

void PELoader::addSearchPath(const std::string& path) {
    m_searchPaths.push_back(path);
}

HMODULE PELoader::loadLibrary(const std::string& dllName) {
    std::string lower = dllName;
    for (auto& c : lower) c = tolower(c);

    auto existing = m_loadedDLLs.find(lower);
    if (existing != m_loadedDLLs.end()) {
        return reinterpret_cast<HMODULE>(existing->second.base);
    }

    std::string lowerWithDll = lower;
    if (lower.size() < 4 || lower.substr(lower.size()-4) != ".dll") {
        lowerWithDll = lower + ".dll";
    }

    for (auto& name : {lower, lowerWithDll}) {
        auto shimIt = m_shims.find(name);
        if (shimIt != m_shims.end()) {
            HMODULE fake = reinterpret_cast<HMODULE>(0x2);
            m_moduleHandles[fake] = name;
            return fake;
        }
    }

    for (const auto& dir : m_searchPaths) {
        for (auto& name : {lower, lowerWithDll}) {
            std::string path = dir + "/" + name;
            std::ifstream f(path);
            if (f.is_open()) {
                f.close();
                if (loadDLL(path, name)) {
                    return reinterpret_cast<HMODULE>(m_loadedDLLs[name].base);
                }
            }
        }
    }

    MS_INFO("PELoader: LoadLibrary(\"%s\") — not found, returning shim handle", dllName.c_str());
    HMODULE fake = reinterpret_cast<HMODULE>(0x2);
    m_moduleHandles[fake] = lowerWithDll;
    return fake;
}

void* PELoader::getProcAddress(HMODULE hModule, const std::string& funcName) {
    auto it = m_moduleHandles.find(hModule);
    if (it != m_moduleHandles.end()) {
        std::string lower = it->second;
        auto shimIt = m_shims.find(lower);
        if (shimIt != m_shims.end()) {
            auto fit = shimIt->second.functions.find(funcName);
            if (fit != shimIt->second.functions.end()) {
                MS_INFO("PELoader: GetProcAddress(%s, %s) -> %p", lower.c_str(), funcName.c_str(), fit->second());
                return fit->second();
            }
            MS_INFO("PELoader: GetProcAddress(%s, %s) -> NOT FOUND in shim", lower.c_str(), funcName.c_str());
        }
    }

    for (auto& [name, mod] : m_loadedDLLs) {
        if (reinterpret_cast<HMODULE>(mod.base) == hModule) {
            void* addr = getExportAddress(mod, funcName);
            MS_INFO("PELoader: GetProcAddress(PE:%s, %s) -> %p", name.c_str(), funcName.c_str(), addr);
            return addr;
        }
    }

    MS_INFO("PELoader: GetProcAddress(%p, %s) -> null", hModule, funcName.c_str());
    return nullptr;
}

bool PELoader::loadDLL(const std::string& path, const std::string& dllName) {
    MS_INFO("PELoader: loading DLL %s from %s", dllName.c_str(), path.c_str());

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t fileSize = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) return false;

    LoadedModule mod;
    mod.name = dllName;
    if (!parsePE(mod, data.data(), fileSize)) return false;
    if (!mapSections(mod, data.data(), fileSize)) return false;
    if (!processRelocations(mod)) return false;
    if (!resolveImports(mod)) return false;

    m_loadedDLLs[dllName] = std::move(mod);
    auto& stored = m_loadedDLLs[dllName];

    if (stored.entryPoint) {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(stored.base);
        auto* fileHdr = reinterpret_cast<const IMAGE_FILE_HEADER*>(stored.base + dos->e_lfanew + 4);
        if (fileHdr->Characteristics & IMAGE_FILE_DLL) {
            typedef int (*DllMainProc)(void*, unsigned long, void*);
            auto dllMain = reinterpret_cast<DllMainProc>(stored.entryPoint);
            dllMain(reinterpret_cast<void*>(stored.base), 1, nullptr);
            MS_INFO("PELoader: DllMain(%s) called", dllName.c_str());
        }
    }

    return true;
}

}
