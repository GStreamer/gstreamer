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


#include <config.h>
#include <gst/gst.h>


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

typedef struct _GstQueue GstQueue;
typedef struct _GstQueueClass GstQueueClass;

struct _GstQueue {
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* the queue of buffers we're keeping our grubby hands on */
  GSList *queue;

  gint level_buffers;	/* number of buffers queued here */
  gint max_buffers;	/* maximum number of buffers queued here */
  gboolean block;	/* if set to FALSE, _get returns NULL if queue empty */
  gint level_bytes;	/* number of bytes queued here */
  gint size_buffers;	/* size of queue in buffers */
  gint size_bytes;	/* size of queue in bytes */

  GMutex *emptylock;	/* used when the queue is empty */
  GCond *emptycond;
  GMutex *fulllock;	/* used when the queue is full */
  GCond *fullcond;
  GTimeVal *timeval;	/* the timeout for the queue locking */
};

struct _GstQueueClass {
  GstElementClass parent_class;
};

GtkType gst_queue_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_QUEUE_H__ */
