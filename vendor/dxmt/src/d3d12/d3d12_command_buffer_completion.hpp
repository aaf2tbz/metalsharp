#pragma once

#include "Metal.hpp"
#include "com/com_pointer.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace dxmt {

struct D3D12MetalSubmissionReferences {
  std::vector<Com<IUnknown>> resources;
  std::vector<WMT::Reference<WMT::Texture>> textures;
  std::vector<WMT::Reference<WMT::MetalDrawable>> drawables;

  void RetainResource(IUnknown *resource) {
    if (resource)
      resources.emplace_back(resource);
  }

  void RetainTexture(WMT::Texture texture) {
    if (texture.handle)
      textures.emplace_back(texture);
  }

  void RetainDrawable(WMT::MetalDrawable drawable) {
    if (drawable.handle)
      drawables.emplace_back(drawable);
  }
};

struct D3D12MetalCommandBufferCompletionSlot {
  WMT::Reference<WMT::CommandBuffer> cmdbuf;
  D3D12MetalSubmissionReferences references;
  uint64_t serial = 0;
  uint64_t completed_serial = 0;
  uint64_t completed_status = WMTCommandBufferStatusNotEnqueued;
};

inline void
ResetD3D12MetalCompletionSlot(D3D12MetalCommandBufferCompletionSlot &slot) {
  slot.cmdbuf = nullptr;
  slot.references = {};
  __atomic_store_n(&slot.serial, 0ull, __ATOMIC_RELEASE);
  __atomic_store_n(&slot.completed_serial, 0ull, __ATOMIC_RELEASE);
  __atomic_store_n(&slot.completed_status,
                   uint64_t(WMTCommandBufferStatusNotEnqueued),
                   __ATOMIC_RELEASE);
}

inline bool IsD3D12MetalCompletionSlotComplete(
    const D3D12MetalCommandBufferCompletionSlot &slot) {
  uint64_t serial = __atomic_load_n(&slot.serial, __ATOMIC_ACQUIRE);
  if (!serial)
    return false;
  return __atomic_load_n(&slot.completed_serial, __ATOMIC_ACQUIRE) == serial;
}

inline WMTCommandBufferStatus D3D12MetalCompletionSlotStatus(
    const D3D12MetalCommandBufferCompletionSlot &slot) {
  return static_cast<WMTCommandBufferStatus>(
      __atomic_load_n(&slot.completed_status, __ATOMIC_ACQUIRE));
}

inline uint64_t D3D12MetalCompletionSlotSerial(
    const D3D12MetalCommandBufferCompletionSlot &slot) {
  return __atomic_load_n(&slot.serial, __ATOMIC_ACQUIRE);
}

inline void
ArmD3D12MetalCompletionSlot(D3D12MetalCommandBufferCompletionSlot &slot,
                            WMT::CommandBuffer cmdbuf, uint64_t serial,
                            D3D12MetalSubmissionReferences references = {}) {
  slot.cmdbuf = cmdbuf;
  slot.references = std::move(references);
  __atomic_store_n(&slot.serial, serial, __ATOMIC_RELEASE);
  __atomic_store_n(&slot.completed_serial, 0ull, __ATOMIC_RELEASE);
  __atomic_store_n(&slot.completed_status,
                   uint64_t(WMTCommandBufferStatusNotEnqueued),
                   __ATOMIC_RELEASE);
  cmdbuf.addCompletedSignal(&slot.serial, &slot.completed_serial,
                            &slot.completed_status, serial);
}

} // namespace dxmt
