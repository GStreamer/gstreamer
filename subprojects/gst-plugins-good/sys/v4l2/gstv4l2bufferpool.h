/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
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
#include "gstv4l2allocator.h"

G_BEGIN_DECLS

#define GST_TYPE_V4L2_BUFFER_POOL      (gst_v4l2_buffer_pool_get_type())
#define GST_IS_V4L2_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_BUFFER_POOL))
#define GST_V4L2_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_BUFFER_POOL, GstV4l2BufferPool))
#define GST_V4L2_BUFFER_POOL_CAST(obj) ((GstV4l2BufferPool*)(obj))

/* Returns true if the pool is streaming. Must be called with stream lock
 * held. */
#define GST_V4L2_BUFFER_POOL_IS_STREAMING(obj) (GST_V4L2_BUFFER_POOL (obj)->streaming)

/* This flow return is used to indicate that the last buffer has been dequeued
 * during draining. This should normally only occur for mem-2-mem devices. */
#define GST_V4L2_FLOW_LAST_BUFFER GST_FLOW_CUSTOM_SUCCESS

/* This flow return is used to indicated that the returned buffer was marked
 * with the error flag and had no payload. This error should be recovered by
 * simply waiting for next buffer. */
#define GST_V4L2_FLOW_CORRUPTED_BUFFER GST_FLOW_CUSTOM_SUCCESS_1

/* This flow return is used to indicate that a SOURCE_CHANGE event with the
 * resolution change flag set was received. */
#define GST_V4L2_FLOW_RESOLUTION_CHANGE GST_FLOW_CUSTOM_SUCCESS_2

struct _GstV4l2BufferPool
{
  GstBufferPool parent;

  GstV4l2Object *obj;        /* the v4l2 object */
  gint video_fd;             /* a dup(2) of the v4l2object's video_fd */

  gboolean empty;
  GCond empty_cond;

  gboolean orphaned;

  GstV4l2Allocator *vallocator;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstBufferPool *other_pool;
  guint size;
  GstVideoInfoDmaDrm caps_info;   /* Default video information */
  gboolean have_dma_drm_caps; /* If the configured caps have memory:DMABuf */

  gboolean add_videometa;    /* set if video meta should be added */
  gboolean enable_copy_threshold; /* If copy_threshold should be set */

  guint min_latency;         /* number of buffers we will hold */
  guint max_latency;         /* number of buffers we can hold */
  guint num_queued;          /* number of buffers queued in the driver */
  guint num_allocated;       /* number of buffers allocated */
  guint copy_threshold;      /* when our pool runs lower, start handing out copies */

  gboolean streaming;
  gboolean flushing;

  GstBuffer *buffers[VIDEO_MAX_FRAME];
  volatile gint buffer_state[VIDEO_MAX_FRAME];

  /* signal handlers */
  gulong group_released_handler;

  /* Control to warn only once on buggy feild driver bug */
  gboolean has_warned_on_buggy_field;
};

struct _GstV4l2BufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_v4l2_buffer_pool_get_type (void);

GstBufferPool *     gst_v4l2_buffer_pool_new     (GstV4l2Object *obj, GstCaps *caps);

GstFlowReturn       gst_v4l2_buffer_pool_process (GstV4l2BufferPool * bpool, GstBuffer ** buf, guint32 * frame_number);

void                gst_v4l2_buffer_pool_set_other_pool (GstV4l2BufferPool * pool,
                                                         GstBufferPool * other_pool);
void                gst_v4l2_buffer_pool_copy_at_threshold (GstV4l2BufferPool * pool,
                                                            gboolean copy);

GstFlowReturn       gst_v4l2_buffer_pool_flush   (GstV4l2Object * v4l2object);

gboolean            gst_v4l2_buffer_pool_orphan  (GstV4l2Object * v4l2object);

void                gst_v4l2_buffer_pool_enable_resolution_change (GstV4l2BufferPool *self);

G_END_DECLS

#endif /*__GST_V4L2_BUFFER_POOL_H__ */
