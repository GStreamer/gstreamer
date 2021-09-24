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

#ifndef __GST_D3D11_COLOR_CONVERTER_H__
#define __GST_D3D11_COLOR_CONVERTER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11.h>

G_BEGIN_DECLS

typedef struct _GstD3D11Converter GstD3D11Converter;

/**
 * GST_D3D11_CONVERTER_OPT_ALPHA_VALUE
 *
 * #G_TYPE_FLOAT, the alpha value color value to use.
 * Default is 1.0
 */
#define GST_D3D11_CONVERTER_OPT_ALPHA_VALUE "GstD3D11Converter.alpha-value"

GstD3D11Converter * gst_d3d11_converter_new  (GstD3D11Device * device,
                                              GstVideoInfo * in_info,
                                              GstVideoInfo * out_info,
                                              GstStructure * config);

void                gst_d3d11_converter_free    (GstD3D11Converter * converter);

gboolean            gst_d3d11_converter_convert (GstD3D11Converter * converter,
                                                 ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES],
                                                 ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES],
                                                 ID3D11BlendState *blend,
                                                 gfloat blend_factor[4]);

gboolean            gst_d3d11_converter_convert_unlocked (GstD3D11Converter * converter,
                                                          ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES],
                                                          ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES],
                                                          ID3D11BlendState *blend,
                                                          gfloat blend_factor[4]);

gboolean            gst_d3d11_converter_update_viewport  (GstD3D11Converter * converter,
                                                          D3D11_VIEWPORT * viewport);

gboolean            gst_d3d11_converter_update_src_rect (GstD3D11Converter * converter,
                                                         RECT * src_rect);

gboolean            gst_d3d11_converter_update_dest_rect (GstD3D11Converter * converter,
                                                          RECT * dest_rect);

gboolean            gst_d3d11_converter_update_config    (GstD3D11Converter * converter,
                                                          GstStructure * config);

G_END_DECLS

#endif /* __GST_D3D11_COLOR_CONVERTER_H__ */
