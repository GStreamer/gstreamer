/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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
#include <gst/hip/gsthip_fwd.h>

G_BEGIN_DECLS

typedef enum
{
  GST_HIP_FORMAT_FLAG_NONE = 0,
  GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D = (1 << 0),
} GstHipFormatFlags;

typedef struct
{
  GstVideoFormat format;
  GstHipFormatFlags format_flags;
  hipArray_Format array_format[GST_VIDEO_MAX_PLANES];
  guint channels[GST_VIDEO_MAX_PLANES];
} GstHipFormat;

GST_HIP_API
gboolean  gst_hip_device_get_format (GstHipDevice * device,
                                     GstVideoFormat format,
                                     GstHipFormat * hip_format);

G_END_DECLS

