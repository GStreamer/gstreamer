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
#include "gstd3d11pluginutils.h"
#include <gmodule.h>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_shader_debug);
#define GST_CAT_DEFAULT gst_d3d11_shader_debug

G_END_DECLS
/* *INDENT-ON* */

/* too many const buffers doesn't make sense */
#define MAX_CONST_BUFFERS 16

static GModule *d3d_compiler_module = NULL;
static pD3DCompile GstD3DCompileFunc = NULL;

gboolean
gst_d3d11_shader_init (void)
{
  static gsize _init = 0;

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
    guint i;
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

  return !!GstD3DCompileFunc;
}

static gboolean
compile_shader (GstD3D11Device * device, const gchar * shader_source,
    gboolean is_pixel_shader, ID3DBlob ** blob)
{
  const gchar *shader_target;
  D3D_FEATURE_LEVEL feature_level;
  HRESULT hr;
  ID3D11Device *device_handle;
  /* *INDENT-OFF* */
  ComPtr<ID3DBlob> ret;
  ComPtr<ID3DBlob> error;
  /* *INDENT-ON* */

  if (!gst_d3d11_shader_init ()) {
    GST_ERROR ("D3DCompiler is unavailable");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);
  feature_level = device_handle->GetFeatureLevel ();

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

  GST_TRACE ("Compile code \n%s", shader_source);

  hr = GstD3DCompileFunc (shader_source, strlen (shader_source), NULL, NULL,
      NULL, "main", shader_target, 0, 0, &ret, &error);

  if (!gst_d3d11_result (hr, device)) {
    const gchar *err = NULL;

    if (error)
      err = (const gchar *) error->GetBufferPointer ();

    GST_ERROR ("could not compile source, hr: 0x%x, error detail %s",
        (guint) hr, GST_STR_NULL (err));
    return FALSE;
  }

  if (error) {
    const gchar *err = (const gchar *) error->GetBufferPointer ();

    GST_DEBUG ("HLSL compiler warnings:\n%s\nShader code:\n%s",
        GST_STR_NULL (err), GST_STR_NULL (shader_source));
  }

  *blob = ret.Detach ();

  return TRUE;
}

gboolean
gst_d3d11_create_pixel_shader (GstD3D11Device * device,
    const gchar * source, ID3D11PixelShader ** shader)
{
  ID3D11Device *device_handle;
  HRESULT hr;
  /* *INDENT-OFF* */
  ComPtr<ID3DBlob> ps_blob;
  /* *INDENT-ON* */

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);

  if (!compile_shader (device, source, TRUE, &ps_blob)) {
    GST_ERROR ("Failed to compile pixel shader");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);
  hr = device_handle->CreatePixelShader (ps_blob->GetBufferPointer (),
      ps_blob->GetBufferSize (), NULL, shader);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("could not create pixel shader, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_create_vertex_shader (GstD3D11Device * device, const gchar * source,
    const D3D11_INPUT_ELEMENT_DESC * input_desc, guint desc_len,
    ID3D11VertexShader ** shader, ID3D11InputLayout ** layout)
{
  ID3D11Device *device_handle;
  HRESULT hr;
  /* *INDENT-OFF* */
  ComPtr<ID3DBlob> vs_blob;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11InputLayout> in_layout;
  /* *INDENT-ON* */

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (input_desc != NULL, FALSE);
  g_return_val_if_fail (desc_len > 0, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);
  g_return_val_if_fail (layout != NULL, FALSE);

  if (!compile_shader (device, source, FALSE, &vs_blob)) {
    GST_ERROR ("Failed to compile shader code");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (device);
  hr = device_handle->CreateVertexShader (vs_blob->GetBufferPointer (),
      vs_blob->GetBufferSize (), NULL, &vs);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("could not create vertex shader, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  hr = device_handle->CreateInputLayout (input_desc,
      desc_len, vs_blob->GetBufferPointer (),
      vs_blob->GetBufferSize (), &in_layout);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("could not create input layout shader, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  *shader = vs.Detach ();
  *layout = in_layout.Detach ();

  return TRUE;
}

struct _GstD3D11Quad
{
  GstD3D11Device *device;
  ID3D11PixelShader *ps;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11Buffer *const_buffer[MAX_CONST_BUFFERS];
  guint num_const_buffers;
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
    ID3D11Buffer ** const_buffers, guint num_const_buffers,
    ID3D11Buffer * vertex_buffer, guint vertex_stride,
    ID3D11Buffer * index_buffer, DXGI_FORMAT index_format, guint index_count)
{
  GstD3D11Quad *quad;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (pixel_shader != NULL, NULL);
  g_return_val_if_fail (vertex_shader != NULL, NULL);
  g_return_val_if_fail (layout != NULL, NULL);
  g_return_val_if_fail (num_const_buffers <= MAX_CONST_BUFFERS, NULL);
  g_return_val_if_fail (vertex_buffer != NULL, NULL);
  g_return_val_if_fail (vertex_stride > 0, NULL);
  g_return_val_if_fail (index_buffer != NULL, NULL);
  g_return_val_if_fail (index_format != DXGI_FORMAT_UNKNOWN, NULL);

  quad = g_new0 (GstD3D11Quad, 1);

  quad->device = (GstD3D11Device *) gst_object_ref (device);
  quad->ps = pixel_shader;
  quad->vs = vertex_shader;
  quad->layout = layout;
  quad->vertex_buffer = vertex_buffer;
  quad->vertex_stride = vertex_stride;
  quad->index_buffer = index_buffer;
  quad->index_format = index_format;
  quad->index_count = index_count;

  pixel_shader->AddRef ();
  vertex_shader->AddRef ();
  layout->AddRef ();
  vertex_buffer->AddRef ();
  index_buffer->AddRef ();

  if (num_const_buffers > 0) {
    guint i;

    g_assert (const_buffers);

    for (i = 0; i < num_const_buffers; i++) {
      quad->const_buffer[i] = const_buffers[i];
      quad->const_buffer[i]->AddRef ();
    }

    quad->num_const_buffers = num_const_buffers;
  }

  return quad;
}

void
gst_d3d11_quad_free (GstD3D11Quad * quad)
{
  guint i;

  g_return_if_fail (quad != NULL);

  GST_D3D11_CLEAR_COM (quad->ps);
  GST_D3D11_CLEAR_COM (quad->vs);
  GST_D3D11_CLEAR_COM (quad->layout);
  for (i = 0; i < quad->num_const_buffers; i++)
    quad->const_buffer[i]->Release ();
  GST_D3D11_CLEAR_COM (quad->vertex_buffer);
  GST_D3D11_CLEAR_COM (quad->index_buffer);

  gst_clear_object (&quad->device);
  g_free (quad);
}

gboolean
gst_d3d11_draw_quad (GstD3D11Quad * quad,
    D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES], guint num_viewport,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES], guint num_srv,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES], guint num_rtv,
    ID3D11BlendState * blend, gfloat blend_factor[4],
    ID3D11SamplerState ** sampler, guint num_sampler)
{
  gboolean ret;

  g_return_val_if_fail (quad != NULL, FALSE);

  gst_d3d11_device_lock (quad->device);
  ret = gst_d3d11_draw_quad_unlocked (quad, viewport, num_viewport,
      srv, num_srv, rtv, num_viewport, blend, blend_factor, sampler,
      num_sampler);
  gst_d3d11_device_unlock (quad->device);

  return ret;
}

gboolean
gst_d3d11_draw_quad_unlocked (GstD3D11Quad * quad,
    D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES], guint num_viewport,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES], guint num_srv,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES], guint num_rtv,
    ID3D11BlendState * blend, gfloat blend_factor[4],
    ID3D11SamplerState ** sampler, guint num_sampler)
{
  ID3D11DeviceContext *context;
  UINT offsets = 0;
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11BlendState *blend_state = blend;

  g_return_val_if_fail (quad != NULL, FALSE);
  g_return_val_if_fail (viewport != NULL, FALSE);
  g_return_val_if_fail (num_viewport <= GST_VIDEO_MAX_PLANES, FALSE);
  g_return_val_if_fail (rtv != NULL, FALSE);
  g_return_val_if_fail (num_rtv <= GST_VIDEO_MAX_PLANES, FALSE);

  context = gst_d3d11_device_get_device_context_handle (quad->device);

  context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout (quad->layout);
  context->IASetVertexBuffers (0, 1, &quad->vertex_buffer, &quad->vertex_stride,
      &offsets);
  context->IASetIndexBuffer (quad->index_buffer, quad->index_format, 0);

  if (sampler)
    context->PSSetSamplers (0, num_sampler, sampler);
  context->VSSetShader (quad->vs, NULL, 0);
  context->PSSetShader (quad->ps, NULL, 0);
  context->RSSetViewports (num_viewport, viewport);

  if (quad->num_const_buffers) {
    context->PSSetConstantBuffers (0, quad->num_const_buffers,
        quad->const_buffer);
  }

  if (srv)
    context->PSSetShaderResources (0, num_srv, srv);
  context->OMSetRenderTargets (num_rtv, rtv, NULL);
  context->OMSetBlendState (blend_state, blend_factor, 0xffffffff);

  context->DrawIndexed (quad->index_count, 0, 0);

  if (srv)
    context->PSSetShaderResources (0, num_srv, clear_view);
  context->OMSetRenderTargets (0, NULL, NULL);

  return TRUE;
}
