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

#ifndef __GST_D3D11_SHADER_H__
#define __GST_D3D11_SHADER_H__

#include <gst/gst.h>
#include "gstd3d11_fwd.h"
#include <gst/video/video.h>

#include <d3dcompiler.h>

G_BEGIN_DECLS

typedef struct _GstD3D11Quad GstD3D11Quad;

gboolean gst_d3d11_shader_init (void);

gboolean gst_d3d11_create_pixel_shader (GstD3D11Device * device,
                                        const gchar * source,
                                        ID3D11PixelShader ** shader);

gboolean gst_d3d11_create_vertex_shader (GstD3D11Device * device,
                                         const gchar * source,
                                         const D3D11_INPUT_ELEMENT_DESC * input_desc,
                                         guint desc_len,
                                         ID3D11VertexShader ** shader,
                                         ID3D11InputLayout ** layout);

GstD3D11Quad * gst_d3d11_quad_new (GstD3D11Device * device,
                                   ID3D11PixelShader * pixel_shader,
                                   ID3D11VertexShader * vertex_shader,
                                   ID3D11InputLayout * layout,
                                   ID3D11SamplerState * sampler,
                                   ID3D11BlendState * blend,
                                   ID3D11DepthStencilState *depth_stencil,
                                   ID3D11Buffer * const_buffer,
                                   ID3D11Buffer * vertex_buffer,
                                   guint vertex_stride,
                                   ID3D11Buffer * index_buffer,
                                   DXGI_FORMAT index_format,
                                   guint index_count);

void          gst_d3d11_quad_free (GstD3D11Quad * quad);

gboolean gst_d3d11_draw_quad (GstD3D11Quad * quad,
                              D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES],
                              guint num_viewport,
                              ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES],
                              guint num_srv,
                              ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES],
                              guint num_rtv,
                              ID3D11DepthStencilView *dsv);

gboolean gst_d3d11_draw_quad_unlocked (GstD3D11Quad * quad,
                                       D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES],
                                       guint num_viewport,
                                       ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES],
                                       guint num_srv,
                                       ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES],
                                       guint num_rtv,
                                       ID3D11DepthStencilView *dsv);

G_END_DECLS

#endif /* __GST_D3D11_SHADER_H__ */
