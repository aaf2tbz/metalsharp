#pragma once

#include <metalsharp/Platform.h>

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
    void play();
    void stop();
    void pause();

private:
    struct Impl;
    Impl* m_impl;
};

}
