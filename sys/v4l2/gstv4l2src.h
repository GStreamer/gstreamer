/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *
 * gstv4l2src.h: BT8x8/V4L2 source element
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

#ifndef __GST_V4L2SRC_H__
#define __GST_V4L2SRC_H__

#include <gstv4l2object.h>

GST_DEBUG_CATEGORY_EXTERN (v4l2src_debug);

#define GST_V4L2_MAX_BUFFERS 16
#define GST_V4L2_MIN_BUFFERS 2
#define GST_V4L2_MAX_SIZE (1<<15) /* 2^15 == 32768 */

G_BEGIN_DECLS

#define GST_TYPE_V4L2SRC \
  (gst_v4l2src_get_type())
#define GST_V4L2SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2SRC,GstV4l2Src))
#define GST_V4L2SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2SRC,GstV4l2SrcClass))
#define GST_IS_V4L2SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2SRC))
#define GST_IS_V4L2SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2SRC))

typedef struct _GstV4l2BufferPool GstV4l2BufferPool;
typedef struct _GstV4l2Buffer GstV4l2Buffer;
typedef struct _GstV4l2Src GstV4l2Src;
typedef struct _GstV4l2SrcClass GstV4l2SrcClass;


/* global info */
struct _GstV4l2BufferPool
{
  GstMiniObject parent;

  GMutex *lock;
  gboolean running; /* with lock */
  gint num_live_buffers; /* with lock */
  gint video_fd; /* a dup(2) of the v4l2object's video_fd */
  guint buffer_count;
  GstV4l2Buffer **buffers; /* with lock; buffers[n] is NULL that buffer has been
                            * dequeued and pushed out */
};

struct _GstV4l2Buffer {
  GstBuffer   buffer;

  struct v4l2_buffer vbuffer;

  /* FIXME: have GstV4l2Src* instead, as this has GstV4l2BufferPool* */
  GstV4l2BufferPool *pool;
};

/**
 * GstV4l2Src:
 * @pushsrc: parent #GstPushSrc.
 *
 * Opaque object.
 */
struct _GstV4l2Src
{
  GstPushSrc pushsrc;

  /*< private >*/
  GstV4l2Object * v4l2object;

  /* pads */
  GstPad *srcpad;

  GstCaps *probed_caps;

  /* internal lists */
  GSList *formats;              /* list of available capture formats */

  /* buffers */
  GstV4l2BufferPool *pool;

  guint32 num_buffers;
  gboolean use_mmap;
  guint32 frame_byte_size;

  /* if the buffer will be or not used from directly mmap */
  gboolean always_copy;

  /* True if we want to stop */
  gboolean quit;
  gboolean is_capturing;

  guint64 offset;

  gint     fps_d, fps_n;       /* framerate if device is open */
};

struct _GstV4l2SrcClass
{
  GstPushSrcClass parent_class;
  
  GList *v4l2_class_devices;
};

GType gst_v4l2src_get_type (void);

G_END_DECLS

#endif /* __GST_V4L2SRC_H__ */
