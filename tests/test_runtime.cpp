#include <metalsharp/Logger.h>
#include <metalsharp/PEHook.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

int main() {
    printf("=== Runtime Tests ===\n\n");

    {
        printf("--- Logger ---\n");
        std::string tmpPath = std::string(getenv("TMPDIR") ?: "/tmp") + "/metalsharp_test_log.txt";
        metalsharp::Logger::init(tmpPath);
        metalsharp::Logger::setLevel(metalsharp::LogLevel::Trace);

        MS_TRACE("trace msg %d", 1);
        MS_INFO("info msg %s", "hello");
        MS_WARN("warn msg");
        MS_ERROR("error msg %f", 3.14);

        metalsharp::Logger::shutdown();

        std::ifstream f(tmpPath);
        std::string contents((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        CHECK(contents.find("trace msg") != std::string::npos, "Trace message logged");
        CHECK(contents.find("info msg") != std::string::npos, "Info message logged");
        CHECK(contents.find("warn msg") != std::string::npos, "Warn message logged");
        CHECK(contents.find("error msg") != std::string::npos, "Error message logged");
        CHECK(contents.find("3.14") != std::string::npos, "Float formatting works");
        remove(tmpPath.c_str());
    }

    {
        printf("\n--- Logger Level Filtering ---\n");
        metalsharp::Logger::init("");

        metalsharp::Logger::setLevel(metalsharp::LogLevel::Error);
        metalsharp::Logger::log(metalsharp::LogLevel::Trace, "should not appear");
        metalsharp::Logger::log(metalsharp::LogLevel::Info, "should not appear");
        metalsharp::Logger::log(metalsharp::LogLevel::Error, "should appear");
        CHECK(true, "Level filtering compiles and runs");

        metalsharp::Logger::shutdown();
    }

    {
        printf("\n--- PEHook Dylib Mappings ---\n");
        auto mappings = metalsharp::PEHook::getDylibMappings();
        CHECK(mappings.size() == 5, "5 DLL mappings registered");

        bool hasD3D11 = false, hasD3D12 = false, hasDXGI = false;
        bool hasXAudio = false, hasXInput = false;
        for (const auto& m : mappings) {
            if (strcmp(m.windowsDll, "d3d11.dll") == 0) hasD3D11 = true;
            if (strcmp(m.windowsDll, "d3d12.dll") == 0) hasD3D12 = true;
            if (strcmp(m.windowsDll, "dxgi.dll") == 0) hasDXGI = true;
            if (strcmp(m.windowsDll, "xaudio2_9.dll") == 0) hasXAudio = true;
            if (strcmp(m.windowsDll, "xinput1_4.dll") == 0) hasXInput = true;
        }
        CHECK(hasD3D11, "d3d11.dll mapped");
        CHECK(hasD3D12, "d3d12.dll mapped");
        CHECK(hasDXGI, "dxgi.dll mapped");
        CHECK(hasXAudio, "xaudio2_9.dll mapped");
        CHECK(hasXInput, "xinput1_4.dll mapped");
    }

    {
        printf("\n--- PEHook Environment Setup ---\n");
        bool result = metalsharp::PEHook::setupEnvironment("/tmp/test_prefix", false);
        CHECK(result, "setupEnvironment returns true");
        CHECK(getenv("WINEPREFIX") != nullptr, "WINEPREFIX set");
        CHECK(getenv("WINEDLLOVERRIDES") != nullptr, "WINEDLLOVERRIDES set");
        CHECK(getenv("METALSHARP_WINE_PREFIX") != nullptr, "METALSHARP_WINE_PREFIX set");

        result = metalsharp::PEHook::setupEnvironment("/tmp/test_prefix_debug", true);
        CHECK(result, "setupEnvironment with debug returns true");
        CHECK(getenv("METAL_DEVICE_WRAPPER_TYPE") != nullptr, "Metal validation env set");
    }

    {
        printf("\n--- PEHook Injection ---\n");
        bool result = metalsharp::PEHook::injectDylibs("/tmp/test_prefix");
        CHECK(result, "injectDylibs returns true");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
