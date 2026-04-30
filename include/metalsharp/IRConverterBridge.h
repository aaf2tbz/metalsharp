#pragma once

#include <metalsharp/Platform.h>
#include <metalsharp/ShaderStage.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <future>
#include <atomic>

namespace metalsharp {

struct IRConverterResourceInfo {
    uint32_t space;
    uint32_t slot;
    uint32_t topLevelOffset;
    uint64_t sizeBytes;
    std::string resourceName;
};

struct IRConverterReflection {
    std::string entryPointName;
    std::string reflectionJSON;
    std::vector<IRConverterResourceInfo> resources;
    bool needsFunctionConstants = false;
    uint32_t threadgroupSize[3] = {};
    uint32_t vertexOutputSizeBytes = 0;
    int32_t instanceIdIndex = -1;
    int32_t vertexIdIndex = -1;
    bool needsDrawParams = false;
    int numRenderTargets = 0;
};

class IRConverterBridge {
public:
    static IRConverterBridge& instance();

    bool isAvailable() const;

    bool compileDXILToMetallib(
        const uint8_t* dxilData,
        size_t dxilSize,
        ShaderStage stage,
        const char* entryPoint,
        std::vector<uint8_t>& outMetallib,
        IRConverterReflection& outReflection
    );

    bool compileDXILToMetallibWithRootSignature(
        const uint8_t* dxilData,
        size_t dxilSize,
        ShaderStage stage,
        const char* entryPoint,
        const void* rootSignatureData,
        size_t rootSignatureSize,
        std::vector<uint8_t>& outMetallib,
        IRConverterReflection& outReflection
    );

    bool compileRayTracingShader(
        const uint8_t* dxilData,
        size_t dxilSize,
        ShaderStage stage,
        const char* entryPoint,
        uint32_t maxRecursionDepth,
        uint32_t maxAttributeSize,
        std::vector<uint8_t>& outMetallib,
        IRConverterReflection& outReflection
    );

    struct ShaderModelCapabilities {
        bool waveOps = false;
        bool halfPrecision = false;
        bool int64 = false;
        bool barycentrics = false;
        bool rayTracing = false;
        bool meshShaders = false;
        bool samplerFeedback = false;
        bool computeDerivatives = false;
    };

    ShaderModelCapabilities getShaderModelCapabilities(uint32_t smVersion) const;

    bool extractDXILFromDXBC(
        const uint8_t* dxbcData,
        size_t dxbcSize,
        std::vector<uint8_t>& outDXIL
    );

    bool isDXIL(const uint8_t* data, size_t size) const;
    uint32_t detectShaderModel(const uint8_t* data, size_t size) const;

    IRConverterBridge(const IRConverterBridge&) = delete;
    IRConverterBridge& operator=(const IRConverterBridge&) = delete;

private:
    IRConverterBridge();
    ~IRConverterBridge();

    bool loadDylib();
    void unloadDylib();

    void* m_dylib = nullptr;
    bool m_loaded = false;
    std::mutex m_compileMutex;

    struct FuncPtrs;
    FuncPtrs* m_fn = nullptr;
};

struct ShaderCompileRequest {
    std::vector<uint8_t> bytecode;
    ShaderStage stage;
    std::string entryPoint;
    uint64_t hash;
    std::vector<uint8_t> rootSignatureData;
};

struct ShaderCompileResult {
    uint64_t hash;
    bool success = false;
    std::vector<uint8_t> metallib;
    IRConverterReflection reflection;
    std::string error;
};

class ShaderCompileService {
public:
    static ShaderCompileService& instance();

    bool init(uint32_t numThreads = 0);
    void shutdown();

    std::future<ShaderCompileResult> submit(const ShaderCompileRequest& request);

    void setCacheDir(const std::string& dir);

    uint64_t compiledCount() const { return m_compiledCount.load(); }
    uint64_t cacheHitCount() const { return m_cacheHits.load(); }

    ShaderCompileService(const ShaderCompileService&) = delete;
    ShaderCompileService& operator=(const ShaderCompileService&) = delete;

private:
    ShaderCompileService() = default;
    ~ShaderCompileService();

    void workerLoop();
    bool checkDiskCache(uint64_t hash, ShaderCompileResult& out);
    void storeDiskCache(uint64_t hash, const ShaderCompileResult& result);

    std::vector<std::thread> m_workers;
    std::queue<std::pair<ShaderCompileRequest, std::promise<ShaderCompileResult>>> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_compiledCount{0};
    std::atomic<uint64_t> m_cacheHits{0};
    std::string m_cacheDir;
};

}
