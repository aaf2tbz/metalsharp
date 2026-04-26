#include <metalsharp/MetalBackend.h>
#include <metalsharp/FormatTranslation.h>
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

}
