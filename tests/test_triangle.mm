#include <metalsharp/D3D11Device.h>
#include <metalsharp/D3D11DeviceContext.h>
#include <d3d/D3D11.h>
#include <AppKit/AppKit.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

static const char* kTriangleMSL = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertexShader(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 fragmentShader(VertexOut in [[stage_in]]) {
    return in.color;
}
)msl";

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

int main() {
    @autoreleasepool {
        printf("=== MetalSharp Triangle Test ===\n\n");

        metalsharp::D3D11Device* device = nullptr;
        HRESULT hr = metalsharp::D3D11Device::create(&device);
        if (FAILED(hr) || !device) {
            printf("FAIL: D3D11Device::create returned 0x%08x\n", hr);
            return 1;
        }
        printf("[OK] D3D11Device created\n");

        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (!ctx) {
            printf("FAIL: GetImmediateContext returned null\n");
            return 1;
        }
        printf("[OK] Immediate context acquired\n");

        ID3D11VertexShader* vs = nullptr;
        hr = device->CreateVertexShader(kTriangleMSL, strlen(kTriangleMSL), nullptr, &vs);
        if (FAILED(hr) || !vs) {
            printf("FAIL: CreateVertexShader returned 0x%08x\n", hr);
            return 1;
        }
        printf("[OK] Vertex shader compiled (MSL)\n");

        ID3D11PixelShader* ps = nullptr;
        hr = device->CreatePixelShader(kTriangleMSL, strlen(kTriangleMSL), nullptr, &ps);
        if (FAILED(hr) || !ps) {
            printf("FAIL: CreatePixelShader returned 0x%08x\n", hr);
            return 1;
        }
        printf("[OK] Pixel shader compiled (MSL)\n");

        Vertex vertices[] = {
            {  0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f, 1.0f },
            { -0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f, 1.0f },
            {  0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f, 1.0f },
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(vertices);
        vbDesc.BindFlags = 0x01;
        vbDesc.Usage = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices;

        ID3D11Buffer* vertexBuffer = nullptr;
        hr = device->CreateBuffer(&vbDesc, &initData, &vertexBuffer);
        if (FAILED(hr) || !vertexBuffer) {
            printf("FAIL: CreateBuffer returned 0x%08x\n", hr);
            return 1;
        }
        printf("[OK] Vertex buffer created (%zu bytes)\n", sizeof(vertices));

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        ctx->IASetPrimitiveTopology(4);

        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);

        D3D11_VIEWPORT vp = { 800, 600 };
        ctx->RSSetViewports(1, &vp);

        hr = ctx->Draw(3, 0);
        if (FAILED(hr)) {
            printf("FAIL: Draw returned 0x%08x\n", hr);
            return 1;
        }
        printf("[OK] Draw submitted — triangle rendered via Metal\n");

        printf("\n=== Triangle Test PASSED ===\n");

        vertexBuffer->Release();
        ps->Release();
        vs->Release();
        ctx->Release();
        device->Release();

        return 0;
    }
}
