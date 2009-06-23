/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef _GST_VDP_UTILS_H_
#define _GST_VDP_UTILS_H_

#include <gst/gst.h>

#include "gstvdpdevice.h"

typedef struct
{
  VdpChromaType chroma_type;
  VdpYCbCrFormat format;
  guint32 fourcc;
} VdpauFormats;

#define N_CHROMA_TYPES 3
#define N_FORMATS 7

static const VdpChromaType chroma_types[N_CHROMA_TYPES] =
    { VDP_CHROMA_TYPE_420, VDP_CHROMA_TYPE_422, VDP_CHROMA_TYPE_444 };

static const VdpauFormats formats[N_FORMATS] = {
  {
        VDP_CHROMA_TYPE_420,
        VDP_YCBCR_FORMAT_YV12,
        GST_MAKE_FOURCC ('I', '4', '2', '0')
      },
  {
        VDP_CHROMA_TYPE_420,
        VDP_YCBCR_FORMAT_YV12,
        GST_MAKE_FOURCC ('Y', 'V', '1', '2')
      },
  {
        VDP_CHROMA_TYPE_420,
        VDP_YCBCR_FORMAT_NV12,
        GST_MAKE_FOURCC ('N', 'V', '1', '2')
      },
  {
        VDP_CHROMA_TYPE_422,
        VDP_YCBCR_FORMAT_UYVY,
        GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y')
      },
  {
        VDP_CHROMA_TYPE_444,
        VDP_YCBCR_FORMAT_V8U8Y8A8,
        GST_MAKE_FOURCC ('A', 'Y', 'U', 'V')
      },
  {
        VDP_CHROMA_TYPE_444,
        VDP_YCBCR_FORMAT_Y8U8V8A8,
        GST_MAKE_FOURCC ('A', 'V', 'U', 'Y')
      },
  {
        VDP_CHROMA_TYPE_422,
        VDP_YCBCR_FORMAT_YUYV,
        GST_MAKE_FOURCC ('Y', 'U', 'Y', 'V')
      }
};

GstCaps *gst_vdp_video_to_yuv_caps (GstCaps *caps);
GstCaps *gst_vdp_yuv_to_video_caps (GstCaps *caps, GstVdpDevice *device);

#endif /* _GST_VDP_UTILS_H_ */