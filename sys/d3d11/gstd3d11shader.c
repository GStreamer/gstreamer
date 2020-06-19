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
#include "config.h"
#endif

#include "gstd3d11shader.h"
#include "gstd3d11device.h"
#include "gstd3d11utils.h"
#include <gmodule.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_shader_debug);
#define GST_CAT_DEFAULT gst_d3d11_shader_debug

static GModule *d3d_compiler_module = NULL;
static pD3DCompile GstD3DCompileFunc = NULL;

gboolean
gst_d3d11_shader_init (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
#if GST_D3D11_WINAPI_ONLY_APP
    /* Assuming that d3d compiler library is available */
    GstD3DCompileFunc = D3DCompile;
#else
    static const gchar *d3d_compiler_names[] = {
      "d3dcompiler_47.dll",
      "d3dcompiler_46.dll",
      "d3dcompiler_45.dll",
      "d3dcompiler_44.dll",
      "d3dcompiler_43.dll",
    };
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
#endif

    g_once_init_leave (&_init, 1);
  }

  return ! !GstD3DCompileFunc;
}

static ID3DBlob *
compile_shader (GstD3D11Device * device, const gchar * shader_source,
    gboolean is_pixel_shader)
{
  ID3DBlob *ret;
  ID3DBlob *error = NULL;
  const gchar *shader_target;
  D3D_FEATURE_LEVEL feature_level;
  HRESULT hr;

  if (!gst_d3d11_shader_init ()) {
    GST_ERROR ("D3DCompiler is unavailable");
    return NULL;
  }

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

  g_assert (GstD3DCompileFunc);

  hr = GstD3DCompileFunc (shader_source, strlen (shader_source), NULL, NULL,
      NULL, "main", shader_target, 0, 0, &ret, &error);

  if (!gst_d3d11_result (hr, device)) {
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

gboolean
gst_d3d11_create_pixel_shader (GstD3D11Device * device,
    const gchar * source, ID3D11PixelShader ** shader)
{
  ID3DBlob *ps_blob;
  ID3D11Device *device_handle;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);

  gst_d3d11_device_lock (device);
  ps_blob = compile_shader (device, source, TRUE);

  if (!ps_blob) {
    GST_ERROR ("Failed to compile pixel shader");
    gst_d3d11_device_unlock (device);
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);
  hr = ID3D11Device_CreatePixelShader (device_handle,
      (gpointer) ID3D10Blob_GetBufferPointer (ps_blob),
      ID3D10Blob_GetBufferSize (ps_blob), NULL, shader);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("could not create pixel shader, hr: 0x%x", (guint) hr);
    gst_d3d11_device_unlock (device);
    return FALSE;
  }

  ID3D10Blob_Release (ps_blob);
  gst_d3d11_device_unlock (device);

  return TRUE;
}

gboolean
gst_d3d11_create_vertex_shader (GstD3D11Device * device, const gchar * source,
    const D3D11_INPUT_ELEMENT_DESC * input_desc, guint desc_len,
    ID3D11VertexShader ** shader, ID3D11InputLayout ** layout)
{
  ID3DBlob *vs_blob;
  ID3D11Device *device_handle;
  HRESULT hr;
  ID3D11VertexShader *vshader = NULL;
  ID3D11InputLayout *in_layout = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (input_desc != NULL, FALSE);
  g_return_val_if_fail (desc_len > 0, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);
  g_return_val_if_fail (layout != NULL, FALSE);

  gst_d3d11_device_lock (device);
  vs_blob = compile_shader (device, source, FALSE);
  if (!vs_blob) {
    GST_ERROR ("Failed to compile shader code");
    goto done;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);

  hr = ID3D11Device_CreateVertexShader (device_handle,
      (gpointer) ID3D10Blob_GetBufferPointer (vs_blob),
      ID3D10Blob_GetBufferSize (vs_blob), NULL, &vshader);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("could not create vertex shader, hr: 0x%x", (guint) hr);
    ID3D10Blob_Release (vs_blob);
    goto done;
  }

  hr = ID3D11Device_CreateInputLayout (device_handle, input_desc,
      desc_len, (gpointer) ID3D10Blob_GetBufferPointer (vs_blob),
      ID3D10Blob_GetBufferSize (vs_blob), &in_layout);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("could not create input layout shader, hr: 0x%x", (guint) hr);
    ID3D10Blob_Release (vs_blob);
    ID3D11VertexShader_Release (vshader);
    goto done;
  }

  ID3D10Blob_Release (vs_blob);

  *shader = vshader;
  *layout = in_layout;

  ret = TRUE;

done:
  gst_d3d11_device_unlock (device);

  return ret;
}

struct _GstD3D11Quad
{
  GstD3D11Device *device;
  ID3D11PixelShader *ps;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11SamplerState *sampler;
  ID3D11BlendState *blend;
  ID3D11DepthStencilState *depth_stencil;
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
    ID3D11SamplerState * sampler, ID3D11BlendState * blend,
    ID3D11DepthStencilState * depth_stencil,
    ID3D11Buffer * const_buffer,
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
  quad->blend = blend;
  quad->depth_stencil = depth_stencil;
  quad->vertex_buffer = vertex_buffer;
  quad->vertex_stride = vertex_stride;
  quad->index_buffer = index_buffer;
  quad->index_format = index_format;
  quad->index_count = index_count;

  ID3D11PixelShader_AddRef (pixel_shader);
  ID3D11VertexShader_AddRef (vertex_shader);
  ID3D11InputLayout_AddRef (layout);
  ID3D11SamplerState_AddRef (sampler);

  if (blend)
    ID3D11BlendState_AddRef (blend);

  if (depth_stencil)
    ID3D11DepthStencilState_AddRef (depth_stencil);

  if (const_buffer) {
    quad->const_buffer = const_buffer;
    ID3D11Buffer_AddRef (const_buffer);
  }
  ID3D11Buffer_AddRef (vertex_buffer);
  ID3D11Buffer_AddRef (index_buffer);

  return quad;
}

void
gst_d3d11_quad_free (GstD3D11Quad * quad)
{
  g_return_if_fail (quad != NULL);

  if (quad->ps)
    ID3D11PixelShader_Release (quad->ps);
  if (quad->vs)
    ID3D11VertexShader_Release (quad->vs);
  if (quad->layout)
    ID3D11InputLayout_Release (quad->layout);
  if (quad->sampler)
    ID3D11SamplerState_Release (quad->sampler);
  if (quad->blend)
    ID3D11BlendState_Release (quad->blend);
  if (quad->depth_stencil)
    ID3D11DepthStencilState_Release (quad->depth_stencil);
  if (quad->const_buffer)
    ID3D11Buffer_Release (quad->const_buffer);
  if (quad->vertex_buffer)
    ID3D11Buffer_Release (quad->vertex_buffer);
  if (quad->index_buffer)
    ID3D11Buffer_Release (quad->index_buffer);

  gst_clear_object (&quad->device);
  g_free (quad);
}

gboolean
gst_d3d11_draw_quad (GstD3D11Quad * quad,
    D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES], guint num_viewport,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES], guint num_srv,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES], guint num_rtv,
    ID3D11DepthStencilView * dsv)
{
  gboolean ret;

  g_return_val_if_fail (quad != NULL, FALSE);

  gst_d3d11_device_lock (quad->device);
  ret = gst_d3d11_draw_quad_unlocked (quad, viewport, num_viewport,
      srv, num_srv, rtv, num_viewport, dsv);
  gst_d3d11_device_unlock (quad->device);

  return ret;
}

gboolean
gst_d3d11_draw_quad_unlocked (GstD3D11Quad * quad,
    D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES], guint num_viewport,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES], guint num_srv,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES], guint num_rtv,
    ID3D11DepthStencilView * dsv)
{
  ID3D11DeviceContext *context_handle;
  UINT offsets = 0;
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { NULL, };

  g_return_val_if_fail (quad != NULL, FALSE);
  g_return_val_if_fail (viewport != NULL, FALSE);
  g_return_val_if_fail (num_viewport <= GST_VIDEO_MAX_PLANES, FALSE);
  g_return_val_if_fail (srv != NULL, FALSE);
  g_return_val_if_fail (num_srv <= GST_VIDEO_MAX_PLANES, FALSE);
  g_return_val_if_fail (rtv != NULL, FALSE);
  g_return_val_if_fail (num_rtv <= GST_VIDEO_MAX_PLANES, FALSE);

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
  ID3D11DeviceContext_RSSetViewports (context_handle, num_viewport, viewport);

  if (quad->const_buffer)
    ID3D11DeviceContext_PSSetConstantBuffers (context_handle,
        0, 1, &quad->const_buffer);

  ID3D11DeviceContext_PSSetShaderResources (context_handle, 0, num_srv, srv);
  ID3D11DeviceContext_OMSetRenderTargets (context_handle, num_rtv, rtv, dsv);
  ID3D11DeviceContext_OMSetBlendState (context_handle,
      quad->blend, NULL, 0xffffffff);
  ID3D11DeviceContext_OMSetDepthStencilState (context_handle,
      quad->depth_stencil, 1);

  ID3D11DeviceContext_DrawIndexed (context_handle, quad->index_count, 0, 0);

  ID3D11DeviceContext_PSSetShaderResources (context_handle,
      0, num_srv, clear_view);
  ID3D11DeviceContext_OMSetRenderTargets (context_handle, 0, NULL, NULL);

  return TRUE;
}
