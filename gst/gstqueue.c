/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Colin Walters <cwalters@gnome.org>
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


#include "gst_private.h"

#include "gstqueue.h"
#include "gstscheduler.h"
#include "gstevent.h"
#include "gstinfo.h"
#include "gsterror.h"

static GstElementDetails gst_queue_details = GST_ELEMENT_DETAILS (
  "Queue",
  "Generic",
  "Simple data queue",
  "Erik Walthinsen <omega@cse.ogi.edu>"
);


/* Queue signals and args */
enum {
  SIGNAL_UNDERRUN,
  SIGNAL_RUNNING,
  SIGNAL_OVERRUN,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FIXME: don't we have another way of doing this
   * "Gstreamer format" (frame/byte/time) queries? */
  ARG_CUR_LEVEL_BUFFERS,
  ARG_CUR_LEVEL_BYTES,
  ARG_CUR_LEVEL_TIME,
  ARG_MAX_SIZE_BUFFERS,
  ARG_MAX_SIZE_BYTES,
  ARG_MAX_SIZE_TIME,
  ARG_MIN_THRESHOLD_BUFFERS,
  ARG_MIN_THRESHOLD_BYTES,
  ARG_MIN_THRESHOLD_TIME,
  ARG_LEAKY,
  ARG_MAY_DEADLOCK,
  ARG_BLOCK_TIMEOUT
  /* FILL ME */
};

typedef struct _GstQueueEventResponse {
  GstEvent *event;
  gboolean ret, handled;
} GstQueueEventResponse;

static void	gst_queue_base_init		(GstQueueClass *klass);
static void	gst_queue_class_init		(GstQueueClass *klass);
static void	gst_queue_init			(GstQueue      *queue);
static void 	gst_queue_dispose 		(GObject       *object);

static void	gst_queue_set_property		(GObject       *object,
						 guint          prop_id, 
						 const GValue  *value,
						 GParamSpec    *pspec);
static void	gst_queue_get_property		(GObject       *object,
						 guint          prop_id, 
						 GValue        *value,
						 GParamSpec    *pspec);

static void	gst_queue_chain			(GstPad        *pad,
						 GstData       *data);
static GstData *gst_queue_get			(GstPad        *pad);
	
static gboolean gst_queue_handle_src_event 	(GstPad        *pad,
						 GstEvent      *event);

static GstCaps *gst_queue_getcaps               (GstPad *pad);
static GstPadLinkReturn
                gst_queue_link                  (GstPad *pad,
                                                 const GstCaps *caps);
static void	gst_queue_locked_flush		(GstQueue      *queue);

static GstElementStateReturn
		gst_queue_change_state		(GstElement    *element);
static gboolean	gst_queue_release_locks		(GstElement    *element);

  
#define GST_TYPE_QUEUE_LEAKY (queue_leaky_get_type ())

static GType
queue_leaky_get_type (void)
{
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
static guint gst_queue_signals[LAST_SIGNAL] = { 0 };

GType
gst_queue_get_type (void) 
{
  static GType queue_type = 0;

  if (!queue_type) {
    static const GTypeInfo queue_info = {
      sizeof (GstQueueClass),
      (GBaseInitFunc) gst_queue_base_init,
      NULL,
      (GClassInitFunc) gst_queue_class_init,
      NULL,
      NULL,
      sizeof (GstQueue),
      4,
      (GInstanceInitFunc) gst_queue_init,
      NULL
    };

    queue_type = g_type_register_static (GST_TYPE_ELEMENT,
					 "GstQueue", &queue_info, 0);
  }

  return queue_type;
}

static void
gst_queue_base_init (GstQueueClass *klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (gstelement_class, &gst_queue_details);
}

static void
gst_queue_class_init (GstQueueClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  /* signals */
  gst_queue_signals[SIGNAL_UNDERRUN] =
    g_signal_new ("underrun", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GstQueueClass, underrun), NULL, NULL,
		  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_queue_signals[SIGNAL_RUNNING] =
    g_signal_new ("running", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GstQueueClass, running), NULL, NULL,
		  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_queue_signals[SIGNAL_OVERRUN] =
    g_signal_new ("overrun", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GstQueueClass, overrun), NULL, NULL,
		  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /* properties */
  g_object_class_install_property (gobject_class, ARG_CUR_LEVEL_BYTES,
    g_param_spec_uint ("current-level-bytes", "Current level (kB)",
		       "Current amount of data in the queue (bytes)",
		       0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CUR_LEVEL_BUFFERS,
    g_param_spec_uint ("current-level-buffers", "Current level (buffers)",
		       "Current number of buffers in the queue",
		       0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CUR_LEVEL_TIME,
    g_param_spec_uint64 ("current-level-time", "Current level (ns)",
			 "Current amount of data in the queue (in ns)",
			 0, G_MAXUINT64, 0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, ARG_MAX_SIZE_BYTES,
    g_param_spec_uint ("max-size-bytes", "Max. size (kB)",
		       "Max. amount of data in the queue (bytes, 0=disable)",
		       0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_SIZE_BUFFERS,
    g_param_spec_uint ("max-size-buffers", "Max. size (buffers)",
		       "Max. number of buffers in the queue (0=disable)",
		       0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_SIZE_TIME,
    g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
			 "Max. amount of data in the queue (in ns, 0=disable)",
			 0, G_MAXUINT64, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_MIN_THRESHOLD_BYTES,
    g_param_spec_uint ("min-threshold-bytes", "Min. threshold (kB)",
		       "Min. amount of data in the queue to allow reading (bytes, 0=disable)",
		       0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MIN_THRESHOLD_BUFFERS,
    g_param_spec_uint ("min-threshold-buffers", "Min. threshold (buffers)",
		       "Min. number of buffers in the queue to allow reading (0=disable)",
		       0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MIN_THRESHOLD_TIME,
    g_param_spec_uint64 ("min-threshold-time", "Min. threshold (ns)",
			 "Min. amount of data in the queue to allow reading (in ns, 0=disable)",
			 0, G_MAXUINT64, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_LEAKY,
    g_param_spec_enum ("leaky", "Leaky",
		       "Where the queue leaks, if at all",
		       GST_TYPE_QUEUE_LEAKY, GST_QUEUE_NO_LEAK, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAY_DEADLOCK,
    g_param_spec_boolean ("may_deadlock", "May Deadlock",
			  "The queue may deadlock if it's full and not PLAYING",
			  TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BLOCK_TIMEOUT,
    g_param_spec_uint64 ("block_timeout", "Timeout for Block", 
			 "Nanoseconds until blocked queue times out and returns filler event. "
			 "Value of -1 disables timeout",
			 0, G_MAXUINT64, -1, G_PARAM_READWRITE));

  /* set several parent class virtual functions */
  gobject_class->dispose        = GST_DEBUG_FUNCPTR (gst_queue_dispose);
  gobject_class->set_property 	= GST_DEBUG_FUNCPTR (gst_queue_set_property);
  gobject_class->get_property 	= GST_DEBUG_FUNCPTR (gst_queue_get_property);

  gstelement_class->change_state  = GST_DEBUG_FUNCPTR (gst_queue_change_state);
  gstelement_class->release_locks = GST_DEBUG_FUNCPTR (gst_queue_release_locks);
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
  gst_pad_set_link_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_link));
  gst_pad_set_getcaps_function (queue->sinkpad, GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_active (queue->sinkpad, TRUE);

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_get));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_link_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_link));
  gst_pad_set_getcaps_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_event_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_handle_src_event));
  gst_pad_set_active (queue->srcpad, TRUE);

  queue->cur_level.buffers	= 0; /* no content */
  queue->cur_level.bytes	= 0; /* no content */
  queue->cur_level.time		= 0; /* no content */
  queue->max_size.buffers	= 100; /* 100 buffers */
  queue->max_size.bytes		= 10 * 1024 * 1024; /* 10 MB */
  queue->max_size.time		= GST_SECOND; /* 1 s. */
  queue->min_threshold.buffers	= 0; /* no threshold */
  queue->min_threshold.bytes	= 0; /* no threshold */
  queue->min_threshold.time	= 0; /* no threshold */

  queue->leaky = GST_QUEUE_NO_LEAK;
  queue->may_deadlock = TRUE;
  queue->block_timeout = GST_CLOCK_TIME_NONE;
  queue->interrupt = FALSE;
  queue->flush = FALSE;

  queue->qlock = g_mutex_new ();
  queue->item_add = g_cond_new ();
  queue->item_del = g_cond_new ();
  queue->event_done = g_cond_new ();
  queue->events = g_queue_new ();
  queue->queue = g_queue_new ();

  GST_CAT_DEBUG_OBJECT (GST_CAT_THREAD, queue,
			"initialized queue's not_empty & not_full conditions");
}

static void
gst_queue_dispose (GObject *object)
{
  GstQueue *queue = GST_QUEUE (object);

  gst_element_set_state (GST_ELEMENT (queue), GST_STATE_NULL);

  while (!g_queue_is_empty (queue->queue)) {
    GstData *data = g_queue_pop_head (queue->queue);
    gst_data_unref (data);
  }
  g_queue_free (queue->queue);
  g_mutex_free (queue->qlock);
  g_cond_free (queue->item_add);
  g_cond_free (queue->item_del);
  g_cond_free (queue->event_done);
  while (!g_queue_is_empty (queue->events)) {
    GstEvent *event = g_queue_pop_head (queue->events);
    gst_event_unref (event);
  }

  if (G_OBJECT_CLASS (parent_class)->dispose)
    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_queue_getcaps (GstPad *pad)
{
  GstQueue *queue;

  queue = GST_QUEUE (gst_pad_get_parent (pad));

  if (queue->cur_level.bytes > 0) {
    return gst_caps_copy (queue->negotiated_caps);
  }

  return gst_pad_proxy_getcaps (pad);
}

static GstPadLinkReturn
gst_queue_link (GstPad *pad, const GstCaps *caps)
{
  GstQueue *queue;
  GstPadLinkReturn link_ret;

  queue = GST_QUEUE (gst_pad_get_parent (pad));

  if (queue->cur_level.bytes > 0) {
    if (gst_caps_is_equal_fixed (caps, queue->negotiated_caps)) {
      return GST_PAD_LINK_OK;
    }
    return GST_PAD_LINK_REFUSED;
  }

  link_ret = gst_pad_proxy_pad_link (pad, caps);

  if (GST_PAD_LINK_SUCCESSFUL (link_ret)) {
    /* we store an extra copy of the negotiated caps, just in case
     * the pads become unnegotiated while we have buffers */
    gst_caps_replace (&queue->negotiated_caps, gst_caps_copy (caps));
  }

  return link_ret;
}

static void
gst_queue_locked_flush (GstQueue *queue)
{
  while (!g_queue_is_empty (queue->queue)) {
    GstData *data = g_queue_pop_head (queue->queue);
    /* First loose the reference we added when putting that data in the queue */
    gst_data_unref (data);
    /* Then loose another reference because we are supposed to destroy that
       data when flushing */
    gst_data_unref (data);
  }
  queue->timeval = NULL;
  queue->cur_level.buffers = 0;
  queue->cur_level.bytes = 0;
  queue->cur_level.time = 0;

  /* make sure any pending buffers to be added are flushed too */
  queue->flush = TRUE;

  /* we deleted something... */
  g_cond_signal (queue->item_del);
}

static void
gst_queue_handle_pending_events (GstQueue *queue)
{
  /* check for events to send upstream */
  while (!g_queue_is_empty (queue->events)){
    GstQueueEventResponse *er = g_queue_pop_head (queue->events);
    GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue, "sending event upstream");
    er->ret = gst_pad_event_default (queue->srcpad, er->event);
    er->handled = TRUE;
    g_cond_signal (queue->event_done);
    GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue, "event sent");
  }
}

#define STATUS(queue, msg) \
  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue, \
		      "(%s:%s) " msg ": %u of %u-%u buffers, %u of %u-%u " \
		      "bytes, %" G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT \
		      "-%" G_GUINT64_FORMAT " ns, %u elements", \
		      GST_DEBUG_PAD_NAME (pad), \
		      queue->cur_level.buffers, \
		      queue->min_threshold.buffers, \
		      queue->max_size.buffers, \
		      queue->cur_level.bytes, \
		      queue->min_threshold.bytes, \
		      queue->max_size.bytes, \
		      queue->cur_level.time, \
		      queue->min_threshold.time, \
		      queue->max_size.time, \
		      queue->queue->length)

static void
gst_queue_chain (GstPad  *pad,
		 GstData *data)
{
  GstQueue *queue;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (data != NULL);

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));
  
restart:
  /* we have to lock the queue since we span threads */
  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue, "locking t:%p", g_thread_self ());
  g_mutex_lock (queue->qlock);
  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue, "locked t:%p", g_thread_self ());

  gst_queue_handle_pending_events (queue);

  /* assume don't need to flush this buffer when the queue is filled */
  queue->flush = FALSE;
  
  if (GST_IS_EVENT (data)) {
    switch (GST_EVENT_TYPE (data)) {
      case GST_EVENT_FLUSH:
        STATUS (queue, "received flush event");
        gst_queue_locked_flush (queue);
        STATUS (queue, "after flush");
	break;
      case GST_EVENT_EOS:
	STATUS (queue, "received EOS");
	break;
      default:
	/* we put the event in the queue, we don't have to act ourselves */
        GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue,
			    "adding event %p of type %d",
			    data, GST_EVENT_TYPE (data));
	break;
    }
  }
  
  if (GST_IS_BUFFER (data))
    GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue, 
                        "adding buffer %p of size %d",
			data, GST_BUFFER_SIZE (data));

  /* We make space available if we're "full" according to whatever
   * the user defined as "full". Note that this only applies to buffers.
   * We always handle events and they don't count in our statistics. */
  if (GST_IS_BUFFER (data) &&
      ((queue->max_size.buffers > 0 &&
        queue->cur_level.buffers >= queue->max_size.buffers) ||
       (queue->max_size.bytes > 0 &&
        queue->cur_level.bytes >= queue->max_size.bytes) ||
       (queue->max_size.time > 0 &&
        queue->cur_level.time >= queue->max_size.time))) {
    g_mutex_unlock (queue->qlock);
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_OVERRUN], 0);
    g_mutex_lock (queue->qlock);

    /* how are we going to make space for this buffer? */
    switch (queue->leaky) {
      /* leak current buffer */
      case GST_QUEUE_LEAK_UPSTREAM:
        GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue,
			      "queue is full, leaking buffer on upstream end");
        /* now we can clean up and exit right away */
        g_mutex_unlock (queue->qlock);
        goto out_unref;

      /* leak first buffer in the queue */
      case GST_QUEUE_LEAK_DOWNSTREAM: {
        /* this is a bit hacky. We'll manually iterate the list
         * and find the first buffer from the head on. We'll
         * unref that and "fix up" the GQueue object... */
        GList *item;
        GstData *leak = NULL;

        GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue,
			      "queue is full, leaking buffer on downstream end");

        for (item = queue->queue->head; item != NULL; item = item->next) {
          if (GST_IS_BUFFER (item->data)) {
            leak = item->data;
            break;
          }
        }

        /* if we didn't find anything, it means we have no buffers
         * in here. That cannot happen, since we had >= 1 bufs */
        g_assert (leak);

        /* Now remove it from the list, fixing up the GQueue
         * CHECKME: is a queue->head the first or the last item? */
        item = g_list_delete_link (queue->queue->head, item);
        queue->queue->head = g_list_first (item);
        queue->queue->tail = g_list_last (item);
        queue->queue->length--;

        /* and unref the data at the end. Twice, because we keep a ref
         * to make things read-only. Also keep our list uptodate. */
        queue->cur_level.bytes -= GST_BUFFER_SIZE (data);
        queue->cur_level.buffers --;
        if (GST_BUFFER_DURATION (data) != GST_CLOCK_TIME_NONE)
          queue->cur_level.time -= GST_BUFFER_DURATION (data);

        gst_data_unref (data);
        gst_data_unref (data);
        break;
      }

      default:
        g_warning ("Unknown leaky type, using default");
        /* fall-through */

      /* don't leak. Instead, wait for space to be available */
      case GST_QUEUE_NO_LEAK:
        STATUS (queue, "pre-full wait");

        while ((queue->max_size.buffers > 0 &&
	        queue->cur_level.buffers >= queue->max_size.buffers) ||
	       (queue->max_size.bytes > 0 &&
	        queue->cur_level.bytes >= queue->max_size.bytes) ||
	       (queue->max_size.time > 0 &&
	        queue->cur_level.time >= queue->max_size.time)) {
          /* if there's a pending state change for this queue
           * or its manager, switch back to iterator so bottom
           * half of state change executes */
          if (queue->interrupt) {
            GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue, "interrupted");
            g_mutex_unlock (queue->qlock);
            if (gst_scheduler_interrupt (gst_pad_get_scheduler (queue->sinkpad),
					 GST_ELEMENT (queue))) {
              goto out_unref;
            }
	    /* if we got here because we were unlocked after a
             * flush, we don't need to add the buffer to the
             * queue again */
	    if (queue->flush) {
              GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue,
				    "not adding pending buffer after flush");
	      goto out_unref;
	    }
            GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue,
				  "adding pending buffer after interrupt");
	    goto restart;
          }

          if (GST_STATE (queue) != GST_STATE_PLAYING) {
	    /* this means the other end is shut down. Try to
             * signal to resolve the error */
	    if (!queue->may_deadlock) {
              g_mutex_unlock (queue->qlock);
              gst_data_unref (data);
              GST_ELEMENT_ERROR (queue, CORE, THREAD, (NULL),
				 ("deadlock found, shutting down source pad elements"));
	      /* we don't go to out_unref here, since we want to
               * unref the buffer *before* calling GST_ELEMENT_ERROR */
	      return;
	    } else {
              GST_CAT_WARNING_OBJECT (GST_CAT_DATAFLOW, queue,
				      "%s: waiting for the app to restart "
				      "source pad elements",
				      GST_ELEMENT_NAME (queue));
	    }
          }

          /* OK, we've got a serious issue here. Imagine the situation
           * where the puller (next element) is sending an event here,
           * so it cannot pull events from the queue, and we cannot
           * push data further because the queue is 'full' and therefore,
           * we wait here (and do not handle events): deadlock! to solve
           * that, we handle pending upstream events here, too. */
          gst_queue_handle_pending_events (queue);

          STATUS (queue, "waiting for item_del signal");
          g_cond_wait (queue->item_del, queue->qlock);
          STATUS (queue, "received item_del signal");
        }

        STATUS (queue, "post-full wait");
        g_mutex_unlock (queue->qlock);
        g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_RUNNING], 0);
        g_mutex_lock (queue->qlock);
        break;
    }
  }

  /* put the buffer on the tail of the list. We keep a reference,
   * so that the data is read-only while in here. There's a good
   * reason to do so: we have a size and time counter, and any
   * modification to the content could change any of the two. */
  gst_data_ref (data);
  g_queue_push_tail (queue->queue, data);

  /* Note that we only add buffers (not events) to the statistics */
  if (GST_IS_BUFFER (data)) {
    queue->cur_level.buffers++;
    queue->cur_level.bytes += GST_BUFFER_SIZE (data);
    if (GST_BUFFER_DURATION (data) != GST_CLOCK_TIME_NONE)
      queue->cur_level.time += GST_BUFFER_DURATION (data);
  }

  STATUS (queue, "+ level");

  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue, "signalling item_add");
  g_cond_signal (queue->item_add);
  g_mutex_unlock (queue->qlock);

  return;

out_unref:
  gst_data_unref (data);
  return;
}

static GstData *
gst_queue_get (GstPad *pad)
{
  GstQueue *queue;
  GstData *data;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  queue = GST_QUEUE (gst_pad_get_parent (pad));

restart:
  /* have to lock for thread-safety */
  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue,
		      "locking t:%p", g_thread_self ());
  g_mutex_lock (queue->qlock);
  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue,
		      "locked t:%p", g_thread_self ());

  if (queue->queue->length == 0 ||
      (queue->min_threshold.buffers > 0 &&
       queue->cur_level.buffers < queue->min_threshold.buffers) ||
      (queue->min_threshold.bytes > 0 &&
       queue->cur_level.bytes < queue->min_threshold.bytes) ||
      (queue->min_threshold.time > 0 &&
       queue->cur_level.time < queue->min_threshold.time)) {
    g_mutex_unlock (queue->qlock);
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_UNDERRUN], 0);
    g_mutex_lock (queue->qlock);

    STATUS (queue, "pre-empty wait");
    while (queue->queue->length == 0 ||
           (queue->min_threshold.buffers > 0 &&
            queue->cur_level.buffers < queue->min_threshold.buffers) ||
           (queue->min_threshold.bytes > 0 &&
            queue->cur_level.bytes < queue->min_threshold.bytes) ||
           (queue->min_threshold.time > 0 &&
            queue->cur_level.time < queue->min_threshold.time)) {
      /* if there's a pending state change for this queue or its
       * manager, switch back to iterator so bottom half of state
       * change executes. */
      if (queue->interrupt) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue, "interrupted");
        g_mutex_unlock (queue->qlock);
        if (gst_scheduler_interrupt (gst_pad_get_scheduler (queue->srcpad),
				     GST_ELEMENT (queue)))
          return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
        goto restart;
      }
      if (GST_STATE (queue) != GST_STATE_PLAYING) {
        /* this means the other end is shut down */
        if (!queue->may_deadlock) {
          g_mutex_unlock (queue->qlock);
          GST_ELEMENT_ERROR (queue, CORE, THREAD, (NULL),
			    ("deadlock found, shutting down sink pad elements"));
          goto restart;
        } else {
          GST_CAT_WARNING_OBJECT (GST_CAT_DATAFLOW, queue,
				  "%s: waiting for the app to restart "
				  "source pad elements",
				  GST_ELEMENT_NAME (queue));
        }
      }

      STATUS (queue, "waiting for item_add");
    
      if (queue->block_timeout != GST_CLOCK_TIME_NONE) {
        GTimeVal timeout;
        g_get_current_time (&timeout);
        g_time_val_add (&timeout, queue->block_timeout / 1000);
        if (!g_cond_timed_wait (queue->item_add, queue->qlock, &timeout)){
          g_mutex_unlock (queue->qlock);
	  GST_CAT_WARNING_OBJECT (GST_CAT_DATAFLOW, queue,
				  "Sending filler event");
          return GST_DATA (gst_event_new_filler ());
        }
      } else {
        g_cond_wait (queue->item_add, queue->qlock);
      }
      STATUS (queue, "got item_add signal");
    }

    STATUS (queue, "post-empty wait");
    g_mutex_unlock (queue->qlock);
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_RUNNING], 0);
    g_mutex_lock (queue->qlock);
  }

  /* There's something in the list now, whatever it is */
  data = g_queue_pop_head (queue->queue);
  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue,
		      "retrieved data %p from queue", data);
  
  if (data == NULL)
    return NULL;

  if (GST_IS_BUFFER (data)) {
    /* Update statistics */
    queue->cur_level.buffers--;
    queue->cur_level.bytes -= GST_BUFFER_SIZE (data);
    if (GST_BUFFER_DURATION (data) != GST_CLOCK_TIME_NONE)
      queue->cur_level.time -= GST_BUFFER_DURATION (data);
  }

  /* Now that we're done, we can lose our own reference to
   * the item, since we're no longer in danger. */
  gst_data_unref (data);

  STATUS (queue, "after _get()");

  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, queue, "signalling item_del");
  g_cond_signal (queue->item_del);
  g_mutex_unlock (queue->qlock);

  /* FIXME: I suppose this needs to be locked, since the EOS
   * bit affects the pipeline state. However, that bit is
   * locked too so it'd cause a deadlock. */
  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue,
			      "queue \"%s\" eos",
			      GST_ELEMENT_NAME (queue));
        gst_element_set_eos (GST_ELEMENT (queue));
        break;
      default:
        break;
    }
  }

  return data;
}


static gboolean
gst_queue_handle_src_event (GstPad   *pad,
			    GstEvent *event)
{
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (pad));
  gboolean res;

  g_mutex_lock (queue->qlock);

  if (gst_element_get_state (GST_ELEMENT (queue)) == GST_STATE_PLAYING) {
    GstQueueEventResponse er;

    /* push the event to the queue and wait for upstream consumption */
    er.event = event;
    er.handled = FALSE;
    g_queue_push_tail (queue->events, &er);
    GST_CAT_WARNING_OBJECT (GST_CAT_DATAFLOW, queue,
			    "Preparing for loop for event handler");
    /* see the chain function on why this is here - it prevents a deadlock */
    g_cond_signal (queue->item_del);
    while (!er.handled) {
      GTimeVal timeout;
      g_get_current_time (&timeout);
      g_time_val_add (&timeout, 500 * 1000); /* half a second */
      if (!g_cond_timed_wait (queue->event_done, queue->qlock, &timeout) &&
          !er.handled) {
        GST_CAT_WARNING_OBJECT (GST_CAT_DATAFLOW, queue,
				"timeout in upstream event handling");
        /* remove ourselves from the pending list. Since we're
         * locked, others cannot reference this anymore. */
        queue->queue->head = g_list_remove (queue->queue->head, &er);
        queue->queue->head = g_list_first (queue->queue->head);
        queue->queue->tail = g_list_last (queue->queue->head);
        queue->queue->length--;
        res = FALSE;
        goto handled;
      }
    }
    GST_CAT_WARNING_OBJECT (GST_CAT_DATAFLOW, queue,
			    "Event handled");
    res = er.ret;
  } else {
    res = gst_pad_event_default (pad, event); 

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_FLUSH:
        GST_CAT_DEBUG_OBJECT (GST_CAT_DATAFLOW, queue,
			      "FLUSH event, flushing queue\n");
        gst_queue_locked_flush (queue);
        break;
      case GST_EVENT_SEEK:
        if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH) {
          gst_queue_locked_flush (queue);
        }
      default:
        break;
    }
  }
handled:
  g_mutex_unlock (queue->qlock);

  return res;
}

static gboolean
gst_queue_release_locks (GstElement *element)
{
  GstQueue *queue;

  queue = GST_QUEUE (element);

  g_mutex_lock (queue->qlock);
  queue->interrupt = TRUE;
  g_cond_signal (queue->item_add);
  g_cond_signal (queue->item_del);
  g_mutex_unlock (queue->qlock);

  return TRUE;
}

static GstElementStateReturn
gst_queue_change_state (GstElement *element)
{
  GstQueue *queue;
  GstElementStateReturn ret = GST_STATE_SUCCESS;

  queue = GST_QUEUE (element);

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element, "starting state change");

  /* lock the queue so another thread (not in sync with this thread's state)
   * can't call this queue's _get (or whatever)
   */
  g_mutex_lock (queue->qlock);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_queue_locked_flush (queue);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (!GST_PAD_IS_LINKED (queue->sinkpad)) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, queue,
			      "queue %s is not linked",
			      GST_ELEMENT_NAME (queue));
        /* FIXME can this be? */
        g_cond_signal (queue->item_add);

        ret = GST_STATE_FAILURE;
        goto error;
      } else {
        GstScheduler *src_sched, *sink_sched;
	    
        src_sched = gst_pad_get_scheduler (GST_PAD (queue->srcpad));
        sink_sched = gst_pad_get_scheduler (GST_PAD (queue->sinkpad));

        if (src_sched == sink_sched) {
          GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, queue,
				"queue %s does not connect different schedulers", 
				GST_ELEMENT_NAME (queue));

	  g_warning ("queue %s does not connect different schedulers",
		     GST_ELEMENT_NAME (queue));

	  ret = GST_STATE_FAILURE;
	  goto error;
        }
      }
      queue->interrupt = FALSE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_queue_locked_flush (queue);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  /* this is an ugly hack to make sure our pads are always active.
   * Reason for this is that pad activation for the queue element
   * depends on 2 schedulers (ugh) */
  gst_pad_set_active (queue->sinkpad, TRUE);
  gst_pad_set_active (queue->srcpad, TRUE);

error:
  g_mutex_unlock (queue->qlock);

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element, "done with state change");

  return ret;
}


static void
gst_queue_set_property (GObject      *object,
			guint         prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
  GstQueue *queue = GST_QUEUE (object);

  /* someone could change levels here, and since this
   * affects the get/put funcs, we need to lock for safety. */
  g_mutex_lock (queue->qlock);

  switch (prop_id) {
    case ARG_MAX_SIZE_BYTES:
      queue->max_size.bytes = g_value_get_uint (value);
      break;
    case ARG_MAX_SIZE_BUFFERS:
      queue->max_size.buffers = g_value_get_uint (value);
      break;
    case ARG_MAX_SIZE_TIME:
      queue->max_size.time = g_value_get_uint64 (value);
      break;
    case ARG_MIN_THRESHOLD_BYTES:
      queue->min_threshold.bytes = g_value_get_uint (value);
      break;
    case ARG_MIN_THRESHOLD_BUFFERS:
      queue->min_threshold.buffers = g_value_get_uint (value);
      break;
    case ARG_MIN_THRESHOLD_TIME:
      queue->min_threshold.time = g_value_get_uint64 (value);
      break;
    case ARG_LEAKY:
      queue->leaky = g_value_get_enum (value);
      break;
    case ARG_MAY_DEADLOCK:
      queue->may_deadlock = g_value_get_boolean (value);
      break;
    case ARG_BLOCK_TIMEOUT:
      queue->block_timeout = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (queue->qlock);
}

static void
gst_queue_get_property (GObject    *object,
			guint       prop_id,
			GValue     *value,
			GParamSpec *pspec)
{
  GstQueue *queue = GST_QUEUE (object);

  switch (prop_id) {
    case ARG_CUR_LEVEL_BYTES:
      g_value_set_uint (value, queue->cur_level.bytes);
      break;
    case ARG_CUR_LEVEL_BUFFERS:
      g_value_set_uint (value, queue->cur_level.buffers);
      break;
    case ARG_CUR_LEVEL_TIME:
      g_value_set_uint64 (value, queue->cur_level.time);
      break;
    case ARG_MAX_SIZE_BYTES:
      g_value_set_uint (value, queue->max_size.bytes);
      break;
    case ARG_MAX_SIZE_BUFFERS:
      g_value_set_uint (value, queue->max_size.buffers);
      break;
    case ARG_MAX_SIZE_TIME:
      g_value_set_uint64 (value, queue->max_size.time);
      break;
    case ARG_MIN_THRESHOLD_BYTES:
      g_value_set_uint (value, queue->min_threshold.bytes);
      break;
    case ARG_MIN_THRESHOLD_BUFFERS:
      g_value_set_uint (value, queue->min_threshold.buffers);
      break;
    case ARG_MIN_THRESHOLD_TIME:
      g_value_set_uint64 (value, queue->min_threshold.time);
      break;
    case ARG_LEAKY:
      g_value_set_enum (value, queue->leaky);
      break;
    case ARG_MAY_DEADLOCK:
      g_value_set_boolean (value, queue->may_deadlock);
      break;
    case ARG_BLOCK_TIMEOUT:
      g_value_set_uint64 (value, queue->block_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
