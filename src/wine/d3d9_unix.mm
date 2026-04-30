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

extern "C" {
#include "mojoshader/mojoshader.h"
}

typedef long NTSTATUS;

#include "d3d9_unix.h"

static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_queue = nil;

static id<MTLFunction> mojo_translate_to_metal(const uint8_t* bytecode, uint32_t size, const char* stage) {
    char func_name[64];
    snprintf(func_name, sizeof(func_name), "%s_main", stage);

    const MOJOSHADER_parseData* pd = MOJOSHADER_parse(
        MOJOSHADER_PROFILE_METAL,
        func_name,
        bytecode, size,
        NULL, 0, NULL, 0,
        NULL, NULL, NULL
    );

    if (!pd || pd->error_count > 0) {
        if (pd && pd->errors) {
            fprintf(stderr, "[mojo] %s error: %s\n", stage, pd->errors[0].error);
        } else {
            fprintf(stderr, "[mojo] %s parse returned NULL\n", stage);
        }
        if (pd) MOJOSHADER_freeParseData(pd);
        return nil;
    }

    if (!pd->output || pd->output_len == 0) {
        fprintf(stderr, "[mojo] %s no output\n", stage);
        MOJOSHADER_freeParseData(pd);
        return nil;
    }

    fprintf(stderr, "[mojo] %s translated %u bytes bytecode -> %d bytes MSL\n", stage, size, pd->output_len);

    @autoreleasepool {
        NSString* source = [[NSString alloc] initWithBytes:pd->output length:pd->output_len encoding:NSUTF8StringEncoding];
        NSError* error = nil;
        id<MTLLibrary> library = [g_device newLibraryWithSource:source options:nil error:&error];
        if (error || !library) {
            fprintf(stderr, "[mojo] %s Metal compile error: %s\n", stage,
                error ? [[error localizedDescription] UTF8String] : "unknown");
            MOJOSHADER_freeParseData(pd);
            return nil;
        }

        NSString* funcName = pd->mainfn ? [NSString stringWithUTF8String:pd->mainfn] : [NSString stringWithUTF8String:func_name];
        id<MTLFunction> func = [library newFunctionWithName:funcName];
        if (!func) {
            func = [library newFunctionWithName:@"main"];
        }
        MOJOSHADER_freeParseData(pd);
        return func;
    }
}

static std::mutex g_mutex;
static uint64_t g_next_handle = 0x2000;
static uint64_t new_handle() { return ++g_next_handle; }

struct D3D9Buffer {
    id<MTLBuffer> buffer;
    size_t size;
};

struct D3D9Texture {
    id<MTLTexture> texture;
    uint32_t width;
    uint32_t height;
    uint32_t format;
};

struct D3D9Shader {
    std::vector<uint8_t> bytecode;
    id<MTLFunction> function;
    id<MTLLibrary> library;
};

struct D3D9VertexDecl {
    struct d3d9_vertex_element elements[64];
    uint32_t num_elements;
    id<MTLRenderPipelineState> cached_pipeline;
};

struct D3D9SwapChain {
    CAMetalLayer* layer;
    NSWindow* window;
    id<CAMetalDrawable> current_drawable;
    id<MTLTexture> back_buffer;
    uint32_t width;
    uint32_t height;
};

struct D3D9State {
    uint64_t vertex_decl_handle;
    uint64_t vertex_buffer_handles[16];
    uint32_t vertex_strides[16];
    uint32_t vertex_offsets[16];
    uint64_t index_buffer_handle;
    uint64_t vertex_shader_handle;
    uint64_t pixel_shader_handle;
    uint64_t texture_handles[16];
    uint64_t render_targets[4];
    uint64_t depth_stencil;
    uint32_t render_states[256];
    uint32_t sampler_states[16][16];
    bool in_scene;
    bool in_render_pass;
    uint32_t clear_flags;
    uint32_t clear_color;
    float clear_z;
    uint32_t clear_stencil;
    uint32_t viewport_x, viewport_y, viewport_w, viewport_h;
    float viewport_min_z, viewport_max_z;
    bool has_viewport;
    bool needs_pipeline_update;
    id<MTLRenderPipelineState> current_pipeline;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
};

static D3D9SwapChain* g_swapchain = nullptr;
static D3D9State g_state;
static std::unordered_map<uint64_t, D3D9Buffer> g_buffers;
static std::unordered_map<uint64_t, D3D9Texture> g_textures;
static std::unordered_map<uint64_t, D3D9Shader> g_shaders;
static std::unordered_map<uint64_t, D3D9VertexDecl> g_vertex_decls;

static MTLPixelFormat d3dformat_to_mtl(uint32_t fmt) {
    switch (fmt) {
        case 21: return MTLPixelFormatBGRA8Unorm;
        case 22: return MTLPixelFormatBGRA8Unorm;
        case 23: return MTLPixelFormatB5G6R5Unorm;
        case 28: return MTLPixelFormatA8Unorm;
        case 75: return MTLPixelFormatDepth24Unorm_Stencil8;
        case 80: return MTLPixelFormatDepth16Unorm;
        case 82: return MTLPixelFormatDepth32Float;
        case 83: return MTLPixelFormatDepth32Float_Stencil8;
        case 111: return MTLPixelFormatR16Float;
        case 112: return MTLPixelFormatRG16Float;
        case 113: return MTLPixelFormatRGBA16Float;
        case 114: return MTLPixelFormatR32Float;
        case 115: return MTLPixelFormatRG32Float;
        case 116: return MTLPixelFormatRGBA32Float;
        case 50: return MTLPixelFormatA8Unorm;
        case 51: return MTLPixelFormatRG8Unorm;
        default: return MTLPixelFormatBGRA8Unorm;
    }
}

static MTLVertexFormat d3ddecltype_to_mtl(uint8_t type) {
    switch (type) {
        case 0: return MTLVertexFormatFloat;
        case 1: return MTLVertexFormatFloat2;
        case 2: return MTLVertexFormatFloat3;
        case 3: return MTLVertexFormatFloat4;
        case 4: return MTLVertexFormatUChar4Normalized;
        case 5: return MTLVertexFormatUChar4;
        case 6: return MTLVertexFormatShort2;
        case 7: return MTLVertexFormatShort4;
        case 8: return MTLVertexFormatUChar4Normalized;
        case 9: return MTLVertexFormatShort2Normalized;
        case 10: return MTLVertexFormatShort4Normalized;
        case 11: return MTLVertexFormatUShort2Normalized;
        case 12: return MTLVertexFormatUShort4Normalized;
        case 15: return MTLVertexFormatHalf2;
        case 16: return MTLVertexFormatHalf4;
        default: return MTLVertexFormatFloat4;
    }
}

static uint32_t d3ddecltype_size(uint8_t type) {
    switch (type) {
        case 0: return 4;
        case 1: return 8;
        case 2: return 12;
        case 3: return 16;
        case 4: return 4;
        case 5: return 4;
        case 6: return 4;
        case 7: return 8;
        case 8: return 4;
        case 9: return 4;
        case 10: return 8;
        case 11: return 4;
        case 12: return 8;
        case 15: return 4;
        case 16: return 8;
        default: return 16;
    }
}

static MTLPrimitiveType d3dprim_to_mtl(uint32_t prim) {
    switch (prim) {
        case 1: return MTLPrimitiveTypePoint;
        case 2: return MTLPrimitiveTypeLine;
        case 3: return MTLPrimitiveTypeLineStrip;
        case 4: return MTLPrimitiveTypeTriangle;
        case 5: return MTLPrimitiveTypeTriangleStrip;
        default: return MTLPrimitiveTypeTriangle;
    }
}

static uint32_t prim_vertex_count(uint32_t prim_type, uint32_t prim_count) {
    switch (prim_type) {
        case 1: return prim_count;
        case 2: return prim_count * 2;
        case 3: return prim_count + 1;
        case 4: return prim_count * 3;
        case 5: return prim_count + 2;
        default: return prim_count * 3;
    }
}

static NTSTATUS d3d9_init(void*) {
    if (g_device) return 0;
    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) return -1;
    g_queue = [g_device newCommandQueue];
    memset((void*)&g_state, 0, sizeof(g_state));
    fprintf(stderr, "[d3d9] Metal device: %s\n", [[g_device name] UTF8String]);
    return 0;
}

static NTSTATUS d3d9_create_device(void* p) {
    auto* params = (struct d3d9_create_device_params*)p;
    if (!g_device) d3d9_init(nullptr);

    if (g_swapchain) {
        if (g_swapchain->window) [g_swapchain->window close];
        delete g_swapchain;
    }
    g_swapchain = new D3D9SwapChain();
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
        [g_swapchain->window setTitle:@"MetalSharp D3D9"];
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

    fprintf(stderr, "[d3d9] Device created: %ux%u windowed=%d\n",
        g_swapchain->width, g_swapchain->height, params->windowed);
    return 0;
}

static NTSTATUS d3d9_create_vertex_buffer(void* p) {
    auto* params = (struct d3d9_create_buffer_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& buf = g_buffers[handle];
    buf.size = params->length;

    buf.buffer = [g_device newBufferWithLength:params->length
                                       options:MTLResourceStorageModeShared];

    if (!buf.buffer) {
        g_buffers.erase(handle);
        return -1;
    }

    params->out_handle = handle;
    fprintf(stderr, "[d3d9] Vertex buffer created: %u bytes handle=%llu\n", params->length, (unsigned long long)handle);
    return 0;
}

static NTSTATUS d3d9_create_index_buffer(void* p) {
    auto* params = (struct d3d9_create_buffer_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& buf = g_buffers[handle];
    buf.size = params->length;

    buf.buffer = [g_device newBufferWithLength:params->length
                                       options:MTLResourceStorageModeShared];

    if (!buf.buffer) {
        g_buffers.erase(handle);
        return -1;
    }

    params->out_handle = handle;
    fprintf(stderr, "[d3d9] Index buffer created: %u bytes handle=%llu\n", params->length, (unsigned long long)handle);
    return 0;
}

static NTSTATUS d3d9_create_texture(void* p) {
    auto* params = (struct d3d9_create_texture_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& tex = g_textures[handle];
    tex.width = params->width;
    tex.height = params->height;
    tex.format = params->format;

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2D;
    desc.pixelFormat = d3dformat_to_mtl(params->format);
    desc.width = params->width;
    desc.height = params->height;
    desc.mipmapLevelCount = params->levels > 0 ? params->levels : 1;
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;

    tex.texture = [g_device newTextureWithDescriptor:desc];
    if (!tex.texture) {
        g_textures.erase(handle);
        return -1;
    }

    params->out_handle = handle;
    return 0;
}

static NTSTATUS d3d9_create_render_target(void* p) {
    auto* params = (struct d3d9_create_surface_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& tex = g_textures[handle];
    tex.width = params->width;
    tex.height = params->height;
    tex.format = params->format;

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2D;
    desc.pixelFormat = d3dformat_to_mtl(params->format);
    desc.width = params->width;
    desc.height = params->height;
    desc.mipmapLevelCount = 1;
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;

    tex.texture = [g_device newTextureWithDescriptor:desc];
    if (!tex.texture) {
        g_textures.erase(handle);
        return -1;
    }

    params->out_handle = handle;
    return 0;
}

static NTSTATUS d3d9_create_depth_stencil(void* p) {
    auto* params = (struct d3d9_create_surface_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& tex = g_textures[handle];
    tex.width = params->width;
    tex.height = params->height;
    tex.format = params->format;

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2D;
    desc.pixelFormat = d3dformat_to_mtl(params->format);
    desc.width = params->width;
    desc.height = params->height;
    desc.mipmapLevelCount = 1;
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;

    tex.texture = [g_device newTextureWithDescriptor:desc];
    if (!tex.texture) {
        g_textures.erase(handle);
        return -1;
    }

    params->out_handle = handle;
    return 0;
}

static NTSTATUS d3d9_create_vertex_shader(void* p) {
    auto* params = (struct d3d9_create_shader_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& shader = g_shaders[handle];
    shader.function = nil;
    shader.library = nil;

    if (params->bytecode && params->bytecode_size > 0) {
        const uint8_t* ptr = (const uint8_t*)params->bytecode;
        if (params->bytecode_size > 5 && ptr[0] == 'M' && ptr[1] == 'S' && ptr[2] == 'L' && ptr[3] == '\0') {
            const char* func_name = (const char*)(ptr + 4);
            size_t remaining = params->bytecode_size - 4;
            size_t name_len = strnlen(func_name, remaining);
            const char* source = func_name + name_len + 1;
            size_t source_len = remaining - name_len - 1;

            @autoreleasepool {
                NSString* nsSource = [[NSString alloc] initWithBytes:source length:source_len encoding:NSUTF8StringEncoding];
                NSError* error = nil;
                id<MTLLibrary> library = [g_device newLibraryWithSource:nsSource options:nil error:&error];
                if (error || !library) {
                    fprintf(stderr, "[d3d9] VS compile error: %s\n",
                        error ? [[error localizedDescription] UTF8String] : "unknown");
                } else {
                    NSString* nsFunc = [NSString stringWithUTF8String:func_name];
                    shader.function = [library newFunctionWithName:nsFunc];
                    shader.library = library;
                    fprintf(stderr, "[d3d9] VS compiled: %s\n", func_name);
                }
            }
        } else {
            shader.function = mojo_translate_to_metal(ptr, params->bytecode_size, "VS");
            if (!shader.function) {
                shader.bytecode.assign(ptr, ptr + params->bytecode_size);
                fprintf(stderr, "[d3d9] VS stored raw bytecode: %u bytes (MojoShader failed)\n", params->bytecode_size);
            }
        }
    }

    params->out_handle = handle;
    return 0;
}

static NTSTATUS d3d9_create_pixel_shader(void* p) {
    auto* params = (struct d3d9_create_shader_params*)p;
    if (!g_device) d3d9_init(nullptr);

    uint64_t handle = new_handle();
    auto& shader = g_shaders[handle];
    shader.function = nil;
    shader.library = nil;

    if (params->bytecode && params->bytecode_size > 0) {
        const uint8_t* ptr = (const uint8_t*)params->bytecode;
        if (params->bytecode_size > 5 && ptr[0] == 'M' && ptr[1] == 'S' && ptr[2] == 'L' && ptr[3] == '\0') {
            const char* func_name = (const char*)(ptr + 4);
            size_t remaining = params->bytecode_size - 4;
            size_t name_len = strnlen(func_name, remaining);
            const char* source = func_name + name_len + 1;
            size_t source_len = remaining - name_len - 1;

            @autoreleasepool {
                NSString* nsSource = [[NSString alloc] initWithBytes:source length:source_len encoding:NSUTF8StringEncoding];
                NSError* error = nil;
                id<MTLLibrary> library = [g_device newLibraryWithSource:nsSource options:nil error:&error];
                if (error || !library) {
                    fprintf(stderr, "[d3d9] PS compile error: %s\n",
                        error ? [[error localizedDescription] UTF8String] : "unknown");
                } else {
                    NSString* nsFunc = [NSString stringWithUTF8String:func_name];
                    shader.function = [library newFunctionWithName:nsFunc];
                    shader.library = library;
                    fprintf(stderr, "[d3d9] PS compiled: %s\n", func_name);
                }
            }
        } else {
            shader.function = mojo_translate_to_metal(ptr, params->bytecode_size, "PS");
            if (!shader.function) {
                shader.bytecode.assign(ptr, ptr + params->bytecode_size);
                fprintf(stderr, "[d3d9] PS stored raw bytecode: %u bytes (MojoShader failed)\n", params->bytecode_size);
            }
        }
    }

    params->out_handle = handle;
    return 0;
}

static NTSTATUS d3d9_create_vertex_declaration(void* p) {
    auto* params = (struct d3d9_create_vertex_decl_params*)p;

    uint64_t handle = new_handle();
    auto& decl = g_vertex_decls[handle];
    decl.num_elements = params->num_elements;
    memset(decl.elements, 0, sizeof(decl.elements));
    decl.cached_pipeline = nil;

    if (params->elements && params->num_elements > 0) {
        memcpy(decl.elements, params->elements, params->num_elements * sizeof(struct d3d9_vertex_element));
    }

    params->out_handle = handle;
    fprintf(stderr, "[d3d9] Vertex declaration created: %u elements handle=%llu\n",
        params->num_elements, (unsigned long long)handle);
    return 0;
}

static NTSTATUS d3d9_set_vertex_declaration(void* p) {
    g_state.vertex_decl_handle = *(uint64_t*)p;
    g_state.needs_pipeline_update = true;
    return 0;
}

static NTSTATUS d3d9_set_stream_source(void* p) {
    auto* params = (struct d3d9_set_stream_source_params*)p;
    if (params->stream_number < 16) {
        g_state.vertex_buffer_handles[params->stream_number] = params->buffer_handle;
        g_state.vertex_strides[params->stream_number] = params->stride;
        g_state.vertex_offsets[params->stream_number] = params->offset;
    }
    return 0;
}

static NTSTATUS d3d9_set_indices(void* p) {
    g_state.index_buffer_handle = *(uint64_t*)p;
    return 0;
}

static NTSTATUS d3d9_set_vertex_shader(void* p) {
    uint64_t h = *(uint64_t*)p;
    if (g_state.vertex_shader_handle != h) {
        g_state.vertex_shader_handle = h;
        g_state.needs_pipeline_update = true;
    }
    return 0;
}

static NTSTATUS d3d9_set_pixel_shader(void* p) {
    uint64_t h = *(uint64_t*)p;
    if (g_state.pixel_shader_handle != h) {
        g_state.pixel_shader_handle = h;
        g_state.needs_pipeline_update = true;
    }
    return 0;
}

static NTSTATUS d3d9_set_vs_constants_f(void* p) {
    auto* params = (struct d3d9_set_constants_f_params*)p;
    if (!g_state.command_buffer || !g_state.in_render_pass) return 0;
    uint32_t byte_count = params->count * sizeof(float);
    if (byte_count == 0) return 0;

    id<MTLBuffer> const_buf = [g_device newBufferWithBytes:params->values
                                                     length:byte_count
                                                    options:MTLResourceStorageModeShared];
    if (const_buf && g_state.encoder) {
        uint32_t base_idx = params->start_register + 20;
        [g_state.encoder setVertexBuffer:const_buf offset:0 atIndex:base_idx];
    }
    return 0;
}

static NTSTATUS d3d9_set_ps_constants_f(void* p) {
    auto* params = (struct d3d9_set_constants_f_params*)p;
    if (!g_state.command_buffer || !g_state.in_render_pass) return 0;
    uint32_t byte_count = params->count * sizeof(float);
    if (byte_count == 0) return 0;

    id<MTLBuffer> const_buf = [g_device newBufferWithBytes:params->values
                                                     length:byte_count
                                                    options:MTLResourceStorageModeShared];
    if (const_buf && g_state.encoder) {
        uint32_t base_idx = params->start_register + 20;
        [g_state.encoder setFragmentBuffer:const_buf offset:0 atIndex:base_idx];
    }
    return 0;
}

static NTSTATUS d3d9_set_texture(void* p) {
    auto* params = (struct d3d9_set_texture_params*)p;
    if (params->stage < 16) {
        g_state.texture_handles[params->stage] = params->texture_handle;
        if (g_state.encoder && params->texture_handle) {
            auto it = g_textures.find(params->texture_handle);
            if (it != g_textures.end() && it->second.texture) {
                [g_state.encoder setFragmentTexture:it->second.texture atIndex:params->stage];
            }
        }
    }
    return 0;
}

static NTSTATUS d3d9_set_sampler_state(void* p) {
    auto* params = (struct d3d9_set_sampler_state_params*)p;
    if (params->sampler < 16 && params->type < 16) {
        g_state.sampler_states[params->sampler][params->type] = params->value;
    }
    return 0;
}

static NTSTATUS d3d9_set_render_state(void* p) {
    auto* params = (struct d3d9_set_render_state_params*)p;
    if (params->state < 256) {
        g_state.render_states[params->state] = params->value;
    }
    return 0;
}

static void ensure_render_pass() {
    if (g_state.in_render_pass) return;
    if (!g_state.command_buffer) {
        g_state.command_buffer = [g_queue commandBuffer];
    }

    MTLRenderPassDescriptor* desc = [[MTLRenderPassDescriptor alloc] init];
    bool has_target = false;

    for (int i = 0; i < 4; i++) {
        if (g_state.render_targets[i]) {
            auto it = g_textures.find(g_state.render_targets[i]);
            if (it != g_textures.end() && it->second.texture) {
                auto* attachment = desc.colorAttachments[i];
                attachment.texture = it->second.texture;
                attachment.loadAction = MTLLoadActionLoad;
                attachment.storeAction = MTLStoreActionStore;
                has_target = true;
            }
        }
    }

    if (!has_target && g_swapchain && g_swapchain->back_buffer) {
        auto* attachment = desc.colorAttachments[0];
        attachment.texture = g_swapchain->back_buffer;
        attachment.loadAction = MTLLoadActionClear;
        attachment.clearColor = MTLClearColorMake(0, 0, 0, 1);
        attachment.storeAction = MTLStoreActionStore;
    }

    if (g_state.depth_stencil) {
        auto it = g_textures.find(g_state.depth_stencil);
        if (it != g_textures.end() && it->second.texture) {
            desc.depthAttachment.texture = it->second.texture;
            desc.depthAttachment.loadAction = MTLLoadActionClear;
            desc.depthAttachment.clearDepth = 1.0;
            desc.depthAttachment.storeAction = MTLStoreActionStore;
        }
    }

    g_state.encoder = [g_state.command_buffer renderCommandEncoderWithDescriptor:desc];
    g_state.in_render_pass = true;

    if (g_state.needs_pipeline_update && g_state.vertex_shader_handle && g_state.pixel_shader_handle) {
        auto vs_it = g_shaders.find(g_state.vertex_shader_handle);
        auto ps_it = g_shaders.find(g_state.pixel_shader_handle);
        if (vs_it != g_shaders.end() && ps_it != g_shaders.end() &&
            vs_it->second.function && ps_it->second.function) {

            @autoreleasepool {
                MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
                pdesc.vertexFunction = vs_it->second.function;
                pdesc.fragmentFunction = ps_it->second.function;

                MTLPixelFormat rt_format = MTLPixelFormatBGRA8Unorm;
                for (int i = 0; i < 4; i++) {
                    if (g_state.render_targets[i]) {
                        auto rt_it = g_textures.find(g_state.render_targets[i]);
                        if (rt_it != g_textures.end()) {
                            rt_format = rt_it->second.texture.pixelFormat;
                            pdesc.colorAttachments[i].pixelFormat = rt_format;
                            break;
                        }
                    }
                }
                if (g_swapchain && g_swapchain->back_buffer && !g_state.render_targets[0]) {
                    pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                }

                NSError* error = nil;
                g_state.current_pipeline = [g_device newRenderPipelineStateWithDescriptor:pdesc error:&error];
                if (error || !g_state.current_pipeline) {
                    fprintf(stderr, "[d3d9] Pipeline error: %s\n",
                        error ? [[error localizedDescription] UTF8String] : "unknown");
                }
                g_state.needs_pipeline_update = false;
            }
        }
    }

    if (g_state.current_pipeline) {
        [g_state.encoder setRenderPipelineState:g_state.current_pipeline];
    }

    if (g_state.has_viewport) {
        MTLViewport vp = { (double)g_state.viewport_x, (double)g_state.viewport_y,
                           (double)g_state.viewport_w, (double)g_state.viewport_h,
                           (double)g_state.viewport_min_z, (double)g_state.viewport_max_z };
        [g_state.encoder setViewport:vp];
    }

    for (int i = 0; i < 16; i++) {
        if (g_state.vertex_buffer_handles[i]) {
            auto it = g_buffers.find(g_state.vertex_buffer_handles[i]);
            if (it != g_buffers.end() && it->second.buffer) {
                [g_state.encoder setVertexBuffer:it->second.buffer
                                          offset:g_state.vertex_offsets[i]
                                         atIndex:i];
            }
        }
    }
}

static NTSTATUS d3d9_draw_primitive(void* p) {
    auto* params = (struct d3d9_draw_params*)p;
    ensure_render_pass();
    if (!g_state.encoder) return -1;

    uint32_t vertex_count = prim_vertex_count(params->primitive_type, params->primitive_count);
    [g_state.encoder drawPrimitives:d3dprim_to_mtl(params->primitive_type)
                        vertexStart:params->start_vertex
                        vertexCount:vertex_count];
    return 0;
}

static NTSTATUS d3d9_draw_indexed_primitive(void* p) {
    auto* params = (struct d3d9_draw_indexed_params*)p;
    ensure_render_pass();
    if (!g_state.encoder) return -1;

    if (g_state.index_buffer_handle) {
        auto it = g_buffers.find(g_state.index_buffer_handle);
        if (it != g_buffers.end() && it->second.buffer) {
            uint32_t index_count = prim_vertex_count(params->primitive_type, params->primitive_count);
            [g_state.encoder drawIndexedPrimitives:d3dprim_to_mtl(params->primitive_type)
                                        indexCount:index_count
                                         indexType:MTLIndexTypeUInt16
                                       indexBuffer:it->second.buffer
                                 indexBufferOffset:params->start_index * 2];
        }
    }
    return 0;
}

static NTSTATUS d3d9_clear(void* p) {
    auto* params = (struct d3d9_clear_params*)p;

    if (g_state.in_render_pass) {
        [g_state.encoder endEncoding];
        g_state.in_render_pass = false;
        g_state.encoder = nil;
    }
    if (!g_state.command_buffer) {
        g_state.command_buffer = [g_queue commandBuffer];
    }

    MTLRenderPassDescriptor* desc = [[MTLRenderPassDescriptor alloc] init];

    if (params->flags & 0x1) {
        id<MTLTexture> target = nil;
        for (int i = 0; i < 4; i++) {
            if (g_state.render_targets[i]) {
                auto it = g_textures.find(g_state.render_targets[i]);
                if (it != g_textures.end()) { target = it->second.texture; break; }
            }
        }
        if (!target && g_swapchain && g_swapchain->back_buffer) target = g_swapchain->back_buffer;

        if (target) {
            auto* attachment = desc.colorAttachments[0];
            attachment.texture = target;
            attachment.loadAction = MTLLoadActionClear;
            float r = ((params->color >> 16) & 0xFF) / 255.0f;
            float g = ((params->color >> 8) & 0xFF) / 255.0f;
            float b = (params->color & 0xFF) / 255.0f;
            float a = ((params->color >> 24) & 0xFF) / 255.0f;
            attachment.clearColor = MTLClearColorMake(r, g, b, a);
            attachment.storeAction = MTLStoreActionStore;
        }
    }

    if ((params->flags & 0x2) && g_state.depth_stencil) {
        auto it = g_textures.find(g_state.depth_stencil);
        if (it != g_textures.end() && it->second.texture) {
            desc.depthAttachment.texture = it->second.texture;
            desc.depthAttachment.loadAction = MTLLoadActionClear;
            desc.depthAttachment.clearDepth = params->z;
            desc.depthAttachment.storeAction = MTLStoreActionStore;
        }
    }

    if ((params->flags & 0x4) && g_state.depth_stencil) {
        auto it = g_textures.find(g_state.depth_stencil);
        if (it != g_textures.end() && it->second.texture) {
            desc.stencilAttachment.texture = it->second.texture;
            desc.stencilAttachment.loadAction = MTLLoadActionClear;
            desc.stencilAttachment.clearStencil = params->stencil;
            desc.stencilAttachment.storeAction = MTLStoreActionStore;
        }
    }

    id<MTLRenderCommandEncoder> enc = [g_state.command_buffer renderCommandEncoderWithDescriptor:desc];
    [enc endEncoding];
    return 0;
}

static NTSTATUS d3d9_present(void*) {
    if (!g_swapchain || !g_swapchain->layer) return -1;

    @autoreleasepool {
        if (g_state.in_render_pass && g_state.encoder) {
            [g_state.encoder endEncoding];
            g_state.in_render_pass = false;
            g_state.encoder = nil;
        }
        if (g_state.command_buffer) {
            [g_state.command_buffer commit];
            g_state.command_buffer = nil;
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

    memset((void*)&g_state, 0, sizeof(g_state));
    return 0;
}

static NTSTATUS d3d9_set_render_target(void* p) {
    uint64_t* handles = (uint64_t*)p;
    uint32_t index = (uint32_t)handles[0];
    uint64_t handle = handles[1];
    if (index < 4) {
        g_state.render_targets[index] = handle;
    }
    return 0;
}

static NTSTATUS d3d9_set_depth_stencil(void* p) {
    g_state.depth_stencil = *(uint64_t*)p;
    return 0;
}

static NTSTATUS d3d9_set_viewport(void* p) {
    auto* vp = (struct d3d9_viewport*)p;
    g_state.viewport_x = vp->x;
    g_state.viewport_y = vp->y;
    g_state.viewport_w = vp->width;
    g_state.viewport_h = vp->height;
    g_state.viewport_min_z = vp->min_z;
    g_state.viewport_max_z = vp->max_z;
    g_state.has_viewport = true;

    if (g_state.encoder) {
        MTLViewport mtlvp = { (double)vp->x, (double)vp->y,
                              (double)vp->width, (double)vp->height,
                              (double)vp->min_z, (double)vp->max_z };
        [g_state.encoder setViewport:mtlvp];
    }
    return 0;
}

static NTSTATUS d3d9_set_scissor_rect(void* p) {
    auto* r = (struct d3d9_scissor_rect*)p;
    if (g_state.encoder) {
        MTLScissorRect scissor = { (NSUInteger)r->left, (NSUInteger)r->top,
                                   (NSUInteger)(r->right - r->left),
                                   (NSUInteger)(r->bottom - r->top) };
        [g_state.encoder setScissorRect:scissor];
    }
    return 0;
}

static NTSTATUS d3d9_begin_scene(void*) {
    g_state.in_scene = true;
    return 0;
}

static NTSTATUS d3d9_end_scene(void*) {
    g_state.in_scene = false;
    return 0;
}

static NTSTATUS d3d9_reset(void* p) {
    auto* params = (struct d3d9_reset_params*)p;
    if (g_swapchain) {
        g_swapchain->width = params->width > 0 ? params->width : 1280;
        g_swapchain->height = params->height > 0 ? params->height : 720;
        g_swapchain->back_buffer = nil;
        if (g_swapchain->layer) {
            g_swapchain->layer.drawableSize = CGSizeMake(g_swapchain->width, g_swapchain->height);
        }
    }
    memset((void*)&g_state, 0, sizeof(g_state));
    return 0;
}

static NTSTATUS d3d9_map(void* p) {
    auto* params = (struct d3d9_map_params*)p;
    auto it = g_buffers.find(params->resource_handle);
    if (it != g_buffers.end() && it->second.buffer) {
        params->out_data = [it->second.buffer contents];
        params->out_row_pitch = (uint32_t)it->second.size;
        return 0;
    }
    params->out_data = nullptr;
    return -1;
}

static NTSTATUS d3d9_stub(void*) { return 0; }

extern "C" {

typedef NTSTATUS (*unixlib_entry_t)(void*);

__attribute__((used, visibility("default")))
unixlib_entry_t __wine_unix_call_funcs[] = {
    d3d9_init,
    d3d9_create_device,
    d3d9_create_vertex_buffer,
    d3d9_create_index_buffer,
    d3d9_create_texture,
    d3d9_create_render_target,
    d3d9_create_depth_stencil,
    d3d9_create_vertex_shader,
    d3d9_create_pixel_shader,
    d3d9_create_vertex_declaration,
    d3d9_set_vertex_declaration,
    d3d9_set_stream_source,
    d3d9_set_indices,
    d3d9_set_vertex_shader,
    d3d9_set_pixel_shader,
    d3d9_set_vs_constants_f,
    d3d9_set_ps_constants_f,
    d3d9_set_texture,
    d3d9_set_sampler_state,
    d3d9_set_render_state,
    d3d9_draw_primitive,
    d3d9_draw_indexed_primitive,
    d3d9_clear,
    d3d9_present,
    d3d9_set_render_target,
    d3d9_set_depth_stencil,
    d3d9_set_viewport,
    d3d9_set_scissor_rect,
    d3d9_begin_scene,
    d3d9_end_scene,
    d3d9_reset,
    d3d9_stub,
    d3d9_map,
    d3d9_stub,
};

__attribute__((used, visibility("default")))
unixlib_entry_t __wine_unix_call_wow64_funcs[] = {
    d3d9_init,
    d3d9_create_device,
    d3d9_create_vertex_buffer,
    d3d9_create_index_buffer,
    d3d9_create_texture,
    d3d9_create_render_target,
    d3d9_create_depth_stencil,
    d3d9_create_vertex_shader,
    d3d9_create_pixel_shader,
    d3d9_create_vertex_declaration,
    d3d9_set_vertex_declaration,
    d3d9_set_stream_source,
    d3d9_set_indices,
    d3d9_set_vertex_shader,
    d3d9_set_pixel_shader,
    d3d9_set_vs_constants_f,
    d3d9_set_ps_constants_f,
    d3d9_set_texture,
    d3d9_set_sampler_state,
    d3d9_set_render_state,
    d3d9_draw_primitive,
    d3d9_draw_indexed_primitive,
    d3d9_clear,
    d3d9_present,
    d3d9_set_render_target,
    d3d9_set_depth_stencil,
    d3d9_set_viewport,
    d3d9_set_scissor_rect,
    d3d9_begin_scene,
    d3d9_end_scene,
    d3d9_reset,
    d3d9_stub,
    d3d9_map,
    d3d9_stub,
};

NTSTATUS __attribute__((visibility("default"))) __wine_unix_lib_init(void) { return 0; }

}
