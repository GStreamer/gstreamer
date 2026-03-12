/* GStreamer
  * Copyright (C) 2026 Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
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

#define GST_TYPE_H266_REORDER (gst_h266_reorder_get_type())
G_DECLARE_FINAL_TYPE (GstH266Reorder, gst_h266_reorder,
    GST, H266_REORDER, GstObject);

GstH266Reorder * gst_h266_reorder_new (gboolean need_reorder);

gboolean gst_h266_reorder_set_caps (GstH266Reorder * reorder,
                                    GstCaps * caps,
                                    GstClockTime * latency);

gboolean gst_h266_reorder_push (GstH266Reorder * reorder,
                                GstVideoCodecFrame * frame,
                                GstClockTime * latency);

GstVideoCodecFrame * gst_h266_reorder_pop (GstH266Reorder * reorder);

void     gst_h266_reorder_drain (GstH266Reorder * reorder);

guint    gst_h266_reorder_get_num_buffered (GstH266Reorder * reorder);

GstBuffer * gst_h266_reorder_insert_sei (GstH266Reorder * reorder,
                                         GstBuffer * au,
                                         GArray * sei);

G_END_DECLS
