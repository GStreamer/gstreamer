/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/gst.h>
#include "gstd3d12basefilter.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D12_BASE_CONVERT             (gst_d3d12_base_convert_get_type())
#define GST_D3D12_BASE_CONVERT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D12_BASE_CONVERT,GstD3D12BaseConvert))
#define GST_D3D12_BASE_CONVERT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_D3D12_BASE_CONVERT,GstD3D12BaseConvertClass))
#define GST_D3D12_BASE_CONVERT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D12_BASE_CONVERT,GstD3D12BaseConvertClass))
#define GST_IS_D3D12_BASE_CONVERT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D12_BASE_CONVERT))
#define GST_IS_D3D12_BASE_CONVERT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D12_BASE_CONVERT))

typedef struct _GstD3D12BaseConvert GstD3D12BaseConvert;
typedef struct _GstD3D12BaseConvertClass GstD3D12BaseConvertClass;

struct _GstD3D12BaseConvertClass
{
  GstD3D12BaseFilterClass parent_class;
};

GType gst_d3d12_base_convert_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstD3D12BaseConvert, gst_object_unref)

#define GST_TYPE_D3D12_CONVERT (gst_d3d12_convert_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Convert, gst_d3d12_convert,
    GST, D3D12_CONVERT, GstD3D12BaseConvert)

#define GST_TYPE_D3D12_COLOR_CONVERT (gst_d3d12_color_convert_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12ColorConvert, gst_d3d12_color_convert,
    GST, D3D12_COLOR_CONVERT, GstD3D12BaseConvert)

#define GST_TYPE_D3D12_SCALE (gst_d3d12_scale_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Scale, gst_d3d12_scale,
    GST, D3D12_SCALE, GstD3D12BaseConvert)

G_END_DECLS

