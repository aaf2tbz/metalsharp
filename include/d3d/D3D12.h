#pragma once

#include <metalsharp/Platform.h>

static const GUID IID_ID3D12Object = {0xc4fec28f, 0x7966, 0x4e14, {0x8a, 0x15, 0x79, 0x57, 0x1c, 0x60, 0x9d, 0xab}};
static const GUID IID_ID3D12Device = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x29, 0x48, 0x31, 0x1a, 0x47}};
static const GUID IID_ID3D12CommandQueue = {0x0ec870a6, 0x5d7e, 0x4c22, {0xa4, 0x89, 0x7a, 0xb2, 0x27, 0x6a, 0x3a, 0x0a}};
static const GUID IID_ID3D12CommandAllocator = {0x6102dee4, 0xaf59, 0x4b09, {0xb6, 0x59, 0xbd, 0x02, 0x62, 0x8e, 0x22, 0x4a}};
static const GUID IID_ID3D12GraphicsCommandList = {0x5b160d0f, 0xac1b, 0x4185, {0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0xd3}};
static const GUID IID_ID3D12Resource = {0x696442be, 0xa72e, 0x4056, {0xbc, 0x83, 0x34, 0x96, 0x4d, 0x34, 0xe2, 0xf3}};
static const GUID IID_ID3D12DescriptorHeap = {0x8efb471d, 0x616c, 0x4f49, {0x90, 0xf7, 0x12, 0x76, 0x30, 0x56, 0x31, 0x8d}};
static const GUID IID_ID3D12RootSignature = {0xc54a6b66, 0x72df, 0x4ee8, {0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x12, 0x32}};
static const GUID IID_ID3D12PipelineState = {0x765a30f3, 0xf624, 0x4c6f, {0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x44, 0x5e}};
static const GUID IID_ID3D12Fence = {0x0a753dcf, 0xc4d8, 0x4b91, {0x9d, 0x7f, 0x41, 0x5e, 0xfb, 0x14, 0x8c, 0x5f}};
static const GUID IID_ID3D12CommandSignature = {0xc36a797c, 0xec80, 0x4f0a, {0x89, 0x64, 0x02, 0xe1, 0x31, 0x74, 0x70, 0xb4}};

typedef UINT64 D3D12_GPU_VIRTUAL_ADDRESS;

constexpr UINT D3D12_DEFAULT_STENCIL_READ_MASK = 0xFF;
constexpr UINT D3D12_DEFAULT_STENCIL_WRITE_MASK = 0xFF;

constexpr UINT D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV = 0;
constexpr UINT D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER = 1;
constexpr UINT D3D12_DESCRIPTOR_HEAP_TYPE_RTV = 2;
constexpr UINT D3D12_DESCRIPTOR_HEAP_TYPE_DSV = 3;
constexpr UINT D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES = 4;
constexpr UINT D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE = 0x1;
constexpr UINT D3D12_DESCRIPTOR_HEAP_FLAG_NONE = 0x0;

constexpr UINT D3D12_RESOURCE_DIMENSION_UNKNOWN = 0;
constexpr UINT D3D12_RESOURCE_DIMENSION_BUFFER = 1;
constexpr UINT D3D12_RESOURCE_DIMENSION_TEXTURE1D = 2;
constexpr UINT D3D12_RESOURCE_DIMENSION_TEXTURE2D = 3;
constexpr UINT D3D12_RESOURCE_DIMENSION_TEXTURE3D = 4;

constexpr UINT D3D12_HEAP_TYPE_DEFAULT = 1;
constexpr UINT D3D12_HEAP_TYPE_UPLOAD = 2;
constexpr UINT D3D12_HEAP_TYPE_READBACK = 3;
constexpr UINT D3D12_HEAP_TYPE_CUSTOM = 4;

constexpr UINT D3D12_RESOURCE_FLAG_NONE = 0x0;
constexpr UINT D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET = 0x1;
constexpr UINT D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL = 0x2;
constexpr UINT D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS = 0x4;
constexpr UINT D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE = 0x8;

constexpr UINT D3D12_RESOURCE_STATE_COMMON = 0;
constexpr UINT D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1;
constexpr UINT D3D12_RESOURCE_STATE_INDEX_BUFFER = 0x2;
constexpr UINT D3D12_RESOURCE_STATE_RENDER_TARGET = 0x4;
constexpr UINT D3D12_RESOURCE_STATE_DEPTH_WRITE = 0x8;
constexpr UINT D3D12_RESOURCE_STATE_DEPTH_READ = 0x10;
constexpr UINT D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x20;
constexpr UINT D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x40;
constexpr UINT D3D12_RESOURCE_STATE_COPY_DEST = 0x400;
constexpr UINT D3D12_RESOURCE_STATE_COPY_SOURCE = 0x800;
constexpr UINT D3D12_RESOURCE_STATE_UNORDERED_ACCESS = 0x100;
constexpr UINT D3D12_RESOURCE_STATE_GENERIC_READ = 0x131;

constexpr UINT D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS = 0x20;
constexpr UINT D3D12_RESOURCE_FLAG_ALLOW_TILED_RESOURCES = 0x40;
constexpr UINT D3D12_RESOURCE_FLAG_ALLOW_SAMPLER_FEEDBACK = 0x80;

constexpr UINT D3D12_TEXTURE_LAYOUT_UNKNOWN = 0;
constexpr UINT D3D12_TEXTURE_LAYOUT_ROW_MAJOR = 1;
constexpr UINT D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE = 3;

constexpr UINT D3D12_TILE_MAPPING_FLAG_NONE = 0;
constexpr UINT D3D12_TILE_RANGE_FLAG_NONE = 0;
constexpr UINT D3D12_TILE_RANGE_FLAG_NULL = 1;
constexpr UINT D3D12_TILE_RANGE_FLAG_SKIP = 2;
constexpr UINT D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE = 4;

struct D3D12_TILE_REGION_SIZE { UINT NumTiles; UINT Width; UINT Height; UINT Depth; BOOL bUseBox; };

struct D3D12_TILED_RESOURCE_COORDINATE { UINT X; UINT Y; UINT Z; UINT Subresource; };

struct D3D12_TILE_RANGE_FLAGS { UINT Flags; };

struct D3D12_PACKED_MIP_INFO {
    UINT NumStandardMips;
    UINT NumPackedMips;
    UINT NumTilesForPackedMips;
    UINT StartTileIndexInOverallResource;
};

struct D3D12_TILE_SHAPE {
    UINT WidthInTexels;
    UINT HeightInTexels;
    UINT DepthInTexels;
};

struct D3D12_SUBRESOURCE_TILING {
    UINT WidthInTiles;
    UINT HeightInTiles;
    UINT DepthInTiles;
    UINT StartTileIndexInOverallResource;
    UINT NumTiles;
};

constexpr UINT D3D12_SAMPLER_FEEDBACK_TYPE_MIN_MIP_OPAQUE = 0;
constexpr UINT D3D12_SAMPLER_FEEDBACK_TYPE_MIP_REGION_USED_OPAQUE = 1;
constexpr UINT D3D12_SAMPLER_FEEDBACK_TYPE_MIN_MIP = 2;
constexpr UINT D3D12_SAMPLER_FEEDBACK_TYPE_MIP_REGION_USED = 3;

constexpr UINT64 D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES = 65536;

class ID3D12Resource;

struct D3D12_SAMPLER_FEEDBACK_DESC {
    ID3D12Resource* pFeedbackTexture;
    UINT FeedbackType;
};

constexpr UINT D3D12_COMMAND_LIST_TYPE_DIRECT = 0;
constexpr UINT D3D12_COMMAND_LIST_TYPE_BUNDLE = 1;
constexpr UINT D3D12_COMMAND_LIST_TYPE_COMPUTE = 2;
constexpr UINT D3D12_COMMAND_LIST_TYPE_COPY = 3;

constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT = 0;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE = 1;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE = 2;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH = 3;

constexpr UINT D3D12_BLEND_ZERO = 1;
constexpr UINT D3D12_BLEND_ONE = 2;
constexpr UINT D3D12_BLEND_SRC_COLOR = 3;
constexpr UINT D3D12_BLEND_INV_SRC_COLOR = 4;
constexpr UINT D3D12_BLEND_SRC_ALPHA = 5;
constexpr UINT D3D12_BLEND_INV_SRC_ALPHA = 6;
constexpr UINT D3D12_BLEND_DEST_ALPHA = 7;
constexpr UINT D3D12_BLEND_INV_DEST_ALPHA = 8;
constexpr UINT D3D12_BLEND_DEST_COLOR = 9;
constexpr UINT D3D12_BLEND_INV_DEST_COLOR = 10;
constexpr UINT D3D12_BLEND_BLEND_FACTOR = 14;
constexpr UINT D3D12_BLEND_OP_ADD = 1;
constexpr UINT D3D12_BLEND_OP_SUBTRACT = 2;
constexpr UINT D3D12_BLEND_OP_REV_SUBTRACT = 3;
constexpr UINT D3D12_BLEND_OP_MIN = 4;
constexpr UINT D3D12_BLEND_OP_MAX = 5;
constexpr UINT D3D12_COLOR_WRITE_ENABLE_ALL = 0xF;

constexpr UINT D3D12_FILL_MODE_SOLID = 3;
constexpr UINT D3D12_CULL_MODE_NONE = 1;
constexpr UINT D3D12_CULL_MODE_FRONT = 2;
constexpr UINT D3D12_CULL_MODE_BACK = 3;
constexpr UINT D3D12_COMPARISON_FUNC_NEVER = 1;
constexpr UINT D3D12_COMPARISON_FUNC_LESS = 2;
constexpr UINT D3D12_COMPARISON_FUNC_EQUAL = 3;
constexpr UINT D3D12_COMPARISON_FUNC_LESS_EQUAL = 4;
constexpr UINT D3D12_COMPARISON_FUNC_GREATER = 5;
constexpr UINT D3D12_COMPARISON_FUNC_NOT_EQUAL = 6;
constexpr UINT D3D12_COMPARISON_FUNC_GREATER_EQUAL = 7;
constexpr UINT D3D12_COMPARISON_FUNC_ALWAYS = 8;
constexpr UINT D3D12_DEPTH_WRITE_MASK_ZERO = 0;
constexpr UINT D3D12_DEPTH_WRITE_MASK_ALL = 1;

constexpr UINT D3D12_FILTER_MIN_MAG_MIP_POINT = 0;
constexpr UINT D3D12_FILTER_MIN_MAG_MIP_LINEAR = 0x55;
constexpr UINT D3D12_TEXTURE_ADDRESS_MODE_WRAP = 1;
constexpr UINT D3D12_TEXTURE_ADDRESS_MODE_MIRROR = 2;
constexpr UINT D3D12_TEXTURE_ADDRESS_MODE_CLAMP = 3;
constexpr UINT D3D12_TEXTURE_ADDRESS_MODE_BORDER = 4;

constexpr UINT D3D12_FENCE_FLAG_NONE = 0;

constexpr UINT D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE = 0;
constexpr UINT D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS = 1;
constexpr UINT D3D12_ROOT_PARAMETER_TYPE_CBV = 2;
constexpr UINT D3D12_ROOT_PARAMETER_TYPE_SRV = 3;
constexpr UINT D3D12_ROOT_PARAMETER_TYPE_UAV = 4;

constexpr UINT D3D12_ROOT_SIGNATURE_FLAG_NONE = 0x0;
constexpr UINT D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT = 0x1;

constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_POINTLIST = 1;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_LINELIST = 2;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_LINESTRIP = 3;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4;
constexpr UINT D3D12_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5;

constexpr UINT DXGI_FORMAT_UNKNOWN = 0;
constexpr UINT DXGI_FORMAT_R32G32B32A32_FLOAT = 2;
constexpr UINT DXGI_FORMAT_R32G32B32_FLOAT = 6;
constexpr UINT DXGI_FORMAT_R16G16B16A16_FLOAT = 10;
constexpr UINT DXGI_FORMAT_R32G32_FLOAT = 16;
constexpr UINT DXGI_FORMAT_R10G10B10A2_UNORM = 24;
constexpr UINT DXGI_FORMAT_R8G8B8A8_UNORM = 28;
constexpr UINT DXGI_FORMAT_B8G8R8A8_UNORM = 87;
constexpr UINT DXGI_FORMAT_R32_FLOAT = 41;
constexpr UINT DXGI_FORMAT_R16_FLOAT = 54;
constexpr UINT DXGI_FORMAT_R16G16_FLOAT = 34;
constexpr UINT DXGI_FORMAT_D24_UNORM_S8_UINT = 45;
constexpr UINT DXGI_FORMAT_D32_FLOAT = 40;
constexpr UINT DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20;
constexpr UINT DXGI_FORMAT_R16_UINT = 57;
constexpr UINT DXGI_FORMAT_R32_UINT = 42;
constexpr UINT DXGI_FORMAT_R8_UNORM = 61;
constexpr UINT DXGI_FORMAT_R8G8_UNORM = 49;

struct D3D12_HEAP_PROPERTIES { UINT Type; UINT CPUPageProperty; UINT MemoryPoolPreference; UINT CreationNodeMask; UINT VisibleNodeMask; };

struct D3D12_RESOURCE_DESC {
    UINT Dimension; UINT64 Alignment; UINT64 Width; UINT Height;
    UINT DepthOrArraySize; UINT MipLevels; UINT Format;
    struct { UINT Count; UINT Quality; } SampleDesc;
    UINT Layout; UINT Flags;
};

struct D3D12_DESCRIPTOR_HEAP_DESC { UINT Type; UINT NumDescriptors; UINT Flags; UINT NodeMask; };

struct D3D12_ROOT_CONSTANTS { UINT ShaderRegister; UINT RegisterSpace; UINT Num32BitValues; };
struct D3D12_ROOT_DESCRIPTOR { UINT ShaderRegister; UINT RegisterSpace; };
struct D3D12_DESCRIPTOR_RANGE { UINT RangeType; UINT NumDescriptors; UINT BaseShaderRegister; UINT RegisterSpace; UINT OffsetInDescriptorsFromTableStart; };

struct D3D12_ROOT_DESCRIPTOR_TABLE { UINT NumDescriptorRanges; const D3D12_DESCRIPTOR_RANGE* pDescriptorRanges; };

struct D3D12_ROOT_PARAMETER {
    UINT ParameterType;
    D3D12_ROOT_DESCRIPTOR_TABLE DescriptorTable;
    D3D12_ROOT_CONSTANTS Constants;
    D3D12_ROOT_DESCRIPTOR Descriptor;
    UINT ShaderVisibility;
};

struct D3D12_STATIC_SAMPLER_DESC {
    UINT Filter; UINT AddressU; UINT AddressV; UINT AddressW;
    FLOAT MipLODBias; UINT MaxAnisotropy; UINT ComparisonFunc;
    UINT BorderColor; FLOAT MinLOD; FLOAT MaxLOD;
    UINT ShaderRegister; UINT RegisterSpace; UINT ShaderVisibility;
};

struct D3D12_ROOT_SIGNATURE_DESC {
    UINT NumParameters; const D3D12_ROOT_PARAMETER* pParameters;
    UINT NumStaticSamplers; const D3D12_STATIC_SAMPLER_DESC* pStaticSamplers;
    UINT Flags;
};

struct D3D12_BLEND_DESC {
    BOOL AlphaToCoverageEnable; BOOL IndependentBlendEnable;
    struct RT { BOOL BlendEnable; BOOL LogicOpEnable; UINT SrcBlend; UINT DestBlend; UINT BlendOp; UINT SrcBlendAlpha; UINT DestBlendAlpha; UINT BlendOpAlpha; UINT LogicOp; UINT RenderTargetWriteMask; };
    RT RenderTarget[8];
};

struct D3D12_RASTERIZER_DESC {
    UINT FillMode; UINT CullMode; BOOL FrontCounterClockwise;
    INT DepthBias; FLOAT DepthBiasClamp; FLOAT SlopeScaledDepthBias;
    BOOL DepthClipEnable; BOOL MultisampleEnable; BOOL AntialiasedLineEnable;
    UINT ForcedSampleCount; BOOL ConservativeRaster;
};

struct D3D12_DEPTH_STENCIL_DESC {
    BOOL DepthEnable; UINT DepthWriteMask; UINT DepthFunc;
    BOOL StencilEnable; UINT StencilReadMask; UINT StencilWriteMask;
    struct Face { UINT StencilFailOp; UINT StencilDepthFailOp; UINT StencilPassOp; UINT StencilFunc; } FrontFace, BackFace;
};

struct D3D12_INPUT_ELEMENT_DESC {
    const char* SemanticName; UINT SemanticIndex; UINT Format;
    UINT InputSlot; UINT AlignedByteOffset; UINT InputSlotClass; UINT InstanceDataStepRate;
};

struct D3D12_VIEWPORT { FLOAT TopLeftX; FLOAT TopLeftY; FLOAT Width; FLOAT Height; FLOAT MinDepth; FLOAT MaxDepth; };
struct D3D12_RECT { LONG left; LONG top; LONG right; LONG bottom; };
struct D3D12_RANGE { SIZE_T Begin; SIZE_T End; };

struct D3D12_VERTEX_BUFFER_VIEW { D3D12_GPU_VIRTUAL_ADDRESS BufferLocation; UINT SizeInBytes; UINT StrideInBytes; };
struct D3D12_INDEX_BUFFER_VIEW { D3D12_GPU_VIRTUAL_ADDRESS BufferLocation; UINT SizeInBytes; UINT Format; };
struct D3D12_GPU_DESCRIPTOR_HANDLE { UINT64 ptr; };
struct D3D12_CPU_DESCRIPTOR_HANDLE { UINT64 ptr; };

struct D3D12_COMMAND_SIGNATURE_DESC { UINT ByteStride; UINT NumArgumentDescs; const void* pArgumentDescs; UINT NodeMask; };

class ID3D12Object : public IUnknown {
public:
    STDMETHOD(GetPrivateData)(const GUID& guid, UINT* pDataSize, void* pData) { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID& guid, UINT DataSize, const void* pData) { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID& guid, const IUnknown* pData) { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char* Name) { return S_OK; }
};

class ID3D12Fence : public ID3D12Object {
public:
    STDMETHOD(GetCompletedValue)(UINT64* pValue) PURE;
    STDMETHOD(SetEventOnCompletion)(UINT64 Value, HANDLE hEvent) PURE;
    STDMETHOD(Signal)(UINT64 Value) PURE;
};

class ID3D12RootSignature : public ID3D12Object {};
class ID3D12PipelineState : public ID3D12Object { public: virtual void* __metalRenderPipelineState() const { return nullptr; } virtual void* __metalComputePipelineState() const { return nullptr; } };
class ID3D12CommandSignature : public ID3D12Object {};
class ID3D12CommandAllocator : public ID3D12Object { public: STDMETHOD(Reset)() PURE; };

class ID3D12Resource : public ID3D12Object {
public:
    STDMETHOD(Map)(UINT Subresource, const D3D12_RANGE* pReadRange, void** ppData) PURE;
    STDMETHOD(Unmap)(UINT Subresource, const D3D12_RANGE* pWrittenRange) PURE;
    STDMETHOD(GetDesc)(D3D12_RESOURCE_DESC* pDesc) PURE;
    STDMETHOD(GetGPUVirtualAddress)(D3D12_GPU_VIRTUAL_ADDRESS* pAddress) PURE;
    virtual void* __metalBufferPtr() const { return nullptr; }
    virtual void* __metalTexturePtr() const { return nullptr; }
    virtual UINT __getResourceState() const { return D3D12_RESOURCE_STATE_COMMON; }
    virtual void __setResourceState(UINT state) {}
};

struct D3D12_RESOURCE_BARRIER { UINT Type; UINT Flags; UINT StateBefore; UINT StateAfter; ID3D12Resource* pResource; };

class ID3D12DescriptorHeap : public ID3D12Object {
public:
    virtual D3D12_CPU_DESCRIPTOR_HANDLE __getCPUDescriptorHandleForHeapStart() { return {0}; }
    virtual D3D12_GPU_DESCRIPTOR_HANDLE __getGPUDescriptorHandleForHeapStart() { return {0}; }
    virtual UINT __getDescriptorCount() const { return 0; }
    virtual UINT __getHeapType() const { return 0; }
};

class ID3D12CommandList : public ID3D12Object {
public:
    virtual void __execute(void* metalDevice) {}
};

class ID3D12CommandQueue : public ID3D12Object {
public:
    STDMETHOD(ExecuteCommandLists)(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) PURE;
    STDMETHOD(Signal)(ID3D12Fence* pFence, UINT64 Value) PURE;
    STDMETHOD(Wait)(ID3D12Fence* pFence, UINT64 Value) PURE;
};

struct D3D12_GRAPHICS_PIPELINE_STATE_DESC {
    ID3D12RootSignature* pRootSignature;
    const void* VS; SIZE_T VSsize;
    const void* PS; SIZE_T PSsize;
    const void* GS; SIZE_T GSsize;
    const void* DS; SIZE_T DSsize;
    const void* HS; SIZE_T HSsize;
    UINT StreamOutput;
    D3D12_BLEND_DESC BlendState;
    UINT SampleMask;
    D3D12_RASTERIZER_DESC RasterizerState;
    D3D12_DEPTH_STENCIL_DESC DepthStencilState;
    const D3D12_INPUT_ELEMENT_DESC* InputLayout;
    UINT NumInputElements;
    UINT PrimitiveTopologyType;
    UINT NumRenderTargets;
    UINT RTVFormats[8];
    UINT DSVFormat;
    struct { UINT Count; UINT Quality; } SampleDesc;
};

class ID3D12GraphicsCommandList : public ID3D12CommandList {
public:
    STDMETHOD(Close)() PURE;
    STDMETHOD(Reset)(ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState) PURE;
    STDMETHOD(IASetPrimitiveTopology)(UINT PrimitiveTopology) PURE;
    STDMETHOD(IASetVertexBuffers)(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW* pViews) PURE;
    STDMETHOD(IASetIndexBuffer)(const D3D12_INDEX_BUFFER_VIEW* pView) PURE;
    STDMETHOD(SetPipelineState)(ID3D12PipelineState* pPipelineState) PURE;
    STDMETHOD(SetGraphicsRootSignature)(ID3D12RootSignature* pRootSignature) PURE;
    STDMETHOD(SetGraphicsRootDescriptorTable)(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) PURE;
    STDMETHOD(SetGraphicsRootConstantBufferView)(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) PURE;
    STDMETHOD(SetGraphicsRootShaderResourceView)(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) PURE;
    STDMETHOD(SetGraphicsRootUnorderedAccessView)(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) PURE;
    STDMETHOD(SetGraphicsRoot32BitConstants)(UINT RootParameterIndex, UINT Num32BitValues, const void* pData, UINT DestOffsetIn32BitValues) PURE;
    STDMETHOD(OMSetRenderTargets)(UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor) PURE;
    STDMETHOD(OMSetStencilRef)(UINT StencilRef) PURE;
    STDMETHOD(OMSetBlendFactor)(const FLOAT BlendFactor[4]) PURE;
    STDMETHOD(RSSetViewports)(UINT NumViewports, const D3D12_VIEWPORT* pViewports) PURE;
    STDMETHOD(RSSetScissorRects)(UINT NumRects, const D3D12_RECT* pRects) PURE;
    STDMETHOD(ClearRenderTargetView)(D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4], UINT NumRects, const D3D12_RECT* pRects) PURE;
    STDMETHOD(ClearDepthStencilView)(D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT* pRects) PURE;
    STDMETHOD(DrawInstanced)(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) PURE;
    STDMETHOD(DrawIndexedInstanced)(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) PURE;
    STDMETHOD(Dispatch)(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) PURE;
    STDMETHOD(CopyResource)(ID3D12Resource* pDstResource, ID3D12Resource* pSrcResource) PURE;
    STDMETHOD(CopyBufferRegion)(ID3D12Resource* pDstResource, UINT64 DstOffset, ID3D12Resource* pSrcResource, UINT64 SrcOffset, UINT64 NumBytes) PURE;
    STDMETHOD(CopyTextureRegion)(const void* pDst, UINT DstX, UINT DstY, UINT DstZ, ID3D12Resource* pSrcResource, UINT SrcSubresource, const void* pSrcBox) PURE;
    STDMETHOD(ResourceBarrier)(UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) PURE;
    STDMETHOD(SetDescriptorHeaps)(UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps) PURE;
    STDMETHOD(IASetInputLayout)(UINT NumElements, const D3D12_INPUT_ELEMENT_DESC* pInputElementDescs) PURE;
    STDMETHOD(Map)(ID3D12Resource* pResource, UINT Subresource, const D3D12_RANGE* pReadRange, void** ppData) PURE;
    STDMETHOD(Unmap)(ID3D12Resource* pResource, UINT Subresource, const D3D12_RANGE* pWrittenRange) PURE;
    STDMETHOD(ExecuteIndirect)(ID3D12CommandSignature* pCommandSignature, UINT MaxCommandCount, ID3D12Resource* pCommandBuffer, UINT64 CommandBufferOffset, ID3D12Resource* pCountBuffer, UINT64 CountBufferOffset) PURE;

    STDMETHOD(CopyTiles)(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate, const D3D12_TILE_REGION_SIZE* pTileRegionSize, ID3D12Resource* pBuffer, UINT64 BufferStartOffset, UINT Flags) PURE;
};

class ID3D12Device : public ID3D12Object {
public:
    STDMETHOD(CreateCommandQueue)(const void* pDesc, REFIID riid, void** ppCommandQueue) PURE;
    STDMETHOD(CreateCommandAllocator)(UINT type, REFIID riid, void** ppCommandAllocator) PURE;
    STDMETHOD(CreateCommandList)(UINT nodeMask, UINT type, ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) PURE;
    STDMETHOD(CreateRenderTargetView)(ID3D12Resource* pResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) PURE;
    STDMETHOD(CreateDepthStencilView)(ID3D12Resource* pResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) PURE;
    STDMETHOD(CreateShaderResourceView)(ID3D12Resource* pResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) PURE;
    STDMETHOD(CreateUnorderedAccessView)(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) PURE;
    STDMETHOD(CreateConstantBufferView)(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) PURE;
    STDMETHOD(CreateCommittedResource)(const D3D12_HEAP_PROPERTIES* pHeapProperties, UINT HeapFlags, const D3D12_RESOURCE_DESC* pDesc, UINT InitialResourceState, const void* pOptimizedClearValue, REFIID riid, void** ppvResource) PURE;
    STDMETHOD(CreateDescriptorHeap)(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvDescriptorHeap) PURE;
    STDMETHOD(CreateRootSignature)(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) PURE;
    STDMETHOD(CreateGraphicsPipelineState)(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) PURE;
    STDMETHOD(CreateComputePipelineState)(const void* pDesc, REFIID riid, void** ppPipelineState) PURE;
    STDMETHOD(CreateFence)(UINT64 InitialValue, UINT Flags, REFIID riid, void** ppFence) PURE;
    STDMETHOD(CreateCommandSignature)(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppCommandSignature) PURE;
    STDMETHOD(CreateSampler)(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) PURE;
    STDMETHOD_(UINT, GetDescriptorHandleIncrementSize)(UINT DescriptorHeapType) PURE;

    STDMETHOD(ReserveTiles)(ID3D12Resource* pTiledResource, UINT NumTileRegions, const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinates, const D3D12_TILE_REGION_SIZE* pTileRegionSizes, BOOL bSingleTile) PURE;
    STDMETHOD(GetResourceTiling)(ID3D12Resource* pTiledResource, UINT* pNumTilesForResource, D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips, UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips) PURE;
    STDMETHOD_(UINT64, GetTiledResourceSize)(UINT64 Width, UINT Height, UINT DepthOrArraySize, UINT Format, UINT MipLevels) PURE;
    STDMETHOD(CreateSamplerFeedback)(ID3D12Resource* pTargetResource, UINT FeedbackType, REFIID riid, void** ppFeedbackResource) PURE;
    STDMETHOD(WriteSamplerFeedback)(ID3D12Resource* pTargetResource, ID3D12Resource* pFeedbackResource, UINT FeedbackType) PURE;
    STDMETHOD(ResolveSamplerFeedback)(ID3D12Resource* pFeedbackResource, ID3D12Resource* pDestResource) PURE;
};
