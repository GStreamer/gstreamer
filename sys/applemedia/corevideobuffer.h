/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
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

#ifndef __GST_CORE_VIDEO_BUFFER_H__
#define __GST_CORE_VIDEO_BUFFER_H__

#include <gst/gst.h>

#include "coremediactx.h"

G_BEGIN_DECLS

#define GST_TYPE_CORE_VIDEO_BUFFER (gst_core_video_buffer_get_type ())

#define GST_IS_CORE_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    GST_TYPE_CORE_VIDEO_BUFFER))
#define GST_CORE_VIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    GST_TYPE_CORE_VIDEO_BUFFER, GstCoreVideoBuffer))
#define GST_CORE_VIDEO_BUFFER_CAST(obj) ((GstCoreVideoBuffer *) (obj))

typedef struct _GstCoreVideoBuffer GstCoreVideoBuffer;
typedef struct _GstCoreVideoBufferClass GstCoreVideoBufferClass;

struct _GstCoreVideoBuffer
{
  GstBuffer buffer;

  GstCoreMediaCtx *ctx;
  CVBufferRef cvbuf;
  CVPixelBufferRef pixbuf;
};

struct _GstCoreVideoBufferClass
{
  GstBufferClass parent_class;
};

GType       gst_core_video_buffer_get_type (void) G_GNUC_CONST;
GstBuffer * gst_core_video_buffer_new      (GstCoreMediaCtx * ctx,
                                            CVBufferRef cvbuf);

G_END_DECLS

#endif /* __GST_CORE_VIDEO_BUFFER_H__ */
