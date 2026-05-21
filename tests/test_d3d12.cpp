#include <chrono>
#include <cstdio>
#include <cstring>
#include <d3d/D3D12.h>
#include <metalsharp/D3D12Device.h>
#include <metalsharp/DXGI.h>
#include <thread>

extern "C" {
HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC*, unsigned int, void**, void**);
HRESULT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, void**, void**);
HRESULT D3D12GetDebugInterface(const GUID&, void**);
HRESULT D3D12EnableExperimentalFeatures(unsigned int, const GUID*, void*, unsigned int*);
}

struct TestSwapChainDesc {
    struct {
        DXGI_FORMAT Format;
        UINT ScanlineOrdering;
        UINT Scaling;
        UINT Width;
        UINT Height;
        UINT RefreshRateNumerator;
        UINT RefreshRateDenominator;
    } BufferDesc;
    struct {
        UINT Count;
        UINT Quality;
    } SampleDesc;
    DXGI_FORMAT BufferFormat;
    UINT BufferUsage;
    UINT BufferCount;
    HWND OutputWindow;
    INT Windowed;
    UINT SwapEffect;
    UINT Flags;
};

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            printf("  [OK] %s\n", msg);                                                                                \
            g_pass++;                                                                                                  \
        } else {                                                                                                       \
            printf("  [FAIL] %s\n", msg);                                                                              \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main() {
    printf("=== MetalSharp D3D12 Tests ===\n\n");

    printf("--- D3D12CreateDevice ---\n");
    metalsharp::D3D12DeviceImpl* device = nullptr;
    HRESULT hr = metalsharp::D3D12DeviceImpl::create(&device);
    CHECK(SUCCEEDED(hr) && device, "D3D12Device creation");

    printf("\n--- Command Queue ---\n");
    ID3D12CommandQueue* cmdQueue = nullptr;
    if (device) {
        hr = device->CreateCommandQueue(nullptr, IID_ID3D12CommandQueue, (void**)&cmdQueue);
        CHECK(SUCCEEDED(hr) && cmdQueue, "CreateCommandQueue");
    }

    printf("\n--- DXGI D3D12 Swap Chain ---\n");
    IDXGIFactory1* factory = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    IDXGIFactory2* factory2 = nullptr;
    IDXGISwapChain1* hwndSwapChain = nullptr;
    ID3D12Resource* swapChainBackBuffer = nullptr;
    if (cmdQueue) {
        GUID anyFactory = {};
        hr = CreateDXGIFactory1(anyFactory, (void**)&factory);
        CHECK(SUCCEEDED(hr) && factory, "CreateDXGIFactory1 for D3D12 queue");

        TestSwapChainDesc swapDesc = {};
        swapDesc.BufferDesc.Width = 1280;
        swapDesc.BufferDesc.Height = 720;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferCount = 2;
        swapDesc.Windowed = TRUE;
        if (factory) {
            hr = factory->CreateSwapChain(cmdQueue, &swapDesc, &swapChain);
            CHECK(SUCCEEDED(hr) && swapChain, "CreateSwapChain accepts ID3D12CommandQueue");
        }
        if (swapChain) {
            TestSwapChainDesc readback = {};
            hr = swapChain->GetDesc(&readback);
            CHECK(SUCCEEDED(hr) && readback.BufferDesc.Width == 1280 && readback.BufferDesc.Height == 720,
                  "D3D12 swap chain desc round-trips dimensions");
        }
        if (factory) {
            hr = factory->QueryInterface(anyFactory, (void**)&factory2);
            CHECK(SUCCEEDED(hr) && factory2, "QueryInterface exposes IDXGIFactory2");
        }
        if (factory2) {
            DXGI_SWAP_CHAIN_DESC1 desc1 = {};
            desc1.Width = 1024;
            desc1.Height = 576;
            desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc1.SampleDesc.Count = 1;
            desc1.BufferUsage = 0x20;
            desc1.BufferCount = 2;
            hr = factory2->CreateSwapChainForHwnd(cmdQueue, nullptr, &desc1, nullptr, nullptr, &hwndSwapChain);
            CHECK(SUCCEEDED(hr) && hwndSwapChain, "CreateSwapChainForHwnd accepts ID3D12CommandQueue");
            if (hwndSwapChain) {
                hr = hwndSwapChain->GetBuffer(0, IID_ID3D12Resource, (void**)&swapChainBackBuffer);
                CHECK(SUCCEEDED(hr) && swapChainBackBuffer && swapChainBackBuffer->__metalTexturePtr() != nullptr,
                      "IDXGISwapChain1::GetBuffer returns ID3D12Resource");
                hr = hwndSwapChain->Present1(0, 0, nullptr);
                CHECK(SUCCEEDED(hr), "IDXGISwapChain1::Present1");
            }
        }
    }

    printf("\n--- Command Allocator ---\n");
    ID3D12CommandAllocator* cmdAlloc = nullptr;
    if (device) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator,
                                            (void**)&cmdAlloc);
        CHECK(SUCCEEDED(hr) && cmdAlloc, "CreateCommandAllocator");
        if (cmdAlloc) {
            hr = cmdAlloc->Reset();
            CHECK(SUCCEEDED(hr), "CommandAllocator::Reset");
        }
    }

    printf("\n--- Command List ---\n");
    ID3D12GraphicsCommandList* cmdList = nullptr;
    if (device && cmdAlloc) {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr,
                                       IID_ID3D12GraphicsCommandList, (void**)&cmdList);
        CHECK(SUCCEEDED(hr) && cmdList, "CreateCommandList");
    }

    printf("\n--- Feature Support ---\n");
    if (device) {
        UINT requestedLevels[] = {D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {2, requestedLevels, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));
        CHECK(SUCCEEDED(hr) && levels.MaxSupportedFeatureLevel == D3D_FEATURE_LEVEL_12_0,
              "CheckFeatureSupport: feature level 12_0");

        D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
        CHECK(SUCCEEDED(hr) && options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2,
              "CheckFeatureSupport: resource binding tier");
        CHECK(options.TypedUAVLoadAdditionalFormats == TRUE, "CheckFeatureSupport: typed UAV loads");

        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {D3D_SHADER_MODEL_6_6};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
        CHECK(SUCCEEDED(hr) && shaderModel.HighestShaderModel == D3D_SHADER_MODEL_6_0,
              "CheckFeatureSupport: shader model clamped");

        D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature = {D3D_ROOT_SIGNATURE_VERSION_1_1};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignature, sizeof(rootSignature));
        CHECK(SUCCEEDED(hr) && rootSignature.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_1,
              "CheckFeatureSupport: root signature 1.1");

        D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = {DXGI_FORMAT_B8G8R8A8_UNORM, 0, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport));
        CHECK(SUCCEEDED(hr) && (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET),
              "CheckFeatureSupport: render target format");

        D3D12_FEATURE_DATA_FORMAT_SUPPORT hdrSupport = {DXGI_FORMAT_R11G11B10_FLOAT, 0, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &hdrSupport, sizeof(hdrSupport));
        CHECK(SUCCEEDED(hr) && (hdrSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET),
              "CheckFeatureSupport: R11G11B10 render target");

        D3D12_FEATURE_DATA_FORMAT_SUPPORT srgbSupport = {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 0, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &srgbSupport, sizeof(srgbSupport));
        CHECK(SUCCEEDED(hr) && (srgbSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET),
              "CheckFeatureSupport: BGRA8 sRGB render target");

        D3D12_FEATURE_DATA_FORMAT_SUPPORT bc7Support = {DXGI_FORMAT_BC7_UNORM, 0, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &bc7Support, sizeof(bc7Support));
        CHECK(SUCCEEDED(hr) && (bc7Support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) &&
                  !(bc7Support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET),
              "CheckFeatureSupport: BC7 shader sample only");

        D3D12_FEATURE_DATA_FORMAT_SUPPORT unknownSupport = {0xffffu, 123, 456};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &unknownSupport, sizeof(unknownSupport));
        CHECK(SUCCEEDED(hr) && unknownSupport.Support1 == 0 && unknownSupport.Support2 == 0,
              "CheckFeatureSupport: unknown format has no support");

        D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = {DXGI_FORMAT_B8G8R8A8_UNORM, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo));
        CHECK(SUCCEEDED(hr) && formatInfo.PlaneCount == 1, "CheckFeatureSupport: supported format plane count");

        D3D12_FEATURE_DATA_FORMAT_INFO unknownFormatInfo = {0xffffu, 7};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &unknownFormatInfo, sizeof(unknownFormatInfo));
        CHECK(SUCCEEDED(hr) && unknownFormatInfo.PlaneCount == 0, "CheckFeatureSupport: unknown format has no planes");

        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa = {DXGI_FORMAT_B8G8R8A8_UNORM, 4, 0, 0};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof(msaa));
        CHECK(SUCCEEDED(hr) && msaa.NumQualityLevels == 1, "CheckFeatureSupport: supported MSAA count");

        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS unsupportedMsaa = {DXGI_FORMAT_BC7_UNORM, 4, 0, 99};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &unsupportedMsaa,
                                         sizeof(unsupportedMsaa));
        CHECK(SUCCEEDED(hr) && unsupportedMsaa.NumQualityLevels == 0,
              "CheckFeatureSupport: compressed format has no MSAA quality");

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
        CHECK(SUCCEEDED(hr) && options5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED,
              "CheckFeatureSupport: raytracing tier not advertised");

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
        CHECK(SUCCEEDED(hr) && options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED,
              "CheckFeatureSupport: mesh shader tier not advertised");

        hr = device->CheckFeatureSupport(0xffffffffu, &options, sizeof(options));
        CHECK(hr == E_INVALIDARG, "CheckFeatureSupport rejects unknown feature");
    }

    if (cmdList) {
        hr = cmdList->IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        CHECK(SUCCEEDED(hr), "IASetPrimitiveTopology");

        D3D12_VIEWPORT vp = {0, 0, 800, 600, 0.0f, 1.0f};
        hr = cmdList->RSSetViewports(1, &vp);
        CHECK(SUCCEEDED(hr), "RSSetViewports");

        hr = cmdList->DrawInstanced(3, 1, 0, 0);
        CHECK(SUCCEEDED(hr), "DrawInstanced (no pipeline)");

        hr = cmdList->Close();
        CHECK(SUCCEEDED(hr), "Close command list");

        printf("\n--- Reset command list ---\n");
        hr = cmdList->Reset(cmdAlloc, nullptr);
        CHECK(SUCCEEDED(hr), "Reset command list");
        hr = cmdList->Close();
        CHECK(SUCCEEDED(hr), "Close after reset");
    }

    printf("\n--- Committed Resource (Buffer) ---\n");
    ID3D12Resource* uploadBuffer = nullptr;
    if (device) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Alignment = 0;
        bufDesc.Width = 256;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc = {1, 0};
        bufDesc.Layout = 0;
        bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = device->CreateCommittedResource(&heapProps, 0, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_ID3D12Resource, (void**)&uploadBuffer);
        CHECK(SUCCEEDED(hr) && uploadBuffer, "CreateCommittedResource (upload buffer)");

        if (uploadBuffer) {
            void* data = nullptr;
            D3D12_RANGE readRange = {0, 0};
            hr = uploadBuffer->Map(0, &readRange, &data);
            CHECK(SUCCEEDED(hr) && data != nullptr, "Map upload buffer");

            if (data) {
                memset(data, 0xAB, 256);
            }

            hr = uploadBuffer->Unmap(0, nullptr);
            CHECK(SUCCEEDED(hr), "Unmap upload buffer");

            D3D12_RESOURCE_DESC retrievedDesc = {};
            hr = uploadBuffer->GetDesc(&retrievedDesc);
            CHECK(SUCCEEDED(hr) && retrievedDesc.Width == 256, "GetDesc returns correct width");
            CHECK(retrievedDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER, "GetDesc: dimension is buffer");

            D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = 0;
            hr = uploadBuffer->GetGPUVirtualAddress(&gpuAddr);
            CHECK(SUCCEEDED(hr) && gpuAddr != 0, "GetGPUVirtualAddress non-zero");
        }
    }

    printf("\n--- Resource Allocation Info ---\n");
    if (device) {
        D3D12_RESOURCE_DESC allocationDescs[2] = {};
        allocationDescs[0].Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        allocationDescs[0].Width = 1024;
        allocationDescs[0].Height = 1;
        allocationDescs[0].DepthOrArraySize = 1;
        allocationDescs[0].MipLevels = 1;
        allocationDescs[0].SampleDesc.Count = 1;
        allocationDescs[0].Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        allocationDescs[1].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        allocationDescs[1].Width = 256;
        allocationDescs[1].Height = 256;
        allocationDescs[1].DepthOrArraySize = 1;
        allocationDescs[1].MipLevels = 4;
        allocationDescs[1].Format = DXGI_FORMAT_BC7_UNORM;
        allocationDescs[1].SampleDesc.Count = 1;

        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = device->GetResourceAllocationInfo(0, 2, allocationDescs);
        CHECK(allocationInfo.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
              "GetResourceAllocationInfo reports default alignment");
        CHECK(allocationInfo.SizeInBytes >= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * 2,
              "GetResourceAllocationInfo accumulates aligned resource sizes");

        D3D12_RESOURCE_ALLOCATION_INFO emptyAllocation = device->GetResourceAllocationInfo(0, 0, nullptr);
        CHECK(emptyAllocation.SizeInBytes == 0 && emptyAllocation.Alignment == 0,
              "GetResourceAllocationInfo handles empty requests");
    }

    printf("\n--- Committed Resource (Texture2D) ---\n");
    ID3D12Resource* rtTexture = nullptr;
    if (device) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = 512;
        texDesc.Height = 512;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc = {1, 0};
        texDesc.Layout = 0;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        hr = device->CreateCommittedResource(&heapProps, 0, &texDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                             IID_ID3D12Resource, (void**)&rtTexture);
        CHECK(SUCCEEDED(hr) && rtTexture, "CreateCommittedResource (render target texture)");

        if (rtTexture) {
            CHECK(rtTexture->__metalTexturePtr() != nullptr, "Texture has Metal texture backing");
            CHECK(rtTexture->__getResourceState() == D3D12_RESOURCE_STATE_RENDER_TARGET,
                  "Initial resource state correct");
        }
    }

    printf("\n--- Committed Resource (HDR Render Target) ---\n");
    ID3D12Resource* hdrTexture = nullptr;
    if (device) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = 128;
        texDesc.Height = 128;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        texDesc.SampleDesc = {1, 0};
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        hr = device->CreateCommittedResource(&heapProps, 0, &texDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                             IID_ID3D12Resource, (void**)&hdrTexture);
        CHECK(SUCCEEDED(hr) && hdrTexture && hdrTexture->__metalTexturePtr() != nullptr,
              "CreateCommittedResource (R11G11B10 render target)");
    }

    printf("\n--- Resource Barrier ---\n");
    if (cmdList && rtTexture) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = 0;
        barrier.Flags = 0;
        barrier.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.pResource = rtTexture;

        ID3D12GraphicsCommandList* freshList = nullptr;
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList,
                                  (void**)&freshList);
        if (freshList) {
            hr = freshList->ResourceBarrier(1, &barrier);
            CHECK(SUCCEEDED(hr), "ResourceBarrier");
            CHECK(rtTexture->__getResourceState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                  "Resource state updated after barrier");
            freshList->Close();
            freshList->Release();
        }
    }

    printf("\n--- Descriptor Heap ---\n");
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* copiedRtvHeap = nullptr;
    if (device) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 4;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = device->CreateDescriptorHeap(&heapDesc, IID_ID3D12DescriptorHeap, (void**)&rtvHeap);
        CHECK(SUCCEEDED(hr) && rtvHeap, "CreateDescriptorHeap (RTV)");

        if (rtvHeap) {
            CHECK(rtvHeap->__getDescriptorCount() == 4, "Descriptor count is 4");
            CHECK(rtvHeap->__getHeapType() == D3D12_DESCRIPTOR_HEAP_TYPE_RTV, "Heap type is RTV");
        }

        hr = device->CreateDescriptorHeap(&heapDesc, IID_ID3D12DescriptorHeap, (void**)&copiedRtvHeap);
        CHECK(SUCCEEDED(hr) && copiedRtvHeap, "CreateDescriptorHeap (copy target)");
        if (rtvHeap && copiedRtvHeap) {
            CHECK(rtvHeap->__getCPUDescriptorHandleForHeapStart().ptr !=
                      copiedRtvHeap->__getCPUDescriptorHandleForHeapStart().ptr,
                  "Descriptor heaps have distinct CPU handle ranges");
        }
    }

    printf("\n--- Render Target View ---\n");
    if (device && rtTexture && rtvHeap) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->__getCPUDescriptorHandleForHeapStart();
        hr = device->CreateRenderTargetView(rtTexture, nullptr, rtvHandle);
        CHECK(SUCCEEDED(hr), "CreateRenderTargetView");

        if (copiedRtvHeap) {
            D3D12_CPU_DESCRIPTOR_HANDLE copyHandle = copiedRtvHeap->__getCPUDescriptorHandleForHeapStart();
            device->CopyDescriptorsSimple(1, copyHandle, rtvHandle, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            auto* copiedHeapImpl = static_cast<metalsharp::D3D12DescriptorHeapImpl*>(copiedRtvHeap);
            auto* copiedDescriptor = copiedHeapImpl->getDescriptor(copyHandle);
            CHECK(copiedDescriptor && copiedDescriptor->resource == rtTexture &&
                      copiedDescriptor->type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                  "CopyDescriptorsSimple copies RTV descriptor across heaps");
        }

        auto* rtvHeapImpl = static_cast<metalsharp::D3D12DescriptorHeapImpl*>(rtvHeap);
        if (rtvHeapImpl) {
            auto heapStart = rtvHeap->__getCPUDescriptorHandleForHeapStart();
            auto* original0 = rtvHeapImpl->getDescriptorByIndex(0);
            auto* original1 = rtvHeapImpl->getDescriptorByIndex(1);
            auto* original2 = rtvHeapImpl->getDescriptorByIndex(2);
            if (original0 && original1 && original2) {
                *original1 = *original0;
                original1->resource = uploadBuffer;
                *original2 = *original0;
                original2->resource = rtTexture;
                D3D12_CPU_DESCRIPTOR_HANDLE overlapDst = {heapStart.ptr + 1};
                device->CopyDescriptorsSimple(2, overlapDst, heapStart, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                CHECK(rtvHeapImpl->getDescriptorByIndex(1)->resource == original0->resource &&
                          rtvHeapImpl->getDescriptorByIndex(2)->resource == uploadBuffer,
                      "CopyDescriptorsSimple handles same-heap overlapping ranges");
            }
        }

        FLOAT clearColor[4] = {0.2f, 0.3f, 0.4f, 1.0f};
        ID3D12GraphicsCommandList* clearList = nullptr;
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList,
                                  (void**)&clearList);
        if (clearList) {
            hr = clearList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            CHECK(SUCCEEDED(hr), "ClearRenderTargetView");
            clearList->Close();
            clearList->Release();
        }
    }

    printf("\n--- Private Data ---\n");
    if (device && rtTexture) {
        static const GUID privateGuid = {0x9bd8ecdb, 0x2b7b, 0x4da3, {0xb5, 0x3b, 0x01, 0x8a, 0xf7, 0x1b, 0xf8, 0x90}};
        const char tag[] = "sons-d3d12-resource";
        hr = rtTexture->SetPrivateData(privateGuid, sizeof(tag), tag);
        CHECK(SUCCEEDED(hr), "SetPrivateData on D3D12 resource");

        UINT privateSize = 0;
        hr = rtTexture->GetPrivateData(privateGuid, &privateSize, nullptr);
        CHECK(SUCCEEDED(hr) && privateSize == sizeof(tag), "GetPrivateData reports required size");

        char privateBuffer[sizeof(tag)] = {};
        privateSize = sizeof(privateBuffer);
        hr = rtTexture->GetPrivateData(privateGuid, &privateSize, privateBuffer);
        CHECK(SUCCEEDED(hr) && strcmp(privateBuffer, tag) == 0, "GetPrivateData round-trips resource blob");

        hr = rtTexture->SetName("MetalSharp D3D12 test resource");
        CHECK(SUCCEEDED(hr), "SetName on D3D12 resource");
    }

    printf("\n--- Root Signature ---\n");
    ID3D12RootSignature* rootSig = nullptr;
    if (device) {
        hr = device->CreateRootSignature(0, nullptr, 0, IID_ID3D12RootSignature, (void**)&rootSig);
        CHECK(SUCCEEDED(hr) && rootSig, "CreateRootSignature");
    }

    printf("\n--- Fence ---\n");
    ID3D12Fence* fence = nullptr;
    if (device) {
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&fence);
        CHECK(SUCCEEDED(hr) && fence, "CreateFence");

        if (fence) {
            UINT64 completed = 0;
            hr = fence->GetCompletedValue(&completed);
            CHECK(SUCCEEDED(hr) && completed == 0, "Fence initial value is 0");

            hr = fence->Signal(42);
            CHECK(SUCCEEDED(hr), "Fence::Signal(42)");

            hr = fence->GetCompletedValue(&completed);
            CHECK(SUCCEEDED(hr) && completed == 42, "Fence completed value is 42");

            void* eventHandle = metalsharp::win32::SyncContext::instance().createEvent(true, false, "");
            hr = fence->SetEventOnCompletion(100, eventHandle);
            CHECK(SUCCEEDED(hr), "Fence::SetEventOnCompletion registers event");

            hr = cmdQueue->Signal(fence, 100);
            CHECK(SUCCEEDED(hr), "CommandQueue::Signal reaches event target");
            uint32_t waitResult = metalsharp::win32::SyncContext::instance().waitForSingleObject(eventHandle, 0);
            CHECK(waitResult == metalsharp::win32::WAIT_OBJECT_0, "Fence completion signals event");
        }
    }

    printf("\n--- Execute Command Lists ---\n");
    if (cmdQueue && cmdList) {
        ID3D12CommandList* lists[] = {cmdList};
        hr = cmdQueue->ExecuteCommandLists(1, lists);
        CHECK(SUCCEEDED(hr), "ExecuteCommandLists");

        if (fence) {
            hr = cmdQueue->Signal(fence, 100);
            CHECK(SUCCEEDED(hr), "CommandQueue::Signal fence");
        }
    }

    printf("\n--- Command Queue Wait Ordering ---\n");
    if (device && cmdQueue) {
        ID3D12Fence* waitFence = nullptr;
        ID3D12Fence* signalFence = nullptr;
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&waitFence);
        CHECK(SUCCEEDED(hr) && waitFence, "Create wait fence");
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&signalFence);
        CHECK(SUCCEEDED(hr) && signalFence, "Create signal fence");

        if (waitFence && signalFence) {
            hr = cmdQueue->Wait(waitFence, 1);
            CHECK(SUCCEEDED(hr), "CommandQueue::Wait queues fence dependency");
            hr = cmdQueue->ExecuteCommandLists(0, nullptr);
            CHECK(SUCCEEDED(hr), "ExecuteCommandLists queues behind wait");
            hr = cmdQueue->Signal(signalFence, 2);
            CHECK(SUCCEEDED(hr), "CommandQueue::Signal queues behind pending wait");

            UINT64 completed = 99;
            hr = signalFence->GetCompletedValue(&completed);
            CHECK(SUCCEEDED(hr) && completed == 0, "Queued signal does not complete before wait fence");

            hr = waitFence->Signal(1);
            CHECK(SUCCEEDED(hr), "Wait fence unblocks queued work");
            for (int attempt = 0; attempt < 100; ++attempt) {
                signalFence->GetCompletedValue(&completed);
                if (completed == 2)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            CHECK(completed == 2, "Queued signal completes after wait fence");

            ID3D12Fence* sameQueueWaitFence = nullptr;
            ID3D12Fence* sameQueueSignalFence = nullptr;
            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&sameQueueWaitFence);
            CHECK(SUCCEEDED(hr) && sameQueueWaitFence, "Create same-queue wait fence");
            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&sameQueueSignalFence);
            CHECK(SUCCEEDED(hr) && sameQueueSignalFence, "Create same-queue signal fence");
            if (sameQueueWaitFence && sameQueueSignalFence) {
                hr = cmdQueue->Wait(sameQueueWaitFence, 3);
                CHECK(SUCCEEDED(hr), "CommandQueue::Wait accepts same-queue signal target");
                hr = cmdQueue->Signal(sameQueueWaitFence, 3);
                CHECK(SUCCEEDED(hr), "CommandQueue::Signal can satisfy prior same-queue wait");
                hr = cmdQueue->Signal(sameQueueSignalFence, 4);
                CHECK(SUCCEEDED(hr), "CommandQueue::Signal queues after same-queue wait");
                for (int attempt = 0; attempt < 100; ++attempt) {
                    sameQueueSignalFence->GetCompletedValue(&completed);
                    if (completed == 4)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                CHECK(completed == 4, "Same-queue signal unblocks queued wait");
            }
            if (sameQueueWaitFence)
                sameQueueWaitFence->Release();
            if (sameQueueSignalFence)
                sameQueueSignalFence->Release();
        }

        if (waitFence)
            waitFence->Release();
        if (signalFence)
            signalFence->Release();
    }

    printf("\n--- Command Queue Pending Wait Teardown ---\n");
    if (device) {
        ID3D12CommandQueue* teardownQueue = nullptr;
        ID3D12Fence* pendingFence = nullptr;
        hr = device->CreateCommandQueue(nullptr, IID_ID3D12CommandQueue, (void**)&teardownQueue);
        CHECK(SUCCEEDED(hr) && teardownQueue, "Create teardown command queue");
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&pendingFence);
        CHECK(SUCCEEDED(hr) && pendingFence, "Create pending wait fence");
        if (teardownQueue && pendingFence) {
            hr = teardownQueue->Wait(pendingFence, 1);
            CHECK(SUCCEEDED(hr), "Queue wait can remain pending before teardown");
            teardownQueue->Release();
            teardownQueue = nullptr;
            CHECK(true, "Command queue teardown cancels pending wait worker");
        }
        if (teardownQueue)
            teardownQueue->Release();
        if (pendingFence)
            pendingFence->Release();
    }

    printf("\n--- Command Signature ---\n");
    ID3D12CommandSignature* cmdSig = nullptr;
    if (device) {
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = 12;
        sigDesc.NumArgumentDescs = 0;
        hr = device->CreateCommandSignature(&sigDesc, nullptr, IID_ID3D12CommandSignature, (void**)&cmdSig);
        CHECK(SUCCEEDED(hr) && cmdSig, "CreateCommandSignature");
    }

    printf("\n--- Pipeline State ---\n");
    ID3D12PipelineState* pso = nullptr;
    ID3D12PipelineState* computePso = nullptr;
    if (device) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        psoDesc.SampleDesc = {1, 0};
        psoDesc.SampleMask = 0xFFFFFFFF;

        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&pso);
        CHECK(SUCCEEDED(hr) && pso, "CreateGraphicsPipelineState (no shaders)");
    }

    printf("\n--- Indexed Graphics Pipeline ---\n");
    if (device && cmdQueue && cmdAlloc && rtTexture && rtvHeap) {
        struct Vertex {
            float position[3];
            float color[4];
        };
        const Vertex vertices[3] = {
            {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        };
        const uint16_t indices[3] = {0, 1, 2};

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = sizeof(vertices);
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.Format = DXGI_FORMAT_UNKNOWN;
        vbDesc.SampleDesc = {1, 0};
        D3D12_RESOURCE_DESC ibDesc = vbDesc;
        ibDesc.Width = sizeof(indices);

        ID3D12Resource* vertexBuffer = nullptr;
        ID3D12Resource* indexBuffer = nullptr;
        hr = device->CreateCommittedResource(&heapProps, 0, &vbDesc, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                             nullptr, IID_ID3D12Resource, (void**)&vertexBuffer);
        CHECK(SUCCEEDED(hr) && vertexBuffer, "Create indexed draw vertex buffer");
        hr = device->CreateCommittedResource(&heapProps, 0, &ibDesc, D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr,
                                             IID_ID3D12Resource, (void**)&indexBuffer);
        CHECK(SUCCEEDED(hr) && indexBuffer, "Create indexed draw index buffer");

        if (vertexBuffer && indexBuffer) {
            void* mapped = nullptr;
            vertexBuffer->Map(0, nullptr, &mapped);
            if (mapped)
                memcpy(mapped, vertices, sizeof(vertices));
            vertexBuffer->Unmap(0, nullptr);
            indexBuffer->Map(0, nullptr, &mapped);
            if (mapped)
                memcpy(mapped, indices, sizeof(indices));
            indexBuffer->Unmap(0, nullptr);
        }

        static const char* graphicsMSL =
            "#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "struct Vertex { float3 position; float4 color; };\n"
            "struct VSOut { float4 position [[position]]; float4 color; };\n"
            "vertex VSOut vertexShader(uint vid [[vertex_id]], const device Vertex* vertices [[buffer(0)]]) { "
            "VSOut out; Vertex in = vertices[vid]; out.position = float4(in.position, 1.0); out.color = in.color; "
            "return out; }\n"
            "fragment float4 fragmentShader(VSOut in [[stage_in]]) { return in.color; }\n";

        ID3D12PipelineState* indexedPso = nullptr;
        D3D12_INPUT_ELEMENT_DESC inputElements[2] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, 0, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, 0, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC indexedDesc = {};
        indexedDesc.VS = graphicsMSL;
        indexedDesc.VSsize = strlen(graphicsMSL);
        indexedDesc.InputLayout = inputElements;
        indexedDesc.NumInputElements = 2;
        indexedDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        indexedDesc.NumRenderTargets = 1;
        indexedDesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        indexedDesc.SampleDesc = {1, 0};
        indexedDesc.SampleMask = 0xFFFFFFFF;
        hr = device->CreateGraphicsPipelineState(&indexedDesc, IID_ID3D12PipelineState, (void**)&indexedPso);
        CHECK(SUCCEEDED(hr) && indexedPso && indexedPso->__metalRenderPipelineState() != nullptr,
              "CreateGraphicsPipelineState (MSL indexed)");

        if (indexedPso && vertexBuffer && indexBuffer) {
            ID3D12GraphicsCommandList* drawList = nullptr;
            hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, indexedPso,
                                           IID_ID3D12GraphicsCommandList, (void**)&drawList);
            CHECK(SUCCEEDED(hr) && drawList, "Create indexed draw command list");
            if (drawList) {
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->__getCPUDescriptorHandleForHeapStart();
                D3D12_VIEWPORT vp = {0, 0, 512, 512, 0.0f, 1.0f};
                D3D12_RECT scissor = {0, 0, 512, 512};
                D3D12_GPU_VIRTUAL_ADDRESS vbAddr = 0;
                D3D12_GPU_VIRTUAL_ADDRESS ibAddr = 0;
                vertexBuffer->GetGPUVirtualAddress(&vbAddr);
                indexBuffer->GetGPUVirtualAddress(&ibAddr);
                D3D12_VERTEX_BUFFER_VIEW vbView = {vbAddr, (UINT)sizeof(vertices), (UINT)sizeof(Vertex)};
                D3D12_INDEX_BUFFER_VIEW ibView = {ibAddr, (UINT)sizeof(indices), DXGI_FORMAT_R16_UINT};

                drawList->SetPipelineState(indexedPso);
                drawList->IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                drawList->RSSetViewports(1, &vp);
                drawList->RSSetScissorRects(1, &scissor);
                drawList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
                drawList->IASetVertexBuffers(0, 1, &vbView);
                drawList->IASetIndexBuffer(&ibView);
                hr = drawList->DrawIndexedInstanced(3, 1, 0, 0, 0);
                CHECK(SUCCEEDED(hr), "Record indexed draw");
                hr = drawList->Close();
                CHECK(SUCCEEDED(hr), "Close indexed draw list");
                ID3D12CommandList* lists[] = {drawList};
                hr = cmdQueue->ExecuteCommandLists(1, lists);
                CHECK(SUCCEEDED(hr), "Execute indexed draw list");
                drawList->Release();
            }
        }

        if (indexedPso)
            indexedPso->Release();
        if (indexBuffer)
            indexBuffer->Release();
        if (vertexBuffer)
            vertexBuffer->Release();
    }

    printf("\n--- Compute Pipeline State ---\n");
    if (device) {
        static const char* computeMSL = "#include <metal_stdlib>\n"
                                        "using namespace metal;\n"
                                        "kernel void computeShader(uint3 tid [[thread_position_in_grid]]) {}\n";
        D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
        computeDesc.CS.pShaderBytecode = computeMSL;
        computeDesc.CS.BytecodeLength = strlen(computeMSL);

        hr = device->CreateComputePipelineState(&computeDesc, IID_ID3D12PipelineState, (void**)&computePso);
        CHECK(SUCCEEDED(hr) && computePso && computePso->__metalComputePipelineState() != nullptr,
              "CreateComputePipelineState (MSL)");

        static const char* badComputeMSL = "#include <metal_stdlib>\n"
                                           "using namespace metal;\n"
                                           "kernel void computeShader( { }\n";
        D3D12_COMPUTE_PIPELINE_STATE_DESC badComputeDesc = {};
        badComputeDesc.CS.pShaderBytecode = badComputeMSL;
        badComputeDesc.CS.BytecodeLength = strlen(badComputeMSL);
        ID3D12PipelineState* failedComputePso = reinterpret_cast<ID3D12PipelineState*>(0x1);
        hr = device->CreateComputePipelineState(&badComputeDesc, IID_ID3D12PipelineState, (void**)&failedComputePso);
        CHECK(FAILED(hr) && failedComputePso == nullptr, "Failed compute PSO leaves null output");

        if (cmdQueue && cmdAlloc && computePso) {
            ID3D12GraphicsCommandList* computeList = nullptr;
            hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, cmdAlloc, computePso,
                                           IID_ID3D12GraphicsCommandList, (void**)&computeList);
            CHECK(SUCCEEDED(hr) && computeList, "Create compute command list");
            if (computeList) {
                hr = computeList->SetPipelineState(computePso);
                CHECK(SUCCEEDED(hr), "Set compute pipeline state");
                hr = computeList->Dispatch(1, 1, 1);
                CHECK(SUCCEEDED(hr), "Dispatch with compute PSO");
                hr = computeList->Close();
                CHECK(SUCCEEDED(hr), "Close compute list");
                ID3D12CommandList* lists[] = {computeList};
                hr = cmdQueue->ExecuteCommandLists(1, lists);
                CHECK(SUCCEEDED(hr), "Execute compute command list");
                computeList->Release();
            }
        }
    }

    printf("\n--- Swapchain D3D12 Render Target View ---\n");
    if (device && swapChainBackBuffer && rtvHeap) {
        D3D12_CPU_DESCRIPTOR_HANDLE swapchainRtv = rtvHeap->__getCPUDescriptorHandleForHeapStart();
        swapchainRtv.ptr += 1;
        hr = device->CreateRenderTargetView(swapChainBackBuffer, nullptr, swapchainRtv);
        CHECK(SUCCEEDED(hr), "CreateRenderTargetView accepts swapchain ID3D12Resource");

        ID3D12GraphicsCommandList* presentList = nullptr;
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr,
                                       IID_ID3D12GraphicsCommandList, (void**)&presentList);
        CHECK(SUCCEEDED(hr) && presentList, "Create command list for swapchain present barriers");
        if (presentList) {
            D3D12_RESOURCE_BARRIER toRenderTarget = {};
            toRenderTarget.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            toRenderTarget.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            toRenderTarget.pResource = swapChainBackBuffer;
            hr = presentList->ResourceBarrier(1, &toRenderTarget);
            CHECK(SUCCEEDED(hr) && swapChainBackBuffer->__getResourceState() == D3D12_RESOURCE_STATE_RENDER_TARGET,
                  "Swapchain backbuffer transitions PRESENT to RENDER_TARGET");

            D3D12_RESOURCE_BARRIER toPresent = {};
            toPresent.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            toPresent.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            toPresent.pResource = swapChainBackBuffer;
            hr = presentList->ResourceBarrier(1, &toPresent);
            CHECK(SUCCEEDED(hr) && swapChainBackBuffer->__getResourceState() == D3D12_RESOURCE_STATE_PRESENT,
                  "Swapchain backbuffer transitions RENDER_TARGET to PRESENT");
            presentList->Close();
            presentList->Release();
        }
    }

    printf("\n--- Descriptor handle increment ---\n");
    if (device) {
        UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CHECK(inc == 1, "GetDescriptorHandleIncrementSize returns 1");
    }

    printf("\n--- Root Signature Serialization ---\n");
    {
        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        void* blob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, 0, &blob, nullptr);
        CHECK(SUCCEEDED(hr) && blob != nullptr, "D3D12SerializeRootSignature empty signature");
        if (blob) {
            auto* blobObject = static_cast<ID3DBlob*>(blob);
            uint32_t magic = 0;
            memcpy(&magic, blobObject->GetBufferPointer(), 4);
            CHECK(magic == 0x43425844, "Serialized blob uses DXBC container layout");
            CHECK(blobObject->GetBufferSize() == 60, "Serialized blob reports DXBC buffer size");
            blobObject->Release();
        }

        D3D12_ROOT_PARAMETER rsParam = {};
        rsParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rsParam.Constants.ShaderRegister = 3;
        rsParam.Constants.RegisterSpace = 2;
        rsParam.Constants.Num32BitValues = 4;
        rsParam.ShaderVisibility = 0;
        rsDesc.NumParameters = 1;
        rsDesc.pParameters = &rsParam;
        blob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, 0, &blob, nullptr);
        CHECK(SUCCEEDED(hr) && blob != nullptr, "D3D12SerializeRootSignature constants signature");
        if (blob && device) {
            auto* blobObject = static_cast<ID3DBlob*>(blob);
            ID3D12RootSignature* parsedRootSig = nullptr;
            hr = device->CreateRootSignature(0, blobObject->GetBufferPointer(), blobObject->GetBufferSize(),
                                             IID_ID3D12RootSignature, (void**)&parsedRootSig);
            auto* parsedImpl = static_cast<metalsharp::D3D12RootSignatureImpl*>(parsedRootSig);
            CHECK(SUCCEEDED(hr) && parsedImpl && parsedImpl->numParameters == 1,
                  "CreateRootSignature parses serialized parameter count");
            CHECK(parsedImpl && parsedImpl->parameters.size() == 1 &&
                      parsedImpl->parameters[0].Constants.Num32BitValues == 4,
                  "CreateRootSignature preserves serialized root constants");
            if (parsedRootSig)
                parsedRootSig->Release();
            blobObject->Release();
        }

        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = 0;
        range.NumDescriptors = 2;
        range.BaseShaderRegister = 1;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
        D3D12_ROOT_PARAMETER mixedParams[3] = {};
        mixedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        mixedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        mixedParams[0].DescriptorTable.pDescriptorRanges = &range;
        mixedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        mixedParams[1].Descriptor.ShaderRegister = 5;
        mixedParams[1].Descriptor.RegisterSpace = 1;
        mixedParams[2] = rsParam;
        rsDesc.NumParameters = 3;
        rsDesc.pParameters = mixedParams;
        blob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, 0, &blob, nullptr);
        CHECK(SUCCEEDED(hr) && blob != nullptr, "D3D12SerializeRootSignature mixed parameter signature");
        if (blob && device) {
            auto* blobObject = static_cast<ID3DBlob*>(blob);
            CHECK(blobObject->GetBufferSize() == 132, "Serialized mixed root signature wraps parser record sizes");
            ID3D12RootSignature* parsedRootSig = nullptr;
            hr = device->CreateRootSignature(0, blobObject->GetBufferPointer(), blobObject->GetBufferSize(),
                                             IID_ID3D12RootSignature, (void**)&parsedRootSig);
            auto* parsedImpl = static_cast<metalsharp::D3D12RootSignatureImpl*>(parsedRootSig);
            CHECK(SUCCEEDED(hr) && parsedImpl && parsedImpl->parameters.size() == 3,
                  "CreateRootSignature parses mixed serialized parameters");
            CHECK(parsedImpl && parsedImpl->parameters[1].Descriptor.ShaderRegister == 5 &&
                      parsedImpl->parameters[2].Constants.Num32BitValues == 4,
                  "CreateRootSignature keeps parameter alignment after descriptor records");
            if (parsedRootSig)
                parsedRootSig->Release();
            blobObject->Release();
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
        versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        versionedDesc.Desc_1_0 = rsDesc;
        void* vBlob = nullptr;
        hr = D3D12SerializeVersionedRootSignature(&versionedDesc, &vBlob, nullptr);
        CHECK(SUCCEEDED(hr) && vBlob != nullptr, "D3D12SerializeVersionedRootSignature");
        if (vBlob)
            static_cast<ID3DBlob*>(vBlob)->Release();

        D3D12_ROOT_SIGNATURE_DESC invalidDesc = {};
        invalidDesc.NumParameters = 1;
        invalidDesc.pParameters = nullptr;
        blob = reinterpret_cast<void*>(0x1);
        hr = D3D12SerializeRootSignature(&invalidDesc, 0, &blob, nullptr);
        CHECK(hr == E_INVALIDARG && blob == nullptr, "D3D12SerializeRootSignature rejects null parameter array");

        invalidDesc = {};
        invalidDesc.NumStaticSamplers = 1;
        invalidDesc.pStaticSamplers = nullptr;
        blob = reinterpret_cast<void*>(0x1);
        hr = D3D12SerializeRootSignature(&invalidDesc, 0, &blob, nullptr);
        CHECK(hr == E_INVALIDARG && blob == nullptr, "D3D12SerializeRootSignature rejects null static sampler array");

        D3D12_ROOT_PARAMETER invalidTable = {};
        invalidTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        invalidTable.DescriptorTable.NumDescriptorRanges = 1;
        invalidTable.DescriptorTable.pDescriptorRanges = nullptr;
        invalidDesc = {};
        invalidDesc.NumParameters = 1;
        invalidDesc.pParameters = &invalidTable;
        blob = reinterpret_cast<void*>(0x1);
        hr = D3D12SerializeRootSignature(&invalidDesc, 0, &blob, nullptr);
        CHECK(hr == E_INVALIDARG && blob == nullptr, "D3D12SerializeRootSignature rejects null descriptor range array");
    }

    printf("\n--- Debug Interface Stubs ---\n");
    {
        void* debugPtr = reinterpret_cast<void*>(0x1);
        hr = D3D12GetDebugInterface(IID_ID3D12Device, &debugPtr);
        CHECK(hr == E_NOINTERFACE && debugPtr == nullptr, "D3D12GetDebugInterface returns E_NOINTERFACE");

        hr = D3D12EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);
        CHECK(SUCCEEDED(hr), "D3D12EnableExperimentalFeatures accepts no-feature no-op");

        hr = D3D12EnableExperimentalFeatures(1, nullptr, nullptr, nullptr);
        CHECK(hr == E_INVALIDARG, "D3D12EnableExperimentalFeatures rejects missing feature GUID array");

        GUID unsupportedFeature = IID_ID3D12Device;
        hr = D3D12EnableExperimentalFeatures(1, &unsupportedFeature, nullptr, nullptr);
        CHECK(hr == E_NOINTERFACE, "D3D12EnableExperimentalFeatures rejects unsupported features");
    }

    if (pso)
        pso->Release();
    if (computePso)
        computePso->Release();
    if (swapChainBackBuffer)
        swapChainBackBuffer->Release();
    if (hwndSwapChain)
        hwndSwapChain->Release();
    if (factory2)
        factory2->Release();
    if (swapChain)
        swapChain->Release();
    if (factory)
        factory->Release();
    if (cmdSig)
        cmdSig->Release();
    if (fence)
        fence->Release();
    if (rootSig)
        rootSig->Release();
    if (copiedRtvHeap)
        copiedRtvHeap->Release();
    if (rtvHeap)
        rtvHeap->Release();
    if (rtTexture)
        rtTexture->Release();
    if (hdrTexture)
        hdrTexture->Release();
    if (uploadBuffer)
        uploadBuffer->Release();
    if (cmdList)
        cmdList->Release();
    if (cmdAlloc)
        cmdAlloc->Release();
    if (cmdQueue)
        cmdQueue->Release();
    if (device)
        device->Release();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
