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

#include "gstd3d11shader.h"
#include "gstd3d11device.h"
#include "gstd3d11utils.h"

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_shader_debug);
#define GST_CAT_DEFAULT gst_d3d11_shader_debug

static ID3DBlob *
compile_shader (GstD3D11Device * device, const gchar * shader_source,
    gboolean is_pixel_shader)
{
  ID3DBlob *ret;
  ID3DBlob *error = NULL;
  const gchar *shader_target;
  D3D_FEATURE_LEVEL feature_level;
  HRESULT hr;

  feature_level = gst_d3d11_device_get_chosen_feature_level (device);

  if (is_pixel_shader) {
    if (feature_level >= D3D_FEATURE_LEVEL_10_0)
      shader_target = "ps_4_0";
    else if (feature_level >= D3D_FEATURE_LEVEL_9_3)
      shader_target = "ps_4_0_level_9_3";
    else
      shader_target = "ps_4_0_level_9_1";
  } else {
    if (feature_level >= D3D_FEATURE_LEVEL_10_0)
      shader_target = "vs_4_0";
    else if (feature_level >= D3D_FEATURE_LEVEL_9_3)
      shader_target = "vs_4_0_level_9_3";
    else
      shader_target = "vs_4_0_level_9_1";
  }

  hr = D3DCompile (shader_source, strlen (shader_source), NULL, NULL, NULL,
      "main", shader_target, 0, 0, &ret, &error);

  if (!gst_d3d11_result (hr)) {
    const gchar *err = NULL;

    if (error)
      err = ID3D10Blob_GetBufferPointer (error);

    GST_ERROR ("could not compile source, hr: 0x%x, error detail %s",
        (guint) hr, GST_STR_NULL (err));

    if (error)
      ID3D10Blob_Release (error);

    return NULL;
  }

  return ret;
}

typedef struct
{
  const gchar *source;
  ID3D11PixelShader *shader;
  gboolean ret;
} CreatePSData;

static void
create_pixel_shader (GstD3D11Device * device, CreatePSData * data)
{
  ID3DBlob *ps_blob;
  ID3D11Device *device_handle;
  HRESULT hr;

  data->ret = TRUE;

  ps_blob = compile_shader (device, data->source, TRUE);

  if (!ps_blob) {
    GST_ERROR ("Failed to compile pixel shader");
    data->ret = FALSE;
    return;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);
  hr = ID3D11Device_CreatePixelShader (device_handle,
      (gpointer) ID3D10Blob_GetBufferPointer (ps_blob),
      ID3D10Blob_GetBufferSize (ps_blob), NULL, &data->shader);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("could not create pixel shader, hr: 0x%x", (guint) hr);
    data->ret = FALSE;
  }

  ID3D10Blob_Release (ps_blob);
}

gboolean
gst_d3d11_create_pixel_shader (GstD3D11Device * device,
    const gchar * source, ID3D11PixelShader ** shader)
{
  CreatePSData data = { 0, };

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);

  data.source = source;

  gst_d3d11_device_thread_add (device,
      (GstD3D11DeviceThreadFunc) create_pixel_shader, &data);

  *shader = data.shader;
  return data.ret;
}

typedef struct
{
  const gchar *source;
  const D3D11_INPUT_ELEMENT_DESC *input_desc;
  guint desc_len;
  ID3D11VertexShader *shader;
  ID3D11InputLayout *layout;
  gboolean ret;
} CreateVSData;

static void
create_vertex_shader (GstD3D11Device * device, CreateVSData * data)
{
  ID3DBlob *vs_blob;
  ID3D11Device *device_handle;
  HRESULT hr;
  ID3D11VertexShader *vshader = NULL;
  ID3D11InputLayout *in_layout = NULL;

  data->ret = TRUE;

  vs_blob = compile_shader (device, data->source, FALSE);
  if (!vs_blob) {
    GST_ERROR ("Failed to compile shader code");
    data->ret = FALSE;
    return;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);

  hr = ID3D11Device_CreateVertexShader (device_handle,
      (gpointer) ID3D10Blob_GetBufferPointer (vs_blob),
      ID3D10Blob_GetBufferSize (vs_blob), NULL, &vshader);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("could not create vertex shader, hr: 0x%x", (guint) hr);
    ID3D10Blob_Release (vs_blob);
    data->ret = FALSE;
    return;
  }

  hr = ID3D11Device_CreateInputLayout (device_handle, data->input_desc,
      data->desc_len, (gpointer) ID3D10Blob_GetBufferPointer (vs_blob),
      ID3D10Blob_GetBufferSize (vs_blob), &in_layout);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("could not create input layout shader, hr: 0x%x", (guint) hr);
    ID3D10Blob_Release (vs_blob);
    ID3D11VertexShader_Release (vshader);
    data->ret = FALSE;
    return;
  }

  ID3D10Blob_Release (vs_blob);

  data->shader = vshader;
  data->layout = in_layout;
}

gboolean
gst_d3d11_create_vertex_shader (GstD3D11Device * device, const gchar * source,
    const D3D11_INPUT_ELEMENT_DESC * input_desc, guint desc_len,
    ID3D11VertexShader ** shader, ID3D11InputLayout ** layout)
{
  CreateVSData data = { 0, };

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (input_desc != NULL, FALSE);
  g_return_val_if_fail (desc_len > 0, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);
  g_return_val_if_fail (layout != NULL, FALSE);

  data.source = source;
  data.input_desc = input_desc;
  data.desc_len = desc_len;

  gst_d3d11_device_thread_add (device,
      (GstD3D11DeviceThreadFunc) create_vertex_shader, &data);

  *shader = data.shader;
  *layout = data.layout;
  return data.ret;
}

struct _GstD3D11Quad
{
  GstD3D11Device *device;
  ID3D11PixelShader *ps;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11SamplerState *sampler;
  ID3D11Buffer *const_buffer;
  ID3D11Buffer *vertex_buffer;
  guint vertex_stride;
  ID3D11Buffer *index_buffer;
  DXGI_FORMAT index_format;
  guint index_count;
  D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES];
  ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES];
  guint num_srv;
  ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES];
  guint num_rtv;
};

GstD3D11Quad *
gst_d3d11_quad_new (GstD3D11Device * device, ID3D11PixelShader * pixel_shader,
    ID3D11VertexShader * vertex_shader, ID3D11InputLayout * layout,
    ID3D11SamplerState * sampler, ID3D11Buffer * const_buffer,
    ID3D11Buffer * vertex_buffer, guint vertex_stride,
    ID3D11Buffer * index_buffer, DXGI_FORMAT index_format, guint index_count)
{
  GstD3D11Quad *quad;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (pixel_shader != NULL, NULL);
  g_return_val_if_fail (vertex_shader != NULL, NULL);
  g_return_val_if_fail (layout != NULL, NULL);
  g_return_val_if_fail (sampler != NULL, NULL);
  g_return_val_if_fail (vertex_buffer != NULL, NULL);
  g_return_val_if_fail (vertex_stride > 0, NULL);
  g_return_val_if_fail (index_buffer != NULL, NULL);
  g_return_val_if_fail (index_format != DXGI_FORMAT_UNKNOWN, NULL);

  quad = g_new0 (GstD3D11Quad, 1);

  quad->device = gst_object_ref (device);
  quad->ps = pixel_shader;
  quad->vs = vertex_shader;
  quad->layout = layout;
  quad->sampler = sampler;
  quad->vertex_buffer = vertex_buffer;
  quad->vertex_stride = vertex_stride;
  quad->index_buffer = index_buffer;
  quad->index_format = index_format;
  quad->index_count = index_count;

  ID3D11PixelShader_AddRef (pixel_shader);
  ID3D11VertexShader_AddRef (vertex_shader);
  ID3D11InputLayout_AddRef (layout);
  ID3D11SamplerState_AddRef (sampler);

  if (const_buffer) {
    quad->const_buffer = const_buffer;
    ID3D11Buffer_AddRef (const_buffer);
  }
  ID3D11Buffer_AddRef (vertex_buffer);
  ID3D11Buffer_AddRef (index_buffer);

  return quad;
}

static void
quad_free (GstD3D11Device * device, GstD3D11Quad * quad)
{
  if (quad->ps)
    ID3D11PixelShader_Release (quad->ps);
  if (quad->vs)
    ID3D11VertexShader_Release (quad->vs);
  if (quad->layout)
    ID3D11InputLayout_Release (quad->layout);
  if (quad->sampler)
    ID3D11SamplerState_Release (quad->sampler);
  if (quad->const_buffer)
    ID3D11Buffer_Release (quad->const_buffer);
  if (quad->vertex_buffer)
    ID3D11Buffer_Release (quad->vertex_buffer);
  if (quad->index_buffer)
    ID3D11Buffer_Release (quad->index_buffer);
}

void
gst_d3d11_quad_free (GstD3D11Quad * quad)
{
  g_return_if_fail (quad != NULL);

  if (quad->device) {
    gst_d3d11_device_thread_add (quad->device,
        (GstD3D11DeviceThreadFunc) quad_free, quad);

    gst_object_unref (quad->device);
  }

  g_free (quad);
}

typedef struct
{
  GstD3D11Quad *quad;
  D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES];
  guint num_viewport;
  ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES];
  guint num_srv;
  ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES];
  guint num_rtv;

  gboolean ret;
} DrawQuadData;

static void
gst_d3d11_draw_quad_internal (GstD3D11Device * device, DrawQuadData * data)
{
  ID3D11DeviceContext *context_handle;
  UINT offsets = 0;
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  GstD3D11Quad *quad = data->quad;

  context_handle = gst_d3d11_device_get_device_context_handle (quad->device);

  ID3D11DeviceContext_IASetPrimitiveTopology (context_handle,
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_IASetInputLayout (context_handle, quad->layout);
  ID3D11DeviceContext_IASetVertexBuffers (context_handle,
      0, 1, &quad->vertex_buffer, &quad->vertex_stride, &offsets);
  ID3D11DeviceContext_IASetIndexBuffer (context_handle,
      quad->index_buffer, quad->index_format, 0);

  ID3D11DeviceContext_PSSetSamplers (context_handle, 0, 1, &quad->sampler);
  ID3D11DeviceContext_VSSetShader (context_handle, quad->vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader (context_handle, quad->ps, NULL, 0);
  ID3D11DeviceContext_RSSetViewports (context_handle,
      data->num_viewport, data->viewport);

  if (quad->const_buffer)
    ID3D11DeviceContext_PSSetConstantBuffers (context_handle,
        0, 1, &quad->const_buffer);

  ID3D11DeviceContext_PSSetShaderResources (context_handle,
      0, data->num_srv, data->srv);
  ID3D11DeviceContext_OMSetRenderTargets (context_handle,
      data->num_rtv, data->rtv, NULL);

  ID3D11DeviceContext_DrawIndexed (context_handle, quad->index_count, 0, 0);

  ID3D11DeviceContext_PSSetShaderResources (context_handle,
      0, data->num_srv, clear_view);

  data->ret = TRUE;
}

gboolean
gst_d3d11_draw_quad (GstD3D11Quad * quad,
    D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES], guint num_viewport,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES], guint num_srv,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES], guint num_rtv)
{
  DrawQuadData data = { 0, };
  gint i;

  g_return_val_if_fail (quad != NULL, FALSE);
  g_return_val_if_fail (viewport != NULL, FALSE);
  g_return_val_if_fail (num_viewport <= GST_VIDEO_MAX_PLANES, FALSE);
  g_return_val_if_fail (srv != NULL, FALSE);
  g_return_val_if_fail (num_srv <= GST_VIDEO_MAX_PLANES, FALSE);
  g_return_val_if_fail (rtv != NULL, FALSE);
  g_return_val_if_fail (num_rtv <= GST_VIDEO_MAX_PLANES, FALSE);

  data.quad = quad;
  for (i = 0; i < num_viewport; i++)
    data.viewport[i] = viewport[i];
  data.num_viewport = num_viewport;

  for (i = 0; i < num_srv; i++)
    data.srv[i] = srv[i];
  data.num_srv = num_srv;

  for (i = 0; i < num_rtv; i++)
    data.rtv[i] = rtv[i];
  data.num_rtv = num_rtv;

  data.ret = TRUE;

  gst_d3d11_device_thread_add (quad->device,
      (GstD3D11DeviceThreadFunc) gst_d3d11_draw_quad_internal, &data);

  return data.ret;
}
