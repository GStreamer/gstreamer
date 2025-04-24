/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>
#include <wrl.h>
#include <directx/d3dx12.h>
#include <memory>

#include <windows.h>
#include <string.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "../key-handler.h"

/* *INDENT-OFF* */
using namespace DirectX;
using namespace Microsoft::WRL;

static const gchar *shader_str = R"(
RWTexture2D<float4> uvLUT : register(u0);

cbuffer Parameters : register(b0)
{
  float4x4 RotationMatrix;
  float2 lutResolution;
  float perspectiveFOV;
  float fisheyeFOV;
  float2 fisheyeCircleCenter;
  float2 fisheyeCircleRadius;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
  if (DTid.x >= (uint)lutResolution.x || DTid.y >= (uint)lutResolution.y)
      return;

  float2 pixelPos = float2(DTid.x, DTid.y);
  float2 uv_ndc = (pixelPos / lutResolution) * 2.0 - 1.0;

  float hFOV_rad = radians(perspectiveFOV);
  float halfWidth = tan(hFOV_rad * 0.5);
  float aspect = lutResolution.y / lutResolution.x;
  float x = uv_ndc.x * halfWidth;
  float y = uv_ndc.y * halfWidth * aspect;

  float3 rayDir = normalize(float3(x, y, 1.0));
  float3x3 rotation3x3 = float3x3(
      RotationMatrix._11, RotationMatrix._12, RotationMatrix._13,
      RotationMatrix._21, RotationMatrix._22, RotationMatrix._23,
      RotationMatrix._31, RotationMatrix._32, RotationMatrix._33
  );
  rayDir = mul(rotation3x3, rayDir);

  float theta = acos(rayDir.z);
  float maxAngle = radians(fisheyeFOV * 0.5);

  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);
  if (theta <= maxAngle) {
    float r_fishX = (fisheyeCircleRadius.x / maxAngle) * theta;
    float r_fishY = (fisheyeCircleRadius.y / maxAngle) * theta;

    float phi = atan2(rayDir.y, rayDir.x);
    fishUV.xy = fisheyeCircleCenter +
        float2(r_fishX * cos(phi), r_fishY * sin(phi));
  } else {
    fishUV.w = 0.0;
  }

  uvLUT[int2(DTid.xy)] = fishUV;
}
)";
/* *INDENT-ON* */

static GMainLoop *loop = nullptr;

#define REMAP_SIZE 1024

struct ConstBuf
{
  XMFLOAT4X4 RotationMatrix;
  FLOAT lutResolution[2];
  FLOAT perspectiveFOV;
  FLOAT fisheyeFOV;
  FLOAT fisheyeCircleCenter[2];
  FLOAT fisheyeCircleRadius[2];
};

struct RemapResource
{
  ~RemapResource()
  {
    if (fence_val > 0 && device) {
      /* Make sure there's no pending GPU task */
      gst_d3d12_device_fence_wait (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            fence_val);
    }

    cl = nullptr;
    uv_remap = nullptr;
    gst_clear_object (&ca_pool);
    gst_clear_object (&fence_data_pool);
    gst_clear_object (&device);
  }

  void UpdateAngle (FLOAT tilt_angle, FLOAT pan_angle, FLOAT roll_angle)
  {
    float tilt_rad = XMConvertToRadians(tilt_angle);
    float pan_rad  = XMConvertToRadians(pan_angle);
    float roll_rad = XMConvertToRadians(roll_angle);

    XMMATRIX rot_x = XMMatrixRotationX(tilt_rad);
    XMMATRIX rot_y = XMMatrixRotationY(pan_rad);
    XMMATRIX rot_z = XMMatrixRotationZ(roll_rad);

    XMMATRIX m = XMMatrixMultiply(rot_z, XMMatrixMultiply(rot_y, rot_x));
    XMStoreFloat4x4 (&cbuf.RotationMatrix, m);
  }

  bool UpdateRemapResource ()
  {
    GstD3D12FenceData *fence_data;
    gst_d3d12_fence_data_pool_acquire (fence_data_pool, &fence_data);

    GstD3D12CmdAlloc *gst_ca;
    if (!gst_d3d12_cmd_alloc_pool_acquire (ca_pool, &gst_ca)) {
      gst_println ("Couldn't acquire cmd allocator");
      gst_d3d12_fence_data_unref (fence_data);
      return false;
    }

    gst_d3d12_fence_data_push (fence_data, gst_ca, (GDestroyNotify)
        gst_mini_object_unref);

    auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
    auto hr = ca->Reset ();
    if (!gst_d3d12_result (hr, device)) {
      gst_print ("Couldn't reset cmd allocator");
      gst_d3d12_fence_data_unref (fence_data);
      return false;
    }

    if (!cl) {
      auto device_handle = gst_d3d12_device_get_device_handle (device);
      hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
          ca, nullptr, IID_PPV_ARGS (&cl));
    } else {
      hr = cl->Reset (ca, nullptr);
    }

    if (!gst_d3d12_result (hr, device)) {
      gst_print ("Couldn't setup cmd list");
      gst_d3d12_fence_data_unref (fence_data);
      return false;
    }

    ID3D12DescriptorHeap *heaps[] = { desc_heap.Get () };

    cl->SetComputeRootSignature (rs.Get ());
    cl->SetPipelineState (pso.Get ());
    cl->SetDescriptorHeaps (1, heaps);
    cl->SetComputeRoot32BitConstants (0, sizeof (cbuf) / 4, &cbuf, 0);
    cl->SetComputeRootDescriptorTable (1,
        desc_heap->GetGPUDescriptorHandleForHeapStart ());
    cl->Dispatch ((REMAP_SIZE + 7) / 8, (REMAP_SIZE + 7) / 8, 1);
    hr = cl->Close ();

    if (!gst_d3d12_result (hr, device)) {
      gst_print ("Couldn't close cmd list");
      gst_d3d12_fence_data_unref (fence_data);
      return false;
    }

    ID3D12CommandList *cmd_list[] = { cl.Get () };
    hr = gst_d3d12_device_execute_command_lists (device,
        D3D12_COMMAND_LIST_TYPE_DIRECT, 1, cmd_list, &fence_val);
    if (!gst_d3d12_result (hr, device)) {
      gst_println ("Couldn't execute command list");
      gst_d3d12_fence_data_unref (fence_data);
      return false;
    }

    gst_d3d12_device_set_fence_notify (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        fence_val, fence_data, (GDestroyNotify) gst_mini_object_unref);

    return true;
  }

  GstD3D12Device *device = nullptr;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12FenceDataPool *fence_data_pool = nullptr;
  ComPtr<ID3D12RootSignature> rs;
  ComPtr<ID3D12PipelineState> pso;
  ComPtr<ID3D12GraphicsCommandList> cl;
  ComPtr<ID3D12Resource> uv_remap;
  ComPtr<ID3D12DescriptorHeap> desc_heap;
  ConstBuf cbuf;
  UINT64 fence_val = 0;
};

static HRESULT
creat_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
  CD3DX12_ROOT_PARAMETER root_params[2];
  CD3DX12_DESCRIPTOR_RANGE range_uav;

  root_params[0].InitAsConstants (sizeof (ConstBuf) / 4, 0);

  range_uav.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
  root_params[1].InitAsDescriptorTable (1, &range_uav);
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 2, root_params,
    0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

  ComPtr < ID3DBlob > error_blob;
  auto hr = D3DX12SerializeVersionedRootSignature (&desc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, blob, &error_blob);
  if (!gst_d3d12_result (hr, device)) {
    const gchar *error_msg = nullptr;
    if (error_blob)
      error_msg = (const gchar *) error_blob->GetBufferPointer ();

    gst_println ("Couldn't serialize rs, hr: 0x%x, error detail: %s",
        (guint) hr, GST_STR_NULL (error_msg));
  }

  return hr;
}

static HRESULT
compile_shader (GstD3D12Device * device, ID3DBlob ** blob)
{
  ComPtr < ID3DBlob > error_blob;
  auto hr = D3DCompile (shader_str, strlen (shader_str), nullptr, nullptr, nullptr,
      "CSMain", "cs_5_0", 0, 0, blob, &error_blob);

  if (!gst_d3d12_result (hr, device)) {
    const gchar *error_msg = nullptr;
    if (error_blob)
      error_msg = (const gchar *) error_blob->GetBufferPointer ();

    gst_println ("Couldn't compile shader, hr: 0x%x, error detail: %s",
        (guint) hr, GST_STR_NULL (error_msg));
  }

  return hr;
}

static std::shared_ptr<RemapResource>
create_remap_resource (void)
{
  auto ret = std::make_shared<RemapResource> ();

  ret->device = gst_d3d12_device_new (0);
  if (!ret->device) {
    gst_println ("Couldn't create d3d12 device");
    return nullptr;
  }

  ret->fence_data_pool = gst_d3d12_fence_data_pool_new ();
  auto device = gst_d3d12_device_get_device_handle (ret->device);
  ret->ca_pool = gst_d3d12_cmd_alloc_pool_new (device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  /* Prepare compute shader and resource.
   * Compute shader will write UV remap data to RGBA texture
   * (R -> U, G -> V, B -> unused, A -> mask where A < 0.5 will fill background
   * color)
   */
  ComPtr<ID3DBlob> shader_blob;
  auto hr = compile_shader (ret->device, &shader_blob);
  if (FAILED (hr))
    return nullptr;

  ComPtr<ID3DBlob> rs_blob;
  hr = creat_rs_blob (ret->device, &rs_blob);
  if (FAILED (hr))
    return nullptr;

  auto device_handle = gst_d3d12_device_get_device_handle (ret->device);
  hr = device_handle->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&ret->rs));
  if (!gst_d3d12_result (hr, ret->device)) {
    gst_println ("Couldn't create root signature");
    return nullptr;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
  pso_desc.pRootSignature = ret->rs.Get ();
  pso_desc.CS.pShaderBytecode = shader_blob->GetBufferPointer ();
  pso_desc.CS.BytecodeLength = shader_blob->GetBufferSize ();
  hr = device_handle->CreateComputePipelineState (&pso_desc,
        IID_PPV_ARGS (&ret->pso));
  if (!gst_d3d12_result (hr, ret->device)) {
    gst_println ("Couldn't create pso");
    return nullptr;
  }

  D3D12_HEAP_PROPERTIES heap_prop = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
  D3D12_RESOURCE_DESC resource_desc =
    CD3DX12_RESOURCE_DESC::Tex2D (DXGI_FORMAT_R16G16B16A16_UNORM,
    REMAP_SIZE, REMAP_SIZE, 1, 1, 1, 0,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
    D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
  hr = device_handle->CreateCommittedResource (&heap_prop, D3D12_HEAP_FLAG_NONE,
      &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS (&ret->uv_remap));
  if (!gst_d3d12_result (hr, ret->device)) {
    gst_println ("Couldn't create texture");
    return nullptr;
  }

  D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = { };
  desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc_heap_desc.NumDescriptors = 1;
  desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  hr = device_handle->CreateDescriptorHeap (&desc_heap_desc,
      IID_PPV_ARGS (&ret->desc_heap));
  if (!gst_d3d12_result (hr, ret->device)) {
    gst_println ("Couldn't create descriptor heap");
    return nullptr;
  }

  auto cpu_handle = ret->desc_heap->GetCPUDescriptorHandleForHeapStart ();
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
  uav_desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device_handle->CreateUnorderedAccessView (ret->uv_remap.Get (),
      nullptr, &uav_desc, cpu_handle);

  ret->cbuf.lutResolution[0] = REMAP_SIZE;
  ret->cbuf.lutResolution[1] = REMAP_SIZE;
  ret->cbuf.perspectiveFOV = 120;
  ret->cbuf.fisheyeFOV = 180;
  ret->cbuf.fisheyeCircleCenter[0] = 0.5;
  ret->cbuf.fisheyeCircleCenter[1] = 0.5;
  ret->cbuf.fisheyeCircleRadius[0] = 0.5;
  ret->cbuf.fisheyeCircleRadius[1] = 0.5;

  ret->UpdateAngle (0, 0, 0);

  if (!ret->UpdateRemapResource ())
    return nullptr;

  return ret;
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
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

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
    {
      gst_println ("Got EOS");
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
print_keyboard_help (void)
{
  static struct
  {
    const gchar *key_desc;
    const gchar *key_help;
  } key_controls[] = {
    {"left arrow", "Decrease pan angle"},
    {"right arrow", "Increase pan angle"},
    {"down arrow", "Decrease tilt angle"},
    {"up arrow", "Increase tilt angle"},
    {"-", "Decrease roll angle"},
    {"+", "Increase roll angle"},
    {"1", "Decrease perspective FOV"},
    {"2", "Increase perspective FOV"},
    {"space", "Reset angle"},
    {"q", "Quit"},
  };

  guint i, chars_to_pad, desc_len, max_desc_len = 0;

  gst_print ("\n%s\n", "Keyboard controls:");

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    desc_len = g_utf8_strlen (key_controls[i].key_desc, -1);
    max_desc_len = MAX (max_desc_len, desc_len);
  }
  ++max_desc_len;

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    chars_to_pad = max_desc_len - g_utf8_strlen (key_controls[i].key_desc, -1);
    gst_print ("\t%s", key_controls[i].key_desc);
    gst_print ("%-*s: ", chars_to_pad, "");
    gst_print ("%s\n", key_controls[i].key_help);
  }
  gst_print ("\n");
}

struct AppData
{
  RemapResource *resource;
  GstElement *remap;
};

static void
keyboard_cb (gchar input, gboolean is_ascii, AppData * app_data)
{
  static FLOAT tilt = 0;
  static FLOAT pan = 0;
  static FLOAT roll = 0;
  static FLOAT fov = 120;
  bool update_angle = false;
  bool update_fov = false;

  if (!is_ascii) {
    switch (input) {
      case KB_ARROW_UP:
        tilt += 1.0;
        if (tilt > 45.0)
          tilt = 45.0;
        gst_println ("Increase tilt angle to %lf", tilt);
        update_angle = true;
        break;
      case KB_ARROW_DOWN:
        tilt -= 1.0;
        if (tilt < -45.0)
          tilt = -45.0;
        gst_println ("Decrease tilt angle to %lf", tilt);
        update_angle = true;
        break;
      case KB_ARROW_LEFT:
        pan -= 1.0;
        if (pan < -45.0)
          pan = -45.0;
        gst_println ("Decrease pan angle to %lf", pan);
        update_angle = true;
        break;
      case KB_ARROW_RIGHT:
        pan += 1.0;
        if (pan > 45.0)
          pan = 45.0;
        gst_println ("Increase pan angle to %lf", pan);
        update_angle = true;
        break;
      default:
        break;
    }
  } else {
    switch (input) {
      case '-':
        roll -= 1.0;
        if (roll < -45.0)
          roll = -45.0;
        gst_println ("Decrease roll angle to %lf", roll);
        update_angle = true;
        break;
      case '+':
        roll += 1.0;
        if (roll > 45.0)
          roll = 45.0;
        gst_println ("Increase roll angle to %lf", roll);
        update_angle = true;
        break;
      case '1':
        fov -= 1.0;
        if (fov < 10)
          fov = 10;
        gst_println ("Decrease fov to %lf", fov);
        update_fov = true;
        break;
      case '2':
        fov += 1.0;
        if (fov > 120.0)
          fov = 120.0;
        gst_println ("Increase fov to %lf", fov);
        update_fov = true;
        break;
      case ' ':
        pan = 0;
        tilt = 0;
        roll = 0;
        fov = 120;
        gst_println ("Reset angle");
        update_angle = true;
        update_fov = true;
        break;
      case 'q':
        g_main_loop_quit (loop);
        break;
      default:
        break;
    }
  }

  if (!update_angle && !update_fov)
    return;

  if (update_angle)
    app_data->resource->UpdateAngle (tilt, pan, roll);

  if (update_fov)
    app_data->resource->cbuf.perspectiveFOV = fov;

  app_data->resource->UpdateRemapResource ();
}

gint
main (gint argc, gchar ** argv)
{
  AppData data;
  gchar *location = nullptr;
  GOptionEntry options[] = {
    {"location", 0, 0, G_OPTION_ARG_STRING, &location,
        "Fisheye image file location"},
    {nullptr}
  };

  auto option_ctx =
      g_option_context_new ("Fisheye to perspective projection using d3d12remap");
  g_option_context_add_main_entries (option_ctx, options, nullptr);
  g_option_context_set_help_enabled (option_ctx, TRUE);
  GError *err = nullptr;
  if (!g_option_context_parse (option_ctx, &argc, &argv, &err)) {
    gst_printerrln ("option parsing failed: %s\n", err->message);
    g_clear_error (&err);
    return 0;
  }
  g_option_context_free (option_ctx);

  if (!location) {
    gst_println ("Location must be specified");
    return 0;
  }

  gst_init (nullptr, nullptr);
  loop = g_main_loop_new (nullptr, FALSE);

  auto resource = create_remap_resource ();
  if (!resource)
    return 0;

  auto pipeline_str = g_strdup_printf ("filesrc location=%s "
    "! decodebin ! d3d12upload ! imagefreeze ! tee name=t ! queue "
    "! d3d12remap name=remap ! d3d12videosink t. ! queue ! d3d12videosink",
      location);

  auto pipeline = gst_parse_launch (pipeline_str, nullptr);
  g_free (location);
  g_free (pipeline_str);
  if (!pipeline) {
    gst_println ("Couldn't create pipeline");
    return 0;
  }

  auto remap = gst_bin_get_by_name (GST_BIN (pipeline), "remap");
  g_object_set (remap, "uv-remap", resource->uv_remap.Get (), nullptr);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_msg, nullptr);

  data.resource = resource.get();
  data.remap = gst_bin_get_by_name (GST_BIN (pipeline), "remap");

  print_keyboard_help ();
  set_key_handler ((KeyInputCallback) keyboard_cb, &data);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (pipeline));

  gst_object_unref (data.remap);
  gst_object_unref (pipeline);
  resource = nullptr;

  return 0;
}
