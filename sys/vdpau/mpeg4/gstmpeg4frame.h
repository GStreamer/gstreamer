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
* Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
* Boston, MA 02110-1301, USA.
*/

#ifndef _GST_MPEG4_FRAME_H_
#define _GST_MPEG4_FRAME_H_

#include <gst/gst.h>

#include "mpeg4util.h"

#define GST_MPEG4_FRAME_GOT_PRIMARY GST_VIDEO_FRAME_FLAG_LAST

typedef struct _GstMpeg4Frame GstMpeg4Frame;

struct _GstMpeg4Frame
{
  GstBuffer *vos_buf;
  GstBuffer *vo_buf;
  GstBuffer *vol_buf;
  GstBuffer *gov_buf;
  GstBuffer *vop_buf;
  
  guint32 vop_time;
};

GstMpeg4Frame *gst_mpeg4_frame_new (void);

#endif
