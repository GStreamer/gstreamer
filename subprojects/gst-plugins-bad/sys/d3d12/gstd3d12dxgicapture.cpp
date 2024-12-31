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

/*
 * The MIT License (MIT)
 *
 * Copyright (c) Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12dxgicapture.h"
#include "gstd3d12pluginutils.h"
#include <d3d11_4.h>
#include <directx/d3dx12.h>
#include <string.h>
#include <mutex>
#include <vector>
#include <memory>
#include <future>
#include <wrl.h>
#include <gst/d3dshader/gstd3dshader.h>
#include <gmodule.h>

#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_screen_capture_debug);
#define GST_CAT_DEFAULT gst_d3d12_screen_capture_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace DirectX;

/* List of GstD3D12DxgiCapture weakref */
static std::mutex g_g_dupl_list_lock;
static GList *g_dupl_list = nullptr;

/* Below implementation were taken from Microsoft sample
 * https://github.com/microsoft/Windows-classic-samples/tree/master/Samples/DXGIDesktopDuplication
 */

/* These are the errors we expect from IDXGIOutput1::DuplicateOutput
 * due to a transition */
static HRESULT CreateDuplicationExpectedErrors[] = {
  DXGI_ERROR_DEVICE_REMOVED,
  static_cast<HRESULT>(E_ACCESSDENIED),
  DXGI_ERROR_SESSION_DISCONNECTED,
  S_OK
};

/* These are the errors we expect from IDXGIOutputDuplication methods
 * due to a transition */
static HRESULT FrameInfoExpectedErrors[] = {
  DXGI_ERROR_DEVICE_REMOVED,
  DXGI_ERROR_ACCESS_LOST,
  S_OK
};

static GstFlowReturn
flow_return_from_hr (ID3D11Device * device,
    HRESULT hr, HRESULT * expected_errors = nullptr)
{
  HRESULT translated_hr = hr;

  /* On an error check if the DX device is lost */
  if (device) {
    HRESULT remove_reason = device->GetDeviceRemovedReason ();

    switch (remove_reason) {
      case DXGI_ERROR_DEVICE_REMOVED:
      case DXGI_ERROR_DEVICE_RESET:
      case static_cast<HRESULT>(E_OUTOFMEMORY):
        /* Our device has been stopped due to an external event on the GPU so
         * map them all to device removed and continue processing the condition
         */
        translated_hr = DXGI_ERROR_DEVICE_REMOVED;
        break;
      case S_OK:
        /* Device is not removed so use original error */
        break;
      default:
        /* Device is removed but not a error we want to remap */
        translated_hr = remove_reason;
        break;
    }
  }

  /* Check if this error was expected or not */
  if (expected_errors) {
    HRESULT* rst = expected_errors;

    while (*rst != S_OK) {
      if (*rst == translated_hr)
        return GST_D3D12_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR;

      rst++;
    }
  }

  return GST_FLOW_ERROR;
}

static guint
get_sdr_white_level (PCWSTR name)
{
  LONG ret = ERROR_SUCCESS;
  std::vector < DISPLAYCONFIG_PATH_INFO > path_info;
  std::vector < DISPLAYCONFIG_MODE_INFO > mode_info;
  gint retry_count = 0;
  guint nits = 80;

  /* QueryDisplayConfig() may return ERROR_INSUFFICIENT_BUFFER if there was
   * configuration update between GetDisplayConfigBufferSizes() and
   * QueryDisplayConfig() call. */
  while (1) {
    UINT32 n_path = 0;
    UINT32 n_mode = 0;

    ret = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &n_path, &n_mode);
    if (ret != ERROR_SUCCESS) {
      GST_WARNING ("GetDisplayConfigBufferSizes failed %d", (gint) ret);
      return nits;
    }

    path_info.resize (n_path);
    mode_info.resize (n_mode);

    ret = QueryDisplayConfig (QDC_ONLY_ACTIVE_PATHS, &n_path, path_info.data (),
        &n_mode, mode_info.data (), nullptr);
    if (ret == ERROR_INSUFFICIENT_BUFFER) {
      /* XXX: avoid infinite loop */
      retry_count++;
      if (retry_count > 100) {
        GST_WARNING ("Too many retry, give up");
        return nits;
      }

      GST_DEBUG ("Insufficient buffer, retrying");
      continue;
    } else if (ret != ERROR_SUCCESS) {
      GST_WARNING ("QueryDisplayConfig failed %d", (gint) ret);
      return nits;
    }

    path_info.resize (n_path);
    mode_info.resize (n_mode);
    break;
  }

  for (size_t i = 0; i < path_info.size (); i++) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME src_name = { };
    src_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    src_name.header.size = sizeof (DISPLAYCONFIG_SOURCE_DEVICE_NAME);
    src_name.header.adapterId = path_info[i].sourceInfo.adapterId;
    src_name.header.id = path_info[i].sourceInfo.id;

    ret = DisplayConfigGetDeviceInfo (&src_name.header);
    if (ret == ERROR_SUCCESS && wcscmp (name, src_name.viewGdiDeviceName) == 0) {
      DISPLAYCONFIG_SDR_WHITE_LEVEL level;
      level.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
      level.header.size = sizeof (level);
      level.header.adapterId = path_info[i].targetInfo.adapterId;
      level.header.id = path_info[i].targetInfo.id;
      ret = DisplayConfigGetDeviceInfo (&level.header);
      if (ret != ERROR_SUCCESS) {
        GST_WARNING ("Couldn't get SDR white level info");
        return nits;
      }

      return (level.SDRWhiteLevel * 80) / 1000;
    }
  }

  return nits;
}

struct DxgiCaptureVTable
{
  gboolean loaded;
  DPI_AWARENESS_CONTEXT (WINAPI * SetThreadDpiAwarenessContext) (DPI_AWARENESS_CONTEXT context);
};

static DxgiCaptureVTable g_vtable = { };

static gboolean
gst_d3d12_dxgi_capture_load_library (void)
{
  static GModule *user32_module = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    g_vtable.loaded = FALSE;
    user32_module = g_module_open ("user32.dll", G_MODULE_BIND_LAZY);
    if (!user32_module)
      return;

    if (!g_module_symbol (user32_module, "SetThreadDpiAwarenessContext",
        (gpointer *) &g_vtable.SetThreadDpiAwarenessContext)) {
      return;
    }

    g_vtable.loaded = TRUE;
  } GST_D3D12_CALL_ONCE_END;

  return g_vtable.loaded;
}

struct PtrInfo
{
  PtrInfo ()
  {
    LastTimeStamp.QuadPart = 0;
    position_info.Visible = FALSE;
  }

  void buildMonochrom ()
  {
    width_ = shape_info.Width;
    height_ = shape_info.Height / 2;
    stride_ = width_ * 4;
    UINT pstride = 4;
    auto size = height_ * stride_;
    texture_.resize (size);
    xor_texture_.resize (size);

    const BYTE black[] = { 0, 0, 0, 0xff };
    const BYTE white[] = { 0xff, 0xff, 0xff, 0xff };
    const BYTE transparent[] = { 0, 0, 0, 0 };

    for (UINT row = 0; row < height_; row++) {
      for (UINT col = 0; col < width_; col++) {
        auto src_pos = (row * shape_info.Pitch) + (col / 8);
        auto and_mask = (shape_buffer[src_pos] >> (7 - (col % 8))) & 0x1;
        auto xor_mask = (shape_buffer[src_pos + size] >> (7 - (col % 8))) & 0x1;
        auto dst_pos = (row * stride_) + (col * pstride);

        if (and_mask) {
          memcpy (texture_.data () + dst_pos,
              transparent, sizeof (transparent));

          if (xor_mask) {
            memcpy (xor_texture_.data () + dst_pos, white, sizeof (white));
          } else {
            memcpy (xor_texture_.data () + dst_pos, transparent,
                sizeof (transparent));
          }
        } else {
          memcpy (texture_.data () + dst_pos, black, sizeof (black));
          memcpy (xor_texture_.data () + dst_pos, transparent,
                sizeof (transparent));
        }
      }
    }
  }

  void buildMaskedColor ()
  {
    width_ = shape_info.Width;
    height_ = shape_info.Height;
    stride_ = shape_info.Pitch;

    auto size = height_ * stride_;
    UINT pstride = 4;
    texture_.resize (size);
    xor_texture_.resize (size);
    memcpy (texture_.data (), shape_buffer.data (), size);
    memcpy (xor_texture_.data (), shape_buffer.data (), size);

    for (UINT row = 0; row < height_; row++) {
      for (UINT col = 0; col < width_; col++) {
        auto mask_pos = row * stride_ + col * pstride + 3;
        texture_[mask_pos] = shape_buffer[mask_pos] ? 0 : 0xff;
        xor_texture_[mask_pos] = shape_buffer[mask_pos] ? 0xff : 0;
      }
    }
  }

  void BuildTexture ()
  {
    texture_.clear ();
    xor_texture_.clear ();
    width_ = 0;
    height_ = 0;
    stride_ = 0;

    switch (shape_info.Type) {
      case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
      {
        width_ = shape_info.Width;
        height_ = shape_info.Height;
        stride_ = shape_info.Pitch;

        auto size = stride_ * height_;
        texture_.resize (size);
        memcpy (texture_.data (), shape_buffer.data (), size);
        break;
      }
      case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
        buildMonochrom ();
        break;
      case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
        buildMaskedColor ();
        break;
      default:
        GST_WARNING ("Unexpected shape type %u", shape_info.Type);
        break;
    }

    token_++;
  }

  std::vector<BYTE> shape_buffer;
  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
  DXGI_OUTDUPL_POINTER_POSITION position_info;
  LARGE_INTEGER LastTimeStamp;

  UINT width_ = 0;
  UINT height_ = 0;
  UINT stride_ = 0;
  std::vector<BYTE> texture_;
  std::vector<BYTE> xor_texture_;
  UINT64 token_ = 0;
};

struct VERTEX
{
  XMFLOAT3 Pos;
  XMFLOAT2 TexCoord;
};

struct PSConstBuffer
{
  float sdr_white_level;
  float padding[3];
};

class DesktopDupCtx
{
public:
  DesktopDupCtx () {}

  ~DesktopDupCtx ()
  {
    if (context_) {
      context_->ClearState ();
      context_->Flush ();
    }
  }

  GstFlowReturn Init (HMONITOR monitor, ID3D11Device5 * device,
      ID3D11DeviceContext4 * context, ID3D11Fence * fence,
      ID3D11SamplerState * sampler, ID3D11PixelShader * ps,
      ID3D11PixelShader * ps_scrgb, ID3D11PixelShader * ps_scrgb_tonemap,
      ID3D11Buffer * ps_cbuf, ID3D11VertexShader * vs,
      ID3D11InputLayout * layout, gboolean use_reinhard)
  {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    ComPtr<IDXGIOutput1> output1;
    ComPtr<IDXGIOutput6> output6;

    HRESULT hr = gst_d3d12_screen_capture_find_output_for_monitor (monitor,
        &adapter, &output);
    if (FAILED (hr)) {
      GST_ERROR ("Couldn't get adapter and output for monitor");
      return GST_FLOW_ERROR;
    }

    hr = output.As (&output1);
    if (FAILED (hr)) {
      GST_ERROR ("Couldn't get IDXGIOutput1 interface, hr 0x%x", (guint) hr);
      return GST_FLOW_ERROR;
    }

    PSConstBuffer cbuf;
    cbuf.sdr_white_level = 80.0;
    gboolean is_hdr = FALSE;

    if (gst_d3d12_dxgi_capture_load_library ()) {
      hr = output.As (&output6);
      if (SUCCEEDED (hr)) {
        DXGI_OUTPUT_DESC1 desc1;
        hr = output6->GetDesc1 (&desc1);
        if (SUCCEEDED (hr) &&
            desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
          is_hdr = TRUE;

          MONITORINFOEXW monitor_info = { };
          monitor_info.cbSize = sizeof (MONITORINFOEXW);
          if (GetMonitorInfoW (desc1.Monitor, (LPMONITORINFO) & monitor_info))
            cbuf.sdr_white_level = get_sdr_white_level (monitor_info.szDevice);
        }
      }
    }

    HDESK hdesk = OpenInputDesktop (0, FALSE, GENERIC_ALL);
    if (hdesk) {
      if (!SetThreadDesktop (hdesk)) {
        GST_WARNING ("SetThreadDesktop() failed, error %lu", GetLastError());
      }

      CloseDesktop (hdesk);
    } else {
      GST_WARNING ("OpenInputDesktop() failed, error %lu", GetLastError());
    }

    hr = E_FAIL;
    output_format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (is_hdr) {
      DXGI_FORMAT formats[] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_B8G8R8A8_UNORM,
      };

      /* XXX: DuplicateOutput1() would fail if dpi awareness is not configured */
      auto prev_ctx = g_vtable.SetThreadDpiAwarenessContext
          (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
      hr = output6->DuplicateOutput1(device, 0, 2, formats, &dupl_);

      /* And restore dpi context for the current thread */
      if (prev_ctx != nullptr)
        g_vtable.SetThreadDpiAwarenessContext (prev_ctx);

      if (FAILED (hr)) {
        GST_WARNING ("IDXGIOutput5::DuplicateOutput1 returned 0x%x",
            (guint) hr);
        is_hdr = FALSE;
      } else {
        output_format_ = DXGI_FORMAT_R16G16B16A16_UNORM;
      }
    }

    if (FAILED (hr))
      hr = output1->DuplicateOutput(device, &dupl_);

    if (FAILED (hr)) {
      if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
        GST_ERROR ("Hit the max allowed number of Desktop Duplication session");
        return GST_FLOW_ERROR;
      }

      /* Seems to be one limitation of Desktop Duplication API design
       * See
       * https://docs.microsoft.com/en-US/troubleshoot/windows-client/shell-experience/error-when-dda-capable-app-is-against-gpu
       */
      if (hr == DXGI_ERROR_UNSUPPORTED) {
        GST_WARNING ("IDXGIOutput1::DuplicateOutput returned "
            "DXGI_ERROR_UNSUPPORTED, possiblely application is run against a "
            "discrete GPU");
        return GST_D3D12_SCREEN_CAPTURE_FLOW_UNSUPPORTED;
      }

      return flow_return_from_hr (device, hr,
          CreateDuplicationExpectedErrors);
    }

    dupl_->GetDesc (&output_desc_);

    D3D11_TEXTURE2D_DESC desc = { };
    desc.Width = output_desc_.ModeDesc.Width;
    desc.Height = output_desc_.ModeDesc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = output_format_;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    device_ = device;
    context_ = context;
    shared_fence_ = fence;
    sampler_ = sampler;
    ps_cbuf_ = ps_cbuf;
    vs_ = vs;
    layout_ = layout;

    if (is_hdr) {
      GST_INFO ("HDR with SDR white level %d nits",
          (guint) cbuf.sdr_white_level);
      if (!use_reinhard) {
        GST_INFO ("Use scRGB sampling");
        ps_ = ps_scrgb;
      } else {
        GST_INFO ("use scRGB sampling with reinhard tonemapping");
        ps_ = ps_scrgb_tonemap;
      }
    } else {
      GST_INFO ("Monitor is SDR mode");
      ps_ = ps;
    }

    hr = device->CreateTexture2D (&desc, nullptr, &texture_);
    if (FAILED (hr)) {
      GST_ERROR ("Couldn't create texture");
      return GST_FLOW_ERROR;
    }

    hr = device->CreateRenderTargetView (texture_.Get (), nullptr, &rtv_);
    if (FAILED (hr)) {
      GST_ERROR ("Couldn't create render target view");
      return GST_FLOW_ERROR;
    }

    D3D11_MAPPED_SUBRESOURCE map;
    hr = context->Map (ps_cbuf_.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0, &map); if (FAILED (hr)) {
      GST_ERROR ("Couldn't map constant buffer");
      return GST_FLOW_ERROR;
    }

    memcpy (map.pData, &cbuf, sizeof (PSConstBuffer));
    context->Unmap (ps_cbuf_.Get (), 0);

    viewport_.TopLeftX = 0;
    viewport_.TopLeftY = 0;
    viewport_.MinDepth = 0;
    viewport_.MaxDepth = 1;
    viewport_.Width = desc.Width;
    viewport_.Height = desc.Height;

    return GST_FLOW_OK;
  }

  void
  setMoveRect (DXGI_OUTDUPL_MOVE_RECT * move_rect,
      DXGI_MODE_ROTATION rotation, INT width, INT height,
      RECT * src, RECT * dst)
  {
    switch (rotation) {
      case DXGI_MODE_ROTATION_ROTATE90:
        src->left = height - (move_rect->SourcePoint.y +
            move_rect->DestinationRect.bottom - move_rect->DestinationRect.top);
        src->top = move_rect->SourcePoint.x;
        src->right = height - move_rect->SourcePoint.y;
        src->bottom = move_rect->SourcePoint.x +
            move_rect->DestinationRect.right - move_rect->DestinationRect.left;

        dst->left = height - move_rect->DestinationRect.bottom;
        dst->top = move_rect->DestinationRect.left;
        dst->right = height - move_rect->DestinationRect.top;
        dst->bottom = move_rect->DestinationRect.right;
        break;
      case DXGI_MODE_ROTATION_ROTATE180:
        src->left = width - (move_rect->SourcePoint.x +
            move_rect->DestinationRect.right - move_rect->DestinationRect.left);
        src->top = height - (move_rect->SourcePoint.y +
            move_rect->DestinationRect.bottom - move_rect->DestinationRect.top);
        src->right = width - move_rect->SourcePoint.x;
        src->bottom = height - move_rect->SourcePoint.y;

        dst->left = width - move_rect->DestinationRect.right;
        dst->top = height - move_rect->DestinationRect.bottom;
        dst->right = width - move_rect->DestinationRect.left;
        dst->bottom =  height - move_rect->DestinationRect.top;
        break;
      case DXGI_MODE_ROTATION_ROTATE270:
        src->left = move_rect->SourcePoint.x;
        src->top = width - (move_rect->SourcePoint.x +
            move_rect->DestinationRect.right - move_rect->DestinationRect.left);
        src->right = move_rect->SourcePoint.y +
            move_rect->DestinationRect.bottom - move_rect->DestinationRect.top;
        src->bottom = width - move_rect->SourcePoint.x;

        dst->left = move_rect->DestinationRect.top;
        dst->top = width - move_rect->DestinationRect.right;
        dst->right = move_rect->DestinationRect.bottom;
        dst->bottom =  width - move_rect->DestinationRect.left;
        break;
      case DXGI_MODE_ROTATION_UNSPECIFIED:
      case DXGI_MODE_ROTATION_IDENTITY:
      default:
        src->left = move_rect->SourcePoint.x;
        src->top = move_rect->SourcePoint.y;
        src->right = move_rect->SourcePoint.x +
            move_rect->DestinationRect.right - move_rect->DestinationRect.left;
        src->bottom = move_rect->SourcePoint.y +
            move_rect->DestinationRect.bottom - move_rect->DestinationRect.top;

        *dst = move_rect->DestinationRect;
        break;
    }
  }

  GstFlowReturn
  copyMoveRects (ID3D11Texture2D * src, DXGI_OUTDUPL_MOVE_RECT * rects,
      UINT move_count)
  {
    if (!move_texture_) {
      D3D11_TEXTURE2D_DESC desc;
      src->GetDesc (&desc);
      desc.BindFlags = 0;
      desc.MiscFlags = 0;
      auto hr = device_->CreateTexture2D (&desc, nullptr, &move_texture_);
      if (FAILED (hr)) {
        GST_ERROR ("Couldn't create move texture");
        return GST_FLOW_ERROR;
      }
    }

    for (UINT i = 0; i < move_count; i++) {
      RECT src;
      RECT dst;

      setMoveRect (&rects[i], output_desc_.Rotation,
          output_desc_.ModeDesc.Width, output_desc_.ModeDesc.Height,
          &src, &dst);

      D3D11_BOX src_box = { };
      src_box.front = 0;
      src_box.back = 1;
      src_box.left = src.left;
      src_box.top = src.top;
      src_box.right = src.right;
      src_box.bottom = src.bottom;

      context_->CopySubresourceRegion(move_texture_.Get(),
          0, src.left, src.top, 0, texture_.Get (), 0, &src_box);
      context_->CopySubresourceRegion (texture_.Get (), 0, dst.left, dst.top,
          0, move_texture_.Get (), 0, &src_box);
    }

    return GST_FLOW_OK;
  }

  void
  setDirtyVert (RECT * dirty, DXGI_MODE_ROTATION rotation,
      INT width, INT height, VERTEX * vert)
  {
    FLOAT center_x = width / 2.0;
    FLOAT center_y = height / 2.0;

    RECT dst = *dirty;

    switch (rotation) {
      case DXGI_MODE_ROTATION_ROTATE90:
        dst.left = width - dirty->bottom;
        dst.top = dirty->left;
        dst.right = width - dirty->top;
        dst.bottom = dirty->right;

        vert[0].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        vert[1].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        vert[2].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        vert[5].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        break;
      case DXGI_MODE_ROTATION_ROTATE180:
        dst.left = width - dirty->right;
        dst.top = height - dirty->bottom;
        dst.right = width - dirty->left;
        dst.bottom = height - dirty->top;

        vert[0].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        vert[1].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        vert[2].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        vert[5].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        break;
      case DXGI_MODE_ROTATION_ROTATE270:
        dst.left = dirty->top;
        dst.top = height - dirty->right;
        dst.right = dirty->bottom;
        dst.bottom = height - dirty->left;

        vert[0].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        vert[1].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        vert[2].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        vert[5].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        break;
      case DXGI_MODE_ROTATION_UNSPECIFIED:
      case DXGI_MODE_ROTATION_IDENTITY:
      default:
        vert[0].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        vert[1].TexCoord =
            XMFLOAT2(dirty->left / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        vert[2].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->bottom / static_cast<FLOAT>(height));
        vert[5].TexCoord =
            XMFLOAT2(dirty->right / static_cast<FLOAT>(width),
                     dirty->top / static_cast<FLOAT>(height));
        break;
    }

    vert[0].Pos =
        XMFLOAT3(
          (dst.left - center_x) / center_x,
          -1 * (dst.bottom - center_y) / center_y,
          0.0f);
    vert[1].Pos =
        XMFLOAT3(
          (dst.left - center_x) / center_x,
          -1 * (dst.top - center_y) / center_y,
          0.0f);
    vert[2].Pos =
        XMFLOAT3(
          (dst.right - center_x) / center_x,
          -1 * (dst.bottom - center_y) / center_y,
          0.0f);
    vert[3].Pos = vert[2].Pos;
    vert[4].Pos = vert[1].Pos;
    vert[5].Pos =
        XMFLOAT3(
          (dst.right - center_x) / center_x,
          -1 * (dst.top - center_y) / center_y,
          0.0f);

    vert[3].TexCoord = vert[2].TexCoord;
    vert[4].TexCoord = vert[1].TexCoord;
  }

  GstFlowReturn
  copyDirtyRects (ID3D11Texture2D * src, RECT * dirty_rects, UINT dirty_count)
  {
    if (dirty_count == 0)
      return GST_FLOW_OK;

    ComPtr<ID3D11ShaderResourceView> cur_srv;
    auto hr = device_->CreateShaderResourceView (src, nullptr, &cur_srv);
    if (FAILED (hr)) {
      GST_ERROR ("Couldn't create shader resource view");
      return GST_FLOW_ERROR;
    }

    auto byte_needed = sizeof (VERTEX) * 6 * dirty_count;
    dirty_vertex_.resize (dirty_count * 6);
    VERTEX *vert_data = dirty_vertex_.data ();

    for (guint i = 0; i < dirty_count; i++, vert_data += 6) {
      setDirtyVert (&dirty_rects[i], output_desc_.Rotation,
          output_desc_.ModeDesc.Width, output_desc_.ModeDesc.Height,
          vert_data);
    }

    if (byte_needed > vertext_buf_size_)
      vertex_buf_ = nullptr;

    if (!vertex_buf_) {
      vertext_buf_size_ = byte_needed;

      D3D11_BUFFER_DESC buf_desc = { };
      D3D11_SUBRESOURCE_DATA subresource = { };
      buf_desc.Usage = D3D11_USAGE_DYNAMIC;
      buf_desc.ByteWidth = byte_needed;
      buf_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      subresource.pSysMem = dirty_vertex_.data ();
      subresource.SysMemPitch = byte_needed;

      hr = device_->CreateBuffer (&buf_desc, &subresource, &vertex_buf_);
      if (FAILED (hr)) {
        GST_ERROR ("Couldn't create vertex buffer");
        return GST_FLOW_ERROR;
      }
    } else {
      D3D11_MAPPED_SUBRESOURCE mapped;
      hr = context_->Map (vertex_buf_.Get (),  0, D3D11_MAP_WRITE_DISCARD,
          0, &mapped);
      if (FAILED (hr)) {
        GST_ERROR ("Couldn't map vertex buffer");
        return GST_FLOW_ERROR;
      }

      memcpy (mapped.pData, dirty_vertex_.data (), byte_needed);
      context_->Unmap (vertex_buf_.Get (), 0);
    }

    UINT stride = sizeof (VERTEX);
    UINT offset = 0;
    ID3D11Buffer *vert[] = { vertex_buf_.Get () };
    context_->IASetVertexBuffers (0, 1, vert, &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(layout_.Get());
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer *ps_cbuf[] = { ps_cbuf_.Get () };
    context_->PSSetConstantBuffers (0, 1, ps_cbuf);

    ID3D11ShaderResourceView *srv[] = { cur_srv.Get () };
    context_->PSSetShaderResources(0, 1, srv);

    ID3D11SamplerState *sampler[] = { sampler_.Get () };
    context_->PSSetSamplers(0, 1, sampler);

    context_->RSSetViewports (1, &viewport_);

    ID3D11RenderTargetView *rtv[] = { rtv_.Get () };
    context_->OMSetRenderTargets(1, rtv, nullptr);
    context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    context_->Draw (6 * dirty_count, 0);

    srv[0] = nullptr;
    rtv[0] = nullptr;

    context_->PSSetShaderResources(0, 1, srv);
    context_->OMSetRenderTargets(1, rtv, nullptr);

    return GST_FLOW_OK;
  }

  GstFlowReturn
  ExecuteInternal (IDXGIResource * resource)
  {
    ComPtr<ID3D11Texture2D> cur_texture;
    HRESULT hr;
    resource->QueryInterface (IID_PPV_ARGS (&cur_texture));
    if (!cur_texture) {
      GST_ERROR ("Couldn't get texture interface");
      return GST_FLOW_ERROR;
    }

    metadata_buffer_.resize (frame_info_.TotalMetadataBufferSize);
    if (frame_info_.TotalMetadataBufferSize > 0) {
      UINT buf_size = frame_info_.TotalMetadataBufferSize;
      hr = dupl_->GetFrameMoveRects (buf_size,
        (DXGI_OUTDUPL_MOVE_RECT *) metadata_buffer_.data (), &buf_size);
      if (FAILED (hr)) {
        GST_ERROR ("Couldn't get move rect, hr: 0x%x", (guint) hr);
        return flow_return_from_hr (device_.Get (),
            hr, FrameInfoExpectedErrors);
      }

      auto move_count = buf_size / sizeof (DXGI_OUTDUPL_MOVE_RECT);
      if (move_count > 0) {
        auto ret = copyMoveRects (cur_texture.Get (),
            (DXGI_OUTDUPL_MOVE_RECT *) metadata_buffer_.data (), move_count);
        if (ret != GST_FLOW_OK)
          return ret;
      }

      auto dirty_rects = metadata_buffer_.data () + buf_size;
      buf_size = frame_info_.TotalMetadataBufferSize - buf_size;

      hr = dupl_->GetFrameDirtyRects (buf_size, (RECT *) dirty_rects,
          &buf_size);
      if (FAILED (hr)) {
        GST_ERROR ("Couldn't get dirty rect, hr: 0x%x", (guint) hr);
        return flow_return_from_hr (device_.Get (),
            hr, FrameInfoExpectedErrors);
      }

      auto dirty_count = buf_size / sizeof (RECT);
      if (dirty_count > 0) {
        auto ret = copyDirtyRects (cur_texture.Get (), (RECT *) dirty_rects,
            dirty_count);
        if (ret != GST_FLOW_OK)
          return ret;
      }
    }

    if (frame_info_.LastMouseUpdateTime.QuadPart != 0) {
      ptr_info_.position_info = frame_info_.PointerPosition;
      ptr_info_.LastTimeStamp = frame_info_.LastMouseUpdateTime;

      if (frame_info_.PointerShapeBufferSize > 0) {
        UINT buf_size;
        ptr_info_.shape_buffer.resize (frame_info_.PointerShapeBufferSize);
        hr = dupl_->GetFramePointerShape (frame_info_.PointerShapeBufferSize,
            (void *) ptr_info_.shape_buffer.data (), &buf_size,
            &ptr_info_.shape_info);
        if (FAILED (hr)) {
          return flow_return_from_hr(device_.Get (),
              hr, FrameInfoExpectedErrors);
        }

        ptr_info_.BuildTexture ();
      }
    }

    return GST_FLOW_OK;
  }

  GstFlowReturn
  Execute (ID3D11Texture2D * dest, D3D11_BOX * src_box, UINT64 fence_val)
  {
    ComPtr<IDXGIResource> resource;
    auto hr = dupl_->AcquireNextFrame(0, &frame_info_, &resource);
    if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
      if (FAILED (hr)) {
        GST_WARNING ("AcquireNextFrame failed with 0x%x", (guint) hr);
        /* XXX: HDR <-> SDR mode switching seems to be racy,
         * and AcquireNextFrame() seems to return DXGI_ERROR_INVALID_CALL
         * sometimes on HDR <-> SDR mode switching.
         * Do return GST_D3D12_SCREEN_CAPTURE_FLOW_UNSUPPORTED here
         * if AcquireNextFrame() returns DXGI_ERROR_INVALID_CALL, then
         * source element will do retry a bit more */
        if (hr == DXGI_ERROR_INVALID_CALL) {
          GST_WARNING ("DXGI_ERROR_INVALID_CALL, trying again");
          dupl_->ReleaseFrame ();
          return GST_D3D12_SCREEN_CAPTURE_FLOW_UNSUPPORTED;
        }

        dupl_->ReleaseFrame ();
        return flow_return_from_hr (device_.Get (), hr, FrameInfoExpectedErrors);
      }

      auto ret = ExecuteInternal(resource.Get ());
      dupl_->ReleaseFrame ();

      if (ret != GST_FLOW_OK)
        return ret;
    } else {
      dupl_->ReleaseFrame ();
    }

    context_->CopySubresourceRegion (dest, 0, 0, 0, 0,
        texture_.Get (), 0, src_box);
    context_->Signal (shared_fence_.Get(), fence_val);

    return GST_FLOW_OK;
  }

  void GetSize (guint * width, guint * height)
  {
    *width = output_desc_.ModeDesc.Width;
    *height = output_desc_.ModeDesc.Height;
  }

  DXGI_FORMAT GetFormat ()
  {
    return output_format_;
  }

  DXGI_OUTDUPL_DESC GetDesc ()
  {
    return output_desc_;
  }

  const PtrInfo & GetPointerInfo ()
  {
    return ptr_info_;
  }

  ID3D11Fence * GetFence ()
  {
    return shared_fence_.Get ();
  }

  ID3D11Device * GetDevice ()
  {
    return device_.Get ();
  }

private:
  PtrInfo ptr_info_;
  DXGI_OUTDUPL_DESC output_desc_;
  DXGI_OUTDUPL_FRAME_INFO frame_info_;
  ComPtr<IDXGIOutputDuplication> dupl_;
  ComPtr<ID3D11Device5> device_;
  ComPtr<ID3D11DeviceContext4> context_;
  ComPtr<ID3D11Fence> shared_fence_;
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<ID3D11Texture2D> move_texture_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11PixelShader> ps_scrgb_;
  ComPtr<ID3D11PixelShader> ps_scrgb_tonemap_;
  ComPtr<ID3D11Buffer> ps_cbuf_;
  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11InputLayout> layout_;
  ComPtr<ID3D11Buffer> vertex_buf_;
  UINT vertext_buf_size_ = 0;
  D3D11_VIEWPORT viewport_ = { };
  std::vector<VERTEX> dirty_vertex_;
  DXGI_FORMAT output_format_ = DXGI_FORMAT_B8G8R8A8_UNORM;

  /* frame metadata */
  std::vector<BYTE> metadata_buffer_;
};

struct GstD3D12DxgiCapturePrivate
{
  GstD3D12DxgiCapturePrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12DxgiCapturePrivate ()
  {
    WaitGPU ();
    ctx = nullptr;
    gst_clear_buffer (&mouse_buf);
    gst_clear_buffer (&mouse_xor_buf);
    gst_clear_object (&ca_pool);
    gst_clear_object (&fence_data_pool);
    gst_clear_object (&mouse_blend);
    gst_clear_object (&mouse_xor_blend);
    gst_clear_object (&mouse_blend_scrgb);
    gst_clear_object (&mouse_xor_blend_scrgb);
  }

  void WaitGPU ()
  {
    if (shared_fence) {
      auto completed = shared_fence->GetCompletedValue ();
      if (completed < fence_val)
        shared_fence->SetEventOnCompletion (fence_val, nullptr);
    }
  }

  std::unique_ptr<DesktopDupCtx> ctx;
  ComPtr<IDXGIOutput1> output;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12FenceDataPool *fence_data_pool;
  ComPtr<ID3D12GraphicsCommandList> cl;
  ComPtr<ID3D12Fence> shared_fence;
  ComPtr<ID3D11Device5> device11;
  ComPtr<ID3D11DeviceContext4> context11;
  ComPtr<ID3D11Fence> shared_fence11;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11PixelShader> ps_scrgb;
  ComPtr<ID3D11PixelShader> ps_scrgb_tonemap;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> const_buf;

  GstBuffer *mouse_buf = nullptr;
  GstBuffer *mouse_xor_buf = nullptr;

  GstD3D12Converter *mouse_blend = nullptr;
  GstD3D12Converter *mouse_xor_blend = nullptr;
  GstD3D12Converter *mouse_blend_scrgb = nullptr;
  GstD3D12Converter *mouse_xor_blend_scrgb = nullptr;

  HMONITOR monitor_handle = nullptr;
  RECT desktop_coordinates = { };
  guint sdr_white_level = 80;
  guint prepare_flags = 0;

  guint cached_width = 0;
  guint cached_height = 0;

  guint64 fence_val = 0;

  guint64 mouse_token = 0;

  std::mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12DxgiCapture
{
  GstD3D12ScreenCapture parent;

  GstD3D12Device *device;

  GstD3D12DxgiCapturePrivate *priv;
};

static void gst_d3d12_dxgi_capture_finalize (GObject * object);
static GstFlowReturn
gst_d3d12_dxgi_capture_prepare (GstD3D12ScreenCapture * capture, guint flags);
static gboolean
gst_d3d12_dxgi_capture_get_size (GstD3D12ScreenCapture * capture,
    guint * width, guint * height);
static GstVideoFormat
gst_d3d12_dxgi_capture_get_format (GstD3D12ScreenCapture * capture);

#define gst_d3d12_dxgi_capture_parent_class parent_class
G_DEFINE_TYPE (GstD3D12DxgiCapture, gst_d3d12_dxgi_capture,
    GST_TYPE_D3D12_SCREEN_CAPTURE);

static void
gst_d3d12_dxgi_capture_class_init (GstD3D12DxgiCaptureClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto capture_class = GST_D3D12_SCREEN_CAPTURE_CLASS (klass);

  object_class->finalize = gst_d3d12_dxgi_capture_finalize;

  capture_class->prepare = GST_DEBUG_FUNCPTR (gst_d3d12_dxgi_capture_prepare);
  capture_class->get_size = GST_DEBUG_FUNCPTR (gst_d3d12_dxgi_capture_get_size);
  capture_class->get_format =
      GST_DEBUG_FUNCPTR (gst_d3d12_dxgi_capture_get_format);
}

static void
gst_d3d12_dxgi_capture_init (GstD3D12DxgiCapture * self)
{
  self->priv = new GstD3D12DxgiCapturePrivate ();
}

static void
gst_d3d12_dxgi_capture_finalize (GObject * object)
{
  auto self = GST_D3D12_DXGI_CAPTURE (object);

  delete self->priv;
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_dxgi_capture_weak_ref_notify (gpointer data,
    GstD3D12DxgiCapture * dupl)
{
  std::lock_guard < std::mutex > lk (g_g_dupl_list_lock);
  g_dupl_list = g_list_remove (g_dupl_list, dupl);
}

static gboolean
gst_d3d12_dxgi_capture_open (GstD3D12DxgiCapture * self,
    HMONITOR monitor_handle)
{
  auto priv = self->priv;
  priv->monitor_handle = monitor_handle;

  ComPtr < IDXGIOutput > output;
  ComPtr < IDXGIOutput6 > output6;
  ComPtr < IDXGIAdapter1 > adapter;
  auto hr = gst_d3d12_screen_capture_find_output_for_monitor (monitor_handle,
      &adapter, &output);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_WARNING_OBJECT (self,
        "Failed to find associated adapter for monitor %p", monitor_handle);
    return FALSE;
  }

  hr = output.As (&priv->output);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_WARNING_OBJECT (self, "IDXGIOutput1 interface unavailable");
    return FALSE;
  }

  DXGI_OUTPUT_DESC output_desc;
  hr = output->GetDesc (&output_desc);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_WARNING_OBJECT (self, "Couldn't get output desc");
    return FALSE;
  }

  /* DesktopCoordinates will not report actual texture size in case that
   * application is running without dpi-awareness. To get actual monitor size,
   * we need to use Win32 API... */
  MONITORINFOEXW monitor_info;
  DEVMODEW dev_mode;

  monitor_info.cbSize = sizeof (MONITORINFOEXW);
  if (!GetMonitorInfoW (output_desc.Monitor, (LPMONITORINFO) & monitor_info)) {
    GST_WARNING_OBJECT (self, "Couldn't get monitor info");
    return FALSE;
  }

  dev_mode.dmSize = sizeof (DEVMODEW);
  dev_mode.dmDriverExtra = sizeof (POINTL);
  dev_mode.dmFields = DM_POSITION;
  if (!EnumDisplaySettingsW
      (monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &dev_mode)) {
    GST_WARNING_OBJECT (self, "Couldn't enumerate display settings");
    return FALSE;
  }

  priv->sdr_white_level = 80;
  hr = output.As (&output6);
  if (SUCCEEDED (hr)) {
    DXGI_OUTPUT_DESC1 desc1;
    hr = output6->GetDesc1 (&desc1);
    if (SUCCEEDED (hr) &&
        desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
      priv->sdr_white_level = get_sdr_white_level (monitor_info.szDevice);
      GST_INFO_OBJECT (self, "HDR mode detected, SDR white level in nits: %d",
          priv->sdr_white_level);
    }
  }

  priv->desktop_coordinates.left = dev_mode.dmPosition.x;
  priv->desktop_coordinates.top = dev_mode.dmPosition.y;
  priv->desktop_coordinates.right =
      dev_mode.dmPosition.x + dev_mode.dmPelsWidth;
  priv->desktop_coordinates.bottom =
      dev_mode.dmPosition.y + dev_mode.dmPelsHeight;

  priv->cached_width =
      priv->desktop_coordinates.right - priv->desktop_coordinates.left;
  priv->cached_height =
      priv->desktop_coordinates.bottom - priv->desktop_coordinates.top;

  GST_DEBUG_OBJECT (self,
      "Desktop coordinates left:top:right:bottom = %ld:%ld:%ld:%ld (%dx%d)",
      priv->desktop_coordinates.left, priv->desktop_coordinates.top,
      priv->desktop_coordinates.right, priv->desktop_coordinates.bottom,
      priv->cached_width, priv->cached_height);

  auto device = gst_d3d12_device_get_device_handle (self->device);

  /* size will be updated later */
  GstVideoInfo info;
  GstVideoInfo scrgb_info;
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_BGRA,
      priv->cached_width, priv->cached_height);
  gst_video_info_set_format (&scrgb_info, GST_VIDEO_FORMAT_RGBA64_LE,
      priv->cached_width, priv->cached_height);
  D3D12_BLEND_DESC blend_desc = CD3DX12_BLEND_DESC (D3D12_DEFAULT);

  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
  blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;

  priv->mouse_blend = gst_d3d12_converter_new (self->device, nullptr, &info,
      &info, &blend_desc, nullptr, nullptr);
  priv->mouse_blend_scrgb = gst_d3d12_converter_new (self->device, nullptr,
      &info, &scrgb_info, &blend_desc, nullptr, nullptr);

  blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
  blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
  priv->mouse_xor_blend = gst_d3d12_converter_new (self->device, nullptr, &info,
      &info, &blend_desc, nullptr, nullptr);
  priv->mouse_xor_blend_scrgb = gst_d3d12_converter_new (self->device, nullptr,
      &info, &scrgb_info, &blend_desc, nullptr, nullptr);

  hr = device->CreateFence (0,
      D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS (&priv->shared_fence));
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create shared fence");
    return FALSE;
  }

  priv->ca_pool = gst_d3d12_cmd_alloc_pool_new (device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_1;
  ComPtr < ID3D11Device > device11;
  ComPtr < ID3D11DeviceContext > context11;
  hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, &feature_level, 1, D3D11_SDK_VERSION,
      &device11, nullptr, &context11);

  hr = device11.As (&priv->device11);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "ID3D11Device5 interface unavilable");
    return FALSE;
  }

  hr = context11.As (&priv->context11);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "ID3D11DeviceContext4 interface unavilable");
    return FALSE;
  }

  HANDLE fence_handle;
  hr = device->CreateSharedHandle (priv->shared_fence.Get (),
      nullptr, GENERIC_ALL, nullptr, &fence_handle);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create shared fence handle");
    return FALSE;
  }

  hr = priv->device11->OpenSharedFence (fence_handle,
      IID_PPV_ARGS (&priv->shared_fence11));
  CloseHandle (fence_handle);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create fence");
    return FALSE;
  }

  GstD3DShaderByteCode vs_code;
  if (!gst_d3d_plugin_shader_get_vs_blob (GST_D3D_PLUGIN_VS_COORD,
          GST_D3D_SM_5_0, &vs_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get vs bytecode");
    return FALSE;
  }

  D3D11_INPUT_ELEMENT_DESC input_desc[2] = { };
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

  hr = priv->device11->CreateVertexShader (vs_code.byte_code,
      vs_code.byte_code_len, nullptr, &priv->vs);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create vertex shader");
    return FALSE;
  }

  hr = device11->CreateInputLayout (input_desc, 2, vs_code.byte_code,
      vs_code.byte_code_len, &priv->layout);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create input layout");
    return FALSE;
  }

  GstD3DShaderByteCode ps_code;
  if (!gst_d3d_plugin_shader_get_ps_blob (GST_D3D_PLUGIN_PS_SAMPLE,
          GST_D3D_SM_5_0, &ps_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get ps bytecode");
    return FALSE;
  }
  hr = priv->device11->CreatePixelShader (ps_code.byte_code,
      ps_code.byte_code_len, nullptr, &priv->ps);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create pixel shader");
    return FALSE;
  }

  if (!gst_d3d_plugin_shader_get_ps_blob (GST_D3D_PLUGIN_PS_SAMPLE_SCRGB,
          GST_D3D_SM_5_0, &ps_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get ps bytecode");
    return FALSE;
  }
  hr = priv->device11->CreatePixelShader (ps_code.byte_code,
      ps_code.byte_code_len, nullptr, &priv->ps_scrgb);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create pixel shader");
    return FALSE;
  }

  if (!gst_d3d_plugin_shader_get_ps_blob
      (GST_D3D_PLUGIN_PS_SAMPLE_SCRGB_TONEMAP, GST_D3D_SM_5_0, &ps_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get ps bytecode");
    return FALSE;
  }
  hr = priv->device11->CreatePixelShader (ps_code.byte_code,
      ps_code.byte_code_len, nullptr, &priv->ps_scrgb_tonemap);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create pixel shader");
    return FALSE;
  }

  PSConstBuffer cbuf;
  cbuf.sdr_white_level = (float) priv->sdr_white_level;

  D3D11_BUFFER_DESC buffer_desc = { };
  D3D11_SUBRESOURCE_DATA subresource = { };
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (PSConstBuffer);
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  subresource.pSysMem = &cbuf;
  subresource.SysMemPitch = sizeof (PSConstBuffer);
  hr = priv->device11->CreateBuffer (&buffer_desc, &subresource,
      &priv->const_buf);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create constant buffer");
    return FALSE;
  }

  D3D11_SAMPLER_DESC sampler_desc = { };
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  hr = priv->device11->CreateSamplerState (&sampler_desc, &priv->sampler);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create sampler state");
    return FALSE;
  }

  return TRUE;
}

GstD3D12ScreenCapture *
gst_d3d12_dxgi_capture_new (GstD3D12Device * device, HMONITOR monitor_handle)
{
  GList *iter;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  gst_d3d12_dxgi_capture_load_library ();

  /* Check if we have dup object corresponding to monitor_handle,
   * and if there is already configured capture object, reuse it.
   * This is because of the limitation of desktop duplication API
   * (i.e., in a process, only one duplication object can exist).
   * See also
   * https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput#remarks
   */
  std::lock_guard < std::mutex > lk (g_g_dupl_list_lock);
  for (iter = g_dupl_list; iter; iter = g_list_next (iter)) {
    auto capture = (GstD3D12DxgiCapture *) iter->data;
    auto priv = capture->priv;

    if (priv->monitor_handle == monitor_handle) {
      GST_DEBUG ("Found configured desktop dup object for monitor handle %p",
          monitor_handle);
      gst_object_ref (capture);
      return GST_D3D12_SCREEN_CAPTURE_CAST (capture);
    }
  }

  auto self = (GstD3D12DxgiCapture *) g_object_new (GST_TYPE_D3D12_DXGI_CAPTURE,
      nullptr);
  self->device = (GstD3D12Device *) gst_object_ref (device);
  gst_object_ref_sink (self);

  if (!gst_d3d12_dxgi_capture_open (self, monitor_handle)) {
    gst_object_unref (self);
    return nullptr;
  }

  g_object_weak_ref (G_OBJECT (self),
      (GWeakNotify) gst_d3d12_dxgi_capture_weak_ref_notify, nullptr);
  g_dupl_list = g_list_append (g_dupl_list, self);

  return GST_D3D12_SCREEN_CAPTURE_CAST (self);
}

static GstFlowReturn
gst_d3d12_dxgi_capture_prepare_unlocked (GstD3D12DxgiCapture * self)
{
  auto priv = self->priv;

  if (priv->ctx) {
    GST_DEBUG_OBJECT (self, "Already prepared");
    return GST_FLOW_OK;
  }

  auto ctx = std::make_unique < DesktopDupCtx > ();
  auto ret = ctx->Init (priv->monitor_handle, priv->device11.Get (),
      priv->context11.Get (), priv->shared_fence11.Get (),
      priv->sampler.Get (), priv->ps.Get (), priv->ps_scrgb.Get (),
      priv->ps_scrgb_tonemap.Get (), priv->const_buf.Get (),
      priv->vs.Get (), priv->layout.Get (), priv->prepare_flags ? TRUE : FALSE);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self,
        "Couldn't prepare capturing, %sexpected failure",
        ret == GST_D3D12_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR ? "" : "un");

    return ret;
  }

  ctx->GetSize (&priv->cached_width, &priv->cached_height);
  priv->ctx = std::move (ctx);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_dxgi_capture_prepare (GstD3D12ScreenCapture * capture, guint flags)
{
  auto self = GST_D3D12_DXGI_CAPTURE (capture);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->prepare_flags = flags;
  return gst_d3d12_dxgi_capture_prepare_unlocked (self);
}

static gboolean
gst_d3d12_dxgi_capture_get_size_unlocked (GstD3D12DxgiCapture * self,
    guint * width, guint * height)
{
  auto priv = self->priv;

  *width = 0;
  *height = 0;

  if (priv->ctx)
    priv->ctx->GetSize (&priv->cached_width, &priv->cached_height);

  *width = priv->cached_width;
  *height = priv->cached_height;

  return TRUE;
}

static gboolean
gst_d3d12_dxgi_capture_get_size (GstD3D12ScreenCapture * capture,
    guint * width, guint * height)
{
  auto self = GST_D3D12_DXGI_CAPTURE (capture);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  return gst_d3d12_dxgi_capture_get_size_unlocked (self, width, height);
}

static GstVideoFormat
gst_d3d12_dxgi_capture_get_format (GstD3D12ScreenCapture * capture)
{
  auto self = GST_D3D12_DXGI_CAPTURE (capture);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->ctx)
    return GST_VIDEO_FORMAT_BGRA;

  auto format = priv->ctx->GetFormat ();
  if (format == DXGI_FORMAT_R16G16B16A16_UNORM)
    return GST_VIDEO_FORMAT_RGBA64_LE;

  return GST_VIDEO_FORMAT_BGRA;
}

static gboolean
gst_d3d12_dxgi_capture_draw_mouse (GstD3D12DxgiCapture * self,
    GstBuffer * buffer, const D3D12_BOX * crop_box, gboolean is_hdr)
{
  auto priv = self->priv;
  const auto & info = priv->ctx->GetPointerInfo ();
  HRESULT hr;

  if (!info.position_info.Visible)
    return TRUE;

  if (!info.width_ || !info.height_)
    return TRUE;

  if (static_cast < INT > (info.position_info.Position.x + info.width_) <
      static_cast < INT > (crop_box->left) ||
      static_cast < INT > (info.position_info.Position.x) >
      static_cast < INT > (crop_box->right) ||
      static_cast < INT > (info.position_info.Position.y + info.height_) <
      static_cast < INT > (crop_box->top) ||
      static_cast < INT > (info.position_info.Position.y) >
      static_cast < INT > (crop_box->bottom)) {
    return TRUE;
  }

  if (info.token_ != priv->mouse_token) {
    gst_clear_buffer (&priv->mouse_buf);
    gst_clear_buffer (&priv->mouse_xor_buf);
    priv->mouse_token = info.token_;
  }

  auto device = gst_d3d12_device_get_device_handle (self->device);
  if (!priv->mouse_buf) {
    ComPtr < ID3D12Resource > mouse_texture;
    ComPtr < ID3D12Resource > mouse_xor_texture;

    D3D12_HEAP_PROPERTIES heap_prop =
        CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D (DXGI_FORMAT_B8G8R8A8_UNORM,
        info.width_, info.height_, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

    hr = device->CreateCommittedResource (&heap_prop,
        D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS (&mouse_texture));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create mouse texture");
      return FALSE;
    }

    if (!info.xor_texture_.empty ()) {
      hr = device->CreateCommittedResource (&heap_prop,
          D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON,
          nullptr, IID_PPV_ARGS (&mouse_xor_texture));
      if (!gst_d3d12_result (hr, self->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create mouse texture");
        return FALSE;
      }
    }

    priv->mouse_buf = gst_buffer_new ();
    auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, self->device,
        mouse_texture.Get (), 0, nullptr, nullptr);
    gst_buffer_append_memory (priv->mouse_buf, mem);

    if (mouse_xor_texture) {
      priv->mouse_xor_buf = gst_buffer_new ();
      auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, self->device,
          mouse_xor_texture.Get (), 0, nullptr, nullptr);
      gst_buffer_append_memory (priv->mouse_xor_buf, mem);
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT64 buffer_size;

    device->GetCopyableFootprints (&resource_desc,
        0, 1, 0, &layout, nullptr, nullptr, &buffer_size);

    GstMapInfo map_info;
    gst_buffer_map (priv->mouse_buf, &map_info, GST_MAP_WRITE);
    auto src = info.texture_.data ();
    auto dst = (guint8 *) map_info.data;
    for (UINT i = 0; i < info.height_; i++) {
      memcpy (dst, src, info.width_ * 4);
      src += info.stride_;
      dst += layout.Footprint.RowPitch;
    }
    gst_buffer_unmap (priv->mouse_buf, &map_info);

    if (priv->mouse_xor_buf) {
      gst_buffer_map (priv->mouse_xor_buf, &map_info, GST_MAP_WRITE);
      auto src = info.xor_texture_.data ();
      auto dst = (guint8 *) map_info.data;
      for (UINT i = 0; i < info.height_; i++) {
        memcpy (dst, src, info.width_ * 4);
        src += info.stride_;
        dst += layout.Footprint.RowPitch;
      }
      gst_buffer_unmap (priv->mouse_xor_buf, &map_info);
    }
  }

  GstD3D12FenceData *fence_data = nullptr;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  GstD3D12CmdAlloc *gst_ca = nullptr;
  if (!gst_d3d12_cmd_alloc_pool_acquire (priv->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  hr = ca->Reset ();
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  if (!priv->cl) {
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&priv->cl));
  } else {
    hr = priv->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  auto cl = priv->cl;
  gint ptr_x = info.position_info.Position.x - crop_box->left;
  gint ptr_y = info.position_info.Position.y - crop_box->top;
  gint ptr_w = info.width_;
  gint ptr_h = info.height_;

  auto blend_conv = is_hdr ? priv->mouse_blend_scrgb : priv->mouse_blend;

  g_object_set (blend_conv, "src-x", 0, "src-y", 0, "src-width",
      ptr_w, "src-height", ptr_h, "dest-x", ptr_x, "dest-y", ptr_y,
      "dest-width", ptr_w, "dest-height", ptr_h, nullptr);

  auto cq = gst_d3d12_device_get_cmd_queue (self->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  if (!gst_d3d12_converter_convert_buffer (blend_conv,
          priv->mouse_buf, buffer, fence_data, cl.Get (), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't build mouse blend command");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  if (priv->mouse_xor_buf) {
    blend_conv = is_hdr ? priv->mouse_xor_blend_scrgb : priv->mouse_xor_blend;
    g_object_set (blend_conv, "src-x", 0, "src-y", 0, "src-width",
        ptr_w, "src-height", ptr_h, "dest-x", ptr_x, "dest-y", ptr_y,
        "dest-width", ptr_w, "dest-height", ptr_h, nullptr);

    if (!gst_d3d12_converter_convert_buffer (blend_conv,
            priv->mouse_xor_buf, buffer, fence_data, cl.Get (), FALSE)) {
      GST_ERROR_OBJECT (self, "Couldn't build mouse blend command");
      gst_d3d12_fence_data_unref (fence_data);
      return FALSE;
    }
  }

  hr = cl->Close ();
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  ID3D12CommandList *cmd_list[] = { cl.Get () };

  guint64 fence_val = 0;
  hr = gst_d3d12_cmd_queue_execute_command_lists (cq, 1, cmd_list, &fence_val);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  gst_d3d12_cmd_queue_set_notify (cq, fence_val, fence_data,
      (GDestroyNotify) gst_d3d12_fence_data_unref);
  gst_d3d12_buffer_set_fence (buffer,
      gst_d3d12_cmd_queue_get_fence_handle (cq), fence_val, FALSE);

  return TRUE;
}

GstFlowReturn
gst_d3d12_dxgi_capture_do_capture (GstD3D12DxgiCapture * capture,
    GstBuffer * buffer, const D3D12_BOX * crop_box, gboolean draw_mouse)
{
  auto self = GST_D3D12_DXGI_CAPTURE (capture);
  auto priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  guint width, height;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->ctx) {
    ret = gst_d3d12_dxgi_capture_prepare_unlocked (self);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "We are not prepared");
      return ret;
    }
  }

  gst_d3d12_dxgi_capture_get_size_unlocked (self, &width, &height);
  if (crop_box->left > width || crop_box->right > width ||
      crop_box->top > height || crop_box->bottom > height) {
    GST_INFO_OBJECT (self,
        "Capture area (%u, %u, %u, %u) doesn't fit into screen size %ux%u",
        crop_box->left, crop_box->right, crop_box->top,
        crop_box->bottom, width, height);

    return GST_D3D12_SCREEN_CAPTURE_FLOW_SIZE_CHANGED;
  }

  auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (buffer, 0);
  auto texture = gst_d3d12_memory_get_d3d11_texture (dmem,
      priv->ctx->GetDevice ());
  if (!texture) {
    GST_ERROR_OBJECT (self, "Couldn't get d3d11 texture");
    return GST_FLOW_ERROR;
  }

  D3D11_TEXTURE2D_DESC tex_desc;
  texture->GetDesc (&tex_desc);
  if (tex_desc.Format != priv->ctx->GetFormat ()) {
    GST_INFO_OBJECT (self, "Format mismatch");
    return GST_D3D12_SCREEN_CAPTURE_FLOW_SIZE_CHANGED;
  }

  priv->fence_val++;
  ret = priv->ctx->Execute (texture, (D3D11_BOX *) crop_box, priv->fence_val);
  if (ret != GST_FLOW_OK) {
    priv->fence_val--;
    priv->WaitGPU ();
    priv->ctx = nullptr;
    if (ret == GST_D3D12_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR) {
      GST_WARNING_OBJECT (self, "Couldn't capture frame, but expected failure");
    } else {
      GST_ERROR_OBJECT (self, "Unexpected failure during capture");
    }

    return ret;
  }

  gst_d3d12_memory_set_fence (dmem, priv->shared_fence.Get (),
      priv->fence_val, FALSE);

  GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
  GST_MINI_OBJECT_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  if (draw_mouse && !gst_d3d12_dxgi_capture_draw_mouse (self, buffer, crop_box,
          tex_desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM)) {
    priv->WaitGPU ();
    priv->ctx = nullptr;
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}
