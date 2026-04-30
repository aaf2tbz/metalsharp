#pragma once

#include <metalsharp/Platform.h>
#include <cstdint>
#include <vector>

namespace metalsharp {

struct WAVEFORMAT {
    UINT wFormatTag;
    UINT nChannels;
    UINT nSamplesPerSec;
    UINT nAvgBytesPerSec;
    UINT nBlockAlign;
    UINT wBitsPerSample;
    UINT cbSize;
};

struct WAVEHDR {
    void* lpData;
    UINT dwBufferLength;
    UINT dwBytesRecorded;
    uintptr_t dwUser;
    UINT dwFlags;
    UINT dwLoops;
    struct WAVEHDR* lpNext;
    uintptr_t reserved;
};

static constexpr UINT WHDR_DONE = 0x00000001;
static constexpr UINT WHDR_PREPARED = 0x00000002;
static constexpr UINT WHDR_BEGINLOOP = 0x00000004;
static constexpr UINT WHDR_ENDLOOP = 0x00000008;
static constexpr UINT WHDR_INQUEUE = 0x00000010;

class DirectSoundBackend {
public:
    static DirectSoundBackend& instance();

    bool init();
    void shutdown();

    void* createBuffer(uint32_t size, const WAVEFORMAT& format);
    void destroyBuffer(void* buffer);
    bool writeBuffer(void* buffer, const void* data, uint32_t offset, uint32_t size);
    bool playBuffer(void* buffer, uint32_t flags);
    bool stopBuffer(void* buffer);
    bool setVolume(void* buffer, float volume);
    float getVolume(void* buffer) const;

private:
    DirectSoundBackend() = default;

    struct DSBuffer {
        std::vector<uint8_t> data;
        WAVEFORMAT format;
        float volume;
        bool playing;
        uint32_t writeCursor;
    };

    std::vector<DSBuffer*> m_buffers;
    bool m_initialized = false;
};

}
