#include <metalsharp/DirectSoundBackend.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <algorithm>

namespace metalsharp {

DirectSoundBackend& DirectSoundBackend::instance() {
    static DirectSoundBackend inst;
    return inst;
}

bool DirectSoundBackend::init() {
    m_initialized = true;
    MS_INFO("DirectSoundBackend initialized");
    return true;
}

void DirectSoundBackend::shutdown() {
    for (auto* buf : m_buffers) {
        delete buf;
    }
    m_buffers.clear();
    m_initialized = false;
}

void* DirectSoundBackend::createBuffer(uint32_t size, const WAVEFORMAT& format) {
    auto* buf = new DSBuffer();
    buf->data.resize(size, 0);
    buf->format = format;
    buf->volume = 1.0f;
    buf->playing = false;
    buf->writeCursor = 0;
    m_buffers.push_back(buf);
    MS_TRACE("DirectSoundBackend: buffer created (%u bytes, %u Hz, %u-bit, %u ch)",
             size, format.nSamplesPerSec, format.wBitsPerSample, format.nChannels);
    return buf;
}

void DirectSoundBackend::destroyBuffer(void* buffer) {
    if (!buffer) return;
    auto* buf = static_cast<DSBuffer*>(buffer);
    auto it = std::find(m_buffers.begin(), m_buffers.end(), buf);
    if (it != m_buffers.end()) {
        m_buffers.erase(it);
    }
    delete buf;
}

bool DirectSoundBackend::writeBuffer(void* buffer, const void* data, uint32_t offset, uint32_t size) {
    if (!buffer || !data) return false;
    auto* buf = static_cast<DSBuffer*>(buffer);
    if (offset + size > buf->data.size()) return false;
    memcpy(buf->data.data() + offset, data, size);
    return true;
}

bool DirectSoundBackend::playBuffer(void* buffer, uint32_t flags) {
    if (!buffer) return false;
    auto* buf = static_cast<DSBuffer*>(buffer);
    buf->playing = true;
    return true;
}

bool DirectSoundBackend::stopBuffer(void* buffer) {
    if (!buffer) return false;
    auto* buf = static_cast<DSBuffer*>(buffer);
    buf->playing = false;
    return true;
}

bool DirectSoundBackend::setVolume(void* buffer, float volume) {
    if (!buffer) return false;
    static_cast<DSBuffer*>(buffer)->volume = volume;
    return true;
}

float DirectSoundBackend::getVolume(void* buffer) const {
    if (!buffer) return 0;
    return static_cast<DSBuffer*>(buffer)->volume;
}

}
