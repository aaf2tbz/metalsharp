#pragma once

#include <metalsharp/Platform.h>

namespace metalsharp {

enum class DXGITranslation {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
    DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    DXGI_FORMAT_R32_FLOAT = 41,
    DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
    DXGI_FORMAT_D32_FLOAT = 40,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
    DXGI_FORMAT_BC1_UNORM = 71,
    DXGI_FORMAT_BC2_UNORM = 74,
    DXGI_FORMAT_BC3_UNORM = 77,
    DXGI_FORMAT_BC4_UNORM = 80,
    DXGI_FORMAT_BC5_UNORM = 83,
    DXGI_FORMAT_BC6H_SF16 = 96,
    DXGI_FORMAT_BC7_UNORM = 98,
};

uint32_t dxgiFormatToMetal(DXGITranslation format);
uint32_t dxgiFormatToBytesPerPixel(DXGITranslation format);
bool dxgiFormatIsDepth(DXGITranslation format);
bool dxgiFormatIsStencil(DXGITranslation format);
bool dxgiFormatIsCompressed(DXGITranslation format);

}
