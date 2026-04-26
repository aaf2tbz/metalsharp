#include <metalsharp/MetalBackend.h>
#include <metalsharp/FormatTranslation.h>
#include <d3d/D3D11.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

uint32_t dxgiFormatToMetal(DXGITranslation format) {
    switch (format) {
        case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM:      return MTLPixelFormatRGBA8Unorm;
        case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return MTLPixelFormatRGBA8Unorm_sRGB;
        case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM:       return MTLPixelFormatBGRA8Unorm;
        case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:  return MTLPixelFormatBGRA8Unorm_sRGB;
        case DXGITranslation::DXGI_FORMAT_R16G16B16A16_FLOAT:   return MTLPixelFormatRGBA16Float;
        case DXGITranslation::DXGI_FORMAT_R32G32B32A32_FLOAT:   return MTLPixelFormatRGBA32Float;
        case DXGITranslation::DXGI_FORMAT_R32_FLOAT:            return MTLPixelFormatR32Float;
        case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:    return MTLPixelFormatDepth24Unorm_Stencil8;
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT:            return MTLPixelFormatDepth32Float;
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return MTLPixelFormatDepth32Float_Stencil8;
        case DXGITranslation::DXGI_FORMAT_BC1_UNORM:            return MTLPixelFormatBC1_RGBA;
        case DXGITranslation::DXGI_FORMAT_BC2_UNORM:            return MTLPixelFormatBC2_RGBA;
        case DXGITranslation::DXGI_FORMAT_BC3_UNORM:            return MTLPixelFormatBC3_RGBA;
        case DXGITranslation::DXGI_FORMAT_BC4_UNORM:            return MTLPixelFormatBC4_RUnorm;
        case DXGITranslation::DXGI_FORMAT_BC5_UNORM:            return MTLPixelFormatBC5_RGUnorm;
        case DXGITranslation::DXGI_FORMAT_BC6H_SF16:            return MTLPixelFormatBC6H_RGBFloat;
        case DXGITranslation::DXGI_FORMAT_BC7_UNORM:            return MTLPixelFormatBC7_RGBAUnorm;
        default: return MTLPixelFormatInvalid;
    }
}

uint32_t dxgiFormatToBytesPerPixel(DXGITranslation format) {
    switch (format) {
        case DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM:       return 4;
        case DXGITranslation::DXGI_FORMAT_R16G16B16A16_FLOAT:   return 8;
        case DXGITranslation::DXGI_FORMAT_R32G32B32A32_FLOAT:   return 16;
        case DXGITranslation::DXGI_FORMAT_R32_FLOAT:            return 4;
        case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:    return 4;
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT:            return 4;
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return 8;
        default: return 0;
    }
}

bool dxgiFormatIsDepth(DXGITranslation format) {
    switch (format) {
        case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT:
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return true;
        default: return false;
    }
}

bool dxgiFormatIsStencil(DXGITranslation format) {
    switch (format) {
        case DXGITranslation::DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGITranslation::DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return true;
        default: return false;
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
        default: return false;
    }
}

struct MetalSampler::Impl {
    id<MTLSamplerState> sampler = nil;
};

MetalSampler::MetalSampler() : m_impl(new Impl()) {}
MetalSampler::~MetalSampler() { delete m_impl; }

static MTLSamplerAddressMode translateAddressMode(UINT mode) {
    switch (mode) {
        case 1: return MTLSamplerAddressModeRepeat;
        case 2: return MTLSamplerAddressModeMirrorRepeat;
        case 3: return MTLSamplerAddressModeClampToEdge;
        case 4: return MTLSamplerAddressModeClampToZero;
        case 5: return MTLSamplerAddressModeMirrorClampToEdge;
        default: return MTLSamplerAddressModeClampToEdge;
    }
}

static MTLSamplerMinMagFilter translateMinMagFilter(UINT filter) {
    bool isPoint = (filter == D3D11_FILTER_MIN_MAG_MIP_POINT ||
                    filter == D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR ||
                    filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT);
    return isPoint ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

static MTLSamplerMipFilter translateMipFilter(UINT filter) {
    bool isPoint = (filter == D3D11_FILTER_MIN_MAG_MIP_POINT ||
                    filter == D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT ||
                    filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT ||
                    filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT);
    return isPoint ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
}

bool MetalSampler::init(MetalDevice& device, uint32_t filter, uint32_t addressU, uint32_t addressV, uint32_t addressW, float mipLodBias, uint32_t maxAnisotropy, uint32_t comparisonFunc) {
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
            case 1: desc.compareFunction = MTLCompareFunctionNever; break;
            case 2: desc.compareFunction = MTLCompareFunctionLess; break;
            case 3: desc.compareFunction = MTLCompareFunctionEqual; break;
            case 4: desc.compareFunction = MTLCompareFunctionLessEqual; break;
            case 5: desc.compareFunction = MTLCompareFunctionGreater; break;
            case 6: desc.compareFunction = MTLCompareFunctionNotEqual; break;
            case 7: desc.compareFunction = MTLCompareFunctionGreaterEqual; break;
            case 8: desc.compareFunction = MTLCompareFunctionAlways; break;
        }
    }

    m_impl->sampler = [mtlDevice newSamplerStateWithDescriptor:desc];
    return m_impl->sampler != nil;
}

MetalSampler* MetalSampler::create(MetalDevice& device, uint32_t filter, uint32_t addressU, uint32_t addressV, uint32_t addressW, float mipLodBias, uint32_t maxAnisotropy, uint32_t comparisonFunc) {
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

}
