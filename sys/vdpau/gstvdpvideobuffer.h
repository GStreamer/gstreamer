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

#include "gstvdpdevice.h"

#include "gstvdpvideobuffer.h"

typedef struct _GstVdpVideoBuffer GstVdpVideoBuffer;

#define GST_TYPE_VDP_VIDEO_BUFFER (gst_vdp_video_buffer_get_type())

#define GST_IS_VDPAU_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_VIDEO_BUFFER))
#define GST_VDP_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_VIDEO_BUFFER, GstVdpVideoBuffer))

struct _GstVdpVideoBuffer {
  GstBuffer buffer;

  GstVdpDevice *device;
  VdpVideoSurface surface;

  GSList *refs;
};

GType gst_vdp_video_buffer_get_type (void);

GstVdpVideoBuffer* gst_vdp_video_buffer_new (GstVdpDevice * device, VdpChromaType chroma_type, gint width, gint height);

void gst_vdp_video_buffer_add_reference (GstVdpVideoBuffer *buffer, GstVdpVideoBuffer *buf);

#define GST_VDP_VIDEO_CAPS \
  "video/x-vdpau-video, " \
  "chroma-type = (int)[0,2], " \
  "width = (int)[1,4096], " \
  "height = (int)[1,4096]"

#endif
