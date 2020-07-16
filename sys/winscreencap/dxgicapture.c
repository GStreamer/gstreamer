/* GStreamer
 * Copyright (C) 2019 OKADA Jun-ichi <okada@abt.jp>
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

/* This code captures the screen using "Desktop Duplication API".
 * For more information
 * https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api */

#include "dxgicapture.h"

#include <d3dcompiler.h>
#include <gmodule.h>

GST_DEBUG_CATEGORY_EXTERN (gst_dxgi_screen_cap_src_debug);
#define GST_CAT_DEFAULT gst_dxgi_screen_cap_src_debug

#define PTR_RELEASE(p) {if(NULL!=(p)){IUnknown_Release((IUnknown *)(p)); (p) = NULL;}}
#define BYTE_PER_PIXEL (4)

/* vertex structures */
typedef struct _vector3d
{
  float x;
  float y;
  float z;
} vector3d;

typedef struct _vector2d
{
  float x;
  float y;
} vector2d;

typedef struct _vertex
{
  vector3d pos;
  vector2d texcoord;
} vertex;
#define VERTEX_NUM (6);

typedef struct _DxgiCapture
{
  GstDXGIScreenCapSrc *src;

  /*Direct3D pointers */
  ID3D11Device *d3d11_device;
  ID3D11DeviceContext *d3d11_context;
  IDXGIOutputDuplication *dxgi_dupl;

  /* Texture that has been rotated and combined fragments. */
  ID3D11Texture2D *work_texture;
  D3D11_TEXTURE2D_DESC work_texture_desc;
  D3D11_VIEWPORT view_port;
  /* Textures that can be read by the CPU.
   * CPU-accessible textures are required separately from work_texture
   * because shaders cannot be executed. */
  ID3D11Texture2D *readable_texture;
  ID3D11VertexShader *vertex_shader;
  ID3D11PixelShader *pixel_shader;
  ID3D11SamplerState *sampler_state;
  ID3D11RenderTargetView *target_view;
  /* Screen output dimensions and rotation status.
   * The texture acquired by AcquireNextFrame has a non-rotated region. */
  DXGI_OUTDUPL_DESC dupl_desc;

  /* mouse pointer image */
  guint8 *pointer_buffer;
  gsize pointer_buffer_capacity;

  /* The movement rectangular regions and the movement
   * destination position from the previous frame. */
  DXGI_OUTDUPL_MOVE_RECT *move_rects;
  gsize move_rects_capacity;

  /* Array of dirty rectangular region for the desktop frame. */
  RECT *dirty_rects;
  gsize dirty_rects_capacity;

  /* Vertex buffer created from array of dirty rectangular region. */
  vertex *dirty_verteces;
  gsize verteces_capacity;

  /* Array of rectangular region to copy to readable_texture. */
  RECT *copy_rects;
  gsize copy_rects_capacity;

  /* latest mouse pointer info */
  DXGI_OUTDUPL_POINTER_SHAPE_INFO pointer_shape_info;
  DXGI_OUTDUPL_POINTER_POSITION last_pointer_position;

} DxgiCapture;

/* Vertex shader for texture rotation by HLSL. */
static const char STR_VERTEX_SHADER[] =
    "struct vs_input  { float4 pos : POSITION; float2 tex : TEXCOORD; }; "
    "struct vs_output { float4 pos : SV_POSITION; float2 tex : TEXCOORD; }; "
    "vs_output vs_main(vs_input input){return input;}";

/* Pixel shader for texture rotation by HLSL. */
static const char STR_PIXEL_SHADER[] =
    "Texture2D tx : register( t0 ); "
    "SamplerState samp : register( s0 ); "
    "struct ps_input { float4 pos : SV_POSITION; float2 tex : TEXCOORD;}; "
    "float4 ps_main(ps_input input) : "
    "SV_Target{ return tx.Sample( samp, input.tex ); }";

/* initial buffer size */
const int INITIAL_POINTER_BUFFER_CAPACITY = 64 * 64 * BYTE_PER_PIXEL;
const int INITIAL_MOVE_RECTS_CAPACITY = 100;
const int INITIAL_DIRTY_RECTS_CAPACITY = 100;
const int INITIAL_VERTICES_CAPACITY = 100 * VERTEX_NUM;
const int INITIAL_COPY_RECTS_CAPACITY = 100;

static D3D_FEATURE_LEVEL feature_levels[] = {
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_10_1,
  D3D_FEATURE_LEVEL_10_0,
  D3D_FEATURE_LEVEL_9_3,
  D3D_FEATURE_LEVEL_9_2,
  D3D_FEATURE_LEVEL_9_1,
};

static D3D11_INPUT_ELEMENT_DESC vertex_layout[] = {
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
      D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA,
      0}
};

static void _draw_pointer (DxgiCapture * self, LPBYTE buffer, LPRECT dst_rect,
    int stride);
static ID3D11Texture2D *_create_texture (DxgiCapture * self,
    enum D3D11_USAGE usage, UINT bindFlags, UINT cpuAccessFlags);

static gboolean _setup_texture (DxgiCapture * self);

static HRESULT _update_work_texture (DxgiCapture * self,
    IDXGIResource * desktop_resource);

static HRESULT _copy_dirty_fragment (DxgiCapture * self,
    ID3D11Texture2D * src_texture, const D3D11_TEXTURE2D_DESC * src_desc,
    guint move_count, guint dirty_count, RECT ** dst_rect);

static void _set_verteces (DxgiCapture * self, vertex * verteces,
    RECT * dest_rect, const D3D11_TEXTURE2D_DESC * dst_desc, RECT * rect,
    const D3D11_TEXTURE2D_DESC * src_desc);

static GModule *d3d_compiler_module = NULL;
static pD3DCompile GstD3DCompileFunc = NULL;

gboolean
gst_dxgicap_shader_init (void)
{
  static volatile gsize _init = 0;
  static const gchar *d3d_compiler_names[] = {
    "d3dcompiler_47.dll",
    "d3dcompiler_46.dll",
    "d3dcompiler_45.dll",
    "d3dcompiler_44.dll",
    "d3dcompiler_43.dll",
  };

  if (g_once_init_enter (&_init)) {
    gint i;
    for (i = 0; i < G_N_ELEMENTS (d3d_compiler_names); i++) {
      d3d_compiler_module =
          g_module_open (d3d_compiler_names[i], G_MODULE_BIND_LAZY);

      if (d3d_compiler_module) {
        GST_INFO ("D3D compiler %s is available", d3d_compiler_names[i]);
        if (!g_module_symbol (d3d_compiler_module, "D3DCompile",
                (gpointer *) & GstD3DCompileFunc)) {
          GST_ERROR ("Cannot load D3DCompile symbol from %s",
              d3d_compiler_names[i]);
          g_module_close (d3d_compiler_module);
          d3d_compiler_module = NULL;
          GstD3DCompileFunc = NULL;
        } else {
          break;
        }
      }
    }

    if (!GstD3DCompileFunc)
      GST_WARNING ("D3D11 compiler library is unavailable");

    g_once_init_leave (&_init, 1);
  }

  return ! !GstD3DCompileFunc;
}

DxgiCapture *
dxgicap_new (HMONITOR monitor, GstDXGIScreenCapSrc * src)
{
  int i, j;
  HRESULT hr;
  IDXGIFactory1 *dxgi_factory1 = NULL;
  IDXGIOutput1 *dxgi_output1 = NULL;
  IDXGIAdapter1 *dxgi_adapter1 = NULL;
  ID3D11InputLayout *vertex_input_layout = NULL;
  ID3DBlob *vertex_shader_blob = NULL;
  ID3DBlob *pixel_shader_blob = NULL;
  D3D11_SAMPLER_DESC sampler_desc;

  DxgiCapture *self = g_new0 (DxgiCapture, 1);
  if (NULL == self) {
    return NULL;
  }

  self->src = src;
  hr = CreateDXGIFactory1 (&IID_IDXGIFactory1, (void **) &dxgi_factory1);
  HR_FAILED_GOTO (hr, CreateDXGIFactory1, new_error);

  dxgi_output1 = NULL;
  for (i = 0;
      IDXGIFactory1_EnumAdapters1 (dxgi_factory1, i,
          &dxgi_adapter1) != DXGI_ERROR_NOT_FOUND; ++i) {
    IDXGIOutput *dxgi_output = NULL;
    D3D_FEATURE_LEVEL feature_level;

    hr = D3D11CreateDevice ((IDXGIAdapter *) dxgi_adapter1,
        D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
        feature_levels, G_N_ELEMENTS (feature_levels),
        D3D11_SDK_VERSION, &self->d3d11_device, &feature_level,
        &self->d3d11_context);
    if (FAILED (hr)) {
      HR_FAILED_INFO (hr, D3D11CreateDevice);
      PTR_RELEASE (dxgi_adapter1);
      continue;
    }

    for (j = 0; IDXGIAdapter1_EnumOutputs (dxgi_adapter1, j, &dxgi_output) !=
        DXGI_ERROR_NOT_FOUND; ++j) {
      DXGI_OUTPUT_DESC output_desc;
      hr = IDXGIOutput_QueryInterface (dxgi_output, &IID_IDXGIOutput1,
          (void **) &dxgi_output1);
      PTR_RELEASE (dxgi_output);
      HR_FAILED_GOTO (hr, IDXGIOutput::QueryInterface, new_error);

      hr = IDXGIOutput1_GetDesc (dxgi_output1, &output_desc);
      HR_FAILED_GOTO (hr, IDXGIOutput1::GetDesc, new_error);

      if (output_desc.Monitor == monitor) {
        GST_DEBUG_OBJECT (src, "found monitor");
        break;
      }

      PTR_RELEASE (dxgi_output1);
      dxgi_output1 = NULL;
    }

    PTR_RELEASE (dxgi_adapter1);

    if (NULL != dxgi_output1) {
      break;
    }

    PTR_RELEASE (self->d3d11_device);
    PTR_RELEASE (self->d3d11_context);
  }

  if (NULL == dxgi_output1) {
    goto new_error;
  }

  PTR_RELEASE (dxgi_factory1);

  hr = IDXGIOutput1_DuplicateOutput (dxgi_output1,
      (IUnknown *) (self->d3d11_device), &self->dxgi_dupl);
  PTR_RELEASE (dxgi_output1);
  HR_FAILED_GOTO (hr, IDXGIOutput1::DuplicateOutput, new_error);

  IDXGIOutputDuplication_GetDesc (self->dxgi_dupl, &self->dupl_desc);
  self->pointer_buffer_capacity = INITIAL_POINTER_BUFFER_CAPACITY;
  self->pointer_buffer = g_malloc (self->pointer_buffer_capacity);
  if (NULL == self->pointer_buffer) {
    goto new_error;
  }

  self->move_rects_capacity = INITIAL_MOVE_RECTS_CAPACITY;
  self->move_rects = g_new0 (DXGI_OUTDUPL_MOVE_RECT, self->move_rects_capacity);
  if (NULL == self->move_rects) {
    goto new_error;
  }

  self->dirty_rects_capacity = INITIAL_DIRTY_RECTS_CAPACITY;
  self->dirty_rects = g_new0 (RECT, self->dirty_rects_capacity);
  if (NULL == self->dirty_rects) {
    goto new_error;
  }

  self->verteces_capacity = INITIAL_VERTICES_CAPACITY;
  self->dirty_verteces = g_new0 (vertex, self->verteces_capacity);
  if (NULL == self->dirty_verteces) {
    goto new_error;
  }

  self->copy_rects_capacity = INITIAL_COPY_RECTS_CAPACITY;
  self->copy_rects = g_new0 (RECT, self->copy_rects_capacity);
  if (NULL == self->copy_rects) {
    goto new_error;
  }

  if (DXGI_MODE_ROTATION_IDENTITY != self->dupl_desc.Rotation) {
    g_assert (GstD3DCompileFunc);

    /* For a rotated display, create a shader. */
    hr = GstD3DCompileFunc (STR_VERTEX_SHADER, sizeof (STR_VERTEX_SHADER),
        NULL, NULL, NULL, "vs_main", "vs_4_0_level_9_1",
        0, 0, &vertex_shader_blob, NULL);
    HR_FAILED_GOTO (hr, D3DCompile, new_error);

    hr = GstD3DCompileFunc (STR_PIXEL_SHADER, sizeof (STR_PIXEL_SHADER),
        NULL, NULL, NULL, "ps_main", "ps_4_0_level_9_1",
        0, 0, &pixel_shader_blob, NULL);
    HR_FAILED_GOTO (hr, D3DCompile, new_error);

    hr = ID3D11Device_CreateVertexShader (self->d3d11_device,
        ID3D10Blob_GetBufferPointer (vertex_shader_blob),
        ID3D10Blob_GetBufferSize (vertex_shader_blob), NULL,
        &self->vertex_shader);
    HR_FAILED_GOTO (hr, ID3D11Device::CreateVertexShader, new_error);

    hr = ID3D11Device_CreateInputLayout (self->d3d11_device, vertex_layout,
        G_N_ELEMENTS (vertex_layout),
        ID3D10Blob_GetBufferPointer (vertex_shader_blob),
        ID3D10Blob_GetBufferSize (vertex_shader_blob), &vertex_input_layout);
    PTR_RELEASE (vertex_shader_blob)
        HR_FAILED_GOTO (hr, ID3D11Device::CreateInputLayout, new_error);

    ID3D11DeviceContext_IASetInputLayout (self->d3d11_context,
        vertex_input_layout);
    PTR_RELEASE (vertex_input_layout);

    hr = ID3D11Device_CreatePixelShader (self->d3d11_device,
        ID3D10Blob_GetBufferPointer (pixel_shader_blob),
        ID3D10Blob_GetBufferSize (pixel_shader_blob), NULL,
        &self->pixel_shader);
    PTR_RELEASE (pixel_shader_blob);
    HR_FAILED_GOTO (hr, ID3D11Device::CreatePixelShader, new_error);

    memset (&sampler_desc, 0, sizeof (sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = ID3D11Device_CreateSamplerState (self->d3d11_device, &sampler_desc,
        &self->sampler_state);
    HR_FAILED_GOTO (hr, ID3D11Device::CreateSamplerState, new_error);
  }

  return self;

new_error:
  PTR_RELEASE (vertex_input_layout);
  PTR_RELEASE (vertex_shader_blob);
  PTR_RELEASE (pixel_shader_blob);

  dxgicap_destory (self);
  return NULL;
}

void
dxgicap_destory (DxgiCapture * self)
{
  if (!self)
    return;
  PTR_RELEASE (self->target_view);
  PTR_RELEASE (self->readable_texture);
  PTR_RELEASE (self->work_texture);
  PTR_RELEASE (self->dxgi_dupl);
  PTR_RELEASE (self->d3d11_context);
  PTR_RELEASE (self->d3d11_device);
  PTR_RELEASE (self->vertex_shader);
  PTR_RELEASE (self->pixel_shader);
  PTR_RELEASE (self->sampler_state);

  g_free (self->pointer_buffer);
  g_free (self->move_rects);
  g_free (self->dirty_rects);
  g_free (self->dirty_verteces);
  g_free (self->copy_rects);

  g_free (self);
}

gboolean
dxgicap_start (DxgiCapture * self)
{
  return _setup_texture (self);
}

void
dxgicap_stop (DxgiCapture * self)
{
  PTR_RELEASE (self->target_view);
  PTR_RELEASE (self->readable_texture);
  PTR_RELEASE (self->work_texture);
}

gboolean
dxgicap_acquire_next_frame (DxgiCapture * self, gboolean show_cursor,
    guint timeout)
{
  gboolean ret = FALSE;
  HRESULT hr;
  GstDXGIScreenCapSrc *src = self->src;

  DXGI_OUTDUPL_FRAME_INFO frame_info;
  IDXGIResource *desktop_resource = NULL;

  /* Get the latest desktop frames. */
  hr = IDXGIOutputDuplication_AcquireNextFrame (self->dxgi_dupl,
      timeout, &frame_info, &desktop_resource);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
    /* In case of DXGI_ERROR_WAIT_TIMEOUT,
     * it has not changed from the last time. */
    GST_LOG_OBJECT (src, "DXGI_ERROR_WAIT_TIMEOUT");
    ret = TRUE;
    goto end;
  }
  HR_FAILED_GOTO (hr, IDXGIOutputDuplication::AcquireNextFrame, end);

  if (0 != frame_info.LastPresentTime.QuadPart) {
    /* The desktop frame has changed since last time. */
    hr = _update_work_texture (self, desktop_resource);
    if (FAILED (hr)) {
      GST_DEBUG_OBJECT (src, "failed to _update_work_texture");
      goto end;
    }
  }

  if (show_cursor && 0 != frame_info.LastMouseUpdateTime.QuadPart) {
    /* The mouse pointer has changed since last time. */
    self->last_pointer_position = frame_info.PointerPosition;

    if (0 < frame_info.PointerShapeBufferSize) {
      /* A valid mouse cursor shape exists. */
      DXGI_OUTDUPL_POINTER_SHAPE_INFO pointer_shape_info;
      guint pointer_shape_size_required;
      /* Get the mouse cursor shape. */
      hr = IDXGIOutputDuplication_GetFramePointerShape (self->dxgi_dupl,
          self->pointer_buffer_capacity,
          self->pointer_buffer,
          &pointer_shape_size_required, &pointer_shape_info);
      if (DXGI_ERROR_MORE_DATA == hr) {
        /* not enough buffers */
        self->pointer_buffer_capacity = pointer_shape_size_required * 2;
        self->pointer_buffer =
            g_realloc (self->pointer_buffer, self->pointer_buffer_capacity);

        hr = IDXGIOutputDuplication_GetFramePointerShape (self->dxgi_dupl,
            self->pointer_buffer_capacity,
            self->pointer_buffer,
            &pointer_shape_size_required, &pointer_shape_info);
      }
      HR_FAILED_GOTO (hr, IDXGIOutputDuplication::GetFramePointerShape, end);
      self->pointer_shape_info = pointer_shape_info;
      ret = TRUE;
    } else {
      ret = TRUE;
    }
  } else {
    ret = TRUE;
  }
end:
  IDXGIOutputDuplication_ReleaseFrame (self->dxgi_dupl);
  PTR_RELEASE (desktop_resource);
  return ret;
}

gboolean
dxgicap_copy_buffer (DxgiCapture * self, gboolean show_cursor, LPRECT dst_rect,
    GstVideoInfo * video_info, GstBuffer * buf)
{
  HRESULT hr;
  int i;
  GstDXGIScreenCapSrc *src = self->src;
  D3D11_MAPPED_SUBRESOURCE readable_map;
  GstVideoFrame vframe;
  gint height = RECT_HEIGHT ((*dst_rect));
  gint width = RECT_WIDTH ((*dst_rect));

  if (NULL == self->readable_texture) {
    GST_DEBUG_OBJECT (src, "readable_texture is null");
    goto flow_error;
  }

  hr = ID3D11DeviceContext_Map (self->d3d11_context,
      (ID3D11Resource *) self->readable_texture, 0,
      D3D11_MAP_READ, 0, &readable_map);
  HR_FAILED_GOTO (hr, IDXGISurface1::Map, flow_error);
  GST_DEBUG_OBJECT (src, "copy size width:%d height:%d", width, height);

  /* Copy from readable_texture to GstVideFrame. */
  if (gst_video_frame_map (&vframe, video_info, buf, GST_MAP_WRITE)) {
    gint line_size;
    gint stride_dst;
    PBYTE frame_buffer;
    PBYTE p_dst;
    PBYTE p_src;

    frame_buffer = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
    p_src = (PBYTE) readable_map.pData +
        (dst_rect->top * readable_map.RowPitch) +
        (dst_rect->left * BYTE_PER_PIXEL);
    p_dst = frame_buffer;

    line_size = width * BYTE_PER_PIXEL;
    stride_dst = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);

    if (line_size > stride_dst) {
      GST_ERROR_OBJECT (src, "not enough stride in video frame");
      ID3D11DeviceContext_Unmap (self->d3d11_context,
          (ID3D11Resource *) self->readable_texture, 0);
      gst_video_frame_unmap (&vframe);
      goto flow_error;
    }

    for (i = 0; i < height; ++i) {
      memcpy (p_dst, p_src, line_size);
      p_dst += stride_dst;
      p_src += readable_map.RowPitch;
    }
    ID3D11DeviceContext_Unmap (self->d3d11_context,
        (ID3D11Resource *) self->readable_texture, 0);
    HR_FAILED_GOTO (hr, IDXGISurface1::Unmap, flow_error);

    if (show_cursor && self->last_pointer_position.Visible) {
      _draw_pointer (self, frame_buffer, dst_rect, stride_dst);
    }
    gst_video_frame_unmap (&vframe);
    return TRUE;
  }

flow_error:
  return FALSE;
}

static void
_draw_pointer (DxgiCapture * self, PBYTE buffer, LPRECT dst_rect, int stride)
{
  RECT pointer_rect;
  RECT clip_pointer_rect;
  int offset_x;
  int offset_y;
  PBYTE p_dst;
  /* For DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME, halve the height. */
  int pointer_height =
      (DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ==
      self->pointer_shape_info.Type)
      ? self->pointer_shape_info.Height / 2 : self->pointer_shape_info.Height;

  /* A rectangular area containing the mouse pointer shape */
  SetRect (&pointer_rect,
      self->last_pointer_position.Position.x,
      self->last_pointer_position.Position.y,
      self->last_pointer_position.Position.x +
      self->pointer_shape_info.Width,
      self->last_pointer_position.Position.y + pointer_height);

  if (!IntersectRect (&clip_pointer_rect, dst_rect, &pointer_rect)) {
    return;
  }

  /* Draw a pointer if it overlaps the destination rectangle range.
   * There are three ways to draw the mouse cursor.
   * see  https://docs.microsoft.com/ja-jp/windows/win32/api/dxgi1_2/ne-dxgi1_2-dxgi_outdupl_pointer_shape_type */
  offset_x = clip_pointer_rect.left - pointer_rect.left;
  offset_y = clip_pointer_rect.top - pointer_rect.top;
  p_dst =
      ((PBYTE) buffer) + ((clip_pointer_rect.top -
          dst_rect->top) * stride) +
      ((clip_pointer_rect.left - dst_rect->left) * BYTE_PER_PIXEL);

  if (DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR ==
      self->pointer_shape_info.Type
      || DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR ==
      self->pointer_shape_info.Type) {
    gboolean mask_mode =
        DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR ==
        self->pointer_shape_info.Type;
    PBYTE p_src =
        (PBYTE) self->pointer_buffer +
        (offset_y * self->pointer_shape_info.Pitch) +
        (offset_x * BYTE_PER_PIXEL);

    int y, x;
    for (y = 0; y < RECT_HEIGHT (clip_pointer_rect); ++y) {
      for (x = 0; x < RECT_WIDTH (clip_pointer_rect); ++x) {
        PBYTE p1 = p_dst + (x * BYTE_PER_PIXEL);
        PBYTE p2 = p_src + (x * BYTE_PER_PIXEL);
        int alpha = *(p2 + 3);
        int i;
        for (i = 0; i < 3; ++i) {
          if (mask_mode) {
            /* case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
             * If the alpha channel of a pixel in the mouse image is 0, copy it.
             * Otherwise, xor each pixel. */
            if (0 == alpha) {
              *p1 = *p2;
            } else {
              *p1 = *p2 ^ *p1;
            }
          } else {
            /* case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
             * Copies the mouse cursor image with alpha channel composition. */
            *p1 = min (255, max (0, *p1 + ((*p2 - *p1) * alpha / 255)));
          }
          ++p1;
          ++p2;
        }
      }
      p_dst += stride;
      p_src += self->pointer_shape_info.Pitch;
    }
  } else if (DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ==
      self->pointer_shape_info.Type) {
    guint mask_bit = 0x80;
    /* AND MASK pointer
     * It is stored in 1 bit per pixel from the beginning. */
    PBYTE p_src_and =
        (PBYTE) self->pointer_buffer +
        (offset_y * self->pointer_shape_info.Pitch);
    /* XOR MASK pointer
     * The XOR MASK is stored after the AND mask. */
    PBYTE p_src_xor =
        (PBYTE) self->pointer_buffer +
        ((offset_y + pointer_height) * self->pointer_shape_info.Pitch);

    int y, x;
    for (y = 0; y < RECT_HEIGHT (clip_pointer_rect); ++y) {
      guint32 *p_dst_32 = ((guint32 *) (p_dst));
      for (x = offset_x; x < RECT_WIDTH (clip_pointer_rect); ++x) {
        int bit_pos = x % 8;
        gboolean and_bit =
            0 != (*(p_src_and + (x / 8)) & (mask_bit >> bit_pos));
        gboolean xor_bit =
            0 != (*(p_src_xor + (x / 8)) & (mask_bit >> bit_pos));

        if (and_bit) {
          if (xor_bit) {
            *p_dst_32 = *p_dst_32 ^ 0x00ffffff;
          }
        } else {
          if (xor_bit) {
            *p_dst_32 = 0xffffffff;
          } else {
            *p_dst_32 = 0xff000000;
          }
        }
        ++p_dst_32;
      }
      p_dst += stride;
      p_src_and += self->pointer_shape_info.Pitch;
      p_src_xor += self->pointer_shape_info.Pitch;
    }
  }
}

static ID3D11Texture2D *
_create_texture (DxgiCapture * self,
    enum D3D11_USAGE usage, UINT bindFlags, UINT cpuAccessFlags)
{
  HRESULT hr;
  GstDXGIScreenCapSrc *src = self->src;
  D3D11_TEXTURE2D_DESC new_desc;
  ID3D11Texture2D *new_texture = NULL;

  ZeroMemory (&new_desc, sizeof (new_desc));
  new_desc.Width = self->dupl_desc.ModeDesc.Width;
  new_desc.Height = self->dupl_desc.ModeDesc.Height;
  new_desc.MipLevels = 1;
  new_desc.ArraySize = 1;
  new_desc.SampleDesc.Count = 1;
  new_desc.SampleDesc.Quality = 0;
  new_desc.Usage = usage;
  new_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  new_desc.BindFlags = bindFlags;
  new_desc.CPUAccessFlags = cpuAccessFlags;
  new_desc.MiscFlags = 0;

  hr = ID3D11Device_CreateTexture2D (self->d3d11_device, &new_desc, NULL,
      &new_texture);
  HR_FAILED_RET (hr, ID3D11Device::CreateTexture2D, NULL);

  return new_texture;
}

static gboolean
_setup_texture (DxgiCapture * self)
{
  HRESULT hr;
  ID3D11Texture2D *new_texture = NULL;
  GstDXGIScreenCapSrc *src = self->src;

  if (NULL == self->readable_texture) {
    new_texture = _create_texture (self, D3D11_USAGE_STAGING, 0,
        D3D11_CPU_ACCESS_READ);
    if (NULL == new_texture) {
      return FALSE;
    }
    self->readable_texture = new_texture;
  }

  if (DXGI_MODE_ROTATION_IDENTITY != self->dupl_desc.Rotation) {
    /* For rotated displays, create work_texture. */
    if (NULL == self->work_texture) {
      new_texture =
          _create_texture (self, D3D11_USAGE_DEFAULT,
          D3D11_BIND_RENDER_TARGET, 0);
      if (NULL == new_texture) {
        return FALSE;
      }

      self->work_texture = new_texture;
      ID3D11Texture2D_GetDesc (self->work_texture, &self->work_texture_desc);
      hr = ID3D11Device_CreateRenderTargetView (self->d3d11_device,
          (ID3D11Resource *) self->work_texture, NULL, &self->target_view);
      HR_FAILED_RET (hr, ID3D11Device::CreateRenderTargetView, FALSE);

      self->view_port.Width = (float) self->work_texture_desc.Width;
      self->view_port.Height = (float) self->work_texture_desc.Height;
      self->view_port.MinDepth = 0.0f;
      self->view_port.MaxDepth = 1.0f;
      self->view_port.TopLeftX = 0.0f;
      self->view_port.TopLeftY = 0.0f;
    }
  }

  return TRUE;
}

/* Update work_texture to the latest desktop frame from the update information
 * that can be obtained from IDXGIOutputDuplication.
 * Then copy to readable_texture.
 */
static HRESULT
_update_work_texture (DxgiCapture * self, IDXGIResource * desktop_resource)
{
  HRESULT hr = S_OK;
  GstDXGIScreenCapSrc *src = self->src;
  int i;
  ID3D11Texture2D *desktop_texture = NULL;
  guint required_size;
  guint move_count;
  guint dirty_rects_capacity_size;
  guint dirty_count;
  guint copy_count;
  D3D11_TEXTURE2D_DESC src_desc;
  RECT *dst_rect;
  ID3D11Texture2D *work_src;
  guint move_rects_capacity_size =
      sizeof (DXGI_OUTDUPL_MOVE_RECT) * self->move_rects_capacity;

  hr = IDXGIResource_QueryInterface (desktop_resource, &IID_ID3D11Texture2D,
      (void **) &desktop_texture);
  HR_FAILED_GOTO (hr, IDXGIResource::QueryInterface, end);

  /* Get the rectangular regions that was moved from the last time.
   * However, I have never obtained a valid value in GetFrameMoveRects.
   * It seems to depend on the implementation of the GPU driver.
   * see https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-getframemoverects
   */
  hr = IDXGIOutputDuplication_GetFrameMoveRects (self->dxgi_dupl,
      move_rects_capacity_size, self->move_rects, &required_size);
  if (DXGI_ERROR_MORE_DATA == hr) {
    /* not enough buffers */
    self->move_rects_capacity =
        (required_size / sizeof (DXGI_OUTDUPL_MOVE_RECT)) * 2;
    self->move_rects =
        g_renew (DXGI_OUTDUPL_MOVE_RECT, self->move_rects,
        self->move_rects_capacity);

    hr = IDXGIOutputDuplication_GetFrameMoveRects (self->dxgi_dupl,
        required_size, self->move_rects, &required_size);
  }
  HR_FAILED_GOTO (hr, IDXGIOutputDuplication::GetFrameMoveRects, end);
  move_count = required_size / sizeof (DXGI_OUTDUPL_MOVE_RECT);

  dirty_rects_capacity_size = sizeof (RECT) * self->dirty_rects_capacity;
  /* Gets the rectangular regions that has changed since the last time.
     see https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-getframedirtyrects
   */
  hr = IDXGIOutputDuplication_GetFrameDirtyRects (self->dxgi_dupl,
      dirty_rects_capacity_size, self->dirty_rects, &required_size);

  if (DXGI_ERROR_MORE_DATA == hr) {
    /* not enough buffers */
    self->dirty_rects_capacity = (required_size / sizeof (RECT)) * 2;
    self->dirty_rects =
        g_renew (RECT, self->dirty_rects, self->dirty_rects_capacity);

    hr = IDXGIOutputDuplication_GetFrameDirtyRects (self->dxgi_dupl,
        required_size, self->dirty_rects, &required_size);
  }
  HR_FAILED_GOTO (hr, IDXGIOutputDuplication::GetFrameDirtyRects, end);

  dirty_count = required_size / sizeof (RECT);

  /* The number of rectangular regions to copy to the readable_texture. */
  copy_count = move_count + dirty_count;

  if (self->copy_rects_capacity < copy_count) {
    /* not enough buffers */
    self->copy_rects_capacity = copy_count * 2;
    self->copy_rects =
        g_renew (RECT, self->copy_rects, self->copy_rects_capacity);
  }

  if (DXGI_MODE_ROTATION_IDENTITY == self->dupl_desc.Rotation) {
    /* For a non-rotating display, copy it directly into readable_texture. */
    RECT *p = self->copy_rects;
    for (i = 0; i < move_count; ++i) {
      *p = self->move_rects[i].DestinationRect;
      ++p;
    }
    for (i = 0; i < dirty_count; ++i) {
      *p = self->dirty_rects[i];
      ++p;
    }
    work_src = desktop_texture;
  } else {
    /* For rotated displays, rotate to work_texture and copy. */
    ID3D11Texture2D_GetDesc (desktop_texture, &src_desc);
    dst_rect = self->copy_rects;
    /* Copy the dirty rectangular and moved rectangular regions from desktop frame to work_texture. */
    hr = _copy_dirty_fragment (self, desktop_texture, &src_desc, move_count,
        dirty_count, &dst_rect);
    work_src = self->work_texture;
    if (FAILED (hr)) {
      goto end;
    }
  }

  /* Copy the updated rectangular regions to readable_texture. */
  for (i = 0; i < copy_count; ++i) {
    RECT *p = (self->copy_rects + i);
    D3D11_BOX box;
    box.left = p->left;
    box.top = p->top;
    box.front = 0;
    box.right = p->right;
    box.bottom = p->bottom;
    box.back = 1;

    ID3D11DeviceContext_CopySubresourceRegion (self->d3d11_context,
        (ID3D11Resource *) self->readable_texture,
        0, p->left, p->top, 0, (ID3D11Resource *) work_src, 0, &box);
  }

end:
  PTR_RELEASE (desktop_texture);
  return hr;
}

static void
_rotate_rect (DXGI_MODE_ROTATION rotation, RECT * dst, const RECT * src,
    gint dst_width, gint dst_height)
{
  switch (rotation) {
    case DXGI_MODE_ROTATION_ROTATE90:
      dst->left = dst_width - src->bottom;
      dst->top = src->left;
      dst->right = dst_width - src->top;
      dst->bottom = src->right;
      break;
    case DXGI_MODE_ROTATION_ROTATE180:
      dst->left = dst_width - src->right;
      dst->top = dst_height - src->bottom;
      dst->right = dst_width - src->left;
      dst->bottom = dst_height - src->top;
      break;
    case DXGI_MODE_ROTATION_ROTATE270:
      dst->left = src->top;
      dst->top = dst_height - src->right;
      dst->right = src->bottom;
      dst->bottom = dst_height - src->left;
      break;
    default:
      *dst = *src;
      break;
  }
}

/* Copy the rectangular area specified by dirty_rects and move_rects from src_texture to work_texture. */
static HRESULT
_copy_dirty_fragment (DxgiCapture * self, ID3D11Texture2D * src_texture,
    const D3D11_TEXTURE2D_DESC * src_desc, guint move_count, guint dirty_count,
    RECT ** dst_rect)
{
  HRESULT hr = S_OK;
  GstDXGIScreenCapSrc *src = self->src;
  int i;
  RECT *dst_rect_p;
  vertex *vp;
  UINT stride;
  UINT offset;
  guint verteces_count;
  ID3D11Buffer *verteces_buffer = NULL;
  ID3D11ShaderResourceView *shader_resource = NULL;
  D3D11_SUBRESOURCE_DATA subresource_data;
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_SHADER_RESOURCE_VIEW_DESC shader_desc;

  shader_desc.Format = src_desc->Format;
  shader_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  shader_desc.Texture2D.MostDetailedMip = src_desc->MipLevels - 1;
  shader_desc.Texture2D.MipLevels = src_desc->MipLevels;
  hr = ID3D11Device_CreateShaderResourceView (self->d3d11_device,
      (ID3D11Resource *) src_texture, &shader_desc, &shader_resource);
  HR_FAILED_GOTO (hr, ID3D11Device::CreateShaderResourceView, end);

  ID3D11DeviceContext_OMSetRenderTargets (self->d3d11_context, 1,
      &self->target_view, NULL);

  ID3D11DeviceContext_VSSetShader (self->d3d11_context, self->vertex_shader,
      NULL, 0);

  ID3D11DeviceContext_PSSetShader (self->d3d11_context, self->pixel_shader,
      NULL, 0);

  ID3D11DeviceContext_PSSetShaderResources (self->d3d11_context, 0, 1,
      &shader_resource);

  ID3D11DeviceContext_PSSetSamplers (self->d3d11_context, 0, 1,
      &self->sampler_state);

  ID3D11DeviceContext_IASetPrimitiveTopology (self->d3d11_context,
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  verteces_count = (move_count + dirty_count) * VERTEX_NUM;
  if (verteces_count > self->verteces_capacity) {
    /* not enough buffers */
    self->verteces_capacity = verteces_count * 2;
    self->dirty_verteces =
        g_renew (vertex, self->dirty_verteces, self->verteces_capacity);
    if (NULL == self->dirty_verteces) {
      hr = S_FALSE;
      goto end;
    }
  }

  dst_rect_p = *dst_rect;
  vp = self->dirty_verteces;
  /* Create a vertex buffer to move and rotate from the move_rects.
   * And set the rectangular region to be copied to readable_texture. */
  for (i = 0; i < move_count; ++i) {
    /* Copy the area to be moved.
     * The source of the move is included in dirty_rects. */
    _set_verteces (self, vp, dst_rect_p, &self->work_texture_desc,
        &(self->move_rects[i].DestinationRect), src_desc);
    vp += VERTEX_NUM;
    ++dst_rect_p;
  }
  /* Create a vertex buffer to move and rotate from the dirty_rects.
   * And set the rectangular region to be copied to readable_texture. */
  for (i = 0; i < dirty_count; ++i) {
    _set_verteces (self, vp, dst_rect_p, &self->work_texture_desc,
        &(self->dirty_rects[i]), src_desc);
    vp += VERTEX_NUM;
    ++dst_rect_p;
  }
  *dst_rect = dst_rect_p;

  memset (&buffer_desc, 0, sizeof (buffer_desc));
  buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
  buffer_desc.ByteWidth = verteces_count * sizeof (vertex);
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = 0;

  memset (&subresource_data, 0, sizeof (subresource_data));
  subresource_data.pSysMem = self->dirty_verteces;

  hr = ID3D11Device_CreateBuffer (self->d3d11_device, &buffer_desc,
      &subresource_data, &verteces_buffer);
  HR_FAILED_GOTO (hr, ID3D11Device::CreateBuffer, end);

  stride = sizeof (vertex);
  offset = 0;
  ID3D11DeviceContext_IASetVertexBuffers (self->d3d11_context, 0, 1,
      &verteces_buffer, &stride, &offset);

  ID3D11DeviceContext_RSSetViewports (self->d3d11_context, 1, &self->view_port);

  /* Copy the rectangular region indicated by dirty_rects from the desktop frame to work_texture. */
  ID3D11DeviceContext_Draw (self->d3d11_context, verteces_count, 0);

end:
  PTR_RELEASE (verteces_buffer);
  PTR_RELEASE (shader_resource);

  return hr;
}

static void
_set_verteces (DxgiCapture * self, vertex * verteces, RECT * dst_rect,
    const D3D11_TEXTURE2D_DESC * dst_desc, RECT * rect,
    const D3D11_TEXTURE2D_DESC * src_desc)
{
  int center_x;
  int center_y;

  /* Rectangular area is moved according to the rotation of the display. */
  _rotate_rect (self->dupl_desc.Rotation, dst_rect, rect, dst_desc->Width,
      dst_desc->Height);

  /* Set the vertex buffer from the rotation of the display. */
  switch (self->dupl_desc.Rotation) {
    case DXGI_MODE_ROTATION_ROTATE90:
    verteces[0].texcoord = (vector2d) {
    (float) rect->right / (float) src_desc->Width,
          (float) rect->bottom / (float) src_desc->Height};
      verteces[1].texcoord = (vector2d) {
      (float) rect->left / (float) src_desc->Width,
            (float) rect->bottom / (float) src_desc->Height};
      verteces[2].texcoord = (vector2d) {
      (float) rect->right / (float) src_desc->Width,
            (float) rect->top / (float) src_desc->Height};
      verteces[5].texcoord = (vector2d) {
      (float) rect->left / (float) src_desc->Width,
            (float) rect->top / (float) src_desc->Height};
      break;
    case DXGI_MODE_ROTATION_ROTATE180:
    verteces[0].texcoord = (vector2d) {
    (float) rect->right / (float) src_desc->Width,
          (float) rect->top / (float) src_desc->Height};
      verteces[1].texcoord = (vector2d) {
      (float) rect->right / (float) src_desc->Width,
            (float) rect->bottom / (float) src_desc->Height};
      verteces[2].texcoord = (vector2d) {
      (float) rect->left / (float) src_desc->Width,
            (float) rect->top / (float) src_desc->Height};
      verteces[5].texcoord = (vector2d) {
      (float) rect->left / (float) src_desc->Width,
            (float) rect->bottom / (float) src_desc->Height};
      break;
    case DXGI_MODE_ROTATION_ROTATE270:
    verteces[0].texcoord = (vector2d) {
    (float) rect->left / (float) src_desc->Width,
          (float) rect->top / (float) src_desc->Height};
      verteces[1].texcoord = (vector2d) {
      (float) rect->right / (float) src_desc->Width,
            (float) rect->top / (float) src_desc->Height};
      verteces[2].texcoord = (vector2d) {
      (float) rect->left / (float) src_desc->Width,
            (float) rect->bottom / (float) src_desc->Height};
      verteces[5].texcoord = (vector2d) {
      (float) rect->right / (float) src_desc->Width,
            (float) rect->bottom / (float) src_desc->Height};
      break;
    default:
    verteces[0].texcoord = (vector2d) {
    (float) rect->left / (float) src_desc->Width,
          (float) rect->bottom / (float) src_desc->Height};
      verteces[1].texcoord = (vector2d) {
      (float) rect->left / (float) src_desc->Width,
            (float) rect->top / (float) src_desc->Height};
      verteces[2].texcoord = (vector2d) {
      (float) rect->right / (float) src_desc->Width,
            (float) rect->bottom / (float) src_desc->Height};
      verteces[5].texcoord = (vector2d) {
      (float) rect->right / (float) src_desc->Width,
            (float) rect->top / (float) src_desc->Height};
      break;
  }
  verteces[3].texcoord = verteces[2].texcoord;
  verteces[4].texcoord = verteces[1].texcoord;

  center_x = (int) dst_desc->Width / 2;
  center_y = (int) dst_desc->Height / 2;

  verteces[0].pos = (vector3d) {
  (float) (dst_rect->left - center_x) / (float) center_x,
        (float) (dst_rect->bottom - center_y) / (float) center_y *-1.0f, 0.0f};
  verteces[1].pos = (vector3d) {
  (float) (dst_rect->left - center_x) / (float) center_x,
        (float) (dst_rect->top - center_y) / (float) center_y *-1.0f, 0.0f};
  verteces[2].pos = (vector3d) {
  (float) (dst_rect->right - center_x) / (float) center_x,
        (float) (dst_rect->bottom - center_y) / (float) center_y *-1.0f, 0.0f};
  verteces[3].pos = verteces[2].pos;
  verteces[4].pos = verteces[1].pos;
  verteces[5].pos = (vector3d) {
  (float) (dst_rect->right - center_x) / (float) center_x,
        (float) (dst_rect->top - center_y) / (float) center_y *-1.0f, 0.0f};
}

typedef struct _monitor_param_by_name
{
  const gchar *device_name;
  HMONITOR hmonitor;
} monitor_param_by_name;

static BOOL CALLBACK
monitor_enum_proc_by_name (HMONITOR hmonitor, HDC hdc, LPRECT rect,
    LPARAM lparam)
{
  MONITORINFOEXA monitor_info;
  monitor_param_by_name *param = (monitor_param_by_name *) lparam;

  monitor_info.cbSize = sizeof (monitor_info);
  if (GetMonitorInfoA (hmonitor, (MONITORINFO *) & monitor_info)) {
    if (0 == g_strcmp0 (monitor_info.szDevice, param->device_name)) {
      param->hmonitor = hmonitor;
      return FALSE;
    }
  }
  return TRUE;
}

HMONITOR
get_hmonitor_by_device_name (const gchar * device_name)
{
  monitor_param_by_name monitor = { device_name, NULL, };
  EnumDisplayMonitors (NULL, NULL, monitor_enum_proc_by_name,
      (LPARAM) & monitor);
  return monitor.hmonitor;
}

static BOOL CALLBACK
monitor_enum_proc_primary (HMONITOR hmonitor, HDC hdc, LPRECT rect,
    LPARAM lparam)
{
  MONITORINFOEXA monitor_info;
  monitor_param_by_name *param = (monitor_param_by_name *) lparam;

  monitor_info.cbSize = sizeof (monitor_info);
  if (GetMonitorInfoA (hmonitor, (MONITORINFO *) & monitor_info)) {
    if (MONITORINFOF_PRIMARY == monitor_info.dwFlags) {
      param->hmonitor = hmonitor;
      return FALSE;
    }
  }
  return TRUE;
}

HMONITOR
get_hmonitor_primary (void)
{
  monitor_param_by_name monitor = { NULL, NULL, };
  EnumDisplayMonitors (NULL, NULL, monitor_enum_proc_primary,
      (LPARAM) & monitor);
  return monitor.hmonitor;
}

typedef struct _monitor_param_by_index
{
  int target;
  int counter;
  HMONITOR hmonitor;
} monitor_param_by_index;

static BOOL CALLBACK
monitor_enum_proc_by_index (HMONITOR hmonitor, HDC hdc, LPRECT rect,
    LPARAM lparam)
{
  MONITORINFOEXA monitor_info;
  monitor_param_by_index *param = (monitor_param_by_index *) lparam;

  monitor_info.cbSize = sizeof (monitor_info);
  if (GetMonitorInfoA (hmonitor, (MONITORINFO *) & monitor_info)) {
    if (param->target == param->counter) {
      param->hmonitor = hmonitor;
      return FALSE;
    }
  }
  ++param->counter;
  return TRUE;
}

HMONITOR
get_hmonitor_by_index (int index)
{
  monitor_param_by_index monitor = { index, 0, NULL, };
  EnumDisplayMonitors (NULL, NULL, monitor_enum_proc_by_index,
      (LPARAM) & monitor);
  return monitor.hmonitor;
}


gboolean
get_monitor_physical_size (HMONITOR hmonitor, LPRECT rect)
{
  MONITORINFOEXW monitor_info;
  DEVMODEW dev_mode;

  monitor_info.cbSize = sizeof (monitor_info);
  if (!GetMonitorInfoW (hmonitor, (LPMONITORINFO) & monitor_info)) {
    return FALSE;
  }

  dev_mode.dmSize = sizeof (dev_mode);
  dev_mode.dmDriverExtra = sizeof (POINTL);
  dev_mode.dmFields = DM_POSITION;
  if (!EnumDisplaySettingsW
      (monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &dev_mode)) {
    return FALSE;
  }

  SetRect (rect, 0, 0, dev_mode.dmPelsWidth, dev_mode.dmPelsHeight);
  return TRUE;
}

static const gchar *
_hresult_to_string_fallback (HRESULT hr)
{
  const gchar *s = "unknown error";

  switch (hr) {
    case DXGI_ERROR_ACCESS_DENIED:
      s = "DXGI_ERROR_ACCESS_DENIED";
      break;
    case DXGI_ERROR_ACCESS_LOST:
      s = "DXGI_ERROR_ACCESS_LOST";
      break;
    case DXGI_ERROR_CANNOT_PROTECT_CONTENT:
      s = "DXGI_ERROR_CANNOT_PROTECT_CONTENT";
      break;
    case DXGI_ERROR_DEVICE_HUNG:
      s = "DXGI_ERROR_DEVICE_HUNG";
      break;
    case DXGI_ERROR_DEVICE_REMOVED:
      s = "DXGI_ERROR_DEVICE_REMOVED";
      break;
    case DXGI_ERROR_DEVICE_RESET:
      s = "DXGI_ERROR_DEVICE_RESET";
      break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
      s = "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
      break;
    case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:
      s = "DXGI_ERROR_FRAME_STATISTICS_DISJOINT";
      break;
    case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:
      s = "DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE";
      break;
    case DXGI_ERROR_INVALID_CALL:
      s = "DXGI_ERROR_INVALID_CALL";
      break;
    case DXGI_ERROR_MORE_DATA:
      s = "DXGI_ERROR_MORE_DATA";
      break;
    case DXGI_ERROR_NAME_ALREADY_EXISTS:
      s = "DXGI_ERROR_NAME_ALREADY_EXISTS";
      break;
    case DXGI_ERROR_NONEXCLUSIVE:
      s = "DXGI_ERROR_NONEXCLUSIVE";
      break;
    case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
      s = "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";
      break;
    case DXGI_ERROR_NOT_FOUND:
      s = "DXGI_ERROR_NOT_FOUND";
      break;
    case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:
      s = "DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";
      break;
    case DXGI_ERROR_REMOTE_OUTOFMEMORY:
      s = "DXGI_ERROR_REMOTE_OUTOFMEMORY";
      break;
    case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:
      s = "DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";
      break;
    case DXGI_ERROR_SDK_COMPONENT_MISSING:
      s = "DXGI_ERROR_SDK_COMPONENT_MISSING";
      break;
    case DXGI_ERROR_SESSION_DISCONNECTED:
      s = "DXGI_ERROR_SESSION_DISCONNECTED";
      break;
    case DXGI_ERROR_UNSUPPORTED:
      s = "DXGI_ERROR_UNSUPPORTED";
      break;
    case DXGI_ERROR_WAIT_TIMEOUT:
      s = "DXGI_ERROR_WAIT_TIMEOUT";
      break;
    case DXGI_ERROR_WAS_STILL_DRAWING:
      s = "DXGI_ERROR_WAS_STILL_DRAWING";
      break;
    case E_FAIL:
      s = "E_FAIL";
      break;
    case E_OUTOFMEMORY:
      s = "E_OUTOFMEMORY";
      break;
    case E_NOTIMPL:
      s = "E_NOTIMPL";
      break;
    case E_ACCESSDENIED:
      s = "E_ACCESSDENIED";
      break;
    case E_POINTER:
      s = "E_POINTER";
      break;
    case E_INVALIDARG:
      s = "E_INVALIDARG";
      break;
#if defined(_MSC_VER) && (_MSC_VER >= 1800)
    case DXGI_ERROR_ALREADY_EXISTS:
      s = "DXGI_ERROR_ALREADY_EXISTS";
      break;
    case D3D11_ERROR_FILE_NOT_FOUND:
      s = "D3D11_ERROR_FILE_NOT_FOUND";
      break;
    case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
      s = "D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS";
      break;
    case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
      s = "D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS";
      break;
    case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD:
      s = "D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD";
      break;
#endif
  }
  return s;
}

gchar *
get_hresult_to_string (HRESULT hr)
{
  gchar *error_text = NULL;

  error_text = g_win32_error_message ((gint) hr);
  /* g_win32_error_message() doesn't cover all HERESULT return code,
   * so it could be empty string, or null if there was an error
   * in g_utf16_to_utf8() */
  if (!error_text || strlen (error_text) == 0) {
    g_free (error_text);
    error_text = g_strdup (_hresult_to_string_fallback (hr));
  }

  return error_text;
}
