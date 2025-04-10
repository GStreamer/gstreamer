/* GStreamer
 * Copyright (C) 2025 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@dmohub.org>
 *
 * gstanalyticssegmentationmtd.h
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

#ifndef __GST_ANALYTICS_IMAGE_UTIL_H__
#define __GST_ANALYTICS_IMAGE_UTIL_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

G_BEGIN_DECLS

GST_ANALYTICS_META_API
gfloat gst_analytics_image_util_iou_int (gint bb1_x, gint bb1_y, gint bb1_w,
    gint bb1_h, gint bb2_x, gint bb2_y, gint bb2_w, gint bb2_h);

GST_ANALYTICS_META_API
gfloat gst_analytics_image_util_iou_float (gfloat bb1_x, gfloat bb1_y, gfloat
    bb1_w, gfloat bb1_h, gfloat bb2_x, gfloat bb2_y, gfloat bb2_w, gfloat
    bb2_h);

G_END_DECLS

#endif /* __GST_ANALYTICS_IMAGE_UTIL_H__ */
