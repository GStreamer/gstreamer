/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#define GST_TYPE_D3D12_WEAVE_INTERLACE (gst_d3d12_weave_interlace_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12WeaveInterlace, gst_d3d12_weave_interlace,
    GST, D3D12_WEAVE_INTERLACE, GstObject)

#define GST_D3D12_WEAVE_INTERLACE_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

typedef enum
{
  GST_D3D12_WEAVE_INTERLACE_PATTERN_1_1,
  GST_D3D12_WEAVE_INTERLACE_PATTERN_2_2,
} GstD3D12WeaveInterlacPattern;

GstD3D12WeaveInterlace * gst_d3d12_weave_interlace_new (GstD3D12Device * device,
                                                        const GstVideoInfo * info,
                                                        GstD3D12WeaveInterlacPattern pattern,
                                                        gboolean bff,
                                                        gboolean use_compute);

void            gst_d3d12_weave_interlace_set_direction (GstD3D12WeaveInterlace * interlace,
                                                         gboolean is_forward);

GstFlowReturn   gst_d3d12_weave_interlace_push (GstD3D12WeaveInterlace * interlace,
                                                GstBuffer * buffer);

GstFlowReturn   gst_d3d12_weave_interlace_pop (GstD3D12WeaveInterlace * interlace,
                                               GstBuffer ** buffer);

GstFlowReturn   gst_d3d12_weave_interlace_drain (GstD3D12WeaveInterlace * interlace);

void            gst_d3d12_weave_interlace_flush (GstD3D12WeaveInterlace * interlace);

G_END_DECLS
