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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_V4L2_BUFFER_POOL_H__
#define __GST_V4L2_BUFFER_POOL_H__

#include <gst/gst.h>

typedef struct _GstV4l2BufferPool GstV4l2BufferPool;
typedef struct _GstV4l2BufferPoolClass GstV4l2BufferPoolClass;
typedef struct _GstMetaV4l2 GstMetaV4l2;

#include "gstv4l2object.h"
//#include "v4l2_calls.h"

GST_DEBUG_CATEGORY_EXTERN (v4l2buffer_debug);

G_BEGIN_DECLS


#define GST_TYPE_V4L2_BUFFER_POOL (gst_v4l2_buffer_pool_get_type())
#define GST_IS_V4L2_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_BUFFER_POOL))
#define GST_V4L2_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_BUFFER_POOL, GstV4l2BufferPool))

struct _GstV4l2BufferPool
{
  GstBufferPool parent;

  GstV4l2Object *obj;        /* the v4l2 object */
  gint video_fd;             /* a dup(2) of the v4l2object's video_fd */

  GstAllocator *allocator;
  guint size;
  guint min_buffers;
  guint max_buffers;
  guint prefix;
  guint align;
  gboolean add_videometa;

  guint num_allocated;       /* number of buffers allocated by the driver */
  guint num_queued;          /* number of buffers queued in the driver */

  gboolean streaming;

  GstBuffer **buffers;
};

struct _GstV4l2BufferPoolClass
{
  GstBufferPoolClass parent_class;
};

struct _GstMetaV4l2 {
  GstMeta meta;

  gpointer mem;
  struct v4l2_buffer vbuffer;
};

const GstMetaInfo * gst_meta_v4l2_get_info (void);
#define GST_META_V4L2_GET(buf) ((GstMetaV4l2 *)gst_buffer_get_meta(buf,gst_meta_v4l2_get_info()))
#define GST_META_V4L2_ADD(buf) ((GstMetaV4l2 *)gst_buffer_add_meta(buf,gst_meta_v4l2_get_info(),NULL))

GType gst_v4l2_buffer_pool_get_type (void);

GstBufferPool *     gst_v4l2_buffer_pool_new     (GstV4l2Object *obj);

GstFlowReturn       gst_v4l2_buffer_pool_process (GstBufferPool * bpool, GstBuffer * buf);

G_END_DECLS

#endif /*__GST_V4L2_BUFFER_POOL_H__ */
