#include "d3d12_resource.hpp"
#include "d3d12_device.hpp"
#include "d3d12_pipeline_state.hpp"
#include "d3d12_trace.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

#include <algorithm>

#define RTRACE(fmt, ...) DXMTD3D12Trace("Resource", fmt, ##__VA_ARGS__)

namespace dxmt {

static std::atomic<uint64_t> g_next_texture_virtual_address{0x200000000000ull};

static WMTTextureType
TextureTypeForResourceDesc(const D3D12_RESOURCE_DESC &desc) {
  UINT sample_count = desc.SampleDesc.Count ? desc.SampleDesc.Count : 1;
  switch (desc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    return (desc.DepthOrArraySize > 1) ? WMTTextureType1DArray : WMTTextureType1D;
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
    return WMTTextureType3D;
  default:
    if (sample_count > 1)
      return (desc.DepthOrArraySize > 1) ? WMTTextureType2DMultisampleArray
                                         : WMTTextureType2DMultisample;
    return (desc.DepthOrArraySize > 1) ? WMTTextureType2DArray : WMTTextureType2D;
  }
}

static uint32_t
SampleCountForResourceDesc(const D3D12_RESOURCE_DESC &desc,
                           WMTTextureType texture_type) {
  UINT sample_count = desc.SampleDesc.Count ? desc.SampleDesc.Count : 1;
  if (texture_type == WMTTextureType2DMultisample ||
      texture_type == WMTTextureType2DMultisampleArray)
    return sample_count;
  return 1;
}

MTLD3D12Resource::MTLD3D12Resource(
    MTLD3D12Device *device, const D3D12_RESOURCE_DESC &desc,
    D3D12_RESOURCE_STATES initial_state,
    D3D12_HEAP_PROPERTIES heap_properties, D3D12_HEAP_FLAGS heap_flags)
    : m_device(device), m_desc(desc), m_state(initial_state),
      m_heap_properties(heap_properties), m_heap_flags(heap_flags) {
  InitializeResource(WMT::Reference<WMT::Buffer>{}, nullptr, 0, 0);
}

MTLD3D12Resource::MTLD3D12Resource(
    MTLD3D12Device *device, const D3D12_RESOURCE_DESC &desc,
    D3D12_RESOURCE_STATES initial_state,
    D3D12_HEAP_PROPERTIES heap_properties,
    D3D12_HEAP_FLAGS heap_flags,
    WMT::Reference<WMT::Buffer> backing_buffer, void *backing_cpu_addr,
    uint64_t backing_gpu_addr, uint64_t backing_offset)
    : m_device(device), m_desc(desc), m_state(initial_state),
      m_heap_properties(heap_properties), m_heap_flags(heap_flags),
      m_backing_offset(backing_offset) {
  InitializeResource(std::move(backing_buffer), backing_cpu_addr,
                     backing_gpu_addr, backing_offset);
}

void MTLD3D12Resource::InitializeResource(
    WMT::Reference<WMT::Buffer> backing_buffer, void *backing_cpu_addr,
    uint64_t backing_gpu_addr, uint64_t backing_offset) {
  m_device->AddRef();

  auto wmt_device = m_device->GetDXMTDevice().device();
  RTRACE("ctor: wmt_device=%llu dim=%u fmt=%u w=%llu h=%u depth_or_arr=%u",
    (unsigned long long)wmt_device.handle, m_desc.Dimension, m_desc.Format,
    m_desc.Width, m_desc.Height, m_desc.DepthOrArraySize);

  if (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
    bool cpu_accessible = (m_heap_properties.Type == D3D12_HEAP_TYPE_UPLOAD ||
                           m_heap_properties.Type == D3D12_HEAP_TYPE_READBACK);
    WMTBufferInfo buf_info = {};
    buf_info.length = m_desc.Width ? m_desc.Width : 256;
    if (backing_buffer.handle) {
      m_mtl_buffer = std::move(backing_buffer);
      m_cpu_addr = backing_cpu_addr
                       ? static_cast<void *>(static_cast<char *>(backing_cpu_addr) +
                                             backing_offset)
                       : nullptr;
      m_gpu_addr = backing_gpu_addr + backing_offset;
      buf_info.gpu_address = m_gpu_addr;
      buf_info.length = m_desc.Width ? m_desc.Width : 256;
      m_buf_info = buf_info;
      RTRACE("ctor: placed buffer cpu=%p gpu=0x%llx len=%llu heap_off=%llu "
             "heap_type=%u",
             m_cpu_addr, (unsigned long long)m_gpu_addr,
             (unsigned long long)m_desc.Width,
             (unsigned long long)backing_offset,
             (unsigned)m_heap_properties.Type);
      if (m_desc.Width >= (64ull << 20)) {
        Logger::info(str::format("M12 large resource uses heap backing width=",
                                 m_desc.Width, " gpu=0x",
                                 (unsigned long long)m_gpu_addr, " off=",
                                 (unsigned long long)backing_offset));
      }
    } else {
      buf_info.options =
          cpu_accessible ? WMTResourceStorageModeShared
                         : WMTResourceStorageModePrivate;
      m_mtl_buffer = wmt_device.newBuffer(buf_info);
      m_cpu_addr = buf_info.memory.get_accessible_or_null();
      m_gpu_addr = buf_info.gpu_address;
      m_buf_info = buf_info;
      RTRACE("ctor: buffer cpu=%p gpu=0x%llx len=%llu opts=%u heap_type=%u",
             m_cpu_addr, (unsigned long long)m_gpu_addr,
             (unsigned long long)m_desc.Width, (unsigned)buf_info.options,
             (unsigned)m_heap_properties.Type);
      if (m_desc.Width >= (64ull << 20)) {
        Logger::info(str::format("M12 large resource standalone buffer width=",
                                 m_desc.Width, " gpu=0x",
                                 (unsigned long long)m_gpu_addr, " heap_type=",
                                 (unsigned)m_heap_properties.Type));
      }
    }
  } else {
    bool cpu_accessible = (m_heap_properties.Type == D3D12_HEAP_TYPE_UPLOAD ||
                           m_heap_properties.Type == D3D12_HEAP_TYPE_READBACK);
    WMTTextureInfo tex_info = {};
    tex_info.width = m_desc.Width;
    tex_info.height = m_desc.Height;
    tex_info.depth = (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                         ? m_desc.DepthOrArraySize
                         : 1;
    tex_info.array_length =
        (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            ? m_desc.DepthOrArraySize
            : 1;
    tex_info.mipmap_level_count = m_desc.MipLevels ? m_desc.MipLevels : 1;
    tex_info.type = TextureTypeForResourceDesc(m_desc);
    tex_info.sample_count = SampleCountForResourceDesc(m_desc, tex_info.type);
    if (tex_info.sample_count > 1)
      tex_info.mipmap_level_count = 1;
    tex_info.usage = (WMTTextureUsage)(WMTTextureUsageRenderTarget |
                                      WMTTextureUsageShaderRead |
                                      WMTTextureUsageShaderWrite |
                                      WMTTextureUsagePixelFormatView);
    tex_info.options = cpu_accessible ? WMTResourceStorageModeShared : WMTResourceStorageModePrivate;
    tex_info.pixel_format = MTLD3D12PipelineState::DXGIToMTLPixelFormat(static_cast<DXGI_FORMAT>(m_desc.Format));
    if (tex_info.pixel_format == WMTPixelFormatInvalid)
      tex_info.pixel_format = WMTPixelFormatBGRA8Unorm;

    RTRACE("ctor: about to newTexture type=%u fmt=%u %ux%u depth=%u arr=%u mip=%u sample=%u opts=%u",
      tex_info.type, tex_info.pixel_format, (unsigned)tex_info.width, (unsigned)tex_info.height,
      (unsigned)tex_info.depth, (unsigned)tex_info.array_length,
       (unsigned)tex_info.mipmap_level_count, (unsigned)tex_info.sample_count, (unsigned)tex_info.options);
    m_mtl_texture = wmt_device.newTexture(tex_info);
    m_tex_gpu_resource_id = tex_info.gpu_resource_id;
    if (!m_mtl_texture.handle) {
      RTRACE("ctor: texture creation FAILED type=%u fmt=%u %ux%u arr=%u",
        tex_info.type, tex_info.pixel_format, (unsigned)tex_info.width, (unsigned)tex_info.height, (unsigned)tex_info.array_length);
    } else {
      RTRACE("ctor: texture created fmt=%u %ux%u arr=%u handle=%llu %s",
        tex_info.pixel_format, (unsigned)tex_info.width, (unsigned)tex_info.height, (unsigned)tex_info.array_length,
        (unsigned long long)m_mtl_texture.handle, cpu_accessible ? "cpu" : "gpu");
    }
    // Textures do not expose a real D3D12 GPU virtual address. Older bridge
    // code allocated a same-sized fake Metal buffer here, which doubled memory
    // pressure for large render targets before UE5/Nanite transient heaps.
    m_gpu_addr = g_next_texture_virtual_address.fetch_add(0x10000ull);
    RTRACE("ctor: texture synthetic gpu_addr=0x%llx (no backing buffer)", (unsigned long long)m_gpu_addr);
  }

  Logger::info(str::format("D3D12Resource: dim=", m_desc.Dimension,
                            " ", m_desc.Width, "x", m_desc.Height,
                            " gpu=", m_gpu_addr));
  m_device->RegisterResource(this);
}

WMT::Reference<WMT::Texture> MTLD3D12Resource::GetMTLTexture() {
  if (!m_mtl_texture.handle && m_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
    bool cpu_accessible = (m_heap_properties.Type == D3D12_HEAP_TYPE_UPLOAD ||
                           m_heap_properties.Type == D3D12_HEAP_TYPE_READBACK);
    auto wmt_device = m_device->GetDXMTDevice().device();
    WMTTextureInfo tex_info = {};
    tex_info.width = m_desc.Width ? m_desc.Width : 1;
    tex_info.height = m_desc.Height ? m_desc.Height : 1;
    tex_info.depth = (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                          ? m_desc.DepthOrArraySize : 1;
    tex_info.array_length = (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
                                  ? m_desc.DepthOrArraySize : 1;
    tex_info.mipmap_level_count = m_desc.MipLevels ? m_desc.MipLevels : 1;
    tex_info.type = TextureTypeForResourceDesc(m_desc);
    tex_info.sample_count = SampleCountForResourceDesc(m_desc, tex_info.type);
    if (tex_info.sample_count > 1)
      tex_info.mipmap_level_count = 1;
    tex_info.usage = (WMTTextureUsage)(WMTTextureUsageRenderTarget | WMTTextureUsageShaderRead | WMTTextureUsageShaderWrite | WMTTextureUsagePixelFormatView);
    tex_info.options = cpu_accessible ? WMTResourceStorageModeShared : WMTResourceStorageModePrivate;
    tex_info.pixel_format = MTLD3D12PipelineState::DXGIToMTLPixelFormat(static_cast<DXGI_FORMAT>(m_desc.Format));
    if (tex_info.pixel_format == WMTPixelFormatInvalid)
      tex_info.pixel_format = WMTPixelFormatBGRA8Unorm;
    RTRACE("GetMTLTexture: creating type=%u fmt=%u %ux%ux%u arr=%u mip=%u sample=%u opts=%u",
      tex_info.type, tex_info.pixel_format, (unsigned)tex_info.width, (unsigned)tex_info.height,
      (unsigned)tex_info.depth, (unsigned)tex_info.array_length, (unsigned)tex_info.mipmap_level_count,
      (unsigned)tex_info.sample_count, (unsigned)tex_info.options);
    m_mtl_texture = wmt_device.newTexture(tex_info);
    m_tex_gpu_resource_id = tex_info.gpu_resource_id;
    if (!m_mtl_texture.handle) {
      RTRACE("GetMTLTexture: newTexture returned NULL handle");
      return m_mtl_texture;
    }
    RTRACE("GetMTLTexture: handle=%llu", (unsigned long long)m_mtl_texture.handle);
  }
  return m_mtl_texture;
}

uint32_t MTLD3D12Resource::GetTextureArrayLength() const {
  if (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    return m_desc.DepthOrArraySize ? m_desc.DepthOrArraySize : 1;
  return 1;
}

uint64_t MTLD3D12Resource::GetBufferByteLength() const {
  if (m_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    return m_desc.Width;
  return m_buf_info.length;
}

bool MTLD3D12Resource::ReadBufferShadow(uint64_t offset, void *dst,
                                        uint64_t bytes) const {
  if (!dst || m_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
    return false;
  if (bytes == 0)
    return true;
  if (offset > m_desc.Width || bytes > m_desc.Width - offset)
    return false;
  if (m_cpu_addr) {
    memcpy(dst, static_cast<const uint8_t *>(m_cpu_addr) + offset,
           static_cast<size_t>(bytes));
    return true;
  }

  std::lock_guard<std::mutex> lock(m_shadow_mutex);
  uint64_t copied = 0;
  auto *out = static_cast<uint8_t *>(dst);
  while (copied < bytes) {
    const uint64_t cursor = offset + copied;
    const BufferShadowSegment *covering = nullptr;
    for (const auto &segment : m_buffer_shadow_segments) {
      const uint64_t segment_size = segment.bytes.size();
      if (cursor >= segment.offset && cursor - segment.offset < segment_size) {
        covering = &segment;
        break;
      }
    }
    if (!covering)
      return false;
    const uint64_t segment_delta = cursor - covering->offset;
    const uint64_t available = covering->bytes.size() - segment_delta;
    const uint64_t to_copy = std::min<uint64_t>(available, bytes - copied);
    memcpy(out + copied, covering->bytes.data() + segment_delta,
           static_cast<size_t>(to_copy));
    copied += to_copy;
  }
  return true;
}

void MTLD3D12Resource::WriteBufferShadow(uint64_t offset, const void *src,
                                         uint64_t bytes) {
  constexpr uint64_t kMaxSingleShadowWrite = 16ull << 20;
  constexpr uint64_t kMaxTotalShadowBytes = 64ull << 20;

  if (!src || m_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || !bytes)
    return;
  if (offset > m_desc.Width || bytes > m_desc.Width - offset)
    return;
  if (m_cpu_addr)
    memcpy(static_cast<uint8_t *>(m_cpu_addr) + offset, src,
           static_cast<size_t>(bytes));
  if (bytes > kMaxSingleShadowWrite)
    return;

  const uint64_t write_end = offset + bytes;
  BufferShadowSegment new_segment;
  new_segment.offset = offset;
  new_segment.bytes.assign(static_cast<const uint8_t *>(src),
                           static_cast<const uint8_t *>(src) + bytes);

  std::lock_guard<std::mutex> lock(m_shadow_mutex);
  std::vector<BufferShadowSegment> next;
  next.reserve(m_buffer_shadow_segments.size() + 1);

  for (const auto &segment : m_buffer_shadow_segments) {
    if (segment.bytes.empty())
      continue;
    const uint64_t segment_start = segment.offset;
    const uint64_t segment_end = segment.offset + segment.bytes.size();
    if (segment_end <= offset || segment_start >= write_end) {
      next.push_back(segment);
      continue;
    }

    if (segment_start < offset) {
      BufferShadowSegment left;
      left.offset = segment_start;
      left.bytes.assign(segment.bytes.begin(),
                        segment.bytes.begin() + (offset - segment_start));
      next.push_back(std::move(left));
    }
    if (segment_end > write_end) {
      BufferShadowSegment right;
      right.offset = write_end;
      const uint64_t right_from = write_end - segment_start;
      right.bytes.assign(segment.bytes.begin() + right_from,
                         segment.bytes.end());
      next.push_back(std::move(right));
    }
  }
  next.push_back(std::move(new_segment));

  std::sort(next.begin(), next.end(),
            [](const BufferShadowSegment &a, const BufferShadowSegment &b) {
              return a.offset < b.offset;
            });

  std::vector<BufferShadowSegment> merged;
  merged.reserve(next.size());
  for (auto &segment : next) {
    if (segment.bytes.empty())
      continue;
    if (merged.empty()) {
      merged.push_back(std::move(segment));
      continue;
    }
    auto &tail = merged.back();
    const uint64_t tail_end = tail.offset + tail.bytes.size();
    if (segment.offset == tail_end) {
      tail.bytes.insert(tail.bytes.end(), segment.bytes.begin(),
                        segment.bytes.end());
    } else {
      merged.push_back(std::move(segment));
    }
  }
  m_buffer_shadow_segments = std::move(merged);

  uint64_t total_shadow_bytes = 0;
  for (const auto &segment : m_buffer_shadow_segments)
    total_shadow_bytes += segment.bytes.size();
  if (total_shadow_bytes > kMaxTotalShadowBytes) {
    m_buffer_shadow_segments.clear();
    BufferShadowSegment retained;
    retained.offset = offset;
    retained.bytes.assign(static_cast<const uint8_t *>(src),
                          static_cast<const uint8_t *>(src) + bytes);
    m_buffer_shadow_segments.push_back(std::move(retained));
  }
}

void MTLD3D12Resource::CopyBufferShadowRangeTo(MTLD3D12Resource *dst,
                                               uint64_t src_offset,
                                               uint64_t dst_offset,
                                               uint64_t bytes) const {
  constexpr uint64_t kMaxChunkBytes = 16ull << 20;
  if (!dst || m_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
      dst->m_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || !bytes)
    return;
  if (src_offset > m_desc.Width || bytes > m_desc.Width - src_offset ||
      dst_offset > dst->m_desc.Width || bytes > dst->m_desc.Width - dst_offset)
    return;

  auto invalidate_destination = [&]() {
    const uint64_t dst_end = dst_offset + bytes;
    std::lock_guard<std::mutex> lock(dst->m_shadow_mutex);
    std::vector<BufferShadowSegment> next;
    next.reserve(dst->m_buffer_shadow_segments.size());
    for (const auto &segment : dst->m_buffer_shadow_segments) {
      if (segment.bytes.empty())
        continue;
      const uint64_t segment_start = segment.offset;
      const uint64_t segment_end = segment.offset + segment.bytes.size();
      if (segment_end <= dst_offset || segment_start >= dst_end) {
        next.push_back(segment);
        continue;
      }
      if (segment_start < dst_offset) {
        BufferShadowSegment left;
        left.offset = segment_start;
        left.bytes.assign(segment.bytes.begin(),
                          segment.bytes.begin() +
                              (dst_offset - segment_start));
        next.push_back(std::move(left));
      }
      if (segment_end > dst_end) {
        BufferShadowSegment right;
        right.offset = dst_end;
        const uint64_t right_from = dst_end - segment_start;
        right.bytes.assign(segment.bytes.begin() + right_from,
                           segment.bytes.end());
        next.push_back(std::move(right));
      }
    }
    dst->m_buffer_shadow_segments = std::move(next);
  };

  if (m_cpu_addr) {
    invalidate_destination();
    uint64_t copied = 0;
    while (copied < bytes) {
      const uint64_t to_copy = std::min<uint64_t>(kMaxChunkBytes, bytes - copied);
      dst->WriteBufferShadow(dst_offset + copied,
                             static_cast<const uint8_t *>(m_cpu_addr) +
                                 src_offset + copied,
                             to_copy);
      copied += to_copy;
    }
    return;
  }

  struct Chunk {
    uint64_t src_offset = 0;
    std::vector<uint8_t> bytes;
  };
  std::vector<Chunk> chunks;

  {
    std::lock_guard<std::mutex> lock(m_shadow_mutex);
    const uint64_t src_end = src_offset + bytes;
    for (const auto &segment : m_buffer_shadow_segments) {
      const uint64_t segment_start = segment.offset;
      const uint64_t segment_end = segment.offset + segment.bytes.size();
      uint64_t overlap_start = std::max<uint64_t>(segment_start, src_offset);
      const uint64_t overlap_end = std::min<uint64_t>(segment_end, src_end);
      while (overlap_start < overlap_end) {
        const uint64_t to_copy =
            std::min<uint64_t>(kMaxChunkBytes, overlap_end - overlap_start);
        Chunk chunk;
        chunk.src_offset = overlap_start;
        const uint64_t segment_delta = overlap_start - segment_start;
        chunk.bytes.assign(segment.bytes.begin() + segment_delta,
                           segment.bytes.begin() + segment_delta + to_copy);
        chunks.push_back(std::move(chunk));
        overlap_start += to_copy;
      }
    }
  }

  invalidate_destination();
  for (const auto &chunk : chunks) {
    dst->WriteBufferShadow(dst_offset + (chunk.src_offset - src_offset),
                           chunk.bytes.data(), chunk.bytes.size());
  }
}

MTLD3D12Resource::~MTLD3D12Resource() {
  m_device->UnregisterResource(this);
  m_mtl_buffer = nullptr;
  m_mtl_texture = nullptr;
  m_device->Release();
}

HRESULT STDMETHODCALLTYPE
MTLD3D12Resource::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_ID3D12Object ||
      riid == IID_ID3D12DeviceChild || riid == IID_ID3D12Pageable ||
      riid == IID_ID3D12Resource || riid == IID_ID3D12Resource1 ||
      riid == IID_ID3D12Resource2) {
    *ppvObject = ref(this);
    return S_OK;
  }
  RTRACE("QI unknown IID %s -> E_NOINTERFACE", str::format(riid).c_str());
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MTLD3D12Resource::AddRef() { return ++m_refCount; }

ULONG STDMETHODCALLTYPE MTLD3D12Resource::Release() {
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
MTLD3D12Resource::GetPrivateData(REFGUID guid, UINT *data_size, void *data) {
  RTRACE("GetPrivateData E_NOTIMPL");
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12Resource::SetPrivateData(REFGUID guid, UINT data_size,
                                 const void *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12Resource::SetPrivateDataInterface(REFGUID guid, const IUnknown *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Resource::SetName(LPCWSTR name) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12Resource::GetDevice(REFIID riid, void **device) {
  return m_device->QueryInterface(riid, device);
}
HRESULT STDMETHODCALLTYPE
MTLD3D12Resource::Map(UINT sub_resource,
                                                 const D3D12_RANGE *read_range,
                                                 void **data) {
  RTRACE("Map sub=%u", sub_resource);
  if (!data)
    return E_POINTER;
  if (m_desc.Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
    RTRACE("Map: invalid dimension, returning fake pointer");
    *data = (void*)1;
    return S_OK;
  }
  if (m_cpu_addr) {
    *data = m_cpu_addr;
    RTRACE("Map returning cpu_addr=%p gpu_addr=0x%llx", m_cpu_addr, (unsigned long long)m_gpu_addr);
    return S_OK;
  }
  RTRACE("Map FAILED - no cpu_addr");
  return E_FAIL;
}

void STDMETHODCALLTYPE MTLD3D12Resource::Unmap(
    UINT sub_resource, const D3D12_RANGE *written_range) {}

D3D12_RESOURCE_DESC *STDMETHODCALLTYPE
MTLD3D12Resource::GetDesc(D3D12_RESOURCE_DESC *__ret) {
  if (!__ret) {
    RTRACE("GetDesc called with null return pointer");
    return nullptr;
  }
  *__ret = m_desc;
  return __ret;
}

D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE
MTLD3D12Resource::GetGPUVirtualAddress() {
  RTRACE("GetGPUVirtualAddress -> 0x%llx this=%p is_buffer=%d", (unsigned long long)m_gpu_addr, (void*)this, IsBuffer());
  return m_gpu_addr;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Resource::WriteToSubresource(
    UINT dst_sub_resource, const D3D12_BOX *dst_box, const void *src_data,
    UINT src_row_pitch, UINT src_slice_pitch) {
  RTRACE("WriteToSubresource sub=%u box=%p", dst_sub_resource, dst_box);
  if (!src_data)
    return E_POINTER;
  if (m_desc.Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
    return S_OK;
  }
  if (m_cpu_addr) {
    if (dst_box) {
      UINT rows = dst_box->bottom - dst_box->top;
      UINT depth = dst_box->back - dst_box->front;
      for (UINT z = 0; z < depth; z++) {
        for (UINT y = 0; y < rows; y++) {
          memcpy((char *)m_cpu_addr + (dst_box->front + z) * src_slice_pitch + (dst_box->top + y) * src_row_pitch + dst_box->left,
                 (char *)src_data + z * src_slice_pitch + y * src_row_pitch,
                 dst_box->right - dst_box->left);
        }
      }
    } else {
      memcpy(m_cpu_addr, src_data, src_slice_pitch ? src_slice_pitch : src_row_pitch);
    }
    return S_OK;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE MTLD3D12Resource::ReadFromSubresource(
    void *dst_data, UINT dst_row_pitch, UINT dst_slice_pitch,
    UINT src_sub_resource, const D3D12_BOX *src_box) {
  void *vtable = *(void**)this;
  RTRACE("ReadFromSubresource dst=%p row_pitch=%u slice_pitch=%u sub=%u box=%p dim=%u this=%p vtable=%p", dst_data, dst_row_pitch, dst_slice_pitch, src_sub_resource, src_box, m_desc.Dimension, (void*)this, vtable);
  if (!dst_data)
    return E_POINTER;
  if (m_desc.Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
    RTRACE("ReadFromSubresource: invalid dimension %u, this=%p is NOT a resource! Skipping.", m_desc.Dimension, (void*)this);
    return S_OK;
  }
  if (m_cpu_addr) {
    UINT rows = m_desc.Height ? m_desc.Height : 1;
    if (src_box) {
      UINT copy_rows = src_box->bottom - src_box->top;
      UINT copy_depth = src_box->back - src_box->front;
      UINT copy_width = src_box->right - src_box->left;
      for (UINT z = 0; z < copy_depth; z++) {
        for (UINT y = 0; y < copy_rows; y++) {
          memcpy((char *)dst_data + z * dst_slice_pitch + y * dst_row_pitch,
                 (char *)m_cpu_addr + (src_box->front + z) * rows * dst_row_pitch + (src_box->top + y) * dst_row_pitch + src_box->left,
                 copy_width);
        }
      }
    } else {
      memcpy(dst_data, m_cpu_addr, dst_slice_pitch ? dst_slice_pitch : dst_row_pitch);
    }
    return S_OK;
  }
  if (m_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
    UINT total = dst_slice_pitch ? dst_slice_pitch : dst_row_pitch * (m_desc.Height ? m_desc.Height : 1);
    if (total) memset(dst_data, 0, total);
    return S_OK;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12Resource::GetHeapProperties(D3D12_HEAP_PROPERTIES *heap_properties,
                                    D3D12_HEAP_FLAGS *flags) {
  if (heap_properties)
    *heap_properties = m_heap_properties;
  if (flags)
    *flags = m_heap_flags;
  return S_OK;
}

} // namespace dxmt
