#include "d3d12_pipeline_state.hpp"
#include "d3d12_device.hpp"
#include "d3d12_trace.hpp"
#include "log/log.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include "Metal.hpp"

#define PTRACE(fmt, ...)                                                       \
  do {                                                                         \
    FILE *_tf = fopen("Z:\\tmp\\dxmt_ps_args_debug.log", "a");                 \
    if (_tf) {                                                                 \
      fprintf(_tf, fmt "\n", ##__VA_ARGS__);                                   \
      fclose(_tf);                                                             \
    }                                                                          \
  } while (0)
#include "airconv_public.h"
#include "dxmt_format.hpp"
#include "dxil/dxil_container.hpp"
#include "dxil/llvm_bitcode.hpp"
#include "dxil/dxil_to_msl.hpp"
#include "../../libs/DXBCParser/BlobContainer.h"
#include "../../libs/DXBCParser/DXBCUtils.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <thread>
#include <unistd.h>
#include <vector>
#include <process.h>
#include <windows.h>

#define PSTRACE(fmt, ...) DXMTD3D12Trace("PSO", fmt, ##__VA_ARGS__)

namespace dxmt {

namespace {
constexpr uint32_t kMetalD3D12VertexBufferSlotCount = 29;
constexpr uint32_t kMetalShaderConverterStageInAttributeBase = 11;

bool DXMTD3D12AsyncPipelineCompileEnabled() {
  static int enabled = []() {
    const char *value = std::getenv("DXMT_ASYNC_PIPELINE_COMPILE");
    return value && value[0] && std::strcmp(value, "0") != 0;
  }();
  return enabled != 0;
}

unsigned DXMTD3D12AsyncPipelineWorkerCount() {
  static unsigned worker_count = []() {
    const char *value = std::getenv("DXMT_D3D12_PSO_WORKERS");
    if (value && value[0]) {
      long parsed = std::strtol(value, nullptr, 10);
      if (parsed <= 0)
        return 0u;
      return (unsigned)parsed;
    }

    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0)
      hw = 4;
    if (hw <= 2)
      return 1u;
    return std::min<unsigned>(8u, hw - 2);
  }();
  return worker_count;
}

bool DXMTD3D12GeometryMeshPipelineEnabled() {
  static int enabled = []() {
    const char *value = std::getenv("DXMT_D3D12_ENABLE_GEOMETRY_MESH");
    return value && value[0] && std::strcmp(value, "0") != 0;
  }();
  return enabled != 0;
}

class PipelineCompileScheduler {
public:
  PipelineCompileScheduler() {
    unsigned worker_count = DXMTD3D12AsyncPipelineWorkerCount();
    workers_.reserve(worker_count);
    for (unsigned i = 0; i < worker_count; i++) {
      workers_.emplace_back([this]() { WorkerMain(); });
    }
  }

  ~PipelineCompileScheduler() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto &worker : workers_) {
      if (worker.joinable())
        worker.join();
    }
  }

  bool Enqueue(MTLD3D12PipelineState *pso) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_ || workers_.empty())
      return false;
    queue_.push_back(pso);
    cv_.notify_one();
    return true;
  }

private:
  void WorkerMain();

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<MTLD3D12PipelineState *> queue_;
  bool stop_ = false;
  std::vector<std::thread> workers_;
};

PipelineCompileScheduler &GetPipelineCompileScheduler() {
  static PipelineCompileScheduler scheduler;
  return scheduler;
}

const char *DescribeShaderBlobMagic(const void *bytecode, SIZE_T size) {
  if (!bytecode || size < 4)
    return "too_small";

  uint32_t magic = *(const uint32_t *)bytecode;
  switch (magic) {
  case 0x43425844:
    return "DXBC";
  case 0x4C495844:
    return "DXIL";
  default:
    return "unknown";
  }
}

WMTBlendFactor D3D12BlendToWMT(D3D12_BLEND b) {
  switch (b) {
  case D3D12_BLEND_ZERO:
    return WMTBlendFactorZero;
  case D3D12_BLEND_ONE:
    return WMTBlendFactorOne;
  case D3D12_BLEND_SRC_COLOR:
    return WMTBlendFactorSourceColor;
  case D3D12_BLEND_INV_SRC_COLOR:
    return WMTBlendFactorOneMinusSourceColor;
  case D3D12_BLEND_SRC_ALPHA:
    return WMTBlendFactorSourceAlpha;
  case D3D12_BLEND_INV_SRC_ALPHA:
    return WMTBlendFactorOneMinusSourceAlpha;
  case D3D12_BLEND_DEST_ALPHA:
    return WMTBlendFactorDestinationAlpha;
  case D3D12_BLEND_INV_DEST_ALPHA:
    return WMTBlendFactorOneMinusDestinationAlpha;
  case D3D12_BLEND_DEST_COLOR:
    return WMTBlendFactorDestinationColor;
  case D3D12_BLEND_INV_DEST_COLOR:
    return WMTBlendFactorOneMinusDestinationColor;
  case D3D12_BLEND_SRC_ALPHA_SAT:
    return WMTBlendFactorSourceAlphaSaturated;
  case D3D12_BLEND_BLEND_FACTOR:
    return WMTBlendFactorBlendColor;
  case D3D12_BLEND_INV_BLEND_FACTOR:
    return WMTBlendFactorOneMinusBlendColor;
  default:
    return WMTBlendFactorOne;
  }
}

WMTBlendOperation D3D12BlendOpToWMT(D3D12_BLEND_OP op) {
  switch (op) {
  case D3D12_BLEND_OP_ADD:
    return WMTBlendOperationAdd;
  case D3D12_BLEND_OP_SUBTRACT:
    return WMTBlendOperationSubtract;
  case D3D12_BLEND_OP_REV_SUBTRACT:
    return WMTBlendOperationReverseSubtract;
  case D3D12_BLEND_OP_MIN:
    return WMTBlendOperationMin;
  case D3D12_BLEND_OP_MAX:
    return WMTBlendOperationMax;
  default:
    return WMTBlendOperationAdd;
  }
}

void TraceDxbcChunks(const void *bytecode, SIZE_T size, const char *label) {
  if (!bytecode || size < 32)
    return;

  const uint32_t *chunks = (const uint32_t *)bytecode;
  if (chunks[0] != 0x43425844)
    return;

  uint32_t container_size = chunks[6];
  uint32_t num_chunks = chunks[7];
  PSTRACE("%s DXBC: container_size=%u num_chunks=%u", label ? label : "blob",
          container_size, num_chunks);
  for (uint32_t i = 0; i < num_chunks && i < 16; i++) {
    uint32_t offset = chunks[8 + i];
    if (offset + 8 <= size) {
      char tag[5] = {};
      memcpy(tag, (const char *)bytecode + offset, 4);
      uint32_t chunk_size = 0;
      memcpy(&chunk_size, (const char *)bytecode + offset + 4,
             sizeof(chunk_size));
      PSTRACE("%s chunk[%u]: tag='%s' offset=%u size=%u",
              label ? label : "blob", i, tag, offset, chunk_size);
    }
  }
}

bool DxbcContainsSm50ShaderBlob(const void *bytecode, SIZE_T size) {
  if (!bytecode || size < 32)
    return false;

  const uint32_t *chunks = (const uint32_t *)bytecode;
  if (chunks[0] != 0x43425844)
    return false;

  uint32_t num_chunks = chunks[7];
  for (uint32_t i = 0; i < num_chunks && i < 64; i++) {
    uint32_t offset = chunks[8 + i];
    if (offset + 8 > size)
      continue;
    char tag[5] = {};
    memcpy(tag, (const char *)bytecode + offset, 4);
    if (std::strcmp(tag, "SHDR") == 0 || std::strcmp(tag, "SHEX") == 0)
      return true;
  }
  return false;
}

bool ShouldFallbackFromMetalShaderConverter(std::string_view fail_text,
                                            ShaderType type) {
  if (fail_text.find("Unrecognized DXIL program header") !=
      std::string_view::npos)
    return true;

  if (type != ShaderType::Compute)
    return false;

  // Treat converter-declared capability misses as authoritative. Falling back
  // to the internal DXIL->MSL path for these cases currently produces invalid
  // MSL and makes launch stability worse than a bounded PSO compile failure.
  if (fail_text.find("Wave size is not supported. Must be 32.") !=
          std::string_view::npos ||
      fail_text.find("dx.op.atomicBinOp.i32") != std::string_view::npos) {
    return false;
  }

  return true;
}

const std::string &GetShaderCacheRoot() {
  static const std::string root = []() {
    // Keep live DXIL/MSL dump artifacts under /tmp for the PE-side runtime.
    // This is the path that reliably works under Wine today and is what the
    // metal-shaderconverter sidecar consumes during live launches.
    return std::string("/tmp/dxmt_shader_cache");
  }();
  return root;
}

std::string BuildShaderCachePath(const char *suffix) {
  auto path = GetShaderCacheRoot();
  if (!path.ends_with('/'))
    path.push_back('/');
  path += suffix;
  return path;
}

void EnsureShaderCacheDir() {
  std::error_code ec;
  std::filesystem::create_directories(GetShaderCacheRoot(), ec);
}

void DumpShaderBlob(const char *path, const void *bytecode, SIZE_T size) {
  if (!path || !bytecode || !size)
    return;
  EnsureShaderCacheDir();
  FILE *df = fopen(path, "wb");
  if (df) {
    fwrite(bytecode, 1, size, df);
    fclose(df);
  }
}

void DumpShaderText(const char *path, const char *text) {
  if (!path || !text)
    return;
  EnsureShaderCacheDir();
  FILE *df = fopen(path, "w");
  if (df) {
    fputs(text, df);
    fclose(df);
  }
}

std::string ReadTextFile(const char *path) {
  if (!path)
    return {};

  FILE *file = fopen(path, "rb");
  if (!file)
    return {};

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    fclose(file);
    return {};
  }

  std::string text;
  text.resize((size_t)size);
  size_t read = fread(text.data(), 1, (size_t)size, file);
  fclose(file);
  text.resize(read);
  return text;
}

bool ExtractJsonStringValue(std::string_view text, const char *key,
                            std::string &out_value) {
  std::string key_token = str::format("\"", key, "\"");
  size_t key_pos = text.find(key_token);
  if (key_pos == std::string_view::npos)
    return false;

  size_t colon = text.find(':', key_pos + key_token.size());
  if (colon == std::string_view::npos)
    return false;

  size_t first_quote = text.find('"', colon + 1);
  if (first_quote == std::string_view::npos)
    return false;

  size_t second_quote = text.find('"', first_quote + 1);
  if (second_quote == std::string_view::npos || second_quote <= first_quote)
    return false;

  out_value.assign(
      text.substr(first_quote + 1, second_quote - first_quote - 1));
  return true;
}

bool ExtractJsonInt3Value(std::string_view text, const char *key, int &x,
                          int &y, int &z) {
  std::string key_token = str::format("\"", key, "\"");
  size_t key_pos = text.find(key_token);
  if (key_pos == std::string_view::npos)
    return false;

  size_t open = text.find('[', key_pos + key_token.size());
  size_t close = open == std::string_view::npos ? std::string_view::npos
                                                : text.find(']', open + 1);
  if (open == std::string_view::npos || close == std::string_view::npos ||
      close <= open)
    return false;

  std::string values(text.substr(open + 1, close - open - 1));
  return sscanf(values.c_str(), "%d , %d , %d", &x, &y, &z) == 3 ||
         sscanf(values.c_str(), "%d,%d,%d", &x, &y, &z) == 3;
}

bool ExtractJsonUIntValue(std::string_view text, const char *key,
                          uint32_t &out_value) {
  std::string key_token = str::format("\"", key, "\"");
  size_t key_pos = text.find(key_token);
  if (key_pos == std::string_view::npos)
    return false;

  size_t colon = text.find(':', key_pos + key_token.size());
  if (colon == std::string_view::npos)
    return false;

  const char *value_begin = text.data() + colon + 1;
  const char *text_end = text.data() + text.size();
  while (value_begin < text_end && std::isspace((unsigned char)*value_begin))
    value_begin++;

  char *end = nullptr;
  unsigned long parsed = std::strtoul(value_begin, &end, 10);
  if (end == value_begin)
    return false;

  out_value = (uint32_t)parsed;
  return true;
}

bool ParseAttributeRegisterName(std::string_view name, uint32_t &out_register) {
  static constexpr std::string_view kPrefix = "attribute";
  if (name.size() <= kPrefix.size() ||
      name.substr(0, kPrefix.size()) != kPrefix)
    return false;

  uint32_t value = 0;
  for (size_t i = kPrefix.size(); i < name.size(); i++) {
    unsigned char ch = (unsigned char)name[i];
    if (!std::isdigit(ch))
      return false;
    value = value * 10 + (uint32_t)(ch - '0');
  }

  out_register = value;
  return true;
}

bool StageInSemanticMatchesInputElement(
    const StageInVertexAttributeInfo &stage_in_attr,
    const D3D12_INPUT_ELEMENT_DESC &element) {
  if (!element.SemanticName || stage_in_attr.semantic_name.empty())
    return false;

  if (strcasecmp(element.SemanticName,
                 stage_in_attr.semantic_name.c_str()) == 0)
    return true;

  uint32_t reflected_semantic_index = 0;
  if (!ParseAttributeRegisterName(stage_in_attr.semantic_name,
                                  reflected_semantic_index))
    return false;

  return strcasecmp(element.SemanticName, "ATTRIBUTE") == 0 &&
         element.SemanticIndex == reflected_semantic_index;
}

WMTAttributeFormat ReflectionElementTypeToFormat(std::string_view element_type,
                                                 uint32_t column_count) {
  std::string normalized(element_type);
  for (char &ch : normalized)
    ch = (char)std::tolower((unsigned char)ch);

  if (column_count == 0) {
    if (!normalized.empty()) {
      char last = normalized.back();
      if (last >= '1' && last <= '4')
        column_count = (uint32_t)(last - '0');
    }
    if (column_count == 0)
      column_count = 1;
  }

  const bool is_float = normalized == "float" || normalized == "float32" ||
                        normalized == "single" || normalized == "half" ||
                        normalized == "float16";
  const bool is_half = normalized == "half" || normalized == "float16";
  const bool is_uint = normalized == "uint" || normalized == "uint32" ||
                       normalized == "unsignedint" ||
                       normalized == "unsignedint32";
  const bool is_sint = normalized == "int" || normalized == "int32" ||
                       normalized == "sint" || normalized == "sint32";

  switch (column_count) {
  case 1:
    if (is_half)
      return WMTAttributeFormatHalf;
    if (is_float)
      return WMTAttributeFormatFloat;
    if (is_uint)
      return WMTAttributeFormatUInt;
    if (is_sint)
      return WMTAttributeFormatInt;
    break;
  case 2:
    if (is_half)
      return WMTAttributeFormatHalf2;
    if (is_float)
      return WMTAttributeFormatFloat2;
    if (is_uint)
      return WMTAttributeFormatUInt2;
    if (is_sint)
      return WMTAttributeFormatInt2;
    break;
  case 3:
    if (is_half)
      return WMTAttributeFormatHalf3;
    if (is_float)
      return WMTAttributeFormatFloat3;
    if (is_uint)
      return WMTAttributeFormatUInt3;
    if (is_sint)
      return WMTAttributeFormatInt3;
    break;
  case 4:
    if (is_half)
      return WMTAttributeFormatHalf4;
    if (is_float)
      return WMTAttributeFormatFloat4;
    if (is_uint)
      return WMTAttributeFormatUInt4;
    if (is_sint)
      return WMTAttributeFormatInt4;
    break;
  default:
    break;
  }

  return WMTAttributeFormatInvalid;
}

bool BufferContainsAsciiToken(const void *data, SIZE_T size,
                              std::string_view token) {
  if (!data || !size || token.empty() || token.size() > size)
    return false;

  const char *bytes = (const char *)data;
  for (size_t i = 0; i + token.size() <= size; i++) {
    if (memcmp(bytes + i, token.data(), token.size()) == 0)
      return true;
  }
  return false;
}

bool InferGeometryPassThroughFromDxbcSignatures(
    const void *bytecode, SIZE_T size, uint32_t &out_passthrough,
    std::string &reason) {
  using namespace microsoft;

  out_passthrough = ~0u;
  reason.clear();

  CSignatureParser input_parser;
  CSignatureParser5 output_parser;
  HRESULT input_hr = DXBCGetInputSignature(bytecode, &input_parser);
  HRESULT output_hr = DXBCGetOutputSignature(bytecode, &output_parser);
  if (FAILED(input_hr) || FAILED(output_hr) || !output_parser.RastSignature()) {
    reason = str::format("sig hr in=0x", str::format("%08lx", (unsigned long)input_hr),
                         " out=0x", str::format("%08lx", (unsigned long)output_hr));
    return false;
  }

  if (output_parser.NumStreams() > 1 || output_parser.RasterizedStream()) {
    reason = str::format("streams=", output_parser.NumStreams(),
                         " rasterized=", output_parser.RasterizedStream());
    return false;
  }

  const D3D11_SIGNATURE_PARAMETER *input_params = nullptr;
  const D3D11_SIGNATURE_PARAMETER *output_params = nullptr;
  size_t num_input = input_parser.GetParameters(&input_params);
  size_t num_output = output_parser.Signature(0)->GetParameters(&output_params);
  MTL_GEOMETRY_SHADER_PASS_THROUGH data = {};
  data.RenderTargetArrayIndexComponent = 255;
  data.RenderTargetArrayIndexReg = 255;
  data.ViewportArrayIndexComponent = 255;
  data.ViewportArrayIndexReg = 255;

  bool saw_rt_array = false;
  bool saw_viewport_array = false;

  auto signature_output_matches_input = [](const D3D11_SIGNATURE_PARAMETER &out,
                                           const D3D11_SIGNATURE_PARAMETER &in) {
    if ((out.Mask & in.Mask) != out.Mask)
      return false;

    if (out.SystemValue != D3D10_SB_NAME_UNDEFINED)
      return out.SystemValue == in.SystemValue;

    return out.SemanticIndex == in.SemanticIndex && out.SemanticName &&
           in.SemanticName && strcasecmp(out.SemanticName, in.SemanticName) == 0;
  };

  for (size_t i = 0; i < num_output; i++) {
    bool matched = false;
    const D3D11_SIGNATURE_PARAMETER &out = output_params[i];
    if (out.SystemValue == D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX) {
      data.RenderTargetArrayIndexReg = out.Register;
      data.RenderTargetArrayIndexComponent = __builtin_ctz(out.Mask);
      saw_rt_array = true;
      continue;
    }
    if (out.SystemValue == D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX) {
      data.ViewportArrayIndexReg = out.Register;
      data.ViewportArrayIndexComponent = __builtin_ctz(out.Mask);
      saw_viewport_array = true;
      continue;
    }

    for (size_t j = 0; j < num_input; j++) {
      const D3D11_SIGNATURE_PARAMETER &in = input_params[j];
      if (signature_output_matches_input(out, in)) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      reason = str::format("unmatched output reg=", out.Register,
                           " sys=", (unsigned)out.SystemValue, " sem=",
                           out.SemanticName ? out.SemanticName : "?",
                           out.SemanticIndex, " mask=", (unsigned)out.Mask);
      return false;
    }
  }

  if (!saw_rt_array && !saw_viewport_array) {
    reason = "no layer system-value passthrough outputs";
    return false;
  }

  if (!BufferContainsAsciiToken(bytecode, size, "WriteToSliceMainGS") &&
      !BufferContainsAsciiToken(bytecode, size, "SV_RenderTargetArrayIndex")) {
    reason = "signature match but no known slice marker";
    return false;
  }

  memcpy(&out_passthrough, &data, sizeof(out_passthrough));
  reason = str::format("rtai_reg=", (unsigned)data.RenderTargetArrayIndexReg,
                       " rtai_comp=", (unsigned)data.RenderTargetArrayIndexComponent,
                       " vpai_reg=", (unsigned)data.ViewportArrayIndexReg,
                       " vpai_comp=", (unsigned)data.ViewportArrayIndexComponent);
  return true;
}

std::vector<StageInVertexAttributeInfo>
ParseVertexInputReflection(std::string_view text) {
  std::vector<StageInVertexAttributeInfo> attributes;

  size_t key_pos = text.find("\"vertex_inputs\"");
  if (key_pos == std::string_view::npos)
    return attributes;

  size_t array_open = text.find('[', key_pos);
  size_t array_close = array_open == std::string_view::npos
                           ? std::string_view::npos
                           : text.find(']', array_open + 1);
  if (array_open == std::string_view::npos ||
      array_close == std::string_view::npos || array_close <= array_open)
    return attributes;

  size_t cursor = array_open + 1;
  while (cursor < array_close) {
    size_t object_open = text.find('{', cursor);
    if (object_open == std::string_view::npos || object_open >= array_close)
      break;

    size_t object_close = text.find('}', object_open + 1);
    if (object_close == std::string_view::npos || object_close > array_close)
      break;

    std::string_view object =
        text.substr(object_open, object_close - object_open + 1);
    std::string name;
    if (ExtractJsonStringValue(object, "name", name)) {
      uint32_t register_index = 0;
      if (ParseAttributeRegisterName(name, register_index)) {
        StageInVertexAttributeInfo reflected = {};
        reflected.register_index = register_index;
        reflected.semantic_name = name;

        size_t index_pos = object.find("\"index\"");
        if (index_pos != std::string_view::npos) {
          size_t colon = object.find(':', index_pos + 7);
          if (colon != std::string_view::npos) {
            const char *value_begin = object.data() + colon + 1;
            while ((size_t)(value_begin - object.data()) < object.size() &&
                   std::isspace((unsigned char)*value_begin))
              value_begin++;
            char *end = nullptr;
            unsigned long parsed = std::strtoul(value_begin, &end, 10);
            if (end != value_begin)
              reflected.attribute_index =
                  kMetalShaderConverterStageInAttributeBase + (uint32_t)parsed;
          }
        }

        uint32_t column_count = 0;
        ExtractJsonUIntValue(object, "columnCount", column_count) ||
            ExtractJsonUIntValue(object, "columns", column_count) ||
            ExtractJsonUIntValue(object, "componentCount", column_count) ||
            ExtractJsonUIntValue(object, "components", column_count) ||
            ExtractJsonUIntValue(object, "vectorSize", column_count);

        std::string element_type;
        ExtractJsonStringValue(object, "elementType", element_type) ||
            ExtractJsonStringValue(object, "componentType", element_type) ||
            ExtractJsonStringValue(object, "scalarType", element_type) ||
            ExtractJsonStringValue(object, "type", element_type);
        reflected.format =
            ReflectionElementTypeToFormat(element_type, column_count);
        attributes.push_back(reflected);
      }
    }

    cursor = object_close + 1;
  }

  return attributes;
}

bool ParseMetalShaderConverterArgumentType(std::string_view type,
                                           SM50BindingType &out_type) {
  if (type == "CBV" || type == "ConstantBuffer") {
    out_type = SM50BindingType::ConstantBuffer;
    return true;
  }
  if (type == "Sampler") {
    out_type = SM50BindingType::Sampler;
    return true;
  }
  if (type == "SRV") {
    out_type = SM50BindingType::SRV;
    return true;
  }
  if (type == "UAV") {
    out_type = SM50BindingType::UAV;
    return true;
  }
  return false;
}

std::vector<MTL_SM50_SHADER_ARGUMENT>
ParseTopLevelArgumentBufferReflection(std::string_view text,
                                      MTL_SHADER_REFLECTION &reflection) {
  std::vector<MTL_SM50_SHADER_ARGUMENT> args;

  size_t key_pos = text.find("\"TopLevelArgumentBuffer\"");
  if (key_pos == std::string_view::npos)
    return args;

  size_t array_open = text.find('[', key_pos);
  size_t array_close = array_open == std::string_view::npos
                           ? std::string_view::npos
                           : text.find(']', array_open + 1);
  if (array_open == std::string_view::npos ||
      array_close == std::string_view::npos || array_close <= array_open)
    return args;

  uint32_t max_qword = 0;
  size_t cursor = array_open + 1;
  while (cursor < array_close) {
    size_t object_open = text.find('{', cursor);
    if (object_open == std::string_view::npos || object_open >= array_close)
      break;

    size_t object_close = text.find('}', object_open + 1);
    if (object_close == std::string_view::npos || object_close > array_close)
      break;

    std::string_view object =
        text.substr(object_open, object_close - object_open + 1);
    std::string type_name;
    uint32_t slot = 0, space = 0, elt_offset = 0, size = 0;
    SM50BindingType type = SM50BindingType::SRV;
    if (ExtractJsonStringValue(object, "Type", type_name) &&
        ParseMetalShaderConverterArgumentType(type_name, type) &&
        ExtractJsonUIntValue(object, "Slot", slot) &&
        ExtractJsonUIntValue(object, "EltOffset", elt_offset)) {
      ExtractJsonUIntValue(object, "Space", space);
      ExtractJsonUIntValue(object, "Size", size);

      MTL_SM50_SHADER_ARGUMENT arg = {};
      arg.Type = type;
      arg.SM50BindingSlot = slot;
      arg.SM50RegisterSpace = space;
      arg.StructurePtrOffset = elt_offset / 8;
      arg.Flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0;
      args.push_back(arg);

      uint32_t entry_qwords = (size + 7) / 8;
      if (entry_qwords == 0)
        entry_qwords = type == SM50BindingType::Sampler ? 3 : 2;
      max_qword = std::max(max_qword, arg.StructurePtrOffset + entry_qwords);

      switch (type) {
      case SM50BindingType::ConstantBuffer:
        if (slot < 16)
          reflection.ConstantBufferSlotMask |= (uint16_t)(1u << slot);
        break;
      case SM50BindingType::Sampler:
        if (slot < 16)
          reflection.SamplerSlotMask |= (uint16_t)(1u << slot);
        break;
      case SM50BindingType::SRV:
        if (slot < 64)
          reflection.SRVSlotMaskLo |= 1ull << slot;
        else if (slot < 128)
          reflection.SRVSlotMaskHi |= 1ull << (slot - 64);
        break;
      case SM50BindingType::UAV:
        if (slot < 64)
          reflection.UAVSlotMask |= 1ull << slot;
        break;
      }
    }

    cursor = object_close + 1;
  }

  if (!args.empty()) {
    reflection.NumArguments = (uint32_t)args.size();
    reflection.ArgumentBufferBindIndex = 30;
    reflection.ArgumentTableQwords = max_qword;
  }

  return args;
}

uint32_t AlignD3D12InputOffset(uint32_t offset, uint32_t size);

const char *DXGIFormatToMetalShaderConverterName(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
    return "R32G32B32A32_FLOAT";
  case DXGI_FORMAT_R32G32B32A32_UINT:
    return "R32G32B32A32_UINT";
  case DXGI_FORMAT_R32G32B32A32_SINT:
    return "R32G32B32A32_SINT";
  case DXGI_FORMAT_R32G32B32_FLOAT:
    return "R32G32B32_FLOAT";
  case DXGI_FORMAT_R32G32B32_UINT:
    return "R32G32B32_UINT";
  case DXGI_FORMAT_R32G32B32_SINT:
    return "R32G32B32_SINT";
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return "R16G16B16A16_FLOAT";
  case DXGI_FORMAT_R16G16B16A16_UNORM:
    return "R16G16B16A16_UNORM";
  case DXGI_FORMAT_R16G16B16A16_UINT:
    return "R16G16B16A16_UINT";
  case DXGI_FORMAT_R16G16B16A16_SNORM:
    return "R16G16B16A16_SNORM";
  case DXGI_FORMAT_R16G16B16A16_SINT:
    return "R16G16B16A16_SINT";
  case DXGI_FORMAT_R32G32_FLOAT:
    return "R32G32_FLOAT";
  case DXGI_FORMAT_R32G32_UINT:
    return "R32G32_UINT";
  case DXGI_FORMAT_R32G32_SINT:
    return "R32G32_SINT";
  case DXGI_FORMAT_R10G10B10A2_UNORM:
    return "R10G10B10A2_UNORM";
  case DXGI_FORMAT_R10G10B10A2_UINT:
    return "R10G10B10A2_UINT";
  case DXGI_FORMAT_R11G11B10_FLOAT:
    return "R11G11B10_FLOAT";
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return "R8G8B8A8_UNORM_SRGB";
  case DXGI_FORMAT_R8G8B8A8_UINT:
    return "R8G8B8A8_UINT";
  case DXGI_FORMAT_R8G8B8A8_SNORM:
    return "R8G8B8A8_SNORM";
  case DXGI_FORMAT_R8G8B8A8_SINT:
    return "R8G8B8A8_SINT";
  case DXGI_FORMAT_R16G16_FLOAT:
    return "R16G16_FLOAT";
  case DXGI_FORMAT_R16G16_UNORM:
    return "R16G16_UNORM";
  case DXGI_FORMAT_R16G16_UINT:
    return "R16G16_UINT";
  case DXGI_FORMAT_R16G16_SNORM:
    return "R16G16_SNORM";
  case DXGI_FORMAT_R16G16_SINT:
    return "R16G16_SINT";
  case DXGI_FORMAT_R32_FLOAT:
    return "R32_FLOAT";
  case DXGI_FORMAT_R32_UINT:
    return "R32_UINT";
  case DXGI_FORMAT_R32_SINT:
    return "R32_SINT";
  case DXGI_FORMAT_R8G8_UNORM:
    return "R8G8_UNORM";
  case DXGI_FORMAT_R8G8_UINT:
    return "R8G8_UINT";
  case DXGI_FORMAT_R8G8_SNORM:
    return "R8G8_SNORM";
  case DXGI_FORMAT_R8G8_SINT:
    return "R8G8_SINT";
  case DXGI_FORMAT_R16_FLOAT:
    return "R16_FLOAT";
  case DXGI_FORMAT_R16_UNORM:
    return "R16_UNORM";
  case DXGI_FORMAT_R16_UINT:
    return "R16_UINT";
  case DXGI_FORMAT_R16_SNORM:
    return "R16_SNORM";
  case DXGI_FORMAT_R16_SINT:
    return "R16_SINT";
  case DXGI_FORMAT_R8_UNORM:
    return "R8_UNORM";
  case DXGI_FORMAT_R8_UINT:
    return "R8_UINT";
  case DXGI_FORMAT_R8_SNORM:
    return "R8_SNORM";
  case DXGI_FORMAT_R8_SINT:
    return "R8_SINT";
  default:
    return nullptr;
  }
}

std::string EscapeJsonString(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char ch : value) {
    if (ch == '"' || ch == '\\')
      escaped.push_back('\\');
    escaped.push_back(ch);
  }
  return escaped;
}

std::string HexHash(size_t hash) {
  char text[32] = {};
  snprintf(text, sizeof(text), "%016zx", hash);
  return text;
}

bool WriteMetalShaderConverterVertexLayout(
    MTLD3D12Device *device, const D3D12_INPUT_LAYOUT_DESC &input_layout,
    const char *path) {
  if (!path || !input_layout.NumElements || !input_layout.pInputElementDescs)
    return false;

  FILE *file = fopen(path, "w");
  if (!file)
    return false;

  uint32_t append_offset[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
  fprintf(file, "{\"InputElements\":[");
  for (UINT i = 0; i < input_layout.NumElements; i++) {
    const auto &el = input_layout.pInputElementDescs[i];
    const char *format_name = DXGIFormatToMetalShaderConverterName(el.Format);
    MTL_DXGI_FORMAT_DESC metal_format = {};
    uint32_t byte_size = 16;
    if (SUCCEEDED(MTLQueryDXGIFormat(device->GetMTLDevice(), el.Format,
                                     metal_format)) &&
        metal_format.BytesPerTexel)
      byte_size = metal_format.BytesPerTexel;

    uint32_t aligned_offset =
        el.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
            ? AlignD3D12InputOffset(append_offset[el.InputSlot], byte_size)
            : el.AlignedByteOffset;
    if (el.InputSlot < WMT_MAX_VERTEX_BUFFER_LAYOUTS)
      append_offset[el.InputSlot] = aligned_offset + byte_size;

    if (i)
      fprintf(file, ",");
    fprintf(file,
            "{\"AlignedByteOffset\":%u,\"Format\":\"%s\",\"InputSlot\":%u,"
            "\"InputSlotClass\":\"%s\",\"InstanceDataStepRate\":%u,"
            "\"SemanticIndex\":%u}",
            aligned_offset, format_name ? format_name : "R32G32B32A32_FLOAT",
            el.InputSlot,
            el.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                ? "PerInstanceData"
                : "PerVertexData",
            el.InstanceDataStepRate, el.SemanticIndex);
  }
  fprintf(file, "],\"SemanticNames\":[");
  for (UINT i = 0; i < input_layout.NumElements; i++) {
    const auto &el = input_layout.pInputElementDescs[i];
    if (i)
      fprintf(file, ",");
    auto semantic = EscapeJsonString(el.SemanticName ? el.SemanticName : "");
    fprintf(file, "\"%s\"", semantic.c_str());
  }
  fprintf(file, "]}");
  fclose(file);
  return true;
}

const char *DxilShaderKindName(dxmt::dxil::DxilShaderKind kind) {
  switch (kind) {
  case dxmt::dxil::DxilShaderKind::Pixel:
    return "pixel";
  case dxmt::dxil::DxilShaderKind::Vertex:
    return "vertex";
  case dxmt::dxil::DxilShaderKind::Geometry:
    return "geometry";
  case dxmt::dxil::DxilShaderKind::Hull:
    return "hull";
  case dxmt::dxil::DxilShaderKind::Domain:
    return "domain";
  case dxmt::dxil::DxilShaderKind::Compute:
    return "compute";
  case dxmt::dxil::DxilShaderKind::Library:
    return "library";
  case dxmt::dxil::DxilShaderKind::Mesh:
    return "mesh";
  case dxmt::dxil::DxilShaderKind::Amplification:
    return "amplification";
  default:
    return "other";
  }
}

void DumpDXILModuleSummary(const char *path,
                           const dxmt::dxil::LLVMModule &module,
                           const dxmt::dxil::DxilParsedShader &shader_info) {
  if (!path)
    return;
  EnsureShaderCacheDir();
  FILE *df = fopen(path, "w");
  if (!df)
    return;

  fprintf(df, "kind=%s(%u)\n", DxilShaderKindName(shader_info.kind),
          (uint32_t)shader_info.kind);
  fprintf(df, "shader_model=%u.%u\n", shader_info.shader_model.major,
          shader_info.shader_model.minor);
  fprintf(df, "entry=%s\n", shader_info.entry_point.c_str());
  fprintf(df, "bitcode_size=%u\n", shader_info.bitcode.size);
  fprintf(df, "source_filename=%s\n", module.source_filename.c_str());
  fprintf(df, "target_triple=%s\n", module.target_triple.c_str());
  fprintf(df, "types=%zu constants=%zu functions=%zu\n", module.types.size(),
          module.constants.size(), module.functions.size());

  size_t total_blocks = 0;
  size_t total_instructions = 0;
  std::map<int, size_t> opcode_counts;
  for (const auto &fn : module.functions) {
    total_blocks += fn.blocks.size();
    for (const auto &block : fn.blocks) {
      total_instructions += block.instructions.size();
      for (const auto &inst : block.instructions)
        opcode_counts[(int)inst.opcode]++;
    }
  }

  fprintf(df, "blocks=%zu instructions=%zu\n", total_blocks,
          total_instructions);
  fprintf(df, "\nfunctions:\n");
  for (const auto &fn : module.functions) {
    size_t inst_count = 0;
    for (const auto &block : fn.blocks)
      inst_count += block.instructions.size();
    fprintf(df,
            "  name=%s declaration=%d value=%u type=%u params=%u inst_start=%u "
            "blocks=%zu instructions=%zu\n",
            fn.name.c_str(), fn.is_declaration, fn.value_id, fn.type_id,
            fn.param_count, fn.instruction_start_value, fn.blocks.size(),
            inst_count);
  }

  fprintf(df, "\nopcodes:\n");
  for (const auto &entry : opcode_counts)
    fprintf(df, "  opcode=%d count=%zu\n", entry.first, entry.second);

  fclose(df);
}

void DumpDXILCompileReport(const char *path, const char *func_name, size_t hash,
                           SIZE_T bytecode_size, const char *dxbc_path,
                           const char *module_summary_path,
                           const char *msl_path,
                           const dxmt::dxil::LLVMModule &module,
                           const dxmt::dxil::DxilParsedShader &shader_info,
                           const dxmt::dxil::MSLShader &msl_result) {
  if (!path)
    return;

  EnsureShaderCacheDir();
  FILE *df = fopen(path, "w");
  if (!df)
    return;

  fprintf(df, "hash=0x%016zx\n", hash);
  fprintf(df, "function=%s\n", func_name ? func_name : "<unknown>");
  fprintf(df, "kind=%s(%u)\n", DxilShaderKindName(shader_info.kind),
          (uint32_t)shader_info.kind);
  fprintf(df, "shader_model=%u.%u\n", shader_info.shader_model.major,
          shader_info.shader_model.minor);
  fprintf(df, "entry=%s\n", shader_info.entry_point.c_str());
  fprintf(df, "bytecode_size=%zu\n", bytecode_size);
  fprintf(df, "bitcode_size=%u\n", shader_info.bitcode.size);
  fprintf(df, "types=%zu constants=%zu functions=%zu\n", module.types.size(),
          module.constants.size(), module.functions.size());
  fprintf(df, "msl_size=%zu\n", msl_result.source.size());
  fprintf(df, "threadgroup_size=%u,%u,%u\n", msl_result.tg_size[0],
          msl_result.tg_size[1], msl_result.tg_size[2]);
  fprintf(df, "unsupported_intrinsics=%u\n", msl_result.unsupported_intrinsics);
  fprintf(df, "unsupported_opcodes=%u\n", msl_result.unsupported_opcodes);
  fprintf(df, "dxbc=%s\n", dxbc_path ? dxbc_path : "");
  fprintf(df, "module=%s\n", module_summary_path ? module_summary_path : "");
  fprintf(df, "msl=%s\n", msl_path ? msl_path : "");
  fprintf(df, "\ndiagnostics:\n");
  for (const auto &diagnostic : msl_result.diagnostics)
    fprintf(df, "  %s\n", diagnostic.c_str());

  fclose(df);

  std::string index_path = BuildShaderCachePath("dxil_report_index.tsv");
  FILE *index = fopen(index_path.c_str(), "a");
  if (index) {
    fprintf(index, "0x%016zx\t%s\t%s\t%u.%u\t%u\t%u\t%s\n", hash,
            DxilShaderKindName(shader_info.kind), func_name ? func_name : "",
            shader_info.shader_model.major, shader_info.shader_model.minor,
            msl_result.unsupported_intrinsics, msl_result.unsupported_opcodes,
            path);
    fclose(index);
  }
}

constexpr WMTColorWriteMask kColorWriteMaskMap[16] = {
    (WMTColorWriteMask)0,
    WMTColorWriteMaskRed,
    WMTColorWriteMaskGreen,
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskGreen),
    WMTColorWriteMaskBlue,
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskBlue),
    (WMTColorWriteMask)(WMTColorWriteMaskGreen | WMTColorWriteMaskBlue),
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskGreen |
                        WMTColorWriteMaskBlue),
    WMTColorWriteMaskAlpha,
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskAlpha),
    (WMTColorWriteMask)(WMTColorWriteMaskGreen | WMTColorWriteMaskAlpha),
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskGreen |
                        WMTColorWriteMaskAlpha),
    (WMTColorWriteMask)(WMTColorWriteMaskBlue | WMTColorWriteMaskAlpha),
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskBlue |
                        WMTColorWriteMaskAlpha),
    (WMTColorWriteMask)(WMTColorWriteMaskGreen | WMTColorWriteMaskBlue |
                        WMTColorWriteMaskAlpha),
    (WMTColorWriteMask)(WMTColorWriteMaskRed | WMTColorWriteMaskGreen |
                        WMTColorWriteMaskBlue | WMTColorWriteMaskAlpha),
};

constexpr WMTCompareFunction kCompareFunctionMap[] = {
    WMTCompareFunctionNever,     WMTCompareFunctionNever,
    WMTCompareFunctionLess,      WMTCompareFunctionEqual,
    WMTCompareFunctionLessEqual, WMTCompareFunctionGreater,
    WMTCompareFunctionNotEqual,  WMTCompareFunctionGreaterEqual,
    WMTCompareFunctionAlways,
};

constexpr WMTStencilOperation kStencilOperationMap[] = {
    WMTStencilOperationZero,           WMTStencilOperationKeep,
    WMTStencilOperationZero,           WMTStencilOperationReplace,
    WMTStencilOperationIncrementClamp, WMTStencilOperationDecrementClamp,
    WMTStencilOperationInvert,         WMTStencilOperationIncrementWrap,
    WMTStencilOperationDecrementWrap,
};

uint32_t AlignD3D12InputOffset(uint32_t offset, uint32_t size) {
  uint32_t alignment = size < 4 ? size : 4;
  if (alignment <= 1)
    return offset;
  return (offset + alignment - 1) & ~(alignment - 1);
}
} // namespace

std::mutex MTLD3D12PipelineState::s_shader_mutex;
std::unordered_map<size_t, WMT::Reference<WMT::Function>>
    MTLD3D12PipelineState::s_shader_cache;

void PipelineCompileScheduler::WorkerMain() {
  while (true) {
    MTLD3D12PipelineState *pso = nullptr;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty())
        return;
      pso = queue_.front();
      queue_.pop_front();
    }

    if (!pso)
      continue;

    pso->RunAsyncCompile();
    pso->Release();
  }
}

MTLD3D12PipelineState::MTLD3D12PipelineState(MTLD3D12Device *device,
                                             bool is_compute)
    : m_device(device), m_is_compute(is_compute) {
  m_device->AddRef();
}

MTLD3D12PipelineState::~MTLD3D12PipelineState() {
  if (m_root_sig)
    m_root_sig->Release();
  m_render_pso = nullptr;
  m_compute_pso = nullptr;
  m_device->Release();
}

void MTLD3D12PipelineState::ClearCompileFailure() {
  std::lock_guard<std::mutex> lock(m_compile_mutex);
  m_compile_failure_stage.clear();
  m_compile_failure_detail.clear();
}

bool MTLD3D12PipelineState::RecordCompileFailure(const char *stage,
                                                 const std::string &detail) {
  std::lock_guard<std::mutex> lock(m_compile_mutex);
  m_compile_failure_stage = stage ? stage : "unknown";
  m_compile_failure_detail = detail;
  PSTRACE("PSO COMPILE FAILURE: this=%p compute=%d stage=%s detail=%s",
          (void *)this, m_is_compute, m_compile_failure_stage.c_str(),
          m_compile_failure_detail.c_str());
  return false;
}

bool MTLD3D12PipelineState::IsCompiled() const {
  return m_compile_state.load(std::memory_order_acquire) ==
         CompileState::Compiled;
}

bool MTLD3D12PipelineState::IsCompilePending() const {
  auto state = m_compile_state.load(std::memory_order_acquire);
  return state == CompileState::Pending || state == CompileState::Compiling;
}

std::string MTLD3D12PipelineState::GetCompileFailureStage() const {
  std::lock_guard<std::mutex> lock(m_compile_mutex);
  return m_compile_failure_stage.empty() ? "none" : m_compile_failure_stage;
}

std::string MTLD3D12PipelineState::GetCompileFailureDetail() const {
  std::lock_guard<std::mutex> lock(m_compile_mutex);
  return m_compile_failure_detail;
}

WMTPixelFormat MTLD3D12PipelineState::DXGIToMTLPixelFormat(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    return WMTPixelFormatRGBA8Unorm;
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return WMTPixelFormatRGBA8Unorm_sRGB;
  case DXGI_FORMAT_B8G8R8A8_UNORM:
    return WMTPixelFormatBGRA8Unorm;
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    return WMTPixelFormatBGRA8Unorm_sRGB;
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return WMTPixelFormatRGBA16Float;
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
    return WMTPixelFormatRGBA32Float;
  case DXGI_FORMAT_R10G10B10A2_UNORM:
    return WMTPixelFormatRGB10A2Unorm;
  case DXGI_FORMAT_R11G11B10_FLOAT:
    return WMTPixelFormatRG11B10Float;
  case DXGI_FORMAT_R8_UNORM:
    return WMTPixelFormatR8Unorm;
  case DXGI_FORMAT_R16_FLOAT:
    return WMTPixelFormatR16Float;
  case DXGI_FORMAT_R32_FLOAT:
    return WMTPixelFormatR32Float;
  case DXGI_FORMAT_D32_FLOAT:
    return WMTPixelFormatDepth32Float;
  case DXGI_FORMAT_D24_UNORM_S8_UINT:
    return WMTPixelFormatDepth24Unorm_Stencil8;
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    return WMTPixelFormatDepth32Float_Stencil8;
  case DXGI_FORMAT_D16_UNORM:
    return WMTPixelFormatDepth16Unorm;
  case DXGI_FORMAT_R16G16_FLOAT:
    return WMTPixelFormatRG16Float;
  case DXGI_FORMAT_R16G16_UNORM:
    return WMTPixelFormatRG16Unorm;
  case DXGI_FORMAT_R8G8_UNORM:
    return WMTPixelFormatRG8Unorm;
  case DXGI_FORMAT_BC1_TYPELESS:
  case DXGI_FORMAT_BC1_UNORM:
    return WMTPixelFormatBC1_RGBA;
  case DXGI_FORMAT_BC1_UNORM_SRGB:
    return WMTPixelFormatBC1_RGBA_sRGB;
  case DXGI_FORMAT_BC2_TYPELESS:
  case DXGI_FORMAT_BC2_UNORM:
    return WMTPixelFormatBC2_RGBA;
  case DXGI_FORMAT_BC2_UNORM_SRGB:
    return WMTPixelFormatBC2_RGBA_sRGB;
  case DXGI_FORMAT_BC3_TYPELESS:
  case DXGI_FORMAT_BC3_UNORM:
    return WMTPixelFormatBC3_RGBA;
  case DXGI_FORMAT_BC3_UNORM_SRGB:
    return WMTPixelFormatBC3_RGBA_sRGB;
  case DXGI_FORMAT_BC4_TYPELESS:
  case DXGI_FORMAT_BC4_UNORM:
    return WMTPixelFormatBC4_RUnorm;
  case DXGI_FORMAT_BC4_SNORM:
    return WMTPixelFormatBC4_RSnorm;
  case DXGI_FORMAT_BC5_TYPELESS:
  case DXGI_FORMAT_BC5_UNORM:
    return WMTPixelFormatBC5_RGUnorm;
  case DXGI_FORMAT_BC5_SNORM:
    return WMTPixelFormatBC5_RGSnorm;
  case DXGI_FORMAT_BC6H_TYPELESS:
  case DXGI_FORMAT_BC6H_UF16:
    return WMTPixelFormatBC6H_RGBUfloat;
  case DXGI_FORMAT_BC6H_SF16:
    return WMTPixelFormatBC6H_RGBFloat;
  case DXGI_FORMAT_BC7_TYPELESS:
  case DXGI_FORMAT_BC7_UNORM:
    return WMTPixelFormatBC7_RGBAUnorm;
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    return WMTPixelFormatBC7_RGBAUnorm_sRGB;
  default:
    return WMTPixelFormatInvalid;
  }
}

bool MTLD3D12PipelineState::CompileShader(
    const void *bytecode, SIZE_T size, ShaderType type, const char *func_name,
    WMT::Reference<WMT::Function> &out_func, sm50_shader_t *out_shader_handle,
    MTL_SHADER_REFLECTION *out_reflection) {
  DXMTD3D12ScopedTimer shader_timer("PSO", "CompileShader");
  if (type == ShaderType::Vertex)
    m_vs_stage_in_register_map.clear();
  if (type == ShaderType::Vertex)
    m_vs_stage_in_attribute_order.clear();

  size_t hash = 0;
  hash = hash * 131 + (size_t)type;
  if (bytecode && size > 0) {
    const uint8_t *p = (const uint8_t *)bytecode;
    for (SIZE_T i = 0; i < size; i++)
      hash = hash * 131 + p[i];
  }
  if (type == ShaderType::Vertex) {
    hash = hash * 131 + m_input_layout.NumElements;
    for (UINT i = 0; i < m_input_layout.NumElements; i++) {
      const auto &el = m_input_layout.pInputElementDescs[i];
      hash = hash * 131 + el.SemanticIndex;
      hash = hash * 131 + el.Format;
      hash = hash * 131 + el.InputSlot;
      hash = hash * 131 + el.AlignedByteOffset;
      hash = hash * 131 + el.InputSlotClass;
      hash = hash * 131 + el.InstanceDataStepRate;
      if (el.SemanticName) {
        for (const char *s = el.SemanticName; *s; s++)
          hash = hash * 131 + (unsigned char)*s;
      }
    }
  }
  shader_timer.SetDetail("func=%s type=%u size=%zu blob=%s hash=0x%zx",
                         func_name, (unsigned)type, size,
                         DescribeShaderBlobMagic(bytecode, size), hash);
  {
    DXMTD3D12ScopedTimer shader_cache_lock_timer("PSO", "ShaderCacheLockWait");
    shader_cache_lock_timer.SetDetail("func=%s hash=0x%zx", func_name, hash);
    std::lock_guard<std::mutex> lock(s_shader_mutex);
    shader_cache_lock_timer.TraceNow();
    PSTRACE("CompileShader: %s hash=0x%zx size=%zu cache_entries=%zu",
            func_name, hash, size, s_shader_cache.size());
    auto it = s_shader_cache.find(hash);
    if (it != s_shader_cache.end() && !out_shader_handle && !out_reflection) {
      out_func = it->second;
      PSTRACE("CompileShader: %s CACHE HIT hash=0x%zx", func_name, hash);
      return true;
    }
  }

  if (bytecode && size >= 4) {
    auto *magic = (const uint32_t *)bytecode;
    PSTRACE("CompileShader: %s size=%zu magic=0x%08x (DXBC=0x43425844 "
            "DXIL=0x4C495844)",
            func_name, size, *magic);
    TraceDxbcChunks(bytecode, size, func_name);
  }
  sm50_error_t sm50_err = nullptr;
  sm50_shader_t shader = nullptr;
  MTL_SHADER_REFLECTION reflection = {};

  if (SM50Initialize(bytecode, size, &shader, &reflection, &sm50_err)) {
    char err_buf[256] = {};
    SM50GetErrorMessage(sm50_err, err_buf, sizeof(err_buf));
    SM50FreeError(sm50_err);

    bool has_dxil = false;
    using namespace microsoft;
    CDXBCParser dxbcParser;
    if (SUCCEEDED(dxbcParser.ReadDXBC(bytecode, size))) {
      for (UINT32 i = 0; i < dxbcParser.GetBlobCount(); i++) {
        if (dxbcParser.GetBlobFourCC(i) == dxmt::dxil::DXIL_FOURCC) {
          has_dxil = true;
          const void *blob = dxbcParser.GetBlob(i);
          UINT32 blob_size = dxbcParser.GetBlobSize(i);
          PSTRACE("DXIL blob found index=%u size=%u", i, blob_size);

          auto wmt_device = m_device->GetDXMTDevice().device();

          std::string cache_path =
              BuildShaderCachePath(str::format("%016zx", hash).c_str());
          char dxbc_path[256], metallib_path[256], reflection_path[256],
              module_summary_path[256], dxil_report_path[256],
              metallib_error_path[256], converter_fail_path[256];
          snprintf(dxbc_path, sizeof(dxbc_path), "%s.dxbc", cache_path.c_str());
          snprintf(metallib_path, sizeof(metallib_path), "%s.metallib",
                   cache_path.c_str());
          snprintf(reflection_path, sizeof(reflection_path), "%s.json",
                   cache_path.c_str());
          snprintf(module_summary_path, sizeof(module_summary_path),
                   "%s.module.txt", cache_path.c_str());
          snprintf(dxil_report_path, sizeof(dxil_report_path),
                   "%s.dxil_report.txt", cache_path.c_str());
          snprintf(metallib_error_path, sizeof(metallib_error_path),
                   "%s.metallib.err.txt", cache_path.c_str());
          snprintf(converter_fail_path, sizeof(converter_fail_path),
                   "%s.msc.fail", cache_path.c_str());
          EnsureShaderCacheDir();

          FILE *mf = fopen(metallib_path, "rb");
          FILE *ff = fopen(converter_fail_path, "rb");
          if (!mf) {
            PSTRACE("  metallib not cached, attempting DXIL->MSL compilation");
            DumpShaderBlob(dxbc_path, bytecode, size);
            for (uint32_t attempt = 0; attempt < 100 && !mf && !ff; attempt++) {
              FILE *rf = fopen(reflection_path, "rb");
              if (rf) {
                fclose(rf);
                mf = fopen(metallib_path, "rb");
                if (mf) {
                  PSTRACE("  external metallib cache hit after %u waits",
                          attempt + 1);
                  break;
                }
              }
              ff = fopen(converter_fail_path, "rb");
              if (ff) {
                PSTRACE(
                    "  external converter failure marker hit after %u waits",
                    attempt + 1);
                break;
              }
              Sleep(20);
            }
          }

          if (ff) {
            fseek(ff, 0, SEEK_END);
            long fail_size = ftell(ff);
            fseek(ff, 0, SEEK_SET);
            std::string fail_text;
            if (fail_size > 0) {
              fail_text.resize((size_t)fail_size);
              size_t read = fread(fail_text.data(), 1, (size_t)fail_size, ff);
              fail_text.resize(read);
            }
            fclose(ff);
            if (fail_text.empty())
              fail_text = "metal-shaderconverter failed";
            if (ShouldFallbackFromMetalShaderConverter(fail_text, type)) {
              PSTRACE("  external converter failed for %s but is eligible for "
                      "DXIL->MSL fallback: %s",
                      func_name, fail_text.c_str());
              Logger::warn(str::format(
                  "metal-shaderconverter failed for ", func_name,
                  "; falling back to internal DXIL->MSL: ", fail_text));
            } else {
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure(
                  "shader/external_metal_shader_converter",
                  str::format(func_name, " metal-shaderconverter failed: ",
                              fail_text, "; dxbc ", dxbc_path));
            }
          }

          if (!mf) {
            auto container = dxmt::dxil::DXILContainer::parse(blob, blob_size);
            if (!container) {
              PSTRACE("  DXILContainer::parse FAILED for %s", func_name);
              return RecordCompileFailure(
                  "shader/dxil_container_parse",
                  str::format(func_name,
                              " DXIL container parse failed; dumped ",
                              dxbc_path));
            }

            auto &shader_info = container->shader();
            PSTRACE("  DXIL container parsed: kind=%u sm=%u.%u bc_size=%u",
                    (uint32_t)shader_info.kind, shader_info.shader_model.major,
                    shader_info.shader_model.minor, shader_info.bitcode.size);

            auto module = dxmt::dxil::BitcodeReader::parse(
                shader_info.bitcode.data, shader_info.bitcode.size);
            if (!module) {
              PSTRACE("  BitcodeReader::parse FAILED");
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure(
                  "shader/bitcode_parse",
                  str::format(func_name, " DXIL bitcode parse failed; dumped ",
                              dxbc_path));
            }

            PSTRACE("  Bitcode parsed: types=%zu functions=%zu constants=%zu",
                    module->types.size(), module->functions.size(),
                    module->constants.size());
            DumpDXILModuleSummary(module_summary_path, *module, shader_info);
            PSTRACE("  DXIL module summary written to %s", module_summary_path);

            auto msl_result =
                dxmt::dxil::DXILToMSL::convert(*module, shader_info);
            if (!msl_result) {
              PSTRACE("  DXILToMSL::convert FAILED");
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure(
                  "shader/dxil_to_msl",
                  str::format(func_name,
                              " DXIL to MSL conversion failed; module ",
                              module_summary_path, "; dxbc ", dxbc_path));
            }

            PSTRACE("  MSL generated: %zu bytes, entry=%s "
                    "unsupported_intrinsics=%u unsupported_opcodes=%u",
                    msl_result->source.size(), msl_result->entry_point.c_str(),
                    msl_result->unsupported_intrinsics,
                    msl_result->unsupported_opcodes);

            char msl_path[256];
            char msl_error_path[256];
            snprintf(msl_path, sizeof(msl_path), "%s.msl", cache_path.c_str());
            snprintf(msl_error_path, sizeof(msl_error_path), "%s.msl.err.txt",
                     cache_path.c_str());
            DumpShaderText(msl_path, msl_result->source.c_str());
            PSTRACE("  MSL source written to %s", msl_path);
            DumpDXILCompileReport(dxil_report_path, func_name, hash, size,
                                  dxbc_path, module_summary_path, msl_path,
                                  *module, shader_info, *msl_result);
            PSTRACE("  DXIL compile report written to %s", dxil_report_path);

            WMT::Reference<WMT::Error> compile_err;
            auto library = wmt_device.newLibraryWithSource(
                msl_result->source.c_str(), msl_result->source.size(),
                compile_err);

            if (compile_err.handle) {
              auto err_desc_string = compile_err.description().getUTF8String();
              const char *err_desc =
                  err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
              DumpShaderText(msl_error_path, err_desc ? err_desc : "unknown");
              PSTRACE("  newLibraryWithSource FAILED: %s",
                      err_desc ? err_desc : "unknown");
              Logger::err(str::format("DXIL MSL compilation failed for ",
                                      func_name, ": ",
                                      err_desc ? err_desc : "unknown error"));
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure(
                  "shader/metal_library_source",
                  str::format(func_name, " MSL compile failed: ",
                              err_desc ? err_desc : "unknown", "; msl ",
                              msl_path, "; error ", msl_error_path, "; dxbc ",
                              dxbc_path));
            }

            PSTRACE("  Metal library compiled OK from source lib_handle=%llu",
                    (unsigned long long)library.handle);

            const char *dump_msl = std::getenv("DXMT_DUMP_MSL");
            if (dump_msl && dump_msl[0] && strcmp(dump_msl, "0") != 0) {
              std::string dump_path = BuildShaderCachePath(
                  str::format("dxmt_msl_", func_name, "_",
                              str::format("%016zx", hash), ".metal")
                      .c_str());
              FILE *df = fopen(dump_path.c_str(), "w");
              if (df) {
                fwrite(msl_result->source.c_str(), 1, msl_result->source.size(),
                       df);
                fclose(df);
              }
            }

            const char *entry_name = msl_result->entry_point.c_str();
            if (strcmp(entry_name, "cs_main") != 0 &&
                strcmp(entry_name, "vs_main") != 0 &&
                strcmp(entry_name, "ps_main") != 0) {
              switch (shader_info.kind) {
              case dxmt::dxil::DxilShaderKind::Compute:
                entry_name = "cs_main";
                break;
              case dxmt::dxil::DxilShaderKind::Vertex:
                entry_name = "vs_main";
                break;
              case dxmt::dxil::DxilShaderKind::Pixel:
                entry_name = "ps_main";
                break;
              default:
                break;
              }
            }

            out_func = library.newFunction(entry_name);
            PSTRACE("  newFunction(%s) on lib=%llu -> func_handle=%llu",
                    entry_name, (unsigned long long)library.handle,
                    (unsigned long long)out_func.handle);
            if (!out_func.handle) {
              PSTRACE("  newFunction(%s) returned null, trying alternatives",
                      entry_name);
              out_func = library.newFunction("main");
              if (!out_func.handle)
                out_func = library.newFunction("cs_main");
              if (!out_func.handle)
                out_func = library.newFunction("vs_main");
              if (!out_func.handle)
                out_func = library.newFunction("ps_main");
            }

            if (out_func.handle) {
              PSTRACE("  DXIL shader compiled OK! entry=%s", entry_name);
              {
                std::lock_guard<std::mutex> lock(s_shader_mutex);
                s_shader_cache[hash] = out_func;
              }

              if (type == ShaderType::Vertex)
                m_vs_uses_stage_in = true;

              if (shader_info.kind == dxmt::dxil::DxilShaderKind::Compute) {
                m_threadgroup_size.width = msl_result->tg_size[0];
                m_threadgroup_size.height = msl_result->tg_size[1];
                m_threadgroup_size.depth = msl_result->tg_size[2];
              }
              return true;
            } else {
              PSTRACE("  newFunction returned null for all entry points");
              Logger::err(str::format(
                  "DXIL: failed to get function from compiled library for ",
                  func_name));
              return RecordCompileFailure(
                  "shader/metal_function_lookup",
                  str::format(func_name,
                              " function lookup failed after MSL compile; msl ",
                              msl_path));
            }
          }

          PSTRACE("  loading cached metallib from %s", metallib_path);
          fseek(mf, 0, SEEK_END);
          long lib_size = ftell(mf);
          fseek(mf, 0, SEEK_SET);
          PSTRACE("  metallib size=%ld", lib_size);
          if (lib_size > 0) {
            std::vector<uint8_t> lib_data(lib_size);
            fread(lib_data.data(), 1, lib_size, mf);
            fclose(mf);
            auto dispatch_data =
                WMT::MakeDispatchData(lib_data.data(), lib_size);
            WMT::Reference<WMT::Error> err;
            auto library = wmt_device.newLibrary(dispatch_data, err);
            if (!err.handle) {
              std::string reflection_text = ReadTextFile(reflection_path);
              std::string actual_entry;
              ExtractJsonStringValue(reflection_text, "EntryPoint",
                                     actual_entry);
              MTL_SHADER_REFLECTION reflected_arguments = {};
              reflected_arguments.ConstanttBufferTableBindIndex = ~0u;
              reflected_arguments.ArgumentBufferBindIndex = ~0u;
              auto msc_args = ParseTopLevelArgumentBufferReflection(
                  reflection_text, reflected_arguments);
              if (!msc_args.empty()) {
                switch (type) {
                case ShaderType::Vertex:
                  m_vs_reflection = reflected_arguments;
                  m_vs_args = msc_args;
                  break;
                case ShaderType::Pixel:
                  m_ps_reflection = reflected_arguments;
                  m_ps_args = msc_args;
                  break;
                case ShaderType::Compute:
                  m_cs_reflection = reflected_arguments;
                  m_cs_args = msc_args;
                  break;
                default:
                  break;
                }
                PSTRACE("  MSC TopLevelArgumentBuffer reflected: type=%u "
                        "args=%zu qwords=%u",
                        (unsigned)type, msc_args.size(),
                        reflected_arguments.ArgumentTableQwords);
                for (size_t i = 0; i < msc_args.size(); i++) {
                  PSTRACE("    MSC arg[%zu] type=%d slot=%u space=%u "
                          "offset=%u flags=0x%x",
                          i, (int)msc_args[i].Type,
                          msc_args[i].SM50BindingSlot,
                          msc_args[i].SM50RegisterSpace,
                          msc_args[i].StructurePtrOffset, msc_args[i].Flags);
                }
              }
              if (type == ShaderType::Vertex) {
                m_vs_stage_in_attribute_order =
                    ParseVertexInputReflection(reflection_text);
                m_vs_stage_in_register_map.clear();
                for (const auto &attr : m_vs_stage_in_attribute_order)
                  m_vs_stage_in_register_map[attr.register_index] = attr;

                if (!m_vs_stage_in_register_map.empty()) {
                  for (const auto &[reg, attr] : m_vs_stage_in_register_map)
                    PSTRACE("  DXIL vertex reflection register %u -> metal "
                            "attribute %u format=%u",
                            reg, attr.attribute_index, (unsigned)attr.format);
                } else {
                  PSTRACE("  DXIL vertex reflection register map unavailable");
                }
                if (!m_vs_stage_in_attribute_order.empty()) {
                  for (size_t attr_i = 0;
                       attr_i < m_vs_stage_in_attribute_order.size(); attr_i++)
                    PSTRACE(
                        "  DXIL vertex reflection order[%zu] register=%u "
                        "attribute=%u format=%u",
                        attr_i,
                        m_vs_stage_in_attribute_order[attr_i].register_index,
                        m_vs_stage_in_attribute_order[attr_i].attribute_index,
                        (unsigned)m_vs_stage_in_attribute_order[attr_i].format);
                } else {
                  PSTRACE(
                      "  DXIL vertex reflection attribute order unavailable");
                }
              }

              const char *fn_name =
                  actual_entry.empty() ? func_name : actual_entry.c_str();
              PSTRACE("  trying newFunction(%s)", fn_name);
              out_func = library.newFunction(fn_name);
              if (!out_func.handle && !actual_entry.empty()) {
                out_func = library.newFunction(func_name);
              }
              if (!out_func.handle)
                out_func = library.newFunction("main");
              if (!out_func.handle)
                out_func = library.newFunction("cs_main");
              if (!out_func.handle)
                out_func = library.newFunction("vs_main");
              if (!out_func.handle)
                out_func = library.newFunction("ps_main");
              if (out_func.handle) {
                PSTRACE("  DXIL loaded from cache OK! entry=%s", fn_name);
                {
                  std::lock_guard<std::mutex> lock(s_shader_mutex);
                  s_shader_cache[hash] = out_func;
                }
                if (type == ShaderType::Vertex)
                  m_vs_uses_stage_in = true;
                int tw = 1, th = 1, td = 1;
                if (ExtractJsonInt3Value(reflection_text, "tg_size", tw, th,
                                         td)) {
                  m_threadgroup_size.width = tw;
                  m_threadgroup_size.height = th;
                  m_threadgroup_size.depth = td;
                  PSTRACE("  threadgroup_size from reflection: %dx%dx%d", tw,
                          th, td);
                }
                return true;
              } else {
                PSTRACE("  WMT newFunction returned null");
                DumpShaderBlob(dxbc_path, bytecode, size);
                return RecordCompileFailure(
                    "shader/dxil_cached_function_lookup",
                    str::format(
                        func_name,
                        " cached metallib function lookup failed; metallib ",
                        metallib_path, "; reflection ", reflection_path,
                        "; dxbc ", dxbc_path));
              }
            } else {
              auto err_desc_string = err.description().getUTF8String();
              const char *err_desc =
                  err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
              DumpShaderText(metallib_error_path,
                             err_desc ? err_desc : "unknown");
              DumpShaderBlob(dxbc_path, bytecode, size);
              PSTRACE("  WMT newLibrary FAILED: %s",
                      err_desc ? err_desc : "unknown");
              return RecordCompileFailure(
                  "shader/dxil_cached_metallib_load",
                  str::format(func_name, " cached metallib load failed: ",
                              err_desc ? err_desc : "unknown", "; metallib ",
                              metallib_path, "; error ", metallib_error_path,
                              "; dxbc ", dxbc_path));
            }
          } else {
            fclose(mf);
            DumpShaderBlob(dxbc_path, bytecode, size);
            return RecordCompileFailure(
                "shader/dxil_cached_metallib_empty",
                str::format(func_name, " cached metallib empty; metallib ",
                            metallib_path, "; dxbc ", dxbc_path));
          }
          break;
        }
      }
    }
    if (!has_dxil) {
      char dxbc_path[256];
      std::string dxbc_dump =
          BuildShaderCachePath(str::format("%016zx.sm50_failed.dxbc", hash).c_str());
      snprintf(dxbc_path, sizeof(dxbc_path), "%s", dxbc_dump.c_str());
      DumpShaderBlob(dxbc_path, bytecode, size);
      PSTRACE("SM50Init FAILED for %s: %s (no DXIL chunk, dumped %s)",
              func_name, err_buf, dxbc_path);
    }
    return RecordCompileFailure(
        has_dxil ? "shader/dxil_metallib_cache" : "shader/sm50_init",
        str::format(func_name, " SM50Initialize failed: ", err_buf));
  }

  SM50_SHADER_COMMON_DATA common = {};
  common.next = nullptr;
  common.type = SM50_SHADER_COMMON;
  common.metal_version = SM50_SHADER_METAL_310;
  common.flags = {};

  if (type == ShaderType::Compute) {
    uint32_t tgx =
        reflection.ThreadgroupSize[0] ? reflection.ThreadgroupSize[0] : 1;
    uint32_t tgy =
        reflection.ThreadgroupSize[1] ? reflection.ThreadgroupSize[1] : 1;
    uint32_t tgz =
        reflection.ThreadgroupSize[2] ? reflection.ThreadgroupSize[2] : 1;
    m_threadgroup_size.width = tgx;
    m_threadgroup_size.height = tgy;
    m_threadgroup_size.depth = tgz;
    PSTRACE("CompileShader: %s SM50 threadgroup_size=%ux%ux%u", func_name, tgx,
            tgy, tgz);
  }

  std::vector<SM50_IA_INPUT_ELEMENT> ia_elements;
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout = {};
  SM50_SHADER_GS_PASS_THROUGH_DATA gs_pass_through = {};
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *compile_args =
      (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&common;
  if (type == ShaderType::Vertex) {
    uint32_t slot_mask = 0;
    BuildIAInputLayout(bytecode, size, ia_elements, slot_mask);
    m_ia_slot_mask = slot_mask;
    ia_layout.next = &common;
    ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
    ia_layout.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_NONE;
    ia_layout.slot_mask = slot_mask;
    ia_layout.num_elements = (uint32_t)ia_elements.size();
    ia_layout.elements = ia_elements.data();
    compile_args = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&ia_layout;
    if (m_gs_passthrough != ~0u) {
      gs_pass_through.type = SM50_SHADER_GS_PASS_THROUGH;
      gs_pass_through.next = &ia_layout;
      gs_pass_through.DataEncoded = m_gs_passthrough;
      gs_pass_through.RasterizationDisabled = false;
      compile_args = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&gs_pass_through;
      PSTRACE("CompileShader: %s enabling synthesized GS pass-through=0x%x",
              func_name, m_gs_passthrough);
    }
    PSTRACE("CompileShader: %s IA args elements=%u slot_mask=0x%x", func_name,
            ia_layout.num_elements, ia_layout.slot_mask);
  }

  sm50_bitcode_t compile_result = nullptr;
  if (SM50Compile(shader, compile_args, func_name, &compile_result,
                  &sm50_err)) {
    char err_buf[256] = {};
    SM50GetErrorMessage(sm50_err, err_buf, sizeof(err_buf));
    char dxbc_path[256];
    std::string dxbc_dump = BuildShaderCachePath(
        str::format("%016zx.sm50_compile_failed.dxbc", hash).c_str());
    snprintf(dxbc_path, sizeof(dxbc_path), "%s", dxbc_dump.c_str());
    DumpShaderBlob(dxbc_path, bytecode, size);
    PSTRACE("SM50Compile failed for %s: %s (dumped %s)", func_name, err_buf,
            dxbc_path);
    Logger::err(
        str::format("SM50Compile failed for ", func_name, ": ", err_buf));
    SM50FreeError(sm50_err);
    SM50Destroy(shader);
    return RecordCompileFailure("shader/sm50_compile",
                                str::format(func_name, " SM50Compile failed: ",
                                            err_buf, "; dumped ", dxbc_path));
  }

  SM50_COMPILED_BITCODE bitcode = {};
  SM50GetCompiledBitcode(compile_result, &bitcode);

  {
    std::string dump_path =
        BuildShaderCachePath(str::format("dxmt_sm50_", func_name, ".metallib").c_str());
    FILE *df = fopen(dump_path.c_str(), "wb");
    if (df) {
      fwrite(bitcode.Data, 1, bitcode.Size, df);
      fclose(df);
    }
    Logger::info(str::format("  SM50 dumped ", func_name, " to ", dump_path,
                             " (", bitcode.Size, " bytes)"));
  }

  auto wmt_device = m_device->GetDXMTDevice().device();
  WMT::Reference<WMT::Error> err;
  auto lib_data = WMT::MakeDispatchData(bitcode.Data, bitcode.Size);
  auto library = wmt_device.newLibrary(lib_data, err);

  if (err.handle) {
    auto err_desc_string = err.description().getUTF8String();
    const char *err_desc =
        err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
    char dxbc_path[256];
    std::string dxbc_dump = BuildShaderCachePath(
        str::format("%016zx.sm50_metal_library_failed.dxbc", hash).c_str());
    snprintf(dxbc_path, sizeof(dxbc_path), "%s", dxbc_dump.c_str());
    DumpShaderBlob(dxbc_path, bytecode, size);
    PSTRACE("Failed to create Metal library for %s: %s (dumped %s)", func_name,
            err_desc ? err_desc : "unknown", dxbc_path);
    Logger::err(str::format("Failed to create Metal library for ", func_name));
    SM50DestroyBitcode(compile_result);
    SM50Destroy(shader);
    return RecordCompileFailure(
        "shader/sm50_metal_library",
        str::format(func_name, " SM50 Metal library creation failed: ",
                    err_desc ? err_desc : "unknown", "; dumped ", dxbc_path));
  }

  out_func = library.newFunction(func_name);
  SM50DestroyBitcode(compile_result);

  if (out_reflection) {
    *out_reflection = reflection;
  }

  if (out_shader_handle) {
    *out_shader_handle = shader;
  } else {
    SM50Destroy(shader);
  }

  if (!out_func.handle) {
    char dxbc_path[256];
    std::string dxbc_dump = BuildShaderCachePath(
        str::format("%016zx.sm50_function_lookup_failed.dxbc", hash).c_str());
    snprintf(dxbc_path, sizeof(dxbc_path), "%s", dxbc_dump.c_str());
    DumpShaderBlob(dxbc_path, bytecode, size);
    PSTRACE("Failed to get function %s from Metal library (dumped %s)",
            func_name, dxbc_path);
    Logger::err(str::format("Failed to get function ", func_name));
    return RecordCompileFailure(
        "shader/sm50_function_lookup",
        str::format(func_name, " SM50 function lookup failed; dumped ",
                    dxbc_path));
  }

  PSTRACE("CompileShader: %s SM50 OK function=%llu", func_name,
          (unsigned long long)out_func.handle);
  Logger::info(str::format("  Compiled ", func_name, " OK"));
  {
    std::lock_guard<std::mutex> lock(s_shader_mutex);
    s_shader_cache[hash] = out_func;
  }
  return true;
}

void MTLD3D12PipelineState::BuildIAInputLayout(
    const void *bytecode, SIZE_T size,
    std::vector<SM50_IA_INPUT_ELEMENT> &elements, uint32_t &slot_mask) const {
  slot_mask = 0;
  elements.clear();

  if (!bytecode || !size || !m_input_layout.NumElements ||
      !m_input_layout.pInputElementDescs)
    return;

  using namespace microsoft;
  CSignatureParser parser;
  HRESULT hr = DXBCGetInputSignature(bytecode, &parser);
  if (FAILED(hr)) {
    PSTRACE("BuildIAInputLayout: DXBCGetInputSignature failed hr=0x%lx", hr);
    return;
  }

  const D3D11_SIGNATURE_PARAMETER *params = nullptr;
  uint32_t param_count = parser.GetParameters(&params);
  uint32_t append_offset[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};

  for (UINT i = 0; i < m_input_layout.NumElements; i++) {
    const auto &desc = m_input_layout.pInputElementDescs[i];
    if (desc.InputSlot >= kMetalD3D12VertexBufferSlotCount) {
      PSTRACE("BuildIAInputLayout skip[%u]: slot %u outside cap %u", i,
              desc.InputSlot, kMetalD3D12VertexBufferSlotCount);
      continue;
    }

    MTL_DXGI_FORMAT_DESC metal_format = {};
    if (FAILED(MTLQueryDXGIFormat(m_device->GetMTLDevice(), desc.Format,
                                  metal_format)) ||
        !metal_format.AttributeFormat || !metal_format.BytesPerTexel) {
      PSTRACE("BuildIAInputLayout skip[%u]: unsupported fmt=%u", i,
              (unsigned)desc.Format);
      continue;
    }

    auto *sig = std::find_if(
        params, params + param_count,
        [&](const D3D11_SIGNATURE_PARAMETER &input_sig) {
          return input_sig.SystemValue == D3D10_SB_NAME_UNDEFINED &&
                 desc.SemanticIndex == input_sig.SemanticIndex &&
                 desc.SemanticName && input_sig.SemanticName &&
                 strcasecmp(desc.SemanticName, input_sig.SemanticName) == 0;
        });
    if (sig == params + param_count) {
      PSTRACE("BuildIAInputLayout skip[%u]: semantic %s%u not consumed by VS",
              i, desc.SemanticName ? desc.SemanticName : "?",
              desc.SemanticIndex);
      continue;
    }

    uint32_t aligned_offset =
        desc.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
            ? AlignD3D12InputOffset(append_offset[desc.InputSlot],
                                    metal_format.BytesPerTexel)
            : desc.AlignedByteOffset;
    append_offset[desc.InputSlot] = aligned_offset + metal_format.BytesPerTexel;

    SM50_IA_INPUT_ELEMENT element = {};
    element.reg = sig->Register;
    element.slot = desc.InputSlot;
    element.aligned_byte_offset = aligned_offset;
    element.format = metal_format.AttributeFormat;
    element.step_function =
        desc.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    element.step_rate =
        desc.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
            ? desc.InstanceDataStepRate
            : 1;
    elements.push_back(element);
    slot_mask |= 1u << desc.InputSlot;

    PSTRACE("BuildIAInputLayout element[%zu]: semantic=%s%u reg=%u slot=%u "
            "offset=%u fmt=%u step=%u/%u",
            elements.size() - 1, desc.SemanticName ? desc.SemanticName : "?",
            desc.SemanticIndex, element.reg, element.slot,
            element.aligned_byte_offset, element.format, element.step_function,
            element.step_rate);
  }
}

bool MTLD3D12PipelineState::RequestCompile(bool allow_async) {
  if (allow_async && DXMTD3D12AsyncPipelineCompileEnabled()) {
    {
      std::unique_lock<std::mutex> lock(m_compile_mutex);
      auto state = m_compile_state.load(std::memory_order_acquire);
      if (state == CompileState::Compiled)
        return true;
      if (state == CompileState::Failed)
        return false;
      if (state == CompileState::Pending || state == CompileState::Compiling)
        return false;
      m_compile_state.store(CompileState::Pending, std::memory_order_release);
    }

    AddRef();
    if (!GetPipelineCompileScheduler().Enqueue(this)) {
      Release();
      std::lock_guard<std::mutex> lock(m_compile_mutex);
      m_compile_state.store(CompileState::NotStarted, std::memory_order_release);
      return Compile();
    }

    PSTRACE("Queued async PSO compile this=%p compute=%d", (void *)this,
            m_is_compute);
    return false;
  }

  return Compile();
}

bool MTLD3D12PipelineState::Compile() {
  {
    std::unique_lock<std::mutex> lock(m_compile_mutex);
    auto state = m_compile_state.load(std::memory_order_acquire);
    if (state == CompileState::Compiled)
      return true;
    if (state == CompileState::Failed)
      return false;
    if (state == CompileState::Pending || state == CompileState::Compiling) {
      m_compile_cv.wait(lock, [this]() {
        auto current = m_compile_state.load(std::memory_order_acquire);
        return current != CompileState::Pending &&
               current != CompileState::Compiling;
      });
      return m_compile_state.load(std::memory_order_acquire) ==
             CompileState::Compiled;
    }
    m_compile_state.store(CompileState::Compiling, std::memory_order_release);
  }

  bool compiled = CompileImpl();
  {
    std::lock_guard<std::mutex> lock(m_compile_mutex);
    m_compile_state.store(compiled ? CompileState::Compiled : CompileState::Failed,
                          std::memory_order_release);
  }
  m_compile_cv.notify_all();
  return compiled;
}

void MTLD3D12PipelineState::RunAsyncCompile() {
  {
    std::lock_guard<std::mutex> lock(m_compile_mutex);
    auto state = m_compile_state.load(std::memory_order_acquire);
    if (state != CompileState::Pending)
      return;
    m_compile_state.store(CompileState::Compiling, std::memory_order_release);
  }

  bool compiled = CompileImpl();
  {
    std::lock_guard<std::mutex> lock(m_compile_mutex);
    m_compile_state.store(compiled ? CompileState::Compiled : CompileState::Failed,
                          std::memory_order_release);
  }
  m_compile_cv.notify_all();
}

bool MTLD3D12PipelineState::CompileImpl() {
  PTRACE("Compile() called state=%u is_compute=%d",
         (unsigned)m_compile_state.load(std::memory_order_acquire),
         m_is_compute);
  ClearCompileFailure();
  m_uses_geometry_mesh_pipeline = false;
  DXMTD3D12ScopedTimer compile_timer("PSO", "CompilePipelineState");
  compile_timer.SetDetail("this=%p compute=%d root=%p vs=%zu ps=%zu cs=%zu il=%u",
                          (void *)this, m_is_compute, (void *)m_root_sig,
                          m_vs.size(), m_ps.size(), m_cs.size(),
                          m_input_layout.NumElements);

  auto wmt_device = m_device->GetDXMTDevice().device();
  WMT::Reference<WMT::Error> err;

  if (m_is_compute) {
    if (m_cs.empty()) {
      Logger::err("Compute PSO has no CS bytecode");
      return RecordCompileFailure("pso/compute_no_cs",
                                  "Compute PSO has no CS bytecode");
    }

    WMT::Reference<WMT::Function> cs_func;
    if (!CompileShader(m_cs.data(), m_cs.size(), ShaderType::Compute, "cs_main",
                       cs_func, &m_cs_shader, &m_cs_reflection))
      return false;

    WMTComputePipelineInfo info = {};
    WMT::InitializeComputePipelineInfo(info);
    info.compute_function = cs_func.handle;

    DXMTD3D12ScopedTimer metal_compute_timer("PSO", "CreateMetalComputePSO");
    metal_compute_timer.SetDetail("this=%p func=%llu threads=%ux%ux%u",
                                  (void *)this,
                                  (unsigned long long)cs_func.handle,
                                  (unsigned)m_threadgroup_size.width,
                                  (unsigned)m_threadgroup_size.height,
                                  (unsigned)m_threadgroup_size.depth);
    m_compute_pso = wmt_device.newComputePipelineState(info, err);
    if (!m_compute_pso.handle) {
      auto err_desc_string =
          err.handle ? err.description().getUTF8String() : std::string();
      const char *err_desc =
          err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
      Logger::err(str::format("Failed to create compute PSO: ",
                              err_desc ? err_desc : "unknown"));
      if (m_cs_shader) {
        SM50Destroy(m_cs_shader);
        m_cs_shader = nullptr;
      }
      return RecordCompileFailure(
          "pso/metal_compute_pso",
          str::format("Metal compute PSO creation failed: ",
                      err_desc ? err_desc : "unknown"));
    }

    PTRACE("CS_ARGS_DEBUG: shader=%llu NumCB=%u NumArgs=%u CBufBindIdx=%u "
           "ArgBufBindIdx=%u ArgTableQwords=%u",
           (unsigned long long)(uintptr_t)m_cs_shader,
           m_cs_reflection.NumConstantBuffers, m_cs_reflection.NumArguments,
           m_cs_reflection.ConstanttBufferTableBindIndex,
           m_cs_reflection.ArgumentBufferBindIndex,
           m_cs_reflection.ArgumentTableQwords);
    if (m_cs_shader && (m_cs_reflection.NumArguments > 0 ||
                        m_cs_reflection.NumConstantBuffers > 0)) {
      if (m_cs_reflection.NumConstantBuffers > 0)
        m_cs_cb_args.resize(m_cs_reflection.NumConstantBuffers);
      if (m_cs_reflection.NumArguments > 0)
        m_cs_args.resize(m_cs_reflection.NumArguments);
      SM50GetArgumentsInfo(m_cs_shader,
                           m_cs_cb_args.empty() ? nullptr : m_cs_cb_args.data(),
                           m_cs_args.empty() ? nullptr : m_cs_args.data());
      for (size_t i = 0; i < m_cs_cb_args.size(); i++) {
        PTRACE("CS_ARGS_DEBUG: cb[%zu] type=%d slot=%u flags=0x%x offset=%u", i,
               (int)m_cs_cb_args[i].Type, m_cs_cb_args[i].SM50BindingSlot,
               m_cs_cb_args[i].Flags, m_cs_cb_args[i].StructurePtrOffset);
      }
      for (size_t i = 0; i < m_cs_args.size(); i++) {
        PTRACE("CS_ARGS_DEBUG: arg[%zu] type=%d slot=%u flags=0x%x offset=%u",
               i, (int)m_cs_args[i].Type, m_cs_args[i].SM50BindingSlot,
               m_cs_args[i].Flags, m_cs_args[i].StructurePtrOffset);
      }
    }
    if (m_cs_shader) {
      SM50Destroy(m_cs_shader);
      m_cs_shader = nullptr;
    }

    Logger::info("Compute PSO compiled successfully");
    return true;
  }

  WMT::Reference<WMT::Function> vs_func, ps_func;

  if (!m_hs.empty() || !m_ds.empty()) {
    return RecordCompileFailure(
        "pso/unsupported_tessellation",
        str::format("Graphics PSO uses HS bytes=", m_hs.size(), " DS bytes=",
                    m_ds.size(), " but D3D12 tessellation is not implemented"));
  }

  m_gs_passthrough = ~0u;
  if (!m_gs.empty()) {
    size_t gs_hash =
        std::hash<std::string_view>{}(std::string_view(
            (const char *)m_gs.data(), (size_t)m_gs.size()));
    char gs_dump_path[256];
    std::string gs_dump =
        BuildShaderCachePath(str::format("%016zx.gs", gs_hash).c_str());
    snprintf(gs_dump_path, sizeof(gs_dump_path), "%s", gs_dump.c_str());
    DumpShaderBlob(gs_dump_path, m_gs.data(), m_gs.size());
    PSTRACE("Graphics PSO GS unsupported: size=%zu hash=0x%016zx magic=%s dumped=%s",
            m_gs.size(), gs_hash, DescribeShaderBlobMagic(m_gs.data(), m_gs.size()),
            gs_dump_path);
    TraceDxbcChunks(m_gs.data(), m_gs.size(), "gs");
    std::string gs_reason;
    if (DescribeShaderBlobMagic(m_gs.data(), m_gs.size()) == std::string_view("DXBC") &&
        InferGeometryPassThroughFromDxbcSignatures(m_gs.data(), m_gs.size(),
                                                   m_gs_passthrough, gs_reason)) {
      Logger::warn(str::format(
          "CreateGraphicsPipelineState: treating DXIL-backed GS bytes=",
          m_gs.size(), " as synthesized pass-through (", gs_reason, ")"));
      PSTRACE("Graphics PSO GS synthesized pass-through hash=0x%016zx detail=%s",
              gs_hash, gs_reason.c_str());
    } else {
      PSTRACE("Graphics PSO GS no synthesized pass-through hash=0x%016zx detail=%s",
              gs_hash, gs_reason.c_str());
    }
    if (m_gs_passthrough == ~0u) {
      if (DXMTD3D12GeometryMeshPipelineEnabled()) {
        if (m_vs.empty()) {
          return RecordCompileFailure(
              "pso/geometry_mesh_no_vs",
              "Geometry mesh PSO requested but graphics PSO has no VS bytecode");
        }
        if (!DxbcContainsSm50ShaderBlob(m_vs.data(), m_vs.size()) ||
            !DxbcContainsSm50ShaderBlob(m_gs.data(), m_gs.size())) {
          size_t geometry_hash = 0;
          auto hash_bytes = [&](const void *data, SIZE_T byte_size) {
            const uint8_t *bytes = (const uint8_t *)data;
            for (SIZE_T i = 0; i < byte_size; i++)
              geometry_hash = geometry_hash * 131 + bytes[i];
          };
          geometry_hash = geometry_hash * 131 + m_input_layout.NumElements;
          hash_bytes(m_vs.data(), m_vs.size());
          hash_bytes(m_gs.data(), m_gs.size());
          for (UINT i = 0; i < m_input_layout.NumElements; i++) {
            const auto &el = m_input_layout.pInputElementDescs[i];
            geometry_hash = geometry_hash * 131 + el.SemanticIndex;
            geometry_hash = geometry_hash * 131 + el.Format;
            geometry_hash = geometry_hash * 131 + el.InputSlot;
            geometry_hash = geometry_hash * 131 + el.AlignedByteOffset;
            geometry_hash = geometry_hash * 131 + el.InputSlotClass;
            geometry_hash = geometry_hash * 131 + el.InstanceDataStepRate;
            if (el.SemanticName) {
              for (const char *s = el.SemanticName; *s; s++)
                geometry_hash = geometry_hash * 131 + (unsigned char)*s;
            }
          }

          const std::string base =
              BuildShaderCachePath((HexHash(geometry_hash) + ".geom").c_str());
          const std::string vs_dxbc_path = base + ".gsvs.dxbc";
          const std::string gs_dxbc_path = base + ".gsmesh.dxbc";
          const std::string vs_layout_path = base + ".gsvs.vertex-layout.json";
          const std::string gs_layout_path = base + ".gsmesh.vertex-layout.json";
          const std::string vs_metallib_path = base + ".gsvs.metallib";
          const std::string gs_metallib_path = base + ".gsmesh.metallib";
          const std::string vs_reflection_path = base + ".gsvs.json";
          const std::string gs_reflection_path = base + ".gsmesh.json";
          const std::string vs_fail_path = base + ".gsvs.msc.fail";
          const std::string gs_fail_path = base + ".gsmesh.msc.fail";
          EnsureShaderCacheDir();
          DumpShaderBlob(vs_dxbc_path.c_str(), m_vs.data(), m_vs.size());
          DumpShaderBlob(gs_dxbc_path.c_str(), m_gs.data(), m_gs.size());
          if (!WriteMetalShaderConverterVertexLayout(
                  m_device, m_input_layout, vs_layout_path.c_str()) ||
              !WriteMetalShaderConverterVertexLayout(
                  m_device, m_input_layout, gs_layout_path.c_str())) {
            return RecordCompileFailure(
                "pso/geometry_msc_vertex_layout",
                str::format("Failed to write MetalShaderConverter vertex "
                            "layout for DXIL geometry PSO base=",
                            base));
          }

          auto wait_for_msc_output =
              [&](const std::string &metallib_path,
                  const std::string &reflection_path,
                  const std::string &fail_path, const char *stage) -> bool {
            for (uint32_t attempt = 0; attempt < 250; attempt++) {
              FILE *mf = fopen(metallib_path.c_str(), "rb");
              FILE *rf = fopen(reflection_path.c_str(), "rb");
              if (mf && rf) {
                fclose(mf);
                fclose(rf);
                PSTRACE("DXIL geometry MSC %s ready after %u waits", stage,
                        attempt + 1);
                return true;
              }
              if (mf)
                fclose(mf);
              if (rf)
                fclose(rf);
              FILE *ff = fopen(fail_path.c_str(), "rb");
              if (ff) {
                fclose(ff);
                PSTRACE("DXIL geometry MSC %s failure marker hit after %u waits",
                        stage, attempt + 1);
                return false;
              }
              Sleep(20);
            }
            PSTRACE("DXIL geometry MSC %s timed out waiting for %s", stage,
                    metallib_path.c_str());
            return false;
          };

          if (!wait_for_msc_output(vs_metallib_path, vs_reflection_path,
                                   vs_fail_path, "object") ||
              !wait_for_msc_output(gs_metallib_path, gs_reflection_path,
                                   gs_fail_path, "mesh")) {
            return RecordCompileFailure(
                "pso/geometry_msc_compile",
                str::format("MetalShaderConverter did not produce DXIL "
                            "geometry object/mesh metallibs; VS=",
                            vs_dxbc_path, " GS=", gs_dxbc_path));
          }

          auto read_binary = [](const std::string &path,
                                std::vector<uint8_t> &out) -> bool {
            FILE *file = fopen(path.c_str(), "rb");
            if (!file)
              return false;
            fseek(file, 0, SEEK_END);
            long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            if (size <= 0) {
              fclose(file);
              return false;
            }
            out.resize((size_t)size);
            size_t read = fread(out.data(), 1, (size_t)size, file);
            fclose(file);
            out.resize(read);
            return read == (size_t)size;
          };

          std::string vs_reflection_text = ReadTextFile(vs_reflection_path.c_str());
          std::string gs_reflection_text = ReadTextFile(gs_reflection_path.c_str());
          uint32_t vertex_output_size = 0;
          uint32_t payload_size = 16256;
          ExtractJsonUIntValue(vs_reflection_text,
                               "vertex_output_size_in_bytes",
                               vertex_output_size);
          ExtractJsonUIntValue(gs_reflection_text,
                               "max_payload_size_in_bytes", payload_size);
          if (!vertex_output_size)
            vertex_output_size = 16;
          if (!payload_size)
            payload_size = 16256;

          std::string vs_entry;
          std::string gs_entry;
          ExtractJsonStringValue(vs_reflection_text, "EntryPoint", vs_entry);
          ExtractJsonStringValue(gs_reflection_text, "EntryPoint", gs_entry);
          if (vs_entry.empty())
            vs_entry = "vs_main";
          if (gs_entry.empty())
            gs_entry = "gs_main";
          std::string object_entry = vs_entry + ".dxil_irconverter_object_shader";

          bool tessellation_enabled = false;
          int vertex_output_size_int = (int)vertex_output_size;
          WMTFunctionConstant constants[2] = {};
          constants[0].data.set(&tessellation_enabled);
          constants[0].type = WMTDataTypeBool;
          constants[0].index = 0;
          constants[1].data.set(&vertex_output_size_int);
          constants[1].type = WMTDataTypeInt;
          constants[1].index = 1;

          auto load_msc_function =
              [&](const std::string &metallib_path, const char *function_name,
                  const char *stage,
                  WMT::Reference<WMT::Function> &out_func) -> bool {
            std::vector<uint8_t> lib_data;
            if (!read_binary(metallib_path, lib_data))
              return RecordCompileFailure(
                  stage, str::format("Failed to read MSC metallib ",
                                     metallib_path));
            auto dispatch_data =
                WMT::MakeDispatchData(lib_data.data(), lib_data.size());
            WMT::Reference<WMT::Error> lib_err;
            auto library = wmt_device.newLibrary(dispatch_data, lib_err);
            if (lib_err.handle) {
              auto err_desc_string = lib_err.description().getUTF8String();
              const char *err_desc =
                  err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
              return RecordCompileFailure(
                  stage, str::format("MSC geometry Metal library failed: ",
                                     err_desc ? err_desc : "unknown",
                                     "; metallib ", metallib_path));
            }

            WMT::Reference<WMT::Error> fn_err;
            out_func = library.newFunctionWithConstants(
                function_name, constants, std::size(constants), fn_err);
            if (!out_func.handle) {
              auto err_desc_string =
                  fn_err.handle ? fn_err.description().getUTF8String()
                                : std::string();
              const char *err_desc =
                  err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
              return RecordCompileFailure(
                  stage, str::format("MSC geometry function lookup failed for ",
                                     function_name, ": ",
                                     err_desc ? err_desc : "unknown",
                                     "; metallib ", metallib_path));
            }
            return true;
          };

          auto load_msc_plain_function =
              [&](const std::string &metallib_path, const char *function_name,
                  const char *stage,
                  WMT::Reference<WMT::Function> &out_func) -> bool {
            std::vector<uint8_t> lib_data;
            if (!read_binary(metallib_path, lib_data))
              return RecordCompileFailure(
                  stage, str::format("Failed to read MSC metallib ",
                                     metallib_path));
            auto dispatch_data =
                WMT::MakeDispatchData(lib_data.data(), lib_data.size());
            WMT::Reference<WMT::Error> lib_err;
            auto library = wmt_device.newLibrary(dispatch_data, lib_err);
            if (lib_err.handle) {
              auto err_desc_string = lib_err.description().getUTF8String();
              const char *err_desc =
                  err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
              return RecordCompileFailure(
                  stage, str::format("MSC geometry linked library failed: ",
                                     err_desc ? err_desc : "unknown",
                                     "; metallib ", metallib_path));
            }

            WMT::Reference<WMT::Error> fn_err;
            out_func = library.newFunctionWithDescriptor(
                function_name, nullptr, constants, std::size(constants),
                WMTFunctionOptionCompileToBinary, fn_err);
            std::string fn_error_desc =
                fn_err.handle ? fn_err.description().getUTF8String()
                              : std::string();
            if (!out_func.handle) {
              std::string visible_ref_name =
                  std::string(function_name) + ".MTL_VISIBLE_FN_REF";
              WMT::Reference<WMT::Error> visible_fn_err;
              out_func = library.newFunctionWithDescriptor(
                  visible_ref_name.c_str(), nullptr, constants,
                  std::size(constants), WMTFunctionOptionCompileToBinary,
                  visible_fn_err);
              if (!out_func.handle && visible_fn_err.handle)
                fn_error_desc = visible_fn_err.description().getUTF8String();
            }
            if (!out_func.handle)
              return RecordCompileFailure(
                  stage, str::format("MSC geometry linked function lookup "
                                     "failed for ",
                                     function_name, ": ",
                                     fn_error_desc.empty()
                                         ? "unknown"
                                         : fn_error_desc,
                                     "; metallib ", metallib_path));
            return true;
          };

          WMT::Reference<WMT::Function> geometry_vs_func;
          WMT::Reference<WMT::Function> geometry_gs_func;
          WMT::Reference<WMT::Function> stage_in_linked_func;
          if (!load_msc_function(vs_metallib_path, object_entry.c_str(),
                                 "shader/geometry_msc_object_function",
                                 geometry_vs_func) ||
              !load_msc_function(gs_metallib_path, gs_entry.c_str(),
                                 "shader/geometry_msc_mesh_function",
                                 geometry_gs_func) ||
              !load_msc_plain_function(
                  vs_metallib_path, "irconverter_stage_in_shader",
                  "shader/geometry_msc_stage_in_function",
                  stage_in_linked_func)) {
            return false;
          }

          MTL_SHADER_REFLECTION reflected_vs = {};
          MTL_SHADER_REFLECTION reflected_gs = {};
          reflected_vs.ConstanttBufferTableBindIndex = ~0u;
          reflected_vs.ArgumentBufferBindIndex = ~0u;
          reflected_gs.ConstanttBufferTableBindIndex = ~0u;
          reflected_gs.ArgumentBufferBindIndex = ~0u;
          m_vs_args = ParseTopLevelArgumentBufferReflection(vs_reflection_text,
                                                            reflected_vs);
          m_gs_args = ParseTopLevelArgumentBufferReflection(gs_reflection_text,
                                                            reflected_gs);
          m_vs_reflection = reflected_vs;
          m_gs_reflection = reflected_gs;

          WMT::Reference<WMT::Function> geometry_ps_func;
          if (!m_ps.empty() &&
              !CompileShader(m_ps.data(), m_ps.size(), ShaderType::Pixel,
                             "ps_main", geometry_ps_func, &m_ps_shader,
                             &m_ps_reflection)) {
            return false;
          }

          WMTMeshRenderPipelineInfo mesh_info;
          WMT::InitializeMeshRenderPipelineInfo(mesh_info);
          mesh_info.object_function = geometry_vs_func.handle;
          mesh_info.mesh_function = geometry_gs_func.handle;
          if (geometry_ps_func.handle)
            mesh_info.fragment_function = geometry_ps_func.handle;
          obj_handle_t mesh_linked_functions[] = {stage_in_linked_func.handle};
          mesh_info.object_linked_functions.set(mesh_linked_functions);
          mesh_info.num_object_linked_functions =
              std::size(mesh_linked_functions);
          mesh_info.mesh_linked_functions.set(mesh_linked_functions);
          mesh_info.num_mesh_linked_functions = std::size(mesh_linked_functions);
          mesh_info.payload_memory_length = payload_size;
          mesh_info.immutable_object_buffers =
              (1 << 16) | (1 << 21) | (1 << 29) | (1 << 30);
          mesh_info.immutable_mesh_buffers = (1 << 29) | (1 << 30);
          mesh_info.immutable_fragment_buffers = (1 << 29) | (1 << 30);
          mesh_info.rasterization_enabled =
              (m_rasterizer_desc.FillMode != D3D12_FILL_MODE_WIREFRAME);
          mesh_info.raster_sample_count = m_sample_count ? m_sample_count : 1;

          for (UINT i = 0; i < m_num_render_targets && i < 8; i++) {
            auto fmt = DXGIToMTLPixelFormat(m_rtv_formats[i]);
            if (fmt != WMTPixelFormatInvalid)
              mesh_info.colors[i].pixel_format = fmt;
            auto &rt = m_blend_desc.RenderTarget[i];
            mesh_info.colors[i].write_mask =
                kColorWriteMaskMap[rt.RenderTargetWriteMask & 0xf];
            mesh_info.colors[i].blending_enabled =
                rt.BlendEnable ? true : false;
            if (rt.BlendEnable) {
              mesh_info.colors[i].src_rgb_blend_factor =
                  D3D12BlendToWMT(rt.SrcBlend);
              mesh_info.colors[i].dst_rgb_blend_factor =
                  D3D12BlendToWMT(rt.DestBlend);
              mesh_info.colors[i].rgb_blend_operation =
                  D3D12BlendOpToWMT(rt.BlendOp);
              mesh_info.colors[i].src_alpha_blend_factor =
                  D3D12BlendToWMT(rt.SrcBlendAlpha);
              mesh_info.colors[i].dst_alpha_blend_factor =
                  D3D12BlendToWMT(rt.DestBlendAlpha);
              mesh_info.colors[i].alpha_blend_operation =
                  D3D12BlendOpToWMT(rt.BlendOpAlpha);
            }
          }

          auto depth_fmt = DXGIToMTLPixelFormat(m_dsv_format);
          if (depth_fmt != WMTPixelFormatInvalid) {
            mesh_info.depth_pixel_format = depth_fmt;
            if (m_dsv_format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                m_dsv_format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
              mesh_info.stencil_pixel_format = depth_fmt;
          }

          WMT::Reference<WMT::Error> mesh_err;
          m_render_pso = wmt_device.newRenderPipelineState(mesh_info, mesh_err);
          if (!m_render_pso.handle) {
            auto err_desc_string =
                mesh_err.handle ? mesh_err.description().getUTF8String()
                                : std::string();
            const char *err_desc =
                err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
            return RecordCompileFailure(
                "pso/metal_geometry_msc_mesh_pso",
                str::format("Metal MSC geometry mesh PSO creation failed: ",
                            err_desc ? err_desc : "unknown",
                            "; object=", object_entry, "; mesh=", gs_entry,
                            "; payload=", payload_size,
                            "; vertex_output=", vertex_output_size));
          }

          if (m_ps_shader) {
            if (m_ps_reflection.NumConstantBuffers > 0 &&
                m_ps_cb_args.empty())
              m_ps_cb_args.resize(m_ps_reflection.NumConstantBuffers);
            if (m_ps_reflection.NumArguments > 0 && m_ps_args.empty())
              m_ps_args.resize(m_ps_reflection.NumArguments);
            if (!m_ps_cb_args.empty() || !m_ps_args.empty()) {
              SM50GetArgumentsInfo(m_ps_shader,
                                   m_ps_cb_args.empty() ? nullptr
                                                        : m_ps_cb_args.data(),
                                   m_ps_args.empty() ? nullptr
                                                     : m_ps_args.data());
            }
            SM50Destroy(m_ps_shader);
            m_ps_shader = nullptr;
          }

          if (m_depth_stencil_desc.DepthEnable ||
              m_depth_stencil_desc.StencilEnable) {
            struct WMTDepthStencilInfo ds_info = {};
            ds_info.depth_compare_function = WMTCompareFunctionAlways;
            ds_info.depth_write_enabled = false;
            ds_info.front_stencil.enabled = false;
            ds_info.back_stencil.enabled = false;
            if (m_depth_stencil_desc.DepthFunc >= D3D12_COMPARISON_FUNC_LESS &&
                m_depth_stencil_desc.DepthFunc <=
                    D3D12_COMPARISON_FUNC_ALWAYS) {
              ds_info.depth_compare_function =
                  kCompareFunctionMap[m_depth_stencil_desc.DepthFunc];
            }
            ds_info.depth_write_enabled =
                m_depth_stencil_desc.DepthEnable &&
                m_depth_stencil_desc.DepthWriteMask ==
                    D3D12_DEPTH_WRITE_MASK_ALL;
            m_depth_stencil_state = wmt_device.newDepthStencilState(ds_info);
          }

          m_uses_geometry_mesh_pipeline = true;
          Logger::info(str::format(
              "Graphics DXIL geometry MSC mesh PSO compiled: object=",
              object_entry, " mesh=", gs_entry, " payload=", payload_size,
              " vertex_output=", vertex_output_size, " RTs=",
              m_num_render_targets, " DSV=", (int)m_dsv_format));
          return true;
        }

        auto init_shader = [&](const void *bytecode, SIZE_T bytecode_size,
                               const char *label, sm50_shader_t *shader,
                               MTL_SHADER_REFLECTION *reflection) -> bool {
          sm50_error_t sm50_err = nullptr;
          if (SM50Initialize(bytecode, bytecode_size, shader, reflection,
                             &sm50_err)) {
            char err_buf[256] = {};
            SM50GetErrorMessage(sm50_err, err_buf, sizeof(err_buf));
            SM50FreeError(sm50_err);
            return RecordCompileFailure(
                "shader/geometry_sm50_init",
                str::format(label, " SM50Initialize failed: ", err_buf));
          }
          return true;
        };

        auto load_sm50_function =
            [&](sm50_bitcode_t bitcode, const char *func_name,
                const char *stage,
                WMT::Reference<WMT::Function> &out_func) -> bool {
          SM50_COMPILED_BITCODE compiled = {};
          SM50GetCompiledBitcode(bitcode, &compiled);
          auto lib_data = WMT::MakeDispatchData(compiled.Data, compiled.Size);
          WMT::Reference<WMT::Error> lib_err;
          auto library = wmt_device.newLibrary(lib_data, lib_err);
          if (lib_err.handle) {
            auto err_desc_string = lib_err.description().getUTF8String();
            const char *err_desc =
                err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
            SM50DestroyBitcode(bitcode);
            return RecordCompileFailure(
                stage, str::format(func_name,
                                   " geometry Metal library failed: ",
                                   err_desc ? err_desc : "unknown"));
          }

          out_func = library.newFunction(func_name);
          SM50DestroyBitcode(bitcode);
          if (!out_func.handle) {
            return RecordCompileFailure(
                stage, str::format(func_name,
                                   " geometry Metal function lookup failed"));
          }
          return true;
        };

        if (!init_shader(m_vs.data(), m_vs.size(), "geometry VS", &m_vs_shader,
                         &m_vs_reflection) ||
            !init_shader(m_gs.data(), m_gs.size(), "geometry GS", &m_gs_shader,
                         &m_gs_reflection)) {
          if (m_vs_shader) {
            SM50Destroy(m_vs_shader);
            m_vs_shader = nullptr;
          }
          if (m_gs_shader) {
            SM50Destroy(m_gs_shader);
            m_gs_shader = nullptr;
          }
          return false;
        }

        SM50_SHADER_COMMON_DATA common = {};
        common.next = nullptr;
        common.type = SM50_SHADER_COMMON;
        common.metal_version = SM50_SHADER_METAL_310;
        common.flags = {};

        std::vector<SM50_IA_INPUT_ELEMENT> ia_elements;
        uint32_t slot_mask = 0;
        BuildIAInputLayout(m_vs.data(), m_vs.size(), ia_elements, slot_mask);
        m_ia_slot_mask = slot_mask;

        SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout = {};
        ia_layout.next = &common;
        ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
        ia_layout.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_NONE;
        ia_layout.slot_mask = slot_mask;
        ia_layout.num_elements = (uint32_t)ia_elements.size();
        ia_layout.elements = ia_elements.data();

        SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geometry_for_vs = {};
        geometry_for_vs.next = &ia_layout;
        geometry_for_vs.type = SM50_SHADER_PSO_GEOMETRY_SHADER;
        geometry_for_vs.strip_topology = false;

        WMT::Reference<WMT::Function> geometry_vs_func;
        sm50_error_t sm50_err = nullptr;
        sm50_bitcode_t geometry_vs_bitcode = nullptr;
        if (SM50CompileGeometryPipelineVertex(
                m_vs_shader, m_gs_shader,
                (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&geometry_for_vs,
                "vsgs_main", &geometry_vs_bitcode, &sm50_err)) {
          char err_buf[256] = {};
          SM50GetErrorMessage(sm50_err, err_buf, sizeof(err_buf));
          SM50FreeError(sm50_err);
          SM50Destroy(m_vs_shader);
          SM50Destroy(m_gs_shader);
          m_vs_shader = nullptr;
          m_gs_shader = nullptr;
          return RecordCompileFailure(
              "shader/geometry_vertex_compile",
              str::format("vsgs_main SM50 geometry vertex compile failed: ",
                          err_buf));
        }
        if (!load_sm50_function(geometry_vs_bitcode, "vsgs_main",
                                "shader/geometry_vertex_metallib",
                                geometry_vs_func)) {
          SM50Destroy(m_vs_shader);
          SM50Destroy(m_gs_shader);
          m_vs_shader = nullptr;
          m_gs_shader = nullptr;
          return false;
        }

        SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geometry_for_gs = {};
        geometry_for_gs.next = &common;
        geometry_for_gs.type = SM50_SHADER_PSO_GEOMETRY_SHADER;
        geometry_for_gs.strip_topology = false;

        WMT::Reference<WMT::Function> geometry_gs_func;
        sm50_bitcode_t geometry_gs_bitcode = nullptr;
        sm50_err = nullptr;
        if (SM50CompileGeometryPipelineGeometry(
                m_vs_shader, m_gs_shader,
                (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&geometry_for_gs,
                "gs_main", &geometry_gs_bitcode, &sm50_err)) {
          char err_buf[256] = {};
          SM50GetErrorMessage(sm50_err, err_buf, sizeof(err_buf));
          SM50FreeError(sm50_err);
          SM50Destroy(m_vs_shader);
          SM50Destroy(m_gs_shader);
          m_vs_shader = nullptr;
          m_gs_shader = nullptr;
          return RecordCompileFailure(
              "shader/geometry_mesh_compile",
              str::format("gs_main SM50 geometry mesh compile failed: ",
                          err_buf));
        }
        if (!load_sm50_function(geometry_gs_bitcode, "gs_main",
                                "shader/geometry_mesh_metallib",
                                geometry_gs_func)) {
          SM50Destroy(m_vs_shader);
          SM50Destroy(m_gs_shader);
          m_vs_shader = nullptr;
          m_gs_shader = nullptr;
          return false;
        }

        WMT::Reference<WMT::Function> geometry_ps_func;
        if (!m_ps.empty() &&
            !CompileShader(m_ps.data(), m_ps.size(), ShaderType::Pixel,
                           "ps_main", geometry_ps_func, &m_ps_shader,
                           &m_ps_reflection)) {
          SM50Destroy(m_vs_shader);
          SM50Destroy(m_gs_shader);
          m_vs_shader = nullptr;
          m_gs_shader = nullptr;
          return false;
        }

        WMTMeshRenderPipelineInfo mesh_info;
        WMT::InitializeMeshRenderPipelineInfo(mesh_info);
        mesh_info.object_function = geometry_vs_func.handle;
        mesh_info.mesh_function = geometry_gs_func.handle;
        if (geometry_ps_func.handle)
          mesh_info.fragment_function = geometry_ps_func.handle;
        mesh_info.payload_memory_length = 16256;
        mesh_info.immutable_object_buffers =
            (1 << 16) | (1 << 21) | (1 << 29) | (1 << 30);
        mesh_info.immutable_mesh_buffers = (1 << 29) | (1 << 30);
        mesh_info.immutable_fragment_buffers = (1 << 29) | (1 << 30);
        mesh_info.rasterization_enabled =
            (m_rasterizer_desc.FillMode != D3D12_FILL_MODE_WIREFRAME);
        mesh_info.raster_sample_count = m_sample_count ? m_sample_count : 1;

        for (UINT i = 0; i < m_num_render_targets && i < 8; i++) {
          auto fmt = DXGIToMTLPixelFormat(m_rtv_formats[i]);
          if (fmt != WMTPixelFormatInvalid)
            mesh_info.colors[i].pixel_format = fmt;
          auto &rt = m_blend_desc.RenderTarget[i];
          mesh_info.colors[i].write_mask =
              kColorWriteMaskMap[rt.RenderTargetWriteMask & 0xf];
          mesh_info.colors[i].blending_enabled = rt.BlendEnable ? true : false;
          if (rt.BlendEnable) {
            mesh_info.colors[i].src_rgb_blend_factor =
                D3D12BlendToWMT(rt.SrcBlend);
            mesh_info.colors[i].dst_rgb_blend_factor =
                D3D12BlendToWMT(rt.DestBlend);
            mesh_info.colors[i].rgb_blend_operation =
                D3D12BlendOpToWMT(rt.BlendOp);
            mesh_info.colors[i].src_alpha_blend_factor =
                D3D12BlendToWMT(rt.SrcBlendAlpha);
            mesh_info.colors[i].dst_alpha_blend_factor =
                D3D12BlendToWMT(rt.DestBlendAlpha);
            mesh_info.colors[i].alpha_blend_operation =
                D3D12BlendOpToWMT(rt.BlendOpAlpha);
          }
        }

        auto depth_fmt = DXGIToMTLPixelFormat(m_dsv_format);
        if (depth_fmt != WMTPixelFormatInvalid) {
          mesh_info.depth_pixel_format = depth_fmt;
          if (m_dsv_format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
              m_dsv_format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
            mesh_info.stencil_pixel_format = depth_fmt;
        }

        WMT::Reference<WMT::Error> mesh_err;
        m_render_pso = wmt_device.newRenderPipelineState(mesh_info, mesh_err);
        if (!m_render_pso.handle) {
          auto err_desc_string =
              mesh_err.handle ? mesh_err.description().getUTF8String()
                              : std::string();
          const char *err_desc =
              err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
          SM50Destroy(m_vs_shader);
          SM50Destroy(m_gs_shader);
          m_vs_shader = nullptr;
          m_gs_shader = nullptr;
          return RecordCompileFailure(
              "pso/metal_geometry_mesh_pso",
              str::format("Metal geometry mesh PSO creation failed: ",
                          err_desc ? err_desc : "unknown"));
        }

        if (m_vs_reflection.NumConstantBuffers > 0)
          m_vs_cb_args.resize(m_vs_reflection.NumConstantBuffers);
        if (m_vs_reflection.NumArguments > 0)
          m_vs_args.resize(m_vs_reflection.NumArguments);
        if (m_gs_reflection.NumConstantBuffers > 0)
          m_gs_cb_args.resize(m_gs_reflection.NumConstantBuffers);
        if (m_gs_reflection.NumArguments > 0)
          m_gs_args.resize(m_gs_reflection.NumArguments);
        SM50GetArgumentsInfo(m_vs_shader,
                             m_vs_cb_args.empty() ? nullptr
                                                  : m_vs_cb_args.data(),
                             m_vs_args.empty() ? nullptr : m_vs_args.data());
        SM50GetArgumentsInfo(m_gs_shader,
                             m_gs_cb_args.empty() ? nullptr
                                                  : m_gs_cb_args.data(),
                             m_gs_args.empty() ? nullptr : m_gs_args.data());
        SM50Destroy(m_vs_shader);
        SM50Destroy(m_gs_shader);
        m_vs_shader = nullptr;
        m_gs_shader = nullptr;

        if (m_ps_shader) {
          if (m_ps_reflection.NumConstantBuffers > 0 &&
              m_ps_cb_args.empty())
            m_ps_cb_args.resize(m_ps_reflection.NumConstantBuffers);
          if (m_ps_reflection.NumArguments > 0 && m_ps_args.empty())
            m_ps_args.resize(m_ps_reflection.NumArguments);
          if (!m_ps_cb_args.empty() || !m_ps_args.empty()) {
            SM50GetArgumentsInfo(
                m_ps_shader, m_ps_cb_args.empty() ? nullptr : m_ps_cb_args.data(),
                m_ps_args.empty() ? nullptr : m_ps_args.data());
          }
          SM50Destroy(m_ps_shader);
          m_ps_shader = nullptr;
        }

        if (m_depth_stencil_desc.DepthEnable ||
            m_depth_stencil_desc.StencilEnable) {
          struct WMTDepthStencilInfo ds_info = {};
          ds_info.depth_compare_function = WMTCompareFunctionAlways;
          ds_info.depth_write_enabled = false;
          ds_info.front_stencil.enabled = false;
          ds_info.back_stencil.enabled = false;
          if (m_depth_stencil_desc.DepthFunc >= D3D12_COMPARISON_FUNC_LESS &&
              m_depth_stencil_desc.DepthFunc <=
                  D3D12_COMPARISON_FUNC_ALWAYS) {
            ds_info.depth_compare_function =
                kCompareFunctionMap[m_depth_stencil_desc.DepthFunc];
          }
          ds_info.depth_write_enabled =
              m_depth_stencil_desc.DepthEnable &&
              m_depth_stencil_desc.DepthWriteMask ==
                  D3D12_DEPTH_WRITE_MASK_ALL;
          m_depth_stencil_state = wmt_device.newDepthStencilState(ds_info);
        }

        m_uses_geometry_mesh_pipeline = true;
        Logger::info(str::format("Graphics geometry mesh PSO compiled: RTs=",
                                 m_num_render_targets, " DSV=",
                                 (int)m_dsv_format, " samples=",
                                 m_sample_count));
        return true;
      }

      std::string detail = str::format(
          "CreateGraphicsPipelineState: dropping unsupported GS bytes=",
          m_gs.size(), " hash=0x", str::format("%016zx", gs_hash), " magic=",
          DescribeShaderBlobMagic(m_gs.data(), m_gs.size()), " reason=",
          gs_reason.empty() ? "unknown" : gs_reason, " dumped=",
          gs_dump_path);
      Logger::warn(detail);
      PSTRACE("Graphics PSO GS dropped hash=0x%016zx detail=%s", gs_hash,
              gs_reason.c_str());
      return RecordCompileFailure("pso/unsupported_geometry_shader", detail);
    }
  }

  if (m_has_stream_output) {
    return RecordCompileFailure(
        "pso/unsupported_stream_output",
        "Graphics PSO uses stream output, which is not implemented");
  }

  if (!m_vs.empty()) {
    if (!CompileShader(m_vs.data(), m_vs.size(), ShaderType::Vertex, "vs_main",
                       vs_func, &m_vs_shader, &m_vs_reflection))
      return false;
  }

  if (!m_ps.empty()) {
    if (!CompileShader(m_ps.data(), m_ps.size(), ShaderType::Pixel, "ps_main",
                       ps_func, &m_ps_shader, &m_ps_reflection))
      return false;
  }

  WMTRenderPipelineInfo info;
  WMT::InitializeRenderPipelineInfo(info);

  if (vs_func.handle)
    info.vertex_function = vs_func.handle;
  if (ps_func.handle)
    info.fragment_function = ps_func.handle;

  info.rasterization_enabled =
      (m_rasterizer_desc.FillMode != D3D12_FILL_MODE_WIREFRAME);
  info.raster_sample_count = m_sample_count ? m_sample_count : 1;

  for (UINT i = 0; i < m_num_render_targets && i < 8; i++) {
    auto fmt = DXGIToMTLPixelFormat(m_rtv_formats[i]);
    if (fmt != WMTPixelFormatInvalid)
      info.colors[i].pixel_format = fmt;
  }

  auto depth_fmt = DXGIToMTLPixelFormat(m_dsv_format);
  if (depth_fmt != WMTPixelFormatInvalid) {
    info.depth_pixel_format = depth_fmt;
    if (m_dsv_format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
        m_dsv_format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
      info.stencil_pixel_format = depth_fmt;
  }

  auto map_blend = [](D3D12_BLEND b) -> WMTBlendFactor {
    switch (b) {
    case D3D12_BLEND_ZERO:
      return WMTBlendFactorZero;
    case D3D12_BLEND_ONE:
      return WMTBlendFactorOne;
    case D3D12_BLEND_SRC_COLOR:
      return WMTBlendFactorSourceColor;
    case D3D12_BLEND_INV_SRC_COLOR:
      return WMTBlendFactorOneMinusSourceColor;
    case D3D12_BLEND_SRC_ALPHA:
      return WMTBlendFactorSourceAlpha;
    case D3D12_BLEND_INV_SRC_ALPHA:
      return WMTBlendFactorOneMinusSourceAlpha;
    case D3D12_BLEND_DEST_ALPHA:
      return WMTBlendFactorDestinationAlpha;
    case D3D12_BLEND_INV_DEST_ALPHA:
      return WMTBlendFactorOneMinusDestinationAlpha;
    case D3D12_BLEND_DEST_COLOR:
      return WMTBlendFactorDestinationColor;
    case D3D12_BLEND_INV_DEST_COLOR:
      return WMTBlendFactorOneMinusDestinationColor;
    case D3D12_BLEND_SRC_ALPHA_SAT:
      return WMTBlendFactorSourceAlphaSaturated;
    case D3D12_BLEND_BLEND_FACTOR:
      return WMTBlendFactorBlendColor;
    case D3D12_BLEND_INV_BLEND_FACTOR:
      return WMTBlendFactorOneMinusBlendColor;
    default:
      return WMTBlendFactorOne;
    }
  };

  auto map_op = [](D3D12_BLEND_OP op) -> WMTBlendOperation {
    switch (op) {
    case D3D12_BLEND_OP_ADD:
      return WMTBlendOperationAdd;
    case D3D12_BLEND_OP_SUBTRACT:
      return WMTBlendOperationSubtract;
    case D3D12_BLEND_OP_REV_SUBTRACT:
      return WMTBlendOperationReverseSubtract;
    case D3D12_BLEND_OP_MIN:
      return WMTBlendOperationMin;
    case D3D12_BLEND_OP_MAX:
      return WMTBlendOperationMax;
    default:
      return WMTBlendOperationAdd;
    }
  };

  for (UINT i = 0; i < m_num_render_targets && i < 8; i++) {
    auto &rt = m_blend_desc.RenderTarget[i];
    info.colors[i].write_mask =
        kColorWriteMaskMap[rt.RenderTargetWriteMask & 0xf];
    info.colors[i].blending_enabled = rt.BlendEnable ? true : false;

    if (!rt.BlendEnable)
      continue;

    info.colors[i].src_rgb_blend_factor = map_blend(rt.SrcBlend);
    info.colors[i].dst_rgb_blend_factor = map_blend(rt.DestBlend);
    info.colors[i].rgb_blend_operation = map_op(rt.BlendOp);
    info.colors[i].src_alpha_blend_factor = map_blend(rt.SrcBlendAlpha);
    info.colors[i].dst_alpha_blend_factor = map_blend(rt.DestBlendAlpha);
    info.colors[i].alpha_blend_operation = map_op(rt.BlendOpAlpha);
  }

  switch (m_topology) {
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
    info.input_primitive_topology = WMTPrimitiveTopologyClassPoint;
    break;
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
    info.input_primitive_topology = WMTPrimitiveTopologyClassLine;
    break;
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
    info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle;
    break;
  default:
    info.input_primitive_topology = WMTPrimitiveTopologyClassUnspecified;
    break;
  }

  info.immutable_vertex_buffers = (1 << 16) | (1 << 29) | (1 << 30);
  info.immutable_fragment_buffers = (1 << 29) | (1 << 30);

  WMTVertexDescriptor vtx_desc = {};
  if (m_input_layout.NumElements > 0 && m_input_layout.pInputElementDescs) {
    uint32_t append_offset[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    uint32_t slot_stride[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    uint32_t max_slot = 0;
    bool slot_per_vertex[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    uint32_t attribute_count = 0;
    uint32_t next_attribute = 0;
    bool attribute_present[WMT_MAX_VERTEX_ATTRIBUTES] = {};
    struct InputLayoutSource {
      UINT desc_index = 0;
      const D3D12_INPUT_ELEMENT_DESC *element = nullptr;
      MTL_DXGI_FORMAT_DESC metal_format = {};
      uint32_t aligned_offset = 0;
      uint32_t end = 0;
    };
    std::vector<InputLayoutSource> input_sources;
    const microsoft::D3D11_SIGNATURE_PARAMETER *input_sig_params = nullptr;
    uint32_t input_sig_count = 0;
    microsoft::CSignatureParser input_sig_parser;
    bool has_input_signature =
        !m_vs.empty() &&
        SUCCEEDED(DXBCGetInputSignature(m_vs.data(), &input_sig_parser));
    if (has_input_signature) {
      input_sig_count = input_sig_parser.GetParameters(&input_sig_params);
      PSTRACE("D3D12 PSO input-layout: shader input signature params=%u",
              input_sig_count);
    } else {
      PSTRACE("D3D12 PSO input-layout: shader input signature unavailable; "
              "using layout order");
    }

    PSTRACE("D3D12 PSO input-layout: elements=%u metal_attr_cap=%u "
            "metal_slot_cap=%u",
            m_input_layout.NumElements, WMT_MAX_VERTEX_ATTRIBUTES,
            kMetalD3D12VertexBufferSlotCount);

    for (UINT i = 0; i < m_input_layout.NumElements; i++) {
      auto &el = m_input_layout.pInputElementDescs[i];

      MTL_DXGI_FORMAT_DESC metal_format = {};
      if (FAILED(MTLQueryDXGIFormat(m_device->GetMTLDevice(), el.Format,
                                    metal_format)) ||
          !metal_format.AttributeFormat || !metal_format.BytesPerTexel) {
        PSTRACE(
            "D3D12 PSO input-layout skip[%u]: unsupported fmt=%u semantic=%s%u",
            i, (unsigned)el.Format, el.SemanticName ? el.SemanticName : "?",
            el.SemanticIndex);
        continue;
      }

      if (el.InputSlot >= kMetalD3D12VertexBufferSlotCount) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: input slot %u is outside "
                "Metal-backed slot cap %u",
                i, el.InputSlot, kMetalD3D12VertexBufferSlotCount);
        continue;
      }

      if (attribute_count >= WMT_MAX_VERTEX_ATTRIBUTES) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: attribute cap %u reached", i,
                WMT_MAX_VERTEX_ATTRIBUTES);
        continue;
      }

      uint32_t attr_index = next_attribute;
      const StageInVertexAttributeInfo *stage_in_attr = nullptr;
      if (m_vs_uses_stage_in && el.SemanticName) {
        auto semantic_match = std::find_if(
            m_vs_stage_in_attribute_order.begin(),
            m_vs_stage_in_attribute_order.end(),
            [&](const StageInVertexAttributeInfo &attr) {
              return StageInSemanticMatchesInputElement(attr, el);
            });
        if (semantic_match != m_vs_stage_in_attribute_order.end()) {
          stage_in_attr = &*semantic_match;
          attr_index = stage_in_attr->attribute_index;
          PSTRACE("D3D12 PSO input-layout desc[%u]: semantic %s%u matched "
                  "DXIL reflection name %s -> metal attribute %u",
                  i, el.SemanticName ? el.SemanticName : "?",
                  el.SemanticIndex, stage_in_attr->semantic_name.c_str(),
                  attr_index);
        }
      }
      if (has_input_signature && input_sig_params) {
        auto *sig = std::find_if(
            input_sig_params, input_sig_params + input_sig_count,
            [&](const microsoft::D3D11_SIGNATURE_PARAMETER &input_sig) {
              return input_sig.SystemValue ==
                         microsoft::D3D10_SB_NAME_UNDEFINED &&
                     el.SemanticName && input_sig.SemanticName &&
                     el.SemanticIndex == input_sig.SemanticIndex &&
                     strcasecmp(el.SemanticName, input_sig.SemanticName) == 0;
            });
        if (sig != input_sig_params + input_sig_count) {
          if (!stage_in_attr)
            attr_index = sig->Register;
          if (m_vs_uses_stage_in && !stage_in_attr) {
            auto remap = m_vs_stage_in_register_map.find(sig->Register);
            if (remap != m_vs_stage_in_register_map.end()) {
              stage_in_attr = &remap->second;
              attr_index = stage_in_attr->attribute_index;
              PSTRACE("D3D12 PSO input-layout desc[%u]: semantic %s%u register "
                      "%u remapped to metal attribute %u",
                      i, el.SemanticName ? el.SemanticName : "?",
                      el.SemanticIndex, sig->Register, attr_index);
            } else {
              PSTRACE("D3D12 PSO input-layout desc[%u]: semantic %s%u register "
                      "%u missing DXIL reflection remap; using register index",
                      i, el.SemanticName ? el.SemanticName : "?",
                      el.SemanticIndex, sig->Register);
            }
          }
        } else {
          PSTRACE("D3D12 PSO input-layout desc[%u]: semantic %s%u not found in "
                  "input signature; using attr order %u",
                  i, el.SemanticName ? el.SemanticName : "?", el.SemanticIndex,
                  attr_index);
        }
      }
      if (!stage_in_attr && m_vs_uses_stage_in &&
          i < m_vs_stage_in_attribute_order.size()) {
        stage_in_attr = &m_vs_stage_in_attribute_order[i];
        attr_index = stage_in_attr->attribute_index;
        PSTRACE("D3D12 PSO input-layout desc[%u]: fallback stage_in order maps "
                "to attribute %u",
                i, attr_index);
      }

      if (attr_index >= WMT_MAX_VERTEX_ATTRIBUTES) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: mapped attribute %u outside "
                "cap %u",
                i, attr_index, WMT_MAX_VERTEX_ATTRIBUTES);
        continue;
      }
      next_attribute = std::max(next_attribute, attr_index + 1);

      uint32_t aligned_offset =
          el.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
              ? AlignD3D12InputOffset(append_offset[el.InputSlot],
                                      metal_format.BytesPerTexel)
              : el.AlignedByteOffset;
      uint32_t end = aligned_offset + metal_format.BytesPerTexel;
      append_offset[el.InputSlot] = end;
      if (end > slot_stride[el.InputSlot])
        slot_stride[el.InputSlot] = end;
      if (el.InputSlot >= max_slot)
        max_slot = el.InputSlot + 1;
      slot_per_vertex[el.InputSlot] =
          (el.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);

      input_sources.push_back({i, &el, metal_format, aligned_offset, end});

      if (attribute_present[attr_index]) {
        const auto &existing_attr = vtx_desc.attributes[attr_index];
        PSTRACE("D3D12 PSO input-layout desc[%u]: mapped attribute %u already "
                "bound as fmt=%u slot=%u offset=%u; keeping first binding "
                "and leaving duplicate semantic=%s%u as fallback source",
                i, attr_index, (unsigned)existing_attr.format,
                existing_attr.buffer_index, existing_attr.offset,
                el.SemanticName ? el.SemanticName : "?", el.SemanticIndex);
        continue;
      }

      auto &attr = vtx_desc.attributes[attr_index];
      attr.format =
          stage_in_attr && stage_in_attr->format != WMTAttributeFormatInvalid
              ? stage_in_attr->format
              : metal_format.AttributeFormat;
      attr.offset = aligned_offset;
      attr.buffer_index = el.InputSlot;
      attribute_present[attr_index] = true;
      attribute_count = std::max(attribute_count, attr_index + 1);

      PSTRACE("D3D12 PSO input-layout attr[%u]<-desc[%u]: semantic=%s%u fmt=%u "
              "mtl_fmt=%u chosen_fmt=%u slot=%u offset=%u stride_end=%u "
              "class=%u step=%u",
              attr_index, i, el.SemanticName ? el.SemanticName : "?",
              el.SemanticIndex, (unsigned)el.Format,
              (unsigned)metal_format.AttributeFormat, (unsigned)attr.format,
              el.InputSlot, aligned_offset, end, (unsigned)el.InputSlotClass,
              el.InstanceDataStepRate);
    }

    if (m_vs_uses_stage_in && !input_sources.empty()) {
      auto find_source_for_stage_in =
          [&](const StageInVertexAttributeInfo &stage_in_attr,
              size_t order_index) -> const InputLayoutSource * {
        for (const auto &source : input_sources) {
          const auto &el = *source.element;
          if (!el.SemanticName)
            continue;
          if (StageInSemanticMatchesInputElement(stage_in_attr, el)) {
            return &source;
          }
          if (strcasecmp(el.SemanticName, "ATTRIBUTE") == 0 &&
              el.SemanticIndex == stage_in_attr.register_index) {
            return &source;
          }
        }
        if (order_index < input_sources.size())
          return &input_sources[order_index];
        return &input_sources.back();
      };

      for (size_t order_i = 0; order_i < m_vs_stage_in_attribute_order.size();
           order_i++) {
        const auto &stage_in_attr = m_vs_stage_in_attribute_order[order_i];
        const uint32_t attr_index = stage_in_attr.attribute_index;
        if (attr_index >= WMT_MAX_VERTEX_ATTRIBUTES)
          continue;
        if (attribute_present[attr_index]) {
          auto &attr = vtx_desc.attributes[attr_index];
          if (stage_in_attr.format != WMTAttributeFormatInvalid &&
              attr.format != stage_in_attr.format) {
            PSTRACE("D3D12 PSO input-layout attr[%u]: overriding descriptor "
                    "format %u with DXIL stage_in format %u for semantic=%s "
                    "register=%u",
                    attr_index, (unsigned)attr.format,
                    (unsigned)stage_in_attr.format,
                    stage_in_attr.semantic_name.c_str(),
                    stage_in_attr.register_index);
            attr.format = stage_in_attr.format;
          }
          continue;
        }

        const InputLayoutSource *source =
            find_source_for_stage_in(stage_in_attr, order_i);
        if (!source || !source->element)
          continue;

        const auto &el = *source->element;
        auto &attr = vtx_desc.attributes[attr_index];
        attr.format = stage_in_attr.format != WMTAttributeFormatInvalid
                          ? stage_in_attr.format
                          : source->metal_format.AttributeFormat;
        attr.offset = source->aligned_offset;
        attr.buffer_index = el.InputSlot;
        attribute_present[attr_index] = true;
        attribute_count = std::max(attribute_count, attr_index + 1);

        PSTRACE("D3D12 PSO input-layout attr[%u]<-desc[%u]: filled missing "
                "DXIL stage_in register=%u semantic=%s%u chosen_fmt=%u "
                "slot=%u offset=%u",
                attr_index, source->desc_index, stage_in_attr.register_index,
                el.SemanticName ? el.SemanticName : "?", el.SemanticIndex,
                (unsigned)attr.format, el.InputSlot, source->aligned_offset);
      }

      if (m_vs_stage_in_attribute_order.empty()) {
        PSTRACE("D3D12 PSO input-layout: DXIL stage_in reflection did not "
                "publish attribute order; preserving input-layout attributes "
                "without synthetic padding");
      }
    }

    vtx_desc.attribute_count = attribute_count;
    vtx_desc.layout_count = max_slot;
    bool slot_used_by_attribute[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    for (uint32_t attr_i = 0; attr_i < attribute_count; attr_i++) {
      const auto &attr = vtx_desc.attributes[attr_i];
      if (attr.format == WMTAttributeFormatInvalid)
        continue;
      if (attr.buffer_index < WMT_MAX_VERTEX_BUFFER_LAYOUTS)
        slot_used_by_attribute[attr.buffer_index] = true;
    }
    for (uint32_t s = 0; s < max_slot; s++) {
      if (!slot_used_by_attribute[s]) {
        vtx_desc.layouts[s].stride = 0;
        vtx_desc.layouts[s].step_function = WMTVertexStepFunctionPerVertex;
        vtx_desc.layouts[s].step_rate = 1;
        if (slot_stride[s] != 0) {
          PSTRACE("D3D12 PSO input-layout slot[%u]: dropping unused layout "
                  "stride=%u because no bound Metal attribute references it",
                  s, slot_stride[s]);
        }
        continue;
      }

      vtx_desc.layouts[s].stride = slot_stride[s];
      vtx_desc.layouts[s].step_function =
          slot_per_vertex[s] ? WMTVertexStepFunctionPerVertex
                             : WMTVertexStepFunctionPerInstance;
      vtx_desc.layouts[s].step_rate = 1;
      PSTRACE("D3D12 PSO input-layout slot[%u]: stride=%u step=%u", s,
              slot_stride[s], (unsigned)vtx_desc.layouts[s].step_function);
    }
    if (m_vs_uses_stage_in && attribute_count > 0) {
      std::stringstream attr_summary;
      attr_summary << "D3D12 stage_in vertex descriptor attrs="
                   << attribute_count << " layouts=" << max_slot
                   << " has_input_signature=" << (has_input_signature ? 1 : 0)
                   << " reflection_order="
                   << m_vs_stage_in_attribute_order.size() << " present=[";
      bool first_attr = true;
      for (uint32_t attr_i = 0; attr_i < attribute_count; attr_i++) {
        const auto &attr = vtx_desc.attributes[attr_i];
        if (attr.format == WMTAttributeFormatInvalid)
          continue;
        if (!first_attr)
          attr_summary << ",";
        first_attr = false;
        attr_summary << attr_i << "@slot" << attr.buffer_index << "+";
        attr_summary << attr.offset << ":fmt" << (unsigned)attr.format;
      }
      attr_summary << "]";
      Logger::info(attr_summary.str());
      info.vertex_descriptor = &vtx_desc;
      PSTRACE("D3D12 PSO input-layout attached as Metal vertex descriptor for "
              "DXIL stage_in");
    } else {
      PSTRACE("D3D12 PSO input-layout compiled for SM50 vertex pulling; Metal "
              "vertex descriptor disabled");
    }
  }
  if (m_vs_uses_stage_in && !info.vertex_descriptor) {
    uint32_t attribute_count = 0;
    for (const auto &stage_in_attr : m_vs_stage_in_attribute_order) {
      if (stage_in_attr.attribute_index < WMT_MAX_VERTEX_ATTRIBUTES)
        attribute_count =
            std::max(attribute_count, stage_in_attr.attribute_index + 1);
    }
    attribute_count =
        std::min<uint32_t>(attribute_count, WMT_MAX_VERTEX_ATTRIBUTES);
    if (attribute_count == 0)
      attribute_count = 1;
    if (attribute_count > 0) {
      for (const auto &stage_in_attr : m_vs_stage_in_attribute_order) {
        const uint32_t attr_index = stage_in_attr.attribute_index;
        if (attr_index >= attribute_count)
          continue;
        auto &attr = vtx_desc.attributes[attr_index];
        attr.format = stage_in_attr.format != WMTAttributeFormatInvalid
                          ? stage_in_attr.format
                          : WMTAttributeFormatFloat4;
        attr.offset = attr_index * 16;
        attr.buffer_index = 0;
      }
      for (uint32_t attr_index = 0; attr_index < attribute_count; attr_index++) {
        auto &attr = vtx_desc.attributes[attr_index];
        if (attr.format != WMTAttributeFormatInvalid)
          continue;
        attr.format = WMTAttributeFormatFloat4;
        attr.offset = attr_index * 16;
        attr.buffer_index = 0;
      }
      vtx_desc.attribute_count = attribute_count;
      vtx_desc.layout_count = 1;
      vtx_desc.layouts[0].stride = std::max<uint32_t>(16, attribute_count * 16);
      vtx_desc.layouts[0].step_function = WMTVertexStepFunctionPerVertex;
      vtx_desc.layouts[0].step_rate = 1;
      info.vertex_descriptor = &vtx_desc;
      Logger::info(str::format(
          "D3D12 stage_in fallback vertex descriptor attrs=", attribute_count,
          " layouts=1 has_input_layout=0"));
    }
  }

  PSTRACE(
      "D3D12 PSO state this=%p rts=%u dsv_fmt=%u depth=%u stencil=%u blend0=%u "
      "write_mask0=0x%x cull=%u fill=%u front_ccw=%u depth_clip=%u",
      (void *)this, m_num_render_targets, (unsigned)m_dsv_format,
      (unsigned)m_depth_stencil_desc.DepthEnable,
      (unsigned)m_depth_stencil_desc.StencilEnable,
      (unsigned)m_blend_desc.RenderTarget[0].BlendEnable,
      (unsigned)m_blend_desc.RenderTarget[0].RenderTargetWriteMask,
      (unsigned)m_rasterizer_desc.CullMode,
      (unsigned)m_rasterizer_desc.FillMode,
      (unsigned)m_rasterizer_desc.FrontCounterClockwise,
      (unsigned)m_rasterizer_desc.DepthClipEnable);

  DXMTD3D12ScopedTimer metal_render_timer("PSO", "CreateMetalRenderPSO");
  metal_render_timer.SetDetail(
      "this=%p rts=%u dsv=%u depth=%u stencil=%u stage_in=%u il=%u",
      (void *)this, m_num_render_targets, (unsigned)m_dsv_format,
      (unsigned)m_depth_stencil_desc.DepthEnable,
      (unsigned)m_depth_stencil_desc.StencilEnable,
      m_vs_uses_stage_in ? 1u : 0u, m_input_layout.NumElements);
  m_render_pso = wmt_device.newRenderPipelineState(info, err);
  if (!m_render_pso.handle) {
    auto err_desc_string =
        err.handle ? err.description().getUTF8String() : std::string();
    const char *err_desc =
        err_desc_string.empty() ? "unknown" : err_desc_string.c_str();
    auto retry_render_pso = [&](const char *reason, auto mutate) -> bool {
      WMT::Reference<WMT::Error> retry_err;
      WMTRenderPipelineInfo retry_info = info;
      mutate(retry_info);
      auto retry_pso = wmt_device.newRenderPipelineState(retry_info, retry_err);
      if (retry_pso.handle) {
        Logger::warn(str::format("CreateGraphicsPipelineState: using bounded "
                                 "Metal render PSO fallback for ",
                                 reason ? reason : "link failure", ": ",
                                 err_desc ? err_desc : "unknown"));
        m_render_pso = std::move(retry_pso);
        return true;
      }
      auto retry_desc_string =
          retry_err.handle ? retry_err.description().getUTF8String()
                           : std::string();
      Logger::warn(str::format(
          "CreateGraphicsPipelineState: bounded Metal render PSO fallback failed for ",
          reason ? reason : "link failure", ": ",
          retry_desc_string.empty() ? "unknown" : retry_desc_string.c_str()));
      PSTRACE("D3D12 PSO bounded fallback failed for %s: %s",
              reason ? reason : "link failure",
              retry_desc_string.empty() ? "unknown" : retry_desc_string.c_str());
      return false;
    };

    std::string_view err_view(err_desc ? err_desc : "");
    if (err_view.find("vertex shader's return type is void") !=
        std::string_view::npos) {
      if (retry_render_pso("void vertex shader", [](auto &retry_info) {
            retry_info.rasterization_enabled = false;
            retry_info.fragment_function = 0;
          })) {
        return true;
      }
    } else if (err_view.find("mismatching vertex shader output") !=
                   std::string_view::npos ||
               err_view.find("not written by vertex shader") !=
                   std::string_view::npos) {
      if (retry_render_pso("VS/PS varying mismatch", [](auto &retry_info) {
            retry_info.fragment_function = 0;
          })) {
        return true;
      }
    }

    Logger::err(str::format("Failed to create render PSO: ",
                            err_desc ? err_desc : "unknown"));
    return RecordCompileFailure(
        "pso/metal_render_pso",
        str::format("Metal render PSO creation failed: ",
                    err_desc ? err_desc : "unknown"));
  }

  if (m_depth_stencil_desc.DepthEnable || m_depth_stencil_desc.StencilEnable) {
    struct WMTDepthStencilInfo ds_info = {};
    ds_info.depth_compare_function = WMTCompareFunctionAlways;
    ds_info.depth_write_enabled = false;
    ds_info.front_stencil.enabled = false;
    ds_info.back_stencil.enabled = false;
    if (m_depth_stencil_desc.DepthFunc >= D3D12_COMPARISON_FUNC_LESS &&
        m_depth_stencil_desc.DepthFunc <= D3D12_COMPARISON_FUNC_ALWAYS) {
      ds_info.depth_compare_function =
          kCompareFunctionMap[m_depth_stencil_desc.DepthFunc];
    }
    ds_info.depth_write_enabled =
        m_depth_stencil_desc.DepthEnable &&
        m_depth_stencil_desc.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL;
    if (m_depth_stencil_desc.StencilEnable) {
      ds_info.front_stencil.enabled = true;
      ds_info.front_stencil.depth_stencil_pass_op =
          kStencilOperationMap[m_depth_stencil_desc.FrontFace.StencilPassOp];
      ds_info.front_stencil.stencil_fail_op =
          kStencilOperationMap[m_depth_stencil_desc.FrontFace.StencilFailOp];
      ds_info.front_stencil.depth_fail_op =
          kStencilOperationMap[m_depth_stencil_desc.FrontFace
                                   .StencilDepthFailOp];
      ds_info.front_stencil.stencil_compare_function =
          kCompareFunctionMap[m_depth_stencil_desc.FrontFace.StencilFunc];
      ds_info.front_stencil.write_mask = m_depth_stencil_desc.StencilWriteMask;
      ds_info.front_stencil.read_mask = m_depth_stencil_desc.StencilReadMask;

      ds_info.back_stencil.enabled = true;
      ds_info.back_stencil.depth_stencil_pass_op =
          kStencilOperationMap[m_depth_stencil_desc.BackFace.StencilPassOp];
      ds_info.back_stencil.stencil_fail_op =
          kStencilOperationMap[m_depth_stencil_desc.BackFace.StencilFailOp];
      ds_info.back_stencil.depth_fail_op =
          kStencilOperationMap[m_depth_stencil_desc.BackFace
                                   .StencilDepthFailOp];
      ds_info.back_stencil.stencil_compare_function =
          kCompareFunctionMap[m_depth_stencil_desc.BackFace.StencilFunc];
      ds_info.back_stencil.write_mask = m_depth_stencil_desc.StencilWriteMask;
      ds_info.back_stencil.read_mask = m_depth_stencil_desc.StencilReadMask;
    }
    m_depth_stencil_state = wmt_device.newDepthStencilState(ds_info);
  }

  {
    PTRACE("VS_ARGS_DEBUG: shader=%llu NumCB=%u NumArgs=%u CBufBindIdx=%u "
           "ArgBufBindIdx=%u ArgTableQwords=%u",
           (unsigned long long)(uintptr_t)m_vs_shader,
           m_vs_reflection.NumConstantBuffers, m_vs_reflection.NumArguments,
           m_vs_reflection.ConstanttBufferTableBindIndex,
           m_vs_reflection.ArgumentBufferBindIndex,
           m_vs_reflection.ArgumentTableQwords);
    if (m_vs_shader && (m_vs_reflection.NumArguments > 0 ||
                        m_vs_reflection.NumConstantBuffers > 0)) {
      if (m_vs_reflection.NumConstantBuffers > 0)
        m_vs_cb_args.resize(m_vs_reflection.NumConstantBuffers);
      if (m_vs_reflection.NumArguments > 0)
        m_vs_args.resize(m_vs_reflection.NumArguments);
      SM50GetArgumentsInfo(m_vs_shader,
                           m_vs_cb_args.empty() ? nullptr : m_vs_cb_args.data(),
                           m_vs_args.empty() ? nullptr : m_vs_args.data());
      for (size_t i = 0; i < m_vs_cb_args.size(); i++) {
        PTRACE("VS_ARGS_DEBUG: cb[%zu] type=%d slot=%u flags=0x%x offset=%u", i,
               (int)m_vs_cb_args[i].Type, m_vs_cb_args[i].SM50BindingSlot,
               m_vs_cb_args[i].Flags, m_vs_cb_args[i].StructurePtrOffset);
      }
      for (size_t i = 0; i < m_vs_args.size(); i++) {
        PTRACE("VS_ARGS_DEBUG: arg[%zu] type=%d slot=%u flags=0x%x offset=%u",
               i, (int)m_vs_args[i].Type, m_vs_args[i].SM50BindingSlot,
               m_vs_args[i].Flags, m_vs_args[i].StructurePtrOffset);
      }
      SM50Destroy(m_vs_shader);
      m_vs_shader = nullptr;
    }
  }

  {
    PTRACE("PS_ARGS_DEBUG: shader=%llu NumCB=%u NumArgs=%u CBufBindIdx=%u "
           "ArgBufBindIdx=%u ArgTableQwords=%u",
           (unsigned long long)(uintptr_t)m_ps_shader,
           m_ps_reflection.NumConstantBuffers, m_ps_reflection.NumArguments,
           m_ps_reflection.ConstanttBufferTableBindIndex,
           m_ps_reflection.ArgumentBufferBindIndex,
           m_ps_reflection.ArgumentTableQwords);
    if (m_ps_shader && (m_ps_reflection.NumArguments > 0 ||
                        m_ps_reflection.NumConstantBuffers > 0)) {
      if (m_ps_reflection.NumConstantBuffers > 0)
        m_ps_cb_args.resize(m_ps_reflection.NumConstantBuffers);
      if (m_ps_reflection.NumArguments > 0)
        m_ps_args.resize(m_ps_reflection.NumArguments);
      SM50GetArgumentsInfo(m_ps_shader,
                           m_ps_cb_args.empty() ? nullptr : m_ps_cb_args.data(),
                           m_ps_args.empty() ? nullptr : m_ps_args.data());
      for (size_t i = 0; i < m_ps_cb_args.size(); i++) {
        PTRACE("PS_ARGS_DEBUG: cb[%zu] type=%d slot=%u flags=0x%x offset=%u", i,
               (int)m_ps_cb_args[i].Type, m_ps_cb_args[i].SM50BindingSlot,
               m_ps_cb_args[i].Flags, m_ps_cb_args[i].StructurePtrOffset);
      }
      for (size_t i = 0; i < m_ps_args.size(); i++) {
        PTRACE("PS_ARGS_DEBUG: arg[%zu] type=%d slot=%u flags=0x%x offset=%u",
               i, (int)m_ps_args[i].Type, m_ps_args[i].SM50BindingSlot,
               m_ps_args[i].Flags, m_ps_args[i].StructurePtrOffset);
      }
      SM50Destroy(m_ps_shader);
      m_ps_shader = nullptr;
    }
  }

  Logger::info(str::format("Graphics PSO compiled: RTs=", m_num_render_targets,
                           " DSV=", (int)m_dsv_format,
                           " samples=", m_sample_count));
  return true;
}

void MTLD3D12PipelineState::SetGraphicsDesc(
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
  if (desc.pRootSignature) {
    m_root_sig = desc.pRootSignature;
    m_root_sig->AddRef();
  }

  if (desc.VS.pShaderBytecode && desc.VS.BytecodeLength) {
    m_vs.resize(desc.VS.BytecodeLength);
    memcpy(m_vs.data(), desc.VS.pShaderBytecode, desc.VS.BytecodeLength);
  }
  if (desc.PS.pShaderBytecode && desc.PS.BytecodeLength) {
    m_ps.resize(desc.PS.BytecodeLength);
    memcpy(m_ps.data(), desc.PS.pShaderBytecode, desc.PS.BytecodeLength);
  }
  if (desc.GS.pShaderBytecode && desc.GS.BytecodeLength) {
    m_gs.resize(desc.GS.BytecodeLength);
    memcpy(m_gs.data(), desc.GS.pShaderBytecode, desc.GS.BytecodeLength);
  }
  if (desc.HS.pShaderBytecode && desc.HS.BytecodeLength) {
    m_hs.resize(desc.HS.BytecodeLength);
    memcpy(m_hs.data(), desc.HS.pShaderBytecode, desc.HS.BytecodeLength);
  }
  if (desc.DS.pShaderBytecode && desc.DS.BytecodeLength) {
    m_ds.resize(desc.DS.BytecodeLength);
    memcpy(m_ds.data(), desc.DS.pShaderBytecode, desc.DS.BytecodeLength);
  }

  m_blend_desc = desc.BlendState;
  m_rasterizer_desc = desc.RasterizerState;
  m_depth_stencil_desc = desc.DepthStencilState;
  m_has_stream_output =
      desc.StreamOutput.NumEntries > 0 || desc.StreamOutput.NumStrides > 0 ||
      desc.StreamOutput.pSODeclaration || desc.StreamOutput.pBufferStrides;
  m_vs_uses_stage_in = false;
  m_uses_geometry_mesh_pipeline = false;
  m_vs_reflection = {};
  m_vs_args.clear();
  m_vs_cb_args.clear();
  m_ps_reflection = {};
  m_ps_args.clear();
  m_ps_cb_args.clear();
  m_gs_reflection = {};
  m_gs_args.clear();
  m_gs_cb_args.clear();
  m_input_elements.clear();
  m_input_semantic_names.clear();
  m_input_layout = {};
  if (desc.InputLayout.NumElements > 0 && desc.InputLayout.pInputElementDescs) {
    m_input_semantic_names.reserve(desc.InputLayout.NumElements);
    m_input_elements.reserve(desc.InputLayout.NumElements);
    for (UINT i = 0; i < desc.InputLayout.NumElements; i++) {
      auto element = desc.InputLayout.pInputElementDescs[i];
      m_input_semantic_names.emplace_back(
          element.SemanticName ? element.SemanticName : "");
      element.SemanticName = m_input_semantic_names.back().c_str();
      m_input_elements.push_back(element);
    }
    m_input_layout.NumElements = (UINT)m_input_elements.size();
    m_input_layout.pInputElementDescs = m_input_elements.data();
  }
  m_strip_cut_value = desc.IBStripCutValue;
  m_topology = desc.PrimitiveTopologyType;
  m_num_render_targets = std::min<UINT>(desc.NumRenderTargets, 8);
  memcpy(m_rtv_formats, desc.RTVFormats, sizeof(m_rtv_formats));
  m_dsv_format = desc.DSVFormat;
  m_sample_mask = desc.SampleMask;
  m_sample_count = desc.SampleDesc.Count ? desc.SampleDesc.Count : 1;
}

void MTLD3D12PipelineState::SetComputeDesc(
    const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc) {
  if (desc.pRootSignature) {
    m_root_sig = desc.pRootSignature;
    m_root_sig->AddRef();
  }
  if (desc.CS.pShaderBytecode && desc.CS.BytecodeLength) {
    m_cs.resize(desc.CS.BytecodeLength);
    memcpy(m_cs.data(), desc.CS.pShaderBytecode, desc.CS.BytecodeLength);
  }
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_ID3D12Object ||
      riid == IID_ID3D12DeviceChild || riid == IID_ID3D12Pageable ||
      riid == IID_ID3D12PipelineState) {
    *ppvObject = ref(this);
    return S_OK;
  }
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MTLD3D12PipelineState::AddRef() { return ++m_refCount; }

ULONG STDMETHODCALLTYPE MTLD3D12PipelineState::Release() {
  uint32_t rc = --m_refCount;
  if (!rc)
    delete this;
  return rc;
}

HRESULT STDMETHODCALLTYPE MTLD3D12PipelineState::GetPrivateData(REFGUID guid,
                                                                UINT *data_size,
                                                                void *data) {
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MTLD3D12PipelineState::SetPrivateData(
    REFGUID guid, UINT data_size, const void *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12PipelineState::SetPrivateDataInterface(
    REFGUID guid, const IUnknown *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12PipelineState::SetName(LPCWSTR name) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12PipelineState::GetDevice(REFIID riid,
                                                           void **device) {
  return m_device->QueryInterface(riid, device);
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::GetCachedBlob(ID3DBlob **blob) {
  return E_NOTIMPL;
}

} // namespace dxmt
