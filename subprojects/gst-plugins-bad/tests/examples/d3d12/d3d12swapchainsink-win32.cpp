/* GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>
#include <directx/d3dx12.h>

#include <windows.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>
#include <memory>
#include <d3dcompiler.h>
#include <string.h>
#include "../key-handler.h"

using namespace Microsoft::WRL;

static GMainLoop *loop_ = nullptr;
static HWND hwnd_ = nullptr;
#define VIEW_WIDTH 640
#define VIEW_HEIGHT 480
#define REMAP_SIZE 1024

static const gchar *shader_str = R"(
RWTexture2D<float4> uvLUT : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
  uint width, height;
  uvLUT.GetDimensions(width, height);

  if (DTid.x >= width || DTid.y >= height)
    return;

  float4 remapUV = float4(0.0, 0.0, 0.0, 1.0);
  remapUV.x = 1.0 - ((float) DTid.x / (float) width);
  remapUV.y = 1.0 - ((float) DTid.y / (float) height);

  uvLUT[int2(DTid.xy)] = remapUV;
}
)";

struct GpuResource
{
  ~GpuResource ()
  {
    if (fence_val > 0 && device) {
      gst_d3d12_device_fence_wait (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
          fence_val);
    }

    gst_clear_object (&device);
  }

  ComPtr<IDCompositionDesktopDevice> dcomp_device;
  ComPtr<IDCompositionTarget> target;
  ComPtr<IDCompositionVisual2> visual;
  ComPtr<IDCompositionVirtualSurface> bg_surface;
  ComPtr<IDCompositionVisual2> swapchain_visual;
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> context11;
  GstD3D12Device *device = nullptr;
  guint64 fence_val = 0;
  ComPtr<ID3D12CommandAllocator> ca;
  ComPtr<ID3D12GraphicsCommandList> cl;
  ComPtr<ID3D12RootSignature> rs;
  ComPtr<ID3D12PipelineState> pso;
  ComPtr<ID3D12Resource> uv_remap;
  ComPtr<ID3D12DescriptorHeap> desc_heap;
};

struct AppData
{
  GstElement *pipeline = nullptr;
  GstElement *sink = nullptr;
  std::shared_ptr<GpuResource> resource;
};

#define APP_DATA_PROP_NAME L"EXAMPLE-APP-DATA"

static LRESULT CALLBACK
window_proc (HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message) {
    case WM_NCCREATE:
    {
      LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW) lparam;
      auto data = (AppData *) lpcs->lpCreateParams;
      SetPropW (hwnd, APP_DATA_PROP_NAME, data);
      break;
    }
    case WM_DESTROY:
      gst_println ("Destroy window");
      if (loop_)
        g_main_loop_quit (loop_);
      break;
    case WM_SIZE:
    {
      auto data = (AppData *) GetPropW (hwnd, APP_DATA_PROP_NAME);
      if (!data)
        break;

      auto resource = data->resource;
      if (!resource)
        break;

      RECT rect = { };
      GetClientRect (hwnd, &rect);
      gint width = (rect.right - rect.left);
      gint height = (rect.bottom - rect.top);

      if (width > 0 && height > 0) {
        POINT offset;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        auto hr = resource->bg_surface->Resize (width, height);
        if (SUCCEEDED (hr)) {
          hr = resource->bg_surface->BeginDraw (nullptr,
              IID_PPV_ARGS (&texture), &offset);
        }

        if (SUCCEEDED (hr)) {
          hr = resource->device11->CreateRenderTargetView (texture.Get (), nullptr,
              &rtv);
        }

        if (SUCCEEDED (hr)) {
          FLOAT bg_color[] = { 0.5, 0.5, 0.5, 0.5 };
          resource->context11->ClearRenderTargetView (rtv.Get (), bg_color);
          hr = resource->bg_surface->EndDraw ();
        }

        if (SUCCEEDED (hr)) {
          if (width > VIEW_WIDTH) {
            FLOAT offset_x = ((FLOAT) (width - VIEW_WIDTH)) / 2.0;
            resource->swapchain_visual->SetOffsetX (offset_x);
          } else {
            resource->swapchain_visual->SetOffsetX (0.0);
          }

          if (height > VIEW_HEIGHT) {
            FLOAT offset_y = ((FLOAT) (height - VIEW_HEIGHT)) / 2.0;
            resource->swapchain_visual->SetOffsetY (offset_y);
          } else {
            resource->swapchain_visual->SetOffsetY (0.0);
          }

          resource->dcomp_device->Commit ();
        }
      }
      break;
    }
    default:
      break;
  }

  return DefWindowProcW (hwnd, message, wparam, lparam);
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, AppData * data)
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

      g_main_loop_quit (loop_);
      break;
    }
    case GST_MESSAGE_EOS:
    {
      gst_println ("Got EOS");
      g_main_loop_quit (loop_);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
msg_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

static void
keyboard_cb (gchar input, gboolean is_ascii, AppData * app_data)
{
  static gboolean set_remap = FALSE;
  static GstState state = GST_STATE_PLAYING;

  if (is_ascii) {
    switch (input) {
      case ' ':
        if (state == GST_STATE_PAUSED)
          state = GST_STATE_PLAYING;
        else
          state = GST_STATE_PAUSED;
        gst_println ("Change state to %s", gst_element_state_get_name (state));

        gst_element_set_state (app_data->pipeline, state);
        break;
      case 'm':
      case 'M':
        set_remap = set_remap ? FALSE : TRUE;
        gst_println ("Set remap %d", set_remap);
        if (set_remap) {
          ID3D12Resource *remap[2];
          D3D12_VIEWPORT viewport[2];

          /* top-left, draw original image */
          remap[0] = nullptr;
          viewport[0].TopLeftX = 0;
          viewport[0].TopLeftY = 0;
          viewport[0].Width = 0.5;
          viewport[0].Height = 0.5;

          /* bottom-right, perform uv remap */
          remap[1] = app_data->resource->uv_remap.Get ();
          viewport[1].TopLeftX = 0.5;
          viewport[1].TopLeftY = 0.5;
          viewport[1].Width = 0.5;
          viewport[1].Height = 0.5;

          g_signal_emit_by_name (app_data->sink, "uv-remap", 2, remap, viewport);
        } else {
          /* Clear remap */
          g_signal_emit_by_name (app_data->sink,
              "uv-remap", 0, nullptr, nullptr);
        }

        /* Redraw to update view */
        if (state == GST_STATE_PAUSED)
          g_signal_emit_by_name (app_data->sink, "redraw");
        break;
      case 'q':
        g_main_loop_quit (loop_);
        break;
      default:
        break;
    }
  }
}

static HRESULT
creat_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
  CD3DX12_ROOT_PARAMETER root_params;
  CD3DX12_DESCRIPTOR_RANGE range_uav;

  range_uav.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
  root_params.InitAsDescriptorTable (1, &range_uav);
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 1, &root_params,
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
  auto hr = D3DCompile (shader_str, strlen (shader_str),
      nullptr, nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, blob, &error_blob);

  if (!gst_d3d12_result (hr, device)) {
    const gchar *error_msg = nullptr;
    if (error_blob)
      error_msg = (const gchar *) error_blob->GetBufferPointer ();

    gst_println ("Couldn't compile shader, hr: 0x%x, error detail: %s",
        (guint) hr, GST_STR_NULL (error_msg));
  }

  return hr;
}

static gboolean
create_remap_resource (GpuResource * resource)
{
  resource->device = gst_d3d12_device_new (0);
  if (!resource->device) {
    gst_println ("Couldn't create d3d12 device");
    return FALSE;
  }

  /* Prepare compute shader and resource.
   * Compute shader will write UV remap data to RGBA texture
   * (R -> U, G -> V, B -> unused, A -> mask where A < 0.5 will fill background
   * color)
   */
  ComPtr<ID3DBlob> shader_blob;
  auto hr = compile_shader (resource->device, &shader_blob);
  if (FAILED (hr))
    return FALSE;

  ComPtr<ID3DBlob> rs_blob;
  hr = creat_rs_blob (resource->device, &rs_blob);
  if (FAILED (hr))
    return FALSE;

  auto device_handle = gst_d3d12_device_get_device_handle (resource->device);
  hr = device_handle->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&resource->rs));
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't create root signature");
    return FALSE;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
  pso_desc.pRootSignature = resource->rs.Get ();
  pso_desc.CS.pShaderBytecode = shader_blob->GetBufferPointer ();
  pso_desc.CS.BytecodeLength = shader_blob->GetBufferSize ();
  hr = device_handle->CreateComputePipelineState (&pso_desc,
        IID_PPV_ARGS (&resource->pso));
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't create pso");
    return FALSE;
  }

  D3D12_HEAP_PROPERTIES heap_prop =
    CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
  D3D12_RESOURCE_DESC resource_desc =
    CD3DX12_RESOURCE_DESC::Tex2D (DXGI_FORMAT_R16G16B16A16_UNORM,
    REMAP_SIZE, REMAP_SIZE, 1, 1, 1, 0,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
    D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
  hr = device_handle->CreateCommittedResource (&heap_prop, D3D12_HEAP_FLAG_NONE,
      &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS (&resource->uv_remap));
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't create texture");
    return FALSE;
  }

  D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = { };
  desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc_heap_desc.NumDescriptors = 1;
  desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  hr = device_handle->CreateDescriptorHeap (&desc_heap_desc,
      IID_PPV_ARGS (&resource->desc_heap));
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't create descriptor heap");
    return FALSE;
  }

  auto cpu_handle = resource->desc_heap->GetCPUDescriptorHandleForHeapStart ();
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
  uav_desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device_handle->CreateUnorderedAccessView (resource->uv_remap.Get (),
      nullptr, &uav_desc, cpu_handle);

  hr = device_handle->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT,
      IID_PPV_ARGS (&resource->ca));
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't create command allocator");
    return FALSE;
  }

  hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      resource->ca.Get (), nullptr, IID_PPV_ARGS (&resource->cl));
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't create command list");
    return FALSE;
  }

  ID3D12DescriptorHeap *heaps[] = { resource->desc_heap.Get () };
  resource->cl->SetComputeRootSignature (resource->rs.Get ());
  resource->cl->SetPipelineState (resource->pso.Get ());
  resource->cl->SetDescriptorHeaps (1, heaps);
  resource->cl->SetComputeRootDescriptorTable (0,
      resource->desc_heap->GetGPUDescriptorHandleForHeapStart ());
  resource->cl->Dispatch ((REMAP_SIZE + 7) / 8, (REMAP_SIZE + 7) / 8, 1);
  hr = resource->cl->Close ();

  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't close command list");
    return FALSE;
  }

  ID3D12CommandList *cmd_list[] = { resource->cl.Get () };
  hr = gst_d3d12_device_execute_command_lists (resource->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT, 1, cmd_list, &resource->fence_val);
  if (!gst_d3d12_result (hr, resource->device)) {
    gst_println ("Couldn't execute command list");
    return FALSE;
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
    {"m", "Toggle remap on/off"},
    {"space", "Toggle pause/play"},
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

int
main (int argc, char ** argv)
{
  GIOChannel *msg_io_channel = nullptr;
  AppData app_data = { };
  HRESULT hr;
  gchar *uri = nullptr;
  GOptionEntry options[] = {
    {"uri", 0, 0, G_OPTION_ARG_STRING, &uri, "URI to play"},
    {nullptr}
  };

  auto opt_ctx = g_option_context_new ("D3D12 swapchainsink");
  g_option_context_add_main_entries (opt_ctx, options, nullptr);
  g_option_context_set_help_enabled (opt_ctx, TRUE);
  g_option_context_add_group (opt_ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (opt_ctx, &argc, &argv, nullptr)) {
    gst_printerrln ("option parsing failed");
    return 1;
  }

  loop_ = g_main_loop_new (nullptr, FALSE);

  /* Creates pipeline */
  GstElement *sink;
  if (uri) {
    app_data.pipeline = gst_element_factory_make ("playbin3", nullptr);
    if (!app_data.pipeline) {
      gst_printerrln ("Couldn't create pipeline");
      return 1;
    }

    sink = gst_element_factory_make ("d3d12swapchainsink", nullptr);
    if (!sink) {
      gst_printerrln ("Couldn't create sink");
      return 1;
    }

    g_object_set (app_data.pipeline, "video-sink", sink, "uri", uri, nullptr);
    /* playbin will take floating refcount */
    gst_object_ref (sink);
  } else {
    app_data.pipeline = gst_parse_launch ("d3d12testsrc ! "
        "video/x-raw(memory:D3D12Memory),format=RGBA,width=240,height=240 ! "
        "dwritetimeoverlay font-size=50 ! queue ! d3d12swapchainsink name=sink",
        nullptr);

    if (!app_data.pipeline) {
      gst_printerrln ("Couldn't create pipeline");
      return 1;
    }

    sink = gst_bin_get_by_name (GST_BIN (app_data.pipeline), "sink");
    g_assert (sink);
  }

  gst_bus_add_watch (GST_ELEMENT_BUS (app_data.pipeline), (GstBusFunc) bus_msg,
      &app_data);

  /* Set swapchain resolution and border color */
  g_signal_emit_by_name (sink, "resize", VIEW_WIDTH, VIEW_HEIGHT);

  guint64 border_color = 0;
  /* alpha */
  border_color |= ((guint64) (G_MAXUINT16 / 2)) << 48;
  /* red */
  border_color |= ((guint64) (G_MAXUINT16 / 2)) << 32;
  g_object_set (sink, "border-color", border_color, nullptr);

  app_data.sink = sink;

  /* Gets swapchain handle. This swapchain will be bound to a dcomp visual node */
  IUnknown *swapchain = nullptr;
  g_object_get (sink, "swapchain", &swapchain, nullptr);
  if (!swapchain) {
    gst_printerrln ("Couldn't get swapchain");
    return 1;
  }

  /* Creates d3d11 device to initialize dcomp device.
   * Note that d3d11 (or d2d) device will not be required if swapchain is
   * the only visual node (i.e., root node) which needs to be composed.
   * In that case, an application can pass nullptr device to
   * DCompositionCreateDevice2() */
  auto resource = std::make_shared<GpuResource> ();
  ComPtr<IDXGIFactory1> factory;
  ComPtr<IDXGIAdapter> adapter;

  if (!create_remap_resource (resource.get ()))
    return 1;

  hr = CreateDXGIFactory1 (IID_PPV_ARGS (&factory));
  if (FAILED (hr)) {
    gst_printerrln ("CreateDXGIFactory1 failed");
    return 1;
  }

  hr = factory->EnumAdapters (0, &adapter);
  if (FAILED (hr)) {
    gst_printerrln ("EnumAdapters failed");
    return 1;
  }

  static const D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_11_1,
  };
  hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels, 1, D3D11_SDK_VERSION,
      &resource->device11, nullptr, &resource->context11);
  if (FAILED (hr)) {
    gst_printerrln ("D3D11CreateDevice failed");
    return 1;
  }

  /* Prepare main window */
  WNDCLASSEXW wc = { };
  RECT wr = { 0, 0, VIEW_WIDTH * 2, VIEW_HEIGHT * 2 };
  HINSTANCE hinstance = GetModuleHandle (nullptr);
  wc.cbSize = sizeof (WNDCLASSEXW);
  wc.lpfnWndProc = (WNDPROC) window_proc;
  wc.hInstance = hinstance;
  wc.hIcon = LoadIcon (nullptr, IDI_WINLOGO);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  wc.lpszClassName = L"GstD3D12SwapChainSinkExample";

  RegisterClassExW (&wc);
  AdjustWindowRect (&wr, WS_OVERLAPPEDWINDOW, FALSE);

  hwnd_ = CreateWindowExW (WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
      L"D3D12SwapChainSink Example - Win32",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) nullptr, (HMENU) nullptr,
      hinstance, &app_data);

  msg_io_channel = g_io_channel_win32_new_messages (0);
  g_io_add_watch (msg_io_channel, G_IO_IN, msg_cb, nullptr);

  /* Create DComp resources */
  hr = DCompositionCreateDevice2 (resource->device11.Get (),
      IID_PPV_ARGS (&resource->dcomp_device));
  if (FAILED (hr)) {
    gst_printerrln ("Couldn't create composition device");
    return 1;
  }

  hr = resource->dcomp_device->CreateTargetForHwnd (hwnd_, TRUE,
      &resource->target);
  if (FAILED (hr)) {
    gst_printerrln ("CreateTargetForHwnd failed");
    return 1;
  }

  hr = resource->dcomp_device->CreateVisual (&resource->visual);
  if (FAILED (hr)) {
    gst_printerrln ("CreateVisual failed");
    return 1;
  }

  hr = resource->target->SetRoot (resource->visual.Get ());
  if (FAILED (hr)) {
    gst_printerrln ("SetRoot failed");
    return 1;
  }

  /* Create background visual, and clear color using d3d11 API */
  hr = resource->dcomp_device->CreateVirtualSurface (VIEW_WIDTH * 2,
      VIEW_HEIGHT * 2,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
      &resource->bg_surface);
  if (FAILED (hr)) {
    gst_printerrln ("CreateVirtualSurface failed");
    return 1;
  }

  hr = resource->visual->SetContent (resource->bg_surface.Get ());
  if (FAILED (hr)) {
    gst_printerrln ("SetContent failed");
    return 1;
  }

  {
    POINT offset;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    hr = resource->bg_surface->BeginDraw (nullptr, IID_PPV_ARGS (&texture),
        &offset);
    if (FAILED (hr)) {
      gst_printerrln ("BeginDraw failed");
      return 1;
    }

    hr = resource->device11->CreateRenderTargetView (texture.Get (),
        nullptr, &rtv);
    if (FAILED (hr)) {
      gst_printerrln ("CreateRenderTargetView failed");
      return 1;
    }

    /* Draw semi-transparent background */
    FLOAT bg_color[] = { 0.5, 0.5, 0.5, 0.5 };
    resource->context11->ClearRenderTargetView (rtv.Get (), bg_color);
    hr = resource->bg_surface->EndDraw ();
    if (FAILED (hr)) {
      gst_printerrln ("EndDraw failed");
      return 1;
    }
  }

  hr = resource->dcomp_device->CreateVisual (&resource->swapchain_visual);
  if (FAILED (hr)) {
    gst_printerrln ("CreateVisual failed");
    return 1;
  }

  hr = resource->visual->AddVisual (resource->swapchain_visual.Get (), TRUE, nullptr);
  if (FAILED (hr)) {
    gst_printerrln ("AddVisual failed");
    return 1;
  }

  hr = resource->swapchain_visual->SetOffsetX (VIEW_WIDTH / 2);
  if (FAILED (hr)) {
    gst_printerrln ("SetOffsetX failed");
    return 1;
  }

  hr = resource->swapchain_visual->SetOffsetY (VIEW_HEIGHT / 2);
  if (FAILED (hr)) {
    gst_printerrln ("SetOffsetY failed");
    return 1;
  }

  hr = resource->swapchain_visual->SetContent (swapchain);
  if (FAILED (hr)) {
    gst_printerrln ("SetContent failed");
    return 1;
  }

  hr = resource->dcomp_device->Commit ();
  if (FAILED (hr)) {
    gst_printerrln ("Commit failed");
    return 1;
  }

  app_data.resource = std::move (resource);

  set_key_handler ((KeyInputCallback) keyboard_cb, &app_data);
  print_keyboard_help ();
  gst_element_set_state (app_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop_);

  unset_key_handler ();

  gst_element_set_state (app_data.pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (app_data.pipeline));

  app_data.resource = nullptr;
  gst_object_unref (app_data.pipeline);
  gst_object_unref (app_data.sink);

  if (hwnd_)
    DestroyWindow (hwnd_);

  g_io_channel_unref (msg_io_channel);
  g_main_loop_unref (loop_);
  g_free (uri);

  gst_deinit ();

  return 0;
}
