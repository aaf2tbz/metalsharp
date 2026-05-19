/// @file D3D12ResourceStateTracker.cpp
/// @brief D3D12 resource state tracking implementation.

#include <metalsharp/D3D12ResourceStateTracker.h>
#include <sstream>

namespace metalsharp {

uint64_t D3D12ResourceStateTracker::keyFor(ID3D12Resource* resource, UINT subresource) {
    uint64_t ptr = reinterpret_cast<uint64_t>(resource);
    return ptr ^ (static_cast<uint64_t>(subresource) << 48);
}

void D3D12ResourceStateTracker::setInitialState(ID3D12Resource* resource, UINT state) {
    if (!resource)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states[keyFor(resource, 0xffffffffu)] = state;
}

UINT D3D12ResourceStateTracker::stateFor(ID3D12Resource* resource, UINT subresource) const {
    if (!resource)
        return D3D12_RESOURCE_STATE_COMMON;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto exact = m_states.find(keyFor(resource, subresource));
    if (exact != m_states.end())
        return exact->second;
    auto all = m_states.find(keyFor(resource, 0xffffffffu));
    if (all != m_states.end())
        return all->second;
    return resource->__getResourceState();
}

D3D12TrackedBarrier D3D12ResourceStateTracker::applyTransition(ID3D12Resource* resource, UINT before, UINT after,
                                                               UINT subresource) {
    D3D12TrackedBarrier result;
    result.kind = D3D12BarrierKind::Transition;
    result.resource = resource;
    result.subresource = subresource;
    result.stateBefore = before;
    result.stateAfter = after;
    result.metalUsage = deriveMetalUsage(after);
    if (!resource)
        return result;

    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t key = keyFor(resource, subresource);
    auto current = m_states.find(key);
    if (current == m_states.end() && subresource != 0xffffffffu)
        current = m_states.find(keyFor(resource, 0xffffffffu));
    UINT knownState = current != m_states.end() ? current->second : resource->__getResourceState();
    result.stateMismatch = knownState != before && before != D3D12_RESOURCE_STATE_COMMON;
    m_states[key] = after;
    resource->__setResourceState(after);
    return result;
}

D3D12TrackedBarrier D3D12ResourceStateTracker::applyBarrier(const D3D12_RESOURCE_BARRIER& barrier) {
    return applyTransition(barrier.pResource, barrier.StateBefore, barrier.StateAfter, 0xffffffffu);
}

std::vector<D3D12TrackedBarrier> D3D12ResourceStateTracker::applyBarriers(UINT count,
                                                                          const D3D12_RESOURCE_BARRIER* barriers) {
    std::vector<D3D12TrackedBarrier> applied;
    if (!barriers)
        return applied;
    applied.reserve(count);
    for (UINT i = 0; i < count; ++i)
        applied.push_back(applyBarrier(barriers[i]));
    return applied;
}

void D3D12ResourceStateTracker::forget(ID3D12Resource* resource) {
    if (!resource)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t ptr = reinterpret_cast<uint64_t>(resource);
    for (auto it = m_states.begin(); it != m_states.end();) {
        if ((it->first & 0x0000ffffffffffffull) == (ptr & 0x0000ffffffffffffull))
            it = m_states.erase(it);
        else
            ++it;
    }
}

void D3D12ResourceStateTracker::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.clear();
}

MetalResourceUsage D3D12ResourceStateTracker::deriveMetalUsage(UINT state) {
    uint32_t usage = 0;
    if (state & (D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
        usage |= static_cast<uint32_t>(MetalResourceUsage::Read);
    if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        usage |= static_cast<uint32_t>(MetalResourceUsage::Read) | static_cast<uint32_t>(MetalResourceUsage::Write);
    if (state & D3D12_RESOURCE_STATE_RENDER_TARGET)
        usage |=
            static_cast<uint32_t>(MetalResourceUsage::RenderTarget) | static_cast<uint32_t>(MetalResourceUsage::Write);
    if (state & (D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ))
        usage |= static_cast<uint32_t>(MetalResourceUsage::DepthStencil);
    if (state & (D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE))
        usage |= static_cast<uint32_t>(MetalResourceUsage::Copy);
    if (usage == 0)
        return MetalResourceUsage::Unknown;
    return static_cast<MetalResourceUsage>(usage);
}

std::string D3D12ResourceStateTracker::describeState(UINT state) {
    if (state == D3D12_RESOURCE_STATE_COMMON)
        return "COMMON";
    std::ostringstream out;
    bool wrote = false;
    auto write = [&](const char* name, UINT bit) {
        if (!(state & bit))
            return;
        if (wrote)
            out << "|";
        out << name;
        wrote = true;
    };
    write("VB_CB", D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    write("IB", D3D12_RESOURCE_STATE_INDEX_BUFFER);
    write("RT", D3D12_RESOURCE_STATE_RENDER_TARGET);
    write("DEPTH_WRITE", D3D12_RESOURCE_STATE_DEPTH_WRITE);
    write("DEPTH_READ", D3D12_RESOURCE_STATE_DEPTH_READ);
    write("UAV", D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    write("NPS_SRV", D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    write("PS_SRV", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    write("COPY_DEST", D3D12_RESOURCE_STATE_COPY_DEST);
    write("COPY_SOURCE", D3D12_RESOURCE_STATE_COPY_SOURCE);
    return wrote ? out.str() : "UNKNOWN";
}

} // namespace metalsharp
