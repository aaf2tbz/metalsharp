#pragma once

#include <metalsharp/Platform.h>

namespace metalsharp {

class MetalDevice;

static constexpr uint32_t MAX_RENDER_TARGETS = 8;

struct PipelineStateDesc {
    void* vertexFunction = nullptr;
    void* fragmentFunction = nullptr;

    uint32_t colorPixelFormats[MAX_RENDER_TARGETS] = {};
    uint32_t numColorAttachments = 1;
    uint32_t depthPixelFormat = 0;
    uint32_t stencilPixelFormat = 0;

    uint32_t vertexStride = 0;

    bool blendEnabled[MAX_RENDER_TARGETS] = {};
    uint32_t srcBlend[MAX_RENDER_TARGETS] = {};
    uint32_t destBlend[MAX_RENDER_TARGETS] = {};
    uint32_t blendOp[MAX_RENDER_TARGETS] = {};
    uint32_t srcBlendAlpha[MAX_RENDER_TARGETS] = {};
    uint32_t destBlendAlpha[MAX_RENDER_TARGETS] = {};
    uint32_t blendOpAlpha[MAX_RENDER_TARGETS] = {};
    uint32_t renderTargetWriteMask[MAX_RENDER_TARGETS] = {15, 15, 15, 15, 15, 15, 15, 15};

    bool depthEnabled = false;
    bool depthWriteEnabled = true;

    uint32_t cullMode = 0;
    bool depthClipEnabled = true;
};

class PipelineState {
public:
    static PipelineState* create(MetalDevice& device, const PipelineStateDesc& desc);
    ~PipelineState();

    void* nativeRenderPipelineState() const;

    PipelineState(const PipelineState&) = delete;
    PipelineState& operator=(const PipelineState&) = delete;

private:
    PipelineState();
    bool init(MetalDevice& device, const PipelineStateDesc& desc);

    struct Impl;
    Impl* m_impl;
};

}
