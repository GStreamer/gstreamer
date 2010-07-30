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

#ifndef _GST_VDP_VIDEO_BUFFER_H_
#define _GST_VDP_VIDEO_BUFFER_H_

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstvdpbuffer.h"
#include "gstvdpdevice.h"

typedef struct _GstVdpVideoBuffer GstVdpVideoBuffer;

#define GST_TYPE_VDP_VIDEO_BUFFER (gst_vdp_video_buffer_get_type())

#define GST_IS_VDP_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_VIDEO_BUFFER))
#define GST_VDP_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_VIDEO_BUFFER, GstVdpVideoBuffer))

struct _GstVdpVideoBuffer {
  GstVdpBuffer vdp_buffer;

  GstVdpDevice *device;
  VdpVideoSurface surface;
};

typedef struct
{
  VdpChromaType chroma_type;
  VdpYCbCrFormat format;
  guint32 fourcc;
} GstVdpVideoBufferFormats;

static const VdpChromaType chroma_types[] =
    { VDP_CHROMA_TYPE_420, VDP_CHROMA_TYPE_422, VDP_CHROMA_TYPE_444 };

static const GstVdpVideoBufferFormats formats[] = {
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
        GST_MAKE_FOURCC ('Y', 'U', 'Y', '2')
      },
};

GType gst_vdp_video_buffer_get_type (void);

GstVdpVideoBuffer *gst_vdp_video_buffer_new (GstVdpDevice * device, VdpChromaType chroma_type, gint width, gint height, GError **error);

GstCaps *gst_vdp_video_buffer_get_caps (gboolean filter, VdpChromaType chroma_type);
GstCaps *gst_vdp_video_buffer_get_allowed_caps (GstVdpDevice * device);

gboolean gst_vdp_video_buffer_calculate_size (guint32 fourcc, gint width, gint height, guint *size);
gboolean gst_vdp_video_buffer_download (GstVdpVideoBuffer *inbuf, GstBuffer *outbuf, guint32 fourcc, gint width, gint height);
gboolean gst_vdp_video_buffer_upload (GstVdpVideoBuffer *video_buf, GstBuffer *src_buf, guint fourcc, gint width, gint height);

#define GST_VDP_VIDEO_CAPS \
  "video/x-vdpau-video, " \
  "chroma-type = (int)[0,2], " \
  "width = (int)[1,4096], " \
  "height = (int)[1,4096]"

#endif
