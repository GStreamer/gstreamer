/*
 * GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* This example demonstrate inter D3D11 device synchronization.
 *
 * For the inter D3D11 device synchronization, this example uses DXGI keyed
 * with shared texture. The shared texture is allocated by RenderEngine's D3D11
 * device and opened by GStreamer DecodingEngine module's D3D11 device.
 *
 * RenderEngine: Representing external render module/library such as game engine.
 * This component constis of swapchain and shader with its own D3D11 device.
 * On render event, this component will render input texture to backbuffer
 * then rendered texture will be presented
 *
 * DecodingEngine: Wrapping GStreamer pipeline with own D3D11 device.
 * On render event, GStreamer produced decoded texture will be rendered to
 * the shared texture by GStreamer D3D11 device.
 *
 * Main thread: Executing window message pumping and trigger render event
 * on timeout. Main render event will be executed in this thread.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11.h>
#include <gst/app/app.h>
#include <windows.h>
#include <wrl.h>
#include <string>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <mutex>

using namespace Microsoft::WRL;
#if 0
Texture2D shaderTexture;
SamplerState samplerState;

struct PS_INPUT
{
  float4 Position : SV_POSITION;
  float2 Texture : TEXCOORD;
};

float4 PSMain_sample (PS_INPUT input): SV_TARGET
{
  return shaderTexture.Sample (samplerState, input.Texture);
}
#endif
const BYTE g_PSMain_sample[] =
{
     68,  88,  66,  67,  42, 171,
     68, 189,  81, 136,  62, 236,
    196,  37,  91, 100, 172, 130,
    148, 251,   1,   0,   0,   0,
     80,   2,   0,   0,   5,   0,
      0,   0,  52,   0,   0,   0,
    220,   0,   0,   0,  52,   1,
      0,   0, 104,   1,   0,   0,
    212,   1,   0,   0,  82,  68,
     69,  70, 160,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   2,   0,   0,   0,
     28,   0,   0,   0,   0,   4,
    255, 255,   0,   1,   0,   0,
    119,   0,   0,   0,  92,   0,
      0,   0,   3,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   1,   0,
      0,   0,   0,   0,   0,   0,
    105,   0,   0,   0,   2,   0,
      0,   0,   5,   0,   0,   0,
      4,   0,   0,   0, 255, 255,
    255, 255,   0,   0,   0,   0,
      1,   0,   0,   0,  12,   0,
      0,   0, 115,  97, 109, 112,
    108, 101, 114,  83, 116,  97,
    116, 101,   0, 115, 104,  97,
    100, 101, 114,  84, 101, 120,
    116, 117, 114, 101,   0,  77,
    105,  99, 114, 111, 115, 111,
    102, 116,  32,  40,  82,  41,
     32,  72,  76,  83,  76,  32,
     83, 104,  97, 100, 101, 114,
     32,  67, 111, 109, 112, 105,
    108, 101, 114,  32,  49,  48,
     46,  49,   0, 171,  73,  83,
     71,  78,  80,   0,   0,   0,
      2,   0,   0,   0,   8,   0,
      0,   0,  56,   0,   0,   0,
      0,   0,   0,   0,   1,   0,
      0,   0,   3,   0,   0,   0,
      0,   0,   0,   0,  15,   0,
      0,   0,  68,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   3,   0,   0,   0,
      1,   0,   0,   0,   3,   3,
      0,   0,  83,  86,  95,  80,
     79,  83,  73,  84,  73,  79,
     78,   0,  84,  69,  88,  67,
     79,  79,  82,  68,   0, 171,
    171, 171,  79,  83,  71,  78,
     44,   0,   0,   0,   1,   0,
      0,   0,   8,   0,   0,   0,
     32,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      3,   0,   0,   0,   0,   0,
      0,   0,  15,   0,   0,   0,
     83,  86,  95,  84,  65,  82,
     71,  69,  84,   0, 171, 171,
     83,  72,  68,  82, 100,   0,
      0,   0,  64,   0,   0,   0,
     25,   0,   0,   0,  90,   0,
      0,   3,   0,  96,  16,   0,
      0,   0,   0,   0,  88,  24,
      0,   4,   0, 112,  16,   0,
      0,   0,   0,   0,  85,  85,
      0,   0,  98,  16,   0,   3,
     50,  16,  16,   0,   1,   0,
      0,   0, 101,   0,   0,   3,
    242,  32,  16,   0,   0,   0,
      0,   0,  69,   0,   0,   9,
    242,  32,  16,   0,   0,   0,
      0,   0,  70,  16,  16,   0,
      1,   0,   0,   0,  70, 126,
     16,   0,   0,   0,   0,   0,
      0,  96,  16,   0,   0,   0,
      0,   0,  62,   0,   0,   1,
     83,  84,  65,  84, 116,   0,
      0,   0,   2,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   2,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      1,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   1,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0
};

#if 0
struct VS_INPUT
{
  float4 Position : POSITION;
  float2 Texture : TEXCOORD;
};

struct VS_OUTPUT
{
  float4 Position : SV_POSITION;
  float2 Texture : TEXCOORD;
};

VS_OUTPUT VSMain_coord (VS_INPUT input)
{
  return input;
}
#endif
const BYTE g_VSMain_coord[] =
{
     68,  88,  66,  67, 119,  76,
    129,  53, 139, 143, 201, 108,
     78,  31,  90,  10,  57, 206,
      5,  93,   1,   0,   0,   0,
     24,   2,   0,   0,   5,   0,
      0,   0,  52,   0,   0,   0,
    128,   0,   0,   0, 212,   0,
      0,   0,  44,   1,   0,   0,
    156,   1,   0,   0,  82,  68,
     69,  70,  68,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
     28,   0,   0,   0,   0,   4,
    254, 255,   0,   1,   0,   0,
     28,   0,   0,   0,  77, 105,
     99, 114, 111, 115, 111, 102,
    116,  32,  40,  82,  41,  32,
     72,  76,  83,  76,  32,  83,
    104,  97, 100, 101, 114,  32,
     67, 111, 109, 112, 105, 108,
    101, 114,  32,  49,  48,  46,
     49,   0,  73,  83,  71,  78,
     76,   0,   0,   0,   2,   0,
      0,   0,   8,   0,   0,   0,
     56,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      3,   0,   0,   0,   0,   0,
      0,   0,  15,  15,   0,   0,
     65,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      3,   0,   0,   0,   1,   0,
      0,   0,   3,   3,   0,   0,
     80,  79,  83,  73,  84,  73,
     79,  78,   0,  84,  69,  88,
     67,  79,  79,  82,  68,   0,
    171, 171,  79,  83,  71,  78,
     80,   0,   0,   0,   2,   0,
      0,   0,   8,   0,   0,   0,
     56,   0,   0,   0,   0,   0,
      0,   0,   1,   0,   0,   0,
      3,   0,   0,   0,   0,   0,
      0,   0,  15,   0,   0,   0,
     68,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      3,   0,   0,   0,   1,   0,
      0,   0,   3,  12,   0,   0,
     83,  86,  95,  80,  79,  83,
     73,  84,  73,  79,  78,   0,
     84,  69,  88,  67,  79,  79,
     82,  68,   0, 171, 171, 171,
     83,  72,  68,  82, 104,   0,
      0,   0,  64,   0,   1,   0,
     26,   0,   0,   0,  95,   0,
      0,   3, 242,  16,  16,   0,
      0,   0,   0,   0,  95,   0,
      0,   3,  50,  16,  16,   0,
      1,   0,   0,   0, 103,   0,
      0,   4, 242,  32,  16,   0,
      0,   0,   0,   0,   1,   0,
      0,   0, 101,   0,   0,   3,
     50,  32,  16,   0,   1,   0,
      0,   0,  54,   0,   0,   5,
    242,  32,  16,   0,   0,   0,
      0,   0,  70,  30,  16,   0,
      0,   0,   0,   0,  54,   0,
      0,   5,  50,  32,  16,   0,
      1,   0,   0,   0,  70,  16,
     16,   0,   1,   0,   0,   0,
     62,   0,   0,   1,  83,  84,
     65,  84, 116,   0,   0,   0,
      3,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      4,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   1,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   2,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,
      0,   0
};

struct VertexData
{
  struct
  {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } pos;

  struct
  {
    FLOAT u;
    FLOAT v;
  } uv;
};

class RenderEngine
{
public:
  /* Setup d3d11 resources, shader, and swapchain */
  RenderEngine (HWND hwnd)
  {
    static const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<IDXGIFactory1> factory;
    auto hr = CreateDXGIFactory1 (IID_PPV_ARGS (&factory));
    g_assert (SUCCEEDED (hr));

    /* We will use CreateSwapChainForHwnd which requires IDXGIFactory2 interface */
    hr = factory.As (&factory_);
    g_assert (SUCCEEDED (hr));

    /* Select first (default) device. User can select one among enumerated adapters */
    ComPtr<IDXGIAdapter> adapter;
    hr = factory_->EnumAdapters (0, &adapter);
    g_assert (SUCCEEDED (hr));

    hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
        G_N_ELEMENTS (feature_levels), D3D11_SDK_VERSION, &device_,
        nullptr, &context_);

    /* Old OS may not understand D3D_FEATURE_LEVEL_11_1, try without it if needed */
    if (FAILED (hr)) {
      hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN,
          nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &feature_levels[1],
          G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &device_,
          nullptr, &context_);
    }
    g_assert (SUCCEEDED (hr));

    /* Create shader pipeline */
    D3D11_SAMPLER_DESC sampler_desc = { };
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState (&sampler_desc, &sampler_);
    g_assert (SUCCEEDED (hr));

    D3D11_INPUT_ELEMENT_DESC input_desc[2];
    input_desc[0].SemanticName = "POSITION";
    input_desc[0].SemanticIndex = 0;
    input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    input_desc[0].InputSlot = 0;
    input_desc[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
    input_desc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    input_desc[0].InstanceDataStepRate = 0;

    input_desc[1].SemanticName = "TEXCOORD";
    input_desc[1].SemanticIndex = 0;
    input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    input_desc[1].InputSlot = 0;
    input_desc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
    input_desc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    input_desc[1].InstanceDataStepRate = 0;
    hr = device_->CreateVertexShader (g_VSMain_coord, sizeof (g_VSMain_coord),
        nullptr, &vs_);
    g_assert (SUCCEEDED (hr));

    hr = device_->CreateInputLayout (input_desc, G_N_ELEMENTS (input_desc),
        g_VSMain_coord, sizeof (g_VSMain_coord), &layout_);
    g_assert (SUCCEEDED (hr));

    hr = device_->CreatePixelShader (g_PSMain_sample, sizeof (g_PSMain_sample),
        nullptr, &ps_);
    g_assert (SUCCEEDED (hr));

    VertexData vertex_data[4];
    /* bottom left */
    vertex_data[0].pos.x = -1.0f;
    vertex_data[0].pos.y = -1.0f;
    vertex_data[0].pos.z = 0.0f;
    vertex_data[0].uv.u = 0.0f;
    vertex_data[0].uv.v = 1.0f;

    /* top left */
    vertex_data[1].pos.x = -1.0f;
    vertex_data[1].pos.y = 1.0f;
    vertex_data[1].pos.z = 0.0f;
    vertex_data[1].uv.u = 0.0f;
    vertex_data[1].uv.v = 0.0f;

    /* top right */
    vertex_data[2].pos.x = 1.0f;
    vertex_data[2].pos.y = 1.0f;
    vertex_data[2].pos.z = 0.0f;
    vertex_data[2].uv.u = 1.0f;
    vertex_data[2].uv.v = 0.0f;

    /* bottom right */
    vertex_data[3].pos.x = 1.0f;
    vertex_data[3].pos.y = -1.0f;
    vertex_data[3].pos.z = 0.0f;
    vertex_data[3].uv.u = 1.0f;
    vertex_data[3].uv.v = 1.0f;

    D3D11_BUFFER_DESC buffer_desc = { };
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth = sizeof (VertexData) * 4;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA subresource_data = { };
    subresource_data.pSysMem = vertex_data;
    subresource_data.SysMemPitch = sizeof (VertexData) * 4;
    hr = device_->CreateBuffer (&buffer_desc, &subresource_data, &vertex_buf_);
    g_assert (SUCCEEDED (hr));

    const WORD indices[6] = { 0, 1, 2, 3, 0, 2 };
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth = sizeof (WORD) * 6;
    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    subresource_data.pSysMem = indices;
    subresource_data.SysMemPitch = sizeof (WORD) * 6;
    hr = device_->CreateBuffer (&buffer_desc, &subresource_data, &index_buf_);
    g_assert (SUCCEEDED (hr));

    D3D11_RASTERIZER_DESC rs_desc = { };
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.DepthClipEnable = TRUE;

    hr = device_->CreateRasterizerState (&rs_desc, &rs_);
    g_assert (SUCCEEDED (hr));

    /* Create swapchain */
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = { };
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = 2;
    swapchain_desc.Scaling = DXGI_SCALING_NONE;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    hr = factory_->CreateSwapChainForHwnd (device_.Get (), hwnd,
        &swapchain_desc, nullptr, nullptr, &swapchain_);
    g_assert (SUCCEEDED (hr));
  }

  RenderEngine () = delete;
  RenderEngine& operator=(const RenderEngine &) = delete;

  /* On HWND resize, swapchain and backbuffer need resizing as well */
  void ResizeSwapchain ()
  {
    rtv_ = nullptr;
    backbuf_ = nullptr;
    auto hr = swapchain_->ResizeBuffers (
         /* Use the same backbuffer count */
        0,
        /* Resize to fit window's client area */
        0, 0,
        /* Use configured format */
        DXGI_FORMAT_UNKNOWN, 0);
    g_assert (SUCCEEDED (hr));

    hr = swapchain_->GetBuffer (0, IID_PPV_ARGS (&backbuf_));
    g_assert (SUCCEEDED (hr));

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = { };
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = device_->CreateRenderTargetView (backbuf_.Get (), &rtv_desc, &rtv_);
    g_assert (SUCCEEDED (hr));

    D3D11_TEXTURE2D_DESC desc = { };
    backbuf_->GetDesc (&desc);

    viewport_[0].TopLeftX = 0;
    viewport_[0].TopLeftY = 0;
    viewport_[0].Width = desc.Width / 2;
    viewport_[0].Height = desc.Height;
    viewport_[0].MinDepth = 0;
    viewport_[0].MaxDepth = 1;

    viewport_[1] = viewport_[0];
    viewport_[1].TopLeftX = viewport_[1].Width;
  }

  void Render (ID3D11Texture2D * texture[2])
  {
    ComPtr<ID3D11ShaderResourceView> srv[2];
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    HRESULT hr;
    for (UINT i = 0; i < 2; i++) {
      hr = device_->CreateShaderResourceView (texture[i], &srv_desc,
          &srv[i]);
      g_assert (SUCCEEDED (hr));
    }

    context_->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer *vb[] = { vertex_buf_.Get () };
    UINT offsets = 0;
    UINT vb_stride = sizeof (VertexData);
    context_->IASetVertexBuffers (0, 1, vb, &vb_stride, &offsets);
    context_->IASetIndexBuffer (index_buf_.Get (), DXGI_FORMAT_R16_UINT, 0);
    context_->IASetInputLayout (layout_.Get ());

    context_->VSSetShader (vs_.Get (), nullptr, 0);

    ID3D11SamplerState *sampler[] = { sampler_.Get () };
    context_->PSSetSamplers (0, 1, sampler);
    context_->PSSetShader(ps_.Get (), nullptr, 0);

    ID3D11ShaderResourceView *view[] = { srv[0].Get () };
    context_->PSSetShaderResources (0, 1, view);

    context_->RSSetState (rs_.Get ());
    context_->RSSetViewports (1, viewport_);

    context_->OMSetBlendState (nullptr, nullptr, 0xffffffff);
    ID3D11RenderTargetView *rtv[] = { rtv_.Get () };
    context_->OMSetRenderTargets (1, rtv, nullptr);
    context_->DrawIndexed (6, 0, 0);

    /* Draw right image */
    view[0] = srv[1].Get ();
    context_->PSSetShaderResources (0, 1, view);
    context_->RSSetViewports (1, &viewport_[1]);
    context_->DrawIndexed (6, 0, 0);

    /* Then present */
    swapchain_->Present (0, 0);
  }

  ID3D11Device * GetDevice ()
  {
    return device_.Get ();
  }

private:
  ComPtr<IDXGIFactory2> factory_;
  ComPtr<IDXGISwapChain1> swapchain_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  D3D11_TEXTURE2D_DESC backbuf_desc_ = { };
  ComPtr<ID3D11Texture2D> backbuf_;
  ComPtr<ID3D11RasterizerState> rs_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11InputLayout> layout_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11Buffer> vertex_buf_;
  ComPtr<ID3D11Buffer> index_buf_;
  D3D11_VIEWPORT viewport_[2] = { };
};

static bool
find_decoder (gint64 luid, std::string & feature_name)
{
  GList *features;
  GList *iter;

  /* Load features of d3d11 plugin */
  features = gst_registry_get_feature_list_by_plugin (gst_registry_get (),
      "d3d11");

  if (!features)
    return false;

  for (iter = features; iter; iter = g_list_next (iter)) {
    GstPluginFeature *f = GST_PLUGIN_FEATURE (iter->data);
    GstElementFactory *factory;
    const gchar *name;
    GstElement *element;
    gint64 adapter_luid;

    if (!GST_IS_ELEMENT_FACTORY (f))
      continue;

    factory = GST_ELEMENT_FACTORY (f);
    if (!gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_DECODER))
      continue;

    name = gst_plugin_feature_get_name (f);
    if (!g_strrstr (name, "h264"))
      continue;

    element = gst_element_factory_create (factory, nullptr);
    /* unexpected */
    if (!element)
      continue;

    /* query adapter-luid associated with this decoder */
    g_object_get (element, "adapter-luid", &adapter_luid, nullptr);
    gst_object_unref (element);

    /* element object can be directly used in pipeline, but this example
     * demonstrates a way of plugin enumeration */
    if (adapter_luid == luid) {
      feature_name = name;
      break;
    }
  }

  gst_plugin_feature_list_free (features);

  if (feature_name.empty ())
    return false;

  return true;
}

class DecodingEngine
{
public:
  DecodingEngine (ID3D11Device * render_device, UINT width, UINT height,
      const std::string & file_location, HANDLE shutdown_handle)
  {
    main_context_ = g_main_context_new ();
    main_loop_ = g_main_loop_new (main_context_, FALSE);
    event_handle_ = shutdown_handle;

    gst_video_info_set_format (&render_info_,
        GST_VIDEO_FORMAT_RGBA, width, height);

    /* Find adapter LUID of render device, then create our device with the same
     * adapter */
    ComPtr<IDXGIDevice> dxgi_device;
    auto hr = render_device->QueryInterface (IID_PPV_ARGS (&dxgi_device));
    g_assert (SUCCEEDED (hr));

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter (&adapter);
    g_assert (SUCCEEDED (hr));

    DXGI_ADAPTER_DESC adapter_desc;
    hr = adapter->GetDesc (&adapter_desc);
    g_assert (SUCCEEDED (hr));

    auto luid = gst_d3d11_luid_to_int64 (&adapter_desc.AdapterLuid);
    std::string decoder_factory;
    if (!find_decoder (luid, decoder_factory)) {
      gst_println ("GPU does not support H.264 decoding");
      decoder_factory = "avdec_h264";
    }

    std::string pipeline_str = "filesrc location=" + file_location +
        " ! parsebin ! h264parse ! " + decoder_factory +
        " ! d3d11upload ! video/x-raw(memory:D3D11Memory) ! appsink name=sink";
    pipeline_ = gst_parse_launch (pipeline_str.c_str (), nullptr);
    g_assert (pipeline_);

    auto appsink = (GstAppSink *)
        gst_bin_get_by_name (GST_BIN (pipeline_), "sink");
    g_assert (appsink);

    /* Install appsink callback */
    GstAppSinkCallbacks callbacks = { };
    callbacks.new_sample = DecodingEngine::onNewSample;
    //callbacks.propose_allocation = DecodingEngine::onProposeAllocation;
    gst_app_sink_set_callbacks (appsink, &callbacks, this, nullptr);
    gst_object_unref (appsink);

    /* This device will be used by our pipeline */
    device_ = gst_d3d11_device_new_for_adapter_luid (luid,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    g_assert (device_);

    /* Create texture owned by render engine */
    D3D11_TEXTURE2D_DESC texture_desc = { };
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags =
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    /* Enable keyed mutex + NT handle to make texture sharable */
    texture_desc.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX |
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    hr = render_device->CreateTexture2D (&texture_desc, nullptr,
        &texture_);
    g_assert (SUCCEEDED (hr));

    /* Gets keyed mutex interface and acquire sync at render device side.
     * This keyed mutex will be temporarily released
     * when rendering to shared texture by GStreamer D3D11 device,
     * then re-acquired for render engine device */
    hr = texture_.As (&keyed_mutex_);
    g_assert (SUCCEEDED (hr));

    hr = keyed_mutex_->AcquireSync (0, INFINITE);
    g_assert (SUCCEEDED (hr));

    /* Create shared NT handle so that GStreamer device can access */
    ComPtr<IDXGIResource1> dxgi_resource;
    hr = texture_.As (&dxgi_resource);
    g_assert (SUCCEEDED (hr));

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->CreateSharedHandle (nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
        &shared_handle);
    g_assert (SUCCEEDED (hr));

    auto gst_device = gst_d3d11_device_get_device_handle (device_);
    ComPtr<ID3D11Device1> device1;
    hr = gst_device->QueryInterface (IID_PPV_ARGS (&device1));
    g_assert (SUCCEEDED (hr));

    /* Open shared texture at GStreamer device side */
    ComPtr<ID3D11Texture2D> gst_texture;
    hr = device1->OpenSharedResource1 (shared_handle,
        IID_PPV_ARGS (&gst_texture));
    g_assert (SUCCEEDED (hr));
    /* Can close NT handle now */
    CloseHandle (shared_handle);

    /* Wrap shared texture with GstD3D11Memory in order to convert texture
     * using converter API */
    GstMemory *mem = gst_d3d11_allocator_alloc_wrapped (nullptr, device_,
        gst_texture.Get (),
        /* CPU accessible (staging texture) memory size is unknown.
        * Pass zero here, then GStreamer will calculate it */
        0, nullptr, nullptr);
    g_assert (mem);

    shared_buffer_ = gst_buffer_new ();
    gst_buffer_append_memory (shared_buffer_, mem);
  }

  DecodingEngine () = delete;
  DecodingEngine& operator=(const DecodingEngine &) = delete;

  ~DecodingEngine ()
  {
    g_main_loop_quit (main_loop_);
    g_clear_pointer (&thread_, g_thread_join);
    gst_clear_sample (&last_sample_);
    gst_clear_caps (&last_caps_);
    gst_clear_buffer (&shared_buffer_);
    gst_clear_object (&pipeline_);
    gst_clear_object (&conv_);
    gst_clear_object (&device_);
    g_main_loop_unref (main_loop_);
    g_main_context_unref (main_context_);
  }

  static GstFlowReturn
  onNewSample (GstAppSink * appsink, gpointer user_data)
  {
    GstSample *new_sample;

    new_sample = gst_app_sink_pull_sample (appsink);
    if (!new_sample)
      return GST_FLOW_ERROR;

    auto caps = gst_sample_get_caps (new_sample);
    if (!caps) {
      gst_sample_unref (new_sample);
      gst_printerrln ("Sample without caps");
      return GST_FLOW_ERROR;
    }

    auto self = (DecodingEngine *) user_data;
    std::lock_guard<std::mutex> lk (self->lock_);
    /* Caps updated, recreate converter */
    if (self->last_caps_ && !gst_caps_is_equal (self->last_caps_, caps))
      gst_clear_object (&self->conv_);

    if (!self->conv_) {
      GstVideoInfo in_info;
      gst_video_info_from_caps (&in_info, caps);

      /* In case of shared texture, video processor might not behave as expected.
       * Use only pixel shader */
      auto config = gst_structure_new ("converter-config",
        GST_D3D11_CONVERTER_OPT_BACKEND, GST_TYPE_D3D11_CONVERTER_BACKEND,
        GST_D3D11_CONVERTER_BACKEND_SHADER, nullptr);

      self->conv_ = gst_d3d11_converter_new (self->device_, &in_info,
          &self->render_info_, config);
    }

    gst_caps_replace (&self->last_caps_, caps);
    gst_clear_sample (&self->last_sample_);
    self->last_sample_ = new_sample;

    return GST_FLOW_OK;
  }

  static gboolean
  busHandler (GstBus * bus, GstMessage * msg, gpointer user_data)
  {
    auto self = (DecodingEngine *) user_data;
    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ERROR:
      {
        GError *err;
        gchar *dbg;

        gst_message_parse_error (msg, &err, &dbg);
        gst_printerrln ("ERROR %s", err->message);
        if (dbg != nullptr)
          gst_printerrln ("ERROR debug information: %s", dbg);
        g_clear_error (&err);
        g_free (dbg);
        g_main_loop_quit (self->main_loop_);
        break;
      }
      case GST_MESSAGE_EOS:
        gst_println ("Got EOS");
        g_main_loop_quit (self->main_loop_);
        break;
      default:
        break;
    }

    return G_SOURCE_CONTINUE;
  }

  static GstBusSyncReply
  busSyncHandler (GstBus * bus, GstMessage * msg, gpointer user_data)
  {
    auto self = (DecodingEngine *) user_data;

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_NEED_CONTEXT:
      {
        const gchar *ctx_type;
        if (!gst_message_parse_context_type (msg, &ctx_type))
          break;

        /* non-d3d11 context message is not interested */
        if (g_strcmp0 (ctx_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) != 0)
          break;

        /* Pass our device to the message source element.
         * Otherwise pipeline will create another device */
        auto context = gst_d3d11_context_new (self->device_);
        gst_element_set_context (GST_ELEMENT (msg->src), context);
        gst_context_unref (context);
        break;
      }
      default:
        break;
    }
    return GST_BUS_PASS;
  }

  static gpointer
  loopFunc (gpointer user_data)
  {
    auto self = (DecodingEngine *) user_data;
    GstBus *bus;

    g_main_context_push_thread_default (self->main_context_);
    bus = gst_element_get_bus (self->pipeline_);
    gst_bus_add_watch (bus, DecodingEngine::busHandler, self);
    gst_bus_set_sync_handler (bus, DecodingEngine::busSyncHandler,
        self, nullptr);

    auto ret = gst_element_set_state (self->pipeline_, GST_STATE_PLAYING);
    g_assert (ret != GST_STATE_CHANGE_FAILURE);

    g_main_loop_run (self->main_loop_);

    gst_element_set_state (self->pipeline_, GST_STATE_NULL);
    gst_bus_set_sync_handler (bus, nullptr, nullptr, nullptr);
    gst_bus_remove_watch (bus);
    gst_object_unref (bus);
    g_main_context_pop_thread_default (self->main_context_);

    /* Set event to terminate main rendering loop */
    SetEvent (self->event_handle_);

    return nullptr;
  }

  void Run ()
  {
    thread_ = g_thread_new ("DecodingLoop", DecodingEngine::loopFunc,
        this);
  }

  void UpdateTexture ()
  {
    GstSample *sample = nullptr;

    /* Steal sample pointer */
    std::lock_guard <std::mutex> lk (lock_);
    /* If there's no updated sample, don't need to render again */
    if (!last_sample_)
      return;

    sample = last_sample_;
    last_sample_ = nullptr;

    auto buf = gst_sample_get_buffer (sample);
    if (!buf) {
      gst_printerrln ("Sample without buffer");
      gst_sample_unref (sample);
      return;
    }

    /* Release sync from render engine device,
     * so that GStreamer device can acquire sync */
    keyed_mutex_->ReleaseSync (0);

    /* Converter will take gst_d3d11_device_lock() and acquire sync */
    gst_d3d11_converter_convert_buffer (conv_, buf, shared_buffer_);

    /* After the above function returned, GStreamer will release sync.
     * Acquire sync again for render engine device */
    keyed_mutex_->AcquireSync (0, INFINITE);
    gst_sample_unref (sample);
  }

  ID3D11Texture2D * GetTexture ()
  {
    return texture_.Get ();
  }

private:
  GMainContext *main_context_ = nullptr;
  GMainLoop *main_loop_ = nullptr;
  /* Texture and keyed mutex owned by render-engine device */
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<IDXGIKeyedMutex> keyed_mutex_;
  GstVideoInfo render_info_;
  std::mutex lock_;
  GstCaps *last_caps_ = nullptr;
  GstSample *last_sample_ = nullptr;
  GstBuffer *shared_buffer_ = nullptr;
  GstD3D11Device *device_ = nullptr;
  GstElement *pipeline_ = nullptr;
  GstD3D11Converter *conv_ = nullptr;
  GThread *thread_ = nullptr;
  HANDLE event_handle_ = nullptr;
};

#define APP_DATA_PROP_NAME "AppData"

struct AppData
{
  void Draw ()
  {
    ID3D11Texture2D *textures[2];
    for (UINT i = 0; i < 2; i++) {
      decoding_engine[i]->UpdateTexture ();
      textures[i] = decoding_engine[i]->GetTexture ();
    }

    render_engine->Render (textures);
  }

  void onResize ()
  {
    ID3D11Texture2D *textures[2];
    for (UINT i = 0; i < 2; i++)
      textures[i] = decoding_engine[i]->GetTexture ();

    render_engine->ResizeSwapchain ();
    render_engine->Render (textures);
  }

  HANDLE shutdown_handle;
  RenderEngine *render_engine;
  DecodingEngine *decoding_engine[2];
};

static LRESULT CALLBACK
window_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  switch (msg) {
    case WM_DESTROY:
    {
      auto app_data = (AppData *) GetPropA (hwnd, APP_DATA_PROP_NAME);
      if (!app_data)
        break;

      SetEvent (app_data->shutdown_handle);
      break;
    }
    case WM_SIZE:
    {
      auto app_data = (AppData *) GetPropA (hwnd, APP_DATA_PROP_NAME);
      if (!app_data)
        break;

      app_data->onResize ();
      break;
    }
    default:
      break;
  }

  return DefWindowProc (hwnd, msg, wparam, lparam);
}

gint
main (gint argc, gchar ** argv)
{
  GOptionContext *option_ctx;
  GError *error = nullptr;
  gboolean ret;
  gchar *location = nullptr;
  GOptionEntry options[] = {
    {"location", 0, 0, G_OPTION_ARG_STRING, &location,
        "H.264 encoded test file location", nullptr},
    {nullptr,}
  };

  option_ctx = g_option_context_new ("Direct3D11 decoding example");
  g_option_context_add_main_entries (option_ctx, options, nullptr);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());
  ret = g_option_context_parse (option_ctx, &argc, &argv, &error);
  g_option_context_free (option_ctx);

  if (!ret) {
    gst_printerrln ("option parsing failed: %s", error->message);
    g_clear_error (&error);
    exit (1);
  }

  if (!location) {
    gst_printerrln ("File location is unspecified");
    exit (1);
  }

  /* Create main window */
  RECT wr = { 0, 0, 1280, 480 };
  WNDCLASSEXA wc = { 0, };
  HINSTANCE hinstance = GetModuleHandle (nullptr);

  wc.cbSize = sizeof (WNDCLASSEXA);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = (WNDPROC) window_proc;
  wc.hInstance = hinstance;
  wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
  wc.lpszClassName = "GstD3D11VideoSinkExample";
  RegisterClassExA (&wc);
  AdjustWindowRect (&wr, WS_OVERLAPPEDWINDOW, FALSE);

  auto hwnd = CreateWindowExA (0, wc.lpszClassName, "GstD3D11VideoDecodeExample",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) nullptr, (HMENU) nullptr,
      hinstance, nullptr);
  g_assert (hwnd);


  /* Prepare rendering engine and pipeline */
  AppData app_data = { };
  app_data.shutdown_handle = CreateEventEx (nullptr,
      nullptr, 0, EVENT_ALL_ACCESS);
  app_data.render_engine = new RenderEngine (hwnd);
  for (UINT i = 0; i < 2; i++) {
    app_data.decoding_engine[i] = new DecodingEngine (
      app_data.render_engine->GetDevice (),
      640, 480, location, app_data.shutdown_handle);
  }

  /* Store render engin pointer to handle window resize/destroy event */
  SetPropA (hwnd, APP_DATA_PROP_NAME, &app_data);

  ShowWindow (hwnd, SW_SHOW);

  /* Call resize method, render engine will configure swapchain backbuffer */
  app_data.render_engine->ResizeSwapchain ();

  /* Start rendering */
  for (UINT i = 0; i < 2; i++)
    app_data.decoding_engine[i]->Run ();

  HANDLE waitables[] = { app_data.shutdown_handle };
  while (true) {
    MSG msg;
    while (PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }

    /* Wait for timeout 10ms or shutdown */
    auto wait_ret = MsgWaitForMultipleObjects (G_N_ELEMENTS (waitables),
        waitables, FALSE, 10, QS_ALLINPUT);
    if (wait_ret == WAIT_OBJECT_0) {
      gst_println ("Got shutdown event");
      break;
    } else if (wait_ret == WAIT_OBJECT_0 + 1) {
      /* New window message, nothing to do */
    } else if (wait_ret == WAIT_TIMEOUT) {
      /* Redraw on timeout */
      app_data.Draw ();
    } else {
      gst_printerrln ("Unexpected wait return %u", (guint) wait_ret);
      break;
    }
  }

  RemovePropA (hwnd, APP_DATA_PROP_NAME);
  DestroyWindow (hwnd);

  for (UINT i = 0; i < 2; i++)
    delete app_data.decoding_engine[i];
  delete app_data.render_engine;
  CloseHandle (app_data.shutdown_handle);
  g_free (location);

  gst_deinit ();

  return 0;
}
