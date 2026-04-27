#define _XOPEN_SOURCE
#include <metalsharp/PELoader.h>
#include <metalsharp/PEHeader.h>
#include <metalsharp/Kernel32Shim.h>
#include <metalsharp/NtdllShim.h>
#include <metalsharp/ExtraShims.h>
#include <metalsharp/D3DShims.h>
#include <metalsharp/MSABITrampolines.h>
#include <metalsharp/Logger.h>
#include <metalsharp/VirtualFileSystem.h>
#include <metalsharp/Registry.h>
#include <metalsharp/WindowManager.h>
#include <metalsharp/NetworkContext.h>
#include <metalsharp/SecureTransport.h>
#include <metalsharp/SyncContext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <pthread.h>

static metalsharp::LoadedModule* g_mainModule = nullptr;

static int g_argc = 1;
static char* g_argv_data[2] = {const_cast<char*>("test.exe"), nullptr};
static char** g_argv = g_argv_data;
static char* g_cmdline = const_cast<char*>("test.exe");
static int g_commode = 0;
static int g_fmode = 0;
static char** g_environ_data = nullptr;

static void MSABI stub_noop_void() {}
static int MSABI stub_noop_int() { return 0; }
static void* MSABI stub_noop_ptr() { return nullptr; }

#define STUB_V ((void*)(void(*)())stub_noop_void)
#define STUB_I ((void*)(int(*)())stub_noop_int)
#define STUB_P ((void*)(void*(*)())stub_noop_ptr)

static void crash_handler(int sig, siginfo_t* info, void* ucontext) {
#if defined(__x86_64__)
    auto* ctx = static_cast<ucontext_t*>(ucontext);
    uint64_t rip = ctx->uc_mcontext->__ss.__rip;
    uint64_t rsp = ctx->uc_mcontext->__ss.__rsp;
    uint64_t rax = ctx->uc_mcontext->__ss.__rax;
    uint64_t rbx = ctx->uc_mcontext->__ss.__rbx;
    uint64_t rcx = ctx->uc_mcontext->__ss.__rcx;
    uint64_t rdx = ctx->uc_mcontext->__ss.__rdx;

    fprintf(stderr, "\n=== CRASH (signal %d) ===\n", sig);
    fprintf(stderr, "Faulting address: 0x%llX\n", (unsigned long long)(uintptr_t)info->si_addr);
    fprintf(stderr, "RIP: 0x%llX  RSP: 0x%llX\n", (unsigned long long)rip, (unsigned long long)rsp);
    fprintf(stderr, "RAX: 0x%llX  RBX: 0x%llX  RCX: 0x%llX  RDX: 0x%llX\n",
            (unsigned long long)rax, (unsigned long long)rbx,
            (unsigned long long)rcx, (unsigned long long)rdx);

    if (g_mainModule) {
        uint64_t base = (uint64_t)g_mainModule->base;
        uint64_t end = base + g_mainModule->size;
        if (rip >= base && rip < end) {
            fprintf(stderr, "Crash RVA: 0x%llX\n", (unsigned long long)(rip - base));
        } else {
            fprintf(stderr, "RIP is OUTSIDE PE image (bad code pointer jump)\n");
        }
        uint64_t* stack = (uint64_t*)rsp;
        fprintf(stderr, "Stack:\n");
        for (int i = 0; i < 16; i++) {
            uint64_t val = stack[i];
            if (val >= base && val < end)
                fprintf(stderr, "  [RSP+0x%02X] = 0x%llX  (PE RVA 0x%llX)\n",
                        i*8, (unsigned long long)val, (unsigned long long)(val - base));
            else
                fprintf(stderr, "  [RSP+0x%02X] = 0x%llX\n", i*8, (unsigned long long)val);
        }
    }
#else
    fprintf(stderr, "\n=== CRASH (signal %d) at 0x%llX ===\n",
            sig, (unsigned long long)(uintptr_t)info->si_addr);
#endif
    _exit(139);
}

#ifndef METALSHARP_VERSION
#define METALSHARP_VERSION "0.2.0"
#endif

using namespace metalsharp;
using namespace metalsharp::win32;

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

    const char* home = getenv("HOME");
    std::string prefix = home ? std::string(home) + "/.metalsharp/prefix" : "/tmp/metalsharp/prefix";
    VirtualFileSystem::instance().setPrefix(prefix);
    Registry::instance().init(prefix);
    WindowManager::instance().init();
    NetworkContext::instance().initialize();
    SecureTransport::instance().initialize();
    SyncContext::instance().initialize();

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

    printf("Registering D3D/DXGI shims...\n");
    loader.registerShim("d3d11.dll", win32::createD3D11Shim());
    loader.registerShim("D3D11.dll", win32::createD3D11Shim());
    loader.registerShim("d3d12.dll", win32::createD3D12Shim());
    loader.registerShim("D3D12.dll", win32::createD3D12Shim());
    loader.registerShim("dxgi.dll", win32::createDxgiShim());
    loader.registerShim("DXGI.dll", win32::createDxgiShim());

    auto apiSets = {
        "api-ms-win-core-synch-l1-2-0",
        "api-ms-win-core-synch-l1-1-0",
        "api-ms-win-core-processthreads-l1-1-3",
        "api-ms-win-core-processthreads-l1-1-2",
        "api-ms-win-core-processthreads-l1-1-1",
        "api-ms-win-core-file-l1-2-0",
        "api-ms-win-core-file-l1-1-0",
        "api-ms-win-core-handle-l1-1-0",
        "api-ms-win-core-heap-l1-1-0",
        "api-ms-win-core-heap-l2-1-0",
        "api-ms-win-core-localization-l1-2-1",
        "api-ms-win-core-localization-l1-2-0",
        "api-ms-win-core-libraryloader-l1-1-0",
        "api-ms-win-core-libraryloader-l1-1-1",
        "api-ms-win-core-memory-l1-1-3",
        "api-ms-win-core-memory-l1-1-1",
        "api-ms-win-core-errorhandling-l1-1-1",
        "api-ms-win-core-errorhandling-l1-1-0",
        "api-ms-win-core-debug-l1-1-1",
        "api-ms-win-core-debug-l1-1-0",
        "api-ms-win-core-profile-l1-1-0",
        "api-ms-win-core-datetime-l1-1-1",
        "api-ms-win-core-datetime-l1-1-0",
        "api-ms-win-core-string-l1-1-0",
        "api-ms-win-core-string-obsolete-l1-1-0",
        "api-ms-win-core-registry-l1-1-0",
        "api-ms-win-core-registry-l2-1-0",
        "api-ms-win-core-io-l1-1-1",
        "api-ms-win-core-io-l1-1-0",
        "api-ms-win-core-processenvironment-l1-1-0",
        "api-ms-win-core-console-l1-1-0",
        "api-ms-win-core-console-l2-1-0",
        "api-ms-win-core-namedpipe-l1-1-0",
        "api-ms-win-core-kernel32-legacy-l1-1-1",
        "api-ms-win-core-kernel32-legacy-l1-1-0",
        "api-ms-win-core-sysinfo-l1-1-0",
        "api-ms-win-core-version-l1-1-0",
        "api-ms-win-core-versionansi-l1-1-0",
        "api-ms-win-security-base-l1-1-0",
        "api-ms-win-security-base-l1-2-0",
        "api-ms-win-eventing-controller-l1-1-0",
        "api-ms-win-core-delayload-l1-1-0",
        "api-ms-win-core-apiquery-l1-1-0",
        "api-ms-win-core-psapi-ansi-l1-1-0",
        "api-ms-win-core-psapi-l1-1-0",
        "api-ms-win-core-shlwapi-legacy-l1-1-0",
        "api-ms-win-core-shlwapi-obsolete-l1-1-0",
        "api-ms-win-shcore-registryhelpers-l1-1-0",
        "api-ms-win-core-winrt-error-l1-1-1",
        "api-ms-win-core-winrt-string-l1-1-0",
    };
    for (const char* apiSet : apiSets) {
        std::string dllName = std::string(apiSet) + ".dll";
        loader.registerShim(dllName, win32::Kernel32Shim::create());
        auto k32 = win32::Kernel32Shim::create();
        win32::addMissingKernel32(k32);
        loader.registerShim(dllName, std::move(k32));
    }

    ShimLibrary msvcrt;
    msvcrt.name = "msvcrt.dll";
    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };
    msvcrt.functions["malloc"] = fn((void*)msabi_malloc);
    msvcrt.functions["free"] = fn((void*)msabi_free);
    msvcrt.functions["realloc"] = fn((void*)msabi_realloc);
    msvcrt.functions["calloc"] = fn((void*)msabi_calloc);
    msvcrt.functions["memset"] = fn((void*)msabi_memset);
    msvcrt.functions["memcpy"] = fn((void*)msabi_memcpy);
    msvcrt.functions["memmove"] = fn((void*)msabi_memmove);
    msvcrt.functions["memcmp"] = fn((void*)msabi_memcmp);
    msvcrt.functions["strlen"] = fn((void*)msabi_strlen);
    msvcrt.functions["strcpy"] = fn((void*)msabi_strcpy);
    msvcrt.functions["strcat"] = fn((void*)msabi_strcat);
    msvcrt.functions["strcmp"] = fn((void*)msabi_strcmp);
    msvcrt.functions["strncmp"] = fn((void*)msabi_strncmp);
    msvcrt.functions["strncpy"] = fn((void*)msabi_strncpy);
    msvcrt.functions["strstr"] = fn((void*)msabi_strstr);
    msvcrt.functions["strchr"] = fn((void*)msabi_strchr);
    msvcrt.functions["strrchr"] = fn((void*)msabi_strrchr);
    msvcrt.functions["atoi"] = fn((void*)msabi_atoi);
    msvcrt.functions["atof"] = fn((void*)msabi_atof);
    msvcrt.functions["strtol"] = fn((void*)msabi_strtol);
    msvcrt.functions["strtod"] = fn((void*)msabi_strtod);
    msvcrt.functions["sprintf"] = fn((void*)msabi_sprintf);
    msvcrt.functions["snprintf"] = fn((void*)msabi_snprintf);
    msvcrt.functions["sscanf"] = fn((void*)msabi_sscanf);
    msvcrt.functions["printf"] = fn((void*)msabi_printf);
    msvcrt.functions["fprintf"] = fn((void*)msabi_fprintf);
    msvcrt.functions["fopen"] = fn((void*)msabi_fopen);
    msvcrt.functions["fclose"] = fn((void*)msabi_fclose);
    msvcrt.functions["fread"] = fn((void*)msabi_fread);
    msvcrt.functions["fwrite"] = fn((void*)msabi_fwrite);
    msvcrt.functions["fseek"] = fn((void*)msabi_fseek);
    msvcrt.functions["ftell"] = fn((void*)msabi_ftell);
    msvcrt.functions["fgets"] = fn((void*)msabi_fgets);
    msvcrt.functions["vsnprintf"] = fn((void*)msabi_vsnprintf);
    msvcrt.functions["_stricmp"] = fn((void*)msabi_strcasecmp);
    msvcrt.functions["_strnicmp"] = fn((void*)msabi_strncasecmp);
    msvcrt.functions["wcslen"] = fn((void*)msabi_wcslen);
    msvcrt.functions["wcscpy"] = fn((void*)msabi_wcscpy);
    msvcrt.functions["wcscat"] = fn((void*)msabi_wcscat);
    msvcrt.functions["wcscmp"] = fn((void*)msabi_wcscmp);
    msvcrt.functions["_wcsicmp"] = fn((void*)nullptr);
    msvcrt.functions["tolower"] = fn((void*)msabi_tolower);
    msvcrt.functions["toupper"] = fn((void*)msabi_toupper);
    msvcrt.functions["isalpha"] = fn((void*)msabi_isalpha);
    msvcrt.functions["isdigit"] = fn((void*)msabi_isdigit);
    msvcrt.functions["isspace"] = fn((void*)msabi_isspace);
    msvcrt.functions["qsort"] = fn((void*)msabi_qsort);
    msvcrt.functions["rand"] = fn((void*)msabi_rand);
    msvcrt.functions["srand"] = fn((void*)msabi_srand);
    msvcrt.functions["abs"] = fn((void*)msabi_abs);
    msvcrt.functions["labs"] = fn((void*)msabi_labs);
    msvcrt.functions["floor"] = fn((void*)msabi_floor);
    msvcrt.functions["ceil"] = fn((void*)msabi_ceil);
    msvcrt.functions["sqrt"] = fn((void*)msabi_sqrt);
    msvcrt.functions["sin"] = fn((void*)msabi_sin);
    msvcrt.functions["cos"] = fn((void*)msabi_cos);
    msvcrt.functions["tan"] = fn((void*)msabi_tan);
    msvcrt.functions["atan2"] = fn((void*)msabi_atan2);
    msvcrt.functions["pow"] = fn((void*)msabi_pow);
    msvcrt.functions["log"] = fn((void*)msabi_log);
    msvcrt.functions["log10"] = fn((void*)msabi_log10);
    msvcrt.functions["exp"] = fn((void*)msabi_exp);
    msvcrt.functions["fabs"] = fn((void*)msabi_fabs);
    msvcrt.functions["fmod"] = fn((void*)msabi_fmod);
    msvcrt.functions["modf"] = fn((void*)msabi_modf);
    msvcrt.functions["ldexp"] = fn((void*)msabi_ldexp);
    msvcrt.functions["frexp"] = fn((void*)msabi_frexp);
    loader.registerShim("msvcrt.dll", std::move(msvcrt));

    ShimLibrary vcruntime;
    vcruntime.name = "vcruntime140.dll";
    vcruntime.functions["memset"] = fn((void*)msabi_memset);
    vcruntime.functions["memcpy"] = fn((void*)msabi_memcpy);
    vcruntime.functions["memmove"] = fn((void*)msabi_memmove);
    vcruntime.functions["memcmp"] = fn((void*)msabi_memcmp);
    vcruntime.functions["strcpy"] = fn((void*)msabi_strcpy);
    vcruntime.functions["strcat"] = fn((void*)msabi_strcat);
    vcruntime.functions["strlen"] = fn((void*)msabi_strlen);
    vcruntime.functions["strcmp"] = fn((void*)msabi_strcmp);
    vcruntime.functions["strstr"] = fn((void*)msabi_strstr);
    vcruntime.functions["_purecall"] = fn(STUB_I);
    loader.registerShim("vcruntime140.dll", std::move(vcruntime));

    ShimLibrary ucrt;
    ucrt.name = "api-ms-win-crt-runtime-l1-1-0.dll";
    ucrt.functions["malloc"] = fn((void*)msabi_malloc);
    ucrt.functions["free"] = fn((void*)msabi_free);
    ucrt.functions["realloc"] = fn((void*)msabi_realloc);
    ucrt.functions["calloc"] = fn((void*)msabi_calloc);
    ucrt.functions["_initterm"] = fn(STUB_V);
    ucrt.functions["_initterm_e"] = fn(STUB_V);
    ucrt.functions["_initialize_onexit_table"] = fn(STUB_I);
    ucrt.functions["_register_onexit_function"] = fn(STUB_P);
    ucrt.functions["_execute_onexit_table"] = fn(STUB_I);
    ucrt.functions["_crt_atexit"] = fn(STUB_I);
    ucrt.functions["_crt_at_quick_exit"] = fn(STUB_I);
    ucrt.functions["_c_exit"] = fn(STUB_V);
    ucrt.functions["exit"] = fn((void*)msabi_exit);
    ucrt.functions["_exit"] = fn((void*)msabi__Exit);
    ucrt.functions["abort"] = fn((void*)msabi_abort);
    ucrt.functions["_configure_narrow_argv"] = fn(STUB_I);
    ucrt.functions["_initialize_narrow_environment"] = fn(STUB_I);
    ucrt.functions["get_initial_narrow_environment"] = fn(STUB_P);
    ucrt.functions["_setusermatherr"] = fn(STUB_I);
    ucrt.functions["__p___argc"] = fn((void*)+[]() -> void* { return &g_argc; });
    ucrt.functions["__p___argv"] = fn((void*)+[]() -> void* { return &g_argv; });
    ucrt.functions["__p__acmdln"] = fn((void*)+[]() -> void* { return &g_cmdline; });
    ucrt.functions["_cexit"] = fn(STUB_V);
    ucrt.functions["_set_app_type"] = fn(STUB_V);
    ucrt.functions["_set_invalid_parameter_handler"] = fn(STUB_P);
    ucrt.functions["signal"] = fn(STUB_P);
    loader.registerShim("api-ms-win-crt-runtime-l1-1-0.dll", std::move(ucrt));

    ShimLibrary crtStdio;
    crtStdio.name = "api-ms-win-crt-stdio-l1-1-0.dll";
    crtStdio.functions["__acrt_iob_func"] = fn(STUB_P);
    crtStdio.functions["__stdio_common_vsprintf"] = fn(STUB_I);
    crtStdio.functions["__stdio_common_vfprintf"] = fn(STUB_I);
    crtStdio.functions["__stdio_common_vsscanf"] = fn(STUB_I);
    crtStdio.functions["__p__commode"] = fn((void*)+[]() -> void* { return &g_commode; });
    crtStdio.functions["__p__fmode"] = fn((void*)+[]() -> void* { return &g_fmode; });
    crtStdio.functions["fflush"] = fn((void*)+[](void*) -> int { return 0; });
    crtStdio.functions["setvbuf"] = fn((void*)+[](void*, char*, int, unsigned) -> int { return 0; });
    loader.registerShim("api-ms-win-crt-stdio-l1-1-0.dll", std::move(crtStdio));

    ShimLibrary crtString;
    crtString.name = "api-ms-win-crt-string-l1-1-0.dll";
    crtString.functions["memcmp"] = fn((void*)msabi_memcmp);
    crtString.functions["memcpy"] = fn((void*)msabi_memcpy);
    crtString.functions["memset"] = fn((void*)msabi_memset);
    crtString.functions["strcmp"] = fn((void*)msabi_strcmp);
    crtString.functions["strlen"] = fn((void*)msabi_strlen);
    crtString.functions["strncpy"] = fn((void*)msabi_strncpy);
    crtString.functions["strchr"] = fn((void*)msabi_strchr);
    crtString.functions["strstr"] = fn((void*)msabi_strstr);
    crtString.functions["strncmp"] = fn((void*)msabi_strncmp);
    crtString.functions["isalpha"] = fn((void*)msabi_isalpha);
    crtString.functions["isdigit"] = fn((void*)msabi_isdigit);
    crtString.functions["isspace"] = fn((void*)msabi_isspace);
    crtString.functions["tolower"] = fn((void*)msabi_tolower);
    crtString.functions["toupper"] = fn((void*)msabi_toupper);
    crtString.functions["_stricmp"] = fn((void*)msabi_strcasecmp);
    loader.registerShim("api-ms-win-crt-string-l1-1-0.dll", std::move(crtString));

    ShimLibrary crtHeap;
    crtHeap.name = "api-ms-win-crt-heap-l1-1-0.dll";
    crtHeap.functions["malloc"] = fn((void*)msabi_malloc);
    crtHeap.functions["free"] = fn((void*)msabi_free);
    crtHeap.functions["realloc"] = fn((void*)msabi_realloc);
    crtHeap.functions["calloc"] = fn((void*)msabi_calloc);
    crtHeap.functions["_recalloc"] = fn(STUB_P);
    crtHeap.functions["_aligned_malloc"] = fn(STUB_P);
    crtHeap.functions["_aligned_free"] = fn(STUB_V);
    crtHeap.functions["_set_new_mode"] = fn(STUB_I);
    loader.registerShim("api-ms-win-crt-heap-l1-1-0.dll", std::move(crtHeap));

    ShimLibrary crtMath;
    crtMath.name = "api-ms-win-crt-math-l1-1-0.dll";
    crtMath.functions["floor"] = fn((void*)msabi_floor);
    crtMath.functions["ceil"] = fn((void*)msabi_ceil);
    crtMath.functions["sqrt"] = fn((void*)msabi_sqrt);
    crtMath.functions["fabs"] = fn((void*)msabi_fabs);
    crtMath.functions["fmod"] = fn((void*)msabi_fmod);
    crtMath.functions["pow"] = fn((void*)msabi_pow);
    crtMath.functions["log"] = fn((void*)msabi_log);
    crtMath.functions["log10"] = fn((void*)msabi_log10);
    crtMath.functions["exp"] = fn((void*)msabi_exp);
    crtMath.functions["sin"] = fn((void*)msabi_sin);
    crtMath.functions["cos"] = fn((void*)msabi_cos);
    crtMath.functions["tan"] = fn((void*)msabi_tan);
    crtMath.functions["atan2"] = fn((void*)msabi_atan2);
    crtMath.functions["modf"] = fn((void*)msabi_modf);
    crtMath.functions["ldexp"] = fn((void*)msabi_ldexp);
    crtMath.functions["frexp"] = fn((void*)msabi_frexp);
    crtMath.functions["abs"] = fn((void*)msabi_abs);
    crtMath.functions["_fdclass"] = fn(STUB_I);
    crtMath.functions["_dsign"] = fn(STUB_I);
    crtMath.functions["__setusermatherr"] = fn(STUB_I);
    loader.registerShim("api-ms-win-crt-math-l1-1-0.dll", std::move(crtMath));

    ShimLibrary crtConvert;
    crtConvert.name = "api-ms-win-crt-convert-l1-1-0.dll";
    crtConvert.functions["atoi"] = fn((void*)msabi_atoi);
    crtConvert.functions["atof"] = fn((void*)msabi_atof);
    crtConvert.functions["strtol"] = fn((void*)msabi_strtol);
    crtConvert.functions["strtod"] = fn((void*)msabi_strtod);
    crtConvert.functions["strtoul"] = fn((void*)msabi_strtoul);
    crtConvert.functions["itoa"] = fn(STUB_P);
    loader.registerShim("api-ms-win-crt-convert-l1-1-0.dll", std::move(crtConvert));

    ShimLibrary crtLocale;
    crtLocale.name = "api-ms-win-crt-locale-l1-1-0.dll";
    crtLocale.functions["setlocale"] = fn((void*)msabi_setlocale);
    crtLocale.functions["localeconv"] = fn(STUB_P);
    crtLocale.functions["_configthreadlocale"] = fn(STUB_I);
    loader.registerShim("api-ms-win-crt-locale-l1-1-0.dll", std::move(crtLocale));

    ShimLibrary crtTime;
    crtTime.name = "api-ms-win-crt-time-l1-1-0.dll";
    crtTime.functions["clock"] = fn((void*)msabi_clock);
    crtTime.functions["time"] = fn((void*)msabi_time);
    crtTime.functions["strftime"] = fn((void*)msabi_strftime);
    loader.registerShim("api-ms-win-crt-time-l1-1-0.dll", std::move(crtTime));

    ShimLibrary crtFileIo;
    crtFileIo.name = "api-ms-win-crt-filesystem-l1-1-0.dll";
    crtFileIo.functions["fopen"] = fn((void*)msabi_fopen);
    crtFileIo.functions["fclose"] = fn((void*)msabi_fclose);
    crtFileIo.functions["fread"] = fn((void*)msabi_fread);
    crtFileIo.functions["fwrite"] = fn((void*)msabi_fwrite);
    crtFileIo.functions["fseek"] = fn((void*)msabi_fseek);
    crtFileIo.functions["ftell"] = fn((void*)msabi_ftell);
    crtFileIo.functions["fgets"] = fn((void*)msabi_fgets);
    loader.registerShim("api-ms-win-crt-filesystem-l1-1-0.dll", std::move(crtFileIo));

    ShimLibrary crtPrivate;
    crtPrivate.name = "api-ms-win-crt-private-l1-1-0.dll";
    crtPrivate.functions["memcpy"] = fn((void*)msabi_memcpy);
    crtPrivate.functions["memset"] = fn((void*)msabi_memset);
    crtPrivate.functions["memmove"] = fn((void*)msabi_memmove);
    crtPrivate.functions["memcmp"] = fn((void*)msabi_memcmp);
    crtPrivate.functions["strlen"] = fn((void*)msabi_strlen);
    crtPrivate.functions["strcmp"] = fn((void*)msabi_strcmp);
    loader.registerShim("api-ms-win-crt-private-l1-1-0.dll", std::move(crtPrivate));

    ShimLibrary crtEnv;
    crtEnv.name = "api-ms-win-crt-environment-l1-1-0.dll";
    crtEnv.functions["__p__environ"] = fn((void*)+[]() -> void* { return &g_environ_data; });
    crtEnv.functions["getenv"] = fn((void*)+[](const char* name) -> char* { return getenv(name); });
    loader.registerShim("api-ms-win-crt-environment-l1-1-0.dll", std::move(crtEnv));

    printf("Loading %s...\n", argv[1]);

    std::string exePath(argv[1]);
    auto lastSlash = exePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        loader.addSearchPath(exePath.substr(0, lastSlash));
    }

    char absPath[4096];
    realpath(argv[1], absPath);
    win32::setExePath(absPath);

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

    struct sigaction sa = {};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    g_mainModule = main;

    uint8_t* fakeTeb = nullptr;
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

        fakeTeb = (uint8_t*)mmap(nullptr, 0x10000, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(fakeTeb, 0, 0x10000);

        fakeTeb[0x30] = 0;
        uint64_t tebAddr = (uint64_t)fakeTeb;
        memcpy(&fakeTeb[0x30], &tebAddr, 8);
        uint64_t stackTop = (uint64_t)mmap(nullptr, 0x100000, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        uint64_t stackBase = stackTop + 0x100000;
        memcpy(&fakeTeb[0x08], &stackTop, 8);
        memcpy(&fakeTeb[0x10], &stackBase, 8);
        uint64_t pebAddr = (uint64_t)(fakeTeb + 0x2000);
        memcpy(&fakeTeb[0x60], &pebAddr, 8);

        fakeTeb[0x2000 + 0x02] = 1;
        fakeTeb[0x2000 + 0x04] = 1;

        uint64_t tlsIndexAddr = (uint64_t)(fakeTeb + 0x1480);
        uint64_t tlsValueAddr = (uint64_t)fakeTeb;
        memcpy(&fakeTeb[0x1480], &tlsIndexAddr, 8);
        memcpy(&fakeTeb[0x1488], &tlsValueAddr, 8);

        __asm__ volatile (
            "mov %0, %%rax\n"
            "mov %%rax, %%gs:0x30\n"
            : : "r"(tebAddr) : "rax", "memory"
        );

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
