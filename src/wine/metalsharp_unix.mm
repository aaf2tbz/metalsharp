#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include <mutex>

typedef long NTSTATUS;

#include "metalsharp_unix.h"

static std::mutex g_mutex;
static uint64_t g_next_handle = 0x1000;

static uint64_t new_handle() { return ++g_next_handle; }

struct MetalBuffer {
    id<MTLBuffer> buffer;
    size_t size;
    void* mapped;
};

struct MetalTexture {
    id<MTLTexture> texture;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t mip_levels;
};

struct MetalShader {
    id<MTLFunction> function;
    id<MTLLibrary> library;
    std::vector<uint8_t> bytecode;
};

struct MetalPipeline {
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
};

struct RenderTarget {
    id<MTLTexture> texture;
    uint32_t width;
    uint32_t height;
};

struct SwapChain {
    CAMetalLayer* layer;
    NSWindow* window;
    id<CAMetalDrawable> current_drawable;
    id<MTLTexture> back_buffer;
    uint32_t width;
    uint32_t height;
};

struct FrameState {
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
    
    id<MTLBuffer> vertex_buffers[16];
    uint32_t vertex_strides[16];
    uint32_t vertex_offsets[16];
    id<MTLBuffer> index_buffer;
    uint32_t index_format;
    uint32_t index_offset;
    
    id<MTLBuffer> vs_constants[14];
    id<MTLBuffer> ps_constants[14];
    id<MTLTexture> vs_resources[128];
    id<MTLTexture> ps_resources[128];
    id<MTLSamplerState> vs_samplers[16];
    id<MTLSamplerState> ps_samplers[16];
    
    id<MTLTexture> render_targets[8];
    id<MTLTexture> depth_target;
    
    float viewport_x, viewport_y, viewport_w, viewport_h, viewport_min, viewport_max;
    bool has_viewport;
    
    uint32_t primitive_topology;
    bool in_render_pass;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
};

static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_queue = nil;
static SwapChain* g_swapchain = nullptr;
static FrameState g_frame;
static std::unordered_map<uint64_t, MetalBuffer> g_buffers;
static std::unordered_map<uint64_t, MetalTexture> g_textures;
static std::unordered_map<uint64_t, MetalShader> g_shaders;
static std::unordered_map<uint64_t, id<MTLSamplerState>> g_samplers;
static std::unordered_map<uint64_t, RenderTarget> g_rtvs;
static std::unordered_map<uint64_t, MetalPipeline> g_pipelines;

static MTLPrimitiveType to_mtl_primitive(uint32_t topo) {
    switch (topo) {
        case 1: return MTLPrimitiveTypePoint;
        case 2: return MTLPrimitiveTypeLine;
        case 3: return MTLPrimitiveTypeLineStrip;
        case 4: return MTLPrimitiveTypeTriangle;
        case 5: return MTLPrimitiveTypeTriangleStrip;
        default: return MTLPrimitiveTypeTriangle;
    }
}

static MTLPixelFormat dxgi_to_mtl(uint32_t format) {
    switch (format) {
        case 28: return MTLPixelFormatBGRA8Unorm;
        case 87: return MTLPixelFormatRGBA8Unorm;
        case 2:  return MTLPixelFormatR32Float;
        case 6:  return MTLPixelFormatR16Float;
        case 11: return MTLPixelFormatRG16Float;
        case 16: return MTLPixelFormatRG32Float;
        case 24: return MTLPixelFormatR8Unorm;
        case 49: return MTLPixelFormatBC1_RGBA;
        case 50: return MTLPixelFormatBC2_RGBA;
        case 51: return MTLPixelFormatBC3_RGBA;
        case 70: return MTLPixelFormatBC7_RGBAUnorm;
        case 71: return MTLPixelFormatBC7_RGBAUnorm_sRGB;
        case 44: return MTLPixelFormatBC1_RGBA_sRGB;
        case 55: return MTLPixelFormatDepth32Float;
        case 40: return MTLPixelFormatDepth16Unorm;
        case 45: return MTLPixelFormatDepth24Unorm_Stencil8;
        case 62: return MTLPixelFormatRGB10A2Unorm;
        case 12: return MTLPixelFormatRG11B10Float;
        default: return MTLPixelFormatBGRA8Unorm;
    }
}

static NTSTATUS ms_init(void*) {
    if (g_device) return 0;
    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) return -1;
    g_queue = [g_device newCommandQueue];
    memset((void*)&g_frame, 0, sizeof(g_frame));
    fprintf(stderr, "[metalsharp] Metal device: %s\n", [[g_device name] UTF8String]);
    return 0;
}

static NTSTATUS ms_create_device(void*) {
    return ms_init(nullptr);
}

static NTSTATUS ms_create_swap_chain(void* p) {
    auto* params = (struct ms_create_swap_chain_params*)p;
    if (!g_device) ms_init(nullptr);
    
    if (g_swapchain) {
        if (g_swapchain->window) [g_swapchain->window close];
        delete g_swapchain;
    }
    g_swapchain = new SwapChain();
    g_swapchain->width = params->width > 0 ? params->width : 1280;
    g_swapchain->height = params->height > 0 ? params->height : 720;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        NSRect frame = NSMakeRect(0, 0, g_swapchain->width, g_swapchain->height);
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        g_swapchain->window = [[NSWindow alloc] initWithContentRect:frame
                                                          styleMask:style
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO];
        [g_swapchain->window setTitle:@"MetalSharp"];
        [g_swapchain->window makeKeyAndOrderFront:nil];
        
        g_swapchain->layer = [CAMetalLayer layer];
        g_swapchain->layer.device = g_device;
        g_swapchain->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_swapchain->layer.framebufferOnly = NO;
        g_swapchain->layer.drawableSize = CGSizeMake(g_swapchain->width, g_swapchain->height);
        
        NSView* cv = [g_swapchain->window contentView];
        cv.wantsLayer = YES;
        cv.layer = g_swapchain->layer;
    });
    
    fprintf(stderr, "[metalsharp] Swap chain %ux%u\n", g_swapchain->width, g_swapchain->height);
    return 0;
}

static NTSTATUS ms_present(void*) {
    if (!g_swapchain || !g_swapchain->layer) return -1;
    
    @autoreleasepool {
        if (g_frame.in_render_pass && g_frame.encoder) {
            [g_frame.encoder endEncoding];
            g_frame.in_render_pass = false;
            g_frame.encoder = nil;
        }
        if (g_frame.command_buffer) {
            [g_frame.command_buffer commit];
            g_frame.command_buffer = nil;
        }
        
        id<CAMetalDrawable> drawable = [g_swapchain->layer nextDrawable];
        if (!drawable) return 0;
        
        if (g_swapchain->back_buffer) {
            id<MTLCommandBuffer> blit = [g_queue commandBuffer];
            id<MTLBlitCommandEncoder> blitEnc = [blit blitCommandEncoder];
            [blitEnc copyFromTexture:g_swapchain->back_buffer
                         sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                           sourceSize:MTLSizeMake(g_swapchain->width, g_swapchain->height, 1)
                            toTexture:drawable.texture
                     destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
            [blitEnc endEncoding];
            [blit presentDrawable:drawable];
            [blit commit];
        } else {
            id<MTLCommandBuffer> cmd = [g_queue commandBuffer];
            [cmd presentDrawable:drawable];
            [cmd commit];
        }
    }
    
    memset((void*)&g_frame, 0, sizeof(g_frame));
    return 0;
}

static NTSTATUS ms_create_buffer(void* p) {
    auto* params = (struct ms_create_buffer_params*)p;
    if (!g_device) ms_init(nullptr);
    
    uint64_t handle = new_handle();
    auto& buf = g_buffers[handle];
    buf.size = params->byte_width;
    buf.mapped = nullptr;
    
    if (params->initial_data && params->initial_data_size > 0) {
        buf.buffer = [g_device newBufferWithBytes:params->initial_data
                                           length:params->byte_width
                                          options:MTLResourceStorageModeShared];
    } else {
        buf.buffer = [g_device newBufferWithLength:params->byte_width
                                           options:MTLResourceStorageModeShared];
    }
    
    if (!buf.buffer) {
        g_buffers.erase(handle);
        return -1;
    }
    
    params->out_handle = handle;
    return 0;
}

static NTSTATUS ms_create_texture_2d(void* p) {
    auto* params = (struct ms_create_texture_2d_params*)p;
    if (!g_device) ms_init(nullptr);
    
    uint64_t handle = new_handle();
    auto& tex = g_textures[handle];
    tex.width = params->width;
    tex.height = params->height;
    tex.format = params->format;
    tex.mip_levels = params->mip_levels > 0 ? params->mip_levels : 1;
    
    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2D;
    desc.pixelFormat = dxgi_to_mtl(params->format);
    desc.width = params->width;
    desc.height = params->height;
    desc.mipmapLevelCount = tex.mip_levels;
    desc.sampleCount = params->sample_count > 0 ? params->sample_count : 1;
    
    MTLTextureUsage usage = 0;
    if (params->bind_flags & 0x1) usage |= MTLTextureUsageShaderRead;
    if (params->bind_flags & 0x2) usage |= MTLTextureUsageRenderTarget;
    if (params->bind_flags & 0x40) usage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    if (params->bind_flags & 0x20) usage |= MTLTextureUsagePixelFormatView;
    if (usage == 0) usage = MTLTextureUsageShaderRead;
    desc.usage = usage;
    desc.storageMode = (params->cpu_access_flags & 0x10000) ? MTLStorageModeShared : MTLStorageModePrivate;
    
    tex.texture = [g_device newTextureWithDescriptor:desc];
    if (!tex.texture) {
        g_textures.erase(handle);
        return -1;
    }
    
    params->out_handle = handle;
    return 0;
}

static NTSTATUS ms_create_render_target_view(void* p) {
    auto* params = (struct ms_create_rt_view_params*)p;
    if (!g_device) ms_init(nullptr);
    
    uint64_t handle = new_handle();
    auto& rtv = g_rtvs[handle];
    
    if (params->texture_handle) {
        auto it = g_textures.find(params->texture_handle);
        if (it != g_textures.end()) {
            rtv.texture = it->second.texture;
            rtv.width = it->second.width;
            rtv.height = it->second.height;
        }
    } else if (g_swapchain) {
        @autoreleasepool {
            if (!g_swapchain->back_buffer) {
                MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
                desc.textureType = MTLTextureType2D;
                desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
                desc.width = g_swapchain->width;
                desc.height = g_swapchain->height;
                desc.mipmapLevelCount = 1;
                desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                desc.storageMode = MTLStorageModePrivate;
                g_swapchain->back_buffer = [g_device newTextureWithDescriptor:desc];
            }
            rtv.texture = g_swapchain->back_buffer;
            rtv.width = g_swapchain->width;
            rtv.height = g_swapchain->height;
        }
    }
    
    params->out_handle = handle;
    return 0;
}

static NTSTATUS ms_create_depth_stencil_view(void* p) {
    auto* params = (struct ms_create_rt_view_params*)p;
    if (!g_device) ms_init(nullptr);
    
    uint64_t handle = new_handle();
    auto& dsv = g_rtvs[handle];
    
    auto it = g_textures.find(params->texture_handle);
    if (it != g_textures.end()) {
        dsv.texture = it->second.texture;
        dsv.width = it->second.width;
        dsv.height = it->second.height;
    }
    
    params->out_handle = handle;
    return 0;
}

static NTSTATUS ms_create_vertex_shader(void* p) {
    auto* params = (struct ms_create_shader_params*)p;
    if (!g_device || !params->bytecode || params->bytecode_size == 0) return -1;
    
    uint64_t handle = new_handle();
    auto& shader = g_shaders[handle];
    auto* bc = (const uint8_t*)params->bytecode;
    shader.bytecode.assign(bc, bc + params->bytecode_size);
    
    params->out_handle = handle;
    return 0;
}

static NTSTATUS ms_create_pixel_shader(void* p) {
    return ms_create_vertex_shader(p);
}

static NTSTATUS ms_create_input_layout(void*) { return 0; }
static NTSTATUS ms_create_blend_state(void*) { return 0; }
static NTSTATUS ms_create_rasterizer_state(void*) { return 0; }
static NTSTATUS ms_create_depth_stencil_state(void*) { return 0; }
static NTSTATUS ms_create_sampler_state(void*) { return 0; }
static NTSTATUS ms_create_shader_resource_view(void*) { return 0; }

static void ensure_render_pass() {
    if (g_frame.in_render_pass) return;
    if (!g_frame.command_buffer) {
        g_frame.command_buffer = [g_queue commandBuffer];
    }
    
    MTLRenderPassDescriptor* desc = [[MTLRenderPassDescriptor alloc] init];
    
    bool has_target = false;
    for (int i = 0; i < 8; i++) {
        if (g_frame.render_targets[i]) {
            auto* attachment = desc.colorAttachments[i];
            attachment.texture = g_frame.render_targets[i];
            attachment.loadAction = MTLLoadActionLoad;
            attachment.storeAction = MTLStoreActionStore;
            has_target = true;
        }
    }
    
    if (!has_target && g_swapchain && g_swapchain->back_buffer) {
        auto* attachment = desc.colorAttachments[0];
        attachment.texture = g_swapchain->back_buffer;
        attachment.loadAction = MTLLoadActionClear;
        attachment.clearColor = MTLClearColorMake(0, 0, 0, 1);
        attachment.storeAction = MTLStoreActionStore;
    }
    
    if (g_frame.depth_target) {
        desc.depthAttachment.texture = g_frame.depth_target;
        desc.depthAttachment.loadAction = MTLLoadActionClear;
        desc.depthAttachment.clearDepth = 1.0;
        desc.depthAttachment.storeAction = MTLStoreActionStore;
    }
    
    g_frame.encoder = [g_frame.command_buffer renderCommandEncoderWithDescriptor:desc];
    g_frame.in_render_pass = true;
    
    if (g_frame.pipeline) {
        [g_frame.encoder setRenderPipelineState:g_frame.pipeline];
    }
    if (g_frame.has_viewport) {
        MTLViewport vp = { g_frame.viewport_x, g_frame.viewport_y,
                           g_frame.viewport_w, g_frame.viewport_h,
                           g_frame.viewport_min, g_frame.viewport_max };
        [g_frame.encoder setViewport:vp];
    }
}

static NTSTATUS ms_om_set_render_targets(void* p) {
    auto* params = (struct ms_set_render_targets_params*)p;
    if (g_frame.in_render_pass) {
        [g_frame.encoder endEncoding];
        g_frame.in_render_pass = false;
        g_frame.encoder = nil;
    }
    
    memset(g_frame.render_targets, 0, sizeof(g_frame.render_targets));
    for (uint32_t i = 0; i < params->num_views && i < 8; i++) {
        if (params->rtv_handles[i]) {
            auto it = g_rtvs.find(params->rtv_handles[i]);
            if (it != g_rtvs.end()) {
                g_frame.render_targets[i] = it->second.texture;
            }
        }
    }
    g_frame.depth_target = nil;
    if (params->dsv_handle) {
        auto it = g_rtvs.find(params->dsv_handle);
        if (it != g_rtvs.end()) {
            g_frame.depth_target = it->second.texture;
        }
    }
    return 0;
}

static NTSTATUS ms_clear_render_target_view(void* p) {
    auto* params = (struct ms_clear_params*)p;
    auto it = g_rtvs.find(params->view_handle);
    if (it == g_rtvs.end()) return -1;
    
    if (g_frame.in_render_pass) {
        [g_frame.encoder endEncoding];
        g_frame.in_render_pass = false;
    }
    if (!g_frame.command_buffer) {
        g_frame.command_buffer = [g_queue commandBuffer];
    }
    
    MTLRenderPassDescriptor* desc = [[MTLRenderPassDescriptor alloc] init];
    auto* attachment = desc.colorAttachments[0];
    attachment.texture = it->second.texture;
    attachment.loadAction = MTLLoadActionClear;
    attachment.clearColor = MTLClearColorMake(params->r, params->g, params->b, params->a);
    attachment.storeAction = MTLStoreActionStore;
    
    id<MTLRenderCommandEncoder> enc = [g_frame.command_buffer renderCommandEncoderWithDescriptor:desc];
    [enc endEncoding];
    return 0;
}

static NTSTATUS ms_set_viewports(void* p) {
    auto* vp = (struct ms_viewport*)p;
    g_frame.viewport_x = vp->top_left_x;
    g_frame.viewport_y = vp->top_left_y;
    g_frame.viewport_w = vp->width;
    g_frame.viewport_h = vp->height;
    g_frame.viewport_min = vp->min_depth;
    g_frame.viewport_max = vp->max_depth;
    g_frame.has_viewport = true;
    
    if (g_frame.encoder) {
        MTLViewport mtlvp = { vp->top_left_x, vp->top_left_y, vp->width, vp->height, vp->min_depth, vp->max_depth };
        [g_frame.encoder setViewport:mtlvp];
    }
    return 0;
}

static NTSTATUS ms_ia_set_vertex_buffers(void* p) {
    auto* params = (struct ms_set_vertex_buffers_params*)p;
    for (uint32_t i = 0; i < params->num_buffers && (params->start_slot + i) < 16; i++) {
        uint32_t slot = params->start_slot + i;
        if (params->buffer_handles[i]) {
            auto it = g_buffers.find(params->buffer_handles[i]);
            if (it != g_buffers.end()) {
                g_frame.vertex_buffers[slot] = it->second.buffer;
                g_frame.vertex_strides[slot] = params->strides[i];
                g_frame.vertex_offsets[slot] = params->offsets[i];
                
                if (g_frame.encoder) {
                    [g_frame.encoder setVertexBuffer:it->second.buffer
                                             offset:params->offsets[i]
                                            atIndex:slot];
                }
            }
        }
    }
    return 0;
}

static NTSTATUS ms_ia_set_index_buffer(void* p) {
    auto* params = (struct ms_set_index_buffer_params*)p;
    if (params->buffer_handle) {
        auto it = g_buffers.find(params->buffer_handle);
        if (it != g_buffers.end()) {
            g_frame.index_buffer = it->second.buffer;
            g_frame.index_format = params->format;
            g_frame.index_offset = params->offset;
        }
    }
    return 0;
}

static NTSTATUS ms_ia_set_primitive_topology(void* p) {
    g_frame.primitive_topology = *(uint32_t*)p;
    return 0;
}

static NTSTATUS ms_vs_set_constant_buffers(void* p) {
    auto* params = (struct ms_set_constant_buffers_params*)p;
    for (uint32_t i = 0; i < params->num_buffers && (params->start_slot + i) < 14; i++) {
        uint32_t slot = params->start_slot + i;
        if (params->buffer_handles[i]) {
            auto it = g_buffers.find(params->buffer_handles[i]);
            if (it != g_buffers.end()) {
                g_frame.vs_constants[slot] = it->second.buffer;
                if (g_frame.encoder) {
                    [g_frame.encoder setVertexBuffer:it->second.buffer offset:0 atIndex:slot + 1];
                }
            }
        }
    }
    return 0;
}

static NTSTATUS ms_ps_set_constant_buffers(void* p) {
    auto* params = (struct ms_set_constant_buffers_params*)p;
    for (uint32_t i = 0; i < params->num_buffers && (params->start_slot + i) < 14; i++) {
        uint32_t slot = params->start_slot + i;
        if (params->buffer_handles[i]) {
            auto it = g_buffers.find(params->buffer_handles[i]);
            if (it != g_buffers.end()) {
                g_frame.ps_constants[slot] = it->second.buffer;
                if (g_frame.encoder) {
                    [g_frame.encoder setFragmentBuffer:it->second.buffer offset:0 atIndex:slot + 1];
                }
            }
        }
    }
    return 0;
}

static NTSTATUS ms_draw_indexed(void* p) {
    auto* params = (struct ms_draw_indexed_params*)p;
    ensure_render_pass();
    if (!g_frame.encoder) return -1;
    
    if (g_frame.index_buffer) {
        MTLIndexType indexType = (g_frame.index_format == 1) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        [g_frame.encoder drawIndexedPrimitives:to_mtl_primitive(g_frame.primitive_topology)
                                    indexCount:params->index_count
                                     indexType:indexType
                                   indexBuffer:g_frame.index_buffer
                             indexBufferOffset:g_frame.index_offset + params->start_index * (g_frame.index_format == 1 ? 2 : 4)];
    }
    return 0;
}

static NTSTATUS ms_draw(void* p) {
    auto* params = (struct ms_draw_params*)p;
    ensure_render_pass();
    if (!g_frame.encoder) return -1;
    
    [g_frame.encoder drawPrimitives:to_mtl_primitive(g_frame.primitive_topology)
                        vertexStart:params->start_vertex
                        vertexCount:params->vertex_count];
    return 0;
}

static NTSTATUS ms_update_subresource(void* p) {
    auto* params = (struct ms_update_subresource_params*)p;
    auto it = g_buffers.find(params->resource_handle);
    if (it != g_buffers.end() && it->second.buffer && params->data) {
        void* contents = [it->second.buffer contents];
        if (contents) {
            memcpy((uint8_t*)contents + params->offset, params->data, params->data_size);
        }
    } else {
        auto tit = g_textures.find(params->resource_handle);
        if (tit != g_textures.end() && tit->second.texture && params->data) {
            MTLRegion region = MTLRegionMake2D(0, 0, tit->second.width, tit->second.height);
            [tit->second.texture replaceRegion:region
                                   mipmapLevel:0
                                     withBytes:params->data
                                   bytesPerRow:params->row_pitch > 0 ? params->row_pitch : tit->second.width * 4];
        }
    }
    return 0;
}

static NTSTATUS ms_map(void* p) {
    auto* params = (struct ms_map_params*)p;
    auto it = g_buffers.find(params->resource_handle);
    if (it != g_buffers.end() && it->second.buffer) {
        params->out_data = [it->second.buffer contents];
        params->out_row_pitch = (uint32_t)it->second.size;
        return 0;
    }
    params->out_data = nullptr;
    return -1;
}

static NTSTATUS ms_unmap(void* p) {
    return 0;
}

static NTSTATUS ms_flush(void*) {
    if (g_frame.in_render_pass && g_frame.encoder) {
        [g_frame.encoder endEncoding];
        g_frame.in_render_pass = false;
        g_frame.encoder = nil;
    }
    if (g_frame.command_buffer) {
        [g_frame.command_buffer commit];
        g_frame.command_buffer = nil;
    }
    return 0;
}

static NTSTATUS ms_resize_buffers(void* p) {
    auto* params = (struct ms_resize_params*)p;
    if (g_swapchain) {
        g_swapchain->width = params->width;
        g_swapchain->height = params->height;
        g_swapchain->back_buffer = nil;
        if (g_swapchain->layer) {
            g_swapchain->layer.drawableSize = CGSizeMake(params->width, params->height);
        }
    }
    return 0;
}

static NTSTATUS ms_get_buffer(void* p) {
    auto* params = (struct ms_get_buffer_params*)p;
    if (g_swapchain && !g_swapchain->back_buffer) {
        @autoreleasepool {
            MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
            desc.textureType = MTLTextureType2D;
            desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
            desc.width = g_swapchain->width;
            desc.height = g_swapchain->height;
            desc.mipmapLevelCount = 1;
            desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;
            g_swapchain->back_buffer = [g_device newTextureWithDescriptor:desc];
        }
    }
    
    uint64_t handle = new_handle();
    auto& tex = g_textures[handle];
    if (g_swapchain && g_swapchain->back_buffer) {
        tex.texture = g_swapchain->back_buffer;
        tex.width = g_swapchain->width;
        tex.height = g_swapchain->height;
        tex.format = 28;
        tex.mip_levels = 1;
    }
    params->out_handle = handle;
    return 0;
}

static NTSTATUS ms_shutdown(void*) {
    if (g_frame.in_render_pass && g_frame.encoder) {
        [g_frame.encoder endEncoding];
    }
    g_buffers.clear();
    g_textures.clear();
    g_shaders.clear();
    g_samplers.clear();
    g_rtvs.clear();
    g_pipelines.clear();
    if (g_swapchain) {
        if (g_swapchain->window) [g_swapchain->window close];
        delete g_swapchain;
        g_swapchain = nullptr;
    }
    g_device = nil;
    g_queue = nil;
    return 0;
}

static NTSTATUS ms_stub(void*) { return 0; }

extern "C" {

typedef NTSTATUS (*unixlib_entry_t)(void*);

__attribute__((used, visibility("default")))
unixlib_entry_t __wine_unix_call_funcs[] = {
    ms_init,
    ms_create_device,
    ms_create_swap_chain,
    ms_create_buffer,
    ms_create_texture_2d,
    ms_create_vertex_shader,
    ms_create_pixel_shader,
    ms_create_render_target_view,
    ms_create_depth_stencil_view,
    ms_create_input_layout,
    ms_create_blend_state,
    ms_create_rasterizer_state,
    ms_create_depth_stencil_state,
    ms_create_sampler_state,
    ms_draw_indexed,
    ms_draw,
    ms_map,
    ms_unmap,
    ms_present,
    ms_resize_buffers,
    ms_get_buffer,
    ms_stub,
    ms_set_viewports,
    ms_stub,
    ms_clear_render_target_view,
    ms_stub,
    ms_om_set_render_targets,
    ms_ia_set_vertex_buffers,
    ms_ia_set_index_buffer,
    ms_ia_set_primitive_topology,
    ms_stub,
    ms_stub,
    ms_vs_set_constant_buffers,
    ms_ps_set_constant_buffers,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_create_shader_resource_view,
    ms_update_subresource,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_flush,
    ms_stub,
    ms_stub,
};

__attribute__((used, visibility("default")))
unixlib_entry_t __wine_unix_call_wow64_funcs[] = {
    ms_init,
    ms_create_device,
    ms_create_swap_chain,
    ms_create_buffer,
    ms_create_texture_2d,
    ms_create_vertex_shader,
    ms_create_pixel_shader,
    ms_create_render_target_view,
    ms_create_depth_stencil_view,
    ms_create_input_layout,
    ms_create_blend_state,
    ms_create_rasterizer_state,
    ms_create_depth_stencil_state,
    ms_create_sampler_state,
    ms_draw_indexed,
    ms_draw,
    ms_map,
    ms_unmap,
    ms_present,
    ms_resize_buffers,
    ms_get_buffer,
    ms_stub,
    ms_set_viewports,
    ms_stub,
    ms_clear_render_target_view,
    ms_stub,
    ms_om_set_render_targets,
    ms_ia_set_vertex_buffers,
    ms_ia_set_index_buffer,
    ms_ia_set_primitive_topology,
    ms_stub,
    ms_stub,
    ms_vs_set_constant_buffers,
    ms_ps_set_constant_buffers,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_create_shader_resource_view,
    ms_update_subresource,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_stub,
    ms_flush,
    ms_stub,
    ms_stub,
};

NTSTATUS __attribute__((visibility("default"))) __wine_unix_lib_init(void) { return 0; }

}
