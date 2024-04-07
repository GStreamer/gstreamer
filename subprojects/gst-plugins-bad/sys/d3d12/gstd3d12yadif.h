/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_YADIF (gst_d3d12_yadif_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Yadif, gst_d3d12_yadif,
    GST, D3D12_YADIF, GstObject)

#define GST_D3D12_YADIF_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

typedef enum
{
  GST_D3D12_YADIF_FIELDS_ALL,
  GST_D3D12_YADIF_FIELDS_TOP,
  GST_D3D12_YADIF_FIELDS_BOTTOM,
} GstD3D12YadifFields;

GstD3D12Yadif * gst_d3d12_yadif_new (GstD3D12Device * device,
                                     const GstVideoInfo * info,
                                     gboolean use_compute);

void            gst_d3d12_yadif_set_fields (GstD3D12Yadif * yadif,
                                            GstD3D12YadifFields fields);

void            gst_d3d12_yadif_set_direction (GstD3D12Yadif * yadif,
                                               gboolean is_forward);

GstFlowReturn   gst_d3d12_yadif_push (GstD3D12Yadif * yadif,
                                      GstBuffer * buffer);

GstFlowReturn   gst_d3d12_yadif_pop (GstD3D12Yadif * yadif,
                                     GstBuffer ** buffer);

GstFlowReturn   gst_d3d12_yadif_drain (GstD3D12Yadif * yadif);

void            gst_d3d12_yadif_flush (GstD3D12Yadif * yadif);

G_END_DECLS
