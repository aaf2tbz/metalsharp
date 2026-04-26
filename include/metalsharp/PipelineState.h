#pragma once

#include <metalsharp/Platform.h>

namespace metalsharp {

class MetalDevice;

struct PipelineStateDesc {
    void* vertexFunction = nullptr;
    void* fragmentFunction = nullptr;

    uint32_t colorPixelFormat = 80;
    uint32_t depthPixelFormat = 0;
    uint32_t stencilPixelFormat = 0;

    uint32_t vertexStride = 0;

    bool blendEnabled = false;

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
