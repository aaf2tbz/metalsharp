#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "m12core.h"
#include "winemetal.h"

using Clock = std::chrono::steady_clock;

@interface MSPressureRenderRecord : NSObject
@property (nonatomic, strong) id<MTLRenderPipelineState> pipeline;
@property (nonatomic, strong) NSArray<NSNumber*>* colorFormats;
@property (nonatomic, assign) uint32_t depthFormat;
@property (nonatomic, assign) uint32_t stencilFormat;
@property (nonatomic, assign) uint32_t sampleCount;
@end

@implementation MSPressureRenderRecord
@end

@interface MSPressureDrawableTarget : NSObject
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) CAMetalLayer* layer;
@end

@implementation MSPressureDrawableTarget
@end

struct RenderPsoPool {
    __strong NSMutableArray<MSPressureRenderRecord*>* records;
    std::atomic<uint64_t> next{0};

    RenderPsoPool() : records([[NSMutableArray alloc] init]) {}

    void add(MSPressureRenderRecord* record) {
        if (!record || !record.pipeline)
            return;
        @synchronized(records) {
            [records addObject:record];
        }
    }

    MSPressureRenderRecord* takeNext() {
        @synchronized(records) {
            NSUInteger count = [records count];
            if (!count)
                return nil;
            uint64_t index = next.fetch_add(1, std::memory_order_relaxed);
            return records[(NSUInteger)(index % count)];
        }
    }

    NSUInteger count() {
        @synchronized(records) {
            return [records count];
        }
    }
};

static NSString* stringValue(NSDictionary* dict, NSString* key, NSString* fallback = @"") {
    id value = dict[key];
    return [value isKindOfClass:[NSString class]] ? (NSString*)value : fallback;
}

static NSDictionary* dictValue(NSDictionary* dict, NSString* key) {
    id value = dict[key];
    return [value isKindOfClass:[NSDictionary class]] ? (NSDictionary*)value : nil;
}

static NSArray* arrayValue(NSDictionary* dict, NSString* key) {
    id value = dict[key];
    return [value isKindOfClass:[NSArray class]] ? (NSArray*)value : nil;
}

static uint64_t parseHex64(NSString* text, uint64_t fallback) {
    if (![text isKindOfClass:[NSString class]] || [text length] == 0)
        return fallback;
    const char* s = [text UTF8String];
    while (*s == ' ' || *s == '\t')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    char* end = nullptr;
    uint64_t value = strtoull(s, &end, 16);
    return end && end != s ? value : fallback;
}

static uint64_t pipelineKey(NSDictionary* pipeline, uint32_t worker, uint32_t iteration, bool salt) {
    NSString* name = stringValue(pipeline, @"name", @"");
    NSRange dash = [name rangeOfString:@"-" options:NSBackwardsSearch];
    NSString* suffix = dash.location != NSNotFound ? [name substringFromIndex:dash.location + 1] : name;
    uint64_t key = parseHex64(suffix, 0xcbf29ce484222325ULL);
    if (salt) {
        key ^= 0x9e3779b97f4a7c15ULL * (uint64_t)(worker + 1);
        key ^= 0xbf58476d1ce4e5b9ULL * (uint64_t)(iteration + 1);
    }
    return key;
}

static enum WMTPixelFormat pixelFormat(NSString* name) {
    if (!name || [name length] == 0)
        return WMTPixelFormatInvalid;
    NSString* n = [name lowercaseString];
    if ([n isEqualToString:@"invalid"] || [n isEqualToString:@"unknown"])
        return WMTPixelFormatInvalid;
    if ([n isEqualToString:@"r8unorm"] || [n isEqualToString:@"r8_unorm"])
        return WMTPixelFormatR8Unorm;
    if ([n isEqualToString:@"rg8unorm"] || [n isEqualToString:@"r8g8_unorm"])
        return WMTPixelFormatRG8Unorm;
    if ([n isEqualToString:@"r16unorm"] || [n isEqualToString:@"r16_unorm"])
        return WMTPixelFormatR16Unorm;
    if ([n isEqualToString:@"r16float"] || [n isEqualToString:@"r16_float"])
        return WMTPixelFormatR16Float;
    if ([n isEqualToString:@"rg16unorm"] || [n isEqualToString:@"r16g16_unorm"])
        return WMTPixelFormatRG16Unorm;
    if ([n isEqualToString:@"rg16float"] || [n isEqualToString:@"r16g16_float"])
        return WMTPixelFormatRG16Float;
    if ([n isEqualToString:@"rgba8unorm"] || [n isEqualToString:@"r8g8b8a8_unorm"])
        return WMTPixelFormatRGBA8Unorm;
    if ([n isEqualToString:@"rgba8unorm_srgb"] || [n isEqualToString:@"r8g8b8a8_unorm_srgb"])
        return WMTPixelFormatRGBA8Unorm_sRGB;
    if ([n isEqualToString:@"bgra8unorm"] || [n isEqualToString:@"b8g8r8a8_unorm"])
        return WMTPixelFormatBGRA8Unorm;
    if ([n isEqualToString:@"bgra8unorm_srgb"] || [n isEqualToString:@"b8g8r8a8_unorm_srgb"])
        return WMTPixelFormatBGRA8Unorm_sRGB;
    if ([n isEqualToString:@"rgba16unorm"] || [n isEqualToString:@"r16g16b16a16_unorm"])
        return WMTPixelFormatRGBA16Unorm;
    if ([n isEqualToString:@"rgba16float"] || [n isEqualToString:@"r16g16b16a16_float"])
        return WMTPixelFormatRGBA16Float;
    if ([n isEqualToString:@"rgb10a2unorm"] || [n isEqualToString:@"r10g10b10a2_unorm"])
        return WMTPixelFormatRGB10A2Unorm;
    if ([n isEqualToString:@"rg11b10float"] || [n isEqualToString:@"r11g11b10_float"])
        return WMTPixelFormatRG11B10Float;
    if ([n isEqualToString:@"r32float"] || [n isEqualToString:@"r32_float"])
        return WMTPixelFormatR32Float;
    if ([n isEqualToString:@"r32uint"] || [n isEqualToString:@"r32_uint"])
        return WMTPixelFormatR32Uint;
    if ([n isEqualToString:@"rgba32float"] || [n isEqualToString:@"r32g32b32a32_float"])
        return WMTPixelFormatRGBA32Float;
    if ([n isEqualToString:@"depth16unorm"] || [n isEqualToString:@"d16_unorm"])
        return WMTPixelFormatDepth16Unorm;
    if ([n isEqualToString:@"depth32float"] || [n isEqualToString:@"d32_float"])
        return WMTPixelFormatDepth32Float;
    if ([n isEqualToString:@"depth24unorm_stencil8"] || [n isEqualToString:@"d24_unorm_s8_uint"])
        return WMTPixelFormatDepth24Unorm_Stencil8;
    if ([n isEqualToString:@"depth32float_stencil8"] || [n isEqualToString:@"d32_float_s8x24_uint"])
        return WMTPixelFormatDepth32Float_Stencil8;
    return WMTPixelFormatInvalid;
}

static uint32_t formatByteSize(uint32_t format) {
    switch (format) {
    case WMTAttributeFormatUChar2:
    case WMTAttributeFormatChar2:
        return 2;
    case WMTAttributeFormatUChar3:
    case WMTAttributeFormatChar3:
        return 3;
    case WMTAttributeFormatUChar4:
    case WMTAttributeFormatChar4:
    case WMTAttributeFormatUChar4Normalized:
    case WMTAttributeFormatChar4Normalized:
    case WMTAttributeFormatFloat:
    case WMTAttributeFormatInt:
    case WMTAttributeFormatUInt:
        return 4;
    case WMTAttributeFormatUShort2:
    case WMTAttributeFormatShort2:
    case WMTAttributeFormatHalf2:
    case WMTAttributeFormatFloat2:
    case WMTAttributeFormatInt2:
    case WMTAttributeFormatUInt2:
        return 8;
    case WMTAttributeFormatFloat3:
    case WMTAttributeFormatInt3:
    case WMTAttributeFormatUInt3:
        return 12;
    case WMTAttributeFormatFloat4:
    case WMTAttributeFormatInt4:
    case WMTAttributeFormatUInt4:
        return 16;
    default:
        return 16;
    }
}

static NSData* readShaderBytes(NSDictionary* shader, uint32_t* inputKind, NSString** pathOut) {
    NSString* msl = stringValue(shader, @"msl", @"");
    if ([msl length] > 0 && [[NSFileManager defaultManager] isReadableFileAtPath:msl]) {
        *inputKind = M12CORE_SHADER_FUNCTION_INPUT_MSL_SOURCE;
        if (pathOut)
            *pathOut = msl;
        return [NSData dataWithContentsOfFile:msl];
    }
    NSString* metallib = stringValue(shader, @"metallib", @"");
    if ([metallib length] > 0 && [[NSFileManager defaultManager] isReadableFileAtPath:metallib]) {
        *inputKind = M12CORE_SHADER_FUNCTION_INPUT_METALLIB;
        if (pathOut)
            *pathOut = metallib;
        return [NSData dataWithContentsOfFile:metallib];
    }
    return nil;
}

static id<MTLFunction> createShaderFunction(id<MTLDevice> device, NSDictionary* shader, uint32_t stage,
                                            std::atomic<uint64_t>& shaderFailures) {
    if (![shader isKindOfClass:[NSDictionary class]]) {
        shaderFailures.fetch_add(1);
        return nil;
    }
    uint32_t inputKind = 0;
    NSString* path = nil;
    NSData* bytes = readShaderBytes(shader, &inputKind, &path);
    if (!bytes || [bytes length] == 0) {
        fprintf(stderr, "shader input missing hash=%s stage=%u\n", [stringValue(shader, @"hash", @"") UTF8String],
                stage);
        shaderFailures.fetch_add(1);
        return nil;
    }

    M12CoreShaderFunctionDesc desc = {};
    desc.abi_version = M12CORE_ABI_VERSION;
    desc.stage = stage;
    desc.input_kind = inputKind;
    desc.shader_hash = parseHex64(stringValue(shader, @"hash", @""), (uint64_t)[path hash]);
    desc.device_handle = (uint64_t)(uintptr_t)device;
    desc.input_data = [bytes bytes];
    desc.input_size = [bytes length];
    NSString* fn = stringValue(shader, @"function", @"");
    if ([fn length] > 0)
        snprintf(desc.entry_point, sizeof(desc.entry_point), "%s", [fn UTF8String]);

    M12CoreShaderFunctionResult result = {};
    int rc = m12core_create_shader_function(&desc, &result);
    if (rc != 0 || result.abi_version != M12CORE_ABI_VERSION || result.status != M12CORE_SHADER_FUNCTION_STATUS_OK ||
        !result.function_handle) {
        fprintf(stderr, "m12core shader function failed hash=%s stage=%u rc=%d status=%u path=%s\n",
                [stringValue(shader, @"hash", @"") UTF8String], stage, rc, result.status,
                path ? [path UTF8String] : "");
        if (result.error_handle) {
            NSError* error = CFBridgingRelease((void*)(uintptr_t)result.error_handle);
            fprintf(stderr, "  error=%s\n", [[error localizedDescription] UTF8String]);
        }
        shaderFailures.fetch_add(1);
        return nil;
    }
    return (__bridge_transfer id<MTLFunction>)(void*)(uintptr_t)result.function_handle;
}

struct Counters {
    std::atomic<uint64_t> pso_ok{0};
    std::atomic<uint64_t> pso_failed{0};
    std::atomic<uint64_t> pso_cache_hits{0};
    std::atomic<uint64_t> shader_failed{0};
    std::atomic<uint64_t> command_buffers_requested{0};
    std::atomic<uint64_t> command_buffers_committed{0};
    std::atomic<uint64_t> command_buffers_completed{0};
    std::atomic<uint64_t> command_buffers_error_status{0};
    std::atomic<uint64_t> render_passes_encoded{0};
    std::atomic<uint64_t> render_passes_skipped{0};
    std::atomic<uint64_t> render_passes_failed{0};
    std::atomic<uint64_t> drawables_requested{0};
    std::atomic<uint64_t> drawables_acquired{0};
    std::atomic<uint64_t> drawable_passes_encoded{0};
    std::atomic<uint64_t> drawable_passes_failed{0};
    std::atomic<uint64_t> drawable_presents{0};
    std::atomic<uint64_t> drawable_nil{0};
    std::atomic<uint64_t> slot_timeouts{0};
};

static bool createComputePipeline(id<MTLDevice> device, NSDictionary* pipeline, uint32_t worker, uint32_t iteration,
                                  bool salt, Counters& counters) {
    id<MTLFunction> compute = createShaderFunction(device, dictValue(pipeline, @"shader"), M12CORE_SHADER_STAGE_COMPUTE,
                                                   counters.shader_failed);
    if (!compute)
        return false;
    WMTComputePipelineInfo info = {};
    info.compute_function = (obj_handle_t)(uintptr_t)compute;

    M12CorePipelineCreateDesc desc = {};
    desc.abi_version = M12CORE_ABI_VERSION;
    desc.kind = M12CORE_PIPELINE_KIND_COMPUTE;
    desc.device_handle = (uint64_t)(uintptr_t)device;
    desc.cache_key = pipelineKey(pipeline, worker, iteration, salt);
    desc.pipeline_info = &info;
    desc.pipeline_info_size = sizeof(info);

    M12CorePipelineCreateResult result = {};
    int rc = m12core_create_pipeline_state(&desc, &result);
    if (rc == 0 && result.abi_version == M12CORE_ABI_VERSION && result.status == M12CORE_PIPELINE_CREATE_STATUS_OK &&
        result.pipeline_handle) {
        if (result.cache_hit)
            counters.pso_cache_hits.fetch_add(1);
        id pso = CFBridgingRelease((void*)(uintptr_t)result.pipeline_handle);
        (void)pso;
        return true;
    }
    if (result.error_handle) {
        NSError* error = CFBridgingRelease((void*)(uintptr_t)result.error_handle);
        fprintf(stderr, "compute PSO failed name=%s status=%u error=%s\n",
                [stringValue(pipeline, @"name", @"") UTF8String], result.status,
                [[error localizedDescription] UTF8String]);
    }
    return false;
}

static void fillVertexDescriptor(NSDictionary* pipeline, WMTVertexDescriptor& vd,
                                 uint32_t strides[WMT_MAX_VERTEX_BUFFER_LAYOUTS]) {
    NSDictionary* layout = dictValue(pipeline, @"input_layout");
    NSArray* elements = layout ? arrayValue(layout, @"elements") : nil;
    if (![elements isKindOfClass:[NSArray class]])
        return;
    for (NSDictionary* element in elements) {
        if (![element isKindOfClass:[NSDictionary class]])
            continue;
        if ([element[@"system_value"] respondsToSelector:@selector(boolValue)] && [element[@"system_value"] boolValue])
            continue;
        uint32_t attr = [element[@"register"] respondsToSelector:@selector(unsignedIntValue)]
                            ? [element[@"register"] unsignedIntValue]
                            : UINT32_MAX;
        uint32_t fmt = [element[@"metal_format"] respondsToSelector:@selector(unsignedIntValue)]
                           ? [element[@"metal_format"] unsignedIntValue]
                           : 0;
        uint32_t slot = [element[@"table_index"] respondsToSelector:@selector(unsignedIntValue)]
                            ? [element[@"table_index"] unsignedIntValue]
                            : 0;
        uint32_t offset = [element[@"offset"] respondsToSelector:@selector(unsignedIntValue)]
                              ? [element[@"offset"] unsignedIntValue]
                              : 0;
        if (attr >= WMT_MAX_VERTEX_ATTRIBUTES || slot >= WMT_MAX_VERTEX_BUFFER_LAYOUTS ||
            fmt == WMTAttributeFormatInvalid)
            continue;
        vd.attributes[attr].format = (WMTAttributeFormat)fmt;
        vd.attributes[attr].offset = offset;
        vd.attributes[attr].buffer_index = slot;
        if (attr + 1 > vd.attribute_count)
            vd.attribute_count = attr + 1;
        if (slot + 1 > vd.layout_count)
            vd.layout_count = slot + 1;
        uint32_t end = offset + formatByteSize(fmt);
        if (end > strides[slot])
            strides[slot] = end;
        NSString* klass = stringValue(element, @"class", @"per_vertex");
        vd.layouts[slot].step_function =
            [klass isEqualToString:@"per_instance"] ? WMTVertexStepFunctionPerInstance : WMTVertexStepFunctionPerVertex;
        vd.layouts[slot].step_rate = [element[@"step_rate"] respondsToSelector:@selector(unsignedIntValue)]
                                         ? [element[@"step_rate"] unsignedIntValue]
                                         : 1;
    }
    for (uint32_t i = 0; i < vd.layout_count; i++) {
        vd.layouts[i].stride = strides[i] ? strides[i] : 16;
        if (vd.layouts[i].step_rate == 0)
            vd.layouts[i].step_rate = 1;
    }
}

static bool createRenderPipeline(id<MTLDevice> device, NSDictionary* pipeline, uint32_t worker, uint32_t iteration,
                                 bool salt, Counters& counters, RenderPsoPool* renderPool) {
    id<MTLFunction> vertex = createShaderFunction(device, dictValue(pipeline, @"vertex"), M12CORE_SHADER_STAGE_VERTEX,
                                                  counters.shader_failed);
    if (!vertex)
        return false;
    id<MTLFunction> fragment = nil;
    NSDictionary* fragmentDict = dictValue(pipeline, @"fragment");
    if (fragmentDict)
        fragment = createShaderFunction(device, fragmentDict, M12CORE_SHADER_STAGE_PIXEL, counters.shader_failed);
    if (fragmentDict && !fragment)
        return false;

    WMTVertexDescriptor vd = {};
    uint32_t strides[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    fillVertexDescriptor(pipeline, vd, strides);

    WMTRenderPipelineInfo info = {};
    NSArray* formats = arrayValue(pipeline, @"color_formats");
    for (NSUInteger i = 0; i < 8; i++) {
        NSString* fmt =
            (formats && i < [formats count] && [formats[i] isKindOfClass:[NSString class]]) ? formats[i] : @"invalid";
        info.colors[i].pixel_format = pixelFormat(fmt);
        info.colors[i].write_mask = WMTColorWriteMaskAll;
        info.colors[i].rgb_blend_operation = WMTBlendOperationAdd;
        info.colors[i].alpha_blend_operation = WMTBlendOperationAdd;
        info.colors[i].src_rgb_blend_factor = WMTBlendFactorOne;
        info.colors[i].dst_rgb_blend_factor = WMTBlendFactorZero;
        info.colors[i].src_alpha_blend_factor = WMTBlendFactorOne;
        info.colors[i].dst_alpha_blend_factor = WMTBlendFactorZero;
    }
    info.rasterization_enabled = true;
    info.raster_sample_count = [pipeline[@"sample_count"] respondsToSelector:@selector(unsignedCharValue)]
                                   ? [pipeline[@"sample_count"] unsignedCharValue]
                                   : 1;
    if (info.raster_sample_count == 0)
        info.raster_sample_count = 1;
    info.depth_pixel_format = pixelFormat(stringValue(pipeline, @"depth_format", @"invalid"));
    info.stencil_pixel_format = pixelFormat(stringValue(pipeline, @"stencil_format", @"invalid"));
    info.vertex_function = (obj_handle_t)(uintptr_t)vertex;
    info.fragment_function = (obj_handle_t)(uintptr_t)fragment;
    info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle;
    info.tessellation_partition_mode = WMTTessellationPartitionModePow2;
    info.tessellation_output_winding_order = WMTWindingClockwise;
    info.tessellation_factor_step = WMTTessellationFactorStepFunctionConstant;
    info.max_tessellation_factor = 1;
    info.vertex_descriptor = (vd.attribute_count || vd.layout_count) ? &vd : nullptr;

    M12CorePipelineCreateDesc desc = {};
    desc.abi_version = M12CORE_ABI_VERSION;
    desc.kind = M12CORE_PIPELINE_KIND_RENDER;
    desc.device_handle = (uint64_t)(uintptr_t)device;
    desc.cache_key = pipelineKey(pipeline, worker, iteration, salt);
    desc.pipeline_info = &info;
    desc.pipeline_info_size = sizeof(info);

    M12CorePipelineCreateResult result = {};
    int rc = m12core_create_pipeline_state(&desc, &result);
    if (rc == 0 && result.abi_version == M12CORE_ABI_VERSION && result.status == M12CORE_PIPELINE_CREATE_STATUS_OK &&
        result.pipeline_handle) {
        if (result.cache_hit)
            counters.pso_cache_hits.fetch_add(1);
        id<MTLRenderPipelineState> pso = CFBridgingRelease((void*)(uintptr_t)result.pipeline_handle);
        if (renderPool && pso) {
            NSMutableArray<NSNumber*>* recordFormats = [NSMutableArray arrayWithCapacity:8];
            for (NSUInteger i = 0; i < 8; i++)
                [recordFormats addObject:@((uint32_t)info.colors[i].pixel_format)];
            MSPressureRenderRecord* record = [[MSPressureRenderRecord alloc] init];
            record.pipeline = pso;
            record.colorFormats = recordFormats;
            record.depthFormat = (uint32_t)info.depth_pixel_format;
            record.stencilFormat = (uint32_t)info.stencil_pixel_format;
            record.sampleCount = info.raster_sample_count ? info.raster_sample_count : 1;
            renderPool->add(record);
        }
        return true;
    }
    if (result.error_handle) {
        NSError* error = CFBridgingRelease((void*)(uintptr_t)result.error_handle);
        fprintf(stderr, "render PSO failed name=%s status=%u error=%s\n",
                [stringValue(pipeline, @"name", @"") UTF8String], result.status,
                [[error localizedDescription] UTF8String]);
    } else {
        fprintf(stderr, "render PSO failed name=%s rc=%d status=%u\n", [stringValue(pipeline, @"name", @"") UTF8String],
                rc, result.status);
    }
    return false;
}

struct CompletionSlot {
    std::atomic<uint64_t> serial{0};
    std::atomic<bool> completed{true};
    std::atomic<int> status{0};
    __strong id<MTLCommandBuffer> cmdbuf;
};

static bool waitSlot(CompletionSlot* slot, uint32_t slotWaitMs, Counters& counters) {
    id<MTLCommandBuffer> cmdbuf = slot->cmdbuf;
    if (!cmdbuf)
        return true;
    if (slotWaitMs == 0) {
        [cmdbuf waitUntilCompleted];
    } else {
        auto deadline = Clock::now() + std::chrono::milliseconds(slotWaitMs);
        while (!slot->completed.load(std::memory_order_acquire) && Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!slot->completed.load(std::memory_order_acquire)) {
            counters.slot_timeouts.fetch_add(1);
            return false;
        }
    }
    if (!slot->completed.load(std::memory_order_acquire)) {
        slot->status.store((int)[cmdbuf status]);
        slot->completed.store(true, std::memory_order_release);
    }
    slot->cmdbuf = nil;
    return true;
}

struct RenderTargetCache {
    __strong NSMutableDictionary<NSString*, id<MTLTexture>>* textures;
    __strong id<MTLBuffer> dummyBuffer;
    __strong id<MTLSamplerState> sampler;

    RenderTargetCache(id<MTLDevice> device) : textures([[NSMutableDictionary alloc] init]) {
        dummyBuffer = [device newBufferWithLength:65536 options:MTLResourceStorageModeShared];
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        sampler = [device newSamplerStateWithDescriptor:samplerDesc];
    }
};

static id<MTLTexture> renderTextureForFormat(id<MTLDevice> device, RenderTargetCache& cache, uint32_t format,
                                             uint32_t sampleCount) {
    if (format == WMTPixelFormatInvalid)
        return nil;
    NSString* key = [NSString stringWithFormat:@"%u:%u", format, sampleCount ? sampleCount : 1];
    id<MTLTexture> existing = cache.textures[key];
    if (existing)
        return existing;

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.pixelFormat = (MTLPixelFormat)format;
    desc.width = 64;
    desc.height = 64;
    desc.mipmapLevelCount = 1;
    desc.sampleCount = sampleCount ? sampleCount : 1;
    desc.textureType = desc.sampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> texture = nil;
    @try {
        texture = [device newTextureWithDescriptor:desc];
    } @catch (NSException* exception) {
        texture = nil;
    }
    if (texture)
        cache.textures[key] = texture;
    return texture;
}

static bool encodeOffscreenRenderPass(id<MTLDevice> device, id<MTLCommandBuffer> cb, RenderTargetCache& cache,
                                      RenderPsoPool* renderPool, Counters& counters) {
    MSPressureRenderRecord* record = renderPool ? renderPool->takeNext() : nil;
    if (!record || !record.pipeline) {
        counters.render_passes_skipped.fetch_add(1);
        return false;
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    bool hasAttachment = false;
    NSUInteger colorCount = [record.colorFormats count];
    for (NSUInteger i = 0; i < colorCount && i < 8; i++) {
        uint32_t fmt = [record.colorFormats[i] unsignedIntValue];
        id<MTLTexture> tex = renderTextureForFormat(device, cache, fmt, record.sampleCount);
        if (!tex)
            continue;
        pass.colorAttachments[i].texture = tex;
        pass.colorAttachments[i].loadAction = MTLLoadActionClear;
        pass.colorAttachments[i].storeAction = MTLStoreActionDontCare;
        pass.colorAttachments[i].clearColor = MTLClearColorMake(0.02, 0.03, 0.04, 1.0);
        hasAttachment = true;
    }

    id<MTLTexture> depthStencil = nil;
    if (record.depthFormat != WMTPixelFormatInvalid)
        depthStencil = renderTextureForFormat(device, cache, record.depthFormat, record.sampleCount);
    if (depthStencil) {
        pass.depthAttachment.texture = depthStencil;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
        hasAttachment = true;
    }
    if (record.stencilFormat != WMTPixelFormatInvalid) {
        id<MTLTexture> stencil =
            depthStencil ?: renderTextureForFormat(device, cache, record.stencilFormat, record.sampleCount);
        if (stencil) {
            pass.stencilAttachment.texture = stencil;
            pass.stencilAttachment.loadAction = MTLLoadActionClear;
            pass.stencilAttachment.storeAction = MTLStoreActionDontCare;
            pass.stencilAttachment.clearStencil = 0;
            hasAttachment = true;
        }
    }
    if (!hasAttachment || !cache.dummyBuffer) {
        counters.render_passes_skipped.fetch_add(1);
        return false;
    }

    @try {
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
        if (!enc) {
            counters.render_passes_failed.fetch_add(1);
            return false;
        }
        [enc setRenderPipelineState:record.pipeline];
        for (NSUInteger i = 0; i < 31; i++) {
            [enc setVertexBuffer:cache.dummyBuffer offset:0 atIndex:i];
            [enc setFragmentBuffer:cache.dummyBuffer offset:0 atIndex:i];
        }
        if (cache.sampler) {
            for (NSUInteger i = 0; i < 16; i++) {
                [enc setVertexSamplerState:cache.sampler atIndex:i];
                [enc setFragmentSamplerState:cache.sampler atIndex:i];
            }
        }
        for (NSUInteger i = 0; i < 16; i++) {
            id<MTLTexture> tex = pass.colorAttachments[0].texture ?: depthStencil;
            if (tex) {
                [enc setVertexTexture:tex atIndex:i];
                [enc setFragmentTexture:tex atIndex:i];
            }
        }
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        counters.render_passes_encoded.fetch_add(1);
        return true;
    } @catch (NSException* exception) {
        fprintf(stderr, "offscreen render encode failed: %s\n", [[exception reason] UTF8String]);
        counters.render_passes_failed.fetch_add(1);
        return false;
    }
}

static void pumpWindowEvents() {
    NSApplication* app = [NSApplication sharedApplication];
    for (;;) {
        NSEvent* event = [app nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate distantPast]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
        if (!event)
            break;
        [app sendEvent:event];
    }
    [app updateWindows];
}

static MSPressureDrawableTarget* createDrawableTarget(id<MTLDevice> device, uint32_t width, uint32_t height,
                                                      bool offscreenWindow, bool displaySync) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    CGFloat w = width ? width : 128;
    CGFloat h = height ? height : 96;
    NSRect frame = offscreenWindow ? NSMakeRect(-10000, -10000, w, h) : NSMakeRect(48, 48, w, h);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setOpaque:NO];
    [window setAlphaValue:offscreenWindow ? 0.02 : 0.08];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setIgnoresMouseEvents:YES];
    [window setReleasedWhenClosed:NO];

    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    [view setWantsLayer:YES];
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.drawableSize = CGSizeMake(w, h);
    layer.maximumDrawableCount = 3;
    layer.displaySyncEnabled = displaySync ? YES : NO;
    view.layer = layer;
    window.contentView = view;
    [window orderFrontRegardless];
    pumpWindowEvents();

    MSPressureDrawableTarget* target = [[MSPressureDrawableTarget alloc] init];
    target.window = window;
    target.layer = layer;
    return target;
}

static void churnDrawableTarget(MSPressureDrawableTarget* target, uint64_t index) {
    if (!target || !target.layer || !target.window)
        return;
    static const CGSize sizes[] = {
        {128, 96}, {192, 128}, {64, 64}, {320, 180}, {1, 1}, {147, 95},
    };
    CGSize size = sizes[index % (sizeof(sizes) / sizeof(sizes[0]))];
    target.layer.drawableSize = size;
    NSRect frame = [target.window frame];
    frame.size = NSMakeSize(size.width, size.height);
    [target.window setFrame:frame display:YES];
    pumpWindowEvents();
}

static bool encodeDrawablePresent(id<MTLCommandBuffer> cb, MSPressureDrawableTarget* target, Counters& counters) {
    if (!target || !target.layer) {
        counters.drawable_nil.fetch_add(1);
        return false;
    }
    counters.drawables_requested.fetch_add(1);
    id<CAMetalDrawable> drawable = nil;
    @try {
        drawable = [target.layer nextDrawable];
    } @catch (NSException* exception) {
        fprintf(stderr, "nextDrawable exception: %s\n", [[exception reason] UTF8String]);
        counters.drawable_nil.fetch_add(1);
        return false;
    }
    if (!drawable) {
        counters.drawable_nil.fetch_add(1);
        return false;
    }
    counters.drawables_acquired.fetch_add(1);

    @try {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        double phase = (double)(counters.drawable_passes_encoded.load() % 255u) / 255.0;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(phase, 0.04, 0.10, 1.0);
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
        if (!enc) {
            counters.drawable_passes_failed.fetch_add(1);
            return false;
        }
        [enc endEncoding];
        [cb presentDrawable:drawable];
        counters.drawable_passes_encoded.fetch_add(1);
        counters.drawable_presents.fetch_add(1);
        return true;
    } @catch (NSException* exception) {
        fprintf(stderr, "drawable present encode failed: %s\n", [[exception reason] UTF8String]);
        counters.drawable_passes_failed.fetch_add(1);
        return false;
    }
}

static void runCommandBufferPressure(id<MTLDevice> device, uint32_t queueDepth, uint32_t durationMs,
                                     uint32_t slotWaitMs, std::atomic<int>& psoActive, Counters& counters,
                                     RenderPsoPool* renderPool, bool encodeRenderPasses, bool encodeDrawablePresents,
                                     bool offscreenWindow, bool displaySync, uint32_t drawableWindowCount,
                                     uint64_t minCommandBuffers, uint32_t drawableChurnInterval) {
    id<MTLCommandQueue> queue = [device newCommandQueueWithMaxCommandBufferCount:queueDepth ? queueDepth : 64];
    if (!queue) {
        fprintf(stderr, "newCommandQueue failed\n");
        return;
    }
    RenderTargetCache renderTargets(device);
    NSMutableArray<MSPressureDrawableTarget*>* drawableTargets = [NSMutableArray array];
    if (encodeDrawablePresents) {
        uint32_t count = drawableWindowCount ? drawableWindowCount : 1;
        for (uint32_t i = 0; i < count; i++) {
            MSPressureDrawableTarget* target =
                createDrawableTarget(device, 128 + 8 * i, 96 + 4 * i, offscreenWindow, displaySync);
            if (target)
                [drawableTargets addObject:target];
        }
    }
    uint32_t depth = queueDepth ? queueDepth : 64;
    std::vector<std::unique_ptr<CompletionSlot>> slots;
    for (uint32_t i = 0; i < depth; i++)
        slots.push_back(std::make_unique<CompletionSlot>());
    uint64_t serial = 0;
    uint64_t index = 0;
    auto deadline = Clock::now() + std::chrono::milliseconds(durationMs ? durationMs : 30000);
    while (Clock::now() < deadline) {
        bool needMoreWork = psoActive.load() > 0 || counters.command_buffers_committed.load() < depth ||
                            counters.command_buffers_committed.load() < minCommandBuffers;
        if (encodeRenderPasses && renderPool) {
            uint64_t renderAttempts = counters.render_passes_encoded.load() + counters.render_passes_failed.load();
            needMoreWork = needMoreWork || renderAttempts < renderPool->count();
        }
        if (!needMoreWork)
            break;
        @autoreleasepool {
            CompletionSlot* slot = slots[index % depth].get();
            if (!waitSlot(slot, slotWaitMs, counters))
                break;
            counters.command_buffers_requested.fetch_add(1);
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            if (!cb) {
                fprintf(stderr, "commandBuffer returned nil at index=%" PRIu64 "\n", index);
                break;
            }
            uint64_t thisSerial = ++serial;
            slot->serial.store(thisSerial, std::memory_order_release);
            slot->completed.store(false, std::memory_order_release);
            slot->status.store(-1);
            slot->cmdbuf = cb;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
              if (slot->serial.load(std::memory_order_acquire) == thisSerial) {
                  MTLCommandBufferStatus status = [done status];
                  slot->status.store((int)status);
                  slot->completed.store(true, std::memory_order_release);
                  counters.command_buffers_completed.fetch_add(1);
                  if (status != MTLCommandBufferStatusCompleted)
                      counters.command_buffers_error_status.fetch_add(1);
              }
            }];
            bool rendered = false;
            if (encodeRenderPasses)
                rendered = encodeOffscreenRenderPass(device, cb, renderTargets, renderPool, counters);
            bool presented = false;
            if (encodeDrawablePresents) {
                MSPressureDrawableTarget* target =
                    [drawableTargets count] ? drawableTargets[(NSUInteger)(index % [drawableTargets count])] : nil;
                if (drawableChurnInterval && index > 0 && (index % drawableChurnInterval) == 0)
                    churnDrawableTarget(target, index / drawableChurnInterval);
                presented = encodeDrawablePresent(cb, target, counters);
            }
            if (!rendered && !presented) {
                id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                [blit endEncoding];
            }
            [cb commit];
            if (encodeDrawablePresents)
                pumpWindowEvents();
            counters.command_buffers_committed.fetch_add(1);
            index++;
        }
    }
    for (auto& slot : slots)
        waitSlot(slot.get(), slotWaitMs, counters);
    for (MSPressureDrawableTarget* target in drawableTargets) {
        if (target.window) {
            [target.window orderOut:nil];
            [target.window close];
        }
    }
    if ([drawableTargets count])
        pumpWindowEvents();
}

static NSDictionary* loadJSON(NSString* path) {
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data)
        return nil;
    id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    return [obj isKindOfClass:[NSDictionary class]] ? (NSDictionary*)obj : nil;
}

static void usage(const char* argv0) {
    fprintf(stderr,
            "usage: %s --manifest manifest.json --output result.json [--mode "
            "baseline|storm|pso-cmdb|pso-render-cmdb|pso-drawable-cmdb] [--pso-workers N] [--iterations N] "
            "[--queue-depth N] [--duration-ms N] [--slot-wait-ms N] [--max-pipelines N] [--min-command-buffers N] "
            "[--drawable-window-count N] [--drawable-churn-interval N] [--salt-cache-keys] [--onscreen-window] "
            "[--display-sync]\n",
            argv0);
}

int main(int argc, const char** argv) {
    @autoreleasepool {
        NSString* manifestPath = nil;
        NSString* outputPath = nil;
        std::string mode = "baseline";
        uint32_t psoWorkers = 1;
        uint32_t iterations = 1;
        uint32_t queueDepth = 8;
        uint32_t durationMs = 30000;
        uint32_t slotWaitMs = 0; // 0 intentionally uses waitUntilCompleted; wrapper enforces process timeout.
        uint32_t maxPipelines = 0;
        uint64_t minCommandBuffers = 0;
        uint32_t drawableWindowCount = 1;
        uint32_t drawableChurnInterval = 0;
        bool saltCacheKeys = false;
        bool offscreenWindow = true;
        bool displaySync = false;

        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--manifest") && i + 1 < argc)
                manifestPath = [NSString stringWithUTF8String:argv[++i]];
            else if (!strcmp(argv[i], "--output") && i + 1 < argc)
                outputPath = [NSString stringWithUTF8String:argv[++i]];
            else if (!strcmp(argv[i], "--mode") && i + 1 < argc)
                mode = argv[++i];
            else if (!strcmp(argv[i], "--pso-workers") && i + 1 < argc)
                psoWorkers = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--iterations") && i + 1 < argc)
                iterations = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--queue-depth") && i + 1 < argc)
                queueDepth = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--duration-ms") && i + 1 < argc)
                durationMs = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--slot-wait-ms") && i + 1 < argc)
                slotWaitMs = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--max-pipelines") && i + 1 < argc)
                maxPipelines = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--min-command-buffers") && i + 1 < argc)
                minCommandBuffers = strtoull(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--drawable-window-count") && i + 1 < argc)
                drawableWindowCount = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--drawable-churn-interval") && i + 1 < argc)
                drawableChurnInterval = (uint32_t)strtoul(argv[++i], nullptr, 10);
            else if (!strcmp(argv[i], "--salt-cache-keys"))
                saltCacheKeys = true;
            else if (!strcmp(argv[i], "--onscreen-window"))
                offscreenWindow = false;
            else if (!strcmp(argv[i], "--display-sync"))
                displaySync = true;
            else {
                usage(argv[0]);
                return 2;
            }
        }
        if (!manifestPath || !outputPath) {
            usage(argv[0]);
            return 2;
        }
        if (psoWorkers == 0)
            psoWorkers = 1;
        if (iterations == 0)
            iterations = 1;

        NSDictionary* manifest = loadJSON(manifestPath);
        NSArray* allPipelines = manifest ? arrayValue(manifest, @"pipelines") : nil;
        if (!allPipelines || [allPipelines count] == 0) {
            fprintf(stderr, "manifest has no pipelines: %s\n", [manifestPath UTF8String]);
            return 2;
        }
        NSUInteger count = [allPipelines count];
        if (maxPipelines > 0 && count > maxPipelines)
            count = maxPipelines;
        NSArray* pipelines = [allPipelines subarrayWithRange:NSMakeRange(0, count)];

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n");
            return 2;
        }

        Counters counters;
        RenderPsoPool renderPool;
        bool encodeRenderPasses = mode == "pso-render-cmdb" || mode == "pso-drawable-cmdb";
        bool encodeDrawablePresents = mode == "pso-drawable-cmdb";
        std::atomic<uint64_t> next{0};
        std::atomic<int> active{(int)psoWorkers};
        uint64_t totalWork = (uint64_t)count * iterations;
        auto started = Clock::now();

        std::vector<std::thread> threads;
        for (uint32_t w = 0; w < psoWorkers; w++) {
            threads.emplace_back([&, w]() {
                @autoreleasepool {
                    while (true) {
                        uint64_t seq = next.fetch_add(1);
                        if (seq >= totalWork)
                            break;
                        uint32_t iter = (uint32_t)(seq / count);
                        NSDictionary* pipeline = pipelines[(NSUInteger)(seq % count)];
                        NSString* type = stringValue(pipeline, @"type", @"");
                        bool ok = false;
                        if ([type isEqualToString:@"render"])
                            ok = createRenderPipeline(device, pipeline, w, iter, saltCacheKeys, counters,
                                                      encodeRenderPasses ? &renderPool : nullptr);
                        else if ([type isEqualToString:@"compute"])
                            ok = createComputePipeline(device, pipeline, w, iter, saltCacheKeys, counters);
                        else
                            fprintf(stderr, "skipping unsupported pipeline type=%s name=%s\n", [type UTF8String],
                                    [stringValue(pipeline, @"name", @"") UTF8String]);
                        if (ok)
                            counters.pso_ok.fetch_add(1);
                        else
                            counters.pso_failed.fetch_add(1);
                    }
                }
                active.fetch_sub(1);
            });
        }

        if (mode == "pso-cmdb" || mode == "pso-render-cmdb" || mode == "pso-drawable-cmdb")
            runCommandBufferPressure(device, queueDepth, durationMs, slotWaitMs, active, counters, &renderPool,
                                     encodeRenderPasses, encodeDrawablePresents, offscreenWindow, displaySync,
                                     drawableWindowCount, minCommandBuffers, drawableChurnInterval);

        for (auto& thread : threads)
            thread.join();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();

        NSDictionary* result = @{
            @"schema" : @"metalsharp.m12.probe-pso-submit-pressure.v1",
            @"ok" : @(counters.pso_failed.load() == 0 && counters.slot_timeouts.load() == 0 &&
                      counters.command_buffers_error_status.load() == 0 && counters.render_passes_failed.load() == 0 &&
                      counters.drawable_passes_failed.load() == 0),
            @"mode" : [NSString stringWithUTF8String:mode.c_str()],
            @"device" : [device name] ? [device name] : @"",
            @"pipeline_count" : @(count),
            @"pso_workers" : @(psoWorkers),
            @"iterations" : @(iterations),
            @"queue_depth" : @(queueDepth),
            @"duration_ms" : @(durationMs),
            @"slot_wait_ms" : @(slotWaitMs),
            @"salt_cache_keys" : @(saltCacheKeys),
            @"window_mode" : encodeDrawablePresents ? (offscreenWindow ? @"offscreen" : @"onscreen") : @"none",
            @"display_sync" : @(displaySync),
            @"min_command_buffers" : @(minCommandBuffers),
            @"drawable_window_count" : @(drawableWindowCount),
            @"drawable_churn_interval" : @(drawableChurnInterval),
            @"elapsed_ms" : @(elapsedMs),
            @"pso_ok" : @(counters.pso_ok.load()),
            @"pso_failed" : @(counters.pso_failed.load()),
            @"pso_cache_hits" : @(counters.pso_cache_hits.load()),
            @"shader_failed" : @(counters.shader_failed.load()),
            @"command_buffers_requested" : @(counters.command_buffers_requested.load()),
            @"command_buffers_committed" : @(counters.command_buffers_committed.load()),
            @"command_buffers_completed" : @(counters.command_buffers_completed.load()),
            @"command_buffers_error_status" : @(counters.command_buffers_error_status.load()),
            @"render_pool_count" : @(renderPool.count()),
            @"render_passes_encoded" : @(counters.render_passes_encoded.load()),
            @"render_passes_skipped" : @(counters.render_passes_skipped.load()),
            @"render_passes_failed" : @(counters.render_passes_failed.load()),
            @"drawables_requested" : @(counters.drawables_requested.load()),
            @"drawables_acquired" : @(counters.drawables_acquired.load()),
            @"drawable_passes_encoded" : @(counters.drawable_passes_encoded.load()),
            @"drawable_passes_failed" : @(counters.drawable_passes_failed.load()),
            @"drawable_presents" : @(counters.drawable_presents.load()),
            @"drawable_nil" : @(counters.drawable_nil.load()),
            @"slot_timeouts" : @(counters.slot_timeouts.load()),
        };
        NSData* json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
        [json writeToFile:outputPath atomically:YES];
        fwrite([json bytes], 1, [json length], stdout);
        fputc('\n', stdout);
        return [result[@"ok"] boolValue] ? 0 : 1;
    }
}
