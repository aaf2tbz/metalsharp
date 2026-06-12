#pragma once

#include "dxil_ir.hpp"
#include "dxil_container.hpp"
#include <string>
#include <sstream>
#include <optional>
#include <vector>

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

enum class MSLVertexTableIndexingMode : uint32_t {
    CompactBySlotMask = 0,
    RawSlot = 1,
};

struct MSLVertexInputElement {
    uint32_t shader_register = 0;
    uint32_t table_index = 0;
    uint32_t input_slot = 0;
    MSLVertexTableIndexingMode table_indexing_mode =
        MSLVertexTableIndexingMode::CompactBySlotMask;
    bool system_value = false;
};

struct MSLLoweringOptions {
    std::vector<MSLVertexInputElement> vertex_inputs;
};

inline uint32_t MSLResolveVertexInputTableIndex(uint32_t shader_register,
                                                const MSLLoweringOptions &options) {
    for (const auto &input : options.vertex_inputs) {
        if (!input.system_value && input.shader_register == shader_register) {
            if (input.table_indexing_mode == MSLVertexTableIndexingMode::RawSlot)
                return input.input_slot;
            return input.table_index;
        }
    }
    return shader_register < 16 ? shader_register : 0;
}

inline std::string MSLVertexPullExpression(uint32_t shader_register,
                                           const MSLLoweringOptions &options) {
    uint32_t table_index = MSLResolveVertexInputTableIndex(shader_register, options);
    if (table_index >= 16)
        table_index = 0;
    return "m12_load_vertex_attr(" + std::to_string(table_index) +
           ", vid, buf16, buf" + std::to_string(table_index) + ")";
}

class MSLLowering {
public:
    static std::optional<TypedMSLShader> lower(const LLVMModule &module,
                                                const DxilParsedShader &shader,
                                                const MSLLoweringOptions &options = {});

private:
    static std::string emitTypedDecl(const std::string &name, const MSLType &type);
    static std::string defaultValueForType(const MSLType &type);
};

}
