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

#ifndef _GST_VDP_MPEG_FRAME_H_
#define _GST_VDP_MPEG_FRAME_H_

#include <gst/gst.h>

#include <vdpau/vdpau.h>

#include "../basevideodecoder/gstvideoframe.h"

#define GST_TYPE_VDP_MPEG_FRAME      (gst_vdp_mpeg_frame_get_type())
#define GST_IS_VDP_MPEG_FRAME(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_MPEG_FRAME))
#define GST_VDP_MPEG_FRAME(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_MPEG_FRAME, GstVdpMpegFrame))
#define GST_VDP_MPEG_FRAME_CAST(obj) ((GstVdpMpegFrame *)obj)

typedef struct _GstVdpMpegStreamInfo GstVdpMpegStreamInfo;

struct _GstVdpMpegStreamInfo
{
	gint width, height;
	gint fps_n, fps_d;
	gint par_n, par_d;
	gboolean interlaced;
	gint version;
	VdpDecoderProfile profile;
};

typedef struct _GstVdpMpegFrame GstVdpMpegFrame;
typedef struct _GstVdpMpegFrameClass GstVdpMpegFrameClass;

struct _GstVdpMpegFrame
{
  GstVideoFrame video_frame;

	GstBuffer *seq;
	GstBuffer *seq_ext;

	GstBuffer *pic;
	GstBuffer *pic_ext;

	GstBuffer *gop;
	GstBuffer *qm_ext;

	gint n_slices;
	GstBuffer *slices;
};

struct _GstVdpMpegFrameClass
{
	GstVideoFrameClass video_frame_class;
};

void gst_vdp_mpeg_frame_add_slice (GstVdpMpegFrame *mpeg_frame, GstBuffer *buf);

GstVdpMpegFrame *gst_vdp_mpeg_frame_new (void);

GType gst_vdp_mpeg_frame_get_type (void);

#endif