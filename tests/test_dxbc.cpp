#include <metalsharp/DXBCParser.h>
#include <metalsharp/DXBCtoMSL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); return 1; } } while(0)

static void writeU32(std::vector<uint8_t>& out, uint32_t val) {
    out.push_back(val & 0xFF);
    out.push_back((val >> 8) & 0xFF);
    out.push_back((val >> 16) & 0xFF);
    out.push_back((val >> 24) & 0xFF);
}

static std::vector<uint8_t> buildMinimalDXBC() {
    std::vector<uint8_t> shaderChunk;

    uint32_t versionToken = 0xFFFF0050; // ps_5_0 (major=5, minor=0)
    uint32_t lengthToken = 3; // total tokens including version + length + ret
    uint32_t retToken = (22) | (1 << 24);

    writeU32(shaderChunk, versionToken);
    writeU32(shaderChunk, lengthToken);
    writeU32(shaderChunk, retToken);

    std::vector<uint8_t> chunk;
    const char* shdr = "SHDR";
    chunk.insert(chunk.end(), shdr, shdr + 4);
    writeU32(chunk, (uint32_t)shaderChunk.size());
    chunk.insert(chunk.end(), shaderChunk.begin(), shaderChunk.end());

    std::vector<uint8_t> dxbc;
    const char* magic = "DXBC";
    dxbc.insert(dxbc.end(), magic, magic + 4);
    dxbc.insert(dxbc.end(), 16, 0); // checksum
    writeU32(dxbc, 1); // version

    uint32_t chunkCount = 1;
    uint32_t chunkDataOffset = 32 + chunkCount * 4; // 36
    uint32_t totalSize = chunkDataOffset + (uint32_t)chunk.size();

    writeU32(dxbc, totalSize); // total file size
    writeU32(dxbc, chunkCount); // chunk count
    writeU32(dxbc, chunkDataOffset); // offset to first chunk

    dxbc.insert(dxbc.end(), chunk.begin(), chunk.end());

    return dxbc;
}

static std::vector<uint8_t> buildShaderWithMov() {
    std::vector<uint8_t> shaderChunk;

    uint32_t versionToken = 0xFFFE0050; // vs_5_0 (major=5, minor=0)
    uint32_t lengthToken = 12;
    uint32_t movToken = (90) | (3 << 24);
    uint32_t dstToken = (0) | (0xF << 12) | (0 << 20);
    uint32_t srcToken = (1) | (0xF << 12) | (0 << 20) | (0 << 22);
    uint32_t retToken = (22) | (1 << 24);

    writeU32(shaderChunk, versionToken);
    writeU32(shaderChunk, lengthToken);
    writeU32(shaderChunk, movToken);
    writeU32(shaderChunk, dstToken);
    writeU32(shaderChunk, srcToken);
    writeU32(shaderChunk, retToken);

    while (shaderChunk.size() / 4 < lengthToken) {
        writeU32(shaderChunk, 0);
    }

    std::vector<uint8_t> chunk;
    const char* shdr = "SHDR";
    chunk.insert(chunk.end(), shdr, shdr + 4);
    writeU32(chunk, (uint32_t)shaderChunk.size());
    chunk.insert(chunk.end(), shaderChunk.begin(), shaderChunk.end());

    std::vector<uint8_t> dxbc;
    const char* magic = "DXBC";
    dxbc.insert(dxbc.end(), magic, magic + 4);
    dxbc.insert(dxbc.end(), 16, 0); // checksum
    writeU32(dxbc, 1); // version

    uint32_t chunkCount = 1;
    uint32_t chunkDataOffset = 32 + chunkCount * 4;
    uint32_t totalSize = chunkDataOffset + (uint32_t)chunk.size();

    writeU32(dxbc, totalSize);
    writeU32(dxbc, chunkCount);
    writeU32(dxbc, chunkDataOffset);
    dxbc.insert(dxbc.end(), chunk.begin(), chunk.end());

    return dxbc;
}

int main() {
    int passed = 0;
    int total = 0;

    {
        total++;
        auto dxbc = buildMinimalDXBC();
        metalsharp::ParsedDXBC parsed;
        bool ok = metalsharp::DXBCParser::parse(dxbc.data(), dxbc.size(), parsed);
        CHECK(ok, "minimal DXBC: parse should succeed");
        CHECK(parsed.shaderType == metalsharp::DXBCShaderType::Pixel, "minimal DXBC: should be pixel shader");
        CHECK(parsed.majorVersion == 5, "minimal DXBC: major version should be 5");
        CHECK(parsed.minorVersion == 0, "minimal DXBC: minor version should be 0");
        passed++;
        printf("PASS: minimal DXBC container parse\n");
    }

    {
        total++;
        auto dxbc = buildMinimalDXBC();
        metalsharp::ParsedDXBC parsed;
        metalsharp::DXBCParser::parse(dxbc.data(), dxbc.size(), parsed);
        std::string msl = metalsharp::DXBCtoMSL::translate(parsed);
        CHECK(!msl.empty(), "MSL generation should produce output");
        CHECK(msl.find("fragment") != std::string::npos, "MSL should contain fragment shader");
        CHECK(msl.find("fragmentShader") != std::string::npos, "MSL should have fragmentShader entry");
        passed++;
        printf("PASS: MSL generation from minimal DXBC\n");
    }

    {
        total++;
        auto dxbc = buildShaderWithMov();
        metalsharp::ParsedDXBC parsed;
        bool ok = metalsharp::DXBCParser::parse(dxbc.data(), dxbc.size(), parsed);
        CHECK(ok, "MOV DXBC: parse should succeed");
        CHECK(parsed.shaderType == metalsharp::DXBCShaderType::Vertex, "MOV DXBC: should be vertex shader");
        passed++;
        printf("PASS: vertex shader DXBC parse\n");
    }

    {
        total++;
        uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF};
        metalsharp::ParsedDXBC parsed;
        bool ok = metalsharp::DXBCParser::parse(garbage, sizeof(garbage), parsed);
        CHECK(!ok, "garbage data should fail to parse");
        passed++;
        printf("PASS: garbage data rejected\n");
    }

    {
        total++;
        metalsharp::ParsedDXBC parsed;
        bool ok = metalsharp::DXBCParser::parse(nullptr, 0, parsed);
        CHECK(!ok, "null data should fail");
        passed++;
        printf("PASS: null data rejected\n");
    }

    printf("\n%d/%d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
