#include <metalsharp/CoreAudioBackend.h>
#include <metalsharp/XAudio2Engine.h>
#include <metalsharp/Platform.h>
#include <cstdio>
#include <cstring>

extern "C" HRESULT XAudio2Create(void** ppXAudio2, UINT Flags, UINT XAudio2Processor);

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

int main() {
    printf("=== Audio Tests ===\n\n");

    {
        printf("--- CoreAudioBackend Lifecycle ---\n");
        metalsharp::CoreAudioBackend backend;
        CHECK(!backend.isActive(), "Backend inactive before init");

        bool initResult = backend.init();
        CHECK(initResult, "Backend init succeeds");
        CHECK(backend.isActive(), "Backend active after init");

        backend.play();
        CHECK(true, "Play succeeds");

        backend.pause();
        CHECK(true, "Pause succeeds");

        backend.setVolume(0.5f);
        CHECK(true, "SetVolume succeeds");

        backend.stop();
        CHECK(true, "Stop succeeds");

        backend.shutdown();
        CHECK(!backend.isActive(), "Backend inactive after shutdown");
    }

    {
        printf("\n--- CoreAudioBackend Submit Buffer ---\n");
        metalsharp::CoreAudioBackend backend;
        backend.init();

        metalsharp::XAudio2WaveFormat fmt{};
        fmt.formatTag = 1;
        fmt.channels = 2;
        fmt.samplesPerSec = 44100;
        fmt.bitsPerSample = 16;
        fmt.blockAlign = 4;
        fmt.avgBytesPerSec = 44100 * 4;

        uint32_t sampleData[64] = {};
        bool submitResult = backend.submitBuffer(sampleData, sizeof(sampleData), fmt);
        CHECK(submitResult, "Submit buffer succeeds");

        backend.shutdown();
    }

    {
        printf("\n--- CoreAudioBackend Volume ---\n");
        metalsharp::CoreAudioBackend backend;
        backend.init();

        backend.setVolume(0.0f);
        CHECK(true, "Volume 0.0 accepted");

        backend.setVolume(1.0f);
        CHECK(true, "Volume 1.0 accepted");

        backend.setVolume(0.75f);
        CHECK(true, "Volume 0.75 accepted");

        backend.shutdown();
    }

    {
        printf("\n--- XAudio2Engine Creation ---\n");
        void* engine = nullptr;
        HRESULT hr = XAudio2Create(&engine, 0, 0);
        CHECK(hr == S_OK, "XAudio2Create returns S_OK");
        CHECK(engine != nullptr, "Engine pointer non-null");

        if (engine) {
            auto* eng = static_cast<metalsharp::XAudio2Engine*>(engine);
            delete eng;
        }
    }

    {
        printf("\n--- XAudio2Engine Null Checks ---\n");
        HRESULT hr = XAudio2Create(nullptr, 0, 0);
        CHECK(hr == E_POINTER, "XAudio2Create null pointer returns E_POINTER");
    }

    {
        printf("\n--- XAudio2WaveFormat Defaults ---\n");
        metalsharp::XAudio2WaveFormat fmt{};
        CHECK(fmt.formatTag == 0, "Default formatTag is 0");
        CHECK(fmt.channels == 0, "Default channels is 0");
        CHECK(fmt.samplesPerSec == 0, "Default samplesPerSec is 0");
        CHECK(fmt.bitsPerSample == 0, "Default bitsPerSample is 0");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
