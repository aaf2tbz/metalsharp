#include <d3d/D3D12.h>
#include <cstdio>
#include <cstring>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

int main() {
    printf("=== Tiled Resources & Sampler Feedback Tests ===\n\n");

    {
        printf("--- D3D12_TILED_RESOURCE_COORDINATE ---\n");
        D3D12_TILED_RESOURCE_COORDINATE coord{};
        coord.X = 4; coord.Y = 2; coord.Z = 1; coord.Subresource = 3;
        CHECK(coord.X == 4, "X coordinate set");
        CHECK(coord.Y == 2, "Y coordinate set");
        CHECK(coord.Z == 1, "Z coordinate set");
        CHECK(coord.Subresource == 3, "Subresource set");
    }

    {
        printf("\n--- D3D12_TILE_REGION_SIZE ---\n");
        D3D12_TILE_REGION_SIZE region{};
        region.NumTiles = 16; region.Width = 4; region.Height = 4; region.Depth = 1; region.bUseBox = TRUE;
        CHECK(region.NumTiles == 16, "NumTiles set");
        CHECK(region.Width == 4, "Width set");
        CHECK(region.Height == 4, "Height set");
        CHECK(region.Depth == 1, "Depth set");
        CHECK(region.bUseBox == TRUE, "bUseBox set");
        CHECK(region.Width * region.Height * region.Depth == 16, "Box volume matches NumTiles");
    }

    {
        printf("\n--- D3D12_PACKED_MIP_INFO ---\n");
        D3D12_PACKED_MIP_INFO info{};
        info.NumStandardMips = 8; info.NumPackedMips = 2;
        info.NumTilesForPackedMips = 1; info.StartTileIndexInOverallResource = 64;
        CHECK(info.NumStandardMips == 8, "NumStandardMips set");
        CHECK(info.NumPackedMips == 2, "NumPackedMips set");
        CHECK(info.NumTilesForPackedMips == 1, "NumTilesForPackedMips set");
        CHECK(info.StartTileIndexInOverallResource == 64, "StartTileIndex set");
    }

    {
        printf("\n--- D3D12_TILE_SHAPE ---\n");
        D3D12_TILE_SHAPE shape{};
        shape.WidthInTexels = 128; shape.HeightInTexels = 128; shape.DepthInTexels = 1;
        CHECK(shape.WidthInTexels == 128, "Tile width is 128 texels");
        CHECK(shape.HeightInTexels == 128, "Tile height is 128 texels");
        CHECK(shape.DepthInTexels == 1, "Tile depth is 1");
        CHECK(shape.WidthInTexels * shape.HeightInTexels * shape.DepthInTexels == 16384, "Tile covers 16384 texels");
    }

    {
        printf("\n--- D3D12_SUBRESOURCE_TILING ---\n");
        D3D12_SUBRESOURCE_TILING tiling{};
        tiling.WidthInTiles = 16; tiling.HeightInTiles = 8; tiling.DepthInTiles = 1;
        tiling.StartTileIndexInOverallResource = 0; tiling.NumTiles = 128;
        CHECK(tiling.WidthInTiles == 16, "WidthInTiles set");
        CHECK(tiling.HeightInTiles == 8, "HeightInTiles set");
        CHECK(tiling.DepthInTiles == 1, "DepthInTiles set");
        CHECK(tiling.NumTiles == 128, "NumTiles set");
        CHECK(tiling.WidthInTiles * tiling.HeightInTiles * tiling.DepthInTiles == 128, "Tile count matches dimensions");
    }

    {
        printf("\n--- Tiled Resource Constants ---\n");
        CHECK(D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES == 65536, "Tile size is 64KB");
        CHECK(D3D12_RESOURCE_FLAG_ALLOW_TILED_RESOURCES == 0x40, "Tiled resource flag is 0x40");
        CHECK(D3D12_RESOURCE_FLAG_ALLOW_SAMPLER_FEEDBACK == 0x80, "Sampler feedback flag is 0x80");
        CHECK(D3D12_TILE_MAPPING_FLAG_NONE == 0, "Tile mapping flag none is 0");
        CHECK(D3D12_TILE_RANGE_FLAG_NONE == 0, "Tile range flag none is 0");
        CHECK(D3D12_TILE_RANGE_FLAG_NULL == 1, "Tile range flag null is 1");
        CHECK(D3D12_TILE_RANGE_FLAG_SKIP == 2, "Tile range flag skip is 2");
        CHECK(D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE == 4, "Tile range flag reuse is 4");
    }

    {
        printf("\n--- Sampler Feedback Constants ---\n");
        CHECK(D3D12_SAMPLER_FEEDBACK_TYPE_MIN_MIP_OPAQUE == 0, "Feedback type MIN_MIP_OPAQUE is 0");
        CHECK(D3D12_SAMPLER_FEEDBACK_TYPE_MIP_REGION_USED_OPAQUE == 1, "Feedback type MIP_REGION_USED_OPAQUE is 1");
        CHECK(D3D12_SAMPLER_FEEDBACK_TYPE_MIN_MIP == 2, "Feedback type MIN_MIP is 2");
        CHECK(D3D12_SAMPLER_FEEDBACK_TYPE_MIP_REGION_USED == 3, "Feedback type MIP_REGION_USED is 3");
    }

    {
        printf("\n--- D3D12_SAMPLER_FEEDBACK_DESC ---\n");
        D3D12_SAMPLER_FEEDBACK_DESC fbDesc{};
        fbDesc.pFeedbackTexture = nullptr;
        fbDesc.FeedbackType = D3D12_SAMPLER_FEEDBACK_TYPE_MIN_MIP;
        CHECK(fbDesc.FeedbackType == D3D12_SAMPLER_FEEDBACK_TYPE_MIN_MIP, "Feedback type MIN_MIP set");
        fbDesc.FeedbackType = D3D12_SAMPLER_FEEDBACK_TYPE_MIP_REGION_USED;
        CHECK(fbDesc.FeedbackType == D3D12_SAMPLER_FEEDBACK_TYPE_MIP_REGION_USED, "Feedback type MIP_REGION_USED set");
    }

    {
        printf("\n--- Tile Calculation: 2048x2048 Texture ---\n");
        UINT width = 2048, height = 2048;
        UINT tilesX = (width + 127) / 128;
        UINT tilesY = (height + 127) / 128;
        CHECK(tilesX == 16, "2048/128 = 16 tiles wide");
        CHECK(tilesY == 16, "2048/128 = 16 tiles tall");
        CHECK(tilesX * tilesY == 256, "Total 256 tiles for 2048x2048");

        UINT64 totalBytes = 256 * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
        CHECK(totalBytes == 256 * 65536, "256 tiles * 64KB = 16MB");
    }

    {
        printf("\n--- Tile Calculation: 4096x4096 Mipmap Chain ---\n");
        UINT totalMips = 13;
        UINT totalTiles = 0;
        for (UINT mip = 0; mip < totalMips; mip++) {
            UINT w = 4096 >> mip; if (w == 0) w = 1;
            UINT h = 4096 >> mip; if (h == 0) h = 1;
            UINT tw = (w + 127) / 128;
            UINT th = (h + 127) / 128;
            totalTiles += tw * th;
        }
        CHECK(totalTiles > 0, "Mipmap chain has tiles");
        CHECK(totalTiles == 1372, "4096x4096 13-mip chain = 1372 tiles");
    }

    {
        printf("\n--- Tile Calculation: Packed Mips Detection ---\n");
        UINT w = 256, h = 256;
        UINT mips = 9;
        UINT packedMips = 0;
        for (UINT i = 0; i < mips; i++) {
            if (w < 128 && h < 128) { packedMips = mips - i; break; }
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
        }
        CHECK(packedMips == 7, "256x256 with 9 mips: last 7 mips are packed (< 128x128)");
    }

    {
        printf("\n--- D3D12_TILE_RANGE_FLAGS Usage ---\n");
        D3D12_TILE_RANGE_FLAGS flags{};
        flags.Flags = D3D12_TILE_RANGE_FLAG_NONE;
        CHECK((flags.Flags & D3D12_TILE_RANGE_FLAG_NULL) == 0, "NONE flag doesn't include NULL");

        flags.Flags = D3D12_TILE_RANGE_FLAG_NULL;
        CHECK((flags.Flags & D3D12_TILE_RANGE_FLAG_NULL) != 0, "NULL flag set");

        flags.Flags = D3D12_TILE_RANGE_FLAG_SKIP;
        CHECK((flags.Flags & D3D12_TILE_RANGE_FLAG_SKIP) != 0, "SKIP flag set");

        flags.Flags = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
        CHECK((flags.Flags & D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE) != 0, "REUSE flag set");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
