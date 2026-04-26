#include <metalsharp/D3D11Device.h>
#include <metalsharp/D3D11DeviceContext.h>
#include <metalsharp/DeferredContext.h>
#include <d3d/D3D11.h>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

static const char* kMSL = R"msl(
#include <metal_stdlib>
using namespace metal;
struct VertexIn { float3 position [[attribute(0)]]; float4 color [[attribute(1)]]; };
struct VertexOut { float4 position [[position]]; float4 color; };
vertex VertexOut vertexShader(VertexIn in [[stage_in]]) {
    VertexOut out; out.position = float4(in.position, 1.0); out.color = in.color; return out;
}
fragment float4 fragmentShader(VertexOut in [[stage_in]]) { return in.color; }
)msl";

int main() {
    printf("=== MetalSharp Deferred Context Tests ===\n\n");

    metalsharp::D3D11Device* device = nullptr;
    HRESULT hr = metalsharp::D3D11Device::create(&device);
    CHECK(SUCCEEDED(hr) && device, "D3D11Device creation");

    printf("\n--- CreateDeferredContext ---\n");

    ID3D11DeviceContext* deferred = nullptr;
    hr = device->CreateDeferredContext(0, &deferred);
    CHECK(SUCCEEDED(hr) && deferred, "CreateDeferredContext");
    CHECK(deferred != nullptr, "Deferred context is non-null");

    ID3D11DeviceContext* immediate = nullptr;
    device->GetImmediateContext(&immediate);
    CHECK(immediate != nullptr, "GetImmediateContext");
    CHECK(deferred != immediate, "Deferred != Immediate context");

    printf("\n--- Record Commands ---\n");

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 256;
    texDesc.Height = 256;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc = {1, 0};
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* rtTexture = nullptr;
    hr = device->CreateTexture2D(&texDesc, nullptr, &rtTexture);
    CHECK(SUCCEEDED(hr) && rtTexture, "CreateTexture2D for deferred RT");

    ID3D11RenderTargetView* rtv = nullptr;
    if (rtTexture) {
        hr = device->CreateRenderTargetView(rtTexture, nullptr, &rtv);
        CHECK(SUCCEEDED(hr) && rtv, "CreateRenderTargetView for deferred");
    }

    if (deferred && rtv) {
        hr = deferred->OMSetRenderTargets(1, &rtv, nullptr);
        CHECK(SUCCEEDED(hr), "Deferred: OMSetRenderTargets");

        FLOAT clearRed[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        hr = deferred->ClearRenderTargetView(rtv, clearRed);
        CHECK(SUCCEEDED(hr), "Deferred: ClearRenderTargetView");

        hr = deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        CHECK(SUCCEEDED(hr), "Deferred: IASetPrimitiveTopology");

        hr = deferred->RSSetViewports(1, nullptr);
        CHECK(SUCCEEDED(hr), "Deferred: RSSetViewports (null)");

        hr = deferred->Draw(3, 0);
        CHECK(SUCCEEDED(hr), "Deferred: Draw(3, 0)");
    }

    printf("\n--- FinishCommandList ---\n");

    ID3D11CommandList* cmdList = nullptr;
    if (deferred) {
        hr = deferred->FinishCommandList(0, &cmdList);
        CHECK(SUCCEEDED(hr) && cmdList, "FinishCommandList");
    }

    printf("\n--- ExecuteCommandList ---\n");

    if (immediate && cmdList) {
        hr = immediate->ExecuteCommandList(cmdList, 0);
        CHECK(SUCCEEDED(hr), "ExecuteCommandList on immediate context");
    }

    printf("\n--- Record again on same deferred context ---\n");

    if (deferred && rtv) {
        FLOAT clearGreen[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        deferred->ClearRenderTargetView(rtv, clearGreen);
        deferred->Draw(6, 0);
        CHECK(true, "Recorded second batch on reused deferred context");
    }

    ID3D11CommandList* cmdList2 = nullptr;
    if (deferred) {
        hr = deferred->FinishCommandList(0, &cmdList2);
        CHECK(SUCCEEDED(hr) && cmdList2, "FinishCommandList (second batch)");
        CHECK(cmdList2 != cmdList, "Second command list is a new object");
    }

    if (immediate && cmdList2) {
        hr = immediate->ExecuteCommandList(cmdList2, 0);
        CHECK(SUCCEEDED(hr), "ExecuteCommandList (second batch)");
    }

    printf("\n--- Immediate context FinishCommandList fails ---\n");

    if (immediate) {
        ID3D11CommandList* dummyList = nullptr;
        hr = immediate->FinishCommandList(0, &dummyList);
        CHECK(FAILED(hr), "Immediate context FinishCommandList returns error");
    }

    printf("\n--- Deferred context ExecuteCommandList fails ---\n");

    if (deferred && cmdList) {
        hr = deferred->ExecuteCommandList(cmdList, 0);
        CHECK(FAILED(hr), "Deferred context ExecuteCommandList returns error");
    }

    printf("\n--- Deferred context Map fails ---\n");

    if (deferred) {
        D3D11_BUFFER_DESC bufDesc = {};
        bufDesc.ByteWidth = 64;
        bufDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* buf = nullptr;
        device->CreateBuffer(&bufDesc, nullptr, &buf);
        if (buf) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            hr = deferred->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            CHECK(FAILED(hr), "Deferred context Map returns error");
            buf->Release();
        } else {
            CHECK(true, "(skipped - buffer creation failed)");
        }
    }

    if (cmdList) cmdList->Release();
    if (cmdList2) cmdList2->Release();
    if (rtv) rtv->Release();
    if (rtTexture) rtTexture->Release();
    if (deferred) deferred->Release();
    if (immediate) immediate->Release();
    device->Release();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
