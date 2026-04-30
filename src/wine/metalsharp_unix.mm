#include "metalsharp_unix.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_command_queue = nil;
static NSWindow* g_window = nil;
static CAMetalLayer* g_metal_layer = nil;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ms_init(void* params) {
    (void)params;
    if (g_device) return 0;

    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        fprintf(stderr, "[metalsharp] Failed to create Metal device\n");
        return -1;
    }

    g_command_queue = [g_device newCommandQueue];
    if (!g_command_queue) {
        fprintf(stderr, "[metalsharp] Failed to create command queue\n");
        return -1;
    }

    const char* name = [[g_device name] UTF8String];
    fprintf(stderr, "[metalsharp] Initialized Metal device: %s\n", name);
    return 0;
}

static int ms_create_device(void* params) {
    struct ms_create_device_params* p = (struct ms_create_device_params*)params;
    (void)p;
    return ms_init(nullptr);
}

static int ms_create_swap_chain(void* params) {
    struct ms_create_swap_chain_params* p = (struct ms_create_swap_chain_params*)params;

    if (!g_device) ms_init(nullptr);

    dispatch_async(dispatch_get_main_queue(), ^{
        NSRect frame = NSMakeRect(0, 0, p->width, p->height);

        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        g_window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        [g_window setTitle:@"MetalSharp"];
        [g_window makeKeyAndOrderFront:nil];

        g_metal_layer = [CAMetalLayer layer];
        g_metal_layer.device = g_device;
        g_metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_metal_layer.framebufferOnly = NO;
        g_metal_layer.drawableSize = CGSizeMake(p->width, p->height);

        NSView* contentView = [g_window contentView];
        [contentView setWantsLayer:YES];
        [contentView setLayer:g_metal_layer];
    });

    fprintf(stderr, "[metalsharp] Created swap chain %dx%d windowed=%d\n",
            p->width, p->height, p->windowed);
    return 0;
}

static int ms_present(void* params) {
    (void)params;
    if (!g_metal_layer || !g_command_queue) return -1;

    id<MTLCommandBuffer> cmdBuffer = [g_command_queue commandBuffer];
    id<CAMetalDrawable> drawable = [g_metal_layer nextDrawable];
    if (!drawable) return 0;

    id<MTLTexture> texture = drawable.texture;
    [cmdBuffer presentDrawable:drawable];
    [cmdBuffer commit];

    return 0;
}

static int ms_shutdown(void* params) {
    (void)params;
    fprintf(stderr, "[metalsharp] Shutting down\n");
    return 0;
}

static int ms_stub(void* params) {
    (void)params;
    return 0;
}

static int ms_dispatch_call(ms_func_id func, void* params) {
    switch (func) {
        case MS_FUNC_INIT:               return ms_init(params);
        case MS_FUNC_CREATE_DEVICE:       return ms_create_device(params);
        case MS_FUNC_CREATE_SWAP_CHAIN:   return ms_create_swap_chain(params);
        case MS_FUNC_PRESENT:             return ms_present(params);
        case MS_FUNC_SHUTDOWN:            return ms_shutdown(params);

        case MS_FUNC_CREATE_TEXTURE_2D:
        case MS_FUNC_CREATE_BUFFER:
        case MS_FUNC_CREATE_VERTEX_SHADER:
        case MS_FUNC_CREATE_PIXEL_SHADER:
        case MS_FUNC_CREATE_RENDER_TARGET_VIEW:
        case MS_FUNC_CREATE_DEPTH_STENCIL_VIEW:
        case MS_FUNC_CREATE_SAMPLER_STATE:
        case MS_FUNC_CREATE_INPUT_LAYOUT:
        case MS_FUNC_CREATE_BLEND_STATE:
        case MS_FUNC_CREATE_RASTERIZER_STATE:
        case MS_FUNC_CREATE_DEPTH_STENCIL_STATE:
        case MS_FUNC_DRAW_INDEXED:
        case MS_FUNC_DRAW:
        case MS_FUNC_MAP:
        case MS_FUNC_UNMAP:
        case MS_FUNC_RESIZE_BUFFERS:
        case MS_FUNC_GET_BUFFER:
        case MS_FUNC_COPY_RESOURCE:
        case MS_FUNC_SET_VIEWPORTS:
        case MS_FUNC_SET_SCISSOR_RECTS:
        case MS_FUNC_CLEAR_RENDER_TARGET_VIEW:
        case MS_FUNC_CLEAR_DEPTH_STENCIL_VIEW:
        case MS_FUNC_OM_SET_RENDER_TARGETS:
        case MS_FUNC_IA_SET_VERTEX_BUFFERS:
        case MS_FUNC_IA_SET_INDEX_BUFFER:
        case MS_FUNC_IA_SET_PRIMITIVE_TOPOLOGY:
        case MS_FUNC_VS_SET_SHADER:
        case MS_FUNC_PS_SET_SHADER:
        case MS_FUNC_VS_SET_CONSTANT_BUFFERS:
        case MS_FUNC_PS_SET_CONSTANT_BUFFERS:
        case MS_FUNC_VS_SET_SHADER_RESOURCES:
        case MS_FUNC_PS_SET_SHADER_RESOURCES:
        case MS_FUNC_VS_SET_SAMPLERS:
        case MS_FUNC_PS_SET_SAMPLERS:
        case MS_FUNC_RSS_SET_STATE:
        case MS_FUNC_OM_SET_BLEND_STATE:
        case MS_FUNC_OM_SET_DEPTH_STENCIL_STATE:
        case MS_FUNC_CREATE_SHADER_RESOURCE_VIEW:
        case MS_FUNC_UPDATE_SUBRESOURCE:
        case MS_FUNC_GENERATE_MIPS:
        case MS_FUNC_CREATE_COMPUTE_SHADER:
        case MS_FUNC_CS_SET_SHADER:
        case MS_FUNC_CS_SET_CONSTANT_BUFFERS:
        case MS_FUNC_CS_SET_SHADER_RESOURCES:
        case MS_FUNC_CS_SET_SAMPLERS:
        case MS_FUNC_CS_SET_UNORDERED_ACCESS_VIEWS:
        case MS_FUNC_DISPATCH:
        case MS_FUNC_CREATE_UNORDERED_ACCESS_VIEW:
        case MS_FUNC_DESTROY_DEVICE:
        case MS_FUNC_DESTROY_TEXTURE:
        case MS_FUNC_DESTROY_BUFFER:
        case MS_FUNC_DESTROY_SHADER:
        case MS_FUNC_DESTROY_VIEW:
        case MS_FUNC_DESTROY_STATE:
        case MS_FUNC_DESTROY_INPUT_LAYOUT:
        case MS_FUNC_GET_DEVICE_CONTEXT:
        case MS_FUNC_EXECUTE_COMMAND_LIST:
        case MS_FUNC_FINISH_COMMAND_LIST:
        case MS_FUNC_FLUSH:
            return ms_stub(params);
    }
    return -1;
}

__attribute__((visibility("default")))
const ms_unix_call_func __wine_unix_call_funcs = ms_dispatch_call;

__attribute__((visibility("default")))
const ms_unix_call_func __wine_unix_call_wow64_funcs = ms_dispatch_call;

__attribute__((visibility("default")))
void __wine_init_lib(void) {}
