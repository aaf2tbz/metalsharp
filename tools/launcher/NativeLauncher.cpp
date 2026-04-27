#include <metalsharp/PELoader.h>
#include <metalsharp/PEHeader.h>
#include <metalsharp/Kernel32Shim.h>
#include <metalsharp/NtdllShim.h>
#include <metalsharp/ExtraShims.h>
#include <metalsharp/Logger.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#ifndef METALSHARP_VERSION
#define METALSHARP_VERSION "0.2.0"
#endif

using namespace metalsharp;

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Usage: %s <game.exe> [args...]\n"
        "\n"
        "MetalSharp %s — Native D3D→Metal translation layer\n"
        "Loads Windows x86_64 executables and translates D3D11/D3D12 calls to Metal.\n"
        "No Wine dependency.\n",
        prog, METALSHARP_VERSION);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        printUsage(argv[0]);
        return 0;
    }

    printf("MetalSharp %s — Native PE Loader\n", METALSHARP_VERSION);

    metalsharp::Logger::init("");
    metalsharp::Logger::setLevel(metalsharp::LogLevel::Trace);

    PELoader loader;

    printf("Registering Win32 shims...\n");
    auto kernel32 = win32::Kernel32Shim::create();
    win32::addMissingKernel32(kernel32);
    loader.registerShim("kernel32.dll", std::move(kernel32));

    auto kernelBase = win32::Kernel32Shim::create();
    win32::addMissingKernel32(kernelBase);
    loader.registerShim("kernelbase.dll", std::move(kernelBase));

    loader.registerShim("ntdll.dll", win32::createNtdllShim());
    loader.registerShim("USER32.dll", win32::createUser32Shim());
    loader.registerShim("user32.dll", win32::createUser32Shim());
    loader.registerShim("GDI32.dll", win32::createGdi32Shim());
    loader.registerShim("gdi32.dll", win32::createGdi32Shim());
    loader.registerShim("ADVAPI32.dll", win32::createAdvapi32Shim());
    loader.registerShim("advapi32.dll", win32::createAdvapi32Shim());
    loader.registerShim("WS2_32.dll", win32::createWs2_32Shim());
    loader.registerShim("ws2_32.dll", win32::createWs2_32Shim());
    loader.registerShim("SHELL32.dll", win32::createShell32Shim());
    loader.registerShim("shell32.dll", win32::createShell32Shim());
    loader.registerShim("ole32.dll", win32::createOle32Shim());
    loader.registerShim("OLEAUT32.dll", win32::createOleAut32Shim());
    loader.registerShim("oleaut32.dll", win32::createOleAut32Shim());
    loader.registerShim("CRYPT32.dll", win32::createCrypt32Shim());
    loader.registerShim("crypt32.dll", win32::createCrypt32Shim());
    loader.registerShim("PSAPI.DLL", win32::createPsapiShim());
    loader.registerShim("psapi.dll", win32::createPsapiShim());
    loader.registerShim("VERSION.dll", win32::createVersionShim());
    loader.registerShim("version.dll", win32::createVersionShim());
    loader.registerShim("bcrypt.dll", win32::createBcryptShim());
    loader.registerShim("COMCTL32.dll", win32::createComCtl32Shim());
    loader.registerShim("comctl32.dll", win32::createComCtl32Shim());
    loader.registerShim("WSOCK32.dll", win32::createWsock32Shim());
    loader.registerShim("wsock32.dll", win32::createWsock32Shim());

    // Minimal msvcrt shims
    ShimLibrary msvcrt;
    msvcrt.name = "msvcrt.dll";
    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };
    msvcrt.functions["malloc"] = fn((void*)malloc);
    msvcrt.functions["free"] = fn((void*)free);
    msvcrt.functions["realloc"] = fn((void*)realloc);
    msvcrt.functions["calloc"] = fn((void*)calloc);
    msvcrt.functions["memset"] = fn((void*)memset);
    msvcrt.functions["memcpy"] = fn((void*)memcpy);
    msvcrt.functions["memmove"] = fn((void*)memmove);
    msvcrt.functions["memcmp"] = fn((void*)memcmp);
    msvcrt.functions["strlen"] = fn((void*)strlen);
    msvcrt.functions["strcpy"] = fn((void*)strcpy);
    msvcrt.functions["strcat"] = fn((void*)strcat);
    msvcrt.functions["strcmp"] = fn((void*)strcmp);
    msvcrt.functions["strncmp"] = fn((void*)strncmp);
    msvcrt.functions["strncpy"] = fn((void*)strncpy);
    msvcrt.functions["strstr"] = fn((void*)(char*(*)(const char*, const char*))strstr);
    msvcrt.functions["strchr"] = fn((void*)(char*(*)(const char*, int))strchr);
    msvcrt.functions["strrchr"] = fn((void*)(char*(*)(const char*, int))strrchr);
    msvcrt.functions["atoi"] = fn((void*)atoi);
    msvcrt.functions["atof"] = fn((void*)atof);
    msvcrt.functions["strtol"] = fn((void*)strtol);
    msvcrt.functions["strtod"] = fn((void*)strtod);
    msvcrt.functions["sprintf"] = fn((void*)sprintf);
    msvcrt.functions["snprintf"] = fn((void*)snprintf);
    msvcrt.functions["sscanf"] = fn((void*)sscanf);
    msvcrt.functions["printf"] = fn((void*)printf);
    msvcrt.functions["fprintf"] = fn((void*)fprintf);
    msvcrt.functions["fopen"] = fn((void*)fopen);
    msvcrt.functions["fclose"] = fn((void*)fclose);
    msvcrt.functions["fread"] = fn((void*)fread);
    msvcrt.functions["fwrite"] = fn((void*)fwrite);
    msvcrt.functions["fseek"] = fn((void*)fseek);
    msvcrt.functions["ftell"] = fn((void*)ftell);
    msvcrt.functions["fgets"] = fn((void*)fgets);
    msvcrt.functions["vsnprintf"] = fn((void*)vsnprintf);
    msvcrt.functions["_stricmp"] = fn((void*)strcasecmp);
    msvcrt.functions["_strnicmp"] = fn((void*)strncasecmp);
    msvcrt.functions["wcslen"] = fn((void*)wcslen);
    msvcrt.functions["wcscpy"] = fn((void*)wcscpy);
    msvcrt.functions["wcscat"] = fn((void*)wcscat);
    msvcrt.functions["wcscmp"] = fn((void*)wcscmp);
    msvcrt.functions["_wcsicmp"] = fn((void*)nullptr);
    msvcrt.functions["tolower"] = fn((void*)tolower);
    msvcrt.functions["toupper"] = fn((void*)toupper);
    msvcrt.functions["isalpha"] = fn((void*)isalpha);
    msvcrt.functions["isdigit"] = fn((void*)isdigit);
    msvcrt.functions["isspace"] = fn((void*)isspace);
    msvcrt.functions["qsort"] = fn((void*)qsort);
    msvcrt.functions["rand"] = fn((void*)rand);
    msvcrt.functions["srand"] = fn((void*)srand);
    msvcrt.functions["abs"] = fn((void*)(int(*)(int))abs);
    msvcrt.functions["labs"] = fn((void*)labs);
    msvcrt.functions["floor"] = fn((void*)(double(*)(double))floor);
    msvcrt.functions["ceil"] = fn((void*)(double(*)(double))ceil);
    msvcrt.functions["sqrt"] = fn((void*)(double(*)(double))sqrt);
    msvcrt.functions["sin"] = fn((void*)(double(*)(double))sin);
    msvcrt.functions["cos"] = fn((void*)(double(*)(double))cos);
    msvcrt.functions["tan"] = fn((void*)(double(*)(double))tan);
    msvcrt.functions["atan2"] = fn((void*)(double(*)(double, double))atan2);
    msvcrt.functions["pow"] = fn((void*)(double(*)(double, double))pow);
    msvcrt.functions["log"] = fn((void*)(double(*)(double))log);
    msvcrt.functions["log10"] = fn((void*)(double(*)(double))log10);
    msvcrt.functions["exp"] = fn((void*)(double(*)(double))exp);
    msvcrt.functions["fabs"] = fn((void*)(double(*)(double))fabs);
    msvcrt.functions["fmod"] = fn((void*)(double(*)(double, double))fmod);
    msvcrt.functions["modf"] = fn((void*)(double(*)(double, double*))modf);
    msvcrt.functions["ldexp"] = fn((void*)(double(*)(double, int))ldexp);
    msvcrt.functions["frexp"] = fn((void*)(double(*)(double, int*))frexp);
    loader.registerShim("msvcrt.dll", std::move(msvcrt));

    // vcruntime shims
    ShimLibrary vcruntime;
    vcruntime.name = "vcruntime140.dll";
    vcruntime.functions["memset"] = fn((void*)memset);
    vcruntime.functions["memcpy"] = fn((void*)memcpy);
    vcruntime.functions["memmove"] = fn((void*)memmove);
    vcruntime.functions["memcmp"] = fn((void*)memcmp);
    vcruntime.functions["strcpy"] = fn((void*)strcpy);
    vcruntime.functions["strcat"] = fn((void*)strcat);
    vcruntime.functions["strlen"] = fn((void*)strlen);
    vcruntime.functions["strcmp"] = fn((void*)strcmp);
    vcruntime.functions["strstr"] = fn((void*)(char*(*)(const char*, const char*))strstr);
    vcruntime.functions["_purecall"] = fn((void*)nullptr);
    loader.registerShim("vcruntime140.dll", std::move(vcruntime));

    ShimLibrary ucrt;
    ucrt.name = "api-ms-win-crt-runtime-l1-1-0.dll";
    ucrt.functions["malloc"] = fn((void*)malloc);
    ucrt.functions["free"] = fn((void*)free);
    ucrt.functions["realloc"] = fn((void*)realloc);
    ucrt.functions["calloc"] = fn((void*)calloc);
    ucrt.functions["_initterm"] = fn((void*)nullptr);
    ucrt.functions["_initterm_e"] = fn((void*)nullptr);
    ucrt.functions["_initialize_onexit_table"] = fn((void*)nullptr);
    ucrt.functions["_register_onexit_function"] = fn((void*)nullptr);
    ucrt.functions["_execute_onexit_table"] = fn((void*)nullptr);
    ucrt.functions["_crt_atexit"] = fn((void*)nullptr);
    ucrt.functions["_crt_at_quick_exit"] = fn((void*)nullptr);
    ucrt.functions["_c_exit"] = fn((void*)nullptr);
    ucrt.functions["exit"] = fn((void*)exit);
    ucrt.functions["_exit"] = fn((void*)_Exit);
    ucrt.functions["abort"] = fn((void*)abort);
    ucrt.functions["_configure_narrow_argv"] = fn((void*)nullptr);
    ucrt.functions["_initialize_narrow_environment"] = fn((void*)nullptr);
    ucrt.functions["get_initial_narrow_environment"] = fn((void*)nullptr);
    ucrt.functions["_setusermatherr"] = fn((void*)nullptr);
    loader.registerShim("api-ms-win-crt-runtime-l1-1-0.dll", std::move(ucrt));

    ShimLibrary crtStdio;
    crtStdio.name = "api-ms-win-crt-stdio-l1-1-0.dll";
    crtStdio.functions["__acrt_iob_func"] = fn((void*)nullptr);
    crtStdio.functions["__stdio_common_vsprintf"] = fn((void*)nullptr);
    crtStdio.functions["__stdio_common_vfprintf"] = fn((void*)nullptr);
    crtStdio.functions["__stdio_common_vsscanf"] = fn((void*)nullptr);
    loader.registerShim("api-ms-win-crt-stdio-l1-1-0.dll", std::move(crtStdio));

    ShimLibrary crtString;
    crtString.name = "api-ms-win-crt-string-l1-1-0.dll";
    crtString.functions["memcmp"] = fn((void*)memcmp);
    crtString.functions["memcpy"] = fn((void*)memcpy);
    crtString.functions["memset"] = fn((void*)memset);
    crtString.functions["strcmp"] = fn((void*)strcmp);
    crtString.functions["strlen"] = fn((void*)strlen);
    crtString.functions["strncpy"] = fn((void*)strncpy);
    crtString.functions["strchr"] = fn((void*)(char*(*)(const char*, int))strchr);
    crtString.functions["strstr"] = fn((void*)(char*(*)(const char*, const char*))strstr);
    crtString.functions["isalpha"] = fn((void*)isalpha);
    crtString.functions["isdigit"] = fn((void*)isdigit);
    crtString.functions["isspace"] = fn((void*)isspace);
    crtString.functions["tolower"] = fn((void*)tolower);
    crtString.functions["toupper"] = fn((void*)toupper);
    crtString.functions["_stricmp"] = fn((void*)strcasecmp);
    loader.registerShim("api-ms-win-crt-string-l1-1-0.dll", std::move(crtString));

    ShimLibrary crtHeap;
    crtHeap.name = "api-ms-win-crt-heap-l1-1-0.dll";
    crtHeap.functions["malloc"] = fn((void*)malloc);
    crtHeap.functions["free"] = fn((void*)free);
    crtHeap.functions["realloc"] = fn((void*)realloc);
    crtHeap.functions["calloc"] = fn((void*)calloc);
    crtHeap.functions["_recalloc"] = fn((void*)nullptr);
    crtHeap.functions["_aligned_malloc"] = fn((void*)nullptr);
    crtHeap.functions["_aligned_free"] = fn((void*)nullptr);
    loader.registerShim("api-ms-win-crt-heap-l1-1-0.dll", std::move(crtHeap));

    ShimLibrary crtMath;
    crtMath.name = "api-ms-win-crt-math-l1-1-0.dll";
    crtMath.functions["floor"] = fn((void*)(double(*)(double))floor);
    crtMath.functions["ceil"] = fn((void*)(double(*)(double))ceil);
    crtMath.functions["sqrt"] = fn((void*)(double(*)(double))sqrt);
    crtMath.functions["fabs"] = fn((void*)(double(*)(double))fabs);
    crtMath.functions["fmod"] = fn((void*)(double(*)(double, double))fmod);
    crtMath.functions["pow"] = fn((void*)(double(*)(double, double))pow);
    crtMath.functions["log"] = fn((void*)(double(*)(double))log);
    crtMath.functions["log10"] = fn((void*)(double(*)(double))log10);
    crtMath.functions["exp"] = fn((void*)(double(*)(double))exp);
    crtMath.functions["sin"] = fn((void*)(double(*)(double))sin);
    crtMath.functions["cos"] = fn((void*)(double(*)(double))cos);
    crtMath.functions["tan"] = fn((void*)(double(*)(double))tan);
    crtMath.functions["atan2"] = fn((void*)(double(*)(double, double))atan2);
    crtMath.functions["modf"] = fn((void*)(double(*)(double, double*))modf);
    crtMath.functions["ldexp"] = fn((void*)(double(*)(double, int))ldexp);
    crtMath.functions["frexp"] = fn((void*)(double(*)(double, int*))frexp);
    crtMath.functions["abs"] = fn((void*)(int(*)(int))abs);
    crtMath.functions["_fdclass"] = fn((void*)nullptr);
    crtMath.functions["_dsign"] = fn((void*)nullptr);
    loader.registerShim("api-ms-win-crt-math-l1-1-0.dll", std::move(crtMath));

    ShimLibrary crtConvert;
    crtConvert.name = "api-ms-win-crt-convert-l1-1-0.dll";
    crtConvert.functions["atoi"] = fn((void*)atoi);
    crtConvert.functions["atof"] = fn((void*)atof);
    crtConvert.functions["strtol"] = fn((void*)strtol);
    crtConvert.functions["strtod"] = fn((void*)strtod);
    crtConvert.functions["strtoul"] = fn((void*)strtoul);
    crtConvert.functions["itoa"] = fn((void*)nullptr);
    loader.registerShim("api-ms-win-crt-convert-l1-1-0.dll", std::move(crtConvert));

    ShimLibrary crtLocale;
    crtLocale.name = "api-ms-win-crt-locale-l1-1-0.dll";
    crtLocale.functions["setlocale"] = fn((void*)setlocale);
    crtLocale.functions["localeconv"] = fn((void*)nullptr);
    loader.registerShim("api-ms-win-crt-locale-l1-1-0.dll", std::move(crtLocale));

    ShimLibrary crtTime;
    crtTime.name = "api-ms-win-crt-time-l1-1-0.dll";
    crtTime.functions["clock"] = fn((void*)clock);
    crtTime.functions["time"] = fn((void*)time);
    crtTime.functions["strftime"] = fn((void*)strftime);
    loader.registerShim("api-ms-win-crt-time-l1-1-0.dll", std::move(crtTime));

    ShimLibrary crtFileIo;
    crtFileIo.name = "api-ms-win-crt-filesystem-l1-1-0.dll";
    crtFileIo.functions["fopen"] = fn((void*)fopen);
    crtFileIo.functions["fclose"] = fn((void*)fclose);
    crtFileIo.functions["fread"] = fn((void*)fread);
    crtFileIo.functions["fwrite"] = fn((void*)fwrite);
    crtFileIo.functions["fseek"] = fn((void*)fseek);
    crtFileIo.functions["ftell"] = fn((void*)ftell);
    crtFileIo.functions["fgets"] = fn((void*)fgets);
    loader.registerShim("api-ms-win-crt-filesystem-l1-1-0.dll", std::move(crtFileIo));

    printf("Loading %s...\n", argv[1]);

    if (!loader.load(argv[1])) {
        fprintf(stderr, "Failed to load %s\n", argv[1]);
        return 1;
    }

    LoadedModule* main = loader.getMainModule();
    if (!main || !main->entryPoint) {
        fprintf(stderr, "No entry point found\n");
        return 1;
    }

    printf("Entry point at %p\n", main->entryPoint);
    printf("Jumping to entry point...\n");

    if (main->isPE) {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(main->base);
        auto* opt = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
            main->base + dos->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER));

        bool isDll = false;
        auto* fileHdr = reinterpret_cast<const IMAGE_FILE_HEADER*>(
            main->base + dos->e_lfanew + 4);
        if (fileHdr->Characteristics & IMAGE_FILE_DLL) {
            isDll = true;
        }

        if (isDll) {
            using DllMainProc = int (*)(void* hinstDLL, unsigned long fdwReason, void* lpReserved);
            auto dllMain = reinterpret_cast<DllMainProc>(main->entryPoint);
            int result = dllMain(reinterpret_cast<void*>(main->base), 1, nullptr);
            printf("DllMain returned: %d\n", result);
        } else {
            using MainProc = int (*)(void);
            auto entry = reinterpret_cast<MainProc>(main->entryPoint);
            int result = entry();
            printf("Entry point returned: %d\n", result);
        }
    }

    return 0;
}
