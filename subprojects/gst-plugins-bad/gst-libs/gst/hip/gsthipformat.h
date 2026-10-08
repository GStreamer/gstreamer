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

#include <gst/hip/gsthip_fwd.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GstHipFormatSupport:
 * @GST_HIP_FORMAT_SUPPORT_NONE: No format support
 * @GST_HIP_FORMAT_SUPPORT_MEMORY: The format is supported by #GstHipMemory
 * @GST_HIP_FORMAT_SUPPORT_CONVERTER: The format is supported by #GstHipConverter
 *
 * Flags describing HIP format support.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_HIP_FORMAT_SUPPORT_NONE = 0,
  GST_HIP_FORMAT_SUPPORT_MEMORY = (1 << 0),
  GST_HIP_FORMAT_SUPPORT_CONVERTER = (1 << 1),
} GstHipFormatSupport;

GST_HIP_API
gboolean gst_hip_check_format_support (GstVideoFormat format,
                                       GstHipFormatSupport * support);

G_END_DECLS
