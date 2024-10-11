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

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <gst/cuda/gstcuda.h>
#include <stdint.h>
#include <nvcomp.h>

G_BEGIN_DECLS

enum GstNvCompMethod
{
  GST_NV_COMP_LZ4,
  GST_NV_COMP_SNAPPY,
  GST_NV_COMP_GDEFLATE,
  GST_NV_COMP_DEFLATE,
  GST_NV_COMP_ZSTD,
  GST_NV_COMP_CASCADED,
  GST_NV_COMP_BITCOMP,
  GST_NV_COMP_ANS,
  GST_NV_COMP_LAST,
};

#define GST_TYPE_NV_COMP_METHOD (gst_nv_comp_method_get_type())
GType gst_nv_comp_method_get_type ();

const gchar * gst_nv_comp_method_to_string (GstNvCompMethod method);

#define GST_NV_COMP_HEADER_VERSION 1
#define GST_NV_COMP_HEADER_MIN_SIZE (sizeof (guint32) * 6)

G_END_DECLS