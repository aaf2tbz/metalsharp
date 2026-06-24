#include "d3d12_command_allocator.hpp"
#include "d3d12_device.hpp"
#include "d3d12_trace.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

#define CATRACE(fmt, ...) DXMTD3D12Trace("CmdAlloc", "CmdAlloc::" fmt, ##__VA_ARGS__)

namespace dxmt {

MTLD3D12CommandAllocator::MTLD3D12CommandAllocator(MTLD3D12Device *device,
                                                   D3D12_COMMAND_LIST_TYPE type)
    : m_device(device), m_type(type) {
  m_device->AddRef();
}

MTLD3D12CommandAllocator::~MTLD3D12CommandAllocator() {
  m_device->Release();
}

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocator::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_ID3D12Object ||
      riid == IID_ID3D12DeviceChild || riid == IID_ID3D12Pageable ||
      riid == IID_ID3D12CommandAllocator) {
    *ppvObject = ref(this);
    return S_OK;
  }
  CATRACE("QI unknown IID %s -> E_NOINTERFACE", str::format(riid).c_str());
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MTLD3D12CommandAllocator::AddRef() {
  return ++m_refCount;
}

ULONG STDMETHODCALLTYPE MTLD3D12CommandAllocator::Release() {
  uint32_t rc = --m_refCount;
  if (!rc) {
    uint32_t rp = --m_refPrivate;
    if (!rp) {
      m_refPrivate += 0x80000000;
      delete this;
    }
  }
  return rc;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocator::GetPrivateData(REFGUID guid, UINT *data_size,
                                          void *data) {
  CATRACE("GetPrivateData E_NOTIMPL");
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocator::SetPrivateData(REFGUID guid, UINT data_size,
                                         const void *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocator::SetPrivateDataInterface(REFGUID guid,
                                                  const IUnknown *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocator::SetName(LPCWSTR name) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocator::GetDevice(REFIID riid, void **device) {
  return m_device->QueryInterface(riid, device);
}

bool MTLD3D12CommandAllocator::RetireIfCompletedLocked() {
  if (!m_in_flight)
    return true;
  bool completed = false;
  if (m_completion_fence &&
      m_completion_fence->GetCompletedValue() >= m_completion_value)
    completed = true;
  if (!completed && m_completion_cmdbuf.handle) {
    auto status = m_completion_cmdbuf.status();
    completed = status > WMTCommandBufferStatusScheduled;
  }
  if (!completed)
    return false;
  m_in_flight = false;
  m_completion_fence = nullptr;
  m_completion_value = 0;
  m_completion_cmdbuf = nullptr;
  return true;
}

void MTLD3D12CommandAllocator::NotifyListOpened() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  m_open_list_count++;
}

void MTLD3D12CommandAllocator::NotifyListClosed() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_open_list_count)
    m_open_list_count--;
}

HRESULT MTLD3D12CommandAllocator::ValidateReusable() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_open_list_count)
    return E_FAIL;
  if (!RetireIfCompletedLocked())
    return E_FAIL;
  return S_OK;
}

void MTLD3D12CommandAllocator::MarkSubmitted() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  m_in_flight = true;
  m_completion_fence = nullptr;
  m_completion_value = 0;
  m_completion_cmdbuf = nullptr;
}

void MTLD3D12CommandAllocator::AttachCompletionFence(ID3D12Fence *fence,
                                                     uint64_t value) {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_in_flight)
    return;
  m_completion_fence = fence;
  m_completion_value = value;
}

void MTLD3D12CommandAllocator::AttachCompletionCommandBuffer(
    WMT::CommandBuffer cmdbuf) {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_in_flight)
    return;
  m_completion_cmdbuf = cmdbuf;
}

void MTLD3D12CommandAllocator::MarkCompleted() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  m_in_flight = false;
  m_completion_fence = nullptr;
  m_completion_value = 0;
  m_completion_cmdbuf = nullptr;
}

HRESULT STDMETHODCALLTYPE MTLD3D12CommandAllocator::Reset() {
  return ValidateReusable();
}

} // namespace dxmt
