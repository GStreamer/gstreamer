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

//#define DEBUG_ENABLED
//#define STATUS_ENABLED
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
  ARG_LEVEL_BUFFERS,
  ARG_LEVEL_BYTES,
  ARG_LEVEL_TIME,
  ARG_SIZE_BUFFERS,
  ARG_SIZE_BYTES,
  ARG_SIZE_TIME,
  ARG_LEAKY,
  ARG_LEVEL,
  ARG_MAX_LEVEL,
};


static void			gst_queue_class_init		(GstQueueClass *klass);
static void			gst_queue_init			(GstQueue *queue);

static void			gst_queue_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_queue_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstPadNegotiateReturn 	gst_queue_handle_negotiate_src 	(GstPad *pad, GstCaps **caps, gpointer *data);
static GstPadNegotiateReturn	gst_queue_handle_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data);
static void			gst_queue_chain			(GstPad *pad, GstBuffer *buf);
static GstBuffer *		gst_queue_get			(GstPad *pad);
static GstBufferPool* 		gst_queue_get_bufferpool 	(GstPad *pad);
	
static void			gst_queue_locked_flush			(GstQueue *queue);
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
//static guint gst_queue_signals[LAST_SIGNAL] = { 0 };

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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LEAKY,
    g_param_spec_enum("leaky","Leaky","Where the queue leaks, if at all.",
                      GST_TYPE_QUEUE_LEAKY,GST_QUEUE_NO_LEAK,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LEVEL,
    g_param_spec_int("level","Level","How many buffers are in the queue.",
                     0,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MAX_LEVEL,
    g_param_spec_int("max_level","Maximum Level","How many buffers the queue holds.",
                     0,G_MAXINT,100,G_PARAM_READWRITE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_queue_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_queue_get_property);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_queue_change_state);
}

static void
gst_queue_init (GstQueue *queue)
{
  // scheduling on this kind of element is, well, interesting
  GST_FLAG_SET (queue, GST_ELEMENT_DECOUPLED);

  queue->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (queue->sinkpad, GST_DEBUG_FUNCPTR(gst_queue_chain));
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);
  gst_pad_set_negotiate_function (queue->sinkpad, GST_DEBUG_FUNCPTR(gst_queue_handle_negotiate_sink));
  gst_pad_set_bufferpool_function (queue->sinkpad, GST_DEBUG_FUNCPTR(gst_queue_get_bufferpool));

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (queue->srcpad, GST_DEBUG_FUNCPTR(gst_queue_get));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_negotiate_function (queue->srcpad, GST_DEBUG_FUNCPTR(gst_queue_handle_negotiate_src));

  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->level_bytes = 0;
  queue->level_time = 0LL;
  queue->size_buffers = 100;		// 100 buffers
  queue->size_bytes = 100 * 1024;	// 100KB
  queue->size_time = 1000000000LL;	// 1sec

  queue->qlock = g_mutex_new ();
  queue->reader = FALSE;
  queue->writer = FALSE;
  queue->not_empty = g_cond_new ();
  queue->not_full = g_cond_new ();
  GST_DEBUG_ELEMENT (GST_CAT_THREAD, queue, "initialized queue's not_empty & not_full conditions\n");
}

static GstBufferPool*
gst_queue_get_bufferpool (GstPad *pad)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  return gst_pad_get_bufferpool (queue->srcpad);
}

static GstPadNegotiateReturn
gst_queue_handle_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  return gst_pad_negotiate_proxy (pad, queue->sinkpad, caps);
}

static GstPadNegotiateReturn
gst_queue_handle_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  return gst_pad_negotiate_proxy (pad, queue->srcpad, caps);
}

static void
gst_queue_cleanup_buffers (gpointer data, const gpointer user_data)
{
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, user_data, "cleaning buffer %p\n", data);
  gst_buffer_unref (GST_BUFFER (data));
}

static void
gst_queue_locked_flush (GstQueue *queue)
{
  g_slist_foreach (queue->queue, gst_queue_cleanup_buffers,
		  (gpointer) queue);
  g_slist_free (queue->queue);

  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->timeval = NULL;
}

static void
gst_queue_flush (GstQueue *queue)
{
  g_mutex_lock (queue->qlock);
  gst_queue_locked_flush (queue);
  g_mutex_unlock (queue->qlock);
}

static void
gst_queue_chain (GstPad *pad, GstBuffer *buf)
{
  GstQueue *queue;
  gboolean reader;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  reader = FALSE;

  /* we have to lock the queue since we span threads */

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locking t:%ld\n", pthread_self ());
  g_mutex_lock (queue->qlock);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locked t:%ld\n", pthread_self ());

  if (GST_IS_EVENT(buf)) {
    GstEvent *event = GST_EVENT(buf);
    switch (GST_EVENT_TYPE(event)) {
      case GST_EVENT_FLUSH:
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "flushing queue\n");
        gst_queue_locked_flush (queue);
        break;
      default:
        break;
    }
  }

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "adding buffer %p of size %d\n",buf,GST_BUFFER_SIZE(buf));

  if (queue->level_buffers == queue->size_buffers) {
    // if this is a leaky queue...
    if (queue->leaky) {
      // FIXME don't want to leak events!
      // if we leak on the upstream side, drop the current buffer
      if (queue->leaky == GST_QUEUE_LEAK_UPSTREAM) {
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on upstream end\n");
        if (GST_IS_EVENT (buf))
          fprintf(stderr, "Error: queue [%s] leaked an event, type:%d\n",
              GST_ELEMENT_NAME(GST_ELEMENT(queue)),
              GST_EVENT_TYPE(GST_EVENT(buf)));
          GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on upstream end\n");
        gst_buffer_unref(buf);
        // now we have to clean up and exit right away
        g_mutex_unlock (queue->qlock);
        return;
      }
      // otherwise we have to push a buffer off the other end
      else {
        GSList *front;
        GstBuffer *leakbuf;
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on downstream end\n");
        front = queue->queue;
        leakbuf = (GstBuffer *)(front->data);
        if (GST_IS_EVENT (leakbuf))
          fprintf(stderr, "Error: queue [%s] leaked an event, type:%d\n",
              GST_ELEMENT_NAME(GST_ELEMENT(queue)),
              GST_EVENT_TYPE(GST_EVENT(leakbuf)));
        queue->level_buffers--;
        queue->level_bytes -= GST_BUFFER_SIZE(leakbuf);
        gst_buffer_unref(leakbuf);
        queue->queue = g_slist_remove_link (queue->queue, front);
        g_slist_free (front);
      }
    }

    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "pre full wait, level:%d/%d\n",
        queue->level_buffers, queue->size_buffers);
    while (queue->level_buffers == queue->size_buffers) {
      // if there's a pending state change for this queue or its manager, switch
      // back to iterator so bottom half of state change executes
      if (GST_STATE_PENDING(queue) != GST_STATE_VOID_PENDING ||
//          GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_VOID_PENDING)
GST_STATE_PENDING(GST_SCHED_PARENT(GST_ELEMENT_SCHED(GST_PAD_PARENT(GST_PAD_PEER(queue->sinkpad))))) != 
GST_STATE_VOID_PENDING)
      {
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "interrupted!!\n");
        if (GST_STATE_PENDING(queue) != GST_STATE_VOID_PENDING)
          GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "GST_STATE_PENDING(queue) != GST_STATE_VOID_PENDING)\n");
        if (GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_VOID_PENDING)
          GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_VOID_PENDING\n");
        g_mutex_unlock (queue->qlock);
        cothread_switch(cothread_current_main());
      }

      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for not_full, level:%d/%d\n", queue->level_buffers, queue->size_buffers);
      if (queue->writer)
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "WARNING: multiple writers on queue!\n");
      queue->writer = TRUE;
      g_cond_wait (queue->not_full, queue->qlock);
      queue->writer = FALSE;
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "got not_full signal\n");
    }
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "post full wait, level:%d/%d\n",
        queue->level_buffers, queue->size_buffers);
  }

  /* put the buffer on the tail of the list */
  queue->queue = g_slist_append (queue->queue, buf);
  queue->level_buffers++;
  queue->level_bytes += GST_BUFFER_SIZE(buf);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "(%s:%s)+ level:%d/%d\n",
      GST_DEBUG_PAD_NAME(pad),
      queue->level_buffers, queue->size_buffers);

  /* reader waiting on an empty queue */
  reader = queue->reader;

  g_mutex_unlock (queue->qlock);

  if (reader)
  {
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "signalling not_empty\n");
    g_cond_signal (queue->not_empty);
  }
}

static GstBuffer *
gst_queue_get (GstPad *pad)
{
  GstQueue *queue;
  GstBuffer *buf = NULL;
  GSList *front;
  gboolean writer;

  g_assert(pad != NULL);
  g_assert(GST_IS_PAD(pad));
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  writer = FALSE;

  /* have to lock for thread-safety */
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locking t:%ld\n", pthread_self ());
  g_mutex_lock (queue->qlock);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "locked t:%ld %p\n", pthread_self (), queue->not_empty);

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "pre empty wait, level:%d/%d\n", queue->level_buffers, queue->size_buffers);
  while (queue->level_buffers == 0) {
    // if there's a pending state change for this queue or its manager, switch
    // back to iterator so bottom half of state change executes
    if (GST_STATE_PENDING(queue) != GST_STATE_VOID_PENDING ||
//        GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_VOID_PENDING)
GST_STATE_PENDING(GST_SCHED_PARENT(GST_ELEMENT_SCHED(GST_PAD_PARENT(GST_PAD_PEER(queue->srcpad))))) != 
GST_STATE_VOID_PENDING)
    {
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "interrupted!!\n");
      if (GST_STATE_PENDING(queue) != GST_STATE_VOID_PENDING)
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "GST_STATE_PENDING(queue) != GST_STATE_VOID_PENDING)\n");
      if (GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_VOID_PENDING)
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_VOID_PENDING\n");
      g_mutex_unlock (queue->qlock);
      cothread_switch(cothread_current_main());
    }

    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for not_empty, level:%d/%d\n", queue->level_buffers, queue->size_buffers);
    if (queue->reader)
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "WARNING: multiple readers on queue!\n");
    queue->reader = TRUE;
    g_cond_wait (queue->not_empty, queue->qlock);
    queue->reader = FALSE;
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "got not_empty signal\n");
  }
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "post empty wait, level:%d/%d\n", queue->level_buffers, queue->size_buffers);

  front = queue->queue;
  buf = (GstBuffer *)(front->data);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "retrieved buffer %p from queue\n", buf);
  queue->queue = g_slist_remove_link (queue->queue, front);
  g_slist_free (front);

  queue->level_buffers--;
  queue->level_bytes -= GST_BUFFER_SIZE(buf);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "(%s:%s)- level:%d/%d\n",
      GST_DEBUG_PAD_NAME(pad),
      queue->level_buffers, queue->size_buffers);

  /* writer waiting on a full queue */
  writer = queue->writer;

  g_mutex_unlock (queue->qlock);

  if (writer)
  {
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "signalling not_full\n");
    g_cond_signal (queue->not_full);
  }

  // FIXME where should this be? locked?
  if (GST_IS_EVENT(buf)) {
    GstEvent *event = GST_EVENT(buf);
    switch (GST_EVENT_TYPE(event)) {
      case GST_EVENT_EOS:
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue eos\n");
        gst_element_set_state (GST_ELEMENT (queue), GST_STATE_PAUSED);
        break;
      default:
        break;
    }
  }

  return buf;
}

static GstElementStateReturn
gst_queue_change_state (GstElement *element)
{
  GstQueue *queue;
  GstElementStateReturn ret;
  g_return_val_if_fail (GST_IS_QUEUE (element), GST_STATE_FAILURE);

  queue = GST_QUEUE (element);

  // lock the queue so another thread (not in sync with this thread's state)
  // can't call this queue's _get (or whatever)
  GST_LOCK (queue);

  /* if going down into NULL state, clear out buffers*/
  if (GST_STATE_PENDING (element) == GST_STATE_READY) {
    /* otherwise (READY or higher) we need to open the file */
    gst_queue_flush (queue);
  }

  // if we haven't failed already, give the parent class a chance to ;-)
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
  {
    gboolean valid_handler = FALSE;
    guint state_change_id = g_signal_lookup("state_change", G_OBJECT_TYPE(element));

    // determine whether we need to block the parent (element) class'
    // STATE_CHANGE signal so we can UNLOCK before returning.  we block
    // it if we could find the state_change signal AND there's a signal
    // handler attached to it.
    //
    // note: this assumes that change_state() *only* emits state_change signal.
    // if element change_state() emits other signals, they need to be blocked
    // as well.
    if (state_change_id &&
        g_signal_has_handler_pending(G_OBJECT(element), state_change_id, 0, FALSE))
      valid_handler = TRUE;
    if (valid_handler)
      g_signal_handler_block(G_OBJECT(element), state_change_id);

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

    if (valid_handler)
      g_signal_handler_unblock(G_OBJECT(element), state_change_id);

    // UNLOCK, *then* emit signal (if there's one there)
    GST_UNLOCK(queue);
    if (valid_handler)
      g_signal_emit(G_OBJECT (element), state_change_id, 0, GST_STATE(element));
  }
  else
  {
    ret = GST_STATE_SUCCESS;
    GST_UNLOCK(queue);
  }

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
      queue->leaky = g_value_get_int(value);
      break;
    case ARG_MAX_LEVEL:
      queue->size_buffers = g_value_get_int(value);
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
      g_value_set_int(value, queue->leaky);
      break;
    case ARG_LEVEL:
      g_value_set_int(value, queue->level_buffers);
      break;
    case ARG_MAX_LEVEL:
      g_value_set_int(value, queue->size_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
