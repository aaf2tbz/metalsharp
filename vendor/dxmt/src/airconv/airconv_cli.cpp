#include "airconv_context.hpp"
#include "airconv_public.h"
#include "metallib_writer.hpp"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <system_error>
#include <cstring>

#ifdef __WIN32
#include "d3dcompiler.h"
#endif
#include "dxbc_converter.hpp"
#include "dxil/dxil_container.hpp"
#include "dxil/llvm_bitcode.hpp"
#include "dxil/msl_lowering.hpp"

using namespace llvm;

static cl::opt<std::string>
  InputFilename(cl::Positional, cl::desc("<input dxbc>"), cl::init("-"));

static cl::opt<std::string> OutputFilename(
  "o", cl::desc("Override output filename"), cl::value_desc("filename")
);

static cl::opt<std::string>
  HullBeforeDomain("hull-before-domain", cl::desc("Compile domain shader with supplied hull shader"));

static cl::opt<std::string>
  VertexBeforeHull("vertex-before-hull", cl::desc("Compile hull shader with supplied vertex shader"));

static cl::opt<std::string>
  HullAfterVertex("hull-after-vertex", cl::desc("Compile vertex shader with supplied hull shader"));

static cl::opt<std::string>
  VertexBeforeGeometry("vertex-before-geometry", cl::desc("Compile geometry shader with supplied vertex shader"));

static cl::opt<std::string>
  GeometryAfterVertex("geometry-after-vertex", cl::desc("Compile vertex shader with supplied geometry shader"));

static cl::opt<bool>
  EmitLLVM("S", cl::init(false), cl::desc("Write output as LLVM assembly"));

static cl::opt<bool>
  EmitMetallib("A", cl::init(false), cl::desc("Write output as .metallib"));

static cl::opt<bool>
  EmitMSL("emit-msl", cl::init(false), cl::desc("Write DXIL input as generated MSL source"));

static cl::list<std::string> MSLVertexInputs(
  "msl-vertex-input",
  cl::desc("Vertex input for --emit-msl as reg,table,slot,offset,dxgi,metal,per_instance,step,raw_slot,system_value"),
  cl::ZeroOrMore
);

static cl::opt<bool> DisassembleDXBC(
  "disas-dxbc", cl::init(false), cl::desc("Disassemble dxbc shader")
);

static cl::opt<bool>
  OptLevelO0("O0", cl::desc("Optimization level 0. Similar to clang -O0. "));

static cl::opt<bool>
  OptLevelO1("O1", cl::desc("Optimization level 1. Similar to clang -O1. "));

static cl::opt<bool>
  OptLevelO2("O2", cl::desc("Optimization level 2. Similar to clang -O2. "));

static cl::opt<bool> PreserveBitcodeUseListOrder(
  "preserve-bc-uselistorder",
  cl::desc("Preserve use-list order when writing LLVM bitcode."),
  cl::init(false), cl::Hidden
);

static cl::opt<bool> PreserveAssemblyUseListOrder(
  "preserve-ll-uselistorder",
  cl::desc("Preserve use-list order when writing LLVM assembly."),
  cl::init(false), cl::Hidden
);

cl::list<std::string> f("f", cl::Prefix, cl::Hidden);

namespace {

struct LLVMDisDiagnosticHandler : public DiagnosticHandler {
  char *Prefix;
  LLVMDisDiagnosticHandler(char *PrefixPtr) : Prefix(PrefixPtr) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    raw_ostream &OS = errs();
    OS << Prefix << ": ";
    switch (DI.getSeverity()) {
    case DS_Error:
      WithColor::error(OS);
      break;
    case DS_Warning:
      WithColor::warning(OS);
      break;
    case DS_Remark:
      OS << "remark: ";
      break;
    case DS_Note:
      WithColor::note(OS);
      break;
    }

    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
    OS << '\n';

    if (DI.getSeverity() == DS_Error)
      exit(1);
    return true;
  }
};
} // namespace

namespace dxmt::dxbc {
llvm::Error convertDXBC(
  sm50_shader_t pShader, const char *name, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);
}

static ExitOnError ExitOnErr;

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  ExitOnErr.setBanner(std::string(argv[0]) + ": error: ");

  LLVMContext Context;
  Context.setDiagnosticHandler(
    std::make_unique<LLVMDisDiagnosticHandler>(argv[0])
  );
  Context.setOpaquePointers(false);
  cl::ParseCommandLineOptions(argc, argv, "DXBC to Metal AIR transpiler\n");

  // bool FastMath = true;
  // for (StringRef Flag : f) {
  //   if (Flag == "no-fast-math") {
  //     FastMath = false;
  //   }
  // }

  if (OutputFilename.empty()) { // Unspecified output, infer it.
    if (InputFilename == "-") {
      OutputFilename = "-";
    } else {
      StringRef IFN = InputFilename;
      OutputFilename = (IFN.endswith(".cso")    ? IFN.drop_back(4)
                        : IFN.endswith(".fxc")  ? IFN.drop_back(4)
                        : IFN.endswith(".obj")  ? IFN.drop_back(4)
                        : IFN.endswith(".o")    ? IFN.drop_back(2)
                        : IFN.endswith(".dxbc") ? IFN.drop_back(5)
                                                : IFN)
                         .str();
      OutputFilename += DisassembleDXBC ? ".txt"
                        : EmitMSL       ? ".msl"
                        : EmitMetallib  ? ".metallib"
                        : EmitLLVM      ? ".ll"
                                        : ".air";
    }
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
    MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/false);
  if (std::error_code EC = FileOrErr.getError()) {
    SMDiagnostic(
      InputFilename, SourceMgr::DK_Error,
      "Could not open input file: " + EC.message()
    )
      .print(argv[0], errs());
    return 1;
  }
  auto MemRef = FileOrErr->get()->getMemBufferRef();

  if (EmitMSL) {
    const uint8_t *dxilData = reinterpret_cast<const uint8_t *>(MemRef.getBufferStart());
    size_t dxilSize = MemRef.getBufferSize();
    if (dxilSize >= 32 && std::memcmp(dxilData, "DXBC", 4) == 0) {
      auto readU32 = [&](size_t offset, uint32_t &value) -> bool {
        if (offset > dxilSize || dxilSize - offset < sizeof(uint32_t))
          return false;
        std::memcpy(&value, dxilData + offset, sizeof(uint32_t));
        return true;
      };
      uint32_t chunkCount = 0;
      if (!readU32(28, chunkCount)) {
        errs() << "truncated DXBC header\n";
        return 1;
      }
      size_t offsetTableBytes = static_cast<size_t>(chunkCount) * sizeof(uint32_t);
      if (dxilSize < 32 || offsetTableBytes > dxilSize - 32) {
        errs() << "truncated DXBC chunk offset table\n";
        return 1;
      }
      bool foundDxil = false;
      for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t off32 = 0;
        if (!readU32(32 + static_cast<size_t>(i) * sizeof(uint32_t), off32))
          break;
        size_t off = off32;
        if (off > dxilSize || dxilSize - off < 8)
          continue;
        uint32_t fourcc = 0;
        uint32_t chunkSize32 = 0;
        if (!readU32(off, fourcc) || !readU32(off + 4, chunkSize32))
          continue;
        size_t chunkSize = chunkSize32;
        if (fourcc == dxmt::dxil::DXIL_FOURCC && chunkSize <= dxilSize - off - 8) {
          dxilData = dxilData + off + 8;
          dxilSize = chunkSize;
          foundDxil = true;
          break;
        }
      }
      if (!foundDxil) {
        errs() << "DXBC input does not contain a DXIL chunk\n";
        return 1;
      }
    }
    auto container = dxmt::dxil::DXILContainer::parse(dxilData, dxilSize);
    if (!container) {
      errs() << "input is not a DXIL program\n";
      return 1;
    }
    auto module = dxmt::dxil::BitcodeReader::parse(container->shader().bitcode.data, container->shader().bitcode.size);
    if (!module) {
      errs() << "failed to parse DXIL bitcode\n";
      return 1;
    }
    dxmt::dxil::MSLLoweringOptions lowering_options = {};
    for (const auto &spec : MSLVertexInputs) {
      unsigned reg = 0, table = 0, slot = 0, offset = 0, dxgi = 0, metal = 0;
      unsigned per_instance = 0, step = 1, raw_slot = 0, system_value = 0;
      int parsed = std::sscanf(
        spec.c_str(), "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
        &reg, &table, &slot, &offset, &dxgi, &metal, &per_instance, &step,
        &raw_slot, &system_value);
      if (parsed != 10) {
        errs() << "invalid --msl-vertex-input: " << spec << '\n';
        return 1;
      }
      dxmt::dxil::MSLVertexInputElement element = {};
      element.shader_register = reg;
      element.table_index = table;
      element.input_slot = slot;
      element.aligned_byte_offset = offset;
      element.dxgi_format = dxgi;
      element.metal_format = metal;
      element.per_instance = per_instance != 0;
      element.instance_step_rate = step;
      element.table_indexing_mode = raw_slot
        ? dxmt::dxil::MSLVertexTableIndexingMode::RawSlot
        : dxmt::dxil::MSLVertexTableIndexingMode::CompactBySlotMask;
      element.system_value = system_value != 0;
      lowering_options.vertex_inputs.push_back(element);
    }
    auto lowered = dxmt::dxil::MSLLowering::lower(*module, container->shader(), lowering_options);
    if (!lowered) {
      errs() << "failed to lower DXIL to MSL\n";
      return 1;
    }
    std::error_code EC;
    std::unique_ptr<ToolOutputFile> Out(new ToolOutputFile(OutputFilename, EC, sys::fs::OF_Text));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }
    Out->os() << lowered->source;
    Out->keep();
    return 0;
  }

  if (DisassembleDXBC) {
#ifdef __WIN32
    std::error_code EC;
    std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(OutputFilename, EC, sys::fs::OF_TextWithCRLF)
    );
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }

    ID3DBlob *blob;
    D3DDisassemble(
      MemRef.getBufferStart(), MemRef.getBufferSize(), 0, nullptr, &blob
    );

    Out->os().write(
      (const char *)blob->GetBufferPointer(), blob->GetBufferSize() - 1
    );
    // Declare success.
    Out->keep();

    blob->Release();
    return 0;
#else
    errs() << "Disassemble only supported on Windows" << '\n';
    return 1;
#endif
  }

  Module M("default", Context);
  dxmt::initializeModule(M);

  sm50_shader_t sm50;
  sm50_error_t err;
  if (SM50Initialize(
        MemRef.getBufferStart(), MemRef.getBufferSize(), &sm50, nullptr, &err
      )) {
    errs() << SM50GetErrorMessageString(err) << '\n';
    SM50FreeError(err);
    return 1;
  }

  SM50_SHADER_COMMON_DATA data;
  data.metal_version = SM50_SHADER_METAL_320;
  data.flags = {};
  data.next = 0;
  data.type = SM50_SHADER_COMMON;

  if (!HullBeforeDomain.getValue().empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFile(HullBeforeDomain, /*IsText=*/false);
    if (std::error_code EC = FileOrErr.getError()) {
      SMDiagnostic(
        HullBeforeDomain, SourceMgr::DK_Error,
        "Could not open input file: " + EC.message()
      )
        .print(argv[0], errs());
      return 1;
    }
    auto MemRef = FileOrErr->get()->getMemBufferRef();
    sm50_shader_t sm50_hull;
    if (SM50Initialize(
          MemRef.getBufferStart(), MemRef.getBufferSize(), &sm50_hull, nullptr,
          &err
        )) {
      errs() << SM50GetErrorMessageString(err) << '\n';
      SM50FreeError(err);
      return 1;
    }
    if (auto err = dxmt::dxbc::convert_dxbc_tesselator_domain_shader(
          (dxmt::dxbc::SM50ShaderInternal *)sm50, "shader_main",
          (dxmt::dxbc::SM50ShaderInternal *)sm50_hull, Context, M, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data
        )) {
      errs() << err << '\n';
      return 1;
    }
  } else if (!VertexBeforeHull.getValue().empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFile(VertexBeforeHull, /*IsText=*/false);
    if (std::error_code EC = FileOrErr.getError()) {
      SMDiagnostic(
        VertexBeforeHull, SourceMgr::DK_Error,
        "Could not open input file: " + EC.message()
      )
        .print(argv[0], errs());
      return 1;
    }
    auto MemRef = FileOrErr->get()->getMemBufferRef();
    sm50_shader_t sm50_vertex;
    if (SM50Initialize(
          MemRef.getBufferStart(), MemRef.getBufferSize(), &sm50_vertex, nullptr,
          &err
        )) {
      errs() << SM50GetErrorMessageString(err) << '\n';
      SM50FreeError(err);
      return 1;
    }
    if (auto err = dxmt::dxbc::convert_dxbc_vertex_hull_shader(
          (dxmt::dxbc::SM50ShaderInternal *)sm50_vertex, (dxmt::dxbc::SM50ShaderInternal *)sm50,
          "shader_main", Context, M, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data
        )) {
      errs() << err << '\n';
      return 1;
    }
  } else if (!HullAfterVertex.getValue().empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFile(HullAfterVertex, /*IsText=*/false);
    if (std::error_code EC = FileOrErr.getError()) {
      SMDiagnostic(
        HullAfterVertex, SourceMgr::DK_Error,
        "Could not open input file: " + EC.message()
      )
        .print(argv[0], errs());
      return 1;
    }
    auto MemRef = FileOrErr->get()->getMemBufferRef();
    sm50_shader_t sm50_hull;
    if (SM50Initialize(
          MemRef.getBufferStart(), MemRef.getBufferSize(), &sm50_hull, nullptr,
          &err
        )) {
      errs() << SM50GetErrorMessageString(err) << '\n';
      SM50FreeError(err);
      return 1;
    }
    if (auto err = dxmt::dxbc::convert_dxbc_vertex_hull_shader(
          (dxmt::dxbc::SM50ShaderInternal *)sm50, (dxmt::dxbc::SM50ShaderInternal *)sm50_hull,
          "shader_main", Context, M, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data
        )) {
      errs() << err << '\n';
      return 1;
    }
  } else if (!VertexBeforeGeometry.getValue().empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFile(VertexBeforeGeometry, /*IsText=*/false);
    if (std::error_code EC = FileOrErr.getError()) {
      SMDiagnostic(
        VertexBeforeGeometry, SourceMgr::DK_Error,
        "Could not open input file: " + EC.message()
      )
        .print(argv[0], errs());
      return 1;
    }
    auto MemRef = FileOrErr->get()->getMemBufferRef();
    sm50_shader_t sm50_vertex;
    if (SM50Initialize(
          MemRef.getBufferStart(), MemRef.getBufferSize(), &sm50_vertex, nullptr,
          &err
        )) {
      errs() << SM50GetErrorMessageString(err) << '\n';
      SM50FreeError(err);
      return 1;
    }
    if (auto err = dxmt::dxbc::convert_dxbc_geometry_shader(
          (dxmt::dxbc::SM50ShaderInternal *)sm50, "shader_main",
          (dxmt::dxbc::SM50ShaderInternal *)sm50_vertex, Context, M, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data
        )) {
      errs() << err << '\n';
      return 1;
    }
  } else if (!GeometryAfterVertex.getValue().empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFile(GeometryAfterVertex, /*IsText=*/false);
    if (std::error_code EC = FileOrErr.getError()) {
      SMDiagnostic(
        GeometryAfterVertex, SourceMgr::DK_Error,
        "Could not open input file: " + EC.message()
      )
        .print(argv[0], errs());
      return 1;
    }
    auto MemRef = FileOrErr->get()->getMemBufferRef();
    sm50_shader_t sm50_geometry;
    if (SM50Initialize(
          MemRef.getBufferStart(), MemRef.getBufferSize(), &sm50_geometry, nullptr,
          &err
        )) {
      errs() << SM50GetErrorMessageString(err) << '\n';
      SM50FreeError(err);
      return 1;
    }
    if (auto err = dxmt::dxbc::convert_dxbc_vertex_for_geometry_shader(
          (dxmt::dxbc::SM50ShaderInternal *)sm50, "shader_main",
          (dxmt::dxbc::SM50ShaderInternal *)sm50_geometry, Context, M, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data
        )) {
      errs() << err << '\n';
      return 1;
    }
  } else {
    if (auto err =
          dxmt::dxbc::convertDXBC(sm50, "shader_main", Context, M, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data)) {
      errs() << err << '\n';
      return 1;
    }
  }

  SM50Destroy(sm50);

  if (OptLevelO0) {
    // do nothing
  } else {
    dxmt::runOptimizationPasses(M);
  }

  dxmt::linkMSAD(M);
  dxmt::linkSamplePos(M);
  dxmt::linkTessellation(M);

  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(new ToolOutputFile(
    OutputFilename, EC,
    (EmitLLVM || EmitMetallib) ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None
  ));
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (EmitLLVM) {
    M.print(Out->os(), nullptr, PreserveAssemblyUseListOrder);
  } else if (EmitMetallib) {
    dxmt::metallib::MetallibWriter writer;
    writer.Write(M, Out->os());
  } else {
    WriteBitcodeToFile(
      M, Out->os(), PreserveBitcodeUseListOrder, nullptr, true
    );
  }

  // Declare success.
  Out->keep();

  return 0;
}