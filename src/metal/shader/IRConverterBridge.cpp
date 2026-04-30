#include <metalsharp/IRConverterBridge.h>
#include <metalsharp/DXBCParser.h>
#include <metalsharp/Logger.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <fstream>
#include <filesystem>

#ifdef METALSHARP_HAS_IRCONVERTER
#include <metal_irconverter/metal_irconverter.h>
#endif

namespace metalsharp {

static const uint32_t DXBC_MAGIC = 0x43425844;
static const uint32_t DXIL_MAGIC = 0x4C495844;
static const uint32_t RTS0_MAGIC = 0x30535452;

#ifdef METALSHARP_HAS_IRCONVERTER

struct IRConverterBridge::FuncPtrs {
    decltype(IRCompilerCreate)* compilerCreate = nullptr;
    decltype(IRCompilerDestroy)* compilerDestroy = nullptr;
    decltype(IRCompilerAllocCompileAndLink)* compileAndLink = nullptr;
    decltype(IRCompilerSetGlobalRootSignature)* setGlobalRootSignature = nullptr;
    decltype(IRCompilerSetStageInGenerationMode)* setStageInGenerationMode = nullptr;
    decltype(IRCompilerSetMinimumDeploymentTarget)* setMinimumDeploymentTarget = nullptr;
    decltype(IRCompilerSetMinimumGPUFamily)* setMinimumGPUFamily = nullptr;
    decltype(IRCompilerSetCompatibilityFlags)* setCompatibilityFlags = nullptr;
    decltype(IRCompilerSetValidationFlags)* setValidationFlags = nullptr;
    decltype(IRCompilerSetEntryPointName)* setEntryPointName = nullptr;
    decltype(IRCompilerIgnoreDebugInformation)* ignoreDebugInformation = nullptr;
    decltype(IRCompilerSetInputTopology)* setInputTopology = nullptr;
    decltype(IRCompilerEnableGeometryAndTessellationEmulation)* enableGeomTess = nullptr;

    decltype(IRObjectCreateFromDXIL)* objectCreateFromDXIL = nullptr;
    decltype(IRObjectDestroy)* objectDestroy = nullptr;
    decltype(IRObjectGetType)* objectGetType = nullptr;
    decltype(IRObjectGetMetalIRShaderStage)* objectGetShaderStage = nullptr;

    decltype(IRMetalLibBinaryCreate)* metallibCreate = nullptr;
    decltype(IRMetalLibBinaryDestroy)* metallibDestroy = nullptr;
    decltype(IRMetalLibGetBytecode)* metallibGetBytecode = nullptr;
    decltype(IRMetalLibGetBytecodeSize)* metallibGetBytecodeSize = nullptr;
    decltype(IRObjectGetMetalLibBinary)* objectGetMetalLib = nullptr;

    decltype(IRShaderReflectionCreate)* reflectionCreate = nullptr;
    decltype(IRShaderReflectionDestroy)* reflectionDestroy = nullptr;
    decltype(IRObjectGetReflection)* objectGetReflection = nullptr;
    decltype(IRShaderReflectionGetEntryPointFunctionName)* reflectionGetEntryPoint = nullptr;
    decltype(IRShaderReflectionNeedsFunctionConstants)* reflectionNeedsFuncConst = nullptr;
    decltype(IRShaderReflectionGetResourceCount)* reflectionGetResourceCount = nullptr;
    decltype(IRShaderReflectionGetResourceLocations)* reflectionGetResourceLocations = nullptr;
    decltype(IRShaderReflectionCopyComputeInfo)* reflectionCopyComputeInfo = nullptr;
    decltype(IRShaderReflectionCopyVertexInfo)* reflectionCopyVertexInfo = nullptr;
    decltype(IRShaderReflectionCopyFragmentInfo)* reflectionCopyFragmentInfo = nullptr;
    decltype(IRShaderReflectionReleaseComputeInfo)* reflectionReleaseComputeInfo = nullptr;
    decltype(IRShaderReflectionReleaseVertexInfo)* reflectionReleaseVertexInfo = nullptr;
    decltype(IRShaderReflectionReleaseFragmentInfo)* reflectionReleaseFragmentInfo = nullptr;
    decltype(IRShaderReflectionCopyJSONString)* reflectionCopyJSON = nullptr;
    decltype(IRShaderReflectionReleaseString)* reflectionReleaseString = nullptr;

    decltype(IRRootSignatureCreateFromDescriptor)* rootSigCreateFromDesc = nullptr;
    decltype(IRRootSignatureDestroy)* rootSigDestroy = nullptr;
    decltype(IRRootSignatureGetResourceCount)* rootSigGetResourceCount = nullptr;
    decltype(IRRootSignatureGetResourceLocations)* rootSigGetResourceLocations = nullptr;

    decltype(IRVersionedRootSignatureDescriptorCreateFromBlob)* rootSigDescFromBlob = nullptr;
    decltype(IRVersionedRootSignatureDescriptorRelease)* rootSigDescRelease = nullptr;

    decltype(IRErrorGetCode)* errorGetCode = nullptr;
    decltype(IRErrorDestroy)* errorDestroy = nullptr;

    decltype(IRRayTracingPipelineConfigurationCreate)* rtPipelineConfigCreate = nullptr;
    decltype(IRRayTracingPipelineConfigurationDestroy)* rtPipelineConfigDestroy = nullptr;
    decltype(IRRayTracingPipelineConfigurationSetMaxAttributeSizeInBytes)* rtSetMaxAttrSize = nullptr;
    decltype(IRRayTracingPipelineConfigurationSetPipelineFlags)* rtSetPipelineFlags = nullptr;
    decltype(IRRayTracingPipelineConfigurationSetMaxRecursiveDepth)* rtSetMaxRecursion = nullptr;
    decltype(IRRayTracingPipelineConfigurationSetRayGenerationCompilationMode)* rtSetRayGenMode = nullptr;
    decltype(IRCompilerSetRayTracingPipelineConfiguration)* compilerSetRTPipeline = nullptr;
};

static IRShaderStage toIRStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return IRShaderStageVertex;
        case ShaderStage::Pixel: return IRShaderStageFragment;
        case ShaderStage::Compute: return IRShaderStageCompute;
        case ShaderStage::Geometry: return IRShaderStageGeometry;
        case ShaderStage::Hull: return IRShaderStageHull;
        case ShaderStage::Domain: return IRShaderStageDomain;
        case ShaderStage::Mesh: return IRShaderStageMesh;
        case ShaderStage::Amplification: return IRShaderStageAmplification;
        case ShaderStage::RayGeneration: return IRShaderStageRayGeneration;
        case ShaderStage::ClosestHit: return IRShaderStageClosestHit;
        case ShaderStage::Miss: return IRShaderStageMiss;
        case ShaderStage::Intersection: return IRShaderStageIntersection;
        case ShaderStage::AnyHit: return IRShaderStageAnyHit;
        case ShaderStage::Callable: return IRShaderStageCallable;
        default: return IRShaderStageInvalid;
    }
}

template<typename T>
static bool loadSym(void* lib, T& ptr, const char* name) {
    ptr = reinterpret_cast<T>(dlsym(lib, name));
    return ptr != nullptr;
}

IRConverterBridge::IRConverterBridge() : m_fn(new FuncPtrs()) {
    loadDylib();
}

IRConverterBridge::~IRConverterBridge() {
    unloadDylib();
    delete m_fn;
}

IRConverterBridge& IRConverterBridge::instance() {
    static IRConverterBridge inst;
    return inst;
}

bool IRConverterBridge::loadDylib() {
    if (m_loaded) return true;

    static const char* paths[] = {
        "/usr/local/lib/libmetalirconverter.dylib",
        "/opt/metal-sharpconverter/lib/libmetalirconverter.dylib",
        "/opt/metal-shaderconverter/lib/libmetalirconverter.dylib",
        "@rpath/libmetalirconverter.dylib",
        nullptr
    };

    for (int i = 0; paths[i]; ++i) {
        m_dylib = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (m_dylib) {
            MS_INFO("IRConverterBridge: loaded libmetalirconverter from %s", paths[i]);
            break;
        }
    }

    if (!m_dylib) {
        MS_WARN("IRConverterBridge: libmetalirconverter not found — DXIL compilation disabled");
        return false;
    }

    auto& f = *m_fn;
    bool ok = true;
    ok &= loadSym(m_dylib, f.compilerCreate, "IRCompilerCreate");
    ok &= loadSym(m_dylib, f.compilerDestroy, "IRCompilerDestroy");
    ok &= loadSym(m_dylib, f.compileAndLink, "IRCompilerAllocCompileAndLink");
    ok &= loadSym(m_dylib, f.objectCreateFromDXIL, "IRObjectCreateFromDXIL");
    ok &= loadSym(m_dylib, f.objectDestroy, "IRObjectDestroy");
    ok &= loadSym(m_dylib, f.metallibCreate, "IRMetalLibBinaryCreate");
    ok &= loadSym(m_dylib, f.metallibDestroy, "IRMetalLibBinaryDestroy");
    ok &= loadSym(m_dylib, f.metallibGetBytecode, "IRMetalLibGetBytecode");
    ok &=     loadSym(m_dylib, f.metallibGetBytecodeSize, "IRMetalLibGetBytecodeSize");
    loadSym(m_dylib, f.objectGetMetalLib, "IRObjectGetMetalLibBinary");
    ok &= loadSym(m_dylib, f.reflectionCreate, "IRShaderReflectionCreate");
    ok &= loadSym(m_dylib, f.reflectionDestroy, "IRShaderReflectionDestroy");
    ok &= loadSym(m_dylib, f.objectGetReflection, "IRObjectGetReflection");
    ok &= loadSym(m_dylib, f.errorGetCode, "IRErrorGetCode");
    ok &= loadSym(m_dylib, f.errorDestroy, "IRErrorDestroy");

    loadSym(m_dylib, f.setGlobalRootSignature, "IRCompilerSetGlobalRootSignature");
    loadSym(m_dylib, f.setStageInGenerationMode, "IRCompilerSetStageInGenerationMode");
    loadSym(m_dylib, f.setMinimumDeploymentTarget, "IRCompilerSetMinimumDeploymentTarget");
    loadSym(m_dylib, f.setMinimumGPUFamily, "IRCompilerSetMinimumGPUFamily");
    loadSym(m_dylib, f.setCompatibilityFlags, "IRCompilerSetCompatibilityFlags");
    loadSym(m_dylib, f.setValidationFlags, "IRCompilerSetValidationFlags");
    loadSym(m_dylib, f.setEntryPointName, "IRCompilerSetEntryPointName");
    loadSym(m_dylib, f.ignoreDebugInformation, "IRCompilerIgnoreDebugInformation");
    loadSym(m_dylib, f.objectGetType, "IRObjectGetType");
    loadSym(m_dylib, f.objectGetShaderStage, "IRObjectGetMetalIRShaderStage");
    loadSym(m_dylib, f.rootSigCreateFromDesc, "IRRootSignatureCreateFromDescriptor");
    loadSym(m_dylib, f.rootSigDestroy, "IRRootSignatureDestroy");
    loadSym(m_dylib, f.rootSigGetResourceCount, "IRRootSignatureGetResourceCount");
    loadSym(m_dylib, f.rootSigGetResourceLocations, "IRRootSignatureGetResourceLocations");
    loadSym(m_dylib, f.rootSigDescFromBlob, "IRVersionedRootSignatureDescriptorCreateFromBlob");
    loadSym(m_dylib, f.rootSigDescRelease, "IRVersionedRootSignatureDescriptorRelease");
    loadSym(m_dylib, f.reflectionGetEntryPoint, "IRShaderReflectionGetEntryPointFunctionName");
    loadSym(m_dylib, f.reflectionNeedsFuncConst, "IRShaderReflectionNeedsFunctionConstants");
    loadSym(m_dylib, f.reflectionGetResourceCount, "IRShaderReflectionGetResourceCount");
    loadSym(m_dylib, f.reflectionGetResourceLocations, "IRShaderReflectionGetResourceLocations");
    loadSym(m_dylib, f.reflectionCopyComputeInfo, "IRShaderReflectionCopyComputeInfo");
    loadSym(m_dylib, f.reflectionCopyVertexInfo, "IRShaderReflectionCopyVertexInfo");
    loadSym(m_dylib, f.reflectionCopyFragmentInfo, "IRShaderReflectionCopyFragmentInfo");
    loadSym(m_dylib, f.reflectionReleaseComputeInfo, "IRShaderReflectionReleaseComputeInfo");
    loadSym(m_dylib, f.reflectionReleaseVertexInfo, "IRShaderReflectionReleaseVertexInfo");
    loadSym(m_dylib, f.reflectionReleaseFragmentInfo, "IRShaderReflectionReleaseFragmentInfo");
    loadSym(m_dylib, f.reflectionCopyJSON, "IRShaderReflectionCopyJSONString");
    loadSym(m_dylib, f.reflectionReleaseString, "IRShaderReflectionReleaseString");
    loadSym(m_dylib, f.setInputTopology, "IRCompilerSetInputTopology");
    loadSym(m_dylib, f.enableGeomTess, "IRCompilerEnableGeometryAndTessellationEmulation");

    loadSym(m_dylib, f.rtPipelineConfigCreate, "IRRayTracingPipelineConfigurationCreate");
    loadSym(m_dylib, f.rtPipelineConfigDestroy, "IRRayTracingPipelineConfigurationDestroy");
    loadSym(m_dylib, f.rtSetMaxAttrSize, "IRRayTracingPipelineConfigurationSetMaxAttributeSizeInBytes");
    loadSym(m_dylib, f.rtSetPipelineFlags, "IRRayTracingPipelineConfigurationSetPipelineFlags");
    loadSym(m_dylib, f.rtSetMaxRecursion, "IRRayTracingPipelineConfigurationSetMaxRecursiveDepth");
    loadSym(m_dylib, f.rtSetRayGenMode, "IRRayTracingPipelineConfigurationSetRayGenerationCompilationMode");
    loadSym(m_dylib, f.compilerSetRTPipeline, "IRCompilerSetRayTracingPipelineConfiguration");

    if (!ok) {
        MS_ERROR("IRConverterBridge: failed to load required symbols from libmetalirconverter");
        dlclose(m_dylib);
        m_dylib = nullptr;
        return false;
    }

    m_loaded = true;
    MS_INFO("IRConverterBridge: successfully loaded all symbols");
    return true;
}

void IRConverterBridge::unloadDylib() {
    if (m_dylib) {
        dlclose(m_dylib);
        m_dylib = nullptr;
    }
    m_loaded = false;
}

bool IRConverterBridge::isAvailable() const {
    return m_loaded;
}

bool IRConverterBridge::isDXIL(const uint8_t* data, size_t size) const {
    if (!data || size < 16) return false;

    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic == DXIL_MAGIC) return true;

    if (magic == DXBC_MAGIC && size >= 32) {
        uint32_t chunkCount;
        memcpy(&chunkCount, data + 28, 4);
        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t offset;
            if (32 + i * 4 + 4 > size) break;
            memcpy(&offset, data + 32 + i * 4, 4);
            if (offset + 8 > size) continue;
            uint32_t chunkMagic;
            memcpy(&chunkMagic, data + offset, 4);
            if (chunkMagic == DXIL_MAGIC) return true;
        }
    }

    return false;
}

uint32_t IRConverterBridge::detectShaderModel(const uint8_t* data, size_t size) const {
    if (!data || size < 16) return 0;

    uint32_t magic;
    memcpy(&magic, data, 4);

    if (magic == DXBC_MAGIC && size >= 32) {
        uint32_t chunkCount;
        memcpy(&chunkCount, data + 28, 4);
        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t offset;
            if (32 + i * 4 + 4 > size) break;
            memcpy(&offset, data + 32 + i * 4, 4);
            if (offset + 8 > size) continue;
            uint32_t chunkMagic;
            memcpy(&chunkMagic, data + offset, 4);
            uint32_t chunkSize;
            memcpy(&chunkSize, data + offset + 4, 4);

            if (chunkMagic == 0x52444853 || chunkMagic == 0x53454853) {
                if (chunkSize >= 8) {
                    uint32_t versionToken;
                    memcpy(&versionToken, data + offset + 8, 4);
                    uint32_t major = (versionToken >> 4) & 0xF;
                    uint32_t minor = versionToken & 0xF;
                    return major * 10 + minor;
                }
            }
        }
    }

    if (magic == DXIL_MAGIC) return 60;
    return 0;
}

bool IRConverterBridge::extractDXILFromDXBC(
    const uint8_t* dxbcData,
    size_t dxbcSize,
    std::vector<uint8_t>& outDXIL
) {
    if (!dxbcData || dxbcSize < 32) return false;

    uint32_t magic;
    memcpy(&magic, dxbcData, 4);
    if (magic != DXBC_MAGIC) return false;

    uint32_t chunkCount;
    memcpy(&chunkCount, dxbcData + 28, 4);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t offset;
        if (32 + i * 4 + 4 > dxbcSize) break;
        memcpy(&offset, dxbcData + 32 + i * 4, 4);
        if (offset + 8 > dxbcSize) continue;

        uint32_t chunkMagic;
        memcpy(&chunkMagic, dxbcData + offset, 4);
        if (chunkMagic == DXIL_MAGIC) {
            uint32_t chunkSize;
            memcpy(&chunkSize, dxbcData + offset + 4, 4);
            if (offset + 8 + chunkSize > dxbcSize) continue;

            const uint8_t* dxilStart = dxbcData + offset + 8;
            uint32_t dxilVersion;
            uint32_t dxilTokenCount;
            if (chunkSize < 8) continue;
            memcpy(&dxilVersion, dxilStart, 4);
            memcpy(&dxilTokenCount, dxilStart + 4, 4);

            uint32_t programHeaderSize = 16;
            if (8 + programHeaderSize > chunkSize) continue;

            uint32_t dxilStartOffset;
            memcpy(&dxilStartOffset, dxilStart + 12, 4);
            if (8 + dxilStartOffset > chunkSize) continue;

            size_t rawSize = chunkSize - 8;
            outDXIL.assign(dxbcData + offset, dxbcData + offset + 8 + chunkSize);
            return true;
        }
    }

    return false;
}

bool IRConverterBridge::compileDXILToMetallib(
    const uint8_t* dxilData,
    size_t dxilSize,
    ShaderStage stage,
    const char* entryPoint,
    std::vector<uint8_t>& outMetallib,
    IRConverterReflection& outReflection
) {
    return compileDXILToMetallibWithRootSignature(
        dxilData, dxilSize, stage, entryPoint,
        nullptr, 0,
        outMetallib, outReflection
    );
}

bool IRConverterBridge::compileDXILToMetallibWithRootSignature(
    const uint8_t* dxilData,
    size_t dxilSize,
    ShaderStage stage,
    const char* entryPoint,
    const void* rootSignatureData,
    size_t rootSignatureSize,
    std::vector<uint8_t>& outMetallib,
    IRConverterReflection& outReflection
) {
    if (!m_loaded || !dxilData || dxilSize == 0) return false;

    std::lock_guard<std::mutex> lock(m_compileMutex);

    auto& f = *m_fn;

    IRCompiler* compiler = f.compilerCreate();
    if (!compiler) {
        MS_ERROR("IRConverterBridge: failed to create IRCompiler");
        return false;
    }
    auto compilerCleanup = [&](IRCompiler*) { f.compilerDestroy(compiler); };
    std::unique_ptr<IRCompiler, decltype(compilerCleanup)> compilerGuard(compiler, compilerCleanup);

    f.setMinimumDeploymentTarget(compiler, IROperatingSystem_macOS, "14.0.0");

    if (rootSignatureData && rootSignatureSize > 0 && f.rootSigDescFromBlob && f.rootSigCreateFromDesc) {
        IRError* rsError = nullptr;
        IRVersionedRootSignatureDescriptor* rsDesc = f.rootSigDescFromBlob(
            static_cast<const uint8_t*>(rootSignatureData),
            static_cast<uint32_t>(rootSignatureSize),
            &rsError
        );
        if (rsError) f.errorDestroy(rsError);

        if (rsDesc) {
            IRError* createError = nullptr;
            IRRootSignature* rootSig = f.rootSigCreateFromDesc(rsDesc, &createError);
            if (createError) f.errorDestroy(createError);
            f.rootSigDescRelease(rsDesc);

            if (rootSig) {
                f.setGlobalRootSignature(compiler, rootSig);

                if (f.rootSigGetResourceCount && f.rootSigGetResourceLocations) {
                    size_t resCount = f.rootSigGetResourceCount(rootSig);
                    if (resCount > 0) {
                        std::vector<IRResourceLocation> locations(resCount);
                        f.rootSigGetResourceLocations(rootSig, locations.data());
                        for (size_t i = 0; i < resCount; ++i) {
                            IRConverterResourceInfo info;
                            info.space = locations[i].space;
                            info.slot = locations[i].slot;
                            info.topLevelOffset = locations[i].topLevelOffset;
                            info.sizeBytes = locations[i].sizeBytes;
                            if (locations[i].resourceName)
                                info.resourceName = locations[i].resourceName;
                            outReflection.resources.push_back(info);
                        }
                    }
                }

                f.rootSigDestroy(rootSig);
            }
        }
    }

    IRObject* irObject = f.objectCreateFromDXIL(dxilData, dxilSize, IRBytecodeOwnershipCopy);
    if (!irObject) {
        MS_ERROR("IRConverterBridge: failed to create IRObject from DXIL");
        return false;
    }

    IRError* compileError = nullptr;
    IRObject* compiled = f.compileAndLink(compiler, entryPoint, irObject, &compileError);
    f.objectDestroy(irObject);

    if (!compiled) {
        if (compileError) {
            uint32_t code = f.errorGetCode(compileError);
            MS_ERROR("IRConverterBridge: compile failed with error code %u", code);
            f.errorDestroy(compileError);
        } else {
            MS_ERROR("IRConverterBridge: compile failed with no error info");
        }
        return false;
    }

    IRShaderStage irStage = toIRStage(stage);
    IRMetalLibBinary* metallib = f.metallibCreate();
    if (!metallib) {
        f.objectDestroy(compiled);
        if (compileError) f.errorDestroy(compileError);
        return false;
    }

    bool gotBinary = f.objectGetMetalLib(compiled, irStage, metallib);
    if (!gotBinary) {
        MS_WARN("IRConverterBridge: no metallib for stage, trying all stages");
        static const IRShaderStage stages[] = {
            IRShaderStageVertex, IRShaderStageFragment, IRShaderStageCompute,
            IRShaderStageGeometry, IRShaderStageHull, IRShaderStageDomain,
            IRShaderStageMesh, IRShaderStageAmplification,
            IRShaderStageRayGeneration, IRShaderStageClosestHit,
            IRShaderStageMiss, IRShaderStageIntersection,
            IRShaderStageAnyHit, IRShaderStageCallable
        };
        for (auto s : stages) {
            if (f.objectGetMetalLib(compiled, s, metallib)) {
                gotBinary = true;
                break;
            }
        }
    }

    if (!gotBinary) {
        MS_ERROR("IRConverterBridge: no metallib binary produced");
        f.metallibDestroy(metallib);
        f.objectDestroy(compiled);
        if (compileError) f.errorDestroy(compileError);
        return false;
    }

    size_t bytecodeSize = f.metallibGetBytecodeSize(metallib);
    if (bytecodeSize == 0) {
        f.metallibDestroy(metallib);
        f.objectDestroy(compiled);
        if (compileError) f.errorDestroy(compileError);
        return false;
    }

    outMetallib.resize(bytecodeSize);
    f.metallibGetBytecode(metallib, outMetallib.data());

    if (f.objectGetReflection) {
        IRShaderReflection* reflection = f.reflectionCreate();
        if (reflection) {
            if (f.objectGetReflection(compiled, irStage, reflection)) {
                if (f.reflectionGetEntryPoint)
                    outReflection.entryPointName = f.reflectionGetEntryPoint(reflection) ?: "";
                if (f.reflectionNeedsFuncConst)
                    outReflection.needsFunctionConstants = f.reflectionNeedsFuncConst(reflection);

                if (f.reflectionGetResourceCount && f.reflectionGetResourceLocations) {
                    size_t resCount = f.reflectionGetResourceCount(reflection);
                    if (resCount > 0 && outReflection.resources.empty()) {
                        std::vector<IRResourceLocation> locs(resCount);
                        f.reflectionGetResourceLocations(reflection, locs.data());
                        for (size_t i = 0; i < resCount; ++i) {
                            IRConverterResourceInfo info;
                            info.space = locs[i].space;
                            info.slot = locs[i].slot;
                            info.topLevelOffset = locs[i].topLevelOffset;
                            info.sizeBytes = locs[i].sizeBytes;
                            if (locs[i].resourceName)
                                info.resourceName = locs[i].resourceName;
                            outReflection.resources.push_back(info);
                        }
                    }
                }

                if (f.reflectionCopyComputeInfo && stage == ShaderStage::Compute) {
                    IRVersionedCSInfo csInfo = {};
                    csInfo.version = IRReflectionVersion_1_0;
                    if (f.reflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &csInfo)) {
                        outReflection.threadgroupSize[0] = csInfo.info_1_0.tg_size[0];
                        outReflection.threadgroupSize[1] = csInfo.info_1_0.tg_size[1];
                        outReflection.threadgroupSize[2] = csInfo.info_1_0.tg_size[2];
                        if (f.reflectionReleaseComputeInfo)
                            f.reflectionReleaseComputeInfo(&csInfo);
                    }
                }

                if (f.reflectionCopyVertexInfo && stage == ShaderStage::Vertex) {
                    IRVersionedVSInfo vsInfo = {};
                    vsInfo.version = IRReflectionVersion_1_0;
                    if (f.reflectionCopyVertexInfo(reflection, IRReflectionVersion_1_0, &vsInfo)) {
                        outReflection.vertexOutputSizeBytes = vsInfo.info_1_0.vertex_output_size_in_bytes;
                        outReflection.instanceIdIndex = vsInfo.info_1_0.instance_id_index;
                        outReflection.vertexIdIndex = vsInfo.info_1_0.vertex_id_index;
                        outReflection.needsDrawParams = vsInfo.info_1_0.needs_draw_params;
                        if (f.reflectionReleaseVertexInfo)
                            f.reflectionReleaseVertexInfo(&vsInfo);
                    }
                }

                if (f.reflectionCopyFragmentInfo && stage == ShaderStage::Pixel) {
                    IRVersionedFSInfo fsInfo = {};
                    fsInfo.version = IRReflectionVersion_1_0;
                    if (f.reflectionCopyFragmentInfo(reflection, IRReflectionVersion_1_0, &fsInfo)) {
                        outReflection.numRenderTargets = fsInfo.info_1_0.num_render_targets;
                        if (f.reflectionReleaseFragmentInfo)
                            f.reflectionReleaseFragmentInfo(&fsInfo);
                    }
                }

                if (f.reflectionCopyJSON) {
                    const char* json = f.reflectionCopyJSON(reflection);
                    if (json) {
                        outReflection.reflectionJSON = json;
                        f.reflectionReleaseString(json);
                    }
                }
            }
            f.reflectionDestroy(reflection);
        }
    }

    f.metallibDestroy(metallib);
    f.objectDestroy(compiled);
    if (compileError) f.errorDestroy(compileError);

    MS_INFO("IRConverterBridge: compiled DXIL → metallib (%zu bytes, stage %d)", bytecodeSize, (int)stage);
    return true;
}

bool IRConverterBridge::compileRayTracingShader(
    const uint8_t* dxilData,
    size_t dxilSize,
    ShaderStage stage,
    const char* entryPoint,
    uint32_t maxRecursionDepth,
    uint32_t maxAttributeSize,
    std::vector<uint8_t>& outMetallib,
    IRConverterReflection& outReflection
) {
    if (!m_loaded || !dxilData || dxilSize == 0) return false;

    std::lock_guard<std::mutex> lock(m_compileMutex);
    auto& f = *m_fn;

    IRCompiler* compiler = f.compilerCreate();
    if (!compiler) return false;
    auto compilerCleanup = [&](IRCompiler*) { f.compilerDestroy(compiler); };
    std::unique_ptr<IRCompiler, decltype(compilerCleanup)> compilerGuard(compiler, compilerCleanup);

    f.setMinimumDeploymentTarget(compiler, IROperatingSystem_macOS, "14.0.0");

    if (f.rtPipelineConfigCreate && f.compilerSetRTPipeline) {
        IRRayTracingPipelineConfiguration* rtConfig = f.rtPipelineConfigCreate();
        if (rtConfig) {
            auto rtCleanup = [&](IRRayTracingPipelineConfiguration*) { if (f.rtPipelineConfigDestroy) f.rtPipelineConfigDestroy(rtConfig); };
            std::unique_ptr<IRRayTracingPipelineConfiguration, decltype(rtCleanup)> rtGuard(rtConfig, rtCleanup);

            if (f.rtSetMaxRecursion)
                f.rtSetMaxRecursion(rtConfig, maxRecursionDepth);
            if (f.rtSetMaxAttrSize)
                f.rtSetMaxAttrSize(rtConfig, maxAttributeSize);

            f.compilerSetRTPipeline(compiler, rtConfig);
        }
    }

    IRObject* irObject = f.objectCreateFromDXIL(dxilData, dxilSize, IRBytecodeOwnershipCopy);
    if (!irObject) return false;

    IRError* compileError = nullptr;
    IRObject* compiled = f.compileAndLink(compiler, entryPoint, irObject, &compileError);
    f.objectDestroy(irObject);

    if (!compiled) {
        if (compileError) { f.errorDestroy(compileError); }
        return false;
    }

    IRShaderStage irStage = toIRStage(stage);
    IRMetalLibBinary* metallib = f.metallibCreate();
    if (!metallib) { f.objectDestroy(compiled); if (compileError) f.errorDestroy(compileError); return false; }

    bool gotBinary = f.objectGetMetalLib(compiled, irStage, metallib);
    if (!gotBinary) {
        static const IRShaderStage rtStages[] = {
            IRShaderStageRayGeneration, IRShaderStageClosestHit,
            IRShaderStageMiss, IRShaderStageIntersection,
            IRShaderStageAnyHit, IRShaderStageCallable
        };
        for (auto s : rtStages) {
            if (f.objectGetMetalLib(compiled, s, metallib)) { gotBinary = true; break; }
        }
    }

    if (!gotBinary) {
        f.metallibDestroy(metallib);
        f.objectDestroy(compiled);
        if (compileError) f.errorDestroy(compileError);
        return false;
    }

    size_t bytecodeSize = f.metallibGetBytecodeSize(metallib);
    if (bytecodeSize == 0) {
        f.metallibDestroy(metallib);
        f.objectDestroy(compiled);
        if (compileError) f.errorDestroy(compileError);
        return false;
    }

    outMetallib.resize(bytecodeSize);
    f.metallibGetBytecode(metallib, outMetallib.data());

    f.metallibDestroy(metallib);
    f.objectDestroy(compiled);
    if (compileError) f.errorDestroy(compileError);

    MS_INFO("IRConverterBridge: compiled RT shader → metallib (%zu bytes, stage %d, maxRecursion=%u)", bytecodeSize, (int)stage, maxRecursionDepth);
    return true;
}

IRConverterBridge::ShaderModelCapabilities IRConverterBridge::getShaderModelCapabilities(uint32_t smVersion) const {
    ShaderModelCapabilities caps;
    if (smVersion >= 60) {
        caps.waveOps = true;
        caps.int64 = true;
    }
    if (smVersion >= 61) {
        caps.halfPrecision = true;
        caps.barycentrics = true;
    }
    if (smVersion >= 63) {
        caps.rayTracing = true;
    }
    if (smVersion >= 65) {
        caps.meshShaders = true;
        caps.samplerFeedback = true;
    }
    if (smVersion >= 66) {
        caps.computeDerivatives = true;
    }
    return caps;
}

ShaderCompileService& ShaderCompileService::instance() {
    static ShaderCompileService inst;
    return inst;
}

ShaderCompileService::~ShaderCompileService() {
    shutdown();
}

bool ShaderCompileService::init(uint32_t numThreads) {
    if (m_running.load()) return true;

    if (numThreads == 0)
        numThreads = std::max(1u, std::thread::hardware_concurrency() / 2);

    m_running.store(true);
    for (uint32_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&ShaderCompileService::workerLoop, this);
    }

    if (!m_cacheDir.empty()) {
        std::string metallibDir = m_cacheDir + "/metallib_cache";
        mkdir(m_cacheDir.c_str(), 0755);
        mkdir(metallibDir.c_str(), 0755);
    }

    MS_INFO("ShaderCompileService: started %u worker threads", numThreads);
    return true;
}

void ShaderCompileService::shutdown() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_queueCV.notify_all();
    for (auto& w : m_workers) {
        if (w.joinable()) w.join();
    }
    m_workers.clear();
}

void ShaderCompileService::setCacheDir(const std::string& dir) {
    m_cacheDir = dir;
    if (!dir.empty()) {
        mkdir(dir.c_str(), 0755);
        mkdir((dir + "/metallib_cache").c_str(), 0755);
    }
}

std::future<ShaderCompileResult> ShaderCompileService::submit(const ShaderCompileRequest& request) {
    std::promise<ShaderCompileResult> promise;
    std::future<ShaderCompileResult> future = promise.get_future();

    ShaderCompileResult cached;
    if (checkDiskCache(request.hash, cached)) {
        m_cacheHits.fetch_add(1);
        promise.set_value(std::move(cached));
        return future;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.emplace(std::move(const_cast<ShaderCompileRequest&>(request)), std::move(promise));
    }
    m_queueCV.notify_one();
    return future;
}

void ShaderCompileService::workerLoop() {
    while (m_running.load()) {
        std::pair<ShaderCompileRequest, std::promise<ShaderCompileResult>> work;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !m_queue.empty() || !m_running.load();
            });
            if (!m_running.load() && m_queue.empty()) return;
            if (m_queue.empty()) continue;
            work = std::move(m_queue.front());
            m_queue.pop();
        }

        ShaderCompileResult result;
        result.hash = work.first.hash;

        auto& bridge = IRConverterBridge::instance();
        std::vector<uint8_t> dxil;

        if (bridge.isDXIL(work.first.bytecode.data(), work.first.bytecode.size())) {
            dxil = work.first.bytecode;
        } else if (!bridge.extractDXILFromDXBC(work.first.bytecode.data(), work.first.bytecode.size(), dxil)) {
            ParsedDXBC parsed;
            if (DXBCParser::parse(work.first.bytecode.data(), work.first.bytecode.size(), parsed)) {
                result.success = false;
                result.error = "DXBC shader not convertible to DXIL — needs custom DXBC→MSL fallback";
                work.second.set_value(std::move(result));
                continue;
            }
            result.success = false;
            result.error = "Failed to extract DXIL from DXBC container";
            work.second.set_value(std::move(result));
            continue;
        }

        bool ok;
        if (!work.first.rootSignatureData.empty()) {
            ok = bridge.compileDXILToMetallibWithRootSignature(
                dxil.data(), dxil.size(),
                work.first.stage,
                work.first.entryPoint.empty() ? nullptr : work.first.entryPoint.c_str(),
                work.first.rootSignatureData.data(),
                work.first.rootSignatureData.size(),
                result.metallib,
                result.reflection
            );
        } else {
            ok = bridge.compileDXILToMetallib(
                dxil.data(), dxil.size(),
                work.first.stage,
                work.first.entryPoint.empty() ? nullptr : work.first.entryPoint.c_str(),
                result.metallib,
                result.reflection
            );
        }

        result.success = ok;
        if (!ok) result.error = "libmetalirconverter compilation failed";
        if (ok) m_compiledCount.fetch_add(1);

        if (ok && !m_cacheDir.empty()) {
            storeDiskCache(work.first.hash, result);
        }

        work.second.set_value(std::move(result));
    }
}

bool ShaderCompileService::checkDiskCache(uint64_t hash, ShaderCompileResult& out) {
    if (m_cacheDir.empty()) return false;

    std::string path = m_cacheDir + "/metallib_cache/" + std::to_string(hash) + ".metallib";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    out.hash = hash;
    out.success = true;
    out.metallib.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    std::string jsonPath = m_cacheDir + "/metallib_cache/" + std::to_string(hash) + ".json";
    std::ifstream jsonFile(jsonPath);
    if (jsonFile.is_open()) {
        out.reflection.reflectionJSON.assign(
            std::istreambuf_iterator<char>(jsonFile), std::istreambuf_iterator<char>()
        );
    }

    return true;
}

void ShaderCompileService::storeDiskCache(uint64_t hash, const ShaderCompileResult& result) {
    std::string base = m_cacheDir + "/metallib_cache/" + std::to_string(hash);

    {
        std::ofstream file(base + ".metallib", std::ios::binary);
        if (file.is_open())
            file.write(reinterpret_cast<const char*>(result.metallib.data()), result.metallib.size());
    }

    if (!result.reflection.reflectionJSON.empty()) {
        std::ofstream file(base + ".json");
        if (file.is_open())
            file << result.reflection.reflectionJSON;
    }
}

#else // !METALSHARP_HAS_IRCONVERTER

IRConverterBridge::IRConverterBridge() : m_fn(nullptr) {}
IRConverterBridge::~IRConverterBridge() {}
IRConverterBridge& IRConverterBridge::instance() {
    static IRConverterBridge inst;
    return inst;
}
bool IRConverterBridge::loadDylib() { return false; }
void IRConverterBridge::unloadDylib() {}
bool IRConverterBridge::isAvailable() const { return false; }

bool IRConverterBridge::isDXIL(const uint8_t* data, size_t size) const {
    if (!data || size < 16) return false;
    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic == DXIL_MAGIC) return true;
    if (magic == DXBC_MAGIC && size >= 32) {
        uint32_t chunkCount;
        memcpy(&chunkCount, data + 28, 4);
        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t offset;
            if (32 + i * 4 + 4 > size) break;
            memcpy(&offset, data + 32 + i * 4, 4);
            if (offset + 8 > size) continue;
            uint32_t chunkMagic;
            memcpy(&chunkMagic, data + offset, 4);
            if (chunkMagic == DXIL_MAGIC) return true;
        }
    }
    return false;
}

uint32_t IRConverterBridge::detectShaderModel(const uint8_t* data, size_t size) const { return 0; }
bool IRConverterBridge::extractDXILFromDXBC(const uint8_t*, size_t, std::vector<uint8_t>&) { return false; }
bool IRConverterBridge::compileDXILToMetallib(const uint8_t*, size_t, ShaderStage, const char*, std::vector<uint8_t>&, IRConverterReflection&) { return false; }
bool IRConverterBridge::compileDXILToMetallibWithRootSignature(const uint8_t*, size_t, ShaderStage, const char*, const void*, size_t, std::vector<uint8_t>&, IRConverterReflection&) { return false; }
bool IRConverterBridge::compileRayTracingShader(const uint8_t*, size_t, ShaderStage, const char*, uint32_t, uint32_t, std::vector<uint8_t>&, IRConverterReflection&) { return false; }
IRConverterBridge::ShaderModelCapabilities IRConverterBridge::getShaderModelCapabilities(uint32_t) const { return {}; }

ShaderCompileService& ShaderCompileService::instance() {
    static ShaderCompileService inst;
    return inst;
}
ShaderCompileService::~ShaderCompileService() { shutdown(); }
bool ShaderCompileService::init(uint32_t) { return false; }
void ShaderCompileService::shutdown() {}
void ShaderCompileService::setCacheDir(const std::string&) {}
std::future<ShaderCompileService::ShaderCompileResult> ShaderCompileService::submit(const ShaderCompileRequest&) {
    std::promise<ShaderCompileResult> p;
    ShaderCompileResult r;
    r.success = false;
    r.error = "libmetalirconverter not available";
    p.set_value(std::move(r));
    return p.get_future();
}
void ShaderCompileService::workerLoop() {}
bool ShaderCompileService::checkDiskCache(uint64_t, ShaderCompileResult&) { return false; }
void ShaderCompileService::storeDiskCache(uint64_t, const ShaderCompileResult&) {}

#endif // METALSHARP_HAS_IRCONVERTER

}
