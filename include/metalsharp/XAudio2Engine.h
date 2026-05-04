/// @file XAudio2Engine.h
/// @brief XAudio2 API translation layer backed by CoreAudio.
///
/// Translates IXAudio2SourceVoice operations (create voice, submit buffer, start, stop,
/// set volume) into CoreAudioBackend calls. Serves as the core of the xaudio2_9.dll shim,
/// bridging the XAudio2 mixing graph to a single CoreAudio output stream. Games that
/// use XAudio2 for playback hit this layer first.

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
