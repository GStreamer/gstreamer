/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Colin Walters <cwalters@gnome.org>
 *                    2005 Wim Taymans <wim@fluendo.com>
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
#include "gstpipeline.h"
#include "gstevent.h"
#include "gstinfo.h"
#include "gsterror.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (queue_dataflow);
#define GST_CAT_DEFAULT (queue_dataflow)

#define STATUS(queue, msg) \
  GST_CAT_LOG_OBJECT (queue_dataflow, queue, \
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

static GstElementDetails gst_queue_details = GST_ELEMENT_DETAILS ("Queue",
    "Generic",
    "Simple data queue",
    "Erik Walthinsen <omega@cse.ogi.edu>");


/* Queue signals and args */
enum
{
  SIGNAL_UNDERRUN,
  SIGNAL_RUNNING,
  SIGNAL_OVERRUN,
  LAST_SIGNAL
};

enum
{
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

#define GST_QUEUE_MUTEX_LOCK G_STMT_START {				\
  GST_CAT_LOG_OBJECT (queue_dataflow, queue,				\
      "locking qlock from thread %p",					\
      g_thread_self ());						\
  g_mutex_lock (queue->qlock);						\
  GST_CAT_LOG_OBJECT (queue_dataflow, queue,				\
      "locked qlock from thread %p",					\
      g_thread_self ());						\
} G_STMT_END

#define GST_QUEUE_MUTEX_UNLOCK G_STMT_START {				\
  GST_CAT_LOG_OBJECT (queue_dataflow, queue,				\
      "unlocking qlock from thread %p",					\
      g_thread_self ());						\
  g_mutex_unlock (queue->qlock);					\
} G_STMT_END


static void gst_queue_base_init (GstQueueClass * klass);
static void gst_queue_class_init (GstQueueClass * klass);
static void gst_queue_init (GstQueue * queue);
static void gst_queue_finalize (GObject * object);

static void gst_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_queue_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_queue_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_queue_loop (GstPad * pad);

static gboolean gst_queue_handle_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_queue_handle_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_queue_handle_src_query (GstPad * pad, GstQuery * query);

static GstCaps *gst_queue_getcaps (GstPad * pad);
static GstPadLinkReturn gst_queue_link_sink (GstPad * pad, GstPad * peer);
static GstPadLinkReturn gst_queue_link_src (GstPad * pad, GstPad * peer);
static void gst_queue_locked_flush (GstQueue * queue);

static gboolean gst_queue_src_activate_push (GstPad * pad, gboolean active);
static gboolean gst_queue_sink_activate_push (GstPad * pad, gboolean active);
static GstElementStateReturn gst_queue_change_state (GstElement * element);


#define GST_TYPE_QUEUE_LEAKY (queue_leaky_get_type ())

static GType
queue_leaky_get_type (void)
{
  static GType queue_leaky_type = 0;
  static GEnumValue queue_leaky[] = {
    {GST_QUEUE_NO_LEAK, "0", "Not Leaky"},
    {GST_QUEUE_LEAK_UPSTREAM, "1", "Leaky on Upstream"},
    {GST_QUEUE_LEAK_DOWNSTREAM, "2", "Leaky on Downstream"},
    {0, NULL, NULL},
  };

  if (!queue_leaky_type) {
    queue_leaky_type = g_enum_register_static ("GstQueueLeaky", queue_leaky);
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
      0,
      (GInstanceInitFunc) gst_queue_init,
      NULL
    };

    queue_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstQueue", &queue_info, 0);
    GST_DEBUG_CATEGORY_INIT (queue_dataflow, "queue_dataflow", 0,
        "dataflow inside the queue element");
  }

  return queue_type;
}

static void
gst_queue_base_init (GstQueueClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_queue_details);
}

static void
gst_queue_class_init (GstQueueClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_queue_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_queue_get_property);

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
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_queue_finalize);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_queue_change_state);
}

static void
gst_queue_init (GstQueue * queue)
{
  queue->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  gst_pad_set_chain_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_chain));
  gst_pad_set_activatepush_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_sink_activate_push));
  gst_pad_set_event_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_handle_sink_event));
  gst_pad_set_link_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_link_sink));
  gst_pad_set_getcaps_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_bufferalloc_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_bufferalloc));
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);

  queue->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  gst_pad_set_activatepush_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_src_activate_push));
  gst_pad_set_link_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_link_src));
  gst_pad_set_getcaps_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_event_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_handle_src_event));
  gst_pad_set_query_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_handle_src_query));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);

  queue->cur_level.buffers = 0; /* no content */
  queue->cur_level.bytes = 0;   /* no content */
  queue->cur_level.time = 0;    /* no content */
  queue->max_size.buffers = 200;        /* 200 buffers */
  queue->max_size.bytes = 10 * 1024 * 1024;     /* 10 MB */
  queue->max_size.time = GST_SECOND;    /* 1 s. */
  queue->min_threshold.buffers = 0;     /* no threshold */
  queue->min_threshold.bytes = 0;       /* no threshold */
  queue->min_threshold.time = 0;        /* no threshold */

  queue->leaky = GST_QUEUE_NO_LEAK;
  queue->may_deadlock = TRUE;
  queue->block_timeout = GST_CLOCK_TIME_NONE;
  queue->flushing = FALSE;

  queue->qlock = g_mutex_new ();
  queue->item_add = g_cond_new ();
  queue->item_del = g_cond_new ();
  queue->queue = g_queue_new ();

  GST_CAT_DEBUG_OBJECT (GST_CAT_THREAD, queue,
      "initialized queue's not_empty & not_full conditions");
}

/* called only once, as opposed to dispose */
static void
gst_queue_finalize (GObject * object)
{
  GstQueue *queue = GST_QUEUE (object);

  GST_DEBUG_OBJECT (queue, "finalizing queue");

  while (!g_queue_is_empty (queue->queue)) {
    GstMiniObject *data = g_queue_pop_head (queue->queue);

    gst_mini_object_unref (data);
  }
  g_queue_free (queue->queue);
  GST_CAT_DEBUG_OBJECT (GST_CAT_THREAD, queue, "free mutex");
  g_mutex_free (queue->qlock);
  GST_CAT_DEBUG_OBJECT (GST_CAT_THREAD, queue, "done free mutex");
  g_cond_free (queue->item_add);
  g_cond_free (queue->item_del);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_queue_getcaps (GstPad * pad)
{
  GstQueue *queue;
  GstPad *otherpad;
  GstCaps *result;

  queue = GST_QUEUE (GST_PAD_PARENT (pad));

  otherpad = (pad == queue->srcpad ? queue->sinkpad : queue->srcpad);
  result = gst_pad_peer_get_caps (otherpad);
  if (result == NULL)
    result = gst_caps_new_any ();

  return result;
}

static GstPadLinkReturn
gst_queue_link_sink (GstPad * pad, GstPad * peer)
{
  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_queue_link_src (GstPad * pad, GstPad * peer)
{
  GstPadLinkReturn result = GST_PAD_LINK_OK;

  /* FIXME, see if we need to push or get pulled */
  if (GST_PAD_LINKFUNC (peer))
    result = GST_PAD_LINKFUNC (peer) (peer, pad);

  return result;
}

static GstFlowReturn
gst_queue_bufferalloc (GstPad * pad, guint64 offset, guint size, GstCaps * caps,
    GstBuffer ** buf)
{
  GstQueue *queue;
  GstFlowReturn result;

  queue = GST_QUEUE (GST_PAD_PARENT (pad));

  result = gst_pad_alloc_buffer (queue->srcpad, offset, size, caps, buf);

  return result;
}


static void
gst_queue_locked_flush (GstQueue * queue)
{
  while (!g_queue_is_empty (queue->queue)) {
    GstMiniObject *data = g_queue_pop_head (queue->queue);

    /* Then loose another reference because we are supposed to destroy that
       data when flushing */
    gst_mini_object_unref (data);
  }
  queue->cur_level.buffers = 0;
  queue->cur_level.bytes = 0;
  queue->cur_level.time = 0;

  /* we deleted something... */
  g_cond_signal (queue->item_del);
}

#define STATUS(queue, msg) \
  GST_CAT_LOG_OBJECT (queue_dataflow, queue, \
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

static gboolean
gst_queue_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      STATUS (queue, "received flush event");
      /* forward event */
      gst_pad_event_default (pad, event);
      if (GST_EVENT_FLUSH_DONE (event)) {
        GST_QUEUE_MUTEX_LOCK;
        queue->flushing = FALSE;
        gst_pad_start_task (queue->srcpad, (GstTaskFunction) gst_queue_loop,
            queue->srcpad);
        GST_QUEUE_MUTEX_UNLOCK;
      } else {
        /* now unblock the chain function */
        GST_QUEUE_MUTEX_LOCK;
        queue->flushing = TRUE;
        gst_queue_locked_flush (queue);
        /* unblock the loop function */
        g_cond_signal (queue->item_add);
        GST_QUEUE_MUTEX_UNLOCK;

        STATUS (queue, "after flush");

        /* make sure it stops */
        gst_pad_pause_task (queue->srcpad);
        GST_CAT_LOG_OBJECT (queue_dataflow, queue, "loop stopped");
      }
      goto done;
    case GST_EVENT_EOS:
      STATUS (queue, "received EOS");
      break;
    default:
      /* we put the event in the queue, we don't have to act ourselves */
      GST_CAT_LOG_OBJECT (queue_dataflow, queue,
          "adding event %p of type %d", event, GST_EVENT_TYPE (event));
      break;
  }

  GST_QUEUE_MUTEX_LOCK;
  g_queue_push_tail (queue->queue, event);
  g_cond_signal (queue->item_add);

  GST_QUEUE_MUTEX_UNLOCK;
done:

  return TRUE;
}

static gboolean
gst_queue_is_empty (GstQueue * queue)
{
  return (queue->queue->length == 0 ||
      (queue->min_threshold.buffers > 0 &&
          queue->cur_level.buffers < queue->min_threshold.buffers) ||
      (queue->min_threshold.bytes > 0 &&
          queue->cur_level.bytes < queue->min_threshold.bytes) ||
      (queue->min_threshold.time > 0 &&
          queue->cur_level.time < queue->min_threshold.time));
}

static gboolean
gst_queue_is_filled (GstQueue * queue)
{
  return (((queue->max_size.buffers > 0 &&
              queue->cur_level.buffers >= queue->max_size.buffers) ||
          (queue->max_size.bytes > 0 &&
              queue->cur_level.bytes >= queue->max_size.bytes) ||
          (queue->max_size.time > 0 &&
              queue->cur_level.time >= queue->max_size.time)));
}


static GstFlowReturn
gst_queue_chain (GstPad * pad, GstBuffer * buffer)
{
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  /* we have to lock the queue since we span threads */
  GST_QUEUE_MUTEX_LOCK;

  GST_CAT_LOG_OBJECT (queue_dataflow, queue,
      "adding buffer %p of size %d", buffer, GST_BUFFER_SIZE (buffer));

  /* We make space available if we're "full" according to whatever
   * the user defined as "full". Note that this only applies to buffers.
   * We always handle events and they don't count in our statistics. */
  while (gst_queue_is_filled (queue)) {
    GST_QUEUE_MUTEX_UNLOCK;
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_OVERRUN], 0);
    GST_QUEUE_MUTEX_LOCK;

    /* how are we going to make space for this buffer? */
    switch (queue->leaky) {
        /* leak current buffer */
      case GST_QUEUE_LEAK_UPSTREAM:
        GST_CAT_DEBUG_OBJECT (queue_dataflow, queue,
            "queue is full, leaking buffer on upstream end");
        /* now we can clean up and exit right away */
        goto out_unref;

        /* leak first buffer in the queue */
      case GST_QUEUE_LEAK_DOWNSTREAM:{
        /* this is a bit hacky. We'll manually iterate the list
         * and find the first buffer from the head on. We'll
         * unref that and "fix up" the GQueue object... */
        GList *item;
        GstMiniObject *leak = NULL;

        GST_CAT_DEBUG_OBJECT (queue_dataflow, queue,
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

        /* and unref the buffer at the end. Twice, because we keep a ref
         * to make things read-only. Also keep our list uptodate. */
        queue->cur_level.bytes -= GST_BUFFER_SIZE (buffer);
        queue->cur_level.buffers--;
        if (GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE)
          queue->cur_level.time -= GST_BUFFER_DURATION (buffer);

        gst_buffer_unref (buffer);
        gst_buffer_unref (buffer);
        break;
      }

      default:
        g_warning ("Unknown leaky type, using default");
        /* fall-through */

        /* don't leak. Instead, wait for space to be available */
      case GST_QUEUE_NO_LEAK:
        STATUS (queue, "pre-full wait");

        while (gst_queue_is_filled (queue)) {
          STATUS (queue, "waiting for item_del signal from thread using qlock");
          g_cond_wait (queue->item_del, queue->qlock);

          if (queue->flushing)
            goto out_flushing;

          /* if there's a pending state change for this queue
           * or its manager, switch back to iterator so bottom
           * half of state change executes */
          STATUS (queue, "received item_del signal from thread using qlock");
        }

        STATUS (queue, "post-full wait");
        GST_QUEUE_MUTEX_UNLOCK;
        g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_RUNNING], 0);
        GST_QUEUE_MUTEX_LOCK;
        break;
    }
  }

  g_queue_push_tail (queue->queue, buffer);

  /* add buffer to the statistics */
  queue->cur_level.buffers++;
  queue->cur_level.bytes += GST_BUFFER_SIZE (buffer);
  if (GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE)
    queue->cur_level.time += GST_BUFFER_DURATION (buffer);

  STATUS (queue, "+ level");

  GST_CAT_LOG_OBJECT (queue_dataflow, queue, "signalling item_add");
  g_cond_signal (queue->item_add);
  GST_QUEUE_MUTEX_UNLOCK;

  return GST_FLOW_OK;

out_unref:
  {
    GST_QUEUE_MUTEX_UNLOCK;

    gst_buffer_unref (buffer);

    return GST_FLOW_OK;
  }
out_flushing:
  {
    GST_CAT_LOG_OBJECT (queue_dataflow, queue, "exit because of flush");
    GST_QUEUE_MUTEX_UNLOCK;
    gst_pad_pause_task (queue->srcpad);

    gst_buffer_unref (buffer);

    return GST_FLOW_WRONG_STATE;
  }
}

static void
gst_queue_loop (GstPad * pad)
{
  GstQueue *queue;
  GstMiniObject *data;
  gboolean restart = TRUE;

  queue = GST_QUEUE (GST_PAD_PARENT (pad));

  /* have to lock for thread-safety */
  GST_QUEUE_MUTEX_LOCK;

restart:
  while (gst_queue_is_empty (queue)) {
    GST_QUEUE_MUTEX_UNLOCK;
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_UNDERRUN], 0);
    GST_QUEUE_MUTEX_LOCK;

    STATUS (queue, "pre-empty wait");
    while (gst_queue_is_empty (queue)) {
      STATUS (queue, "waiting for item_add");

      if (queue->flushing)
        goto out_flushing;

      GST_LOG_OBJECT (queue, "doing g_cond_wait using qlock from thread %p",
          g_thread_self ());
      g_cond_wait (queue->item_add, queue->qlock);

      if (queue->flushing)
        goto out_flushing;

      GST_LOG_OBJECT (queue, "done g_cond_wait using qlock from thread %p",
          g_thread_self ());
      STATUS (queue, "got item_add signal");
    }

    STATUS (queue, "post-empty wait");
    GST_QUEUE_MUTEX_UNLOCK;
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_RUNNING], 0);
    GST_QUEUE_MUTEX_LOCK;
  }

  /* There's something in the list now, whatever it is */
  data = g_queue_pop_head (queue->queue);
  GST_CAT_LOG_OBJECT (queue_dataflow, queue,
      "retrieved data %p from queue", data);

  if (GST_IS_BUFFER (data)) {
    GstFlowReturn result;

    /* Update statistics */
    queue->cur_level.buffers--;
    queue->cur_level.bytes -= GST_BUFFER_SIZE (data);
    if (GST_BUFFER_DURATION (data) != GST_CLOCK_TIME_NONE)
      queue->cur_level.time -= GST_BUFFER_DURATION (data);

    GST_QUEUE_MUTEX_UNLOCK;
    result = gst_pad_push (pad, GST_BUFFER (data));
    GST_QUEUE_MUTEX_LOCK;
    if (result != GST_FLOW_OK) {
      gst_pad_pause_task (queue->srcpad);
    }
  } else {
    if (GST_EVENT_TYPE (data) == GST_EVENT_EOS) {
      gst_pad_pause_task (queue->srcpad);
      restart = FALSE;
    }
    GST_QUEUE_MUTEX_UNLOCK;
    gst_pad_push_event (queue->srcpad, GST_EVENT (data));
    GST_QUEUE_MUTEX_LOCK;
    if (restart == TRUE)
      goto restart;
  }

  STATUS (queue, "after _get()");

  GST_CAT_LOG_OBJECT (queue_dataflow, queue, "signalling item_del");
  g_cond_signal (queue->item_del);
  GST_QUEUE_MUTEX_UNLOCK;
  return;

out_flushing:
  GST_CAT_LOG_OBJECT (queue_dataflow, queue, "exit because of flush");
  gst_pad_pause_task (pad);
  GST_QUEUE_MUTEX_UNLOCK;
  return;
}


static gboolean
gst_queue_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;

#ifndef GST_DISABLE_GST_DEBUG
  GstQueue *queue = GST_QUEUE (GST_PAD_PARENT (pad));

  GST_CAT_DEBUG_OBJECT (queue_dataflow, queue, "got event %p (%d)",
      event, GST_EVENT_TYPE (event));
#endif

  res = gst_pad_event_default (pad, event);

  return res;
}

static gboolean
gst_queue_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstQueue *queue = GST_QUEUE (GST_PAD_PARENT (pad));

  if (!GST_PAD_PEER (queue->sinkpad))
    return FALSE;
  if (!gst_pad_query (GST_PAD_PEER (queue->sinkpad), query))
    return FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 peer_pos, peer_total;
      GstFormat format;

      /* get peer position */
      gst_query_parse_position (query, &format, &peer_pos, &peer_total);

      /* FIXME: this code assumes that there's no discont in the queue */
      switch (format) {
        case GST_FORMAT_BYTES:
          peer_pos -= queue->cur_level.bytes;
          break;
        case GST_FORMAT_TIME:
          peer_pos -= queue->cur_level.time;
          break;
        default:
          /* FIXME */
          break;
      }
      /* set updated positions */
      gst_query_set_position (query, format, peer_pos, peer_total);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
gst_queue_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  if (active) {
    queue->flushing = FALSE;
    result = TRUE;
  } else {
    /* step 1, unblock chain and loop functions */
    GST_QUEUE_MUTEX_LOCK;
    queue->flushing = TRUE;
    gst_queue_locked_flush (queue);
    g_cond_signal (queue->item_del);
    GST_QUEUE_MUTEX_UNLOCK;

    /* step 2, make sure streaming finishes */
    result = gst_pad_stop_task (pad);
  }

  return result;
}

static gboolean
gst_queue_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstQueue *queue;

  queue = GST_QUEUE (GST_OBJECT_PARENT (pad));

  if (active) {
    GST_QUEUE_MUTEX_LOCK;
    queue->flushing = FALSE;
    result = gst_pad_start_task (pad, (GstTaskFunction) gst_queue_loop, pad);
    GST_QUEUE_MUTEX_UNLOCK;
  } else {
    /* step 1, unblock chain and loop functions */
    GST_QUEUE_MUTEX_LOCK;
    queue->flushing = TRUE;
    g_cond_signal (queue->item_add);
    GST_QUEUE_MUTEX_UNLOCK;

    /* step 2, make sure streaming finishes */
    result = gst_pad_stop_task (pad);
  }

  return result;
}

static GstElementStateReturn
gst_queue_change_state (GstElement * element)
{
  GstQueue *queue;
  GstElementStateReturn ret = GST_STATE_SUCCESS;

  queue = GST_QUEUE (element);

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element, "starting state change");

  /* lock the queue so another thread (not in sync with this thread's state)
   * can't call this queue's _loop (or whatever) */

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element, "done with state change");

  return ret;
}

static void
gst_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstQueue *queue = GST_QUEUE (object);

  /* someone could change levels here, and since this
   * affects the get/put funcs, we need to lock for safety. */
  GST_QUEUE_MUTEX_LOCK;

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

  GST_QUEUE_MUTEX_UNLOCK;
}

static void
gst_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
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
