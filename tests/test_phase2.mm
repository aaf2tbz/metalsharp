#include <metalsharp/D3D11Device.h>
#include <metalsharp/D3D11DeviceContext.h>
#include <d3d/D3D11.h>
#include <cstdio>
#include <cstdlib>
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
    printf("=== MetalSharp Phase 2 Tests ===\n\n");

    metalsharp::D3D11Device* device = nullptr;
    HRESULT hr = metalsharp::D3D11Device::create(&device);
    CHECK(SUCCEEDED(hr) && device, "D3D11Device creation");

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);
    CHECK(ctx != nullptr, "GetImmediateContext");

    printf("\n--- State Objects ---\n");

    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        for (int i = 0; i < 8; i++) {
            blendDesc.RenderTarget[i].BlendEnable = FALSE;
            blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
            blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        ID3D11BlendState* blendState = nullptr;
        hr = device->CreateBlendState(&blendDesc, &blendState);
        CHECK(SUCCEEDED(hr) && blendState, "CreateBlendState");
        if (blendState) {
            CHECK(blendState->__getBlendEnable(0) == FALSE, "BlendState: blend disabled");
            CHECK(blendState->__getSrcBlend(0) == D3D11_BLEND_SRC_ALPHA, "BlendState: src blend");
            blendState->Release();
        }
    }

    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ID3D11BlendState* blendState = nullptr;
        hr = device->CreateBlendState(&blendDesc, &blendState);
        CHECK(SUCCEEDED(hr) && blendState, "CreateBlendState (alpha blend)");
        if (blendState) {
            CHECK(blendState->__getBlendEnable(0) == TRUE, "BlendState: blend enabled on RT0");
            blendState->Release();
        }
    }

    {
        D3D11_RASTERIZER_DESC rastDesc = {};
        rastDesc.FillMode = D3D11_FILL_SOLID;
        rastDesc.CullMode = D3D11_CULL_BACK;
        rastDesc.FrontCounterClockwise = FALSE;
        rastDesc.DepthClipEnable = TRUE;
        ID3D11RasterizerState* rastState = nullptr;
        hr = device->CreateRasterizerState(&rastDesc, &rastState);
        CHECK(SUCCEEDED(hr) && rastState, "CreateRasterizerState");
        if (rastState) {
            CHECK(rastState->__getCullMode() == D3D11_CULL_BACK, "RasterizerState: cull mode");
            CHECK(rastState->__getFillMode() == D3D11_FILL_SOLID, "RasterizerState: fill mode");
            rastState->Release();
        }
    }

    {
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
        dsDesc.StencilEnable = FALSE;
        ID3D11DepthStencilState* dsState = nullptr;
        hr = device->CreateDepthStencilState(&dsDesc, &dsState);
        CHECK(SUCCEEDED(hr) && dsState, "CreateDepthStencilState");
        if (dsState) {
            CHECK(dsState->__getDepthEnable() == TRUE, "DepthStencilState: depth enabled");
            CHECK(dsState->__getDepthWriteMask() == D3D11_DEPTH_WRITE_MASK_ALL, "DepthStencilState: write mask");
            CHECK(dsState->__getDepthFunc() == D3D11_COMPARISON_LESS, "DepthStencilState: depth func");
            dsState->Release();
        }
    }

    {
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.MaxAnisotropy = 1;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = 1000;
        ID3D11SamplerState* sampState = nullptr;
        hr = device->CreateSamplerState(&sampDesc, &sampState);
        CHECK(SUCCEEDED(hr) && sampState, "CreateSamplerState");
        if (sampState) {
            CHECK(sampState->__metalSamplerState() != nullptr, "SamplerState: Metal sampler created");
            sampState->Release();
        }
    }

    printf("\n--- Views ---\n");

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* rtTexture = nullptr;
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = 256;
        texDesc.Height = 256;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        hr = device->CreateTexture2D(&texDesc, nullptr, &rtTexture);
        CHECK(SUCCEEDED(hr) && rtTexture, "CreateTexture2D (render target)");

        if (rtTexture) {
            hr = device->CreateRenderTargetView(rtTexture, nullptr, &rtv);
            CHECK(SUCCEEDED(hr) && rtv, "CreateRenderTargetView");
            CHECK(rtv->__metalTexturePtr() != nullptr, "RTV: Metal texture accessible");
        }
    }

    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Texture2D* srTexture = nullptr;
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = 64;
        texDesc.Height = 64;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        hr = device->CreateTexture2D(&texDesc, nullptr, &srTexture);
        if (SUCCEEDED(hr) && srTexture) {
            hr = device->CreateShaderResourceView(srTexture, nullptr, &srv);
            CHECK(SUCCEEDED(hr) && srv, "CreateShaderResourceView");
            if (srv) {
                CHECK(srv->__metalTexturePtr() != nullptr, "SRV: Metal texture accessible");
            }
        }
    }

    printf("\n--- Input Layout ---\n");

    {
        D3D11_INPUT_ELEMENT_DESC elements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ID3D11InputLayout* layout = nullptr;
        hr = device->CreateInputLayout(elements, 2, kMSL, strlen(kMSL), &layout);
        CHECK(SUCCEEDED(hr) && layout, "CreateInputLayout");
        if (layout) {
            CHECK(layout->__getNumElements() == 2, "InputLayout: element count");
            const auto* stored = static_cast<const D3D11_INPUT_ELEMENT_DESC*>(layout->__getElements());
            CHECK(stored != nullptr, "InputLayout: elements stored");
            layout->Release();
        }
    }

    printf("\n--- Map/Unmap ---\n");

    {
        D3D11_BUFFER_DESC bufDesc = {};
        bufDesc.ByteWidth = 256;
        bufDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ID3D11Buffer* cb = nullptr;
        hr = device->CreateBuffer(&bufDesc, nullptr, &cb);
        CHECK(SUCCEEDED(hr) && cb, "CreateBuffer (dynamic constant buffer)");

        if (cb) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            hr = ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            CHECK(SUCCEEDED(hr) && mapped.pData != nullptr, "Map (write discard)");

            if (mapped.pData) {
                float data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
                memcpy(mapped.pData, data, sizeof(data));
            }

            hr = ctx->Unmap(cb, 0);
            CHECK(SUCCEEDED(hr), "Unmap");

            cb->Release();
        }
    }

    printf("\n--- Draw with vertex count ---\n");

    {
        if (rtv) {
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
        }

        FLOAT clear_color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        if (rtv) ctx->ClearRenderTargetView(rtv, clear_color);

        struct Vertex { float x,y,z; float r,g,b,a; };
        Vertex verts[6] = {
            {-0.5f, 0.5f, 0.0f, 1,0,0,1},
            { 0.5f, 0.5f, 0.0f, 0,1,0,1},
            {-0.5f,-0.5f, 0.0f, 0,0,1,1},
            { 0.5f,-0.5f, 0.0f, 1,1,0,1},
            {-0.5f,-0.5f, 0.0f, 0,1,1,1},
            { 0.5f,-0.5f, 0.0f, 1,0,1,1},
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(verts);
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.Usage = D3D11_USAGE_DEFAULT;

        D3D11_SUBRESOURCE_DATA initData = { verts };
        ID3D11Buffer* vb = nullptr;
        hr = device->CreateBuffer(&vbDesc, &initData, &vb);
        CHECK(SUCCEEDED(hr) && vb, "CreateBuffer (vertex buffer for draw test)");

        if (vb) {
            ID3D11VertexShader* vs = nullptr;
            ID3D11PixelShader* ps = nullptr;
            device->CreateVertexShader(kMSL, strlen(kMSL), nullptr, &vs);
            device->CreatePixelShader(kMSL, strlen(kMSL), nullptr, &ps);

            UINT stride = sizeof(Vertex), offset = 0;
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(vs, nullptr, 0);
            ctx->PSSetShader(ps, nullptr, 0);

            D3D11_VIEWPORT vp = {0, 0, 256, 256, 0.0f, 1.0f};
            ctx->RSSetViewports(1, &vp);

            hr = ctx->Draw(6, 0);
            CHECK(SUCCEEDED(hr), "Draw(6 vertices) — two triangles");

            if (vs) vs->Release();
            if (ps) ps->Release();
            vb->Release();
        }
    }

    if (rtv) rtv->Release();
    if (rtTexture) rtTexture->Release();
    if (srv) srv->Release();
    if (srTexture) srTexture->Release();
    ctx->Release();
    device->Release();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
