#include "dxil_to_msl.hpp"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>
#include <cmath>
#include <cctype>

#define DXTRACE(fmt, ...) do { FILE *_tf = fopen("Z:\\tmp\\dxmt_dxil_trace.log", "a"); if (_tf) { fprintf(_tf, fmt "\n", ##__VA_ARGS__); fclose(_tf); } } while(0)

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
  DXOP_TextureStoreSample = 225,
  DXOP_TextureGather = 73,
  DXOP_TextureGatherCmp = 74,
  DXOP_TextureGatherRaw = 223,
  DXOP_TextureSample = 60,
  DXOP_TextureSampleBias = 61,
  DXOP_TextureSampleLevel = 62,
  DXOP_TextureSampleGrad = 63,
  DXOP_TextureSampleCmp = 64,
  DXOP_TextureSampleCmpLevelZero = 65,
  DXOP_TextureSampleCmpLevel = 224,
  DXOP_BufferUpdateCounter = 70,
  DXOP_CheckAccessFullyMapped = 71,
  DXOP_GetDimensions = 72,
  DXOP_Barrier = 80,
  DXOP_Unary = 13,
  DXOP_Binary = 14,
  DXOP_Tertiary = 15,
  DXOP_Dot2 = 54,
  DXOP_Dot3 = 55,
  DXOP_Dot4 = 56,
  DXOP_MakeDouble = 101,
  DXOP_SplitDouble = 102,
  DXOP_RawBufferLoad = 139,
  DXOP_RawBufferStore = 140,
  DXOP_RawBufferVectorLoad = 303,
  DXOP_RawBufferVectorStore = 304,
  DXOP_RawBufferLoadLegacy = 1025,
  DXOP_RawBufferStoreLegacy = 1026,
  DXOP_AtomicBinOp = 78,
  DXOP_AtomicCompareExchange = 79,
  DXOP_DerivCoarseX = 83,
  DXOP_DerivCoarseY = 84,
  DXOP_DerivFineX = 85,
  DXOP_DerivFineY = 86,
  DXOP_CalcLOD = 81,
  DXOP_Texture2DMSGetSamplePosition = 75,
  DXOP_RenderTargetGetSamplePosition = 76,
  DXOP_RenderTargetGetSampleCount = 77,
  DXOP_Texture2DMSGetSamplePositionLegacy = 97,
  DXOP_RenderTargetGetSamplePositionLegacy = 98,
  DXOP_NumPrimitives = 109,
  DXOP_NumOutputVertices = 110,
  DXOP_UnaryBits = 10001,
  DXOP_LegacyF16ToF32 = 10002,
  DXOP_LegacyF32ToF16 = 10003,
};

enum DXILMathOpcode {
  DXILOP_FAbs = 6,
  DXILOP_Saturate = 7,
  DXILOP_IsNaN = 8,
  DXILOP_IsInf = 9,
  DXILOP_IsFinite = 10,
  DXILOP_Cos = 12,
  DXILOP_Sin = 13,
  DXILOP_Tan = 14,
  DXILOP_Acos = 15,
  DXILOP_Asin = 16,
  DXILOP_Atan = 17,
  DXILOP_Exp = 21,
  DXILOP_Frc = 22,
  DXILOP_Log = 23,
  DXILOP_Sqrt = 24,
  DXILOP_Rsqrt = 25,
  DXILOP_Round_ne = 26,
  DXILOP_Round_ni = 27,
  DXILOP_Round_pi = 28,
  DXILOP_Round_z = 29,
  DXILOP_FMax = 35,
  DXILOP_FMin = 36,
  DXILOP_IMax = 37,
  DXILOP_IMin = 38,
  DXILOP_UMax = 39,
  DXILOP_UMin = 40,
  DXILOP_FMad = 46,
  DXILOP_Fma = 47,
  DXILOP_IMad = 48,
  DXILOP_UMad = 49,
};

static bool isKnownDXILUnaryOpcode(int64_t op) {
  switch (op) {
  case DXILOP_FAbs:
  case DXILOP_Saturate:
  case DXILOP_IsNaN:
  case DXILOP_IsInf:
  case DXILOP_IsFinite:
  case DXILOP_Cos:
  case DXILOP_Sin:
  case DXILOP_Tan:
  case DXILOP_Acos:
  case DXILOP_Asin:
  case DXILOP_Atan:
  case DXILOP_Exp:
  case DXILOP_Frc:
  case DXILOP_Log:
  case DXILOP_Sqrt:
  case DXILOP_Rsqrt:
  case DXILOP_Round_ne:
  case DXILOP_Round_ni:
  case DXILOP_Round_pi:
  case DXILOP_Round_z:
    return true;
  default:
    return false;
  }
}

static bool isKnownDXILBinaryOpcode(int64_t op) {
  switch (op) {
  case DXILOP_FMax:
  case DXILOP_FMin:
  case DXILOP_IMax:
  case DXILOP_IMin:
  case DXILOP_UMax:
  case DXILOP_UMin:
    return true;
  default:
    return false;
  }
}

static bool isKnownDXILTertiaryOpcode(int64_t op) {
  switch (op) {
  case DXILOP_FMad:
  case DXILOP_Fma:
  case DXILOP_IMad:
  case DXILOP_UMad:
    return true;
  default:
    return false;
  }
}

static const char *kMetalHeader = R"(#include <metal_stdlib>
using namespace metal;

inline uint dxmt_uint(bool v) { return v ? 1u : 0u; }
inline uint dxmt_uint(int v) { return uint(v); }
inline uint dxmt_uint(uint v) { return v; }
inline uint dxmt_uint(long v) { return uint(v); }
inline uint dxmt_uint(ulong v) { return uint(v); }
inline uint dxmt_uint(float v) { return uint(v); }
inline uint dxmt_uint(bool2 v) { return dxmt_uint(v.x); }
inline uint dxmt_uint(bool3 v) { return dxmt_uint(v.x); }
inline uint dxmt_uint(bool4 v) { return dxmt_uint(v.x); }
inline uint dxmt_uint(int2 v) { return uint(v.x); }
inline uint dxmt_uint(int3 v) { return uint(v.x); }
inline uint dxmt_uint(int4 v) { return uint(v.x); }
inline uint dxmt_uint(uint2 v) { return v.x; }
inline uint dxmt_uint(uint3 v) { return v.x; }
inline uint dxmt_uint(uint4 v) { return v.x; }
inline uint dxmt_uint(float2 v) { return uint(v.x); }
inline uint dxmt_uint(float3 v) { return uint(v.x); }
inline uint dxmt_uint(float4 v) { return uint(v.x); }
inline float dxmt_float(bool v) { return v ? 1.0f : 0.0f; }
inline float dxmt_float(int v) { return float(v); }
inline float dxmt_float(uint v) { return float(v); }
inline float dxmt_float(long v) { return float(v); }
inline float dxmt_float(ulong v) { return float(v); }
inline float dxmt_float(float v) { return v; }
inline float dxmt_float(bool2 v) { return dxmt_float(v.x); }
inline float dxmt_float(bool3 v) { return dxmt_float(v.x); }
inline float dxmt_float(bool4 v) { return dxmt_float(v.x); }
inline float dxmt_float(int2 v) { return float(v.x); }
inline float dxmt_float(int3 v) { return float(v.x); }
inline float dxmt_float(int4 v) { return float(v.x); }
inline float dxmt_float(uint2 v) { return float(v.x); }
inline float dxmt_float(uint3 v) { return float(v.x); }
inline float dxmt_float(uint4 v) { return float(v.x); }
inline float dxmt_float(float2 v) { return v.x; }
inline float dxmt_float(float3 v) { return v.x; }
inline float dxmt_float(float4 v) { return v.x; }
inline bool dxmt_bool(bool v) { return v; }
inline bool dxmt_bool(int v) { return v != 0; }
inline bool dxmt_bool(uint v) { return v != 0; }
inline bool dxmt_bool(long v) { return v != 0; }
inline bool dxmt_bool(ulong v) { return v != 0; }
inline bool dxmt_bool(float v) { return v != 0.0f; }
inline bool dxmt_bool(bool2 v) { return v.x; }
inline bool dxmt_bool(bool3 v) { return v.x; }
inline bool dxmt_bool(bool4 v) { return v.x; }
inline bool dxmt_bool(int2 v) { return v.x != 0; }
inline bool dxmt_bool(int3 v) { return v.x != 0; }
inline bool dxmt_bool(int4 v) { return v.x != 0; }
inline bool dxmt_bool(uint2 v) { return v.x != 0; }
inline bool dxmt_bool(uint3 v) { return v.x != 0; }
inline bool dxmt_bool(uint4 v) { return v.x != 0; }
inline bool dxmt_bool(float2 v) { return v.x != 0.0f; }
inline bool dxmt_bool(float3 v) { return v.x != 0.0f; }
inline bool dxmt_bool(float4 v) { return v.x != 0.0f; }

)";

static std::string escapeName(const std::string &s) {
  if (s.empty()) return "_";
  std::string r;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_')
      r += c;
    else
      r += '_';
  }
  if (!r.empty() && r[0] >= '0' && r[0] <= '9')
    r = "_" + r;
  return r;
}

static const char *componentSuffix(uint32_t component) {
  switch (component & 3) {
  case 0: return ".x";
  case 1: return ".y";
  case 2: return ".z";
  default: return ".w";
  }
}

static const char *mslScalarName(MSLScalarKind scalar) {
  switch (scalar) {
  case MSLScalarKind::UInt:
    return "uint";
  case MSLScalarKind::SInt:
    return "int";
  case MSLScalarKind::Float:
  default:
    return "float";
  }
}

static MSLStageIOType normalizedIOType(MSLStageIOType type) {
  if (!type.valid)
    type = {MSLScalarKind::Float, 4, true};
  if (type.components == 0)
    type.components = 1;
  type.components = std::min<uint32_t>(4, type.components);
  return type;
}

static std::string mslIOTypeName(MSLStageIOType type) {
  type = normalizedIOType(type);
  std::string name = mslScalarName(type.scalar);
  if (type.components > 1)
    name += std::to_string(type.components);
  return name;
}

static std::string mslIOZeroValue(MSLStageIOType type) {
  type = normalizedIOType(type);
  return mslIOTypeName(type) + "(0)";
}

static std::string scalarZeroValue(MSLScalarKind scalar) {
  switch (scalar) {
  case MSLScalarKind::UInt:
    return "0u";
  case MSLScalarKind::SInt:
    return "0";
  case MSLScalarKind::Float:
  default:
    return "0.0";
  }
}

static std::string readIOComponent(MSLStageIOType type, const std::string &field,
                                   uint32_t component) {
  type = normalizedIOType(type);
  if (component >= type.components)
    return scalarZeroValue(type.scalar);
  if (type.components == 1)
    return field;
  return field + componentSuffix(component);
}

static std::string writeIOComponent(MSLStageIOType type, const std::string &field,
                                    uint32_t component) {
  type = normalizedIOType(type);
  if (type.components == 1)
    return field;
  return field + componentSuffix(component);
}

static MSLStageIOType vertexInputType(const MSLConvertOptions &options,
                                      uint32_t signature_id) {
  if (signature_id >= 16 && signature_id < 32)
    signature_id -= 16;
  if (signature_id < options.vertex_inputs.size() &&
      options.vertex_inputs[signature_id].valid)
    return normalizedIOType(options.vertex_inputs[signature_id]);
  return {MSLScalarKind::Float, 4, true};
}

static MSLStageIOType pixelOutputType(const MSLConvertOptions &options,
                                      uint32_t target) {
  if (target < options.pixel_outputs.size() &&
      options.pixel_outputs[target].valid)
    return normalizedIOType(options.pixel_outputs[target]);
  return {MSLScalarKind::Float, 4, true};
}

static uint32_t vertexInputRegisterForSignature(const MSLConvertOptions &options,
                                                uint32_t signature_id) {
  if (signature_id < options.vertex_input_signature_valid.size() &&
      options.vertex_input_signature_valid[signature_id])
    return options.vertex_input_register_for_signature[signature_id];
  return signature_id;
}

static uint32_t vertexOutputRegisterForSignature(const MSLConvertOptions &options,
                                                 uint32_t signature_id) {
  if (signature_id < options.vertex_output_signature_valid.size() &&
      options.vertex_output_signature_valid[signature_id])
    return options.vertex_output_register_for_signature[signature_id];
  return signature_id;
}

static uint32_t pixelInputRegisterForSignature(const MSLConvertOptions &options,
                                               uint32_t signature_id) {
  if (signature_id < options.pixel_input_signature_valid.size() &&
      options.pixel_input_signature_valid[signature_id])
    return options.pixel_input_register_for_signature[signature_id];
  return signature_id;
}

static uint32_t pixelOutputTargetForSignature(const MSLConvertOptions &options,
                                              uint32_t signature_id) {
  if (signature_id < options.pixel_output_signature_valid.size() &&
      options.pixel_output_signature_valid[signature_id])
    return options.pixel_output_target_for_signature[signature_id];
  return signature_id;
}

static bool decodeVertexLoadInputSequence(const MSLConvertOptions &options,
                                          uint32_t seq, uint32_t &input_id,
                                          uint32_t &component) {
  for (uint32_t sig = 0; sig < options.vertex_input_signature_valid.size(); sig++) {
    if (!options.vertex_input_signature_valid[sig])
      continue;
    const uint32_t reg = vertexInputRegisterForSignature(options, sig);
    const uint32_t components = normalizedIOType(vertexInputType(options, reg)).components;
    if (seq < components) {
      input_id = sig;
      component = seq;
      return true;
    }
    seq -= components;
  }
  return false;
}

static bool decodePixelLoadInputSequence(const MSLConvertOptions &options,
                                         uint32_t seq, uint32_t &input_id,
                                         uint32_t &component) {
  for (uint32_t sig = 0; sig < options.pixel_input_signature_valid.size(); sig++) {
    if (!options.pixel_input_signature_valid[sig])
      continue;
    constexpr uint32_t kComponents = 4;
    if (seq < kComponents) {
      input_id = sig;
      component = seq;
      return true;
    }
    seq -= kComponents;
  }
  return false;
}

static bool decodeVertexStoreOutputSequence(const MSLConvertOptions &options,
                                            uint32_t seq, uint32_t &output_id,
                                            uint32_t &component) {
  for (uint32_t sig = 0; sig < options.vertex_output_signature_valid.size(); sig++) {
    if (!options.vertex_output_signature_valid[sig])
      continue;
    constexpr uint32_t kComponents = 4;
    if (seq < kComponents) {
      output_id = sig;
      component = seq;
      return true;
    }
    seq -= kComponents;
  }
  return false;
}

static bool decodePixelStoreOutputSequence(const MSLConvertOptions &options,
                                           uint32_t seq, uint32_t &output_id,
                                           uint32_t &component) {
  for (uint32_t sig = 0; sig < options.pixel_output_signature_valid.size(); sig++) {
    if (!options.pixel_output_signature_valid[sig])
      continue;
    const uint32_t target = pixelOutputTargetForSignature(options, sig);
    const uint32_t components = normalizedIOType(pixelOutputType(options, target)).components;
    if (seq < components) {
      output_id = sig;
      component = seq;
      return true;
    }
    seq -= components;
  }
  return false;
}

static const char *componentName(uint32_t component) {
  switch (component & 3) {
  case 0: return "x";
  case 1: return "y";
  case 2: return "z";
  default: return "w";
  }
}

static std::string userVaryingField(const char *base, uint32_t register_index) {
  switch (register_index) {
  case 0: return std::string(base) + ".v0";
  case 1: return std::string(base) + ".v1";
  case 2: return std::string(base) + ".v2";
  case 3: return std::string(base) + ".v3";
  case 4: return std::string(base) + ".v4";
  case 5: return std::string(base) + ".v5";
  case 6: return std::string(base) + ".v6";
  case 7: return std::string(base) + ".v7";
  case 8: return std::string(base) + ".v8";
  case 9: return std::string(base) + ".v9";
  case 10: return std::string(base) + ".v10";
  case 11: return std::string(base) + ".v11";
  case 12: return std::string(base) + ".v12";
  case 13: return std::string(base) + ".v13";
  case 14: return std::string(base) + ".v14";
  case 15: return std::string(base) + ".v15";
  default: return std::string(base) + ".v0";
  }
}

static std::string pixelInputField(const MSLConvertOptions &options,
                                   const char *base, uint32_t signature_id) {
  if (signature_id < options.pixel_input_is_position.size() &&
      options.pixel_input_is_position[signature_id])
    return std::string(base) + ".position";
  return userVaryingField(base, pixelInputRegisterForSignature(options, signature_id));
}

static std::string vertexOutputField(const MSLConvertOptions &options,
                                     const char *base, uint32_t signature_id) {
  if (signature_id < options.vertex_output_is_position.size() &&
      options.vertex_output_is_position[signature_id])
    return std::string(base) + ".position";
  return userVaryingField(base,
                          vertexOutputRegisterForSignature(options, signature_id));
}

static std::string varyingField(const char *base, uint32_t signature_id) {
  switch (signature_id) {
  case 0: return std::string(base) + ".position";
  case 1: return std::string(base) + ".v0";
  case 2: return std::string(base) + ".v1";
  case 3: return std::string(base) + ".v2";
  case 4: return std::string(base) + ".v3";
  case 5: return std::string(base) + ".v4";
  case 6: return std::string(base) + ".v5";
  case 7: return std::string(base) + ".v6";
  case 8: return std::string(base) + ".v7";
  default: return std::string(base) + ".v0";
  }
}

static std::string vertexInputField(const char *base, uint32_t signature_id) {
  if (signature_id >= 16 && signature_id < 32)
    signature_id -= 16;
  switch (signature_id) {
  case 0: return std::string(base) + ".a0";
  case 1: return std::string(base) + ".a1";
  case 2: return std::string(base) + ".a2";
  case 3: return std::string(base) + ".a3";
  case 4: return std::string(base) + ".a4";
  case 5: return std::string(base) + ".a5";
  case 6: return std::string(base) + ".a6";
  case 7: return std::string(base) + ".a7";
  case 8: return std::string(base) + ".a8";
  case 9: return std::string(base) + ".a9";
  case 10: return std::string(base) + ".a10";
  case 11: return std::string(base) + ".a11";
  case 12: return std::string(base) + ".a12";
  case 13: return std::string(base) + ".a13";
  case 14: return std::string(base) + ".a14";
  case 15: return std::string(base) + ".a15";
  default: return std::string(base) + ".a0";
  }
}

static bool parseUnsignedLiteral(const std::string &text, uint32_t &value) {
  if (text.empty())
    return false;
  char *end = nullptr;
  unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  value = (uint32_t)parsed;
  return true;
}

static bool parseSignedLiteral(const std::string &text, int64_t &value) {
  if (text.empty())
    return false;
  char *end = nullptr;
  long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  value = (int64_t)parsed;
  return true;
}

static std::string componentAccessor(const std::string &index) {
  uint32_t component = 0;
  if (parseUnsignedLiteral(index, component))
    return componentSuffix(component);
  return "[" + index + "]";
}

static std::string asUIntExpr(const std::string &expr) {
  return "dxmt_uint(" + expr + ")";
}

static std::string asIntegerIndexExpr(const std::string &expr) {
  uint32_t literal = 0;
  if (parseUnsignedLiteral(expr, literal))
    return expr;
  return asUIntExpr(expr);
}

static constexpr uint32_t kMaxMSLBufferBindings = 31;
static constexpr uint32_t kMaxMSLTextureBindings = 8;
static constexpr uint32_t kMaxMSLSamplerBindings = 16;

static uint32_t clampMSLBindingIndex(const char *target_prefix, uint32_t index) {
  uint32_t limit = kMaxMSLBufferBindings;
  if (std::strcmp(target_prefix, "tex") == 0)
    limit = kMaxMSLTextureBindings;
  else if (std::strcmp(target_prefix, "samp") == 0)
    limit = kMaxMSLSamplerBindings;
  return limit ? std::min(index, limit - 1) : 0;
}

static bool startsWith(const std::string &text, const char *prefix) {
  return text.rfind(prefix, 0) == 0;
}

static uint32_t inferDXIntrinsicIdFromName(const std::string &name) {
  if (!startsWith(name, "dx.op."))
    return 0;
  if (name.find("loadInput") != std::string::npos)
    return DXOP_LoadInput;
  if (name.find("storeOutput") != std::string::npos)
    return DXOP_StoreOutput;
  if (name.find("createHandleFromBinding") != std::string::npos)
    return DXOP_CreateHandleFromBinding;
  if (name.find("createHandleFromHeap") != std::string::npos)
    return DXOP_CreateHandleFromHeap;
  if (name.find("createHandleForLib") != std::string::npos)
    return DXOP_CreateHandleForLib;
  if (name.find("createHandle") != std::string::npos)
    return DXOP_CreateHandle;
  if (name.find("annotateHandle") != std::string::npos)
    return DXOP_AnnotateHandle;
  if (name.find("cbufferLoadLegacy") != std::string::npos)
    return DXOP_CBufferLoadLegacy;
  if (name.find("cbufferLoad") != std::string::npos)
    return DXOP_CBufferLoad;
  if (name.find("threadIdInGroup") != std::string::npos)
    return DXOP_ThreadIDInGroup;
  if (name.find("flattenedThreadIdInGroup") != std::string::npos)
    return DXOP_FlattenedThreadIDInGroup;
  if (name.find("threadId") != std::string::npos)
    return DXOP_ThreadId;
  if (name.find("groupId") != std::string::npos)
    return DXOP_GroupId;
  if (name.find("sampleLevel") != std::string::npos)
    return DXOP_TextureSampleLevel;
  if (name.find("sampleCmpLevel") != std::string::npos)
    return DXOP_TextureSampleCmpLevel;
  if (name.find("sampleCmp") != std::string::npos)
    return DXOP_TextureSampleCmp;
  if (name.find("sampleGrad") != std::string::npos)
    return DXOP_TextureSampleGrad;
  if (name.find("sampleBias") != std::string::npos)
    return DXOP_TextureSampleBias;
  if (name.find("sample") != std::string::npos)
    return DXOP_TextureSample;
  if (name.find("textureStoreSample") != std::string::npos)
    return DXOP_TextureStoreSample;
  if (name.find("textureStore") != std::string::npos)
    return DXOP_TextureStore;
  if (name.find("textureLoad") != std::string::npos)
    return DXOP_TextureLoad;
  if (name.find("bufferStore") != std::string::npos)
    return DXOP_BufferStore;
  if (name.find("rawBufferStore") != std::string::npos)
    return DXOP_RawBufferStore;
  if (name.find("rawBufferLoad") != std::string::npos)
    return DXOP_RawBufferLoad;
  if (name.find("bufferLoad") != std::string::npos)
    return DXOP_BufferLoad;
  if (name.find("legacyF16ToF32") != std::string::npos)
    return DXOP_LegacyF16ToF32;
  if (name.find("legacyF32ToF16") != std::string::npos)
    return DXOP_LegacyF32ToF16;
  if (name.find("unaryBits") != std::string::npos)
    return DXOP_UnaryBits;
  if (name.find("unary") != std::string::npos)
    return DXOP_Unary;
  if (name.find("binary") != std::string::npos)
    return DXOP_Binary;
  if (name.find("tertiary") != std::string::npos)
    return DXOP_Tertiary;
  if (name.find("dot2") != std::string::npos)
    return DXOP_Dot2;
  if (name.find("dot3") != std::string::npos)
    return DXOP_Dot3;
  if (name.find("dot4") != std::string::npos)
    return DXOP_Dot4;
  if (name.find("barrier") != std::string::npos)
    return DXOP_Barrier;
  return 0;
}

static uint32_t getFunctionParamCountForType(const LLVMModule &mod,
                                             uint32_t type_id) {
  if (type_id >= mod.types.size())
    return 0;
  if (mod.types[type_id].kind == LLVMType::Pointer &&
      !mod.types[type_id].type_refs.empty()) {
    type_id = mod.types[type_id].type_refs[0];
    if (type_id >= mod.types.size())
      return 0;
  }
  if (mod.types[type_id].kind != LLVMType::Function ||
      mod.types[type_id].type_refs.empty())
    return 0;
  return (uint32_t)mod.types[type_id].type_refs.size() - 1;
}

static const LLVMType *getScalarTypeForTypeId(const LLVMModule &mod,
                                              uint32_t type_id) {
  if (type_id >= mod.types.size())
    return nullptr;
  const LLVMType *type = &mod.types[type_id];
  while (type && type->kind == LLVMType::Vector && !type->type_refs.empty()) {
    type_id = type->type_refs[0];
    if (type_id >= mod.types.size())
      return nullptr;
    type = &mod.types[type_id];
  }
  return type;
}

static bool typeLooksFloatLike(const LLVMModule &mod, uint32_t type_id) {
  const LLVMType *type = getScalarTypeForTypeId(mod, type_id);
  return type && (type->kind == LLVMType::Float || type->kind == LLVMType::Double);
}

static bool typeLooksIntegerLike(const LLVMModule &mod, uint32_t type_id) {
  const LLVMType *type = getScalarTypeForTypeId(mod, type_id);
  return type && type->kind == LLVMType::Integer;
}

static std::string findDeclaredDXOpByFragment(const LLVMModule &mod,
                                              const char *fragment) {
  for (const auto &fn : mod.functions) {
    if (!fn.is_declaration)
      continue;
    if (fn.name.find(fragment) != std::string::npos)
      return fn.name;
  }
  return "";
}

static std::string inferDXIntrinsicNameFromCallShape(
    const LLVMModule &mod, uint32_t function_type_id, uint32_t result_type_id,
    const std::vector<uint32_t> &call_args, const std::string &first_arg_text) {
  const uint32_t param_count = getFunctionParamCountForType(mod, function_type_id);
  const bool float_like = typeLooksFloatLike(mod, result_type_id);
  const bool integer_like = typeLooksIntegerLike(mod, result_type_id);
  int64_t signed_literal = 0;
  const bool first_arg_is_signed_literal =
      parseSignedLiteral(first_arg_text, signed_literal);

  if ((call_args.size() == 2 || param_count == 2) && first_arg_is_signed_literal) {
    if (integer_like) {
      auto match = findDeclaredDXOpByFragment(mod, "unaryBits");
      if (!match.empty())
        return match;
    }
    if (float_like && isKnownDXILUnaryOpcode(signed_literal)) {
      auto match = findDeclaredDXOpByFragment(mod, "unary.f32");
      if (!match.empty())
        return match;
    }
  }

  if ((call_args.size() == 3 || param_count == 3) && first_arg_is_signed_literal &&
      isKnownDXILBinaryOpcode(signed_literal)) {
    auto match = findDeclaredDXOpByFragment(mod, "binary");
    if (!match.empty())
      return match;
  }

  if (call_args.size() == 4 || param_count == 4) {
    if (first_arg_is_signed_literal && float_like &&
        isKnownDXILTertiaryOpcode(signed_literal)) {
      auto match = findDeclaredDXOpByFragment(mod, "tertiary.f32");
      if (!match.empty())
        return match;
    }
    if (integer_like) {
      auto match = findDeclaredDXOpByFragment(mod, "bufferLoad.i32");
      if (!match.empty())
        return match;
    } else {
      auto match = findDeclaredDXOpByFragment(mod, "bufferLoad.f32");
      if (!match.empty())
        return match;
    }
  }

  if (call_args.size() == 6 || param_count == 6) {
    if (integer_like) {
      auto match = findDeclaredDXOpByFragment(mod, "rawBufferLoad.i32");
      if (!match.empty())
        return match;
    } else {
      auto match = findDeclaredDXOpByFragment(mod, "rawBufferLoad.f32");
      if (!match.empty())
        return match;
    }
  }

  if (call_args.size() == 1 || param_count == 1) {
    if (integer_like) {
      auto match = findDeclaredDXOpByFragment(mod, "legacyF32ToF16");
      if (!match.empty())
        return match;
    }
    if (float_like) {
      auto match = findDeclaredDXOpByFragment(mod, "legacyF16ToF32");
      if (!match.empty())
        return match;
    }
  }

  return "";
}

static std::string findDeclaredDXOpNameForType(const LLVMModule &mod,
                                               uint32_t type_id) {
  std::string match;
  for (const auto &fn : mod.functions) {
    if (!fn.is_declaration || fn.type_id != type_id || !startsWith(fn.name, "dx.op."))
      continue;
    if (!match.empty())
      return "";
    match = fn.name;
  }
  return match;
}

static uint32_t findFunctionTypeForValue(const LLVMModule &mod, uint32_t value_id) {
  for (const auto &fn : mod.functions) {
    if (fn.value_id == value_id)
      return fn.type_id;
  }
  return 0;
}

static std::string findFunctionNameForValue(const LLVMModule &mod, uint32_t value_id) {
  for (const auto &fn : mod.functions) {
    if (fn.is_declaration && fn.value_id == value_id)
      return fn.name;
  }
  return "";
}

static bool isFunctionLikeSymbol(const std::string &text) {
  auto endsWith = [&](const char *suffix) {
    size_t suffix_len = std::strlen(suffix);
    return text.size() >= suffix_len &&
           text.compare(text.size() - suffix_len, suffix_len, suffix) == 0;
  };
  return startsWith(text, "dx.op.") || text == "cs_main" || text == "vs_main" ||
         text == "ps_main" || text.find("MainCS") != std::string::npos ||
         text.find("_CS") != std::string::npos || endsWith("CS") ||
         endsWith("Main") || endsWith("VS") || endsWith("PS");
}

static bool parseSSAName(const std::string &text, uint32_t &value_id) {
  if (text.size() < 2 || text[0] != 'v')
    return false;
  return parseUnsignedLiteral(text.substr(1), value_id);
}

static bool isIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool isMemberAccessToken(const std::string &source, size_t pos) {
  if (pos == 0)
    return false;
  size_t prev = pos;
  while (prev > 0 &&
         std::isspace(static_cast<unsigned char>(source[prev - 1])))
    prev--;
  return prev > 0 && source[prev - 1] == '.';
}

static void collectSSATokens(const std::string &source,
                             std::unordered_set<uint32_t> &used,
                             std::unordered_set<uint32_t> &declared) {
  for (size_t pos = 0; pos < source.size(); pos++) {
    if (source[pos] != 'v' || pos + 1 >= source.size() ||
        !std::isdigit(static_cast<unsigned char>(source[pos + 1])))
      continue;
    if (pos > 0 && isIdentifierChar(source[pos - 1]))
      continue;
    if (isMemberAccessToken(source, pos))
      continue;

    size_t end = pos + 1;
    while (end < source.size() &&
           std::isdigit(static_cast<unsigned char>(source[end])))
      end++;
    if (end < source.size() && isIdentifierChar(source[end]))
      continue;

    uint32_t value_id = 0;
    if (!parseUnsignedLiteral(source.substr(pos + 1, end - pos - 1), value_id))
      continue;

    used.insert(value_id);
    size_t line_start = source.rfind('\n', pos);
    line_start = line_start == std::string::npos ? 0 : line_start + 1;
    std::string prefix = source.substr(line_start, pos - line_start);
    size_t non_space = prefix.find_first_not_of(" \t");
    std::string trimmed =
        non_space == std::string::npos ? std::string() : prefix.substr(non_space);
    if (trimmed == "auto ")
      declared.insert(value_id);
  }
}

static std::string trimMSLExpr(std::string expr) {
  size_t first = expr.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return std::string();
  size_t last = expr.find_last_not_of(" \t\r\n");
  return expr.substr(first, last - first + 1);
}

static bool outerParensWrapExpr(const std::string &expr) {
  if (expr.size() < 2 || expr.front() != '(' || expr.back() != ')')
    return false;
  int depth = 0;
  for (size_t i = 0; i < expr.size(); i++) {
    if (expr[i] == '(') {
      depth++;
    } else if (expr[i] == ')') {
      depth--;
      if (depth == 0 && i + 1 < expr.size())
        return false;
    }
  }
  return depth == 0;
}

static std::string stripBalancedOuterParens(std::string expr) {
  expr = trimMSLExpr(std::move(expr));
  while (outerParensWrapExpr(expr))
    expr = trimMSLExpr(expr.substr(1, expr.size() - 2));
  return expr;
}

static bool exprEndsWithScalarComponent(std::string expr) {
  expr = stripBalancedOuterParens(std::move(expr));
  auto ends_with = [&](const char *suffix) {
    const size_t suffix_len = std::strlen(suffix);
    return expr.size() >= suffix_len &&
           expr.compare(expr.size() - suffix_len, suffix_len, suffix) == 0;
  };
  return ends_with(".x") || ends_with(".y") || ends_with(".z") ||
         ends_with(".w") || ends_with(".r") || ends_with(".g") ||
         ends_with(".b") || ends_with(".a");
}

static bool exprContainsUnsuffixedVectorValue(const std::string &expr) {
  auto containsUnsuffixedCast = [&](const char *needle) {
    size_t pos = 0;
    while ((pos = expr.find(needle, pos)) != std::string::npos) {
      size_t cast_end = expr.find("&>", pos);
      if (cast_end == std::string::npos)
        return true;
      size_t open = expr.find('(', cast_end + 2);
      if (open == std::string::npos)
        return true;
      int depth = 0;
      size_t end = std::string::npos;
      for (size_t i = open; i < expr.size(); i++) {
        if (expr[i] == '(')
          depth++;
        else if (expr[i] == ')') {
          depth--;
          if (depth == 0) {
            end = i;
            break;
          }
        }
      }
      if (end == std::string::npos)
        return true;
      while (end + 1 < expr.size() &&
             (expr[end + 1] == ' ' || expr[end + 1] == '\t' ||
              expr[end + 1] == '\n' || expr[end + 1] == '\r' ||
              expr[end + 1] == ')'))
        end++;
      if (end + 2 >= expr.size() || expr[end + 1] != '.' ||
          (expr[end + 2] != 'x' && expr[end + 2] != 'y' &&
           expr[end + 2] != 'z' && expr[end + 2] != 'w') ||
          (end + 3 < expr.size() && isIdentifierChar(expr[end + 3])))
        return true;
      pos = end + 3;
    }
    return false;
  };
  return containsUnsuffixedCast("reinterpret_cast<device float4&>") ||
         containsUnsuffixedCast("reinterpret_cast<device uint4&>") ||
         containsUnsuffixedCast("reinterpret_cast<device int4&>") ||
         containsUnsuffixedCast("reinterpret_cast<const device float4&>") ||
         containsUnsuffixedCast("reinterpret_cast<const device uint4&>") ||
         containsUnsuffixedCast("reinterpret_cast<const device int4&>");
}

static std::string scalarizeUnsuffixedVectorValuesInExpr(std::string expr) {
  auto scalarizeCast = [&](const char *needle) {
    size_t pos = 0;
    while ((pos = expr.find(needle, pos)) != std::string::npos) {
      size_t cast_end = expr.find("&>", pos);
      if (cast_end == std::string::npos) {
        pos += std::strlen(needle);
        continue;
      }
      size_t open = expr.find('(', cast_end + 2);
      if (open == std::string::npos) {
        pos += std::strlen(needle);
        continue;
      }

      int depth = 0;
      size_t end = std::string::npos;
      for (size_t i = open; i < expr.size(); i++) {
        if (expr[i] == '(') {
          depth++;
        } else if (expr[i] == ')') {
          depth--;
          if (depth == 0) {
            end = i;
            break;
          }
        }
      }
      if (end == std::string::npos) {
        pos += std::strlen(needle);
        continue;
      }

      auto nextNonSpace = [&](size_t index) {
        while (index < expr.size() &&
               (expr[index] == ' ' || expr[index] == '\t' ||
                expr[index] == '\n' || expr[index] == '\r'))
          index++;
        return index;
      };
      auto hasComponentSuffixAt = [&](size_t index) {
        index = nextNonSpace(index);
        return index + 1 < expr.size() && expr[index] == '.' &&
               (expr[index + 1] == 'x' || expr[index + 1] == 'y' ||
                expr[index + 1] == 'z' || expr[index + 1] == 'w') &&
               (index + 2 >= expr.size() || !isIdentifierChar(expr[index + 2]));
      };

      size_t next = nextNonSpace(end + 1);
      bool suffixed = hasComponentSuffixAt(next);
      while (!suffixed && next < expr.size() && expr[next] == ')') {
        next = nextNonSpace(next + 1);
        suffixed = hasComponentSuffixAt(next);
      }
      if (suffixed) {
        pos = next + 2;
        continue;
      }

      std::string cast_expr = expr.substr(pos, end - pos + 1);
      std::string replacement = "(" + cast_expr + ").x";
      expr.replace(pos, end - pos + 1, replacement);
      pos += replacement.size();
    }
  };

  scalarizeCast("reinterpret_cast<device float4&>");
  scalarizeCast("reinterpret_cast<device uint4&>");
  scalarizeCast("reinterpret_cast<device int4&>");
  scalarizeCast("reinterpret_cast<const device float4&>");
  scalarizeCast("reinterpret_cast<const device uint4&>");
  scalarizeCast("reinterpret_cast<const device int4&>");
  return expr;
}

static bool exprContainsScalarComponentAccess(const std::string &expr) {
  for (size_t pos = 0; pos + 1 < expr.size(); pos++) {
    if (expr[pos] != '.')
      continue;
    char component = expr[pos + 1];
    if (component != 'x' && component != 'y' &&
        component != 'z' && component != 'w' &&
        component != 'r' && component != 'g' &&
        component != 'b' && component != 'a')
      continue;
    if (pos + 2 < expr.size() && isIdentifierChar(expr[pos + 2]))
      continue;
    return true;
  }
  return false;
}

static bool exprContainsUnsuffixedTextureRead(const std::string &expr) {
  auto containsUnsuffixedCall = [&](const char *needle) {
  size_t pos = 0;
  while ((pos = expr.find(needle, pos)) != std::string::npos) {
    size_t open = pos + std::strlen(needle) - 1;
    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = open; i < expr.size(); i++) {
      if (expr[i] == '(')
        depth++;
      else if (expr[i] == ')') {
        depth--;
        if (depth == 0) {
          end = i;
          break;
        }
      }
    }
    if (end == std::string::npos)
      return true;

    auto nextNonSpace = [&](size_t index) {
      while (index < expr.size() &&
             (expr[index] == ' ' || expr[index] == '\t' ||
              expr[index] == '\n' || expr[index] == '\r'))
        index++;
      return index;
    };
    auto hasComponentSuffixAt = [&](size_t index) {
      index = nextNonSpace(index);
      return index + 1 < expr.size() && expr[index] == '.' &&
             (expr[index + 1] == 'x' || expr[index + 1] == 'y' ||
              expr[index + 1] == 'z' || expr[index + 1] == 'w' ||
              expr[index + 1] == 'r' || expr[index + 1] == 'g' ||
              expr[index + 1] == 'b' || expr[index + 1] == 'a') &&
             (index + 2 >= expr.size() || !isIdentifierChar(expr[index + 2]));
    };

    size_t next = nextNonSpace(end + 1);
    bool suffixed = hasComponentSuffixAt(next);
    while (!suffixed && next < expr.size() && expr[next] == ')') {
      next = nextNonSpace(next + 1);
      suffixed = hasComponentSuffixAt(next);
    }
    if (!suffixed)
      return true;

    pos = end + 1;
  }
  return false;
  };
  if (containsUnsuffixedCall(".read("))
    return true;
  if (containsUnsuffixedCall(".sample("))
    return true;
  return false;
}

static bool exprLooksScalar(const std::string &expr) {
  std::string trimmed = expr;
  size_t first = trimmed.find_first_not_of(" \t\r\n(");
  if (first != std::string::npos)
    trimmed = trimmed.substr(first);

  if (trimmed.empty() || trimmed == "0" || trimmed == "0.0" ||
      trimmed == "0.0f" || trimmed == "false")
    return true;
  if (exprEndsWithScalarComponent(trimmed))
    return true;

  int64_t signed_literal = 0;
  if (parseSignedLiteral(trimmed, signed_literal))
    return true;

  // Expressions assembled from scalar swizzles, like a dot product lowered to
  // "(a.x*b.x + a.y*b.y)", are scalar even if they reference vector sources.
  if (!exprContainsUnsuffixedTextureRead(trimmed) &&
      !exprContainsUnsuffixedVectorValue(trimmed) &&
      exprContainsScalarComponentAccess(trimmed))
    return true;

  return false;
}

static uint8_t inferVectorLaneCountFromExpr(const std::string &expr) {
  std::string trimmed = expr;
  size_t first = trimmed.find_first_not_of(" \t\r\n(");
  if (first != std::string::npos)
    trimmed = trimmed.substr(first);
  auto startsExpr = [&](const char *prefix) {
    const size_t len = std::strlen(prefix);
    return trimmed.size() >= len && trimmed.compare(0, len, prefix) == 0;
  };
  if (exprLooksScalar(expr))
    return 0;
  if (startsExpr("float4") || startsExpr("uint4") || startsExpr("int4") ||
      startsExpr("bool4") || startsExpr("vec<float, 4>") ||
      startsExpr("reinterpret_cast<device float4&>") ||
      startsExpr("reinterpret_cast<device uint4&>") ||
      startsExpr("reinterpret_cast<const device float4&>") ||
      startsExpr("reinterpret_cast<const device uint4&>") ||
      trimmed.find(".sample(") != std::string::npos ||
      exprContainsUnsuffixedVectorValue(trimmed) ||
      exprContainsUnsuffixedTextureRead(trimmed))
    return 4;
  if (startsExpr("float3") || startsExpr("uint3") || startsExpr("int3") ||
      startsExpr("bool3") || startsExpr("vec<float, 3>"))
    return 3;
  if (startsExpr("float2") || startsExpr("uint2") || startsExpr("int2") ||
      startsExpr("bool2") || startsExpr("vec<float, 2>"))
    return 2;
  if (exprEndsWithScalarComponent(expr))
    return 0;
  if (startsExpr("clamp(") || startsExpr("fma(") || startsExpr("abs(") ||
      startsExpr("cos(") || startsExpr("sin(") || startsExpr("tan(") ||
      startsExpr("acos(") || startsExpr("asin(") || startsExpr("atan(") ||
      startsExpr("exp2(") || startsExpr("fract(") || startsExpr("log2(") ||
      startsExpr("sqrt(") || startsExpr("rsqrt(") || startsExpr("rint(") ||
      startsExpr("floor(") || startsExpr("ceil(") || startsExpr("trunc("))
    return 0;
  return 0;
}

static std::string scalarizeMSLExpr(const std::string &expr, uint8_t lanes) {
  std::string scalarized_expr = scalarizeUnsuffixedVectorValuesInExpr(expr);
  if (exprLooksScalar(scalarized_expr))
    return scalarized_expr;
  uint8_t inferred_lanes = inferVectorLaneCountFromExpr(scalarized_expr);
  if (inferred_lanes > 1 || lanes > 1)
    return "(" + scalarized_expr + ").x";
  return scalarized_expr;
}

static std::string normalizeMSLNumericExpr(std::string expr) {
  if (exprContainsUnsuffixedVectorValue(expr))
    expr = scalarizeUnsuffixedVectorValuesInExpr(std::move(expr));
  return expr;
}

static bool tryFoldIntegerBinary(LLVMInstruction::Opcode opcode,
                                 const std::string &lhs_text,
                                 const std::string &rhs_text,
                                 std::string &result_text) {
  int64_t lhs = 0;
  int64_t rhs = 0;
  if (!parseSignedLiteral(lhs_text, lhs) || !parseSignedLiteral(rhs_text, rhs))
    return false;

  switch (opcode) {
  case LLVMInstruction::Add:
    result_text = std::to_string(lhs + rhs);
    return true;
  case LLVMInstruction::Sub:
    result_text = std::to_string(lhs - rhs);
    return true;
  case LLVMInstruction::Mul:
    result_text = std::to_string(lhs * rhs);
    return true;
  case LLVMInstruction::And:
    result_text = std::to_string(lhs & rhs);
    return true;
  case LLVMInstruction::Or:
    result_text = std::to_string(lhs | rhs);
    return true;
  case LLVMInstruction::Xor:
    result_text = std::to_string(lhs ^ rhs);
    return true;
  case LLVMInstruction::Shl:
    result_text = std::to_string(lhs << rhs);
    return true;
  case LLVMInstruction::LShr:
    result_text = std::to_string((uint64_t)lhs >> rhs);
    return true;
  case LLVMInstruction::AShr:
    result_text = std::to_string(lhs >> rhs);
    return true;
  default:
    return false;
  }
}

static bool isBoolTypeId(uint32_t type_id, const LLVMModule &mod) {
  return type_id < mod.types.size() &&
         mod.types[type_id].kind == LLVMType::Integer &&
         mod.types[type_id].bit_width == 1;
}

static bool isIntegerLikeTypeId(uint32_t type_id, const LLVMModule &mod) {
  return type_id < mod.types.size() &&
         mod.types[type_id].kind == LLVMType::Integer;
}

static uint8_t vectorLaneCountForTypeId(uint32_t type_id, const LLVMModule &mod) {
  if (type_id >= mod.types.size())
    return 0;
  const auto &type = mod.types[type_id];
  if (type.kind != LLVMType::Vector)
    return 0;
  if (type.bit_width > 0 && type.bit_width <= 16)
    return (uint8_t)type.bit_width;
  if (!type.subtypes.empty() && type.subtypes.size() <= 16)
    return (uint8_t)type.subtypes.size();
  return 4;
}

static uint8_t intrinsicResultVectorLaneCount(uint32_t intrinsic_id) {
  switch (intrinsic_id) {
  case DXOP_CBufferLoad:
  case DXOP_CBufferLoadLegacy:
  case DXOP_BufferLoad:
  case DXOP_RawBufferLoad:
  case DXOP_RawBufferVectorLoad:
  case DXOP_RawBufferLoadLegacy:
  case DXOP_TextureLoad:
  case DXOP_TextureSample:
  case DXOP_TextureSampleBias:
  case DXOP_TextureSampleLevel:
  case DXOP_TextureSampleGrad:
  case DXOP_TextureGather:
  case DXOP_TextureGatherCmp:
  case DXOP_TextureGatherRaw:
  case DXOP_GetDimensions:
    return 4;
  default:
    return 0;
  }
}

static bool isSideEffectOnlyIntrinsic(uint32_t intrinsic_id) {
  switch (intrinsic_id) {
  case DXOP_BufferStore:
  case DXOP_RawBufferStore:
  case DXOP_RawBufferVectorStore:
  case DXOP_RawBufferStoreLegacy:
  case DXOP_TextureStore:
  case DXOP_TextureStoreSample:
  case DXOP_StoreOutput:
  case DXOP_Barrier:
    return true;
  default:
    return false;
  }
}

static std::vector<std::string> parseAggregateLiteral(const std::string &text) {
  std::vector<std::string> values;
  if (!startsWith(text, "agg(") || text.size() < 5 || text.back() != ')')
    return values;
  size_t start = 4;
  while (start < text.size() - 1) {
    size_t comma = text.find(',', start);
    size_t end = comma == std::string::npos ? text.size() - 1 : comma;
    values.push_back(text.substr(start, end - start));
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return values;
}

static std::string normalizeFloatSuffixLiteral(const std::string &text) {
  if (text == "nan" || text == "+nan" || text == "-nan" ||
      text == "nanf" || text == "+nanf" || text == "-nanf")
    return "as_type<float>(0x7fc00000u)";
  if (text == "inf" || text == "+inf" || text == "inff" ||
      text == "+inff")
    return "INFINITY";
  if (text == "-inf" || text == "-inff")
    return "-INFINITY";
  if (text.size() < 2 || text.back() != 'f')
    return text;
  std::string body = text.substr(0, text.size() - 1);
  if (body.find_first_not_of("+-0123456789") != std::string::npos)
    return text;
  return body + ".0f";
}

static std::string defaultValueForTypeId(uint32_t type_id, const LLVMModule &mod);

static std::string normalizeConstantData(const std::string &text, uint32_t type_id,
                                         const LLVMModule &mod) {
  if (text.empty())
    return defaultValueForTypeId(type_id, mod);
  if (startsWith(text, "agg("))
    return defaultValueForTypeId(type_id, mod);
  return normalizeFloatSuffixLiteral(text);
}

static bool isZeroLiteral(const std::string &text) {
  return text == "0" || text == "0.0" || text == "0.0f" || text == "false";
}

static bool isPointerLikeMSLExpr(const std::string &text) {
  return startsWith(text, "buf") || text.find("_storage") != std::string::npos ||
         text.find("device char*") != std::string::npos ||
         text.find("device char *") != std::string::npos;
}

static std::string vectorComponent(const std::string &vector, uint32_t index) {
  return "(" + vector + ")" + componentSuffix(index);
}

static const char *bindingPrefixForClass(uint32_t resource_class) {
  switch (resource_class) {
  case 0: return "srv";
  case 1: return "uav";
  case 2: return "cbuf";
  case 3: return "samp";
  default: return "buf";
  }
}

static std::string resolveBindingName(const std::string &handle, const char *target_prefix) {
  if (handle.empty() || isFunctionLikeSymbol(handle))
    return std::string(target_prefix) + "0";
  const char *prefixes[] = {"srv", "uav", "cbuf", "buf", "tex", "samp"};
  for (auto *prefix : prefixes) {
    if (startsWith(handle, prefix)) {
      const char *suffix = handle.c_str() + std::strlen(prefix);
      uint32_t literal_index = 0;
      if (parseUnsignedLiteral(suffix, literal_index))
        return std::string(target_prefix) +
               std::to_string(clampMSLBindingIndex(target_prefix, literal_index));
      return std::string(target_prefix) + "0";
    }
  }
  uint32_t literal_index = 0;
  if (parseUnsignedLiteral(handle, literal_index))
    return std::string(target_prefix) +
           std::to_string(clampMSLBindingIndex(target_prefix, literal_index));
  return std::string(target_prefix) + "0";
}

static bool isHandleIntrinsic(uint32_t intrinsic_id) {
  switch (intrinsic_id) {
  case DXOP_CreateHandle:
  case DXOP_CreateHandleForLib:
  case DXOP_AnnotateHandle:
  case DXOP_CreateHandleFromBinding:
  case DXOP_CreateHandleFromHeap:
    return true;
  default:
    return false;
  }
}

static bool isKnownDXIntrinsic(uint32_t intrinsic_id) {
  switch (intrinsic_id) {
  case DXOP_LoadInput:
  case DXOP_StoreOutput:
  case DXOP_CreateHandle:
  case DXOP_CreateHandleForLib:
  case DXOP_AnnotateHandle:
  case DXOP_CreateHandleFromBinding:
  case DXOP_CreateHandleFromHeap:
  case DXOP_CBufferLoad:
  case DXOP_CBufferLoadLegacy:
  case DXOP_ThreadId:
  case DXOP_GroupId:
  case DXOP_ThreadIDInGroup:
  case DXOP_FlattenedThreadIDInGroup:
  case DXOP_BufferLoad:
  case DXOP_BufferStore:
  case DXOP_TextureLoad:
  case DXOP_TextureStore:
  case DXOP_TextureStoreSample:
  case DXOP_TextureGather:
  case DXOP_TextureGatherCmp:
  case DXOP_TextureGatherRaw:
  case DXOP_TextureSample:
  case DXOP_TextureSampleBias:
  case DXOP_TextureSampleLevel:
  case DXOP_TextureSampleGrad:
  case DXOP_TextureSampleCmp:
  case DXOP_TextureSampleCmpLevelZero:
  case DXOP_TextureSampleCmpLevel:
  case DXOP_BufferUpdateCounter:
  case DXOP_CheckAccessFullyMapped:
  case DXOP_GetDimensions:
  case DXOP_Barrier:
  case DXOP_Unary:
  case DXOP_Binary:
  case DXOP_Tertiary:
  case DXOP_Dot2:
  case DXOP_Dot3:
  case DXOP_Dot4:
  case DXOP_MakeDouble:
  case DXOP_SplitDouble:
  case DXOP_RawBufferLoad:
  case DXOP_RawBufferStore:
  case DXOP_RawBufferVectorLoad:
  case DXOP_RawBufferVectorStore:
  case DXOP_RawBufferLoadLegacy:
  case DXOP_RawBufferStoreLegacy:
  case DXOP_AtomicBinOp:
  case DXOP_AtomicCompareExchange:
  case DXOP_DerivCoarseX:
  case DXOP_DerivCoarseY:
  case DXOP_DerivFineX:
  case DXOP_DerivFineY:
  case DXOP_CalcLOD:
  case DXOP_Texture2DMSGetSamplePosition:
  case DXOP_RenderTargetGetSamplePosition:
  case DXOP_RenderTargetGetSampleCount:
  case DXOP_Texture2DMSGetSamplePositionLegacy:
  case DXOP_RenderTargetGetSamplePositionLegacy:
  case DXOP_NumPrimitives:
  case DXOP_NumOutputVertices:
    return true;
  default:
    return false;
  }
}

void DXILToMSL::recordDiagnostic(EmitContext &ctx, const char *fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  ctx.diagnostics.emplace_back(buffer);
  DXTRACE("%s", buffer);
}

std::string DXILToMSL::getTypeName(const LLVMType &t, const LLVMModule &mod) {
  switch (t.kind) {
  case LLVMType::Void: return "void";
  case LLVMType::Float: return "float";
  case LLVMType::Double: return "float64_t";
  case LLVMType::Integer:
    if (t.bit_width == 1) return "bool";
    if (t.bit_width == 8) return "char";
    if (t.bit_width == 16) return "short";
    if (t.bit_width == 32) return "int";
    if (t.bit_width == 64) return "long";
    return "int";
  case LLVMType::Pointer: return "device char*";
  case LLVMType::Struct: return "char" + std::to_string((uint64_t)&t % 997);
  case LLVMType::Array: return "array<char," + std::to_string(t.bit_width) + ">";
  case LLVMType::Vector: {
    if (t.subtypes.empty())
      return "float4";
    return getTypeName(t.subtypes[0], mod) + std::to_string(t.bit_width);
  }
  case LLVMType::Function: return "void";
  }
  return "int";
}

std::string DXILToMSL::getVectorTypeName(const LLVMType &elem, uint32_t count, const LLVMModule &mod) {
  return getTypeName(elem, mod) + std::to_string(count);
}

uint32_t DXILToMSL::getTypeSize(const LLVMType &t, const LLVMModule &mod) {
  switch (t.kind) {
  case LLVMType::Void: return 0;
  case LLVMType::Float: return 4;
  case LLVMType::Double: return 8;
  case LLVMType::Integer: return (t.bit_width + 7) / 8;
  case LLVMType::Pointer: return 8;
  case LLVMType::Struct: {
    uint32_t s = 0;
    for (auto &st : t.subtypes)
      s += getTypeSize(st, mod);
    return s;
  }
  case LLVMType::Array: return t.bit_width * (t.subtypes.empty() ? 4 : getTypeSize(t.subtypes[0], mod));
  case LLVMType::Vector: return t.bit_width * 4;
  case LLVMType::Function: return 0;
  }
  return 4;
}

std::string DXILToMSL::emitValue(uint32_t idx) {
  if (idx == 0xFFFFFFFF) return "undef";
  return "v" + std::to_string(idx);
}

std::string DXILToMSL::emitConstant(const std::vector<uint64_t> &ops, uint32_t type_id, const LLVMModule &mod) {
  if (type_id >= mod.types.size())
    return "0";
  auto &t = mod.types[type_id];
  if (ops.empty())
    return "0";
  switch (t.kind) {
  case LLVMType::Integer:
    if (t.bit_width == 1) return ops[0] ? "true" : "false";
    if (t.bit_width <= 32) return std::to_string((int32_t)ops[0]);
    return std::to_string((int64_t)ops[0]);
  case LLVMType::Float: {
    float f;
    uint32_t u = (uint32_t)ops[0];
    memcpy(&f, &u, 4);
    if (std::isnan(f))
      return "as_type<float>(0x7fc00000u)";
    if (std::isinf(f))
      return f < 0.0f ? "-INFINITY" : "INFINITY";
    char buf[64];
    snprintf(buf, sizeof(buf), "%.9g", (double)f);
    if (strstr(buf, "nan") || strstr(buf, "NaN") || strstr(buf, "NAN"))
      return "as_type<float>(0x7fc00000u)";
    if (strstr(buf, "inf") || strstr(buf, "Inf") || strstr(buf, "INF"))
      return f < 0.0f ? "-INFINITY" : "INFINITY";
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
      strcat(buf, ".0");
    return std::string(buf) + "f";
  }
  case LLVMType::Double: {
    double d;
    uint64_t u = ops[0];
    memcpy(&d, &u, 8);
    if (std::isnan(d))
      return "as_type<double>(0x7ff8000000000000ul)";
    if (std::isinf(d))
      return d < 0.0 ? "-INFINITY" : "INFINITY";
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", d);
    if (strstr(buf, "nan") || strstr(buf, "NaN") || strstr(buf, "NAN"))
      return "as_type<double>(0x7ff8000000000000ul)";
    if (strstr(buf, "inf") || strstr(buf, "Inf") || strstr(buf, "INF"))
      return d < 0.0 ? "-INFINITY" : "INFINITY";
    return std::string(buf);
  }
  default:
    return "0";
  }
}

static std::string defaultValueForType(const LLVMType &t, const LLVMModule &mod) {
  switch (t.kind) {
  case LLVMType::Float:
  case LLVMType::Double:
    return "0.0f";
  case LLVMType::Integer:
    if (t.bit_width == 1)
      return "false";
    return "0";
  case LLVMType::Pointer:
    return "0";
  case LLVMType::Vector: {
    std::string type_name = "int";
    if (!t.subtypes.empty()) {
      const auto &elem = t.subtypes[0];
      if (elem.kind == LLVMType::Float || elem.kind == LLVMType::Double)
        type_name = "float";
      else if (elem.kind == LLVMType::Integer && elem.bit_width == 1)
        type_name = "bool";
      else if (elem.kind == LLVMType::Integer)
        type_name = "int";
    }
    type_name += std::to_string(t.subtypes.size());
    if (type_name == "float2" || type_name == "float3" || type_name == "float4")
      return type_name + "(0.0f)";
    if (type_name == "int2" || type_name == "int3" || type_name == "int4")
      return type_name + "(0)";
    if (type_name == "uint2" || type_name == "uint3" || type_name == "uint4")
      return type_name + "(0u)";
    return type_name + "(0)";
  }
  default:
    return "0";
  }
}

static std::string defaultValueForTypeId(uint32_t type_id, const LLVMModule &mod) {
  if (type_id < mod.types.size())
    return defaultValueForType(mod.types[type_id], mod);
  return "0";
}

static bool isPointerTypeId(uint32_t type_id, const LLVMModule &mod) {
  return type_id < mod.types.size() && mod.types[type_id].kind == LLVMType::Pointer;
}

void DXILToMSL::emitBindings(EmitContext &ctx) {
  auto &os = ctx.os;

  if (ctx.shader.kind == DxilShaderKind::Compute) {
    os << "  uint3 dtid [[thread_position_in_grid]];\n";
    os << "  uint3 gtid [[thread_position_in_threadgroup]];\n";
    os << "  uint3 ggid [[threadgroup_position_in_grid]];\n";
    os << "  uint3 gsz [[threads_per_threadgroup]];\n";
    ctx.uses_thread_id = true;
    ctx.uses_group_id = true;
    ctx.uses_group_thread_id = true;
    ctx.uses_group_size = true;
  }

  os << "\n";
}

void DXILToMSL::emitFunctionPrologue(EmitContext &ctx) {
  auto &os = ctx.os;
  os << kMetalHeader;

  os << "struct input_v {\n";
  os << "  float4 position [[position]];\n";
  os << "  float4 v0 [[user(locn0)]]; float4 v1 [[user(locn1)]];\n";
  os << "  float4 v2 [[user(locn2)]]; float4 v3 [[user(locn3)]];\n";
  os << "  float4 v4 [[user(locn4)]]; float4 v5 [[user(locn5)]];\n";
  os << "  float4 v6 [[user(locn6)]]; float4 v7 [[user(locn7)]];\n";
  os << "  float4 v8 [[user(locn8)]]; float4 v9 [[user(locn9)]];\n";
  os << "  float4 v10 [[user(locn10)]]; float4 v11 [[user(locn11)]];\n";
  os << "  float4 v12 [[user(locn12)]]; float4 v13 [[user(locn13)]];\n";
  os << "  float4 v14 [[user(locn14)]]; float4 v15 [[user(locn15)]];\n";
  os << "};\n\n";

  os << "struct output_v {\n";
  os << "  float4 position [[position]];\n";
  os << "  float4 v0 [[user(locn0)]]; float4 v1 [[user(locn1)]];\n";
  os << "  float4 v2 [[user(locn2)]]; float4 v3 [[user(locn3)]];\n";
  os << "  float4 v4 [[user(locn4)]]; float4 v5 [[user(locn5)]];\n";
  os << "  float4 v6 [[user(locn6)]]; float4 v7 [[user(locn7)]];\n";
  os << "  float4 v8 [[user(locn8)]]; float4 v9 [[user(locn9)]];\n";
  os << "  float4 v10 [[user(locn10)]]; float4 v11 [[user(locn11)]];\n";
  os << "  float4 v12 [[user(locn12)]]; float4 v13 [[user(locn13)]];\n";
  os << "  float4 v14 [[user(locn14)]]; float4 v15 [[user(locn15)]];\n";
  os << "};\n\n";

  os << "struct pixel_output_v {\n";
  for (uint32_t i = 0; i < 8; i++)
    os << "  " << mslIOTypeName(pixelOutputType(ctx.options, i))
       << " color" << i << " [[color(" << i << ")]];\n";
  os << "};\n\n";

  os << "struct vertex_input_v {\n";
  for (uint32_t i = 0; i < 16; i++)
    os << "  " << mslIOTypeName(vertexInputType(ctx.options, i)) << " a" << i
       << " [[attribute(" << i << ")]];\n";
  os << "};\n\n";

  if (ctx.shader.kind == DxilShaderKind::Compute) {
    os << "kernel void cs_main(\n";
    for (uint32_t i = 0; i < kMaxMSLBufferBindings; i++)
      os << "  device char* buf" << i << " [[buffer(" << i << ")]],\n";
    for (uint32_t i = 0; i < kMaxMSLTextureBindings; i++)
      os << "  texture2d<float, access::read_write> tex" << i
         << " [[texture(" << i << ")]],\n";
    for (uint32_t i = 0; i < kMaxMSLSamplerBindings; i++)
      os << "  sampler samp" << i << " [[sampler(" << i << ")]],\n";
    os << "  uint3 dtid [[thread_position_in_grid]],\n";
    os << "  uint3 gtid [[thread_position_in_threadgroup]],\n";
    os << "  uint3 ggid [[threadgroup_position_in_grid]],\n";
    os << "  uint3 gsz [[threads_per_threadgroup]]\n";
    os << ") {\n";
  } else if (ctx.shader.kind == DxilShaderKind::Vertex) {
    os << "vertex output_v vs_main(\n";
    os << "  vertex_input_v vin [[stage_in]],\n";
    os << "  uint vid [[vertex_id]],\n";
    for (uint32_t i = 0; i < kMaxMSLBufferBindings; i++)
      os << "  const device char* buf" << i << " [[buffer(" << i << ")]],\n";
    for (uint32_t i = 0; i < kMaxMSLTextureBindings; i++)
      os << "  texture2d<float, access::sample> tex" << i
         << " [[texture(" << i << ")]],\n";
    for (uint32_t i = 0; i < kMaxMSLSamplerBindings; i++) {
      os << "  sampler samp" << i << " [[sampler(" << i << ")]]";
      os << (i + 1 == kMaxMSLSamplerBindings ? "\n" : ",\n");
    }
    os << ") {\n";
    os << "  output_v out = {};\n";
  } else if (ctx.shader.kind == DxilShaderKind::Pixel) {
    os << "fragment pixel_output_v ps_main(\n";
    os << "  input_v in [[stage_in]],\n";
    for (uint32_t i = 0; i < kMaxMSLBufferBindings; i++)
      os << "  device char* buf" << i << " [[buffer(" << i << ")]],\n";
    for (uint32_t i = 0; i < kMaxMSLTextureBindings; i++)
      os << "  texture2d<float, access::sample> tex" << i
         << " [[texture(" << i << ")]],\n";
    for (uint32_t i = 0; i < kMaxMSLSamplerBindings; i++) {
      os << "  sampler samp" << i << " [[sampler(" << i << ")]]";
      os << (i + 1 == kMaxMSLSamplerBindings ? "\n" : ",\n");
    }
    os << ") {\n";
    os << "  pixel_output_v result = {};\n";
    os << "  result.color0 = "
       << mslIOZeroValue(pixelOutputType(ctx.options, 0)) << ";\n";
  } else {
    os << "kernel void unknown_main() {\n";
  }
}

std::string DXILToMSL::translateDXIntrinsic(EmitContext &ctx, uint32_t intrinsic_id,
                                              const std::vector<uint32_t> &args) {
  auto argTypeId = [&](size_t arg) -> uint32_t {
    if (arg >= args.size())
      return 0;
    uint32_t idx = args[arg];
    return idx < ctx.value_types.size() ? ctx.value_types[idx] : 0;
  };

  auto resolvedValueArg = [&](auto &&self, uint32_t idx, uint32_t depth) -> std::string {
    if (depth > 8)
      return idx < ctx.value_table.size() ? ctx.value_table[idx] : "0";
    if (idx < ctx.value_expr_table.size() && !ctx.value_expr_table[idx].empty()) {
      const auto &expr = ctx.value_expr_table[idx];
      uint32_t alias_idx = 0;
      if (parseSSAName(expr, alias_idx)) {
        if (alias_idx != idx)
          return self(self, alias_idx, depth + 1);
        if (ctx.emitted_values.find(idx) == ctx.emitted_values.end())
          return defaultValueForTypeId(
              idx < ctx.value_types.size() ? ctx.value_types[idx] : 0,
              ctx.mod);
      }
      if (!isFunctionLikeSymbol(expr))
        return expr;
    }
    if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty()) {
      const auto &value = ctx.value_table[idx];
      uint32_t alias_idx = 0;
      if (parseSSAName(value, alias_idx) && alias_idx != idx)
        return self(self, alias_idx, depth + 1);
      return value;
    }
    return "0";
  };

  auto valueArg = [&](size_t arg, const char *fallback) -> std::string {
    if (arg >= args.size())
      return fallback;
    uint32_t idx = args[arg];
    std::string value = resolvedValueArg(resolvedValueArg, idx, 0);
    if (!isFunctionLikeSymbol(value))
      return value;
    recordDiagnostic(ctx,
                     "DXIL intrinsic %u rejected function symbol operand: %s",
                     intrinsic_id, value.c_str());
    return fallback;
  };

  auto argVectorLaneCount = [&](size_t arg) -> uint8_t {
    if (arg >= args.size())
      return 0;
    uint32_t idx = args[arg];
    if (idx < ctx.value_vector_lanes.size() && ctx.value_vector_lanes[idx] > 0)
      return ctx.value_vector_lanes[idx];
    uint8_t lanes = vectorLaneCountForTypeId(argTypeId(arg), ctx.mod);
    if (lanes > 1)
      return lanes;
    return inferVectorLaneCountFromExpr(valueArg(arg, ""));
  };

  auto scalarValueArg = [&](size_t arg, const char *fallback) -> std::string {
    std::string value = valueArg(arg, fallback);
    if (isZeroLiteral(value))
      return value;
    if (arg < args.size()) {
      uint32_t idx = args[arg];
      uint32_t type_id = idx < ctx.value_types.size() ? ctx.value_types[idx] : 0;
      if (isPointerTypeId(type_id, ctx.mod) || isPointerLikeMSLExpr(value) ||
          ctx.pointer_slots.find(idx) != ctx.pointer_slots.end()) {
        recordDiagnostic(ctx,
                         "DXIL intrinsic %u pointer operand used as scalar: %s",
                         intrinsic_id, value.c_str());
        return fallback;
      }
    }
    return scalarizeMSLExpr(value, argVectorLaneCount(arg));
  };

  auto literalArg = [&](size_t arg, uint32_t fallback, const char *label) -> uint32_t {
    std::string text = valueArg(arg, "");
    uint32_t value = 0;
    if (parseUnsignedLiteral(text, value))
      return value;
    recordDiagnostic(ctx, "DXIL intrinsic %u: %s is not a literal: %s",
                     intrinsic_id, label, text.empty() ? "<missing>" : text.c_str());
    return fallback;
  };

  auto tryLiteralArg = [&](size_t arg, uint32_t &value) -> bool {
    if (arg >= args.size())
      return false;
    std::string text = valueArg(arg, "");
    return parseUnsignedLiteral(text, value);
  };

  switch (intrinsic_id) {
  case DXOP_CreateHandle: {
    if (args.size() < 4) return "0";
    uint32_t resource_class = literalArg(0, 0, "resource class");
    uint32_t range_id = literalArg(1, 0, "range id");
    uint32_t index = literalArg(2, 0, "resource index");
    bool non_uniform = literalArg(3, 0, "non-uniform index") != 0;
    (void)non_uniform;
    if (resource_class > 3) {
      // Some DXIL bitcode records arrive with the recovered dx.op/global value
      // in the first operand slot. Keep simple SM6 probes and fallback shaders
      // on the declared t#/s# binding instead of drifting to tex1/samp1.
      recordDiagnostic(ctx,
                       "DXIL CreateHandle repaired shifted resource class: %u",
                       resource_class);
      resource_class = resource_class == 9 ? 3 : 0;
      range_id = 0;
      index = 0;
    }
    ctx.next_binding++;
    std::string res_name = std::to_string(range_id);
    DXTRACE("DXIL CreateHandle: class=%u range=%u index=%u -> %s", resource_class, range_id, index, res_name.c_str());
    return res_name;
  }

  case DXOP_CreateHandleForLib: {
    auto handle = valueArg(0, "0");
    DXTRACE("DXIL CreateHandleForLib: %s", handle.c_str());
    return handle;
  }

  case DXOP_AnnotateHandle: {
    auto handle = valueArg(0, "0");
    DXTRACE("DXIL AnnotateHandle: %s", handle.c_str());
    return handle;
  }

  case DXOP_CreateHandleFromBinding: {
    auto binding = valueArg(0, "");
    auto binding_values = parseAggregateLiteral(binding);
    uint32_t lower_bound = 0;
    uint32_t resource_class = 0;
    if (binding_values.size() > 0)
      parseUnsignedLiteral(binding_values[0], lower_bound);
    if (binding_values.size() > 3)
      parseUnsignedLiteral(binding_values[3], resource_class);
    uint32_t index = args.size() >= 2
                         ? literalArg(1, 0, "binding resource index")
                         : 0;
    bool non_uniform = args.size() >= 3 &&
                       literalArg(2, 0, "binding non-uniform index") != 0;
    (void)non_uniform;
    uint32_t binding_index = lower_bound + index;
    std::string res_name = std::to_string(binding_index);
    DXTRACE("DXIL CreateHandleFromBinding: binding=%s lower=%u class=%u index=%u -> %s",
            binding.empty() ? "<missing>" : binding.c_str(), lower_bound,
            resource_class, index, res_name.c_str());
    return res_name;
  }

  case DXOP_CreateHandleFromHeap: {
    uint32_t heap_index = literalArg(0, 0, "heap index");
    bool sampler_heap = args.size() >= 2 &&
                        literalArg(1, 0, "sampler heap") != 0;
    bool non_uniform = args.size() >= 3 &&
                       literalArg(2, 0, "heap non-uniform index") != 0;
    (void)non_uniform;
    std::string res_name = std::to_string(heap_index);
    DXTRACE("DXIL CreateHandleFromHeap: heap_index=%u sampler=%d -> %s",
            heap_index, sampler_heap, res_name.c_str());
    return res_name;
  }

  case DXOP_ThreadId: {
    if (ctx.shader.kind != DxilShaderKind::Compute) {
      recordDiagnostic(ctx, "DXIL ThreadId used outside compute shader; lowered to 0");
      return "0";
    }
    ctx.uses_thread_id = true;
    if (!args.empty()) {
      uint32_t component = literalArg(0, 0, "thread id component");
      if (component == 0) return "(int)dtid.x";
      if (component == 1) return "(int)dtid.y";
      if (component == 2) return "(int)dtid.z";
    }
    return "(int)dtid.x";
  }

  case DXOP_GroupId: {
    if (ctx.shader.kind != DxilShaderKind::Compute) {
      recordDiagnostic(ctx, "DXIL GroupId used outside compute shader; lowered to 0");
      return "0";
    }
    ctx.uses_group_id = true;
    if (!args.empty()) {
      uint32_t component = literalArg(0, 0, "group id component");
      if (component == 0) return "(int)ggid.x";
      if (component == 1) return "(int)ggid.y";
      if (component == 2) return "(int)ggid.z";
    }
    return "(int)ggid.x";
  }

  case DXOP_ThreadIDInGroup: {
    if (ctx.shader.kind != DxilShaderKind::Compute) {
      recordDiagnostic(ctx, "DXIL ThreadIDInGroup used outside compute shader; lowered to 0");
      return "0";
    }
    ctx.uses_group_thread_id = true;
    if (!args.empty()) {
      uint32_t component = literalArg(0, 0, "group thread id component");
      if (component == 0) return "(int)gtid.x";
      if (component == 1) return "(int)gtid.y";
      if (component == 2) return "(int)gtid.z";
    }
    return "(int)gtid.x";
  }

  case DXOP_FlattenedThreadIDInGroup: {
    if (ctx.shader.kind != DxilShaderKind::Compute) {
      recordDiagnostic(ctx, "DXIL FlattenedThreadIDInGroup used outside compute shader; lowered to 0");
      return "0";
    }
    ctx.uses_group_thread_id = true;
    ctx.uses_group_size = true;
    return "(int)(gtid.x + gtid.y * gsz.x + gtid.z * gsz.x * gsz.y)";
  }

  case DXOP_CBufferLoad:
  case DXOP_CBufferLoadLegacy: {
    if (args.size() < 2) return "float4(0)";
    auto handle = resolveBindingName(valueArg(0, "cbuf0"), "buf");
    auto reg_idx = asIntegerIndexExpr(scalarValueArg(1, "0"));
    const char *addr = ctx.shader.kind == DxilShaderKind::Vertex
                           ? "const device"
                           : "device";
    return "(reinterpret_cast<" + std::string(addr) + " float4&>(" + handle +
           "[(" + reg_idx + ")*64]))";
  }

  case DXOP_BufferLoad: {
    if (args.size() < 3) return "float4(0)";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "buf");
    auto index = asIntegerIndexExpr(scalarValueArg(1, "0"));
    const char *addr = ctx.shader.kind == DxilShaderKind::Vertex
                           ? "const device"
                           : "device";
    return "(reinterpret_cast<" + std::string(addr) + " float4&>(" + handle +
           "[(" + index + ")*16]))";
  }

  case DXOP_RawBufferLoad:
  case DXOP_RawBufferVectorLoad:
  case DXOP_RawBufferLoadLegacy: {
    if (args.size() < 3) return "uint4(0)";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "buf");
    auto index = asIntegerIndexExpr(scalarValueArg(1, "0"));
    auto elem_offset = asIntegerIndexExpr(scalarValueArg(2, "0"));
    auto byte_offset = "((" + index + ")*4 + (" + elem_offset + "))";
    const char *addr = ctx.shader.kind == DxilShaderKind::Vertex
                           ? "const device"
                           : "device";
    return "(reinterpret_cast<" + std::string(addr) + " uint4&>(" + handle +
           "[" + byte_offset + "]))";
  }

  case DXOP_BufferStore:
  case DXOP_RawBufferStore:
  case DXOP_RawBufferStoreLegacy: {
    if (args.size() < 4) return "";
    auto handle = resolveBindingName(valueArg(0, "uav0"), "buf");
    auto index = asIntegerIndexExpr(scalarValueArg(1, "0"));
    auto elem_offset = asIntegerIndexExpr(scalarValueArg(2, "0"));
    std::string base_offset = "((" + index + ")*4 + (" + elem_offset + "))";
    std::ostringstream store;
    uint32_t value_count = std::min<uint32_t>(4, (uint32_t)args.size() - 3);
    for (uint32_t i = 0; i < value_count; i++) {
      auto value_text = scalarValueArg(3 + i, "0");
      uint32_t value_type_id =
          args[3 + i] < ctx.value_types.size() ? ctx.value_types[args[3 + i]] : 0;
      if (value_type_id < ctx.mod.types.size() &&
          ctx.mod.types[value_type_id].kind == LLVMType::Vector) {
        value_text = valueArg(3 + i, "0") + componentSuffix(i);
      }
      if (i)
        store << ";\n  ";
      store << "reinterpret_cast<device uint&>(" << handle << "[(" << base_offset
            << ") + " << (i * 4) << "]) = dxmt_uint(" << value_text
            << ")";
    }
    return store.str();
  }

  case DXOP_RawBufferVectorStore: {
    if (args.size() < 4) return "";
    auto handle = resolveBindingName(valueArg(0, "uav0"), "buf");
    auto index = asIntegerIndexExpr(scalarValueArg(1, "0"));
    auto elem_offset = asIntegerIndexExpr(scalarValueArg(2, "0"));
    auto value = valueArg(3, "uint4(0)");
    if (isZeroLiteral(value))
      value = "uint4(0)";
    std::string base_offset = "((" + index + ")*4 + (" + elem_offset + "))";
    std::ostringstream store;
    for (uint32_t i = 0; i < 4; i++) {
      if (i)
        store << ";\n  ";
      store << "reinterpret_cast<device uint&>(" << handle << "[(" << base_offset
            << ") + " << (i * 4) << "]) = dxmt_uint(" << value
            << componentSuffix(i) << ")";
    }
    return store.str();
  }

  case DXOP_TextureLoad: {
    if (args.size() < 3) return "float4(0)";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "tex");
    auto coord_x = scalarValueArg(2, "0");
    auto coord_y = scalarValueArg(3, "0");
    auto coord = "uint2(" + asIntegerIndexExpr(coord_x) + ", " +
                 asIntegerIndexExpr(coord_y) + ")";
    return handle + ".read(" + coord + ")";
  }

  case DXOP_TextureStore:
  case DXOP_TextureStoreSample: {
    if (args.size() < 6) return "";
    auto handle = resolveBindingName(valueArg(0, "uav0"), "tex");
    auto coord_x = scalarValueArg(1, "0");
    auto coord_y = scalarValueArg(2, "0");
    size_t value_base = intrinsic_id == DXOP_TextureStoreSample ? 5 : 4;
    auto value_x = scalarValueArg(value_base + 0, "0.0");
    auto value_y = scalarValueArg(value_base + 1, "0.0");
    auto value_z = scalarValueArg(value_base + 2, "0.0");
    auto value_w = scalarValueArg(value_base + 3, "0.0");
    if (intrinsic_id == DXOP_TextureStoreSample) {
      recordDiagnostic(ctx, "DXIL TextureStoreSample lowered without explicit sample index");
    }
    return handle + ".write(float4(" + value_x + ", " + value_y + ", " +
           value_z + ", " + value_w + "), uint2(" + asIntegerIndexExpr(coord_x) + ", " +
           asIntegerIndexExpr(coord_y) + "))";
  }

  case DXOP_TextureSample:
  case DXOP_TextureSampleBias:
  case DXOP_TextureSampleLevel:
  case DXOP_TextureSampleGrad: {
    if (args.size() < 4) return "float4(0)";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "tex");
    auto sampler = resolveBindingName(valueArg(1, "samp0"), "samp");
    auto coord_x = scalarValueArg(2, "0.0");
    auto coord_y = scalarValueArg(3, "0.0");
    auto coord = "float2(dxmt_float(" + coord_x + "), dxmt_float(" + coord_y + "))";
    if (ctx.shader.kind == DxilShaderKind::Compute) {
      if (intrinsic_id != DXOP_TextureSample)
        recordDiagnostic(ctx, "DXIL compute texture sample lowered through integer read fallback");
      return handle + ".read(uint2(" + asIntegerIndexExpr(coord_x) + ", " +
             asIntegerIndexExpr(coord_y) + "))";
    }
    if (intrinsic_id == DXOP_TextureSampleGrad) {
      recordDiagnostic(ctx, "DXIL SampleGrad lowered without explicit gradients");
    } else if (intrinsic_id == DXOP_TextureSampleLevel) {
      auto lod = args.size() > 4 ? scalarValueArg(4, "0.0") : "0.0";
      return handle + ".sample(" + sampler + ", " + coord + ", level(" + lod + "))";
    } else if (intrinsic_id == DXOP_TextureSampleBias) {
      recordDiagnostic(ctx, "DXIL SampleBias lowered without explicit bias");
    }
    return handle + ".sample(" + sampler + ", " + coord + ")";
  }

  case DXOP_TextureGather:
  case DXOP_TextureGatherCmp:
  case DXOP_TextureGatherRaw: {
    if (args.size() < 4) return "float4(0)";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "tex");
    auto sampler = resolveBindingName(valueArg(1, "samp0"), "samp");
    auto coord_x = scalarValueArg(2, "0.0");
    auto coord_y = scalarValueArg(3, "0.0");
    uint32_t channel = args.size() > 8 ? literalArg(8, 0, "gather channel") : 0;
    if (intrinsic_id == DXOP_TextureGatherCmp) {
      recordDiagnostic(ctx, "DXIL TextureGatherCmp lowered without explicit compare");
    } else if (intrinsic_id == DXOP_TextureGatherRaw) {
      recordDiagnostic(ctx, "DXIL TextureGatherRaw lowered through typed gather");
    }
    (void)sampler;
    return "float4(" + handle + ".read(uint2(" + asIntegerIndexExpr(coord_x) + ", " +
           asIntegerIndexExpr(coord_y) +
           "))." + componentName(channel) + ")";
  }

  case DXOP_TextureSampleCmp:
  case DXOP_TextureSampleCmpLevelZero:
  case DXOP_TextureSampleCmpLevel: {
    if (args.size() < 5) return "0.0";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "tex");
    auto sampler = resolveBindingName(valueArg(1, "samp0"), "samp");
    auto coord_x = scalarValueArg(2, "0.0");
    auto coord_y = scalarValueArg(3, "0.0");
    auto compare = valueArg(4, "0.0");
    (void)sampler;
    auto sample = handle + ".read(uint2(" + asIntegerIndexExpr(coord_x) + ", " +
                  asIntegerIndexExpr(coord_y) + ")).r";
    return "((" + sample + ") < (" + compare + ") ? 1.0 : 0.0)";
  }

  case DXOP_BufferUpdateCounter: {
    recordDiagnostic(ctx, "DXIL BufferUpdateCounter lowered to non-mutating counter fallback");
    return "0";
  }

  case DXOP_CheckAccessFullyMapped:
    return "true";

  case DXOP_GetDimensions: {
    if (args.empty()) return "uint4(0)";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "tex");
    return "uint4(" + handle + ".get_width(), " + handle +
           ".get_height(), 1, 1)";
  }

  case DXOP_DerivCoarseX:
  case DXOP_DerivFineX: {
    if (args.empty()) return "0.0";
    if (ctx.shader.kind != DxilShaderKind::Pixel) {
      recordDiagnostic(ctx, "DXIL derivative X used outside pixel shader; lowered to 0");
      return "0.0f";
    }
    return "dfdx(dxmt_float(" + scalarValueArg(0, "0.0") + "))";
  }

  case DXOP_DerivCoarseY:
  case DXOP_DerivFineY: {
    if (args.empty()) return "0.0";
    if (ctx.shader.kind != DxilShaderKind::Pixel) {
      recordDiagnostic(ctx, "DXIL derivative Y used outside pixel shader; lowered to 0");
      return "0.0f";
    }
    return "dfdy(dxmt_float(" + scalarValueArg(0, "0.0") + "))";
  }

  case DXOP_CalcLOD: {
    if (args.size() < 4) return "0.0";
    auto handle = resolveBindingName(valueArg(0, "srv0"), "tex");
    auto sampler = resolveBindingName(valueArg(1, "samp0"), "samp");
    auto coord_x = scalarValueArg(2, "0.0");
    auto coord_y = scalarValueArg(3, "0.0");
    return handle + ".calculate_unclamped_lod(" + sampler + ", float2(dxmt_float(" +
           coord_x + "), dxmt_float(" + coord_y + ")))";
  }

  case DXOP_AtomicBinOp: {
    if (args.size() < 4) return "0";
    auto handle = resolveBindingName(valueArg(1, "uav0"), "buf");
    auto offset = asIntegerIndexExpr(scalarValueArg(2, "0"));
    auto value = scalarValueArg(args.size() > 6 ? 6 : args.size() - 1, "0");
    return "atomic_fetch_add_explicit(reinterpret_cast<device atomic_uint*>(" +
           handle + " + (" + offset + ")), dxmt_uint(" + value +
           "), memory_order_relaxed)";
  }

  case DXOP_AtomicCompareExchange: {
    if (args.size() < 2) return "0";
    auto handle = resolveBindingName(valueArg(0, "uav0"), "buf");
    auto offset = asIntegerIndexExpr(scalarValueArg(1, "0"));
    recordDiagnostic(ctx, "DXIL AtomicCompareExchange lowered to atomic load fallback");
    return "atomic_load_explicit(reinterpret_cast<device atomic_uint*>(" +
           handle + " + (" + offset + ")), memory_order_relaxed)";
  }

  case DXOP_Texture2DMSGetSamplePosition:
  case DXOP_RenderTargetGetSamplePosition:
  case DXOP_Texture2DMSGetSamplePositionLegacy:
  case DXOP_RenderTargetGetSamplePositionLegacy: {
    if (!args.empty()) {
      uint32_t component = literalArg(args.size() - 1, 0, "sample position component");
      return component == 0 ? "0.5" : "0.5";
    }
    return "0.5";
  }

  case DXOP_RenderTargetGetSampleCount:
    return "1";

  case DXOP_NumPrimitives:
  case DXOP_NumOutputVertices:
    return "0";

  case DXOP_Barrier: {
    return "threadgroup_barrier(mem_flags::mem_threadgroup)";
  }

  case DXOP_Unary: {
    if (args.size() < 2) return "0";
    uint32_t op = literalArg(0, 0xFFFFFFFFu, "unary opcode");
    auto x = scalarValueArg(1, "0.0");
    switch (op) {
    case DXILOP_FAbs: return "abs((float)(" + x + "))";
    case DXILOP_Saturate: return "clamp((float)(" + x + "), 0.0f, 1.0f)";
    case DXILOP_IsNaN: return "isnan((float)(" + x + "))";
    case DXILOP_IsInf: return "isinf((float)(" + x + "))";
    case DXILOP_IsFinite: return "isfinite((float)(" + x + "))";
    case DXILOP_Cos: return "cos((float)(" + x + "))";
    case DXILOP_Sin: return "sin((float)(" + x + "))";
    case DXILOP_Tan: return "tan((float)(" + x + "))";
    case DXILOP_Acos: return "acos((float)(" + x + "))";
    case DXILOP_Asin: return "asin((float)(" + x + "))";
    case DXILOP_Atan: return "atan((float)(" + x + "))";
    case DXILOP_Exp: return "exp2((float)(" + x + "))";
    case DXILOP_Frc: return "fract((float)(" + x + "))";
    case DXILOP_Log: return "log2((float)(" + x + "))";
    case DXILOP_Sqrt: return "sqrt((float)(" + x + "))";
    case DXILOP_Rsqrt: return "rsqrt((float)(" + x + "))";
    case DXILOP_Round_ne: return "rint((float)(" + x + "))";
    case DXILOP_Round_ni: return "floor((float)(" + x + "))";
    case DXILOP_Round_pi: return "ceil((float)(" + x + "))";
    case DXILOP_Round_z: return "trunc((float)(" + x + "))";
    default:
      ctx.unsupported_intrinsics++;
      recordDiagnostic(ctx, "DXIL unknown unary opcode: %u", op);
      return x;
    }
  }

  case DXOP_UnaryBits: {
    if (args.size() < 2)
      return "0";
    int64_t op = 0;
    std::string op_text = valueArg(0, "0");
    if (!parseSignedLiteral(op_text, op)) {
      ctx.unsupported_intrinsics++;
      recordDiagnostic(ctx, "DXIL unaryBits opcode is not a literal: %s",
                       op_text.c_str());
      return valueArg(1, "0");
    }
    auto x = valueArg(1, "0");
    switch (op) {
    case -2:
      return "as_type<int>(as_type<uint>(" + x + "))";
    default:
      ctx.unsupported_intrinsics++;
      recordDiagnostic(ctx, "DXIL unknown unaryBits opcode: %" PRId64, op);
      return x;
    }
  }

  case DXOP_LegacyF16ToF32: {
    auto x = scalarValueArg(0, "0");
    return "(float)(half)as_type<half>((ushort)(dxmt_uint(" + x + ")))";
  }

  case DXOP_LegacyF32ToF16: {
    auto x = scalarValueArg(0, "0.0");
    return "(uint)as_type<ushort>(half(dxmt_float(" + x + ")))";
  }

  case DXOP_Binary: {
    if (args.size() < 3) return "0";
    uint32_t op = literalArg(0, 0xFFFFFFFFu, "binary opcode");
    auto a = scalarValueArg(1, "0");
    auto b = scalarValueArg(2, "0");
    switch (op) {
    case DXILOP_FMax: return "max((float)(" + a + "), (float)(" + b + "))";
    case DXILOP_FMin: return "min((float)(" + a + "), (float)(" + b + "))";
    case DXILOP_IMax:
      return "max((int)(dxmt_uint(" + a + ")), (int)(dxmt_uint(" + b + ")))";
    case DXILOP_IMin:
      return "min((int)(dxmt_uint(" + a + ")), (int)(dxmt_uint(" + b + ")))";
    case DXILOP_UMax:
      return "max((uint)(dxmt_uint(" + a + ")), (uint)(dxmt_uint(" + b + ")))";
    case DXILOP_UMin:
      return "min((uint)(dxmt_uint(" + a + ")), (uint)(dxmt_uint(" + b + ")))";
    default:
      ctx.unsupported_intrinsics++;
      recordDiagnostic(ctx, "DXIL unknown binary opcode: %u", op);
      return a;
    }
  }

  case DXOP_Tertiary: {
    if (args.size() < 4) return "0";
    uint32_t op = literalArg(0, 0xFFFFFFFFu, "tertiary opcode");
    auto a = scalarValueArg(1, "0");
    auto b = scalarValueArg(2, "0");
    auto c = scalarValueArg(3, "0");
    switch (op) {
    case DXILOP_FMad:
    case DXILOP_Fma:
      return "(dxmt_float(" + a + ") * dxmt_float(" + b +
             ") + dxmt_float(" + c + "))";
    case DXILOP_IMad:
    case DXILOP_UMad: return "((" + a + ") * (" + b + ") + (" + c + "))";
    default:
      ctx.unsupported_intrinsics++;
      recordDiagnostic(ctx, "DXIL unknown tertiary opcode: %u", op);
      return a;
    }
  }

  case DXOP_Dot2: {
    if (args.size() < 4) return "0.0";
    auto ax = valueArg(0, "0.0");
    auto ay = valueArg(1, "0.0");
    auto bx = valueArg(2, "0.0");
    auto by = valueArg(3, "0.0");
    return "((" + ax + ")*(" + bx + ") + (" + ay + ")*(" + by + "))";
  }

  case DXOP_Dot3: {
    if (args.size() < 6) return "0.0";
    auto ax = valueArg(0, "0.0");
    auto ay = valueArg(1, "0.0");
    auto az = valueArg(2, "0.0");
    auto bx = valueArg(3, "0.0");
    auto by = valueArg(4, "0.0");
    auto bz = valueArg(5, "0.0");
    return "((" + ax + ")*(" + bx + ") + (" + ay + ")*(" + by + ") + (" + az + ")*(" + bz + "))";
  }

  case DXOP_Dot4: {
    if (args.size() < 8) return "0.0";
    auto ax = valueArg(0, "0.0");
    auto ay = valueArg(1, "0.0");
    auto az = valueArg(2, "0.0");
    auto aw = valueArg(3, "0.0");
    auto bx = valueArg(4, "0.0");
    auto by = valueArg(5, "0.0");
    auto bz = valueArg(6, "0.0");
    auto bw = valueArg(7, "0.0");
    return "((" + ax + ")*(" + bx + ") + (" + ay + ")*(" + by + ") + (" + az + ")*(" + bz + ") + (" + aw + ")*(" + bw + "))";
  }

  case DXOP_LoadInput: {
    if (args.size() < 3) return "0.0";
    uint32_t input_id = UINT32_MAX;
    uint32_t component = 0;
    std::string row_expr = "0";
    size_t base = 0;
    while (base < args.size() && startsWith(valueArg(base, ""), "dx.op."))
      base++;
    uint32_t leading = 0;
    if (tryLiteralArg(base, leading) && leading == DXOP_LoadInput &&
        base + 3 < args.size())
      base++;

    bool decoded = false;
    if (base + 2 < args.size()) {
      decoded = tryLiteralArg(base, input_id) &&
                tryLiteralArg(base + 2, component);
      row_expr = valueArg(base + 1, "0");
    }

    if (!decoded) {
      recordDiagnostic(ctx,
                       "DXIL LoadInput failed exact operand decode: argc=%zu base=%zu a0=%s a1=%s a2=%s",
                       args.size(), base, valueArg(0, "<missing>").c_str(),
                       valueArg(1, "<missing>").c_str(),
                       valueArg(2, "<missing>").c_str());
      return "0.0";
    }

    if (component > 3 || input_id >= 32) {
      const uint32_t seq = ctx.shader.kind == DxilShaderKind::Vertex
                               ? ctx.vertex_load_input_counter
                               : ctx.pixel_load_input_counter;
      uint32_t fallback_id = 0;
      uint32_t fallback_component = 0;
      bool fallback_decoded = false;
      if (ctx.shader.kind == DxilShaderKind::Vertex) {
        fallback_decoded = decodeVertexLoadInputSequence(
            ctx.options, seq, fallback_id, fallback_component);
      } else if (ctx.shader.kind == DxilShaderKind::Pixel) {
        fallback_decoded = decodePixelLoadInputSequence(
            ctx.options, seq, fallback_id, fallback_component);
      }
      if (!fallback_decoded) {
        fallback_id = std::min<uint32_t>(31u, seq / 4u);
        fallback_component = seq & 3u;
      }
      recordDiagnostic(
          ctx,
          "DXIL LoadInput signature fallback: raw_input_sig=%u raw_row=%s raw_component=%u seq=%u input_sig=%u component=%u",
          input_id, row_expr.c_str(), component, seq, fallback_id,
          fallback_component);
      input_id = fallback_id;
      component = fallback_component;
      row_expr = "0";
    }

    if (ctx.shader.kind == DxilShaderKind::Vertex)
      ctx.vertex_load_input_counter++;
    else if (ctx.shader.kind == DxilShaderKind::Pixel)
      ctx.pixel_load_input_counter++;

    uint32_t literal_row = 0;
    if (!row_expr.empty() && !parseUnsignedLiteral(row_expr, literal_row)) {
      recordDiagnostic(ctx,
                       "DXIL LoadInput dynamic row ignored: shader_kind=%u input_sig=%u row=%s component=%u",
                       (uint32_t)ctx.shader.kind, input_id, row_expr.c_str(),
                       component);
    }

    if (ctx.shader.kind == DxilShaderKind::Pixel) {
      return pixelInputField(ctx.options, "in", input_id) +
             componentSuffix(component);
    }
    if (ctx.shader.kind == DxilShaderKind::Vertex) {
      const uint32_t input_register =
          vertexInputRegisterForSignature(ctx.options, input_id);
      return readIOComponent(vertexInputType(ctx.options, input_register),
                             vertexInputField("vin", input_register), component);
    }
    recordDiagnostic(ctx, "DXIL LoadInput fallback: shader_kind=%u input_id=%u component=%u",
                     (uint32_t)ctx.shader.kind, input_id, component);
    return "0.0";
  }

  case DXOP_StoreOutput: {
    if (args.size() < 4) return "";
    uint32_t output_id = UINT32_MAX;
    uint32_t component = 0;
    size_t value_arg = 3;
    std::string row_expr = "0";
    size_t base = 0;
    while (base < args.size() && startsWith(valueArg(base, ""), "dx.op."))
      base++;
    uint32_t leading = 0;
    if (tryLiteralArg(base, leading) && leading == DXOP_StoreOutput &&
        base + 4 < args.size())
      base++;

    bool decoded = false;
    if (base + 3 < args.size()) {
      decoded = tryLiteralArg(base, output_id) &&
                tryLiteralArg(base + 2, component);
      row_expr = valueArg(base + 1, "0");
      value_arg = base + 3;
    }

    if (!decoded) {
      recordDiagnostic(ctx,
                       "DXIL StoreOutput failed exact operand decode: argc=%zu base=%zu a0=%s a1=%s a2=%s",
                       args.size(), base, valueArg(0, "<missing>").c_str(),
                       valueArg(1, "<missing>").c_str(),
                       valueArg(2, "<missing>").c_str());
      return "";
    }

    if (component > 3 || output_id >= 32) {
      const uint32_t seq = ctx.shader.kind == DxilShaderKind::Vertex
                               ? ctx.vertex_store_output_counter
                               : ctx.pixel_store_output_counter;
      uint32_t fallback_id = 0;
      uint32_t fallback_component = 0;
      bool fallback_decoded = false;
      if (ctx.shader.kind == DxilShaderKind::Vertex) {
        fallback_decoded = decodeVertexStoreOutputSequence(
            ctx.options, seq, fallback_id, fallback_component);
      } else if (ctx.shader.kind == DxilShaderKind::Pixel) {
        fallback_decoded = decodePixelStoreOutputSequence(
            ctx.options, seq, fallback_id, fallback_component);
      }
      if (!fallback_decoded) {
        fallback_id = std::min<uint32_t>(31u, seq / 4u);
        fallback_component = seq & 3u;
      }
      recordDiagnostic(
          ctx,
          "DXIL StoreOutput signature fallback: raw_output_sig=%u raw_row=%s raw_component=%u seq=%u output_sig=%u component=%u",
          output_id, row_expr.c_str(), component, seq, fallback_id,
          fallback_component);
      output_id = fallback_id;
      component = fallback_component;
      row_expr = "0";
    }

    uint32_t literal_row = 0;
    if (!row_expr.empty() && !parseUnsignedLiteral(row_expr, literal_row)) {
      recordDiagnostic(ctx,
                       "DXIL StoreOutput dynamic row ignored: shader_kind=%u output_sig=%u row=%s component=%u",
                       (uint32_t)ctx.shader.kind, output_id, row_expr.c_str(),
                       component);
    }

    auto val = scalarValueArg(value_arg, "0");

    if (ctx.shader.kind == DxilShaderKind::Vertex) {
      ctx.vertex_store_output_counter++;
      return vertexOutputField(ctx.options, "out", output_id) +
             componentSuffix(component) + " = " + val;
    }
    if (ctx.shader.kind == DxilShaderKind::Pixel) {
      ctx.pixel_store_output_counter++;
      uint32_t target = std::min<uint32_t>(
          7u, pixelOutputTargetForSignature(ctx.options, output_id));
      const auto out_type = pixelOutputType(ctx.options, target);
      return writeIOComponent(out_type,
                              std::string("result.color") +
                                  std::to_string(target),
                              component) +
             " = " + val;
    }
    recordDiagnostic(ctx, "DXIL StoreOutput fallback: shader_kind=%u output_id=%u component=%u",
                     (uint32_t)ctx.shader.kind, output_id, component);
    return "";
  }

  default:
    ctx.unsupported_intrinsics++;
    recordDiagnostic(ctx, "DXIL unknown intrinsic: %u", intrinsic_id);
    break;
  }

  return "0 /* unknown dx intrinsic " + std::to_string(intrinsic_id) + " */";
}

void DXILToMSL::emitInstruction(EmitContext &ctx, const LLVMInstruction &inst, uint32_t &value_counter) {
  auto &os = ctx.os;
  uint32_t result_slot = inst.result_id ? inst.result_id : value_counter;
  value_counter = result_slot;
  std::string result = emitValue(result_slot);

  auto rawValue = [&](uint32_t idx) -> std::string {
    if (idx < ctx.value_table.size() && !ctx.value_table[idx].empty()) {
      const auto &value = ctx.value_table[idx];
      uint32_t alias_idx = 0;
      if (parseSSAName(value, alias_idx) && alias_idx == idx &&
          ctx.emitted_values.find(idx) == ctx.emitted_values.end()) {
        recordDiagnostic(ctx, "DXIL unresolved SSA alias: v%u", idx);
        uint32_t type_id = idx < ctx.value_types.size() ? ctx.value_types[idx] : 0;
        return defaultValueForTypeId(type_id, ctx.mod);
      }
      return value;
    }
    recordDiagnostic(ctx, "DXIL missing SSA value: v%u", idx);
    return "0";
  };

  auto getValue = [&](uint32_t idx) -> std::string {
    std::string value = rawValue(idx);
    if (isFunctionLikeSymbol(value)) {
      recordDiagnostic(ctx, "DXIL function symbol used as SSA value: %s",
                       value.c_str());
      return "0";
    }
    return value;
  };

  auto valueTypeId = [&](uint32_t idx) -> uint32_t {
    return idx < ctx.value_types.size() ? ctx.value_types[idx] : 0;
  };

  auto resolvedExpr = [&](auto &&self, uint32_t idx, uint32_t depth) -> std::string {
    if (depth > 8)
      return getValue(idx);
    if (idx < ctx.value_expr_table.size() && !ctx.value_expr_table[idx].empty()) {
      const auto &expr = ctx.value_expr_table[idx];
      uint32_t alias_idx = 0;
      if (parseSSAName(expr, alias_idx)) {
        if (alias_idx != idx)
          return self(self, alias_idx, depth + 1);
        if (ctx.emitted_values.find(idx) == ctx.emitted_values.end())
          return defaultValueForTypeId(
              idx < ctx.value_types.size() ? ctx.value_types[idx] : 0,
              ctx.mod);
      }
      if (isFunctionLikeSymbol(expr))
        return getValue(idx);
      return expr;
    }
    return getValue(idx);
  };

  auto valueVectorLaneCount = [&](uint32_t idx) -> uint8_t {
    if (idx < ctx.value_vector_lanes.size() && ctx.value_vector_lanes[idx] > 0)
      return ctx.value_vector_lanes[idx];
    return vectorLaneCountForTypeId(valueTypeId(idx), ctx.mod);
  };

  auto resolvedVectorLaneCount = [&](auto &&self, uint32_t idx, uint32_t depth) -> uint8_t {
    if (depth > 8)
      return valueVectorLaneCount(idx);
    uint8_t lanes = valueVectorLaneCount(idx);
    if (lanes > 1)
      return lanes;
    if (idx < ctx.value_expr_table.size() && !ctx.value_expr_table[idx].empty()) {
      uint32_t alias_idx = 0;
      if (parseSSAName(ctx.value_expr_table[idx], alias_idx) && alias_idx != idx)
        return self(self, alias_idx, depth + 1);
    }
    return lanes;
  };

  auto scalarValue = [&](uint32_t idx) -> std::string {
    std::string resolved = resolvedExpr(resolvedExpr, idx, 0);
    if (isZeroLiteral(resolved))
      return resolved;
    uint8_t lanes = resolvedVectorLaneCount(resolvedVectorLaneCount, idx, 0);
    if (lanes <= 1)
      lanes = inferVectorLaneCountFromExpr(resolved);
    return scalarizeMSLExpr(resolved, lanes);
  };

  auto uintValue = [&](uint32_t idx) -> std::string {
    std::string resolved = resolvedExpr(resolvedExpr, idx, 0);
    if (isFunctionLikeSymbol(resolved)) {
      recordDiagnostic(ctx, "DXIL function symbol used as integer SSA value: %s",
                       resolved.c_str());
      resolved = "0";
    }
    return "dxmt_uint(" + resolved + ")";
  };

  auto intValue = [&](uint32_t idx) -> std::string {
    std::string resolved = resolvedExpr(resolvedExpr, idx, 0);
    if (isFunctionLikeSymbol(resolved)) {
      recordDiagnostic(ctx, "DXIL function symbol used as integer SSA value: %s",
                       resolved.c_str());
      resolved = "0";
    }
    return "(int)(dxmt_uint(" + resolved + "))";
  };

  auto boolValue = [&](uint32_t idx) -> std::string {
    std::string resolved = resolvedExpr(resolvedExpr, idx, 0);
    if (isFunctionLikeSymbol(resolved)) {
      recordDiagnostic(ctx, "DXIL function symbol used as bool SSA value: %s",
                       resolved.c_str());
      resolved = "0";
    }
    return "dxmt_bool(" + resolved + ")";
  };

  auto ensureValueTable = [&](uint32_t needed) {
    if (ctx.value_table.size() <= needed) {
      ctx.value_table.resize(needed + 1);
      ctx.value_expr_table.resize(needed + 1);
      ctx.value_types.resize(needed + 1, 0);
      ctx.value_vector_lanes.resize(needed + 1, 0);
    }
  };

  auto publishResult = [&](uint8_t vector_lanes_override = UINT8_MAX,
                           std::string expr_override = std::string()) {
    ensureValueTable(result_slot);
    ctx.value_table[result_slot] = result;
    ctx.emitted_values.insert(result_slot);
    ctx.value_expr_table[result_slot] = std::move(expr_override);
    ctx.value_types[result_slot] = inst.type_id;
    ctx.value_vector_lanes[result_slot] =
        vector_lanes_override != UINT8_MAX
            ? (vector_lanes_override ? vector_lanes_override : 1)
            : vectorLaneCountForTypeId(inst.type_id, ctx.mod);
    value_counter = std::max(value_counter + 1, result_slot + 1);
  };

  auto usesPointerOperand = [&]() -> bool {
    for (auto operand : inst.operands) {
      if (ctx.pointer_slots.find(operand) != ctx.pointer_slots.end())
        return true;
      if (isPointerTypeId(valueTypeId(operand), ctx.mod))
        return true;
      std::string resolved = resolvedExpr(resolvedExpr, operand, 0);
      if (isPointerLikeMSLExpr(resolved))
        return true;
      if (operand < ctx.value_table.size()) {
        const auto &value = ctx.value_table[operand];
        if (startsWith(value, "buf") || value.find("_storage") != std::string::npos)
          return true;
      }
    }
    return false;
  };

  auto maxOperandVectorLaneCount = [&]() -> uint8_t {
    uint8_t lanes = 0;
    for (auto operand : inst.operands)
      lanes = std::max(lanes, valueVectorLaneCount(operand));
    return lanes;
  };

  auto operandValueForResultType = [&](uint32_t operand, uint32_t result_type_id) -> std::string {
    if (!isPointerTypeId(result_type_id, ctx.mod) &&
        vectorLaneCountForTypeId(result_type_id, ctx.mod) == 0) {
      uint8_t lanes = resolvedVectorLaneCount(resolvedVectorLaneCount, operand, 0);
      if (lanes <= 1)
        lanes = inferVectorLaneCountFromExpr(resolvedExpr(resolvedExpr, operand, 0));
      if (lanes > 1)
        return scalarValue(operand);
    }
    return getValue(operand);
  };

  auto floatOperandForResultType = [&](uint32_t operand, uint32_t result_type_id) -> std::string {
    uint8_t result_lanes = vectorLaneCountForTypeId(result_type_id, ctx.mod);
    std::string value = resolvedExpr(resolvedExpr, operand, 0);
    if (ctx.pointer_slots.find(operand) != ctx.pointer_slots.end() ||
        isPointerTypeId(valueTypeId(operand), ctx.mod) ||
        isPointerLikeMSLExpr(value)) {
      recordDiagnostic(ctx, "DXIL pointer operand used as float SSA value: v%u",
                       operand);
      return defaultValueForTypeId(result_type_id, ctx.mod);
    }
    if (result_lanes > 1) {
      return std::string("float") + std::to_string(result_lanes) + "(" +
             "dxmt_float(" + value + "))";
    }
    return "dxmt_float(" + scalarValue(operand) + ")";
  };

  auto numericOperandForResultType = [&](uint32_t operand, uint32_t result_type_id) -> std::string {
    if (!isIntegerLikeTypeId(result_type_id, ctx.mod) &&
        !isPointerTypeId(result_type_id, ctx.mod))
      return floatOperandForResultType(operand, result_type_id);
    uint8_t result_lanes = vectorLaneCountForTypeId(result_type_id, ctx.mod);
    if (result_lanes == 0)
      return scalarValue(operand);
    std::string value = resolvedExpr(resolvedExpr, operand, 0);
    return std::string("uint") + std::to_string(result_lanes) + "(dxmt_uint(" +
           value + "))";
  };

  auto publishPointerResult = [&]() {
    publishResult();
    ctx.pointer_slots.insert(result_slot);
  };

  switch (inst.opcode) {
  case LLVMInstruction::Ret:
    if (ctx.shader.kind == DxilShaderKind::Vertex) {
      os << "  return out;\n";
    } else if (ctx.shader.kind == DxilShaderKind::Pixel) {
      if (!inst.operands.empty()) {
        std::string ret = getValue(inst.operands[0]);
        uint8_t lanes =
            resolvedVectorLaneCount(resolvedVectorLaneCount, inst.operands[0], 0);
        if (lanes <= 1)
          lanes = inferVectorLaneCountFromExpr(ret);
        if (lanes >= 4) {
          os << "  result.color0 = float4(" << ret << ");\n";
          os << "  return result;\n";
        } else if (lanes == 3) {
          os << "  result.color0 = float4(" << ret << ", 1.0);\n";
          os << "  return result;\n";
        } else if (lanes == 2) {
          os << "  result.color0 = float4(" << ret << ", 0.0, 1.0);\n";
          os << "  return result;\n";
        } else {
          os << "  result.color0.x = " << ret << ";\n";
          os << "  return result;\n";
        }
        break;
      }
      os << "  return result;\n";
    } else {
      os << "  return;\n";
    }
    break;

  case LLVMInstruction::Call: {
    if (inst.operands.empty())
      break;
    uint32_t callee = inst.operands[0];
    uint32_t function_type_id = inst.operands.size() > 1 ? inst.operands[1] : 0;

    std::vector<uint32_t> call_args;
    for (size_t i = 2; i < inst.operands.size(); i++)
      call_args.push_back(inst.operands[i]);

    uint32_t intrinsic_id = 0;
    bool has_intrinsic_literal = false;
    if (call_args.size() > 0) {
      std::string id_str = rawValue(call_args[0]);
      has_intrinsic_literal = parseUnsignedLiteral(id_str, intrinsic_id);
    }

    std::string callee_name =
        callee < ctx.value_table.size() ? rawValue(callee) : "";
    std::string declared_callee_name = findFunctionNameForValue(ctx.mod, callee);
    if ((callee_name.empty() || callee_name == "0") &&
        !declared_callee_name.empty())
      callee_name = declared_callee_name;
    uint32_t callee_type_id = findFunctionTypeForValue(ctx.mod, callee);
    const bool callee_is_dxop = startsWith(callee_name, "dx.op");
    const bool callee_is_declared_non_dx_function =
        !declared_callee_name.empty() && !startsWith(declared_callee_name, "dx.op");
    const bool callee_needs_dxop_recovery =
        !callee_is_declared_non_dx_function &&
        (callee_name.empty() || callee_name == "0" || callee_is_dxop);
    std::string type_callee_name =
        callee_needs_dxop_recovery
            ? findDeclaredDXOpNameForType(ctx.mod, function_type_id)
            : "";
    if (type_callee_name.empty() &&
        (!has_intrinsic_literal || !isKnownDXIntrinsic(intrinsic_id))) {
      std::string first_arg_text = call_args.empty() ? "" : rawValue(call_args[0]);
      type_callee_name = inferDXIntrinsicNameFromCallShape(
          ctx.mod, function_type_id, inst.type_id, call_args, first_arg_text);
    }
    if (!type_callee_name.empty() && has_intrinsic_literal &&
        isKnownDXIntrinsic(intrinsic_id)) {
      uint32_t type_intrinsic_id = inferDXIntrinsicIdFromName(type_callee_name);
      if (type_intrinsic_id && type_intrinsic_id != intrinsic_id)
        type_callee_name.clear();
    }
    bool callee_matches_type =
        !type_callee_name.empty() && callee_type_id == function_type_id &&
        callee_name == type_callee_name;
    std::string effective_callee_name = callee_name;
    if (!type_callee_name.empty() &&
        (!startsWith(callee_name, "dx.op") || !callee_matches_type)) {
      effective_callee_name = type_callee_name;
    }
    bool named_dxop = startsWith(effective_callee_name, "dx.op");
    std::string dxop_arg_symbol;
    if (!named_dxop) {
      for (size_t i = 0; i < call_args.size(); i++) {
        dxop_arg_symbol = rawValue(call_args[i]);
        if (startsWith(dxop_arg_symbol, "dx.op.")) {
          effective_callee_name = dxop_arg_symbol;
          named_dxop = true;
          break;
        }
      }
    }
    uint32_t inferred_intrinsic_id =
        inferDXIntrinsicIdFromName(effective_callee_name);
    if (named_dxop && inferred_intrinsic_id &&
        !has_intrinsic_literal) {
      intrinsic_id = inferred_intrinsic_id;
      has_intrinsic_literal = true;
    } else if (named_dxop && inferred_intrinsic_id && has_intrinsic_literal &&
               intrinsic_id != inferred_intrinsic_id &&
               isKnownDXIntrinsic(intrinsic_id)) {
      recordDiagnostic(ctx,
                       "DXIL intrinsic name/literal mismatch: name=%u literal=%u; trusting literal",
                       inferred_intrinsic_id, intrinsic_id);
    } else if (named_dxop && inferred_intrinsic_id && has_intrinsic_literal &&
               intrinsic_id != inferred_intrinsic_id &&
               !isKnownDXIntrinsic(intrinsic_id)) {
      recordDiagnostic(ctx,
                       "DXIL intrinsic name/literal mismatch: name=%u literal=%u; trusting name",
                       inferred_intrinsic_id, intrinsic_id);
      intrinsic_id = inferred_intrinsic_id;
    }
    if (named_dxop && !has_intrinsic_literal) {
      ctx.unsupported_intrinsics++;
      std::string id_str = call_args.empty() ? "<missing>" : rawValue(call_args[0]);
      recordDiagnostic(ctx, "DXIL intrinsic id is not a literal: %s", id_str.c_str());
      os << "  // dx.op call without literal intrinsic id\n";
      publishResult();
    } else if (has_intrinsic_literal && (named_dxop || isKnownDXIntrinsic(intrinsic_id))) {
      std::vector<uint32_t> remaining_args;
      bool skipped_intrinsic_literal = false;
      bool skipped_symbol_operand = false;
      bool saw_non_symbol_operand = false;
      for (size_t i = 0; i < call_args.size(); i++) {
        std::string arg_text = rawValue(call_args[i]);
        if (startsWith(arg_text, "dx.op.")) {
          skipped_symbol_operand = true;
          continue;
        }
        uint32_t literal = 0;
        if (!saw_non_symbol_operand && !skipped_intrinsic_literal &&
            parseUnsignedLiteral(arg_text, literal) && literal == intrinsic_id) {
          skipped_intrinsic_literal = true;
          saw_non_symbol_operand = true;
          continue;
        }
        saw_non_symbol_operand = true;
        remaining_args.push_back(call_args[i]);
      }
      if (skipped_symbol_operand) {
        recordDiagnostic(ctx, "DXIL intrinsic %u skipped recovered function symbol operand",
                         intrinsic_id);
      }

      std::string translated = translateDXIntrinsic(ctx, intrinsic_id, remaining_args);

      if (inst.type_id == 0) {
        if (!translated.empty())
          os << "  " << translated << ";\n";
      } else if (isSideEffectOnlyIntrinsic(intrinsic_id)) {
        if (!translated.empty())
          os << "  " << translated << ";\n";
        ensureValueTable(result_slot);
        ctx.value_table[result_slot] = defaultValueForTypeId(inst.type_id, ctx.mod);
        ctx.value_types[result_slot] = inst.type_id;
      } else if (isHandleIntrinsic(intrinsic_id)) {
        ensureValueTable(result_slot);
        ctx.value_table[result_slot] = translated.empty() ? "0" : translated;
        ctx.value_types[result_slot] = inst.type_id;
      } else if (translated.find('=') == std::string::npos) {
        ensureValueTable(result_slot);
        if (!translated.empty() && translated[0] != ' ') {
          os << "  auto " << result << " = " << translated << ";\n";
          ctx.value_table[result_slot] = result;
          ctx.emitted_values.insert(result_slot);
          ctx.value_expr_table[result_slot] = translated;
          ctx.value_types[result_slot] = inst.type_id;
          ctx.value_vector_lanes[result_slot] =
              intrinsicResultVectorLaneCount(intrinsic_id);
        } else if (!translated.empty()) {
          os << "  " << translated << ";\n";
        }
      } else {
        os << "  " << translated << ";\n";
        ensureValueTable(result_slot);
        os << "  auto " << result << " = "
           << defaultValueForTypeId(inst.type_id, ctx.mod)
           << "; // fallback result after side-effect call\n";
        ctx.value_table[result_slot] = result;
        ctx.emitted_values.insert(result_slot);
        ctx.value_expr_table[result_slot].clear();
        ctx.value_types[result_slot] = inst.type_id;
        ctx.value_vector_lanes[result_slot] = 0;
      }
    } else {
      std::string fallback_callee =
          effective_callee_name.empty() ? rawValue(callee) : effective_callee_name;
      if ((fallback_callee.empty() || fallback_callee == "0") &&
          ctx.shader.kind == DxilShaderKind::Vertex && !call_args.empty()) {
        ensureValueTable(result_slot);
        if (call_args.size() == 3) {
          auto a = getValue(call_args[0]);
          auto b = getValue(call_args[1]);
          auto c = getValue(call_args[2]);
          std::string expr = "(dxmt_float(" + a + ") * dxmt_float(" + b +
                             ") + dxmt_float(" + c + "))";
          os << "  auto " << result << " = " << expr
             << "; // recovered unnamed vertex ternary call\n";
          ctx.value_table[result_slot] = result;
          ctx.value_expr_table[result_slot] = expr;
          ctx.value_types[result_slot] = inst.type_id;
          ctx.value_vector_lanes[result_slot] = 0;
          recordDiagnostic(ctx,
                           "DXIL recovered unnamed vertex ternary call as scalar mad: "
                           "result=v%u",
                           result_slot);
          value_counter = std::max(value_counter + 1, result_slot + 1);
          break;
        }
        auto value = getValue(call_args[0]);
        os << "  auto " << result << " = " << value
           << "; // recovered unnamed vertex call passthrough\n";
        ctx.value_table[result_slot] = result;
        ctx.value_expr_table[result_slot] = value;
        ctx.value_types[result_slot] = inst.type_id;
        ctx.value_vector_lanes[result_slot] = 0;
        recordDiagnostic(ctx,
                         "DXIL recovered unnamed vertex call passthrough: "
                         "result=v%u argc=%zu",
                         result_slot, call_args.size());
        value_counter = std::max(value_counter + 1, result_slot + 1);
        break;
      }
      os << "  auto " << result << " = 0; // call " << fallback_callee << "(";
      for (size_t i = 0; i < call_args.size(); i++) {
        if (i) os << ", ";
        os << rawValue(call_args[i]);
      }
      os << ")\n";
      ensureValueTable(result_slot);
      ctx.value_table[result_slot] = result;
      ctx.value_expr_table[result_slot].clear();
      ctx.value_vector_lanes[result_slot] = 0;
    }
    value_counter = std::max(value_counter + 1, result_slot + 1);
    break;
  }

  case LLVMInstruction::Add: {
    ensureValueTable(result_slot);
    std::string expr;
    if (usesPointerOperand() && !isPointerTypeId(inst.type_id, ctx.mod)) {
      recordDiagnostic(ctx, "DXIL pointer add fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // pointer add fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        expr = folded;
      } else {
        expr = numericOperandForResultType(inst.operands[0], inst.type_id) +
               " + " +
               numericOperandForResultType(inst.operands[1], inst.type_id);
      }
      expr = normalizeMSLNumericExpr(std::move(expr));
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult(inferVectorLaneCountFromExpr(expr), expr);
    break;
  }

  case LLVMInstruction::Sub: {
    ensureValueTable(result_slot);
    std::string expr;
    if (usesPointerOperand() && !isPointerTypeId(inst.type_id, ctx.mod)) {
      recordDiagnostic(ctx, "DXIL pointer sub fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // pointer sub fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        expr = folded;
      } else {
        expr = numericOperandForResultType(inst.operands[0], inst.type_id) +
               " - " +
               numericOperandForResultType(inst.operands[1], inst.type_id);
      }
      expr = normalizeMSLNumericExpr(std::move(expr));
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult(inferVectorLaneCountFromExpr(expr), expr);
    break;
  }

  case LLVMInstruction::Mul: {
    ensureValueTable(result_slot);
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer mul fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // pointer mul fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        expr = folded;
      } else {
        expr = numericOperandForResultType(inst.operands[0], inst.type_id) +
               " * " +
               numericOperandForResultType(inst.operands[1], inst.type_id);
      }
      expr = normalizeMSLNumericExpr(std::move(expr));
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult(inferVectorLaneCountFromExpr(expr), expr);
    break;
  }

  case LLVMInstruction::UDiv: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer udiv fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // pointer udiv fallback\n";
    } else {
      expr = "(" + scalarValue(inst.operands[0]) + ") / (" +
             scalarValue(inst.operands[1]) + ")";
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::SDiv: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer sdiv fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // pointer sdiv fallback\n";
    } else {
      expr = "(" + scalarValue(inst.operands[0]) + ") / (" +
             scalarValue(inst.operands[1]) + ")";
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::FAdd: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer fadd fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
    } else {
      expr = floatOperandForResultType(inst.operands[0], inst.type_id) +
             " + " + floatOperandForResultType(inst.operands[1], inst.type_id);
    }
    expr = normalizeMSLNumericExpr(std::move(expr));
    os << "  auto " << result << " = " << expr << ";\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FSub: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer fsub fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
    } else {
      expr = floatOperandForResultType(inst.operands[0], inst.type_id) +
             " - " + floatOperandForResultType(inst.operands[1], inst.type_id);
    }
    expr = normalizeMSLNumericExpr(std::move(expr));
    os << "  auto " << result << " = " << expr << ";\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FMul: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer fmul fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
    } else {
      expr = floatOperandForResultType(inst.operands[0], inst.type_id) +
             " * " + floatOperandForResultType(inst.operands[1], inst.type_id);
    }
    expr = normalizeMSLNumericExpr(std::move(expr));
    os << "  auto " << result << " = " << expr << ";\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FDiv: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer fdiv fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
    } else {
      expr = floatOperandForResultType(inst.operands[0], inst.type_id) +
             " / " + floatOperandForResultType(inst.operands[1], inst.type_id);
    }
    expr = normalizeMSLNumericExpr(std::move(expr));
    os << "  auto " << result << " = " << expr << ";\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FRem: {
    std::string expr;
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer frem fallback: result=v%u", result_slot);
      expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // pointer frem fallback\n";
    } else {
      expr = "fmod(" + floatOperandForResultType(inst.operands[0], inst.type_id) +
             ", " + floatOperandForResultType(inst.operands[1], inst.type_id) + ")";
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::And: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer and fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer and fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        os << "  auto " << result << " = " << folded << ";\n";
      } else if (isBoolTypeId(inst.type_id, ctx.mod)) {
        os << "  bool " << result << " = (" << boolValue(inst.operands[0]) << ") && ("
           << boolValue(inst.operands[1]) << ");\n";
      } else if (!isIntegerLikeTypeId(inst.type_id, ctx.mod)) {
        os << "  auto " << result << " = " << uintValue(inst.operands[0]) << " & "
           << uintValue(inst.operands[1]) << ";\n";
      } else {
        os << "  auto " << result << " = " << intValue(inst.operands[0]) << " & "
           << intValue(inst.operands[1]) << ";\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::Or: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer or fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer or fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        os << "  auto " << result << " = " << folded << ";\n";
      } else if (isBoolTypeId(inst.type_id, ctx.mod)) {
        os << "  bool " << result << " = (" << boolValue(inst.operands[0]) << ") || ("
           << boolValue(inst.operands[1]) << ");\n";
      } else if (!isIntegerLikeTypeId(inst.type_id, ctx.mod)) {
        os << "  auto " << result << " = " << uintValue(inst.operands[0]) << " | "
           << uintValue(inst.operands[1]) << ";\n";
      } else {
        os << "  auto " << result << " = " << intValue(inst.operands[0]) << " | "
           << intValue(inst.operands[1]) << ";\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::Xor: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer xor fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer xor fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        os << "  auto " << result << " = " << folded << ";\n";
      } else if (isBoolTypeId(inst.type_id, ctx.mod)) {
        os << "  bool " << result << " = (" << boolValue(inst.operands[0]) << ") != ("
           << boolValue(inst.operands[1]) << ");\n";
      } else if (!isIntegerLikeTypeId(inst.type_id, ctx.mod)) {
        os << "  auto " << result << " = " << uintValue(inst.operands[0]) << " ^ "
           << uintValue(inst.operands[1]) << ";\n";
      } else {
        os << "  auto " << result << " = " << intValue(inst.operands[0]) << " ^ "
           << intValue(inst.operands[1]) << ";\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::Shl: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer shl fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer shl fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        os << "  auto " << result << " = " << folded << ";\n";
      } else {
        os << "  auto " << result << " = " << intValue(inst.operands[0]) << " << "
           << uintValue(inst.operands[1]) << ";\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::LShr: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer lshr fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer lshr fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        os << "  auto " << result << " = " << folded << ";\n";
      } else {
        os << "  auto " << result << " = " << uintValue(inst.operands[0]) << " >> "
           << uintValue(inst.operands[1]) << ";\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::AShr: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer ashr fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer ashr fallback\n";
    } else {
      auto lhs = getValue(inst.operands[0]);
      auto rhs = getValue(inst.operands[1]);
      std::string folded;
      if (tryFoldIntegerBinary(inst.opcode, lhs, rhs, folded)) {
        os << "  auto " << result << " = " << folded << ";\n";
      } else {
        os << "  auto " << result << " = " << intValue(inst.operands[0]) << " >> "
           << uintValue(inst.operands[1]) << ";\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::BitCast: {
    std::string bitcast_expr = inst.operands.size() >= 1
                                   ? operandValueForResultType(inst.operands[0], inst.type_id)
                                   : "0";
    if (inst.operands.size() >= 1) {
      os << "  auto " << result << " = " << bitcast_expr << "; // bitcast\n";
    }
    if (isPointerTypeId(inst.type_id, ctx.mod))
      publishPointerResult();
    else
      publishResult(0, bitcast_expr);
    break;
  }

  case LLVMInstruction::ZExt: {
    std::string expr = inst.operands.size() >= 1
                           ? operandValueForResultType(inst.operands[0], inst.type_id)
                           : "0";
    if (inst.operands.size() >= 1) {
      os << "  auto " << result << " = " << expr << "; // zext\n";
    }
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::SExt: {
    std::string expr = inst.operands.size() >= 1
                           ? operandValueForResultType(inst.operands[0], inst.type_id)
                           : "0";
    if (inst.operands.size() >= 1) {
      os << "  auto " << result << " = " << expr << "; // sext\n";
    }
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::Trunc: {
    std::string expr = inst.operands.size() >= 1
                           ? operandValueForResultType(inst.operands[0], inst.type_id)
                           : "0";
    if (inst.operands.size() >= 1) {
      os << "  auto " << result << " = " << expr << "; // trunc\n";
    }
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FPToUI: {
    os << "  auto " << result << " = " << uintValue(inst.operands[0]) << "; // fptoui\n";
    publishResult();
    break;
  }

  case LLVMInstruction::FPToSI: {
    os << "  auto " << result << " = " << intValue(inst.operands[0]) << "; // fptosi\n";
    publishResult();
    break;
  }

  case LLVMInstruction::UIToFP: {
    std::string expr = operandValueForResultType(inst.operands[0], inst.type_id);
    os << "  auto " << result << " = " << expr << "; // uitofp\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::SIToFP: {
    std::string expr = operandValueForResultType(inst.operands[0], inst.type_id);
    os << "  auto " << result << " = " << expr << "; // sitofp\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FPTrunc: {
    std::string expr = operandValueForResultType(inst.operands[0], inst.type_id);
    os << "  auto " << result << " = " << expr << "; // fptrunc\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::FPExt: {
    std::string expr = operandValueForResultType(inst.operands[0], inst.type_id);
    os << "  auto " << result << " = " << expr << "; // fpext\n";
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::PtrToInt: {
    recordDiagnostic(ctx, "DXIL ptrtoint fallback: result=v%u", result_slot);
    os << "  auto " << result << " = 0; // ptrtoint fallback\n";
    publishResult();
    break;
  }

  case LLVMInstruction::IntToPtr: {
    if (inst.operands.size() >= 1) {
      os << "  auto " << result << " = " << getValue(inst.operands[0])
         << "; // inttoptr fallback\n";
    } else {
      os << "  auto " << result << " = 0; // inttoptr fallback\n";
    }
    publishPointerResult();
    break;
  }

  case LLVMInstruction::ICmp: {
    if (inst.operands.size() >= 3) {
      auto pred = inst.operands[0];
      auto lhs = getValue(inst.operands[1]);
      auto rhs = getValue(inst.operands[2]);
      std::string op;
      switch (pred) {
      case 32: op = "=="; break;
      case 33: op = "!="; break;
      case 34: op = ">"; break;
      case 35: op = ">="; break;
      case 36: op = "<"; break;
      case 37: op = "<="; break;
      default: op = "=="; break;
      }
      os << "  bool " << result << " = " << scalarValue(inst.operands[1]) << " "
         << op << " " << scalarValue(inst.operands[2]) << ";\n";
    }
    publishResult(1, result);
    break;
  }

  case LLVMInstruction::FCmp: {
    if (inst.operands.size() >= 3) {
      auto pred = inst.operands[0];
      auto lhs = getValue(inst.operands[1]);
      auto rhs = getValue(inst.operands[2]);
      std::string op;
      switch (pred) {
      case 0: os << "  bool " << result << " = false;\n"; break;
      case 1: os << "  bool " << result << " = true;\n"; break;
      case 2: os << "  bool " << result << " = (isnan((float)(" << scalarValue(inst.operands[1]) << ")) || isnan((float)(" << scalarValue(inst.operands[2]) << ")));\n"; break;
      case 3: os << "  bool " << result << " = (" << scalarValue(inst.operands[1]) << " == " << scalarValue(inst.operands[2]) << ");\n"; break;
      case 4: os << "  bool " << result << " = (" << scalarValue(inst.operands[1]) << " != " << scalarValue(inst.operands[2]) << ");\n"; break;
      case 5: os << "  bool " << result << " = (" << scalarValue(inst.operands[1]) << " > " << scalarValue(inst.operands[2]) << ");\n"; break;
      case 6: os << "  bool " << result << " = (" << scalarValue(inst.operands[1]) << " >= " << scalarValue(inst.operands[2]) << ");\n"; break;
      case 7: os << "  bool " << result << " = (" << scalarValue(inst.operands[1]) << " < " << scalarValue(inst.operands[2]) << ");\n"; break;
      case 8: os << "  bool " << result << " = (" << scalarValue(inst.operands[1]) << " <= " << scalarValue(inst.operands[2]) << ");\n"; break;
      default: os << "  bool " << result << " = false;\n"; break;
      }
    }
    publishResult(1, result);
    break;
  }

  case LLVMInstruction::Select: {
    std::string expr;
    if (inst.operands.size() >= 3) {
      expr = boolValue(inst.operands[0]) + " ? " +
             operandValueForResultType(inst.operands[1], inst.type_id) + " : " +
             operandValueForResultType(inst.operands[2], inst.type_id);
      expr = normalizeMSLNumericExpr(std::move(expr));
      os << "  auto " << result << " = " << expr << ";\n";
    }
    publishResult(inferVectorLaneCountFromExpr(expr), expr);
    break;
  }

  case LLVMInstruction::Load: {
    std::string expr = "0";
    if (inst.operands.size() >= 1) {
      auto ptr = getValue(inst.operands[0]);
      auto stored = ctx.local_values.find(ptr);
      if (stored != ctx.local_values.end()) {
        expr = stored->second;
        os << "  auto " << result << " = " << stored->second
           << "; // load local " << ptr << "\n";
      } else {
        recordDiagnostic(ctx, "DXIL generic load fallback: ptr=%s", ptr.c_str());
        expr = "0";
        os << "  auto " << result << " = 0; // load from " << ptr << "\n";
      }
    }
    publishResult(maxOperandVectorLaneCount(), expr);
    break;
  }

  case LLVMInstruction::Store: {
    if (inst.operands.size() >= 2) {
      auto ptr = getValue(inst.operands[0]);
      auto value = getValue(inst.operands[1]);
      ctx.local_values[ptr] = value;
      os << "  // generic store " << ptr << " <- " << value << "\n";
    }
    break;
  }

  case LLVMInstruction::GEP:
  case LLVMInstruction::GetElementPtr: {
    if (inst.operands.size() >= 2) {
      auto base = getValue(inst.operands[0]);
      std::string offset = "0";
      if (inst.operands.size() >= 2)
        offset = getValue(inst.operands[1]);
      for (size_t i = 2; i < inst.operands.size(); i++) {
        offset = "(" + offset + " + " + getValue(inst.operands[i]) + ")";
      }
      bool base_is_pointer = ctx.pointer_slots.find(inst.operands[0]) != ctx.pointer_slots.end()
        || startsWith(base, "buf") || base.find("_storage") != std::string::npos;
      bool offset_is_pointer = false;
      for (size_t i = 1; i < inst.operands.size(); i++) {
        if (ctx.pointer_slots.find(inst.operands[i]) != ctx.pointer_slots.end()) {
          offset_is_pointer = true;
          break;
        }
      }
      if (base_is_pointer && !offset_is_pointer) {
        os << "  auto " << result << " = " << base << " + (long)(" << offset
           << ");\n";
      } else if (!base_is_pointer && !offset_is_pointer) {
        os << "  auto " << result << " = " << offset
           << "; // gep integer fallback\n";
      } else {
        recordDiagnostic(ctx, "DXIL gep pointer fallback: result=v%u", result_slot);
        os << "  auto " << result << " = " << base << "; // gep pointer fallback\n";
      }
      if (isZeroLiteral(offset)) {
        auto stored = ctx.local_values.find(base);
        if (stored != ctx.local_values.end())
          ctx.local_values[result] = stored->second;
      }
    }
    publishPointerResult();
    break;
  }

  case LLVMInstruction::Alloca: {
    os << "  thread char " << result << "_storage[256] = {};\n";
    os << "  thread char* " << result << " = " << result << "_storage;\n";
    ctx.local_values[result] = "0";
    publishPointerResult();
    break;
  }

  case LLVMInstruction::PHI: {
    std::string expr;
    if (!inst.operands.empty()) {
      expr = resolvedExpr(resolvedExpr, inst.operands[0], 0);
      if (isFunctionLikeSymbol(expr))
        expr = defaultValueForTypeId(inst.type_id, ctx.mod);
      os << "  auto " << result << " = " << expr << "; // phi first incoming\n";
    } else {
      expr = "0";
      os << "  auto " << result << " = 0; // empty phi\n";
    }
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::Br: {
    if (inst.operands.size() == 1) {
      // unconditional branch
    } else if (inst.operands.size() >= 3) {
      auto cond = boolValue(inst.operands[0]);
      os << "  if (" << cond << ") {\n  // br true\n  } else {\n  // br false\n  }\n";
    }
    break;
  }

  case LLVMInstruction::Switch: {
    os << "  // switch\n";
    break;
  }

  case LLVMInstruction::ExtractValue: {
    std::string expr;
    uint8_t lanes = 0;
    if (inst.operands.size() >= 2) {
      auto agg = resolvedExpr(resolvedExpr, inst.operands[0], 0);
      uint32_t agg_type_id = inst.operands[0] < ctx.value_types.size()
                                 ? ctx.value_types[inst.operands[0]]
                                 : 0;
      lanes = resolvedVectorLaneCount(resolvedVectorLaneCount, inst.operands[0], 0);
      uint8_t expr_lanes = inferVectorLaneCountFromExpr(agg);
      if (lanes <= 1)
        lanes = expr_lanes;
      else if (expr_lanes <= 1 && exprLooksScalar(agg))
        lanes = expr_lanes;
      bool aggregate_like =
          agg_type_id < ctx.mod.types.size() &&
          (ctx.mod.types[agg_type_id].kind == LLVMType::Struct ||
           ctx.mod.types[agg_type_id].kind == LLVMType::Array ||
           ctx.mod.types[agg_type_id].kind == LLVMType::Vector);
      auto idx = inst.operands[1];
      if (!aggregate_like || isZeroLiteral(agg) || isFunctionLikeSymbol(agg) ||
          (lanes <= 1 && idx < 4) || (lanes > 1 && idx >= lanes)) {
        expr = defaultValueForTypeId(inst.type_id, ctx.mod);
        os << "  auto " << result << " = " << expr
           << "; // extractvalue fallback\n";
      } else if (idx < 4 && lanes > 1) {
        expr = "(" + agg + ")" + componentSuffix(idx);
        os << "  auto " << result << " = " << expr
           << "; // extractvalue\n";
      } else {
        expr = "(" + agg + ")";
        os << "  auto " << result << " = " << expr
           << "; // extractvalue idx=" << idx << "\n";
      }
    }
    publishResult(0, expr);
    break;
  }

  case LLVMInstruction::InsertValue: {
    std::string base = inst.operands.size() >= 1 ? getValue(inst.operands[0])
                                                 : defaultValueForTypeId(inst.type_id, ctx.mod);
    uint32_t agg_type_id = inst.operands.size() >= 1 && inst.operands[0] < ctx.value_types.size()
                               ? ctx.value_types[inst.operands[0]]
                               : 0;
    bool aggregate_like =
        agg_type_id < ctx.mod.types.size() &&
        (ctx.mod.types[agg_type_id].kind == LLVMType::Struct ||
         ctx.mod.types[agg_type_id].kind == LLVMType::Array ||
         ctx.mod.types[agg_type_id].kind == LLVMType::Vector);
    if (!aggregate_like || isZeroLiteral(base) || isFunctionLikeSymbol(base))
      base = defaultValueForTypeId(inst.type_id, ctx.mod);
    os << "  auto " << result << " = " << base << "; // insertvalue\n";
    if (inst.operands.size() >= 3 && inst.operands[2] < 4) {
      os << "  " << result << componentSuffix(inst.operands[2])
         << " = " << getValue(inst.operands[1]) << ";\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::ExtractElement: {
    if (inst.operands.size() >= 2) {
      auto idx = getValue(inst.operands[1]);
      auto vec = resolvedExpr(resolvedExpr, inst.operands[0], 0);
      uint8_t lanes = resolvedVectorLaneCount(resolvedVectorLaneCount,
                                              inst.operands[0], 0);
      if (lanes <= 1)
        lanes = inferVectorLaneCountFromExpr(vec);
      uint32_t literal_index = 0;
      bool has_literal_index = parseUnsignedLiteral(idx, literal_index);
      if (lanes > 1 && (!has_literal_index || literal_index < lanes)) {
        os << "  auto " << result << " = " << vec
           << componentAccessor(idx) << ";\n";
      } else {
        os << "  auto " << result << " = " << vec
           << "; // extractelement scalar fallback\n";
      }
    }
    publishResult();
    break;
  }

  case LLVMInstruction::InsertElement: {
    if (inst.operands.size() >= 3) {
      auto vec = getValue(inst.operands[0]);
      auto elem = getValue(inst.operands[1]);
      auto idx = getValue(inst.operands[2]);
      os << "  auto " << result << " = " << vec << ";\n";
      os << "  " << result << componentAccessor(idx) << " = " << elem << ";\n";
    } else {
      os << "  auto " << result << " = "
         << (inst.operands.size() >= 1 ? getValue(inst.operands[0]) : "float4(0)")
         << "; // insertelement fallback\n";
    }
    publishResult();
    break;
  }

	  case LLVMInstruction::ShuffleVector: {
	    auto lhs = inst.operands.size() >= 1 ? getValue(inst.operands[0]) : "float4(0)";
	    auto rhs = inst.operands.size() >= 2 ? getValue(inst.operands[1]) : "float4(0)";
	    auto mask = inst.operands.size() >= 3 ? getValue(inst.operands[2]) : "";
	    uint8_t lhs_lanes = inst.operands.size() >= 1
	                            ? resolvedVectorLaneCount(resolvedVectorLaneCount,
	                                                      inst.operands[0], 0)
	                            : 4;
	    uint8_t rhs_lanes = inst.operands.size() >= 2
	                            ? resolvedVectorLaneCount(resolvedVectorLaneCount,
	                                                      inst.operands[1], 0)
	                            : 4;
	    if (lhs_lanes <= 1)
	      lhs_lanes = inferVectorLaneCountFromExpr(lhs);
	    if (rhs_lanes <= 1)
	      rhs_lanes = inferVectorLaneCountFromExpr(rhs);
	    auto shuffleComponent = [](const std::string &value, uint8_t lanes,
	                               uint32_t index) {
	      if (lanes > 1 && index < lanes)
	        return vectorComponent(value, index);
	      return value;
	    };
	    auto mask_values = parseAggregateLiteral(mask);
	    if (mask_values.empty()) {
	      uint32_t single_index = 0;
	      if (parseUnsignedLiteral(mask, single_index))
	        mask_values.push_back(mask);
    }

    if (!mask_values.empty()) {
      std::vector<std::string> components;
      for (auto &mask_value : mask_values) {
        uint32_t index = 0;
	        if (!parseUnsignedLiteral(mask_value, index) || index == 0xFFFFFFFFu) {
	          components.push_back("0.0f");
	        } else if (index < 4) {
	          components.push_back(shuffleComponent(lhs, lhs_lanes, index));
	        } else {
	          components.push_back(shuffleComponent(rhs, rhs_lanes, index - 4));
	        }
	      }

      if (components.size() == 1) {
        os << "  auto " << result << " = " << components[0]
           << "; // shufflevector scalar\n";
      } else {
        std::string type_name = "float" + std::to_string(components.size());
        if (inst.type_id < ctx.mod.types.size() &&
            ctx.mod.types[inst.type_id].kind == LLVMType::Vector) {
          type_name = getTypeName(ctx.mod.types[inst.type_id], ctx.mod);
        }
        os << "  auto " << result << " = " << type_name << "(";
        for (size_t i = 0; i < components.size(); i++) {
          if (i) os << ", ";
          os << components[i];
        }
        os << "); // shufflevector\n";
      }
    } else {
      recordDiagnostic(ctx, "DXIL shufflevector fallback: mask=%s", mask.c_str());
      os << "  auto " << result << " = " << lhs << "; // shufflevector fallback\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::Unreachable:
    os << "  // unreachable\n";
    break;

  case LLVMInstruction::FNeg: {
    if (inst.operands.size() >= 1) {
      os << "  auto " << result << " = -(" << getValue(inst.operands[0]) << ");\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::URem:
  case LLVMInstruction::SRem: {
    if (usesPointerOperand()) {
      recordDiagnostic(ctx, "DXIL pointer rem fallback: result=v%u", result_slot);
      os << "  auto " << result << " = " << defaultValueForTypeId(inst.type_id, ctx.mod)
         << "; // pointer rem fallback\n";
    } else {
      os << "  auto " << result << " = " << intValue(inst.operands[0]) << " % "
         << intValue(inst.operands[1]) << ";\n";
    }
    publishResult();
    break;
  }

  case LLVMInstruction::Invoke: {
    os << "  // invoke\n";
    break;
  }

  default:
    ctx.unsupported_opcodes++;
    recordDiagnostic(ctx, "DXIL unhandled opcode: %d type=%u operands=%zu", (int)inst.opcode,
                     inst.type_id, inst.operands.size());
    os << "  // unhandled opcode " << (int)inst.opcode << "\n";
    publishResult();
    break;
  }
}

std::optional<MSLShader> DXILToMSL::convert(const LLVMModule &module,
                                              const DxilParsedShader &shader) {
  static const MSLConvertOptions default_options = {};
  return convert(module, shader, default_options);
}

std::optional<MSLShader> DXILToMSL::convert(const LLVMModule &module,
                                              const DxilParsedShader &shader,
                                              const MSLConvertOptions &options) {
  DXTRACE("DXILToMSL::convert: kind=%u sm=%u.%u functions=%zu types=%zu",
          (uint32_t)shader.kind, shader.shader_model.major, shader.shader_model.minor,
          module.functions.size(), module.types.size());

  std::ostringstream os;
  EmitContext ctx{os, module, shader, options, {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, 0,
                  false, false, false, false};

  if (module.functions.empty()) {
    DXTRACE("DXILToMSL: refusing to emit default shader for module with no functions");
    return std::nullopt;
  }

  const LLVMFunction *entry_fn = nullptr;
  for (auto &fn : module.functions) {
    if (!fn.blocks.empty() && !shader.entry_point.empty() &&
        fn.name == shader.entry_point) {
      entry_fn = &fn;
      break;
    }
  }
  if (!entry_fn) {
    for (auto it = module.functions.rbegin(); it != module.functions.rend(); ++it) {
      if (!it->blocks.empty()) {
        entry_fn = &*it;
        break;
      }
    }
  }
  if (!entry_fn) {
    DXTRACE("DXILToMSL: refusing to emit shader because no parsed function has blocks");
    return std::nullopt;
  }

  auto &fn = *entry_fn;

  emitFunctionPrologue(ctx);

  ctx.value_table.resize(256);
  ctx.value_expr_table.resize(256);
  ctx.value_types.resize(256, 0);
  ctx.value_vector_lanes.resize(256, 0);

  for (size_t i = 0; i < module.constants.size(); i++) {
    uint32_t val_idx = module.constants[i].id;
    if (ctx.value_table.size() <= val_idx)
      ctx.value_table.resize(val_idx + 1);
    if (val_idx < ctx.value_table.size()) {
      ctx.value_table[val_idx] = normalizeConstantData(
          module.constants[i].constant_data, module.constants[i].type_id, module);
      ctx.value_expr_table[val_idx] = ctx.value_table[val_idx];
      ctx.value_types[val_idx] = module.constants[i].type_id;
      ctx.value_vector_lanes[val_idx] =
          vectorLaneCountForTypeId(module.constants[i].type_id, module);
    }
  }

  for (const auto &fn_value : module.functions) {
    if (!fn_value.is_declaration || fn_value.value_id == 0)
      continue;
    if (fn_value.value_id >= ctx.value_table.size()) {
      ctx.value_table.resize(fn_value.value_id + 1);
      ctx.value_expr_table.resize(fn_value.value_id + 1);
      ctx.value_types.resize(fn_value.value_id + 1, 0);
    }
    if (!fn_value.name.empty())
      ctx.value_table[fn_value.value_id] = fn_value.name;
    ctx.value_expr_table[fn_value.value_id] = ctx.value_table[fn_value.value_id];
    ctx.value_types[fn_value.value_id] = fn_value.type_id;
    ctx.value_vector_lanes[fn_value.value_id] = 0;
  }

  DXTRACE("DXILToMSL: entry function has %zu blocks", fn.blocks.size());

  uint32_t value_counter = fn.instruction_start_value;

  for (auto &block : fn.blocks) {
    for (auto &inst : block.instructions) {
      emitInstruction(ctx, inst, value_counter);
    }
  }

  os << "}\n";

  MSLShader result;
  result.source = os.str();
  std::unordered_set<uint32_t> unresolved_ssa;
  std::unordered_set<uint32_t> used_ssa;
  std::unordered_set<uint32_t> declared_ssa;
  collectSSATokens(result.source, used_ssa, declared_ssa);
  for (uint32_t idx : used_ssa) {
    if (declared_ssa.find(idx) == declared_ssa.end())
      unresolved_ssa.insert(idx);
  }
  for (const auto &diag : ctx.diagnostics) {
    uint32_t idx = 0;
    if (sscanf(diag.c_str(), "DXIL missing SSA value: v%u", &idx) == 1 ||
        sscanf(diag.c_str(), "DXIL unresolved SSA alias: v%u", &idx) == 1) {
      unresolved_ssa.insert(idx);
    }
  }
  if (!unresolved_ssa.empty()) {
    std::string prologue;
    for (uint32_t idx : unresolved_ssa) {
      std::string name = emitValue(idx);
      if (result.source.find(" " + name + " =") == std::string::npos) {
        uint32_t type_id = idx < ctx.value_types.size() ? ctx.value_types[idx] : 0;
        prologue += "  auto " + name + " = " +
                    defaultValueForTypeId(type_id, ctx.mod) +
                    "; // generated-source SSA fallback\n";
        recordDiagnostic(ctx, "DXIL generated-source SSA fallback: v%u", idx);
      }
    }
    if (!prologue.empty()) {
      size_t body = result.source.find(") {\n");
      if (body != std::string::npos)
        result.source.insert(body + 4, prologue);
    }
  }
  result.entry_point = shader.entry_point;
  result.tg_size[0] = 1;
  result.tg_size[1] = 1;
  result.tg_size[2] = 1;
  result.unsupported_intrinsics = ctx.unsupported_intrinsics;
  result.unsupported_opcodes = ctx.unsupported_opcodes;
  result.diagnostics = ctx.diagnostics;

  DXTRACE("DXILToMSL: generated %zu bytes of MSL unsupported_intrinsics=%u unsupported_opcodes=%u",
          result.source.size(), ctx.unsupported_intrinsics, ctx.unsupported_opcodes);

  return result;
}

}
