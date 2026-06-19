#include "d3d12_pipeline_state.hpp"
#include "d3d12_device.hpp"
#include "d3d12_root_signature.hpp"
#include "d3d12_trace.hpp"
#include "d3d12_vertex_input.hpp"
#include "d3d12_m12core_counters.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include "Metal.hpp"

static bool DXMTD3D12PSOTraceEnabled() {
  static int enabled = []() {
    const char *value = std::getenv("DXMT_D3D12_PSO_TRACE");
    return value && value[0] && std::strcmp(value, "0") != 0;
  }();
  return enabled != 0;
}

#define PTRACE(fmt, ...) do { if (DXMTD3D12PSOTraceEnabled()) { FILE *_tf = dxmt::openDiagnosticLog("dxmt-d3d12-pso.log"); if (_tf) { fprintf(_tf, fmt "\n", ##__VA_ARGS__); fclose(_tf); } } } while(0)
#include "airconv_public.h"
#include "dxmt_format.hpp"
#include "dxil/dxil_container.hpp"
#include "dxil/llvm_bitcode.hpp"
#include "dxil/dxil_to_msl.hpp"
#include "dxil/msl_lowering.hpp"
#include "../../libs/DXBCParser/BlobContainer.h"
#include "../../libs/DXBCParser/DXBCUtils.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <process.h>
#include <windows.h>

#define PSTRACE(fmt, ...) DXMTD3D12Trace("PSO", fmt, ##__VA_ARGS__)

namespace dxmt {

namespace {
constexpr uint32_t kMetalD3D12VertexBufferSlotCount = 29;

std::atomic<uint64_t> g_shader_memory_cache_hits{0};
std::atomic<uint64_t> g_shader_memory_cache_misses{0};
std::atomic<uint64_t> g_shader_metallib_cache_hits{0};
std::atomic<uint64_t> g_shader_metallib_cache_misses{0};
std::atomic<uint64_t> g_metal_compute_pipeline_creates{0};
std::atomic<uint64_t> g_metal_render_pipeline_creates{0};
std::atomic<uint64_t> g_render_pipeline_cache_hits{0};
std::atomic<uint64_t> g_render_pipeline_cache_misses{0};
std::atomic<uint64_t> g_compute_pipeline_cache_hits{0};
std::atomic<uint64_t> g_compute_pipeline_cache_misses{0};
std::atomic<uint64_t> g_compile_wait_count{0};
std::atomic<uint64_t> g_compile_wait_ns{0};

std::mutex g_metal_pipeline_cache_mutex;
std::unordered_map<size_t, WMT::Reference<WMT::RenderPipelineState>> g_render_pipeline_cache;
std::unordered_map<size_t, WMT::Reference<WMT::ComputePipelineState>> g_compute_pipeline_cache;

std::string ShaderCacheDir() {
  const char *env_path = std::getenv("DXMT_SHADER_CACHE_PATH");
  std::string path = (env_path && env_path[0]) ? env_path : "/tmp/dxmt_shader_cache";
  while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
    path.pop_back();
  return path;
}

void FormatShaderCachePath(char *out, size_t out_size, const char *suffix_fmt,
                           size_t hash) {
  char suffix[128];
  snprintf(suffix, sizeof(suffix), suffix_fmt, hash);
  snprintf(out, out_size, "%s/%s", ShaderCacheDir().c_str(), suffix);
}

void EnsureShaderCacheDir() {
  auto dir = ShaderCacheDir();
  if (dir == "/tmp/dxmt_shader_cache")
    CreateDirectoryA("Z:\\tmp\\dxmt_shader_cache", nullptr);
  mkdir(dir.c_str());
}

std::string DescribeNSObject(obj_handle_t handle) {
  if (!handle)
    return "unknown";
  auto desc = WMT::String{NSObject_description(handle)}.getUTF8String();
  return desc.empty() ? "unknown" : desc;
}

bool IsTransientMetalCompilerError(const std::string &desc) {
  return desc.find("XPC_ERROR_CONNECTION_INTERRUPTED") != std::string::npos ||
         desc.find("interrupted connection") != std::string::npos;
}

dxmt::dxil::MSLShader
ToRuntimeMSLShader(dxmt::dxil::TypedMSLShader &&typed) {
  dxmt::dxil::MSLShader shader;
  shader.source = std::move(typed.source);
  shader.entry_point = std::move(typed.entry_point);
  shader.tg_size[0] = typed.tg_size[0];
  shader.tg_size[1] = typed.tg_size[1];
  shader.tg_size[2] = typed.tg_size[2];
  shader.unsupported_intrinsics = typed.unsupported_intrinsics;
  shader.unsupported_opcodes = typed.unsupported_opcodes;
  shader.diagnostics = std::move(typed.diagnostics);
  shader.diagnostics.push_back(str::format(
      "MSLLowering runtime path active: typed_values=", typed.typed_value_count,
      " auto_values=", typed.auto_value_count));
  return shader;
}

bool AsyncPipelineCompileEnabled() {
  char value[16] = {};
  DWORD len = GetEnvironmentVariableA("DXMT_ASYNC_PIPELINE_COMPILE", value,
                                      sizeof(value));
  return len > 0 && value[0] && value[0] != '0';
}

uint32_t AsyncPipelineWorkerCount() {
  char value[16] = {};
  DWORD len = GetEnvironmentVariableA("DXMT_D3D12_PSO_WORKERS", value,
                                      sizeof(value));
  if (len == 0 || !value[0])
    return 4;

  char *end = nullptr;
  unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || parsed == 0)
    return 4;
  return std::max(1u, std::min<uint32_t>((uint32_t)parsed, 12u));
}

bool EnvFlagEnabled(const char *name) {
  char value[16] = {};
  DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
  return len > 0 && value[0] && value[0] != '0';
}

WMTAttributeFormat AttributeFormatForMetalDataType(uint32_t type) {
  switch (type) {
  case 3: return WMTAttributeFormatFloat;
  case 4: return WMTAttributeFormatFloat2;
  case 5: return WMTAttributeFormatFloat3;
  case 6: return WMTAttributeFormatFloat4;
  case 29: return WMTAttributeFormatInt;
  case 30: return WMTAttributeFormatInt2;
  case 31: return WMTAttributeFormatInt3;
  case 32: return WMTAttributeFormatInt4;
  case 33: return WMTAttributeFormatUInt;
  case 34: return WMTAttributeFormatUInt2;
  case 35: return WMTAttributeFormatUInt3;
  case 36: return WMTAttributeFormatUInt4;
  default: return WMTAttributeFormatInvalid;
  }
}

uint32_t AttributeFormatByteSize(WMTAttributeFormat format) {
  switch (format) {
  case WMTAttributeFormatFloat:
  case WMTAttributeFormatInt:
  case WMTAttributeFormatUInt:
    return 4;
  case WMTAttributeFormatFloat2:
  case WMTAttributeFormatInt2:
  case WMTAttributeFormatUInt2:
    return 8;
  case WMTAttributeFormatFloat3:
  case WMTAttributeFormatInt3:
  case WMTAttributeFormatUInt3:
    return 12;
  default:
    return 16;
  }
}

bool BuildVertexDescriptorFromMetalFunction(WMT::Function function,
                                            WMTVertexDescriptor &desc) {
  WMTFunctionVertexAttribute attributes[WMT_MAX_VERTEX_ATTRIBUTES] = {};
  uint32_t count = function.copyVertexAttributes(attributes, WMT_MAX_VERTEX_ATTRIBUTES);
  if (!count)
    return false;

  uint32_t stride = 0;
  uint32_t emitted = 0;
  for (uint32_t i = 0; i < count; i++) {
    const auto &src = attributes[i];
    if (!src.active || src.attribute_index >= WMT_MAX_VERTEX_ATTRIBUTES)
      continue;
    auto format = AttributeFormatForMetalDataType(src.attribute_type);
    if (format == WMTAttributeFormatInvalid)
      continue;
    auto &attr = desc.attributes[src.attribute_index];
    attr.format = format;
    attr.offset = stride;
    attr.buffer_index = 0;
    stride += AttributeFormatByteSize(format);
    emitted = std::max(emitted, src.attribute_index + 1);
    PSTRACE("D3D12 PSO reflected vertex attr[%u] name=%s mtl_type=%u fmt=%u offset=%u",
            src.attribute_index, src.name, src.attribute_type, (unsigned)format,
            attr.offset);
  }
  if (!emitted)
    return false;
  desc.attribute_count = emitted;
  desc.layout_count = 1;
  desc.layouts[0].stride = stride ? stride : 16;
  desc.layouts[0].step_function = WMTVertexStepFunctionPerVertex;
  desc.layouts[0].step_rate = 1;
  return true;
}

class AsyncPipelineCompiler {
public:
  ~AsyncPipelineCompiler() { Shutdown(); }

  void Enqueue(MTLD3D12PipelineState *pso) {
    EnsureStarted();
    pso->AddRef();
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_stop) {
        pso->Release();
        return;
      }
      m_queue.push_back(pso);
    }
    m_cv.notify_one();
  }

  void Shutdown() {
    std::vector<std::thread> workers;
    std::deque<MTLD3D12PipelineState *> pending;
    {
      std::lock_guard<std::mutex> start_lock(m_start_mutex);
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        pending.swap(m_queue);
      }
      workers.swap(m_workers);
      m_started = false;
      m_worker_count = 0;
    }
    m_cv.notify_all();
    for (auto &worker : workers) {
      if (worker.joinable())
        worker.join();
    }
    for (auto *pso : pending) {
      if (pso)
        pso->Release();
    }
  }

  void RequestShutdown() {
    {
      std::lock_guard<std::mutex> start_lock(m_start_mutex);
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
      }
    }
    m_cv.notify_all();
  }

private:
  void EnsureStarted() {
    std::lock_guard<std::mutex> lock(m_start_mutex);
    if (m_started)
      return;
    m_started = true;
    m_worker_count = AsyncPipelineWorkerCount();
    Logger::info(str::format("M12 async PSO compiler starting workers=",
                             m_worker_count));
    for (uint32_t i = 0; i < m_worker_count; i++) {
      m_workers.emplace_back([this, i]() { WorkerLoop(i); });
    }
  }

  void WorkerLoop(uint32_t worker_index) {
    for (;;) {
      MTLD3D12PipelineState *pso = nullptr;
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
        if (m_stop && m_queue.empty())
          break;
        pso = m_queue.front();
        m_queue.pop_front();
      }
      if (!pso)
        continue;
      PSTRACE("PSO async worker[%u] compiling pso=%p", worker_index,
              (void *)pso);
      pso->RunAsyncCompile();
      pso->Release();
    }
    PSTRACE("PSO async worker[%u] stopped", worker_index);
  }

  std::mutex m_start_mutex;
  bool m_started = false;
  bool m_stop = false;
  uint32_t m_worker_count = 0;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<MTLD3D12PipelineState *> m_queue;
  std::vector<std::thread> m_workers;
};

AsyncPipelineCompiler &GetAsyncPipelineCompiler() {
  static AsyncPipelineCompiler *compiler = new AsyncPipelineCompiler();
  return *compiler;
}

uint32_t M12CoreStageForShaderType(ShaderType type) {
  switch (type) {
  case ShaderType::Vertex:
    return M12CORE_SHADER_STAGE_VERTEX;
  case ShaderType::Pixel:
    return M12CORE_SHADER_STAGE_PIXEL;
  case ShaderType::Geometry:
    return M12CORE_SHADER_STAGE_GEOMETRY;
  case ShaderType::Hull:
    return M12CORE_SHADER_STAGE_HULL;
  case ShaderType::Domain:
    return M12CORE_SHADER_STAGE_DOMAIN;
  case ShaderType::Compute:
    return M12CORE_SHADER_STAGE_COMPUTE;
  default:
    return M12CORE_SHADER_STAGE_UNKNOWN;
  }
}

bool ShaderBytecodeContainsDxil(const void *bytecode, SIZE_T size) {
  M12CoreShaderBytecodeInfo core_info = {};
  if (WMTM12CoreHashShaderBytecode(bytecode, size, M12CORE_SHADER_STAGE_UNKNOWN,
                                   &core_info))
    return core_info.contains_dxil != 0;

  if (!bytecode || size < 4)
    return false;
  using namespace microsoft;
  CDXBCParser dxbc_parser;
  if (FAILED(dxbc_parser.ReadDXBC(bytecode, size)))
    return false;
  for (UINT32 i = 0; i < dxbc_parser.GetBlobCount(); i++) {
    if (dxbc_parser.GetBlobFourCC(i) == dxmt::dxil::DXIL_FOURCC)
      return true;
  }
  return false;
}

size_t ComputeShaderCacheHash(const void *bytecode, SIZE_T size,
                              ShaderType type,
                              const D3D12_INPUT_LAYOUT_DESC *input_layout) {
  size_t hash = 0;
  M12CoreShaderBytecodeInfo core_info = {};
  if (WMTM12CoreHashShaderBytecode(bytecode, size, M12CoreStageForShaderType(type),
                                   &core_info)) {
    hash = (size_t)core_info.bytecode_hash;
  } else {
    hash = hash * 131 + (size_t)type;
    if (type == ShaderType::Vertex)
      hash = hash * 131 + 0x4d3132506833ull; // M12 Phase 3 explicit varying contract.
    if (bytecode && size > 0) {
      const uint8_t *p = (const uint8_t *)bytecode;
      for (SIZE_T i = 0; i < size; i++)
        hash = hash * 131 + p[i];
    }
  }
  if (type == ShaderType::Vertex && input_layout) {
    hash = hash * 131 + input_layout->NumElements;
    for (UINT i = 0; i < input_layout->NumElements; i++) {
      const auto &el = input_layout->pInputElementDescs[i];
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
  return hash;
}

const char *PixelFormatManifestName(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatRGBA8Unorm: return "rgba8unorm";
  case WMTPixelFormatRGBA8Unorm_sRGB: return "rgba8unorm_srgb";
  case WMTPixelFormatBGRA8Unorm: return "bgra8unorm";
  case WMTPixelFormatBGRA8Unorm_sRGB: return "bgra8unorm_srgb";
  case WMTPixelFormatRGBA16Float: return "rgba16float";
  case WMTPixelFormatRGBA32Float: return "rgba32float";
  case WMTPixelFormatRGB10A2Unorm: return "rgb10a2unorm";
  case WMTPixelFormatRG11B10Float: return "rg11b10float";
  case WMTPixelFormatR8Unorm: return "r8unorm";
  case WMTPixelFormatR16Float: return "r16float";
  case WMTPixelFormatR32Float: return "r32float";
  case WMTPixelFormatRG16Float: return "rg16float";
  case WMTPixelFormatRG16Unorm: return "rg16unorm";
  case WMTPixelFormatRG8Unorm: return "rg8unorm";
  case WMTPixelFormatDepth16Unorm: return "depth16unorm";
  case WMTPixelFormatDepth32Float: return "depth32float";
  case WMTPixelFormatDepth24Unorm_Stencil8: return "depth24unorm_stencil8";
  case WMTPixelFormatDepth32Float_Stencil8: return "depth32float_stencil8";
  case WMTPixelFormatBC1_RGBA: return "bc1_rgba";
  case WMTPixelFormatBC1_RGBA_sRGB: return "bc1_rgba_srgb";
  case WMTPixelFormatBC2_RGBA: return "bc2_rgba";
  case WMTPixelFormatBC2_RGBA_sRGB: return "bc2_rgba_srgb";
  case WMTPixelFormatBC3_RGBA: return "bc3_rgba";
  case WMTPixelFormatBC3_RGBA_sRGB: return "bc3_rgba_srgb";
  case WMTPixelFormatBC4_RUnorm: return "bc4_runorm";
  case WMTPixelFormatBC4_RSnorm: return "bc4_rsnorm";
  case WMTPixelFormatBC5_RGUnorm: return "bc5_rgunorm";
  case WMTPixelFormatBC5_RGSnorm: return "bc5_rgsnorm";
  case WMTPixelFormatBC6H_RGBFloat: return "bc6h_rgbfloat";
  case WMTPixelFormatBC6H_RGBUfloat: return "bc6h_rgbufloat";
  case WMTPixelFormatBC7_RGBAUnorm: return "bc7_rgbaunorm";
  case WMTPixelFormatBC7_RGBAUnorm_sRGB: return "bc7_rgbaunorm_srgb";
  default: return "invalid";
  }
}

void PsoCacheHashCombine(size_t &hash, size_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
}

uint32_t ToM12CoreShaderStage(ShaderType type) {
  switch (type) {
  case ShaderType::Vertex: return M12CORE_SHADER_STAGE_VERTEX;
  case ShaderType::Pixel: return M12CORE_SHADER_STAGE_PIXEL;
  case ShaderType::Compute: return M12CORE_SHADER_STAGE_COMPUTE;
  case ShaderType::Hull: return M12CORE_SHADER_STAGE_HULL;
  case ShaderType::Domain: return M12CORE_SHADER_STAGE_DOMAIN;
  case ShaderType::Geometry: return M12CORE_SHADER_STAGE_GEOMETRY;
  default: return M12CORE_SHADER_STAGE_UNKNOWN;
  }
}

bool CreateM12CoreShaderFunction(obj_handle_t device, ShaderType type,
                                 uint32_t input_kind, size_t shader_hash,
                                 const void *input_data, uint64_t input_size,
                                 const char *entry_name,
                                 WMT::Reference<WMT::Function> &out_func,
                                 WMT::Reference<WMT::Error> *out_error,
                                 M12CoreShaderFunctionResult *out_result) {
  /* Phase 3 shader-function seam: libm12core owns Metal library creation,
   * function fallback lookup, and a native in-process function cache when this
   * bridge is available.  PE-side code still keeps the legacy WMT path below as
   * fallback while DXIL->MSL lowering/reflection compatibility remain in D3D12.
   */
  M12CoreShaderFunctionResult result = {};
  if (!WMTM12CoreCreateShaderFunction(device, ToM12CoreShaderStage(type), input_kind,
                                      (uint64_t)shader_hash, input_data, input_size,
                                      entry_name, &result) ||
      result.abi_version != M12CORE_ABI_VERSION)
    return false;
  if (out_result)
    *out_result = result;
  if (result.error_handle && out_error)
    *out_error = WMT::Reference<WMT::Error>(result.error_handle);
  if (result.status != M12CORE_SHADER_FUNCTION_STATUS_OK || !result.function_handle)
    return true;
  out_func = WMT::Reference<WMT::Function>(result.function_handle);
  return true;
}

void CopyM12CoreReflectionToSM50(const M12CoreSM50ShaderReflection &src,
                                 MTL_SHADER_REFLECTION &dst) {
  dst = {};
  dst.ConstanttBufferTableBindIndex = src.constant_buffer_table_bind_index;
  dst.ArgumentBufferBindIndex = src.argument_buffer_bind_index;
  dst.NumConstantBuffers = src.num_constant_buffers;
  dst.NumArguments = src.num_arguments;
  dst.ThreadgroupSize[0] = src.stage_payload[0];
  dst.ThreadgroupSize[1] = src.stage_payload[1];
  dst.ThreadgroupSize[2] = src.stage_payload[2];
  dst.ConstantBufferSlotMask = src.constant_buffer_slot_mask;
  dst.SamplerSlotMask = src.sampler_slot_mask;
  dst.UAVSlotMask = src.uav_slot_mask;
  dst.SRVSlotMaskHi = src.srv_slot_mask_hi;
  dst.SRVSlotMaskLo = src.srv_slot_mask_lo;
  dst.NumOutputElement = src.num_output_element;
  dst.ThreadsPerPatch = src.threads_per_patch;
  dst.ArgumentTableQwords = src.argument_table_qwords;
}

void CopyM12CoreArgumentToSM50(const M12CoreSM50ShaderArgument &src,
                               MTL_SM50_SHADER_ARGUMENT &dst) {
  dst.Type = (SM50BindingType)src.type;
  dst.SM50BindingSlot = src.binding_slot;
  dst.SM50RegisterSpace = src.register_space;
  dst.Flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)src.flags;
  dst.StructurePtrOffset = src.structure_ptr_offset;
  dst.SizeInVec4 = src.size_in_vec4;
}

bool ReflectSM50WithM12Core(const void *bytecode, uint64_t bytecode_size,
                            MTL_SHADER_REFLECTION &reflection,
                            std::vector<MTL_SM50_SHADER_ARGUMENT> &constant_buffers,
                            std::vector<MTL_SM50_SHADER_ARGUMENT> &arguments) {
  if (ShaderBytecodeContainsDxil(bytecode, bytecode_size))
    return false;

  /* Phase 3 reflection seam: the native core owns SM50 reflection/argument
   * extraction and returns POD copies.  D3D12 keeps the current binding structs
   * until Phase 5 moves root/descriptor binding plan ownership.
   */
  M12CoreSM50ShaderReflection core_reflection = {};
  M12CoreSM50ReflectionResult probe = {};
  if (!WMTM12CoreReflectSM50Shader(bytecode, bytecode_size, 0,
                                   &core_reflection, nullptr, 0, nullptr, 0,
                                   &probe) ||
      probe.abi_version != M12CORE_ABI_VERSION)
    return false;
  if (probe.status != M12CORE_SM50_REFLECTION_STATUS_OUTPUT_TOO_SMALL &&
      probe.status != M12CORE_SM50_REFLECTION_STATUS_OK)
    return false;

  std::vector<M12CoreSM50ShaderArgument> core_cbs(probe.required_constant_buffers);
  std::vector<M12CoreSM50ShaderArgument> core_args(probe.required_arguments);
  M12CoreSM50ReflectionResult result = {};
  if (!WMTM12CoreReflectSM50Shader(
          bytecode, bytecode_size, 0, &core_reflection,
          core_cbs.empty() ? nullptr : core_cbs.data(),
          static_cast<uint32_t>(core_cbs.size()),
          core_args.empty() ? nullptr : core_args.data(),
          static_cast<uint32_t>(core_args.size()), &result) ||
      result.abi_version != M12CORE_ABI_VERSION ||
      result.status != M12CORE_SM50_REFLECTION_STATUS_OK)
    return false;

  CopyM12CoreReflectionToSM50(core_reflection, reflection);
  constant_buffers.resize(core_cbs.size());
  arguments.resize(core_args.size());
  for (size_t i = 0; i < core_cbs.size(); i++)
    CopyM12CoreArgumentToSM50(core_cbs[i], constant_buffers[i]);
  for (size_t i = 0; i < core_args.size(); i++)
    CopyM12CoreArgumentToSM50(core_args[i], arguments[i]);
  return true;
}

bool LowerDXILToMSLWithM12Core(const void *dxil_container,
                               uint64_t dxil_container_size,
                               ShaderType type,
                               const std::vector<D3D12IAInputElementInfo> &ia_inputs,
                               dxmt::dxil::MSLShader &out_shader,
                               bool &out_used_typed_lowering) {
  /* Phase 3 DXIL->MSL seam: libm12core owns DXIL container parsing, LLVM
   * bitcode parsing, typed MSL lowering, and DXILToMSL fallback conversion.
   * D3D12 still owns file diagnostics and compile reports until those reporting
   * formats are moved behind a native-core diagnostics ABI.
   */
  std::vector<M12CoreVertexInputElement> core_inputs;
  if (type == ShaderType::Vertex) {
    core_inputs.reserve(ia_inputs.size());
    for (const auto &input : ia_inputs) {
      if (input.table_index >= kMetalD3D12VertexBufferSlotCount)
        continue;
      M12CoreVertexInputElement element = {};
      element.shader_register = input.shader_register;
      element.table_index = input.table_index;
      element.input_slot = input.input_slot;
      element.aligned_byte_offset = input.aligned_byte_offset;
      element.dxgi_format = static_cast<uint32_t>(input.dxgi_format);
      element.metal_format = static_cast<uint32_t>(input.metal_format);
      element.per_instance = input.per_instance ? 1u : 0u;
      element.instance_step_rate = input.instance_step_rate;
      element.table_indexing_mode = input.table_indexing_mode == D3D12VertexTableIndexingMode::RawSlot ? 1u : 0u;
      element.system_value = input.system_value ? 1u : 0u;
      core_inputs.push_back(element);
    }
  }

  M12CoreDXILToMSLDesc desc = {};
  desc.abi_version = M12CORE_ABI_VERSION;
  desc.stage = ToM12CoreShaderStage(type);
  desc.dxil_container = dxil_container;
  desc.dxil_container_size = dxil_container_size;
  desc.vertex_inputs = core_inputs.empty() ? nullptr : core_inputs.data();
  desc.vertex_input_count = static_cast<uint32_t>(core_inputs.size());

  M12CoreDXILToMSLResult probe = {};
  if (!WMTM12CoreLowerDXILToMSL(&desc, nullptr, 0, &probe) ||
      probe.abi_version != M12CORE_ABI_VERSION)
    return false;
  if (probe.status != M12CORE_DXIL_TO_MSL_STATUS_OUTPUT_TOO_SMALL &&
      probe.status != M12CORE_DXIL_TO_MSL_STATUS_OK)
    return false;

  std::vector<char> source(static_cast<size_t>(probe.required_source_size) + 1);
  M12CoreDXILToMSLResult result = {};
  if (!WMTM12CoreLowerDXILToMSL(&desc, source.data(), source.size(), &result) ||
      result.abi_version != M12CORE_ABI_VERSION ||
      result.status != M12CORE_DXIL_TO_MSL_STATUS_OK)
    return false;

  out_shader.source.assign(source.data(), static_cast<size_t>(result.required_source_size));
  out_shader.entry_point = result.entry_point;
  out_shader.tg_size[0] = result.threadgroup_size[0];
  out_shader.tg_size[1] = result.threadgroup_size[1];
  out_shader.tg_size[2] = result.threadgroup_size[2];
  out_shader.unsupported_intrinsics = result.unsupported_intrinsics;
  out_shader.unsupported_opcodes = result.unsupported_opcodes;
  out_shader.diagnostics.push_back("DXIL->MSL generated by libm12core");
  out_used_typed_lowering = result.used_typed_lowering != 0;
  return true;
}

template <typename PipelineRef>
bool LookupM12CorePipelineCache(uint32_t kind, size_t key, PipelineRef &out_pipeline,
                                bool &out_hit) {
  M12CorePipelineCacheQuery query = {};
  query.abi_version = M12CORE_ABI_VERSION;
  query.kind = kind;
  query.key = (uint64_t)key;
  M12CorePipelineCacheResult result = {};
  if (!WMTM12CoreLookupPipelineCache(&query, &result) ||
      result.abi_version != M12CORE_ABI_VERSION || result.kind != kind)
    return false;
  out_hit = result.hit != 0;
  if (out_hit && result.pipeline_handle)
    out_pipeline = PipelineRef(result.pipeline_handle);
  return true;
}

bool StoreM12CorePipelineCache(uint32_t kind, size_t key, obj_handle_t pipeline_handle) {
  if (!pipeline_handle)
    return false;
  M12CorePipelineCacheQuery query = {};
  query.abi_version = M12CORE_ABI_VERSION;
  query.kind = kind;
  query.key = (uint64_t)key;
  return WMTM12CoreStorePipelineCache(&query, pipeline_handle);
}

bool FinalizeM12CorePipelineCacheKeyFromFields(size_t base_hash, uint64_t device_id,
                                               uint32_t kind, uint64_t flags,
                                               const std::vector<uint64_t> &fields,
                                               size_t &out_key) {
  /* Phase 4 normalized-key seam: D3D12 still maps its descriptor state into
   * stable scalar fields, but libm12core owns the ordered field accumulation
   * and final device/kind namespace.  If the native core is unavailable, the
   * caller falls back to the legacy PE-local hash combiner below.
   */
  M12CorePipelineKeyFields input = {};
  input.abi_version = M12CORE_ABI_VERSION;
  input.kind = kind;
  input.base_hash = (uint64_t)base_hash;
  input.device_id = device_id;
  input.flags = flags;
  input.fields = fields.empty() ? nullptr : fields.data();
  input.field_count = static_cast<uint32_t>(fields.size());
  M12CorePipelineCacheKey key = {};
  if (!WMTM12CoreMakePipelineCacheKeyFromFields(&input, &key) ||
      key.abi_version != M12CORE_ABI_VERSION || key.kind != kind)
    return false;
  out_key = (size_t)key.key;
  return true;
}

template <typename PipelineRef>
bool CreateM12CorePipelineState(obj_handle_t device, uint32_t kind,
                                size_t cache_key, const void *pipeline_info,
                                size_t pipeline_info_size,
                                PipelineRef &out_pipeline,
                                WMT::Reference<WMT::Error> &out_error,
                                bool &out_cache_hit) {
  M12CorePipelineCreateResult result = {};
  if (!WMTM12CoreCreatePipelineState(device, kind, (uint64_t)cache_key,
                                     pipeline_info, pipeline_info_size,
                                     &result) ||
      result.abi_version != M12CORE_ABI_VERSION || result.kind != kind)
    return false;
  out_cache_hit = result.cache_hit != 0;
  if (result.status == M12CORE_PIPELINE_CREATE_STATUS_OK && result.pipeline_handle) {
    out_pipeline = PipelineRef(result.pipeline_handle);
    return true;
  }
  /* Phase 4 fallback seam: native creation may return a retained NSError for
   * diagnostics, but D3D12 immediately falls back to the legacy WMT creation
   * path.  Keep that fallback's error slot clean and release the native error
   * here so the fallback cannot overwrite/leak it.
   */
  if (result.error_handle) {
    WMT::Reference<WMT::Error> native_error(result.error_handle);
    (void)native_error;
  }
  return false;
}

uint64_t PsoFieldFloat(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t RootSignaturePipelineKey(ID3D12RootSignature *root_signature) {
  if (!root_signature)
    return 0;
  auto *m12_root = static_cast<MTLD3D12RootSignature *>(root_signature);
  /* Phase 5 PSO/root linkage seam: prefer libm12core's canonical structural
   * root-signature key so pipeline/oracle keys share the same namespace.  If
   * the native core is disabled or unavailable, fall back to the existing blob
   * hash and keep all descriptor binding behavior on the PE-local path.
   */
  if (m12_root->HasM12CoreSummary())
    return m12_root->GetM12CoreRootSignatureKey();
  return static_cast<uint64_t>(m12_root->GetBlobHash());
}

void AppendRenderTargetBlendFields(std::vector<uint64_t> &fields,
                                   const D3D12_RENDER_TARGET_BLEND_DESC &rt) {
  fields.push_back(rt.BlendEnable ? 1 : 0);
  fields.push_back(rt.LogicOpEnable ? 1 : 0);
  fields.push_back(rt.SrcBlend);
  fields.push_back(rt.DestBlend);
  fields.push_back(rt.BlendOp);
  fields.push_back(rt.SrcBlendAlpha);
  fields.push_back(rt.DestBlendAlpha);
  fields.push_back(rt.BlendOpAlpha);
  fields.push_back(rt.LogicOp);
  fields.push_back(rt.RenderTargetWriteMask);
}

void AppendStencilOpFields(std::vector<uint64_t> &fields,
                           const D3D12_DEPTH_STENCILOP_DESC &op) {
  fields.push_back(op.StencilFailOp);
  fields.push_back(op.StencilDepthFailOp);
  fields.push_back(op.StencilPassOp);
  fields.push_back(op.StencilFunc);
}

std::vector<uint64_t> BuildRenderMetalPipelineKeyFields(
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
    const D3D12_BLEND_DESC &blend_desc,
    const D3D12_RASTERIZER_DESC &rasterizer_desc,
    const D3D12_DEPTH_STENCIL_DESC &depth_stencil_desc,
    const WMTVertexDescriptor *vertex_descriptor,
    bool reflected_descriptor_enabled,
    bool reflected_unspecified_topology) {
  std::vector<uint64_t> fields;
  fields.reserve(128);
  fields.push_back((size_t)topology);
  fields.push_back(reflected_descriptor_enabled ? 1 : 0);
  fields.push_back(reflected_unspecified_topology ? 1 : 0);
  fields.push_back(blend_desc.AlphaToCoverageEnable ? 1 : 0);
  fields.push_back(blend_desc.IndependentBlendEnable ? 1 : 0);
  for (const auto &rt : blend_desc.RenderTarget)
    AppendRenderTargetBlendFields(fields, rt);
  fields.push_back(rasterizer_desc.FillMode);
  fields.push_back(rasterizer_desc.CullMode);
  fields.push_back(rasterizer_desc.FrontCounterClockwise ? 1 : 0);
  fields.push_back(rasterizer_desc.DepthBias);
  fields.push_back(PsoFieldFloat(rasterizer_desc.DepthBiasClamp));
  fields.push_back(PsoFieldFloat(rasterizer_desc.SlopeScaledDepthBias));
  fields.push_back(rasterizer_desc.DepthClipEnable ? 1 : 0);
  fields.push_back(rasterizer_desc.MultisampleEnable ? 1 : 0);
  fields.push_back(rasterizer_desc.AntialiasedLineEnable ? 1 : 0);
  fields.push_back(rasterizer_desc.ForcedSampleCount);
  fields.push_back(rasterizer_desc.ConservativeRaster);
  fields.push_back(depth_stencil_desc.DepthEnable ? 1 : 0);
  fields.push_back(depth_stencil_desc.DepthWriteMask);
  fields.push_back(depth_stencil_desc.DepthFunc);
  fields.push_back(depth_stencil_desc.StencilEnable ? 1 : 0);
  fields.push_back(depth_stencil_desc.StencilReadMask);
  fields.push_back(depth_stencil_desc.StencilWriteMask);
  AppendStencilOpFields(fields, depth_stencil_desc.FrontFace);
  AppendStencilOpFields(fields, depth_stencil_desc.BackFace);
  if (vertex_descriptor) {
    fields.push_back(vertex_descriptor->attribute_count);
    fields.push_back(vertex_descriptor->layout_count);
    for (uint32_t i = 0; i < vertex_descriptor->attribute_count &&
                         i < WMT_MAX_VERTEX_ATTRIBUTES; i++) {
      const auto &attr = vertex_descriptor->attributes[i];
      fields.push_back(attr.format);
      fields.push_back(attr.offset);
      fields.push_back(attr.buffer_index);
    }
    for (uint32_t i = 0; i < vertex_descriptor->layout_count &&
                         i < WMT_MAX_VERTEX_BUFFER_LAYOUTS; i++) {
      const auto &layout = vertex_descriptor->layouts[i];
      fields.push_back(layout.stride);
      fields.push_back(layout.step_function);
      fields.push_back(layout.step_rate);
    }
  }
  return fields;
}

size_t ComputeRenderMetalPipelineCacheHash(
    size_t base_hash, D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
    const D3D12_BLEND_DESC &blend_desc,
    const D3D12_RASTERIZER_DESC &rasterizer_desc,
    const D3D12_DEPTH_STENCIL_DESC &depth_stencil_desc,
    const WMTVertexDescriptor *vertex_descriptor,
    bool reflected_descriptor_enabled,
    bool reflected_unspecified_topology) {
  size_t hash = base_hash;
  auto fields = BuildRenderMetalPipelineKeyFields(
      topology, blend_desc, rasterizer_desc, depth_stencil_desc,
      vertex_descriptor, reflected_descriptor_enabled,
      reflected_unspecified_topology);
  for (uint64_t field : fields)
    PsoCacheHashCombine(hash, field);
  return hash;
}

size_t ComputeRenderPSOManifestHash(size_t vs_hash, size_t ps_hash,
                                    size_t gs_hash,
                                    UINT num_render_targets,
                                    const DXGI_FORMAT *rtv_formats,
                                    DXGI_FORMAT dsv_format,
                                    UINT sample_count,
                                    UINT input_elements,
                                    uint32_t ia_slot_mask,
                                    bool uses_stage_in) {
  size_t hash = vs_hash;
  hash = hash * 131 + ps_hash;
  hash = hash * 131 + gs_hash;
  hash = hash * 131 + num_render_targets;
  for (UINT i = 0; i < 8; i++)
    hash = hash * 131 + (size_t)rtv_formats[i];
  hash = hash * 131 + (size_t)dsv_format;
  hash = hash * 131 + (size_t)sample_count;
  hash = hash * 131 + (size_t)input_elements;
  hash = hash * 131 + (size_t)ia_slot_mask;
  hash = hash * 131 + (uses_stage_in ? 1 : 0);
  return hash;
}

void WriteJsonString(FILE *df, const std::string &value) {
  fputc('"', df);
  for (char ch : value) {
    switch (ch) {
    case '\\': fputs("\\\\", df); break;
    case '"': fputs("\\\"", df); break;
    case '\n': fputs("\\n", df); break;
    case '\r': fputs("\\r", df); break;
    case '\t': fputs("\\t", df); break;
    default: fputc(ch, df); break;
    }
  }
  fputc('"', df);
}

void DumpComputePSOManifest(size_t cs_hash, SIZE_T cs_size,
                            uint32_t threadgroup_width,
                            uint32_t threadgroup_height,
                            uint32_t threadgroup_depth,
                            uintptr_t compute_function) {
  char path[1024];
  char metallib_path[1024];
  FormatShaderCachePath(path, sizeof(path), "pso-compute-%016zx.json", cs_hash);
  FormatShaderCachePath(metallib_path, sizeof(metallib_path), "%016zx.metallib", cs_hash);
  EnsureShaderCacheDir();
  FILE *df = fopen(path, "w");
  if (!df)
    return;

  fprintf(df, "{\n");
  fprintf(df, "  \"schema\": \"metalsharp.d3d12-metal.offline-pso-manifest.v1\",\n");
  fprintf(df, "  \"source\": \"dxmt-d3d12-runtime\",\n");
  fprintf(df, "  \"pipelines\": [\n");
  fprintf(df, "    {\n");
  fprintf(df, "      \"name\": \"compute-%016zx\",\n", cs_hash);
  fprintf(df, "      \"type\": \"compute\",\n");
  fprintf(df, "      \"d3d12\": { \"cs_hash\": \"%016zx\", \"cs_bytes\": %zu },\n",
          cs_hash, (size_t)cs_size);
  fprintf(df, "      \"shader\": { \"hash\": \"%016zx\", \"metallib\": \"%s\", \"function\": \"cs_main\" },\n",
          cs_hash, metallib_path);
  fprintf(df, "      \"threadgroup_size\": [%llu, %llu, %llu],\n",
          (unsigned long long)threadgroup_width,
          (unsigned long long)threadgroup_height,
          (unsigned long long)threadgroup_depth);
  fprintf(df, "      \"metal\": { \"compute_function\": %llu }\n",
          (unsigned long long)compute_function);
  fprintf(df, "    }\n");
  fprintf(df, "  ]\n");
  fprintf(df, "}\n");
  fclose(df);
  PSTRACE("Compute PSO manifest written to %s", path);
}

void DumpRenderPSOManifest(size_t pso_hash, size_t vs_hash, size_t ps_hash,
                           size_t gs_hash, SIZE_T vs_size, SIZE_T ps_size,
                           SIZE_T gs_size, UINT num_render_targets,
                           const DXGI_FORMAT *rtv_formats,
                           DXGI_FORMAT dsv_format, UINT sample_count,
                           UINT input_elements, uint32_t ia_slot_mask,
                           const std::vector<D3D12IAInputElementInfo> &ia_elements,
                           bool uses_stage_in,
                           bool uses_geometry_mesh,
                           bool rasterization_enabled,
                           uintptr_t vertex_function,
                           uintptr_t fragment_function) {
  char path[1024];
  char vs_metallib_path[1024];
  char ps_metallib_path[1024];
  FormatShaderCachePath(path, sizeof(path), "pso-render-%016zx.json", pso_hash);
  FormatShaderCachePath(vs_metallib_path, sizeof(vs_metallib_path), "%016zx.metallib", vs_hash);
  FormatShaderCachePath(ps_metallib_path, sizeof(ps_metallib_path), "%016zx.metallib", ps_hash);
  EnsureShaderCacheDir();
  FILE *df = fopen(path, "w");
  if (!df)
    return;

  WMTPixelFormat depth_format =
      MTLD3D12PipelineState::DXGIToMTLPixelFormat(dsv_format);
  WMTPixelFormat stencil_format = WMTPixelFormatInvalid;
  if (dsv_format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
      dsv_format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
    stencil_format = depth_format;

  fprintf(df, "{\n");
  fprintf(df, "  \"schema\": \"metalsharp.d3d12-metal.offline-pso-manifest.v1\",\n");
  fprintf(df, "  \"source\": \"dxmt-d3d12-runtime\",\n");
  fprintf(df, "  \"pipelines\": [\n");
  fprintf(df, "    {\n");
  fprintf(df, "      \"name\": \"render-%016zx\",\n", pso_hash);
  fprintf(df, "      \"type\": \"render\",\n");
  fprintf(df, "      \"d3d12\": { \"vs_hash\": \"%016zx\", \"ps_hash\": \"%016zx\", \"gs_hash\": \"%016zx\", \"vs_bytes\": %zu, \"ps_bytes\": %zu, \"gs_bytes\": %zu, \"num_render_targets\": %u, \"dsv_format\": %u, \"input_elements\": %u },\n",
          vs_hash, ps_hash, gs_hash, (size_t)vs_size, (size_t)ps_size,
          (size_t)gs_size, num_render_targets, (unsigned)dsv_format,
          input_elements);
  fprintf(df, "      \"color_formats\": [");
  for (UINT i = 0; i < 8; i++) {
    if (i)
      fprintf(df, ", ");
    fprintf(df, "\"%s\"",
            PixelFormatManifestName(
                MTLD3D12PipelineState::DXGIToMTLPixelFormat(rtv_formats[i])));
  }
  fprintf(df, "],\n");
  fprintf(df, "      \"depth_format\": \"%s\",\n",
          PixelFormatManifestName(depth_format));
  fprintf(df, "      \"stencil_format\": \"%s\",\n",
          PixelFormatManifestName(stencil_format));
  fprintf(df, "      \"sample_count\": %u,\n", sample_count ? sample_count : 1);
  fprintf(df, "      \"input_layout\": { \"slot_mask\": \"0x%08x\", \"elements\": [\n",
          ia_slot_mask);
  for (size_t i = 0; i < ia_elements.size(); i++) {
    const auto &element = ia_elements[i];
    fprintf(df, "        { \"semantic\": ");
    WriteJsonString(df, element.semantic_name);
    fprintf(df, ", \"semantic_index\": %u, \"register\": %u, \"slot\": %u, \"table_index\": %u, \"table_indexing_mode\": \"%s\", \"offset\": %u, \"dxgi_format\": %u, \"metal_format\": %u, \"input_slot_class\": %u, \"class\": \"%s\", \"step_rate\": %u, \"system_value\": %s }%s\n",
            element.semantic_index, element.shader_register,
            element.input_slot, element.table_index,
            D3D12VertexTableIndexingModeName(element.table_indexing_mode),
            element.aligned_byte_offset, (unsigned)element.dxgi_format,
            (unsigned)element.metal_format, (unsigned)element.input_slot_class,
            element.per_instance ? "per_instance" : "per_vertex",
            element.instance_step_rate,
            element.system_value ? "true" : "false",
            i + 1 == ia_elements.size() ? "" : ",");
  }
  fprintf(df, "      ] },\n");
  fprintf(df, "      \"vertex\": { \"hash\": \"%016zx\", \"metallib\": \"%s\", \"function\": \"vs_main\" },\n",
          vs_hash, vs_metallib_path);
  if (ps_size > 0) {
    fprintf(df, "      \"fragment\": { \"hash\": \"%016zx\", \"metallib\": \"%s\", \"function\": \"ps_main\" },\n",
            ps_hash, ps_metallib_path);
  } else {
    fprintf(df, "      \"fragment\": null,\n");
  }
  fprintf(df, "      \"metal\": { \"vertex_function\": %llu, \"fragment_function\": %llu, \"uses_stage_in\": %s, \"uses_geometry_mesh\": %s, \"rasterization_enabled\": %s }\n",
          (unsigned long long)vertex_function,
          (unsigned long long)fragment_function,
          uses_stage_in ? "true" : "false",
          uses_geometry_mesh ? "true" : "false",
          rasterization_enabled ? "true" : "false");
  fprintf(df, "    }\n");
  fprintf(df, "  ]\n");
  fprintf(df, "}\n");
  fclose(df);
  PSTRACE("Render PSO manifest written to %s", path);
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

const char *DxilShaderKindName(dxmt::dxil::DxilShaderKind kind) {
  switch (kind) {
  case dxmt::dxil::DxilShaderKind::Pixel: return "pixel";
  case dxmt::dxil::DxilShaderKind::Vertex: return "vertex";
  case dxmt::dxil::DxilShaderKind::Geometry: return "geometry";
  case dxmt::dxil::DxilShaderKind::Hull: return "hull";
  case dxmt::dxil::DxilShaderKind::Domain: return "domain";
  case dxmt::dxil::DxilShaderKind::Compute: return "compute";
  case dxmt::dxil::DxilShaderKind::Library: return "library";
  case dxmt::dxil::DxilShaderKind::Mesh: return "mesh";
  case dxmt::dxil::DxilShaderKind::Amplification: return "amplification";
  default: return "other";
  }
}

const char *RootParameterTypeName(D3D12_ROOT_PARAMETER_TYPE type) {
  switch (type) {
  case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: return "descriptor_table";
  case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return "constants";
  case D3D12_ROOT_PARAMETER_TYPE_CBV: return "cbv";
  case D3D12_ROOT_PARAMETER_TYPE_SRV: return "srv";
  case D3D12_ROOT_PARAMETER_TYPE_UAV: return "uav";
  default: return "unknown";
  }
}

const char *DescriptorRangeTypeName(D3D12_DESCRIPTOR_RANGE_TYPE type) {
  switch (type) {
  case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return "srv";
  case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return "uav";
  case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return "cbv";
  case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: return "sampler";
  default: return "unknown";
  }
}

const char *ShaderVisibilityName(uint32_t visibility) {
  switch ((D3D12_SHADER_VISIBILITY)visibility) {
  case D3D12_SHADER_VISIBILITY_ALL: return "all";
  case D3D12_SHADER_VISIBILITY_VERTEX: return "vertex";
  case D3D12_SHADER_VISIBILITY_HULL: return "hull";
  case D3D12_SHADER_VISIBILITY_DOMAIN: return "domain";
  case D3D12_SHADER_VISIBILITY_GEOMETRY: return "geometry";
  case D3D12_SHADER_VISIBILITY_PIXEL: return "pixel";
  default: return "unknown";
  }
}

void DumpRootSignatureSummary(FILE *df, const MTLD3D12RootSignature *root_sig) {
  fprintf(df, "\nroot_signature:\n");
  if (!root_sig) {
    fprintf(df, "  present=0\n");
    return;
  }

  const auto &parameters = root_sig->GetParameters();
  const auto &static_samplers = root_sig->GetStaticSamplers();
  fprintf(df, "  present=1\n");
  fprintf(df, "  blob_hash=0x%016zx\n", root_sig->GetBlobHash());
  fprintf(df, "  flags=0x%08x\n", (uint32_t)root_sig->GetFlags());
  fprintf(df, "  parameter_count=%zu\n", parameters.size());
  fprintf(df, "  static_sampler_count=%zu\n", static_samplers.size());

  for (size_t i = 0; i < parameters.size(); i++) {
    const auto &param = parameters[i];
    fprintf(df,
            "  parameter[%zu] type=%s visibility=%s register_space=%u register=%u descriptors=%u range_type=%s table_ranges=%zu\n",
            i, RootParameterTypeName(param.type),
            ShaderVisibilityName(param.shader_visibility), param.register_space,
            param.register_index, param.num_descriptors,
            DescriptorRangeTypeName(param.range_type), param.ranges.size());
    for (size_t r = 0; r < param.ranges.size(); r++) {
      const auto &range = param.ranges[r];
      fprintf(df,
              "    range[%zu] type=%s space=%u base=%u count=%u offset=%u\n",
              r, DescriptorRangeTypeName(range.range_type),
              range.register_space, range.base_register, range.num_descriptors,
              range.offset_in_table);
    }
  }

  for (size_t i = 0; i < static_samplers.size(); i++) {
    const auto &sampler = static_samplers[i];
    fprintf(df,
            "  static_sampler[%zu] visibility=%s space=%u register=%u sampler_gpu=0x%016llx sampler_cube_gpu=0x%016llx lod_bias_bits=0x%016llx\n",
            i, ShaderVisibilityName(sampler.shader_visibility),
            sampler.register_space, sampler.shader_register,
            (unsigned long long)sampler.sampler_gpu_id,
            (unsigned long long)sampler.sampler_cube_gpu_id,
            (unsigned long long)sampler.lod_bias_bits);
  }
}

void DumpDXILModuleSummary(const char *path, const dxmt::dxil::LLVMModule &module,
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
  fprintf(df, "num_threads=%u,%u,%u\n", module.num_threads[0],
          module.num_threads[1], module.num_threads[2]);

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
            "  name=%s declaration=%d value=%u type=%u params=%u inst_start=%u blocks=%zu instructions=%zu\n",
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
                           const char *module_summary_path, const char *msl_path,
                           const dxmt::dxil::LLVMModule &module,
                           const dxmt::dxil::DxilParsedShader &shader_info,
                           const dxmt::dxil::MSLShader &msl_result,
                           const MTLD3D12RootSignature *root_sig) {
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
  fprintf(df, "unsupported_intrinsics=%u\n",
          msl_result.unsupported_intrinsics);
  fprintf(df, "unsupported_opcodes=%u\n", msl_result.unsupported_opcodes);
  fprintf(df, "dxbc=%s\n", dxbc_path ? dxbc_path : "");
  fprintf(df, "module=%s\n", module_summary_path ? module_summary_path : "");
  fprintf(df, "msl=%s\n", msl_path ? msl_path : "");
  DumpRootSignatureSummary(df, root_sig);
  fprintf(df, "\ndiagnostics:\n");
  for (const auto &diagnostic : msl_result.diagnostics)
    fprintf(df, "  %s\n", diagnostic.c_str());

  fclose(df);

  char index_path[1024];
  snprintf(index_path, sizeof(index_path), "%s/dxil_report_index.tsv",
           ShaderCacheDir().c_str());
  FILE *index = fopen(index_path, "a");
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
    WMTCompareFunctionNever,
    WMTCompareFunctionNever,
    WMTCompareFunctionLess,
    WMTCompareFunctionEqual,
    WMTCompareFunctionLessEqual,
    WMTCompareFunctionGreater,
    WMTCompareFunctionNotEqual,
    WMTCompareFunctionGreaterEqual,
    WMTCompareFunctionAlways,
};

constexpr WMTStencilOperation kStencilOperationMap[] = {
    WMTStencilOperationZero,
    WMTStencilOperationKeep,
    WMTStencilOperationZero,
    WMTStencilOperationReplace,
    WMTStencilOperationIncrementClamp,
    WMTStencilOperationDecrementClamp,
    WMTStencilOperationInvert,
    WMTStencilOperationIncrementWrap,
    WMTStencilOperationDecrementWrap,
};

} // namespace

void ShutdownAsyncPipelineCompiler() {
  GetAsyncPipelineCompiler().Shutdown();
}

void RequestShutdownAsyncPipelineCompiler() {
  GetAsyncPipelineCompiler().RequestShutdown();
}

std::mutex MTLD3D12PipelineState::s_shader_mutex;
std::unordered_map<size_t, WMT::Reference<WMT::Function>> MTLD3D12PipelineState::s_shader_cache;

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
  m_compile_failure_stage.clear();
  m_compile_failure_detail.clear();
}

bool MTLD3D12PipelineState::RecordCompileFailure(const char *stage, const std::string &detail) {
  m_compile_failure_stage = stage ? stage : "unknown";
  m_compile_failure_detail = detail;
  m_compile_state.store(CompileState::Failed);
  m_compile_cv.notify_all();
  PSTRACE("PSO COMPILE FAILURE: this=%p compute=%d stage=%s detail=%s",
          (void *)this, m_is_compute, m_compile_failure_stage.c_str(),
          m_compile_failure_detail.c_str());
  return false;
}

bool MTLD3D12PipelineState::IsCompiled() const {
  return m_compile_state.load() == CompileState::Compiled;
}

bool MTLD3D12PipelineState::IsCompilePending() const {
  CompileState state = m_compile_state.load();
  return state == CompileState::Pending || state == CompileState::Compiling;
}

std::string MTLD3D12PipelineState::GetVSCacheHash() const {
  if (m_vs.empty())
    return {};
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%016zx",
           ComputeShaderCacheHash(m_vs.data(), m_vs.size(), ShaderType::Vertex,
                                  &m_input_layout));
  return buffer;
}

std::string MTLD3D12PipelineState::GetPSCacheHash() const {
  if (m_ps.empty())
    return {};
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%016zx",
           ComputeShaderCacheHash(m_ps.data(), m_ps.size(), ShaderType::Pixel,
                                  nullptr));
  return buffer;
}

std::string MTLD3D12PipelineState::GetGSCacheHash() const {
  if (m_gs.empty())
    return {};
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%016zx",
           ComputeShaderCacheHash(m_gs.data(), m_gs.size(),
                                  ShaderType::Geometry, nullptr));
  return buffer;
}

std::string MTLD3D12PipelineState::GetCompileFailureStage() const {
  return m_compile_failure_stage;
}

std::string MTLD3D12PipelineState::GetCompileFailureDetail() const {
  return m_compile_failure_detail;
}

bool MTLD3D12PipelineState::RequestCompile(bool allow_async) {
  if (!allow_async || !AsyncPipelineCompileEnabled())
    return Compile();

  CompileState expected = CompileState::NotStarted;
  if (m_compile_state.compare_exchange_strong(expected, CompileState::Pending)) {
    PSTRACE("PSO async compile scheduled pso=%p compute=%d", (void *)this,
            m_is_compute);
    GetAsyncPipelineCompiler().Enqueue(this);
    return false;
  }

  return expected == CompileState::Compiled;
}

bool MTLD3D12PipelineState::TryCompilePendingInline() {
  CompileState expected = CompileState::Pending;
  if (!m_compile_state.compare_exchange_strong(expected,
                                               CompileState::NotStarted))
    return expected == CompileState::Compiled;

  PSTRACE("PSO pending compile promoted inline pso=%p compute=%d",
          (void *)this, m_is_compute);
  return Compile();
}

void MTLD3D12PipelineState::RunAsyncCompile() {
  Compile();
}

WMTPixelFormat MTLD3D12PipelineState::DXGIToMTLPixelFormat(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM: return WMTPixelFormatRGBA8Unorm;
  case DXGI_FORMAT_R8G8B8A8_TYPELESS: return WMTPixelFormatRGBA8Unorm;
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return WMTPixelFormatRGBA8Unorm_sRGB;
  case DXGI_FORMAT_B8G8R8A8_UNORM: return WMTPixelFormatBGRA8Unorm;
  case DXGI_FORMAT_B8G8R8A8_TYPELESS: return WMTPixelFormatBGRA8Unorm;
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return WMTPixelFormatBGRA8Unorm_sRGB;
  case DXGI_FORMAT_R16G16B16A16_FLOAT: return WMTPixelFormatRGBA16Float;
  case DXGI_FORMAT_R32G32B32A32_FLOAT: return WMTPixelFormatRGBA32Float;
  case DXGI_FORMAT_R10G10B10A2_UNORM: return WMTPixelFormatRGB10A2Unorm;
  case DXGI_FORMAT_R11G11B10_FLOAT: return WMTPixelFormatRG11B10Float;
  case DXGI_FORMAT_R8_UNORM: return WMTPixelFormatR8Unorm;
  case DXGI_FORMAT_R16_FLOAT: return WMTPixelFormatR16Float;
  case DXGI_FORMAT_R32_FLOAT: return WMTPixelFormatR32Float;
  case DXGI_FORMAT_D32_FLOAT: return WMTPixelFormatDepth32Float;
  case DXGI_FORMAT_D24_UNORM_S8_UINT:
    return WMTPixelFormatDepth32Float_Stencil8;
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return WMTPixelFormatDepth32Float_Stencil8;
  case DXGI_FORMAT_D16_UNORM: return WMTPixelFormatDepth16Unorm;
  case DXGI_FORMAT_R16G16_FLOAT: return WMTPixelFormatRG16Float;
  case DXGI_FORMAT_R16G16_UNORM: return WMTPixelFormatRG16Unorm;
  case DXGI_FORMAT_R8G8_UNORM: return WMTPixelFormatRG8Unorm;
  case DXGI_FORMAT_BC1_TYPELESS: return WMTPixelFormatBC1_RGBA;
  case DXGI_FORMAT_BC1_UNORM: return WMTPixelFormatBC1_RGBA;
  case DXGI_FORMAT_BC1_UNORM_SRGB: return WMTPixelFormatBC1_RGBA_sRGB;
  case DXGI_FORMAT_BC2_TYPELESS: return WMTPixelFormatBC2_RGBA;
  case DXGI_FORMAT_BC2_UNORM: return WMTPixelFormatBC2_RGBA;
  case DXGI_FORMAT_BC2_UNORM_SRGB: return WMTPixelFormatBC2_RGBA_sRGB;
  case DXGI_FORMAT_BC3_TYPELESS: return WMTPixelFormatBC3_RGBA;
  case DXGI_FORMAT_BC3_UNORM: return WMTPixelFormatBC3_RGBA;
  case DXGI_FORMAT_BC3_UNORM_SRGB: return WMTPixelFormatBC3_RGBA_sRGB;
  case DXGI_FORMAT_BC4_TYPELESS: return WMTPixelFormatBC4_RUnorm;
  case DXGI_FORMAT_BC4_UNORM: return WMTPixelFormatBC4_RUnorm;
  case DXGI_FORMAT_BC4_SNORM: return WMTPixelFormatBC4_RSnorm;
  case DXGI_FORMAT_BC5_TYPELESS: return WMTPixelFormatBC5_RGUnorm;
  case DXGI_FORMAT_BC5_UNORM: return WMTPixelFormatBC5_RGUnorm;
  case DXGI_FORMAT_BC5_SNORM: return WMTPixelFormatBC5_RGSnorm;
  case DXGI_FORMAT_BC6H_TYPELESS: return WMTPixelFormatBC6H_RGBUfloat;
  case DXGI_FORMAT_BC6H_UF16: return WMTPixelFormatBC6H_RGBUfloat;
  case DXGI_FORMAT_BC6H_SF16: return WMTPixelFormatBC6H_RGBFloat;
  case DXGI_FORMAT_BC7_TYPELESS: return WMTPixelFormatBC7_RGBAUnorm;
  case DXGI_FORMAT_BC7_UNORM: return WMTPixelFormatBC7_RGBAUnorm;
  case DXGI_FORMAT_BC7_UNORM_SRGB: return WMTPixelFormatBC7_RGBAUnorm_sRGB;
  default: return WMTPixelFormatInvalid;
  }
}

bool MTLD3D12PipelineState::CompileShader(const void *bytecode, SIZE_T size,
                                          ShaderType type,
                                          const char *func_name,
                                          WMT::Reference<WMT::Function> &out_func,
                                          sm50_shader_t *out_shader_handle,
                                          MTL_SHADER_REFLECTION *out_reflection) {
  size_t hash = ComputeShaderCacheHash(bytecode, size, type,
                                       type == ShaderType::Vertex
                                           ? &m_input_layout
                                           : nullptr);
  std::vector<SM50_IA_INPUT_ELEMENT> ia_elements;
  uint32_t ia_slot_mask = 0;
  if (type == ShaderType::Vertex) {
    BuildIAInputLayout(bytecode, size, ia_elements, ia_slot_mask);
    m_ia_slot_mask = ia_slot_mask;
  }

  bool dxil_shader = ShaderBytecodeContainsDxil(bytecode, size);
  bool reflection_independent_cache = dxil_shader || (!out_shader_handle && !out_reflection);
  {
    std::lock_guard<std::mutex> lock(s_shader_mutex);
    PSTRACE("CompileShader: %s hash=0x%zx size=%zu cache_entries=%zu dxil=%u", func_name, hash, size, s_shader_cache.size(), dxil_shader ? 1u : 0u);
    auto it = s_shader_cache.find(hash);
    if (it != s_shader_cache.end() && reflection_independent_cache) {
      out_func = it->second;
      if (out_shader_handle)
        *out_shader_handle = nullptr;
      if (out_reflection)
        *out_reflection = {};
      if (type == ShaderType::Vertex)
        m_vs_uses_stage_in = false;
      uint64_t hits = ++g_shader_memory_cache_hits;
      dxmt::m12core::RecordCounter(M12CORE_COUNTER_SHADER_MEMORY_CACHE_HITS);
      Logger::info(str::format("PSO_PRESSURE shader_memory_cache_hit stage=", func_name,
                               " hash=0x", std::hex, hash, std::dec, " hits=", hits));
      return true;
    }
    if (reflection_independent_cache) {
      uint64_t misses = ++g_shader_memory_cache_misses;
      dxmt::m12core::RecordCounter(M12CORE_COUNTER_SHADER_MEMORY_CACHE_MISSES);
      Logger::info(str::format("PSO_PRESSURE shader_memory_cache_miss stage=", func_name,
                               " hash=0x", std::hex, hash, std::dec, " misses=", misses));
    }
  }

  if (bytecode && size >= 4) {
    auto *magic = (const uint32_t *)bytecode;
    PSTRACE("CompileShader: %s size=%zu magic=0x%08x (DXBC=0x43425844 DXIL=0x4C495844)", func_name, size, *magic);
    if (*magic == 0x43425844 && size >= 32) {
      auto *chunks = (const uint32_t *)bytecode;
      uint32_t container_size = chunks[6];
      uint32_t num_chunks = chunks[7];
      PSTRACE("  DXBC: container_size=%u num_chunks=%u", container_size, num_chunks);
      for (uint32_t i = 0; i < num_chunks && i < 16; i++) {
        uint32_t offset = chunks[8 + i];
        if (offset + 8 <= size) {
          char tag[5] = {};
          memcpy(tag, (const char *)bytecode + offset, 4);
          uint32_t chunk_size = 0;
          memcpy(&chunk_size, (const char *)bytecode + offset + 4,
                 sizeof(chunk_size));
          PSTRACE("  chunk[%u]: tag='%s' offset=%u size=%u", i, tag, offset, chunk_size);
        }
      }
    }
  }
  sm50_error_t sm50_err = nullptr;
  sm50_shader_t shader = nullptr;
  MTL_SHADER_REFLECTION reflection = {};
  const uint32_t sm50_options = 0;
  if (SM50InitializeWithOptions(bytecode, size, sm50_options,
                                &shader, &reflection, &sm50_err)) {
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

          char cache_path[1024];
          char dxbc_path[1024], metallib_path[1024], reflection_path[1024],
              module_summary_path[1024], dxil_report_path[1024],
              metallib_error_path[1024];
          bool force_source_compile = EnvFlagEnabled("DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE");
          bool core_lookup_valid = false;
          M12CoreShaderCacheLookup core_lookup = {};
          auto cache_root = ShaderCacheDir();
          if (WMTM12CoreProbeShaderCache(cache_root.c_str(), (uint64_t)hash,
                                         force_source_compile ? 1u : 0u,
                                         &core_lookup) &&
              core_lookup.abi_version == M12CORE_ABI_VERSION &&
              core_lookup.paths.path_capacity == M12CORE_SHADER_CACHE_PATH_CAPACITY) {
            core_lookup_valid = true;
            snprintf(cache_path, sizeof(cache_path), "%s", core_lookup.paths.cache_path);
            snprintf(dxbc_path, sizeof(dxbc_path), "%s", core_lookup.paths.dxbc_path);
            snprintf(metallib_path, sizeof(metallib_path), "%s", core_lookup.paths.metallib_path);
            snprintf(reflection_path, sizeof(reflection_path), "%s", core_lookup.paths.reflection_path);
            snprintf(module_summary_path, sizeof(module_summary_path), "%s", core_lookup.paths.module_summary_path);
            snprintf(dxil_report_path, sizeof(dxil_report_path), "%s", core_lookup.paths.dxil_report_path);
            snprintf(metallib_error_path, sizeof(metallib_error_path), "%s", core_lookup.paths.metallib_error_path);
          } else {
            FormatShaderCachePath(cache_path, sizeof(cache_path), "%016zx", hash);
            snprintf(dxbc_path, sizeof(dxbc_path), "%s.dxbc", cache_path);
            snprintf(metallib_path, sizeof(metallib_path), "%s.metallib", cache_path);
            snprintf(reflection_path, sizeof(reflection_path), "%s.json", cache_path);
            snprintf(module_summary_path, sizeof(module_summary_path),
                     "%s.module.txt", cache_path);
            snprintf(dxil_report_path, sizeof(dxil_report_path),
                     "%s.dxil_report.txt", cache_path);
            snprintf(metallib_error_path, sizeof(metallib_error_path),
                     "%s.metallib.err.txt", cache_path);
          }
          EnsureShaderCacheDir();
          DumpShaderBlob(dxbc_path, bytecode, size);

          FILE *mf = nullptr;
          if (core_lookup_valid) {
            if (core_lookup.metallib_available)
              mf = fopen(metallib_path, "rb");
            if (!core_lookup.metallib_available && core_lookup.force_source_compile &&
                core_lookup.metallib_exists)
              PSTRACE("  cached metallib ignored by libm12core lookup policy due to DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE; attempting DXIL->MSL compilation");
          } else {
            mf = fopen(metallib_path, "rb");
            if (mf && force_source_compile) {
              fclose(mf);
              mf = nullptr;
              PSTRACE("  cached metallib ignored by DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE; attempting DXIL->MSL compilation");
            }
          }
          if (!mf) {
            uint64_t misses = ++g_shader_metallib_cache_misses;
            dxmt::m12core::RecordCounter(M12CORE_COUNTER_SHADER_METALLIB_CACHE_MISSES);
            Logger::info(str::format("PSO_PRESSURE shader_metallib_cache_miss stage=", func_name,
                                     " hash=0x", std::hex, hash, std::dec, " metallib_misses=", misses));

            auto container = dxmt::dxil::DXILContainer::parse(blob, blob_size);
            if (!container) {
              PSTRACE("  DXILContainer::parse FAILED for %s", func_name);
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure("shader/dxil_container_parse",
                                          str::format(func_name, " DXIL container parse failed; dumped ", dxbc_path));
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
              return RecordCompileFailure("shader/bitcode_parse",
                                          str::format(func_name, " DXIL bitcode parse failed; dumped ", dxbc_path));
            }

            PSTRACE("  Bitcode parsed: types=%zu functions=%zu constants=%zu",
                    module->types.size(), module->functions.size(), module->constants.size());
            DumpDXILModuleSummary(module_summary_path, *module, shader_info);
            PSTRACE("  DXIL module summary written to %s", module_summary_path);

            for (const auto &input : m_ia_input_elements) {
              PSTRACE("  M12 vertex input map reg=%u slot=%u table=%u system=%u",
                      input.shader_register, input.input_slot,
                      input.table_index, input.system_value ? 1u : 0u);
            }

            std::optional<dxmt::dxil::MSLShader> msl_result;
            bool typed_msl_used = false;
            dxmt::dxil::MSLShader core_msl = {};
            if (LowerDXILToMSLWithM12Core(blob, blob_size, type, m_ia_input_elements,
                                          core_msl, typed_msl_used)) {
              msl_result.emplace(std::move(core_msl));
              PSTRACE("  MSL generated by libm12core via %s", typed_msl_used ? "MSLLowering" : "DXILToMSL");
            } else {
              dxmt::dxil::MSLLoweringOptions lowering_options = {};
              if (type == ShaderType::Vertex) {
                lowering_options.vertex_inputs.reserve(m_ia_input_elements.size());
                for (const auto &input : m_ia_input_elements) {
                  if (input.table_index >= kMetalD3D12VertexBufferSlotCount)
                    continue;
                  dxmt::dxil::MSLVertexInputElement element = {};
                  element.shader_register = input.shader_register;
                  element.table_index = input.table_index;
                  element.input_slot = input.input_slot;
                  element.aligned_byte_offset = input.aligned_byte_offset;
                  element.dxgi_format = static_cast<uint32_t>(input.dxgi_format);
                  element.metal_format = static_cast<uint32_t>(input.metal_format);
                  element.per_instance = input.per_instance;
                  element.instance_step_rate = input.instance_step_rate;
                  element.table_indexing_mode =
                      input.table_indexing_mode ==
                              D3D12VertexTableIndexingMode::RawSlot
                          ? dxmt::dxil::MSLVertexTableIndexingMode::RawSlot
                          : dxmt::dxil::MSLVertexTableIndexingMode::
                                CompactBySlotMask;
                  element.system_value = input.system_value;
                  lowering_options.vertex_inputs.push_back(element);
                }
              }

              auto typed_msl =
                  dxmt::dxil::MSLLowering::lower(*module, shader_info,
                                                 lowering_options);
              typed_msl_used = typed_msl.has_value();
              msl_result = typed_msl
                  ? std::optional<dxmt::dxil::MSLShader>(std::in_place, ToRuntimeMSLShader(std::move(*typed_msl)))
                  : dxmt::dxil::DXILToMSL::convert(*module, shader_info);
            }
            if (!msl_result) {
              PSTRACE("  libm12core/PE DXIL->MSL lowering FAILED");
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure("shader/dxil_to_msl",
                                          str::format(func_name,
                                                      " DXIL to MSL conversion failed; module ",
                                                      module_summary_path,
                                                      "; dxbc ", dxbc_path));
            }

            PSTRACE("  MSL generated via %s: %zu bytes, entry=%s unsupported_intrinsics=%u unsupported_opcodes=%u",
                    typed_msl_used ? "MSLLowering" : "DXILToMSL",
                    msl_result->source.size(), msl_result->entry_point.c_str(),
                    msl_result->unsupported_intrinsics, msl_result->unsupported_opcodes);

            char msl_path[1024];
            char msl_error_path[1024];
            snprintf(msl_path, sizeof(msl_path), "%s.msl", cache_path);
            snprintf(msl_error_path, sizeof(msl_error_path),
                     "%s.msl.err.txt", cache_path);
            DumpShaderText(msl_path, msl_result->source.c_str());
            PSTRACE("  MSL source written to %s", msl_path);
            DumpDXILCompileReport(dxil_report_path, func_name, hash, size,
                                  dxbc_path, module_summary_path, msl_path,
                                  *module, shader_info, *msl_result,
                                  static_cast<MTLD3D12RootSignature *>(m_root_sig));
            PSTRACE("  DXIL compile report written to %s", dxil_report_path);

            const char *dump_msl = std::getenv("DXMT_DUMP_MSL");
            if (dump_msl && dump_msl[0] && strcmp(dump_msl, "0") != 0) {
              char dump_path[1024];
              snprintf(dump_path, sizeof(dump_path), "%s/dxmt_msl_%s_%016zx.metal",
                       ShaderCacheDir().c_str(), func_name, hash);
              FILE *df = fopen(dump_path, "w");
              if (df) { fwrite(msl_result->source.c_str(), 1, msl_result->source.size(), df); fclose(df); }
            }

            const char *entry_name = msl_result->entry_point.c_str();
            if (strcmp(entry_name, "cs_main") != 0 &&
                strcmp(entry_name, "vs_main") != 0 &&
                strcmp(entry_name, "ps_main") != 0) {
              switch (shader_info.kind) {
              case dxmt::dxil::DxilShaderKind::Compute: entry_name = "cs_main"; break;
              case dxmt::dxil::DxilShaderKind::Vertex: entry_name = "vs_main"; break;
              case dxmt::dxil::DxilShaderKind::Pixel: entry_name = "ps_main"; break;
              default: break;
              }
            }

            M12CoreShaderFunctionResult core_function = {};
            WMT::Reference<WMT::Error> core_error;
            bool core_function_attempted = CreateM12CoreShaderFunction(
                wmt_device.handle, type, M12CORE_SHADER_FUNCTION_INPUT_MSL_SOURCE,
                hash, msl_result->source.c_str(), msl_result->source.size(),
                entry_name, out_func, &core_error, &core_function);
            if (core_function_attempted) {
              if (core_function.status == M12CORE_SHADER_FUNCTION_STATUS_OK && out_func.handle) {
                PSTRACE("  DXIL shader compiled by libm12core OK! entry=%s cache_hit=%u func=%llu",
                        core_function.selected_entry[0] ? core_function.selected_entry : entry_name,
                        core_function.cache_hit,
                        (unsigned long long)out_func.handle);
                {
                  std::lock_guard<std::mutex> lock(s_shader_mutex);
                  s_shader_cache[hash] = out_func;
                }
                if (type == ShaderType::Vertex)
                  m_vs_uses_stage_in = false;
                if (shader_info.kind == dxmt::dxil::DxilShaderKind::Compute) {
                  m_threadgroup_size.width = msl_result->tg_size[0];
                  m_threadgroup_size.height = msl_result->tg_size[1];
                  m_threadgroup_size.depth = msl_result->tg_size[2];
                }
                return true;
              }
              if (core_function.status == M12CORE_SHADER_FUNCTION_STATUS_LIBRARY_FAILED) {
                auto err_desc = core_error.handle ? DescribeNSObject(core_error.handle) : "unknown";
                DumpShaderText(msl_error_path, err_desc.c_str());
                PSTRACE("  libm12core newLibraryWithSource FAILED: %s", err_desc.c_str());
                Logger::err(str::format("DXIL MSL compilation failed in libm12core for ", func_name, ": ",
                                         err_desc));
                DumpShaderBlob(dxbc_path, bytecode, size);
                return RecordCompileFailure("shader/m12core_metal_library_source",
                                            str::format(func_name, " libm12core MSL compile failed: ",
                                                        err_desc,
                                                        "; msl ", msl_path,
                                                        "; error ", msl_error_path,
                                                        "; dxbc ", dxbc_path));
              }
              PSTRACE("  libm12core function lookup failed status=%u", core_function.status);
              Logger::err(str::format("DXIL: libm12core failed to get function from compiled library for ", func_name));
              return RecordCompileFailure("shader/m12core_metal_function_lookup",
                                          str::format(func_name, " libm12core function lookup failed after MSL compile; msl ",
                                                      msl_path));
            }

            WMT::Reference<WMT::Error> compile_err;
            auto library = wmt_device.newLibraryWithSource(
                msl_result->source.c_str(), msl_result->source.size(), compile_err);

            if (compile_err.handle) {
              auto err_desc = DescribeNSObject(compile_err.handle);
              DumpShaderText(msl_error_path, err_desc.c_str());
              PSTRACE("  fallback newLibraryWithSource FAILED: %s", err_desc.c_str());
              Logger::err(str::format("DXIL MSL compilation failed for ", func_name, ": ",
                                       err_desc));
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure("shader/metal_library_source",
                                          str::format(func_name, " MSL compile failed: ",
                                                      err_desc,
                                                      "; msl ", msl_path,
                                                      "; error ", msl_error_path,
                                                      "; dxbc ", dxbc_path));
            }

            PSTRACE("  fallback Metal library compiled OK from source lib_handle=%llu", (unsigned long long)library.handle);

            out_func = library.newFunction(entry_name);
            PSTRACE("  fallback newFunction(%s) on lib=%llu -> func_handle=%llu", entry_name, (unsigned long long)library.handle, (unsigned long long)out_func.handle);
            if (!out_func.handle) {
              PSTRACE("  fallback newFunction(%s) returned null, trying alternatives", entry_name);
              out_func = library.newFunction("main");
              if (!out_func.handle)
                out_func = library.newFunction("cs_main");
              if (!out_func.handle)
                out_func = library.newFunction("vs_main");
              if (!out_func.handle)
                out_func = library.newFunction("ps_main");
            }

            if (out_func.handle) {
              PSTRACE("  DXIL shader compiled OK via fallback! entry=%s", entry_name);
              {
                std::lock_guard<std::mutex> lock(s_shader_mutex);
                s_shader_cache[hash] = out_func;
              }

              if (type == ShaderType::Vertex)
                m_vs_uses_stage_in = false;

              if (shader_info.kind == dxmt::dxil::DxilShaderKind::Compute) {
                m_threadgroup_size.width = msl_result->tg_size[0];
                m_threadgroup_size.height = msl_result->tg_size[1];
                m_threadgroup_size.depth = msl_result->tg_size[2];
              }
              return true;
            } else {
              PSTRACE("  fallback newFunction returned null for all entry points");
              Logger::err(str::format("DXIL: failed to get function from compiled library for ", func_name));
              return RecordCompileFailure("shader/metal_function_lookup",
                                          str::format(func_name, " function lookup failed after MSL compile; msl ",
                                                      msl_path));
            }
          }

          {
            uint64_t hits = ++g_shader_metallib_cache_hits;
            dxmt::m12core::RecordCounter(M12CORE_COUNTER_SHADER_METALLIB_CACHE_HITS);
            Logger::info(str::format("PSO_PRESSURE shader_metallib_cache_hit stage=", func_name,
                                     " hash=0x", std::hex, hash, std::dec, " metallib_hits=", hits,
                                     " path=", metallib_path));
          }
          fseek(mf, 0, SEEK_END);
          long lib_size = ftell(mf);
          fseek(mf, 0, SEEK_SET);
          PSTRACE("  metallib size=%ld", lib_size);
          if (lib_size > 0) {
            std::vector<uint8_t> lib_data(lib_size);
            fread(lib_data.data(), 1, lib_size, mf);
            fclose(mf);
            char actual_entry[256] = {};
            char rbuf[4096] = {};
            size_t rbuf_len = 0;
            M12CoreShaderReflectionSummary core_reflection = {};
            bool core_reflection_valid = false;
            FILE *rf = fopen(reflection_path, "r");
            if (rf) {
              rbuf_len = fread(rbuf, 1, sizeof(rbuf)-1, rf);
              fclose(rf);
              rbuf[rbuf_len] = 0;
              core_reflection_valid = WMTM12CoreParseShaderReflection(
                  rbuf, rbuf_len, &core_reflection) &&
                  core_reflection.abi_version == M12CORE_ABI_VERSION;
              if (core_reflection_valid && core_reflection.has_entry_point) {
                snprintf(actual_entry, sizeof(actual_entry), "%s", core_reflection.entry_point);
              } else {
                char *ep = strstr(rbuf, "\"EntryPoint\"");
                if (ep) {
                  char *q1 = strchr(ep + 13, '"');
                  char *q2 = q1 ? strchr(q1+1, '"') : nullptr;
                  if (q1 && q2) {
                    size_t len = q2 - q1 - 1;
                    if (len < sizeof(actual_entry)) {
                      memcpy(actual_entry, q1+1, len);
                      actual_entry[len] = 0;
                    }
                  }
                }
              }
            }
            const char *fn_name = actual_entry[0] ? actual_entry : func_name;

            auto apply_cached_reflection = [&]() {
              if (core_reflection_valid && core_reflection.has_threadgroup_size) {
                m_threadgroup_size.width = core_reflection.threadgroup_size[0];
                m_threadgroup_size.height = core_reflection.threadgroup_size[1];
                m_threadgroup_size.depth = core_reflection.threadgroup_size[2];
                PSTRACE("  threadgroup_size from libm12core reflection: %ux%ux%u",
                        core_reflection.threadgroup_size[0], core_reflection.threadgroup_size[1],
                        core_reflection.threadgroup_size[2]);
              } else {
                char *tg = strstr(rbuf, "\"tg_size\"");
                if (tg) {
                  int tw=1,th=1,td=1;
                  if (sscanf(tg, "\"tg_size\": [%d, %d, %d]", &tw, &th, &td) == 3 ||
                      sscanf(tg, "\"tg_size\":[%d,%d,%d]", &tw, &th, &td) == 3) {
                    m_threadgroup_size.width = tw;
                    m_threadgroup_size.height = th;
                    m_threadgroup_size.depth = td;
                    PSTRACE("  threadgroup_size from reflection: %dx%dx%d", tw, th, td);
                  }
                }
              }
            };

            M12CoreShaderFunctionResult core_function = {};
            WMT::Reference<WMT::Error> core_error;
            bool core_function_attempted = CreateM12CoreShaderFunction(
                wmt_device.handle, type, M12CORE_SHADER_FUNCTION_INPUT_METALLIB,
                hash, lib_data.data(), lib_data.size(), fn_name, out_func,
                &core_error, &core_function);
            if (core_function_attempted) {
              if (core_function.status == M12CORE_SHADER_FUNCTION_STATUS_OK && out_func.handle) {
                PSTRACE("  DXIL loaded from cache by libm12core OK! entry=%s cache_hit=%u func=%llu",
                        core_function.selected_entry[0] ? core_function.selected_entry : fn_name,
                        core_function.cache_hit,
                        (unsigned long long)out_func.handle);
                {
                  std::lock_guard<std::mutex> lock(s_shader_mutex);
                  s_shader_cache[hash] = out_func;
                }
                if (type == ShaderType::Vertex)
                  m_vs_uses_stage_in = false;
                apply_cached_reflection();
                return true;
              }
              if (core_function.status == M12CORE_SHADER_FUNCTION_STATUS_LIBRARY_FAILED) {
                auto err_desc = core_error.handle ? DescribeNSObject(core_error.handle) : "unknown";
                DumpShaderText(metallib_error_path, err_desc.c_str());
                DumpShaderBlob(dxbc_path, bytecode, size);
                PSTRACE("  libm12core cached metallib load FAILED: %s", err_desc.c_str());
                return RecordCompileFailure(
                    "shader/m12core_dxil_cached_metallib_load",
                    str::format(func_name,
                                " libm12core cached metallib load failed: ",
                                err_desc,
                                "; metallib ", metallib_path, "; error ",
                                metallib_error_path, "; dxbc ", dxbc_path));
              }
              PSTRACE("  libm12core cached metallib function lookup failed status=%u", core_function.status);
              DumpShaderBlob(dxbc_path, bytecode, size);
              return RecordCompileFailure(
                  "shader/m12core_dxil_cached_function_lookup",
                  str::format(func_name,
                              " libm12core cached metallib function lookup failed; metallib ",
                              metallib_path, "; reflection ", reflection_path,
                              "; dxbc ", dxbc_path));
            }

            auto dispatch_data = WMT::MakeDispatchData(lib_data.data(), lib_size);
            WMT::Reference<WMT::Error> err;
            auto library = wmt_device.newLibrary(dispatch_data, err);
            if (!err.handle) {
              PSTRACE("  fallback trying newFunction(%s)", fn_name);
              out_func = library.newFunction(fn_name);
              if (!out_func.handle && actual_entry[0]) {
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
                PSTRACE("  DXIL loaded from cache OK via fallback! entry=%s", fn_name);
                {
                  std::lock_guard<std::mutex> lock(s_shader_mutex);
                  s_shader_cache[hash] = out_func;
                }
                if (type == ShaderType::Vertex)
                  m_vs_uses_stage_in = false;
                apply_cached_reflection();
                return true;
              } else {
                PSTRACE("  fallback WMT newFunction returned null");
                DumpShaderBlob(dxbc_path, bytecode, size);
                return RecordCompileFailure(
                    "shader/dxil_cached_function_lookup",
                    str::format(func_name,
                                " cached metallib function lookup failed; metallib ",
                                metallib_path, "; reflection ", reflection_path,
                                "; dxbc ", dxbc_path));
              }
            } else {
              auto err_desc = DescribeNSObject(err.handle);
              DumpShaderText(metallib_error_path, err_desc.c_str());
              DumpShaderBlob(dxbc_path, bytecode, size);
              PSTRACE("  fallback WMT newLibrary FAILED: %s", err_desc.c_str());
              return RecordCompileFailure(
                  "shader/dxil_cached_metallib_load",
                  str::format(func_name,
                              " cached metallib load failed: ",
                              err_desc,
                              "; metallib ", metallib_path, "; error ",
                              metallib_error_path, "; dxbc ", dxbc_path));
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
      char dxbc_path[1024];
      FormatShaderCachePath(dxbc_path, sizeof(dxbc_path),
                            "%016zx.sm50_failed.dxbc", hash);
      DumpShaderBlob(dxbc_path, bytecode, size);
      PSTRACE("SM50Init FAILED for %s: %s (no DXIL chunk, dumped %s)",
              func_name, err_buf, dxbc_path);
    }
    return RecordCompileFailure(has_dxil ? "shader/dxil_metallib_cache" : "shader/sm50_init",
                                str::format(func_name, " SM50Initialize failed: ", err_buf));
  }

  SM50_SHADER_COMMON_DATA common = {};
  common.next = nullptr;
  common.type = SM50_SHADER_COMMON;
  common.metal_version = SM50_SHADER_METAL_310;
  common.flags = {};

  if (type == ShaderType::Compute) {
    uint32_t tgx = reflection.ThreadgroupSize[0] ? reflection.ThreadgroupSize[0] : 1;
    uint32_t tgy = reflection.ThreadgroupSize[1] ? reflection.ThreadgroupSize[1] : 1;
    uint32_t tgz = reflection.ThreadgroupSize[2] ? reflection.ThreadgroupSize[2] : 1;
    m_threadgroup_size.width = tgx;
    m_threadgroup_size.height = tgy;
    m_threadgroup_size.depth = tgz;
    PSTRACE("CompileShader: %s SM50 threadgroup_size=%ux%ux%u",
            func_name, tgx, tgy, tgz);
  }

  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout = {};
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *compile_args =
      (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&common;
  if (type == ShaderType::Vertex) {
    ia_layout.next = &common;
    ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
    ia_layout.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_NONE;
    ia_layout.slot_mask = ia_slot_mask;
    ia_layout.num_elements = (uint32_t)ia_elements.size();
    ia_layout.elements = ia_elements.data();
    compile_args = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&ia_layout;
    PSTRACE("CompileShader: %s IA args elements=%u slot_mask=0x%x",
            func_name, ia_layout.num_elements, ia_layout.slot_mask);
  }

  sm50_bitcode_t compile_result = nullptr;
  if (SM50Compile(shader, compile_args,
                  func_name, &compile_result, &sm50_err)) {
    char err_buf[256] = {};
    SM50GetErrorMessage(sm50_err, err_buf, sizeof(err_buf));
    char dxbc_path[1024];
    FormatShaderCachePath(dxbc_path, sizeof(dxbc_path),
                          "%016zx.sm50_compile_failed.dxbc", hash);
    DumpShaderBlob(dxbc_path, bytecode, size);
    char diag_path[1024];
    FormatShaderCachePath(diag_path, sizeof(diag_path),
                          "%016zx.sm50_compile_failed.txt", hash);
    if (FILE *diag = fopen(diag_path, "w")) {
      fprintf(diag, "function=%s\n", func_name);
      fprintf(diag, "hash=%016zx\n", hash);
      fprintf(diag, "shader_type=%u\n", static_cast<unsigned>(type));
      fprintf(diag, "bytecode_size=%zu\n", static_cast<size_t>(size));
      fprintf(diag, "sm50_error=%s\n", err_buf[0] ? err_buf : "<empty>");
      fprintf(diag, "ia_elements=%zu\n", ia_elements.size());
      fprintf(diag, "ia_slot_mask=0x%x\n", ia_slot_mask);
      fprintf(diag, "dxbc=%s\n", dxbc_path);
      fprintf(diag, "note=If metal-shaderconverter can compile this DXBC offline, investigate the runtime SM50Compile/airconv bridge path.\n");
      fclose(diag);
    }
    PSTRACE("SM50Compile failed for %s: %s (dumped %s, diag %s)",
            func_name, err_buf, dxbc_path, diag_path);
    Logger::err(str::format("SM50Compile failed for ", func_name, ": ", err_buf));
    SM50FreeError(sm50_err);
    SM50Destroy(shader);
    return RecordCompileFailure("shader/sm50_compile",
                                str::format(func_name, " SM50Compile failed: ",
                                            err_buf, "; dumped ", dxbc_path));
  }

  SM50_COMPILED_BITCODE bitcode = {};
  SM50GetCompiledBitcode(compile_result, &bitcode);

  {
    char dump_path[1024];
    snprintf(dump_path, sizeof(dump_path), "%s/dxmt_sm50_%s.metallib",
             ShaderCacheDir().c_str(), func_name);
    FILE *df = fopen(dump_path, "wb");
    if (df) { fwrite(bitcode.Data, 1, bitcode.Size, df); fclose(df); }
    Logger::info(str::format("  SM50 dumped ", func_name, " to ", dump_path, " (", bitcode.Size, " bytes)"));
  }

  auto wmt_device = m_device->GetDXMTDevice().device();
  WMT::Reference<WMT::Error> err;
  auto lib_data = WMT::MakeDispatchData(bitcode.Data, bitcode.Size);
  auto library = wmt_device.newLibrary(lib_data, err);

  if (err.handle) {
    auto err_desc = DescribeNSObject(err.handle);
    char dxbc_path[1024];
    FormatShaderCachePath(dxbc_path, sizeof(dxbc_path),
                          "%016zx.sm50_metal_library_failed.dxbc", hash);
    DumpShaderBlob(dxbc_path, bytecode, size);
    PSTRACE("Failed to create Metal library for %s: %s (dumped %s)",
            func_name, err_desc.c_str(), dxbc_path);
    Logger::err(str::format("Failed to create Metal library for ", func_name));
    SM50DestroyBitcode(compile_result);
    SM50Destroy(shader);
    return RecordCompileFailure("shader/sm50_metal_library",
                                str::format(func_name, " SM50 Metal library creation failed: ",
                                            err_desc,
                                            "; dumped ", dxbc_path));
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
    char dxbc_path[1024];
    FormatShaderCachePath(dxbc_path, sizeof(dxbc_path),
                          "%016zx.sm50_function_lookup_failed.dxbc", hash);
    DumpShaderBlob(dxbc_path, bytecode, size);
    PSTRACE("Failed to get function %s from Metal library (dumped %s)",
            func_name, dxbc_path);
    Logger::err(str::format("Failed to get function ", func_name));
    return RecordCompileFailure("shader/sm50_function_lookup",
                                str::format(func_name,
                                            " SM50 function lookup failed; dumped ",
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
    std::vector<SM50_IA_INPUT_ELEMENT> &elements,
    uint32_t &slot_mask) {
  slot_mask = 0;
  elements.clear();
  m_ia_input_elements.clear();

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
  std::vector<D3D12IAInputLayoutElementMetadata> layout_metadata;
  std::vector<D3D12IAInputSignatureElementMetadata> signature_metadata;
  layout_metadata.reserve(m_input_layout.NumElements);
  signature_metadata.reserve(param_count);

  for (UINT i = 0; i < m_input_layout.NumElements; i++) {
    const auto &desc = m_input_layout.pInputElementDescs[i];
    if (desc.InputSlot >= kMetalD3D12VertexBufferSlotCount) {
      PSTRACE("BuildIAInputLayout skip[%u]: slot %u outside cap %u",
              i, desc.InputSlot, kMetalD3D12VertexBufferSlotCount);
    }

    MTL_DXGI_FORMAT_DESC metal_format = {};
    bool supported_format =
        SUCCEEDED(MTLQueryDXGIFormat(m_device->GetMTLDevice(), desc.Format,
                                     metal_format)) &&
        metal_format.AttributeFormat && metal_format.BytesPerTexel;
    if (!supported_format) {
      PSTRACE("BuildIAInputLayout skip[%u]: unsupported fmt=%u",
              i, (unsigned)desc.Format);
    }

    D3D12IAInputLayoutElementMetadata element = {};
    element.semantic_name = desc.SemanticName ? desc.SemanticName : "";
    element.semantic_index = desc.SemanticIndex;
    element.input_slot = desc.InputSlot;
    element.aligned_byte_offset = desc.AlignedByteOffset;
    element.dxgi_format = desc.Format;
    element.metal_format = metal_format.AttributeFormat;
    element.bytes_per_texel = metal_format.BytesPerTexel;
    element.input_slot_class =
        desc.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
            ? D3D12VertexInputSlotClass::PerInstance
            : D3D12VertexInputSlotClass::PerVertex;
    element.instance_step_rate =
        desc.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
            ? desc.InstanceDataStepRate
            : 1;
    element.supported_format = supported_format;
    layout_metadata.push_back(std::move(element));
  }

  for (uint32_t i = 0; i < param_count; i++) {
    D3D12IAInputSignatureElementMetadata input_sig = {};
    input_sig.semantic_name = params[i].SemanticName ? params[i].SemanticName : "";
    input_sig.semantic_index = params[i].SemanticIndex;
    input_sig.shader_register = params[i].Register;
    input_sig.system_value = params[i].SystemValue != D3D10_SB_NAME_UNDEFINED;
    signature_metadata.push_back(std::move(input_sig));
  }

  auto metadata = D3D12BuildIAInputLayoutMetadata(
      layout_metadata, signature_metadata, kMetalD3D12VertexBufferSlotCount,
      D3D12_APPEND_ALIGNED_ELEMENT);
  slot_mask = metadata.slot_mask;

  for (UINT i = 0; i < m_input_layout.NumElements; i++) {
    const auto &desc = m_input_layout.pInputElementDescs[i];
    bool consumed = std::any_of(
        metadata.elements.begin(), metadata.elements.end(),
        [&](const D3D12ResolvedIAInputElementMetadata &input) {
          return !input.system_value &&
                 input.semantic_index == desc.SemanticIndex &&
                 input.input_slot == desc.InputSlot && desc.SemanticName &&
                 D3D12SemanticNameEquals(input.semantic_name, desc.SemanticName);
        });
    if (!consumed && desc.InputSlot < kMetalD3D12VertexBufferSlotCount)
      PSTRACE("BuildIAInputLayout skip[%u]: semantic %s%u not consumed by VS",
              i, desc.SemanticName ? desc.SemanticName : "?",
              desc.SemanticIndex);
  }

  for (const auto &resolved : metadata.elements) {
    D3D12IAInputElementInfo info = {};
    info.semantic_name = resolved.semantic_name;
    info.semantic_index = resolved.semantic_index;
    info.shader_register = resolved.shader_register;
    info.input_slot = resolved.input_slot;
    info.table_index = resolved.table_index;
    info.table_indexing_mode = resolved.table_indexing_mode;
    info.aligned_byte_offset = resolved.aligned_byte_offset;
    info.dxgi_format = static_cast<DXGI_FORMAT>(resolved.dxgi_format);
    info.metal_format = static_cast<WMTAttributeFormat>(resolved.metal_format);
    info.input_slot_class =
        resolved.input_slot_class == D3D12VertexInputSlotClass::PerInstance
            ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
            : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    info.per_instance =
        resolved.input_slot_class == D3D12VertexInputSlotClass::PerInstance;
    info.instance_step_rate = resolved.instance_step_rate;
    info.system_value = resolved.system_value;
    m_ia_input_elements.push_back(std::move(info));

    if (resolved.system_value)
      continue;

    SM50_IA_INPUT_ELEMENT element = {};
    element.reg = resolved.shader_register;
    element.slot = resolved.input_slot;
    element.aligned_byte_offset = resolved.aligned_byte_offset;
    element.format = resolved.metal_format;
    element.step_function =
        resolved.input_slot_class == D3D12VertexInputSlotClass::PerInstance;
    element.step_rate =
        resolved.input_slot_class == D3D12VertexInputSlotClass::PerInstance
            ? resolved.instance_step_rate
            : 1;
    elements.push_back(element);

    PSTRACE("BuildIAInputLayout element[%zu]: semantic=%s%u reg=%u slot=%u offset=%u fmt=%u step=%u/%u",
            elements.size() - 1, resolved.semantic_name.c_str(),
            resolved.semantic_index, element.reg, element.slot,
            element.aligned_byte_offset, element.format,
            element.step_function, element.step_rate);
  }
}

bool MTLD3D12PipelineState::Compile() {
  PTRACE("Compile() called state=%u is_compute=%d",
         (unsigned)m_compile_state.load(), m_is_compute);
  std::unique_lock<std::mutex> lock(m_compile_mutex);
  if (m_compile_state.load() == CompileState::Compiled)
    return true;
  if (m_compile_state.load() == CompileState::Compiling) {
    auto wait_start = std::chrono::steady_clock::now();
    m_compile_cv.wait(lock, [this]() {
      CompileState state = m_compile_state.load();
      return state != CompileState::Compiling && state != CompileState::Pending;
    });
    auto wait_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now() - wait_start)
                       .count();
    uint64_t waits = ++g_compile_wait_count;
    uint64_t total_ns = g_compile_wait_ns.fetch_add(wait_ns) + wait_ns;
    dxmt::m12core::RecordCounter(M12CORE_COUNTER_COMPILE_WAIT_COUNT);
    dxmt::m12core::RecordCounter(M12CORE_COUNTER_COMPILE_WAIT_NS, wait_ns);
    Logger::info(str::format("PSO_PRESSURE compile_wait pso=0x", std::hex,
                             reinterpret_cast<uintptr_t>(this), std::dec,
                             " compute=", m_is_compute, " wait_ns=", wait_ns,
                             " waits=", waits, " total_wait_ns=", total_ns));
    return m_compile_state.load() == CompileState::Compiled;
  }
  m_compile_state.store(CompileState::Compiling);
  ClearCompileFailure();
  lock.unlock();

  auto wmt_device = m_device->GetDXMTDevice().device();
  WMT::Reference<WMT::Error> err;

  if (m_is_compute) {
    if (m_cs.empty()) {
      Logger::err("Compute PSO has no CS bytecode");
      return RecordCompileFailure("pso/compute_no_cs", "Compute PSO has no CS bytecode");
    }

    size_t cs_hash = ComputeShaderCacheHash(m_cs.data(), m_cs.size(),
                                            ShaderType::Compute, nullptr);
    WMT::Reference<WMT::Function> cs_func;
    if (!CompileShader(m_cs.data(), m_cs.size(), ShaderType::Compute,
                       "cs_main", cs_func, &m_cs_shader, &m_cs_reflection))
      return false;

    WMTComputePipelineInfo info = {};
    WMT::InitializeComputePipelineInfo(info);
    info.compute_function = cs_func.handle;

    size_t compute_pipeline_cache_key = cs_hash;
    std::vector<uint64_t> compute_key_fields;
    const uint64_t compute_root_key = RootSignaturePipelineKey(m_root_sig);
    if (compute_root_key)
      compute_key_fields.push_back(compute_root_key);
    if (!FinalizeM12CorePipelineCacheKeyFromFields(cs_hash, (uint64_t)wmt_device.handle,
                                                   M12CORE_PIPELINE_KIND_COMPUTE, 0,
                                                   compute_key_fields,
                                                   compute_pipeline_cache_key)) {
      PsoCacheHashCombine(compute_pipeline_cache_key, (size_t)wmt_device.handle);
      PsoCacheHashCombine(compute_pipeline_cache_key, (size_t)compute_root_key);
    }
    m_compute_pipeline_cache_key = (uint64_t)compute_pipeline_cache_key;
    bool compute_core_cache_available = false;
    bool compute_cache_hit = false;
    compute_core_cache_available = LookupM12CorePipelineCache(
        M12CORE_PIPELINE_KIND_COMPUTE, compute_pipeline_cache_key,
        m_compute_pso, compute_cache_hit);
    if (!compute_core_cache_available) {
      std::lock_guard<std::mutex> cache_lock(g_metal_pipeline_cache_mutex);
      auto cached = g_compute_pipeline_cache.find(compute_pipeline_cache_key);
      if (cached != g_compute_pipeline_cache.end()) {
        m_compute_pso = cached->second;
        compute_cache_hit = true;
      }
    }
    if (m_compute_pso.handle) {
      uint64_t hits = ++g_compute_pipeline_cache_hits;
      dxmt::m12core::RecordCounter(M12CORE_COUNTER_COMPUTE_PIPELINE_CACHE_HITS);
      Logger::info(str::format("PSO_PRESSURE compute_pipeline_cache_hit key=0x",
                               std::hex, compute_pipeline_cache_key, std::dec,
                               " native=", compute_core_cache_available ? 1 : 0,
                               " native_hit=", compute_cache_hit ? 1 : 0,
                               " hits=", hits));
    } else {
      uint64_t misses = ++g_compute_pipeline_cache_misses;
      dxmt::m12core::RecordCounter(M12CORE_COUNTER_COMPUTE_PIPELINE_CACHE_MISSES);
      Logger::info(str::format("PSO_PRESSURE compute_pipeline_cache_miss key=0x",
                               std::hex, compute_pipeline_cache_key, std::dec,
                               " native=", compute_core_cache_available ? 1 : 0,
                               " native_hit=", compute_cache_hit ? 1 : 0,
                               " misses=", misses));
    }

    std::string err_desc = "unknown";
    bool compute_core_creation_used = false;
    if (!m_compute_pso.handle) {
      for (uint32_t attempt = 0; attempt < 4; attempt++) {
        err = nullptr;
        uint64_t total = ++g_metal_compute_pipeline_creates;
        dxmt::m12core::RecordCounter(M12CORE_COUNTER_METAL_COMPUTE_PIPELINE_CREATES);
        Logger::info(str::format("PSO_PRESSURE metal_compute_create total=", total,
                                 " attempt=", attempt + 1,
                                 " cs=0x", std::hex, cs_hash, std::dec));
        bool core_create_cache_hit = false;
        if (CreateM12CorePipelineState(wmt_device.handle,
                                       M12CORE_PIPELINE_KIND_COMPUTE,
                                       compute_pipeline_cache_key,
                                       &info, sizeof(info),
                                       m_compute_pso, err,
                                       core_create_cache_hit)) {
          compute_core_creation_used = true;
          Logger::info(str::format("PSO_PRESSURE metal_compute_create_native key=0x",
                                   std::hex, compute_pipeline_cache_key,
                                   " cs=0x", cs_hash, std::dec,
                                   " cache_hit=", core_create_cache_hit ? 1 : 0));
          break;
        }
        m_compute_pso = wmt_device.newComputePipelineState(info, err);
        if (m_compute_pso.handle)
          break;

        err_desc = DescribeNSObject(err.handle);
        if (!IsTransientMetalCompilerError(err_desc) || attempt == 3)
          break;

        Logger::warn(str::format("Retrying compute PSO after transient Metal compiler error attempt=",
                                 attempt + 1, " detail=", err_desc));
        PSTRACE("Compute PSO transient Metal compiler failure attempt=%u detail=%s",
                attempt + 1, err_desc.c_str());
        Sleep(50 * (attempt + 1));
      }
      if (m_compute_pso.handle && !compute_core_creation_used) {
        if (!StoreM12CorePipelineCache(M12CORE_PIPELINE_KIND_COMPUTE,
                                       compute_pipeline_cache_key,
                                       m_compute_pso.handle)) {
          std::lock_guard<std::mutex> cache_lock(g_metal_pipeline_cache_mutex);
          g_compute_pipeline_cache[compute_pipeline_cache_key] = m_compute_pso;
        }
      }
    }
    if (!m_compute_pso.handle) {
      Logger::err(str::format("Failed to create compute PSO: ",
                              err_desc));
      if (m_cs_shader) {
        SM50Destroy(m_cs_shader);
        m_cs_shader = nullptr;
      }
      return RecordCompileFailure("pso/metal_compute_pso",
                                  str::format("Metal compute PSO creation failed: ",
                                              err_desc));
    }
    DumpComputePSOManifest(cs_hash, m_cs.size(), m_threadgroup_size.width,
                           m_threadgroup_size.height, m_threadgroup_size.depth,
                           (uintptr_t)cs_func.handle);

    bool cs_core_reflection = ReflectSM50WithM12Core(m_cs.data(), m_cs.size(),
                                                     m_cs_reflection, m_cs_cb_args,
                                                     m_cs_args);
    PTRACE("CS_ARGS_DEBUG: shader=%llu core=%u NumCB=%u NumArgs=%u CBufBindIdx=%u ArgBufBindIdx=%u ArgTableQwords=%u",
      (unsigned long long)(uintptr_t)m_cs_shader, cs_core_reflection ? 1u : 0u,
      m_cs_reflection.NumConstantBuffers, m_cs_reflection.NumArguments,
      m_cs_reflection.ConstanttBufferTableBindIndex,
      m_cs_reflection.ArgumentBufferBindIndex,
      m_cs_reflection.ArgumentTableQwords);
    if (!cs_core_reflection && m_cs_shader && (m_cs_reflection.NumArguments > 0 ||
                                               m_cs_reflection.NumConstantBuffers > 0)) {
      if (m_cs_reflection.NumConstantBuffers > 0)
        m_cs_cb_args.resize(m_cs_reflection.NumConstantBuffers);
      if (m_cs_reflection.NumArguments > 0)
        m_cs_args.resize(m_cs_reflection.NumArguments);
      SM50GetArgumentsInfo(m_cs_shader,
                           m_cs_cb_args.empty() ? nullptr : m_cs_cb_args.data(),
                           m_cs_args.empty() ? nullptr : m_cs_args.data());
    }
    for (size_t i = 0; i < m_cs_cb_args.size(); i++) {
      PTRACE("CS_ARGS_DEBUG: cb[%zu] type=%d slot=%u flags=0x%x offset=%u",
        i, (int)m_cs_cb_args[i].Type, m_cs_cb_args[i].SM50BindingSlot,
        m_cs_cb_args[i].Flags, m_cs_cb_args[i].StructurePtrOffset);
    }
    for (size_t i = 0; i < m_cs_args.size(); i++) {
      PTRACE("CS_ARGS_DEBUG: arg[%zu] type=%d slot=%u flags=0x%x offset=%u",
        i, (int)m_cs_args[i].Type, m_cs_args[i].SM50BindingSlot,
        m_cs_args[i].Flags, m_cs_args[i].StructurePtrOffset);
    }
    if (m_cs_shader) {
      SM50Destroy(m_cs_shader);
      m_cs_shader = nullptr;
    }

    m_compile_state.store(CompileState::Compiled);
    m_compile_cv.notify_all();
    Logger::info("Compute PSO compiled successfully");
    return true;
  }

  WMT::Reference<WMT::Function> vs_func, ps_func;
  size_t vs_hash = m_vs.empty()
                       ? 0
                       : ComputeShaderCacheHash(m_vs.data(), m_vs.size(),
                                                ShaderType::Vertex,
                                                &m_input_layout);
  size_t ps_hash = m_ps.empty()
                       ? 0
                       : ComputeShaderCacheHash(m_ps.data(), m_ps.size(),
                                                ShaderType::Pixel, nullptr);
  size_t gs_hash = m_gs.empty()
                       ? 0
                       : ComputeShaderCacheHash(m_gs.data(), m_gs.size(),
                                                ShaderType::Geometry, nullptr);

  const bool tessellation_fallback = !m_hs.empty() || !m_ds.empty();
  m_uses_tessellation_fallback = tessellation_fallback;
  if (tessellation_fallback) {
    Logger::warn(str::format(
        "D3D12 tessellation fallback: compiling VS/PS-only render PSO "
        "HS bytes=",
        m_hs.size(), " DS bytes=", m_ds.size(),
        " topology=", (unsigned)m_topology));
    PSTRACE("D3D12 tessellation fallback pso=%p hs=%zu ds=%zu topo=%u",
            (void *)this, m_hs.size(), m_ds.size(), (unsigned)m_topology);
  }

  if (!m_gs.empty()) {
    return RecordCompileFailure(
        "pso/unsupported_geometry_shader",
        str::format("Graphics PSO uses GS bytes=", m_gs.size(),
                    " but D3D12 geometry shaders are not implemented"));
  }

  if (m_has_stream_output) {
    return RecordCompileFailure(
        "pso/unsupported_stream_output",
        "Graphics PSO uses stream output, which is not implemented");
  }

  if (!m_vs.empty()) {
    if (!CompileShader(m_vs.data(), m_vs.size(), ShaderType::Vertex,
                       "vs_main", vs_func, &m_vs_shader, &m_vs_reflection))
      return false;
  }

  if (!m_ps.empty()) {
    if (!CompileShader(m_ps.data(), m_ps.size(), ShaderType::Pixel,
                       "ps_main", ps_func, &m_ps_shader, &m_ps_reflection))
      return false;
  }

  WMTRenderPipelineInfo info;
  WMT::InitializeRenderPipelineInfo(info);

  if (vs_func.handle)
    info.vertex_function = vs_func.handle;
  if (ps_func.handle)
    info.fragment_function = ps_func.handle;

  info.rasterization_enabled = true;
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

  if (m_blend_desc.RenderTarget[0].BlendEnable) {
    for (UINT i = 0; i < m_num_render_targets && i < 8; i++) {
      auto &rt = m_blend_desc.RenderTarget[i];
      info.colors[i].blending_enabled = rt.BlendEnable ? true : false;
      info.colors[i].write_mask = kColorWriteMaskMap[rt.RenderTargetWriteMask & 0xf];

      auto map_blend = [](D3D12_BLEND b) -> WMTBlendFactor {
        switch (b) {
        case D3D12_BLEND_ZERO: return WMTBlendFactorZero;
        case D3D12_BLEND_ONE: return WMTBlendFactorOne;
        case D3D12_BLEND_SRC_COLOR: return WMTBlendFactorSourceColor;
        case D3D12_BLEND_INV_SRC_COLOR: return WMTBlendFactorOneMinusSourceColor;
        case D3D12_BLEND_SRC_ALPHA: return WMTBlendFactorSourceAlpha;
        case D3D12_BLEND_INV_SRC_ALPHA: return WMTBlendFactorOneMinusSourceAlpha;
        case D3D12_BLEND_DEST_ALPHA: return WMTBlendFactorDestinationAlpha;
        case D3D12_BLEND_INV_DEST_ALPHA: return WMTBlendFactorOneMinusDestinationAlpha;
        case D3D12_BLEND_DEST_COLOR: return WMTBlendFactorDestinationColor;
        case D3D12_BLEND_INV_DEST_COLOR: return WMTBlendFactorOneMinusDestinationColor;
        case D3D12_BLEND_SRC_ALPHA_SAT: return WMTBlendFactorSourceAlphaSaturated;
        case D3D12_BLEND_BLEND_FACTOR: return WMTBlendFactorBlendColor;
        case D3D12_BLEND_INV_BLEND_FACTOR: return WMTBlendFactorOneMinusBlendColor;
        default: return WMTBlendFactorOne;
        }
      };

      auto map_op = [](D3D12_BLEND_OP op) -> WMTBlendOperation {
        switch (op) {
        case D3D12_BLEND_OP_ADD: return WMTBlendOperationAdd;
        case D3D12_BLEND_OP_SUBTRACT: return WMTBlendOperationSubtract;
        case D3D12_BLEND_OP_REV_SUBTRACT: return WMTBlendOperationReverseSubtract;
        case D3D12_BLEND_OP_MIN: return WMTBlendOperationMin;
        case D3D12_BLEND_OP_MAX: return WMTBlendOperationMax;
        default: return WMTBlendOperationAdd;
        }
      };

      info.colors[i].src_rgb_blend_factor = map_blend(rt.SrcBlend);
      info.colors[i].dst_rgb_blend_factor = map_blend(rt.DestBlend);
      info.colors[i].rgb_blend_operation = map_op(rt.BlendOp);
      info.colors[i].src_alpha_blend_factor = map_blend(rt.SrcBlendAlpha);
      info.colors[i].dst_alpha_blend_factor = map_blend(rt.DestBlendAlpha);
      info.colors[i].alpha_blend_operation = map_op(rt.BlendOpAlpha);
    }
  }

  switch (m_topology) {
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT: info.input_primitive_topology = WMTPrimitiveTopologyClassPoint; break;
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE: info.input_primitive_topology = WMTPrimitiveTopologyClassLine; break;
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE: info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle; break;
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
    info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle;
    break;
  default: info.input_primitive_topology = WMTPrimitiveTopologyClassUnspecified; break;
  }

  info.immutable_vertex_buffers = (1 << 16) | (1 << 29) | (1 << 30);
  info.immutable_fragment_buffers = (1 << 29) | (1 << 30);

  WMTVertexDescriptor vtx_desc = {};
  WMTVertexDescriptor reflected_vtx_desc = {};
  if (m_input_layout.NumElements > 0 && m_input_layout.pInputElementDescs) {
    uint32_t append_offset[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    uint32_t slot_stride[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    uint32_t max_slot = 0;
    bool slot_per_vertex[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    uint32_t attribute_count = 0;
    uint32_t next_attribute = 0;
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
      PSTRACE("D3D12 PSO input-layout: shader input signature unavailable; using layout order");
    }

    PSTRACE("D3D12 PSO input-layout: elements=%u metal_attr_cap=%u metal_slot_cap=%u",
            m_input_layout.NumElements, WMT_MAX_VERTEX_ATTRIBUTES,
            kMetalD3D12VertexBufferSlotCount);

    for (UINT i = 0; i < m_input_layout.NumElements; i++) {
      auto &el = m_input_layout.pInputElementDescs[i];

      MTL_DXGI_FORMAT_DESC metal_format = {};
      if (FAILED(MTLQueryDXGIFormat(m_device->GetMTLDevice(), el.Format, metal_format)) ||
          !metal_format.AttributeFormat || !metal_format.BytesPerTexel) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: unsupported fmt=%u semantic=%s%u",
                i, (unsigned)el.Format, el.SemanticName ? el.SemanticName : "?",
                el.SemanticIndex);
        continue;
      }

      if (el.InputSlot >= kMetalD3D12VertexBufferSlotCount) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: input slot %u is outside Metal-backed slot cap %u",
                i, el.InputSlot, kMetalD3D12VertexBufferSlotCount);
        continue;
      }

      if (attribute_count >= WMT_MAX_VERTEX_ATTRIBUTES) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: attribute cap %u reached",
                i, WMT_MAX_VERTEX_ATTRIBUTES);
        continue;
      }

      uint32_t attr_index = next_attribute;
      if (has_input_signature && input_sig_params) {
        auto *sig = std::find_if(
            input_sig_params, input_sig_params + input_sig_count,
            [&](const microsoft::D3D11_SIGNATURE_PARAMETER &input_sig) {
              return input_sig.SystemValue == microsoft::D3D10_SB_NAME_UNDEFINED &&
                     el.SemanticName && input_sig.SemanticName &&
                     el.SemanticIndex == input_sig.SemanticIndex &&
                     strcasecmp(el.SemanticName, input_sig.SemanticName) == 0;
            });
        if (sig != input_sig_params + input_sig_count) {
          attr_index = sig->Register;
        } else {
          PSTRACE("D3D12 PSO input-layout desc[%u]: semantic %s%u not found in input signature; using attr order %u",
                  i, el.SemanticName ? el.SemanticName : "?",
                  el.SemanticIndex, attr_index);
        }
      }

      if (attr_index >= WMT_MAX_VERTEX_ATTRIBUTES) {
        PSTRACE("D3D12 PSO input-layout skip[%u]: mapped attribute %u outside cap %u",
                i, attr_index, WMT_MAX_VERTEX_ATTRIBUTES);
        continue;
      }
      next_attribute = std::max(next_attribute, attr_index + 1);

      uint32_t aligned_offset =
          el.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
              ? D3D12ResolveAlignedInputOffset(append_offset[el.InputSlot],
                                               metal_format.BytesPerTexel)
              : el.AlignedByteOffset;
      uint32_t end = aligned_offset + metal_format.BytesPerTexel;
      append_offset[el.InputSlot] = end;
      if (end > slot_stride[el.InputSlot])
        slot_stride[el.InputSlot] = end;
      if (el.InputSlot >= max_slot)
        max_slot = el.InputSlot + 1;
      slot_per_vertex[el.InputSlot] = (el.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);

      auto &attr = vtx_desc.attributes[attr_index];
      attr.format = metal_format.AttributeFormat;
      attr.offset = aligned_offset;
      attr.buffer_index = el.InputSlot;
      attribute_count = std::max(attribute_count, attr_index + 1);

      PSTRACE("D3D12 PSO input-layout attr[%u]<-desc[%u]: semantic=%s%u fmt=%u mtl_fmt=%u slot=%u offset=%u stride_end=%u class=%u step=%u",
              attr_index, i, el.SemanticName ? el.SemanticName : "?",
              el.SemanticIndex, (unsigned)el.Format,
              (unsigned)metal_format.AttributeFormat, el.InputSlot,
              aligned_offset, end, (unsigned)el.InputSlotClass,
              el.InstanceDataStepRate);
    }
    vtx_desc.attribute_count = attribute_count;
    vtx_desc.layout_count = max_slot;
    for (uint32_t s = 0; s < max_slot; s++) {
      vtx_desc.layouts[s].stride = slot_stride[s];
      vtx_desc.layouts[s].step_function = slot_per_vertex[s]
          ? WMTVertexStepFunctionPerVertex : WMTVertexStepFunctionPerInstance;
      vtx_desc.layouts[s].step_rate = 1;
      PSTRACE("D3D12 PSO input-layout slot[%u]: stride=%u step=%u",
              s, slot_stride[s], (unsigned)vtx_desc.layouts[s].step_function);
    }
    if (!m_vs_uses_stage_in) {
      if (EnvFlagEnabled("DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR") &&
          BuildVertexDescriptorFromMetalFunction(vs_func, reflected_vtx_desc)) {
        info.vertex_descriptor = &reflected_vtx_desc;
        if (EnvFlagEnabled("DXMT_D3D12_REFLECTED_DESCRIPTOR_UNSPECIFIED_TOPOLOGY")) {
          info.input_primitive_topology = WMTPrimitiveTopologyClassUnspecified;
          PSTRACE("D3D12 PSO experimental reflected vertex descriptor using unspecified topology");
        }
        PSTRACE("D3D12 PSO experimental reflected vertex descriptor attached attrs=%u stride=%u",
                reflected_vtx_desc.attribute_count,
                reflected_vtx_desc.layouts[0].stride);
      } else {
        PSTRACE("D3D12 PSO input-layout compiled for SM50 vertex pulling; Metal vertex descriptor disabled");
      }
    }
  }
  if (m_vs_uses_stage_in) {
    constexpr uint32_t kSyntheticStageInAttributes = 16;
    constexpr uint32_t kSyntheticStageInStride = 16 * kSyntheticStageInAttributes;
    for (uint32_t i = 0; i < kSyntheticStageInAttributes && i < WMT_MAX_VERTEX_ATTRIBUTES; i++) {
      auto &attr = vtx_desc.attributes[i];
      attr.format = WMTAttributeFormatFloat4;
      attr.offset = i * 16;
      attr.buffer_index = 0;
    }
    vtx_desc.attribute_count = std::min<uint32_t>(kSyntheticStageInAttributes,
                                                  WMT_MAX_VERTEX_ATTRIBUTES);
    vtx_desc.layout_count = 1;
    vtx_desc.layouts[0].stride = kSyntheticStageInStride;
    vtx_desc.layouts[0].step_function = WMTVertexStepFunctionPerVertex;
    vtx_desc.layouts[0].step_rate = 1;
    info.vertex_descriptor = &vtx_desc;
    PSTRACE("D3D12 PSO synthetic vertex descriptor attached attrs=%u stride=%u",
            vtx_desc.attribute_count, vtx_desc.layouts[0].stride);
  }

  PSTRACE("D3D12 PSO state this=%p rts=%u dsv_fmt=%u depth=%u stencil=%u blend0=%u write_mask0=0x%x cull=%u fill=%u front_ccw=%u depth_clip=%u",
          (void *)this, m_num_render_targets, (unsigned)m_dsv_format,
          (unsigned)m_depth_stencil_desc.DepthEnable,
          (unsigned)m_depth_stencil_desc.StencilEnable,
          (unsigned)m_blend_desc.RenderTarget[0].BlendEnable,
          (unsigned)m_blend_desc.RenderTarget[0].RenderTargetWriteMask,
          (unsigned)m_rasterizer_desc.CullMode,
          (unsigned)m_rasterizer_desc.FillMode,
          (unsigned)m_rasterizer_desc.FrontCounterClockwise,
          (unsigned)m_rasterizer_desc.DepthClipEnable);

  size_t pso_manifest_hash = ComputeRenderPSOManifestHash(
      vs_hash, ps_hash, gs_hash, m_num_render_targets, m_rtv_formats,
      m_dsv_format, m_sample_count ? m_sample_count : 1,
      m_input_layout.NumElements, m_ia_slot_mask, m_vs_uses_stage_in);
  const bool reflected_descriptor_enabled =
      EnvFlagEnabled("DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR") &&
      info.vertex_descriptor == &reflected_vtx_desc;

  const bool reflected_unspecified_topology =
      reflected_descriptor_enabled &&
      EnvFlagEnabled("DXMT_D3D12_REFLECTED_DESCRIPTOR_UNSPECIFIED_TOPOLOGY");
  auto render_key_fields = BuildRenderMetalPipelineKeyFields(
      m_topology, m_blend_desc, m_rasterizer_desc, m_depth_stencil_desc,
      info.vertex_descriptor, reflected_descriptor_enabled,
      reflected_unspecified_topology);
  const uint64_t render_root_key = RootSignaturePipelineKey(m_root_sig);
  if (render_root_key)
    render_key_fields.push_back(render_root_key);
  size_t render_pipeline_cache_key = pso_manifest_hash;
  if (!FinalizeM12CorePipelineCacheKeyFromFields(pso_manifest_hash, (uint64_t)wmt_device.handle,
                                                 M12CORE_PIPELINE_KIND_RENDER, 0,
                                                 render_key_fields,
                                                 render_pipeline_cache_key)) {
    render_pipeline_cache_key = ComputeRenderMetalPipelineCacheHash(
        pso_manifest_hash, m_topology, m_blend_desc, m_rasterizer_desc,
        m_depth_stencil_desc, info.vertex_descriptor, reflected_descriptor_enabled,
        reflected_unspecified_topology);
    PsoCacheHashCombine(render_pipeline_cache_key, (size_t)wmt_device.handle);
    PsoCacheHashCombine(render_pipeline_cache_key, (size_t)render_root_key);
  }
  m_render_pipeline_cache_key = (uint64_t)render_pipeline_cache_key;
  bool render_core_cache_available = false;
  bool render_cache_hit = false;
  render_core_cache_available = LookupM12CorePipelineCache(
      M12CORE_PIPELINE_KIND_RENDER, render_pipeline_cache_key,
      m_render_pso, render_cache_hit);
  if (!render_core_cache_available) {
    std::lock_guard<std::mutex> cache_lock(g_metal_pipeline_cache_mutex);
    auto cached = g_render_pipeline_cache.find(render_pipeline_cache_key);
    if (cached != g_render_pipeline_cache.end()) {
      m_render_pso = cached->second;
      render_cache_hit = true;
    }
  }
  if (m_render_pso.handle) {
    uint64_t hits = ++g_render_pipeline_cache_hits;
    dxmt::m12core::RecordCounter(M12CORE_COUNTER_RENDER_PIPELINE_CACHE_HITS);
    Logger::info(str::format("PSO_PRESSURE render_pipeline_cache_hit key=0x",
                             std::hex, render_pipeline_cache_key,
                             " pso=0x", pso_manifest_hash, std::dec,
                             " native=", render_core_cache_available ? 1 : 0,
                             " native_hit=", render_cache_hit ? 1 : 0,
                             " hits=", hits));
  } else {
    uint64_t misses = ++g_render_pipeline_cache_misses;
    dxmt::m12core::RecordCounter(M12CORE_COUNTER_RENDER_PIPELINE_CACHE_MISSES);
    Logger::info(str::format("PSO_PRESSURE render_pipeline_cache_miss key=0x",
                             std::hex, render_pipeline_cache_key,
                             " pso=0x", pso_manifest_hash, std::dec,
                             " native=", render_core_cache_available ? 1 : 0,
                             " native_hit=", render_cache_hit ? 1 : 0,
                             " misses=", misses));
  }

  std::string render_err_desc = "unknown";
  bool render_core_creation_used = false;
  if (!m_render_pso.handle) {
    for (uint32_t attempt = 0; attempt < 4; attempt++) {
      err = nullptr;
      uint64_t total = ++g_metal_render_pipeline_creates;
      dxmt::m12core::RecordCounter(M12CORE_COUNTER_METAL_RENDER_PIPELINE_CREATES);
      Logger::info(str::format("PSO_PRESSURE metal_render_create total=", total,
                               " attempt=", attempt + 1,
                               " pso=0x", std::hex, pso_manifest_hash,
                               " vs=0x", vs_hash, " ps=0x", ps_hash, std::dec));
      bool core_create_cache_hit = false;
      if (CreateM12CorePipelineState(wmt_device.handle,
                                     M12CORE_PIPELINE_KIND_RENDER,
                                     render_pipeline_cache_key,
                                     &info, sizeof(info),
                                     m_render_pso, err,
                                     core_create_cache_hit)) {
        render_core_creation_used = true;
        Logger::info(str::format("PSO_PRESSURE metal_render_create_native key=0x",
                                 std::hex, render_pipeline_cache_key,
                                 " pso=0x", pso_manifest_hash,
                                 " vs=0x", vs_hash, " ps=0x", ps_hash, std::dec,
                                 " cache_hit=", core_create_cache_hit ? 1 : 0));
        break;
      }
      m_render_pso = wmt_device.newRenderPipelineState(info, err);
      if (m_render_pso.handle)
        break;

      render_err_desc = DescribeNSObject(err.handle);
      if (!IsTransientMetalCompilerError(render_err_desc) || attempt == 3)
        break;

      Logger::warn(str::format("Retrying render PSO after transient Metal compiler error attempt=",
                               attempt + 1, " detail=", render_err_desc));
      PSTRACE("Render PSO transient Metal compiler failure attempt=%u detail=%s",
              attempt + 1, render_err_desc.c_str());
      Sleep(50 * (attempt + 1));
    }
    if (m_render_pso.handle && !render_core_creation_used) {
      if (!StoreM12CorePipelineCache(M12CORE_PIPELINE_KIND_RENDER,
                                     render_pipeline_cache_key,
                                     m_render_pso.handle)) {
        std::lock_guard<std::mutex> cache_lock(g_metal_pipeline_cache_mutex);
        g_render_pipeline_cache[render_pipeline_cache_key] = m_render_pso;
      }
    }
  }
  if (!m_render_pso.handle) {
    if (EnvFlagEnabled("DXMT_D3D12_LOG_RENDER_PSO_FAILURE_KEYS")) {
      Logger::err(str::format(
          "Render PSO failure key pso=", std::hex, pso_manifest_hash,
          " vs=", vs_hash, " ps=", ps_hash, " gs=", gs_hash,
          " input_elements=", std::dec, m_input_layout.NumElements,
          " ia_slot_mask=0x", std::hex, m_ia_slot_mask,
          " uses_stage_in=", m_vs_uses_stage_in ? 1 : 0,
          " reflected_descriptor=", reflected_descriptor_enabled ? 1 : 0,
          " reflected_unspecified_topology=",
          (reflected_descriptor_enabled &&
           EnvFlagEnabled("DXMT_D3D12_REFLECTED_DESCRIPTOR_UNSPECIFIED_TOPOLOGY")) ? 1 : 0,
          " rts=", std::dec, m_num_render_targets,
          " rtv0=", (unsigned)m_rtv_formats[0],
          " rtv1=", (unsigned)m_rtv_formats[1],
          " rtv2=", (unsigned)m_rtv_formats[2],
          " rtv3=", (unsigned)m_rtv_formats[3],
          " rtv4=", (unsigned)m_rtv_formats[4],
          " rtv5=", (unsigned)m_rtv_formats[5],
          " rtv6=", (unsigned)m_rtv_formats[6],
          " rtv7=", (unsigned)m_rtv_formats[7],
          " dsv=", (unsigned)m_dsv_format,
          " samples=", (unsigned)(m_sample_count ? m_sample_count : 1),
          " topology=", (unsigned)m_topology,
          " error=", render_err_desc));
      if (reflected_descriptor_enabled) {
        Logger::err(str::format(
            "Render PSO reflected descriptor key pso=", std::hex, pso_manifest_hash,
            " attrs=", std::dec, reflected_vtx_desc.attribute_count,
            " layouts=", reflected_vtx_desc.layout_count,
            " stride0=", reflected_vtx_desc.layouts[0].stride));
        for (uint32_t i = 0; i < reflected_vtx_desc.attribute_count &&
                             i < WMT_MAX_VERTEX_ATTRIBUTES; i++) {
          const auto &attr = reflected_vtx_desc.attributes[i];
          if (attr.format == WMTAttributeFormatInvalid)
            continue;
          Logger::err(str::format(
              "Render PSO reflected descriptor attr pso=", std::hex, pso_manifest_hash,
              " attr=", std::dec, i,
              " fmt=", (unsigned)attr.format,
              " offset=", attr.offset,
              " buffer=", attr.buffer_index));
        }
      }
    }
    Logger::err(str::format("Failed to create render PSO: ", render_err_desc));
    return RecordCompileFailure("pso/metal_render_pso",
                                str::format("Metal render PSO creation failed: ",
                                            render_err_desc));
  }
  {
    DumpRenderPSOManifest(
        pso_manifest_hash, vs_hash, ps_hash, gs_hash, m_vs.size(), m_ps.size(),
        m_gs.size(), m_num_render_targets, m_rtv_formats, m_dsv_format,
        m_sample_count ? m_sample_count : 1, m_input_layout.NumElements,
        m_ia_slot_mask, m_ia_input_elements,
        m_vs_uses_stage_in, m_uses_geometry_mesh_pipeline,
        info.rasterization_enabled, (uintptr_t)vs_func.handle,
        (uintptr_t)ps_func.handle);
  }

  struct WMTDepthStencilInfo ds_info = {};
  ds_info.depth_compare_function = WMTCompareFunctionAlways;
  ds_info.depth_write_enabled = false;
  ds_info.front_stencil.enabled = false;
  ds_info.back_stencil.enabled = false;
  if (m_depth_stencil_desc.DepthEnable &&
      m_depth_stencil_desc.DepthFunc >= D3D12_COMPARISON_FUNC_LESS &&
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
        kStencilOperationMap[m_depth_stencil_desc.FrontFace.StencilDepthFailOp];
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
        kStencilOperationMap[m_depth_stencil_desc.BackFace.StencilDepthFailOp];
    ds_info.back_stencil.stencil_compare_function =
        kCompareFunctionMap[m_depth_stencil_desc.BackFace.StencilFunc];
    ds_info.back_stencil.write_mask = m_depth_stencil_desc.StencilWriteMask;
    ds_info.back_stencil.read_mask = m_depth_stencil_desc.StencilReadMask;
  }
  m_depth_stencil_state = wmt_device.newDepthStencilState(ds_info);

  {
    bool vs_core_reflection = ReflectSM50WithM12Core(m_vs.data(), m_vs.size(),
                                                     m_vs_reflection, m_vs_cb_args,
                                                     m_vs_args);
    PTRACE("VS_ARGS_DEBUG: shader=%llu core=%u NumCB=%u NumArgs=%u CBufBindIdx=%u ArgBufBindIdx=%u ArgTableQwords=%u",
      (unsigned long long)(uintptr_t)m_vs_shader, vs_core_reflection ? 1u : 0u,
      m_vs_reflection.NumConstantBuffers, m_vs_reflection.NumArguments,
      m_vs_reflection.ConstanttBufferTableBindIndex,
      m_vs_reflection.ArgumentBufferBindIndex,
      m_vs_reflection.ArgumentTableQwords);
    if (!vs_core_reflection && m_vs_shader && (m_vs_reflection.NumArguments > 0 ||
                                               m_vs_reflection.NumConstantBuffers > 0)) {
      if (m_vs_reflection.NumConstantBuffers > 0)
        m_vs_cb_args.resize(m_vs_reflection.NumConstantBuffers);
      if (m_vs_reflection.NumArguments > 0)
        m_vs_args.resize(m_vs_reflection.NumArguments);
      SM50GetArgumentsInfo(m_vs_shader,
                           m_vs_cb_args.empty() ? nullptr : m_vs_cb_args.data(),
                           m_vs_args.empty() ? nullptr : m_vs_args.data());
    }
    for (size_t i = 0; i < m_vs_cb_args.size(); i++) {
      PTRACE("VS_ARGS_DEBUG: cb[%zu] type=%d slot=%u flags=0x%x offset=%u",
        i, (int)m_vs_cb_args[i].Type, m_vs_cb_args[i].SM50BindingSlot,
        m_vs_cb_args[i].Flags, m_vs_cb_args[i].StructurePtrOffset);
    }
    for (size_t i = 0; i < m_vs_args.size(); i++) {
      PTRACE("VS_ARGS_DEBUG: arg[%zu] type=%d slot=%u flags=0x%x offset=%u",
        i, (int)m_vs_args[i].Type, m_vs_args[i].SM50BindingSlot,
        m_vs_args[i].Flags, m_vs_args[i].StructurePtrOffset);
    }
    if (m_vs_shader) {
      SM50Destroy(m_vs_shader);
      m_vs_shader = nullptr;
    }
  }

  {
    bool ps_core_reflection = ReflectSM50WithM12Core(m_ps.data(), m_ps.size(),
                                                     m_ps_reflection, m_ps_cb_args,
                                                     m_ps_args);
    PTRACE("PS_ARGS_DEBUG: shader=%llu core=%u NumCB=%u NumArgs=%u CBufBindIdx=%u ArgBufBindIdx=%u ArgTableQwords=%u",
      (unsigned long long)(uintptr_t)m_ps_shader, ps_core_reflection ? 1u : 0u,
      m_ps_reflection.NumConstantBuffers, m_ps_reflection.NumArguments,
      m_ps_reflection.ConstanttBufferTableBindIndex,
      m_ps_reflection.ArgumentBufferBindIndex,
      m_ps_reflection.ArgumentTableQwords);
    if (!ps_core_reflection && m_ps_shader && (m_ps_reflection.NumArguments > 0 ||
                                               m_ps_reflection.NumConstantBuffers > 0)) {
      if (m_ps_reflection.NumConstantBuffers > 0)
        m_ps_cb_args.resize(m_ps_reflection.NumConstantBuffers);
      if (m_ps_reflection.NumArguments > 0)
        m_ps_args.resize(m_ps_reflection.NumArguments);
      SM50GetArgumentsInfo(m_ps_shader,
                           m_ps_cb_args.empty() ? nullptr : m_ps_cb_args.data(),
                           m_ps_args.empty() ? nullptr : m_ps_args.data());
    }
    for (size_t i = 0; i < m_ps_cb_args.size(); i++) {
      PTRACE("PS_ARGS_DEBUG: cb[%zu] type=%d slot=%u flags=0x%x offset=%u",
        i, (int)m_ps_cb_args[i].Type, m_ps_cb_args[i].SM50BindingSlot,
        m_ps_cb_args[i].Flags, m_ps_cb_args[i].StructurePtrOffset);
    }
    for (size_t i = 0; i < m_ps_args.size(); i++) {
      PTRACE("PS_ARGS_DEBUG: arg[%zu] type=%d slot=%u flags=0x%x offset=%u",
        i, (int)m_ps_args[i].Type, m_ps_args[i].SM50BindingSlot,
        m_ps_args[i].Flags, m_ps_args[i].StructurePtrOffset);
    }
    if (m_ps_shader) {
      SM50Destroy(m_ps_shader);
      m_ps_shader = nullptr;
    }
  }

  m_compile_state.store(CompileState::Compiled);
  m_compile_cv.notify_all();
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
  m_has_stream_output = desc.StreamOutput.NumEntries > 0 ||
                        desc.StreamOutput.NumStrides > 0 ||
                        desc.StreamOutput.pSODeclaration ||
                        desc.StreamOutput.pBufferStrides;
  m_vs_uses_stage_in = false;
  m_ia_slot_mask = 0;
  m_ia_input_elements.clear();
  m_input_elements.clear();
  m_input_semantic_names.clear();
  m_input_layout = {};
  if (desc.InputLayout.NumElements > 0 && desc.InputLayout.pInputElementDescs) {
    m_input_semantic_names.reserve(desc.InputLayout.NumElements);
    m_input_elements.reserve(desc.InputLayout.NumElements);
    for (UINT i = 0; i < desc.InputLayout.NumElements; i++) {
      auto element = desc.InputLayout.pInputElementDescs[i];
      m_input_semantic_names.emplace_back(element.SemanticName ? element.SemanticName : "");
      element.SemanticName = m_input_semantic_names.back().c_str();
      m_input_elements.push_back(element);
    }
    m_input_layout.NumElements = (UINT)m_input_elements.size();
    m_input_layout.pInputElementDescs = m_input_elements.data();
  }
  m_strip_cut_value = desc.IBStripCutValue;
  m_topology = desc.PrimitiveTopologyType;
  m_num_render_targets = desc.NumRenderTargets;
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
  m_ia_slot_mask = 0;
  m_ia_input_elements.clear();
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

ULONG STDMETHODCALLTYPE
MTLD3D12PipelineState::AddRef() { return ++m_refCount; }

ULONG STDMETHODCALLTYPE MTLD3D12PipelineState::Release() {
  uint32_t rc = --m_refCount;
  if (!rc)
    delete this;
  return rc;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::GetPrivateData(REFGUID guid, UINT *data_size,
                                      void *data) {
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::SetPrivateData(REFGUID guid, UINT data_size,
                                      const void *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::SetPrivateDataInterface(REFGUID guid,
                                               const IUnknown *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12PipelineState::SetName(LPCWSTR name) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::GetDevice(REFIID riid, void **device) {
  return m_device->QueryInterface(riid, device);
}

HRESULT STDMETHODCALLTYPE
MTLD3D12PipelineState::GetCachedBlob(ID3DBlob **blob) {
  return E_NOTIMPL;
}

} // namespace dxmt
