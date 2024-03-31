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
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

struct _GstD3D12Format
{
  GstVideoFormat format;

  /* direct mapping to dxgi format if applicable */
  DXGI_FORMAT dxgi_format;

  /* formats for texture processing */
  DXGI_FORMAT resource_format[GST_VIDEO_MAX_PLANES];

  /* extra format used for unordered access view (unused) */
  DXGI_FORMAT uav_format[GST_VIDEO_MAX_PLANES];

  /* D3D12_FORMAT_SUPPORT1 flags */
  guint format_support1[GST_VIDEO_MAX_PLANES];

  /* D3D12_FORMAT_SUPPORT2 flags (unused) */
  guint format_support2[GST_VIDEO_MAX_PLANES];

  /*< private >*/
  guint padding[GST_PADDING_LARGE];
};

GST_D3D12_API
GstVideoFormat  gst_d3d12_dxgi_format_to_gst        (DXGI_FORMAT format);

G_END_DECLS

