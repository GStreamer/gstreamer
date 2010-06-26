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
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifndef _GST_H264_FRAME_H_
#define _GST_H264_FRAME_H_

#include <gst/gst.h>

#include "../basevideodecoder/gstvideoframe.h"

#include "gsth264parser.h"

#define GST_TYPE_H264_FRAME      (gst_h264_frame_get_type())
#define GST_IS_H264_FRAME(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_H264_FRAME))
#define GST_H264_FRAME(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_H264_FRAME, GstH264Frame))
#define GST_H264_FRAME_CAST(obj) ((GstH264Frame *)obj)

#define GST_H264_FRAME_GOT_PRIMARY GST_VIDEO_FRAME_FLAG_LAST

typedef struct _GstH264Frame GstH264Frame;
typedef struct _GstH264FrameClass GstH264FrameClass;

struct _GstH264Frame
{
  GstVideoFrame video_frame;

  GstH264Slice slice_hdr;
  GPtrArray *slices;

  guint poc;
  guint16 frame_idx;
  gboolean is_reference;
  gboolean is_long_term;
  gboolean output_needed;
};

struct _GstH264FrameClass
{
	GstVideoFrameClass video_frame_class;
};

void gst_h264_frame_add_slice (GstH264Frame *h264_frame, GstBuffer *buf);

GstH264Frame *gst_h264_frame_new (void);

GType gst_h264_frame_get_type (void);

#endif