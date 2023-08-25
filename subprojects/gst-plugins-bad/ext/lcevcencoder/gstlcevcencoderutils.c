/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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

#include "gstlcevcencoderutils.h"

EILColourFormat
gst_lcevc_encoder_utils_get_color_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      return EIL_YUV_420P;
    case GST_VIDEO_FORMAT_I420_10LE:
      return EIL_YUV_420P10;
    case GST_VIDEO_FORMAT_Y42B:
      return EIL_YUV_422P;
    case GST_VIDEO_FORMAT_I422_10LE:
      return EIL_YUV_422P10;
    case GST_VIDEO_FORMAT_Y444:
      return EIL_YUV_444P;
    case GST_VIDEO_FORMAT_Y444_10LE:
      return EIL_YUV_444P10;
    case GST_VIDEO_FORMAT_RGB:
      return EIL_RGB_24;
    case GST_VIDEO_FORMAT_BGR:
      return EIL_BGR_24;
    case GST_VIDEO_FORMAT_RGBA:
      return EIL_RGBA_32;
    case GST_VIDEO_FORMAT_BGRA:
      return EIL_BGRA_32;
    case GST_VIDEO_FORMAT_ARGB:
      return EIL_ARGB_32;
    case GST_VIDEO_FORMAT_ABGR:
      return EIL_ABGR_32;
    default:
      break;
  }

  return -1;
}

gboolean
gst_lcevc_encoder_utils_init_eil_picture (EILFrameType frame_type,
    GstVideoFrame * frame, GstClockTime pts, EILPicture * picture)
{
  picture->memory_type = EIL_MT_Host;
  picture->num_planes = GST_VIDEO_FRAME_N_PLANES (frame);

  if (picture->num_planes > EIL_MaxPlanes)
    return FALSE;

  for (guint i = 0; i < picture->num_planes; i++) {
    picture->plane[i] = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
    picture->stride[i] = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);
    picture->offset[i] = GST_VIDEO_FRAME_PLANE_OFFSET (frame, i);
  }

  picture->base_type = EIL_BT_Unknown;
  picture->frame_type = frame_type;

  switch (frame->info.ABI.abi.field_order) {
    case GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST:
      picture->field_type = EIL_FieldType_Top;
      break;
    case GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST:
      picture->field_type = EIL_FieldType_Bottom;
      break;
    default:
      picture->field_type = EIL_FieldType_None;
      break;
  }

  picture->pts = pts;

  return TRUE;
}
