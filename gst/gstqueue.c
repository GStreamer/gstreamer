/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstqueue.c:
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

/* #define DEBUG_ENABLED */
/* #define STATUS_ENABLED */

#ifdef STATUS_ENABLED
#define STATUS(A) GST_DEBUG(GST_CAT_DATAFLOW, A, GST_ELEMENT_NAME(queue))
#else
#define STATUS(A)
#endif


#include "config.h"
#include "gst_private.h"

#include "gstqueue.h"
#include "gstscheduler.h"
#include "gstevent.h"
#include "gstlog.h"

GstElementDetails gst_queue_details = {
  "Queue",
  "Generic",
  "LGPL",
  "Simple data queue",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Queue signals and args */
enum {
  LOW_WATERMARK,
  HIGH_WATERMARK,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LEVEL_BUFFERS,
  ARG_LEVEL_BYTES,
  ARG_LEVEL_TIME,
  ARG_SIZE_BUFFERS,
  ARG_SIZE_BYTES,
  ARG_SIZE_TIME,
  ARG_LEAKY,
  ARG_LEVEL,
  ARG_MAX_LEVEL,
  ARG_MAY_DEADLOCK,
  ARG_BLOCK_TIMEOUT,
};


static void			gst_queue_class_init		(GstQueueClass *klass);
static void			gst_queue_init			(GstQueue *queue);
static void 			gst_queue_dispose 		(GObject *object);

static void			gst_queue_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_queue_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static void			gst_queue_chain			(GstPad *pad, GstBuffer *buf);
static GstBuffer *		gst_queue_get			(GstPad *pad);
static GstBufferPool* 		gst_queue_get_bufferpool 	(GstPad *pad);
	
static gboolean 		gst_queue_handle_src_event 	(GstPad *pad, GstEvent *event);


static void			gst_queue_locked_flush		(GstQueue *queue);

static GstElementStateReturn	gst_queue_change_state		(GstElement *element);
static gboolean			gst_queue_release_locks		(GstElement *element);

  
#define GST_TYPE_QUEUE_LEAKY (queue_leaky_get_type())
static GType
queue_leaky_get_type(void) {
  static GType queue_leaky_type = 0;
  static GEnumValue queue_leaky[] = {
    { GST_QUEUE_NO_LEAK, 		"0", "Not Leaky" },
    { GST_QUEUE_LEAK_UPSTREAM, 		"1", "Leaky on Upstream" },
    { GST_QUEUE_LEAK_DOWNSTREAM, 	"2", "Leaky on Downstream" },
    { 0, NULL, NULL },
  };
  if (!queue_leaky_type) {
    queue_leaky_type = g_enum_register_static("GstQueueLeaky", queue_leaky);
  }
  return queue_leaky_type;
}

static GstElementClass *parent_class = NULL;
/* static guint gst_queue_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_queue_get_type(void) 
{
  static GType queue_type = 0;

  if (!queue_type) {
    static const GTypeInfo queue_info = {
      sizeof(GstQueueClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_queue_class_init,
      NULL,
      NULL,
      sizeof(GstQueue),
      4,
      (GInstanceInitFunc)gst_queue_init,
      NULL
    };
    queue_type = g_type_register_static (GST_TYPE_ELEMENT, "GstQueue", &queue_info, 0);
  }
  return queue_type;
}

static void
gst_queue_class_init (GstQueueClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEAKY,
    g_param_spec_enum ("leaky", "Leaky", "Where the queue leaks, if at all.",
                       GST_TYPE_QUEUE_LEAKY, GST_QUEUE_NO_LEAK, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEVEL,
    g_param_spec_int ("level", "Level", "How many buffers are in the queue.",
                      0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_LEVEL,
    g_param_spec_int ("max_level", "Maximum Level", "How many buffers the queue holds.",
                      0, G_MAXINT, 100, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAY_DEADLOCK,
    g_param_spec_boolean ("may_deadlock", "May Deadlock", "The queue may deadlock if it's full and not PLAYING",
                      TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCK_TIMEOUT,
    g_param_spec_int ("block_timeout", "Timeout for Block", 
                      "Microseconds until blocked queue times out and returns filler event. "
                      "Value of -1 disables timeout",
                      -1, G_MAXINT, -1, G_PARAM_READWRITE));

  gobject_class->dispose                = GST_DEBUG_FUNCPTR (gst_queue_dispose);
  gobject_class->set_property 		= GST_DEBUG_FUNCPTR (gst_queue_set_property);
  gobject_class->get_property 		= GST_DEBUG_FUNCPTR (gst_queue_get_property);

  gstelement_class->change_state  = GST_DEBUG_FUNCPTR(gst_queue_change_state);
  gstelement_class->release_locks = GST_DEBUG_FUNCPTR(gst_queue_release_locks);
}

static GstPadLinkReturn
gst_queue_link (GstPad *pad, GstCaps *caps)
{
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == queue->srcpad) 
    otherpad = queue->sinkpad;
  else
    otherpad = queue->srcpad;

  return gst_pad_proxy_link (otherpad, caps);
}

static GstCaps*
gst_queue_getcaps (GstPad *pad, GstCaps *caps)
{
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == queue->srcpad) 
    otherpad = GST_PAD_PEER (queue->sinkpad);
  else
    otherpad = GST_PAD_PEER (queue->srcpad);
  
  if (otherpad)
    return gst_pad_get_caps (otherpad);

  return NULL;
}

static void
gst_queue_init (GstQueue *queue)
{
  /* scheduling on this kind of element is, well, interesting */
  GST_FLAG_SET (queue, GST_ELEMENT_DECOUPLED);
  GST_FLAG_SET (queue, GST_ELEMENT_EVENT_AWARE);

  queue->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_chain));
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);
  gst_pad_set_bufferpool_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_get_bufferpool));
  gst_pad_set_link_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_link));
  gst_pad_set_getcaps_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_getcaps));

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_get));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_link_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_link));
  gst_pad_set_getcaps_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_event_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_handle_src_event));

  queue->leaky = GST_QUEUE_NO_LEAK;
  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->level_bytes = 0;
  queue->level_time = G_GINT64_CONSTANT (0);
  queue->size_buffers = 100;				/* 100 buffers */
  queue->size_bytes = 100 * 1024;			/* 100KB */
  queue->size_time = G_GINT64_CONSTANT (1000000000);	/* 1sec */
  queue->may_deadlock = TRUE;
  queue->block_timeout = -1;
  queue->interrupt = FALSE;
  queue->flush = FALSE;

  queue->qlock = g_mutex_new ();
  queue->not_empty = g_cond_new ();
  queue->not_full = g_cond_new ();
  queue->events = g_async_queue_new();
  queue->queue = g_queue_new ();
  GST_DEBUG_ELEMENT (GST_CAT_THREAD, queue, "initialized queue's not_empty & not_full conditions");
}

static void
gst_queue_dispose (GObject *object)
{
  GstQueue *queue = GST_QUEUE (object);

  gst_queue_locked_flush (queue);

  g_mutex_free (queue->qlock);
  g_cond_free (queue->not_empty);
  g_cond_free (queue->not_full);
  g_queue_free (queue->queue);

  g_async_queue_unref(queue->events);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstBufferPool*
gst_queue_get_bufferpool (GstPad *pad)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  return gst_pad_get_bufferpool (queue->srcpad);
}

static void
gst_queue_cleanup_data (gpointer data, const gpointer user_data)
{
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, user_data, "cleaning buffer %p", data);

  gst_data_unref (GST_DATA (data));
}

static void
gst_queue_locked_flush (GstQueue *queue)
{
  gpointer data;
  
  while ((data = g_queue_pop_head (queue->queue))) {
    gst_queue_cleanup_data (data, (gpointer) queue);
  }
  queue->timeval = NULL;
  queue->level_buffers = 0;
  queue->level_bytes = 0;
  queue->level_time = G_GINT64_CONSTANT (0);
  /* make sure any pending buffers to be added are flushed too */
  queue->flush = TRUE;
  /* signal not_full, since we apparently aren't full anymore */
  g_cond_signal (queue->not_full);
}

static void
gst_queue_chain (GstPad *pad, GstBuffer *buf)
{
  GstQueue *queue;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));
  
  /* check for events to send upstream */
  g_async_queue_lock(queue->events);
  while (g_async_queue_length_unlocked(queue->events) > 0){
    GstEvent *event = (GstEvent*)g_async_queue_pop_unlocked(queue->events);
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "sending event upstream\n");
    gst_pad_event_default (pad, event);
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "event sent\n");
  }
  g_async_queue_unlock(queue->events);

restart:
  /* we have to lock the queue since we span threads */
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locking t:%p", g_thread_self ());
  g_mutex_lock (queue->qlock);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locked t:%p", g_thread_self ());

  /* assume don't need to flush this buffer when the queue is filled */
  queue->flush = FALSE;

  if (GST_IS_EVENT (buf)) {
    switch (GST_EVENT_TYPE (buf)) {
      case GST_EVENT_FLUSH:
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "FLUSH event, flushing queue\n");
        gst_queue_locked_flush (queue);
	break;
      case GST_EVENT_EOS:
	GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "eos in on %s %d\n", 
			   GST_ELEMENT_NAME (queue), queue->level_buffers);
	break;
      default:
	/* we put the event in the queue, we don't have to act ourselves */
	break;
    }
  }

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "adding buffer %p of size %d",buf,GST_BUFFER_SIZE(buf));

  if (queue->level_buffers == queue->size_buffers) {
    /* if this is a leaky queue... */
    if (queue->leaky) {
      /* FIXME don't want to leak events! */
      /* if we leak on the upstream side, drop the current buffer */
      if (queue->leaky == GST_QUEUE_LEAK_UPSTREAM) {
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on upstream end");
        if (GST_IS_EVENT (buf))
          fprintf(stderr, "Error: queue [%s] leaked an event, type:%d\n",
              GST_ELEMENT_NAME(GST_ELEMENT(queue)),
              GST_EVENT_TYPE(GST_EVENT(buf)));
          GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on upstream end");
        /* now we have to clean up and exit right away */
        g_mutex_unlock (queue->qlock);
        goto out_unref;
      }
      /* otherwise we have to push a buffer off the other end */
      else {
        gpointer front;
        GstBuffer *leakbuf;

        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on downstream end");

        front = g_queue_pop_head (queue->queue);
        leakbuf = (GstBuffer *)(front);

        if (GST_IS_EVENT (leakbuf)) {
          fprintf(stderr, "Error: queue [%s] leaked an event, type:%d\n",
              GST_ELEMENT_NAME(GST_ELEMENT(queue)),
              GST_EVENT_TYPE(GST_EVENT(leakbuf)));
	}
        queue->level_buffers--;
        queue->level_bytes -= GST_BUFFER_SIZE(leakbuf);
        gst_data_unref (GST_DATA (leakbuf));
      }
    }

    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "pre full wait, level:%d/%d",
        		queue->level_buffers, queue->size_buffers);

    while (queue->level_buffers == queue->size_buffers) {
      /* if there's a pending state change for this queue or its manager, switch */
      /* back to iterator so bottom half of state change executes */
      if (queue->interrupt) {
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "interrupted!!");
        g_mutex_unlock (queue->qlock);
	if (gst_scheduler_interrupt (gst_pad_get_scheduler (queue->sinkpad), GST_ELEMENT (queue)))
          goto out_unref;
	/* if we got here bacause we were unlocked after a flush, we don't need
	 * to add the buffer to the queue again */
	if (queue->flush) {
          GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "not adding pending buffer after flush");
	  goto out_unref;
	}
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "adding pending buffer after interrupt");
	goto restart;
      }
      if (GST_STATE (queue) != GST_STATE_PLAYING) {
	/* this means the other end is shut down */
	/* try to signal to resolve the error */
	if (!queue->may_deadlock) {
          g_mutex_unlock (queue->qlock);
          gst_data_unref (GST_DATA (buf));
          gst_element_error (GST_ELEMENT (queue), "deadlock found, source pad elements are shut down");
	  /* we don't want to goto out_unref here, since we want to clean up before calling gst_element_error */
	  return;
	}
	else {
          g_print ("%s: waiting for the app to restart source pad elements\n", GST_ELEMENT_NAME (queue));
	}
      }

      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for not_full, level:%d/%d", 
		      queue->level_buffers, queue->size_buffers);
      g_cond_wait (queue->not_full, queue->qlock);
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "got not_full signal");
    }
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "post full wait, level:%d/%d",
        queue->level_buffers, queue->size_buffers);
  }

  /* put the buffer on the tail of the list */
  g_queue_push_tail (queue->queue, buf);

  queue->level_buffers++;
  queue->level_bytes += GST_BUFFER_SIZE(buf);

  /* this assertion _has_ to hold */
  g_assert (queue->queue->length == queue->level_buffers);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "(%s:%s)+ level:%d/%d",
      GST_DEBUG_PAD_NAME(pad),
      queue->level_buffers, queue->size_buffers);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "signalling not_empty");
  g_cond_signal (queue->not_empty);
  g_mutex_unlock (queue->qlock);

  return;

out_unref:
  gst_data_unref (GST_DATA (buf));
  return;
}

static GstBuffer *
gst_queue_get (GstPad *pad)
{
  GstQueue *queue;
  GstBuffer *buf = NULL;
  gpointer front;

  g_assert(pad != NULL);
  g_assert(GST_IS_PAD(pad));
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

restart:
  /* have to lock for thread-safety */
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locking t:%p", g_thread_self ());
  g_mutex_lock (queue->qlock);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locked t:%p %p", g_thread_self (), queue->not_empty);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "pre empty wait, level:%d/%d", queue->level_buffers, queue->size_buffers);
  while (queue->level_buffers == 0) {
    /* if there's a pending state change for this queue or its manager, switch
     * back to iterator so bottom half of state change executes
     */ 
    //while (GST_STATE_PENDING (queue) != GST_STATE_VOID_PENDING) {
    if (queue->interrupt) {
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "interrupted!!");
      g_mutex_unlock (queue->qlock);
      if (gst_scheduler_interrupt (gst_pad_get_scheduler (queue->srcpad), GST_ELEMENT (queue)))
        return GST_BUFFER (gst_event_new (GST_EVENT_INTERRUPT));
      goto restart;
    }
    if (GST_STATE (queue) != GST_STATE_PLAYING) {
      /* this means the other end is shut down */
      if (!queue->may_deadlock) {
        g_mutex_unlock (queue->qlock);
        gst_element_error (GST_ELEMENT (queue), "deadlock found, sink pad elements are shut down");
        goto restart;
      }
      else {
        g_print ("%s: waiting for the app to restart source pad elements\n", GST_ELEMENT_NAME (queue));
      }
    }

    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for not_empty, level:%d/%d", queue->level_buffers, queue->size_buffers);
    
    /* if (queue->block_timeout > -1){ */
    if (FALSE) {
      GTimeVal timeout;
      g_get_current_time(&timeout);
      g_time_val_add(&timeout, queue->block_timeout);
      if (!g_cond_timed_wait (queue->not_empty, queue->qlock, &timeout)){
        g_mutex_unlock (queue->qlock);
	g_warning ("filler");
        return GST_BUFFER(gst_event_new_filler());
      }
    }
    else {
      g_cond_wait (queue->not_empty, queue->qlock);
    }
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "got not_empty signal");
  }
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "post empty wait, level:%d/%d", queue->level_buffers, queue->size_buffers);

  front = g_queue_pop_head (queue->queue);
  buf = (GstBuffer *)(front);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "retrieved buffer %p from queue", buf);

  queue->level_buffers--;
  queue->level_bytes -= GST_BUFFER_SIZE(buf);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "(%s:%s)- level:%d/%d",
      GST_DEBUG_PAD_NAME(pad),
      queue->level_buffers, queue->size_buffers);

  /* this assertion _has_ to hold */
  g_assert (queue->queue->length == queue->level_buffers);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "signalling not_full");
  g_cond_signal (queue->not_full);

  g_mutex_unlock (queue->qlock);

  /* FIXME where should this be? locked? */
  if (GST_IS_EVENT(buf)) {
    GstEvent *event = GST_EVENT(buf);
    switch (GST_EVENT_TYPE(event)) {
      case GST_EVENT_EOS:
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue \"%s\" eos", GST_ELEMENT_NAME (queue));
        gst_element_set_eos (GST_ELEMENT (queue));
        break;
      default:
        break;
    }
  }

  return buf;
}


static gboolean
gst_queue_handle_src_event (GstPad *pad, GstEvent *event)
{
  GstQueue *queue;
  gboolean res;
  gint event_type;
  gint flag_flush = 0;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  g_mutex_lock (queue->qlock);

  if (gst_element_get_state (GST_ELEMENT (queue)) == GST_STATE_PLAYING) {
    /* push the event to the queue for upstream consumption */
    g_async_queue_push(queue->events, event);
    g_warning ("FIXME: sending event in a running queue");
    /* FIXME wait for delivery of the event here, then return the result
     * instead of FALSE */
    res = FALSE;
    goto done;
  }

  event_type = GST_EVENT_TYPE (event);
  if (event_type == GST_EVENT_SEEK)
    flag_flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

  res = gst_pad_event_default (pad, event); 

  switch (event_type) {
    case GST_EVENT_FLUSH:
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "FLUSH event, flushing queue\n");
      gst_queue_locked_flush (queue);
      break;
    case GST_EVENT_SEEK:
      if (flag_flush) {
        gst_queue_locked_flush (queue);
      }
    default:
      break;
  }

done:
  g_mutex_unlock (queue->qlock);

  /* we have to claim success, but we don't really know */
  return res;
}

static gboolean
gst_queue_release_locks (GstElement *element)
{
  GstQueue *queue;

  queue = GST_QUEUE (element);

  g_mutex_lock (queue->qlock);
  queue->interrupt = TRUE;
  g_cond_signal (queue->not_full);
  g_cond_signal (queue->not_empty); 
  g_mutex_unlock (queue->qlock);

  return TRUE;
}

static GstElementStateReturn
gst_queue_change_state (GstElement *element)
{
  GstQueue *queue;
  GstElementStateReturn ret;

  queue = GST_QUEUE (element);

  GST_DEBUG_ENTER("('%s')", GST_ELEMENT_NAME (element));

  /* lock the queue so another thread (not in sync with this thread's state)
   * can't call this queue's _get (or whatever)
   */
  g_mutex_lock (queue->qlock);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_queue_locked_flush (queue);
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (!GST_PAD_IS_LINKED (queue->sinkpad)) {
        GST_DEBUG_ELEMENT (GST_CAT_STATES, queue, "queue %s is not linked", GST_ELEMENT_NAME (queue));
        /* FIXME can this be? */
        g_cond_signal (queue->not_empty);

        ret = GST_STATE_FAILURE;
        goto error;
      }
      else {
        GstScheduler *src_sched, *sink_sched;
	    
        src_sched = gst_pad_get_scheduler (GST_PAD_CAST (queue->srcpad));
        sink_sched = gst_pad_get_scheduler (GST_PAD_CAST (queue->sinkpad));

        if (src_sched == sink_sched) {
          GST_DEBUG_ELEMENT (GST_CAT_STATES, queue, "queue %s does not connect different schedulers", 
			GST_ELEMENT_NAME (queue));

	  g_warning ("queue %s does not connect different schedulers",
			GST_ELEMENT_NAME (queue));

	  ret = GST_STATE_FAILURE;
	  goto error;
        }
      }
      queue->interrupt = FALSE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_queue_locked_flush (queue);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

error:
  g_mutex_unlock (queue->qlock);

  GST_DEBUG_LEAVE("('%s')", GST_ELEMENT_NAME (element));
  return ret;
}


static void
gst_queue_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUEUE (object));

  queue = GST_QUEUE (object);

  switch (prop_id) {
    case ARG_LEAKY:
      queue->leaky = g_value_get_enum (value);
      break;
    case ARG_MAX_LEVEL:
      queue->size_buffers = g_value_get_int (value);
      break;
    case ARG_MAY_DEADLOCK:
      queue->may_deadlock = g_value_get_boolean (value);
      break;
    case ARG_BLOCK_TIMEOUT:
      queue->block_timeout = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_queue_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUEUE (object));

  queue = GST_QUEUE (object);

  switch (prop_id) {
    case ARG_LEAKY:
      g_value_set_enum (value, queue->leaky);
      break;
    case ARG_LEVEL:
      g_value_set_int (value, queue->level_buffers);
      break;
    case ARG_MAX_LEVEL:
      g_value_set_int (value, queue->size_buffers);
      break;
    case ARG_MAY_DEADLOCK:
      g_value_set_boolean (value, queue->may_deadlock);
      break;
    case ARG_BLOCK_TIMEOUT:
      g_value_set_int (value, queue->block_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
