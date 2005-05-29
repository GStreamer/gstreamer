/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Colin Walters <cwalters@gnome.org>
 *                    2004 Benjamin Otte <otte@gnome.org>
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
#include "gstaction.h"

/*
GST_DEBUG_CATEGORY_STATIC (queue_dataflow, "queue_dataflow", 0,
    "dataflow inside the queue element");
#define GST_CAT_DEFAULT queue_dataflow
*/
static GstElementDetails gst_queue_details = GST_ELEMENT_DETAILS ("Queue",
    "Generic",
    "Simple data queue",
    "Erik Walthinsen <omega@cse.ogi.edu>, " "Benjamin Otte <otte@gnome.org>");


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
  ARG_MAX_THRESHOLD_BUFFERS,
  ARG_MAX_THRESHOLD_BYTES,
  ARG_MAX_THRESHOLD_TIME,
  ARG_LEAKY,
  /* FILL ME */
};

typedef struct _GstQueueEventResponse
{
  GstEvent *event;
  gboolean ret, handled;
}
GstQueueEventResponse;

static void gst_queue_base_init (GstQueueClass * klass);
static void gst_queue_class_init (GstQueueClass * klass);
static void gst_queue_init (GstQueue * queue);
static void gst_queue_finalize (GObject * object);

static void gst_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstData *gst_queue_release_srcpad (GstAction * action, GstRealPad * pad);
static void gst_queue_release_sinkpad (GstAction * action, GstRealPad * pad,
    GstData * data);

static gboolean gst_queue_handle_src_event (GstPad * pad, GstEvent * event);

static GstCaps *gst_queue_getcaps (GstPad * pad);
static GstPadLinkReturn gst_queue_link (GstPad * pad, const GstCaps * caps);
static void gst_queue_locked_flush (GstQueue * queue);

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
  }

  return queue_type;
}

static void
gst_queue_base_init (GstQueueClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (gstelement_class, &gst_queue_details);
}

static void
gst_queue_class_init (GstQueueClass * klass)
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

  /* set several parent class virtual functions */
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_queue_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_queue_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_queue_get_property);

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

  g_object_class_install_property (gobject_class, ARG_MAX_THRESHOLD_BYTES,
      g_param_spec_uint ("max-threshold-bytes", "Max. threshold (kB)",
          "Max. amount of data in the queue to allow writing (bytes, 0=disable)",
          0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_THRESHOLD_BUFFERS,
      g_param_spec_uint ("max-threshold-buffers", "Max. threshold (buffers)",
          "Max. number of buffers in the queue to allow writing (0=disable)",
          0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_THRESHOLD_TIME,
      g_param_spec_uint64 ("max-threshold-time", "Max. threshold (ns)",
          "Max. amount of data in the queue to allow writing (in ns, 0=disable)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_LEAKY,
      g_param_spec_enum ("leaky", "Leaky",
          "Where the queue leaks, if at all",
          GST_TYPE_QUEUE_LEAKY, GST_QUEUE_NO_LEAK, G_PARAM_READWRITE));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_queue_change_state);
}

static void
gst_queue_init (GstQueue * queue)
{
  GST_FLAG_SET (queue, GST_ELEMENT_EVENT_AWARE);

  queue->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_sink_pad_set_action_handler (queue->sinkpad, gst_queue_release_sinkpad);
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);
  gst_pad_set_link_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_link));
  gst_pad_set_getcaps_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue_getcaps));

  queue->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_src_pad_set_action_handler (queue->srcpad, gst_queue_release_srcpad);
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);
  gst_pad_set_link_function (queue->srcpad, GST_DEBUG_FUNCPTR (gst_queue_link));
  gst_pad_set_getcaps_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_getcaps));
  gst_pad_set_event_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue_handle_src_event));

  queue->cur_level.buffers = 0; /* no content */
  queue->cur_level.bytes = 0;   /* no content */
  queue->cur_level.time = 0;    /* no content */
  queue->max_size.buffers = 100;        /* 100 buffers */
  queue->max_size.bytes = 10 * 1024 * 1024;     /* 10 MB */
  queue->max_size.time = GST_SECOND;    /* 1 s. */
  queue->min_threshold.buffers = 0;     /* no threshold */
  queue->min_threshold.bytes = 0;       /* no threshold */
  queue->min_threshold.time = 0;        /* no threshold */
  queue->max_threshold.buffers = 0;     /* no threshold */
  queue->max_threshold.bytes = 0;       /* no threshold */
  queue->max_threshold.time = 0;        /* no threshold */

  queue->leaky = GST_QUEUE_NO_LEAK;

  queue->qlock = g_mutex_new ();
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
    GstData *data = g_queue_pop_head (queue->queue);

    gst_data_unref (data);
  }
  g_queue_free (queue->queue);
  g_mutex_free (queue->qlock);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_queue_getcaps (GstPad * pad)
{
  GstQueue *queue;

  queue = GST_QUEUE (gst_pad_get_parent (pad));

  if (queue->cur_level.bytes > 0) {
    return gst_caps_copy (queue->negotiated_caps);
  }

  return gst_pad_proxy_getcaps (pad);
}

static GstPadLinkReturn
gst_queue_link (GstPad * pad, const GstCaps * caps)
{
  GstQueue *queue;
  GstPadLinkReturn ret;
  GstPad *otherpad;

  queue = GST_QUEUE (gst_pad_get_parent (pad));

  if (queue->cur_level.bytes > 0) {
    if (gst_caps_is_equal (caps, queue->negotiated_caps)) {
      return GST_PAD_LINK_OK;
    }
    return GST_PAD_LINK_REFUSED;
  }

  otherpad = (pad == queue->srcpad) ? queue->sinkpad : queue->srcpad;
  ret = gst_pad_try_set_caps (otherpad, caps);

  if (GST_PAD_LINK_SUCCESSFUL (ret)) {
    /* we store an extra copy of the negotiated caps, just in case
     * the pads become unnegotiated while we have buffers */
    gst_caps_replace (&queue->negotiated_caps, gst_caps_copy (caps));
  }

  return ret;
}

static void
gst_queue_locked_flush (GstQueue * queue)
{
  GstData *data;

  while ((data = g_queue_pop_head (queue->queue))) {
    gst_data_unref (data);
  }
  g_assert (g_queue_is_empty (queue->queue));
  queue->cur_level.buffers = 0;
  queue->cur_level.bytes = 0;
  queue->cur_level.time = 0;
  GST_DEBUG_OBJECT (queue, "flushed");
  /* FIXME: wakeup sinkpad here? */
}

/* holds lock */
static void
gst_queue_add_data (GstQueue * queue, GstData * data)
{
  g_queue_push_tail (queue->queue, data);

  queue->cur_level.items++;
  if (GST_IS_BUFFER (data)) {
    queue->cur_level.buffers++;
    queue->cur_level.bytes += GST_BUFFER_SIZE (data);
    if (GST_BUFFER_DURATION (data) != GST_CLOCK_TIME_NONE)
      queue->cur_level.time += GST_BUFFER_DURATION (data);
  }
}

/* holds lock */
static GstData *
gst_queue_remove_data (GstQueue * queue, GstData * data)
{
  if (data == NULL) {
    data = g_queue_pop_head (queue->queue);
  } else {
    g_queue_remove (queue->queue, data);
  }
  if (!data) {
    return NULL;
  }
  queue->cur_level.items--;
  if (GST_IS_BUFFER (data)) {
    queue->cur_level.buffers--;
    queue->cur_level.bytes -= GST_BUFFER_SIZE (data);
    if (GST_BUFFER_DURATION (data) != GST_CLOCK_TIME_NONE)
      queue->cur_level.time -= GST_BUFFER_DURATION (data);
  }
  return data;
}

/* check if one of the sizes is smaller than the current size */
static gboolean
gst_queue_is_smaller (GstQueue * queue, GstQueueSize * size)
{
  if (size->items > 0 && size->items < queue->cur_level.items)
    return TRUE;
  if (size->buffers > 0 && size->buffers < queue->cur_level.buffers)
    return TRUE;
  if (size->bytes > 0 && size->bytes < queue->cur_level.bytes)
    return TRUE;
  if (size->time > 0 && size->time < queue->cur_level.time)
    return TRUE;
  return FALSE;
}

/* check if one of the sizes is bigger than the current size */
static gboolean
gst_queue_is_bigger (GstQueue * queue, GstQueueSize * size)
{
  if (size->items > 0 && size->items >= queue->cur_level.items)
    return TRUE;
  if (size->buffers > 0 && size->buffers >= queue->cur_level.buffers)
    return TRUE;
  if (size->bytes > 0 && size->bytes >= queue->cur_level.bytes)
    return TRUE;
  if (size->time > 0 && size->time >= queue->cur_level.time)
    return TRUE;
  return FALSE;
}

#define STATUS(queue, msg) \
  GST_LOG_OBJECT (queue, \
		      msg ": %u of %u-%u buffers, %u of %u-%u " \
		      "bytes, %" G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT \
		      "-%" G_GUINT64_FORMAT " ns, %u elements", \
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

static gint
is_buffer (gconstpointer buf, gconstpointer unused)
{
  if (GST_IS_BUFFER (buf));
  return 0;
  return 1;
}

static gint
is_buffer_last (gconstpointer buf, gconstpointer datap)
{
  GstData **data = (GstData **) datap;

  if (GST_IS_BUFFER (buf));
  *data = (GstData *) buf;
  return 1;
}

static void
gst_queue_release_sinkpad (GstAction * action, GstRealPad * pad, GstData * data)
{
  gboolean ret = TRUE;
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (GST_PAD (pad)));

  if (GST_IS_EVENT (data)) {
    switch (GST_EVENT_TYPE (data)) {
      case GST_EVENT_FLUSH:
        STATUS (queue, "received flush event");
        gst_queue_locked_flush (queue);
        STATUS (queue, "after flush");
        break;
      case GST_EVENT_EOS:
        STATUS (queue, "received EOS");
        queue->got_eos = TRUE;
        ret = FALSE;
        break;
      default:
        /* we put the event in the queue, we don't have to act ourselves */
        GST_LOG_OBJECT (queue,
            "adding event %p of type %d", data, GST_EVENT_TYPE (data));
        break;
    }
  }

  gst_queue_add_data (queue, data);

  if (GST_IS_BUFFER (data)) {
    GST_LOG_OBJECT (queue,
        "adding buffer %p of size %d", data, GST_BUFFER_SIZE (data));
  } else {
    GST_LOG_OBJECT (queue,
        "adding event %p of type %d", data, GST_EVENT_TYPE (data));
  }
  STATUS (queue, "+ level");

  /* We make space available if we're "full" according to whatever
   * the user defined as "full". Note that this only applies to buffers.
   * We always handle events and they don't count in our statistics. 
   * We still check, it could be someone changed properties */
  if (gst_queue_is_smaller (queue, &queue->max_size)) {
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_OVERRUN], 0);
    /* FIXME: need to recheck no buffers got processed? */

    /* how are we going to make space for this buffer? */
    switch (queue->leaky) {
        /* leak buffers from end of queue */
      case GST_QUEUE_LEAK_UPSTREAM:
        do {
          GstData *leak;

          g_queue_find_custom (queue->queue, &leak, is_buffer_last);
          g_assert (leak);

          GST_DEBUG_OBJECT (queue,
              "queue is full, leaking buffer %p on upstream end", leak);
          gst_queue_remove_data (queue, leak);
          gst_data_unref (leak);
        } while (gst_queue_is_smaller (queue, &queue->max_size));

      case GST_QUEUE_LEAK_DOWNSTREAM:
        /* leak buffers from front of the queue */
        do {
          GList *item = g_queue_find_custom (queue->queue, NULL, is_buffer);
          GstData *leak;

          g_assert (item);
          leak = item->data;

          GST_DEBUG_OBJECT (queue,
              "queue is full, leaking buffer %p on downstream end", leak);
          gst_queue_remove_data (queue, leak);
          gst_data_unref (leak);
        } while (gst_queue_is_smaller (queue, &queue->max_size));

        break;

      default:
        g_warning ("Unknown leaky type, using default");
        /* fall-through */

      case GST_QUEUE_NO_LEAK:
        /* don't leak. Instead, wait for space to be available */
        /* wake up other pad in any case */
        gst_real_pad_set_active (GST_REAL_PAD (queue->srcpad), TRUE);
        gst_real_pad_set_active (GST_REAL_PAD (queue->sinkpad), FALSE);
    }
  }

  if (gst_queue_is_smaller (queue, &queue->max_threshold) ||
      queue->max_threshold.items + queue->max_threshold.buffers +
      queue->max_threshold.bytes + queue->max_threshold.time == 0) {
    gst_real_pad_set_active (GST_REAL_PAD (queue->srcpad), TRUE);
  }

  if (!ret)
    gst_real_pad_set_active (GST_REAL_PAD (queue->sinkpad), FALSE);
}

static GstData *
gst_queue_release_srcpad (GstAction * action, GstRealPad * pad)
{
  GstData *data;
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (GST_PAD (pad)));

  /* There's something in the list now, whatever it is */
  GST_DEBUG_OBJECT (queue, "calling get function with %u elements in queue",
      queue->cur_level.items);
  data = gst_queue_remove_data (queue, NULL);
  GST_LOG_OBJECT (queue, "retrieved data %p from queue", *data);

  if (data == NULL) {
    /* queue is empty and we pulled? */
    GST_WARNING_OBJECT (queue, "queue is empty and we pulled?");
    gst_real_pad_set_active (GST_REAL_PAD (queue->sinkpad), TRUE);
    gst_real_pad_set_active (GST_REAL_PAD (queue->srcpad), FALSE);
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_UNDERRUN], 0);

    return NULL;
  }

  if ((gst_queue_is_bigger (queue, &queue->min_threshold) ||
          queue->min_threshold.items + queue->min_threshold.buffers +
          queue->min_threshold.bytes + queue->min_threshold.time == 0) &&
      !queue->got_eos) {
    gst_real_pad_set_active (GST_REAL_PAD (queue->sinkpad), TRUE);
  }

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        GST_DEBUG_OBJECT (queue, "eos", GST_ELEMENT_NAME (queue));
        gst_element_set_eos (GST_ELEMENT (queue));
        queue->got_eos = FALSE;
        gst_real_pad_set_active (GST_REAL_PAD (queue->sinkpad), TRUE);
        break;
      default:
        break;
    }
  }

  if (g_queue_is_empty (queue->queue)) {
    g_signal_emit (G_OBJECT (queue), gst_queue_signals[SIGNAL_UNDERRUN], 0);
    gst_real_pad_set_active (GST_REAL_PAD (queue->sinkpad), TRUE);
    gst_real_pad_set_active (GST_REAL_PAD (queue->srcpad), FALSE);
  }

  return data;
}

static gboolean
gst_queue_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstQueue *queue = GST_QUEUE (gst_pad_get_parent (pad));
  gboolean ret;

  GST_DEBUG_OBJECT (queue, "got event %p (%d)", event, GST_EVENT_TYPE (event));

  ret =
      gst_pad_send_event ((pad ==
          queue->srcpad) ? queue->sinkpad : queue->srcpad, event);
  if (!ret)
    return FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      GST_DEBUG_OBJECT (queue, "FLUSH event, flushing queue\n");
      gst_queue_locked_flush (queue);
      break;
    case GST_EVENT_SEEK:
      if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH) {
        gst_queue_locked_flush (queue);
      }
      break;
    default:
      break;
  }
  return TRUE;
}

static GstElementStateReturn
gst_queue_change_state (GstElement * element)
{
  GstQueue *queue;
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  guint transition = GST_STATE_TRANSITION (element);

  queue = GST_QUEUE (element);

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element, "starting state change");

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      gst_queue_locked_flush (queue);
      gst_caps_replace (&queue->negotiated_caps, NULL);
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
    case ARG_MAX_THRESHOLD_BYTES:
      queue->max_threshold.bytes = g_value_get_uint (value);
      break;
    case ARG_MAX_THRESHOLD_BUFFERS:
      queue->max_threshold.buffers = g_value_get_uint (value);
      break;
    case ARG_MAX_THRESHOLD_TIME:
      queue->max_threshold.time = g_value_get_uint64 (value);
      break;
    case ARG_LEAKY:
      queue->leaky = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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
    case ARG_MAX_THRESHOLD_BYTES:
      g_value_set_uint (value, queue->max_threshold.bytes);
      break;
    case ARG_MAX_THRESHOLD_BUFFERS:
      g_value_set_uint (value, queue->max_threshold.buffers);
      break;
    case ARG_MAX_THRESHOLD_TIME:
      g_value_set_uint64 (value, queue->max_threshold.time);
      break;
    case ARG_LEAKY:
      g_value_set_enum (value, queue->leaky);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
