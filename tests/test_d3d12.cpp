#include <chrono>
#include <cstdio>
#include <cstring>
#include <d3d/D3D12.h>
#include <metalsharp/D3D12Device.h>
#include <thread>

extern "C" {
HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC*, unsigned int, void**, void**);
HRESULT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, void**, void**);
HRESULT D3D12GetDebugInterface(const GUID&, void**);
HRESULT D3D12EnableExperimentalFeatures(unsigned int, const GUID*, void*, unsigned int*);
}

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
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList,
                                  (void**)&clearList);
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
        }

        if (waitFence)
            waitFence->Release();
        if (signalFence)
            signalFence->Release();
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
            uint32_t version = 0;
            memcpy(&version, blobObject->GetBufferPointer(), 4);
            CHECK(version == 1, "Serialized blob uses raw root signature layout");
            CHECK(blobObject->GetBufferSize() == 16, "Serialized blob reports COM buffer size");
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
            CHECK(blobObject->GetBufferSize() == 88, "Serialized mixed root signature uses parser record sizes");
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
    }

    printf("\n--- Debug Interface Stubs ---\n");
    {
        void* debugPtr = reinterpret_cast<void*>(0x1);
        hr = D3D12GetDebugInterface(IID_ID3D12Device, &debugPtr);
        CHECK(hr == E_NOINTERFACE && debugPtr == nullptr, "D3D12GetDebugInterface returns E_NOINTERFACE");

        hr = D3D12EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);
        CHECK(SUCCEEDED(hr), "D3D12EnableExperimentalFeatures succeeds");
    }

    if (pso)
        pso->Release();
    if (computePso)
        computePso->Release();
    if (cmdSig)
        cmdSig->Release();
    if (fence)
        fence->Release();
    if (rootSig)
        rootSig->Release();
    if (rtvHeap)
        rtvHeap->Release();
    if (rtTexture)
        rtTexture->Release();
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
