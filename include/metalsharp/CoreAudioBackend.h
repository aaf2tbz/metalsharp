#pragma once

#include <metalsharp/Platform.h>
#include <cstdint>

namespace metalsharp {

struct XAudio2WaveFormat {
    UINT formatTag;
    UINT channels;
    UINT samplesPerSec;
    UINT avgBytesPerSec;
    UINT blockAlign;
    UINT bitsPerSample;
};

class CoreAudioBackend {
public:
    CoreAudioBackend();
    ~CoreAudioBackend();

    bool init();
    void shutdown();
    bool isActive() const;

    bool submitBuffer(const void* data, uint32_t size, const XAudio2WaveFormat& format);
    void setVolume(float volume);
    float volume() const;
    void play();
    void stop();
    void pause();
    void setFrequencyRatio(float ratio);
    float frequencyRatio() const;

    uint32_t queuedBufferCount() const;
    void flushBuffers();

    struct Impl;

private:
    Impl* m_impl;
};

}
