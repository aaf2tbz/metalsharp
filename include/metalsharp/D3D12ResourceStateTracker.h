/// @file D3D12ResourceStateTracker.h
/// @brief D3D12 resource/subresource state tracking before Metal usage derivation.

#pragma once

#include <cstdint>
#include <d3d/D3D12.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace metalsharp {

enum class D3D12BarrierKind : uint32_t {
    Transition,
    UAV,
    Aliasing,
    Unsupported,
};

enum class MetalResourceUsage : uint32_t {
    Unknown = 0,
    Read = 1,
    Write = 2,
    RenderTarget = 4,
    DepthStencil = 8,
    Copy = 16,
};

struct D3D12TrackedBarrier {
    D3D12BarrierKind kind = D3D12BarrierKind::Unsupported;
    ID3D12Resource* resource = nullptr;
    UINT subresource = 0xffffffffu;
    UINT stateBefore = D3D12_RESOURCE_STATE_COMMON;
    UINT stateAfter = D3D12_RESOURCE_STATE_COMMON;
    bool stateMismatch = false;
    MetalResourceUsage metalUsage = MetalResourceUsage::Unknown;
};

class D3D12ResourceStateTracker {
  public:
    void setInitialState(ID3D12Resource* resource, UINT state);
    UINT stateFor(ID3D12Resource* resource, UINT subresource = 0xffffffffu) const;
    D3D12TrackedBarrier applyTransition(ID3D12Resource* resource, UINT before, UINT after,
                                        UINT subresource = 0xffffffffu);
    D3D12TrackedBarrier applyBarrier(const D3D12_RESOURCE_BARRIER& barrier);
    std::vector<D3D12TrackedBarrier> applyBarriers(UINT count, const D3D12_RESOURCE_BARRIER* barriers);
    void forget(ID3D12Resource* resource);
    void clear();

    static MetalResourceUsage deriveMetalUsage(UINT d3d12State);
    static std::string describeState(UINT d3d12State);

  private:
    static uint64_t keyFor(ID3D12Resource* resource, UINT subresource);

    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, UINT> m_states;
};

} // namespace metalsharp
