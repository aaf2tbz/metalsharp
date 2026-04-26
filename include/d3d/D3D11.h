#pragma once

#include <metalsharp/Platform.h>

typedef UINT DXGI_FORMAT;

static const GUID IID_ID3D11Device = {0x2e8f6e0c, 0x3e86, 0x4a63, {0x9e, 0xa5, 0x7b, 0x52, 0xd9, 0x5e, 0x08, 0xc1}};
static const GUID IID_ID3D11DeviceContext = {0xaec81fb3, 0x4e01, 0x4dfe, {0xa0, 0xa3, 0xd0, 0xa9, 0x75, 0x7d, 0x88, 0x6b}};
static const GUID IID_ID3D11DeviceChild = {0xdb6f6ddb, 0xac77, 0x4e88, {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
static const GUID IID_ID3D11Buffer = {0x4eddea9b, 0x3239, 0x4e14, {0x8c, 0xb4, 0x4e, 0xe4, 0x3d, 0x72, 0x0d, 0x50}};
static const GUID IID_ID3D11Texture2D = {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};
static const GUID IID_ID3D11RenderTargetView = {0xdfdba067, 0x0b8d, 0x4865, {0x87, 0x5b, 0x5d, 0x1e, 0x9c, 0x88, 0x4e, 0x36}};
static const GUID IID_ID3D11DepthStencilView = {0x9fdac92a, 0x187f, 0x4f1e, {0xa7, 0x24, 0x55, 0x8c, 0x1c, 0x8b, 0x2b, 0x49}};
static const GUID IID_ID3D11ShaderResourceView = {0xb0e06fe0, 0x8192, 0x4e05, {0xa9, 0x81, 0x22, 0x1a, 0x45, 0x6f, 0x10, 0x73}};
static const GUID IID_ID3D11InputLayout = {0xe0aac9bd, 0xd237, 0x4d80, {0xa8, 0x2f, 0x6e, 0x88, 0x6c, 0x8e, 0x54, 0x83}};
static const GUID IID_ID3D11VertexShader = {0xe4e6e4c1, 0x3a30, 0x4a65, {0x9c, 0x41, 0x82, 0xbc, 0x91, 0x5b, 0x62, 0x54}};
static const GUID IID_ID3D11PixelShader = {0xea82e2e4, 0x4e41, 0x4e80, {0xa8, 0xf7, 0x77, 0x81, 0x1f, 0xbe, 0x6c, 0x82}};
static const GUID IID_ID3D11Resource = {0xdb8d2769, 0x6ef3, 0x44ee, {0x8b, 0x8e, 0x10, 0x8f, 0x33, 0x36, 0x6d, 0xe0}};
static const GUID IID_ID3D11View = {0x839d1216, 0xbb2e, 0x412b, {0xb7, 0xf4, 0xa9, 0xdb, 0xeb, 0xe0, 0x8e, 0xd1}};
static const GUID IID_ID3D11SamplerState = {0x38e4f8a8, 0xcc78, 0x4a48, {0x84, 0x24, 0x66, 0x72, 0x38, 0x63, 0x74, 0x4b}};
static const GUID IID_ID3D11RasterizerState = {0x1e9c9db7, 0x21b3, 0x4a30, {0x84, 0xc1, 0x3a, 0xcf, 0x16, 0xd2, 0x61, 0x8b}};
static const GUID IID_ID3D11DepthStencilState = {0x431583c5, 0x1c9d, 0x4e72, {0x90, 0xa7, 0x3e, 0x42, 0x03, 0xfb, 0x9f, 0xf3}};
static const GUID IID_ID3D11BlendState = {0x46e71f5a, 0x2501, 0x41a2, {0xac, 0xfb, 0x7a, 0x7f, 0x09, 0xe2, 0x81, 0x7b}};
static const GUID IID_ID3D11CommandList = {0xa24bc4d1, 0x762e, 0x4c3e, {0x82, 0x27, 0x09, 0x3d, 0x26, 0x8e, 0x4a, 0xbf}};

class ID3D11Device;
class ID3D11DeviceContext;
class ID3D11ClassInstance;
class ID3D11ClassLinkage;
class ID3D11SamplerState;
class ID3D11RasterizerState;
class ID3D11DepthStencilState;
class ID3D11BlendState;
class ID3D11UnorderedAccessView;
class ID3D11Query;
class ID3D11Predicate;

constexpr UINT D3D11_BIND_VERTEX_BUFFER    = 0x1;
constexpr UINT D3D11_BIND_INDEX_BUFFER     = 0x2;
constexpr UINT D3D11_BIND_CONSTANT_BUFFER  = 0x4;
constexpr UINT D3D11_BIND_SHADER_RESOURCE  = 0x8;
constexpr UINT D3D11_BIND_STREAM_OUTPUT    = 0x10;
constexpr UINT D3D11_BIND_RENDER_TARGET    = 0x20;
constexpr UINT D3D11_BIND_DEPTH_STENCIL    = 0x40;
constexpr UINT D3D11_BIND_UNORDERED_ACCESS = 0x80;

constexpr UINT D3D11_USAGE_DEFAULT   = 0;
constexpr UINT D3D11_USAGE_IMMUTABLE = 1;
constexpr UINT D3D11_USAGE_DYNAMIC   = 2;
constexpr UINT D3D11_USAGE_STAGING   = 3;

constexpr UINT D3D11_CPU_ACCESS_READ  = 0x10000;
constexpr UINT D3D11_CPU_ACCESS_WRITE = 0x20000;

constexpr UINT D3D11_MAP_READ              = 1;
constexpr UINT D3D11_MAP_WRITE             = 2;
constexpr UINT D3D11_MAP_READ_WRITE        = 3;
constexpr UINT D3D11_MAP_WRITE_DISCARD     = 4;
constexpr UINT D3D11_MAP_WRITE_NO_OVERWRITE = 5;

constexpr UINT D3D11_CLEAR_DEPTH   = 0x1;
constexpr UINT D3D11_CLEAR_STENCIL = 0x2;

constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_POINTLIST     = 1;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_LINELIST      = 2;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP     = 3;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST  = 4;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ         = 10;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ        = 11;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ     = 12;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ    = 13;

constexpr DXGI_FORMAT DXGI_FORMAT_UNKNOWN              = 0;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32B32A32_FLOAT   = 2;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32B32_FLOAT      = 6;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16B16A16_FLOAT   = 10;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32_FLOAT         = 16;
constexpr DXGI_FORMAT DXGI_FORMAT_R10G10B10A2_UNORM    = 24;
constexpr DXGI_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM       = 28;
constexpr DXGI_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM_SRGB  = 29;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16_FLOAT         = 34;
constexpr DXGI_FORMAT DXGI_FORMAT_R32_FLOAT            = 41;
constexpr DXGI_FORMAT DXGI_FORMAT_R8G8_UNORM           = 49;
constexpr DXGI_FORMAT DXGI_FORMAT_R16_FLOAT            = 56;
constexpr DXGI_FORMAT DXGI_FORMAT_R16_UNORM            = 57;
constexpr DXGI_FORMAT DXGI_FORMAT_R8_UNORM             = 61;
constexpr DXGI_FORMAT DXGI_FORMAT_R8G8_B8G8_UNORM      = 68;
constexpr DXGI_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM       = 87;
constexpr DXGI_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM_SRGB  = 91;
constexpr DXGI_FORMAT DXGI_FORMAT_D32_FLOAT            = 40;
constexpr DXGI_FORMAT DXGI_FORMAT_D24_UNORM_S8_UINT    = 45;
constexpr DXGI_FORMAT DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16B16A16_UNORM   = 11;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16B16A16_UINT    = 14;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32_UINT          = 17;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32B32_UINT       = 7;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32B32A32_UINT    = 3;
constexpr DXGI_FORMAT DXGI_FORMAT_R16_UINT             = 57;
constexpr DXGI_FORMAT DXGI_FORMAT_R32_UINT             = 42;
constexpr DXGI_FORMAT DXGI_FORMAT_BC1_UNORM            = 71;
constexpr DXGI_FORMAT DXGI_FORMAT_BC2_UNORM            = 74;
constexpr DXGI_FORMAT DXGI_FORMAT_BC3_UNORM            = 77;
constexpr DXGI_FORMAT DXGI_FORMAT_BC4_UNORM            = 80;
constexpr DXGI_FORMAT DXGI_FORMAT_BC5_UNORM            = 83;
constexpr DXGI_FORMAT DXGI_FORMAT_BC6H_SF16            = 96;
constexpr DXGI_FORMAT DXGI_FORMAT_BC7_UNORM            = 98;

class MIDL_INTERFACE("db6f6ddb-ac77-4e88-8253-819df9bbf140")
ID3D11DeviceChild : public IUnknown {
public:
    STDMETHOD(GetDevice)(THIS_ struct ID3D11Device** ppDevice) PURE;
    STDMETHOD(GetPrivateData)(THIS_ const GUID& guid, UINT* pDataSize, void* pData) PURE;
    STDMETHOD(SetPrivateData)(THIS_ const GUID& guid, UINT DataSize, const void* pData) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ const GUID& guid, const IUnknown* pData) PURE;
};

struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
};

struct D3D11_SUBRESOURCE_DATA {
    const void* pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
};

struct DXGI_SAMPLE_DESC {
    UINT Count;
    UINT Quality;
};

struct D3D11_TEXTURE2D_DESC {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};

struct D3D11_TEXTURE1D_DESC {
    UINT Width;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};

struct D3D11_TEXTURE3D_DESC {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    DXGI_FORMAT Format;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};

struct D3D11_VIEWPORT {
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    UINT Width;
    UINT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
};

constexpr UINT D3D11_BLEND_ZERO            = 1;
constexpr UINT D3D11_BLEND_ONE             = 2;
constexpr UINT D3D11_BLEND_SRC_COLOR       = 3;
constexpr UINT D3D11_BLEND_INV_SRC_COLOR   = 4;
constexpr UINT D3D11_BLEND_SRC_ALPHA       = 5;
constexpr UINT D3D11_BLEND_INV_SRC_ALPHA   = 6;
constexpr UINT D3D11_BLEND_DEST_ALPHA      = 7;
constexpr UINT D3D11_BLEND_INV_DEST_ALPHA  = 8;
constexpr UINT D3D11_BLEND_DEST_COLOR      = 9;
constexpr UINT D3D11_BLEND_INV_DEST_COLOR  = 10;
constexpr UINT D3D11_BLEND_SRC_ALPHA_SAT   = 11;
constexpr UINT D3D11_BLEND_BLEND_FACTOR    = 14;
constexpr UINT D3D11_BLEND_INV_BLEND_FACTOR = 15;

constexpr UINT D3D11_BLEND_OP_ADD          = 1;
constexpr UINT D3D11_BLEND_OP_SUBTRACT     = 2;
constexpr UINT D3D11_BLEND_OP_REV_SUBTRACT = 3;
constexpr UINT D3D11_BLEND_OP_MIN          = 4;
constexpr UINT D3D11_BLEND_OP_MAX          = 5;

constexpr UINT D3D11_COLOR_WRITE_ENABLE_RED   = 1;
constexpr UINT D3D11_COLOR_WRITE_ENABLE_GREEN = 2;
constexpr UINT D3D11_COLOR_WRITE_ENABLE_BLUE  = 4;
constexpr UINT D3D11_COLOR_WRITE_ENABLE_ALPHA = 8;
constexpr UINT D3D11_COLOR_WRITE_ENABLE_ALL   = 15;

struct D3D11_RENDER_TARGET_BLEND_DESC {
    INT BlendEnable;
    UINT SrcBlend;
    UINT DestBlend;
    UINT BlendOp;
    UINT SrcBlendAlpha;
    UINT DestBlendAlpha;
    UINT BlendOpAlpha;
    UINT RenderTargetWriteMask;
};

struct D3D11_BLEND_DESC {
    INT AlphaToCoverageEnable;
    INT IndependentBlendEnable;
    D3D11_RENDER_TARGET_BLEND_DESC RenderTarget[8];
};

constexpr UINT D3D11_FILL_WIREFRAME = 2;
constexpr UINT D3D11_FILL_SOLID     = 3;

constexpr UINT D3D11_CULL_NONE  = 1;
constexpr UINT D3D11_CULL_FRONT = 2;
constexpr UINT D3D11_CULL_BACK  = 3;

struct D3D11_RASTERIZER_DESC {
    UINT FillMode;
    UINT CullMode;
    INT FrontCounterClockwise;
    INT DepthBias;
    FLOAT DepthBiasClamp;
    FLOAT SlopeScaledDepthBias;
    INT DepthClipEnable;
    INT ScissorEnable;
    INT MultisampleEnable;
    INT AntialiasedLineEnable;
};

constexpr UINT D3D11_COMPARISON_NEVER         = 1;
constexpr UINT D3D11_COMPARISON_LESS          = 2;
constexpr UINT D3D11_COMPARISON_EQUAL         = 3;
constexpr UINT D3D11_COMPARISON_LESS_EQUAL    = 4;
constexpr UINT D3D11_COMPARISON_GREATER       = 5;
constexpr UINT D3D11_COMPARISON_NOT_EQUAL     = 6;
constexpr UINT D3D11_COMPARISON_GREATER_EQUAL = 7;
constexpr UINT D3D11_COMPARISON_ALWAYS        = 8;

constexpr UINT D3D11_STENCIL_OP_KEEP     = 1;
constexpr UINT D3D11_STENCIL_OP_ZERO     = 2;
constexpr UINT D3D11_STENCIL_OP_REPLACE  = 3;
constexpr UINT D3D11_STENCIL_OP_INCR_SAT = 4;
constexpr UINT D3D11_STENCIL_OP_DECR_SAT = 5;
constexpr UINT D3D11_STENCIL_OP_INVERT   = 6;
constexpr UINT D3D11_STENCIL_OP_INCR     = 7;
constexpr UINT D3D11_STENCIL_OP_DECR     = 8;

struct D3D11_DEPTH_STENCILOP_DESC {
    UINT StencilFailOp;
    UINT StencilDepthFailOp;
    UINT StencilPassOp;
    UINT StencilFunc;
};

struct D3D11_DEPTH_STENCIL_DESC {
    INT DepthEnable;
    UINT DepthWriteMask;
    UINT DepthFunc;
    INT StencilEnable;
    UINT StencilReadMask;
    UINT StencilWriteMask;
    D3D11_DEPTH_STENCILOP_DESC FrontFace;
    D3D11_DEPTH_STENCILOP_DESC BackFace;
};

constexpr UINT D3D11_DEPTH_WRITE_MASK_ALL = 1;
constexpr UINT D3D11_DEPTH_WRITE_MASK_ZERO = 0;

constexpr UINT D3D11_FILTER_MIN_MAG_MIP_POINT          = 0;
constexpr UINT D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR   = 0x1;
constexpr UINT D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x4;
constexpr UINT D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR   = 0x5;
constexpr UINT D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT   = 0x10;
constexpr UINT D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11;
constexpr UINT D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT   = 0x14;
constexpr UINT D3D11_FILTER_MIN_MAG_MIP_LINEAR          = 0x15;
constexpr UINT D3D11_FILTER_ANISOTROPIC                 = 0x55;
constexpr UINT D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT = 0x80;
constexpr UINT D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0x95;

constexpr UINT D3D11_TEXTURE_ADDRESS_WRAP       = 1;
constexpr UINT D3D11_TEXTURE_ADDRESS_MIRROR     = 2;
constexpr UINT D3D11_TEXTURE_ADDRESS_CLAMP      = 3;
constexpr UINT D3D11_TEXTURE_ADDRESS_BORDER     = 4;
constexpr UINT D3D11_TEXTURE_ADDRESS_MIRROR_ONCE = 5;

struct D3D11_SAMPLER_DESC {
    UINT Filter;
    UINT AddressU;
    UINT AddressV;
    UINT AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    UINT ComparisonFunc;
    FLOAT BorderColor[4];
    FLOAT MinLOD;
    FLOAT MaxLOD;
};

constexpr UINT D3D11_INPUT_PER_VERTEX_DATA   = 0;
constexpr UINT D3D11_INPUT_PER_INSTANCE_DATA = 1;

struct D3D11_INPUT_ELEMENT_DESC {
    const char* SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    UINT InputSlotClass;
    UINT InstanceDataStepRate;
};

struct D3D11_RENDER_TARGET_VIEW_DESC {
    DXGI_FORMAT Format;
    UINT ViewDimension;
    union {
        struct { UINT BufferElementOffset; UINT BufferElementWidth; } Buffer;
        struct { UINT MipSlice; } Texture1D;
        struct { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } Texture1DArray;
        struct { UINT MipSlice; } Texture2D;
        struct { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } Texture2DArray;
        struct { UINT MipSlice; } Texture2DMS;
        struct { UINT FirstArraySlice; UINT ArraySize; } Texture2DMSArray;
        struct { UINT MipSlice; UINT FirstWSlice; UINT WSize; } Texture3D;
    };
};

struct D3D11_DEPTH_STENCIL_VIEW_DESC {
    DXGI_FORMAT Format;
    UINT ViewDimension;
    UINT Flags;
    union {
        struct { UINT MipSlice; } Texture1D;
        struct { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } Texture1DArray;
        struct { UINT MipSlice; } Texture2D;
        struct { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } Texture2DArray;
        struct { UINT UnusedFlags_NothingToDefine; } Texture2DMS;
        struct { UINT FirstArraySlice; UINT ArraySize; } Texture2DMSArray;
    };
};

struct D3D11_SHADER_RESOURCE_VIEW_DESC {
    DXGI_FORMAT Format;
    UINT ViewDimension;
    union {
        struct { UINT ElementOffset; UINT ElementWidth; } Buffer;
        struct { UINT FirstElement; UINT NumElements; UINT Flags; } BufferEx;
        struct { UINT MostDetailedMip; UINT MipLevels; } Texture1D;
        struct { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; } Texture1DArray;
        struct { UINT MostDetailedMip; UINT MipLevels; } Texture2D;
        struct { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; } Texture2DArray;
        struct { UINT Unused; } Texture2DMS;
        struct { UINT FirstArraySlice; UINT ArraySize; } Texture2DMSArray;
        struct { UINT MostDetailedMip; UINT MipLevels; } Texture3D;
        struct { UINT First2DArrayFace; UINT NumCubes; UINT MostDetailedMip; UINT MipLevels; } TextureCubeArray;
    };
};

constexpr UINT D3D11_UAV_DIMENSION_UNKNOWN       = 0;
constexpr UINT D3D11_UAV_DIMENSION_BUFFER         = 1;
constexpr UINT D3D11_UAV_DIMENSION_TEXTURE1D      = 2;
constexpr UINT D3D11_UAV_DIMENSION_TEXTURE1DARRAY = 3;
constexpr UINT D3D11_UAV_DIMENSION_TEXTURE2D      = 4;
constexpr UINT D3D11_UAV_DIMENSION_TEXTURE2DARRAY = 5;
constexpr UINT D3D11_UAV_DIMENSION_TEXTURE3D      = 8;

constexpr UINT D3D11_BUFFER_UAV_FLAG_RAW = 0x1;

struct D3D11_UNORDERED_ACCESS_VIEW_DESC {
    DXGI_FORMAT Format;
    UINT ViewDimension;
    union {
        struct { UINT FirstElement; UINT NumElements; UINT Flags; } Buffer;
        struct { UINT MipSlice; } Texture1D;
        struct { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } Texture1DArray;
        struct { UINT MipSlice; } Texture2D;
        struct { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; } Texture2DArray;
        struct { UINT MipSlice; UINT FirstWSlice; UINT WSize; } Texture3D;
    };
};

struct D3D11_MAPPED_SUBRESOURCE {
    void* pData;
    UINT RowPitch;
    UINT DepthPitch;
};

constexpr UINT D3D11_RESOURCE_DIMENSION_UNKNOWN  = 0;
constexpr UINT D3D11_RESOURCE_DIMENSION_BUFFER   = 1;
constexpr UINT D3D11_RESOURCE_DIMENSION_TEXTURE1D = 2;
constexpr UINT D3D11_RESOURCE_DIMENSION_TEXTURE2D = 3;
constexpr UINT D3D11_RESOURCE_DIMENSION_TEXTURE3D = 4;

class ID3D11Resource : public ID3D11DeviceChild {
public:
    STDMETHOD(GetType)(THIS_ UINT* pResourceDimension) PURE;
    STDMETHOD(SetEvictionPriority)(THIS_ UINT EvictionPriority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
    virtual void* __metalBufferPtr() const { return nullptr; }
    virtual void* __metalTexturePtr() const { return nullptr; }
    virtual UINT __getResourceDimension() const { return D3D11_RESOURCE_DIMENSION_UNKNOWN; }
    virtual UINT __getCPUAccessFlags() const { return 0; }
    virtual UINT __getUsage() const { return D3D11_USAGE_DEFAULT; }
};

class ID3D11Buffer : public ID3D11Resource {
};

class ID3D11Texture1D : public ID3D11Resource {
public:
    virtual void GetDesc(struct D3D11_TEXTURE1D_DESC* pDesc) = 0;
};

class ID3D11Texture2D : public ID3D11Resource {
public:
    virtual void GetDesc(struct D3D11_TEXTURE2D_DESC* pDesc) = 0;
};

class ID3D11Texture3D : public ID3D11Resource {
public:
    virtual void GetDesc(struct D3D11_TEXTURE3D_DESC* pDesc) = 0;
};

class ID3D11View : public ID3D11DeviceChild {
public:
    STDMETHOD(GetResource)(THIS_ ID3D11Resource** ppResource) PURE;
    virtual void* __metalTexturePtr() const { return nullptr; }
    virtual void* __metalBufferPtr() const { return nullptr; }
};

class ID3D11RenderTargetView : public ID3D11View {
};

class ID3D11DepthStencilView : public ID3D11View {
};

class ID3D11ShaderResourceView : public ID3D11View {
};

class ID3D11UnorderedAccessView : public ID3D11View {
};

class ID3D11InputLayout : public ID3D11DeviceChild {
public:
    virtual UINT __getNumElements() const { return 0; }
    virtual const void* __getElements() const { return nullptr; }
};

class ID3D11VertexShader : public ID3D11DeviceChild {
public:
    virtual void* __metalVertexFunction() const { return nullptr; }
};

class ID3D11PixelShader : public ID3D11DeviceChild {
public:
    virtual void* __metalFragmentFunction() const { return nullptr; }
};

class ID3D11GeometryShader : public ID3D11DeviceChild {
};

class ID3D11ComputeShader : public ID3D11DeviceChild {
};

class ID3D11HullShader : public ID3D11DeviceChild {
};

class ID3D11DomainShader : public ID3D11DeviceChild {
};

class ID3D11SamplerState : public ID3D11DeviceChild {
public:
    virtual void* __metalSamplerState() const { return nullptr; }
};

class ID3D11RasterizerState : public ID3D11DeviceChild {
public:
    virtual INT __getCullMode() const { return D3D11_CULL_BACK; }
    virtual INT __getFillMode() const { return D3D11_FILL_SOLID; }
    virtual INT __getDepthClipEnable() const { return TRUE; }
    virtual INT __getFrontCCW() const { return FALSE; }
    virtual INT __getDepthBias() const { return 0; }
    virtual FLOAT __getSlopeScaledDepthBias() const { return 0.0f; }
};

class ID3D11DepthStencilState : public ID3D11DeviceChild {
public:
    virtual INT __getDepthEnable() const { return TRUE; }
    virtual UINT __getDepthWriteMask() const { return D3D11_DEPTH_WRITE_MASK_ALL; }
    virtual UINT __getDepthFunc() const { return D3D11_COMPARISON_LESS; }
    virtual INT __getStencilEnable() const { return FALSE; }
    virtual UINT __getStencilReadMask() const { return 0xFF; }
    virtual UINT __getStencilWriteMask() const { return 0xFF; }
};

class ID3D11BlendState : public ID3D11DeviceChild {
public:
    virtual INT __getBlendEnable(UINT rtIndex) const { return 0; }
    virtual UINT __getSrcBlend(UINT rtIndex) const { return D3D11_BLEND_ONE; }
    virtual UINT __getDestBlend(UINT rtIndex) const { return D3D11_BLEND_ZERO; }
    virtual UINT __getBlendOp(UINT rtIndex) const { return D3D11_BLEND_OP_ADD; }
    virtual UINT __getSrcBlendAlpha(UINT rtIndex) const { return D3D11_BLEND_ONE; }
    virtual UINT __getDestBlendAlpha(UINT rtIndex) const { return D3D11_BLEND_ZERO; }
    virtual UINT __getBlendOpAlpha(UINT rtIndex) const { return D3D11_BLEND_OP_ADD; }
    virtual UINT __getRenderTargetWriteMask(UINT rtIndex) const { return D3D11_COLOR_WRITE_ENABLE_ALL; }
    virtual INT __getAlphaToCoverageEnable() const { return FALSE; }
};

constexpr UINT D3D11_QUERY_EVENT                   = 0;
constexpr UINT D3D11_QUERY_OCCLUSION               = 1;
constexpr UINT D3D11_QUERY_TIMESTAMP               = 2;
constexpr UINT D3D11_QUERY_TIMESTAMP_DISJOINT      = 3;
constexpr UINT D3D11_QUERY_PIPELINE_STATISTICS     = 4;
constexpr UINT D3D11_QUERY_OCCLUSION_PREDICATE     = 5;
constexpr UINT D3D11_QUERY_SO_STATISTICS           = 6;
constexpr UINT D3D11_QUERY_SO_OVERFLOW_PREDICATE   = 7;

struct D3D11_QUERY_DESC {
    UINT Query;
    UINT MiscFlags;
};

class ID3D11Query : public ID3D11DeviceChild {
public:
    virtual void GetDesc(D3D11_QUERY_DESC* pDesc) = 0;
    virtual void* __metalBufferPtr() const { return nullptr; }
    virtual UINT __getQueryType() const { return D3D11_QUERY_EVENT; }
};

class ID3D11Predicate : public ID3D11DeviceChild {
};

class ID3D11CommandList : public ID3D11DeviceChild {
public:
    STDMETHOD_(UINT, GetContextFlags)() { return 0; }
};

class MIDL_INTERFACE("aec81fb3-4e01-4dfe-a0a3-d0a9757d886b")
ID3D11DeviceContext : public ID3D11DeviceChild {
public:
    STDMETHOD(IASetInputLayout)(THIS_ ID3D11InputLayout* pInputLayout) PURE;
    STDMETHOD(IASetVertexBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) PURE;
    STDMETHOD(IASetIndexBuffer)(THIS_ ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) PURE;
    STDMETHOD(IASetPrimitiveTopology)(THIS_ UINT Topology) PURE;

    STDMETHOD(VSSetShader)(THIS_ ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;
    STDMETHOD(VSSetConstantBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) PURE;
    STDMETHOD(VSSetShaderResources)(THIS_ UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) PURE;
    STDMETHOD(VSSetSamplers)(THIS_ UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) PURE;

    STDMETHOD(PSSetShader)(THIS_ ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;
    STDMETHOD(PSSetConstantBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) PURE;
    STDMETHOD(PSSetShaderResources)(THIS_ UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) PURE;
    STDMETHOD(PSSetSamplers)(THIS_ UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) PURE;

    STDMETHOD(GSSetShader)(THIS_ ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;

    STDMETHOD(CSSetShader)(THIS_ ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const*, UINT) PURE;
    STDMETHOD(CSSetConstantBuffers)(THIS_ UINT, UINT, ID3D11Buffer* const*) PURE;
    STDMETHOD(CSSetShaderResources)(THIS_ UINT, UINT, ID3D11ShaderResourceView* const*) PURE;
    STDMETHOD(CSSetUnorderedAccessViews)(THIS_ UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) PURE;
    STDMETHOD(CSSetSamplers)(THIS_ UINT, UINT, ID3D11SamplerState* const*) PURE;
    STDMETHOD(Dispatch)(THIS_ UINT, UINT, UINT) PURE;

    STDMETHOD(OMSetRenderTargets)(THIS_ UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) PURE;
    STDMETHOD(OMSetRenderTargetsAndUnorderedAccessViews)(THIS_ UINT NumRTVs, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) PURE;
    STDMETHOD(OMSetBlendState)(THIS_ ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) PURE;
    STDMETHOD(OMSetDepthStencilState)(THIS_ ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef) PURE;

    STDMETHOD(RSSetState)(THIS_ ID3D11RasterizerState* pRasterizerState) PURE;
    STDMETHOD(RSSetViewports)(THIS_ UINT NumViewports, const D3D11_VIEWPORT* pViewports) PURE;
    STDMETHOD(RSSetScissorRects)(THIS_ UINT NumRects, const RECT* pRects) PURE;

    STDMETHOD(ClearRenderTargetView)(THIS_ ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) PURE;
    STDMETHOD(ClearDepthStencilView)(THIS_ ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) PURE;

    STDMETHOD(DrawIndexed)(THIS_ UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) PURE;
    STDMETHOD(Draw)(THIS_ UINT VertexCount, UINT StartVertexLocation) PURE;
    STDMETHOD(DrawIndexedInstanced)(THIS_ UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) PURE;
    STDMETHOD(DrawInstanced)(THIS_ UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) PURE;

    STDMETHOD(Map)(THIS_ ID3D11Resource* pResource, UINT Subresource, UINT MapType, UINT MapFlags, void* pMappedResource) PURE;
    STDMETHOD(Unmap)(THIS_ ID3D11Resource* pResource, UINT Subresource) PURE;

    STDMETHOD(GenerateMips)(THIS_ ID3D11ShaderResourceView* pShaderResourceView) PURE;
    STDMETHOD(CopyResource)(THIS_ ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) PURE;
    STDMETHOD(UpdateSubresource)(THIS_ ID3D11Resource* pDstResource, UINT DstSubresource, const void* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) PURE;
    STDMETHOD(CopySubresourceRegion)(THIS_ ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource, const void* pSrcBox) PURE;
    STDMETHOD(ResolveSubresource)(THIS_ ID3D11Resource* pDstResource, UINT DstSubresource, ID3D11Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) PURE;
    STDMETHOD(Begin)(THIS_ ID3D11Query* pQuery) PURE;
    STDMETHOD(End)(THIS_ ID3D11Query* pQuery) PURE;
    STDMETHOD(GetData)(THIS_ ID3D11Query* pQuery, void* pData, UINT DataSize, UINT GetDataFlags) PURE;
    STDMETHOD(SetPredication)(THIS_ ID3D11Predicate* pPredicate, INT PredicateValue) PURE;

    STDMETHOD(FinishCommandList)(THIS_ INT RestoreDeferredContextState, ID3D11CommandList** ppCommandList) PURE;
    STDMETHOD(ExecuteCommandList)(THIS_ ID3D11CommandList* pCommandList, INT RestoreContextState) PURE;
};

typedef struct {
    UINT BufferCount;
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    UINT Windowed;
} DXGI_SWAP_CHAIN_DESC;

class MIDL_INTERFACE("2e8f6e0c-3e86-4a63-9ea5-7b52d95e08c1")
ID3D11Device : public IUnknown {
public:
    STDMETHOD(CreateBuffer)(THIS_ const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) PURE;
    STDMETHOD(CreateTexture1D)(THIS_ const D3D11_TEXTURE1D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture1D** ppTexture1D) PURE;
    STDMETHOD(CreateTexture2D)(THIS_ const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) PURE;
    STDMETHOD(CreateTexture3D)(THIS_ const D3D11_TEXTURE3D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture3D** ppTexture3D) PURE;
    STDMETHOD(CreateShaderResourceView)(THIS_ ID3D11Resource* pResource, const void* pDesc, ID3D11ShaderResourceView** ppSRView) PURE;
    STDMETHOD(CreateRenderTargetView)(THIS_ ID3D11Resource* pResource, const void* pDesc, ID3D11RenderTargetView** ppRTView) PURE;
    STDMETHOD(CreateDepthStencilView)(THIS_ ID3D11Resource* pResource, const void* pDesc, ID3D11DepthStencilView** ppDepthStencilView) PURE;
    STDMETHOD(CreateUnorderedAccessView)(THIS_ ID3D11Resource* pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc, ID3D11UnorderedAccessView** ppUAView) PURE;
    STDMETHOD(CreateVertexShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader) PURE;
    STDMETHOD(CreatePixelShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader) PURE;
    STDMETHOD(CreateGeometryShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader) PURE;
    STDMETHOD(CreateComputeShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11ComputeShader** ppComputeShader) PURE;
    STDMETHOD(CreateInputLayout)(THIS_ const void* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout) PURE;
    STDMETHOD(CreateSamplerState)(THIS_ const void* pSamplerDesc, ID3D11SamplerState** ppSamplerState) PURE;
    STDMETHOD(CreateRasterizerState)(THIS_ const void* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState) PURE;
    STDMETHOD(CreateDepthStencilState)(THIS_ const void* pDepthStencilDesc, ID3D11DepthStencilState** ppDepthStencilState) PURE;
    STDMETHOD(CreateBlendState)(THIS_ const void* pBlendStateDesc, ID3D11BlendState** ppBlendState) PURE;
    STDMETHOD(CreateQuery)(THIS_ const D3D11_QUERY_DESC* pQueryDesc, ID3D11Query** ppQuery) PURE;
    STDMETHOD(CreatePredicate)(THIS_ const D3D11_QUERY_DESC* pPredicateDesc, ID3D11Predicate** ppPredicate) PURE;
    STDMETHOD_(void, GetImmediateContext)(THIS_ ID3D11DeviceContext** ppImmediateContext) PURE;
    STDMETHOD(CreateDeferredContext)(THIS_ UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext) PURE;
    STDMETHOD(GetDeviceFeatureLevel)(THIS_ UINT* pFeatureLevel) PURE;
};
