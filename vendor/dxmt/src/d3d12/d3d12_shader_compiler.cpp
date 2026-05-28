#include "d3d12_shader_compiler.hpp"
#include "dxil/dxil_container.hpp"
#include "dxil/llvm_bitcode.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <cstdio>
#include <iomanip>

namespace dxmt {

namespace {

bool ReadBinaryFile(const std::string &path, std::vector<uint8_t> &out) {
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
  return read > 0;
}

void WriteTextFile(const std::string &path, const std::string &text) {
  if (path.empty())
    return;
  FILE *file = fopen(path.c_str(), "w");
  if (!file)
    return;
  fwrite(text.data(), 1, text.size(), file);
  fclose(file);
}

std::string DescribeError(WMT::Reference<WMT::Error> &error) {
  if (!error.handle)
    return {};
  auto desc = error.description().getUTF8String();
  return desc.empty() ? "unknown" : desc;
}

const char *FallbackEntryForStage(D3D12ShaderCompilerStage stage) {
  switch (stage) {
  case D3D12ShaderCompilerStage::Compute:
    return "cs_main";
  case D3D12ShaderCompilerStage::Vertex:
    return "vs_main";
  case D3D12ShaderCompilerStage::Pixel:
    return "ps_main";
  default:
    return "main";
  }
}

D3D12ShaderCompilerStage
StageFromDxilKind(dxmt::dxil::DxilShaderKind kind) {
  switch (kind) {
  case dxmt::dxil::DxilShaderKind::Vertex:
    return D3D12ShaderCompilerStage::Vertex;
  case dxmt::dxil::DxilShaderKind::Pixel:
    return D3D12ShaderCompilerStage::Pixel;
  case dxmt::dxil::DxilShaderKind::Geometry:
    return D3D12ShaderCompilerStage::Geometry;
  case dxmt::dxil::DxilShaderKind::Hull:
    return D3D12ShaderCompilerStage::Hull;
  case dxmt::dxil::DxilShaderKind::Domain:
    return D3D12ShaderCompilerStage::Domain;
  case dxmt::dxil::DxilShaderKind::Compute:
    return D3D12ShaderCompilerStage::Compute;
  default:
    return D3D12ShaderCompilerStage::Unknown;
  }
}

WMT::Reference<WMT::Function>
FindFunction(WMT::Reference<WMT::Library> &library, const std::string &entry,
             D3D12ShaderCompilerStage stage, std::string &diagnostics) {
  WMT::Reference<WMT::Function> function;
  if (!entry.empty()) {
    function = library.newFunction(entry.c_str());
    if (function.handle)
      return function;
    diagnostics += str::format("function lookup failed for requested entry ",
                               entry, "\n");
  }

  const char *stage_entry = FallbackEntryForStage(stage);
  function = library.newFunction(stage_entry);
  if (function.handle)
    return function;

  function = library.newFunction("main");
  if (function.handle)
    return function;
  function = library.newFunction("cs_main");
  if (function.handle)
    return function;
  function = library.newFunction("vs_main");
  if (function.handle)
    return function;
  function = library.newFunction("ps_main");
  if (function.handle)
    return function;

  diagnostics += "function lookup failed for all fallback entry points\n";
  return function;
}

std::string BuildModuleSummary(const dxmt::dxil::LLVMModule &module,
                               const dxmt::dxil::DxilParsedShader &shader) {
  std::string text;
  text += str::format("kind=", D3D12ShaderCompilerStageName(
                           StageFromDxilKind(shader.kind)),
                      "(", (uint32_t)shader.kind, ")\n");
  text += str::format("shader_model=", shader.shader_model.major, ".",
                      shader.shader_model.minor, "\n");
  text += str::format("entry=", shader.entry_point, "\n");
  text += str::format("bitcode_size=", shader.bitcode.size, "\n");
  text += str::format("source_filename=", module.source_filename, "\n");
  text += str::format("target_triple=", module.target_triple, "\n");
  text += str::format("types=", module.types.size(), " constants=",
                      module.constants.size(), " functions=",
                      module.functions.size(), "\n");
  text += "\nfunctions:\n";
  for (const auto &fn : module.functions) {
    size_t inst_count = 0;
    for (const auto &block : fn.blocks)
      inst_count += block.instructions.size();
    text += str::format("  name=", fn.name, " declaration=", fn.is_declaration,
                        " value=", fn.value_id, " type=", fn.type_id,
                        " params=", fn.param_count, " inst_start=",
                        fn.instruction_start_value, " blocks=",
                        fn.blocks.size(), " instructions=", inst_count, "\n");
  }
  return text;
}

std::string BuildCompileReport(const D3D12ShaderCompileRequest &request,
                               const dxmt::dxil::LLVMModule &module,
                               const dxmt::dxil::DxilParsedShader &shader,
                               const dxmt::dxil::MSLShader &msl,
                               const char *backend_name) {
  std::string text;
  text += str::format("hash=0x", std::hex, request.original_bytecode_hash,
                      std::dec, "\n");
  text += str::format("backend=", backend_name ? backend_name : "unknown",
                      "\n");
  text += str::format("stage=", D3D12ShaderCompilerStageName(request.stage),
                      "\n");
  text += str::format("function=", request.requested_entry_point, "\n");
  text += str::format("entry=", shader.entry_point, "\n");
  text += str::format("cache_key=", request.cache_key, "\n");
  text += str::format("root_signature_hash=", request.root_signature_hash,
                      "\n");
  text += str::format("converter_options_hash=",
                      request.converter_options_hash, "\n");
  text += str::format("metal_device_family=", request.metal_device_family,
                      "\n");
  text += str::format("shader_model=", shader.shader_model.major, ".",
                      shader.shader_model.minor, "\n");
  text += str::format("bytecode_size=", request.original_bytecode_size, "\n");
  text += str::format("bitcode_size=", shader.bitcode.size, "\n");
  text += str::format("types=", module.types.size(), " constants=",
                      module.constants.size(), " functions=",
                      module.functions.size(), "\n");
  text += str::format("msl_size=", msl.source.size(), "\n");
  text += str::format("threadgroup_size=", msl.tg_size[0], ",",
                      msl.tg_size[1], ",", msl.tg_size[2], "\n");
  text += str::format("unsupported_intrinsics=", msl.unsupported_intrinsics,
                      "\n");
  text += str::format("unsupported_opcodes=", msl.unsupported_opcodes, "\n");
  text += str::format("dxbc=", request.paths.dxbc, "\n");
  text += str::format("module=", request.paths.module_summary, "\n");
  text += str::format("msl=", request.paths.msl, "\n");
  text += str::format("reflection=", request.paths.reflection, "\n");
  text += str::format("stage_input_layout=", request.paths.stage_input_layout,
                      "\n");
  text += "\ndiagnostics:\n";
  for (const auto &diagnostic : msl.diagnostics)
    text += str::format("  ", diagnostic, "\n");
  return text;
}

} // namespace

const char *D3D12ShaderCompilerStageName(D3D12ShaderCompilerStage stage) {
  switch (stage) {
  case D3D12ShaderCompilerStage::Vertex:
    return "vertex";
  case D3D12ShaderCompilerStage::Pixel:
    return "pixel";
  case D3D12ShaderCompilerStage::Geometry:
    return "geometry";
  case D3D12ShaderCompilerStage::Hull:
    return "hull";
  case D3D12ShaderCompilerStage::Domain:
    return "domain";
  case D3D12ShaderCompilerStage::Compute:
    return "compute";
  default:
    return "unknown";
  }
}

D3D12ShaderCompileOutput
MetalIRCompilerBackend::Compile(const D3D12ShaderCompileRequest &request) {
  D3D12ShaderCompileOutput output;
  output.backend_name = Name();
  output.stage = request.stage;
  output.original_bytecode_hash = request.original_bytecode_hash;
  output.entry_point = request.requested_entry_point;

  if (!ReadBinaryFile(request.paths.metallib, output.metallib_bytes)) {
    output.diagnostics =
        str::format("persistent metallib cache miss: ",
                    request.paths.metallib, "\n");
    return output;
  }

  WMT::Reference<WMT::Error> error;
  auto dispatch =
      WMT::MakeDispatchData(output.metallib_bytes.data(),
                            (uint64_t)output.metallib_bytes.size());
  auto metal_device = request.metal_device;
  auto library = metal_device.newLibrary(dispatch, error);
  if (error.handle || !library.handle) {
    output.diagnostics = str::format("Metal library load failed: ",
                                     DescribeError(error), "\n");
    return output;
  }

  output.function =
      FindFunction(library, request.requested_entry_point, request.stage,
                   output.diagnostics);
  if (!output.function.handle)
    return output;

  ReadBinaryFile(request.paths.reflection, output.reflection_resources);
  ReadBinaryFile(request.paths.stage_input_layout, output.stage_input_layout);
  output.from_persistent_cache = true;
  output.success = true;
  return output;
}

D3D12ShaderCompileOutput
DebugMSLEmitterBackend::Compile(const D3D12ShaderCompileRequest &request) {
  D3D12ShaderCompileOutput output;
  output.backend_name = Name();
  output.stage = request.stage;
  output.original_bytecode_hash = request.original_bytecode_hash;
  output.used_debug_msl_backend = true;

  auto container = dxmt::dxil::DXILContainer::parse(
      request.dxil_container, request.dxil_container_size);
  if (!container) {
    output.diagnostics = "DXIL container parse failed\n";
    return output;
  }

  const auto &shader = container->shader();
  auto module = dxmt::dxil::BitcodeReader::parse(shader.bitcode.data,
                                                 shader.bitcode.size);
  if (!module) {
    output.diagnostics = "DXIL bitcode parse failed\n";
    return output;
  }

  WriteTextFile(request.paths.module_summary,
                BuildModuleSummary(*module, shader));

  auto msl =
      dxmt::dxil::DXILToMSL::convert(*module, shader, request.msl_options);
  if (!msl) {
    output.diagnostics = "DXILToMSL conversion failed\n";
    return output;
  }

  WriteTextFile(request.paths.dxil_report,
                BuildCompileReport(request, *module, shader, *msl, Name()));
  if (msl->unsupported_intrinsics || msl->unsupported_opcodes) {
    output.diagnostics =
        str::format("DXILToMSL rejected unsupported semantics: intrinsics=",
                    msl->unsupported_intrinsics, " opcodes=",
                    msl->unsupported_opcodes, "\n");
    return output;
  }
  for (const auto &diagnostic : msl->diagnostics) {
    if (diagnostic.find("generated-source SSA fallback") != std::string::npos) {
      output.diagnostics =
          str::format("DXILToMSL rejected generated SSA fallback: ",
                      diagnostic, "\n");
      return output;
    }
  }

  WriteTextFile(request.paths.msl, msl->source);

  WMT::Reference<WMT::Error> compile_error;
  auto metal_device = request.metal_device;
  auto library = metal_device.newLibraryWithSource(
      msl->source.c_str(), msl->source.size(), compile_error);
  if (compile_error.handle || !library.handle) {
    output.diagnostics =
        str::format("MSL source compile failed: ",
                    DescribeError(compile_error), "\n");
    WriteTextFile(request.paths.msl_error, output.diagnostics);
    return output;
  }

  std::string entry = msl->entry_point.empty()
                          ? request.requested_entry_point
                          : msl->entry_point;
  output.function = FindFunction(library, entry, request.stage,
                                 output.diagnostics);
  if (!output.function.handle)
    return output;

  output.success = true;
  output.entry_point = entry;
  output.threadgroup_size[0] = msl->tg_size[0];
  output.threadgroup_size[1] = msl->tg_size[1];
  output.threadgroup_size[2] = msl->tg_size[2];
  for (const auto &diagnostic : msl->diagnostics)
    output.diagnostics += str::format(diagnostic, "\n");
  return output;
}

D3D12ShaderCompileOutput
D3D12ShaderCompiler::Compile(const D3D12ShaderCompileRequest &request) {
  MetalIRCompilerBackend metal_ir;
  DebugMSLEmitterBackend debug_msl;

  if (!request.prefer_debug_msl_backend) {
    auto primary = metal_ir.Compile(request);
    if (primary.success || !request.allow_debug_msl_backend)
      return primary;
  }

  if (!request.allow_debug_msl_backend) {
    D3D12ShaderCompileOutput output;
    output.backend_name = metal_ir.Name();
    output.stage = request.stage;
    output.original_bytecode_hash = request.original_bytecode_hash;
    output.diagnostics =
        "Debug MSL emitter backend is disabled for this compile\n";
    return output;
  }

  return debug_msl.Compile(request);
}

} // namespace dxmt
