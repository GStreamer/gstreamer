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


static void			gst_queue_class_init	(GstQueueClass *klass);
static void			gst_queue_init		(GstQueue *queue);

static void			gst_queue_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void			gst_queue_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static gboolean			gst_queue_handle_eos	(GstPad *pad);
static GstPadNegotiateReturn 	gst_queue_handle_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data);
static GstPadNegotiateReturn	gst_queue_handle_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data);
static void			gst_queue_chain		(GstPad *pad, GstBuffer *buf);
static GstBuffer *		gst_queue_get		(GstPad *pad);
static GstBufferPool* 		gst_queue_get_bufferpool (GstPad *pad);
	
static void			gst_queue_flush		(GstQueue *queue);

static GstElementStateReturn	gst_queue_change_state	(GstElement *element);

  
static GtkType
queue_leaky_get_type(void) {
  static GtkType queue_leaky_type = 0;
  static GtkEnumValue queue_leaky[] = {
    { GST_QUEUE_NO_LEAK, "0", "Not Leaky" },
    { GST_QUEUE_LEAK_UPSTREAM, "1", "Leaky on Upstream" },
    { GST_QUEUE_LEAK_DOWNSTREAM, "2", "Leaky on Downstream" },
    { 0, NULL, NULL },
  };
  if (!queue_leaky_type) {
    queue_leaky_type = gtk_type_register_enum("GstQueueLeaky", queue_leaky);
  }
  return queue_leaky_type;
}
#define GST_TYPE_QUEUE_LEAKY (queue_leaky_get_type())


static GstElementClass *parent_class = NULL;
//static guint gst_queue_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_queue_get_type(void) {
  static GtkType queue_type = 0;

  if (!queue_type) {
    static const GtkTypeInfo queue_info = {
      "GstQueue",
      sizeof(GstQueue),
      sizeof(GstQueueClass),
      (GtkClassInitFunc)gst_queue_class_init,
      (GtkObjectInitFunc)gst_queue_init,
      (GtkArgSetFunc)gst_queue_set_arg,
      (GtkArgGetFunc)gst_queue_get_arg,
      (GtkClassInitFunc)NULL,
    };
    queue_type = gtk_type_unique (GST_TYPE_ELEMENT, &queue_info);
  }
  return queue_type;
}

static void
gst_queue_class_init (GstQueueClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstQueue::leaky", GST_TYPE_QUEUE_LEAKY,
                           GTK_ARG_READWRITE, ARG_LEAKY);
  gtk_object_add_arg_type ("GstQueue::level", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_LEVEL);
  gtk_object_add_arg_type ("GstQueue::max_level", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_MAX_LEVEL);

  gtkobject_class->set_arg = gst_queue_set_arg;
  gtkobject_class->get_arg = gst_queue_get_arg;

  gstelement_class->change_state = gst_queue_change_state;
}

static void
gst_queue_init (GstQueue *queue)
{
  // scheduling on this kind of element is, well, interesting
  GST_FLAG_SET (queue, GST_ELEMENT_DECOUPLED);

  queue->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (queue->sinkpad, GST_DEBUG_FUNCPTR(gst_queue_chain));
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);
  gst_pad_set_eos_function (queue->sinkpad, gst_queue_handle_eos);
  gst_pad_set_negotiate_function (queue->sinkpad, gst_queue_handle_negotiate_sink);
  gst_pad_set_bufferpool_function (queue->sinkpad, gst_queue_get_bufferpool);

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (queue->srcpad, GST_DEBUG_FUNCPTR(gst_queue_get));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_negotiate_function (queue->srcpad, gst_queue_handle_negotiate_src);

  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->level_bytes = 0;
  queue->level_time = 0LL;
  queue->size_buffers = 100;		// 100 buffers
  queue->size_bytes = 100 * 1024;	// 100KB
  queue->size_time = 1000000000LL;	// 1sec

  queue->emptycond = g_cond_new ();
  queue->fullcond = g_cond_new ();
  GST_DEBUG(GST_CAT_THREAD, "initialized queue's emptycond and fullcond\n");
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
  

  //return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadNegotiateReturn
gst_queue_handle_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  /*
  if (counter == 0) {
     *caps = NULL;
     return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
  */
    return gst_pad_negotiate_proxy (pad, queue->srcpad, caps);
    /*
  }

  return GST_PAD_NEGOTIATE_FAIL;
  */
}

static gboolean
gst_queue_handle_eos (GstPad *pad)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  GST_DEBUG (GST_CAT_DATAFLOW,"%s received eos\n", GST_ELEMENT_NAME (queue));

  GST_LOCK (queue);
  GST_DEBUG (GST_CAT_DATAFLOW,"%s has %d buffers left\n", GST_ELEMENT_NAME (queue),
		  queue->level_buffers);

  GST_FLAG_SET (pad, GST_PAD_EOS);

  g_cond_signal (queue->emptycond);

  GST_UNLOCK (queue);

  return TRUE;
}

static void
gst_queue_cleanup_buffers (gpointer data, const gpointer user_data)
{
  GST_DEBUG (GST_CAT_DATAFLOW,"%s cleaning buffer %p\n", (gchar *)user_data, data);

  gst_buffer_unref (GST_BUFFER (data));
}

static void
gst_queue_flush (GstQueue *queue)
{
  g_slist_foreach (queue->queue, gst_queue_cleanup_buffers,
		  (char *) GST_ELEMENT_NAME (queue));
  g_slist_free (queue->queue);

  queue->queue = NULL;
  queue->level_buffers = 0;
  queue->timeval = NULL;
}

static void
gst_queue_chain (GstPad *pad, GstBuffer *buf)
{
  GstQueue *queue;
  gboolean tosignal = FALSE;
  const guchar *name;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));
  name = GST_ELEMENT_NAME (queue);

  /* we have to lock the queue since we span threads */

//  GST_DEBUG (GST_CAT_DATAFLOW,"trying to get lock on queue \"%s\"\n",name);
  GST_LOCK (queue);

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLUSH)) {
    GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "buffer has FLUSH bit set, flushing queue\n");
    gst_queue_flush (queue);
  }

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "adding buffer %p of size %d\n",buf,GST_BUFFER_SIZE(buf));

  if (queue->level_buffers >= queue->size_buffers) {
    // if this is a leaky queue...
    if (queue->leaky) {
      // if we leak on the upstream side, drop the current buffer
      if (queue->leaky == GST_QUEUE_LEAK_UPSTREAM) {
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on upstream end\n");
        gst_buffer_unref(buf);
        // now we have to clean up and exit right away
        GST_UNLOCK (queue);
        return;
      }
      // otherwise we have to push a buffer off the other end
      else {
        GSList *front;
        GstBuffer *leakbuf;
        GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "queue is full, leaking buffer on downstream end\n");
        front = queue->queue;
        leakbuf = (GstBuffer *)(front->data);
        queue->level_buffers--;
        queue->level_bytes -= GST_BUFFER_SIZE(leakbuf);
        gst_buffer_unref(leakbuf);
        queue->queue = g_slist_remove_link (queue->queue, front);
        g_slist_free (front);
      }
    }

    while (queue->level_buffers >= queue->size_buffers) {
      // if there's a pending state change for this queue or its manager, switch
      // back to iterator so bottom half of state change executes
      if (GST_STATE_PENDING(queue) != GST_STATE_NONE_PENDING ||
//          GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_NONE_PENDING)
GST_STATE_PENDING(GST_SCHED_PARENT(GST_ELEMENT_SCHED(GST_PAD_PARENT(GST_PAD_PEER(queue->sinkpad))))) != 
GST_STATE_NONE_PENDING)
      {
        GST_DEBUG(GST_CAT_DATAFLOW,"interrupted!!\n");
        if (GST_STATE_PENDING(queue) != GST_STATE_NONE_PENDING)
          GST_DEBUG(GST_CAT_DATAFLOW,"GST_STATE_PENDING(queue) != GST_STATE_NONE_PENDING)\n");
        if (GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_NONE_PENDING)
          GST_DEBUG(GST_CAT_DATAFLOW,"GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_NONE_PENDING\n");
        GST_UNLOCK(queue);
        cothread_switch(cothread_current_main());
      }

      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "waiting for space, level is %d\n", queue->level_buffers);
      g_cond_signal (queue->emptycond);
      g_cond_wait (queue->fullcond, GST_OBJECT(queue)->lock);
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, queue, "done waiting, level is now %d\n", queue->level_buffers);
    }
  }

  /* put the buffer on the tail of the list */
  queue->queue = g_slist_append (queue->queue, buf);
  queue->level_buffers++;
  queue->level_bytes += GST_BUFFER_SIZE(buf);
//  GST_DEBUG (GST_CAT_DATAFLOW, "(%s:%s)+\n",GST_DEBUG_PAD_NAME(pad));

  /* if we were empty, but aren't any more, signal a condition */
  if (queue->level_buffers == 1)
  {
    GST_DEBUG (GST_CAT_DATAFLOW,"%s signalling emptycond\n", name);
    g_cond_signal (queue->emptycond);
  }

  GST_UNLOCK (queue);
}

static GstBuffer *
gst_queue_get (GstPad *pad)
{
  GstQueue *queue;
  GstBuffer *buf = NULL;
  GSList *front;
  const guchar *name;

  g_assert(pad != NULL);
  g_assert(GST_IS_PAD(pad));
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));
  name = GST_ELEMENT_NAME (queue);

  /* have to lock for thread-safety */
  GST_DEBUG (GST_CAT_DATAFLOW,"%s try have queue lock\n", name);
  GST_LOCK (queue);
  GST_DEBUG (GST_CAT_DATAFLOW,"%s push %d %ld %p\n", name, queue->level_buffers, pthread_self (), queue->emptycond);
  GST_DEBUG (GST_CAT_DATAFLOW,"%s have queue lock\n", name);

  while (!queue->level_buffers) {
    if (GST_FLAG_IS_SET (queue->sinkpad, GST_PAD_EOS)) {
      GST_DEBUG (GST_CAT_DATAFLOW, "%s U released lock\n", name);
      GST_UNLOCK(queue);
      gst_pad_set_eos (queue->srcpad);
      // this return NULL shouldn't hurt anything...
      return NULL;
    }

    // if there's a pending state change for this queue or its manager, switch
    // back to iterator so bottom half of state change executes
    if (GST_STATE_PENDING(queue) != GST_STATE_NONE_PENDING ||
//        GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_NONE_PENDING)
GST_STATE_PENDING(GST_SCHED_PARENT(GST_ELEMENT_SCHED(GST_PAD_PARENT(GST_PAD_PEER(queue->srcpad))))) != 
GST_STATE_NONE_PENDING)
    {
      GST_DEBUG(GST_CAT_DATAFLOW,"interrupted!!\n");
      if (GST_STATE_PENDING(queue) != GST_STATE_NONE_PENDING)
        GST_DEBUG(GST_CAT_DATAFLOW,"GST_STATE_PENDING(queue) != GST_STATE_NONE_PENDING)\n");
      if (GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_NONE_PENDING)
        GST_DEBUG(GST_CAT_DATAFLOW,"GST_STATE_PENDING(GST_SCHEDULE(GST_ELEMENT(queue)->sched)->parent) != GST_STATE_NONE_PENDING\n");
      GST_UNLOCK(queue);
      cothread_switch(cothread_current_main());
    }

    g_cond_signal (queue->fullcond);
    g_cond_wait (queue->emptycond, GST_OBJECT(queue)->lock);
  }

  front = queue->queue;
  buf = (GstBuffer *)(front->data);
  GST_DEBUG (GST_CAT_DATAFLOW,"retrieved buffer %p from queue\n",buf);
  queue->queue = g_slist_remove_link (queue->queue, front);
  g_slist_free (front);

//  if (queue->level_buffers < queue->size_buffers)
  if (queue->level_buffers == queue->size_buffers)
  {
    GST_DEBUG (GST_CAT_DATAFLOW,"%s signalling fullcond\n", name);
    g_cond_signal (queue->fullcond);
  }

  queue->level_buffers--;
  queue->level_bytes -= GST_BUFFER_SIZE(buf);
  GST_DEBUG (GST_CAT_DATAFLOW,"(%s:%s)- ",GST_DEBUG_PAD_NAME(pad));

  GST_UNLOCK(queue);

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

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
  {
    gboolean valid_handler = FALSE;
    guint state_change_id = gtk_signal_lookup("state_change", GTK_OBJECT_TYPE(element));

    // determine whether we need to block the parent (element) class'
    // STATE_CHANGE signal so we can UNLOCK before returning.  we block
    // it if we could find the state_change signal AND there's a signal
    // handler attached to it.
    //
    // note: this assumes that change_state() *only* emits state_change signal.
    // if element change_state() emits other signals, they need to be blocked
    // as well.
    if (state_change_id &&
        gtk_signal_handler_pending(GTK_OBJECT(element), state_change_id, FALSE))
      valid_handler = TRUE;
    if (valid_handler)
      gtk_signal_handler_block(GTK_OBJECT(element), state_change_id);

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

    if (valid_handler)
      gtk_signal_handler_unblock(GTK_OBJECT(element), state_change_id);

    // UNLOCK, *then* emit signal (if there's one there)
    GST_UNLOCK(queue);
    if (valid_handler)
      gtk_signal_emit(GTK_OBJECT (element), state_change_id, GST_STATE(element));
  }
  else
  {
    ret = GST_STATE_SUCCESS;
    GST_UNLOCK(queue);
  }

  return ret;
}


static void
gst_queue_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUEUE (object));

  queue = GST_QUEUE (object);

  switch(id) {
    case ARG_LEAKY:
      queue->leaky = GTK_VALUE_INT (*arg);
      break;
    case ARG_MAX_LEVEL:
      queue->size_buffers = GTK_VALUE_INT (*arg);
      break;
    default:
      break;
  }
}

static void
gst_queue_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_QUEUE (object));

  queue = GST_QUEUE (object);

  switch (id) {
    case ARG_LEAKY:
      GTK_VALUE_INT (*arg) = queue->leaky;
      break;
    case ARG_LEVEL:
      GTK_VALUE_INT (*arg) = queue->level_buffers;
      break;
    case ARG_MAX_LEVEL:
      GTK_VALUE_INT (*arg) = queue->size_buffers;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}
