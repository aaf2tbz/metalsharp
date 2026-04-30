#include <metalsharp/CoreAudioBackend.h>
#include <metalsharp/Logger.h>
#include <deque>
#include <mutex>
#include <vector>
#include <cstring>
#import <AudioUnit/AudioUnit.h>
#import <AVFoundation/AVFoundation.h>

namespace metalsharp {

struct AudioBufferEntry {
    std::vector<uint8_t> data;
    uint32_t readPos = 0;
    uint32_t loopCount = 0;
};

struct CoreAudioBackend::Impl {
    AudioUnit audioUnit = nullptr;
    bool active = false;
    bool playing = false;
    float volume = 1.0f;
    float frequencyRatio = 1.0f;
    XAudio2WaveFormat format = {};
    uint32_t bytesPerSample = 2;
    std::deque<AudioBufferEntry> bufferQueue;
    mutable std::mutex bufferMutex;
};

static OSStatus audioRenderCallback(void* inRefCon,
                                      AudioUnitRenderActionFlags* ioActionFlags,
                                      const AudioTimeStamp* inTimeStamp,
                                      UInt32 inBusNumber,
                                      UInt32 inNumberFrames,
                                      AudioBufferList* ioData) {
    auto* impl = static_cast<CoreAudioBackend::Impl*>(inRefCon);

    if (!impl->playing || !ioData) {
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        return noErr;
    }

    std::lock_guard<std::mutex> lock(impl->bufferMutex);

    for (UInt32 bufIdx = 0; bufIdx < ioData->mNumberBuffers; bufIdx++) {
        auto& outBuf = ioData->mBuffers[bufIdx];
        uint8_t* outPtr = static_cast<uint8_t*>(outBuf.mData);
        uint32_t bytesRemaining = outBuf.mDataByteSize;
        uint32_t outOffset = 0;

        while (bytesRemaining > 0 && !impl->bufferQueue.empty()) {
            auto& front = impl->bufferQueue.front();
            uint32_t available = static_cast<uint32_t>(front.data.size()) - front.readPos;

            if (available == 0) {
                impl->bufferQueue.pop_front();
                continue;
            }

            uint32_t toCopy = std::min(bytesRemaining, available);
            memcpy(outPtr + outOffset, front.data.data() + front.readPos, toCopy);
            front.readPos += toCopy;
            outOffset += toCopy;
            bytesRemaining -= toCopy;
        }

        if (bytesRemaining > 0) {
            memset(outPtr + outOffset, 0, bytesRemaining);
        }
    }

    if (impl->volume < 1.0f && impl->volume >= 0.0f) {
        for (UInt32 bufIdx = 0; bufIdx < ioData->mNumberBuffers; bufIdx++) {
            auto& buf = ioData->mBuffers[bufIdx];
            int16_t* samples = static_cast<int16_t*>(buf.mData);
            uint32_t sampleCount = buf.mDataByteSize / sizeof(int16_t);
            for (uint32_t i = 0; i < sampleCount; i++) {
                samples[i] = static_cast<int16_t>(samples[i] * impl->volume);
            }
        }
    }

    return noErr;
}

CoreAudioBackend::CoreAudioBackend() : m_impl(new Impl()) {}
CoreAudioBackend::~CoreAudioBackend() { shutdown(); delete m_impl; }

bool CoreAudioBackend::init() {
    m_impl->active = true;
    m_impl->playing = false;
    m_impl->volume = 1.0f;
    m_impl->frequencyRatio = 1.0f;

    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        MS_WARN("CoreAudioBackend: failed to find default audio output component");
        return true;
    }

    AudioUnit audioUnit = nullptr;
    OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
    if (status != noErr || !audioUnit) {
        MS_WARN("CoreAudioBackend: failed to create AudioUnit (%d)", (int)status);
        return true;
    }

    m_impl->audioUnit = audioUnit;

    XAudio2WaveFormat defaultFmt = {};
    defaultFmt.formatTag = 1;
    defaultFmt.channels = 2;
    defaultFmt.samplesPerSec = 44100;
    defaultFmt.bitsPerSample = 16;
    defaultFmt.blockAlign = 4;
    defaultFmt.avgBytesPerSec = 44100 * 4;
    m_impl->format = defaultFmt;
    m_impl->bytesPerSample = 2;

    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = 44100.0;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = 4;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 4;
    asbd.mChannelsPerFrame = 2;
    asbd.mBitsPerChannel = 16;

    status = AudioUnitSetProperty(audioUnit,
                                   kAudioUnitProperty_StreamFormat,
                                   kAudioUnitScope_Input,
                                   0,
                                   &asbd,
                                   sizeof(asbd));
    if (status != noErr) {
        MS_WARN("CoreAudioBackend: failed to set stream format (%d)", (int)status);
    }

    AURenderCallbackStruct callback = {};
    callback.inputProc = audioRenderCallback;
    callback.inputProcRefCon = m_impl;

    status = AudioUnitSetProperty(audioUnit,
                                   kAudioUnitProperty_SetRenderCallback,
                                   kAudioUnitScope_Input,
                                   0,
                                   &callback,
                                   sizeof(callback));
    if (status != noErr) {
        MS_WARN("CoreAudioBackend: failed to set render callback (%d)", (int)status);
    }

    status = AudioUnitInitialize(audioUnit);
    if (status != noErr) {
        MS_WARN("CoreAudioBackend: failed to initialize AudioUnit (%d)", (int)status);
    }

    MS_INFO("CoreAudioBackend: AudioUnit initialized (44100 Hz, 16-bit, stereo)");
    return true;
}

void CoreAudioBackend::shutdown() {
    if (m_impl->playing) stop();

    if (m_impl->audioUnit) {
        AudioOutputUnitStop(m_impl->audioUnit);
        AudioUnitUninitialize(m_impl->audioUnit);
        AudioComponentInstanceDispose(m_impl->audioUnit);
        m_impl->audioUnit = nullptr;
    }

    m_impl->active = false;
    m_impl->bufferQueue.clear();
}

bool CoreAudioBackend::isActive() const { return m_impl->active; }

bool CoreAudioBackend::submitBuffer(const void* data, uint32_t size, const XAudio2WaveFormat& format) {
    if (!data || size == 0) return false;

    std::lock_guard<std::mutex> lock(m_impl->bufferMutex);
    m_impl->format = format;

    m_impl->bytesPerSample = format.bitsPerSample == 8 ? 1 :
                              format.bitsPerSample == 16 ? 2 :
                              format.bitsPerSample == 24 ? 3 :
                              format.bitsPerSample == 32 ? 4 : 2;

    AudioBufferEntry buf;
    buf.data.assign(static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + size);
    buf.readPos = 0;
    buf.loopCount = 0;
    m_impl->bufferQueue.push_back(std::move(buf));
    return true;
}

void CoreAudioBackend::setVolume(float v) {
    m_impl->volume = v < 0 ? 0 : (v > 1 ? 1 : v);
    if (m_impl->audioUnit) {
        AudioUnitSetParameter(m_impl->audioUnit,
                              kHALOutputParam_Volume,
                              kAudioUnitScope_Output,
                              0,
                              m_impl->volume,
                              0);
    }
}

float CoreAudioBackend::volume() const { return m_impl->volume; }

void CoreAudioBackend::play() {
    m_impl->playing = true;
    if (m_impl->audioUnit) {
        AudioOutputUnitStart(m_impl->audioUnit);
    }
}

void CoreAudioBackend::stop() {
    if (m_impl->audioUnit) {
        AudioOutputUnitStop(m_impl->audioUnit);
    }
    m_impl->playing = false;
}

void CoreAudioBackend::pause() {
    if (m_impl->audioUnit) {
        AudioOutputUnitStop(m_impl->audioUnit);
    }
    m_impl->playing = false;
}

void CoreAudioBackend::setFrequencyRatio(float ratio) {
    m_impl->frequencyRatio = ratio < 0.0f ? 0.0f : ratio;
}

float CoreAudioBackend::frequencyRatio() const { return m_impl->frequencyRatio; }

uint32_t CoreAudioBackend::queuedBufferCount() const {
    std::lock_guard<std::mutex> lock(m_impl->bufferMutex);
    return static_cast<uint32_t>(m_impl->bufferQueue.size());
}

void CoreAudioBackend::flushBuffers() {
    std::lock_guard<std::mutex> lock(m_impl->bufferMutex);
    m_impl->bufferQueue.clear();
}

}
