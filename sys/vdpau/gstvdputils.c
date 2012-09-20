/*
 * gst-plugins-bad
 * Copyright (C) 2012 Edward Hervey <edward@collabora.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvdputils.h"

typedef struct
{
  VdpChromaType chroma_type;
  VdpYCbCrFormat format;
  GstVideoFormat vformat;
} GstVdpVideoBufferFormats;

static const GstVdpVideoBufferFormats yuv_formats[] = {
  {VDP_CHROMA_TYPE_420, VDP_YCBCR_FORMAT_YV12, GST_VIDEO_FORMAT_YV12},
  {VDP_CHROMA_TYPE_420, VDP_YCBCR_FORMAT_NV12, GST_VIDEO_FORMAT_NV12},
  {VDP_CHROMA_TYPE_422, VDP_YCBCR_FORMAT_UYVY, GST_VIDEO_FORMAT_UYVY},
  {VDP_CHROMA_TYPE_444, VDP_YCBCR_FORMAT_V8U8Y8A8, GST_VIDEO_FORMAT_AYUV},
  /* { */
  /* VDP_CHROMA_TYPE_444, */
  /* VDP_YCBCR_FORMAT_Y8U8V8A8, */
  /* GST_MAKE_FOURCC ('A', 'V', 'U', 'Y') */
  /* }, */
  {VDP_CHROMA_TYPE_422, VDP_YCBCR_FORMAT_YUYV, GST_VIDEO_FORMAT_YUY2}
};

VdpYCbCrFormat
gst_video_format_to_vdp_ycbcr (GstVideoFormat format)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (yuv_formats); i++) {
    if (yuv_formats[i].vformat == format)
      return yuv_formats[i].format;
  }

  return -1;
}

VdpChromaType
gst_video_info_to_vdp_chroma_type (GstVideoInfo * info)
{
  const GstVideoFormatInfo *finfo = info->finfo;
  VdpChromaType ret = -1;

  /* Check subsampling of second plane (first is always non-subsampled) */
  switch (GST_VIDEO_FORMAT_INFO_W_SUB (finfo, 1)) {
    case 0:
      /* Not subsampled in width for second plane */
      if (GST_VIDEO_FORMAT_INFO_W_SUB (finfo, 2))
        /* Not subsampled at all (4:4:4) */
        ret = VDP_CHROMA_TYPE_444;
      break;
    case 1:
      /* Subsampled horizontally once */
      if (GST_VIDEO_FORMAT_INFO_H_SUB (finfo, 2) == 0)
        /* Not subsampled vertically (4:2:2) */
        ret = VDP_CHROMA_TYPE_422;
      else if (GST_VIDEO_FORMAT_INFO_H_SUB (finfo, 2) == 1)
        /* Subsampled vertically once (4:2:0) */
        ret = VDP_CHROMA_TYPE_420;
      break;
    default:
      break;
  }

  return ret;
}
