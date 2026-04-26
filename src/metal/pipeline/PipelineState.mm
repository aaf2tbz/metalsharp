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

static MTLBlendFactor translateBlendFactor(uint32_t d3dFactor) {
    switch (d3dFactor) {
        case 1: return MTLBlendFactorZero;
        case 2: return MTLBlendFactorOne;
        case 3: return MTLBlendFactorSourceColor;
        case 4: return MTLBlendFactorOneMinusSourceColor;
        case 5: return MTLBlendFactorSourceAlpha;
        case 6: return MTLBlendFactorOneMinusSourceAlpha;
        case 7: return MTLBlendFactorDestinationAlpha;
        case 8: return MTLBlendFactorOneMinusDestinationAlpha;
        case 9: return MTLBlendFactorDestinationColor;
        case 10: return MTLBlendFactorOneMinusDestinationColor;
        case 11: return MTLBlendFactorSourceAlphaSaturated;
        case 14: return MTLBlendFactorBlendColor;
        case 15: return MTLBlendFactorOneMinusBlendColor;
        default: return MTLBlendFactorOne;
    }
}

static MTLBlendOperation translateBlendOp(uint32_t d3dOp) {
    switch (d3dOp) {
        case 1: return MTLBlendOperationAdd;
        case 2: return MTLBlendOperationSubtract;
        case 3: return MTLBlendOperationReverseSubtract;
        case 4: return MTLBlendOperationMin;
        case 5: return MTLBlendOperationMax;
        default: return MTLBlendOperationAdd;
    }
}

bool PipelineState::init(MetalDevice& device, const PipelineStateDesc& desc) {
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device.nativeDevice();

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];

    if (desc.vertexFunction) {
        pipelineDesc.vertexFunction = (__bridge id<MTLFunction>)desc.vertexFunction;
    }
    if (desc.fragmentFunction) {
        pipelineDesc.fragmentFunction = (__bridge id<MTLFunction>)desc.fragmentFunction;
    }

    for (uint32_t i = 0; i < desc.numColorAttachments && i < MAX_RENDER_TARGETS; ++i) {
        if (desc.colorPixelFormats[i] == 0) continue;
        pipelineDesc.colorAttachments[i].pixelFormat = (MTLPixelFormat)desc.colorPixelFormats[i];
        pipelineDesc.colorAttachments[i].writeMask = (MTLColorWriteMask)desc.renderTargetWriteMask[i];

        if (desc.blendEnabled[i]) {
            pipelineDesc.colorAttachments[i].blendingEnabled = YES;
            pipelineDesc.colorAttachments[i].sourceRGBBlendFactor = translateBlendFactor(desc.srcBlend[i]);
            pipelineDesc.colorAttachments[i].destinationRGBBlendFactor = translateBlendFactor(desc.destBlend[i]);
            pipelineDesc.colorAttachments[i].rgbBlendOperation = translateBlendOp(desc.blendOp[i]);
            pipelineDesc.colorAttachments[i].sourceAlphaBlendFactor = translateBlendFactor(desc.srcBlendAlpha[i]);
            pipelineDesc.colorAttachments[i].destinationAlphaBlendFactor = translateBlendFactor(desc.destBlendAlpha[i]);
            pipelineDesc.colorAttachments[i].alphaBlendOperation = translateBlendOp(desc.blendOpAlpha[i]);
        }
    }

    if (desc.depthPixelFormat != 0) {
        pipelineDesc.depthAttachmentPixelFormat = (MTLPixelFormat)desc.depthPixelFormat;
    }

    if (desc.vertexStride > 0) {
        MTLVertexDescriptor* vertDesc = [MTLVertexDescriptor vertexDescriptor];
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
            NSLog(@"MetalSharp pipeline error: %@", [error localizedDescription]);
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
