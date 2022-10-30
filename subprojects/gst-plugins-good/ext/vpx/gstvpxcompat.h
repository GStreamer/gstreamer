/*
 * GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

G_BEGIN_DECLS

#define GST_VPX_IMG_FMT_PLANAR 0x100
#define GST_VPX_IMG_FMT_UV_FLIP 0x200
#define GST_VPX_IMG_FMT_HIGHBITDEPTH 0x800

/* vpx_img_fmt with GST_ prefix */
typedef enum gst_vpx_img_fmt
{
  GST_VPX_IMG_FMT_NONE,
  GST_VPX_IMG_FMT_YV12 = GST_VPX_IMG_FMT_PLANAR | GST_VPX_IMG_FMT_UV_FLIP | 1,
  GST_VPX_IMG_FMT_I420 = GST_VPX_IMG_FMT_PLANAR | 2,
  GST_VPX_IMG_FMT_I422 = GST_VPX_IMG_FMT_PLANAR | 5,
  GST_VPX_IMG_FMT_I444 = GST_VPX_IMG_FMT_PLANAR | 6,
  GST_VPX_IMG_FMT_I440 = GST_VPX_IMG_FMT_PLANAR | 7,
  GST_VPX_IMG_FMT_NV12 = GST_VPX_IMG_FMT_PLANAR | 9,
  GST_VPX_IMG_FMT_I42016 = GST_VPX_IMG_FMT_I420 | GST_VPX_IMG_FMT_HIGHBITDEPTH,
  GST_VPX_IMG_FMT_I42216 = GST_VPX_IMG_FMT_I422 | GST_VPX_IMG_FMT_HIGHBITDEPTH,
  GST_VPX_IMG_FMT_I44416 = GST_VPX_IMG_FMT_I444 | GST_VPX_IMG_FMT_HIGHBITDEPTH,
  GST_VPX_IMG_FMT_I44016 = GST_VPX_IMG_FMT_I440 | GST_VPX_IMG_FMT_HIGHBITDEPTH
} gst_vpx_img_fmt_t;

G_END_DECLS
