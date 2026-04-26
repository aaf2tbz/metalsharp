#pragma once

#include <metalsharp/Platform.h>
#include <metalsharp/CoreAudioBackend.h>

namespace metalsharp {

class XAudio2Engine {
public:
    XAudio2Engine();
    ~XAudio2Engine();

    HRESULT init();
    HRESULT createSourceVoice(void** ppVoice, const XAudio2WaveFormat* pFormat);
    HRESULT submitSourceBuffer(void* pVoice, const void* pBufferDesc);
    HRESULT start(void* pVoice);
    HRESULT stop(void* pVoice);
    HRESULT setVolume(void* pVoice, float volume);

private:
    CoreAudioBackend m_backend;
    bool m_initialized = false;
};

}
