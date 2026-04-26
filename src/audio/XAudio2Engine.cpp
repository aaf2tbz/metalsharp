#include <metalsharp/XAudio2Engine.h>
#include <cstring>

namespace metalsharp {

struct SourceVoice {
    ULONG refCount = 1;
    XAudio2WaveFormat format = {};
    float volume = 1.0f;
    bool playing = false;
};

XAudio2Engine::XAudio2Engine() {}
XAudio2Engine::~XAudio2Engine() { m_backend.shutdown(); }

HRESULT XAudio2Engine::init() {
    if (m_initialized) return S_OK;
    if (!m_backend.init()) return E_FAIL;
    m_initialized = true;
    return S_OK;
}

HRESULT XAudio2Engine::createSourceVoice(void** ppVoice, const XAudio2WaveFormat* pFormat) {
    if (!ppVoice || !pFormat) return E_POINTER;
    auto* voice = new SourceVoice();
    voice->format = *pFormat;
    *ppVoice = voice;
    return S_OK;
}

HRESULT XAudio2Engine::submitSourceBuffer(void* pVoice, const void* pBufferDesc) {
    if (!pVoice) return E_POINTER;
    auto* voice = static_cast<SourceVoice*>(pVoice);
    return m_backend.submitBuffer(nullptr, 0, voice->format) ? S_OK : E_FAIL;
}

HRESULT XAudio2Engine::start(void* pVoice) {
    if (!pVoice) return E_POINTER;
    static_cast<SourceVoice*>(pVoice)->playing = true;
    m_backend.play();
    return S_OK;
}

HRESULT XAudio2Engine::stop(void* pVoice) {
    if (!pVoice) return E_POINTER;
    static_cast<SourceVoice*>(pVoice)->playing = false;
    m_backend.stop();
    return S_OK;
}

HRESULT XAudio2Engine::setVolume(void* pVoice, float volume) {
    if (!pVoice) return E_POINTER;
    static_cast<SourceVoice*>(pVoice)->volume = volume;
    m_backend.setVolume(volume);
    return S_OK;
}

}
