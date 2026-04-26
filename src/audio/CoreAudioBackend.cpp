#include <metalsharp/CoreAudioBackend.h>

namespace metalsharp {

struct CoreAudioBackend::Impl {
    bool active = false;
    float volume = 1.0f;
    XAudio2WaveFormat format = {};
};

CoreAudioBackend::CoreAudioBackend() : m_impl(new Impl()) {}
CoreAudioBackend::~CoreAudioBackend() { shutdown(); delete m_impl; }

bool CoreAudioBackend::init() {
    m_impl->active = true;
    return true;
}

void CoreAudioBackend::shutdown() { m_impl->active = false; }
bool CoreAudioBackend::isActive() const { return m_impl->active; }

bool CoreAudioBackend::submitBuffer(const void* data, uint32_t size, const XAudio2WaveFormat& format) {
    m_impl->format = format;
    return true;
}

void CoreAudioBackend::setVolume(float v) { m_impl->volume = v; }
void CoreAudioBackend::play() {}
void CoreAudioBackend::stop() {}
void CoreAudioBackend::pause() {}

}
