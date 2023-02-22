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
/* *INDENT-ON* */

struct _GstD3D11OverlayCompositorPrivate
{
  GstVideoInfo info;

  D3D11_VIEWPORT viewport;

  ID3D11PixelShader *ps;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11SamplerState *sampler;
  ID3D11BlendState *blend;
  ID3D11Buffer *index_buffer;

  /* GstD3D11CompositionOverlay */
  GList *overlays;
};

typedef struct
{
  GstVideoOverlayRectangle *overlay_rect;
  ID3D11Texture2D *texture;
  ID3D11ShaderResourceView *srv;
  ID3D11Buffer *vertex_buffer;
} GstD3D11CompositionOverlay;

static void gst_d3d11_overlay_compositor_dispose (GObject * object);
static void
gst_d3d11_overlay_compositor_free_overlays (GstD3D11OverlayCompositor * self);

#define gst_d3d11_overlay_compositor_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11OverlayCompositor,
    gst_d3d11_overlay_compositor, GST_TYPE_OBJECT);

static void
gst_d3d11_overlay_compositor_class_init (GstD3D11OverlayCompositorClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_d3d11_overlay_compositor_dispose;
}

static void
gst_d3d11_overlay_compositor_init (GstD3D11OverlayCompositor * self)
{
  self->priv = (GstD3D11OverlayCompositorPrivate *)
      gst_d3d11_overlay_compositor_get_instance_private (self);
}

static void
gst_d3d11_overlay_compositor_dispose (GObject * object)
{
  GstD3D11OverlayCompositor *self = GST_D3D11_OVERLAY_COMPOSITOR (object);
  GstD3D11OverlayCompositorPrivate *priv = self->priv;

  gst_d3d11_overlay_compositor_free_overlays (self);

  GST_D3D11_CLEAR_COM (priv->ps);
  GST_D3D11_CLEAR_COM (priv->vs);
  GST_D3D11_CLEAR_COM (priv->layout);
  GST_D3D11_CLEAR_COM (priv->sampler);
  GST_D3D11_CLEAR_COM (priv->blend);
  GST_D3D11_CLEAR_COM (priv->index_buffer);

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstD3D11CompositionOverlay *
gst_d3d11_composition_overlay_new (GstD3D11OverlayCompositor * self,
    GstVideoOverlayRectangle * overlay_rect)
{
  GstD3D11OverlayCompositorPrivate *priv = self->priv;
  GstD3D11CompositionOverlay *overlay = nullptr;
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

  buf = gst_video_overlay_rectangle_get_pixels_unscaled_argb (overlay_rect,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  if (!buf) {
    GST_ERROR_OBJECT (self, "Failed to get overlay buffer");
    return nullptr;
  }

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

  overlay = g_new0 (GstD3D11CompositionOverlay, 1);
  overlay->overlay_rect = gst_video_overlay_rectangle_ref (overlay_rect);
  overlay->texture = texture.Detach ();
  overlay->srv = srv.Detach ();
  overlay->vertex_buffer = vertex_buffer.Detach ();

  return overlay;
}

static void
gst_d3d11_composition_overlay_free (GstD3D11CompositionOverlay * overlay)
{
  if (!overlay)
    return;

  if (overlay->overlay_rect)
    gst_video_overlay_rectangle_unref (overlay->overlay_rect);

  GST_D3D11_CLEAR_COM (overlay->srv);
  GST_D3D11_CLEAR_COM (overlay->texture);
  GST_D3D11_CLEAR_COM (overlay->vertex_buffer);

  g_free (overlay);
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

  priv->ps = ps.Detach ();
  priv->vs = vs.Detach ();
  priv->layout = layout.Detach ();
  priv->sampler = sampler.Detach ();
  priv->blend = blend.Detach ();
  priv->index_buffer = index_buffer.Detach ();

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

static void
gst_d3d11_overlay_compositor_free_overlays (GstD3D11OverlayCompositor * self)
{
  GstD3D11OverlayCompositorPrivate *priv = self->priv;

  if (priv->overlays) {
    g_list_free_full (priv->overlays,
        (GDestroyNotify) gst_d3d11_composition_overlay_free);

    priv->overlays = nullptr;
  }
}

static gint
find_in_compositor (const GstD3D11CompositionOverlay * overlay,
    const GstVideoOverlayRectangle * rect)
{
  return !(overlay->overlay_rect == rect);
}

static gboolean
is_in_video_overlay_composition (GstVideoOverlayComposition * voc,
    GstD3D11CompositionOverlay * overlay)
{
  guint i;

  for (i = 0; i < gst_video_overlay_composition_n_rectangles (voc); i++) {
    GstVideoOverlayRectangle *rectangle =
        gst_video_overlay_composition_get_rectangle (voc, i);
    if (overlay->overlay_rect == rectangle)
      return TRUE;
  }
  return FALSE;
}

gboolean
gst_d3d11_overlay_compositor_upload (GstD3D11OverlayCompositor * compositor,
    GstBuffer * buf)
{
  GstD3D11OverlayCompositorPrivate *priv;
  GstVideoOverlayCompositionMeta *meta;
  gint i, num_overlays;
  GList *iter;

  g_return_val_if_fail (compositor != nullptr, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buf), FALSE);

  priv = compositor->priv;

  meta = gst_buffer_get_video_overlay_composition_meta (buf);
  if (!meta) {
    gst_d3d11_overlay_compositor_free_overlays (compositor);
    return TRUE;
  }

  num_overlays = gst_video_overlay_composition_n_rectangles (meta->overlay);
  if (!num_overlays) {
    gst_d3d11_overlay_compositor_free_overlays (compositor);
    return TRUE;
  }

  GST_LOG_OBJECT (compositor, "Upload %d overlay rectangles", num_overlays);

  /* Upload new overlay */
  for (i = 0; i < num_overlays; i++) {
    GstVideoOverlayRectangle *rectangle =
        gst_video_overlay_composition_get_rectangle (meta->overlay, i);

    if (!g_list_find_custom (priv->overlays,
            rectangle, (GCompareFunc) find_in_compositor)) {
      GstD3D11CompositionOverlay *overlay = nullptr;

      overlay = gst_d3d11_composition_overlay_new (compositor, rectangle);

      if (!overlay)
        return FALSE;

      priv->overlays = g_list_append (priv->overlays, overlay);
    }
  }

  /* Remove old overlay */
  iter = priv->overlays;
  while (iter) {
    GstD3D11CompositionOverlay *overlay =
        (GstD3D11CompositionOverlay *) iter->data;
    GList *next = iter->next;

    if (!is_in_video_overlay_composition (meta->overlay, overlay)) {
      priv->overlays = g_list_delete_link (priv->overlays, iter);
      gst_d3d11_composition_overlay_free (overlay);
    }

    iter = next;
  }

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
  GList *iter;
  ID3D11DeviceContext *context;
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { nullptr, };
  UINT strides = sizeof (VertexData);
  UINT offsets = 0;

  g_return_val_if_fail (compositor != nullptr, FALSE);
  g_return_val_if_fail (rtv != nullptr, FALSE);

  priv = compositor->priv;

  if (!priv->overlays)
    return TRUE;

  context = gst_d3d11_device_get_device_context_handle (compositor->device);
  context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout (priv->layout);
  context->IASetIndexBuffer (priv->index_buffer, DXGI_FORMAT_R16_UINT, 0);
  context->PSSetSamplers (0, 1, &priv->sampler);
  context->VSSetShader (priv->vs, nullptr, 0);
  context->PSSetShader (priv->ps, nullptr, 0);
  context->RSSetViewports (1, &priv->viewport);
  context->OMSetRenderTargets (1, rtv, nullptr);
  context->OMSetBlendState (priv->blend, nullptr, 0xffffffff);

  for (iter = priv->overlays; iter; iter = g_list_next (iter)) {
    GstD3D11CompositionOverlay *overlay =
        (GstD3D11CompositionOverlay *) iter->data;

    context->PSSetShaderResources (0, 1, &overlay->srv);
    context->IASetVertexBuffers (0,
        1, &overlay->vertex_buffer, &strides, &offsets);

    context->DrawIndexed (6, 0, 0);
  }

  context->PSSetShaderResources (0, 1, clear_view);
  context->OMSetRenderTargets (0, nullptr, nullptr);

  return TRUE;
}
