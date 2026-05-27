#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <d3d12.h>
#include <dxgiformat.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

static std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static std::string getenv_string(const char* key) {
    DWORD needed = GetEnvironmentVariableA(key, nullptr, 0);
    if (needed == 0)
        return "";
    std::string value(needed, '\0');
    DWORD written = GetEnvironmentVariableA(key, value.data(), needed);
    if (written == 0)
        return "";
    value.resize(written);
    return value;
}

static void print_hr(const char* key, HRESULT hr, bool comma = true) {
    std::printf("    \"%s\": \"0x%08lx\"%s\n", key, static_cast<unsigned long>(static_cast<uint32_t>(hr)),
                comma ? "," : "");
}

static D3D12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

static D3D12_RESOURCE_DESC buffer_desc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT increment, UINT index) {
    start.ptr += static_cast<SIZE_T>(increment) * index;
    return start;
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    using SerializeRootSignatureFn =
        HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    using SerializeVersionedRootSignatureFn =
        HRESULT(WINAPI*)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
    using CreateRootSignatureDeserializerFn = HRESULT(WINAPI*)(const void*, SIZE_T, REFIID, void**);
    auto create_device = reinterpret_cast<CreateDeviceFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr));
    auto serialize_root_signature = reinterpret_cast<SerializeRootSignatureFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12SerializeRootSignature") : nullptr));
    auto serialize_versioned_root_signature = reinterpret_cast<SerializeVersionedRootSignatureFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12SerializeVersionedRootSignature") : nullptr));
    auto create_root_signature_deserializer = reinterpret_cast<CreateRootSignatureDeserializerFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12CreateRootSignatureDeserializer") : nullptr));

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : E_FAIL;

    UINT cbv_srv_uav_increment =
        device ? device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) : 0;
    UINT sampler_increment = device ? device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) : 0;
    UINT rtv_increment = device ? device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) : 0;
    UINT dsv_increment = device ? device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV) : 0;

    D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc = {};
    cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbv_heap_desc.NumDescriptors = 16;
    cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
    sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.NumDescriptors = 4;
    sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.NumDescriptors = 2;
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.NumDescriptors = 1;

    ID3D12DescriptorHeap* cbv_heap = nullptr;
    ID3D12DescriptorHeap* sampler_heap = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12DescriptorHeap* dsv_heap = nullptr;
    HRESULT cbv_heap_hr = device ? device->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap)) : E_FAIL;
    HRESULT sampler_heap_hr =
        device ? device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap)) : E_FAIL;
    HRESULT rtv_heap_hr = device ? device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)) : E_FAIL;
    HRESULT dsv_heap_hr = device ? device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap)) : E_FAIL;

    D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_roundtrip = cbv_heap ? cbv_heap->GetDesc() : D3D12_DESCRIPTOR_HEAP_DESC{};
    D3D12_CPU_DESCRIPTOR_HANDLE cbv_start =
        cbv_heap ? cbv_heap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    D3D12_GPU_DESCRIPTOR_HANDLE cbv_gpu_start =
        cbv_heap ? cbv_heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    D3D12_CPU_DESCRIPTOR_HANDLE sampler_start =
        sampler_heap ? sampler_heap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_start =
        sampler_heap ? sampler_heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_start =
        rtv_heap ? rtv_heap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_start =
        dsv_heap ? dsv_heap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};

    D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC upload_buffer_desc = buffer_desc(4096);
    D3D12_RESOURCE_DESC uav_buffer_desc = buffer_desc(4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC color_desc = texture_desc(DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_RESOURCE_DESC uav_texture_desc =
        texture_desc(DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC depth_desc = texture_desc(DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE color_clear = {};
    color_clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    color_clear.Color[3] = 1.0f;
    D3D12_CLEAR_VALUE depth_clear = {};
    depth_clear.Format = DXGI_FORMAT_D32_FLOAT;
    depth_clear.DepthStencil.Depth = 1.0f;

    ID3D12Resource* upload_buffer = nullptr;
    ID3D12Resource* uav_buffer = nullptr;
    ID3D12Resource* color_texture = nullptr;
    ID3D12Resource* uav_texture = nullptr;
    ID3D12Resource* depth_texture = nullptr;
    HRESULT upload_buffer_hr =
        device
            ? device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_buffer_desc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_buffer))
            : E_FAIL;
    HRESULT uav_buffer_hr =
        device
            ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &uav_buffer_desc,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&uav_buffer))
            : E_FAIL;
    HRESULT color_texture_hr = device ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                                                                        &color_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                        &color_clear, IID_PPV_ARGS(&color_texture))
                                      : E_FAIL;
    HRESULT uav_texture_hr = device ? device->CreateCommittedResource(
                                          &default_heap, D3D12_HEAP_FLAG_NONE, &uav_texture_desc,
                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&uav_texture))
                                    : E_FAIL;
    HRESULT depth_texture_hr = device ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                                                                        &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                        &depth_clear, IID_PPV_ARGS(&depth_texture))
                                      : E_FAIL;

    bool cbv_created = false;
    bool srv_created = false;
    bool uav_created = false;
    bool texture_srv_created = false;
    bool texture_uav_created = false;
    bool sampler_created = false;
    bool rtv_created = false;
    bool dsv_created = false;
    bool descriptors_copied = false;
    bool null_srv_created = false;
    bool null_uav_created = false;
    bool null_cbv_created = false;
    if (device && cbv_heap && upload_buffer && cbv_srv_uav_increment != 0) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
        cbv.BufferLocation = upload_buffer->GetGPUVirtualAddress();
        cbv.SizeInBytes = 256;
        device->CreateConstantBufferView(&cbv, offset_cpu(cbv_start, cbv_srv_uav_increment, 0));
        cbv_created = cbv.BufferLocation != 0;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.NumElements = 128;
        srv.Buffer.StructureByteStride = 0;
        srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(upload_buffer, &srv, offset_cpu(cbv_start, cbv_srv_uav_increment, 1));
        srv_created = true;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R32_UINT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 1024;
        uav.Buffer.StructureByteStride = 0;
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(uav_buffer, nullptr, &uav, offset_cpu(cbv_start, cbv_srv_uav_increment, 2));
        uav_created = true;

        if (color_texture) {
            D3D12_SHADER_RESOURCE_VIEW_DESC texture_srv = {};
            texture_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texture_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            texture_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            texture_srv.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(color_texture, &texture_srv,
                                             offset_cpu(cbv_start, cbv_srv_uav_increment, 3));
            texture_srv_created = true;
        }

        if (uav_texture) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC texture_uav = {};
            texture_uav.Format = DXGI_FORMAT_R32_UINT;
            texture_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(uav_texture, nullptr, &texture_uav,
                                              offset_cpu(cbv_start, cbv_srv_uav_increment, 4));
            texture_uav_created = true;
        }

        device->CopyDescriptorsSimple(5, offset_cpu(cbv_start, cbv_srv_uav_increment, 5), cbv_start,
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        descriptors_copied = true;

        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv = {};
        null_srv.Format = DXGI_FORMAT_R32_FLOAT;
        null_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv.Buffer.NumElements = 4;
        device->CreateShaderResourceView(nullptr, &null_srv, offset_cpu(cbv_start, cbv_srv_uav_increment, 10));
        null_srv_created = true;

        D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav = {};
        null_uav.Format = DXGI_FORMAT_R32_UINT;
        null_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        null_uav.Buffer.NumElements = 4;
        device->CreateUnorderedAccessView(nullptr, nullptr, &null_uav,
                                          offset_cpu(cbv_start, cbv_srv_uav_increment, 11));
        null_uav_created = true;

        device->CreateConstantBufferView(nullptr, offset_cpu(cbv_start, cbv_srv_uav_increment, 12));
        null_cbv_created = true;
    }

    if (device && sampler_heap && sampler_increment != 0) {
        D3D12_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = 3.402823466e+38F;
        device->CreateSampler(&sampler, sampler_start);
        sampler_created = true;
    }

    if (device && rtv_heap && color_texture) {
        D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
        rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(color_texture, &rtv, rtv_start);
        rtv_created = true;
    }

    if (device && dsv_heap && depth_texture) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth_texture, &dsv, dsv_start);
        dsv_created = true;
    }

    D3D12_DESCRIPTOR_RANGE ranges[3] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 2;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 8;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 4;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].RegisterSpace = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_params[5] = {};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].DescriptorTable.NumDescriptorRanges = 3;
    root_params[0].DescriptorTable.pDescriptorRanges = ranges;
    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_params[1].Constants.ShaderRegister = 0;
    root_params[1].Constants.RegisterSpace = 1;
    root_params[1].Constants.Num32BitValues = 16;
    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params[2].Descriptor.ShaderRegister = 1;
    root_params[2].Descriptor.RegisterSpace = 0;
    root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params[3].Descriptor.ShaderRegister = 9;
    root_params[3].Descriptor.RegisterSpace = 0;
    root_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params[4].Descriptor.ShaderRegister = 3;
    root_params[4].Descriptor.RegisterSpace = 0;

    D3D12_STATIC_SAMPLER_DESC static_sampler = {};
    static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    static_sampler.MinLOD = 0.0f;
    static_sampler.MaxLOD = 3.402823466e+38F;
    static_sampler.ShaderRegister = 0;
    static_sampler.RegisterSpace = 0;
    static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 5;
    root_desc.pParameters = root_params;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &static_sampler;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* root_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    HRESULT serialize_hr =
        serialize_root_signature
            ? serialize_root_signature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_blob, &error_blob)
            : E_FAIL;

    ID3D12RootSignatureDeserializer* deserializer = nullptr;
    HRESULT deserialize_hr =
        (create_root_signature_deserializer && root_blob)
            ? create_root_signature_deserializer(root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                                 IID_PPV_ARGS(&deserializer))
            : E_FAIL;
    const D3D12_ROOT_SIGNATURE_DESC* deserialized_desc = deserializer ? deserializer->GetRootSignatureDesc() : nullptr;
    bool deserialized_matches =
        deserialized_desc && deserialized_desc->NumParameters == 5 && deserialized_desc->NumStaticSamplers == 1 &&
        deserialized_desc->pParameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
        deserialized_desc->pParameters[0].DescriptorTable.NumDescriptorRanges == 3 &&
        deserialized_desc->pParameters[1].ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
        deserialized_desc->pParameters[1].Constants.Num32BitValues == 16 &&
        deserialized_desc->pParameters[1].Constants.RegisterSpace == 1 &&
        deserialized_desc->pParameters[2].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV &&
        deserialized_desc->pParameters[3].ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV &&
        deserialized_desc->pParameters[4].ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV;

    ID3D12RootSignature* root_signature = nullptr;
    HRESULT create_root_signature_hr =
        (device && root_blob) ? device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                                            root_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))
                              : E_FAIL;

    D3D12_DESCRIPTOR_RANGE1 ranges1[3] = {};
    for (UINT i = 0; i < 3; ++i) {
        ranges1[i].RangeType = ranges[i].RangeType;
        ranges1[i].NumDescriptors = ranges[i].NumDescriptors;
        ranges1[i].BaseShaderRegister = ranges[i].BaseShaderRegister;
        ranges1[i].RegisterSpace = ranges[i].RegisterSpace;
        ranges1[i].OffsetInDescriptorsFromTableStart = ranges[i].OffsetInDescriptorsFromTableStart;
        ranges1[i].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    }
    D3D12_ROOT_PARAMETER1 root_params1[5] = {};
    root_params1[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params1[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params1[0].DescriptorTable.NumDescriptorRanges = 3;
    root_params1[0].DescriptorTable.pDescriptorRanges = ranges1;
    root_params1[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params1[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_params1[1].Constants = root_params[1].Constants;
    root_params1[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params1[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params1[2].Descriptor.ShaderRegister = root_params[2].Descriptor.ShaderRegister;
    root_params1[2].Descriptor.RegisterSpace = root_params[2].Descriptor.RegisterSpace;
    root_params1[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    root_params1[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params1[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params1[3].Descriptor.ShaderRegister = root_params[3].Descriptor.ShaderRegister;
    root_params1[3].Descriptor.RegisterSpace = root_params[3].Descriptor.RegisterSpace;
    root_params1[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    root_params1[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_params1[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params1[4].Descriptor.ShaderRegister = root_params[4].Descriptor.ShaderRegister;
    root_params1[4].Descriptor.RegisterSpace = root_params[4].Descriptor.RegisterSpace;
    root_params1[4].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc1 = {};
    root_desc1.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    root_desc1.Desc_1_1.NumParameters = 5;
    root_desc1.Desc_1_1.pParameters = root_params1;
    root_desc1.Desc_1_1.NumStaticSamplers = 1;
    root_desc1.Desc_1_1.pStaticSamplers = &static_sampler;
    root_desc1.Desc_1_1.Flags = root_desc.Flags;

    ID3DBlob* root_blob1 = nullptr;
    ID3DBlob* error_blob1 = nullptr;
    HRESULT serialize_1_1_hr = serialize_versioned_root_signature
                                   ? serialize_versioned_root_signature(&root_desc1, &root_blob1, &error_blob1)
                                   : E_FAIL;
    ID3D12RootSignature* root_signature1 = nullptr;
    HRESULT create_root_signature_1_1_hr =
        (device && root_blob1)
            ? device->CreateRootSignature(0, root_blob1->GetBufferPointer(), root_blob1->GetBufferSize(),
                                          IID_PPV_ARGS(&root_signature1))
            : E_FAIL;

    D3D12_DESCRIPTOR_RANGE1 collision_ranges[2] = {};
    collision_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    collision_ranges[0].NumDescriptors = 1;
    collision_ranges[0].BaseShaderRegister = 0;
    collision_ranges[0].RegisterSpace = 0;
    collision_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    collision_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    collision_ranges[1].NumDescriptors = 1;
    collision_ranges[1].BaseShaderRegister = 0;
    collision_ranges[1].RegisterSpace = 1;
    collision_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_DESCRIPTOR_RANGE1 unbounded_range = {};
    unbounded_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    unbounded_range.NumDescriptors = UINT_MAX;
    unbounded_range.BaseShaderRegister = 4;
    unbounded_range.RegisterSpace = 2;
    unbounded_range.OffsetInDescriptorsFromTableStart = 2;

    D3D12_ROOT_PARAMETER1 binding_params1[2] = {};
    binding_params1[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    binding_params1[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    binding_params1[0].DescriptorTable.NumDescriptorRanges = 2;
    binding_params1[0].DescriptorTable.pDescriptorRanges = collision_ranges;
    binding_params1[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    binding_params1[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    binding_params1[1].DescriptorTable.NumDescriptorRanges = 1;
    binding_params1[1].DescriptorTable.pDescriptorRanges = &unbounded_range;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC binding_root_desc1 = {};
    binding_root_desc1.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    binding_root_desc1.Desc_1_1.NumParameters = 2;
    binding_root_desc1.Desc_1_1.pParameters = binding_params1;

    ID3DBlob* binding_root_blob = nullptr;
    ID3DBlob* binding_error_blob = nullptr;
    HRESULT serialize_binding_hr =
        serialize_versioned_root_signature
            ? serialize_versioned_root_signature(&binding_root_desc1, &binding_root_blob, &binding_error_blob)
            : E_FAIL;
    ID3D12RootSignatureDeserializer* binding_deserializer = nullptr;
    HRESULT deserialize_binding_hr = (create_root_signature_deserializer && binding_root_blob)
                                         ? create_root_signature_deserializer(binding_root_blob->GetBufferPointer(),
                                                                              binding_root_blob->GetBufferSize(),
                                                                              IID_PPV_ARGS(&binding_deserializer))
                                         : E_FAIL;
    const D3D12_ROOT_SIGNATURE_DESC* binding_desc =
        binding_deserializer ? binding_deserializer->GetRootSignatureDesc() : nullptr;
    bool register_space_collision_preserved =
        binding_desc && binding_desc->NumParameters == 2 &&
        binding_desc->pParameters[0].DescriptorTable.NumDescriptorRanges == 2 &&
        binding_desc->pParameters[0].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister == 0 &&
        binding_desc->pParameters[0].DescriptorTable.pDescriptorRanges[0].RegisterSpace == 0 &&
        binding_desc->pParameters[0].DescriptorTable.pDescriptorRanges[1].BaseShaderRegister == 0 &&
        binding_desc->pParameters[0].DescriptorTable.pDescriptorRanges[1].RegisterSpace == 1;
    bool unbounded_range_preserved =
        binding_desc && binding_desc->NumParameters == 2 &&
        binding_desc->pParameters[1].DescriptorTable.NumDescriptorRanges == 1 &&
        binding_desc->pParameters[1].DescriptorTable.pDescriptorRanges[0].NumDescriptors == UINT_MAX &&
        binding_desc->pParameters[1].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister == 4 &&
        binding_desc->pParameters[1].DescriptorTable.pDescriptorRanges[0].RegisterSpace == 2 &&
        binding_desc->pParameters[1].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart == 2;
    ID3D12RootSignature* binding_root_signature = nullptr;
    HRESULT create_binding_root_signature_hr =
        (device && binding_root_blob)
            ? device->CreateRootSignature(0, binding_root_blob->GetBufferPointer(), binding_root_blob->GetBufferSize(),
                                          IID_PPV_ARGS(&binding_root_signature))
            : E_FAIL;

    const char invalid_blob[] = "not a root signature";
    ID3D12RootSignatureDeserializer* invalid_deserializer = nullptr;
    HRESULT invalid_blob_hr = create_root_signature_deserializer
                                  ? create_root_signature_deserializer(invalid_blob, sizeof(invalid_blob),
                                                                       IID_PPV_ARGS(&invalid_deserializer))
                                  : E_FAIL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC unsupported_root_desc = {};
    unsupported_root_desc.Version = static_cast<D3D_ROOT_SIGNATURE_VERSION>(0x7fffffff);
    ID3DBlob* unsupported_blob = nullptr;
    ID3DBlob* unsupported_error_blob = nullptr;
    HRESULT unsupported_version_hr =
        serialize_versioned_root_signature
            ? serialize_versioned_root_signature(&unsupported_root_desc, &unsupported_blob, &unsupported_error_blob)
            : E_FAIL;
    bool unsupported_rejected = FAILED(unsupported_version_hr);
    bool invalid_blob_rejected = FAILED(invalid_blob_hr);

    bool handles_valid = cbv_start.ptr != 0 && cbv_gpu_start.ptr != 0 && sampler_start.ptr != 0 &&
                         sampler_gpu_start.ptr != 0 && rtv_start.ptr != 0 && dsv_start.ptr != 0;
    bool increments_valid =
        cbv_srv_uav_increment != 0 && sampler_increment != 0 && rtv_increment != 0 && dsv_increment != 0 &&
        offset_cpu(cbv_start, cbv_srv_uav_increment, 1).ptr - cbv_start.ptr == cbv_srv_uav_increment &&
        offset_cpu(sampler_start, sampler_increment, 1).ptr - sampler_start.ptr == sampler_increment &&
        offset_cpu(rtv_start, rtv_increment, 1).ptr - rtv_start.ptr == rtv_increment;
    bool heap_desc_roundtrip = cbv_heap_roundtrip.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
                               cbv_heap_roundtrip.NumDescriptors == 16 &&
                               cbv_heap_roundtrip.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    bool entrypoints_valid = d3d12 && create_device && serialize_root_signature && serialize_versioned_root_signature &&
                             create_root_signature_deserializer;
    bool resources_valid = SUCCEEDED(upload_buffer_hr) && SUCCEEDED(uav_buffer_hr) && SUCCEEDED(color_texture_hr) &&
                           SUCCEEDED(uav_texture_hr) && SUCCEEDED(depth_texture_hr);
    bool descriptors_valid = SUCCEEDED(cbv_heap_hr) && SUCCEEDED(sampler_heap_hr) && SUCCEEDED(rtv_heap_hr) &&
                             SUCCEEDED(dsv_heap_hr) && cbv_created && srv_created && uav_created && sampler_created &&
                             texture_srv_created && texture_uav_created && rtv_created && dsv_created &&
                             descriptors_copied && null_srv_created && null_uav_created && null_cbv_created;
    bool root_signature_valid = SUCCEEDED(serialize_hr) && SUCCEEDED(deserialize_hr) && deserialized_matches &&
                                SUCCEEDED(create_root_signature_hr) && SUCCEEDED(serialize_1_1_hr) &&
                                SUCCEEDED(create_root_signature_1_1_hr) && SUCCEEDED(serialize_binding_hr) &&
                                SUCCEEDED(deserialize_binding_hr) && SUCCEEDED(create_binding_root_signature_hr) &&
                                register_space_collision_preserved && unbounded_range_preserved &&
                                unsupported_rejected && invalid_blob_rejected;
    bool pass = entrypoints_valid && SUCCEEDED(create_hr) && handles_valid && increments_valid && heap_desc_roundtrip &&
                resources_valid && descriptors_valid && root_signature_valid;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-descriptors.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"entrypoints\": {\n");
    std::printf("    \"d3d12_loaded\": %s,\n", d3d12 ? "true" : "false");
    std::printf("    \"D3D12CreateDevice\": %s,\n", create_device ? "true" : "false");
    std::printf("    \"D3D12SerializeRootSignature\": %s,\n", serialize_root_signature ? "true" : "false");
    std::printf("    \"D3D12SerializeVersionedRootSignature\": %s,\n",
                serialize_versioned_root_signature ? "true" : "false");
    std::printf("    \"D3D12CreateRootSignatureDeserializer\": %s\n",
                create_root_signature_deserializer ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"device\": {\n");
    print_hr("D3D12CreateDevice", create_hr, false);
    std::printf("  },\n");
    std::printf("  \"heaps\": {\n");
    print_hr("CBV_SRV_UAV", cbv_heap_hr);
    print_hr("SAMPLER", sampler_heap_hr);
    print_hr("RTV", rtv_heap_hr);
    print_hr("DSV", dsv_heap_hr);
    std::printf("    \"heap_desc_roundtrip\": %s,\n", heap_desc_roundtrip ? "true" : "false");
    std::printf("    \"handles_valid\": %s,\n", handles_valid ? "true" : "false");
    std::printf("    \"increments_valid\": %s,\n", increments_valid ? "true" : "false");
    std::printf("    \"cbv_srv_uav_increment\": %u,\n", cbv_srv_uav_increment);
    std::printf("    \"sampler_increment\": %u,\n", sampler_increment);
    std::printf("    \"rtv_increment\": %u,\n", rtv_increment);
    std::printf("    \"dsv_increment\": %u\n", dsv_increment);
    std::printf("  },\n");
    std::printf("  \"resources\": {\n");
    print_hr("upload_buffer", upload_buffer_hr);
    print_hr("uav_buffer", uav_buffer_hr);
    print_hr("color_texture", color_texture_hr);
    print_hr("uav_texture", uav_texture_hr);
    print_hr("depth_texture", depth_texture_hr);
    std::printf("    \"gpu_virtual_address_nonzero\": %s\n",
                (upload_buffer && upload_buffer->GetGPUVirtualAddress() != 0) ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"descriptors\": {\n");
    std::printf("    \"cbv_created\": %s,\n", cbv_created ? "true" : "false");
    std::printf("    \"srv_created\": %s,\n", srv_created ? "true" : "false");
    std::printf("    \"uav_created\": %s,\n", uav_created ? "true" : "false");
    std::printf("    \"texture_srv_created\": %s,\n", texture_srv_created ? "true" : "false");
    std::printf("    \"texture_uav_created\": %s,\n", texture_uav_created ? "true" : "false");
    std::printf("    \"sampler_created\": %s,\n", sampler_created ? "true" : "false");
    std::printf("    \"rtv_created\": %s,\n", rtv_created ? "true" : "false");
    std::printf("    \"dsv_created\": %s,\n", dsv_created ? "true" : "false");
    std::printf("    \"null_srv_created\": %s,\n", null_srv_created ? "true" : "false");
    std::printf("    \"null_uav_created\": %s,\n", null_uav_created ? "true" : "false");
    std::printf("    \"null_cbv_created\": %s,\n", null_cbv_created ? "true" : "false");
    std::printf("    \"copy_descriptors_simple\": %s\n", descriptors_copied ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"root_signature\": {\n");
    print_hr("serialize", serialize_hr);
    print_hr("deserialize", deserialize_hr);
    print_hr("create_root_signature", create_root_signature_hr);
    print_hr("serialize_1_1", serialize_1_1_hr);
    print_hr("create_root_signature_1_1", create_root_signature_1_1_hr);
    print_hr("serialize_binding", serialize_binding_hr);
    print_hr("deserialize_binding", deserialize_binding_hr);
    print_hr("create_binding_root_signature", create_binding_root_signature_hr);
    print_hr("unsupported_version", unsupported_version_hr);
    print_hr("invalid_blob", invalid_blob_hr);
    std::printf("    \"blob_size\": %llu,\n",
                static_cast<unsigned long long>(root_blob ? root_blob->GetBufferSize() : 0));
    std::printf("    \"blob_1_1_size\": %llu,\n",
                static_cast<unsigned long long>(root_blob1 ? root_blob1->GetBufferSize() : 0));
    std::printf("    \"deserialized_matches\": %s,\n", deserialized_matches ? "true" : "false");
    std::printf("    \"register_space_collision_preserved\": %s,\n",
                register_space_collision_preserved ? "true" : "false");
    std::printf("    \"unbounded_range_preserved\": %s,\n", unbounded_range_preserved ? "true" : "false");
    std::printf("    \"unsupported_rejected\": %s,\n", unsupported_rejected ? "true" : "false");
    std::printf("    \"invalid_blob_rejected\": %s\n", invalid_blob_rejected ? "true" : "false");
    std::printf("  }\n");
    std::printf("}\n");

    if (root_signature)
        root_signature->Release();
    if (root_signature1)
        root_signature1->Release();
    if (binding_root_signature)
        binding_root_signature->Release();
    if (binding_deserializer)
        binding_deserializer->Release();
    if (binding_root_blob)
        binding_root_blob->Release();
    if (binding_error_blob)
        binding_error_blob->Release();
    if (invalid_deserializer)
        invalid_deserializer->Release();
    if (deserializer)
        deserializer->Release();
    if (root_blob)
        root_blob->Release();
    if (error_blob)
        error_blob->Release();
    if (root_blob1)
        root_blob1->Release();
    if (error_blob1)
        error_blob1->Release();
    if (unsupported_blob)
        unsupported_blob->Release();
    if (unsupported_error_blob)
        unsupported_error_blob->Release();
    if (depth_texture)
        depth_texture->Release();
    if (color_texture)
        color_texture->Release();
    if (uav_texture)
        uav_texture->Release();
    if (uav_buffer)
        uav_buffer->Release();
    if (upload_buffer)
        upload_buffer->Release();
    if (dsv_heap)
        dsv_heap->Release();
    if (rtv_heap)
        rtv_heap->Release();
    if (sampler_heap)
        sampler_heap->Release();
    if (cbv_heap)
        cbv_heap->Release();
    if (device)
        device->Release();

    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
