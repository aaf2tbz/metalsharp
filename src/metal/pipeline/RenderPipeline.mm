#include <metalsharp/PipelineState.h>
#include <metalsharp/MetalBackend.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

struct PipelineState::Impl {
    id<MTLRenderPipelineState> pipelineState = nil;
};

PipelineState::PipelineState() : m_impl(new Impl()) {}
PipelineState::~PipelineState() { delete m_impl; }

bool PipelineState::init(MetalDevice& device, const PipelineStateDesc& desc) {
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device.nativeDevice();

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];

    if (desc.vertexFunction) {
        pipelineDesc.vertexFunction = (__bridge id<MTLFunction>)desc.vertexFunction;
    }
    if (desc.fragmentFunction) {
        pipelineDesc.fragmentFunction = (__bridge id<MTLFunction>)desc.fragmentFunction;
    }

    pipelineDesc.colorAttachments[0].pixelFormat = (MTLPixelFormat)desc.colorPixelFormat;
    if (desc.blendEnabled) {
        pipelineDesc.colorAttachments[0].blendingEnabled = YES;
        pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    }

    if (desc.depthPixelFormat != 0) {
        pipelineDesc.depthAttachmentPixelFormat = (MTLPixelFormat)desc.depthPixelFormat;
    }
    if (desc.stencilPixelFormat != 0) {
        pipelineDesc.stencilAttachmentPixelFormat = (MTLPixelFormat)desc.stencilPixelFormat;
    }

    if (desc.vertexStride > 0) {
        MTLVertexDescriptor* vertDesc = [[MTLVertexDescriptor alloc] init];
        vertDesc.layouts[0].stride = desc.vertexStride;
        vertDesc.layouts[0].stepRate = 1;
        vertDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        vertDesc.attributes[0].format = MTLVertexFormatFloat3;
        vertDesc.attributes[0].offset = 0;
        vertDesc.attributes[0].bufferIndex = 0;

        vertDesc.attributes[1].format = MTLVertexFormatFloat4;
        vertDesc.attributes[1].offset = 12;
        vertDesc.attributes[1].bufferIndex = 0;

        pipelineDesc.vertexDescriptor = vertDesc;
    }

    NSError* error = nil;
    m_impl->pipelineState = [mtlDevice newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!m_impl->pipelineState) {
        if (error) {
            NSLog(@"MetalSharp pipeline creation error: %@", [error localizedDescription]);
        }
        return false;
    }
    return true;
}

PipelineState* PipelineState::create(MetalDevice& device, const PipelineStateDesc& desc) {
    auto* ps = new PipelineState();
    if (!ps->init(device, desc)) {
        delete ps;
        return nullptr;
    }
    return ps;
}

void* PipelineState::nativeRenderPipelineState() const {
    return (__bridge void*)m_impl->pipelineState;
}

}
