#include "d3d12_fence.hpp"
#include "d3d12_device.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <cstdlib>
#include <thread>

#define FTRACE(fmt, ...)                                                       \
  do {                                                                         \
    static bool _trace_enabled = [] {                                          \
      const char *_raw = std::getenv("DXMT_D3D12_FENCE_TRACE");                \
      return _raw && _raw[0] && _raw[0] != '0';                                \
    }();                                                                       \
    if (!_trace_enabled)                                                       \
      break;                                                                   \
    FILE *_tf = fopen("Z:\\tmp\\dxmt_dxgi_trace.log", "a");                    \
    if (_tf) {                                                                 \
      fprintf(_tf, "Fence::" fmt "\n", ##__VA_ARGS__);                         \
      fclose(_tf);                                                             \
    }                                                                          \
  } while (0)

namespace dxmt {

MTLD3D12Fence::MTLD3D12Fence(MTLD3D12Device *device, uint64_t initial_value,
                             D3D12_FENCE_FLAGS flags)
    : m_device(device), m_flags(flags), m_value(initial_value) {
  m_device->AddRef();
  auto wmt_device = m_device->GetDXMTDevice().device();
  m_shared_event = wmt_device.newSharedEvent();
  m_shared_event.signalValue(initial_value);
  Logger::info(str::format("D3D12Fence: created value=", initial_value));
}

MTLD3D12Fence::~MTLD3D12Fence() {
  m_shared_event = nullptr;
  m_device->Release();
}

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::QueryInterface(REFIID riid,
                                                        void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_ID3D12Object ||
      riid == IID_ID3D12DeviceChild || riid == IID_ID3D12Pageable ||
      riid == IID_ID3D12Fence) {
    *ppvObject = ref(this);
    return S_OK;
  }
  FTRACE("QI unknown IID %s -> E_NOINTERFACE", str::format(riid).c_str());
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MTLD3D12Fence::AddRef() { return ++m_refCount; }

ULONG STDMETHODCALLTYPE MTLD3D12Fence::Release() {
  uint32_t rc = --m_refCount;
  if (!rc)
    delete this;
  return rc;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::GetPrivateData(REFGUID guid,
                                                        UINT *data_size,
                                                        void *data) {
  FTRACE("GetPrivateData E_NOTIMPL");
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::SetPrivateData(REFGUID guid,
                                                        UINT data_size,
                                                        const void *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12Fence::SetPrivateDataInterface(REFGUID guid, const IUnknown *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::SetName(LPCWSTR name) { return S_OK; }

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::GetDevice(REFIID riid, void **device) {
  return m_device->QueryInterface(riid, device);
}

uint64_t STDMETHODCALLTYPE MTLD3D12Fence::GetCompletedValue() {
  uint64_t current = m_value.load(std::memory_order_acquire);
  if (m_shared_event.handle) {
    uint64_t shared_value = m_shared_event.signaledValue();
    if (shared_value > current) {
      m_value.store(shared_value, std::memory_order_release);
      current = shared_value;
    }
  }
  FTRACE("GetCompletedValue -> %llu", (unsigned long long)current);
  return current;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::SetEventOnCompletion(uint64_t value,
                                                              HANDLE event) {
  uint64_t current = GetCompletedValue();
  FTRACE("SetEventOnCompletion value=%llu current=%llu this=%p event=%p",
         (unsigned long long)value, (unsigned long long)current, (void *)this,
         (void *)(uintptr_t)event);
  if (current >= value) {
    if (event)
      SetEvent(event);
    return S_OK;
  }

  if (!m_shared_event.handle) {
    FTRACE("SetEventOnCompletion no shared event this=%p", (void *)this);
    return E_FAIL;
  }

  AddRef();
  auto shared_event = m_shared_event;
  auto wait_value = value;
  auto wait_event = event;
  auto *self = this;
  std::thread([self, shared_event, wait_value, wait_event]() mutable {
    FTRACE("SetEventOnCompletion async wait begin value=%llu this=%p event=%p",
           (unsigned long long)wait_value, (void *)self,
           (void *)(uintptr_t)wait_event);
    shared_event.waitUntilSignaledValue(wait_value, UINT64_MAX);
    uint64_t shared_value = shared_event.signaledValue();
    if (shared_value > self->m_value.load(std::memory_order_acquire))
      self->m_value.store(shared_value, std::memory_order_release);
    FTRACE("SetEventOnCompletion async wait end value=%llu shared=%llu this=%p",
           (unsigned long long)wait_value, (unsigned long long)shared_value,
           (void *)self);
    if (wait_event)
      SetEvent(wait_event);
    self->Release();
  }).detach();

  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Fence::Signal(uint64_t value) {
  FTRACE("Signal value=%llu this=%p", (unsigned long long)value, (void *)this);
  m_value.store(value, std::memory_order_release);
  if (m_shared_event.handle) {
    m_shared_event.signalValue(value);
  }
  return S_OK;
}

} // namespace dxmt
