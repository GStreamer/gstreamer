/* G-Streamer Video4linux2 video-capture plugin
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <gstv4l2element.h>

GST_DEBUG_CATEGORY_EXTERN (v4l2src_debug);

#define GST_V4L2_MAX_BUFFERS 16
#define GST_V4L2_MIN_BUFFERS 2

#define GST_TYPE_V4L2SRC \
		(gst_v4l2src_get_type())
#define GST_V4L2SRC(obj) \
		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2SRC,GstV4l2Src))
#define GST_V4L2SRC_CLASS(klass) \
		(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2SRC,GstV4l2SrcClass))
#define GST_IS_V4L2SRC(obj) \
		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2SRC))
#define GST_IS_V4L2SRC_CLASS(obj) \
		(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2SRC))

typedef struct _GstV4l2BufferPool GstV4l2BufferPool;
typedef struct _GstV4l2Buffer GstV4l2Buffer;
typedef struct _GstV4l2Src GstV4l2Src;
typedef struct _GstV4l2SrcClass GstV4l2SrcClass;

/* global info */
struct _GstV4l2BufferPool
{
  GstAtomicInt refcount;	/* number of users: 1 for every buffer, 1 for element */
  gint video_fd;
  guint buffer_count;
  GstV4l2Buffer *buffers;
};

struct _GstV4l2Buffer
{
  struct v4l2_buffer buffer;
  guint8 *start;
  guint length;
  GstAtomicInt refcount;	/* add 1 if in use by element, add 1 if in use by GstBuffer */
  GstV4l2BufferPool *pool;
};

struct _GstV4l2Src
{
  GstV4l2Element v4l2element;

  /* pads */
  GstPad *srcpad;

  /* internal lists */
  GSList *formats;		/* list of available capture formats */

  /* buffers */
  GstV4l2BufferPool *pool;

  struct v4l2_requestbuffers breq;
  struct v4l2_format format;

  /* True if we want to stop */
  gboolean quit;

  /* A/V sync... frame counter and internal cache */
  gulong handled;
  gint need_writes;
  GstBuffer *cached_buffer;
  gulong last_seq;

  /* clock */
  GstClock *clock;

  /* time to substract from clock time to get back to timestamp */
  GstClockTime substract_time;

  /* how are we going to push buffers? */
  gboolean use_fixed_fps;
};

struct _GstV4l2SrcClass
{
  GstV4l2ElementClass parent_class;

  void (*frame_capture) (GObject * object);
  void (*frame_drop) (GObject * object);
  void (*frame_insert) (GObject * object);
  void (*frame_lost) (GObject * object, gint num_lost);
};


GType gst_v4l2src_get_type (void);

#endif /* __GST_V4L2SRC_H__ */
