/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oravnas@cisco.com>
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

#ifndef __GST_CORE_MEDIA_BUFFER_H__
#define __GST_CORE_MEDIA_BUFFER_H__

#include <gst/gst.h>

#include "coremediactx.h"

G_BEGIN_DECLS

#define GST_TYPE_CORE_MEDIA_BUFFER (gst_core_media_buffer_get_type ())

#define GST_IS_CORE_MEDIA_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    GST_TYPE_CORE_MEDIA_BUFFER))
#define GST_CORE_MEDIA_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    GST_TYPE_CORE_MEDIA_BUFFER, GstCoreMediaBuffer))
#define GST_CORE_MEDIA_BUFFER_CAST(obj) ((GstCoreMediaBuffer *) (obj))

typedef struct _GstCoreMediaBuffer GstCoreMediaBuffer;
typedef struct _GstCoreMediaBufferClass GstCoreMediaBufferClass;

struct _GstCoreMediaBuffer
{
  GstBuffer buffer;

  GstCoreMediaCtx * ctx;
  CMSampleBufferRef sample_buf;
  CVImageBufferRef image_buf;
  CVPixelBufferRef pixel_buf;
  CMBlockBufferRef block_buf;
};

struct _GstCoreMediaBufferClass
{
  GstBufferClass parent_class;
};

GType       gst_core_media_buffer_get_type (void) G_GNUC_CONST;
GstBuffer * gst_core_media_buffer_new      (GstCoreMediaCtx * ctx,
                                            CMSampleBufferRef sample_buf);
CVPixelBufferRef gst_core_media_buffer_get_pixel_buffer
                                           (GstCoreMediaBuffer * buf);

G_END_DECLS

#endif /* __GST_CORE_MEDIA_BUFFER_H__ */
