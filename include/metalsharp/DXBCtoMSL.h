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
