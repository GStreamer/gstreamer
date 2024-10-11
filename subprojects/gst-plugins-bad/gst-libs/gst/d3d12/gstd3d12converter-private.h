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

GST_D3D12_API
gboolean            gst_d3d12_converter_apply_transform (GstD3D12Converter * converter,
                                                         GstVideoOrientationMethod orientation,
                                                         gfloat viewport_width,
                                                         gfloat viewport_height,
                                                         gfloat fov,
                                                         gboolean ortho,
                                                         gfloat rotation_x,
                                                         gfloat rotation_y,
                                                         gfloat rotation_z,
                                                         gfloat scale_x,
                                                         gfloat scale_y);

G_END_DECLS
