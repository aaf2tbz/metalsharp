#include <metalsharp/DXBCtoMSL.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace metalsharp {

const char* DXBCtoMSL::semanticToMSL(const std::string& semantic, uint32_t index, uint32_t componentType) {
    if (semantic == "POSITION") return "position";
    if (semantic == "SV_POSITION") return "position";
    if (semantic == "NORMAL") return "normal";
    if (semantic == "TEXCOORD") {
        static char buf[32];
        snprintf(buf, sizeof(buf), "texcoord%u", index);
        return buf;
    }
    if (semantic == "COLOR") {
        static char buf[32];
        snprintf(buf, sizeof(buf), "color%u", index);
        return buf;
    }
    if (semantic == "TANGENT") return "tangent";
    if (semantic == "BINORMAL") return "binormal";
    if (semantic == "BLENDWEIGHT") return "blendweight";
    if (semantic == "BLENDINDICES") return "blendindices";
    if (semantic == "SV_TARGET") {
        static char buf[32];
        snprintf(buf, sizeof(buf), "target%u", index);
        return buf;
    }
    if (semantic == "SV_Depth") return "depth_out";
    return "user";
}

static const char* componentTypeName(uint32_t type) {
    switch (type) {
        case 1: return "int";
        case 2: return "uint";
        case 3: return "float";
        default: return "float";
    }
}

std::string DXBCtoMSL::opcodeToMSL(uint32_t opcode) {
    switch (opcode) {
        case 0: return "+";
        case 1: return "-";
        case 2: return "*";
        case 4: return "mad";
        case 6: return "dot";
        case 7: return "dot4";
        case 8: return "min";
        case 9: return "max";
        case 10: return "lessThan";
        case 11: return "greaterThanEqual";
        case 12: return "equal";
        case 13: return "notEqual";
        case 15: return "mix";
        case 17: return "rsqrt";
        case 21: return "sqrt";
        case 26: return "exp2";
        case 27: return "log2";
        case 64: return "sample";
        case 65: return "sample_l";
        case 66: return "sample_b";
        case 67: return "sample_c";
        case 81: return "gather4";
        case 90: return "mov";
        case 100: return "+";
        case 104: return "*";
        case 22: return "int";
        case 24: return "float";
        case 82: return "dfdx";
        case 83: return "dfdy";
        case 84: return "discard";
        default: return "unknown";
    }
}

static std::string swizzleStr(uint32_t mask) {
    std::string s = ".";
    const char* comps = "xyzw";
    for (int i = 0; i < 4; ++i) {
        if (mask & (1 << i)) s += comps[i];
    }
    if (s.size() == 1) return "";
    if (s == ".xyzw") return "";
    return s;
}

std::string DXBCtoMSL::operandToString(const DXBCOperand& operand, uint32_t componentType) {
    char buf[128];
    switch (operand.type) {
        case 0: // temp
            if (operand.mask) snprintf(buf, sizeof(buf), "r%u%s", operand.indices[0], swizzleStr(operand.mask).c_str());
            else snprintf(buf, sizeof(buf), "r%u", operand.indices[0]);
            break;
        case 1: // input
            if (operand.mask) snprintf(buf, sizeof(buf), "input%u%s", operand.indices[0], swizzleStr(operand.mask).c_str());
            else snprintf(buf, sizeof(buf), "input%u", operand.indices[0]);
            break;
        case 2: // output
            if (operand.mask) snprintf(buf, sizeof(buf), "output%u%s", operand.indices[0], swizzleStr(operand.mask).c_str());
            else snprintf(buf, sizeof(buf), "output%u", operand.indices[0]);
            break;
        case 3: // indexable temp
            if (operand.mask) snprintf(buf, sizeof(buf), "x%u[%u]%s", operand.indices[0], operand.indices[1], swizzleStr(operand.mask).c_str());
            else snprintf(buf, sizeof(buf), "x%u[%u]", operand.indices[0], operand.indices[1]);
            break;
        case 4: // immediate32
            return std::to_string(operand.indices[0]);
        case 5: // immediate64
            return std::to_string(operand.indices[0]);
        case 6: // sampler
            snprintf(buf, sizeof(buf), "sampler%u", operand.indices[0]);
            break;
        case 7: // resource
            snprintf(buf, sizeof(buf), "texture%u", operand.indices[0]);
            break;
        case 8: // constant buffer
            if (operand.indexDimension >= 1)
                snprintf(buf, sizeof(buf), "cb%u[%u]", operand.indices[0], operand.indices[1]);
            else
                snprintf(buf, sizeof(buf), "cb%u", operand.indices[0]);
            break;
        case 9: // immediate constant buffer
            snprintf(buf, sizeof(buf), "icb[%u]", operand.indices[0]);
            break;
        default:
            snprintf(buf, sizeof(buf), "unk%u", operand.type);
            break;
    }
    return buf;
}

std::string DXBCtoMSL::translate(const ParsedDXBC& dxbc) {
    std::string src;

    const char* stageFn = "main0";
    const char* stageAttr = "";
    const char* returnPrefix = "float4";
    bool isCompute = false;

    switch (dxbc.shaderType) {
        case DXBCShaderType::Vertex:
            stageFn = "vertexShader";
            stageAttr = "vertex ";
            returnPrefix = "float4";
            break;
        case DXBCShaderType::Pixel:
            stageFn = "fragmentShader";
            stageAttr = "fragment ";
            returnPrefix = "float4";
            break;
        case DXBCShaderType::Compute:
            stageFn = "computeShader";
            stageAttr = "kernel ";
            returnPrefix = "void";
            isCompute = true;
            break;
        default:
            stageFn = "main0";
            break;
    }

    src += "#include <metal_stdlib>\nusing namespace metal;\n\n";

    struct VarDecl {
        uint32_t regIndex;
        std::string name;
        std::string type;
        std::string attribute;
    };

    std::vector<VarDecl> inputs, outputs;

    for (auto& elem : dxbc.inputSignature) {
        VarDecl d;
        d.regIndex = elem.registerIndex;
        d.name = semanticToMSL(elem.semanticName, elem.semanticIndex, elem.componentType);
        uint32_t componentCount = 0;
        for (int i = 0; i < 4; ++i) if (elem.mask & (1 << i)) componentCount++;
        switch (componentCount) {
            case 1: d.type = "float"; break;
            case 2: d.type = "float2"; break;
            case 3: d.type = "float3"; break;
            default: d.type = "float4"; break;
        }
        if (dxbc.shaderType == DXBCShaderType::Vertex)
            d.attribute = " [[attribute(" + std::to_string(elem.registerIndex) + ")]]";
        inputs.push_back(d);
    }

    for (auto& elem : dxbc.outputSignature) {
        VarDecl d;
        d.regIndex = elem.registerIndex;
        d.name = semanticToMSL(elem.semanticName, elem.semanticIndex, elem.componentType);
        d.type = "float4";
        if (dxbc.shaderType == DXBCShaderType::Pixel)
            d.attribute = " [[color(" + std::to_string(elem.registerIndex) + ")]]";
        else if (elem.semanticName == "SV_POSITION" || elem.semanticName == "POSITION")
            d.attribute = " [[position]]";
        outputs.push_back(d);
    }

    uint32_t maxTemp = 0;
    for (auto& inst : dxbc.instructions) {
        for (uint32_t i = 0; i < inst.operandCount; ++i) {
            if (inst.operands[i].type == 0) {
                maxTemp = std::max(maxTemp, inst.operands[i].indices[0]);
            }
        }
    }

    if (dxbc.shaderType == DXBCShaderType::Vertex) {
        src += "struct VertexIn {\n";
        for (auto& inp : inputs) {
            src += "    " + inp.type + " " + inp.name + inp.attribute + ";\n";
        }
        src += "};\n\n";

        src += "struct VertexOut {\n";
        for (auto& out : outputs) {
            src += "    " + out.type + " " + out.name + out.attribute + ";\n";
        }
        src += "    float4 position [[position]];\n";
        src += "};\n\n";
    } else if (dxbc.shaderType == DXBCShaderType::Pixel) {
        src += "struct PixelIn {\n";
        for (auto& inp : inputs) {
            src += "    " + inp.type + " " + inp.name + ";\n";
        }
        src += "};\n\n";

        if (outputs.size() <= 1) {
            // return value
        } else {
            src += "struct PixelOut {\n";
            for (auto& out : outputs) {
                src += "    " + out.type + " " + out.name + out.attribute + ";\n";
            }
            src += "};\n\n";
        }
    }

    if (!isCompute) {
        if (dxbc.shaderType == DXBCShaderType::Vertex)
            src += stageAttr + std::string("VertexOut ") + stageFn + "(VertexIn input_in [[stage_in]]";
        else if (dxbc.shaderType == DXBCShaderType::Pixel && outputs.size() <= 1)
            src += stageAttr + std::string("float4 ") + stageFn + "(PixelIn input_in [[stage_in]]";
        else if (dxbc.shaderType == DXBCShaderType::Pixel)
            src += stageAttr + std::string("PixelOut ") + stageFn + "(PixelIn input_in [[stage_in]]";

        src += ", constant float* cb0 [[buffer(1)]]";
        src += ", texture2d<float> texture0 [[texture(0)]]";
        src += ", sampler sampler0 [[sampler(0)]]";
        src += ") {\n";
    } else {
        src += stageAttr + std::string("void ") + stageFn + "(";
        src += "uint3 gid [[thread_position_in_threadgroup]]";
        src += ", constant float* cb0 [[buffer(1)]]";
        src += ") {\n";
    }

    for (uint32_t i = 0; i <= maxTemp; ++i) {
        src += "    float4 r" + std::to_string(i) + " = float4(0);\n";
    }

    for (auto& inp : inputs) {
        src += "    float4 input" + std::to_string(inp.regIndex) + " = float4(input_in." + inp.name;
        if (inp.type == "float") src += ", 0, 0, 0";
        else if (inp.type == "float2") src += ", 0, 0";
        else if (inp.type == "float3") src += ", 0";
        src += ");\n";
    }

    for (auto& inst : dxbc.instructions) {
        if (inst.opcode == 90) { // MOV
            if (inst.operandCount >= 2) {
                src += "    " + operandToString(inst.operands[0], 3) + " = " + operandToString(inst.operands[1], 3) + ";\n";
            }
        } else if (inst.opcode == 0 || inst.opcode == 1 || inst.opcode == 2) { // ADD, SUB, MUL
            if (inst.operandCount >= 3) {
                const char* op = inst.opcode == 0 ? "+" : inst.opcode == 1 ? "-" : "*";
                src += "    " + operandToString(inst.operands[0], 3) + " = " +
                       operandToString(inst.operands[1], 3) + " " + op + " " +
                       operandToString(inst.operands[2], 3) + ";\n";
            }
        } else if (inst.opcode == 4) { // MAD
            if (inst.operandCount >= 4) {
                src += "    " + operandToString(inst.operands[0], 3) + " = " +
                       operandToString(inst.operands[1], 3) + " * " +
                       operandToString(inst.operands[2], 3) + " + " +
                       operandToString(inst.operands[3], 3) + ";\n";
            }
        } else if (inst.opcode == 6 || inst.opcode == 7) { // DP3, DP4
            if (inst.operandCount >= 3) {
                src += "    " + operandToString(inst.operands[0], 3) + " = float4(dot(" +
                       operandToString(inst.operands[1], 3) + ", " +
                       operandToString(inst.operands[2], 3) + "));\n";
            }
        } else if (inst.opcode == 7) { // DP4 (already handled above with DP3)
        } else if (inst.opcode == 17) { // RSQ
            if (inst.operandCount >= 2) {
                src += "    " + operandToString(inst.operands[0], 3) + " = float4(rsqrt(" +
                       operandToString(inst.operands[1], 3) + "));\n";
            }
        } else if (inst.opcode == 64) { // SAMPLE
            if (inst.operandCount >= 4) {
                src += "    " + operandToString(inst.operands[0], 3) + " = " +
                       operandToString(inst.operands[3], 3) + ".sample(" +
                       operandToString(inst.operands[2], 3) + ", " +
                       operandToString(inst.operands[1], 3) + ");\n";
            }
        } else if (inst.opcode == 82) { // DERIV_RTX
            if (inst.operandCount >= 2) {
                src += "    " + operandToString(inst.operands[0], 3) + " = float4(dfdx(" +
                       operandToString(inst.operands[1], 3) + "));\n";
            }
        } else if (inst.opcode == 83) { // DERIV_RTY
            if (inst.operandCount >= 2) {
                src += "    " + operandToString(inst.operands[0], 3) + " = float4(dfdy(" +
                       operandToString(inst.operands[1], 3) + "));\n";
            }
        } else if (inst.opcode == 84) { // DISCARD
            src += "    discard_fragment();\n";
        } else if (inst.opcode == 22) { // RET
            // handled at end
        }
    }

    if (dxbc.shaderType == DXBCShaderType::Vertex) {
        src += "    VertexOut out;\n";
        for (auto& out : outputs) {
            src += "    out." + out.name + " = output" + std::to_string(out.regIndex) + ";\n";
        }
        src += "    out.position = output0;\n";
        src += "    return out;\n";
    } else if (dxbc.shaderType == DXBCShaderType::Pixel) {
        if (outputs.size() == 1) {
            src += "    return output" + std::to_string(outputs[0].regIndex) + ";\n";
        } else if (outputs.size() > 1) {
            src += "    PixelOut out;\n";
            for (auto& out : outputs) {
                src += "    out." + out.name + " = output" + std::to_string(out.regIndex) + ";\n";
            }
            src += "    return out;\n";
        }
    }

    src += "}\n";
    return src;
}

}
