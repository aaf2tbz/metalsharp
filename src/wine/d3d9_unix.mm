/// @file d3d9_unix.mm
/// @brief Unix-side D3D9 Metal backend (Objective-C++ Metal implementation).
///
/// Implements the Metal backend for D3D9 calls received from the PE side. Handles IDirect3DDevice9 draw calls, texture management, fixed-function pipeline emulation via Metal shaders, and swap chain presentation through CAMetalLayer.
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

struct D3D9Shader;

static id<MTLFunction> mojo_translate_to_metal(const uint8_t* bytecode, uint32_t size, const char* stage, D3D9Shader* outShader);

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
    int uniform_count;
    int sampler_count;
    int attribute_count;
    int output_count;
    uint32_t used_sampler_mask;
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
static std::unordered_map<uint64_t, id<MTLDepthStencilState>> g_depth_states;
static std::unordered_map<uint64_t, id<MTLSamplerState>> g_sampler_states;

enum D3D9RenderState {
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_LASTPIXEL = 16,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_WRAP0 = 128,
    D3DRS_WRAP1 = 129,
    D3DRS_WRAP2 = 130,
    D3DRS_WRAP3 = 131,
    D3DRS_WRAP4 = 132,
    D3DRS_WRAP5 = 133,
    D3DRS_WRAP6 = 134,
    D3DRS_WRAP7 = 135,
    D3DRS_CLIPPING = 136,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_FOGVERTEXMODE = 140,
    D3DRS_COLORVERTEX = 141,
    D3DRS_LOCALVIEWER = 142,
    D3DRS_NORMALIZENORMALS = 143,
    D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148,
    D3DRS_VERTEXBLEND = 151,
    D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_POINTSIZE = 154,
    D3DRS_POINTSIZE_MIN = 155,
    D3DRS_POINTSPRITEENABLE = 156,
    D3DRS_POINTSCALEENABLE = 157,
    D3DRS_POINTSCALE_A = 158,
    D3DRS_POINTSCALE_B = 159,
    D3DRS_POINTSCALE_C = 160,
    D3DRS_MULTISAMPLEANTIALIAS = 161,
    D3DRS_MULTISAMPLEMASK = 162,
    D3DRS_PATCHEDGESTYLE = 163,
    D3DRS_DEBUGMONITORTOKEN = 165,
    D3DRS_POINTSIZE_MAX = 166,
    D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_TWEENFACTOR = 170,
    D3DRS_BLENDOP = 171,
    D3DRS_POSITIONDEGREE = 172,
    D3DRS_NORMALDEGREE = 173,
    D3DRS_SCISSORTESTENABLE = 174,
    D3DRS_SLOPESCALEDEPTHBIAS = 175,
    D3DRS_ANTIALIASEDLINEENABLE = 176,
    D3DRS_MINTESSELLATIONLEVEL = 178,
    D3DRS_MAXTESSELLATIONLEVEL = 179,
    D3DRS_ADAPTIVETESS_X = 180,
    D3DRS_ADAPTIVETESS_Y = 181,
    D3DRS_ADAPTIVETESS_Z = 182,
    D3DRS_ADAPTIVETESS_W = 183,
    D3DRS_ENABLEADAPTIVETESSELLATION = 184,
    D3DRS_TWOSIDEDSTENCILMODE = 185,
    D3DRS_CCW_STENCILFAIL = 186,
    D3DRS_CCW_STENCILZFAIL = 187,
    D3DRS_CCW_STENCILPASS = 188,
    D3DRS_CCW_STENCILFUNC = 189,
    D3DRS_COLORWRITEENABLE1 = 190,
    D3DRS_COLORWRITEENABLE2 = 191,
    D3DRS_COLORWRITEENABLE3 = 192,
    D3DRS_BLENDFACTOR = 193,
    D3DRS_SRGBWRITEENABLE = 194,
    D3DRS_DEPTHBIAS = 195,
    D3DRS_WRAP8 = 198,
    D3DRS_WRAP9 = 199,
    D3DRS_WRAP10 = 200,
    D3DRS_WRAP11 = 201,
    D3DRS_WRAP12 = 202,
    D3DRS_WRAP13 = 203,
    D3DRS_WRAP14 = 204,
    D3DRS_WRAP15 = 205,
    D3DRS_SEPARATEALPHABLENDENABLE = 206,
    D3DRS_SRCBLENDALPHA = 207,
    D3DRS_DESTBLENDALPHA = 208,
    D3DRS_BLENDOPALPHA = 209,
};

enum D3D9Blend {
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT = 11,
    D3DBLEND_BOTHSRCALPHA = 12,
    D3DBLEND_BOTHINVSRCALPHA = 13,
    D3DBLEND_BLENDFACTOR = 14,
    D3DBLEND_INVBLENDFACTOR = 15,
};

enum D3D9BlendOp {
    D3DBLENDOP_ADD = 1,
    D3DBLENDOP_SUBTRACT = 2,
    D3DBLENDOP_REVSUBTRACT = 3,
    D3DBLENDOP_MIN = 4,
    D3DBLENDOP_MAX = 5,
};

enum D3D9Cull {
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
};

enum D3D9CmpFunc {
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
};

enum D3D9StencilOp {
    D3DSTENCILOP_KEEP = 1,
    D3DSTENCILOP_ZERO = 2,
    D3DSTENCILOP_REPLACE = 3,
    D3DSTENCILOP_INCRSAT = 4,
    D3DSTENCILOP_DECRSAT = 5,
    D3DSTENCILOP_INVERT = 6,
    D3DSTENCILOP_INCR = 7,
    D3DSTENCILOP_DECR = 8,
};

enum D3D9FillMode {
    D3DFILL_POINT = 1,
    D3DFILL_WIREFRAME = 2,
    D3DFILL_SOLID = 3,
};

static MTLBlendFactor d3d_blend_to_mtl(uint32_t blend) {
    switch (blend) {
        case D3DBLEND_ZERO: return MTLBlendFactorZero;
        case D3DBLEND_ONE: return MTLBlendFactorOne;
        case D3DBLEND_SRCCOLOR: return MTLBlendFactorSourceColor;
        case D3DBLEND_INVSRCCOLOR: return MTLBlendFactorOneMinusSourceColor;
        case D3DBLEND_SRCALPHA: return MTLBlendFactorSourceAlpha;
        case D3DBLEND_INVSRCALPHA: return MTLBlendFactorOneMinusSourceAlpha;
        case D3DBLEND_DESTALPHA: return MTLBlendFactorDestinationAlpha;
        case D3DBLEND_INVDESTALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
        case D3DBLEND_DESTCOLOR: return MTLBlendFactorDestinationColor;
        case D3DBLEND_INVDESTCOLOR: return MTLBlendFactorOneMinusDestinationColor;
        case D3DBLEND_SRCALPHASAT: return MTLBlendFactorSourceAlphaSaturated;
        case D3DBLEND_BOTHSRCALPHA: return MTLBlendFactorSourceAlpha;
        case D3DBLEND_BOTHINVSRCALPHA: return MTLBlendFactorOneMinusSourceAlpha;
        case D3DBLEND_BLENDFACTOR: return MTLBlendFactorBlendColor;
        case D3DBLEND_INVBLENDFACTOR: return MTLBlendFactorOneMinusBlendColor;
        default: return MTLBlendFactorOne;
    }
}

static MTLBlendOperation d3d_blendop_to_mtl(uint32_t op) {
    switch (op) {
        case D3DBLENDOP_ADD: return MTLBlendOperationAdd;
        case D3DBLENDOP_SUBTRACT: return MTLBlendOperationSubtract;
        case D3DBLENDOP_REVSUBTRACT: return MTLBlendOperationReverseSubtract;
        case D3DBLENDOP_MIN: return MTLBlendOperationMin;
        case D3DBLENDOP_MAX: return MTLBlendOperationMax;
        default: return MTLBlendOperationAdd;
    }
}

static MTLCompareFunction d3d_cmp_to_mtl(uint32_t func) {
    switch (func) {
        case D3DCMP_NEVER: return MTLCompareFunctionNever;
        case D3DCMP_LESS: return MTLCompareFunctionLess;
        case D3DCMP_EQUAL: return MTLCompareFunctionEqual;
        case D3DCMP_LESSEQUAL: return MTLCompareFunctionLessEqual;
        case D3DCMP_GREATER: return MTLCompareFunctionGreater;
        case D3DCMP_NOTEQUAL: return MTLCompareFunctionNotEqual;
        case D3DCMP_GREATEREQUAL: return MTLCompareFunctionGreaterEqual;
        case D3DCMP_ALWAYS: return MTLCompareFunctionAlways;
        default: return MTLCompareFunctionLess;
    }
}

static MTLStencilOperation d3d_stencilop_to_mtl(uint32_t op) {
    switch (op) {
        case D3DSTENCILOP_KEEP: return MTLStencilOperationKeep;
        case D3DSTENCILOP_ZERO: return MTLStencilOperationZero;
        case D3DSTENCILOP_REPLACE: return MTLStencilOperationReplace;
        case D3DSTENCILOP_INCRSAT: return MTLStencilOperationIncrementClamp;
        case D3DSTENCILOP_DECRSAT: return MTLStencilOperationDecrementClamp;
        case D3DSTENCILOP_INVERT: return MTLStencilOperationInvert;
        case D3DSTENCILOP_INCR: return MTLStencilOperationIncrementWrap;
        case D3DSTENCILOP_DECR: return MTLStencilOperationDecrementWrap;
        default: return MTLStencilOperationKeep;
    }
}

static MTLCullMode d3d_cull_to_mtl(uint32_t cull) {
    switch (cull) {
        case D3DCULL_NONE: return MTLCullModeNone;
        case D3DCULL_CW: return MTLCullModeBack;
        case D3DCULL_CCW: return MTLCullModeFront;
        default: return MTLCullModeNone;
    }
}

static MTLTriangleFillMode d3d_fill_to_mtl(uint32_t fill) {
    switch (fill) {
        case D3DFILL_POINT: return MTLTriangleFillModeLines;
        case D3DFILL_WIREFRAME: return MTLTriangleFillModeLines;
        case D3DFILL_SOLID: return MTLTriangleFillModeFill;
        default: return MTLTriangleFillModeFill;
    }
}

static uint64_t hash_depth_stencil_state(uint32_t zenable, uint32_t zwrite, uint32_t zfunc,
    uint32_t stencilenable, uint32_t stencilfunc, uint32_t stencilfail,
    uint32_t stencilzfail, uint32_t stencilpass, uint32_t stencilmask,
    uint32_t stencilwritemask, uint32_t twosided,
    uint32_t ccw_func, uint32_t ccw_fail, uint32_t ccw_zfail, uint32_t ccw_pass) {
    uint64_t h = 0;
    h = zenable | (zwrite << 1) | ((uint64_t)zfunc << 2);
    h ^= stencilenable | ((uint64_t)stencilfunc << 4) | ((uint64_t)stencilfail << 8);
    h ^= ((uint64_t)stencilzfail << 12) | ((uint64_t)stencilpass << 16);
    h ^= stencilmask | ((uint64_t)stencilwritemask << 8);
    h ^= twosided | ((uint64_t)ccw_func << 4) | ((uint64_t)ccw_fail << 8);
    h ^= ((uint64_t)ccw_zfail << 12) | ((uint64_t)ccw_pass << 16);
    return h;
}

static id<MTLDepthStencilState> get_depth_stencil_state() {
    uint32_t zenable = g_state.render_states[D3DRS_ZENABLE];
    uint32_t zwrite = g_state.render_states[D3DRS_ZWRITEENABLE];
    uint32_t zfunc = g_state.render_states[D3DRS_ZFUNC];
    uint32_t stencilenable = g_state.render_states[D3DRS_STENCILENABLE];
    uint32_t stencilfunc = g_state.render_states[D3DRS_STENCILFUNC];
    uint32_t stencilfail = g_state.render_states[D3DRS_STENCILFAIL];
    uint32_t stencilzfail = g_state.render_states[D3DRS_STENCILZFAIL];
    uint32_t stencilpass = g_state.render_states[D3DRS_STENCILPASS];
    uint32_t stencilmask = g_state.render_states[D3DRS_STENCILMASK];
    uint32_t stencilwritemask = g_state.render_states[D3DRS_STENCILWRITEMASK];
    uint32_t twosided = g_state.render_states[D3DRS_TWOSIDEDSTENCILMODE];
    uint32_t ccw_func = g_state.render_states[D3DRS_CCW_STENCILFUNC];
    uint32_t ccw_fail = g_state.render_states[D3DRS_CCW_STENCILFAIL];
    uint32_t ccw_zfail = g_state.render_states[D3DRS_CCW_STENCILZFAIL];
    uint32_t ccw_pass = g_state.render_states[D3DRS_CCW_STENCILPASS];

    uint64_t key = hash_depth_stencil_state(zenable, zwrite, zfunc, stencilenable,
        stencilfunc, stencilfail, stencilzfail, stencilpass, stencilmask,
        stencilwritemask, twosided, ccw_func, ccw_fail, ccw_zfail, ccw_pass);

    auto it = g_depth_states.find(key);
    if (it != g_depth_states.end()) return it->second;

    MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
    desc.depthCompareFunction = zenable ? d3d_cmp_to_mtl(zfunc ? zfunc : D3DCMP_LESSEQUAL) : MTLCompareFunctionAlways;
    desc.depthWriteEnabled = zwrite ? YES : NO;

    if (stencilenable) {
        MTLStencilDescriptor* front = [[MTLStencilDescriptor alloc] init];
        front.stencilCompareFunction = d3d_cmp_to_mtl(stencilfunc ? stencilfunc : D3DCMP_ALWAYS);
        front.stencilFailureOperation = d3d_stencilop_to_mtl(stencilfail ? stencilfail : D3DSTENCILOP_KEEP);
        front.depthFailureOperation = d3d_stencilop_to_mtl(stencilzfail ? stencilzfail : D3DSTENCILOP_KEEP);
        front.depthStencilPassOperation = d3d_stencilop_to_mtl(stencilpass ? stencilpass : D3DSTENCILOP_KEEP);
        front.readMask = stencilmask ? stencilmask : 0xFFFFFFFF;
        front.writeMask = stencilwritemask ? stencilwritemask : 0xFFFFFFFF;
        desc.frontFaceStencil = front;

        if (twosided) {
            MTLStencilDescriptor* back = [[MTLStencilDescriptor alloc] init];
            back.stencilCompareFunction = d3d_cmp_to_mtl(ccw_func ? ccw_func : D3DCMP_ALWAYS);
            back.stencilFailureOperation = d3d_stencilop_to_mtl(ccw_fail ? ccw_fail : D3DSTENCILOP_KEEP);
            back.depthFailureOperation = d3d_stencilop_to_mtl(ccw_zfail ? ccw_zfail : D3DSTENCILOP_KEEP);
            back.depthStencilPassOperation = d3d_stencilop_to_mtl(ccw_pass ? ccw_pass : D3DSTENCILOP_KEEP);
            back.readMask = stencilmask ? stencilmask : 0xFFFFFFFF;
            back.writeMask = stencilwritemask ? stencilwritemask : 0xFFFFFFFF;
            desc.backFaceStencil = back;
        } else {
            desc.backFaceStencil = front;
        }
    }

    id<MTLDepthStencilState> state = [g_device newDepthStencilStateWithDescriptor:desc];
    g_depth_states[key] = state;
    return state;
}

static void apply_blend_to_pipeline(MTLRenderPipelineDescriptor* pdesc) {
    bool alphaBlend = g_state.render_states[D3DRS_ALPHABLENDENABLE] != 0;
    uint32_t colorWrite = g_state.render_states[D3DRS_COLORWRITEENABLE];
    if (!colorWrite) colorWrite = 0xF;

    for (int i = 0; i < 4; i++) {
        auto* ca = pdesc.colorAttachments[i];
        if (ca.pixelFormat == MTLPixelFormatInvalid) continue;

        if (alphaBlend) {
            uint32_t srcBlend = g_state.render_states[D3DRS_SRCBLEND];
            uint32_t dstBlend = g_state.render_states[D3DRS_DESTBLEND];
            uint32_t blendOp = g_state.render_states[D3DRS_BLENDOP];
            uint32_t separateAlpha = g_state.render_states[D3DRS_SEPARATEALPHABLENDENABLE];

            ca.blendingEnabled = YES;
            ca.sourceRGBBlendFactor = d3d_blend_to_mtl(srcBlend ? srcBlend : D3DBLEND_ONE);
            ca.destinationRGBBlendFactor = d3d_blend_to_mtl(dstBlend ? dstBlend : D3DBLEND_ZERO);
            ca.rgbBlendOperation = d3d_blendop_to_mtl(blendOp ? blendOp : D3DBLENDOP_ADD);

            if (separateAlpha) {
                uint32_t srcAlpha = g_state.render_states[D3DRS_SRCBLENDALPHA];
                uint32_t dstAlpha = g_state.render_states[D3DRS_DESTBLENDALPHA];
                uint32_t blendOpAlpha = g_state.render_states[D3DRS_BLENDOPALPHA];
                ca.sourceAlphaBlendFactor = d3d_blend_to_mtl(srcAlpha ? srcAlpha : D3DBLEND_ONE);
                ca.destinationAlphaBlendFactor = d3d_blend_to_mtl(dstAlpha ? dstAlpha : D3DBLEND_ZERO);
                ca.alphaBlendOperation = d3d_blendop_to_mtl(blendOpAlpha ? blendOpAlpha : D3DBLENDOP_ADD);
            } else {
                ca.sourceAlphaBlendFactor = ca.sourceRGBBlendFactor;
                ca.destinationAlphaBlendFactor = ca.destinationRGBBlendFactor;
                ca.alphaBlendOperation = ca.rgbBlendOperation;
            }
        } else {
            ca.blendingEnabled = NO;
        }

        uint32_t writeMask = (i == 0) ? colorWrite : g_state.render_states[D3DRS_COLORWRITEENABLE1 + i - 1];
        if (writeMask == 0 && i > 0) writeMask = 0xF;
        ca.writeMask = (MTLColorWriteMask)(
            ((writeMask & 1) ? MTLColorWriteMaskRed : 0) |
            ((writeMask & 2) ? MTLColorWriteMaskGreen : 0) |
            ((writeMask & 4) ? MTLColorWriteMaskBlue : 0) |
            ((writeMask & 8) ? MTLColorWriteMaskAlpha : 0));
    }
}

enum D3D9SamplerState {
    D3DSAMP_ADDRESSU = 1,
    D3DSAMP_ADDRESSV = 2,
    D3DSAMP_ADDRESSW = 3,
    D3DSAMP_BORDERCOLOR = 4,
    D3DSAMP_MAGFILTER = 5,
    D3DSAMP_MINFILTER = 6,
    D3DSAMP_MIPFILTER = 7,
    D3DSAMP_MIPMAPLODBIAS = 8,
    D3DSAMP_MAXMIPLEVEL = 9,
    D3DSAMP_MAXANISOTROPY = 10,
    D3DSAMP_SRGBTEXTURE = 11,
    D3DSAMP_ELEMENTINDEX = 12,
    D3DSAMP_DMAPOFFSET = 13,
};

enum D3D9TextureAddressMode {
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP = 3,
    D3DTADDRESS_BORDER = 4,
    D3DTADDRESS_MIRRORONCE = 5,
};

enum D3D9TextureFilterType {
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
    D3DTEXF_ANISOTROPIC = 3,
    D3DTEXF_PYRAMIDALQUAD = 6,
    D3DTEXF_GAUSSIANQUAD = 7,
};

static MTLSamplerAddressMode d3d_address_to_mtl(uint32_t mode) {
    switch (mode) {
        case D3DTADDRESS_WRAP: return MTLSamplerAddressModeRepeat;
        case D3DTADDRESS_MIRROR: return MTLSamplerAddressModeMirrorRepeat;
        case D3DTADDRESS_CLAMP: return MTLSamplerAddressModeClampToEdge;
        case D3DTADDRESS_BORDER: return MTLSamplerAddressModeClampToZero;
        case D3DTADDRESS_MIRRORONCE: return MTLSamplerAddressModeMirrorClampToEdge;
        default: return MTLSamplerAddressModeClampToEdge;
    }
}

static uint64_t hash_sampler_state(uint32_t sampler_idx) {
    uint64_t h = sampler_idx;
    for (int i = 0; i < 14; i++)
        h = (h << 3) ^ g_state.sampler_states[sampler_idx][i];
    return h;
}

static id<MTLSamplerState> get_sampler_state(uint32_t sampler_idx) {
    uint64_t key = hash_sampler_state(sampler_idx);
    auto it = g_sampler_states.find(key);
    if (it != g_sampler_states.end()) return it->second;

    MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
    desc.sAddressMode = d3d_address_to_mtl(g_state.sampler_states[sampler_idx][D3DSAMP_ADDRESSU]);
    desc.tAddressMode = d3d_address_to_mtl(g_state.sampler_states[sampler_idx][D3DSAMP_ADDRESSV]);
    desc.rAddressMode = d3d_address_to_mtl(g_state.sampler_states[sampler_idx][D3DSAMP_ADDRESSW]);

    uint32_t magFilter = g_state.sampler_states[sampler_idx][D3DSAMP_MAGFILTER];
    uint32_t minFilter = g_state.sampler_states[sampler_idx][D3DSAMP_MINFILTER];
    uint32_t mipFilter = g_state.sampler_states[sampler_idx][D3DSAMP_MIPFILTER];
    uint32_t maxAniso = g_state.sampler_states[sampler_idx][D3DSAMP_MAXANISOTROPY];
    if (!maxAniso) maxAniso = 1;

    if (magFilter == D3DTEXF_ANISOTROPIC || minFilter == D3DTEXF_ANISOTROPIC) {
        desc.minFilter = MTLSamplerMinMagFilterLinear;
        desc.magFilter = MTLSamplerMinMagFilterLinear;
        desc.mipFilter = (mipFilter == D3DTEXF_POINT) ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
        desc.maxAnisotropy = maxAniso;
    } else if (magFilter == D3DTEXF_LINEAR || minFilter == D3DTEXF_LINEAR) {
        desc.minFilter = MTLSamplerMinMagFilterLinear;
        desc.magFilter = MTLSamplerMinMagFilterLinear;
        desc.mipFilter = (mipFilter == D3DTEXF_LINEAR) ? MTLSamplerMipFilterLinear :
                         (mipFilter == D3DTEXF_POINT) ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped;
    } else {
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.magFilter = MTLSamplerMinMagFilterNearest;
        desc.mipFilter = (mipFilter == D3DTEXF_LINEAR) ? MTLSamplerMipFilterLinear :
                         (mipFilter == D3DTEXF_POINT) ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped;
    }

    float lodBias = *(float*)&g_state.sampler_states[sampler_idx][D3DSAMP_MIPMAPLODBIAS];
    uint32_t maxMipLevel = g_state.sampler_states[sampler_idx][D3DSAMP_MAXMIPLEVEL];
    if (lodBias != 0.0f) {
        desc.lodBias = lodBias;
        desc.lodMinClamp = -lodBias;
        desc.lodMaxClamp = 14.0f;
    }
    if (maxMipLevel) {
        desc.lodMinClamp = (float)maxMipLevel;
    }

    uint32_t borderColor = g_state.sampler_states[sampler_idx][D3DSAMP_BORDERCOLOR];
    if (borderColor) {
        uint32_t a = (borderColor >> 24) & 0xFF;
        desc.borderColor = (a == 0) ? MTLSamplerBorderColorTransparentBlack : MTLSamplerBorderColorOpaqueBlack;
    }

    if (g_state.sampler_states[sampler_idx][D3DSAMP_SRGBTEXTURE])
        desc.supportArgumentBuffers = NO;

    id<MTLSamplerState> state = [g_device newSamplerStateWithDescriptor:desc];
    g_sampler_states[key] = state;
    return state;
}

static void apply_sampler_states_to_encoder() {
    if (!g_state.encoder) return;
    for (int i = 0; i < 16; i++) {
        if (g_state.texture_handles[i]) {
            auto tex_it = g_textures.find(g_state.texture_handles[i]);
            if (tex_it != g_textures.end() && tex_it->second.texture) {
                [g_state.encoder setFragmentTexture:tex_it->second.texture atIndex:i];
                id<MTLSamplerState> sampler = get_sampler_state(i);
                if (sampler) {
                    [g_state.encoder setFragmentSamplerState:sampler atIndex:i];
                }
                [g_state.encoder setVertexTexture:tex_it->second.texture atIndex:i];
                if (sampler) {
                    [g_state.encoder setVertexSamplerState:sampler atIndex:i];
                }
            }
        }
    }
}

enum D3D9DeclUsage {
    D3DDECLUSAGE_POSITION = 0,
    D3DDECLUSAGE_BLENDWEIGHT = 1,
    D3DDECLUSAGE_BLENDINDICES = 2,
    D3DDECLUSAGE_NORMAL = 3,
    D3DDECLUSAGE_PSIZE = 4,
    D3DDECLUSAGE_TEXCOORD = 5,
    D3DDECLUSAGE_TANGENT = 6,
    D3DDECLUSAGE_BINORMAL = 7,
    D3DDECLUSAGE_TESSFACTOR = 8,
    D3DDECLUSAGE_POSITIONT = 9,
    D3DDECLUSAGE_COLOR = 10,
    D3DDECLUSAGE_FOG = 11,
    D3DDECLUSAGE_DEPTH = 12,
    D3DDECLUSAGE_SAMPLE = 13,
};

static uint32_t usage_to_attribute_index(uint8_t usage, uint8_t usage_index) {
    switch (usage) {
        case D3DDECLUSAGE_POSITION: return 0;
        case D3DDECLUSAGE_BLENDWEIGHT: return 1;
        case D3DDECLUSAGE_BLENDINDICES: return 2;
        case D3DDECLUSAGE_NORMAL: return 3;
        case D3DDECLUSAGE_PSIZE: return 4;
        case D3DDECLUSAGE_TEXCOORD: return 5 + usage_index;
        case D3DDECLUSAGE_TANGENT: return 13;
        case D3DDECLUSAGE_BINORMAL: return 14;
        case D3DDECLUSAGE_COLOR: return 15 + usage_index;
        case D3DDECLUSAGE_POSITIONT: return 17;
        case D3DDECLUSAGE_FOG: return 18;
        default: return 19 + usage_index;
    }
}

static MTLVertexDescriptor* build_vertex_descriptor();

static void apply_render_state_to_encoder() {
    if (!g_state.encoder) return;

    id<MTLDepthStencilState> dsState = get_depth_stencil_state();
    if (dsState) [g_state.encoder setDepthStencilState:dsState];

    uint32_t stencilRef = g_state.render_states[D3DRS_STENCILREF];
    if (g_state.render_states[D3DRS_STENCILENABLE]) {
        [g_state.encoder setStencilFrontReferenceValue:stencilRef backReferenceValue:stencilRef];
    }

    uint32_t cullMode = g_state.render_states[D3DRS_CULLMODE];
    [g_state.encoder setCullMode:d3d_cull_to_mtl(cullMode ? cullMode : D3DCULL_NONE)];

    uint32_t fillMode = g_state.render_states[D3DRS_FILLMODE];
    [g_state.encoder setTriangleFillMode:d3d_fill_to_mtl(fillMode ? fillMode : D3DFILL_SOLID)];

    if (g_state.render_states[D3DRS_SCISSORTESTENABLE] && g_state.has_viewport) {
        MTLScissorRect scissor = {
            (NSUInteger)g_state.viewport_x, (NSUInteger)g_state.viewport_y,
            (NSUInteger)g_state.viewport_w, (NSUInteger)g_state.viewport_h
        };
        [g_state.encoder setScissorRect:scissor];
    }

    uint32_t blendFactor = g_state.render_states[D3DRS_BLENDFACTOR];
    if (blendFactor) {
        float r = ((blendFactor >> 16) & 0xFF) / 255.0f;
        float g = ((blendFactor >> 8) & 0xFF) / 255.0f;
        float b = (blendFactor & 0xFF) / 255.0f;
        float a = ((blendFactor >> 24) & 0xFF) / 255.0f;
        [g_state.encoder setBlendColorRed:r green:g blue:b alpha:a];
    }

    float depthBias = *(float*)&g_state.render_states[D3DRS_DEPTHBIAS];
    float slopeBias = *(float*)&g_state.render_states[D3DRS_SLOPESCALEDEPTHBIAS];
    if (depthBias != 0.0f || slopeBias != 0.0f) {
        [g_state.encoder setDepthBias:depthBias slopeScale:slopeBias clamp:0.0f];
    }
}

static id<MTLFunction> mojo_translate_to_metal(const uint8_t* bytecode, uint32_t size, const char* stage, D3D9Shader* outShader) {
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

    fprintf(stderr, "[mojo] %s translated %u bytes bytecode -> %d bytes MSL (uniforms=%d samplers=%d attrs=%d)\n",
        stage, size, pd->output_len, pd->uniform_count, pd->sampler_count, pd->attribute_count);

    if (outShader) {
        outShader->uniform_count = pd->uniform_count;
        outShader->sampler_count = pd->sampler_count;
        outShader->attribute_count = pd->attribute_count;
        outShader->output_count = pd->output_count;
        outShader->used_sampler_mask = 0;
        for (int i = 0; i < pd->sampler_count; i++)
            if (pd->samplers[i].index < 16)
                outShader->used_sampler_mask |= (1u << pd->samplers[i].index);
    }

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
        if (outShader && func) {
            outShader->library = library;
        }
        MOJOSHADER_freeParseData(pd);
        return func;
    }
}

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

static MTLVertexDescriptor* build_vertex_descriptor() {
    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];

    if (!g_state.vertex_decl_handle) return vd;
    auto it = g_vertex_decls.find(g_state.vertex_decl_handle);
    if (it == g_vertex_decls.end()) return vd;

    const auto& decl = it->second;
    uint32_t stream_strides[16] = {};
    bool stream_used[16] = {};

    for (uint32_t i = 0; i < decl.num_elements; i++) {
        const auto& el = decl.elements[i];
        if (el.stream >= 16) continue;

        uint32_t attrIdx = usage_to_attribute_index(el.usage, el.usage_index);
        if (attrIdx >= 31) continue;

        MTLVertexFormat fmt = d3ddecltype_to_mtl(el.type);
        uint32_t stride = d3ddecltype_size(el.type);

        auto* attr = vd.attributes[attrIdx];
        attr.format = fmt;
        attr.offset = el.offset;
        attr.bufferIndex = el.stream;

        stream_used[el.stream] = true;
        if (stride + el.offset > stream_strides[el.stream])
            stream_strides[el.stream] = stride + el.offset;
    }

    for (uint32_t s = 0; s < 16; s++) {
        if (!stream_used[s] && g_state.vertex_strides[s])
            stream_used[s] = true;
        if (stream_used[s]) {
            uint32_t stride = g_state.vertex_strides[s] ? g_state.vertex_strides[s] : stream_strides[s];
            if (!stride) stride = stream_strides[s];
            auto* layout = vd.layouts[s];
            layout.stride = stride;
            layout.stepFunction = MTLVertexStepFunctionPerVertex;
            layout.stepRate = 1;
        }
    }

    return vd;
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
     fprintf(stderr, "[d3d9] create_device: initiating\n");
     if (!g_device) { fprintf(stderr, "[d3d9] create_device: calling init\n"); d3d9_init(nullptr); }
     fprintf(stderr, "[d3d9] create_device: device=%p\n", (void*)(__bridge void*)g_device);
 
     if (g_swapchain) {
         if (g_swapchain->window) {
             dispatch_async(dispatch_get_main_queue(), ^{
                 [g_swapchain->window close];
             });
         }
         delete g_swapchain;
     }
     g_swapchain = new D3D9SwapChain();
     g_swapchain->width = params->width > 0 ? params->width : 1280;
     g_swapchain->height = params->height > 0 ? params->height : 720;
     fprintf(stderr, "[d3d9] create_device: swapchain %ux%u, dispatching window\n", g_swapchain->width, g_swapchain->height);
 
     dispatch_sync(dispatch_get_main_queue(), ^{
        @autoreleasepool {
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
        }
    });

     fprintf(stderr, "[d3d9] create_device: window created, making back_buffer\n");
 
     @autoreleasepool {
         MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
         fprintf(stderr, "[d3d9] create_device: td=%p\n", (void*)(__bridge void*)td);
         td.textureType = MTLTextureType2D;
         td.pixelFormat = MTLPixelFormatBGRA8Unorm;
         td.width = g_swapchain->width;
         td.height = g_swapchain->height;
         td.mipmapLevelCount = 1;
         td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
         td.storageMode = MTLStorageModePrivate;
         fprintf(stderr, "[d3d9] create_device: creating texture from device=%p\n", (void*)(__bridge void*)g_device);
         g_swapchain->back_buffer = [g_device newTextureWithDescriptor:td];
         fprintf(stderr, "[d3d9] create_device: back_buffer=%p\n", (void*)(__bridge void*)g_swapchain->back_buffer);
     }

    fprintf(stderr, "[d3d9] Device created: %ux%u windowed=%d backbuf=%p\n",
        g_swapchain->width, g_swapchain->height, params->windowed, g_swapchain->back_buffer);
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
        const uint8_t* ptr = (const uint8_t*)(uintptr_t)params->bytecode;
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
            shader.function = mojo_translate_to_metal(ptr, params->bytecode_size, "VS", &shader);
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
        const uint8_t* ptr = (const uint8_t*)(uintptr_t)params->bytecode;
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
            shader.function = mojo_translate_to_metal(ptr, params->bytecode_size, "PS", &shader);
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
        memcpy(decl.elements, (const void*)(uintptr_t)params->elements, params->num_elements * sizeof(struct d3d9_vertex_element));
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

    static id<MTLLibrary> g_ff_vs_lib = nil;
    static id<MTLLibrary> g_ff_ps_lib = nil;
    static id<MTLFunction> g_ff_vs_func = nil;
    static id<MTLFunction> g_ff_ps_func = nil;

    if (!g_ff_vs_lib) {
        @autoreleasepool {
            NSString* ffVS = @""
                "#include <metal_stdlib>\n"
                "using namespace metal;\n"
                "struct VIn { float3 position [[attribute(0)]]; float4 color [[attribute(15)]]; };\n"
                "struct VOut { float4 pos [[position]]; float4 color; };\n"
                "vertex VOut ff_vs(VIn in [[stage_in]]) {\n"
                "  VOut o; o.pos = float4(in.position, 1.0); o.color = in.color; return o;\n"
                "}\n";
            NSString* ffPS = @""
                "#include <metal_stdlib>\n"
                "using namespace metal;\n"
                "struct FIn { float4 pos [[position]]; float4 color; };\n"
                "fragment float4 ff_ps(FIn in [[stage_in]]) { return in.color; }\n";
            NSError* err = nil;
            g_ff_vs_lib = [g_device newLibraryWithSource:ffVS options:nil error:&err];
            if (err) fprintf(stderr, "[d3d9] FF VS compile error: %s\n", [[err localizedDescription] UTF8String]);
            if (!err) fprintf(stderr, "[d3d9] FF VS compiled OK\n");
            g_ff_ps_lib = [g_device newLibraryWithSource:ffPS options:nil error:&err];
            if (err) fprintf(stderr, "[d3d9] FF PS compile error: %s\n", [[err localizedDescription] UTF8String]);
            if (!err) fprintf(stderr, "[d3d9] FF PS compiled OK\n");
            if (err) fprintf(stderr, "[d3d9] FF PS compile error: %s\n", [[err localizedDescription] UTF8String]);
            if (g_ff_vs_lib) g_ff_vs_func = [g_ff_vs_lib newFunctionWithName:@"ff_vs"];
            if (g_ff_ps_lib) g_ff_ps_func = [g_ff_ps_lib newFunctionWithName:@"ff_ps"];
        }
    }

    if (g_state.needs_pipeline_update) {
        id<MTLFunction> vsFunc = nil;
        id<MTLFunction> psFunc = nil;

        if (g_state.vertex_shader_handle && g_state.pixel_shader_handle) {
            auto vs_it = g_shaders.find(g_state.vertex_shader_handle);
            auto ps_it = g_shaders.find(g_state.pixel_shader_handle);
            if (vs_it != g_shaders.end() && ps_it != g_shaders.end() &&
                vs_it->second.function && ps_it->second.function) {
                vsFunc = vs_it->second.function;
                psFunc = ps_it->second.function;
            }
        }

        if (!vsFunc && g_ff_vs_func && g_state.vertex_decl_handle) {
            vsFunc = g_ff_vs_func;
            psFunc = g_ff_ps_func;
            fprintf(stderr, "[d3d9] Using fixed-function fallback shaders\n");
        }

        if (vsFunc && psFunc) {
            @autoreleasepool {
                MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
                pdesc.vertexFunction = vsFunc;
                pdesc.fragmentFunction = psFunc;

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

                if (g_state.depth_stencil) {
                    auto ds_it = g_textures.find(g_state.depth_stencil);
                    if (ds_it != g_textures.end() && ds_it->second.texture) {
                        MTLPixelFormat dsFmt = ds_it->second.texture.pixelFormat;
                        if (dsFmt == MTLPixelFormatDepth32Float_Stencil8 || dsFmt == MTLPixelFormatDepth24Unorm_Stencil8)
                            pdesc.depthAttachmentPixelFormat = dsFmt;
                        else
                            pdesc.depthAttachmentPixelFormat = dsFmt;
                    }
                }

                apply_blend_to_pipeline(pdesc);

                pdesc.vertexDescriptor = build_vertex_descriptor();

                NSError* error = nil;
                g_state.current_pipeline = [g_device newRenderPipelineStateWithDescriptor:pdesc error:&error];
                if (error || !g_state.current_pipeline) {
                    fprintf(stderr, "[d3d9] Pipeline error: %s\n",
                        error ? [[error localizedDescription] UTF8String] : "unknown");
                } else {
                    fprintf(stderr, "[d3d9] Pipeline created OK (FF=%d)\n", vsFunc == g_ff_vs_func);
                }
                g_state.needs_pipeline_update = false;
            }
        }
    }

    if (g_state.current_pipeline) {
        [g_state.encoder setRenderPipelineState:g_state.current_pipeline];
    }

    apply_render_state_to_encoder();
    apply_sampler_states_to_encoder();

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
    if (!g_state.encoder) { fprintf(stderr, "[d3d9] draw: no encoder!\n"); return -1; }
    if (!g_state.current_pipeline) { fprintf(stderr, "[d3d9] draw: no pipeline!\n"); return -1; }

    uint32_t vertex_count = prim_vertex_count(params->primitive_type, params->primitive_count);
    fprintf(stderr, "[d3d9] draw: prim=%u count=%u verts=%u start=%u pipeline=%p vb0=%llu\n",
        params->primitive_type, params->primitive_count, vertex_count, params->start_vertex,
        g_state.current_pipeline, g_state.vertex_buffer_handles[0]);
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
    fprintf(stderr, "[d3d9] map: handle=%llu\n", (unsigned long long)params->resource_handle);
    auto it = g_buffers.find(params->resource_handle);
    if (it != g_buffers.end() && it->second.buffer) {
        params->out_data = (uint64_t)(uintptr_t)[it->second.buffer contents];
        params->out_row_pitch = (uint32_t)it->second.size;
        fprintf(stderr, "[d3d9] map: OK data=%llu size=%u\n", (unsigned long long)params->out_data, params->out_row_pitch);
        return 0;
    }
    fprintf(stderr, "[d3d9] map: handle not found\n");
    params->out_data = 0;
    return -1;
}

static NTSTATUS d3d9_upload_buffer(void* p) {
    auto* params = (struct d3d9_upload_params*)p;
    const void* src = (const void*)(uintptr_t)params->data;
    fprintf(stderr, "[d3d9] upload: handle=%llu data=%p size=%u\n",
            (unsigned long long)params->resource_handle, src, params->size);
    auto it = g_buffers.find(params->resource_handle);
    if (it == g_buffers.end() || !it->second.buffer) {
        fprintf(stderr, "[d3d9] upload: handle not found\n");
        return -1;
    }
    void* dst = (void*)[it->second.buffer contents];
    if (dst && src && params->size > 0 && params->size <= it->second.size) {
        memcpy(dst, src, params->size);
        fprintf(stderr, "[d3d9] upload: copied %u bytes to Metal buffer\n", params->size);
    }
    return 0;
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
    d3d9_upload_buffer,
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
    d3d9_upload_buffer,
};

NTSTATUS __attribute__((visibility("default"))) __wine_unix_lib_init(void) { return 0; }

}
