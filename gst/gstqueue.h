/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstqueue.h:
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


#ifndef __GST_QUEUE_H__
#define __GST_QUEUE_H__


#include <gst/gstelement.h>

G_BEGIN_DECLS

#define GST_TYPE_QUEUE \
  (gst_queue_get_type())
#define GST_QUEUE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QUEUE,GstQueue))
#define GST_QUEUE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QUEUE,GstQueueClass))
#define GST_IS_QUEUE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QUEUE))
#define GST_IS_QUEUE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QUEUE))

enum {
  GST_QUEUE_NO_LEAK		= 0,
  GST_QUEUE_LEAK_UPSTREAM	= 1,
  GST_QUEUE_LEAK_DOWNSTREAM	= 2
};

typedef struct _GstQueue GstQueue;
typedef struct _GstQueueSize GstQueueSize;
typedef struct _GstQueueClass GstQueueClass;

/**
 * GstQueueSize:
 * @buffers: number of buffers
 * @bytes: number of bytes
 * @time: amount of time
 *
 * Structure describing the size of a queue.
 */
struct _GstQueueSize {
    guint   buffers;
    guint   bytes;
    guint64 time;
};

/**
 * GstQueue:
 *
 * Opaque #GstQueue structure.
 */
struct _GstQueue {
  GstElement element;

  /*< private >*/
  GstPad *sinkpad;
  GstPad *srcpad;

  /* flowreturn when srcpad is paused */
  GstFlowReturn srcresult;

  /* the queue of data we're keeping our grubby hands on */
  GQueue *queue;

  GstQueueSize
    cur_level,		/* currently in the queue */
    max_size,		/* max. amount of data allowed in the queue */
    min_threshold;	/* min. amount of data required to wake reader */

  /* whether we leak data, and at which end */
  gint leaky;

  GMutex *qlock;	/* lock for queue (vs object lock) */
  GCond *item_add;	/* signals buffers now available for reading */
  GCond *item_del;	/* signals space now available for writing */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstQueueClass {
  GstElementClass parent_class;

  /* signals - 'running' is called from both sides
   * which might make it sort of non-useful... */
  void (*underrun)	(GstQueue *queue);
  void (*running)	(GstQueue *queue);
  void (*overrun)	(GstQueue *queue);

  gpointer _gst_reserved[GST_PADDING];
};

GType gst_queue_get_type (void);

G_END_DECLS


#endif /* __GST_QUEUE_H__ */
