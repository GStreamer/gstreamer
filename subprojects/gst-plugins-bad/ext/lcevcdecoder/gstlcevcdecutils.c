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

#include "gstlcevcdecutils.h"

LCEVC_ColorFormat
gst_lcevc_dec_utils_get_color_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      return LCEVC_I420_8;
    case GST_VIDEO_FORMAT_I420_10LE:
      return LCEVC_I420_10_LE;
    case GST_VIDEO_FORMAT_I420_12LE:
      return LCEVC_I420_12_LE;

    case GST_VIDEO_FORMAT_Y42B:
      return LCEVC_I422_8;
    case GST_VIDEO_FORMAT_I422_10LE:
      return LCEVC_I422_10_LE;
    case GST_VIDEO_FORMAT_I422_12LE:
      return LCEVC_I422_12_LE;

    case GST_VIDEO_FORMAT_Y444:
      return LCEVC_I444_8;
    case GST_VIDEO_FORMAT_Y444_10LE:
      return LCEVC_I444_10_LE;
    case GST_VIDEO_FORMAT_Y444_12LE:
      return LCEVC_I444_12_LE;

    case GST_VIDEO_FORMAT_NV12:
      return LCEVC_NV12_8;
    case GST_VIDEO_FORMAT_NV21:
      return LCEVC_NV21_8;

    case GST_VIDEO_FORMAT_RGB:
      return LCEVC_RGB_8;
    case GST_VIDEO_FORMAT_BGR:
      return LCEVC_BGR_8;
    case GST_VIDEO_FORMAT_RGBA:
      return LCEVC_RGBA_8;
    case GST_VIDEO_FORMAT_BGRA:
      return LCEVC_BGRA_8;
    case GST_VIDEO_FORMAT_ARGB:
      return LCEVC_ARGB_8;
    case GST_VIDEO_FORMAT_ABGR:
      return LCEVC_ABGR_8;

    case GST_VIDEO_FORMAT_GRAY8:
      return LCEVC_GRAY_8;
    case GST_VIDEO_FORMAT_GRAY16_LE:
      return LCEVC_GRAY_16_LE;

    default:
      break;
  }

  return LCEVC_ColorFormat_Unknown;
}

gboolean
gst_lcevc_dec_utils_alloc_picture_handle (LCEVC_DecoderHandle decoder_handle,
    GstVideoFrame * frame, LCEVC_PictureHandle * picture_handle,
    LCEVC_Access access)
{
  LCEVC_PictureDesc picture_desc = { 0, };
  LCEVC_PictureBufferDesc buffer_desc = { 0, };
  LCEVC_PicturePlaneDesc plane_desc[GST_VIDEO_MAX_PLANES] = { 0, };
  LCEVC_ColorFormat fmt;
  guint i;

  fmt = gst_lcevc_dec_utils_get_color_format (GST_VIDEO_FRAME_FORMAT (frame));
  if (fmt == LCEVC_ColorFormat_Unknown)
    return FALSE;

  /* Set LCEVC Picture Description */
  if (LCEVC_DefaultPictureDesc (&picture_desc, fmt,
          GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame))
      != LCEVC_Success)
    return FALSE;
  picture_desc.sampleAspectRatioNum = GST_VIDEO_INFO_PAR_N (&frame->info);
  picture_desc.sampleAspectRatioDen = GST_VIDEO_INFO_PAR_D (&frame->info);

  /* Set buffer description */
  buffer_desc.data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  buffer_desc.byteSize = GST_VIDEO_FRAME_SIZE (frame);
  buffer_desc.access = access;

  /* Set plane description */
  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
    plane_desc[i].firstSample = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
    plane_desc[i].rowByteStride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);
  }

  /* Allocate LCEVC Picture */
  if (LCEVC_AllocPictureExternal (decoder_handle, &picture_desc, &buffer_desc,
          plane_desc, picture_handle) != LCEVC_Success)
    return FALSE;

  return TRUE;
}
