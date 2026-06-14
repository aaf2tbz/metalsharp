#pragma once

#include "dxil_ir.hpp"
#include "dxil_container.hpp"
#include <string>
#include <sstream>
#include <optional>

namespace dxmt::dxil {

struct TypedMSLShader {
    std::string source;
    std::string entry_point;
    uint32_t tg_size[3] = {1, 1, 1};
    uint32_t unsupported_intrinsics = 0;
    uint32_t unsupported_opcodes = 0;
    uint32_t typed_value_count = 0;
    uint32_t auto_value_count = 0;
    std::vector<std::string> diagnostics;
};

class MSLLowering {
public:
    static std::optional<TypedMSLShader> lower(const LLVMModule &module,
                                                const DxilParsedShader &shader);

private:
    static std::string emitTypedDecl(const std::string &name, const MSLType &type);
    static std::string defaultValueForType(const MSLType &type);
};

}
