/* GStreamer
 * Copyright (C) 2025 Igalia, S.L.
 *               Author: Victor Jaquez <vjaquez@igalia.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsty4mformat.h"

gboolean
gst_y4m_video_unpadded_info (GstVideoInfo * y4m_info,
    const GstVideoInfo * vinfo)
{
  g_return_val_if_fail (y4m_info && vinfo, FALSE);

  gsize cr_h;
  guint width, height;
  GstVideoFormat format;
  GstVideoInfo out_info;

  format = GST_VIDEO_INFO_FORMAT (vinfo);
  width = GST_VIDEO_INFO_WIDTH (vinfo);
  height = GST_VIDEO_INFO_HEIGHT (vinfo);

  out_info = *vinfo;

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      out_info.stride[0] = width;
      out_info.stride[1] = GST_ROUND_UP_2 (width) / 2;
      out_info.stride[2] = out_info.stride[1];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      cr_h = GST_ROUND_UP_2 (height) / 2;
      if (GST_VIDEO_INFO_IS_INTERLACED (vinfo))
        cr_h = GST_ROUND_UP_2 (height);
      out_info.offset[2] = out_info.offset[1] + out_info.stride[1] * cr_h;
      out_info.size = out_info.offset[2] + out_info.stride[2] * cr_h;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      out_info.stride[0] = width;
      out_info.stride[1] = GST_ROUND_UP_2 (width) / 2;
      out_info.stride[2] = out_info.stride[1];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      out_info.offset[2] = out_info.offset[1] + out_info.stride[1] * height;
      /* simplification of ROUNDUP4(w)*h + 2*(ROUNDUP8(w)/2)*h */
      out_info.size = out_info.offset[2] + out_info.stride[2] * height;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      out_info.stride[0] = width;
      out_info.stride[1] = GST_ROUND_UP_2 (width) / 4;
      out_info.stride[2] = out_info.stride[1];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      out_info.offset[2] = out_info.offset[1] + out_info.stride[1] * height;
      /* simplification of ROUNDUP4(w)*h + 2*((ROUNDUP16(w)/4)*h */
      out_info.size = (width + (GST_ROUND_UP_2 (width) / 2)) * height;
      break;
    case GST_VIDEO_FORMAT_Y444:
      out_info.stride[0] = width;
      out_info.stride[1] = out_info.stride[0];
      out_info.stride[2] = out_info.stride[0];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      out_info.offset[2] = out_info.offset[1] * 2;
      out_info.size = out_info.stride[0] * height * 3;
      break;
    default:
      GST_FIXME ("%s is not supported", gst_video_format_to_string (format));
  }

  *y4m_info = out_info;

  return TRUE;
}
