#include <cassert>
#include <cstdio>
#include <metalsharp/FormatTranslation.h>

int main() {
    using namespace metalsharp;

    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM) != 0);
    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM) != 0);
    assert(dxgiFormatToMetal(DXGI_FORMAT_D32_FLOAT) != 0);
    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_UNKNOWN) == 0);
    assert(static_cast<unsigned int>(DXGITranslation::DXGI_FORMAT_R16G16_UNORM) == 35);
    assert(dxgiFormatToMetal(static_cast<DXGITranslation>(35)) ==
           dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_R16G16_UNORM));
    assert(static_cast<unsigned int>(DXGI_FORMAT_D32_FLOAT) == 40);
    assert(dxgiDepthFormatToMetal(static_cast<DXGITranslation>(40)) == dxgiDepthFormatToMetal(DXGI_FORMAT_D32_FLOAT));
    assert(dxgiDepthFormatToMetal(DXGI_FORMAT_D32_FLOAT) != 0);
    assert(dxgiDepthFormatToMetal(DXGI_FORMAT_D32_FLOAT) != dxgiFormatToMetal(DXGI_FORMAT_D32_FLOAT));

    assert(dxgiFormatIsDepth(DXGI_FORMAT_D32_FLOAT));
    assert(!dxgiFormatIsDepth(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM));

    assert(dxgiFormatIsCompressed(DXGITranslation::DXGI_FORMAT_BC1_UNORM));
    assert(!dxgiFormatIsCompressed(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM));

    assert(dxgiFormatToBytesPerPixel(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM) == 4);
    assert(dxgiFormatToBytesPerPixel(DXGITranslation::DXGI_FORMAT_R16G16B16A16_FLOAT) == 8);

    printf("PASS: Format translation\n");
    return 0;
}
