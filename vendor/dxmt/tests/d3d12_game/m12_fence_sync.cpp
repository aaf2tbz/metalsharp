#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <cstring>
#include <cstdio>
#include <windows.h>

template <typename T>
static void releaseIf(T *&ptr) {
  if (ptr) {
    ptr->Release();
    ptr = nullptr;
  }
}

static bool checkHr(const char *label, HRESULT hr) {
  if (SUCCEEDED(hr)) {
    std::printf("[PASS] %s\n", label);
    return true;
  }
  std::printf("[FAIL] %s: 0x%08lx\n", label, static_cast<unsigned long>(hr));
  return false;
}

struct SignalThreadCtx {
  ID3D12Fence *fence;
  UINT64 value;
  DWORD delayMs;
};

static DWORD WINAPI signalAfterDelay(void *arg) {
  auto *ctx = static_cast<SignalThreadCtx *>(arg);
  Sleep(ctx->delayMs);
  ctx->fence->Signal(ctx->value);
  ctx->fence->Release();
  delete ctx;
  return 0;
}

static bool compileShader(const char *label, const char *source, const char *entry,
                          const char *target, ID3DBlob **blob) {
  ID3DBlob *errors = nullptr;
  HRESULT hr = D3DCompile(source, std::strlen(source), label, nullptr, nullptr,
                          entry, target, 0, 0, blob, &errors);
  if (FAILED(hr)) {
    if (errors)
      std::printf("[FAIL] %s compile: %s\n", label,
                  static_cast<const char *>(errors->GetBufferPointer()));
    else
      std::printf("[FAIL] %s compile: 0x%08lx\n", label,
                  static_cast<unsigned long>(hr));
    releaseIf(errors);
    return false;
  }
  releaseIf(errors);
  std::printf("[PASS] %s compiled (%zu bytes)\n", label, (*blob)->GetBufferSize());
  return true;
}

static bool createRootSignature(ID3D12Device *device,
                                ID3D12RootSignature **rootSig) {
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ID3DBlob *blob = nullptr;
  ID3DBlob *errors = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &blob, &errors);
  if (FAILED(hr)) {
    if (errors)
      std::printf("[FAIL] SerializeRootSignature: %s\n",
                  static_cast<const char *>(errors->GetBufferPointer()));
    else
      std::printf("[FAIL] SerializeRootSignature: 0x%08lx\n",
                  static_cast<unsigned long>(hr));
    releaseIf(errors);
    return false;
  }
  releaseIf(errors);

  hr = device->CreateRootSignature(0, blob->GetBufferPointer(),
                                   blob->GetBufferSize(),
                                   IID_PPV_ARGS(rootSig));
  releaseIf(blob);
  return checkHr("CreateRootSignature smoke", hr);
}

static bool createGraphicsPsoSmoke(ID3D12Device *device, const char *label,
                                   DXGI_FORMAT dsvFormat,
                                   bool includeTessellation) {
  static const char *triangleHlsl =
      "struct VSIn { float3 pos : POSITION; };\n"
      "struct VSOut { float4 pos : SV_Position; };\n"
      "VSOut VSMain(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); return o; }\n"
      "float4 PSMain(VSOut i) : SV_Target { return float4(1.0, 0.0, 1.0, 1.0); }\n";
  static const char *tessHlsl =
      "struct VSIn { float3 pos : POSITION; };\n"
      "struct VSOut { float4 pos : SV_Position; };\n"
      "VSOut VSMain(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); return o; }\n"
      "struct HSConst { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };\n"
      "HSConst HSConstMain(InputPatch<VSOut, 3> patch, uint pid : SV_PrimitiveID) {\n"
      "  HSConst o; o.edges[0] = 1.0; o.edges[1] = 1.0; o.edges[2] = 1.0; o.inside = 1.0; return o;\n"
      "}\n"
      "[domain(\"tri\")]\n"
      "[partitioning(\"integer\")]\n"
      "[outputtopology(\"triangle_cw\")]\n"
      "[outputcontrolpoints(3)]\n"
      "[patchconstantfunc(\"HSConstMain\")]\n"
      "VSOut HSMain(InputPatch<VSOut, 3> patch, uint i : SV_OutputControlPointID, uint pid : SV_PrimitiveID) {\n"
      "  return patch[i];\n"
      "}\n"
      "[domain(\"tri\")]\n"
      "VSOut DSMain(HSConst c, const OutputPatch<VSOut, 3> patch, float3 uvw : SV_DomainLocation) {\n"
      "  VSOut o; o.pos = patch[0].pos * uvw.x + patch[1].pos * uvw.y + patch[2].pos * uvw.z; return o;\n"
      "}\n"
      "float4 PSMain(VSOut i) : SV_Target { return float4(1.0, 0.0, 1.0, 1.0); }\n";

  const char *source = includeTessellation ? tessHlsl : triangleHlsl;
  ID3DBlob *vsBlob = nullptr;
  ID3DBlob *psBlob = nullptr;
  ID3DBlob *hsBlob = nullptr;
  ID3DBlob *dsBlob = nullptr;
  if (!compileShader("smoke VS", source, "VSMain", "vs_5_0", &vsBlob) ||
      !compileShader("smoke PS", source, "PSMain", "ps_5_0", &psBlob) ||
      (includeTessellation &&
       (!compileShader("smoke HS", source, "HSMain", "hs_5_0", &hsBlob) ||
        !compileShader("smoke DS", source, "DSMain", "ds_5_0", &dsBlob)))) {
    releaseIf(dsBlob);
    releaseIf(hsBlob);
    releaseIf(psBlob);
    releaseIf(vsBlob);
    return false;
  }

  ID3D12RootSignature *rootSig = nullptr;
  if (!createRootSignature(device, &rootSig)) {
    releaseIf(dsBlob);
    releaseIf(hsBlob);
    releaseIf(psBlob);
    releaseIf(vsBlob);
    return false;
  }

  D3D12_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = rootSig;
  desc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  if (includeTessellation) {
    desc.HS = {hsBlob->GetBufferPointer(), hsBlob->GetBufferSize()};
    desc.DS = {dsBlob->GetBufferPointer(), dsBlob->GetBufferSize()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
  } else {
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
  desc.InputLayout = {layout, 1};
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.DSVFormat = dsvFormat;
  desc.SampleDesc.Count = 1;
  desc.SampleMask = UINT_MAX;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.DepthStencilState.DepthEnable = FALSE;
  desc.DepthStencilState.StencilEnable = FALSE;

  ID3D12PipelineState *pso = nullptr;
  HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  releaseIf(pso);
  releaseIf(rootSig);
  releaseIf(dsBlob);
  releaseIf(hsBlob);
  releaseIf(psBlob);
  releaseIf(vsBlob);
  return checkHr(label, hr);
}

static bool createMismatchedVaryingPsoSmoke(ID3D12Device *device) {
  static const char *hlsl =
      "struct VSIn { float3 pos : POSITION; };\n"
      "struct VSOut { float4 pos : SV_Position; };\n"
      "struct PSIn {\n"
      "  float4 pos : SV_Position;\n"
      "  float4 t0 : TEXCOORD0;\n"
      "  float4 t1 : TEXCOORD1;\n"
      "  float4 t2 : TEXCOORD2;\n"
      "  float4 t3 : TEXCOORD3;\n"
      "  float4 t4 : TEXCOORD4;\n"
      "  float4 t6 : TEXCOORD6;\n"
      "};\n"
      "VSOut VSMain(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); return o; }\n"
      "float4 PSMain(PSIn i) : SV_Target {\n"
      "  return float4(i.t0.x + i.t1.y + i.t2.z + i.t3.w + i.t4.x + i.t6.y, 0.0, 1.0, 1.0);\n"
      "}\n";

  ID3DBlob *vsBlob = nullptr;
  ID3DBlob *psBlob = nullptr;
  if (!compileShader("mismatched-varying VS", hlsl, "VSMain", "vs_5_0",
                     &vsBlob) ||
      !compileShader("mismatched-varying PS", hlsl, "PSMain", "ps_5_0",
                     &psBlob)) {
    releaseIf(psBlob);
    releaseIf(vsBlob);
    return false;
  }

  ID3D12RootSignature *rootSig = nullptr;
  if (!createRootSignature(device, &rootSig)) {
    releaseIf(psBlob);
    releaseIf(vsBlob);
    return false;
  }

  D3D12_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = rootSig;
  desc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  desc.InputLayout = {layout, 1};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleMask = UINT_MAX;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.DepthStencilState.DepthEnable = FALSE;
  desc.DepthStencilState.StencilEnable = FALSE;

  ID3D12PipelineState *pso = nullptr;
  HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  releaseIf(pso);
  releaseIf(rootSig);
  releaseIf(psBlob);
  releaseIf(vsBlob);
  return checkHr("CreateGraphicsPipelineState mismatched varying padding smoke",
                 hr);
}

static HANDLE signalFenceLater(ID3D12Fence *fence, UINT64 value, DWORD delayMs) {
  fence->AddRef();
  auto *ctx = new SignalThreadCtx{fence, value, delayMs};
  HANDLE thread = CreateThread(nullptr, 0, signalAfterDelay, ctx, 0, nullptr);
  if (!thread) {
    fence->Release();
    delete ctx;
    return nullptr;
  }
  return thread;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  ID3D12Device *device = nullptr;
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(&device));
  if (!checkHr("D3D12CreateDevice", hr))
    return 1;

  ID3D12Device1 *device1 = nullptr;
  hr = device->QueryInterface(IID_PPV_ARGS(&device1));
  if (!checkHr("QueryInterface ID3D12Device1", hr))
    return 1;

  LUID d3d12Luid = device->GetAdapterLuid();
  if (!d3d12Luid.HighPart && !d3d12Luid.LowPart) {
    std::printf("[FAIL] D3D12 adapter LUID is zero\n");
    return 1;
  }
  IDXGIFactory4 *factory = nullptr;
  hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (!checkHr("CreateDXGIFactory1", hr))
    return 1;
  IDXGIAdapter1 *adapter = nullptr;
  hr = factory->EnumAdapterByLuid(d3d12Luid, IID_PPV_ARGS(&adapter));
  if (!checkHr("EnumAdapterByLuid(D3D12 LUID)", hr))
    return 1;
  std::printf("[PASS] D3D12 adapter LUID matches DXGI: %08lx:%08lx\n",
              static_cast<unsigned long>(d3d12Luid.HighPart),
              static_cast<unsigned long>(d3d12Luid.LowPart));
  adapter->Release();
  factory->Release();

  D3D12_RESOURCE_DESC smallBufferDesc = {};
  smallBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  smallBufferDesc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
  smallBufferDesc.Width = 176;
  smallBufferDesc.Height = 1;
  smallBufferDesc.DepthOrArraySize = 1;
  smallBufferDesc.MipLevels = 1;
  smallBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  smallBufferDesc.SampleDesc.Count = 1;
  smallBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_RESOURCE_ALLOCATION_INFO smallBufferInfo =
      device->GetResourceAllocationInfo(0, 1, &smallBufferDesc);
  if (smallBufferInfo.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT ||
      smallBufferInfo.SizeInBytes != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::printf("[FAIL] Small-buffer allocation info: size=%llu align=%llu\n",
                static_cast<unsigned long long>(smallBufferInfo.SizeInBytes),
                static_cast<unsigned long long>(smallBufferInfo.Alignment));
    return 1;
  }
  std::printf("[PASS] Small-buffer allocation info uses 4 KB alignment\n");

  D3D12_RESOURCE_DESC defaultBufferDesc = smallBufferDesc;
  defaultBufferDesc.Alignment = 0;
  D3D12_RESOURCE_ALLOCATION_INFO defaultBufferInfo =
      device->GetResourceAllocationInfo(0, 1, &defaultBufferDesc);
  if (defaultBufferInfo.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
      defaultBufferInfo.SizeInBytes != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::printf("[FAIL] Default-buffer allocation info: size=%llu align=%llu\n",
                static_cast<unsigned long long>(defaultBufferInfo.SizeInBytes),
                static_cast<unsigned long long>(defaultBufferInfo.Alignment));
    return 1;
  }
  std::printf("[PASS] Default-buffer allocation info keeps 64 KB alignment\n");

  if (!createGraphicsPsoSmoke(device, "CreateGraphicsPipelineState D24S8 smoke",
                              DXGI_FORMAT_D24_UNORM_S8_UINT, false))
    return 1;

  if (!createGraphicsPsoSmoke(device,
                              "CreateGraphicsPipelineState tessellation fallback smoke",
                              DXGI_FORMAT_D24_UNORM_S8_UINT, true))
    return 1;

  if (!createMismatchedVaryingPsoSmoke(device))
    return 1;

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ID3D12CommandQueue *queue = nullptr;
  hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
  if (!checkHr("CreateCommandQueue", hr))
    return 1;

  ID3D12Fence *queueWaitFence = nullptr;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                           IID_PPV_ARGS(&queueWaitFence));
  if (!checkHr("CreateFence queue wait", hr))
    return 1;

  DWORD start = GetTickCount();
  hr = queue->Wait(queueWaitFence, 1);
  DWORD elapsed = GetTickCount() - start;
  if (!checkHr("CommandQueue::Wait unsignaled fence returns", hr))
    return 1;
  if (elapsed > 500) {
    std::printf("[FAIL] CommandQueue::Wait blocked for %lu ms\n",
                static_cast<unsigned long>(elapsed));
    return 1;
  }
  std::printf("[PASS] CommandQueue::Wait returned in %lu ms\n",
              static_cast<unsigned long>(elapsed));
  queueWaitFence->Signal(1);
  Sleep(50);

  ID3D12Fence *queueSignalFence = nullptr;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                           IID_PPV_ARGS(&queueSignalFence));
  if (!checkHr("CreateFence queue signal", hr))
    return 1;
  HANDLE signalEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  hr = queue->Signal(queueSignalFence, 2);
  if (!checkHr("CommandQueue::Signal", hr))
    return 1;
  hr = queueSignalFence->SetEventOnCompletion(2, signalEvent);
  if (!checkHr("Fence event after queue signal", hr))
    return 1;
  DWORD waitResult = WaitForSingleObject(signalEvent, 1000);
  if (waitResult != WAIT_OBJECT_0) {
    std::printf("[FAIL] Queue signal fence did not complete: wait=0x%08lx\n",
                static_cast<unsigned long>(waitResult));
    return 1;
  }
  std::printf("[PASS] Queue signal fence completed\n");
  CloseHandle(signalEvent);

  ID3D12Fence *multiA = nullptr;
  ID3D12Fence *multiB = nullptr;
  if (!checkHr("CreateFence multiA",
               device->CreateFence(1, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&multiA))) ||
      !checkHr("CreateFence multiB",
               device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&multiB))))
    return 1;
  ID3D12Fence *fences[] = {multiA, multiB};
  UINT64 values[] = {1, 1};
  HANDLE allEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  hr = device1->SetEventOnMultipleFenceCompletion(
      fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, allEvent);
  if (!checkHr("SetEventOnMultipleFenceCompletion ALL", hr))
    return 1;
  waitResult = WaitForSingleObject(allEvent, 50);
  if (waitResult != WAIT_TIMEOUT) {
    std::printf("[FAIL] Multi-fence ALL released before all fences: wait=0x%08lx\n",
                static_cast<unsigned long>(waitResult));
    return 1;
  }
  std::printf("[PASS] Multi-fence ALL held while one fence was unsignaled\n");
  multiB->Signal(1);
  waitResult = WaitForSingleObject(allEvent, 1000);
  if (waitResult != WAIT_OBJECT_0) {
    std::printf("[FAIL] Multi-fence ALL did not release after signal: wait=0x%08lx\n",
                static_cast<unsigned long>(waitResult));
    return 1;
  }
  std::printf("[PASS] Multi-fence ALL released after all fences completed\n");
  CloseHandle(allEvent);

  ID3D12Fence *blockingFence = nullptr;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                           IID_PPV_ARGS(&blockingFence));
  if (!checkHr("CreateFence blocking", hr))
    return 1;
  HANDLE signalThread = signalFenceLater(blockingFence, 5, 50);
  if (!signalThread) {
    std::printf("[FAIL] signalFenceLater\n");
    return 1;
  }
  start = GetTickCount();
  hr = blockingFence->SetEventOnCompletion(5, nullptr);
  elapsed = GetTickCount() - start;
  if (!checkHr("Fence SetEventOnCompletion null event", hr))
    return 1;
  if (elapsed < 20) {
    std::printf("[FAIL] Null-event fence wait returned too early: %lu ms\n",
                static_cast<unsigned long>(elapsed));
    return 1;
  }
  WaitForSingleObject(signalThread, 1000);
  CloseHandle(signalThread);
  std::printf("[PASS] Null-event fence wait blocked for %lu ms\n",
              static_cast<unsigned long>(elapsed));

  blockingFence->Release();
  multiB->Release();
  multiA->Release();
  queueSignalFence->Release();
  queueWaitFence->Release();
  queue->Release();
  device1->Release();
  device->Release();
  fflush(stdout);
  TerminateProcess(GetCurrentProcess(), 0);
}
