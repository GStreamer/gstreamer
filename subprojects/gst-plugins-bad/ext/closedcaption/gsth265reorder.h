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

G_BEGIN_DECLS

#define GST_TYPE_H265_REORDER (gst_h265_reorder_get_type())
G_DECLARE_FINAL_TYPE (GstH265Reorder, gst_h265_reorder,
    GST, H265_REORDER, GstObject);

GstH265Reorder * gst_h265_reorder_new (gboolean need_reorder);

gboolean gst_h265_reorder_set_caps (GstH265Reorder * reorder,
                                    GstCaps * caps,
                                    GstClockTime * latency);

gboolean gst_h265_reorder_push (GstH265Reorder * reorder,
                                GstVideoCodecFrame * frame,
                                GstClockTime * latency);

GstVideoCodecFrame * gst_h265_reorder_pop (GstH265Reorder * reorder);

void     gst_h265_reorder_drain (GstH265Reorder * reorder);

guint    gst_h265_reorder_get_num_buffered (GstH265Reorder * reorder);

GstBuffer * gst_h265_reorder_insert_sei (GstH265Reorder * reorder,
                                         GstBuffer * au,
                                         GArray * sei);

G_END_DECLS
