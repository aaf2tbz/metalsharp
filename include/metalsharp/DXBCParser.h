/// @file DXBCParser.h
/// @brief DXBC container and bytecode parser for legacy Direct3D shader blobs.
///
/// Parses the DXBC container format used by D3D10–D3D11 shader bytecode, extracting
/// the shader type (VS/PS/GS/HS/DS/CS), version, input/output signatures with semantic
/// names, and decoded instruction streams. The ParsedDXBC result feeds into DXBCtoMSL
/// for source-level translation or into IRConverterBridge when the container embeds
/// a DXIL payload. Handles operand decoding including index dimensions and write masks.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace metalsharp {

enum class DXBCShaderType : uint32_t {
    Pixel = 0,
    Vertex = 1,
    Geometry = 2,
    Hull = 3,
    Domain = 4,
    Compute = 5,
};

struct DXBCOperand {
    uint32_t type;
    uint32_t indexDimension;
    uint32_t indices[3];
    uint32_t mask;
};

struct DXBCInstruction {
    uint32_t opcode;
    uint32_t length;
    DXBCOperand operands[4];
    uint32_t operandCount;
    uint32_t immInt32;
    float immFloat;
};

struct DXBCSignatureElement {
    uint32_t semanticNameOffset;
    uint32_t semanticIndex;
    uint32_t systemValueType;
    uint32_t componentType;
    uint32_t registerIndex;
    uint32_t mask;
    std::string semanticName;
};

struct ParsedDXBC {
    DXBCShaderType shaderType;
    uint32_t majorVersion;
    uint32_t minorVersion;
    std::vector<DXBCInstruction> instructions;
    std::vector<DXBCSignatureElement> inputSignature;
    std::vector<DXBCSignatureElement> outputSignature;
};

class DXBCParser {
  public:
    static bool parse(const uint8_t* data, size_t size, ParsedDXBC& out);

  private:
    static bool parseContainer(const uint8_t* data, size_t size, ParsedDXBC& out);
    static bool parseBytecode(const uint32_t* tokens, size_t tokenCount, ParsedDXBC& out);
    static bool parseSignature(const uint8_t* chunkData, size_t chunkSize, std::vector<DXBCSignatureElement>& out,
                               const uint8_t* containerBase);
    static bool parseOperand(const uint32_t* tokens, size_t maxTokens, DXBCOperand& operand, size_t& tokensConsumed);
};

} // namespace metalsharp
