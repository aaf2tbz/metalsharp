#include <metalsharp/ArgumentBufferBinding.h>
#include <metalsharp/IRConverterBridge.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <algorithm>

namespace metalsharp {

ArgumentBufferManager& ArgumentBufferManager::instance() {
    static ArgumentBufferManager inst;
    return inst;
}

ArgumentBufferLayout ArgumentBufferManager::buildLayoutFromReflection(
    const IRConverterReflection& reflection,
    ShaderStage stage
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ArgumentBufferLayout layout;
    layout.hasRootSignature = !reflection.resources.empty();
    layout.rootParameterCount = static_cast<uint32_t>(reflection.resources.size());

    uint64_t currentOffset = 0;
    for (size_t i = 0; i < reflection.resources.size(); ++i) {
        const auto& res = reflection.resources[i];
        ArgumentBufferEntry entry;
        entry.rootParameterIndex = static_cast<uint32_t>(i);
        entry.space = res.space;
        entry.registerSlot = res.slot;
        entry.topLevelOffset = res.topLevelOffset;
        entry.sizeBytes = res.sizeBytes;
        entry.resourceName = res.resourceName;

        if (entry.topLevelOffset == 0 && currentOffset > 0) {
            entry.topLevelOffset = static_cast<uint32_t>(currentOffset);
        }

        entry.type = ArgumentBufferResourceType::ConstantBuffer;

        layout.entries.push_back(entry);
        currentOffset = std::max(currentOffset,
            static_cast<uint64_t>(entry.topLevelOffset) + entry.sizeBytes);
    }

    layout.totalSizeBytes = currentOffset;
    if (layout.totalSizeBytes == 0)
        layout.totalSizeBytes = 256;

    MS_INFO("ArgumentBufferManager: built layout from reflection — %u entries, %llu bytes",
             layout.rootParameterCount, layout.totalSizeBytes);
    return layout;
}

ArgumentBufferLayout ArgumentBufferManager::buildLayoutFromRootSignature(
    const void* rootSignatureData,
    size_t rootSignatureSize
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ArgumentBufferLayout layout;
    layout.hasRootSignature = rootSignatureData != nullptr && rootSignatureSize > 0;

    if (!rootSignatureData || rootSignatureSize == 0) {
        layout.totalSizeBytes = 256;
        return layout;
    }

    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) {
        MS_WARN("ArgumentBufferManager: IRConverter not available for root signature parsing");
        layout.totalSizeBytes = 256;
        return layout;
    }

    layout.totalSizeBytes = 256;
    return layout;
}

bool ArgumentBufferManager::encodeResource(
    const ArgumentBufferLayout& layout,
    uint32_t rootParameterIndex,
    void* argumentBufferData,
    size_t argumentBufferSize,
    const void* resourceData,
    size_t resourceDataSize
) {
    if (!argumentBufferData || argumentBufferSize == 0) return false;

    for (const auto& entry : layout.entries) {
        if (entry.rootParameterIndex != rootParameterIndex) continue;

        uint32_t offset = entry.topLevelOffset;
        if (offset + resourceDataSize > argumentBufferSize) {
            MS_WARN("ArgumentBufferManager: resource data exceeds buffer at offset %u", offset);
            return false;
        }

        memcpy(static_cast<uint8_t*>(argumentBufferData) + offset,
               resourceData, resourceDataSize);
        return true;
    }

    MS_WARN("ArgumentBufferManager: root parameter %u not found in layout", rootParameterIndex);
    return false;
}

bool ArgumentBufferManager::encodeRootConstants(
    const ArgumentBufferLayout& layout,
    uint32_t rootParameterIndex,
    void* argumentBufferData,
    size_t argumentBufferSize,
    const uint32_t* constants,
    uint32_t numConstants
) {
    if (!argumentBufferData || !constants || numConstants == 0) return false;

    size_t dataSize = numConstants * sizeof(uint32_t);

    for (const auto& entry : layout.entries) {
        if (entry.rootParameterIndex != rootParameterIndex) continue;

        uint32_t offset = entry.topLevelOffset;
        if (offset + dataSize > argumentBufferSize) {
            MS_WARN("ArgumentBufferManager: constants exceed buffer at offset %u", offset);
            return false;
        }

        memcpy(static_cast<uint8_t*>(argumentBufferData) + offset,
               constants, dataSize);
        return true;
    }

    return false;
}

bool ArgumentBufferManager::encodeDescriptorTable(
    const ArgumentBufferLayout& layout,
    uint32_t rootParameterIndex,
    void* argumentBufferData,
    size_t argumentBufferSize,
    uint64_t gpuAddress
) {
    if (!argumentBufferData) return false;

    for (const auto& entry : layout.entries) {
        if (entry.rootParameterIndex != rootParameterIndex) continue;

        uint32_t offset = entry.topLevelOffset;
        if (offset + sizeof(uint64_t) > argumentBufferSize) {
            MS_WARN("ArgumentBufferManager: descriptor table ptr exceeds buffer at offset %u", offset);
            return false;
        }

        memcpy(static_cast<uint8_t*>(argumentBufferData) + offset,
               &gpuAddress, sizeof(uint64_t));
        return true;
    }

    return false;
}

bool ArgumentBufferManager::encodeRootDescriptor(
    const ArgumentBufferLayout& layout,
    uint32_t rootParameterIndex,
    void* argumentBufferData,
    size_t argumentBufferSize,
    uint64_t gpuAddress
) {
    if (!argumentBufferData) return false;

    for (const auto& entry : layout.entries) {
        if (entry.rootParameterIndex != rootParameterIndex) continue;

        uint32_t offset = entry.topLevelOffset;
        if (offset + sizeof(uint64_t) > argumentBufferSize) return false;

        memcpy(static_cast<uint8_t*>(argumentBufferData) + offset,
               &gpuAddress, sizeof(uint64_t));
        return true;
    }

    return false;
}

}
