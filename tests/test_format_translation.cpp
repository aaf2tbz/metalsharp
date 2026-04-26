#include <metalsharp/FormatTranslation.h>
#include <cstdio>
#include <cassert>

int main() {
    using namespace metalsharp;

    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM) != 0);
    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_B8G8R8A8_UNORM) != 0);
    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_D32_FLOAT) != 0);
    assert(dxgiFormatToMetal(DXGITranslation::DXGI_FORMAT_UNKNOWN) == 0);

    assert(dxgiFormatIsDepth(DXGITranslation::DXGI_FORMAT_D32_FLOAT));
    assert(!dxgiFormatIsDepth(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM));

    assert(dxgiFormatIsCompressed(DXGITranslation::DXGI_FORMAT_BC1_UNORM));
    assert(!dxgiFormatIsCompressed(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM));

    assert(dxgiFormatToBytesPerPixel(DXGITranslation::DXGI_FORMAT_R8G8B8A8_UNORM) == 4);
    assert(dxgiFormatToBytesPerPixel(DXGITranslation::DXGI_FORMAT_R16G16B16A16_FLOAT) == 8);

    printf("PASS: Format translation\n");
    return 0;
}
