#pragma once

#include "com/com_pointer.hpp"
#include "d3d12.h"
#include "Metal.hpp"
#include <atomic>
#include <mutex>

namespace dxmt {

class MTLD3D12Device;

class MTLD3D12CommandAllocator : public ID3D12CommandAllocator {
public:
  MTLD3D12CommandAllocator(MTLD3D12Device *device,
                           D3D12_COMMAND_LIST_TYPE type);
  ~MTLD3D12CommandAllocator();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *data_size,
                                          void *data) override;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT data_size,
                                          const void *data) override;
  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
      REFGUID guid, const IUnknown *data) override;
  HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override;

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override;
  HRESULT STDMETHODCALLTYPE Reset() override;

  D3D12_COMMAND_LIST_TYPE GetType() const { return m_type; }

  void NotifyListOpened();
  void NotifyListClosed();
  HRESULT ValidateReusable();
  void MarkSubmitted();
  void AttachCompletionFence(ID3D12Fence *fence, uint64_t value);
  void AttachCompletionCommandBuffer(WMT::CommandBuffer cmdbuf);
  void MarkCompleted();

private:
  bool RetireIfCompletedLocked();

  MTLD3D12Device *m_device;
  D3D12_COMMAND_LIST_TYPE m_type;
  mutable std::mutex m_state_mutex;
  uint32_t m_open_list_count = 0;
  bool m_in_flight = false;
  Com<ID3D12Fence> m_completion_fence;
  uint64_t m_completion_value = 0;
  WMT::Reference<WMT::CommandBuffer> m_completion_cmdbuf;
  std::atomic<uint32_t> m_refCount = {1ul};
  std::atomic<uint32_t> m_refPrivate = {1ul};
};

} // namespace dxmt
