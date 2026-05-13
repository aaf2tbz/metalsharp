/// @file DXBCtoMSL.h
/// @brief DXBC bytecode to Metal Shading Language source translation.
///
/// Fallback shader translation path that converts parsed DXBC instructions directly
/// into MSL source code. Maps HLSL semantic names (SV_POSITION, TEXCOORD, etc.) to
/// Metal attribute bindings, translates DXBC opcodes to equivalent MSL operations, and
/// produces compilable MSL source. This path is used when Apple's IRConverter dylib is
/// unavailable or when processing legacy SM4.0/SM5.0 shaders without DXIL payloads.

#pragma once

#include <metalsharp/DXBCParser.h>
#include <string>

namespace metalsharp {

class DXBCtoMSL {
public:
    static std::string translate(const ParsedDXBC& dxbc);

private:
    static const char* semanticToMSL(const std::string& semantic, uint32_t index, uint32_t componentType);
    static std::string operandToString(const DXBCOperand& operand, uint32_t componentType);
    static std::string opcodeToMSL(uint32_t opcode);
};

}
