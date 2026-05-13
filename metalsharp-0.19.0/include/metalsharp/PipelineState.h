/// @file PipelineState.h
/// @brief Metal render pipeline state descriptor and creation.
///
/// Defines PipelineStateDesc which captures blend state, pixel formats, cull mode,
/// depth state, and vertex/fragment function references — a flattened representation
/// of what D3D11_BLEND_DESC, D3D11_DEPTH_STENCIL_DESC, and D3D11_RASTERIZER_DESC
/// contribute to an MTLRenderPipelineState. PipelineState wraps the compiled Metal
/// pipeline object and is produced by the D3D11 device when OMSetBlendState and
/// similar calls flush a state change.

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
