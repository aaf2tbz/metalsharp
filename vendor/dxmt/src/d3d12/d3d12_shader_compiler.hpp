#pragma once

#include "Metal.hpp"
#include "dxil/dxil_to_msl.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dxmt {

enum class D3D12ShaderCompilerStage : uint8_t {
  Vertex,
  Pixel,
  Geometry,
  Hull,
  Domain,
  Compute,
  Unknown,
};

struct D3D12ShaderCompilerPaths {
  std::string dxbc;
  std::string module_summary;
  std::string dxil_report;
  std::string msl;
  std::string msl_error;
  std::string metallib;
  std::string reflection;
  std::string stage_input_layout;
};

struct D3D12ShaderCompileRequest {
  WMT::Device metal_device;
  D3D12ShaderCompilerStage stage = D3D12ShaderCompilerStage::Unknown;
  const void *dxil_container = nullptr;
  size_t dxil_container_size = 0;
  size_t original_bytecode_hash = 0;
  size_t original_bytecode_size = 0;
  std::string requested_entry_point;
  std::string cache_key;
  std::string root_signature_hash;
  std::string converter_options_hash;
  std::string metal_device_family;
  bool allow_debug_msl_backend = false;
  bool prefer_debug_msl_backend = false;
  D3D12ShaderCompilerPaths paths;
};

struct D3D12ShaderCompileOutput {
  bool success = false;
  bool used_debug_msl_backend = false;
  bool from_persistent_cache = false;
  D3D12ShaderCompilerStage stage = D3D12ShaderCompilerStage::Unknown;
  size_t original_bytecode_hash = 0;
  std::string backend_name;
  std::string entry_point;
  std::string diagnostics;
  std::vector<uint8_t> metallib_bytes;
  std::vector<uint8_t> reflection_resources;
  std::vector<uint8_t> stage_input_layout;
  uint32_t threadgroup_size[3] = {1, 1, 1};
  WMT::Reference<WMT::Function> function;
};

class D3D12ShaderCompilerBackend {
public:
  virtual ~D3D12ShaderCompilerBackend() = default;
  virtual const char *Name() const = 0;
  virtual D3D12ShaderCompileOutput
  Compile(const D3D12ShaderCompileRequest &request) = 0;
};

class MetalIRCompilerBackend final : public D3D12ShaderCompilerBackend {
public:
  const char *Name() const override { return "MetalIRCompilerBackend"; }
  D3D12ShaderCompileOutput
  Compile(const D3D12ShaderCompileRequest &request) override;
};

class DebugMSLEmitterBackend final : public D3D12ShaderCompilerBackend {
public:
  const char *Name() const override { return "DebugMSLEmitterBackend"; }
  D3D12ShaderCompileOutput
  Compile(const D3D12ShaderCompileRequest &request) override;
};

class D3D12ShaderCompiler final {
public:
  D3D12ShaderCompileOutput
  Compile(const D3D12ShaderCompileRequest &request);
};

const char *D3D12ShaderCompilerStageName(D3D12ShaderCompilerStage stage);

} // namespace dxmt
