#include <metalsharp/X3DAudioEngine.h>
#include <metalsharp/Logger.h>
#include <cmath>
#include <cstring>

namespace metalsharp {

X3DAudioEngine& X3DAudioEngine::instance() {
    static X3DAudioEngine inst;
    return inst;
}

void X3DAudioEngine::init(uint32_t speakerChannelMask) {
    m_speakerChannelMask = speakerChannelMask;
    m_initialized = true;
    MS_INFO("X3DAudioEngine initialized (speaker mask: 0x%x)", speakerChannelMask);
}

void X3DAudioEngine::shutdown() {
    m_initialized = false;
}

static float dot(const Audio3DVector& a, const Audio3DVector& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float length(const Audio3DVector& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Audio3DVector normalize(const Audio3DVector& v) {
    float len = length(v);
    if (len < 1e-6f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}

static Audio3DVector sub(const Audio3DVector& a, const Audio3DVector& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float X3DAudioEngine::computePan(const Audio3DListener& listener, const Audio3DEmitter& emitter) {
    auto dir = normalize(sub(emitter.position, listener.position));
    auto right = Audio3DVector{
        listener.front.y * listener.top.z - listener.front.z * listener.top.y,
        listener.front.z * listener.top.x - listener.front.x * listener.top.z,
        listener.front.x * listener.top.y - listener.front.y * listener.top.x
    };
    return dot(dir, right);
}

float X3DAudioEngine::computeDistanceAttenuation(float distance) {
    if (distance <= m_nearDistance) return 1.0f;
    if (distance >= m_farDistance) return 0.0f;

    float ratio = (m_farDistance - distance) / (m_farDistance - m_nearDistance);
    return std::pow(ratio, m_rolloffFactor);
}

float X3DAudioEngine::computeDoppler(const Audio3DListener& listener, const Audio3DEmitter& emitter) {
    auto toListener = sub(listener.position, emitter.position);
    float dist = length(toListener);
    if (dist < 1e-6f) return 1.0f;

    auto normDir = normalize(toListener);
    float emitterVel = dot(emitter.velocity, normDir);
    float listenerVel = dot(listener.velocity, normDir);

    float denom = m_speedOfSound - listenerVel * m_dopplerFactor;
    float numer = m_speedOfSound - emitterVel * m_dopplerFactor;

    if (denom < 1e-6f) denom = 1e-6f;
    return numer / denom;
}

void X3DAudioEngine::calculate(const Audio3DListener& listener,
                                const Audio3DEmitter& emitter,
                                uint32_t flags,
                                uint32_t dstChannelCount,
                                Audio3DOutput& output) {
    if (!m_initialized) return;

    memset(&output, 0, sizeof(Audio3DOutput));

    float dist = length(sub(emitter.position, listener.position));
    output.emitterToListenerDistance = dist;

    float attenuation = computeDistanceAttenuation(dist);
    float pan = computePan(listener, emitter);

    output.reverbLevel = attenuation * 0.5f;
    output.lfeLevel = attenuation;
    output.dopplerFactor = computeDoppler(listener, emitter);
    output.lpFDirectCoefficient = attenuation;

    for (uint32_t ch = 0; ch < dstChannelCount && ch < 2; ch++) {
        float channelGain = attenuation;
        if (dstChannelCount == 2) {
            if (ch == 0) {
                channelGain *= std::cos(pan * 0.5f * 3.14159f);
            } else {
                channelGain *= std::sin((pan + 1.0f) * 0.5f * 3.14159f);
            }
        }
        for (uint32_t src = 0; src < emitter.channelCount && src < 18; src++) {
            output.matrixCoefficients[src * dstChannelCount + ch] = channelGain;
        }
    }
}

void X3DAudioEngine::setDistanceCurve(float nearDist, float farDist, float rolloff) {
    m_nearDistance = nearDist;
    m_farDistance = farDist;
    m_rolloffFactor = rolloff;
}

void X3DAudioEngine::setDopplerFactor(float factor) {
    m_dopplerFactor = factor;
}

}
