// tests/run.ps1 -Addon <binary> -ReShadeDll <matching full ReShade runtime> runs this in a scratch directory.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "crc32_hash.hpp"

using Microsoft::WRL::ComPtr;

static void check(HRESULT hr) {
    if (FAILED(hr)) {
        std::cerr << "Graphics operation failed, HRESULT 0x" << std::hex << hr << '\n';
        throw std::runtime_error("D3D11 smoke test failed");
    }
}

static ComPtr<ID3DBlob> compile(const char* source, const char* target) {
    ComPtr<ID3DBlob> output;
    check(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, &output, nullptr));
    return output;
}

int main() {
    const auto marked = compile("float4 main():SV_Target {return float4(1,0,0,1);}", "ps_4_0");
    const auto other = compile("float4 main():SV_Target {return float4(0,1,0,1);}", "ps_4_0");
    const auto hud = compile("float4 main(float4 p:SV_Position):SV_Target {clip(64-p.x); return float4(1,0,0,1);}", "ps_4_0");
    const auto vertex = compile("float4 main(uint i:SV_VertexID):SV_Position {"
                                "return float4(i==1?3:-1,i==2?-3:1,0,1);}", "vs_4_0");
    std::ofstream("ReShade.ini") << "[GENERAL]\nEffectSearchPaths=.\\\nPresetPath=.\\SmokePreset.ini\n"
                                    "[OVERLAY]\nTutorialProgress=4\nShowClock=0\n";
    std::ofstream("SmokePreset.ini") << "Techniques=PaintBlue@Smoke.fx\n";
    std::ofstream("Smoke.fx") <<
        "float4 VS(uint i:SV_VertexID):SV_Position {return float4(i==1?3:-1,i==2?-3:1,0,1);}\n"
        "float4 PS():SV_Target {return float4(0,0,1,1);}\n"
        "technique PaintBlue { pass { VertexShader=VS; PixelShader=PS; } }\n";
    std::ofstream("ReshadeEffectShaderToggler.ini") << "[General]\nAmountGroups=2\n[Group0]\nName=Smoke\n"
        "Active=True\nSuppressDrawCall=True\nMatchVertexCount=True\nVertexCountMin=3\nVertexCountMax=3\n"
        "[Group0_PixelShaders]\nAmountHashes=1\nShaderHash0="
        << compute_crc32(static_cast<const uint8_t*>(marked->GetBufferPointer()), marked->GetBufferSize())
        << "\n[Group1]\nName=HUD\nActive=True\nAllowAllTechniques=True\nInvocationLocation=0\n"
           "[Group1_PixelShaders]\nAmountHashes=1\nShaderHash0="
        << compute_crc32(static_cast<const uint8_t*>(hud->GetBufferPointer()), hud->GetBufferSize()) << '\n';

    HWND window = CreateWindowW(L"STATIC", L"REST smoke test", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr, nullptr, nullptr, nullptr);
    if (!window) return 1;
    HMODULE module = LoadLibraryW(L".\\d3d11.dll");
    if (!module || !GetProcAddress(module, "ReShadeRegisterAddon")) return 2;
    auto create = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(module, "D3D11CreateDeviceAndSwapChain"));
    if (!create) return 3;
    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferDesc.Width = swap.BufferDesc.Height = 128;
    swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.BufferCount = 1;
    swap.OutputWindow = window;
    swap.Windowed = TRUE;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    check(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                 &swap, &chain, &device, nullptr, &ctx));
    for (int frame = 0; frame < 60; ++frame) {
        check(chain->Present(0, 0));
        Sleep(10);
    }

    ComPtr<ID3D11Texture2D> target, readback;
    check(chain->GetBuffer(0, IID_PPV_ARGS(&target)));
    ComPtr<ID3D11RenderTargetView> rtv;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv));
    D3D11_TEXTURE2D_DESC desc;
    target->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = desc.MiscFlags = 0;
    check(device->CreateTexture2D(&desc, nullptr, &readback));
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> red, green, foreground;
    check(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &vs));
    check(device->CreatePixelShader(marked->GetBufferPointer(), marked->GetBufferSize(), nullptr, &red));
    check(device->CreatePixelShader(other->GetBufferPointer(), other->GetBufferSize(), nullptr, &green));
    check(device->CreatePixelShader(hud->GetBufferPointer(), hud->GetBufferSize(), nullptr, &foreground));
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_VIEWPORT viewport{0, 0, 128, 128, 0, 1};
    ctx->RSSetViewports(1, &viewport);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    auto expect = [&](ID3D11PixelShader* shader, UINT count, uint32_t left, uint32_t right) {
        const float black[4]{0, 0, 0, 1};
        ctx->ClearRenderTargetView(rtv.Get(), black);
        ctx->PSSetShader(shader, nullptr, 0);
        ctx->Draw(count, 0);
        ctx->CopyResource(readback.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(ctx->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData) + 64 * mapped.RowPitch);
        const uint32_t actualLeft = row[32] & 0x00ffffff;
        const uint32_t actualRight = row[96] & 0x00ffffff;
        ctx->Unmap(readback.Get(), 0);
        if (actualLeft != left || actualRight != right) {
            std::cerr << "Expected pixels " << std::hex << left << ", " << right << " got " << actualLeft << ", " << actualRight << '\n';
            throw std::runtime_error("Pixel mismatch");
        }
    };
    expect(green.Get(), 3, 0x00ff00, 0x00ff00);
    expect(red.Get(), 3, 0, 0);
    expect(red.Get(), 4, 0x0000ff, 0x0000ff);
    expect(green.Get(), 3, 0x00ff00, 0x00ff00);
    expect(foreground.Get(), 3, 0x0000ff, 0xff0000);
    std::cout << "D3D11 readback verified shader suppression, geometry filtering and an effect behind foreground geometry.\n";
}
