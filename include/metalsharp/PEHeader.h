#pragma once

#include <cstdint>
#include <cstddef>

namespace metalsharp {

#pragma pack(push, 1)

struct IMAGE_DOS_HEADER {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;
};

static_assert(sizeof(IMAGE_DOS_HEADER) == 64);

struct IMAGE_FILE_HEADER {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct IMAGE_DATA_DIRECTORY {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct IMAGE_OPTIONAL_HEADER64 {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[16];
};

static_assert(sizeof(IMAGE_OPTIONAL_HEADER64) == 240);

struct IMAGE_SECTION_HEADER {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct IMAGE_IMPORT_DESCRIPTOR {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
};

struct IMAGE_THUNK_DATA64 {
    uint64_t AddressOfData;
};

struct IMAGE_IMPORT_BY_NAME {
    uint16_t Hint;
    uint8_t  Name[1];
};

struct IMAGE_BASE_RELOCATION {
    uint32_t VirtualAddress;
    uint32_t SizeOfBlock;
};

struct IMAGE_TLS_DIRECTORY64 {
    uint64_t StartAddressOfRawData;
    uint64_t EndAddressOfRawData;
    uint64_t AddressOfIndex;
    uint64_t AddressOfCallBacks;
    uint32_t SizeOfZeroFill;
    uint32_t Characteristics;
};

struct IMAGE_EXPORT_DIRECTORY {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Name;
    uint32_t Base;
    uint32_t NumberOfFunctions;
    uint32_t NumberOfNames;
    uint32_t AddressOfFunctions;
    uint32_t AddressOfNames;
    uint32_t AddressOfNameOrdinals;
};

struct IMAGE_RESOURCE_DIRECTORY {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint16_t NumberOfNamedEntries;
    uint16_t NumberOfIdEntries;
};

struct IMAGE_RESOURCE_DIRECTORY_ENTRY {
    uint32_t Name;
    uint32_t OffsetToData;
};

struct IMAGE_RESOURCE_DATA_ENTRY {
    uint32_t OffsetToData;
    uint32_t Size;
    uint32_t CodePage;
    uint32_t Reserved;
};

struct IMAGE_DELAY_IMPORT_DESCRIPTOR {
    uint32_t grfAttrs;
    uint32_t rvaDLLName;
    uint32_t rvaHmod;
    uint32_t rvaIAT;
    uint32_t rvaINT;
    uint32_t rvaBoundIAT;
    uint32_t rvaUnloadIAT;
    uint32_t dwTimeStamp;
};

#pragma pack(pop)

constexpr uint16_t IMAGE_DOS_SIGNATURE     = 0x5A4D;
constexpr uint32_t IMAGE_PE_SIGNATURE      = 0x00004550;
constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
constexpr uint16_t IMAGE_FILE_MACHINE_I386  = 0x014C;
constexpr uint16_t IMAGE_FILE_DLL          = 0x2000;

constexpr uint16_t IMAGE_OPTIONAL_MAGIC_PE32PLUS = 0x20B;
constexpr uint16_t IMAGE_OPTIONAL_MAGIC_PE32     = 0x10B;

constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
constexpr uint32_t IMAGE_SCN_MEM_READ    = 0x40000000;
constexpr uint32_t IMAGE_SCN_MEM_WRITE   = 0x80000000;
constexpr uint32_t IMAGE_SCN_CNT_CODE    = 0x00000020;
constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;

constexpr uint32_t IMAGE_REL_BASED_ABSOLUTE = 0;
constexpr uint32_t IMAGE_REL_BASED_DIR64    = 10;

constexpr int DIRECTORY_EXPORT    = 0;
constexpr int DIRECTORY_IMPORT    = 1;
constexpr int DIRECTORY_RESOURCE  = 2;
constexpr int DIRECTORY_EXCEPTION = 3;
constexpr int DIRECTORY_SECURITY  = 4;
constexpr int DIRECTORY_BASERELOC = 5;
constexpr int DIRECTORY_DEBUG     = 6;
constexpr int DIRECTORY_TLS       = 9;
constexpr int DIRECTORY_LOAD_CONFIG = 10;
constexpr int DIRECTORY_DELAY_IMPORT = 13;

constexpr uint32_t DLL_PROCESS_ATTACH = 1;
constexpr uint32_t DLL_THREAD_ATTACH = 2;
constexpr uint32_t DLL_THREAD_DETACH = 3;
constexpr uint32_t DLL_PROCESS_DETACH = 0;

}
