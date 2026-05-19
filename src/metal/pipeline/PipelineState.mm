/// @file PipelineState.mm
/// @brief Metal render pipeline state — MTLRenderPipelineState creation and caching.
///
/// Objective-C++ implementation that constructs MTLRenderPipelineState objects from
/// compiled Metal shaders and vertex descriptors. Includes pipeline state caching
/// keyed on descriptor hash for amortized creation cost.

#include <cstdlib>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <metalsharp/Logger.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/PipelineCache.h>
#include <metalsharp/PipelineState.h>

namespace metalsharp {

struct PipelineState::Impl {
    id<MTLRenderPipelineState> pipelineState = nil;
};

PipelineState::PipelineState() : m_impl(new Impl()) {}
PipelineState::~PipelineState() {
    delete m_impl;
}

static MTLBlendFactor translateBlendFactor(uint32_t d3dFactor) {
    switch (d3dFactor) {
    case 1:
        return MTLBlendFactorZero;
    case 2:
        return MTLBlendFactorOne;
    case 3:
        return MTLBlendFactorSourceColor;
    case 4:
        return MTLBlendFactorOneMinusSourceColor;
    case 5:
        return MTLBlendFactorSourceAlpha;
    case 6:
        return MTLBlendFactorOneMinusSourceAlpha;
    case 7:
        return MTLBlendFactorDestinationAlpha;
    case 8:
        return MTLBlendFactorOneMinusDestinationAlpha;
    case 9:
        return MTLBlendFactorDestinationColor;
    case 10:
        return MTLBlendFactorOneMinusDestinationColor;
    case 11:
        return MTLBlendFactorSourceAlphaSaturated;
    case 14:
        return MTLBlendFactorBlendColor;
    case 15:
        return MTLBlendFactorOneMinusBlendColor;
    default:
        return MTLBlendFactorOne;
    }
}

static MTLBlendOperation translateBlendOp(uint32_t d3dOp) {
    switch (d3dOp) {
    case 1:
        return MTLBlendOperationAdd;
    case 2:
        return MTLBlendOperationSubtract;
    case 3:
        return MTLBlendOperationReverseSubtract;
    case 4:
        return MTLBlendOperationMin;
    case 5:
        return MTLBlendOperationMax;
    default:
        return MTLBlendOperationAdd;
    }
}

static NSString* binaryArchiveDirectory() {
    const char* overrideDir = std::getenv("METALSHARP_BINARY_ARCHIVE_DIR");
    if (overrideDir && overrideDir[0] != '\0')
        return [NSString stringWithUTF8String:overrideDir];

    NSString* home = NSHomeDirectory();
    return [home stringByAppendingPathComponent:@".metalsharp/shader-cache/binary-archives"];
}

static uint32_t hashAttachmentState(const PipelineStateDesc& desc) {
    uint64_t hash = 2166136261u;
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i) {
        hash = PipelineCache::combineHash(hash, desc.blendEnabled[i] ? 1 : 0);
        hash = PipelineCache::combineHash(hash, desc.srcBlend[i]);
        hash = PipelineCache::combineHash(hash, desc.destBlend[i]);
        hash = PipelineCache::combineHash(hash, desc.blendOp[i]);
        hash = PipelineCache::combineHash(hash, desc.srcBlendAlpha[i]);
        hash = PipelineCache::combineHash(hash, desc.destBlendAlpha[i]);
        hash = PipelineCache::combineHash(hash, desc.blendOpAlpha[i]);
        hash = PipelineCache::combineHash(hash, desc.renderTargetWriteMask[i]);
    }
    return static_cast<uint32_t>(hash);
}

static uint64_t pipelineArchiveHash(const PipelineStateDesc& desc) {
    PipelineCacheKey key;
    key.vertexShaderHash = reinterpret_cast<uint64_t>(desc.vertexFunction);
    key.pixelShaderHash = reinterpret_cast<uint64_t>(desc.fragmentFunction);
    key.vertexLayoutHash = desc.vertexStride;
    key.depthStencilFormat = desc.depthPixelFormat ^ (desc.stencilPixelFormat << 16);
    key.sampleCount = 1;
    key.blendStateHash = hashAttachmentState(desc);
    key.rasterStateHash = desc.cullMode ^ (desc.depthClipEnabled ? 0x80000000u : 0);
    key.depthStateHash = (desc.depthEnabled ? 1u : 0u) | (desc.depthWriteEnabled ? 2u : 0u);
    key.featureFlags = 0x1;
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i)
        key.renderTargetFormats[i] = desc.colorPixelFormats[i];
    return PipelineCache::computeKeyHash(key);
}

static id<MTLBinaryArchive> prepareBinaryArchive(id<MTLDevice> mtlDevice, MTLRenderPipelineDescriptor* pipelineDesc,
                                                 uint64_t hash, NSURL** outURL) {
    if (!mtlDevice)
        return nil;
    if (@available(macOS 11.0, *)) {
        NSString* archiveDir = binaryArchiveDirectory();
        [[NSFileManager defaultManager] createDirectoryAtPath:archiveDir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        NSString* archiveName = [NSString stringWithFormat:@"%016llx.metallibarchive", hash];
        NSString* archivePath = [archiveDir stringByAppendingPathComponent:archiveName];
        NSURL* archiveURL = [NSURL fileURLWithPath:archivePath];
        if (outURL)
            *outURL = archiveURL;

        MTLBinaryArchiveDescriptor* archiveDesc = [[MTLBinaryArchiveDescriptor alloc] init];
        archiveDesc.url = archiveURL;

        NSError* archiveError = nil;
        id<MTLBinaryArchive> archive = [mtlDevice newBinaryArchiveWithDescriptor:archiveDesc error:&archiveError];
        if (!archive) {
            MS_WARN("Metal binary archive unavailable for hash=%llu error=%s", hash,
                    archiveError ? [[archiveError localizedDescription] UTF8String] : "unknown");
            return nil;
        }

        pipelineDesc.binaryArchives = @[ archive ];
        NSError* addError = nil;
        if (![archive addRenderPipelineFunctionsWithDescriptor:pipelineDesc error:&addError]) {
            MS_WARN("Metal binary archive add failed hash=%llu error=%s", hash,
                    addError ? [[addError localizedDescription] UTF8String] : "unknown");
        }
        return archive;
    }
    return nil;
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
        if (desc.colorPixelFormats[i] == 0)
            continue;
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

    uint64_t archiveHash = pipelineArchiveHash(desc);
    NSURL* archiveURL = nil;
    id<MTLBinaryArchive> archive = prepareBinaryArchive(mtlDevice, pipelineDesc, archiveHash, &archiveURL);
    PipelineCache::instance().recordMiss(archiveHash, archive ? "binary_archive_prepare" : "binary_archive_unavailable",
                                         "render_pipeline");

    NSError* error = nil;
    m_impl->pipelineState = [mtlDevice newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!m_impl->pipelineState) {
        if (error) {
            NSLog(@"MetalSharp pipeline error: %@", [error localizedDescription]);
            PipelineCache::instance().recordMiss(archiveHash, [[error localizedDescription] UTF8String],
                                                 "render_pipeline_create_failed");
        }
        return false;
    }
    if (archive) {
        if (@available(macOS 11.0, *)) {
            NSError* serializeError = nil;
            if (archiveURL && ![archive serializeToURL:archiveURL error:&serializeError]) {
                MS_WARN("Metal binary archive serialize failed hash=%llu error=%s", archiveHash,
                        serializeError ? [[serializeError localizedDescription] UTF8String] : "unknown");
            }
        }
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

} // namespace metalsharp
