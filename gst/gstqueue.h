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
typedef struct _GstQueueClass GstQueueClass;

struct _GstQueue {
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* the queue of buffers we're keeping our grubby hands on */
  GQueue *queue;

  guint level_buffers;	/* number of buffers queued here */
  guint level_bytes;	/* number of bytes queued here */
  guint64 level_time;	/* amount of time queued here */

  guint size_buffers;	/* size of queue in buffers */
  guint size_bytes;	/* size of queue in bytes */
  guint64 size_time;	/* size of queue in time */

  gint leaky;		/* whether the queue is leaky, and if so at which end */
  gint block_timeout;   /* microseconds until a blocked queue times out and returns GST_EVENT_FILLER. 
                         * A value of -1 will block forever. */
  guint min_threshold_bytes; /* the minimum number of bytes required before
                              * waking up the reader thread */ 
  gboolean may_deadlock; /* it the queue should fail on possible deadlocks */
  gboolean interrupt;
  gboolean flush;

  GMutex *qlock;	/* lock for queue (vs object lock) */
  GCond *not_empty;	/* signals buffers now available for reading */
  GCond *not_full;	/* signals space now available for writing */

  GTimeVal *timeval;	/* the timeout for the queue locking */
  GAsyncQueue *events;	/* upstream events get decoupled here */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstQueueClass {
  GstElementClass parent_class;

  /* signal callbacks */
  void (*full)		(GstQueue *queue);

  gpointer _gst_reserved[GST_PADDING];
};

GType gst_queue_get_type (void);

G_END_DECLS


#endif /* __GST_QUEUE_H__ */
