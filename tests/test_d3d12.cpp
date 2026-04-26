#include <metalsharp/D3D12Device.h>
#include <d3d/D3D12.h>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

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

    printf("\n--- Command Allocator ---\n");
    ID3D12CommandAllocator* cmdAlloc = nullptr;
    if (device) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator, (void**)&cmdAlloc);
        CHECK(SUCCEEDED(hr) && cmdAlloc, "CreateCommandAllocator");
        if (cmdAlloc) {
            hr = cmdAlloc->Reset();
            CHECK(SUCCEEDED(hr), "CommandAllocator::Reset");
        }
    }

    printf("\n--- Command List ---\n");
    ID3D12GraphicsCommandList* cmdList = nullptr;
    if (device && cmdAlloc) {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&cmdList);
        CHECK(SUCCEEDED(hr) && cmdList, "CreateCommandList");
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

        hr = device->CreateCommittedResource(&heapProps, 0, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_ID3D12Resource, (void**)&uploadBuffer);
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

        hr = device->CreateCommittedResource(&heapProps, 0, &texDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_ID3D12Resource, (void**)&rtTexture);
        CHECK(SUCCEEDED(hr) && rtTexture, "CreateCommittedResource (render target texture)");

        if (rtTexture) {
            CHECK(rtTexture->__metalTexturePtr() != nullptr, "Texture has Metal texture backing");
            CHECK(rtTexture->__getResourceState() == D3D12_RESOURCE_STATE_RENDER_TARGET, "Initial resource state correct");
        }
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
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&freshList);
        if (freshList) {
            hr = freshList->ResourceBarrier(1, &barrier);
            CHECK(SUCCEEDED(hr), "ResourceBarrier");
            CHECK(rtTexture->__getResourceState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "Resource state updated after barrier");
            freshList->Close();
            freshList->Release();
        }
    }

    printf("\n--- Descriptor Heap ---\n");
    ID3D12DescriptorHeap* rtvHeap = nullptr;
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
    }

    printf("\n--- Render Target View ---\n");
    if (device && rtTexture && rtvHeap) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->__getCPUDescriptorHandleForHeapStart();
        hr = device->CreateRenderTargetView(rtTexture, nullptr, rtvHandle);
        CHECK(SUCCEEDED(hr), "CreateRenderTargetView");

        FLOAT clearColor[4] = {0.2f, 0.3f, 0.4f, 1.0f};
        ID3D12GraphicsCommandList* clearList = nullptr;
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&clearList);
        if (clearList) {
            hr = clearList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            CHECK(SUCCEEDED(hr), "ClearRenderTargetView");
            clearList->Close();
            clearList->Release();
        }
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
    if (device) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        psoDesc.SampleDesc = {1, 0};
        psoDesc.SampleMask = 0xFFFFFFFF;

        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&pso);
        CHECK(SUCCEEDED(hr) && pso, "CreateGraphicsPipelineState (no shaders)");
    }

    printf("\n--- Descriptor handle increment ---\n");
    if (device) {
        UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CHECK(inc == 1, "GetDescriptorHandleIncrementSize returns 1");
    }

    if (pso) pso->Release();
    if (cmdSig) cmdSig->Release();
    if (fence) fence->Release();
    if (rootSig) rootSig->Release();
    if (rtvHeap) rtvHeap->Release();
    if (rtTexture) rtTexture->Release();
    if (uploadBuffer) uploadBuffer->Release();
    if (cmdList) cmdList->Release();
    if (cmdAlloc) cmdAlloc->Release();
    if (cmdQueue) cmdQueue->Release();
    if (device) device->Release();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
