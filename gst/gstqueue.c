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

#include <pthread.h>

#include "config.h"
#include "gst_private.h"

#include "gstqueue.h"
#include "gstscheduler.h"
#include "gstevent.h"

GstElementDetails gst_queue_details = {
  "Queue",
  "Connection",
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
  ARG_LEAKY,
  ARG_LEVEL,
  ARG_MAX_LEVEL,
  ARG_MAY_DEADLOCK,
  ARG_MAX_WAIT,
};

/* FIXME: I don't want to copy from glib */
struct _GAsyncQueue
{
  GMutex *mutex;
  GCond *cond;
  GQueue *queue;
  guint waiting_threads;
  guint ref_count;
};

static gpointer
g_async_queue_peek_unlocked (GAsyncQueue *queue)
{
  return g_queue_peek_tail (queue->queue);
}
/* static gpointer
g_async_queue_peek (GAsyncQueue *queue)
{
  gpointer retval;
  
  g_mutex_lock (queue->mutex);
  retval = g_async_queue_peek_unlocked (queue);
  g_mutex_unlock (queue->mutex);

  return retval;  
}*/

static void			gst_queue_class_init		(GstQueueClass *klass);
static void			gst_queue_init			(GstQueue *queue);
static void 			gst_queue_dispose 		(GObject *object);

static void			gst_queue_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_queue_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static void			gst_queue_chain			(GstPad *pad, GstData *buf);
static GstData *		gst_queue_get			(GstPad *pad);
static GstBufferPool* 		gst_queue_get_bufferpool 	(GstPad *pad);
static gpointer			gst_queue_upstream_event	(GstPad *pad, GstData *event);
	
static void			gst_queue_locked_flush		(GstQueue *queue);
static void			gst_queue_flush			(GstQueue *queue);

static GstElementStateReturn	gst_queue_change_state		(GstElement *element);

  
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
    g_param_spec_int ("max-level", "Maximum Level", "How many buffers the queue holds.",
                      1, G_MAXINT, 100, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAY_DEADLOCK,
    g_param_spec_boolean ("may-deadlock", "May Deadlock", "The queue may deadlock if it's full and not PLAYING",
                      TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_WAIT,
    g_param_spec_ulong ("max-wait", "Max Wait", "How long the queue will wait",
			1, 10 * 1000 * 1000, 10 * 1000, G_PARAM_READWRITE));

  gobject_class->dispose                = GST_DEBUG_FUNCPTR (gst_queue_dispose);
  gobject_class->set_property 		= GST_DEBUG_FUNCPTR (gst_queue_set_property);
  gobject_class->get_property 		= GST_DEBUG_FUNCPTR (gst_queue_get_property);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_queue_change_state);
}

static GstPadConnectReturn
gst_queue_connect (GstPad *pad, GstCaps *caps)
{
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == queue->srcpad) 
    otherpad = queue->sinkpad;
  else
    otherpad = queue->srcpad;

  return gst_pad_proxy_connect (otherpad, caps);
}

static GstCaps*
gst_queue_getcaps (GstPad *pad, GstCaps *caps)
{
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == queue->srcpad) 
    otherpad = queue->sinkpad;
  else
    otherpad = queue->srcpad;

  return gst_pad_get_allowed_caps (otherpad);
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
  gst_pad_set_connect_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_connect));
  gst_pad_set_getcaps_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_getcaps));

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_get));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_connect_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_connect));
  gst_pad_set_getcaps_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_event_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_upstream_event));
  
  queue->leaky = GST_QUEUE_NO_LEAK;
  queue->queue = g_async_queue_new ();
  queue->not_full = g_cond_new ();
  queue->need_src = GST_QUEUE_NEED_NOTHING;
  queue->need_sink = GST_QUEUE_NEED_NOTHING;

  queue->size = 100;		/* 100 buffers */
  queue->may_deadlock = TRUE;
  queue->max_wait = 10 * 1000;	/* wait max 10 milliseconds */

  queue->upstream_event = NULL;
  queue->upstream_return = NULL;
  queue->upstream_mutex = g_mutex_new();
  queue->upstream_cond = g_cond_new();
}

static void
gst_queue_dispose (GObject *object)
{
  GstQueue *queue = GST_QUEUE (object);

  if (queue->upstream_event || queue->upstream_return)
    g_warning ("losing event while disposing queue");
  g_cond_free (queue->upstream_cond);
  g_mutex_free (queue->upstream_mutex);
    
  g_cond_free (queue->not_full);
  g_async_queue_unref (queue->queue);
  

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
gst_queue_locked_flush (GstQueue *queue)
{
  GstData *data;
  
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "cleaning queue\n");
  while ((data = GST_DATA (g_async_queue_try_pop_unlocked (queue->queue))) != NULL)
  {
    switch (GST_DATA_TYPE (data))
    {
      case GST_EVENT_NEWMEDIA: 
  	queue->need_sink |= GST_QUEUE_NEED_NEWMEDIA;
  	break;
      case GST_EVENT_EOS:
	queue->need_sink |= GST_QUEUE_NEED_EOS;
	break;
      default:
	queue->need_sink |= GST_QUEUE_NEED_DISCONTINUOUS;
	break;
    }
    gst_data_unref (data);
  }
  queue->need_sink |= queue->need_src;
  queue->need_src = GST_QUEUE_NEED_NOTHING;
  g_cond_signal (queue->not_full);
}
static void
gst_queue_flush (GstQueue *queue)
{
  g_async_queue_lock (queue->queue);
  gst_queue_locked_flush (queue);
  g_async_queue_unlock (queue->queue);
}
static void
gst_queue_chain (GstPad *pad, GstData *buf)
{
  GstQueue *queue;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

restart:

  /* handle events */
  if (queue->upstream_event)
  {
    GstPad *peer = GST_PAD_PEER (pad);
    g_print ("handling event\n");
    g_mutex_lock (queue->upstream_mutex);
    
    if (peer)
    {
      queue->upstream_return = gst_pad_send_event (peer, queue->upstream_event);
    } else {
      gst_data_unref (queue->upstream_event);
    }
    queue->upstream_event = NULL;
    g_cond_signal (queue->upstream_cond);
    g_mutex_unlock (queue->upstream_mutex);
    g_print ("done handling event\n");
  }
  
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "adding buffer %p of size %d\n", buf, GST_BUFFER_SIZE(buf));

  /* if we leak on the upstream side, drop the current buffer */
  if (queue->leaky == GST_QUEUE_LEAK_UPSTREAM && g_async_queue_length (queue->queue) >= queue->size) {
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on upstream end\n");
    /* check if we have to schedule an event for when the stream gets empty */
    switch (GST_DATA_TYPE (buf))
    {
      case GST_EVENT_NEWMEDIA: 
	g_async_queue_lock (queue->queue);
	queue->need_sink |= GST_QUEUE_NEED_NEWMEDIA;
	g_async_queue_unlock (queue->queue);
	break;
      case GST_EVENT_DISCONTINUOUS: 
	g_async_queue_lock (queue->queue);
	queue->need_sink |= GST_QUEUE_NEED_DISCONTINUOUS;
	g_async_queue_unlock (queue->queue);
	break;
      case GST_EVENT_EOS:
	g_async_queue_lock (queue->queue);
	queue->need_sink |= GST_QUEUE_NEED_EOS;
	g_async_queue_unlock (queue->queue);
	break;
      default:
	break;
    }
    gst_data_unref(buf);
    /* now we have to exit right away */
    return;
  } 
  else if (queue->leaky == GST_QUEUE_LEAK_DOWNSTREAM)
  {
    /* while there are too much buffers */
    while (g_async_queue_length (queue->queue) >= queue->size)
    {
      GstData *leakbuf;
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on downstream end\n");
      g_async_queue_lock (queue->queue);
      leakbuf = (GstData *) g_async_queue_try_pop_unlocked (queue->queue);
      if (leakbuf != NULL)
      {
        switch (GST_DATA_TYPE (buf))
        {
	  case GST_EVENT_NEWMEDIA: 
  	    queue->need_src |= GST_QUEUE_NEED_NEWMEDIA;
  	    break;
	  case GST_EVENT_EOS:
	    queue->need_src |= GST_QUEUE_NEED_EOS;
	    break;
	  default:
	    queue->need_src |= GST_QUEUE_NEED_DISCONTINUOUS;
	    break;
	}
        gst_data_unref (leakbuf);
      }
      g_async_queue_unlock (queue->queue);
    }
  }
    
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "pre full wait, level:%d/%d\n",
        g_async_queue_length (queue->queue), queue->size);
  /* while there isn't enough space available */
  if (g_async_queue_length (queue->queue) >= queue->size) 
  {
    /* if there's a pending state change for this queue or its manager, switch */
    /* back to iterator so bottom half of state change executes */
    while (GST_STATE_PENDING (queue) != GST_STATE_VOID_PENDING) {
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "interrupted!!\n");
      if (gst_element_interrupt (GST_ELEMENT (queue)))
        return;
      goto restart;
    }
    if (GST_STATE (queue) != GST_STATE_PLAYING) {
      /* this means the other end is shut down */
      /* try to signal to resolve the error */
      if (!queue->may_deadlock) {
        gst_data_unref (buf);
        gst_element_error (GST_ELEMENT (queue), "deadlock found, source pad elements are shut down");
	return;
      } else {
        gst_element_info (GST_ELEMENT (queue), "waiting for the app to restart source pad elements");
      }
    }

    /* FIXME: we're poking in AsyncQueue internals here */
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for not_full, level:%d/%d\n", g_async_queue_length (queue->queue), queue->size);
    g_print ("waiting for not null\n");
    g_mutex_lock (queue->upstream_mutex);
    if (queue->upstream_event == NULL)
      g_cond_wait (queue->not_full, queue->upstream_mutex);
    g_mutex_unlock (queue->upstream_mutex);
    g_print ("not null\n");
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "got not_full signal\n");
    goto restart;
  }
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "post full wait, level:%d/%d\n", g_async_queue_length (queue->queue), queue->size);

  /* put the buffer on the tail of the list */
  g_async_queue_push (queue->queue, buf);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "(%s:%s)+ level:%d/%d\n",
      GST_DEBUG_PAD_NAME(pad), g_async_queue_length (queue->queue), queue->size);

}

static GstData *
gst_queue_get (GstPad *pad)
{
  GstQueue *queue;
  GstData *data;

  g_assert(pad != NULL);
  g_assert(GST_IS_PAD(pad));
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

restart:
  
  /* make sure we're not in the middle of a state change */
  while (GST_STATE_PENDING (queue) != GST_STATE_VOID_PENDING) {
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "interrupted!!\n");
    if (gst_element_interrupt (GST_ELEMENT (queue)))
      return NULL;
  }

  g_async_queue_lock (queue->queue);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "pre empty wait, level:%d/%d\n", g_async_queue_length_unlocked (queue->queue), queue->size);
  if (queue->need_src)
  {
    GstData *event;
    switch (queue->need_src)
    {
      case GST_QUEUE_NEED_NEWMEDIA:
	event = GST_DATA (gst_event_new_newmedia ());
	break;
      case GST_QUEUE_NEED_EOS:
	event = GST_DATA (gst_event_new_eos ());
	break;
      case GST_QUEUE_NEED_DISCONTINUOUS:
	event = GST_DATA (gst_event_new_discontinuous ());
	break;
      default:
        event = NULL;
        g_assert_not_reached ();
      break;
    }
    data = g_async_queue_peek_unlocked (queue->queue);
    if (data)
    {
      guint i;
      for (i = 0; i < GST_OFFSET_TYPES; i++)
      {
	event->offset[i] = data->offset[i];
      }
    }
    queue->need_src = GST_QUEUE_NEED_NOTHING;
    g_async_queue_unlock (queue->queue);
    return event;
  }
  if ((data = g_async_queue_pop_unlocked (queue->queue)) == NULL)
  {
    GTimeVal timeval;
    /* if there's a pending state change for this queue or its manager, switch
     * back to iterator so bottom half of state change executes
     */ 
    if (GST_STATE (queue) != GST_STATE_PLAYING) {
      /* this means the other end is shut down */
      if (!queue->may_deadlock) {
	g_async_queue_unlock (queue->queue);
        gst_element_error (GST_ELEMENT (queue), "deadlock found, sink pad elements are shut down");
        goto restart;
      }
      else {
        gst_element_info (GST_ELEMENT (queue), "waiting for the app to restart sink pad elements");
      }
    }

    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for not_empty, level:%d/%d\n", g_async_queue_length_unlocked (queue->queue), queue->size);
    g_get_current_time (&timeval);
    g_time_val_add (&timeval, queue->max_wait);
    g_print ("waiting for data\n");
    data = g_async_queue_timed_pop_unlocked (queue->queue, &timeval);
    g_print ("%s\n", data == NULL ? "timer goes off while waiting for data\n" : "data available\n");
    if (data == NULL)
    {
      g_async_queue_unlock (queue->queue);
      gst_element_yield (GST_ELEMENT (data));
      goto restart;
    }
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "got not_empty signal\n");
  }
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "post empty wait, level:%d/%d\n", g_async_queue_length_unlocked (queue->queue), queue->size);
  g_async_queue_unlock (queue->queue);
  
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "(%s:%s)- level:%d/%d\n",
      GST_DEBUG_PAD_NAME(pad),
      g_async_queue_length (queue->queue), queue->size);

  g_cond_signal (queue->not_full);

  /* FIXME where should this be? locked? */
  if (GST_DATA_TYPE(data) == GST_EVENT_EOS)
  {
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue \"%s\" eos\n", GST_ELEMENT_NAME (queue));
    gst_element_set_eos (GST_ELEMENT (queue));
  }

  return data;
}

static GstElementStateReturn
gst_queue_change_state (GstElement *element)
{
  GstQueue *queue;
  GstElementStateReturn ret;
  GstElementState new_state;
  g_return_val_if_fail (GST_IS_QUEUE (element), GST_STATE_FAILURE);

  queue = GST_QUEUE (element);

  GST_DEBUG_ENTER("('%s')", GST_ELEMENT_NAME (element));

  /* lock the queue so another thread (not in sync with this thread's state)
   * can't call this queue's _get (or whatever)
   */
  g_async_queue_lock (queue->queue);

  new_state = GST_STATE_PENDING (element);

  if (new_state == GST_STATE_PAUSED) {
    /* g_cond_signal (queue->not_full); */
  }
  else if (new_state == GST_STATE_READY) {
    gst_queue_locked_flush (queue);
  }
  else if (new_state == GST_STATE_PLAYING) {
    if (!GST_PAD_IS_CONNECTED (queue->sinkpad)) {
      /* FIXME can this be? */
      g_async_queue_unlock (queue->queue);

      return GST_STATE_FAILURE;
    }
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  g_async_queue_unlock (queue->queue);

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
      g_object_notify (object, "leaky");
      break;
    case ARG_MAX_LEVEL:
      queue->size = g_value_get_int (value);
      g_object_notify (object, "max-level");
      break;
    case ARG_MAY_DEADLOCK:
      queue->may_deadlock = g_value_get_boolean (value);
      g_object_notify (object, "may-deadlock");
      break;
    case ARG_MAX_WAIT:
      queue->max_wait = g_value_get_ulong (value);
      g_object_notify (object, "max-wait");
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
      g_value_set_int (value, g_async_queue_length (queue->queue));
      break;
    case ARG_MAX_LEVEL:
      g_value_set_int (value, queue->size);
      break;
    case ARG_MAY_DEADLOCK:
      g_value_set_boolean (value, queue->may_deadlock);
      break;
    case ARG_MAX_WAIT:
      g_value_set_ulong (value, queue->max_wait);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static gpointer
gst_queue_upstream_event (GstPad *pad, GstData *event)
{
  GstQueue *queue = GST_QUEUE (GST_PAD_PARENT (pad));
  gpointer *ret = NULL;
  
  /* check that everything is ok. */
  if (queue->upstream_event || queue->upstream_return)
  {
    g_warning ("unhandled event in queue.");
    queue->upstream_return = NULL;
  }
  
  /* transfer event */
  g_mutex_lock (queue->upstream_mutex);
  gst_data_ref (event);
  queue->upstream_event = event;
  g_cond_signal (queue->not_full);
  g_print ("waiting for handling of upstream event\n");
  while (queue->upstream_event != NULL)
    g_cond_wait (queue->upstream_cond, queue->upstream_mutex);
  g_print ("upstream event handled\n");
  ret = queue->upstream_return;
  queue->upstream_return = NULL;
  g_mutex_unlock (queue->upstream_mutex);
  
  /* handle event */
  if (ret != NULL)
  {
    switch (GST_DATA_TYPE (event))
    {
      case GST_EVENT_SEEK:
	if (GST_EVENT_SEEK_FLUSH(event))
	  gst_queue_flush (queue);
	break;
      case GST_EVENT_FLUSH:
	gst_queue_flush (queue);
	break;
      default:
        break;
    }
  }
  
  /* return */
  gst_data_unref (event);
  return ret;
}

