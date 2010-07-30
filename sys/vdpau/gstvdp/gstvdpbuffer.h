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

#ifndef _GST_VDP_BUFFER_H_
#define _GST_VDP_BUFFER_H_

#include <gst/gst.h>

typedef struct _GstVdpBuffer GstVdpBuffer;

#include "gstvdpbufferpool.h"

#define GST_TYPE_VDP_BUFFER (gst_vdp_buffer_get_type())

#define GST_IS_VDP_BUFFER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_BUFFER))
#define GST_VDP_BUFFER(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_BUFFER, GstVdpBuffer))
#define GST_VDP_BUFFER_CAST(obj) ((GstVdpBuffer *)obj)

struct _GstVdpBuffer {
  GstBuffer buffer;

  GstVdpBufferPool *bpool;
};

void gst_vdp_buffer_set_buffer_pool (GstVdpBuffer *buffer, GstVdpBufferPool *bpool);
gboolean gst_vdp_buffer_revive (GstVdpBuffer * buffer);

static inline GstVdpBuffer *
gst_vdp_buffer_ref (GstVdpBuffer *buffer)
{
  return (GstVdpBuffer *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (buffer));
}

static inline void
gst_vdp_buffer_unref (GstVdpBuffer *buffer)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (buffer));
}

GType gst_vdp_buffer_get_type (void);

#endif