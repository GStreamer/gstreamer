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

#define GST_TYPE_H264_REORDER (gst_h264_reorder_get_type())
G_DECLARE_FINAL_TYPE (GstH264Reorder, gst_h264_reorder,
    GST, H264_REORDER, GstObject);

GstH264Reorder * gst_h264_reorder_new (gboolean need_reorder);

gboolean gst_h264_reorder_set_caps (GstH264Reorder * reorder,
                                    GstCaps * caps,
                                    GstClockTime * latency);

gboolean gst_h264_reorder_push (GstH264Reorder * reorder,
                                GstVideoCodecFrame * frame,
                                GstClockTime * latency);

GstVideoCodecFrame * gst_h264_reorder_pop (GstH264Reorder * reorder);

void     gst_h264_reorder_drain (GstH264Reorder * reorder);

guint    gst_h264_reorder_get_num_buffered (GstH264Reorder * reorder);

GstBuffer * gst_h264_reorder_insert_sei (GstH264Reorder * reorder,
                                         GstBuffer * au,
                                         GArray * sei);

gboolean gst_h264_reorder_is_cea708_sei (guint8 country_code,
                                         const guint8 * data,
                                         gsize size);

G_END_DECLS
