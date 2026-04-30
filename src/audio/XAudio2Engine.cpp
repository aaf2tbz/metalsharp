#include <metalsharp/XAudio2Engine.h>
#include <metalsharp/CoreAudioBackend.h>
#include <metalsharp/Logger.h>
#include <cstring>

namespace metalsharp {

struct SourceVoice {
    ULONG refCount = 1;
    XAudio2WaveFormat format = {};
    float volume = 1.0f;
    float frequencyRatio = 1.0f;
    bool playing = false;
    uint32_t buffersSubmitted = 0;
    uint32_t buffersCompleted = 0;
};

struct SubmixVoice {
    float volume = 1.0f;
    uint32_t channels = 2;
};

struct MasteringVoice {
    float volume = 1.0f;
    uint32_t channels = 2;
    uint32_t sampleRate = 44100;
};

XAudio2Engine::XAudio2Engine() {}
XAudio2Engine::~XAudio2Engine() { m_backend.shutdown(); }

HRESULT XAudio2Engine::init() {
    if (m_initialized) return S_OK;
    if (!m_backend.init()) return E_FAIL;
    m_initialized = true;
    MS_INFO("XAudio2Engine initialized");
    return S_OK;
}

HRESULT XAudio2Engine::createSourceVoice(void** ppVoice, const XAudio2WaveFormat* pFormat) {
    if (!ppVoice || !pFormat) return E_POINTER;
    auto* voice = new SourceVoice();
    voice->format = *pFormat;
    *ppVoice = voice;
    MS_TRACE("XAudio2Engine: source voice created (%u Hz, %u-bit, %u ch)",
             pFormat->samplesPerSec, pFormat->bitsPerSample, pFormat->channels);
    return S_OK;
}

HRESULT XAudio2Engine::submitSourceBuffer(void* pVoice, const void* pBufferDesc) {
    if (!pVoice) return E_POINTER;
    auto* voice = static_cast<SourceVoice*>(pVoice);

    struct XAudio2Buffer {
        uint32_t flags;
        uint32_t audioBytes;
        const void* pAudioData;
        uint32_t playBegin;
        uint32_t playLength;
        uint32_t loopBegin;
        uint32_t loopLength;
        uint32_t loopCount;
        void* pContext;
    };

    if (pBufferDesc) {
        auto* buf = static_cast<const XAudio2Buffer*>(pBufferDesc);
        if (buf->pAudioData && buf->audioBytes > 0) {
            m_backend.submitBuffer(buf->pAudioData, buf->audioBytes, voice->format);
            voice->buffersSubmitted++;
        }
    } else {
        m_backend.submitBuffer(nullptr, 0, voice->format);
    }
    return S_OK;
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
