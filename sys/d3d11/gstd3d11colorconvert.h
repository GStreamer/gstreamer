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

#ifndef __GST_D3D11_COLOR_CONVERT_H__
#define __GST_D3D11_COLOR_CONVERT_H__

#include <gst/gst.h>

#include "gstd3d11basefilter.h"
#include "gstd3d11colorconverter.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_COLOR_CONVERT             (gst_d3d11_color_convert_get_type())
#define GST_D3D11_COLOR_CONVERT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_COLOR_CONVERT,GstD3D11ColorConvert))
#define GST_D3D11_COLOR_CONVERT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_D3D11_COLOR_CONVERT,GstD3D11ColorConvertClass))
#define GST_D3D11_COLOR_CONVERT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D11_COLOR_CONVERT,GstD3D11ColorConvertClass))
#define GST_IS_D3D11_COLOR_CONVERT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_COLOR_CONVERT))
#define GST_IS_D3D11_COLOR_CONVERT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D11_COLOR_CONVERT))

struct _GstD3D11ColorConvert
{
  GstD3D11BaseFilter parent;

  const GstD3D11Format *in_d3d11_format;
  const GstD3D11Format *out_d3d11_format;

  ID3D11Texture2D *in_texture[GST_VIDEO_MAX_PLANES];
  ID3D11ShaderResourceView *shader_resource_view[GST_VIDEO_MAX_PLANES];
  guint num_input_view;

  ID3D11Texture2D *out_texture[GST_VIDEO_MAX_PLANES];
  ID3D11RenderTargetView *render_target_view[GST_VIDEO_MAX_PLANES];
  guint num_output_view;

  GstD3D11ColorConverter *converter;

  /* used for fallback texture copy */
  D3D11_BOX in_src_box;
  D3D11_BOX out_src_box;
};

struct _GstD3D11ColorConvertClass
{
  GstD3D11BaseFilterClass parent_class;
};

GType gst_d3d11_color_convert_get_type (void);

G_END_DECLS

#endif /* __GST_D3D11_COLOR_CONVERT_H__ */
