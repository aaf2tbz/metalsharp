#include "msl_lowering.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace dxmt::dxil {

enum DXIntrinsicOpcode {
  DXOP_LoadInput = 4,
  DXOP_StoreOutput = 5,
  DXOP_CreateHandle = 57,
  DXOP_CreateHandleForLib = 160,
  DXOP_AnnotateHandle = 216,
  DXOP_CreateHandleFromBinding = 217,
  DXOP_CreateHandleFromHeap = 218,
  DXOP_CBufferLoad = 58,
  DXOP_CBufferLoadLegacy = 59,
  DXOP_ThreadId = 93,
  DXOP_GroupId = 94,
  DXOP_ThreadIDInGroup = 95,
  DXOP_FlattenedThreadIDInGroup = 96,
  DXOP_BufferLoad = 68,
  DXOP_BufferStore = 69,
  DXOP_TextureLoad = 66,
  DXOP_TextureStore = 67,
  DXOP_TextureGather = 73,
  DXOP_TextureSample = 60,
  DXOP_TextureSampleBias = 61,
  DXOP_TextureSampleLevel = 62,
  DXOP_TextureSampleGrad = 63,
  DXOP_TextureSampleCmp = 64,
  DXOP_TextureSampleCmpLevelZero = 65,
  DXOP_Barrier = 80,
  DXOP_Unary = 13,
  DXOP_Binary = 14,
  DXOP_Tertiary = 15,
  DXOP_Dot2 = 54,
  DXOP_Dot3 = 55,
  DXOP_Dot4 = 56,
  DXOP_RawBufferLoad = 139,
  DXOP_RawBufferStore = 140,
  DXOP_BufferUpdateCounter = 70,
  DXOP_CheckAccessFullyMapped = 71,
  DXOP_GetDimensions = 72,
  DXOP_AtomicBinOp = 78,
  DXOP_AtomicCompareExchange = 79,
  DXOP_DerivCoarseX = 83,
  DXOP_DerivCoarseY = 84,
  DXOP_DerivFineX = 85,
  DXOP_DerivFineY = 86,
  DXOP_CalcLOD = 81,
  DXOP_LegacyF16ToF32 = 131,
  DXOP_LegacyF32ToF16 = 132,
  DXOP_WaveIsFirstLane = 110,
  DXOP_WaveGetLaneIndex = 111,
  DXOP_WaveGetLaneCount = 112,
  DXOP_WaveAnyTrue = 113,
  DXOP_WaveAllTrue = 114,
  DXOP_WaveReadLaneAt = 117,
  DXOP_WaveReadLaneFirst = 118,
  DXOP_QuadReadLaneAt = 122,
  DXOP_TextureStoreSample = 225,
  DXOP_TextureSampleCmpLevel = 224,
  DXOP_TextureGatherCmp = 74,
  DXOP_TextureGatherRaw = 223,
};

enum DXILMathOpcode {
  DXILOP_FAbs = 6, DXILOP_Saturate = 7, DXILOP_IsNaN = 8, DXILOP_IsInf = 9,
  DXILOP_IsFinite = 10, DXILOP_Cos = 12, DXILOP_Sin = 13, DXILOP_Tan = 14,
  DXILOP_Acos = 15, DXILOP_Asin = 16, DXILOP_Atan = 17,
  DXILOP_Exp = 21, DXILOP_Frc = 22, DXILOP_Log = 23,
  DXILOP_Sqrt = 24, DXILOP_Rsqrt = 25,
  DXILOP_Round_ne = 26, DXILOP_Round_ni = 27, DXILOP_Round_pi = 28, DXILOP_Round_z = 29,
  DXILOP_Bfrev = 30, DXILOP_Countbits = 31,
  DXILOP_FirstbitLo = 32, DXILOP_FirstbitHi = 33, DXILOP_FirstbitSHi = 34,
  DXILOP_FMax = 35, DXILOP_FMin = 36, DXILOP_IMax = 37, DXILOP_IMin = 38,
  DXILOP_UMax = 39, DXILOP_UMin = 40,
  DXILOP_IMul = 41, DXILOP_UMul = 42, DXILOP_UDiv = 43,
  DXILOP_UAddc = 44, DXILOP_USubb = 45,
  DXILOP_FMad = 46, DXILOP_Fma = 47, DXILOP_IMad = 48, DXILOP_UMad = 49,
  DXILOP_Ibfe = 51, DXILOP_Ubfe = 52, DXILOP_Bfi = 53,
};

static const char *kMetalHeader = R"(#include <metal_stdlib>
using namespace metal;

)";

static std::string emitValue(uint32_t idx) {
    if (idx == 0xFFFFFFFF) return "undef";
    return "v" + std::to_string(idx);
}

static bool startsWith(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

static bool parseUnsignedLiteral(const std::string &text, uint32_t &value) {
    if (text.empty()) return false;
    char *end = nullptr;
    unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    value = (uint32_t)parsed;
    return true;
}

static bool parseEmittedValueName(const std::string &name, uint32_t &idx) {
    if (name.size() < 2 || name[0] != 'v') return false;
    return parseUnsignedLiteral(name.substr(1), idx);
}

static std::vector<std::string> parseAggregateLiteral(const std::string &text) {
    std::vector<std::string> values;
    bool is_agg = startsWith(text, "agg(") && text.size() >= 5 && text.back() == ')';
    bool is_brace = !text.empty() && text[0] == '{' && text.back() == '}';
    size_t start = is_agg ? 4 : 1;
    if (!is_agg && !is_brace) {
        static const char *ctors[] = {
            "float2(", "float3(", "float4(",
            "int2(", "int3(", "int4(",
            "uint2(", "uint3(", "uint4(",
            "half2(", "half3(", "half4("
        };
        bool is_ctor = false;
        for (auto *ctor : ctors) {
            if (startsWith(text, ctor) && text.back() == ')') {
                start = std::strlen(ctor);
                is_ctor = true;
                break;
            }
        }
        if (!is_ctor) return values;
    }
    while (start < text.size() - 1) {
        size_t comma = text.find(',', start);
        size_t end_pos = comma == std::string::npos ? text.size() - 1 : comma;
        std::string val = text.substr(start, end_pos - start);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
            val.erase(val.begin());
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
            val.pop_back();
        if (!val.empty()) values.push_back(val);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return values;
}

static std::string ensureScalarIndex(const std::string &val) {
    auto parts = parseAggregateLiteral(val);
    if (!parts.empty()) return parts[0];
    if (startsWith(val, "buf") || startsWith(val, "tex") || startsWith(val, "samp") ||
        val.find("char*") != std::string::npos || val.find("char *") != std::string::npos)
        return "0";
    return val;
}

static const char *componentSuffix(uint32_t component) {
    switch (component & 3) {
    case 0: return ".x"; case 1: return ".y"; case 2: return ".z"; default: return ".w";
    }
}

static const char *componentName(uint32_t component) {
    switch (component & 3) {
    case 0: return "x"; case 1: return "y"; case 2: return "z"; default: return "w";
    }
}

static std::string varyingField(const char *base, uint32_t sig_id) {
    if (sig_id == 0) return std::string(base) + ".position";
    if (sig_id <= 8) return std::string(base) + ".v" + std::to_string(sig_id - 1);
    return std::string(base) + ".v7";
}

static std::string vertexInputField(const char *base, uint32_t sig_id) {
    if (sig_id <= 15) return std::string(base) + ".a" + std::to_string(sig_id);
    return std::string(base) + ".a0";
}

static std::string escapeName(const std::string &s) {
    if (s.empty()) return "_";
    std::string r;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_')
            r += c;
        else r += '_';
    }
    if (!r.empty() && r[0] >= '0' && r[0] <= '9') r = "_" + r;
    return r;
}

static uint32_t intrinsicIdFromCalleeName(const std::string &name) {
    if (name.size() < 6 || name[0] != 'd' || name[1] != 'x' || name[2] != '.' || name[3] != 'o' || name[4] != 'p' || name[5] != '.')
        return 0;
    const char *s = name.c_str() + 6;
    if (strncmp(s, "loadInput.", 10) == 0) return 4;
    if (strncmp(s, "storeOutput.", 12) == 0) return 5;
    if (strncmp(s, "createHandleFromBinding", 23) == 0) return 217;
    if (strncmp(s, "createHandleFromHeap", 20) == 0) return 218;
    if (strncmp(s, "createHandleForLib", 18) == 0) return 160;
    if (strncmp(s, "createHandle", 12) == 0) return 57;
    if (strncmp(s, "annotateHandle", 14) == 0) return 216;
    if (strncmp(s, "cbufferLoadLegacy.", 18) == 0) return 59;
    if (strncmp(s, "cbufferLoad.", 12) == 0) return 58;
    if (strncmp(s, "threadIdInGroup", 15) == 0) return 95;
    if (strncmp(s, "flattenedThreadIdInGroup", 24) == 0) return 96;
    if (strncmp(s, "threadId", 8) == 0) return 93;
    if (strncmp(s, "groupId", 7) == 0) return 94;
    if (strncmp(s, "bufferLoad.", 11) == 0) return 68;
    if (strncmp(s, "bufferStore.", 12) == 0) return 69;
    if (strncmp(s, "bufferUpdateCounter", 19) == 0) return 70;
    if (strncmp(s, "textureStoreSample.", 19) == 0) return 225;
    if (strncmp(s, "textureStore.", 13) == 0) return 67;
    if (strncmp(s, "textureLoad.", 12) == 0) return 66;
    if (strncmp(s, "textureGatherCmp.", 17) == 0) return 74;
    if (strncmp(s, "textureGatherRaw.", 17) == 0) return 223;
    if (strncmp(s, "textureGather.", 14) == 0) return 73;
    if (strncmp(s, "sampleCmpLevelZero.", 19) == 0) return 65;
    if (strncmp(s, "sampleCmpLevel.", 15) == 0) return 224;
    if (strncmp(s, "sampleCmp.", 10) == 0) return 64;
    if (strncmp(s, "sampleGrad.", 11) == 0) return 63;
    if (strncmp(s, "sampleLevel.", 12) == 0) return 62;
    if (strncmp(s, "sampleBias.", 10) == 0) return 61;
    if (strncmp(s, "sample.", 7) == 0) return 60;
    if (strncmp(s, "unaryBits.", 10) == 0) return 13;
    if (strncmp(s, "unary.", 6) == 0) return 13;
    if (strncmp(s, "binary.", 7) == 0) return 14;
    if (strncmp(s, "tertiary.", 9) == 0) return 15;
    if (strncmp(s, "dot2.", 5) == 0) return 54;
    if (strncmp(s, "dot3.", 5) == 0) return 55;
    if (strncmp(s, "dot4.", 5) == 0) return 56;
    if (strncmp(s, "barrier", 7) == 0) return 80;
    if (strncmp(s, "checkAccessFullyMapped", 22) == 0) return 71;
    if (strncmp(s, "getDimensions", 13) == 0) return 72;
    if (strncmp(s, "rawBufferLoadLegacy", 19) == 0) return 1025;
    if (strncmp(s, "rawBufferStoreLegacy", 20) == 0) return 1026;
    if (strncmp(s, "rawBufferVectorLoad", 19) == 0) return 303;
    if (strncmp(s, "rawBufferVectorStore", 20) == 0) return 304;
    if (strncmp(s, "rawBufferLoad", 13) == 0) return 139;
    if (strncmp(s, "rawBufferStore", 14) == 0) return 140;
    if (strncmp(s, "atomicCompareExchange", 21) == 0) return 79;
    if (strncmp(s, "atomicBinOp", 11) == 0) return 78;
    if (strncmp(s, "derivCoarseX", 12) == 0) return 83;
    if (strncmp(s, "derivCoarseY", 12) == 0) return 84;
    if (strncmp(s, "derivFineX", 10) == 0) return 85;
    if (strncmp(s, "derivFineY", 10) == 0) return 86;
    if (strncmp(s, "calculateLOD", 12) == 0 || strncmp(s, "calcLOD", 7) == 0) return 81;
    if (strncmp(s, "makeDouble", 10) == 0) return 101;
    if (strncmp(s, "splitDouble", 11) == 0) return 102;
    if (strncmp(s, "legacyF16ToF32", 14) == 0) return 131;
    if (strncmp(s, "legacyF32ToF16", 14) == 0) return 132;
    if (strncmp(s, "waveReadLaneFirst", 17) == 0) return 118;
    if (strncmp(s, "waveReadLaneAt", 14) == 0) return 117;
    if (strncmp(s, "waveIsFirstLane", 15) == 0) return 110;
    if (strncmp(s, "waveGetLaneIndex", 16) == 0) return 111;
    if (strncmp(s, "waveGetLaneCount", 16) == 0) return 112;
    if (strncmp(s, "waveAnyTrue", 11) == 0) return 113;
    if (strncmp(s, "waveAllTrue", 11) == 0) return 114;
    if (strncmp(s, "quadReadLaneAt", 14) == 0) return 122;
    if (strncmp(s, "isSpecialFloat", 14) == 0) return 0;
    if (strncmp(s, "cycleCounterLegacy", 18) == 0) return 109;
    if (strncmp(s, "texture2DMSGetSamplePosition", 27) == 0) return 75;
    if (strncmp(s, "renderTargetGetSamplePosition", 29) == 0) return 76;
    if (strncmp(s, "renderTargetGetSampleCount", 26) == 0) return 77;
    return 0;
}

static std::string emitTypeName(const MSLType &t) {
    if (t.kind == MSLTypeKind::Struct || t.kind == MSLTypeKind::Unknown)
        return "auto";
    return DXILIRBuilder::mslTypeName(t);
}

static std::string defaultForType(const MSLType &t) {
    switch (t.kind) {
    case MSLTypeKind::Bool: return "false";
    case MSLTypeKind::Float: return "0.0f";
    case MSLTypeKind::Float2: return "float2(0.0f)";
    case MSLTypeKind::Float3: return "float3(0.0f)";
    case MSLTypeKind::Float4: return "float4(0.0f)";
    case MSLTypeKind::Int: return "0";
    case MSLTypeKind::Int2: return "int2(0)";
    case MSLTypeKind::Int3: return "int3(0)";
    case MSLTypeKind::Int4: return "int4(0)";
    case MSLTypeKind::UInt: return "0u";
    case MSLTypeKind::UInt2: return "uint2(0)";
    case MSLTypeKind::UInt3: return "uint3(0)";
    case MSLTypeKind::UInt4: return "uint4(0)";
    default: return "0";
    }
}

static MSLType aggregateFallbackType(const std::vector<std::string> &parts) {
    bool has_float = false;
    bool has_unsigned = false;
    for (const auto &part : parts) {
        if (part.find('.') != std::string::npos || part.find('f') != std::string::npos ||
            part.find("inf") != std::string::npos || part.find("nan") != std::string::npos) {
            has_float = true;
            break;
        }
        if (!part.empty() && part.back() == 'u')
            has_unsigned = true;
    }

    size_t count = std::min<size_t>(std::max<size_t>(parts.size(), 1), 4);
    if (has_float) {
        switch (count) {
        case 2: return {MSLTypeKind::Float2, 0, {}};
        case 3: return {MSLTypeKind::Float3, 0, {}};
        case 4: return {MSLTypeKind::Float4, 0, {}};
        default: return {MSLTypeKind::Float, 0, {}};
        }
    }
    if (has_unsigned) {
        switch (count) {
        case 2: return {MSLTypeKind::UInt2, 0, {}};
        case 3: return {MSLTypeKind::UInt3, 0, {}};
        case 4: return {MSLTypeKind::UInt4, 0, {}};
        default: return {MSLTypeKind::UInt, 0, {}};
        }
    }
    switch (count) {
    case 2: return {MSLTypeKind::Int2, 0, {}};
    case 3: return {MSLTypeKind::Int3, 0, {}};
    case 4: return {MSLTypeKind::Int4, 0, {}};
    default: return {MSLTypeKind::Int, 0, {}};
    }
}

static bool isAggregateLiteralText(const std::string &text) {
    return startsWith(text, "agg(") && text.size() >= 5 && text.back() == ')';
}

static std::string aggregateConstructor(const std::string &literal, MSLType type = {}) {
    auto parts = parseAggregateLiteral(literal);
    if (parts.empty()) return literal;
    if (!DXILIRBuilder::isVectorType(type))
        type = aggregateFallbackType(parts);

    std::string type_name = emitTypeName(type);
    if (type_name.empty() || type_name == "auto")
        type_name = emitTypeName(aggregateFallbackType(parts));

    std::string args;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) args += ", ";
        args += parts[i];
    }
    return type_name + "(" + args + ")";
}

static std::string normalizeAggregateExpressions(const std::string &expr, MSLType preferred_type = {}) {
    if (isAggregateLiteralText(expr))
        return aggregateConstructor(expr, preferred_type);

    std::string out;
    size_t pos = 0;
    while (pos < expr.size()) {
        size_t start = expr.find("agg(", pos);
        if (start == std::string::npos) {
            out += expr.substr(pos);
            break;
        }
        out += expr.substr(pos, start - pos);
        int depth = 0;
        size_t end = start;
        for (; end < expr.size(); end++) {
            if (expr[end] == '(') depth++;
            else if (expr[end] == ')') {
                depth--;
                if (depth == 0) {
                    end++;
                    break;
                }
            }
        }
        if (depth != 0) {
            out += expr.substr(start);
            break;
        }
        out += aggregateConstructor(expr.substr(start, end - start));
        pos = end;
    }
    return out;
}

static std::string typedDecl(const std::string &name, const MSLType &t) {
    return emitTypeName(t) + " " + name;
}

struct DescriptorRangePlan {
    enum class Kind { SRV, UAV, CBV, Sampler } kind = Kind::SRV;
    uint32_t register_space = 0;
    uint32_t lower_bound = 0;
    uint32_t count = 1;
};

struct ResourceHandleRecord {
    DescriptorRangePlan::Kind kind = DescriptorRangePlan::Kind::SRV;
    uint32_t resource_class = 0;
    uint32_t register_space = 0;
    uint32_t lower_bound = 0;
    uint32_t binding_index = 0;
    bool non_uniform = false;
};

struct BindingPlan {
    std::vector<DescriptorRangePlan> ranges;
    uint32_t direct_buffer_count = 31;
    uint32_t direct_texture_count = 8;
    uint32_t direct_sampler_count = 4;
};

struct LowerContext {
    std::ostringstream &os;
    const LLVMModule &mod;
    const DxilParsedShader &shader;
    std::vector<std::string> value_table;
    std::vector<MSLType> value_types;
    std::vector<ValueRole> value_roles;
    std::unordered_map<uint32_t, std::string> buffer_origin;
    std::unordered_map<uint32_t, ResourceHandleRecord> resource_handles;
    std::optional<ResourceHandleRecord> pending_handle;
    std::string last_buffer_handle;
    std::unordered_map<std::string, std::string> local_values;
    std::vector<std::string> diagnostics;
    std::unordered_map<uint32_t, std::string> function_decls;
    std::set<std::string> predeclared_names;
    std::set<uint32_t> predeclared_allocas;
    std::unordered_map<std::string, MSLType> predeclared_types;
    BindingPlan binding_plan;
    uint32_t next_binding = 0;
    uint32_t unsupported_intrinsics = 0;
    uint32_t unsupported_opcodes = 0;
    uint32_t instruction_start_value = 0;
    const LLVMFunction *current_fn = nullptr;
    bool uses_thread_id = false;
    bool uses_group_id = false;
    bool uses_group_thread_id = false;
    bool uses_group_size = false;
};

static void recordDiagnostic(LowerContext &ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ctx.diagnostics.push_back(buf);
}

static void emitBindings(LowerContext &ctx) {
    auto &os = ctx.os;
    if (ctx.shader.kind == DxilShaderKind::Compute) {
        ctx.uses_thread_id = true;
        ctx.uses_group_id = true;
        ctx.uses_group_thread_id = true;
        ctx.uses_group_size = true;
    }
    os << "\n";
}

static const char *descriptorRangeKindName(DescriptorRangePlan::Kind kind) {
    switch (kind) {
    case DescriptorRangePlan::Kind::SRV: return "srv";
    case DescriptorRangePlan::Kind::UAV: return "uav";
    case DescriptorRangePlan::Kind::CBV: return "cbv";
    case DescriptorRangePlan::Kind::Sampler: return "sampler";
    }
    return "unknown";
}

static void emitBindingManifest(LowerContext &ctx) {
    auto &os = ctx.os;
    os << "// metalsharp.binding_manifest.v1\n";
    os << "// direct_buffers=" << ctx.binding_plan.direct_buffer_count
       << " direct_textures=" << ctx.binding_plan.direct_texture_count
       << " direct_samplers=" << ctx.binding_plan.direct_sampler_count << "\n";
    if (ctx.binding_plan.ranges.empty()) {
        os << "// range none\n\n";
        return;
    }
    for (const auto &range : ctx.binding_plan.ranges) {
        os << "// range kind=" << descriptorRangeKindName(range.kind)
           << " space=" << range.register_space
           << " lower=" << range.lower_bound
           << " count=" << range.count << "\n";
    }
    os << "\n";
}

static void emitFunctionPrologue(LowerContext &ctx) {
    auto &os = ctx.os;
    os << kMetalHeader;
    emitBindingManifest(ctx);

    os << "struct input_v {\n";
    os << "  float4 position [[position]];\n";
    os << "  float4 v0 [[user(locn0)]]; float4 v1 [[user(locn1)]];\n";
    os << "  float4 v2 [[user(locn2)]]; float4 v3 [[user(locn3)]];\n";
    os << "  float4 v4 [[user(locn4)]]; float4 v5 [[user(locn5)]];\n";
    os << "  float4 v6 [[user(locn6)]]; float4 v7 [[user(locn7)]];\n";
    os << "  float2 uv0 [[user(locn8)]]; float2 uv1 [[user(locn9)]];\n";
    os << "  float2 uv2 [[user(locn10)]]; float2 uv3 [[user(locn11)]];\n";
    os << "  float4 color0 [[user(locn12)]]; float4 color1 [[user(locn13)]];\n";
    os << "  float4 color2 [[user(locn14)]]; float4 color3 [[user(locn15)]];\n";
    os << "};\n\n";

    os << "struct output_v {\n";
    os << "  float4 position [[position]];\n";
    os << "  float4 v0 [[user(locn0)]]; float4 v1 [[user(locn1)]];\n";
    os << "  float4 v2 [[user(locn2)]]; float4 v3 [[user(locn3)]];\n";
    os << "  float4 v4 [[user(locn4)]]; float4 v5 [[user(locn5)]];\n";
    os << "  float4 v6 [[user(locn6)]]; float4 v7 [[user(locn7)]];\n";
    os << "  float2 uv0 [[user(locn8)]]; float2 uv1 [[user(locn9)]];\n";
    os << "  float2 uv2 [[user(locn10)]]; float2 uv3 [[user(locn11)]];\n";
    os << "  float4 color0 [[user(locn12)]]; float4 color1 [[user(locn13)]];\n";
    os << "  float4 color2 [[user(locn14)]]; float4 color3 [[user(locn15)]];\n";
    os << "};\n\n";

    os << "struct vertex_input_v {\n";
    for (uint32_t i = 0; i < 16; i++)
        os << "  float4 a" << i << " [[attribute(" << i << ")]];\n";
    os << "};\n\n";

    if (ctx.shader.kind == DxilShaderKind::Compute) {
        os << "kernel void cs_main(\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_buffer_count; i++)
            os << "  device char* buf" << i << " [[buffer(" << i << ")]],\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_texture_count; i++)
            os << "  texture2d<float, access::read_write> tex" << i << " [[texture(" << i << ")]],\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_sampler_count; i++)
            os << "  sampler samp" << i << " [[sampler(" << i << ")]],\n";
        os << "  uint3 dtid [[thread_position_in_grid]],\n";
        os << "  uint3 gtid [[thread_position_in_threadgroup]],\n";
        os << "  uint3 ggid [[threadgroup_position_in_grid]],\n";
        os << "  uint3 gsz [[threads_per_threadgroup]]\n) {\n";
    } else if (ctx.shader.kind == DxilShaderKind::Vertex) {
        os << "vertex output_v vs_main(\n";
        os << "  vertex_input_v vin [[stage_in]],\n  uint vid [[vertex_id]],\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_buffer_count; i++)
            os << "  device char* buf" << i << " [[buffer(" << i << ")]]"
               << (i + 1 == ctx.binding_plan.direct_buffer_count ? "\n" : ",\n");
        os << ") {\n";
        os << "  output_v out = {};\n";
    } else if (ctx.shader.kind == DxilShaderKind::Pixel) {
        os << "fragment float4 ps_main(\n";
        os << "  input_v in [[stage_in]],\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_buffer_count; i++)
            os << "  device char* buf" << i << " [[buffer(" << i << ")]],\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_texture_count; i++)
            os << "  texture2d<float, access::sample> tex" << i << " [[texture(" << i << ")]],\n";
        for (uint32_t i = 0; i < ctx.binding_plan.direct_sampler_count; i++) {
            os << "  sampler samp" << i << " [[sampler(" << i << ")]]";
            os << (i + 1 == ctx.binding_plan.direct_sampler_count ? "\n" : ",\n");
        }
        os << ") {\n";
        os << "  float4 result = float4(0,0,0,1);\n";
    } else {
        os << "kernel void unknown_main() {\n";
    }
}

static std::string resolveValue(LowerContext &ctx, uint32_t idx) {
    if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty()) {
        const auto &v = ctx.value_table[idx];
        if (startsWith(v, "dx.")) {
            // function name, not a resolvable value
        } else if (startsWith(v, "agg(")) {
            MSLType type = idx < ctx.value_types.size() ? ctx.value_types[idx] : MSLType{};
            return aggregateConstructor(v, type);
        } else if (v.find('.') != std::string::npos) {
            return v;
        } else {
            return v;
        }
    }
    for (auto &c : ctx.mod.constants) {
        if (c.id == idx && !c.constant_data.empty())
            return normalizeAggregateExpressions(c.constant_data, DXILIRBuilder::resolveType(c.type_id, ctx.mod));
    }
    if (ctx.current_fn) {
        for (auto &c : ctx.current_fn->constants) {
            if (c.id == idx && !c.constant_data.empty())
                return normalizeAggregateExpressions(c.constant_data, DXILIRBuilder::resolveType(c.type_id, ctx.mod));
        }
    }
    return emitValue(idx);
}

static bool exprLooksResourceHandle(const std::string &value) {
    return startsWith(value, "tex") || startsWith(value, "samp") || startsWith(value, "buf");
}

static bool typeLooksResourceHandle(const MSLType &type) {
    switch (type.kind) {
    case MSLTypeKind::DeviceCharPtr:
    case MSLTypeKind::ThreadgroupCharPtr:
    case MSLTypeKind::Texture2D:
    case MSLTypeKind::Texture2DArray:
    case MSLTypeKind::Texture3D:
    case MSLTypeKind::TextureCube:
    case MSLTypeKind::Texture2DMS:
    case MSLTypeKind::RWTexture2D:
    case MSLTypeKind::RWTexture2DArray:
    case MSLTypeKind::RWTexture3D:
    case MSLTypeKind::Sampler:
        return true;
    default:
        return false;
    }
}

static bool exprLooksSideEffectOnly(const std::string &value) {
    return value.find(".write(") != std::string::npos ||
           value.find("threadgroup_barrier(") != std::string::npos;
}

static bool exprLooksVectorValue(const std::string &value) {
    return startsWith(value, "float2(") || startsWith(value, "float3(") ||
           startsWith(value, "float4(") || startsWith(value, "int2(") ||
           startsWith(value, "int3(") || startsWith(value, "int4(") ||
           startsWith(value, "uint2(") || startsWith(value, "uint3(") ||
           startsWith(value, "uint4(") ||
           value.find("float2(") != std::string::npos ||
           value.find("float3(") != std::string::npos ||
           value.find("float4(") != std::string::npos ||
           value.find("int2(") != std::string::npos ||
           value.find("int3(") != std::string::npos ||
           value.find("int4(") != std::string::npos ||
           value.find("uint2(") != std::string::npos ||
           value.find("uint3(") != std::string::npos ||
           value.find("uint4(") != std::string::npos ||
           value.find("reinterpret_cast<device float4&>") != std::string::npos ||
           value.find("reinterpret_cast<device uint4&>") != std::string::npos ||
           value.find("reinterpret_cast<device int4&>") != std::string::npos ||
           value.find(".read(") != std::string::npos ||
           value.find(".sample(") != std::string::npos ||
           value.find(".gather(") != std::string::npos;
}

static bool exprContainsRawResourceHandle(const std::string &value) {
    if (value.find(".read(") != std::string::npos ||
        value.find(".sample(") != std::string::npos ||
        value.find(".gather(") != std::string::npos ||
        value.find(".write(") != std::string::npos ||
        value.find(".get_width(") != std::string::npos ||
        value.find(".get_height(") != std::string::npos)
        return false;

    for (const char *prefix : {"tex", "samp", "buf"}) {
        size_t pos = value.find(prefix);
        while (pos != std::string::npos) {
            bool start_ok = pos == 0 ||
                (!std::isalnum((unsigned char)value[pos - 1]) && value[pos - 1] != '_' && value[pos - 1] != '.');
            size_t end = pos + std::strlen(prefix);
            bool has_digits = false;
            while (end < value.size() && std::isdigit((unsigned char)value[end])) {
                has_digits = true;
                end++;
            }
            bool end_ok = end >= value.size() ||
                (!std::isalnum((unsigned char)value[end]) && value[end] != '_' && value[end] != '.');
            if (start_ok && has_digits && end_ok)
                return true;
            pos = value.find(prefix, pos + 1);
        }
    }
    return false;
}

static std::string coerceResolvedValue(const std::string &value, const MSLType &target) {
    std::string resolved = normalizeAggregateExpressions(value, target);
    if (target.kind == MSLTypeKind::Bool) {
        if (exprLooksResourceHandle(resolved) || exprLooksSideEffectOnly(resolved)) return "false";
        return resolved;
    }
    if (exprLooksResourceHandle(resolved) &&
        target.kind != MSLTypeKind::DeviceCharPtr &&
        target.kind != MSLTypeKind::ThreadgroupCharPtr &&
        target.kind != MSLTypeKind::Texture2D &&
        target.kind != MSLTypeKind::RWTexture2D &&
        target.kind != MSLTypeKind::Sampler) {
        return defaultForType(target);
    }
    if ((target.kind == MSLTypeKind::DeviceCharPtr ||
         target.kind == MSLTypeKind::ThreadgroupCharPtr) &&
        !exprLooksResourceHandle(resolved) &&
        resolved.find("char*") == std::string::npos &&
        resolved.find("char *") == std::string::npos &&
        resolved.find("thread") == std::string::npos &&
        resolved.find("device") == std::string::npos) {
        return defaultForType(target);
    }
    if (exprLooksSideEffectOnly(resolved))
        return defaultForType(target);
    if ((DXILIRBuilder::isFloatType(target) || DXILIRBuilder::isIntType(target)) &&
        exprContainsRawResourceHandle(resolved))
        return defaultForType(target);
    if ((DXILIRBuilder::isFloatType(target) || DXILIRBuilder::isIntType(target)) &&
        !DXILIRBuilder::isVectorType(target) && exprLooksVectorValue(resolved))
        return "(" + resolved + ").x";
    if (DXILIRBuilder::isVectorType(target) && !exprLooksVectorValue(resolved)) {
        std::string type_name = emitTypeName(target);
        if (!type_name.empty() && type_name != "auto")
            return type_name + "(" + resolved + ")";
    }
    if (resolved == "inf" || resolved == "+inf")
        return "INFINITY";
    if (resolved == "-inf")
        return "-INFINITY";
    return resolved;
}

static bool exprEndsWithComponent(const std::string &value) {
    if (value.size() < 2) return false;
    char c = value.back();
    if (c != 'x' && c != 'y' && c != 'z' && c != 'w' &&
        c != 'r' && c != 'g' && c != 'b' && c != 'a')
        return false;
    return value[value.size() - 2] == '.';
}

static std::string componentAccess(const std::string &value, uint32_t component, const MSLType &source_type) {
    if (!DXILIRBuilder::isVectorType(source_type) || exprEndsWithComponent(value))
        return value;
    return "(" + value + ")" + componentSuffix(component);
}

static std::string resolveCondition(LowerContext &ctx, uint32_t idx) {
    std::string value = resolveValue(ctx, idx);
    if (exprLooksResourceHandle(value) || exprContainsRawResourceHandle(value) ||
        exprLooksSideEffectOnly(value))
        return "false";
    if (idx < ctx.value_types.size() && DXILIRBuilder::isVectorType(ctx.value_types[idx]))
        return "any(" + value + " != " + defaultForType(ctx.value_types[idx]) + ")";
    return value;
}

static bool usableType(const MSLType &t) {
    return t.kind != MSLTypeKind::Unknown && t.kind != MSLTypeKind::Void &&
           t.kind != MSLTypeKind::Struct;
}

static MSLType valueTypeOrUnknown(const LowerContext &ctx, uint32_t idx) {
    if (idx < ctx.value_types.size()) return ctx.value_types[idx];
    return {};
}

static bool hasConstantValue(const LowerContext &ctx, uint32_t idx) {
    for (auto &c : ctx.mod.constants)
        if (c.id == idx && !c.constant_data.empty())
            return true;
    if (ctx.current_fn) {
        for (auto &c : ctx.current_fn->constants)
            if (c.id == idx && !c.constant_data.empty())
                return true;
    }
    return false;
}

static bool hasEmittableValue(const LowerContext &ctx, uint32_t idx) {
    if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty() &&
        !startsWith(ctx.value_table[idx], "dx."))
        return true;
    return hasConstantValue(ctx, idx);
}

static bool isPointerMSLType(const MSLType &t) {
    return t.kind == MSLTypeKind::DeviceCharPtr || t.kind == MSLTypeKind::ThreadgroupCharPtr;
}

static std::vector<uint32_t> functionParamTypeIds(const LLVMModule &module, const LLVMFunction &fn) {
    uint32_t type_id = fn.type_id;
    if (type_id < module.types.size() && module.types[type_id].kind == LLVMType::Pointer &&
        !module.types[type_id].type_refs.empty())
        type_id = module.types[type_id].type_refs[0];
    if (type_id >= module.types.size() || module.types[type_id].kind != LLVMType::Function ||
        module.types[type_id].type_refs.size() <= 1)
        return {};
    std::vector<uint32_t> params;
    params.reserve(module.types[type_id].type_refs.size() - 1);
    for (size_t i = 1; i < module.types[type_id].type_refs.size(); i++)
        params.push_back(module.types[type_id].type_refs[i]);
    return params;
}

static MSLType promoteNumericType(const MSLType &a, const MSLType &b, MSLType fallback) {
    if (DXILIRBuilder::isVectorType(a)) return a;
    if (DXILIRBuilder::isVectorType(b)) return b;
    if (DXILIRBuilder::isFloatType(a)) return a;
    if (DXILIRBuilder::isFloatType(b)) return b;
    if (usableType(a)) return a;
    if (usableType(b)) return b;
    return fallback;
}

static uint32_t literalFromValue(const LowerContext &ctx, uint32_t idx, uint32_t fallback) {
    std::string text;
    if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty())
        text = ctx.value_table[idx];
    else {
        for (auto &c : ctx.mod.constants)
            if (c.id == idx && !c.constant_data.empty()) { text = c.constant_data; break; }
        if (text.empty() && ctx.current_fn)
            for (auto &c : ctx.current_fn->constants)
                if (c.id == idx && !c.constant_data.empty()) { text = c.constant_data; break; }
    }
    uint32_t value = 0;
    if (parseUnsignedLiteral(text, value)) return value;
    return fallback;
}

static DescriptorRangePlan::Kind descriptorKindForResourceClass(uint32_t resource_class) {
    switch (resource_class) {
    case 0: return DescriptorRangePlan::Kind::SRV;
    case 1: return DescriptorRangePlan::Kind::UAV;
    case 2: return DescriptorRangePlan::Kind::CBV;
    case 3: return DescriptorRangePlan::Kind::Sampler;
    default: return DescriptorRangePlan::Kind::SRV;
    }
}

static const char *bindingPrefixForKind(DescriptorRangePlan::Kind kind) {
    switch (kind) {
    case DescriptorRangePlan::Kind::CBV: return "buf";
    case DescriptorRangePlan::Kind::Sampler: return "samp";
    case DescriptorRangePlan::Kind::SRV:
    case DescriptorRangePlan::Kind::UAV:
        return "tex";
    }
    return "buf";
}

static MSLType typeForHandleKind(DescriptorRangePlan::Kind kind) {
    (void)kind;
    return {MSLTypeKind::DeviceCharPtr, 0, {}};
}

static ValueRole roleForHandleKind(DescriptorRangePlan::Kind kind) {
    switch (kind) {
    case DescriptorRangePlan::Kind::CBV:
        return ValueRole::BufferHandle;
    case DescriptorRangePlan::Kind::Sampler:
        return ValueRole::SamplerHandle;
    case DescriptorRangePlan::Kind::SRV:
    case DescriptorRangePlan::Kind::UAV:
        return ValueRole::TextureHandle;
    }
    return ValueRole::Generic;
}

static uint32_t cappedBindingIndex(const LowerContext &ctx, const char *prefix, uint32_t binding_index) {
    uint32_t limit = 0;
    if (std::strcmp(prefix, "buf") == 0)
        limit = ctx.binding_plan.direct_buffer_count;
    else if (std::strcmp(prefix, "tex") == 0)
        limit = ctx.binding_plan.direct_texture_count;
    else if (std::strcmp(prefix, "samp") == 0)
        limit = ctx.binding_plan.direct_sampler_count;

    if (limit == 0)
        return 0;
    return std::min<uint32_t>(binding_index, limit - 1);
}

static std::string materializeHandleName(const LowerContext &ctx,
                                         const ResourceHandleRecord &handle,
                                         const char *target_prefix = nullptr) {
    const char *prefix = target_prefix ? target_prefix : bindingPrefixForKind(handle.kind);
    uint32_t binding_index = handle.lower_bound + handle.binding_index;
    return std::string(prefix) + std::to_string(cappedBindingIndex(ctx, prefix, binding_index));
}

static void recordDescriptorRange(BindingPlan &plan, DescriptorRangePlan range) {
    if (range.count == 0)
        range.count = 1;
    for (auto &existing : plan.ranges) {
        if (existing.kind == range.kind &&
            existing.register_space == range.register_space &&
            existing.lower_bound == range.lower_bound) {
            existing.count = std::max(existing.count, range.count);
            return;
        }
    }
    plan.ranges.push_back(range);
}

static void analyzeBindingPlan(LowerContext &ctx, const LLVMFunction &fn) {
    BindingPlan plan;
    if (ctx.shader.kind == DxilShaderKind::Vertex) {
        plan.direct_texture_count = 0;
        plan.direct_sampler_count = 0;
    }

    auto calleeName = [&](uint32_t callee) -> std::string {
        auto decl_it = ctx.function_decls.find(callee);
        if (decl_it != ctx.function_decls.end())
            return decl_it->second;
        if (callee < ctx.value_table.size())
            return ctx.value_table[callee];
        return {};
    };

    for (const auto &block : fn.blocks) {
        for (const auto &inst : block.instructions) {
            if (inst.opcode != LLVMInstruction::Call || inst.operands.size() < 2)
                continue;

            uint32_t intrinsic_id = intrinsicIdFromCalleeName(calleeName(inst.operands[0]));
            if (intrinsic_id == 0)
                continue;

            std::vector<uint32_t> call_args;
            for (size_t i = 2; i < inst.operands.size(); i++)
                call_args.push_back(inst.operands[i]);

            std::vector<uint32_t> fn_args;
            if (intrinsic_id == DXOP_CreateHandle || intrinsic_id == DXOP_CreateHandleForLib ||
                intrinsic_id == DXOP_AnnotateHandle)
                fn_args = call_args;
            else if (call_args.size() > 1)
                fn_args.assign(call_args.begin() + 1, call_args.end());

            if (intrinsic_id == DXOP_CreateHandle && fn_args.size() >= 3) {
                uint32_t resource_class = literalFromValue(ctx, fn_args[0], 0);
                uint32_t lower_bound = literalFromValue(ctx, fn_args[1], 0);
                uint32_t index = literalFromValue(ctx, fn_args[2], 0);
                recordDescriptorRange(plan, {descriptorKindForResourceClass(resource_class),
                                             0, lower_bound, index + 1});
            } else if (intrinsic_id == DXOP_CreateHandleFromBinding && fn_args.size() >= 1) {
                std::string binding = resolveValue(ctx, fn_args[0]);
                auto parts = parseAggregateLiteral(binding);
                uint32_t lower_bound = 0, count = 1, space = 0, resource_class = 0;
                if (parts.size() > 0) parseUnsignedLiteral(parts[0], lower_bound);
                if (parts.size() > 1) parseUnsignedLiteral(parts[1], count);
                if (parts.size() > 2) parseUnsignedLiteral(parts[2], space);
                if (parts.size() > 3) parseUnsignedLiteral(parts[3], resource_class);
                recordDescriptorRange(plan, {descriptorKindForResourceClass(resource_class),
                                             space, lower_bound, count});
            } else if (intrinsic_id == DXOP_CreateHandleFromHeap && fn_args.size() >= 1) {
                uint32_t heap_index = literalFromValue(ctx, fn_args[0], 0);
                bool sampler = fn_args.size() >= 2 && literalFromValue(ctx, fn_args[1], 0) != 0;
                recordDescriptorRange(plan, {sampler ? DescriptorRangePlan::Kind::Sampler
                                                     : DescriptorRangePlan::Kind::SRV,
                                             0, heap_index, 1});
            }
        }
    }

    uint32_t max_sampler = 0;
    bool has_sampler = false;
    for (const auto &range : plan.ranges) {
        if (range.kind == DescriptorRangePlan::Kind::Sampler) {
            has_sampler = true;
            max_sampler = std::max(max_sampler, range.lower_bound + range.count);
        }
    }
    if (has_sampler)
        plan.direct_sampler_count = std::max<uint32_t>(1, std::min<uint32_t>(max_sampler, 4));
    ctx.binding_plan = std::move(plan);
}

static MSLType inferDXIntrinsicResultType(LowerContext &ctx, uint32_t intrinsic_id,
                                          const std::vector<uint32_t> &args,
                                          MSLType declared = {}) {
    switch (intrinsic_id) {
    case DXOP_CreateHandle:
    case DXOP_CreateHandleForLib:
    case DXOP_AnnotateHandle:
    case DXOP_CreateHandleFromBinding:
    case DXOP_CreateHandleFromHeap:
        return {MSLTypeKind::DeviceCharPtr, 0, {}};
    case DXOP_TextureStore:
    case DXOP_BufferStore:
    case DXOP_RawBufferStore:
    case DXOP_Barrier:
    case 225:
    case 1026:
        return {MSLTypeKind::Void, 0, {}};
    case DXOP_CBufferLoad:
    case DXOP_CBufferLoadLegacy:
    case DXOP_BufferLoad:
    case DXOP_TextureLoad:
    case DXOP_TextureSample:
    case DXOP_TextureSampleBias:
    case DXOP_TextureSampleLevel:
    case DXOP_TextureSampleGrad:
    case DXOP_TextureGather:
    case DXOP_TextureGatherCmp:
    case DXOP_TextureGatherRaw:
        return {MSLTypeKind::Float4, 0, {}};
    case DXOP_RawBufferLoad:
    case 303:
    case 1025:
    case DXOP_GetDimensions:
        return {MSLTypeKind::UInt4, 0, {}};
    case DXOP_TextureSampleCmp:
    case DXOP_TextureSampleCmpLevelZero:
    case DXOP_TextureSampleCmpLevel:
    case DXOP_CalcLOD:
    case DXOP_Dot2:
    case DXOP_Dot3:
    case DXOP_Dot4:
    case DXOP_LegacyF16ToF32:
        return {MSLTypeKind::Float, 0, {}};
    case DXOP_CheckAccessFullyMapped:
    case DXOP_WaveIsFirstLane:
    case DXOP_WaveAnyTrue:
    case DXOP_WaveAllTrue:
        return {MSLTypeKind::Bool, 0, {}};
    case DXOP_ThreadId:
    case DXOP_GroupId:
    case DXOP_ThreadIDInGroup:
    case DXOP_FlattenedThreadIDInGroup:
    case DXOP_BufferUpdateCounter:
    case DXOP_AtomicBinOp:
    case DXOP_AtomicCompareExchange:
    case DXOP_WaveGetLaneIndex:
    case DXOP_WaveGetLaneCount:
    case DXOP_LegacyF32ToF16:
        return {MSLTypeKind::UInt, 0, {}};
    case DXOP_DerivCoarseX:
    case DXOP_DerivCoarseY:
    case DXOP_DerivFineX:
    case DXOP_DerivFineY:
        return args.empty() ? MSLType{MSLTypeKind::Float, 0, {}} : valueTypeOrUnknown(ctx, args[0]);
    case DXOP_WaveReadLaneAt:
    case DXOP_WaveReadLaneFirst:
    case DXOP_QuadReadLaneAt:
        return args.size() > 1 ? valueTypeOrUnknown(ctx, args[1]) : declared;
    case DXOP_Unary: {
        uint32_t op = args.empty() ? 0xFFFFFFFFu : literalFromValue(ctx, args[0], 0xFFFFFFFFu);
        MSLType operand = args.size() > 1 ? valueTypeOrUnknown(ctx, args[1]) : declared;
        switch (op) {
        case DXILOP_IsNaN:
        case DXILOP_IsInf:
        case DXILOP_IsFinite:
            return {MSLTypeKind::Bool, 0, {}};
        case DXILOP_Countbits:
        case DXILOP_FirstbitLo:
        case DXILOP_FirstbitHi:
        case DXILOP_FirstbitSHi:
            if (DXILIRBuilder::isVectorType(operand))
                return DXILIRBuilder::vectorOfType({MSLTypeKind::Int, 0, {}}, DXILIRBuilder::vectorWidth(operand));
            return {MSLTypeKind::Int, 0, {}};
        default:
            return usableType(operand) ? operand : MSLType{MSLTypeKind::Float, 0, {}};
        }
    }
    case DXOP_Binary: {
        uint32_t op = args.empty() ? 0xFFFFFFFFu : literalFromValue(ctx, args[0], 0xFFFFFFFFu);
        MSLType a = args.size() > 1 ? valueTypeOrUnknown(ctx, args[1]) : MSLType{};
        MSLType b = args.size() > 2 ? valueTypeOrUnknown(ctx, args[2]) : MSLType{};
        MSLType promoted = promoteNumericType(a, b, {MSLTypeKind::Int, 0, {}});
        if (op == DXILOP_UMax || op == DXILOP_UMin || op == DXILOP_UMul || op == DXILOP_UDiv ||
            op == DXILOP_UAddc || op == DXILOP_USubb)
            return DXILIRBuilder::isVectorType(promoted)
                ? DXILIRBuilder::vectorOfType({MSLTypeKind::UInt, 0, {}}, DXILIRBuilder::vectorWidth(promoted))
                : MSLType{MSLTypeKind::UInt, 0, {}};
        return promoted;
    }
    case DXOP_Tertiary: {
        MSLType a = args.size() > 1 ? valueTypeOrUnknown(ctx, args[1]) : MSLType{};
        MSLType b = args.size() > 2 ? valueTypeOrUnknown(ctx, args[2]) : MSLType{};
        MSLType c = args.size() > 3 ? valueTypeOrUnknown(ctx, args[3]) : MSLType{};
        return promoteNumericType(promoteNumericType(a, b, declared), c, {MSLTypeKind::Int, 0, {}});
    }
    case DXOP_LoadInput:
        return {MSLTypeKind::Float, 0, {}};
    case DXOP_StoreOutput:
        return {MSLTypeKind::Void, 0, {}};
    default:
        break;
    }
    return declared;
}

static std::string resolveBindingName(const std::string &handle, const char *target_prefix) {
    if (startsWith(handle, "agg(")) {
        auto parts = parseAggregateLiteral(handle);
        uint32_t lower_bound = 0;
        if (!parts.empty()) parseUnsignedLiteral(parts[0], lower_bound);
        return std::string(target_prefix) + std::to_string(lower_bound);
    }
    for (auto *prefix : {"tex", "buf", "samp"}) {
        if (startsWith(handle, prefix)) {
            return std::string(target_prefix) + std::string(handle.c_str() + std::strlen(prefix));
        }
    }
    return handle;
}

static std::string translateDXIntrinsic(LowerContext &ctx, uint32_t intrinsic_id,
                                          const std::vector<uint32_t> &args) {
    ctx.pending_handle.reset();

    auto valueArg = [&](size_t arg, const char *fallback) -> std::string {
        if (arg >= args.size()) return fallback;
        uint32_t idx = args[arg];
        if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty()) {
            const auto &v = ctx.value_table[idx];
            if (v.find('.') != std::string::npos) return fallback;
            return v;
        }
        return fallback;
    };

    auto numericArg = [&](size_t arg, const char *fallback) -> std::string {
        if (arg >= args.size()) return fallback;
        uint32_t idx = args[arg];
        std::string value = valueArg(arg, fallback);
        MSLType type = valueTypeOrUnknown(ctx, idx);
        if (exprLooksResourceHandle(value) || typeLooksResourceHandle(type) ||
            value.find("char*") != std::string::npos ||
            value.find("char *") != std::string::npos ||
            value.find("device ") != std::string::npos ||
            value.find("threadgroup ") != std::string::npos)
            return fallback;
        return value;
    };

    auto handleArg = [&](size_t arg, const char *prefix, const char *fallback) -> std::string {
        if (arg >= args.size()) return fallback;
        uint32_t idx = args[arg];
        auto handle_it = ctx.resource_handles.find(idx);
        if (handle_it != ctx.resource_handles.end())
            return materializeHandleName(ctx, handle_it->second, prefix);
        auto it = ctx.buffer_origin.find(idx);
        if (it != ctx.buffer_origin.end()) return it->second;
        std::string value = valueArg(arg, fallback);
        MSLType type = valueTypeOrUnknown(ctx, idx);
        if (startsWith(value, "buf") || startsWith(value, "tex") || startsWith(value, "samp") ||
            type.kind == MSLTypeKind::DeviceCharPtr ||
            type.kind == MSLTypeKind::ThreadgroupCharPtr ||
            type.kind == MSLTypeKind::Texture2D ||
            type.kind == MSLTypeKind::RWTexture2D ||
            type.kind == MSLTypeKind::Sampler)
            return resolveBindingName(value, prefix);
        return fallback;
    };

    auto recordHandle = [&](DescriptorRangePlan::Kind kind, uint32_t resource_class,
                            uint32_t lower_bound, uint32_t binding_index,
                            uint32_t register_space = 0, bool non_uniform = false) -> std::string {
        ResourceHandleRecord handle;
        handle.kind = kind;
        handle.resource_class = resource_class;
        handle.register_space = register_space;
        handle.lower_bound = lower_bound;
        handle.binding_index = binding_index;
        handle.non_uniform = non_uniform;
        ctx.pending_handle = handle;
        return materializeHandleName(ctx, handle);
    };

    auto literalArg = [&](size_t arg, uint32_t fallback, const char *label) -> uint32_t {
        if (arg >= args.size()) return fallback;
        uint32_t idx = args[arg];
        std::string text;
        if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty())
            text = ctx.value_table[idx];
        else {
            for (auto &c : ctx.mod.constants)
                if (c.id == idx && !c.constant_data.empty()) { text = c.constant_data; break; }
            if (text.empty() && ctx.current_fn)
                for (auto &c : ctx.current_fn->constants)
                    if (c.id == idx && !c.constant_data.empty()) { text = c.constant_data; break; }
        }
        uint32_t value = 0;
        if (parseUnsignedLiteral(text, value)) return value;
        return fallback;
    };

    switch (intrinsic_id) {
    case DXOP_CreateHandle: {
        if (args.size() < 4) return "0";
        uint32_t resource_class = literalArg(0, 0, "resource class");
        uint32_t range_id = literalArg(1, 0, "range id");
        bool non_uniform = args.size() >= 4 && literalArg(3, 0, "non uniform") != 0;
        ctx.next_binding++;
        return recordHandle(descriptorKindForResourceClass(resource_class),
                            resource_class, range_id, 0, 0, non_uniform);
    }
    case DXOP_CreateHandleForLib: case DXOP_AnnotateHandle: {
        if (!args.empty()) {
            auto handle_it = ctx.resource_handles.find(args[0]);
            if (handle_it != ctx.resource_handles.end()) {
                ctx.pending_handle = handle_it->second;
                return materializeHandleName(ctx, handle_it->second);
            }
        }
        auto handle = valueArg(0, "tex0");
        if (startsWith(handle, "agg(")) handle = resolveBindingName(handle, "buf");
        return handle;
    }
    case DXOP_CreateHandleFromBinding: {
        auto binding = valueArg(0, "");
        auto bvals = parseAggregateLiteral(binding);
        uint32_t lower_bound = 0, resource_class = 0;
        uint32_t count = 1, register_space = 0;
        if (bvals.size() > 0) parseUnsignedLiteral(bvals[0], lower_bound);
        if (bvals.size() > 1) parseUnsignedLiteral(bvals[1], count);
        if (bvals.size() > 2) parseUnsignedLiteral(bvals[2], register_space);
        if (bvals.size() > 3) parseUnsignedLiteral(bvals[3], resource_class);
        uint32_t index = args.size() >= 2 ? literalArg(1, 0, "idx") : 0;
        if (count != 0)
            index = std::min<uint32_t>(index, count - 1);
        bool non_uniform = args.size() >= 3 && literalArg(2, 0, "non uniform") != 0;
        return recordHandle(descriptorKindForResourceClass(resource_class),
                            resource_class, lower_bound, index, register_space, non_uniform);
    }
    case DXOP_CreateHandleFromHeap: {
        uint32_t heap_index = literalArg(0, 0, "heap");
        bool sampler = args.size() >= 2 && literalArg(1, 0, "samp") != 0;
        return recordHandle(sampler ? DescriptorRangePlan::Kind::Sampler
                                    : DescriptorRangePlan::Kind::SRV,
                            sampler ? 3 : 0, heap_index, 0);
    }
    case DXOP_ThreadId: {
        ctx.uses_thread_id = true;
        uint32_t c = args.empty() ? 0 : literalArg(0, 0, "comp");
        if (c == 0) return "(int)dtid.x"; if (c == 1) return "(int)dtid.y"; return "(int)dtid.z";
    }
    case DXOP_GroupId: {
        ctx.uses_group_id = true;
        uint32_t c = args.empty() ? 0 : literalArg(0, 0, "comp");
        if (c == 0) return "(int)ggid.x"; if (c == 1) return "(int)ggid.y"; return "(int)ggid.z";
    }
    case DXOP_ThreadIDInGroup: {
        ctx.uses_group_thread_id = true;
        uint32_t c = args.empty() ? 0 : literalArg(0, 0, "comp");
        if (c == 0) return "(int)gtid.x"; if (c == 1) return "(int)gtid.y"; return "(int)gtid.z";
    }
    case DXOP_FlattenedThreadIDInGroup:
        ctx.uses_group_thread_id = true; ctx.uses_group_size = true;
        return "(int)(gtid.x + gtid.y * gsz.x + gtid.z * gsz.x * gsz.y)";
    case DXOP_CBufferLoad: case DXOP_CBufferLoadLegacy: {
        if (args.size() < 2) return "float4(0)";
        auto handle = handleArg(0, "buf", "buf0");
        ctx.last_buffer_handle = handle;
        auto reg = ensureScalarIndex(valueArg(1, "0"));
        return "(reinterpret_cast<device float4&>(" + handle + "[(" + reg + ")*64]))";
    }
    case DXOP_BufferLoad: {
        if (args.size() < 3) return "float4(0)";
        auto handle = handleArg(0, "buf", "buf0");
        ctx.last_buffer_handle = handle;
        auto idx = ensureScalarIndex(valueArg(1, "0"));
        return "(reinterpret_cast<device float4&>(" + handle + "[(" + idx + ")*16]))";
    }
    case DXOP_RawBufferLoad: case 303: case 1025: {
        if (args.size() < 3) return "uint4(0)";
        auto handle = handleArg(0, "buf", "buf0");
        ctx.last_buffer_handle = handle;
        auto idx = ensureScalarIndex(valueArg(1, "0"));
        auto off = ensureScalarIndex(valueArg(2, "0"));
        return "(reinterpret_cast<device uint4&>(" + handle + "[((" + idx + ")*4 + (" + off + "))]))";
    }
    case DXOP_BufferStore: case DXOP_RawBufferStore: case 1026: {
        if (args.size() < 4) return "";
        auto handle = handleArg(0, "buf", "buf0");
        auto idx = ensureScalarIndex(valueArg(1, "0"));
        auto off = ensureScalarIndex(valueArg(2, "0"));
        std::string base = "((" + idx + ")*4 + (" + off + "))";
        std::ostringstream store;
        uint32_t vc = std::min<uint32_t>(4, (uint32_t)args.size() - 3);
        for (uint32_t i = 0; i < vc; i++) {
            if (i) store << ";\n  ";
            store << "reinterpret_cast<device uint&>(" << handle << "[(" << base << ") + " << (i*4)
                  << "]) = (uint)(" << valueArg(3+i, "0") << ")";
        }
        return store.str();
    }
    case 304: {
        if (args.size() < 4) return "";
        auto handle = handleArg(0, "buf", "buf0");
        auto idx = ensureScalarIndex(valueArg(1, "0"));
        auto off = ensureScalarIndex(valueArg(2, "0"));
        auto val = valueArg(3, "uint4(0)");
        std::string base = "((" + idx + ")*4 + (" + off + "))";
        std::ostringstream store;
        for (uint32_t i = 0; i < 4; i++) {
            if (i) store << ";\n  ";
            store << "reinterpret_cast<device uint&>(" << handle << "[(" << base << ") + " << (i*4)
                  << "]) = (uint)(" << val << componentSuffix(i) << ")";
        }
        return store.str();
    }
    case DXOP_TextureLoad: {
        if (args.size() < 3) return "float4(0)";
        auto handle = handleArg(0, "tex", "tex0");
        ctx.last_buffer_handle = handle;
        auto cx = ensureScalarIndex(valueArg(2, "0"));
        auto cy = ensureScalarIndex(valueArg(3, "0"));
        return handle + ".read(uint2(" + cx + ", " + cy + "))";
    }
    case DXOP_TextureStore: case 225: {
        if (args.size() < 6) return "";
        auto handle = handleArg(0, "tex", "tex0");
        auto cx = ensureScalarIndex(valueArg(1, "0"));
        auto cy = ensureScalarIndex(valueArg(2, "0"));
        size_t vb = intrinsic_id == 225 ? 5 : 4;
        return handle + ".write(float4(" + numericArg(vb, "0.0") + ", " + numericArg(vb+1, "0.0") +
               ", " + numericArg(vb+2, "0.0") + ", " + numericArg(vb+3, "0.0") +
               "), uint2(" + cx + ", " + cy + "))";
    }
    case DXOP_TextureSample: case DXOP_TextureSampleBias:
    case DXOP_TextureSampleLevel: case DXOP_TextureSampleGrad: {
        if (args.size() < 4) return "float4(0)";
        auto handle = handleArg(0, "tex", "tex0");
        auto samp = handleArg(1, "samp", "samp0");
        auto cx = ensureScalarIndex(valueArg(2, "0.0"));
        auto cy = ensureScalarIndex(valueArg(3, "0.0"));
        if (ctx.shader.kind == DxilShaderKind::Compute)
            return handle + ".read(uint2((uint)(" + cx + "), (uint)(" + cy + ")))";
        return handle + ".sample(" + samp + ", float2(" + cx + ", " + cy + "))";
    }
    case DXOP_TextureGather: case 74: case 223: {
        if (args.size() < 4) return "float4(0)";
        auto handle = handleArg(0, "tex", "tex0");
        auto samp = handleArg(1, "samp", "samp0");
        auto cx = ensureScalarIndex(valueArg(2, "0.0"));
        auto cy = ensureScalarIndex(valueArg(3, "0.0"));
        uint32_t ch = args.size() > 8 ? literalArg(8, 0, "ch") : 0;
        return handle + ".gather(" + samp + ", float2(" + cx + ", " + cy + "), component::" + componentName(ch) + ")";
    }
    case DXOP_TextureSampleCmp: case DXOP_TextureSampleCmpLevelZero: case 224: {
        if (args.size() < 5) return "0.0";
        auto handle = handleArg(0, "tex", "tex0");
        auto samp = handleArg(1, "samp", "samp0");
        auto cx = ensureScalarIndex(valueArg(2, "0.0"));
        auto cy = ensureScalarIndex(valueArg(3, "0.0"));
        auto cmp = valueArg(4, "0.0");
        if (ctx.shader.kind == DxilShaderKind::Compute)
            return "((" + handle + ".read(uint2((uint)(" + cx + "), (uint)(" + cy + "))).r) < (" + cmp + ") ? 1.0 : 0.0)";
        return "((" + handle + ".sample(" + samp + ", float2(" + cx + ", " + cy + ")).r) < (" + cmp + ") ? 1.0 : 0.0)";
    }
    case 70: return "0";
    case 71: return "true";
    case 72: {
        auto handle = handleArg(0, "tex", "tex0");
        return "uint4(" + handle + ".get_width(), " + handle + ".get_height(), 1, 1)";
    }
    case 83: case 85: return "dfdx(" + valueArg(0, "0.0") + ")";
    case 84: case 86: return "dfdy(" + valueArg(0, "0.0") + ")";
    case 81: {
        if (args.size() < 4) return "0.0";
        auto handle = handleArg(0, "tex", "tex0");
        auto samp = handleArg(1, "samp", "samp0");
        return handle + ".calculate_unclamped_lod(" + samp + ", float2(" + ensureScalarIndex(valueArg(2, "0.0")) + ", " + ensureScalarIndex(valueArg(3, "0.0")) + "))";
    }
    case 78: {
        if (args.size() < 4) return "0";
        auto handle = handleArg(1, "buf", "buf0");
        auto off = ensureScalarIndex(valueArg(2, "0"));
        auto val = ensureScalarIndex(valueArg(3, "0"));
        return "atomic_fetch_add_explicit(reinterpret_cast<device atomic_uint*>(" + handle + " + (" + off + ")), (uint)(" + val + "), memory_order_relaxed)";
    }
    case 79: {
        if (args.size() < 2) return "0";
        auto handle = handleArg(0, "buf", "buf0");
        return "atomic_load_explicit(reinterpret_cast<device atomic_uint*>(" + handle + " + (" + ensureScalarIndex(valueArg(1, "0")) + ")), memory_order_relaxed)";
    }
    case 75: case 76: case 97: case 98: return "0.5";
    case 77: return "1";
    case 109: return "0";
    case 80: return "threadgroup_barrier(mem_flags::mem_threadgroup)";
    case DXOP_Unary: {
        if (args.size() < 2) return "0";
        uint32_t op = literalArg(0, 0xFFFFFFFFu, "unary");
        bool int_op = op == DXILOP_Bfrev || op == DXILOP_Countbits ||
                      op == DXILOP_FirstbitLo || op == DXILOP_FirstbitHi ||
                      op == DXILOP_FirstbitSHi;
        auto x = numericArg(1, int_op ? "0" : "0.0");
        switch (op) {
        case DXILOP_FAbs: return "abs(" + x + ")";
        case DXILOP_Saturate: return "clamp(" + x + ", 0.0, 1.0)";
        case DXILOP_IsNaN: return "isnan(" + x + ")";
        case DXILOP_IsInf: return "isinf(" + x + ")";
        case DXILOP_IsFinite: return "isfinite(" + x + ")";
        case DXILOP_Cos: return "cos(" + x + ")";
        case DXILOP_Sin: return "sin(" + x + ")";
        case DXILOP_Tan: return "tan(" + x + ")";
        case DXILOP_Acos: return "acos(" + x + ")";
        case DXILOP_Asin: return "asin(" + x + ")";
        case DXILOP_Atan: return "atan(" + x + ")";
        case DXILOP_Exp: return "exp2(" + x + ")";
        case DXILOP_Frc: return "fract(" + x + ")";
        case DXILOP_Log: return "log2(" + x + ")";
        case DXILOP_Sqrt: return "sqrt(" + x + ")";
        case DXILOP_Rsqrt: return "rsqrt(" + x + ")";
        case DXILOP_Round_ne: return "rint(" + x + ")";
        case DXILOP_Round_ni: return "floor(" + x + ")";
        case DXILOP_Round_pi: return "ceil(" + x + ")";
        case DXILOP_Round_z: return "trunc(" + x + ")";
        case DXILOP_Bfrev: return "reverse_bits(" + x + ")";
        case DXILOP_Countbits: return "popcount(" + x + ")";
        case DXILOP_FirstbitLo: return "ctz(" + x + ")";
        case DXILOP_FirstbitHi: return "clz(" + x + ")";
        case DXILOP_FirstbitSHi: return "((" + x + ") < 0 ? clz(~(" + x + ")) : clz(" + x + "))";
        default: ctx.unsupported_intrinsics++; return x;
        }
    }
    case DXOP_Binary: {
        if (args.size() < 3) return "0";
        uint32_t op = literalArg(0, 0xFFFFFFFFu, "binary");
        auto a = numericArg(1, "0"), b = numericArg(2, "0");
        switch (op) {
        case DXILOP_FMax: case DXILOP_IMax: return "max(" + a + ", " + b + ")";
        case DXILOP_FMin: case DXILOP_IMin: return "min(" + a + ", " + b + ")";
        case DXILOP_UMax: return "max((uint)(" + a + "), (uint)(" + b + "))";
        case DXILOP_UMin: return "min((uint)(" + a + "), (uint)(" + b + "))";
        case DXILOP_IMul: return "mul24(" + a + ", " + b + ")";
        case DXILOP_UMul: return "mul24((uint)(" + a + "), (uint)(" + b + "))";
        case DXILOP_UDiv: return "((uint)(" + a + ") / (uint)(" + b + "))";
        case DXILOP_UAddc: return "((" + a + ") + (" + b + "))";
        case DXILOP_USubb: return "((" + a + ") - (" + b + "))";
        default: ctx.unsupported_intrinsics++; return a;
        }
    }
    case DXOP_Tertiary: {
        if (args.size() < 4) return "0";
        uint32_t op = literalArg(0, 0xFFFFFFFFu, "tertiary");
        auto a = numericArg(1, "0"), b = numericArg(2, "0"), c = numericArg(3, "0");
        switch (op) {
        case DXILOP_FMad: case DXILOP_Fma: return "fma(" + a + ", " + b + ", " + c + ")";
        case DXILOP_IMad: case DXILOP_UMad: return "((" + a + ") * (" + b + ") + (" + c + "))";
        case DXILOP_Ibfe: return "extract_bits(" + a + ", " + b + ", " + c + ")";
        case DXILOP_Ubfe: return "extract_bits((uint)(" + a + "), (uint)(" + b + "), (uint)(" + c + "))";
        case DXILOP_Bfi: return "insert_bits((uint)(" + b + "), (uint)(" + a + "), (uint)(" + c + "))";
        default: ctx.unsupported_intrinsics++; return a;
        }
    }
    case DXOP_Dot2: {
        if (args.size() < 4) return "0.0";
        return "((" + numericArg(0,"0.0") + ")*(" + numericArg(2,"0.0") + ") + (" + numericArg(1,"0.0") + ")*(" + numericArg(3,"0.0") + "))";
    }
    case DXOP_Dot3: {
        if (args.size() < 6) return "0.0";
        return "((" + numericArg(0,"0.0") + ")*(" + numericArg(3,"0.0") + ") + (" + numericArg(1,"0.0") + ")*(" + numericArg(4,"0.0") + ") + (" + numericArg(2,"0.0") + ")*(" + numericArg(5,"0.0") + "))";
    }
    case DXOP_Dot4: {
        if (args.size() < 8) return "0.0";
        return "((" + numericArg(0,"0.0") + ")*(" + numericArg(4,"0.0") + ") + (" + numericArg(1,"0.0") + ")*(" + numericArg(5,"0.0") + ") + (" + numericArg(2,"0.0") + ")*(" + numericArg(6,"0.0") + ") + (" + numericArg(3,"0.0") + ")*(" + numericArg(7,"0.0") + "))";
    }
    case DXOP_LoadInput: {
        if (args.size() < 3) return "0.0";
        uint32_t input_id = literalArg(0, 0, "input");
        uint32_t comp = literalArg(2, 0, "comp");
        if (ctx.shader.kind == DxilShaderKind::Pixel) return varyingField("in", input_id) + componentSuffix(comp);
        if (ctx.shader.kind == DxilShaderKind::Vertex) return vertexInputField("vin", input_id) + componentSuffix(comp);
        return "0.0";
    }
    case DXOP_StoreOutput: {
        if (args.size() < 4) return "";
        uint32_t output_id = literalArg(0, 0, "output");
        uint32_t comp = literalArg(2, 0, "comp");
        auto val = numericArg(3, "0.0");
        if (ctx.shader.kind == DxilShaderKind::Vertex) return varyingField("out", output_id) + componentSuffix(comp) + " = " + val;
        if (ctx.shader.kind == DxilShaderKind::Pixel) return std::string("result") + componentSuffix(comp) + " = " + val;
        return "";
    }
    case 131: return "static_cast<float>(half(" + numericArg(1, "0") + "))";
    case 132: return "static_cast<uint>(half(" + numericArg(1, "0.0") + "))";
    case 118: return numericArg(1, "0");
    case 117: return numericArg(1, "0");
    case 110: return "true";
    case 111: return "0";
    case 112: return "1";
    case 113: return "simd_any(" + numericArg(1, "0") + ") ? 1 : 0";
    case 114: return "simd_all(" + numericArg(1, "0") + ") ? 1 : 0";
    case 122: return "quad_broadcast(" + numericArg(1, "0") + ", (uint)(" + numericArg(2, "0") + "))";
    default:
        ctx.unsupported_intrinsics++;
        break;
    }
    return "0";
}

static void emitTypedInstruction(LowerContext &ctx, const LLVMInstruction &inst, uint32_t &value_counter) {
    auto &os = ctx.os;
    std::string result = emitValue(value_counter);

    auto getValue = [&](uint32_t idx) -> std::string {
        if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty()) {
            const auto &v = ctx.value_table[idx];
            if (startsWith(v, "dx.")) {
                // function name, not a value — fall through to constants/emitValue
            } else if (startsWith(v, "agg(")) {
                MSLType type = idx < ctx.value_types.size() ? ctx.value_types[idx] : MSLType{};
                return aggregateConstructor(v, type);
            } else if (v.find('.') != std::string::npos) {
                return v;
            } else {
                return v;
            }
        }
        for (auto &c : ctx.mod.constants)
            if (c.id == idx && !c.constant_data.empty())
                return normalizeAggregateExpressions(c.constant_data, DXILIRBuilder::resolveType(c.type_id, ctx.mod));
        if (ctx.current_fn)
            for (auto &c : ctx.current_fn->constants)
                if (c.id == idx && !c.constant_data.empty())
                    return normalizeAggregateExpressions(c.constant_data, DXILIRBuilder::resolveType(c.type_id, ctx.mod));
        return emitValue(idx);
    };

    auto ensureValueTable = [&](uint32_t needed) {
        if (ctx.value_table.size() <= needed) ctx.value_table.resize(needed + 1);
        if (ctx.value_types.size() <= needed) ctx.value_types.resize(needed + 1);
        if (ctx.value_roles.size() <= needed) ctx.value_roles.resize(needed + 1);
    };

    auto getTypeForInst = [&](uint32_t type_id) -> MSLType {
        if (type_id > 0 && type_id < ctx.mod.types.size())
            return DXILIRBuilder::resolveType(type_id, ctx.mod);
        return {MSLTypeKind::Unknown, 0, {}};
    };

    auto inferTypeFromExpr = [](const std::string &expr) -> MSLType {
        if (startsWith(expr, "buf"))
            return {MSLTypeKind::DeviceCharPtr, 0, {}};
        if (startsWith(expr, "tex"))
            return {MSLTypeKind::RWTexture2D, 0, {}};
        if (startsWith(expr, "samp"))
            return {MSLTypeKind::Sampler, 0, {}};
        if (expr.find("reinterpret_cast<device float4&>") != std::string::npos)
            return {MSLTypeKind::Float4, 0, {}};
        if (expr.find("reinterpret_cast<device uint4&>") != std::string::npos)
            return {MSLTypeKind::UInt4, 0, {}};
        if (expr.find("reinterpret_cast<device int4&>") != std::string::npos)
            return {MSLTypeKind::Int4, 0, {}};
        if (expr.find("reinterpret_cast<device float&>") != std::string::npos)
            return {MSLTypeKind::Float, 0, {}};
        if (expr.find("reinterpret_cast<device uint&>") != std::string::npos)
            return {MSLTypeKind::UInt, 0, {}};
        if (expr.find(".read(") != std::string::npos)
            return {MSLTypeKind::Float4, 0, {}};
        if (expr.find(".sample(") != std::string::npos)
            return {MSLTypeKind::Float4, 0, {}};
        if (expr.find(".gather(") != std::string::npos)
            return {MSLTypeKind::Float4, 0, {}};
        if (expr.find("float4(") == 0 || expr.find("(float4(") != std::string::npos)
            return {MSLTypeKind::Float4, 0, {}};
        if (expr.find("uint4(") == 0 || expr.find("(uint4(") != std::string::npos)
            return {MSLTypeKind::UInt4, 0, {}};
        if (expr.find("int4(") == 0 || expr.find("(int4(") != std::string::npos)
            return {MSLTypeKind::Int4, 0, {}};
        if (expr.find("float2(") == 0)
            return {MSLTypeKind::Float2, 0, {}};
        if (expr.find("float3(") == 0)
            return {MSLTypeKind::Float3, 0, {}};
        return {MSLTypeKind::Unknown, 0, {}};
    };

    auto bestType = [&](MSLType declared, const std::string &expr) -> MSLType {
        auto inferred = inferTypeFromExpr(expr);
        if (inferred.kind != MSLTypeKind::Unknown) return inferred;
        return declared;
    };

    auto emitTypedLine = [&](MSLType &type, const std::string &name, const std::string &expr) {
        if (type.kind == MSLTypeKind::Unknown || type.kind == MSLTypeKind::Void ||
            type.kind == MSLTypeKind::Struct) {
            auto inferred = inferTypeFromExpr(expr);
            if (inferred.kind != MSLTypeKind::Unknown) type = inferred;
            else if (exprContainsRawResourceHandle(expr)) type = {MSLTypeKind::Int, 0, {}};
        }
        if (ctx.predeclared_names.find(name) != ctx.predeclared_names.end()) {
            MSLType declared_type = type;
            auto pre_it = ctx.predeclared_types.find(name);
            if (pre_it != ctx.predeclared_types.end())
                declared_type = pre_it->second;
            std::string assigned = coerceResolvedValue(expr, declared_type);
            uint32_t source_id = 0;
            if (assigned == expr && parseEmittedValueName(expr, source_id) &&
                source_id < ctx.value_types.size() &&
                DXILIRBuilder::isVectorType(ctx.value_types[source_id]) &&
                (DXILIRBuilder::isFloatType(declared_type) || DXILIRBuilder::isIntType(declared_type)) &&
                !DXILIRBuilder::isVectorType(declared_type)) {
                assigned = "(" + expr + ").x";
            }
            os << "  " << name << " = " << assigned << ";\n";
            return;
        }
        std::string assigned = coerceResolvedValue(expr, type);
        if (type.kind != MSLTypeKind::Unknown && type.kind != MSLTypeKind::Void &&
            type.kind != MSLTypeKind::Struct)
            os << "  " << emitTypeName(type) << " " << name << " = " << assigned << ";\n";
        else
            os << "  auto " << name << " = " << assigned << ";\n";
    };

    auto promoteType = [](const MSLType &a, const MSLType &b) -> MSLType {
        if (DXILIRBuilder::isVectorType(a)) return a;
        if (DXILIRBuilder::isVectorType(b)) return b;
        if (a.kind == MSLTypeKind::Float || a.kind == MSLTypeKind::Double ||
            a.kind == MSLTypeKind::Half) return a;
        if (b.kind == MSLTypeKind::Float || b.kind == MSLTypeKind::Double ||
            b.kind == MSLTypeKind::Half) return b;
        return a;
    };

    auto isPointerType = [](const MSLType &t) -> bool {
        return t.kind == MSLTypeKind::DeviceCharPtr || t.kind == MSLTypeKind::ThreadgroupCharPtr;
    };

    auto isUsableType = [](const MSLType &t) -> bool {
        return t.kind != MSLTypeKind::Unknown && t.kind != MSLTypeKind::Void &&
               t.kind != MSLTypeKind::Struct;
    };

    auto valueType = [&](uint32_t idx) -> MSLType {
        if (idx < ctx.value_types.size()) return ctx.value_types[idx];
        return {};
    };

    auto operandType = [&](uint32_t idx) -> MSLType {
        MSLType tracked = valueType(idx);
        MSLType inferred = inferTypeFromExpr(getValue(idx));
        if (isUsableType(inferred) &&
            (!isUsableType(tracked) || DXILIRBuilder::isVectorType(inferred)))
            return inferred;
        return tracked;
    };

    auto demotePointerType = [&](MSLType t, MSLTypeKind scalar_kind = MSLTypeKind::Int) -> MSLType {
        if (!isPointerType(t)) return t;
        return {scalar_kind, 0, {}};
    };

    auto integerTypeFor = [](MSLType t) -> MSLType {
        switch (t.kind) {
        case MSLTypeKind::Float2: return {MSLTypeKind::Int2, 0, {}};
        case MSLTypeKind::Float3: return {MSLTypeKind::Int3, 0, {}};
        case MSLTypeKind::Float4: return {MSLTypeKind::Int4, 0, {}};
        case MSLTypeKind::Float:
        case MSLTypeKind::Half:
        case MSLTypeKind::Double:
            return {MSLTypeKind::Int, 0, {}};
        default:
            return t;
        }
    };

    auto castExpr = [&](const std::string &expr, const MSLType &target) -> std::string {
        std::string type_name = emitTypeName(target);
        if (type_name.empty() || type_name == "auto" || type_name == "void") return expr;
        if (DXILIRBuilder::isVectorType(target))
            return type_name + "(" + expr + ")";
        return "static_cast<" + type_name + ">(" + expr + ")";
    };

    auto coerceOperand = [&](uint32_t idx, const MSLType &target) -> std::string {
        std::string value = getValue(idx);
        MSLType source = operandType(idx);
        if ((exprLooksResourceHandle(value) || exprContainsRawResourceHandle(value) ||
             typeLooksResourceHandle(source)) &&
            (DXILIRBuilder::isFloatType(target) || DXILIRBuilder::isIntType(target)))
            return defaultForType(target);
        if (!isUsableType(target) || target.kind == source.kind)
            return value;

        if (isPointerType(source)) {
            if (DXILIRBuilder::isFloatType(target) || DXILIRBuilder::isIntType(target))
                return defaultForType(target);
            return value;
        }

        if (source.kind == MSLTypeKind::Unknown &&
            (DXILIRBuilder::isFloatType(target) || DXILIRBuilder::isIntType(target)))
            return castExpr(value, target);
        if (DXILIRBuilder::isIntType(source) && DXILIRBuilder::isFloatType(target))
            return castExpr(value, target);
        if (DXILIRBuilder::isFloatType(source) && DXILIRBuilder::isIntType(target))
            return castExpr(value, target);
        if (DXILIRBuilder::isVectorType(source) &&
            (DXILIRBuilder::isFloatType(target) || DXILIRBuilder::isIntType(target)) &&
            !DXILIRBuilder::isVectorType(target))
            return castExpr("(" + value + ").x", target);
        if (DXILIRBuilder::isVectorType(source) && DXILIRBuilder::isVectorType(target) &&
            source.kind != target.kind)
            return castExpr(value, target);
        return value;
    };

    auto chooseBinaryType = [&](const MSLType &declared, const MSLType &lhs,
                                const MSLType &rhs, MSLTypeKind pointer_scalar) -> MSLType {
        MSLType result_type = demotePointerType(declared, pointer_scalar);
        if (typeLooksResourceHandle(result_type))
            result_type = {pointer_scalar, 0, {}};
        MSLType op0 = typeLooksResourceHandle(lhs) ? MSLType{pointer_scalar, 0, {}}
                                                   : demotePointerType(lhs, pointer_scalar);
        MSLType op1 = typeLooksResourceHandle(rhs) ? MSLType{pointer_scalar, 0, {}}
                                                   : demotePointerType(rhs, pointer_scalar);

        if (DXILIRBuilder::isVectorType(op0)) result_type = op0;
        else if (DXILIRBuilder::isVectorType(op1)) result_type = op1;
        else if (DXILIRBuilder::isFloatType(op0) || DXILIRBuilder::isFloatType(op1))
            result_type = {pointer_scalar == MSLTypeKind::Float ? MSLTypeKind::Float : MSLTypeKind::Float, 0, {}};
        else if (isUsableType(op0)) result_type = op0;
        else if (isUsableType(op1)) result_type = op1;

        if (!isUsableType(result_type)) result_type = {pointer_scalar, 0, {}};
        return result_type;
    };

    auto pointerAddressSpace = [&](uint32_t idx) -> const char * {
        MSLType type = valueType(idx);
        if (type.kind == MSLTypeKind::ThreadgroupCharPtr)
            return "threadgroup";
        std::string value = getValue(idx);
        if (startsWith(value, "(threadgroup") || startsWith(value, "threadgroup"))
            return "threadgroup";
        if (startsWith(value, "(thread") || startsWith(value, "thread"))
            return "thread";
        return "device";
    };

    switch (inst.opcode) {
    case LLVMInstruction::Ret:
        if (ctx.shader.kind == DxilShaderKind::Vertex) os << "  return out;\n";
        else if (ctx.shader.kind == DxilShaderKind::Pixel) os << "  return result;\n";
        else os << "  return;\n";
        break;

    case LLVMInstruction::Call: {
        if (inst.operands.empty()) break;
        uint32_t callee = inst.operands[0];
        std::vector<uint32_t> call_args;
        for (size_t i = 2; i < inst.operands.size(); i++) call_args.push_back(inst.operands[i]);

        std::string callee_name;
        auto decl_it = ctx.function_decls.find(callee);
        if (decl_it != ctx.function_decls.end()) callee_name = decl_it->second;
        else if (callee < ctx.value_table.size()) callee_name = ctx.value_table[callee];
        uint32_t intrinsic_id = intrinsicIdFromCalleeName(callee_name);

        if (intrinsic_id != 0 && call_args.empty()) {
            ctx.unsupported_intrinsics++;
            ensureValueTable(value_counter);
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = getTypeForInst(inst.type_id);
        } else if (intrinsic_id != 0) {
            std::vector<uint32_t> fn_args;
            if (intrinsic_id == 13 || intrinsic_id == 14 || intrinsic_id == 15)
                fn_args = call_args;
            else
                fn_args.assign(call_args.begin() + 1, call_args.end());

            std::string translated = translateDXIntrinsic(ctx, intrinsic_id, fn_args);
            MSLType result_type = inferDXIntrinsicResultType(
                ctx, intrinsic_id, fn_args, bestType(getTypeForInst(inst.type_id), translated));
            ensureValueTable(value_counter);
            ctx.value_types[value_counter] = result_type;

            if (ctx.pending_handle.has_value()) {
                ResourceHandleRecord handle = *ctx.pending_handle;
                ctx.resource_handles[value_counter] = handle;
                ctx.value_table[value_counter] = materializeHandleName(ctx, handle);
                ctx.value_types[value_counter] = typeForHandleKind(handle.kind);
                ctx.value_roles[value_counter] = roleForHandleKind(handle.kind);
                ctx.pending_handle.reset();
            } else if (inst.type_id == 0) {
                if (!translated.empty()) {
                    os << "  " << translated << ";\n";
                    ctx.value_table[value_counter] = translated;
                }
            } else if (result_type.kind == MSLTypeKind::Void || exprLooksSideEffectOnly(translated)) {
                if (!translated.empty())
                    os << "  " << translated << ";\n";
                MSLType fallback_type = getTypeForInst(inst.type_id);
                if (!isUsableType(fallback_type))
                    fallback_type = {MSLTypeKind::Int, 0, {}};
                if (ctx.predeclared_names.find(result) != ctx.predeclared_names.end()) {
                    os << "  " << result << " = " << defaultForType(fallback_type) << ";\n";
                } else {
                    os << "  " << emitTypeName(fallback_type) << " " << result
                       << " = " << defaultForType(fallback_type) << ";\n";
                }
                ctx.value_table[value_counter] = result;
                ctx.value_types[value_counter] = fallback_type;
            } else if (translated.empty()) {
                MSLType fallback_type = result_type;
                if (!isUsableType(fallback_type))
                    fallback_type = getTypeForInst(inst.type_id);
                if (!isUsableType(fallback_type))
                    fallback_type = {MSLTypeKind::Int, 0, {}};
                if (ctx.predeclared_names.find(result) != ctx.predeclared_names.end()) {
                    os << "  " << result << " = " << defaultForType(fallback_type) << ";\n";
                } else {
                    os << "  " << typedDecl(result, fallback_type) << " = "
                       << defaultForType(fallback_type) << ";\n";
                }
                ctx.value_table[value_counter] = result;
                ctx.value_types[value_counter] = fallback_type;
            } else if (translated.find('=') == std::string::npos) {
                if (!translated.empty() && translated[0] != ' ') {
                    bool is_resource_handle = startsWith(translated, "buf") ||
                                              startsWith(translated, "tex") ||
                                              startsWith(translated, "samp");
                    if (is_resource_handle) {
                        ctx.value_table[value_counter] = translated;
                    } else {
                        emitTypedLine(result_type, result, translated);
                        ctx.value_table[value_counter] = result;
                    }
                    if (!ctx.last_buffer_handle.empty()) {
                        ctx.buffer_origin[value_counter] = ctx.last_buffer_handle;
                        ctx.last_buffer_handle.clear();
                    }
                } else if (!translated.empty()) {
                    os << "  " << translated << ";\n";
                    ctx.value_table[value_counter] = translated;
                }
            } else {
                os << "  " << translated << ";\n";
                ctx.value_table[value_counter] = translated;
            }
        } else {
            ensureValueTable(value_counter);
            MSLType result_type = getTypeForInst(inst.type_id);
            if (!isUsableType(result_type))
                result_type = {MSLTypeKind::Int, 0, {}};
            if (inst.type_id != 0) {
                if (ctx.predeclared_names.find(result) != ctx.predeclared_names.end())
                    os << "  " << result << " = " << defaultForType(result_type) << "; // call " << (callee_name.empty() ? getValue(callee) : callee_name) << "(";
                else
                    os << "  " << typedDecl(result, result_type) << " = 0; // call " << (callee_name.empty() ? getValue(callee) : callee_name) << "(";
            } else {
                os << "  // call " << (callee_name.empty() ? getValue(callee) : callee_name) << "(";
            }
            for (size_t i = 0; i < call_args.size(); i++) {
                if (i) os << ", ";
                os << getValue(call_args[i]);
            }
            os << ")\n";
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::Add: case LLVMInstruction::Sub: case LLVMInstruction::Mul:
    case LLVMInstruction::UDiv: case LLVMInstruction::SDiv:
    case LLVMInstruction::URem: case LLVMInstruction::SRem:
    case LLVMInstruction::And: case LLVMInstruction::Or: case LLVMInstruction::Xor:
    case LLVMInstruction::Shl: case LLVMInstruction::LShr: case LLVMInstruction::AShr: {
        ensureValueTable(value_counter);
        const char *op_str = "+";
        switch (inst.opcode) {
        case LLVMInstruction::Add: op_str = "+"; break;
        case LLVMInstruction::Sub: op_str = "-"; break;
        case LLVMInstruction::Mul: op_str = "*"; break;
        case LLVMInstruction::UDiv: case LLVMInstruction::SDiv: op_str = "/"; break;
        case LLVMInstruction::URem: case LLVMInstruction::SRem: op_str = "%"; break;
        case LLVMInstruction::And: op_str = "&"; break;
        case LLVMInstruction::Or: op_str = "|"; break;
        case LLVMInstruction::Xor: op_str = "^"; break;
        case LLVMInstruction::Shl: op_str = "<<"; break;
        case LLVMInstruction::LShr: case LLVMInstruction::AShr: op_str = ">>"; break;
        default: break;
        }
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 2)
            result_type = chooseBinaryType(result_type, operandType(inst.operands[0]),
                                           operandType(inst.operands[1]), MSLTypeKind::Int);
        result_type = integerTypeFor(result_type);
        std::string expr = coerceOperand(inst.operands[0], result_type) + " " +
                           std::string(op_str) + " " +
                           coerceOperand(inst.operands[1], result_type);
        emitTypedLine(result_type, result, expr);
        ctx.value_table[value_counter] = result;
        ctx.value_types[value_counter] = result_type;
        value_counter++;
        break;
    }

    case LLVMInstruction::ExtractValue: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 2) {
            auto agg = getValue(inst.operands[0]);
            uint32_t idx = inst.operands[1];

            MSLType agg_type = {MSLTypeKind::Unknown, 0, {}};
            if (inst.operands[0] < ctx.value_types.size())
                agg_type = ctx.value_types[inst.operands[0]];

            bool is_struct = (agg_type.kind == MSLTypeKind::Struct);
            bool agg_is_vector = DXILIRBuilder::isVectorType(agg_type);

            if (is_struct) {
                if (idx == 0) {
                    emitTypedLine(result_type, result, agg);
                } else {
                    emitTypedLine(result_type, result, defaultForType(result_type));
                }
            } else if (agg_is_vector && idx < 4) {
                std::string expr = componentAccess(agg, idx, agg_type);
                auto scalar = DXILIRBuilder::scalarType(agg_type);
                emitTypedLine(scalar, result, expr);
                result_type = scalar;
            } else if (agg_is_vector == false && agg_type.kind != MSLTypeKind::Unknown) {
                emitTypedLine(result_type, result, agg);
            } else {
                auto inferred = inferTypeFromExpr(agg);
                if (DXILIRBuilder::isVectorType(inferred) && idx < 4) {
                    std::string expr = componentAccess(agg, idx, inferred);
                    auto scalar = DXILIRBuilder::scalarType(inferred);
                    emitTypedLine(scalar, result, expr);
                    result_type = scalar;
                } else {
                    emitTypedLine(result_type, result, agg);
                }
            }
        } else {
            emitTypedLine(result_type, result, "0");
        }
        ctx.value_table[value_counter] = result;
        ctx.value_types[value_counter] = result_type;
        value_counter++;
        break;
    }

    case LLVMInstruction::InsertValue: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 1 && inst.operands[0] < ctx.value_types.size()) {
            auto &op_type = ctx.value_types[inst.operands[0]];
            if (op_type.kind != MSLTypeKind::Unknown && op_type.kind != MSLTypeKind::Struct)
                result_type = op_type;
        }
        auto agg = inst.operands.size() >= 1 ? getValue(inst.operands[0]) : "float4(0)";
        emitTypedLine(result_type, result, agg);
        if (inst.operands.size() >= 3 && inst.operands[2] < 4) {
            os << "  " << result << componentSuffix(inst.operands[2]) << " = " << getValue(inst.operands[1]) << ";\n";
        }
        ctx.value_table[value_counter] = result;
        ctx.value_types[value_counter] = result_type;
        value_counter++;
        break;
    }

    case LLVMInstruction::ExtractElement: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 2) {
            auto vec = getValue(inst.operands[0]);
            auto idx = getValue(inst.operands[1]);
            MSLType vec_type = inst.operands[0] < ctx.value_types.size() ? ctx.value_types[inst.operands[0]] : MSLType{};
            uint32_t idx_val = 0;
            std::string expr;
            if (!DXILIRBuilder::isVectorType(vec_type))
                expr = vec;
            else if (parseUnsignedLiteral(idx, idx_val) && idx_val < 4)
                expr = componentAccess(vec, idx_val, vec_type);
            else
                expr = vec + "[" + idx + "]";
            auto scalar = result_type.kind == MSLTypeKind::Unknown
                ? DXILIRBuilder::scalarType(vec_type)
                : result_type;
            emitTypedLine(scalar, result, expr);
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = scalar;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::InsertElement: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 3) {
            auto vec = getValue(inst.operands[0]);
            auto elem = getValue(inst.operands[1]);
            auto idx = getValue(inst.operands[2]);
            emitTypedLine(result_type, result, vec);
            uint32_t idx_val = 0;
            if (parseUnsignedLiteral(idx, idx_val) && idx_val < 4)
                os << "  " << result << componentSuffix(idx_val) << " = " << elem << ";\n";
            else
                os << "  " << result << "[" + idx + "] = " << elem << ";\n";
        } else {
            emitTypedLine(result_type, result, inst.operands.size() >= 1 ? getValue(inst.operands[0]) : "float4(0)");
        }
        ctx.value_table[value_counter] = result;
        ctx.value_types[value_counter] = result_type;
        value_counter++;
        break;
    }

    case LLVMInstruction::ShuffleVector: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        auto lhs = inst.operands.size() >= 1 ? getValue(inst.operands[0]) : "float4(0)";
        auto rhs = inst.operands.size() >= 2 ? getValue(inst.operands[1]) : "float4(0)";
        auto mask = inst.operands.size() >= 3 ? getValue(inst.operands[2]) : "";
        auto mask_values = parseAggregateLiteral(mask);
        if (mask_values.empty()) {
            uint32_t si = 0;
            if (parseUnsignedLiteral(mask, si)) mask_values.push_back(mask);
        }
        if (!mask_values.empty()) {
            std::vector<std::string> components;
            for (auto &mv : mask_values) {
                uint32_t index = 0;
                if (!parseUnsignedLiteral(mv, index) || index == 0xFFFFFFFFu)
                    components.push_back("0.0f");
                else if (index < 4)
                    components.push_back("(" + lhs + ")" + componentSuffix(index));
                else
                    components.push_back("(" + rhs + ")" + componentSuffix(index - 4));
            }
            std::string type_name = emitTypeName(result_type);
            std::string expr = type_name + "(";
            for (size_t i = 0; i < components.size(); i++) {
                if (i) expr += ", ";
                expr += components[i];
            }
            expr += ")";
            emitTypedLine(result_type, result, expr);
        } else {
            emitTypedLine(result_type, result, lhs);
        }
        ctx.value_table[value_counter] = result;
        ctx.value_types[value_counter] = result_type;
        value_counter++;
        break;
    }

    case LLVMInstruction::Unreachable:
        os << "  // unreachable\n";
        break;

    case LLVMInstruction::FNeg: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 1) {
            MSLType op_type = inst.operands[0] < ctx.value_types.size() ? ctx.value_types[inst.operands[0]] : MSLType{};
            if (op_type.kind != MSLTypeKind::Unknown && op_type.kind != MSLTypeKind::Struct)
                result_type = op_type;
            emitTypedLine(result_type, result, "-(" + getValue(inst.operands[0]) + ")");
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::FAdd: case LLVMInstruction::FSub:
    case LLVMInstruction::FMul: case LLVMInstruction::FDiv: case LLVMInstruction::FRem: {
        ensureValueTable(value_counter);
        const char *fop = "+";
        switch (inst.opcode) {
        case LLVMInstruction::FAdd: fop = "+"; break;
        case LLVMInstruction::FSub: fop = "-"; break;
        case LLVMInstruction::FMul: fop = "*"; break;
        case LLVMInstruction::FDiv: fop = "/"; break;
        case LLVMInstruction::FRem: fop = "%"; break;
        default: break;
        }
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 2) {
            result_type = chooseBinaryType(result_type, operandType(inst.operands[0]),
                                           operandType(inst.operands[1]), MSLTypeKind::Float);
            std::string lhs = coerceOperand(inst.operands[0], result_type);
            std::string rhs = coerceOperand(inst.operands[1], result_type);
            std::string expr = inst.opcode == LLVMInstruction::FRem
                ? "fmod(" + lhs + ", " + rhs + ")"
                : lhs + std::string(" ") + fop + " " + rhs;
            emitTypedLine(result_type, result, expr);
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::BitCast: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 1) {
            std::string val = getValue(inst.operands[0]);
            auto src_type = inst.operands[0] < ctx.value_types.size() ? ctx.value_types[inst.operands[0]] : MSLType{};
            auto dst_type = result_type;
            if (!isUsableType(dst_type)) {
                if (isUsableType(src_type)) {
                    dst_type = src_type;
                    result_type = src_type;
                } else {
                    dst_type = {MSLTypeKind::UInt, 0, {}};
                    result_type = dst_type;
                }
            }
            std::string src_name = emitTypeName(src_type);
            std::string dst_name = emitTypeName(dst_type);
            if (isPointerType(src_type) || isPointerType(dst_type)) {
                ctx.value_table[value_counter] = val;
                ctx.value_types[value_counter] = dst_type;
                value_counter++;
                break;
            }
            if (src_name != dst_name && !src_name.empty() && !dst_name.empty() &&
                src_type.kind != MSLTypeKind::Unknown && dst_type.kind != MSLTypeKind::Unknown &&
                DXILIRBuilder::typeBitWidth(src_type) == DXILIRBuilder::typeBitWidth(dst_type)) {
                emitTypedLine(result_type, result, "as_type<" + dst_name + ">(" + val + ")");
            } else if (src_name != dst_name && !src_name.empty() && !dst_name.empty() &&
                       src_type.kind != MSLTypeKind::Unknown && dst_type.kind != MSLTypeKind::Unknown) {
                emitTypedLine(result_type, result, castExpr(val, dst_type));
            } else {
                emitTypedLine(result_type, result, val);
            }
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::ZExt: case LLVMInstruction::SExt: case LLVMInstruction::Trunc:
    case LLVMInstruction::FPToUI: case LLVMInstruction::FPToSI:
    case LLVMInstruction::UIToFP: case LLVMInstruction::SIToFP:
    case LLVMInstruction::FPTrunc: case LLVMInstruction::FPExt: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 1) {
            std::string val = getValue(inst.operands[0]);
            MSLType source_type = valueType(inst.operands[0]);
            if (!isUsableType(result_type))
                result_type = {MSLTypeKind::Int, 0, {}};
            std::string dst_name = emitTypeName(result_type);
            if (isPointerType(source_type) || typeLooksResourceHandle(source_type) ||
                exprLooksResourceHandle(val) || exprContainsRawResourceHandle(val))
                emitTypedLine(result_type, result, defaultForType(result_type));
            else
                emitTypedLine(result_type, result, "static_cast<" + dst_name + ">(" + val + ")");
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::PtrToInt: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 1) {
            std::string val = getValue(inst.operands[0]);
            MSLType source_type = valueType(inst.operands[0]);
            if (!isUsableType(result_type))
                result_type = {MSLTypeKind::UInt, 0, {}};
            if (isPointerType(source_type) || typeLooksResourceHandle(source_type) ||
                exprLooksResourceHandle(val) || exprContainsRawResourceHandle(val))
                emitTypedLine(result_type, result, defaultForType(result_type));
            else
                emitTypedLine(result_type, result, val);
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::IntToPtr: {
        ensureValueTable(value_counter);
        if (inst.operands.size() >= 1) {
            std::string val = getValue(inst.operands[0]);
            ctx.value_table[value_counter] = val;
            ctx.value_types[value_counter] = {MSLTypeKind::DeviceCharPtr, 0, {}};
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::FCmp: case LLVMInstruction::ICmp: {
        ensureValueTable(value_counter);
        if (inst.operands.size() >= 3) {
            uint32_t pred = inst.operands[0];
            MSLType lhs_type = operandType(inst.operands[1]);
            MSLType rhs_type = operandType(inst.operands[2]);
            MSLType cmp_type = chooseBinaryType(
                inst.opcode == LLVMInstruction::FCmp ? MSLType{MSLTypeKind::Float, 0, {}}
                                                      : MSLType{MSLTypeKind::Int, 0, {}},
                lhs_type, rhs_type,
                inst.opcode == LLVMInstruction::FCmp ? MSLTypeKind::Float : MSLTypeKind::Int);
            auto lhs = coerceOperand(inst.operands[1], cmp_type);
            auto rhs = coerceOperand(inst.operands[2], cmp_type);
            const char *cmp = "==";
            const char *decl = ctx.predeclared_names.find(result) != ctx.predeclared_names.end() ? "" : "bool ";
            if (pred == 0) { os << "  " << decl << result << " = false;\n"; }
            else if (pred >= 15) { os << "  " << decl << result << " = true;\n"; }
            else if (pred == 7) { os << "  " << decl << result << " = (!isnan(" << lhs << ") && !isnan(" << rhs << "));\n"; }
            else if (pred == 14) { os << "  " << decl << result << " = (isnan(" << lhs << ") || isnan(" << rhs << "));\n"; }
            else {
                if (pred == 1 || pred == 8) cmp = "==";
                else if (pred == 2 || pred == 9) cmp = ">";
                else if (pred == 3 || pred == 10) cmp = ">=";
                else if (pred == 4 || pred == 11) cmp = "<";
                else if (pred == 5 || pred == 12) cmp = "<=";
                else if (pred == 6 || pred == 13) cmp = "!=";
                os << "  " << decl << result << " = (" << lhs << " " << cmp << " " << rhs << ");\n";
            }
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = {MSLTypeKind::Bool, 0, {}};
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::Select: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 3) {
            auto cond = coerceOperand(inst.operands[0], {MSLTypeKind::Bool, 0, {}});
            MSLType tv_type = operandType(inst.operands[1]);
            MSLType fv_type = operandType(inst.operands[2]);
            result_type = chooseBinaryType(result_type, tv_type, fv_type, MSLTypeKind::Int);
            auto tv = coerceOperand(inst.operands[1], result_type);
            auto fv = coerceOperand(inst.operands[2], result_type);
            emitTypedLine(result_type, result, "(" + cond + " ? " + tv + " : " + fv + ")");
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::Alloca: {
        std::string storage_class = "thread";
        if (inst.type_id > 0 && inst.type_id < ctx.mod.types.size()) {
            auto &ptr_type = ctx.mod.types[inst.type_id];
            if (ptr_type.kind == LLVMType::Pointer && ptr_type.address_space == 3)
                storage_class = "threadgroup";
        }
        std::string alloca_name = "alloca_" + std::to_string(value_counter);
        if (ctx.predeclared_allocas.find(value_counter) == ctx.predeclared_allocas.end())
            os << "  " << storage_class << " char " << alloca_name << "[256];\n";
        ensureValueTable(value_counter);
        ctx.value_table[value_counter] = "(" + storage_class + " char*)&" + alloca_name;
        ctx.value_types[value_counter] = {MSLTypeKind::DeviceCharPtr, 0, {}};
        value_counter++;
        break;
    }

    case LLVMInstruction::Load: {
        ensureValueTable(value_counter);
        MSLType result_type = getTypeForInst(inst.type_id);
        if (inst.operands.size() >= 1) {
            auto ptr = getValue(inst.operands[0]);
            auto ptr_type = valueType(inst.operands[0]);
            if (!isUsableType(result_type))
                result_type = {MSLTypeKind::UInt, 0, {}};
            std::string type_name = emitTypeName(result_type);
            std::string expr = defaultForType(result_type);
            if (startsWith(ptr, "tex") || startsWith(ptr, "samp") ||
                exprContainsRawResourceHandle(ptr)) {
                expr = defaultForType(result_type);
            } else if (isPointerType(ptr_type)) {
                expr = "*((" + std::string(pointerAddressSpace(inst.operands[0])) + " " +
                       type_name + "*)(" + ptr + "))";
            }
            emitTypedLine(result_type, result, expr);
            ctx.value_table[value_counter] = result;
            ctx.value_types[value_counter] = result_type;
        }
        value_counter++;
        break;
    }

    case LLVMInstruction::Store: {
        if (inst.operands.size() >= 2) {
            auto ptr = getValue(inst.operands[0]);
            auto val = getValue(inst.operands[1]);
            auto ptr_type = valueType(inst.operands[0]);
            auto val_type = valueType(inst.operands[1]);
            if (startsWith(ptr, "tex") || startsWith(ptr, "samp") ||
                exprContainsRawResourceHandle(ptr)) {
                os << "  // skipped store through resource handle " << ptr << "\n";
            } else if (isPointerType(ptr_type)) {
                std::string type_name = emitTypeName(val_type);
                if (type_name.empty() || type_name == "auto" || type_name == "void") type_name = "uint";
                os << "  *((" << pointerAddressSpace(inst.operands[0]) << " " << type_name
                   << "*)(" << ptr << ")) = " << val << ";\n";
            } else {
                os << "  // skipped store through non-pointer " << ptr << "\n";
            }
        }
        break;
    }

    case LLVMInstruction::GetElementPtr: {
        ensureValueTable(value_counter);
        if (inst.operands.empty()) {
            ctx.value_table[value_counter] = "0";
            ctx.value_types[value_counter] = {MSLTypeKind::DeviceCharPtr, 0, {}};
            value_counter++;
            break;
        }
        auto base = getValue(inst.operands[0]);
        std::string gep = base;
        if (startsWith(base, "tex") || startsWith(base, "samp") ||
            exprContainsRawResourceHandle(base)) {
            ctx.value_table[value_counter] = "0";
            ctx.value_types[value_counter] = {MSLTypeKind::DeviceCharPtr, 0, {}};
            value_counter++;
            break;
        }
        for (size_t i = 1; i < inst.operands.size(); i++) {
            auto idx = getValue(inst.operands[i]);
            if (idx != "0" && idx != "0.0" && idx != "0.0f")
                gep += " + " + idx;
        }
        ctx.value_table[value_counter] = gep;
        ctx.value_types[value_counter] = {MSLTypeKind::DeviceCharPtr, 0, {}};
        value_counter++;
        break;
    }

    case LLVMInstruction::PHI: {
        ensureValueTable(value_counter);
        ctx.value_table[value_counter] = result;
        ctx.value_types[value_counter] = getTypeForInst(inst.type_id);
        value_counter++;
        break;
    }

    case LLVMInstruction::Invoke:
        os << "  // invoke\n";
        break;

    default:
        ctx.unsupported_opcodes++;
        os << "  // unhandled opcode " << (int)inst.opcode << "\n";
        ensureValueTable(value_counter);
        ctx.value_table[value_counter] = result;
        value_counter++;
        break;
    }
}

std::optional<TypedMSLShader> MSLLowering::lower(const LLVMModule &module,
                                                   const DxilParsedShader &shader) {
    if (module.functions.empty()) return std::nullopt;

    std::ostringstream os;
    LowerContext ctx{os, module, shader};

    const LLVMFunction *entry_fn = nullptr;
    for (auto &fn : module.functions) {
        if (!fn.blocks.empty() && !shader.entry_point.empty() && fn.name == shader.entry_point) {
            entry_fn = &fn; break;
        }
    }
    if (!entry_fn) {
        for (auto it = module.functions.rbegin(); it != module.functions.rend(); ++it) {
            if (!it->blocks.empty()) { entry_fn = &*it; break; }
        }
    }
    if (!entry_fn) return std::nullopt;

    auto &fn = *entry_fn;
    ctx.current_fn = entry_fn;

    ctx.value_table.resize(256);
    ctx.value_types.resize(256);

    auto loadConstants = [&](const std::vector<LLVMValue> &consts) {
        for (auto &c : consts) {
            uint32_t val_idx = c.id;
            if (ctx.value_table.size() <= val_idx) ctx.value_table.resize(val_idx + 1);
            if (ctx.value_types.size() <= val_idx) ctx.value_types.resize(val_idx + 1);
            std::string cdata = c.constant_data;
            if (startsWith(cdata, "agg(")) {
                MSLType type = DXILIRBuilder::resolveType(c.type_id, module);
                ctx.value_table[val_idx] = aggregateConstructor(cdata, type);
                ctx.value_types[val_idx] = type;
                continue;
            }
            ctx.value_table[val_idx] = cdata.empty() ? "const_" + std::to_string(val_idx)
                                                     : normalizeAggregateExpressions(cdata);
            ctx.value_types[val_idx] = DXILIRBuilder::resolveType(c.type_id, module);
        }
    };

    loadConstants(module.constants);
    loadConstants(fn.constants);

    for (auto &dfn : module.functions) {
        if (!dfn.is_declaration || dfn.name.empty()) continue;
        if (ctx.value_table.size() <= dfn.value_id) ctx.value_table.resize(dfn.value_id + 1);
        if (ctx.value_types.size() <= dfn.value_id) ctx.value_types.resize(dfn.value_id + 1);
        ctx.value_table[dfn.value_id] = dfn.name;
        ctx.value_types[dfn.value_id] = {MSLTypeKind::Unknown, 0, {}};
        ctx.function_decls[dfn.value_id] = dfn.name;
    }

    analyzeBindingPlan(ctx, fn);
    emitFunctionPrologue(ctx);

    for (auto &gv : module.globals) {
        if (gv.address_space == 3) {
            std::string gv_name = gv.name.empty() ? "gvar_" + std::to_string(gv.value_id) : escapeName(gv.name);
            os << "  threadgroup char " << gv_name << "[256];\n";
            if (ctx.value_table.size() <= gv.value_id) ctx.value_table.resize(gv.value_id + 1);
            ctx.value_table[gv.value_id] = "(threadgroup char*)&" + gv_name;
            if (ctx.value_types.size() <= gv.value_id) ctx.value_types.resize(gv.value_id + 1);
            ctx.value_types[gv.value_id] = {MSLTypeKind::ThreadgroupCharPtr, 0, {}};
        }
    }

    auto seedValue = [&](uint32_t value_id, const std::string &expr, MSLType type,
                         ValueRole role = ValueRole::Generic) {
        if (ctx.value_table.size() <= value_id) ctx.value_table.resize(value_id + 1);
        if (ctx.value_types.size() <= value_id) ctx.value_types.resize(value_id + 1);
        if (ctx.value_roles.size() <= value_id) ctx.value_roles.resize(value_id + 1);
        ctx.value_table[value_id] = expr;
        ctx.value_types[value_id] = type;
        ctx.value_roles[value_id] = role;
    };

    auto seedBufferHandle = [&](uint32_t value_id, uint32_t binding_index) {
        seedValue(value_id, "buf" + std::to_string(std::min<uint32_t>(binding_index, 30)),
                  {MSLTypeKind::DeviceCharPtr, 0, {}}, ValueRole::BufferHandle);
    };

    auto param_type_ids = functionParamTypeIds(module, fn);
    if (fn.param_count != 0 && fn.instruction_start_value >= fn.param_count) {
        uint32_t first_param_value = fn.instruction_start_value - fn.param_count;
        for (uint32_t i = 0; i < fn.param_count; i++) {
            uint32_t value_id = first_param_value + i;
            MSLType type = i < param_type_ids.size()
                ? DXILIRBuilder::resolveType(param_type_ids[i], module)
                : MSLType{MSLTypeKind::Int, 0, {}};
            if (type.kind == MSLTypeKind::Void || type.kind == MSLTypeKind::Unknown ||
                type.kind == MSLTypeKind::Struct)
                type = {MSLTypeKind::Int, 0, {}};
            if (isPointerMSLType(type)) {
                seedBufferHandle(value_id, i);
            } else {
                std::string name = emitValue(value_id);
                seedValue(value_id, name, type);
                ctx.predeclared_names.insert(name);
                ctx.predeclared_types[name] = type;
                os << "  " << typedDecl(name, type) << " = "
                   << defaultForType(type) << "; // function parameter fallback\n";
            }
        }
    }

    std::map<uint32_t, uint32_t> block_value_to_index;
    for (size_t i = 0; i < fn.block_value_ids.size(); i++)
        block_value_to_index[fn.block_value_ids[i]] = (uint32_t)i;

    std::map<uint32_t, std::set<uint32_t>> successors;
    struct TerminatorInfo { enum Kind { None, Br, Switch, Ret, Unreachable } kind = None; std::vector<uint32_t> operands; };
    std::vector<TerminatorInfo> terminators(fn.blocks.size());

    for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
        auto &block = fn.blocks[bi];
        if (block.instructions.empty()) continue;
        auto &last = block.instructions.back();
        if (last.opcode == LLVMInstruction::Br) {
            terminators[bi].kind = TerminatorInfo::Br;
            terminators[bi].operands = last.operands;
            if (last.operands.size() == 1) successors[(uint32_t)bi].insert(last.operands[0]);
            else if (last.operands.size() >= 3) { successors[(uint32_t)bi].insert(last.operands[1]); successors[(uint32_t)bi].insert(last.operands[2]); }
        } else if (last.opcode == LLVMInstruction::Switch) {
            terminators[bi].kind = TerminatorInfo::Switch;
            terminators[bi].operands = last.operands;
            if (last.operands.size() >= 2) {
                successors[(uint32_t)bi].insert(last.operands[1]);
                for (size_t j = 2; j + 1 < last.operands.size(); j += 2)
                    successors[(uint32_t)bi].insert(last.operands[j + 1]);
            }
        } else if (last.opcode == LLVMInstruction::Ret) {
            terminators[bi].kind = TerminatorInfo::Ret; terminators[bi].operands = last.operands;
        } else if (last.opcode == LLVMInstruction::Unreachable) {
            terminators[bi].kind = TerminatorInfo::Unreachable;
        }
    }

    std::map<uint32_t, std::set<uint32_t>> predecessors;
    for (auto &[from, succs] : successors)
        for (uint32_t to : succs) predecessors[to].insert(from);

    struct PhiIncoming { uint32_t value_id; uint32_t pred_block_idx; };
    struct PhiInfo { uint32_t result_slot; uint32_t type_id; std::vector<PhiIncoming> incoming; };
    std::map<uint32_t, std::vector<PhiInfo>> phi_info_per_block;
    std::map<uint32_t, uint32_t> value_def_block;
    std::set<uint32_t> cross_block_values;
    std::set<uint32_t> phi_result_values;
    std::set<uint32_t> unresolved_referenced_values;
    std::map<uint32_t, MSLType> unresolved_reference_types;

    auto resultTypeForPredecl = [&](const LLVMInstruction &inst) -> MSLType {
        switch (inst.opcode) {
        case LLVMInstruction::FCmp:
        case LLVMInstruction::ICmp:
            return {MSLTypeKind::Bool, 0, {}};
        case LLVMInstruction::Alloca:
        case LLVMInstruction::GetElementPtr:
        case LLVMInstruction::IntToPtr:
            return {MSLTypeKind::DeviceCharPtr, 0, {}};
        case LLVMInstruction::Add: case LLVMInstruction::Sub: case LLVMInstruction::Mul:
        case LLVMInstruction::UDiv: case LLVMInstruction::SDiv:
        case LLVMInstruction::URem: case LLVMInstruction::SRem:
        case LLVMInstruction::And: case LLVMInstruction::Or: case LLVMInstruction::Xor:
        case LLVMInstruction::Shl: case LLVMInstruction::LShr: case LLVMInstruction::AShr:
        case LLVMInstruction::FAdd: case LLVMInstruction::FSub:
        case LLVMInstruction::FMul: case LLVMInstruction::FDiv: case LLVMInstruction::FRem: {
            MSLType declared = DXILIRBuilder::resolveType(inst.type_id, module);
            MSLType lhs = inst.operands.size() > 0 && inst.operands[0] < ctx.value_types.size()
                ? ctx.value_types[inst.operands[0]]
                : MSLType{};
            MSLType rhs = inst.operands.size() > 1 && inst.operands[1] < ctx.value_types.size()
                ? ctx.value_types[inst.operands[1]]
                : MSLType{};
            MSLType fallback = (inst.opcode == LLVMInstruction::FAdd ||
                                inst.opcode == LLVMInstruction::FSub ||
                                inst.opcode == LLVMInstruction::FMul ||
                                inst.opcode == LLVMInstruction::FDiv ||
                                inst.opcode == LLVMInstruction::FRem)
                ? MSLType{MSLTypeKind::Float, 0, {}}
                : MSLType{MSLTypeKind::Int, 0, {}};
            MSLType result = promoteNumericType(lhs, rhs, usableType(declared) ? declared : fallback);
            if (inst.opcode != LLVMInstruction::FAdd &&
                inst.opcode != LLVMInstruction::FSub &&
                inst.opcode != LLVMInstruction::FMul &&
                inst.opcode != LLVMInstruction::FDiv &&
                inst.opcode != LLVMInstruction::FRem) {
                if (result.kind == MSLTypeKind::Float2) return {MSLTypeKind::Int2, 0, {}};
                if (result.kind == MSLTypeKind::Float3) return {MSLTypeKind::Int3, 0, {}};
                if (result.kind == MSLTypeKind::Float4) return {MSLTypeKind::Int4, 0, {}};
                if (DXILIRBuilder::isFloatType(result)) return {MSLTypeKind::Int, 0, {}};
            }
            return result;
        }
        case LLVMInstruction::Call: {
            if (!inst.operands.empty()) {
                uint32_t callee = inst.operands[0];
                std::string callee_name;
                auto decl_it = ctx.function_decls.find(callee);
                if (decl_it != ctx.function_decls.end()) callee_name = decl_it->second;
                else if (callee < ctx.value_table.size()) callee_name = ctx.value_table[callee];
                uint32_t intrinsic_id = intrinsicIdFromCalleeName(callee_name);
                if (intrinsic_id != 0 && inst.operands.size() > 2) {
                    std::vector<uint32_t> fn_args;
                    if (intrinsic_id == DXOP_Unary || intrinsic_id == DXOP_Binary ||
                        intrinsic_id == DXOP_Tertiary)
                        fn_args.assign(inst.operands.begin() + 2, inst.operands.end());
                    else
                        fn_args.assign(inst.operands.begin() + 3, inst.operands.end());
                    MSLType declared = DXILIRBuilder::resolveType(inst.type_id, module);
                    MSLType inferred = inferDXIntrinsicResultType(ctx, intrinsic_id, fn_args, declared);
                    if (inferred.kind != MSLTypeKind::Unknown && inferred.kind != MSLTypeKind::Void)
                        return inferred;
                }
            }
            break;
        }
        case LLVMInstruction::ExtractValue:
        case LLVMInstruction::ExtractElement: {
            MSLType source_type = inst.operands.empty() || inst.operands[0] >= ctx.value_types.size()
                ? MSLType{}
                : ctx.value_types[inst.operands[0]];
            if (DXILIRBuilder::isVectorType(source_type))
                return DXILIRBuilder::scalarType(source_type);
            break;
        }
        case LLVMInstruction::ShuffleVector:
        case LLVMInstruction::InsertElement:
        case LLVMInstruction::InsertValue: {
            if (!inst.operands.empty() && inst.operands[0] < ctx.value_types.size() &&
                DXILIRBuilder::isVectorType(ctx.value_types[inst.operands[0]]))
                return ctx.value_types[inst.operands[0]];
            break;
        }
        default:
            break;
        }
        MSLType type = DXILIRBuilder::resolveType(inst.type_id, module);
        if (type.kind == MSLTypeKind::Void || type.kind == MSLTypeKind::Unknown ||
            type.kind == MSLTypeKind::Struct)
            type = {MSLTypeKind::Int, 0, {}};
        return type;
    };

    {
        uint32_t vc = fn.instruction_start_value;
        for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
            for (auto &inst : fn.blocks[bi].instructions) {
                bool produces_value = inst.opcode != LLVMInstruction::Ret &&
                                      inst.opcode != LLVMInstruction::Br &&
                                      inst.opcode != LLVMInstruction::Switch &&
                                      inst.opcode != LLVMInstruction::Unreachable &&
                                      inst.opcode != LLVMInstruction::Store;
                if (produces_value)
                    value_def_block[vc] = (uint32_t)bi;
                if (inst.opcode == LLVMInstruction::PHI) {
                    PhiInfo pi; pi.result_slot = vc; pi.type_id = inst.type_id;
                    phi_result_values.insert(vc);
                    for (size_t j = 0; j + 1 < inst.operands.size(); j += 2) {
                        PhiIncoming inc; inc.value_id = inst.operands[j];
                        uint32_t pbv = inst.operands[j + 1];
                        auto it = block_value_to_index.find(pbv);
                        inc.pred_block_idx = it == block_value_to_index.end() ? UINT32_MAX : it->second;
                        pi.incoming.push_back(inc);
                    }
                    phi_info_per_block[(uint32_t)bi].push_back(pi);
                }
                switch (inst.opcode) {
                case LLVMInstruction::Ret: case LLVMInstruction::Br:
                case LLVMInstruction::Switch: case LLVMInstruction::Unreachable:
                case LLVMInstruction::Store: break;
                default: vc++; break;
                }
            }
        }
    }

    auto hasResolvedValue = [&](uint32_t value_id) -> bool {
        if (value_id < ctx.value_table.size() && !ctx.value_table[value_id].empty() &&
            !startsWith(ctx.value_table[value_id], "dx."))
            return true;
        if (value_def_block.find(value_id) != value_def_block.end())
            return true;
        for (auto &c : module.constants)
            if (c.id == value_id && !c.constant_data.empty())
                return true;
        for (auto &c : fn.constants)
            if (c.id == value_id && !c.constant_data.empty())
                return true;
        return false;
    };

    auto rememberUnresolvedReference = [&](uint32_t value_id, MSLType type_hint) {
        if (hasResolvedValue(value_id))
            return;
        unresolved_referenced_values.insert(value_id);
        if (type_hint.kind != MSLTypeKind::Unknown && type_hint.kind != MSLTypeKind::Void &&
            type_hint.kind != MSLTypeKind::Struct)
            unresolved_reference_types[value_id] = type_hint;
    };

    for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
        for (auto &inst : fn.blocks[bi].instructions) {
            MSLType inst_type = DXILIRBuilder::resolveType(inst.type_id, module);
            for (size_t oi = 0; oi < inst.operands.size(); oi++) {
                bool value_operand = true;
                MSLType type_hint = inst_type;
                if (inst.opcode == LLVMInstruction::PHI)
                    value_operand = (oi % 2) == 0;
                else if (inst.opcode == LLVMInstruction::Call)
                    value_operand = oi >= 2;
                else if (inst.opcode == LLVMInstruction::Br)
                    value_operand = inst.operands.size() >= 3 && oi == 0;
                else if (inst.opcode == LLVMInstruction::Switch)
                    value_operand = oi == 0;
                if (!value_operand) continue;

                if ((inst.opcode == LLVMInstruction::Load ||
                     inst.opcode == LLVMInstruction::Store ||
                     inst.opcode == LLVMInstruction::GetElementPtr ||
                     inst.opcode == LLVMInstruction::GEP) && oi == 0)
                    type_hint = {MSLTypeKind::DeviceCharPtr, 0, {}};
                else if (inst.opcode == LLVMInstruction::PHI)
                    type_hint = inst_type;
                rememberUnresolvedReference(inst.operands[oi], type_hint);

                auto def_it = value_def_block.find(inst.operands[oi]);
                if (def_it != value_def_block.end() && def_it->second != (uint32_t)bi)
                    cross_block_values.insert(inst.operands[oi]);
            }
        }
    }

    uint32_t value_counter = fn.instruction_start_value;
    ctx.instruction_start_value = fn.instruction_start_value;

    bool needs_dispatch = fn.blocks.size() > 1;
    if (needs_dispatch) {
        uint32_t vc = fn.instruction_start_value;
        for (auto &block : fn.blocks) {
            for (auto &inst : block.instructions) {
                bool produces_value = inst.opcode != LLVMInstruction::Ret &&
                                      inst.opcode != LLVMInstruction::Br &&
                                      inst.opcode != LLVMInstruction::Switch &&
                                      inst.opcode != LLVMInstruction::Unreachable &&
                                      inst.opcode != LLVMInstruction::Store;
                if (produces_value && inst.opcode == LLVMInstruction::Alloca) {
                    std::string storage_class = "thread";
                    if (inst.type_id > 0 && inst.type_id < module.types.size()) {
                        auto &ptr_type = module.types[inst.type_id];
                        if (ptr_type.kind == LLVMType::Pointer && ptr_type.address_space == 3)
                            storage_class = "threadgroup";
                    }
                    std::string alloca_name = "alloca_" + std::to_string(vc);
                    os << "  " << storage_class << " char " << alloca_name
                       << "[256]; // dispatch alloca storage\n";
                    if (ctx.value_table.size() <= vc) ctx.value_table.resize(vc + 1);
                    if (ctx.value_types.size() <= vc) ctx.value_types.resize(vc + 1);
                    ctx.value_table[vc] = "(" + storage_class + " char*)&" + alloca_name;
                    ctx.value_types[vc] = storage_class == "threadgroup"
                        ? MSLType{MSLTypeKind::ThreadgroupCharPtr, 0, {}}
                        : MSLType{MSLTypeKind::DeviceCharPtr, 0, {}};
                    ctx.predeclared_allocas.insert(vc);
                }
                if (produces_value) vc++;
            }
        }
    }

    for (uint32_t value_id : unresolved_referenced_values) {
        auto type_it = unresolved_reference_types.find(value_id);
        if (value_id < 31 && type_it != unresolved_reference_types.end() &&
            isPointerMSLType(type_it->second) && !hasResolvedValue(value_id))
            seedBufferHandle(value_id, value_id);
    }

    for (uint32_t value_id : unresolved_referenced_values) {
        if (hasResolvedValue(value_id))
            continue;
        MSLType pre_type = {MSLTypeKind::Int, 0, {}};
        auto type_it = unresolved_reference_types.find(value_id);
        if (type_it != unresolved_reference_types.end())
            pre_type = type_it->second;
        std::string name = emitValue(value_id);
        bool declaration_slot = ctx.function_decls.find(value_id) != ctx.function_decls.end();
        if (ctx.value_table.size() <= value_id) ctx.value_table.resize(value_id + 1);
        if (ctx.value_types.size() <= value_id) ctx.value_types.resize(value_id + 1);
        if (!declaration_slot)
            ctx.value_table[value_id] = name;
        ctx.value_types[value_id] = pre_type;
        ctx.predeclared_names.insert(name);
        ctx.predeclared_types[name] = pre_type;
        os << "  " << typedDecl(name, pre_type) << " = "
           << defaultForType(pre_type) << "; // unresolved value pre-decl\n";
    }

    if (needs_dispatch) {
        uint32_t vc = fn.instruction_start_value;
        for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
            for (auto &inst : fn.blocks[bi].instructions) {
                bool produces_value = inst.opcode != LLVMInstruction::Ret &&
                                      inst.opcode != LLVMInstruction::Br &&
                                      inst.opcode != LLVMInstruction::Switch &&
                                      inst.opcode != LLVMInstruction::Unreachable &&
                                      inst.opcode != LLVMInstruction::Store;
                MSLType static_type;
                if (produces_value) {
                    if (ctx.value_types.size() <= vc) ctx.value_types.resize(vc + 1);
                    static_type = resultTypeForPredecl(inst);
                    ctx.value_types[vc] = static_type;
                }
                if (produces_value &&
                    (cross_block_values.find(vc) != cross_block_values.end() ||
                     phi_result_values.find(vc) != phi_result_values.end())) {
                    MSLType pre_type = static_type;
                    std::string name = emitValue(vc);
                    if (ctx.value_table.size() <= vc) ctx.value_table.resize(vc + 1);
                    ctx.value_table[vc] = name;
                    ctx.predeclared_names.insert(name);
                    ctx.predeclared_types[name] = pre_type;
                    os << "  " << typedDecl(name, pre_type) << " = "
                       << defaultForType(pre_type) << "; // dispatch pre-decl\n";
                }
                if (produces_value) vc++;
            }
        }
    }
    if (needs_dispatch) {
        os << "  int _block_state = 0;\n  for (;;) {\n    switch (_block_state) {\n";
    }

    for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
        auto &block = fn.blocks[bi];
        if (needs_dispatch) os << "    case " << bi << ": {\n";

        for (size_t ii = 0; ii < block.instructions.size(); ii++) {
            auto &inst = block.instructions[ii];
            bool is_terminator = (inst.opcode == LLVMInstruction::Br ||
                                   inst.opcode == LLVMInstruction::Switch ||
                                   inst.opcode == LLVMInstruction::Ret ||
                                   inst.opcode == LLVMInstruction::Unreachable);

            if (inst.opcode == LLVMInstruction::PHI) {
                ctx.value_table.resize(std::max((size_t)value_counter + 1, ctx.value_table.size()));
                ctx.value_types.resize(std::max((size_t)value_counter + 1, ctx.value_types.size()));
                ctx.value_table[value_counter] = emitValue(value_counter);
                ctx.value_types[value_counter] = DXILIRBuilder::resolveType(inst.type_id, module);
                value_counter++;
                continue;
            }

            if (!is_terminator) {
                emitTypedInstruction(ctx, inst, value_counter);
                continue;
            }

            auto succ_it = successors.find((uint32_t)bi);
            if (succ_it != successors.end()) {
                for (uint32_t succ : succ_it->second) {
                    auto phi_it = phi_info_per_block.find(succ);
                    if (phi_it != phi_info_per_block.end()) {
                        for (auto &pi : phi_it->second) {
                            for (auto &inc : pi.incoming) {
                                if (inc.pred_block_idx == (uint32_t)bi) {
                                    MSLType phi_type = pi.result_slot < ctx.value_types.size()
                                        ? ctx.value_types[pi.result_slot]
                                        : DXILIRBuilder::resolveType(pi.type_id, module);
                                    std::string incoming = hasEmittableValue(ctx, inc.value_id)
                                        ? resolveValue(ctx, inc.value_id)
                                        : defaultForType(phi_type);
                                    if (inc.value_id < ctx.value_types.size() &&
                                        DXILIRBuilder::isVectorType(ctx.value_types[inc.value_id]) &&
                                        (DXILIRBuilder::isFloatType(phi_type) || DXILIRBuilder::isIntType(phi_type)) &&
                                        !DXILIRBuilder::isVectorType(phi_type))
                                        incoming = "(" + incoming + ").x";
                                    os << "    " << emitValue(pi.result_slot) << " = "
                                       << coerceResolvedValue(incoming, phi_type) << ";\n";
                                }
                            }
                        }
                    }
                }
            }

            if (inst.opcode == LLVMInstruction::Ret) {
                if (ctx.shader.kind == DxilShaderKind::Vertex) os << "    return out;\n";
                else if (ctx.shader.kind == DxilShaderKind::Pixel) {
                    if (!inst.operands.empty()) os << "    result.color0 = float4(" << resolveValue(ctx, inst.operands[0]) << ");\n";
                    os << "    return result;\n";
                } else {
                    if (!inst.operands.empty()) os << "    return " << resolveValue(ctx, inst.operands[0]) << ";\n";
                    else os << "    return;\n";
                }
            } else if (inst.opcode == LLVMInstruction::Br) {
                if (inst.operands.size() == 1 && needs_dispatch) {
                    os << "    _block_state = " << inst.operands[0] << "; continue;\n";
                } else if (inst.operands.size() >= 3 && needs_dispatch) {
                    os << "    if (" << resolveCondition(ctx, inst.operands[0]) << ") { _block_state = " << inst.operands[1] << "; continue; } else { _block_state = " << inst.operands[2] << "; continue; }\n";
                }
            } else if (inst.opcode == LLVMInstruction::Switch) {
                if (inst.operands.size() >= 2) {
                    os << "    { int _sv = (int)(" << coerceResolvedValue(resolveValue(ctx, inst.operands[0]), {MSLTypeKind::Int, 0, {}}) << ");\n";
                    for (size_t j = 2; j + 1 < inst.operands.size(); j += 2)
                        os << "    if (_sv == " << inst.operands[j] << ") { _block_state = " << inst.operands[j+1] << "; continue; }\n";
                    os << "    _block_state = " << inst.operands[1] << "; continue; }\n";
                }
            } else if (inst.opcode == LLVMInstruction::Unreachable) {
                os << "    // unreachable\n";
            }

            if (needs_dispatch) os << "    }\n";
        }
    }

    if (needs_dispatch) os << "    }\n    break;\n  }\n";
    os << "}\n";

    TypedMSLShader result;
    result.source = os.str();
    result.entry_point = shader.entry_point;
    result.tg_size[0] = module.num_threads[0];
    result.tg_size[1] = module.num_threads[1];
    result.tg_size[2] = module.num_threads[2];
    result.unsupported_intrinsics = ctx.unsupported_intrinsics;
    result.unsupported_opcodes = ctx.unsupported_opcodes;
    result.diagnostics = ctx.diagnostics;

    return std::optional<TypedMSLShader>(std::in_place, std::move(result));
}

}
