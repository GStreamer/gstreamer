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


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_queue_details;


#define GST_TYPE_QUEUE \
  (gst_queue_get_type())
#define GST_QUEUE(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_QUEUE,GstQueue))
#define GST_QUEUE_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_QUEUE,GstQueueClass))
#define GST_IS_QUEUE(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_QUEUE))
#define GST_IS_QUEUE_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_QUEUE))

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
  GSList *queue;

  gint level_buffers;	/* number of buffers queued here */
  gint level_bytes;	/* number of bytes queued here */
  guint64 level_time;	/* amount of time queued here */

  gint size_buffers;	/* size of queue in buffers */
  gint size_bytes;	/* size of queue in bytes */
  guint64 size_time;	/* size of queue in time */

  gint leaky;		/* whether the queue is leaky, and if so at which end */

//  GMutex *lock;	(optimization?)
  GCond *emptycond;
  GCond *fullcond;

  GTimeVal *timeval;	/* the timeout for the queue locking */
};

struct _GstQueueClass {
  GstElementClass parent_class;

  /* signal callbacks */
  void (*low_watermark)		(GstQueue *queue, gint level);
  void (*high_watermark)	(GstQueue *queue, gint level);
};

GtkType gst_queue_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_QUEUE_H__ */
