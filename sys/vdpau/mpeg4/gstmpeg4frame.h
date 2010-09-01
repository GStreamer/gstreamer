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

#ifndef _GST_MPEG4_FRAME_H_
#define _GST_MPEG4_FRAME_H_

#include <gst/gst.h>

#include "../basevideodecoder/gstvideoframe.h"

#include "mpeg4util.h"

#define GST_TYPE_MPEG4_FRAME      (gst_mpeg4_frame_get_type())
#define GST_IS_MPEG4_FRAME(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPEG4_FRAME))
#define GST_MPEG4_FRAME(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPEG4_FRAME, GstMpeg4Frame))
#define GST_MPEG4_FRAME_CAST(obj) ((GstMpeg4Frame *)obj)

#define GST_MPEG4_FRAME_GOT_PRIMARY GST_VIDEO_FRAME_FLAG_LAST

typedef struct _GstMpeg4Frame GstMpeg4Frame;
typedef struct _GstMpeg4FrameClass GstMpeg4FrameClass;

struct _GstMpeg4Frame
{
  GstVideoFrame video_frame;

  GstBuffer *vos_buf;
  GstBuffer *vo_buf;
  GstBuffer *vol_buf;
  GstBuffer *gov_buf;
  GstBuffer *vop_buf;
  
  guint32 vop_time;
};

struct _GstMpeg4FrameClass
{
	GstVideoFrameClass video_frame_class;
};



GstMpeg4Frame *gst_mpeg4_frame_new (void);

GType gst_mpeg4_frame_get_type (void);

#endif