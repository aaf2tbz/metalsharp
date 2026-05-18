/// @file Sampler.mm
/// @brief Metal sampler creation — MTLSamplerState for texture filtering and addressing.
///
/// Objective-C++ implementation wrapping MTLSamplerState creation. Translates D3D11
/// sampler descriptions (filter mode, address mode, LOD, comparison) to Metal sampler
/// descriptor configuration.

#include <d3d/D3D11.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <metalsharp/FormatTranslation.h>
#include <metalsharp/MetalBackend.h>

namespace metalsharp {

uint32_t dxgiFormatToMetal(DXGITranslation format) {
    switch (format) {
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_FLOAT:
        return MTLPixelFormatRGBA32Float;
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_UINT:
        return MTLPixelFormatRGBA32Uint;
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_SINT:
        return MTLPixelFormatRGBA32Sint;

    case DXGITranslation::DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32_UINT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32_SINT:
        return MTLPixelFormatInvalid;

    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_FLOAT:
        return MTLPixelFormatRGBA16Float;
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_UNORM:
        return MTLPixelFormatRGBA16Unorm;
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_UINT:
        return MTLPixelFormatRGBA16Uint;
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_SNORM:
        return MTLPixelFormatRGBA16Snorm;
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_SINT:
        return MTLPixelFormatRGBA16Sint;
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_UNORM_SRGB:
        return MTLPixelFormatRGBA16Float;

    case DXGITranslation::DXGI_FORMAT_R32G32_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R32G32_FLOAT:
        return MTLPixelFormatRG32Float;
    case DXGITranslation::DXGI_FORMAT_R32G32_UINT:
        return MTLPixelFormatRG32Uint;
    case DXGITranslation::DXGI_FORMAT_R32G32_SINT:
        return MTLPixelFormatRG32Sint;

    case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return MTLPixelFormatDepth32Float_Stencil8;
    case DXGITranslation::DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return MTLPixelFormatDepth32Float_Stencil8;
    case DXGITranslation::DXGI_FORMAT_R32G8X24_TYPELESS:
        return MTLPixelFormatDepth32Float_Stencil8;

    case DXGITranslation::DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R10G10B10A2_UNORM:
        return MTLPixelFormatRGB10A2Unorm;
    case DXGITranslation::DXGI_FORMAT_R10G10B10A2_UINT:
        return MTLPixelFormatRGB10A2Uint;
    case DXGITranslation::DXGI_FORMAT_R10G10B10A2_TYPELESS_COMPAT:
        return MTLPixelFormatRGB10A2Unorm;

    case DXGITranslation::DXGI_FORMAT_R11G11B10_FLOAT:
        return MTLPixelFormatRG11B10Float;

    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM:
        return MTLPixelFormatRGBA8Unorm;
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UINT:
        return MTLPixelFormatRGBA8Uint;
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_SNORM:
        return MTLPixelFormatRGBA8Snorm;
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_SINT:
        return MTLPixelFormatRGBA8Sint;

    case DXGITranslation::DXGI_FORMAT_R16G16_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R16G16_FLOAT:
        return MTLPixelFormatRG16Float;
    case DXGITranslation::DXGI_FORMAT_R16G16_UNORM:
        return MTLPixelFormatRG16Unorm;
    case DXGITranslation::DXGI_FORMAT_R16G16_UINT:
        return MTLPixelFormatRG16Uint;
    case DXGITranslation::DXGI_FORMAT_R16G16_SNORM:
        return MTLPixelFormatRG16Snorm;
    case DXGITranslation::DXGI_FORMAT_R16G16_SINT:
        return MTLPixelFormatRG16Sint;

    case DXGITranslation::DXGI_FORMAT_R32_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R32_FLOAT:
        return MTLPixelFormatR32Float;
    case DXGITranslation::DXGI_FORMAT_R32_UINT:
        return MTLPixelFormatR32Uint;
    case DXGITranslation::DXGI_FORMAT_R32_SINT:
        return MTLPixelFormatR32Sint;

    case DXGITranslation::DXGI_FORMAT_R24G8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:
        return MTLPixelFormatDepth24Unorm_Stencil8;
    case DXGITranslation::DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return MTLPixelFormatDepth24Unorm_Stencil8;

    case DXGITranslation::DXGI_FORMAT_R8G8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R8G8_UNORM:
        return MTLPixelFormatRG8Unorm;
    case DXGITranslation::DXGI_FORMAT_R8G8_UINT:
        return MTLPixelFormatRG8Uint;
    case DXGITranslation::DXGI_FORMAT_R8G8_SNORM:
        return MTLPixelFormatRG8Snorm;
    case DXGITranslation::DXGI_FORMAT_R8G8_SINT:
        return MTLPixelFormatRG8Sint;

    case DXGITranslation::DXGI_FORMAT_R16_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R16_FLOAT:
        return MTLPixelFormatR16Float;
    case DXGITranslation::DXGI_FORMAT_D16_UNORM:
        return MTLPixelFormatDepth16Unorm;
    case DXGITranslation::DXGI_FORMAT_R16_UNORM:
        return MTLPixelFormatR16Unorm;
    case DXGITranslation::DXGI_FORMAT_R16_UINT:
        return MTLPixelFormatR16Uint;
    case DXGITranslation::DXGI_FORMAT_R16_SNORM:
        return MTLPixelFormatR16Snorm;
    case DXGITranslation::DXGI_FORMAT_R16_SINT:
        return MTLPixelFormatR16Sint;

    case DXGITranslation::DXGI_FORMAT_R8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R8_UNORM:
        return MTLPixelFormatR8Unorm;
    case DXGITranslation::DXGI_FORMAT_R8_UINT:
        return MTLPixelFormatR8Uint;
    case DXGITranslation::DXGI_FORMAT_R8_SNORM:
        return MTLPixelFormatR8Snorm;
    case DXGITranslation::DXGI_FORMAT_R8_SINT:
        return MTLPixelFormatR8Sint;
    case DXGITranslation::DXGI_FORMAT_A8_UNORM:
        return MTLPixelFormatA8Unorm;

    case DXGITranslation::DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        return MTLPixelFormatRGB9E5Float;

    case DXGITranslation::DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGITranslation::DXGI_FORMAT_G8R8_G8B8_UNORM:
        return MTLPixelFormatBGRA8Unorm;

    case DXGITranslation::DXGI_FORMAT_BC1_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC1_UNORM:
        return MTLPixelFormatBC1_RGBA;
    case DXGITranslation::DXGI_FORMAT_BC1_UNORM_SRGB:
        return MTLPixelFormatBC1_RGBA_sRGB;
    case DXGITranslation::DXGI_FORMAT_BC2_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC2_UNORM:
        return MTLPixelFormatBC2_RGBA;
    case DXGITranslation::DXGI_FORMAT_BC2_UNORM_SRGB:
        return MTLPixelFormatBC2_RGBA_sRGB;
    case DXGITranslation::DXGI_FORMAT_BC3_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC3_UNORM:
        return MTLPixelFormatBC3_RGBA;
    case DXGITranslation::DXGI_FORMAT_BC3_UNORM_SRGB:
        return MTLPixelFormatBC3_RGBA_sRGB;
    case DXGITranslation::DXGI_FORMAT_BC4_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC4_UNORM:
        return MTLPixelFormatBC4_RUnorm;
    case DXGITranslation::DXGI_FORMAT_BC4_SNORM:
        return MTLPixelFormatBC4_RSnorm;
    case DXGITranslation::DXGI_FORMAT_BC5_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC5_UNORM:
        return MTLPixelFormatBC5_RGUnorm;
    case DXGITranslation::DXGI_FORMAT_BC5_SNORM:
        return MTLPixelFormatBC5_RGSnorm;
    case DXGITranslation::DXGI_FORMAT_BC6H_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC6H_UF16:
        return MTLPixelFormatBC6H_RGBUfloat;
    case DXGITranslation::DXGI_FORMAT_BC6H_SF16:
        return MTLPixelFormatBC6H_RGBFloat;
    case DXGITranslation::DXGI_FORMAT_BC7_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_BC7_UNORM:
        return MTLPixelFormatBC7_RGBAUnorm;
    case DXGITranslation::DXGI_FORMAT_BC7_UNORM_SRGB:
        return MTLPixelFormatBC7_RGBAUnorm_sRGB;

    case DXGITranslation::DXGI_FORMAT_B5G6R5_UNORM:
        return MTLPixelFormatB5G6R5Unorm;
    case DXGITranslation::DXGI_FORMAT_B5G5R5A1_UNORM:
        return MTLPixelFormatBGR5A1Unorm;
    case DXGITranslation::DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM:
        return MTLPixelFormatBGRA8Unorm;
    case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return MTLPixelFormatBGRA8Unorm_sRGB;
    case DXGITranslation::DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGITranslation::DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return MTLPixelFormatBGRA8Unorm;
    case DXGITranslation::DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return MTLPixelFormatBGRA8Unorm_sRGB;
    case DXGITranslation::DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return MTLPixelFormatBGR10A2Unorm;

    default:
        return MTLPixelFormatInvalid;
    }
}

uint32_t dxgiFormatToBytesPerPixel(DXGITranslation format) {
    switch (format) {
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return 16;
    case DXGITranslation::DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32_UINT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32_SINT:
    case DXGITranslation::DXGI_FORMAT_R32G32B32_TYPELESS:
        return 12;
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGITranslation::DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R32G32_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R32G32_UINT:
    case DXGITranslation::DXGI_FORMAT_R32G32_SINT:
    case DXGITranslation::DXGI_FORMAT_R32G32_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return 8;
    case DXGITranslation::DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGITranslation::DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGITranslation::DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGITranslation::DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGITranslation::DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGITranslation::DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGITranslation::DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R16G16_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R16G16_UNORM:
    case DXGITranslation::DXGI_FORMAT_R16G16_UINT:
    case DXGITranslation::DXGI_FORMAT_R16G16_SNORM:
    case DXGITranslation::DXGI_FORMAT_R16G16_SINT:
    case DXGITranslation::DXGI_FORMAT_R16G16_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGITranslation::DXGI_FORMAT_R32_FLOAT:
    case DXGITranslation::DXGI_FORMAT_R32_UINT:
    case DXGITranslation::DXGI_FORMAT_R32_SINT:
    case DXGITranslation::DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGITranslation::DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGITranslation::DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return 4;
    case DXGITranslation::DXGI_FORMAT_R8G8_UNORM:
    case DXGITranslation::DXGI_FORMAT_R8G8_UINT:
    case DXGITranslation::DXGI_FORMAT_R8G8_SNORM:
    case DXGITranslation::DXGI_FORMAT_R8G8_SINT:
    case DXGITranslation::DXGI_FORMAT_R8G8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_R16_FLOAT:
    case DXGITranslation::DXGI_FORMAT_D16_UNORM:
    case DXGITranslation::DXGI_FORMAT_R16_UNORM:
    case DXGITranslation::DXGI_FORMAT_R16_UINT:
    case DXGITranslation::DXGI_FORMAT_R16_SNORM:
    case DXGITranslation::DXGI_FORMAT_R16_SINT:
    case DXGITranslation::DXGI_FORMAT_R16_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_B5G6R5_UNORM:
        return 2;
    case DXGITranslation::DXGI_FORMAT_R8_UNORM:
    case DXGITranslation::DXGI_FORMAT_R8_UINT:
    case DXGITranslation::DXGI_FORMAT_R8_SNORM:
    case DXGITranslation::DXGI_FORMAT_R8_SINT:
    case DXGITranslation::DXGI_FORMAT_R8_TYPELESS:
    case DXGITranslation::DXGI_FORMAT_A8_UNORM:
        return 1;
    default:
        return 0;
    }
}

bool dxgiFormatIsDepth(DXGITranslation format) {
    switch (format) {
    case DXGITranslation::DXGI_FORMAT_D16_UNORM:
    case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return true;
    default:
        return false;
    }
}

bool dxgiFormatIsStencil(DXGITranslation format) {
    switch (format) {
    case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return true;
    default:
        return false;
    }
}

bool dxgiFormatIsCompressed(DXGITranslation format) {
    switch (format) {
    case DXGITranslation::DXGI_FORMAT_BC1_UNORM:
    case DXGITranslation::DXGI_FORMAT_BC2_UNORM:
    case DXGITranslation::DXGI_FORMAT_BC3_UNORM:
    case DXGITranslation::DXGI_FORMAT_BC4_UNORM:
    case DXGITranslation::DXGI_FORMAT_BC5_UNORM:
    case DXGITranslation::DXGI_FORMAT_BC6H_SF16:
    case DXGITranslation::DXGI_FORMAT_BC7_UNORM:
        return true;
    default:
        return false;
    }
}

struct MetalSampler::Impl {
    id<MTLSamplerState> sampler = nil;
};

MetalSampler::MetalSampler() : m_impl(new Impl()) {}
MetalSampler::~MetalSampler() {
    delete m_impl;
}

static MTLSamplerAddressMode translateAddressMode(UINT mode) {
    switch (mode) {
    case 1:
        return MTLSamplerAddressModeRepeat;
    case 2:
        return MTLSamplerAddressModeMirrorRepeat;
    case 3:
        return MTLSamplerAddressModeClampToEdge;
    case 4:
        return MTLSamplerAddressModeClampToZero;
    case 5:
        return MTLSamplerAddressModeMirrorClampToEdge;
    default:
        return MTLSamplerAddressModeClampToEdge;
    }
}

static MTLSamplerMinMagFilter translateMinMagFilter(UINT filter) {
    bool isPoint = (filter == D3D11_FILTER_MIN_MAG_MIP_POINT || filter == D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR ||
                    filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT);
    return isPoint ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

static MTLSamplerMipFilter translateMipFilter(UINT filter) {
    bool isPoint =
        (filter == D3D11_FILTER_MIN_MAG_MIP_POINT || filter == D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT ||
         filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT || filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT);
    return isPoint ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
}

bool MetalSampler::init(MetalDevice& device, uint32_t filter, uint32_t addressU, uint32_t addressV, uint32_t addressW,
                        float mipLodBias, uint32_t maxAnisotropy, uint32_t comparisonFunc) {
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device.nativeDevice();

    MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
    desc.minFilter = translateMinMagFilter(filter);
    desc.magFilter = translateMinMagFilter(filter);
    desc.mipFilter = translateMipFilter(filter);
    desc.sAddressMode = translateAddressMode(addressU);
    desc.tAddressMode = translateAddressMode(addressV);
    desc.rAddressMode = translateAddressMode(addressW);

    desc.maxAnisotropy = maxAnisotropy > 1 ? maxAnisotropy : 1;

    if (filter >= 0x80) {
        desc.compareFunction = MTLCompareFunctionAlways;
        switch (comparisonFunc) {
        case 1:
            desc.compareFunction = MTLCompareFunctionNever;
            break;
        case 2:
            desc.compareFunction = MTLCompareFunctionLess;
            break;
        case 3:
            desc.compareFunction = MTLCompareFunctionEqual;
            break;
        case 4:
            desc.compareFunction = MTLCompareFunctionLessEqual;
            break;
        case 5:
            desc.compareFunction = MTLCompareFunctionGreater;
            break;
        case 6:
            desc.compareFunction = MTLCompareFunctionNotEqual;
            break;
        case 7:
            desc.compareFunction = MTLCompareFunctionGreaterEqual;
            break;
        case 8:
            desc.compareFunction = MTLCompareFunctionAlways;
            break;
        }
    }

    m_impl->sampler = [mtlDevice newSamplerStateWithDescriptor:desc];
    return m_impl->sampler != nil;
}

MetalSampler* MetalSampler::create(MetalDevice& device, uint32_t filter, uint32_t addressU, uint32_t addressV,
                                   uint32_t addressW, float mipLodBias, uint32_t maxAnisotropy,
                                   uint32_t comparisonFunc) {
    auto* s = new MetalSampler();
    if (!s->init(device, filter, addressU, addressV, addressW, mipLodBias, maxAnisotropy, comparisonFunc)) {
        delete s;
        return nullptr;
    }
    return s;
}

void* MetalSampler::nativeSamplerState() const {
    return (__bridge void*)m_impl->sampler;
}

} // namespace metalsharp
