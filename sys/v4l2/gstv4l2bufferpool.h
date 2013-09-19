/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *               2009 Texas Instruments, Inc - http://www.ti.com/
 *
 * gstv4l2bufferpool.h V4L2 buffer pool class
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_V4L2_BUFFER_POOL_H__
#define __GST_V4L2_BUFFER_POOL_H__

#include <gst/gst.h>

typedef struct _GstV4l2BufferPool GstV4l2BufferPool;
typedef struct _GstV4l2BufferPoolClass GstV4l2BufferPoolClass;
typedef struct _GstV4l2Meta GstV4l2Meta;

#include "gstv4l2object.h"

GST_DEBUG_CATEGORY_EXTERN (v4l2buffer_debug);

G_BEGIN_DECLS


#define GST_TYPE_V4L2_BUFFER_POOL      (gst_v4l2_buffer_pool_get_type())
#define GST_IS_V4L2_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_BUFFER_POOL))
#define GST_V4L2_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_BUFFER_POOL, GstV4l2BufferPool))
#define GST_V4L2_BUFFER_POOL_CAST(obj) ((GstV4l2BufferPool*)(obj))

struct _GstV4l2BufferPool
{
  GstBufferPool parent;

  GstV4l2Object *obj;        /* the v4l2 object */
  gint video_fd;             /* a dup(2) of the v4l2object's video_fd */

  GstAllocator *allocator;
  GstAllocationParams params;
  guint size;
  gboolean add_videometa;
  gboolean can_alloc;        /* if extra buffers can be allocated */

  guint num_buffers;         /* number of buffers we use */
  guint num_allocated;       /* number of buffers allocated by the driver */
  guint num_queued;          /* number of buffers queued in the driver */
  guint copy_threshold;      /* when our pool runs lower, start handing out copies */

  gboolean streaming;

  GstBuffer **buffers;
};

struct _GstV4l2BufferPoolClass
{
  GstBufferPoolClass parent_class;
};

struct _GstV4l2Meta {
  GstMeta meta;

  gpointer mem;
  struct v4l2_buffer vbuffer;
};

GType gst_v4l2_meta_api_get_type (void);
const GstMetaInfo * gst_v4l2_meta_get_info (void);
#define GST_V4L2_META_GET(buf) ((GstV4l2Meta *)gst_buffer_get_meta(buf,gst_v4l2_meta_api_get_type()))
#define GST_V4L2_META_ADD(buf) ((GstV4l2Meta *)gst_buffer_add_meta(buf,gst_v4l2_meta_get_info(),NULL))

GType gst_v4l2_buffer_pool_get_type (void);

GstBufferPool *     gst_v4l2_buffer_pool_new     (GstV4l2Object *obj, GstCaps *caps);

GstFlowReturn       gst_v4l2_buffer_pool_process (GstV4l2BufferPool * bpool, GstBuffer * buf);

G_END_DECLS

#endif /*__GST_V4L2_BUFFER_POOL_H__ */
