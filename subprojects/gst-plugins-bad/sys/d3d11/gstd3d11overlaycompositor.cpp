/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
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
#  include <config.h>
#endif

#include "gstd3d11overlaycompositor.h"
#include "gstd3d11pluginutils.h"
#include <wrl.h>
#include <memory>
#include <vector>
#include <algorithm>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_overlay_compositor_debug);
#define GST_CAT_DEFAULT gst_d3d11_overlay_compositor_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

typedef struct
{
  struct {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct {
    FLOAT u;
    FLOAT v;
  } texture;
} VertexData;

static const gchar templ_pixel_shader[] =
    "Texture2D shaderTexture;\n"
    "SamplerState samplerState;\n"
    "struct PS_INPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "float4 main(PS_INPUT input): SV_TARGET\n"
    "{\n"
    "  return shaderTexture.Sample(samplerState, input.Texture);\n"
    "}\n";

static const gchar templ_premul_pixel_shader[] =
    "Texture2D shaderTexture;\n"
    "SamplerState samplerState;\n"
    "struct PS_INPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "float4 main(PS_INPUT input): SV_TARGET\n"
    "{\n"
    "  float4 sample = shaderTexture.Sample(samplerState, input.Texture);\n"
    "  float4 unpremul_sample;\n"
    "  if (sample.a == 0 || sample.a == 1)\n"
    "    return sample;\n"
    "  unpremul_sample.r = saturate (sample.r / sample.a);\n"
    "  unpremul_sample.g = saturate (sample.g / sample.a);\n"
    "  unpremul_sample.b = saturate (sample.b / sample.a);\n"
    "  unpremul_sample.a = sample.a;\n"
    "  return unpremul_sample;\n"
    "}\n";

static const gchar templ_vertex_shader[] =
    "struct VS_INPUT\n"
    "{\n"
    "  float4 Position : POSITION;\n"
    "  float2 Texture : TEXCOORD;\n"
    "};\n"
    "\n"
    "struct VS_OUTPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "\n"
    "VS_OUTPUT main(VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}\n";

struct GstD3D11CompositionOverlay
{
  ~GstD3D11CompositionOverlay ()
  {
    if (overlay_rect)
      gst_video_overlay_rectangle_unref (overlay_rect);
    if (d3d11_buffer)
      gst_buffer_unref (d3d11_buffer);
  }

  GstVideoOverlayRectangle *overlay_rect = nullptr;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11Buffer> vertex_buffer;
  gboolean premul_alpha = FALSE;
  GstBuffer *d3d11_buffer = nullptr;
};

typedef std::shared_ptr<GstD3D11CompositionOverlay> GstD3D11CompositionOverlayPtr;

struct _GstD3D11OverlayCompositorPrivate
{
  GstVideoInfo info;

  D3D11_VIEWPORT viewport;

  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11PixelShader> premul_ps;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11Buffer> index_buffer;

  std::vector<GstD3D11CompositionOverlayPtr> overlays;
};
/* *INDENT-ON* */

static void gst_d3d11_overlay_compositor_finalize (GObject * object);

#define gst_d3d11_overlay_compositor_parent_class parent_class
G_DEFINE_TYPE (GstD3D11OverlayCompositor,
    gst_d3d11_overlay_compositor, GST_TYPE_OBJECT);

static void
gst_d3d11_overlay_compositor_class_init (GstD3D11OverlayCompositorClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d11_overlay_compositor_finalize;
}

static void
gst_d3d11_overlay_compositor_init (GstD3D11OverlayCompositor * self)
{
  self->priv = new GstD3D11OverlayCompositorPrivate ();
}

static void
gst_d3d11_overlay_compositor_finalize (GObject * object)
{
  GstD3D11OverlayCompositor *self = GST_D3D11_OVERLAY_COMPOSITOR (object);

  delete self->priv;

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstD3D11CompositionOverlayPtr
gst_d3d11_composition_overlay_new (GstD3D11OverlayCompositor * self,
    GstVideoOverlayRectangle * overlay_rect)
{
  GstD3D11OverlayCompositorPrivate *priv = self->priv;
  gint x, y;
  guint width, height;
  D3D11_SUBRESOURCE_DATA subresource_data;
  D3D11_TEXTURE2D_DESC texture_desc;
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  GstBuffer *buf;
  GstVideoMeta *vmeta;
  GstMapInfo info;
  guint8 *data;
  gint stride;
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  GstD3D11Device *device = self->device;
  FLOAT x1, y1, x2, y2;
  gdouble val;
  ComPtr < ID3D11Texture2D > texture;
  ComPtr < ID3D11ShaderResourceView > srv;
  ComPtr < ID3D11Buffer > vertex_buffer;
  GstVideoOverlayFormatFlags flags;
  gboolean premul_alpha = FALSE;
  GstMemory *mem;
  gboolean is_d3d11 = FALSE;

  memset (&subresource_data, 0, sizeof (subresource_data));
  memset (&texture_desc, 0, sizeof (texture_desc));
  memset (&srv_desc, 0, sizeof (srv_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  if (!gst_video_overlay_rectangle_get_render_rectangle (overlay_rect, &x, &y,
          &width, &height)) {
    GST_ERROR_OBJECT (self, "Failed to get render rectangle");
    return nullptr;
  }

  flags = gst_video_overlay_rectangle_get_flags (overlay_rect);
  if ((flags & GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA) != 0) {
    premul_alpha = TRUE;
    flags = GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA;
  } else {
    flags = GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE;
  }

  buf = gst_video_overlay_rectangle_get_pixels_unscaled_argb (overlay_rect,
      flags);
  if (!buf) {
    GST_ERROR_OBJECT (self, "Failed to get overlay buffer");
    return nullptr;
  }

  mem = gst_buffer_peek_memory (buf, 0);
  if (gst_is_d3d11_memory (mem)) {
    GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
    if (dmem->device == self->device) {
      srv = gst_d3d11_memory_get_shader_resource_view (dmem, 0);
      if (srv) {
        texture = (ID3D11Texture2D *)
            gst_d3d11_memory_get_resource_handle (dmem);
        is_d3d11 = TRUE;
      }
    }
  }

  if (!is_d3d11) {
    vmeta = gst_buffer_get_video_meta (buf);
    if (!vmeta) {
      GST_ERROR_OBJECT (self, "Failed to get video meta");
      return nullptr;
    }

    if (!gst_video_meta_map (vmeta,
            0, &info, (gpointer *) & data, &stride, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Failed to map");
      return nullptr;
    }

    /* Do create texture and upload data at once, for create immutable texture */
    subresource_data.pSysMem = data;
    subresource_data.SysMemPitch = stride;
    subresource_data.SysMemSlicePitch = 0;

    texture_desc.Width = vmeta->width;
    texture_desc.Height = vmeta->height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;

    hr = device_handle->CreateTexture2D (&texture_desc,
        &subresource_data, &texture);
    gst_video_meta_unmap (vmeta, 0, &info);

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Failed to create texture");
      return nullptr;
    }

    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device_handle->CreateShaderResourceView (texture.Get (), &srv_desc,
        &srv);
    if (!gst_d3d11_result (hr, device) || !srv) {
      GST_ERROR_OBJECT (self, "Failed to create shader resource view");
      return nullptr;
    }
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &vertex_buffer);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create vertex buffer, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  GstD3D11DeviceLockGuard lk (device);
  hr = context_handle->Map (vertex_buffer.Get (),
      0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  vertex_data = (VertexData *) map.pData;
  /* bottom left */
  gst_util_fraction_to_double (x, GST_VIDEO_INFO_WIDTH (&priv->info), &val);
  x1 = (val * 2.0f) - 1.0f;

  gst_util_fraction_to_double (y + height,
      GST_VIDEO_INFO_HEIGHT (&priv->info), &val);
  y1 = (val * -2.0f) + 1.0f;

  /* top right */
  gst_util_fraction_to_double (x + width,
      GST_VIDEO_INFO_WIDTH (&priv->info), &val);
  x2 = (val * 2.0f) - 1.0f;

  gst_util_fraction_to_double (y, GST_VIDEO_INFO_HEIGHT (&priv->info), &val);
  y2 = (val * -2.0f) + 1.0f;

  /* bottom left */
  vertex_data[0].position.x = x1;
  vertex_data[0].position.y = y1;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.u = 0.0f;
  vertex_data[0].texture.v = 1.0f;

  /* top left */
  vertex_data[1].position.x = x1;
  vertex_data[1].position.y = y2;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.u = 0.0f;
  vertex_data[1].texture.v = 0.0f;

  /* top right */
  vertex_data[2].position.x = x2;
  vertex_data[2].position.y = y2;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.u = 1.0f;
  vertex_data[2].texture.v = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = x2;
  vertex_data[3].position.y = y1;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.u = 1.0f;
  vertex_data[3].texture.v = 1.0f;

  context_handle->Unmap (vertex_buffer.Get (), 0);

  auto overlay = std::make_shared < GstD3D11CompositionOverlay > ();
  overlay->overlay_rect = gst_video_overlay_rectangle_ref (overlay_rect);
  overlay->texture = texture;
  overlay->srv = srv;
  overlay->vertex_buffer = vertex_buffer;
  overlay->premul_alpha = premul_alpha;
  if (is_d3d11)
    overlay->d3d11_buffer = gst_buffer_ref (buf);

  return overlay;
}

static gboolean
gst_d3d11_overlay_compositor_setup_shader (GstD3D11OverlayCompositor * self)
{
  GstD3D11OverlayCompositorPrivate *priv = self->priv;
  GstVideoInfo *info = &priv->info;
  GstD3D11Device *device = self->device;
  HRESULT hr;
  D3D11_SAMPLER_DESC sampler_desc;
  D3D11_INPUT_ELEMENT_DESC input_desc[2];
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_BLEND_DESC blend_desc;
  D3D11_MAPPED_SUBRESOURCE map;
  WORD *indices;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  ComPtr < ID3D11PixelShader > ps;
  ComPtr < ID3D11PixelShader > premul_ps;
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11SamplerState > sampler;
  ComPtr < ID3D11BlendState > blend;
  ComPtr < ID3D11Buffer > index_buffer;

  memset (&sampler_desc, 0, sizeof (sampler_desc));
  memset (input_desc, 0, sizeof (input_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));
  memset (&blend_desc, 0, sizeof (blend_desc));

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  /* bilinear filtering */
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = device_handle->CreateSamplerState (&sampler_desc, &sampler);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create sampler state, hr: 0x%x",
        (guint) hr);
    return FALSE;
  }

  hr = gst_d3d11_create_pixel_shader_simple (device,
      templ_pixel_shader, "main", &ps);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create pixel shader");
    return FALSE;
  }

  hr = gst_d3d11_create_pixel_shader_simple (device,
      templ_premul_pixel_shader, "main", &premul_ps);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create premul pixel shader");
    return FALSE;
  }

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

  hr = gst_d3d11_create_vertex_shader_simple (device, templ_vertex_shader,
      "main", input_desc, G_N_ELEMENTS (input_desc), &vs, &layout);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't vertex pixel shader");
    return FALSE;
  }

  blend_desc.AlphaToCoverageEnable = FALSE;
  blend_desc.IndependentBlendEnable = FALSE;
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;

  hr = device_handle->CreateBlendState (&blend_desc, &blend);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create blend staten, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * 6;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &index_buffer);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create index buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  GstD3D11DeviceLockGuard lk (device);
  hr = context_handle->Map (index_buffer.Get (),
      0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't map index buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  indices = (WORD *) map.pData;

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  context_handle->Unmap (index_buffer.Get (), 0);

  priv->ps = ps;
  priv->premul_ps = premul_ps;
  priv->vs = vs;
  priv->layout = layout;
  priv->sampler = sampler;
  priv->blend = blend;
  priv->index_buffer = index_buffer;

  priv->viewport.TopLeftX = 0;
  priv->viewport.TopLeftY = 0;
  priv->viewport.Width = GST_VIDEO_INFO_WIDTH (info);
  priv->viewport.Height = GST_VIDEO_INFO_HEIGHT (info);
  priv->viewport.MinDepth = 0.0f;
  priv->viewport.MaxDepth = 1.0f;

  return TRUE;
}

GstD3D11OverlayCompositor *
gst_d3d11_overlay_compositor_new (GstD3D11Device * device,
    const GstVideoInfo * info)
{
  GstD3D11OverlayCompositor *self = nullptr;
  GstD3D11OverlayCompositorPrivate *priv;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), nullptr);
  g_return_val_if_fail (info != nullptr, nullptr);

  self = (GstD3D11OverlayCompositor *)
      g_object_new (GST_TYPE_D3D11_OVERLAY_COMPOSITOR, nullptr);
  gst_object_ref_sink (self);
  priv = self->priv;

  self->device = (GstD3D11Device *) gst_object_ref (device);
  priv->info = *info;

  if (!gst_d3d11_overlay_compositor_setup_shader (self)) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

static gboolean
gst_d3d11_overlay_compositor_foreach_meta (GstBuffer * buffer, GstMeta ** meta,
    std::vector < GstVideoOverlayRectangle * >*overlay_rect)
{
  GstVideoOverlayCompositionMeta *cmeta;
  guint num_rect;

  if ((*meta)->info->api != GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE)
    return TRUE;

  cmeta = (GstVideoOverlayCompositionMeta *) (*meta);
  if (!cmeta->overlay)
    return TRUE;

  num_rect = gst_video_overlay_composition_n_rectangles (cmeta->overlay);
  for (guint i = 0; i < num_rect; i++) {
    auto rect = gst_video_overlay_composition_get_rectangle (cmeta->overlay, i);
    overlay_rect->push_back (rect);
  }

  return TRUE;
}

gboolean
gst_d3d11_overlay_compositor_upload (GstD3D11OverlayCompositor * compositor,
    GstBuffer * buf)
{
  GstD3D11OverlayCompositorPrivate *priv;
  std::vector < GstVideoOverlayRectangle * >new_overlay_rect;

  g_return_val_if_fail (compositor != nullptr, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buf), FALSE);

  priv = compositor->priv;

  gst_buffer_foreach_meta (buf,
      (GstBufferForeachMetaFunc) gst_d3d11_overlay_compositor_foreach_meta,
      &new_overlay_rect);

  if (new_overlay_rect.empty ()) {
    priv->overlays.clear ();
    return TRUE;
  }

  GST_LOG_OBJECT (compositor, "Found %" G_GSIZE_FORMAT
      " overlay rectangles, %" G_GSIZE_FORMAT " in current queue",
      new_overlay_rect.size (), priv->overlays.size ());

  /* *INDENT-OFF* */
  for (auto it : new_overlay_rect) {
    if (std::find_if (priv->overlays.begin (), priv->overlays.end (),
          [&] (const auto & overlay) -> bool {
            return overlay->overlay_rect == it;
           }) == priv->overlays.end ()) {
      auto new_overlay = gst_d3d11_composition_overlay_new (compositor, it);
      if (!new_overlay)
        return FALSE;

      priv->overlays.push_back (new_overlay);
    }
  }
  /* *INDENT-ON* */

  GST_LOG_OBJECT (compositor, "Overlay rectangles in queue after uploaded %"
      G_GSIZE_FORMAT, priv->overlays.size ());

  /* Remove old overlay */
  /* *INDENT-OFF* */
  auto it = priv->overlays.begin ();
  while (it != priv->overlays.end ()) {
    auto old_overlay = *it;
    if (std::find_if (new_overlay_rect.begin (), new_overlay_rect.end (),
          [&] (const auto & overlay) -> bool {
            return overlay == old_overlay->overlay_rect;
          }) == new_overlay_rect.end ()) {
      GST_LOG_OBJECT (compositor, "Removing %p from queue",
          old_overlay->overlay_rect);
      it = priv->overlays.erase (it);
    } else {
      it++;
    }
  }
  /* *INDENT-ON* */

  GST_LOG_OBJECT (compositor, "Final queue size %" G_GSIZE_FORMAT,
      priv->overlays.size ());

  return TRUE;
}

gboolean
gst_d3d11_overlay_compositor_update_viewport (GstD3D11OverlayCompositor *
    compositor, D3D11_VIEWPORT * viewport)
{
  g_return_val_if_fail (GST_IS_D3D11_OVERLAY_COMPOSITOR (compositor), FALSE);
  g_return_val_if_fail (viewport != nullptr, FALSE);

  compositor->priv->viewport = *viewport;

  return TRUE;
}

gboolean
gst_d3d11_overlay_compositor_draw (GstD3D11OverlayCompositor * compositor,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  g_return_val_if_fail (compositor != nullptr, FALSE);
  g_return_val_if_fail (rtv != nullptr, FALSE);

  GstD3D11DeviceLockGuard lk (compositor->device);
  return gst_d3d11_overlay_compositor_draw_unlocked (compositor, rtv);
}

gboolean
gst_d3d11_overlay_compositor_draw_unlocked (GstD3D11OverlayCompositor *
    compositor, ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  GstD3D11OverlayCompositorPrivate *priv;
  ID3D11DeviceContext *context;
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { nullptr, };
  UINT strides = sizeof (VertexData);
  UINT offsets = 0;
  ID3D11SamplerState *samplers[1];

  g_return_val_if_fail (compositor != nullptr, FALSE);
  g_return_val_if_fail (rtv != nullptr, FALSE);

  priv = compositor->priv;

  if (priv->overlays.empty ())
    return TRUE;

  samplers[0] = priv->sampler.Get ();

  context = gst_d3d11_device_get_device_context_handle (compositor->device);
  context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout (priv->layout.Get ());
  context->IASetIndexBuffer (priv->index_buffer.Get (),
      DXGI_FORMAT_R16_UINT, 0);
  context->PSSetSamplers (0, 1, samplers);
  context->VSSetShader (priv->vs.Get (), nullptr, 0);
  context->RSSetViewports (1, &priv->viewport);
  context->OMSetRenderTargets (1, rtv, nullptr);
  context->OMSetBlendState (priv->blend.Get (), nullptr, 0xffffffff);

  /* *INDENT-OFF* */
  for (auto overlay : priv->overlays) {
    ID3D11ShaderResourceView *srv[] = { overlay->srv.Get () };
    ID3D11Buffer *vertex_buf[] = { overlay->vertex_buffer.Get () };
    GstMapInfo info;
    GstMemory *mem = nullptr;

    if (overlay->d3d11_buffer) {
      mem = gst_buffer_peek_memory (overlay->d3d11_buffer, 0);
      gst_memory_map (mem, &info, (GstMapFlags) (GST_MAP_D3D11 | GST_MAP_READ));
    }

    if (overlay->premul_alpha)
      context->PSSetShader (priv->premul_ps.Get (), nullptr, 0);
    else
      context->PSSetShader (priv->ps.Get (), nullptr, 0);

    context->PSSetShaderResources (0, 1, srv);
    context->IASetVertexBuffers (0, 1, vertex_buf, &strides, &offsets);

    context->DrawIndexed (6, 0, 0);

    if (mem)
      gst_memory_unmap (mem, &info);
  }
  /* *INDENT-ON* */

  context->PSSetShaderResources (0, 1, clear_view);
  context->OMSetRenderTargets (0, nullptr, nullptr);

  return TRUE;
}
