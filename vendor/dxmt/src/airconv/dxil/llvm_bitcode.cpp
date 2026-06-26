#include "llvm_bitcode.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <bitset>
#include <unordered_set>

static bool
dxmt_dxil_trace_enabled() {
  const char *value = std::getenv("DXMT_DXIL_TRACE");
  return value && value[0] && std::strcmp(value, "0") != 0;
}

static FILE *
dxmt_dxil_open_trace_log(const char *fallback_name) {
  if (!dxmt_dxil_trace_enabled())
    return nullptr;

  const char *root = std::getenv("DXMT_LOG_PATH");
  const char *file = fallback_name && fallback_name[0] ? fallback_name : "dxmt-dxil-trace.log";
  char path[4096];

  if (!root || !root[0])
    return std::fopen(file, "a");

  std::snprintf(path, sizeof(path), "%s%s%s", root, (root[std::strlen(root) - 1] == '/' || root[std::strlen(root) - 1] == '\\') ? "" : "/", file);
  path[sizeof(path) - 1] = '\0';
  return std::fopen(path, "a");
}

#define DXTRACE(fmt, ...) do { FILE *_tf = dxmt_dxil_open_trace_log("dxmt-dxil-trace.log"); if (_tf) { fprintf(_tf, fmt "\n", ##__VA_ARGS__); fclose(_tf); } } while(0)

namespace dxmt::dxil {

class BitstreamReader {
public:
  BitstreamReader(const uint8_t *data, uint32_t size)
    : m_data(data), m_size(size), m_offset(0), m_cur_byte(0), m_bits_left(0) {}

  uint32_t read(uint32_t num_bits) {
    uint32_t result = 0;
    uint32_t bits_read = 0;
    while (bits_read < num_bits) {
      if (m_bits_left == 0) {
        if (m_offset >= m_size) return 0;
        m_cur_byte = m_data[m_offset++];
        m_bits_left = 8;
      }
      uint32_t to_read = std::min(num_bits - bits_read, m_bits_left);
      result |= (uint32_t)(m_cur_byte & ((1 << to_read) - 1)) << bits_read;
      m_cur_byte >>= to_read;
      m_bits_left -= to_read;
      bits_read += to_read;
    }
    return result;
  }

  uint64_t read64(uint32_t num_bits) {
    if (num_bits <= 32) return read(num_bits);
    uint64_t lo = read(32);
    uint64_t hi = read(num_bits - 32);
    return lo | (hi << 32);
  }

  uint32_t readVBR(uint32_t width) {
    uint32_t result = 0;
    uint32_t shift = 0;
    uint32_t chunk;
    do {
      chunk = read(width);
      result |= (chunk & ((1u << (width - 1)) - 1)) << shift;
      shift += width - 1;
    } while (chunk & (1u << (width - 1)));
    return result;
  }

  uint64_t readVBR64(uint32_t width) {
    uint64_t result = 0;
    uint64_t shift = 0;
    uint32_t chunk;
    do {
      chunk = read(width);
      result |= (uint64_t)(chunk & ((1u << (width - 1)) - 1)) << shift;
      shift += width - 1;
    } while (chunk & (1u << (width - 1)));
    return result;
  }

  void align32() {
    uint32_t bit_pos = tell();
    uint32_t aligned = (bit_pos + 31) & ~31u;
    seek(aligned);
  }

  uint32_t tell() const { return m_offset * 8 - m_bits_left; }
  void seek(uint32_t bit_pos) {
    m_offset = bit_pos / 8;
    m_bits_left = 0;
    uint32_t skip = bit_pos % 8;
    if (skip) read(skip);
  }

  bool atEnd() const { return m_offset >= m_size && m_bits_left == 0; }

private:
  const uint8_t *m_data;
  uint32_t m_size;
  uint32_t m_offset;
  uint8_t m_cur_byte;
  uint32_t m_bits_left;
};

static constexpr uint32_t kEnterSubBlock = 1;
static constexpr uint32_t kEndBlock = 0;
static constexpr uint32_t kDefineAbbrev = 2;
static constexpr uint32_t kUnabbrevRecord = 3;

static constexpr uint32_t kBlockID_Module = 8;
static constexpr uint32_t kBlockID_BlockInfo = 0;
static constexpr uint32_t kBlockID_ValueSymTab = 14;
static constexpr uint32_t kBlockID_Strtab = 23;
static constexpr uint32_t kBlockID_Function = 12;
static constexpr uint32_t kBlockID_Type = 17;
static constexpr uint32_t kBlockID_Constants = 11;
static constexpr uint32_t kBlockID_Metadata = 15;
static constexpr uint32_t kBlockID_MetadataAttachment = 16;

static constexpr uint32_t kMetadataCode_String = 3;
static constexpr uint32_t kMetadataCode_Value = 5;
static constexpr uint32_t kMetadataCode_Kind = 6;
static constexpr uint32_t kMetadataCode_Node = 1;
static constexpr uint32_t kMetadataCode_NamedNode = 13;

static constexpr uint32_t kModuleCode_Version = 1;
static constexpr uint32_t kTypeCode_Void = 2;
static constexpr uint32_t kTypeCode_Float = 3;
static constexpr uint32_t kTypeCode_Double = 4;
static constexpr uint32_t kTypeCode_Label = 5;
static constexpr uint32_t kTypeCode_Opaque = 6;
static constexpr uint32_t kTypeCode_Integer = 7;
static constexpr uint32_t kTypeCode_Pointer = 8;
static constexpr uint32_t kTypeCode_FunctionOld = 9;
static constexpr uint32_t kTypeCode_Half = 10;
static constexpr uint32_t kTypeCode_Array = 11;
static constexpr uint32_t kTypeCode_Vector = 12;
static constexpr uint32_t kTypeCode_X86FP80 = 13;
static constexpr uint32_t kTypeCode_FP128 = 14;
static constexpr uint32_t kTypeCode_PPCFP128 = 15;
static constexpr uint32_t kTypeCode_Metadata = 16;
static constexpr uint32_t kTypeCode_X86MMX = 17;
static constexpr uint32_t kTypeCode_StructAnon = 18;
static constexpr uint32_t kTypeCode_StructName = 19;
static constexpr uint32_t kTypeCode_StructNamed = 20;
static constexpr uint32_t kTypeCode_Function = 21;
static constexpr uint32_t kTypeCode_Token = 22;
static constexpr uint32_t kTypeCode_BFloat = 23;
static constexpr uint32_t kTypeCode_X86AMX = 24;
static constexpr uint32_t kTypeCode_OpaquePointer = 25;
static constexpr uint32_t kTypeCode_TargetType = 26;
static constexpr uint32_t kTypeCode_Byte = 27;

static constexpr uint32_t kModuleCode_Function = 8;
static constexpr uint32_t kModuleCode_GlobalVar = 7;
static constexpr uint32_t kModuleCode_Triple = 2;
static constexpr uint32_t kModuleCode_VSTOffset = 13;
static constexpr uint32_t kModuleCode_SourceFilename = 16;

static constexpr uint32_t kValueSymTabCode_Entry = 1;
static constexpr uint32_t kValueSymTabCode_BBEntry = 2;
static constexpr uint32_t kValueSymTabCode_FnEntry = 3;

static constexpr uint32_t kStrtabCode_Blob = 1;

static constexpr uint32_t kFuncCode_DeclareBlocks = 1;
static constexpr uint32_t kFuncCode_InstRet = 10;
static constexpr uint32_t kFuncCode_InstBr = 11;
static constexpr uint32_t kFuncCode_InstCall = 34;
static constexpr uint32_t kFuncCode_InstPHI = 16;
static constexpr uint32_t kFuncCode_InstBinop = 2;
static constexpr uint32_t kFuncCode_InstCast = 3;
static constexpr uint32_t kFuncCode_InstGEP_OLD = 4;
static constexpr uint32_t kFuncCode_InstInBoundsGEP_OLD = 30;
static constexpr uint32_t kFuncCode_InstGEP = 43;
static constexpr uint32_t kFuncCode_InstLoad = 20;
static constexpr uint32_t kFuncCode_InstAtomicRMW = 38;
static constexpr uint32_t kFuncCode_InstStore = 44;
static constexpr uint32_t kFuncCode_InstExtractVal = 26;
static constexpr uint32_t kFuncCode_InstInsertVal = 27;
static constexpr uint32_t kFuncCode_InstSelect = 5;
static constexpr uint32_t kFuncCode_InstVSelect = 29;
static constexpr uint32_t kFuncCode_InstCmp = 9;
static constexpr uint32_t kFuncCode_InstCmp2 = 28;
static constexpr uint32_t kFuncCode_InstUnreachable = 15;
static constexpr uint32_t kFuncCode_InstAlloca = 19;
static constexpr uint32_t kFuncCode_InstExtractElt = 6;
static constexpr uint32_t kFuncCode_InstInsertElt = 7;
static constexpr uint32_t kFuncCode_InstShuffleVec = 8;
static constexpr uint32_t kFuncCode_InstSwitch = 12;
static constexpr uint32_t kFuncCode_InstInvoke = 13;

static constexpr uint32_t kCallFlag_ExplicitType = 15;
static constexpr uint32_t kCallFlag_FastMathFlags = 17;

static constexpr uint32_t kConstantsCode_SetType = 1;
static constexpr uint32_t kConstantsCode_Null = 2;
static constexpr uint32_t kConstantsCode_Undefined = 3;
static constexpr uint32_t kConstantsCode_Integer = 4;
static constexpr uint32_t kConstantsCode_Float = 6;
static constexpr uint32_t kConstantsCode_Aggregate = 7;
static constexpr uint32_t kConstantsCode_String = 8;
static constexpr uint32_t kConstantsCode_Cast = 11;
static constexpr uint32_t kConstantsCode_GEP = 12;
static constexpr uint32_t kConstantsCode_InBoundsGEP = 20;
static constexpr uint32_t kConstantsCode_Data = 15;

static int64_t decodeSignedVBR(uint64_t value) {
  if ((value & 1) == 0)
    return (int64_t)(value >> 1);
  if (value != 1)
    return -(int64_t)(value >> 1);
  return INT64_MIN;
}

static LLVMInstruction::Opcode decodeBinop(uint32_t opcode) {
  switch (opcode) {
  case 0: return LLVMInstruction::Add;
  case 1: return LLVMInstruction::Sub;
  case 2: return LLVMInstruction::Mul;
  case 3: return LLVMInstruction::UDiv;
  case 4: return LLVMInstruction::SDiv;
  case 5: return LLVMInstruction::URem;
  case 6: return LLVMInstruction::SRem;
  case 7: return LLVMInstruction::Shl;
  case 8: return LLVMInstruction::LShr;
  case 9: return LLVMInstruction::AShr;
  case 10: return LLVMInstruction::And;
  case 11: return LLVMInstruction::Or;
  case 12: return LLVMInstruction::Xor;
  default: return LLVMInstruction::Add;
  }
}

static bool isFloatingBinopType(const LLVMModule &module, uint32_t type_id) {
  if (type_id >= module.types.size())
    return false;
  const auto &type = module.types[type_id];
  if (type.kind == LLVMType::Float || type.kind == LLVMType::Double)
    return true;
  if (type.kind == LLVMType::Vector && !type.subtypes.empty())
    return type.subtypes[0].kind == LLVMType::Float ||
           type.subtypes[0].kind == LLVMType::Double;
  return false;
}

static LLVMInstruction::Opcode decodeBinopForType(const LLVMModule &module,
                                                  uint32_t opcode,
                                                  uint32_t type_id) {
  if (!isFloatingBinopType(module, type_id))
    return decodeBinop(opcode);

  switch (opcode) {
  case 0: return LLVMInstruction::FAdd;
  case 1: return LLVMInstruction::FSub;
  case 2: return LLVMInstruction::FMul;
  case 3:
  case 4: return LLVMInstruction::FDiv;
  case 5:
  case 6: return LLVMInstruction::FRem;
  default: return decodeBinop(opcode);
  }
}

static LLVMInstruction::Opcode decodeCast(uint32_t opcode) {
  switch (opcode) {
  case 0: return LLVMInstruction::Trunc;
  case 1: return LLVMInstruction::ZExt;
  case 2: return LLVMInstruction::SExt;
  case 3: return LLVMInstruction::FPToUI;
  case 4: return LLVMInstruction::FPToSI;
  case 5: return LLVMInstruction::UIToFP;
  case 6: return LLVMInstruction::SIToFP;
  case 7: return LLVMInstruction::FPTrunc;
  case 8: return LLVMInstruction::FPExt;
  case 9: return LLVMInstruction::PtrToInt;
  case 10: return LLVMInstruction::IntToPtr;
  default: return LLVMInstruction::BitCast;
  }
}

static LLVMInstruction::Opcode decodeCmp(uint32_t predicate) {
  return predicate >= 32 ? LLVMInstruction::ICmp : LLVMInstruction::FCmp;
}

static uint32_t decodeRelativeValue(uint64_t encoded, uint32_t next_value) {
  if (encoded <= next_value)
    return next_value - (uint32_t)encoded;
  return (uint32_t)encoded;
}

static uint32_t decodeSignRotatedValue(uint64_t encoded) {
  if ((encoded & 1) == 0)
    return (uint32_t)(encoded >> 1);
  if (encoded != 1)
    return (uint32_t)(~(encoded >> 1) + 1);
  return (uint32_t)(1ull << 63);
}

static uint32_t decodeSignedRelativeValue(uint64_t encoded, uint32_t next_value) {
  uint32_t decoded = decodeSignRotatedValue(encoded);
  return next_value - decoded;
}

struct Abbrev {
  struct Op {
    bool literal = false;
    uint32_t encoding = 0;
    uint64_t value = 0;
  };
  std::vector<Op> ops;
};

struct BlockInfo {
  uint32_t block_id = 0;
  std::vector<Abbrev> abbrevs;
};

struct ParseContext {
  BitstreamReader &reader;
  LLVMModule &module;
  std::vector<Abbrev> cur_abbrevs;
  std::vector<BlockInfo> block_infos;
};

struct MetadataValue {
  uint32_t type_id = 0;
  uint64_t value = 0;
};

struct MetadataNode {
  std::vector<uint32_t> operands;
};

struct MetadataState {
  std::vector<MetadataValue> values;
  std::vector<MetadataNode> nodes;
  std::unordered_map<std::string, uint32_t> kind_map;
  uint32_t numthreads_kind = (uint32_t)-1;
  bool parsed = false;
};

struct PendingFunction {
  uint32_t value_id = 0;
  uint32_t type_id = 0;
  uint32_t param_count = 0;
  std::string name;
};

struct FunctionNameRef {
  uint32_t value_id = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
};

static BlockInfo *findOrCreateBlockInfo(std::vector<BlockInfo> &block_infos,
                                        uint32_t block_id) {
  for (auto &info : block_infos) {
    if (info.block_id == block_id)
      return &info;
  }
  block_infos.push_back({block_id, {}});
  return &block_infos.back();
}

static std::vector<Abbrev> getBlockAbbrevs(const ParseContext &ctx,
                                           uint32_t block_id) {
  for (auto &info : ctx.block_infos) {
    if (info.block_id == block_id)
      return info.abbrevs;
  }
  return {};
}

static uint32_t getFunctionParamCount(const LLVMModule &module, uint32_t type_id) {
  if (type_id >= module.types.size())
    return 0;
  if (module.types[type_id].kind == LLVMType::Pointer &&
      !module.types[type_id].type_refs.empty()) {
    type_id = module.types[type_id].type_refs[0];
    if (type_id >= module.types.size())
      return 0;
  }
  auto &type = module.types[type_id];
  if (type.kind != LLVMType::Function || type.type_refs.empty())
    return 0;
  return (uint32_t)type.type_refs.size() - 1;
}

static bool isFunctionTypeRef(const LLVMModule &module, uint32_t type_id) {
  if (type_id >= module.types.size())
    return false;
  if (module.types[type_id].kind == LLVMType::Function)
    return true;
  return module.types[type_id].kind == LLVMType::Pointer &&
         !module.types[type_id].type_refs.empty() &&
         module.types[type_id].type_refs[0] < module.types.size() &&
         module.types[module.types[type_id].type_refs[0]].kind == LLVMType::Function;
}

static const LLVMValue *findConstantById(const LLVMModule &module, uint32_t id) {
  for (auto &constant : module.constants) {
    if (constant.id == id)
      return &constant;
  }
  return nullptr;
}

static const LLVMValue *findFunctionConstantById(const LLVMFunction &fn, uint32_t id) {
  for (auto &constant : fn.constants) {
    if (constant.id == id)
      return &constant;
  }
  return nullptr;
}

static bool parseUnsignedConstant(const std::string &text, uint64_t &value) {
  if (text.empty())
    return false;
  char *end = nullptr;
  unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
  if (!end || *end != '\0')
    return false;
  value = (uint64_t)parsed;
  return true;
}

static bool getUnsignedConstantValue(const LLVMModule &module, const LLVMFunction &fn,
                                     uint32_t id, uint64_t &value) {
  if (auto constant = findFunctionConstantById(fn, id))
    return parseUnsignedConstant(constant->constant_data, value);
  if (auto constant = findConstantById(module, id))
    return parseUnsignedConstant(constant->constant_data, value);
  return false;
}

static bool dxilFunctionNameMatchesBase(const std::string &name, const char *base) {
  size_t base_len = std::strlen(base);
  return name.compare(0, base_len, base) == 0 &&
         (name.size() == base_len || name[base_len] == '.');
}

static uint32_t findVoidTypeId(const LLVMModule &module) {
  for (uint32_t i = 0; i < module.types.size(); i++)
    if (module.types[i].kind == LLVMType::Void)
      return i;
  return UINT32_MAX;
}

static bool dxilOpcodeMatchesFunctionName(uint64_t opcode, const std::string &name) {
  switch (opcode) {
  case 4: return dxilFunctionNameMatchesBase(name, "dx.op.loadInput");
  case 5: return dxilFunctionNameMatchesBase(name, "dx.op.storeOutput");
  case 8:
  case 9:
  case 10:
  case 11:
    return dxilFunctionNameMatchesBase(name, "dx.op.isSpecialFloat");
  case 13: return dxilFunctionNameMatchesBase(name, "dx.op.unary");
  case 14: return dxilFunctionNameMatchesBase(name, "dx.op.binary");
  case 15: return dxilFunctionNameMatchesBase(name, "dx.op.tertiary");
  case 54: return dxilFunctionNameMatchesBase(name, "dx.op.dot2");
  case 55: return dxilFunctionNameMatchesBase(name, "dx.op.dot3");
  case 56: return dxilFunctionNameMatchesBase(name, "dx.op.dot4");
  case 57: return dxilFunctionNameMatchesBase(name, "dx.op.createHandle");
  case 58: return dxilFunctionNameMatchesBase(name, "dx.op.cbufferLoad");
  case 59: return dxilFunctionNameMatchesBase(name, "dx.op.cbufferLoadLegacy");
  case 60: return dxilFunctionNameMatchesBase(name, "dx.op.sample") || dxilFunctionNameMatchesBase(name, "dx.op.textureSample");
  case 61: return dxilFunctionNameMatchesBase(name, "dx.op.sampleBias") || dxilFunctionNameMatchesBase(name, "dx.op.textureSampleBias");
  case 62: return dxilFunctionNameMatchesBase(name, "dx.op.sampleLevel") || dxilFunctionNameMatchesBase(name, "dx.op.textureSampleLevel");
  case 63: return dxilFunctionNameMatchesBase(name, "dx.op.sampleGrad") || dxilFunctionNameMatchesBase(name, "dx.op.textureSampleGrad");
  case 64: return dxilFunctionNameMatchesBase(name, "dx.op.sampleCmp") || dxilFunctionNameMatchesBase(name, "dx.op.textureSampleCmp");
  case 65: return dxilFunctionNameMatchesBase(name, "dx.op.sampleCmpLevelZero") || dxilFunctionNameMatchesBase(name, "dx.op.textureSampleCmpLevelZero");
  case 66: return dxilFunctionNameMatchesBase(name, "dx.op.textureLoad");
  case 67: return dxilFunctionNameMatchesBase(name, "dx.op.textureStore");
  case 68: return dxilFunctionNameMatchesBase(name, "dx.op.bufferLoad");
  case 69: return dxilFunctionNameMatchesBase(name, "dx.op.bufferStore");
  case 70: return dxilFunctionNameMatchesBase(name, "dx.op.bufferUpdateCounter");
  case 71: return dxilFunctionNameMatchesBase(name, "dx.op.checkAccessFullyMapped");
  case 72: return dxilFunctionNameMatchesBase(name, "dx.op.getDimensions");
  case 73: return dxilFunctionNameMatchesBase(name, "dx.op.textureGather") || dxilFunctionNameMatchesBase(name, "dx.op.gather");
  case 74: return dxilFunctionNameMatchesBase(name, "dx.op.textureGatherCmp") || dxilFunctionNameMatchesBase(name, "dx.op.gatherCmp");
  case 78: return dxilFunctionNameMatchesBase(name, "dx.op.atomicBinOp");
  case 79: return dxilFunctionNameMatchesBase(name, "dx.op.atomicCompareExchange");
  case 80: return dxilFunctionNameMatchesBase(name, "dx.op.barrier");
  case 81: return dxilFunctionNameMatchesBase(name, "dx.op.calculateLOD") || dxilFunctionNameMatchesBase(name, "dx.op.calcLOD");
  case 83: return dxilFunctionNameMatchesBase(name, "dx.op.derivCoarseX");
  case 84: return dxilFunctionNameMatchesBase(name, "dx.op.derivCoarseY");
  case 85: return dxilFunctionNameMatchesBase(name, "dx.op.derivFineX");
  case 86: return dxilFunctionNameMatchesBase(name, "dx.op.derivFineY");
  case 93: return dxilFunctionNameMatchesBase(name, "dx.op.threadId");
  case 94: return dxilFunctionNameMatchesBase(name, "dx.op.groupId");
  case 95: return dxilFunctionNameMatchesBase(name, "dx.op.threadIdInGroup");
  case 96: return dxilFunctionNameMatchesBase(name, "dx.op.flattenedThreadIdInGroup");
  case 110: return dxilFunctionNameMatchesBase(name, "dx.op.waveIsFirstLane");
  case 111: return dxilFunctionNameMatchesBase(name, "dx.op.waveGetLaneIndex");
  case 112: return dxilFunctionNameMatchesBase(name, "dx.op.waveGetLaneCount");
  case 113: return dxilFunctionNameMatchesBase(name, "dx.op.waveAnyTrue");
  case 114: return dxilFunctionNameMatchesBase(name, "dx.op.waveAllTrue");
  case 116: return dxilFunctionNameMatchesBase(name, "dx.op.waveActiveBallot");
  case 117: return dxilFunctionNameMatchesBase(name, "dx.op.waveReadLaneAt");
  case 118: return dxilFunctionNameMatchesBase(name, "dx.op.waveReadLaneFirst");
  case 119: return dxilFunctionNameMatchesBase(name, "dx.op.waveActiveOp");
  case 120: return dxilFunctionNameMatchesBase(name, "dx.op.waveActiveBit");
  case 121: return dxilFunctionNameMatchesBase(name, "dx.op.wavePrefixOp");
  case 135: return dxilFunctionNameMatchesBase(name, "dx.op.waveAllOp");
  case 122: return dxilFunctionNameMatchesBase(name, "dx.op.quadReadLaneAt");
  case 123: return dxilFunctionNameMatchesBase(name, "dx.op.quadOp");
  case 131: return dxilFunctionNameMatchesBase(name, "dx.op.legacyF16ToF32");
  case 132: return dxilFunctionNameMatchesBase(name, "dx.op.legacyF32ToF16");
  case 139: return dxilFunctionNameMatchesBase(name, "dx.op.rawBufferLoad");
  case 140: return dxilFunctionNameMatchesBase(name, "dx.op.rawBufferStore");
  case 160: return dxilFunctionNameMatchesBase(name, "dx.op.createHandleForLib");
  case 216: return dxilFunctionNameMatchesBase(name, "dx.op.annotateHandle");
  case 217: return dxilFunctionNameMatchesBase(name, "dx.op.createHandleFromBinding");
  case 218: return dxilFunctionNameMatchesBase(name, "dx.op.createHandleFromHeap");
  case 223: return dxilFunctionNameMatchesBase(name, "dx.op.textureGatherRaw") || dxilFunctionNameMatchesBase(name, "dx.op.gatherRaw");
  case 224: return dxilFunctionNameMatchesBase(name, "dx.op.textureSampleCmpLevel") || dxilFunctionNameMatchesBase(name, "dx.op.sampleCmpLevel");
  case 225: return dxilFunctionNameMatchesBase(name, "dx.op.textureStoreSample");
  default: return false;
  }
}

static bool dxilFunctionNameIsVoidOp(const std::string &name) {
  return name.find("Store") != std::string::npos ||
         name.find("store") != std::string::npos ||
         dxilFunctionNameMatchesBase(name, "dx.op.barrier") ||
         dxilFunctionNameMatchesBase(name, "dx.op.barrierByMemoryType") ||
         dxilFunctionNameMatchesBase(name, "dx.op.barrierByMemoryHandle") ||
         dxilFunctionNameMatchesBase(name, "dx.op.barrierByNodeRecordHandle") ||
         dxilFunctionNameMatchesBase(name, "dx.op.discard") ||
         dxilFunctionNameMatchesBase(name, "dx.op.emitStream") ||
         dxilFunctionNameMatchesBase(name, "dx.op.cutStream") ||
         dxilFunctionNameMatchesBase(name, "dx.op.emitThenCutStream") ||
         dxilFunctionNameMatchesBase(name, "dx.op.traceRay") ||
         dxilFunctionNameMatchesBase(name, "dx.op.callShader") ||
         dxilFunctionNameMatchesBase(name, "dx.op.dispatchMesh") ||
         dxilFunctionNameMatchesBase(name, "dx.op.setMeshOutputCounts") ||
         dxilFunctionNameMatchesBase(name, "dx.op.emitIndices") ||
         dxilFunctionNameMatchesBase(name, "dx.op.ignoreHit") ||
         dxilFunctionNameMatchesBase(name, "dx.op.acceptHitAndEndSearch") ||
         dxilFunctionNameMatchesBase(name, "dx.op.rayQuery_Abort") ||
         dxilFunctionNameMatchesBase(name, "dx.op.rayQuery_CommitNonOpaqueTriangleHit") ||
         dxilFunctionNameMatchesBase(name, "dx.op.rayQuery_CommitProceduralPrimitiveHit") ||
         dxilFunctionNameMatchesBase(name, "dx.op.rayQuery_TraceRayInline") ||
         dxilFunctionNameMatchesBase(name, "dx.op.writeSamplerFeedback") ||
         dxilFunctionNameMatchesBase(name, "dx.op.writeSamplerFeedbackBias") ||
         dxilFunctionNameMatchesBase(name, "dx.op.writeSamplerFeedbackGrad") ||
         dxilFunctionNameMatchesBase(name, "dx.op.writeSamplerFeedbackLevel") ||
         dxilFunctionNameMatchesBase(name, "dx.op.incrementOutputCount") ||
         dxilFunctionNameMatchesBase(name, "dx.op.outputComplete");
}

static const LLVMGlobal *findGlobalById(const LLVMModule &module, uint32_t id) {
  for (auto &global : module.globals) {
    if (global.value_id == id)
      return &global;
  }
  return nullptr;
}

static std::string recordString(const std::vector<uint64_t> &ops, size_t first) {
  std::string text;
  for (size_t i = first; i < ops.size(); i++) {
    if (ops[i] == 0)
      break;
    text.push_back((char)(ops[i] & 0xff));
  }
  return text;
}

static bool isPrintableString(const std::string &text) {
  if (text.empty())
    return false;
  for (char c : text) {
    unsigned char ch = (unsigned char)c;
    if (ch < 0x20 || ch >= 0x7f)
      return false;
  }
  return true;
}

static std::string formatFloatConstant(double value, bool suffix_f) {
  if (std::isnan(value))
    return "NAN";
  if (std::isinf(value))
    return std::signbit(value) ? "-INFINITY" : "INFINITY";

  char buf[64];
  snprintf(buf, sizeof(buf), suffix_f ? "%.9g" : "%.17g", value);
  std::string text = buf;
  if (text.find('.') == std::string::npos &&
      text.find('e') == std::string::npos &&
      text.find('E') == std::string::npos)
    text += ".0";
  if (suffix_f)
    text += 'f';
  return text;
}

struct SubBlockHeader {
  uint32_t block_id = 0;
  uint32_t new_abbrev_len = 0;
  uint32_t end_bit = 0;
};

static SubBlockHeader readSubBlockHeader(BitstreamReader &r) {
  SubBlockHeader header;
  header.block_id = r.readVBR(8);
  header.new_abbrev_len = r.readVBR(4);
  r.align32();
  uint32_t block_len = r.read(32);
  header.end_bit = r.tell() + block_len * 32;
  return header;
}

static void readAbbrevRecord(BitstreamReader &r, std::vector<Abbrev> &abbrevs) {
  Abbrev abbrev;
  uint32_t num_ops = r.readVBR(5);
  for (uint32_t i = 0; i < num_ops; i++) {
    Abbrev::Op op;
    op.literal = r.read(1) != 0;
    if (op.literal) {
      op.value = r.readVBR64(8);
    } else {
      op.encoding = r.read(3);
      if (op.encoding == 1 || op.encoding == 2)
        op.value = r.readVBR64(5);
    }
    abbrev.ops.push_back(op);
  }
  abbrevs.push_back(abbrev);
}

static uint64_t readAbbrevField(BitstreamReader &r, const Abbrev::Op &op) {
  switch (op.encoding) {
  case 1: return r.read64((uint32_t)op.value);
  case 2: return r.readVBR64((uint32_t)op.value);
  case 4: {
    static const char table[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._";
    return table[r.read(6) & 63];
  }
  default:
    return 0;
  }
}

static std::vector<uint64_t> readUnabbrevRecord(BitstreamReader &r) {
  uint32_t code = r.readVBR(6);
  uint32_t num_ops = r.readVBR(6);
  std::vector<uint64_t> ops;
  ops.push_back(code);
  for (uint32_t i = 0; i < num_ops; i++) {
    ops.push_back(r.readVBR64(6));
  }
  return ops;
}

static std::vector<uint64_t> readRecord(BitstreamReader &r, uint32_t code,
                                        const std::vector<Abbrev> &abbrevs) {
  if (code == kUnabbrevRecord)
    return readUnabbrevRecord(r);

  if (code < 4 || code - 4 >= abbrevs.size())
    return {};

  const Abbrev &abbrev = abbrevs[code - 4];
  std::vector<uint64_t> ops;
  for (size_t i = 0; i < abbrev.ops.size(); i++) {
    auto &op = abbrev.ops[i];
    if (op.literal) {
      ops.push_back(op.value);
      continue;
    }

    if (op.encoding == 3) {
      if (i + 1 >= abbrev.ops.size())
        break;
      uint32_t count = r.readVBR(6);
      auto &element = abbrev.ops[++i];
      for (uint32_t j = 0; j < count; j++)
        ops.push_back(readAbbrevField(r, element));
      continue;
    }

    if (op.encoding == 5) {
      uint32_t count = r.readVBR(6);
      r.align32();
      for (uint32_t j = 0; j < count; j++)
        ops.push_back(r.read(8));
      r.align32();
      continue;
    }

    ops.push_back(readAbbrevField(r, op));
  }
  return ops;
}

static bool parseBlockInfoBlock(ParseContext &ctx, uint32_t abbrev_len,
                                uint32_t end_bit) {
  static constexpr uint32_t kBlockInfoCodeSetBID = 1;
  uint32_t current_block_id = 0;

  while (!ctx.reader.atEnd() && ctx.reader.tell() < end_bit) {
    uint32_t code = ctx.reader.read(abbrev_len);
    if (code == kEndBlock) {
      ctx.reader.align32();
      return true;
    }
    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(ctx.reader);
      ctx.reader.seek(header.end_bit);
      continue;
    }
    if (code == kDefineAbbrev) {
      if (current_block_id) {
        auto *info = findOrCreateBlockInfo(ctx.block_infos, current_block_id);
        readAbbrevRecord(ctx.reader, info->abbrevs);
      } else {
        std::vector<Abbrev> ignored;
        readAbbrevRecord(ctx.reader, ignored);
      }
      continue;
    }

    auto ops = readRecord(ctx.reader, code, ctx.cur_abbrevs);
    if (ops.empty())
      continue;

    if ((uint32_t)ops[0] == kBlockInfoCodeSetBID && ops.size() > 1)
      current_block_id = (uint32_t)ops[1];
  }
  return false;
}

static std::string parseStringTableBlock(BitstreamReader &reader,
                                         uint32_t abbrev_len,
                                         uint32_t end_bit) {
  std::vector<Abbrev> abbrevs;
  std::string strtab;

  while (!reader.atEnd() && reader.tell() < end_bit) {
    uint32_t code = reader.read(abbrev_len);
    if (code == kEndBlock) {
      reader.align32();
      return strtab;
    }
    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(reader);
      reader.seek(header.end_bit);
      continue;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(reader, abbrevs);
      continue;
    }

    auto ops = readRecord(reader, code, abbrevs);
    if (ops.empty() || (uint32_t)ops[0] != kStrtabCode_Blob)
      continue;

    strtab.clear();
    strtab.reserve(ops.size() - 1);
    for (size_t i = 1; i < ops.size(); i++)
      strtab.push_back((char)(ops[i] & 0xff));
  }
  return strtab;
}

static void applyFunctionNamesFromStrtab(
    LLVMModule &module, const std::vector<FunctionNameRef> &name_refs,
    const std::string &strtab) {
  if (strtab.empty())
    return;

  for (auto &ref : name_refs) {
    if ((uint64_t)ref.offset + ref.size > strtab.size())
      continue;
    std::string name = strtab.substr(ref.offset, ref.size);
    if (!isPrintableString(name))
      continue;

    for (size_t i = 0; i < module.functions.size(); i++) {
      auto &fn = module.functions[i];
      if (fn.value_id == ref.value_id && fn.name.empty()) {
        fn.name = name;
        module.function_map[name] = i;
        DXTRACE("DXIL strtab function name: value=%u name=%s", ref.value_id,
                name.c_str());
      }
    }

    for (auto &gv : module.globals) {
      if (gv.value_id == ref.value_id && gv.name.empty()) {
        gv.name = name;
        DXTRACE("DXIL strtab global name: value=%u name=%s", gv.value_id, name.c_str());
      }
    }
  }
}

static bool parseTypeBlock(ParseContext &ctx, uint32_t abbrev_len, uint32_t end_bit) {

  while (!ctx.reader.atEnd() && ctx.reader.tell() < end_bit) {
    uint32_t code = ctx.reader.read(abbrev_len);
    if (code == kEndBlock) {
      ctx.reader.align32();
      return true;
    }
    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(ctx.reader);
      ctx.reader.seek(header.end_bit);
      continue;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(ctx.reader, ctx.cur_abbrevs);
      continue;
    }

    auto ops = readRecord(ctx.reader, code, ctx.cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];
    LLVMType t;
    t.kind = LLVMType::Void;

    switch (rec_code) {
    case kTypeCode_Void:
      t.kind = LLVMType::Void;
      ctx.module.types.push_back(t);
      break;
    case kTypeCode_Label:
    case kTypeCode_Metadata:
    case kTypeCode_Token:
      t.kind = LLVMType::Void;
      ctx.module.types.push_back(t);
      break;
    case kTypeCode_Float:
      t.kind = LLVMType::Float;
      t.bit_width = 32;
      ctx.module.types.push_back(t);
      break;
    case kTypeCode_Half:
    case kTypeCode_BFloat:
      t.kind = LLVMType::Float;
      t.bit_width = 16;
      ctx.module.types.push_back(t);
      break;
    case kTypeCode_Double:
      t.kind = LLVMType::Double;
      t.bit_width = 64;
      ctx.module.types.push_back(t);
      break;
    case kTypeCode_X86FP80:
    case kTypeCode_FP128:
    case kTypeCode_PPCFP128:
      t.kind = LLVMType::Double;
      t.bit_width = 128;
      ctx.module.types.push_back(t);
      break;
    case kTypeCode_Integer: {
      t.kind = LLVMType::Integer;
      t.bit_width = ops.size() > 1 ? (uint32_t)ops[1] : 32;
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_Pointer: {
      t.kind = LLVMType::Pointer;
      if (ops.size() > 1) {
        t.subtypes.push_back({LLVMType::Void, 0, 0, {}, {}});
        t.type_refs.push_back((uint32_t)ops[1]);
      }
      if (ops.size() > 2)
        t.address_space = (uint32_t)ops[2];
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_OpaquePointer: {
      t.kind = LLVMType::Pointer;
      if (ops.size() > 1)
        t.address_space = (uint32_t)ops[1];
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_Opaque:
    case kTypeCode_X86MMX:
    case kTypeCode_X86AMX:
    case kTypeCode_TargetType: {
      t.kind = LLVMType::Struct;
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_Byte: {
      t.kind = LLVMType::Integer;
      t.bit_width = ops.size() > 1 ? (uint32_t)ops[1] : 8;
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_StructAnon:
    case kTypeCode_StructNamed: {
      t.kind = LLVMType::Struct;
      size_t first_type = rec_code == kTypeCode_StructAnon ||
                              rec_code == kTypeCode_StructNamed
                          ? 2
                          : 1;
      for (size_t i = first_type; i < ops.size(); i++) {
        t.subtypes.push_back({LLVMType::Void, 0, 0, {}, {}});
        t.type_refs.push_back((uint32_t)ops[i]);
      }
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_StructName:
      break;
    case kTypeCode_Array: {
      t.kind = LLVMType::Array;
      t.bit_width = ops.size() > 1 ? (uint32_t)ops[1] : 0;
      if (ops.size() > 2)
        t.type_refs.push_back((uint32_t)ops[2]);
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_Vector: {
      t.kind = LLVMType::Vector;
      t.bit_width = ops.size() > 1 ? (uint32_t)ops[1] : 0;
      if (ops.size() > 2)
        t.type_refs.push_back((uint32_t)ops[2]);
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_Function: {
      t.kind = LLVMType::Function;
      t.bit_width = ops.size() > 1 ? (uint32_t)ops[1] : 0;
      for (size_t i = 2; i < ops.size(); i++) {
        t.subtypes.push_back({LLVMType::Void, 0, 0, {}, {}});
        t.type_refs.push_back((uint32_t)ops[i]);
      }
      ctx.module.types.push_back(t);
      break;
    }
    case kTypeCode_FunctionOld: {
      t.kind = LLVMType::Function;
      t.bit_width = ops.size() > 1 ? (uint32_t)ops[1] : 0;
      for (size_t i = 3; i < ops.size(); i++) {
        t.subtypes.push_back({LLVMType::Void, 0, 0, {}, {}});
        t.type_refs.push_back((uint32_t)ops[i]);
      }
      ctx.module.types.push_back(t);
      break;
    }
    default:
      break;
    }
  }
  return false;
}

static bool parseConstantsBlock(ParseContext &ctx, std::vector<LLVMValue> &target,
                                uint32_t &next_value_id,
                                uint32_t abbrev_len, uint32_t end_bit) {
  uint32_t cur_type = 0;
  while (!ctx.reader.atEnd() && ctx.reader.tell() < end_bit) {
    uint32_t code = ctx.reader.read(abbrev_len);
    if (code == kEndBlock) {
      ctx.reader.align32();
      return true;
    }
    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(ctx.reader);
      ctx.reader.seek(header.end_bit);
      continue;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(ctx.reader, ctx.cur_abbrevs);
      continue;
    }

    auto ops = readRecord(ctx.reader, code, ctx.cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];
    switch (rec_code) {
    case kConstantsCode_SetType:
      if (ops.size() > 1) cur_type = (uint32_t)ops[1];
      break;
    case kConstantsCode_Integer:
    case kConstantsCode_Float:
    case kConstantsCode_Null:
    case kConstantsCode_Undefined: {
      LLVMValue v;
      v.kind = LLVMValue::Constant;
      v.type_id = cur_type;
      v.id = next_value_id++;
      if (rec_code == kConstantsCode_Integer && ops.size() > 1) {
        v.constant_data = std::to_string(decodeSignedVBR(ops[1]));
      } else if (rec_code == kConstantsCode_Float && ops.size() > 1) {
        if (cur_type < ctx.module.types.size() &&
            ctx.module.types[cur_type].kind == LLVMType::Float) {
          float f;
          uint32_t raw = (uint32_t)ops[1];
          memcpy(&f, &raw, sizeof(f));
          v.constant_data = formatFloatConstant((double)f, true);
        } else if (cur_type < ctx.module.types.size() &&
                   ctx.module.types[cur_type].kind == LLVMType::Double) {
          double d;
          uint64_t raw = ops[1];
          memcpy(&d, &raw, sizeof(d));
          v.constant_data = formatFloatConstant(d, false);
        }
      } else if (rec_code == kConstantsCode_Null) {
        v.constant_data = "0";
      } else if (rec_code == kConstantsCode_Undefined) {
        v.constant_data = "0";
      }
      target.push_back(v);
      break;
    }
    case kConstantsCode_Aggregate: {
      LLVMValue v;
      v.kind = LLVMValue::Constant;
      v.type_id = cur_type;
      v.id = next_value_id++;
      v.constant_data = "agg(";
      for (size_t i = 1; i < ops.size(); i++) {
        if (i > 1)
          v.constant_data += ",";
        uint32_t value_id = (uint32_t)ops[i];
        auto constant = findConstantById(ctx.module, value_id);
        if (!constant) {
          for (auto &tc : target) {
            if (tc.id == value_id) { constant = &tc; break; }
          }
        }
        if (constant && !constant->constant_data.empty()) {
          v.constant_data += constant->constant_data;
        } else {
          v.constant_data += std::to_string(value_id);
        }
      }
      v.constant_data += ")";
      target.push_back(v);
      break;
    }
    case kConstantsCode_Cast:
    case kConstantsCode_GEP:
    case kConstantsCode_InBoundsGEP: {
      /*
       * Constant expressions consume value IDs just like scalar constants. DXIL
       * commonly emits an inbounds GEP constant for @dx.nothing loads; if we
       * ignore it, the function instruction_start_value is one slot too low and
       * every subsequent relative operand (including dx.op callees/constants)
       * resolves to the wrong value. Preserve the value-numbering even when the
       * expression itself is only needed as a harmless null/fallback pointer.
       */
      LLVMValue v;
      v.kind = LLVMValue::Constant;
      v.type_id = cur_type;
      v.id = next_value_id++;
      v.constant_data = "0";
      target.push_back(v);
      break;
    }
    default:
      break;
    }
  }
  return false;
}

static void setFunctionName(LLVMModule &module,
                            std::vector<PendingFunction> &pending_functions,
                            uint32_t value_id, const std::string &name) {
  if (name.empty())
    return;
  for (auto &pending : pending_functions) {
    if (pending.value_id == value_id) {
      pending.name = name;
      return;
    }
  }
  for (size_t i = 0; i < module.functions.size(); i++) {
    if (module.functions[i].value_id == value_id) {
      module.functions[i].name = name;
      module.function_map[name] = i;
      return;
    }
  }
}

static bool parseValueSymbolTable(ParseContext &ctx,
                                  std::vector<PendingFunction> &pending_functions,
                                  uint32_t abbrev_len, uint32_t end_bit) {
  while (!ctx.reader.atEnd() && ctx.reader.tell() < end_bit) {
    uint32_t code = ctx.reader.read(abbrev_len);
    if (code == kEndBlock) {
      ctx.reader.align32();
      return true;
    }
    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(ctx.reader);
      ctx.reader.seek(header.end_bit);
      continue;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(ctx.reader, ctx.cur_abbrevs);
      continue;
    }

    auto ops = readRecord(ctx.reader, code, ctx.cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];
    if (rec_code == kValueSymTabCode_Entry && ops.size() >= 3) {
      setFunctionName(ctx.module, pending_functions, (uint32_t)ops[1],
                      recordString(ops, 2));
    } else if (rec_code == kValueSymTabCode_FnEntry && ops.size() >= 4) {
      setFunctionName(ctx.module, pending_functions, (uint32_t)ops[1],
                      recordString(ops, 3));
    }
  }
  return false;
}

static bool parseFunctionBlock(ParseContext &ctx, LLVMFunction &fn,
                               uint32_t abbrev_len, uint32_t end_bit,
                               MetadataState *md_state = nullptr);
static bool parseMetadataAttachmentBlock(
    LLVMModule &module, BitstreamReader &reader, uint32_t abbrev_len,
    uint32_t end_bit, std::vector<Abbrev> &cur_abbrevs,
    const MetadataState &md_state);

static bool parseFunctionBlock(ParseContext &ctx, LLVMFunction &fn,
                               uint32_t abbrev_len, uint32_t end_bit,
                               MetadataState *md_state) {
  uint32_t cur_block = 0;
  uint32_t next_value = fn.instruction_start_value;

  std::unordered_set<uint32_t> function_declaration_values;
  for (const auto &dfn : ctx.module.functions)
    if (dfn.is_declaration)
      function_declaration_values.insert(dfn.value_id);

  auto isFunctionDeclarationValue = [&](uint32_t value_id) {
    return function_declaration_values.find(value_id) != function_declaration_values.end();
  };

  auto normalizeFunctionBodyValue = [&](uint32_t value_id, bool skip_declarations) {
    /*
     * DXIL function bodies encode ordinary operands in a body-local value
     * namespace.  For entry points that are not module value 0, that namespace
     * omits the current function's own module slot; map those operands back to
     * the module value IDs used by declarations/constants.  Non-callee operands
     * must also never resolve to dx.op declarations; if they do, the encoded
     * value is referring to the following constant/value slot instead. Call
     * callees intentionally bypass this normalization; opcode/type recovery
     * below preserves or recovers their declaration identity.
     */
    if (fn.value_id != 0 && value_id >= fn.value_id && value_id < fn.instruction_start_value)
      value_id++;
    if (skip_declarations) {
      while (value_id < fn.instruction_start_value && isFunctionDeclarationValue(value_id))
        value_id++;
    }
    return value_id;
  };

  auto value = [&](uint64_t encoded) {
    return normalizeFunctionBodyValue(decodeRelativeValue(encoded, next_value), true);
  };

  auto rawCalleeValue = [&](uint64_t encoded) {
    return decodeRelativeValue(encoded, next_value);
  };

  auto normalizedCalleeValue = [&](uint64_t encoded) {
    return normalizeFunctionBodyValue(decodeRelativeValue(encoded, next_value), false);
  };

  auto signedValue = [&](uint64_t encoded) {
    return normalizeFunctionBodyValue(decodeSignedRelativeValue(encoded, next_value), true);
  };

  auto inferValueType = [&](uint32_t value_id, size_t &slot,
                            const std::vector<uint64_t> &record,
                            uint32_t &type_id) {
    type_id = 0;
    if (value_id >= next_value && slot < record.size()) {
      type_id = (uint32_t)record[slot++];
    } else if (auto global = findGlobalById(ctx.module, value_id)) {
      type_id = global->type_id;
    } else if (auto constant = findConstantById(ctx.module, value_id)) {
      type_id = constant->type_id;
    } else {
      for (auto &c : fn.constants) {
        if (c.id == value_id) { type_id = c.type_id; break; }
      }
    }
  };

  auto valueTypePair = [&](const std::vector<uint64_t> &record, size_t &slot, uint32_t &type_id) {
    uint32_t value_id = slot < record.size() ? value(record[slot++]) : 0;
    inferValueType(value_id, slot, record, type_id);
    return value_id;
  };

  auto calleeValueTypePair = [&](const std::vector<uint64_t> &record, size_t &slot,
                                 uint32_t &type_id, uint32_t &normalized_value_id,
                                 uint32_t &normalized_type_id,
                                 size_t &callee_operand_end_slot) {
    if (slot >= record.size()) {
      type_id = 0;
      normalized_value_id = 0;
      normalized_type_id = 0;
      callee_operand_end_slot = slot;
      return 0u;
    }
    uint64_t encoded = record[slot++];
    callee_operand_end_slot = slot;
    uint32_t value_id = rawCalleeValue(encoded);
    normalized_value_id = normalizedCalleeValue(encoded);
    size_t raw_slot = slot;
    inferValueType(value_id, raw_slot, record, type_id);
    size_t normalized_slot = slot;
    inferValueType(normalized_value_id, normalized_slot, record, normalized_type_id);
    slot = raw_slot;
    return value_id;
  };

  auto memoryValueTypePair = [&](const std::vector<uint64_t> &record, size_t &slot, uint32_t &type_id) {
    if (slot >= record.size()) {
      type_id = 0;
      return 0u;
    }
    uint64_t raw = record[slot++];
    uint32_t raw_id = (uint32_t)raw;
    uint32_t value_id = findGlobalById(ctx.module, raw_id) ? raw_id : value(raw);
    inferValueType(value_id, slot, record, type_id);
    return value_id;
  };

  auto popValue = [&](const std::vector<uint64_t> &record, size_t &slot) {
    return slot < record.size() ? value(record[slot++]) : 0;
  };

  auto noteResult = [&]() {
    next_value++;
  };

  auto nextBlock = [&]() {
    if (cur_block + 1 < fn.blocks.size())
      cur_block++;
  };

  while (!ctx.reader.atEnd() && ctx.reader.tell() < end_bit) {
    uint32_t code = ctx.reader.read(abbrev_len);
    if (code == kEndBlock) {
      ctx.reader.align32();
      return true;
    }
    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(ctx.reader);
      if (header.block_id == kBlockID_Constants) {
        uint32_t function_next_value = next_value;
        ParseContext const_ctx{ctx.reader, ctx.module,
                               getBlockAbbrevs(ctx, header.block_id),
                               ctx.block_infos};
        parseConstantsBlock(const_ctx, fn.constants, function_next_value,
                            header.new_abbrev_len, header.end_bit);
        next_value = function_next_value;
        fn.instruction_start_value = next_value;
      } else if (header.block_id == kBlockID_MetadataAttachment &&
                 md_state && md_state->parsed) {
        std::vector<Abbrev> ma_abbrevs;
        parseMetadataAttachmentBlock(ctx.module, ctx.reader,
                                     header.new_abbrev_len, header.end_bit,
                                     ma_abbrevs, *md_state);
      } else {
        ctx.reader.seek(header.end_bit);
      }
      continue;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(ctx.reader, ctx.cur_abbrevs);
      continue;
    }

    auto ops = readRecord(ctx.reader, code, ctx.cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];

    switch (rec_code) {
    case kFuncCode_DeclareBlocks:
      fn.blocks.resize(ops.size() > 1 ? (size_t)ops[1] : 0);
      fn.block_value_ids.resize(fn.blocks.size());
      for (size_t i = 0; i < fn.blocks.size(); i++) {
        fn.block_value_ids[i] = (uint32_t)i;
      }
      fn.instruction_start_value = next_value;
      cur_block = 0;
      break;
    case kFuncCode_InstRet:
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Ret;
        fn.blocks[cur_block].instructions.push_back(inst);
        nextBlock();
      }
      break;
    case kFuncCode_InstBr: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Br;
        if (ops.size() == 2) {
          inst.operands.push_back((uint32_t)ops[1]);
        } else if (ops.size() >= 4) {
          inst.operands.push_back(value(ops[3]));
          inst.operands.push_back((uint32_t)ops[1]);
          inst.operands.push_back((uint32_t)ops[2]);
        }
        fn.blocks[cur_block].instructions.push_back(inst);
        nextBlock();
      }
      break;
    }
    case kFuncCode_InstCall: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Call;
        size_t slot = 1;
        uint32_t attributes = slot < ops.size() ? (uint32_t)ops[slot++] : 0;
        uint32_t cc_info = slot < ops.size() ? (uint32_t)ops[slot++] : 0;
        (void)attributes;

        if (((cc_info >> kCallFlag_FastMathFlags) & 1) && slot < ops.size())
          slot++;

        uint32_t function_type_id = 0;
        if (((cc_info >> kCallFlag_ExplicitType) & 1) && slot < ops.size())
          function_type_id = (uint32_t)ops[slot++];

        uint32_t callee_type_id = 0;
        uint32_t normalized_callee = 0;
        uint32_t normalized_callee_type_id = 0;
        size_t callee_operand_end_slot = slot;
        uint32_t callee = calleeValueTypePair(ops, slot, callee_type_id,
                                              normalized_callee,
                                              normalized_callee_type_id,
                                              callee_operand_end_slot);
        uint64_t dxop_opcode = 0;
        bool has_dxop_opcode = slot < ops.size() &&
          getUnsignedConstantValue(ctx.module, fn, value(ops[slot]), dxop_opcode);
        if (function_type_id) {
          const LLVMFunction *decoded_fn = nullptr;
          const LLVMFunction *typed_dxop_decl = nullptr;
          const LLVMFunction *opcode_dxop_decl = nullptr;
          bool typed_dxop_ambiguous = false;
          bool opcode_dxop_ambiguous = false;

          for (const auto &dfn : ctx.module.functions) {
            if (dfn.value_id == callee)
              decoded_fn = &dfn;
            if (dfn.is_declaration && dfn.type_id == function_type_id &&
                dfn.name.rfind("dx.op.", 0) == 0) {
              if (!typed_dxop_decl && !typed_dxop_ambiguous) {
                typed_dxop_decl = &dfn;
              } else {
                typed_dxop_decl = nullptr;
                typed_dxop_ambiguous = true;
              }
              if (has_dxop_opcode && dxilOpcodeMatchesFunctionName(dxop_opcode, dfn.name)) {
                if (!opcode_dxop_decl && !opcode_dxop_ambiguous) {
                  opcode_dxop_decl = &dfn;
                } else {
                  opcode_dxop_decl = nullptr;
                  opcode_dxop_ambiguous = true;
                }
              }
            }
          }

          bool decoded_is_dxop = decoded_fn && decoded_fn->is_declaration &&
            decoded_fn->name.rfind("dx.op.", 0) == 0;
          bool decoded_is_current_function = callee == fn.value_id ||
            (decoded_fn && decoded_fn->value_id == fn.value_id);
          const LLVMFunction *recovered_fn = nullptr;
          const char *recovery_reason = nullptr;
          if (opcode_dxop_decl &&
              (!decoded_fn || decoded_is_current_function ||
               (decoded_is_dxop && !dxilOpcodeMatchesFunctionName(dxop_opcode, decoded_fn->name)))) {
            recovered_fn = opcode_dxop_decl;
            recovery_reason = "explicit type/opcode";
          } else if (decoded_is_current_function && typed_dxop_decl) {
            recovered_fn = typed_dxop_decl;
            recovery_reason = "self-callee explicit type";
          } else if (!decoded_fn && callee == 0 && has_dxop_opcode && typed_dxop_decl) {
            recovered_fn = typed_dxop_decl;
            recovery_reason = "zero-callee explicit type";
          } else if (!decoded_fn && typed_dxop_decl &&
                     (callee >= fn.instruction_start_value || callee < fn.value_id)) {
            recovered_fn = typed_dxop_decl;
            recovery_reason = "non-function callee explicit type";
          }

          if (recovered_fn) {
            DXTRACE("DXIL call recovered callee by %s: raw_callee=%u fnty=%u opcode=%llu recovered=%u name=%s",
                    recovery_reason, callee, function_type_id,
                    (unsigned long long)(has_dxop_opcode ? dxop_opcode : 0),
                    recovered_fn->value_id, recovered_fn->name.c_str());
            callee = recovered_fn->value_id;
            slot = callee_operand_end_slot;
          } else if (!decoded_fn && normalized_callee != callee) {
            const LLVMFunction *normalized_fn = nullptr;
            for (const auto &dfn : ctx.module.functions) {
              if (dfn.value_id == normalized_callee) {
                normalized_fn = &dfn;
                break;
              }
            }
            if (normalized_fn) {
              DXTRACE("DXIL call using normalized non-dxop callee: raw_callee=%u normalized=%u name=%s",
                      callee, normalized_callee, normalized_fn->name.c_str());
              callee = normalized_callee;
              callee_type_id = normalized_callee_type_id;
            }
          }
        }

        uint32_t return_type_id = 0;
        size_t fixed_arg_count = 0;
        bool has_function_type = function_type_id < ctx.module.types.size() &&
          ctx.module.types[function_type_id].kind == LLVMType::Function &&
          !ctx.module.types[function_type_id].type_refs.empty();
        if (has_function_type) {
          auto &fn_type = ctx.module.types[function_type_id];
          return_type_id = fn_type.type_refs[0];
          fixed_arg_count = fn_type.type_refs.size() - 1;
        } else if (callee_type_id < ctx.module.types.size() &&
                   ctx.module.types[callee_type_id].kind == LLVMType::Pointer &&
                   !ctx.module.types[callee_type_id].type_refs.empty()) {
          uint32_t pointee_type_id = ctx.module.types[callee_type_id].type_refs[0];
          if (pointee_type_id < ctx.module.types.size() &&
              ctx.module.types[pointee_type_id].kind == LLVMType::Function &&
              !ctx.module.types[pointee_type_id].type_refs.empty()) {
            function_type_id = pointee_type_id;
            auto &fn_type = ctx.module.types[function_type_id];
            return_type_id = fn_type.type_refs[0];
            fixed_arg_count = fn_type.type_refs.size() - 1;
            has_function_type = true;
          }
        }

        std::string callee_name;
        for (const auto &dfn : ctx.module.functions) {
          if (dfn.value_id == callee) {
            callee_name = dfn.name;
            break;
          }
        }

        bool is_dxop = callee_name.rfind("dx.op.", 0) == 0;
        bool dxop_is_void = is_dxop && dxilFunctionNameIsVoidOp(callee_name);
        if (dxop_is_void) {
          uint32_t void_type_id = findVoidTypeId(ctx.module);
          if (void_type_id < ctx.module.types.size())
            return_type_id = void_type_id;
        } else if (return_type_id >= ctx.module.types.size() ||
                   ctx.module.types[return_type_id].kind == LLVMType::Void) {
          if (is_dxop) {
            uint32_t wanted_bits = callee_name.find(".i64") != std::string::npos ? 64u : 32u;
            for (uint32_t i = 0; i < ctx.module.types.size(); i++) {
              const auto &type = ctx.module.types[i];
              if (type.kind == LLVMType::Integer && type.bit_width == wanted_bits) {
                return_type_id = i;
                break;
              }
              if (return_type_id == 0 && type.kind == LLVMType::Float)
                return_type_id = i;
            }
          }
        }

        inst.type_id = return_type_id;
        inst.operands.push_back(callee);
        inst.operands.push_back(function_type_id);

        if (has_function_type) {
          for (size_t i = 0; i < fixed_arg_count && slot < ops.size(); i++, slot++)
            inst.operands.push_back(value(ops[slot]));
          while (slot < ops.size()) {
            uint32_t arg_type_id = 0;
            inst.operands.push_back(valueTypePair(ops, slot, arg_type_id));
          }
        } else {
          while (slot < ops.size())
            inst.operands.push_back(value(ops[slot++]));
        }

        DXTRACE("DXIL call: cc=0x%x fnty=%u ret=%u callee=%u args=%zu",
                cc_info, function_type_id, inst.type_id, callee,
                inst.operands.size() > 2 ? inst.operands.size() - 2 : 0);
        fn.blocks[cur_block].instructions.push_back(inst);
        if (inst.type_id < ctx.module.types.size() &&
            ctx.module.types[inst.type_id].kind != LLVMType::Void)
          noteResult();
      }
      break;
    }
    case kFuncCode_InstBinop: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        size_t slot = 1;
        uint32_t type_id = 0;
        uint32_t lhs = valueTypePair(ops, slot, type_id);
        uint32_t rhs = popValue(ops, slot);
        inst.opcode = slot < ops.size()
                          ? decodeBinopForType(ctx.module, (uint32_t)ops[slot], type_id)
                          : LLVMInstruction::Add;
        inst.type_id = type_id;
        inst.operands.push_back(lhs);
        inst.operands.push_back(rhs);
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstCast: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = ops.size() > 3 ? decodeCast((uint32_t)ops[3]) : LLVMInstruction::BitCast;
        if (ops.size() > 2)
          inst.type_id = (uint32_t)ops[2];
        if (ops.size() > 1)
          inst.operands.push_back(value(ops[1]));
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstGEP_OLD:
    case kFuncCode_InstInBoundsGEP_OLD:
    case kFuncCode_InstGEP: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::GetElementPtr;
        size_t first_operand = rec_code == kFuncCode_InstGEP ? 3 : 1;
        if (first_operand < ops.size())
          inst.operands.push_back(value(ops[first_operand]));
        for (size_t i = first_operand + 2; i < ops.size(); i += 2)
          inst.operands.push_back(value(ops[i]));
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstLoad: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Load;
        size_t slot = 1;
        uint32_t ptr_type_id = 0;
        inst.operands.push_back(memoryValueTypePair(ops, slot, ptr_type_id));
        if (slot + 3 == ops.size())
          inst.type_id = (uint32_t)ops[slot++];
        else if (ptr_type_id < ctx.module.types.size() &&
                 ctx.module.types[ptr_type_id].kind == LLVMType::Pointer &&
                 !ctx.module.types[ptr_type_id].type_refs.empty())
          inst.type_id = ctx.module.types[ptr_type_id].type_refs[0];
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstStore: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Store;
        size_t slot = 1;
        uint32_t ptr_type_id = 0;
        uint32_t ptr = memoryValueTypePair(ops, slot, ptr_type_id);
        uint32_t value_type_id = 0;
        uint32_t stored = valueTypePair(ops, slot, value_type_id);
        inst.operands.push_back(ptr);
        inst.operands.push_back(stored);
        fn.blocks[cur_block].instructions.push_back(inst);
      }
      break;
    }
    case kFuncCode_InstAtomicRMW: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::AtomicRMW;
        size_t slot = 1;
        uint32_t ptr_type_id = 0;
        uint32_t ptr = memoryValueTypePair(ops, slot, ptr_type_id);
        uint32_t stored = popValue(ops, slot);
        uint32_t op = slot < ops.size() ? (uint32_t)ops[slot++] : 0;
        inst.operands.push_back(ptr);
        inst.operands.push_back(stored);
        inst.operands.push_back(op);
        if (ptr_type_id < ctx.module.types.size() &&
            ctx.module.types[ptr_type_id].kind == LLVMType::Pointer &&
            !ctx.module.types[ptr_type_id].type_refs.empty())
          inst.type_id = ctx.module.types[ptr_type_id].type_refs[0];
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstSelect:
    case kFuncCode_InstVSelect: {
      if (cur_block < fn.blocks.size() && ops.size() >= 4) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Select;
        size_t slot = 1;
        uint32_t true_value = popValue(ops, slot);
        uint32_t false_value = popValue(ops, slot);
        uint32_t cond_type_id = 0;
        uint32_t cond = valueTypePair(ops, slot, cond_type_id);
        inst.operands.push_back(cond);
        inst.operands.push_back(true_value);
        inst.operands.push_back(false_value);
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstCmp:
    case kFuncCode_InstCmp2: {
      // CMP/CMP2 records are usually [opty, opval, opval, pred] after the
      // record code, but DXIL also appears in a compact legacy form
      // [opval, opval, pred].  Support both so the compare result consumes its
      // value slot and branches reference the i1 result instead of the lhs.
      if (cur_block < fn.blocks.size() && ops.size() >= 4) {
        LLVMInstruction inst;
        size_t slot = 1;
        uint32_t type_id = 0;
        uint32_t lhs = 0;
        uint32_t rhs = 0;
        uint32_t pred = 0;
        if (ops.size() >= 5) {
          type_id = (uint32_t)ops[slot++];
          lhs = popValue(ops, slot);
          rhs = popValue(ops, slot);
          pred = slot < ops.size() ? (uint32_t)ops[slot] : 0;
        } else {
          lhs = popValue(ops, slot);
          rhs = popValue(ops, slot);
          pred = slot < ops.size() ? (uint32_t)ops[slot] : 0;
          if (auto constant = findConstantById(ctx.module, lhs))
            type_id = constant->type_id;
          for (auto &c : fn.constants) {
            if (c.id == lhs) { type_id = c.type_id; break; }
          }
        }
        inst.opcode = decodeCmp(pred);
        inst.type_id = type_id;
        inst.operands.push_back(pred);
        inst.operands.push_back(lhs);
        inst.operands.push_back(rhs);
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstAlloca: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Alloca;
        if (ops.size() > 1) inst.type_id = (uint32_t)ops[1];
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstExtractElt: {
      if (cur_block < fn.blocks.size() && ops.size() >= 4) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::ExtractElement;
        inst.type_id = (uint32_t)ops[2];
        inst.operands.push_back(value(ops[1]));
        inst.operands.push_back(value(ops[3]));
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstInsertElt:
    case kFuncCode_InstShuffleVec: {
      if (cur_block < fn.blocks.size() && ops.size() >= 5) {
        LLVMInstruction inst;
        inst.opcode = rec_code == kFuncCode_InstInsertElt
                          ? LLVMInstruction::InsertElement
                          : LLVMInstruction::ShuffleVector;
        inst.type_id = (uint32_t)ops[2];
        inst.operands.push_back(value(ops[1]));
        inst.operands.push_back(value(ops[3]));
        inst.operands.push_back(value(ops[4]));
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstExtractVal:
    case kFuncCode_InstInsertVal:
    case kFuncCode_InstPHI: {
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = rec_code == kFuncCode_InstExtractVal
                          ? LLVMInstruction::ExtractValue
                          : rec_code == kFuncCode_InstInsertVal
                                ? LLVMInstruction::InsertValue
                                 : LLVMInstruction::PHI;
        if (rec_code == kFuncCode_InstExtractVal) {
          if (ops.size() > 1)
            inst.operands.push_back(value(ops[1]));
          for (size_t i = 2; i < ops.size(); i++)
            inst.operands.push_back((uint32_t)ops[i]);
        } else if (rec_code == kFuncCode_InstInsertVal) {
          if (ops.size() > 1)
            inst.operands.push_back(value(ops[1]));
          if (ops.size() > 2)
            inst.operands.push_back(value(ops[2]));
          for (size_t i = 3; i < ops.size(); i++)
            inst.operands.push_back((uint32_t)ops[i]);
        } else {
          if (ops.size() > 1)
            inst.type_id = (uint32_t)ops[1];
          for (size_t i = 2; i + 1 < ops.size(); i += 2) {
            inst.operands.push_back(signedValue(ops[i]));
            inst.operands.push_back((uint32_t)ops[i + 1]);
          }
        }
        fn.blocks[cur_block].instructions.push_back(inst);
        noteResult();
      }
      break;
    }
    case kFuncCode_InstUnreachable:
      if (cur_block < fn.blocks.size()) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Unreachable;
        fn.blocks[cur_block].instructions.push_back(inst);
        nextBlock();
      }
      break;
    case kFuncCode_InstSwitch: {
      if (cur_block < fn.blocks.size() && ops.size() >= 5) {
        LLVMInstruction inst;
        inst.opcode = LLVMInstruction::Switch;
        size_t slot = 1;
        uint32_t cond_type_id = 0;
        inst.operands.push_back(valueTypePair(ops, slot, cond_type_id));
        inst.operands.push_back((uint32_t)ops[slot++]);
        uint32_t num_cases = (uint32_t)ops[slot++];
        for (uint32_t i = 0; i < num_cases && slot + 1 < ops.size(); i++) {
          inst.operands.push_back((uint32_t)ops[slot++]);
          inst.operands.push_back((uint32_t)ops[slot++]);
        }
        fn.blocks[cur_block].instructions.push_back(inst);
        nextBlock();
      }
      break;
    }
    case kFuncCode_InstInvoke:
    default:
      break;
    }
  }
  return false;
}

static bool parseMetadataBlock(LLVMModule &module, BitstreamReader &reader,
                               uint32_t abbrev_len, uint32_t end_bit,
                               std::vector<Abbrev> &cur_abbrevs,
                               const std::vector<BlockInfo> &block_infos,
                               MetadataState &md_state) {
  auto abbrevs = getBlockAbbrevs(ParseContext{reader, module, {}, block_infos},
                                 kBlockID_Metadata);
  if (!abbrevs.empty())
    cur_abbrevs = abbrevs;

  while (!reader.atEnd() && reader.tell() < end_bit) {
    uint32_t code = reader.read(abbrev_len);
    if (code == kEndBlock) {
      reader.align32();
      break;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(reader, cur_abbrevs);
      continue;
    }
    if (code == kEnterSubBlock) {
      auto hdr = readSubBlockHeader(reader);
      reader.seek(hdr.end_bit);
      continue;
    }

    auto ops = readRecord(reader, code, cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];

    if (rec_code == kMetadataCode_Kind && ops.size() > 2) {
      std::string kind_name = recordString(ops, 2);
      uint32_t kind_id = (uint32_t)ops[1];
      md_state.kind_map[kind_name] = kind_id;
      DXTRACE("DXIL metadata kind: id=%u name=%s", kind_id, kind_name.c_str());
    } else if (rec_code == kMetadataCode_Value && ops.size() > 2) {
      MetadataValue mv;
      mv.type_id = (uint32_t)ops[1];
      mv.value = ops[2];
      md_state.values.push_back(mv);
    } else if (rec_code == kMetadataCode_Node) {
      MetadataNode node;
      for (size_t i = 1; i < ops.size(); i++) {
        uint32_t ref = (uint32_t)ops[i];
        if (ref != (uint32_t)-1)
          node.operands.push_back(ref);
      }
      md_state.nodes.push_back(node);
    }
  }

  auto it = md_state.kind_map.find("numthreads");
  if (it != md_state.kind_map.end()) {
    md_state.numthreads_kind = it->second;
    DXTRACE("DXIL numthreads kind_id=%u values=%zu nodes=%zu",
            md_state.numthreads_kind, md_state.values.size(),
            md_state.nodes.size());
  }

  md_state.parsed = true;
  return true;
}

static void resolveNumthreadsFromAttachment(
    LLVMModule &module, const std::vector<uint64_t> &ops,
    const MetadataState &md_state) {
  if (md_state.numthreads_kind == (uint32_t)-1)
    return;

  for (size_t i = 0; i + 1 < ops.size(); i += 2) {
    uint32_t kind = (uint32_t)ops[i];
    uint32_t md_idx = (uint32_t)ops[i + 1];
    if (kind == md_state.numthreads_kind && md_idx < md_state.nodes.size()) {
      const auto &node = md_state.nodes[md_idx];
      uint32_t xyz[3] = {1, 1, 1};
      for (size_t j = 0; j < 3 && j < node.operands.size(); j++) {
        uint32_t val_idx = node.operands[j];
        if (val_idx < md_state.values.size()) {
          xyz[j] = (uint32_t)md_state.values[val_idx].value;
        }
      }
      module.num_threads[0] = xyz[0] ? xyz[0] : 1;
      module.num_threads[1] = xyz[1] ? xyz[1] : 1;
      module.num_threads[2] = xyz[2] ? xyz[2] : 1;
      DXTRACE("DXIL numthreads from attachment: %u,%u,%u",
              module.num_threads[0], module.num_threads[1],
              module.num_threads[2]);
    }
  }
}

static bool parseMetadataAttachmentBlock(
    LLVMModule &module, BitstreamReader &reader, uint32_t abbrev_len,
    uint32_t end_bit, std::vector<Abbrev> &cur_abbrevs,
    const MetadataState &md_state) {
  while (!reader.atEnd() && reader.tell() < end_bit) {
    uint32_t code = reader.read(abbrev_len);
    if (code == kEndBlock) {
      reader.align32();
      break;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(reader, cur_abbrevs);
      continue;
    }
    if (code == kEnterSubBlock) {
      auto hdr = readSubBlockHeader(reader);
      reader.seek(hdr.end_bit);
      continue;
    }

    auto ops = readRecord(reader, code, cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];
    if (rec_code == 18 && ops.size() >= 3) {
      std::vector<uint64_t> attachment(ops.begin() + 1, ops.end());
      resolveNumthreadsFromAttachment(module, attachment, md_state);
    }
  }
  return true;
}

std::optional<LLVMModule> BitcodeReader::parse(const uint8_t *data, uint32_t size) {
  LLVMModule module;

  DXTRACE("BitcodeReader::parse size=%u", size);
  if (size >= 8) {
    DXTRACE("  bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
      data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
  }

  BitstreamReader reader(data, size);

  uint32_t magic = reader.read(32);
  if (magic != 0xDEC04342)
    return std::nullopt;

  // DXIL bitcode: after magic, the bitstream starts directly
  // No wrapper header — seek past the 4-byte magic
  reader.seek(32);

  SubBlockHeader module_header;
  bool found_module = false;
  std::vector<Abbrev> top_abbrevs;
  while (!reader.atEnd()) {
    uint32_t bc_abbrev = reader.read(2);
    DXTRACE("  bc_abbrev=%u at bit %u", bc_abbrev, reader.tell());
    if (bc_abbrev == kEndBlock) {
      reader.align32();
      break;
    }
    if (bc_abbrev == kDefineAbbrev) {
      readAbbrevRecord(reader, top_abbrevs);
      continue;
    }
    if (bc_abbrev != kEnterSubBlock)
      return std::nullopt;

    auto header = readSubBlockHeader(reader);
    DXTRACE("  top block=%u abbrev_len=%u", header.block_id,
            header.new_abbrev_len);
    if (header.block_id == kBlockID_Module) {
      module_header = header;
      found_module = true;
      break;
    }
    reader.seek(header.end_bit);
  }

  if (!found_module || !module_header.new_abbrev_len)
    return std::nullopt;

  std::vector<PendingFunction> pending_functions;
  std::vector<FunctionNameRef> function_name_refs;
  size_t next_function_body = 0;
  uint32_t next_module_value_id = 0;
  uint32_t next_function_value_id = 0;
  bool use_strtab_names = false;
  MetadataState md_state;

  ParseContext ctx{reader, module, {}, {}};

  while (!reader.atEnd() && reader.tell() < module_header.end_bit) {
    uint32_t code = reader.read(module_header.new_abbrev_len);
    if (code == kEndBlock) {
      reader.align32();
      break;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(reader, ctx.cur_abbrevs);
      continue;
    }

    if (code == kEnterSubBlock) {
      auto header = readSubBlockHeader(reader);

      switch (header.block_id) {
      case kBlockID_BlockInfo:
        parseBlockInfoBlock(ctx, header.new_abbrev_len, header.end_bit);
        break;
      case kBlockID_Type: {
        ParseContext type_ctx{reader, module,
                              getBlockAbbrevs(ctx, header.block_id),
                              ctx.block_infos};
        parseTypeBlock(type_ctx, header.new_abbrev_len, header.end_bit);
        break;
      }
      case kBlockID_Constants: {
        ParseContext const_ctx{reader, module,
                               getBlockAbbrevs(ctx, header.block_id),
                               ctx.block_infos};
        parseConstantsBlock(const_ctx, module.constants, next_module_value_id,
                            header.new_abbrev_len, header.end_bit);
        break;
      }
      case kBlockID_Function: {
        DXTRACE("DXIL function block: next=%zu pending=%zu bit=%u end=%u",
                next_function_body, pending_functions.size(), reader.tell(),
                header.end_bit);
        if (next_function_body < pending_functions.size()) {
          auto pending = pending_functions[next_function_body++];
          LLVMFunction fn;
          fn.value_id = pending.value_id;
          fn.type_id = pending.type_id;
          fn.param_count = pending.param_count;
          fn.name = pending.name;
          fn.instruction_start_value = next_module_value_id + fn.param_count;
          fn.is_declaration = false;
          ParseContext func_ctx{reader, module,
                                getBlockAbbrevs(ctx, header.block_id),
                                ctx.block_infos};
          parseFunctionBlock(func_ctx, fn, header.new_abbrev_len, header.end_bit, &md_state);
          module.functions.push_back(fn);
          DXTRACE("DXIL parsed function: value=%u name=%s blocks=%zu",
                  fn.value_id, fn.name.c_str(), fn.blocks.size());
          if (!fn.name.empty())
            module.function_map[fn.name] = module.functions.size() - 1;
        } else {
          reader.seek(header.end_bit);
        }
        break;
      }
      case kBlockID_ValueSymTab: {
        ParseContext vst_ctx{reader, module,
                             getBlockAbbrevs(ctx, header.block_id),
                             ctx.block_infos};
        parseValueSymbolTable(vst_ctx, pending_functions,
                              header.new_abbrev_len, header.end_bit);
        break;
      }
      case kBlockID_Metadata: {
        std::vector<Abbrev> md_abbrevs;
        parseMetadataBlock(module, reader, header.new_abbrev_len,
                           header.end_bit, md_abbrevs, ctx.block_infos,
                           md_state);
        break;
      }
      default:
        reader.seek(header.end_bit);
        break;
      }
      continue;
    }

    auto ops = readRecord(reader, code, ctx.cur_abbrevs);
    if (ops.empty())
      continue;

    uint32_t rec_code = (uint32_t)ops[0];
    if (rec_code == kModuleCode_Version) {
      if (ops.size() > 1)
        use_strtab_names = ops[1] >= 2;
      DXTRACE("DXIL module version=%llu use_strtab=%u",
              ops.size() > 1 ? (unsigned long long)ops[1] : 0,
              use_strtab_names ? 1 : 0);
    } else if (rec_code == kModuleCode_Triple) {
      auto triple = recordString(ops, 1);
      if (isPrintableString(triple)) {
        module.target_triple = triple;
        DXTRACE("DXIL module triple=%s", module.target_triple.c_str());
      }
    } else if (rec_code == kModuleCode_SourceFilename) {
      auto source = recordString(ops, 1);
      if (isPrintableString(source)) {
        module.source_filename = source;
        DXTRACE("DXIL module source=%s", module.source_filename.c_str());
      }
    } else if (rec_code == kModuleCode_VSTOffset && ops.size() > 1) {
      DXTRACE("DXIL module VST offset word=%llu",
              (unsigned long long)ops[1]);
    } else if (rec_code == kModuleCode_Function) {
      size_t record_base = 1;
      if (use_strtab_names && ops.size() > 5 &&
          isFunctionTypeRef(module, (uint32_t)ops[3]) && ops[5] <= 1)
        record_base = 3;
      uint32_t fn_type = ops.size() > record_base ? (uint32_t)ops[record_base] : 0;
      bool is_declaration = ops.size() > record_base + 2
                                ? ops[record_base + 2] != 0
                                : true;
      PendingFunction pending;
      pending.value_id = next_function_value_id++;
      if (next_module_value_id < next_function_value_id)
        next_module_value_id = next_function_value_id;
      pending.type_id = fn_type;
      pending.param_count = getFunctionParamCount(module, fn_type);
      if (use_strtab_names && record_base == 3 && ops.size() > 2) {
        function_name_refs.push_back(
            {pending.value_id, (uint32_t)ops[1], (uint32_t)ops[2]});
      }
      if (!is_declaration) {
        pending_functions.push_back(pending);
      } else {
        LLVMFunction decl;
        decl.value_id = pending.value_id;
        decl.type_id = fn_type;
        decl.param_count = pending.param_count;
        decl.is_declaration = true;
        module.functions.push_back(decl);
        if (!decl.name.empty())
          module.function_map[decl.name] = module.functions.size() - 1;
      }
      DXTRACE("DXIL module function: value=%u type=%u params=%u decl=%u pending=%zu",
              pending.value_id, pending.type_id, pending.param_count,
              is_declaration ? 1 : 0, pending_functions.size());
    } else if (rec_code == kModuleCode_GlobalVar) {
      LLVMGlobal gv;
      gv.value_id = next_function_value_id++;
      if (next_module_value_id < next_function_value_id)
        next_module_value_id = next_function_value_id;
      if (ops.size() > 1)
        gv.type_id = (uint32_t)ops[1];
      if (ops.size() > 2) {
        gv.is_constant = (ops[2] & 1) != 0;
        if ((ops[2] & 2) != 0)
          gv.address_space = (uint32_t)(ops[2] >> 2);
      }
      if (gv.address_space == 0 && gv.type_id < module.types.size() &&
          module.types[gv.type_id].kind == LLVMType::Pointer)
        gv.address_space = module.types[gv.type_id].address_space;
      if (use_strtab_names && ops.size() > 4) {
        function_name_refs.push_back(
            {gv.value_id, (uint32_t)ops[3], (uint32_t)ops[4]});
      }
      module.globals.push_back(gv);
      DXTRACE("DXIL module global: value=%u type=%u addr_space=%u",
              gv.value_id, gv.type_id, gv.address_space);
    }
  }

  while (!reader.atEnd()) {
    uint32_t code = reader.read(2);
    if (code == kEndBlock) {
      reader.align32();
      continue;
    }
    if (code == kDefineAbbrev) {
      readAbbrevRecord(reader, top_abbrevs);
      continue;
    }
    if (code != kEnterSubBlock) {
      auto ops = readRecord(reader, code, top_abbrevs);
      if (ops.empty())
        break;
      continue;
    }

    auto header = readSubBlockHeader(reader);
    if (header.block_id == kBlockID_Strtab) {
      auto strtab =
          parseStringTableBlock(reader, header.new_abbrev_len, header.end_bit);
      applyFunctionNamesFromStrtab(module, function_name_refs, strtab);
    } else if (header.block_id == kBlockID_Function &&
               next_function_body < pending_functions.size()) {
      DXTRACE("DXIL trailing function block: next=%zu pending=%zu bit=%u end=%u",
              next_function_body, pending_functions.size(), reader.tell(),
              header.end_bit);
      auto pending = pending_functions[next_function_body++];
      LLVMFunction fn;
      fn.value_id = pending.value_id;
      fn.type_id = pending.type_id;
      fn.param_count = pending.param_count;
      fn.name = pending.name;
      fn.instruction_start_value = next_module_value_id + fn.param_count;
      fn.is_declaration = false;
      ParseContext func_ctx{reader, module,
                            getBlockAbbrevs(ctx, header.block_id),
                            ctx.block_infos};
      parseFunctionBlock(func_ctx, fn, header.new_abbrev_len, header.end_bit, &md_state);
      module.functions.push_back(fn);
      DXTRACE("DXIL parsed trailing function: value=%u name=%s blocks=%zu",
              fn.value_id, fn.name.c_str(), fn.blocks.size());
      if (!fn.name.empty())
        module.function_map[fn.name] = module.functions.size() - 1;
    } else {
      reader.seek(header.end_bit);
    }
  }

  return module;
}

}
