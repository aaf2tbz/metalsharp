#include <metalsharp/DXBCParser.h>
#include <cstring>
#include <cstdio>

namespace metalsharp {

static const uint32_t DXBC_MAGIC = 0x43425844; // "DXBC"
static const uint32_t SHDR_MAGIC = 0x52444853; // "SHDR"
static const uint32_t SHEX_MAGIC = 0x53454853; // "SHEX"
static const uint32_t ISGN_MAGIC = 0x49475349; // "ISGN"
static const uint32_t OSGN_MAGIC = 0x4F47534F; // "OSGN"

enum DXBCOpcode : uint32_t {
    OP_ADD = 0,
    OP_SUB = 1,
    OP_MUL = 2,
    OP_DIV = 3,
    OP_MAD = 4,
    OP_DP2 = 5,
    OP_DP3 = 6,
    OP_DP4 = 7,
    OP_MIN = 8,
    OP_MAX = 9,
    OP_LT = 10,
    OP_GE = 11,
    OP_EQ = 12,
    OP_NE = 13,
    OP_LE = 14,
    OP_MOVC = 15,
    OP_SINCOS = 16,
    OP_RSQ = 17,
    OP_ROUND_PI = 18,
    OP_ROUND_NI = 19,
    OP_ROUND_Z = 20,
    OP_SQRT = 21,
    OP_FTOI = 22,
    OP_FTOU = 23,
    OP_ITOF = 24,
    OP_UTOF = 25,
    OP_EXP = 26,
    OP_LOG = 27,
    OP_SAMPLE = 64,
    OP_SAMPLE_L = 65,
    OP_SAMPLE_B = 66,
    OP_SAMPLE_C = 67,
    OP_SAMPLE_C_LZ = 68,
    OP_GATHER4 = 81,
    OP_MOV = 90,
    OP_IADD = 100,
    OP_ISHL = 101,
    OP_ISHR = 102,
    OP_IMUL = 104,
    OP_INE = 107,
    OP_IGE = 108,
    OP_ILT = 109,
    OP_IEQ = 110,
    OP_AND = 111,
    OP_OR = 112,
    OP_XOR = 113,
    OP_NOT = 114,
    OP_UMUL = 115,
    OP_UGE = 116,
    OP_ULT = 117,
    OP_RET = 22,
    OP_LOOP = 23,
    OP_ENDLOOP = 24,
    OP_IF = 25,
    OP_ELSE = 26,
    OP_ENDIF = 27,
    OP_BREAK = 28,
    OP_SWITCH = 29,
    OP_CASE = 30,
    OP_DEFAULT = 31,
    OP_ENDSWITCH = 32,
    OP_NOP = 0xFF,
    OP_DERIV_RTX = 82,
    OP_DERIV_RTY = 83,
    OP_DISCARD = 84,
    OP_DCL_INPUT = 1,
    OP_DCL_OUTPUT = 2,
    OP_DCL_SAMPLER = 3,
    OP_DCL_RESOURCE = 4,
    OP_DCL_CONSTANT_BUFFER = 5,
    OP_DCL_TEMPS = 6,
    OP_DCL_INDEXABLE_TEMP = 7,
    OP_DCL_GLOBAL_FLAGS = 8,
    OP_DCL_INPUT_SGV = 9,
    OP_DCL_INPUT_PS = 10,
    OP_DCL_INPUT_PS_SGV = 11,
    OP_DCL_OUTPUT_SGV = 12,
    OP_DCL_INPUT_SIV = 13,
    OP_DCL_OUTPUT_SIV = 14,
    OP_CUSTOMDATA = 15,
    OP_BUF_LD = 68,
    OP_BUF_ST = 69,
    OP_TFETCH = 70,
};

static bool readU32(const uint8_t* data, size_t size, size_t offset, uint32_t& out) {
    if (offset + 4 > size) return false;
    memcpy(&out, data + offset, 4);
    return true;
}

static std::string readString(const uint8_t* base, size_t totalSize, uint32_t offset) {
    std::string result;
    while (offset < totalSize) {
        char c = static_cast<char>(base[offset]);
        if (c == 0) break;
        result += c;
        offset++;
    }
    return result;
}

bool DXBCParser::parseSignature(const uint8_t* chunkData, size_t chunkSize, std::vector<DXBCSignatureElement>& out, const uint8_t* containerBase) {
    uint32_t elementCount, key;
    memcpy(&elementCount, chunkData + 8, 4);
    memcpy(&key, chunkData + 12, 4);

    const uint8_t* elements = chunkData + 16;
    size_t elementStride = 24;

    for (uint32_t i = 0; i < elementCount; ++i) {
        size_t eoff = i * elementStride;
        if (eoff + elementStride > chunkSize - 16) break;

        DXBCSignatureElement elem;
        memcpy(&elem.semanticNameOffset, elements + eoff, 4);
        memcpy(&elem.semanticIndex, elements + eoff + 4, 4);
        memcpy(&elem.systemValueType, elements + eoff + 8, 4);
        memcpy(&elem.componentType, elements + eoff + 12, 4);
        memcpy(&elem.registerIndex, elements + eoff + 16, 4);
        memcpy(&elem.mask, elements + eoff + 20, 4);

        size_t nameOffset = 16 + elem.semanticNameOffset;
        if (nameOffset < chunkSize) {
            elem.semanticName = readString(chunkData, chunkSize, nameOffset);
        }
        out.push_back(elem);
    }
    return true;
}

bool DXBCParser::parseOperand(const uint32_t* tokens, size_t maxTokens, DXBCOperand& operand, size_t& tokensConsumed) {
    if (maxTokens == 0) return false;
    tokensConsumed = 0;

    uint32_t token = tokens[0];
    tokensConsumed++;

    operand.type = token & 0xFF;
    operand.indexDimension = (token >> 20) & 0x3;
    operand.mask = (token >> 12) & 0xF;

    for (uint32_t i = 0; i <= operand.indexDimension && i < 3; ++i) {
        if (tokensConsumed >= maxTokens) return false;
        uint32_t indexRep = (token >> (22 + i * 2)) & 0x3;
        if (indexRep == 0) {
            operand.indices[i] = tokens[tokensConsumed++];
        } else if (indexRep == 1 || indexRep == 2) {
            operand.indices[i] = tokens[tokensConsumed++];
        } else {
            operand.indices[i] = 0;
        }
    }

    return true;
}

bool DXBCParser::parseBytecode(const uint32_t* tokens, size_t tokenCount, ParsedDXBC& out) {
    if (tokenCount < 2) return false;

    uint32_t versionToken = tokens[0];
    uint32_t shaderType = (versionToken >> 16) & 0xFFFF;
    switch (shaderType) {
        case 0xFFFF: out.shaderType = DXBCShaderType::Pixel; break;
        case 0xFFFE: out.shaderType = DXBCShaderType::Vertex; break;
        case 0xFFF2: out.shaderType = DXBCShaderType::Geometry; break;
        case 0xFFF1: out.shaderType = DXBCShaderType::Hull; break;
        case 0xFFF0: out.shaderType = DXBCShaderType::Domain; break;
        case 0xFFF3: out.shaderType = DXBCShaderType::Compute; break;
        default: return false;
    }
    out.majorVersion = (versionToken >> 4) & 0xF;
    out.minorVersion = versionToken & 0xF;

    uint32_t lengthInTokens = tokens[1];
    if (lengthInTokens > tokenCount) return false;

    size_t pos = 2;
    while (pos < lengthInTokens) {
        uint32_t instToken = tokens[pos];
        uint32_t opcode = instToken & 0x7FF;
        uint32_t instLength = (instToken >> 24) & 0x7F;

        if (instLength == 0) break;

        DXBCInstruction inst = {};
        inst.opcode = opcode;
        inst.length = instLength;

        size_t operandPos = pos + 1;
        size_t remainingTokens = lengthInTokens - operandPos;
        inst.operandCount = 0;

        if (opcode == 0x0F) {
            if (operandPos + 1 <= lengthInTokens) {
                inst.immInt32 = tokens[operandPos];
            }
        } else {
            for (uint32_t op = 0; op < 4 && remainingTokens > 0; ++op) {
                size_t consumed = 0;
                if (parseOperand(tokens + operandPos, remainingTokens, inst.operands[op], consumed)) {
                    operandPos += consumed;
                    remainingTokens -= consumed;
                    inst.operandCount++;
                } else {
                    break;
                }
            }
        }

        out.instructions.push_back(inst);
        pos += instLength;
    }

    return true;
}

bool DXBCParser::parseContainer(const uint8_t* data, size_t size, ParsedDXBC& out) {
    uint32_t magic;
    if (!readU32(data, size, 0, magic) || magic != DXBC_MAGIC) return false;
    if (size < 32) return false;

    uint32_t totalFileSize;
    memcpy(&totalFileSize, data + 24, 4);

    uint32_t chunkCount;
    memcpy(&chunkCount, data + 28, 4);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t chunkOffset;
        if (!readU32(data, size, 32 + i * 4, chunkOffset)) continue;
        if (chunkOffset + 8 > size) continue;

        uint32_t chunkMagic;
        uint32_t chunkSize;
        memcpy(&chunkMagic, data + chunkOffset, 4);
        memcpy(&chunkSize, data + chunkOffset + 4, 4);

        if (chunkOffset + 8 + chunkSize > size) continue;

        if (chunkMagic == SHDR_MAGIC || chunkMagic == SHEX_MAGIC) {
            const uint32_t* tokens = reinterpret_cast<const uint32_t*>(data + chunkOffset + 8);
            size_t tokenCount = chunkSize / 4;
            if (!parseBytecode(tokens, tokenCount, out)) return false;
        } else if (chunkMagic == ISGN_MAGIC) {
            parseSignature(data + chunkOffset, chunkSize + 8, out.inputSignature, data);
        } else if (chunkMagic == OSGN_MAGIC) {
            parseSignature(data + chunkOffset, chunkSize + 8, out.outputSignature, data);
        }
    }

    return !out.instructions.empty();
}

bool DXBCParser::parse(const uint8_t* data, size_t size, ParsedDXBC& out) {
    if (!data || size < 24) return false;
    return parseContainer(data, size, out);
}

}
