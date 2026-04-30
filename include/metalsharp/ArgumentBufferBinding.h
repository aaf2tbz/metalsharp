#pragma once

#include <metalsharp/Platform.h>
#include <metalsharp/ShaderStage.h>
#include <metalsharp/IRConverterBridge.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>

namespace metalsharp {

enum class ArgumentBufferResourceType : uint32_t {
    ConstantBuffer = 0,
    ShaderResourceView = 1,
    UnorderedAccessView = 2,
    Sampler = 3,
};

struct ArgumentBufferEntry {
    uint32_t rootParameterIndex = 0;
    uint32_t space = 0;
    uint32_t registerSlot = 0;
    uint32_t topLevelOffset = 0;
    uint64_t sizeBytes = 0;
    ArgumentBufferResourceType type = ArgumentBufferResourceType::ConstantBuffer;
    std::string resourceName;
};

struct ArgumentBufferLayout {
    uint32_t rootParameterCount = 0;
    uint64_t totalSizeBytes = 0;
    std::vector<ArgumentBufferEntry> entries;
    bool hasRootSignature = false;
};

class ArgumentBufferManager {
public:
    static ArgumentBufferManager& instance();

    ArgumentBufferLayout buildLayoutFromReflection(
        const IRConverterReflection& reflection,
        ShaderStage stage
    );

    ArgumentBufferLayout buildLayoutFromRootSignature(
        const void* rootSignatureData,
        size_t rootSignatureSize
    );

    bool encodeResource(
        const ArgumentBufferLayout& layout,
        uint32_t rootParameterIndex,
        void* argumentBufferData,
        size_t argumentBufferSize,
        const void* resourceData,
        size_t resourceDataSize
    );

    bool encodeRootConstants(
        const ArgumentBufferLayout& layout,
        uint32_t rootParameterIndex,
        void* argumentBufferData,
        size_t argumentBufferSize,
        const uint32_t* constants,
        uint32_t numConstants
    );

    bool encodeDescriptorTable(
        const ArgumentBufferLayout& layout,
        uint32_t rootParameterIndex,
        void* argumentBufferData,
        size_t argumentBufferSize,
        uint64_t gpuAddress
    );

    bool encodeRootDescriptor(
        const ArgumentBufferLayout& layout,
        uint32_t rootParameterIndex,
        void* argumentBufferData,
        size_t argumentBufferSize,
        uint64_t gpuAddress
    );

    ArgumentBufferManager(const ArgumentBufferManager&) = delete;
    ArgumentBufferManager& operator=(const ArgumentBufferManager&) = delete;

private:
    ArgumentBufferManager() = default;
    std::mutex m_mutex;
};

}
