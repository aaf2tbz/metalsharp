/// @file CommandBatcher.h
/// @brief Draw, dispatch, and copy command batching for reduced Metal encoder overhead.
///
/// Accumulates D3D draw calls (Draw, DrawIndexed, DrawInstanced, etc.), compute dispatches,
/// copy operations, and clear commands into batched vectors that are flushed to Metal encoders
/// as groups. Batching reduces the per-call overhead of MTLRenderCommandEncoder state transitions
/// and is especially effective for games that issue many small draw calls. Configurable batch
/// size with automatic flush at frame boundaries.

#pragma once

#include <cstdint>
#include <functional>
#include <metalsharp/Platform.h>
#include <vector>

namespace metalsharp {

struct BatchedCommand {
    enum Type : uint8_t {
        Draw,
        DrawIndexed,
        DrawInstanced,
        DrawIndexedInstanced,
        Dispatch,
        CopyBuffer,
        CopyTexture,
        ClearRT,
        ClearDS,
    };
    Type type;
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t startIndex;
    int32_t baseVertex;
    uint32_t threadGroupX;
    uint32_t threadGroupY;
    uint32_t threadGroupZ;
    void* srcResource;
    void* dstResource;
    float clearColor[4];
    float clearDepth;
    uint8_t clearStencil;
    uint32_t clearFlags;
};

class CommandBatcher {
  public:
    static CommandBatcher& instance();

    void init();
    void shutdown();

    void beginFrame();
    void endFrame();

    void enqueue(const BatchedCommand& cmd);
    void flush();

    void setMaxBatchSize(uint32_t size) { m_maxBatchSize = size; }
    uint32_t batchSize() const { return static_cast<uint32_t>(m_batch.size()); }
    uint64_t totalBatches() const { return m_totalBatches; }
    uint64_t totalFlushes() const { return m_totalFlushes; }

    using ExecuteFn = std::function<void(const BatchedCommand&)>;
    void setExecuteFunction(ExecuteFn fn);

  private:
    CommandBatcher() = default;

    std::vector<BatchedCommand> m_batch;
    uint32_t m_maxBatchSize = 256;
    bool m_initialized = false;
    uint64_t m_totalBatches = 0;
    uint64_t m_totalFlushes = 0;
    ExecuteFn m_execute;
};

} // namespace metalsharp
