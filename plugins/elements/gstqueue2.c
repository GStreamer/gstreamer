/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2003 Colin Walters <cwalters@gnome.org>
 *                    2000,2005,2007 Wim Taymans <wim.taymans@gmail.com>
 *                    2007 Thiago Sousa Santos <thiagoss@lcc.ufcg.edu.br>
 *                 SA 2010 ST-Ericsson <benjamin.gaignard@stericsson.com>
 *
 * gstqueue2.c:
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

/**
 * SECTION:element-queue2
 *
 * Data is queued until one of the limits specified by the
 * #GstQueue2:max-size-buffers, #GstQueue2:max-size-bytes and/or
 * #GstQueue2:max-size-time properties has been reached. Any attempt to push
 * more buffers into the queue will block the pushing thread until more space
 * becomes available.
 *
 * The queue will create a new thread on the source pad to decouple the
 * processing on sink and source pad.
 *
 * You can query how many buffers are queued by reading the
 * #GstQueue2:current-level-buffers property.
 *
 * The default queue size limits are 100 buffers, 2MB of data, or
 * two seconds worth of data, whichever is reached first.
 *
 * If you set temp-tmpl to a value such as /tmp/gstreamer-XXXXXX, the element
 * will allocate a random free filename and buffer data in the file.
 * By using this, it will buffer the entire stream data on the file independently
 * of the queue size limits, they will only be used for buffering statistics.
 *
 * Since 0.10.24, setting the temp-location property with a filename is deprecated
 * because it's impossible to securely open a temporary file in this way. The
 * property will still be used to notify the application of the allocated
 * filename, though.
 *
 * Last reviewed on 2009-07-10 (0.10.24)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqueue2.h"

#include <glib/gstdio.h>

#include "gst/gst-i18n-lib.h"
#include "gst/glib-compat-private.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#else
#include <unistd.h>
#endif

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (queue_debug);
#define GST_CAT_DEFAULT (queue_debug)
GST_DEBUG_CATEGORY_STATIC (queue_dataflow);

enum
{
  LAST_SIGNAL
};

/* other defines */
#define DEFAULT_BUFFER_SIZE 4096
#define QUEUE_IS_USING_TEMP_FILE(queue) ((queue)->temp_location_set || (queue)->temp_template != NULL)
#define QUEUE_IS_USING_RING_BUFFER(queue) ((queue)->ring_buffer_max_size != 0)  /* for consistency with the above macro */
#define QUEUE_IS_USING_QUEUE(queue) (!QUEUE_IS_USING_TEMP_FILE(queue) && !QUEUE_IS_USING_RING_BUFFER (queue))

#define QUEUE_MAX_BYTES(queue) MIN((queue)->max_level.bytes, (queue)->ring_buffer_max_size)

/* default property values */
#define DEFAULT_MAX_SIZE_BUFFERS   100  /* 100 buffers */
#define DEFAULT_MAX_SIZE_BYTES     (2 * 1024 * 1024)    /* 2 MB */
#define DEFAULT_MAX_SIZE_TIME      2 * GST_SECOND       /* 2 seconds */
#define DEFAULT_USE_BUFFERING      FALSE
#define DEFAULT_USE_RATE_ESTIMATE  TRUE
#define DEFAULT_LOW_PERCENT        10
#define DEFAULT_HIGH_PERCENT       99
#define DEFAULT_TEMP_REMOVE        TRUE
#define DEFAULT_RING_BUFFER_MAX_SIZE 0

enum
{
  PROP_0,
  PROP_CUR_LEVEL_BUFFERS,
  PROP_CUR_LEVEL_BYTES,
  PROP_CUR_LEVEL_TIME,
  PROP_MAX_SIZE_BUFFERS,
  PROP_MAX_SIZE_BYTES,
  PROP_MAX_SIZE_TIME,
  PROP_USE_BUFFERING,
  PROP_USE_RATE_ESTIMATE,
  PROP_LOW_PERCENT,
  PROP_HIGH_PERCENT,
  PROP_TEMP_TEMPLATE,
  PROP_TEMP_LOCATION,
  PROP_TEMP_REMOVE,
  PROP_RING_BUFFER_MAX_SIZE,
  PROP_LAST
};

#define GST_QUEUE2_CLEAR_LEVEL(l) G_STMT_START {         \
  l.buffers = 0;                                        \
  l.bytes = 0;                                          \
  l.time = 0;                                           \
  l.rate_time = 0;                                      \
} G_STMT_END

#define STATUS(queue, pad, msg) \
  GST_CAT_LOG_OBJECT (queue_dataflow, queue, \
                      "(%s:%s) " msg ": %u of %u buffers, %u of %u " \
                      "bytes, %" G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT \
                      " ns, %"G_GUINT64_FORMAT" items", \
                      GST_DEBUG_PAD_NAME (pad), \
                      queue->cur_level.buffers, \
                      queue->max_level.buffers, \
                      queue->cur_level.bytes, \
                      queue->max_level.bytes, \
                      queue->cur_level.time, \
                      queue->max_level.time, \
                      (guint64) (!QUEUE_IS_USING_QUEUE(queue) ? \
                        queue->current->writing_pos - queue->current->max_reading_pos : \
                        queue->queue.length))

#define GST_QUEUE2_MUTEX_LOCK(q) G_STMT_START {                          \
  g_mutex_lock (q->qlock);                                              \
} G_STMT_END

#define GST_QUEUE2_MUTEX_LOCK_CHECK(q,res,label) G_STMT_START {         \
  GST_QUEUE2_MUTEX_LOCK (q);                                            \
  if (res != GST_FLOW_OK)                                               \
    goto label;                                                         \
} G_STMT_END

#define GST_QUEUE2_MUTEX_UNLOCK(q) G_STMT_START {                        \
  g_mutex_unlock (q->qlock);                                            \
} G_STMT_END

#define GST_QUEUE2_WAIT_DEL_CHECK(q, res, label) G_STMT_START {         \
  STATUS (queue, q->sinkpad, "wait for DEL");                           \
  q->waiting_del = TRUE;                                                \
  g_cond_wait (q->item_del, queue->qlock);                              \
  q->waiting_del = FALSE;                                               \
  if (res != GST_FLOW_OK) {                                             \
    STATUS (queue, q->srcpad, "received DEL wakeup");                   \
    goto label;                                                         \
  }                                                                     \
  STATUS (queue, q->sinkpad, "received DEL");                           \
} G_STMT_END

#define GST_QUEUE2_WAIT_ADD_CHECK(q, res, label) G_STMT_START {         \
  STATUS (queue, q->srcpad, "wait for ADD");                            \
  q->waiting_add = TRUE;                                                \
  g_cond_wait (q->item_add, q->qlock);                                  \
  q->waiting_add = FALSE;                                               \
  if (res != GST_FLOW_OK) {                                             \
    STATUS (queue, q->srcpad, "received ADD wakeup");                   \
    goto label;                                                         \
  }                                                                     \
  STATUS (queue, q->srcpad, "received ADD");                            \
} G_STMT_END

#define GST_QUEUE2_SIGNAL_DEL(q) G_STMT_START {                          \
  if (q->waiting_del) {                                                 \
    STATUS (q, q->srcpad, "signal DEL");                                \
    g_cond_signal (q->item_del);                                        \
  }                                                                     \
} G_STMT_END

#define GST_QUEUE2_SIGNAL_ADD(q) G_STMT_START {                          \
  if (q->waiting_add) {                                                 \
    STATUS (q, q->sinkpad, "signal ADD");                               \
    g_cond_signal (q->item_add);                                        \
  }                                                                     \
} G_STMT_END

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (queue_debug, "queue2", 0, "queue element"); \
    GST_DEBUG_CATEGORY_INIT (queue_dataflow, "queue2_dataflow", 0, \
        "dataflow inside the queue element");

GST_BOILERPLATE_FULL (GstQueue2, gst_queue2, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_queue2_finalize (GObject * object);

static void gst_queue2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_queue2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_queue2_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_queue2_chain_list (GstPad * pad,
    GstBufferList * buffer_list);
static GstFlowReturn gst_queue2_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static GstFlowReturn gst_queue2_push_one (GstQueue2 * queue);
static void gst_queue2_loop (GstPad * pad);

static gboolean gst_queue2_handle_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_queue2_handle_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_queue2_handle_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_queue2_handle_query (GstElement * element,
    GstQuery * query);

static GstCaps *gst_queue2_getcaps (GstPad * pad);
static gboolean gst_queue2_acceptcaps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_queue2_get_range (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer);
static gboolean gst_queue2_src_checkgetrange_function (GstPad * pad);

static gboolean gst_queue2_src_activate_pull (GstPad * pad, gboolean active);
static gboolean gst_queue2_src_activate_push (GstPad * pad, gboolean active);
static gboolean gst_queue2_sink_activate_push (GstPad * pad, gboolean active);
static GstStateChangeReturn gst_queue2_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_queue2_is_empty (GstQueue2 * queue);
static gboolean gst_queue2_is_filled (GstQueue2 * queue);

static void update_cur_level (GstQueue2 * queue, GstQueue2Range * range);

typedef enum
{
  GST_QUEUE2_ITEM_TYPE_UNKNOWN = 0,
  GST_QUEUE2_ITEM_TYPE_BUFFER,
  GST_QUEUE2_ITEM_TYPE_BUFFER_LIST,
  GST_QUEUE2_ITEM_TYPE_EVENT
} GstQueue2ItemType;

/* static guint gst_queue2_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_queue2_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details_simple (gstelement_class, "Queue 2",
      "Generic",
      "Simple data queue",
      "Erik Walthinsen <omega@cse.ogi.edu>, "
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_queue2_class_init (GstQueue2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_queue2_set_property;
  gobject_class->get_property = gst_queue2_get_property;

  /* properties */
  g_object_class_install_property (gobject_class, PROP_CUR_LEVEL_BYTES,
      g_param_spec_uint ("current-level-bytes", "Current level (kB)",
          "Current amount of data in the queue (bytes)",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CUR_LEVEL_BUFFERS,
      g_param_spec_uint ("current-level-buffers", "Current level (buffers)",
          "Current number of buffers in the queue",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CUR_LEVEL_TIME,
      g_param_spec_uint64 ("current-level-time", "Current level (ns)",
          "Current amount of data in the queue (in ns)",
          0, G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint ("max-size-bytes", "Max. size (kB)",
          "Max. amount of data in the queue (bytes, 0=disable)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BUFFERS,
      g_param_spec_uint ("max-size-buffers", "Max. size (buffers)",
          "Max. number of buffers in the queue (0=disable)", 0, G_MAXUINT,
          DEFAULT_MAX_SIZE_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
          "Max. amount of data in the queue (in ns, 0=disable)", 0, G_MAXUINT64,
          DEFAULT_MAX_SIZE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USE_BUFFERING,
      g_param_spec_boolean ("use-buffering", "Use buffering",
          "Emit GST_MESSAGE_BUFFERING based on low-/high-percent thresholds",
          DEFAULT_USE_BUFFERING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USE_RATE_ESTIMATE,
      g_param_spec_boolean ("use-rate-estimate", "Use Rate Estimate",
          "Estimate the bitrate of the stream to calculate time level",
          DEFAULT_USE_RATE_ESTIMATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_LOW_PERCENT,
      g_param_spec_int ("low-percent", "Low percent",
          "Low threshold for buffering to start. Only used if use-buffering is True",
          0, 100, DEFAULT_LOW_PERCENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HIGH_PERCENT,
      g_param_spec_int ("high-percent", "High percent",
          "High threshold for buffering to finish. Only used if use-buffering is True",
          0, 100, DEFAULT_HIGH_PERCENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TEMP_TEMPLATE,
      g_param_spec_string ("temp-template", "Temporary File Template",
          "File template to store temporary files in, should contain directory "
          "and XXXXXX. (NULL == disabled)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TEMP_LOCATION,
      g_param_spec_string ("temp-location", "Temporary File Location",
          "Location to store temporary files in (Deprecated: Only read this "
          "property, use temp-template to configure the name template)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstQueue2:temp-remove
   *
   * When temp-template is set, remove the temporary file when going to READY.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_TEMP_REMOVE,
      g_param_spec_boolean ("temp-remove", "Remove the Temporary File",
          "Remove the temp-location after use",
          DEFAULT_TEMP_REMOVE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstQueue2:ring-buffer-max-size
   *
   * The maximum size of the ring buffer in bytes. If set to 0, the ring
   * buffer is disabled. Default 0.
   *
   * Since: 0.10.31
   */
  g_object_class_install_property (gobject_class, PROP_RING_BUFFER_MAX_SIZE,
      g_param_spec_uint64 ("ring-buffer-max-size",
          "Max. ring buffer size (bytes)",
          "Max. amount of data in the ring buffer (bytes, 0 = disabled)",
          0, G_MAXUINT64, DEFAULT_RING_BUFFER_MAX_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* set several parent class virtual functions */
  gobject_class->finalize = gst_queue2_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_queue2_change_state);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_queue2_handle_query);
}

static void
gst_queue2_init (GstQueue2 * queue, GstQueue2Class * g_class)
{
  queue->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");

  gst_pad_set_chain_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_chain));
  gst_pad_set_chain_list_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_chain_list));
  gst_pad_set_activatepush_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_sink_activate_push));
  gst_pad_set_event_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_handle_sink_event));
  gst_pad_set_getcaps_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_getcaps));
  gst_pad_set_acceptcaps_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_acceptcaps));
  gst_pad_set_bufferalloc_function (queue->sinkpad,
      GST_DEBUG_FUNCPTR (gst_queue2_bufferalloc));
  gst_element_add_pad (GST_ELEMENT (queue), queue->sinkpad);

  queue->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_activatepull_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_src_activate_pull));
  gst_pad_set_activatepush_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_src_activate_push));
  gst_pad_set_getrange_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_get_range));
  gst_pad_set_checkgetrange_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_src_checkgetrange_function));
  gst_pad_set_getcaps_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_getcaps));
  gst_pad_set_acceptcaps_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_acceptcaps));
  gst_pad_set_event_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_handle_src_event));
  gst_pad_set_query_function (queue->srcpad,
      GST_DEBUG_FUNCPTR (gst_queue2_handle_src_query));
  gst_element_add_pad (GST_ELEMENT (queue), queue->srcpad);

  /* levels */
  GST_QUEUE2_CLEAR_LEVEL (queue->cur_level);
  queue->max_level.buffers = DEFAULT_MAX_SIZE_BUFFERS;
  queue->max_level.bytes = DEFAULT_MAX_SIZE_BYTES;
  queue->max_level.time = DEFAULT_MAX_SIZE_TIME;
  queue->max_level.rate_time = DEFAULT_MAX_SIZE_TIME;
  queue->use_buffering = DEFAULT_USE_BUFFERING;
  queue->use_rate_estimate = DEFAULT_USE_RATE_ESTIMATE;
  queue->low_percent = DEFAULT_LOW_PERCENT;
  queue->high_percent = DEFAULT_HIGH_PERCENT;

  gst_segment_init (&queue->sink_segment, GST_FORMAT_TIME);
  gst_segment_init (&queue->src_segment, GST_FORMAT_TIME);

  queue->sinktime = GST_CLOCK_TIME_NONE;
  queue->srctime = GST_CLOCK_TIME_NONE;
  queue->sink_tainted = TRUE;
  queue->src_tainted = TRUE;

  queue->srcresult = GST_FLOW_WRONG_STATE;
  queue->sinkresult = GST_FLOW_WRONG_STATE;
  queue->is_eos = FALSE;
  queue->in_timer = g_timer_new ();
  queue->out_timer = g_timer_new ();

  queue->qlock = g_mutex_new ();
  queue->waiting_add = FALSE;
  queue->item_add = g_cond_new ();
  queue->waiting_del = FALSE;
  queue->item_del = g_cond_new ();
  g_queue_init (&queue->queue);

  queue->buffering_percent = 100;

  /* tempfile related */
  queue->temp_template = NULL;
  queue->temp_location = NULL;
  queue->temp_location_set = FALSE;
  queue->temp_remove = DEFAULT_TEMP_REMOVE;

  queue->ring_buffer = NULL;
  queue->ring_buffer_max_size = DEFAULT_RING_BUFFER_MAX_SIZE;

  GST_DEBUG_OBJECT (queue,
      "initialized queue's not_empty & not_full conditions");
}

/* called only once, as opposed to dispose */
static void
gst_queue2_finalize (GObject * object)
{
  GstQueue2 *queue = GST_QUEUE2 (object);

  GST_DEBUG_OBJECT (queue, "finalizing queue");

  while (!g_queue_is_empty (&queue->queue)) {
    GstMiniObject *data = g_queue_pop_head (&queue->queue);

    gst_mini_object_unref (data);
  }

  g_queue_clear (&queue->queue);
  g_mutex_free (queue->qlock);
  g_cond_free (queue->item_add);
  g_cond_free (queue->item_del);
  g_timer_destroy (queue->in_timer);
  g_timer_destroy (queue->out_timer);

  /* temp_file path cleanup  */
  g_free (queue->temp_template);
  g_free (queue->temp_location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
debug_ranges (GstQueue2 * queue)
{
  GstQueue2Range *walk;

  for (walk = queue->ranges; walk; walk = walk->next) {
    GST_DEBUG_OBJECT (queue,
        "range [%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "] (rb [%"
        G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "]), reading %" G_GUINT64_FORMAT
        " current range? %s", walk->offset, walk->writing_pos, walk->rb_offset,
        walk->rb_writing_pos, walk->reading_pos,
        walk == queue->current ? "**y**" : "  n  ");
  }
}

/* clear all the downloaded ranges */
static void
clean_ranges (GstQueue2 * queue)
{
  GST_DEBUG_OBJECT (queue, "clean queue ranges");

  g_slice_free_chain (GstQueue2Range, queue->ranges, next);
  queue->ranges = NULL;
  queue->current = NULL;
}

/* find a range that contains @offset or NULL when nothing does */
static GstQueue2Range *
find_range (GstQueue2 * queue, guint64 offset)
{
  GstQueue2Range *range = NULL;
  GstQueue2Range *walk;

  /* first do a quick check for the current range */
  for (walk = queue->ranges; walk; walk = walk->next) {
    if (offset >= walk->offset && offset <= walk->writing_pos) {
      /* we can reuse an existing range */
      range = walk;
      break;
    }
  }
  if (range) {
    GST_DEBUG_OBJECT (queue,
        "found range for %" G_GUINT64_FORMAT ": [%" G_GUINT64_FORMAT "-%"
        G_GUINT64_FORMAT "]", offset, range->offset, range->writing_pos);
  } else {
    GST_DEBUG_OBJECT (queue, "no range for %" G_GUINT64_FORMAT, offset);
  }
  return range;
}

static void
update_cur_level (GstQueue2 * queue, GstQueue2Range * range)
{
  guint64 max_reading_pos, writing_pos;

  writing_pos = range->writing_pos;
  max_reading_pos = range->max_reading_pos;

  if (writing_pos > max_reading_pos)
    queue->cur_level.bytes = writing_pos - max_reading_pos;
  else
    queue->cur_level.bytes = 0;
}

/* make a new range for @offset or reuse an existing range */
static GstQueue2Range *
add_range (GstQueue2 * queue, guint64 offset)
{
  GstQueue2Range *range, *prev, *next;

  GST_DEBUG_OBJECT (queue, "find range for %" G_GUINT64_FORMAT, offset);

  if ((range = find_range (queue, offset))) {
    GST_DEBUG_OBJECT (queue,
        "reusing range %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT, range->offset,
        range->writing_pos);
    range->writing_pos = offset;
  } else {
    GST_DEBUG_OBJECT (queue,
        "new range %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT, offset, offset);

    range = g_slice_new0 (GstQueue2Range);
    range->offset = offset;
    /* we want to write to the next location in the ring buffer */
    range->rb_offset = queue->current ? queue->current->rb_writing_pos : 0;
    range->writing_pos = offset;
    range->rb_writing_pos = range->rb_offset;
    range->reading_pos = offset;
    range->max_reading_pos = offset;

    /* insert sorted */
    prev = NULL;
    next = queue->ranges;
    while (next) {
      if (next->offset > offset) {
        /* insert before next */
        GST_DEBUG_OBJECT (queue,
            "insert before range %p, offset %" G_GUINT64_FORMAT, next,
            next->offset);
        break;
      }
      /* try next */
      prev = next;
      next = next->next;
    }
    range->next = next;
    if (prev)
      prev->next = range;
    else
      queue->ranges = range;
  }
  debug_ranges (queue);

  /* update the stats for this range */
  update_cur_level (queue, range);

  return range;
}


/* clear and init the download ranges for offset 0 */
static void
init_ranges (GstQueue2 * queue)
{
  GST_DEBUG_OBJECT (queue, "init queue ranges");

  /* get rid of all the current ranges */
  clean_ranges (queue);
  /* make a range for offset 0 */
  queue->current = add_range (queue, 0);
}

static gboolean
gst_queue2_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstQueue2 *queue;
  GstPad *otherpad;
  gboolean result;

  queue = GST_QUEUE2 (GST_PAD_PARENT (pad));

  otherpad = (pad == queue->srcpad ? queue->sinkpad : queue->srcpad);
  result = gst_pad_peer_accept_caps (otherpad, caps);

  return result;
}

static GstCaps *
gst_queue2_getcaps (GstPad * pad)
{
  GstQueue2 *queue;
  GstPad *otherpad;
  GstCaps *result;

  queue = GST_QUEUE2 (gst_pad_get_parent (pad));
  if (G_UNLIKELY (queue == NULL))
    return gst_caps_new_any ();

  otherpad = (pad == queue->srcpad ? queue->sinkpad : queue->srcpad);
  result = gst_pad_peer_get_caps (otherpad);
  if (result == NULL)
    result = gst_caps_new_any ();

  gst_object_unref (queue);

  return result;
}

static GstFlowReturn
gst_queue2_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstQueue2 *queue;
  GstFlowReturn result;

  queue = GST_QUEUE2 (GST_PAD_PARENT (pad));

  /* Forward to src pad, without setting caps on the src pad */
  result = gst_pad_alloc_buffer (queue->srcpad, offset, size, caps, buf);

  return result;
}

/* calculate the diff between running time on the sink and src of the queue.
 * This is the total amount of time in the queue. */
static void
update_time_level (GstQueue2 * queue)
{
  if (queue->sink_tainted) {
    queue->sinktime =
        gst_segment_to_running_time (&queue->sink_segment, GST_FORMAT_TIME,
        queue->sink_segment.last_stop);
    queue->sink_tainted = FALSE;
  }

  if (queue->src_tainted) {
    queue->srctime =
        gst_segment_to_running_time (&queue->src_segment, GST_FORMAT_TIME,
        queue->src_segment.last_stop);
    queue->src_tainted = FALSE;
  }

  GST_DEBUG_OBJECT (queue, "sink %" GST_TIME_FORMAT ", src %" GST_TIME_FORMAT,
      GST_TIME_ARGS (queue->sinktime), GST_TIME_ARGS (queue->srctime));

  if (queue->sinktime != GST_CLOCK_TIME_NONE
      && queue->srctime != GST_CLOCK_TIME_NONE
      && queue->sinktime >= queue->srctime)
    queue->cur_level.time = queue->sinktime - queue->srctime;
  else
    queue->cur_level.time = 0;
}

/* take a NEWSEGMENT event and apply the values to segment, updating the time
 * level of queue. */
static void
apply_segment (GstQueue2 * queue, GstEvent * event, GstSegment * segment,
    gboolean is_sink)
{
  gboolean update;
  GstFormat format;
  gdouble rate, arate;
  gint64 start, stop, time;

  gst_event_parse_new_segment_full (event, &update, &rate, &arate,
      &format, &start, &stop, &time);

  GST_DEBUG_OBJECT (queue,
      "received NEWSEGMENT update %d, rate %lf, applied rate %lf, "
      "format %d, "
      "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
      G_GINT64_FORMAT, update, rate, arate, format, start, stop, time);

  if (format == GST_FORMAT_BYTES) {
    if (QUEUE_IS_USING_TEMP_FILE (queue)) {
      /* start is where we'll be getting from and as such writing next */
      queue->current = add_range (queue, start);
      /* update the stats for this range */
      update_cur_level (queue, queue->current);
    }
  }

  /* now configure the values, we use these to track timestamps on the
   * sinkpad. */
  if (format != GST_FORMAT_TIME) {
    /* non-time format, pretent the current time segment is closed with a
     * 0 start and unknown stop time. */
    update = FALSE;
    format = GST_FORMAT_TIME;
    start = 0;
    stop = -1;
    time = 0;
  }
  gst_segment_set_newsegment_full (segment, update,
      rate, arate, format, start, stop, time);

  GST_DEBUG_OBJECT (queue,
      "configured NEWSEGMENT %" GST_SEGMENT_FORMAT, segment);

  if (is_sink)
    queue->sink_tainted = TRUE;
  else
    queue->src_tainted = TRUE;

  /* segment can update the time level of the queue */
  update_time_level (queue);
}

/* take a buffer and update segment, updating the time level of the queue. */
static void
apply_buffer (GstQueue2 * queue, GstBuffer * buffer, GstSegment * segment,
    gboolean is_sink)
{
  GstClockTime duration, timestamp;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  /* if no timestamp is set, assume it's continuous with the previous
   * time */
  if (timestamp == GST_CLOCK_TIME_NONE)
    timestamp = segment->last_stop;

  /* add duration */
  if (duration != GST_CLOCK_TIME_NONE)
    timestamp += duration;

  GST_DEBUG_OBJECT (queue, "last_stop updated to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  gst_segment_set_last_stop (segment, GST_FORMAT_TIME, timestamp);

  if (is_sink)
    queue->sink_tainted = TRUE;
  else
    queue->src_tainted = TRUE;

  /* calc diff with other end */
  update_time_level (queue);
}

static GstBufferListItem
buffer_list_apply_time (GstBuffer ** buf, guint group, guint idx, gpointer data)
{
  GstClockTime *timestamp = data;

  GST_TRACE ("buffer %u in group %u has ts %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT, idx, group,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (*buf)));

  if (GST_BUFFER_TIMESTAMP_IS_VALID (*buf))
    *timestamp = GST_BUFFER_TIMESTAMP (*buf);

  if (GST_BUFFER_DURATION_IS_VALID (*buf))
    *timestamp += GST_BUFFER_DURATION (*buf);

  GST_TRACE ("ts now %" GST_TIME_FORMAT, GST_TIME_ARGS (*timestamp));
  return GST_BUFFER_LIST_CONTINUE;
}

/* take a buffer list and update segment, updating the time level of the queue */
static void
apply_buffer_list (GstQueue2 * queue, GstBufferList * buffer_list,
    GstSegment * segment, gboolean is_sink)
{
  GstClockTime timestamp;

  /* if no timestamp is set, assume it's continuous with the previous time */
  timestamp = segment->last_stop;

  gst_buffer_list_foreach (buffer_list, buffer_list_apply_time, &timestamp);

  GST_DEBUG_OBJECT (queue, "last_stop updated to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  gst_segment_set_last_stop (segment, GST_FORMAT_TIME, timestamp);

  if (is_sink)
    queue->sink_tainted = TRUE;
  else
    queue->src_tainted = TRUE;

  /* calc diff with other end */
  update_time_level (queue);
}

static void
update_buffering (GstQueue2 * queue)
{
  gint64 percent;
  gboolean post = FALSE;

  if (queue->high_percent <= 0)
    return;

#define GET_PERCENT(format,alt_max) ((queue->max_level.format) > 0 ? (queue->cur_level.format) * 100 / ((alt_max) > 0 ? MIN ((alt_max), (queue->max_level.format)) : (queue->max_level.format)) : 0)

  if (queue->is_eos) {
    /* on EOS we are always 100% full, we set the var here so that it we can
     * reuse the logic below to stop buffering */
    percent = 100;
    GST_LOG_OBJECT (queue, "we are EOS");
  } else {
    /* figure out the percent we are filled, we take the max of all formats. */

    if (!QUEUE_IS_USING_RING_BUFFER (queue)) {
      percent = GET_PERCENT (bytes, 0);
    } else {
      guint64 rb_size = queue->ring_buffer_max_size;
      percent = GET_PERCENT (bytes, rb_size);
    }
    percent = MAX (percent, GET_PERCENT (time, 0));
    percent = MAX (percent, GET_PERCENT (buffers, 0));

    /* also apply the rate estimate when we need to */
    if (queue->use_rate_estimate)
      percent = MAX (percent, GET_PERCENT (rate_time, 0));
  }

  if (queue->is_buffering) {
    post = TRUE;
    /* if we were buffering see if we reached the high watermark */
    if (percent >= queue->high_percent)
      queue->is_buffering = FALSE;
  } else {
    /* we were not buffering, check if we need to start buffering if we drop
     * below the low threshold */
    if (percent < queue->low_percent) {
      queue->is_buffering = TRUE;
      queue->buffering_iteration++;
      post = TRUE;
    }
  }
  if (post) {
    GstMessage *message;
    GstBufferingMode mode;
    gint64 buffering_left = -1;

    /* scale to high percent so that it becomes the 100% mark */
    percent = percent * 100 / queue->high_percent;
    /* clip */
    if (percent > 100)
      percent = 100;

    if (percent != queue->buffering_percent) {
      queue->buffering_percent = percent;

      if (!QUEUE_IS_USING_QUEUE (queue)) {
        GstFormat fmt = GST_FORMAT_BYTES;
        gint64 duration;

        if (QUEUE_IS_USING_RING_BUFFER (queue))
          mode = GST_BUFFERING_TIMESHIFT;
        else
          mode = GST_BUFFERING_DOWNLOAD;

        if (queue->byte_in_rate > 0) {
          if (gst_pad_query_peer_duration (queue->sinkpad, &fmt, &duration))
            buffering_left =
                (gdouble) ((duration -
                    queue->current->writing_pos) * 1000) / queue->byte_in_rate;
        } else {
          buffering_left = G_MAXINT64;
        }
      } else {
        mode = GST_BUFFERING_STREAM;
      }

      GST_DEBUG_OBJECT (queue, "buffering %d percent", (gint) percent);
      message = gst_message_new_buffering (GST_OBJECT_CAST (queue),
          (gint) percent);
      gst_message_set_buffering_stats (message, mode,
          queue->byte_in_rate, queue->byte_out_rate, buffering_left);

      gst_element_post_message (GST_ELEMENT_CAST (queue), message);
    }
  } else {
    GST_DEBUG_OBJECT (queue, "filled %d percent", (gint) percent);
  }

#undef GET_PERCENT
}

static void
reset_rate_timer (GstQueue2 * queue)
{
  queue->bytes_in = 0;
  queue->bytes_out = 0;
  queue->byte_in_rate = 0.0;
  queue->byte_in_period = 0;
  queue->byte_out_rate = 0.0;
  queue->last_in_elapsed = 0.0;
  queue->last_out_elapsed = 0.0;
  queue->in_timer_started = FALSE;
  queue->out_timer_started = FALSE;
}

/* the interval in seconds to recalculate the rate */
#define RATE_INTERVAL    0.2
/* Tuning for rate estimation. We use a large window for the input rate because
 * it should be stable when connected to a network. The output rate is less
 * stable (the elements preroll, queues behind a demuxer fill, ...) and should
 * therefore adapt more quickly.
 * However, initial input rate may be subject to a burst, and should therefore
 * initially also adapt more quickly to changes, and only later on give higher
 * weight to previous values. */
#define AVG_IN(avg,val,w1,w2)  ((avg) * (w1) + (val) * (w2)) / ((w1) + (w2))
#define AVG_OUT(avg,val) ((avg) * 3.0 + (val)) / 4.0

static void
update_in_rates (GstQueue2 * queue)
{
  gdouble elapsed, period;
  gdouble byte_in_rate;

  if (!queue->in_timer_started) {
    queue->in_timer_started = TRUE;
    g_timer_start (queue->in_timer);
    return;
  }

  elapsed = g_timer_elapsed (queue->in_timer, NULL);

  /* recalc after each interval. */
  if (queue->last_in_elapsed + RATE_INTERVAL < elapsed) {
    period = elapsed - queue->last_in_elapsed;

    GST_DEBUG_OBJECT (queue,
        "rates: period %f, in %" G_GUINT64_FORMAT ", global period %f",
        period, queue->bytes_in, queue->byte_in_period);

    byte_in_rate = queue->bytes_in / period;

    if (queue->byte_in_rate == 0.0)
      queue->byte_in_rate = byte_in_rate;
    else
      queue->byte_in_rate = AVG_IN (queue->byte_in_rate, byte_in_rate,
          (double) queue->byte_in_period, period);

    /* another data point, cap at 16 for long time running average */
    if (queue->byte_in_period < 16 * RATE_INTERVAL)
      queue->byte_in_period += period;

    /* reset the values to calculate rate over the next interval */
    queue->last_in_elapsed = elapsed;
    queue->bytes_in = 0;
  }

  if (queue->byte_in_rate > 0.0) {
    queue->cur_level.rate_time =
        queue->cur_level.bytes / queue->byte_in_rate * GST_SECOND;
  }
  GST_DEBUG_OBJECT (queue, "rates: in %f, time %" GST_TIME_FORMAT,
      queue->byte_in_rate, GST_TIME_ARGS (queue->cur_level.rate_time));
}

static void
update_out_rates (GstQueue2 * queue)
{
  gdouble elapsed, period;
  gdouble byte_out_rate;

  if (!queue->out_timer_started) {
    queue->out_timer_started = TRUE;
    g_timer_start (queue->out_timer);
    return;
  }

  elapsed = g_timer_elapsed (queue->out_timer, NULL);

  /* recalc after each interval. */
  if (queue->last_out_elapsed + RATE_INTERVAL < elapsed) {
    period = elapsed - queue->last_out_elapsed;

    GST_DEBUG_OBJECT (queue,
        "rates: period %f, out %" G_GUINT64_FORMAT, period, queue->bytes_out);

    byte_out_rate = queue->bytes_out / period;

    if (queue->byte_out_rate == 0.0)
      queue->byte_out_rate = byte_out_rate;
    else
      queue->byte_out_rate = AVG_OUT (queue->byte_out_rate, byte_out_rate);

    /* reset the values to calculate rate over the next interval */
    queue->last_out_elapsed = elapsed;
    queue->bytes_out = 0;
  }
  if (queue->byte_in_rate > 0.0) {
    queue->cur_level.rate_time =
        queue->cur_level.bytes / queue->byte_in_rate * GST_SECOND;
  }
  GST_DEBUG_OBJECT (queue, "rates: out %f, time %" GST_TIME_FORMAT,
      queue->byte_out_rate, GST_TIME_ARGS (queue->cur_level.rate_time));
}

static void
update_cur_pos (GstQueue2 * queue, GstQueue2Range * range, guint64 pos)
{
  guint64 reading_pos, max_reading_pos;

  reading_pos = pos;
  max_reading_pos = range->max_reading_pos;

  max_reading_pos = MAX (max_reading_pos, reading_pos);

  GST_DEBUG_OBJECT (queue,
      "updating max_reading_pos from %" G_GUINT64_FORMAT " to %"
      G_GUINT64_FORMAT, range->max_reading_pos, max_reading_pos);
  range->max_reading_pos = max_reading_pos;

  update_cur_level (queue, range);
}

static gboolean
perform_seek_to_offset (GstQueue2 * queue, guint64 offset)
{
  GstEvent *event;
  gboolean res;

  GST_QUEUE2_MUTEX_UNLOCK (queue);

  GST_DEBUG_OBJECT (queue, "Seeking to %" G_GUINT64_FORMAT, offset);

  event =
      gst_event_new_seek (1.0, GST_FORMAT_BYTES,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, offset,
      GST_SEEK_TYPE_NONE, -1);

  res = gst_pad_push_event (queue->sinkpad, event);
  GST_QUEUE2_MUTEX_LOCK (queue);

  if (res)
    queue->current = add_range (queue, offset);

  return res;
}

/* see if there is enough data in the file to read a full buffer */
static gboolean
gst_queue2_have_data (GstQueue2 * queue, guint64 offset, guint length)
{
  GstQueue2Range *range;

  GST_DEBUG_OBJECT (queue, "looking for offset %" G_GUINT64_FORMAT ", len %u",
      offset, length);

  if ((range = find_range (queue, offset))) {
    if (queue->current != range) {
      GST_DEBUG_OBJECT (queue, "switching ranges, do seek to range position");
      perform_seek_to_offset (queue, range->writing_pos);
    }

    GST_INFO_OBJECT (queue, "cur_level.bytes %u (max %" G_GUINT64_FORMAT ")",
        queue->cur_level.bytes, QUEUE_MAX_BYTES (queue));

    /* we have a range for offset */
    GST_DEBUG_OBJECT (queue,
        "we have a range %p, offset %" G_GUINT64_FORMAT ", writing_pos %"
        G_GUINT64_FORMAT, range, range->offset, range->writing_pos);

    if (!QUEUE_IS_USING_RING_BUFFER (queue) && queue->is_eos)
      return TRUE;

    if (offset + length <= range->writing_pos)
      return TRUE;
    else
      GST_DEBUG_OBJECT (queue,
          "Need more data (%" G_GUINT64_FORMAT " bytes more)",
          (offset + length) - range->writing_pos);

  } else {
    GST_INFO_OBJECT (queue, "not found in any range");
    /* we don't have the range, see how far away we are, FIXME, find a good
     * threshold based on the incoming rate. */
    if (!queue->is_eos && queue->current) {
      if (QUEUE_IS_USING_RING_BUFFER (queue)) {
        if (offset < queue->current->offset || offset >
            queue->current->writing_pos + QUEUE_MAX_BYTES (queue) -
            queue->cur_level.bytes) {
          perform_seek_to_offset (queue, offset);
        } else {
          GST_INFO_OBJECT (queue,
              "requested data is within range, wait for data");
        }
      } else if (offset < queue->current->writing_pos + 200000) {
        update_cur_pos (queue, queue->current, offset + length);
        GST_INFO_OBJECT (queue, "wait for data");
        return FALSE;
      }
    }

    /* too far away, do a seek */
    perform_seek_to_offset (queue, offset);
  }

  return FALSE;
}

#ifdef HAVE_FSEEKO
#define FSEEK_FILE(file,offset)  (fseeko (file, (off_t) offset, SEEK_SET) != 0)
#elif defined (G_OS_UNIX) || defined (G_OS_WIN32)
#define FSEEK_FILE(file,offset)  (lseek (fileno (file), (off_t) offset, SEEK_SET) == (off_t) -1)
#else
#define FSEEK_FILE(file,offset)  (fseek (file, offset, SEEK_SET) != 0)
#endif

static gint64
gst_queue2_read_data_at_offset (GstQueue2 * queue, guint64 offset, guint length,
    guint8 * dst)
{
  guint8 *ring_buffer;
  size_t res;

  ring_buffer = queue->ring_buffer;

  if (QUEUE_IS_USING_TEMP_FILE (queue) && FSEEK_FILE (queue->temp_file, offset))
    goto seek_failed;

  /* this should not block */
  GST_LOG_OBJECT (queue, "Reading %d bytes from offset %" G_GUINT64_FORMAT,
      length, offset);
  if (QUEUE_IS_USING_TEMP_FILE (queue)) {
    res = fread (dst, 1, length, queue->temp_file);
  } else {
    memcpy (dst, ring_buffer + offset, length);
    res = length;
  }

  GST_LOG_OBJECT (queue, "read %" G_GSIZE_FORMAT " bytes", res);

  if (G_UNLIKELY (res < length)) {
    if (!QUEUE_IS_USING_TEMP_FILE (queue))
      goto could_not_read;
    /* check for errors or EOF */
    if (ferror (queue->temp_file))
      goto could_not_read;
    if (feof (queue->temp_file) && length > 0)
      goto eos;
  }

  return res;

seek_failed:
  {
    GST_ELEMENT_ERROR (queue, RESOURCE, SEEK, (NULL), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (queue, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }
eos:
  {
    GST_DEBUG ("non-regular file hits EOS");
    return GST_FLOW_UNEXPECTED;
  }
}

static GstFlowReturn
gst_queue2_create_read (GstQueue2 * queue, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstBuffer *buf;
  guint8 *data;
  guint64 file_offset;
  guint block_length, remaining, read_length;
  gint64 read_return;
  guint64 rb_size;
  guint64 rpos;

  /* allocate the output buffer of the requested size */
  buf = gst_buffer_new_and_alloc (length);
  data = GST_BUFFER_DATA (buf);

  GST_DEBUG_OBJECT (queue, "Reading %u bytes from %" G_GUINT64_FORMAT, length,
      offset);

  rpos = offset;
  rb_size = queue->ring_buffer_max_size;

  remaining = length;
  while (remaining > 0) {
    /* configure how much/whether to read */
    if (!gst_queue2_have_data (queue, rpos, remaining)) {
      read_length = 0;

      if (QUEUE_IS_USING_RING_BUFFER (queue)) {
        guint64 level;

        /* calculate how far away the offset is */
        if (queue->current->writing_pos > rpos)
          level = queue->current->writing_pos - rpos;
        else
          level = 0;

        GST_DEBUG_OBJECT (queue,
            "reading %" G_GUINT64_FORMAT ", writing %" G_GUINT64_FORMAT
            ", level %" G_GUINT64_FORMAT,
            rpos, queue->current->writing_pos, level);

        if (level >= rb_size) {
          /* we don't have the data but if we have a ring buffer that is full, we
           * need to read */
          GST_DEBUG_OBJECT (queue,
              "ring buffer full, reading ring-buffer-max-size %"
              G_GUINT64_FORMAT " bytes", rb_size);
          read_length = rb_size;
        } else if (queue->is_eos) {
          /* won't get any more data so read any data we have */
          if (level) {
            GST_DEBUG_OBJECT (queue,
                "EOS hit but read %" G_GUINT64_FORMAT " bytes that we have",
                level);
            read_length = level;
          } else {
            GST_DEBUG_OBJECT (queue,
                "EOS hit and we don't have any requested data");
            gst_buffer_unref (buf);
            return GST_FLOW_UNEXPECTED;
          }
        }
      }

      if (read_length == 0) {
        if (QUEUE_IS_USING_RING_BUFFER (queue)
            && queue->current->max_reading_pos > rpos) {
          /* protect cached data (data between offset and max_reading_pos)
           * and update current level */
          GST_DEBUG_OBJECT (queue,
              "protecting cached data [%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT
              "]", rpos, queue->current->max_reading_pos);
          queue->current->max_reading_pos = rpos;
          update_cur_level (queue, queue->current);
        }
        GST_DEBUG_OBJECT (queue, "waiting for add");
        GST_QUEUE2_WAIT_ADD_CHECK (queue, queue->srcresult, out_flushing);
        continue;
      }
    } else {
      /* we have the requested data so read it */
      read_length = remaining;
    }

    /* set range reading_pos to actual reading position for this read */
    queue->current->reading_pos = rpos;

    /* configure how much and from where to read */
    if (QUEUE_IS_USING_RING_BUFFER (queue)) {
      file_offset =
          (queue->current->rb_offset + (rpos -
              queue->current->offset)) % rb_size;
      if (file_offset + read_length > rb_size) {
        block_length = rb_size - file_offset;
      } else {
        block_length = read_length;
      }
    } else {
      file_offset = rpos;
      block_length = read_length;
    }

    /* while we still have data to read, we loop */
    while (read_length > 0) {
      read_return =
          gst_queue2_read_data_at_offset (queue, file_offset, block_length,
          data);
      if (read_return < 0)
        goto read_error;

      file_offset += read_return;
      if (QUEUE_IS_USING_RING_BUFFER (queue))
        file_offset %= rb_size;

      data += read_return;
      read_length -= read_return;
      block_length = read_length;
      remaining -= read_return;

      rpos = (queue->current->reading_pos += read_return);
      update_cur_pos (queue, queue->current, queue->current->reading_pos);
    }
    GST_QUEUE2_SIGNAL_DEL (queue);
    GST_DEBUG_OBJECT (queue, "%u bytes left to read", remaining);
  }

  GST_BUFFER_SIZE (buf) = length;
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + length;

  *buffer = buf;

  return GST_FLOW_OK;

  /* ERRORS */
out_flushing:
  {
    GST_DEBUG_OBJECT (queue, "we are flushing");
    gst_buffer_unref (buf);
    return GST_FLOW_WRONG_STATE;
  }
read_error:
  {
    GST_DEBUG_OBJECT (queue, "we have a read error");
    gst_buffer_unref (buf);
    return read_return;
  }
}

/* should be called with QUEUE_LOCK */
static GstMiniObject *
gst_queue2_read_item_from_file (GstQueue2 * queue)
{
  GstMiniObject *item;

  if (queue->starting_segment != NULL) {
    item = GST_MINI_OBJECT_CAST (queue->starting_segment);
    queue->starting_segment = NULL;
  } else {
    GstFlowReturn ret;
    GstBuffer *buffer;
    guint64 reading_pos;

    reading_pos = queue->current->reading_pos;

    ret =
        gst_queue2_create_read (queue, reading_pos, DEFAULT_BUFFER_SIZE,
        &buffer);

    switch (ret) {
      case GST_FLOW_OK:
        item = GST_MINI_OBJECT_CAST (buffer);
        break;
      case GST_FLOW_UNEXPECTED:
        item = GST_MINI_OBJECT_CAST (gst_event_new_eos ());
        break;
      default:
        item = NULL;
        break;
    }
  }
  return item;
}

/* must be called with MUTEX_LOCK. Will briefly release the lock when notifying
 * the temp filename. */
static gboolean
gst_queue2_open_temp_location_file (GstQueue2 * queue)
{
  gint fd = -1;
  gchar *name = NULL;

  if (queue->temp_file)
    goto already_opened;

  GST_DEBUG_OBJECT (queue, "opening temp file %s", queue->temp_template);

  /* we have two cases:
   * - temp_location was set to something !NULL (Deprecated). in this case we
   *   open the specified filename.
   * - temp_template was set, allocate a filename and open that filename
   */
  if (!queue->temp_location_set) {
    /* nothing to do */
    if (queue->temp_template == NULL)
      goto no_directory;

    /* make copy of the template, we don't want to change this */
    name = g_strdup (queue->temp_template);
    fd = g_mkstemp (name);
    if (fd == -1)
      goto mkstemp_failed;

    /* open the file for update/writing */
    queue->temp_file = fdopen (fd, "wb+");
    /* error creating file */
    if (queue->temp_file == NULL)
      goto open_failed;

    g_free (queue->temp_location);
    queue->temp_location = name;

    GST_QUEUE2_MUTEX_UNLOCK (queue);

    /* we can't emit the notify with the lock */
    g_object_notify (G_OBJECT (queue), "temp-location");

    GST_QUEUE2_MUTEX_LOCK (queue);
  } else {
    /* open the file for update/writing, this is deprecated but we still need to
     * support it for API/ABI compatibility */
    queue->temp_file = g_fopen (queue->temp_location, "wb+");
    /* error creating file */
    if (queue->temp_file == NULL)
      goto open_failed;
  }
  GST_DEBUG_OBJECT (queue, "opened temp file %s", queue->temp_template);

  return TRUE;

  /* ERRORS */
already_opened:
  {
    GST_DEBUG_OBJECT (queue, "temp file was already open");
    return TRUE;
  }
no_directory:
  {
    GST_ELEMENT_ERROR (queue, RESOURCE, NOT_FOUND,
        (_("No Temp directory specified.")), (NULL));
    return FALSE;
  }
mkstemp_failed:
  {
    GST_ELEMENT_ERROR (queue, RESOURCE, OPEN_READ,
        (_("Could not create temp file \"%s\"."), queue->temp_template),
        GST_ERROR_SYSTEM);
    g_free (name);
    return FALSE;
  }
open_failed:
  {
    GST_ELEMENT_ERROR (queue, RESOURCE, OPEN_READ,
        (_("Could not open file \"%s\" for reading."), name), GST_ERROR_SYSTEM);
    g_free (name);
    if (fd != -1)
      close (fd);
    return FALSE;
  }
}

static void
gst_queue2_close_temp_location_file (GstQueue2 * queue)
{
  /* nothing to do */
  if (queue->temp_file == NULL)
    return;

  GST_DEBUG_OBJECT (queue, "closing temp file");

  fflush (queue->temp_file);
  fclose (queue->temp_file);

  if (queue->temp_remove)
    remove (queue->temp_location);

  queue->temp_file = NULL;
  clean_ranges (queue);
}

static void
gst_queue2_flush_temp_file (GstQueue2 * queue)
{
  if (queue->temp_file == NULL)
    return;

  GST_DEBUG_OBJECT (queue, "flushing temp file");

  queue->temp_file = g_freopen (queue->temp_location, "wb+", queue->temp_file);
}

static void
gst_queue2_locked_flush (GstQueue2 * queue)
{
  if (!QUEUE_IS_USING_QUEUE (queue)) {
    if (QUEUE_IS_USING_TEMP_FILE (queue))
      gst_queue2_flush_temp_file (queue);
    init_ranges (queue);
  } else {
    while (!g_queue_is_empty (&queue->queue)) {
      GstMiniObject *data = g_queue_pop_head (&queue->queue);

      /* Then lose another reference because we are supposed to destroy that
         data when flushing */
      gst_mini_object_unref (data);
    }
  }
  GST_QUEUE2_CLEAR_LEVEL (queue->cur_level);
  gst_segment_init (&queue->sink_segment, GST_FORMAT_TIME);
  gst_segment_init (&queue->src_segment, GST_FORMAT_TIME);
  queue->sinktime = queue->srctime = GST_CLOCK_TIME_NONE;
  queue->sink_tainted = queue->src_tainted = TRUE;
  if (queue->starting_segment != NULL)
    gst_event_unref (queue->starting_segment);
  queue->starting_segment = NULL;
  queue->segment_event_received = FALSE;

  /* we deleted a lot of something */
  GST_QUEUE2_SIGNAL_DEL (queue);
}

static gboolean
gst_queue2_wait_free_space (GstQueue2 * queue)
{
  /* We make space available if we're "full" according to whatever
   * the user defined as "full". */
  if (gst_queue2_is_filled (queue)) {
    gboolean started;

    /* pause the timer while we wait. The fact that we are waiting does not mean
     * the byterate on the input pad is lower */
    if ((started = queue->in_timer_started))
      g_timer_stop (queue->in_timer);

    GST_CAT_DEBUG_OBJECT (queue_dataflow, queue,
        "queue is full, waiting for free space");
    do {
      /* Wait for space to be available, we could be unlocked because of a flush. */
      GST_QUEUE2_WAIT_DEL_CHECK (queue, queue->sinkresult, out_flushing);
    }
    while (gst_queue2_is_filled (queue));

    /* and continue if we were running before */
    if (started)
      g_timer_continue (queue->in_timer);
  }
  return TRUE;

  /* ERRORS */
out_flushing:
  {
    GST_CAT_DEBUG_OBJECT (queue_dataflow, queue, "queue is flushing");
    return FALSE;
  }
}

static gboolean
gst_queue2_create_write (GstQueue2 * queue, GstBuffer * buffer)
{
  guint8 *data, *ring_buffer;
  guint size, rb_size;
  guint64 writing_pos, new_writing_pos;
  GstQueue2Range *range, *prev, *next;

  if (QUEUE_IS_USING_RING_BUFFER (queue))
    writing_pos = queue->current->rb_writing_pos;
  else
    writing_pos = queue->current->writing_pos;
  ring_buffer = queue->ring_buffer;
  rb_size = queue->ring_buffer_max_size;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  GST_DEBUG_OBJECT (queue, "Writing %u bytes to %" G_GUINT64_FORMAT, size,
      GST_BUFFER_OFFSET (buffer));

  while (size > 0) {
    guint to_write;

    if (QUEUE_IS_USING_RING_BUFFER (queue)) {
      gint64 space;

      /* calculate the space in the ring buffer not used by data from
       * the current range */
      while (QUEUE_MAX_BYTES (queue) <= queue->cur_level.bytes) {
        /* wait until there is some free space */
        GST_QUEUE2_WAIT_DEL_CHECK (queue, queue->sinkresult, out_flushing);
      }
      /* get the amount of space we have */
      space = QUEUE_MAX_BYTES (queue) - queue->cur_level.bytes;

      /* calculate if we need to split or if we can write the entire
       * buffer now */
      to_write = MIN (size, space);

      /* the writing position in the ring buffer after writing (part
       * or all of) the buffer */
      new_writing_pos = (writing_pos + to_write) % rb_size;

      prev = NULL;
      range = queue->ranges;

      /* if we need to overwrite data in the ring buffer, we need to
       * update the ranges
       *
       * warning: this code is complicated and includes some
       * simplifications - pen, paper and diagrams for the cases
       * recommended! */
      while (range) {
        guint64 range_data_start, range_data_end;
        GstQueue2Range *range_to_destroy = NULL;

        range_data_start = range->rb_offset;
        range_data_end = range->rb_writing_pos;

        /* handle the special case where the range has no data in it */
        if (range->writing_pos == range->offset) {
          if (range != queue->current) {
            GST_DEBUG_OBJECT (queue,
                "Removing range: offset %" G_GUINT64_FORMAT ", wpos %"
                G_GUINT64_FORMAT, range->offset, range->writing_pos);
            /* remove range */
            range_to_destroy = range;
            if (prev)
              prev->next = range->next;
          }
          goto next_range;
        }

        if (range_data_end > range_data_start) {
          if (writing_pos >= range_data_end && new_writing_pos >= writing_pos)
            goto next_range;

          if (new_writing_pos > range_data_start) {
            if (new_writing_pos >= range_data_end) {
              GST_DEBUG_OBJECT (queue,
                  "Removing range: offset %" G_GUINT64_FORMAT ", wpos %"
                  G_GUINT64_FORMAT, range->offset, range->writing_pos);
              /* remove range */
              range_to_destroy = range;
              if (prev)
                prev->next = range->next;
            } else {
              GST_DEBUG_OBJECT (queue,
                  "advancing offsets from %" G_GUINT64_FORMAT " (%"
                  G_GUINT64_FORMAT ") to %" G_GUINT64_FORMAT " (%"
                  G_GUINT64_FORMAT ")", range->offset, range->rb_offset,
                  range->offset + new_writing_pos - range_data_start,
                  new_writing_pos);
              range->offset += (new_writing_pos - range_data_start);
              range->rb_offset = new_writing_pos;
            }
          }
        } else {
          guint64 new_wpos_virt = writing_pos + to_write;

          if (new_wpos_virt <= range_data_start)
            goto next_range;

          if (new_wpos_virt > rb_size && new_writing_pos >= range_data_end) {
            GST_DEBUG_OBJECT (queue,
                "Removing range: offset %" G_GUINT64_FORMAT ", wpos %"
                G_GUINT64_FORMAT, range->offset, range->writing_pos);
            /* remove range */
            range_to_destroy = range;
            if (prev)
              prev->next = range->next;
          } else {
            GST_DEBUG_OBJECT (queue,
                "advancing offsets from %" G_GUINT64_FORMAT " (%"
                G_GUINT64_FORMAT ") to %" G_GUINT64_FORMAT " (%"
                G_GUINT64_FORMAT ")", range->offset, range->rb_offset,
                range->offset + new_writing_pos - range_data_start,
                new_writing_pos);
            range->offset += (new_wpos_virt - range_data_start);
            range->rb_offset = new_writing_pos;
          }
        }

      next_range:
        if (!range_to_destroy)
          prev = range;

        range = range->next;
        if (range_to_destroy) {
          if (range_to_destroy == queue->ranges)
            queue->ranges = range;
          g_slice_free (GstQueue2Range, range_to_destroy);
          range_to_destroy = NULL;
        }
      }
    } else {
      to_write = size;
      new_writing_pos = writing_pos + to_write;
    }

    if (QUEUE_IS_USING_TEMP_FILE (queue)
        && FSEEK_FILE (queue->temp_file, writing_pos))
      goto seek_failed;

    if (new_writing_pos > writing_pos) {
      GST_INFO_OBJECT (queue,
          "writing %u bytes to range [%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT
          "] (rb wpos %" G_GUINT64_FORMAT ")", to_write, queue->current->offset,
          queue->current->writing_pos, queue->current->rb_writing_pos);
      /* either not using ring buffer or no wrapping, just write */
      if (QUEUE_IS_USING_TEMP_FILE (queue)) {
        if (fwrite (data, to_write, 1, queue->temp_file) != 1)
          goto handle_error;
      } else {
        memcpy (ring_buffer + writing_pos, data, to_write);
      }

      if (!QUEUE_IS_USING_RING_BUFFER (queue)) {
        /* try to merge with next range */
        while ((next = queue->current->next)) {
          GST_INFO_OBJECT (queue,
              "checking merge with next range %" G_GUINT64_FORMAT " < %"
              G_GUINT64_FORMAT, new_writing_pos, next->offset);
          if (new_writing_pos < next->offset)
            break;

          GST_DEBUG_OBJECT (queue, "merging ranges %" G_GUINT64_FORMAT,
              next->writing_pos);

          /* remove the group, we could choose to not read the data in this range
           * again. This would involve us doing a seek to the current writing position
           * in the range. FIXME, It would probably make sense to do a seek when there
           * is a lot of data in the range we merged with to avoid reading it all
           * again. */
          queue->current->next = next->next;
          g_slice_free (GstQueue2Range, next);

          debug_ranges (queue);
        }
        goto update_and_signal;
      }
    } else {
      /* wrapping */
      guint block_one, block_two;

      block_one = rb_size - writing_pos;
      block_two = to_write - block_one;

      if (block_one > 0) {
        GST_INFO_OBJECT (queue, "writing %u bytes", block_one);
        /* write data to end of ring buffer */
        if (QUEUE_IS_USING_TEMP_FILE (queue)) {
          if (fwrite (data, block_one, 1, queue->temp_file) != 1)
            goto handle_error;
        } else {
          memcpy (ring_buffer + writing_pos, data, block_one);
        }
      }

      if (QUEUE_IS_USING_TEMP_FILE (queue) && FSEEK_FILE (queue->temp_file, 0))
        goto seek_failed;

      if (block_two > 0) {
        GST_INFO_OBJECT (queue, "writing %u bytes", block_two);
        if (QUEUE_IS_USING_TEMP_FILE (queue)) {
          if (fwrite (data + block_one, block_two, 1, queue->temp_file) != 1)
            goto handle_error;
        } else {
          memcpy (ring_buffer, data + block_one, block_two);
        }
      }
    }

  update_and_signal:
    /* update the writing positions */
    size -= to_write;
    GST_INFO_OBJECT (queue,
        "wrote %u bytes to %" G_GUINT64_FORMAT " (%u bytes remaining to write)",
        to_write, writing_pos, size);

    if (QUEUE_IS_USING_RING_BUFFER (queue)) {
      data += to_write;
      queue->current->writing_pos += to_write;
      queue->current->rb_writing_pos = writing_pos = new_writing_pos;
    } else {
      queue->current->writing_pos = writing_pos = new_writing_pos;
    }
    update_cur_level (queue, queue->current);

    /* update the buffering status */
    if (queue->use_buffering)
      update_buffering (queue);

    GST_INFO_OBJECT (queue, "cur_level.bytes %u (max %" G_GUINT64_FORMAT ")",
        queue->cur_level.bytes, QUEUE_MAX_BYTES (queue));

    GST_QUEUE2_SIGNAL_ADD (queue);
  };

  return TRUE;

  /* ERRORS */
out_flushing:
  {
    GST_DEBUG_OBJECT (queue, "we are flushing");
    /* FIXME - GST_FLOW_UNEXPECTED ? */
    return FALSE;
  }
seek_failed:
  {
    GST_ELEMENT_ERROR (queue, RESOURCE, SEEK, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
handle_error:
  {
    switch (errno) {
      case ENOSPC:{
        GST_ELEMENT_ERROR (queue, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
        break;
      }
      default:{
        GST_ELEMENT_ERROR (queue, RESOURCE, WRITE,
            (_("Error while writing to download file.")),
            ("%s", g_strerror (errno)));
      }
    }
    return FALSE;
  }
}

static GstBufferListItem
buffer_list_create_write (GstBuffer ** buf, guint group, guint idx, gpointer q)
{
  GstQueue2 *queue = q;

  GST_TRACE_OBJECT (queue, "writing buffer %u in group %u of size %u bytes",
      idx, group, GST_BUFFER_SIZE (*buf));

  if (!gst_queue2_create_write (queue, *buf)) {
    GST_INFO_OBJECT (queue, "create_write() returned FALSE, bailing out");
    return GST_BUFFER_LIST_END;
  }

  return GST_BUFFER_LIST_CONTINUE;
}

static GstBufferListItem
buffer_list_calc_size (GstBuffer ** buf, guint group, guint idx, gpointer data)
{
  guint *p_size = data;
  guint buf_size;

  buf_size = GST_BUFFER_SIZE (*buf);
  GST_TRACE ("buffer %u in group %u has size %u", idx, group, buf_size);
  *p_size += buf_size;

  return GST_BUFFER_LIST_CONTINUE;
}

/* enqueue an item an update the level stats */
static void
gst_queue2_locked_enqueue (GstQueue2 * queue, gpointer item,
    GstQueue2ItemType item_type)
{
  if (item_type == GST_QUEUE2_ITEM_TYPE_BUFFER) {
    GstBuffer *buffer;
    guint size;

    buffer = GST_BUFFER_CAST (item);
    size = GST_BUFFER_SIZE (buffer);

    /* add buffer to the statistics */
    if (QUEUE_IS_USING_QUEUE (queue)) {
      queue->cur_level.buffers++;
      queue->cur_level.bytes += size;
    }
    queue->bytes_in += size;

    /* apply new buffer to segment stats */
    apply_buffer (queue, buffer, &queue->sink_segment, TRUE);
    /* update the byterate stats */
    update_in_rates (queue);

    if (!QUEUE_IS_USING_QUEUE (queue)) {
      /* FIXME - check return value? */
      gst_queue2_create_write (queue, buffer);
    }
  } else if (item_type == GST_QUEUE2_ITEM_TYPE_BUFFER_LIST) {
    GstBufferList *buffer_list;
    guint size = 0;

    buffer_list = GST_BUFFER_LIST_CAST (item);

    gst_buffer_list_foreach (buffer_list, buffer_list_calc_size, &size);
    GST_LOG_OBJECT (queue, "total size of buffer list: %u bytes", size);

    /* add buffer to the statistics */
    if (QUEUE_IS_USING_QUEUE (queue)) {
      queue->cur_level.buffers++;
      queue->cur_level.bytes += size;
    }
    queue->bytes_in += size;

    /* apply new buffer to segment stats */
    apply_buffer_list (queue, buffer_list, &queue->sink_segment, TRUE);

    /* update the byterate stats */
    update_in_rates (queue);

    if (!QUEUE_IS_USING_QUEUE (queue)) {
      gst_buffer_list_foreach (buffer_list, buffer_list_create_write, queue);
    }
  } else if (item_type == GST_QUEUE2_ITEM_TYPE_EVENT) {
    GstEvent *event;

    event = GST_EVENT_CAST (item);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        /* Zero the thresholds, this makes sure the queue is completely
         * filled and we can read all data from the queue. */
        GST_DEBUG_OBJECT (queue, "we have EOS");
        queue->is_eos = TRUE;
        break;
      case GST_EVENT_NEWSEGMENT:
        apply_segment (queue, event, &queue->sink_segment, TRUE);
        /* This is our first new segment, we hold it
         * as we can't save it on the temp file */
        if (!QUEUE_IS_USING_QUEUE (queue)) {
          if (queue->segment_event_received)
            goto unexpected_event;

          queue->segment_event_received = TRUE;
          if (queue->starting_segment != NULL)
            gst_event_unref (queue->starting_segment);
          queue->starting_segment = event;
          item = NULL;
        }
        /* a new segment allows us to accept more buffers if we got UNEXPECTED
         * from downstream */
        queue->unexpected = FALSE;
        break;
      default:
        if (!QUEUE_IS_USING_QUEUE (queue))
          goto unexpected_event;
        break;
    }
  } else {
    g_warning ("Unexpected item %p added in queue %s (refcounting problem?)",
        item, GST_OBJECT_NAME (queue));
    /* we can't really unref since we don't know what it is */
    item = NULL;
  }

  if (item) {
    /* update the buffering status */
    if (queue->use_buffering)
      update_buffering (queue);

    if (QUEUE_IS_USING_QUEUE (queue)) {
      g_queue_push_tail (&queue->queue, item);
    } else {
      gst_mini_object_unref (GST_MINI_OBJECT_CAST (item));
    }

    GST_QUEUE2_SIGNAL_ADD (queue);
  }

  return;

  /* ERRORS */
unexpected_event:
  {
    g_warning
        ("Unexpected event of kind %s can't be added in temp file of queue %s ",
        gst_event_type_get_name (GST_EVENT_TYPE (item)),
        GST_OBJECT_NAME (queue));
    gst_event_unref (GST_EVENT_CAST (item));
    return;
  }
}

/* dequeue an item from the queue and update level stats */
static GstMiniObject *
gst_queue2_locked_dequeue (GstQueue2 * queue, GstQueue2ItemType * item_type)
{
  GstMiniObject *item;

  if (!QUEUE_IS_USING_QUEUE (queue))
    item = gst_queue2_read_item_from_file (queue);
  else
    item = g_queue_pop_head (&queue->queue);

  if (item == NULL)
    goto no_item;

  if (GST_IS_BUFFER (item)) {
    GstBuffer *buffer;
    guint size;

    buffer = GST_BUFFER_CAST (item);
    size = GST_BUFFER_SIZE (buffer);
    *item_type = GST_QUEUE2_ITEM_TYPE_BUFFER;

    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "retrieved buffer %p from queue", buffer);

    if (QUEUE_IS_USING_QUEUE (queue)) {
      queue->cur_level.buffers--;
      queue->cur_level.bytes -= size;
    }
    queue->bytes_out += size;

    apply_buffer (queue, buffer, &queue->src_segment, FALSE);
    /* update the byterate stats */
    update_out_rates (queue);
    /* update the buffering */
    if (queue->use_buffering)
      update_buffering (queue);

  } else if (GST_IS_EVENT (item)) {
    GstEvent *event = GST_EVENT_CAST (item);

    *item_type = GST_QUEUE2_ITEM_TYPE_EVENT;

    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "retrieved event %p from queue", event);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        /* queue is empty now that we dequeued the EOS */
        GST_QUEUE2_CLEAR_LEVEL (queue->cur_level);
        break;
      case GST_EVENT_NEWSEGMENT:
        apply_segment (queue, event, &queue->src_segment, FALSE);
        break;
      default:
        break;
    }
  } else if (GST_IS_BUFFER_LIST (item)) {
    GstBufferList *buffer_list;
    guint size = 0;

    buffer_list = GST_BUFFER_LIST_CAST (item);
    gst_buffer_list_foreach (buffer_list, buffer_list_calc_size, &size);
    *item_type = GST_QUEUE2_ITEM_TYPE_BUFFER_LIST;

    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "retrieved buffer list %p from queue", buffer_list);

    if (QUEUE_IS_USING_QUEUE (queue)) {
      queue->cur_level.buffers--;
      queue->cur_level.bytes -= size;
    }
    queue->bytes_out += size;

    apply_buffer_list (queue, buffer_list, &queue->src_segment, FALSE);
    /* update the byterate stats */
    update_out_rates (queue);
    /* update the buffering */
    if (queue->use_buffering)
      update_buffering (queue);

  } else {
    g_warning
        ("Unexpected item %p dequeued from queue %s (refcounting problem?)",
        item, GST_OBJECT_NAME (queue));
    item = NULL;
    *item_type = GST_QUEUE2_ITEM_TYPE_UNKNOWN;
  }
  GST_QUEUE2_SIGNAL_DEL (queue);

  return item;

  /* ERRORS */
no_item:
  {
    GST_CAT_LOG_OBJECT (queue_dataflow, queue, "the queue is empty");
    return NULL;
  }
}

static gboolean
gst_queue2_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstQueue2 *queue;

  queue = GST_QUEUE2 (GST_OBJECT_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      GST_CAT_LOG_OBJECT (queue_dataflow, queue, "received flush start event");
      if (QUEUE_IS_USING_QUEUE (queue)) {
        /* forward event */
        gst_pad_push_event (queue->srcpad, event);

        /* now unblock the chain function */
        GST_QUEUE2_MUTEX_LOCK (queue);
        queue->srcresult = GST_FLOW_WRONG_STATE;
        queue->sinkresult = GST_FLOW_WRONG_STATE;
        /* unblock the loop and chain functions */
        GST_QUEUE2_SIGNAL_ADD (queue);
        GST_QUEUE2_SIGNAL_DEL (queue);
        GST_QUEUE2_MUTEX_UNLOCK (queue);

        /* make sure it pauses, this should happen since we sent
         * flush_start downstream. */
        gst_pad_pause_task (queue->srcpad);
        GST_CAT_LOG_OBJECT (queue_dataflow, queue, "loop stopped");
      } else {
        GST_QUEUE2_MUTEX_LOCK (queue);
        /* flush the sink pad */
        queue->sinkresult = GST_FLOW_WRONG_STATE;
        GST_QUEUE2_SIGNAL_DEL (queue);
        GST_QUEUE2_MUTEX_UNLOCK (queue);

        gst_event_unref (event);
      }
      goto done;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_CAT_LOG_OBJECT (queue_dataflow, queue, "received flush stop event");

      if (QUEUE_IS_USING_QUEUE (queue)) {
        /* forward event */
        gst_pad_push_event (queue->srcpad, event);

        GST_QUEUE2_MUTEX_LOCK (queue);
        gst_queue2_locked_flush (queue);
        queue->srcresult = GST_FLOW_OK;
        queue->sinkresult = GST_FLOW_OK;
        queue->is_eos = FALSE;
        queue->unexpected = FALSE;
        /* reset rate counters */
        reset_rate_timer (queue);
        gst_pad_start_task (queue->srcpad, (GstTaskFunction) gst_queue2_loop,
            queue->srcpad);
        GST_QUEUE2_MUTEX_UNLOCK (queue);
      } else {
        GST_QUEUE2_MUTEX_LOCK (queue);
        queue->segment_event_received = FALSE;
        queue->is_eos = FALSE;
        queue->unexpected = FALSE;
        queue->sinkresult = GST_FLOW_OK;
        GST_QUEUE2_MUTEX_UNLOCK (queue);

        gst_event_unref (event);
      }
      goto done;
    }
    default:
      if (GST_EVENT_IS_SERIALIZED (event)) {
        /* serialized events go in the queue */
        GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->sinkresult, out_flushing);
        /* refuse more events on EOS */
        if (queue->is_eos)
          goto out_eos;
        gst_queue2_locked_enqueue (queue, event, GST_QUEUE2_ITEM_TYPE_EVENT);
        GST_QUEUE2_MUTEX_UNLOCK (queue);
      } else {
        /* non-serialized events are passed upstream. */
        gst_pad_push_event (queue->srcpad, event);
      }
      break;
  }
done:
  return TRUE;

  /* ERRORS */
out_flushing:
  {
    GST_DEBUG_OBJECT (queue, "refusing event, we are flushing");
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_event_unref (event);
    return FALSE;
  }
out_eos:
  {
    GST_DEBUG_OBJECT (queue, "refusing event, we are EOS");
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_queue2_is_empty (GstQueue2 * queue)
{
  /* never empty on EOS */
  if (queue->is_eos)
    return FALSE;

  if (!QUEUE_IS_USING_QUEUE (queue) && queue->current) {
    return queue->current->writing_pos <= queue->current->max_reading_pos;
  } else {
    if (queue->queue.length == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
gst_queue2_is_filled (GstQueue2 * queue)
{
  gboolean res;

  /* always filled on EOS */
  if (queue->is_eos)
    return TRUE;

#define CHECK_FILLED(format,alt_max) ((queue->max_level.format) > 0 && \
    (queue->cur_level.format) >= ((alt_max) ? \
      MIN ((queue->max_level.format), (alt_max)) : (queue->max_level.format)))

  /* if using a ring buffer we're filled if all ring buffer space is used
   * _by the current range_ */
  if (QUEUE_IS_USING_RING_BUFFER (queue)) {
    guint64 rb_size = queue->ring_buffer_max_size;
    GST_DEBUG_OBJECT (queue,
        "max bytes %u, rb size %" G_GUINT64_FORMAT ", cur bytes %u",
        queue->max_level.bytes, rb_size, queue->cur_level.bytes);
    return CHECK_FILLED (bytes, rb_size);
  }

  /* if using file, we're never filled if we don't have EOS */
  if (QUEUE_IS_USING_TEMP_FILE (queue))
    return FALSE;

  /* we are never filled when we have no buffers at all */
  if (queue->cur_level.buffers == 0)
    return FALSE;

  /* we are filled if one of the current levels exceeds the max */
  res = CHECK_FILLED (buffers, 0) || CHECK_FILLED (bytes, 0)
      || CHECK_FILLED (time, 0);

  /* if we need to, use the rate estimate to check against the max time we are
   * allowed to queue */
  if (queue->use_rate_estimate)
    res |= CHECK_FILLED (rate_time, 0);

#undef CHECK_FILLED
  return res;
}

static GstFlowReturn
gst_queue2_chain_buffer_or_buffer_list (GstQueue2 * queue,
    GstMiniObject * item, GstQueue2ItemType item_type)
{
  /* we have to lock the queue since we span threads */
  GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->sinkresult, out_flushing);
  /* when we received EOS, we refuse more data */
  if (queue->is_eos)
    goto out_eos;
  /* when we received unexpected from downstream, refuse more buffers */
  if (queue->unexpected)
    goto out_unexpected;

  if (!gst_queue2_wait_free_space (queue))
    goto out_flushing;

  /* put buffer in queue now */
  gst_queue2_locked_enqueue (queue, item, item_type);
  GST_QUEUE2_MUTEX_UNLOCK (queue);

  return GST_FLOW_OK;

  /* special conditions */
out_flushing:
  {
    GstFlowReturn ret = queue->sinkresult;

    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "exit because task paused, reason: %s", gst_flow_get_name (ret));
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_mini_object_unref (item);

    return ret;
  }
out_eos:
  {
    GST_CAT_LOG_OBJECT (queue_dataflow, queue, "exit because we received EOS");
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_mini_object_unref (item);

    return GST_FLOW_UNEXPECTED;
  }
out_unexpected:
  {
    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "exit because we received UNEXPECTED");
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_mini_object_unref (item);

    return GST_FLOW_UNEXPECTED;
  }
}

static GstFlowReturn
gst_queue2_chain (GstPad * pad, GstBuffer * buffer)
{
  GstQueue2 *queue;

  queue = GST_QUEUE2 (GST_OBJECT_PARENT (pad));

  GST_CAT_LOG_OBJECT (queue_dataflow, queue,
      "received buffer %p of size %d, time %" GST_TIME_FORMAT ", duration %"
      GST_TIME_FORMAT, buffer, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  return gst_queue2_chain_buffer_or_buffer_list (queue,
      GST_MINI_OBJECT_CAST (buffer), GST_QUEUE2_ITEM_TYPE_BUFFER);
}

static GstFlowReturn
gst_queue2_chain_list (GstPad * pad, GstBufferList * buffer_list)
{
  GstQueue2 *queue;

  queue = GST_QUEUE2 (GST_OBJECT_PARENT (pad));

  GST_CAT_LOG_OBJECT (queue_dataflow, queue,
      "received buffer list %p", buffer_list);

  return gst_queue2_chain_buffer_or_buffer_list (queue,
      GST_MINI_OBJECT_CAST (buffer_list), GST_QUEUE2_ITEM_TYPE_BUFFER_LIST);
}

static GstMiniObject *
gst_queue2_dequeue_on_unexpected (GstQueue2 * queue,
    GstQueue2ItemType * item_type)
{
  GstMiniObject *data;

  GST_CAT_LOG_OBJECT (queue_dataflow, queue, "got UNEXPECTED from downstream");

  /* stop pushing buffers, we dequeue all items until we see an item that we
   * can push again, which is EOS or NEWSEGMENT. If there is nothing in the
   * queue we can push, we set a flag to make the sinkpad refuse more
   * buffers with an UNEXPECTED return value until we receive something
   * pushable again or we get flushed. */
  while ((data = gst_queue2_locked_dequeue (queue, item_type))) {
    if (*item_type == GST_QUEUE2_ITEM_TYPE_BUFFER) {
      GST_CAT_LOG_OBJECT (queue_dataflow, queue,
          "dropping UNEXPECTED buffer %p", data);
      gst_buffer_unref (GST_BUFFER_CAST (data));
    } else if (*item_type == GST_QUEUE2_ITEM_TYPE_EVENT) {
      GstEvent *event = GST_EVENT_CAST (data);
      GstEventType type = GST_EVENT_TYPE (event);

      if (type == GST_EVENT_EOS || type == GST_EVENT_NEWSEGMENT) {
        /* we found a pushable item in the queue, push it out */
        GST_CAT_LOG_OBJECT (queue_dataflow, queue,
            "pushing pushable event %s after UNEXPECTED",
            GST_EVENT_TYPE_NAME (event));
        return data;
      }
      GST_CAT_LOG_OBJECT (queue_dataflow, queue,
          "dropping UNEXPECTED event %p", event);
      gst_event_unref (event);
    } else if (*item_type == GST_QUEUE2_ITEM_TYPE_BUFFER_LIST) {
      GST_CAT_LOG_OBJECT (queue_dataflow, queue,
          "dropping UNEXPECTED buffer list %p", data);
      gst_buffer_list_unref (GST_BUFFER_LIST_CAST (data));
    }
  }
  /* no more items in the queue. Set the unexpected flag so that upstream
   * make us refuse any more buffers on the sinkpad. Since we will still
   * accept EOS and NEWSEGMENT we return _FLOW_OK to the caller so that the
   * task function does not shut down. */
  queue->unexpected = TRUE;
  return NULL;
}

/* dequeue an item from the queue an push it downstream. This functions returns
 * the result of the push. */
static GstFlowReturn
gst_queue2_push_one (GstQueue2 * queue)
{
  GstFlowReturn result = GST_FLOW_OK;
  GstMiniObject *data;
  GstQueue2ItemType item_type;

  data = gst_queue2_locked_dequeue (queue, &item_type);
  if (data == NULL)
    goto no_item;

next:
  GST_QUEUE2_MUTEX_UNLOCK (queue);

  if (item_type == GST_QUEUE2_ITEM_TYPE_BUFFER) {
    GstBuffer *buffer;
    GstCaps *caps;

    buffer = GST_BUFFER_CAST (data);
    caps = GST_BUFFER_CAPS (buffer);

    /* set caps before pushing the buffer so that core does not try to do
     * something fancy to check if this is possible. */
    if (caps && caps != GST_PAD_CAPS (queue->srcpad))
      gst_pad_set_caps (queue->srcpad, caps);

    result = gst_pad_push (queue->srcpad, buffer);

    /* need to check for srcresult here as well */
    GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->srcresult, out_flushing);
    if (result == GST_FLOW_UNEXPECTED) {
      data = gst_queue2_dequeue_on_unexpected (queue, &item_type);
      if (data != NULL)
        goto next;
      /* Since we will still accept EOS and NEWSEGMENT we return _FLOW_OK
       * to the caller so that the task function does not shut down */
      result = GST_FLOW_OK;
    }
  } else if (item_type == GST_QUEUE2_ITEM_TYPE_EVENT) {
    GstEvent *event = GST_EVENT_CAST (data);
    GstEventType type = GST_EVENT_TYPE (event);

    gst_pad_push_event (queue->srcpad, event);

    /* if we're EOS, return UNEXPECTED so that the task pauses. */
    if (type == GST_EVENT_EOS) {
      GST_CAT_LOG_OBJECT (queue_dataflow, queue,
          "pushed EOS event %p, return UNEXPECTED", event);
      result = GST_FLOW_UNEXPECTED;
    }

    GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->srcresult, out_flushing);
  } else if (item_type == GST_QUEUE2_ITEM_TYPE_BUFFER_LIST) {
    GstBufferList *buffer_list;
    GstBuffer *first_buf;
    GstCaps *caps;

    buffer_list = GST_BUFFER_LIST_CAST (data);

    first_buf = gst_buffer_list_get (buffer_list, 0, 0);
    caps = (first_buf != NULL) ? GST_BUFFER_CAPS (first_buf) : NULL;

    /* set caps before pushing the buffer so that core does not try to do
     * something fancy to check if this is possible. */
    if (caps && caps != GST_PAD_CAPS (queue->srcpad))
      gst_pad_set_caps (queue->srcpad, caps);

    result = gst_pad_push_list (queue->srcpad, buffer_list);

    /* need to check for srcresult here as well */
    GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->srcresult, out_flushing);
    if (result == GST_FLOW_UNEXPECTED) {
      data = gst_queue2_dequeue_on_unexpected (queue, &item_type);
      if (data != NULL)
        goto next;
      /* Since we will still accept EOS and NEWSEGMENT we return _FLOW_OK
       * to the caller so that the task function does not shut down */
      result = GST_FLOW_OK;
    }
  }
  return result;

  /* ERRORS */
no_item:
  {
    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "exit because we have no item in the queue");
    return GST_FLOW_ERROR;
  }
out_flushing:
  {
    GST_CAT_LOG_OBJECT (queue_dataflow, queue, "exit because we are flushing");
    return GST_FLOW_WRONG_STATE;
  }
}

/* called repeatedly with @pad as the source pad. This function should push out
 * data to the peer element. */
static void
gst_queue2_loop (GstPad * pad)
{
  GstQueue2 *queue;
  GstFlowReturn ret;

  queue = GST_QUEUE2 (GST_PAD_PARENT (pad));

  /* have to lock for thread-safety */
  GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->srcresult, out_flushing);

  if (gst_queue2_is_empty (queue)) {
    gboolean started;

    /* pause the timer while we wait. The fact that we are waiting does not mean
     * the byterate on the output pad is lower */
    if ((started = queue->out_timer_started))
      g_timer_stop (queue->out_timer);

    GST_CAT_DEBUG_OBJECT (queue_dataflow, queue,
        "queue is empty, waiting for new data");
    do {
      /* Wait for data to be available, we could be unlocked because of a flush. */
      GST_QUEUE2_WAIT_ADD_CHECK (queue, queue->srcresult, out_flushing);
    }
    while (gst_queue2_is_empty (queue));

    /* and continue if we were running before */
    if (started)
      g_timer_continue (queue->out_timer);
  }
  ret = gst_queue2_push_one (queue);
  queue->srcresult = ret;
  queue->sinkresult = ret;
  if (ret != GST_FLOW_OK)
    goto out_flushing;

  GST_QUEUE2_MUTEX_UNLOCK (queue);

  return;

  /* ERRORS */
out_flushing:
  {
    gboolean eos = queue->is_eos;
    GstFlowReturn ret = queue->srcresult;

    gst_pad_pause_task (queue->srcpad);
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    GST_CAT_LOG_OBJECT (queue_dataflow, queue,
        "pause task, reason:  %s", gst_flow_get_name (queue->srcresult));
    /* let app know about us giving up if upstream is not expected to do so */
    /* UNEXPECTED is already taken care of elsewhere */
    if (eos && (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED)) {
      GST_ELEMENT_ERROR (queue, STREAM, FAILED,
          (_("Internal data flow error.")),
          ("streaming task paused, reason %s (%d)",
              gst_flow_get_name (ret), ret));
      gst_pad_push_event (queue->srcpad, gst_event_new_eos ());
    }
    return;
  }
}

static gboolean
gst_queue2_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstQueue2 *queue = GST_QUEUE2 (gst_pad_get_parent (pad));

  if (G_UNLIKELY (queue == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }
#ifndef GST_DISABLE_GST_DEBUG
  GST_CAT_DEBUG_OBJECT (queue_dataflow, queue, "got event %p (%s)",
      event, GST_EVENT_TYPE_NAME (event));
#endif

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (QUEUE_IS_USING_QUEUE (queue)) {
        /* just forward upstream */
        res = gst_pad_push_event (queue->sinkpad, event);
      } else {
        /* now unblock the getrange function */
        GST_QUEUE2_MUTEX_LOCK (queue);
        GST_DEBUG_OBJECT (queue, "flushing");
        queue->srcresult = GST_FLOW_WRONG_STATE;
        GST_QUEUE2_SIGNAL_ADD (queue);
        GST_QUEUE2_MUTEX_UNLOCK (queue);

        /* when using a temp file, we eat the event */
        res = TRUE;
        gst_event_unref (event);
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      if (QUEUE_IS_USING_QUEUE (queue)) {
        /* just forward upstream */
        res = gst_pad_push_event (queue->sinkpad, event);
      } else {
        /* now unblock the getrange function */
        GST_QUEUE2_MUTEX_LOCK (queue);
        queue->srcresult = GST_FLOW_OK;
        if (queue->current) {
          /* forget the highest read offset, we'll calculate a new one when we
           * get the next getrange request. We need to do this in order to reset
           * the buffering percentage */
          queue->current->max_reading_pos = 0;
        }
        GST_QUEUE2_MUTEX_UNLOCK (queue);

        /* when using a temp file, we eat the event */
        res = TRUE;
        gst_event_unref (event);
      }
      break;
    default:
      res = gst_pad_push_event (queue->sinkpad, event);
      break;
  }

  gst_object_unref (queue);
  return res;
}

static gboolean
gst_queue2_peer_query (GstQueue2 * queue, GstPad * pad, GstQuery * query)
{
  gboolean ret = FALSE;
  GstPad *peer;

  if ((peer = gst_pad_get_peer (pad))) {
    ret = gst_pad_query (peer, query);
    gst_object_unref (peer);
  }
  return ret;
}

static gboolean
gst_queue2_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstQueue2 *queue;

  queue = GST_QUEUE2 (gst_pad_get_parent (pad));
  if (G_UNLIKELY (queue == NULL))
    return FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 peer_pos;
      GstFormat format;

      if (!gst_queue2_peer_query (queue, queue->sinkpad, query))
        goto peer_failed;

      /* get peer position */
      gst_query_parse_position (query, &format, &peer_pos);

      /* FIXME: this code assumes that there's no discont in the queue */
      switch (format) {
        case GST_FORMAT_BYTES:
          peer_pos -= queue->cur_level.bytes;
          break;
        case GST_FORMAT_TIME:
          peer_pos -= queue->cur_level.time;
          break;
        default:
          GST_WARNING_OBJECT (queue, "dropping query in %s format, don't "
              "know how to adjust value", gst_format_get_name (format));
          return FALSE;
      }
      /* set updated position */
      gst_query_set_position (query, format, peer_pos);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GST_DEBUG_OBJECT (queue, "doing peer query");

      if (!gst_queue2_peer_query (queue, queue->sinkpad, query))
        goto peer_failed;

      GST_DEBUG_OBJECT (queue, "peer query success");
      break;
    }
    case GST_QUERY_BUFFERING:
    {
      GstFormat format;

      GST_DEBUG_OBJECT (queue, "query buffering");

      /* FIXME - is this condition correct? what should ring buffer do? */
      if (QUEUE_IS_USING_QUEUE (queue)) {
        /* no temp file, just forward to the peer */
        if (!gst_queue2_peer_query (queue, queue->sinkpad, query))
          goto peer_failed;
        GST_DEBUG_OBJECT (queue, "buffering forwarded to peer");
      } else {
        gint64 start, stop, range_start, range_stop;
        guint64 writing_pos;
        gint percent;
        gint64 estimated_total, buffering_left;
        GstFormat peer_fmt;
        gint64 duration;
        gboolean peer_res, is_buffering, is_eos;
        gdouble byte_in_rate, byte_out_rate;
        GstQueue2Range *queued_ranges;

        /* we need a current download region */
        if (queue->current == NULL)
          return FALSE;

        writing_pos = queue->current->writing_pos;
        byte_in_rate = queue->byte_in_rate;
        byte_out_rate = queue->byte_out_rate;
        is_buffering = queue->is_buffering;
        is_eos = queue->is_eos;
        percent = queue->buffering_percent;

        if (is_eos) {
          /* we're EOS, we know the duration in bytes now */
          peer_res = TRUE;
          duration = writing_pos;
        } else {
          /* get duration of upstream in bytes */
          peer_fmt = GST_FORMAT_BYTES;
          peer_res = gst_pad_query_peer_duration (queue->sinkpad, &peer_fmt,
              &duration);
        }

        /* calculate remaining and total download time */
        if (peer_res && byte_in_rate > 0.0) {
          estimated_total = (duration * 1000) / byte_in_rate;
          buffering_left = ((duration - writing_pos) * 1000) / byte_in_rate;
        } else {
          estimated_total = -1;
          buffering_left = -1;
        }
        GST_DEBUG_OBJECT (queue, "estimated %" G_GINT64_FORMAT ", left %"
            G_GINT64_FORMAT, estimated_total, buffering_left);

        gst_query_parse_buffering_range (query, &format, NULL, NULL, NULL);

        switch (format) {
          case GST_FORMAT_PERCENT:
            /* we need duration */
            if (!peer_res)
              goto peer_failed;

            GST_DEBUG_OBJECT (queue,
                "duration %" G_GINT64_FORMAT ", writing %" G_GINT64_FORMAT,
                duration, writing_pos);

            start = 0;
            /* get our available data relative to the duration */
            if (duration != -1)
              stop = GST_FORMAT_PERCENT_MAX * writing_pos / duration;
            else
              stop = -1;
            break;
          case GST_FORMAT_BYTES:
            start = 0;
            stop = writing_pos;
            break;
          default:
            start = -1;
            stop = -1;
            break;
        }

        /* fill out the buffered ranges */
        for (queued_ranges = queue->ranges; queued_ranges;
            queued_ranges = queued_ranges->next) {
          switch (format) {
            case GST_FORMAT_PERCENT:
              if (duration == -1) {
                range_start = 0;
                range_stop = 0;
                break;
              }
              range_start = 100 * queued_ranges->offset / duration;
              range_stop = 100 * queued_ranges->writing_pos / duration;
              break;
            case GST_FORMAT_BYTES:
              range_start = queued_ranges->offset;
              range_stop = queued_ranges->writing_pos;
              break;
            default:
              range_start = -1;
              range_stop = -1;
              break;
          }
          if (range_start == range_stop)
            continue;
          GST_DEBUG_OBJECT (queue,
              "range starting at %" G_GINT64_FORMAT " and finishing at %"
              G_GINT64_FORMAT, range_start, range_stop);
          gst_query_add_buffering_range (query, range_start, range_stop);
        }

        gst_query_set_buffering_percent (query, is_buffering, percent);
        gst_query_set_buffering_range (query, format, start, stop,
            estimated_total);
        gst_query_set_buffering_stats (query, GST_BUFFERING_DOWNLOAD,
            byte_in_rate, byte_out_rate, buffering_left);
      }
      break;
    }
    default:
      /* peer handled other queries */
      if (!gst_queue2_peer_query (queue, queue->sinkpad, query))
        goto peer_failed;
      break;
  }

  gst_object_unref (queue);
  return TRUE;

  /* ERRORS */
peer_failed:
  {
    GST_DEBUG_OBJECT (queue, "failed peer query");
    gst_object_unref (queue);
    return FALSE;
  }
}

static gboolean
gst_queue2_handle_query (GstElement * element, GstQuery * query)
{
  /* simply forward to the srcpad query function */
  return gst_queue2_handle_src_query (GST_QUEUE2_CAST (element)->srcpad, query);
}

static void
gst_queue2_update_upstream_size (GstQueue2 * queue)
{
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 upstream_size = -1;

  if (gst_pad_query_peer_duration (queue->sinkpad, &fmt, &upstream_size)) {
    GST_INFO_OBJECT (queue, "upstream size: %" G_GINT64_FORMAT, upstream_size);
    queue->upstream_size = upstream_size;
  }
}

static GstFlowReturn
gst_queue2_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstQueue2 *queue;
  GstFlowReturn ret;

  queue = GST_QUEUE2_CAST (gst_pad_get_parent (pad));

  length = (length == -1) ? DEFAULT_BUFFER_SIZE : length;
  GST_QUEUE2_MUTEX_LOCK_CHECK (queue, queue->srcresult, out_flushing);
  offset = (offset == -1) ? queue->current->reading_pos : offset;

  GST_DEBUG_OBJECT (queue,
      "Getting range: offset %" G_GUINT64_FORMAT ", length %u", offset, length);

  /* catch any reads beyond the size of the file here to make sure queue2
   * doesn't send seek events beyond the size of the file upstream, since
   * that would confuse elements such as souphttpsrc and/or http servers.
   * Demuxers often just loop until EOS at the end of the file to figure out
   * when they've read all the end-headers or index chunks. */
  if (G_UNLIKELY (offset >= queue->upstream_size)) {
    gst_queue2_update_upstream_size (queue);
    if (queue->upstream_size > 0 && offset >= queue->upstream_size)
      goto out_unexpected;
  }

  if (G_UNLIKELY (offset + length > queue->upstream_size)) {
    gst_queue2_update_upstream_size (queue);
    if (queue->upstream_size > 0 && offset + length >= queue->upstream_size) {
      length = queue->upstream_size - offset;
      GST_DEBUG_OBJECT (queue, "adjusting length downto %d", length);
    }
  }

  /* FIXME - function will block when the range is not yet available */
  ret = gst_queue2_create_read (queue, offset, length, buffer);
  GST_QUEUE2_MUTEX_UNLOCK (queue);

  gst_object_unref (queue);

  return ret;

  /* ERRORS */
out_flushing:
  {
    ret = queue->srcresult;

    GST_DEBUG_OBJECT (queue, "we are flushing");
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_object_unref (queue);
    return ret;
  }
out_unexpected:
  {
    GST_DEBUG_OBJECT (queue, "read beyond end of file");
    GST_QUEUE2_MUTEX_UNLOCK (queue);
    gst_object_unref (queue);
    return GST_FLOW_UNEXPECTED;
  }
}

static gboolean
gst_queue2_src_checkgetrange_function (GstPad * pad)
{
  GstQueue2 *queue;
  gboolean ret;

  queue = GST_QUEUE2 (gst_pad_get_parent (pad));

  /* we can operate in pull mode when we are using a tempfile */
  ret = !QUEUE_IS_USING_QUEUE (queue);

  gst_object_unref (GST_OBJECT (queue));

  return ret;
}

/* sink currently only operates in push mode */
static gboolean
gst_queue2_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstQueue2 *queue;

  queue = GST_QUEUE2 (gst_pad_get_parent (pad));

  if (active) {
    GST_QUEUE2_MUTEX_LOCK (queue);
    GST_DEBUG_OBJECT (queue, "activating push mode");
    queue->srcresult = GST_FLOW_OK;
    queue->sinkresult = GST_FLOW_OK;
    queue->is_eos = FALSE;
    queue->unexpected = FALSE;
    reset_rate_timer (queue);
    GST_QUEUE2_MUTEX_UNLOCK (queue);
  } else {
    /* unblock chain function */
    GST_QUEUE2_MUTEX_LOCK (queue);
    GST_DEBUG_OBJECT (queue, "deactivating push mode");
    queue->srcresult = GST_FLOW_WRONG_STATE;
    queue->sinkresult = GST_FLOW_WRONG_STATE;
    gst_queue2_locked_flush (queue);
    GST_QUEUE2_MUTEX_UNLOCK (queue);
  }

  gst_object_unref (queue);

  return result;
}

/* src operating in push mode, we start a task on the source pad that pushes out
 * buffers from the queue */
static gboolean
gst_queue2_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstQueue2 *queue;

  queue = GST_QUEUE2 (gst_pad_get_parent (pad));

  if (active) {
    GST_QUEUE2_MUTEX_LOCK (queue);
    GST_DEBUG_OBJECT (queue, "activating push mode");
    queue->srcresult = GST_FLOW_OK;
    queue->sinkresult = GST_FLOW_OK;
    queue->is_eos = FALSE;
    queue->unexpected = FALSE;
    result = gst_pad_start_task (pad, (GstTaskFunction) gst_queue2_loop, pad);
    GST_QUEUE2_MUTEX_UNLOCK (queue);
  } else {
    /* unblock loop function */
    GST_QUEUE2_MUTEX_LOCK (queue);
    GST_DEBUG_OBJECT (queue, "deactivating push mode");
    queue->srcresult = GST_FLOW_WRONG_STATE;
    queue->sinkresult = GST_FLOW_WRONG_STATE;
    /* the item add signal will unblock */
    GST_QUEUE2_SIGNAL_ADD (queue);
    GST_QUEUE2_MUTEX_UNLOCK (queue);

    /* step 2, make sure streaming finishes */
    result = gst_pad_stop_task (pad);
  }

  gst_object_unref (queue);

  return result;
}

/* pull mode, downstream will call our getrange function */
static gboolean
gst_queue2_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result;
  GstQueue2 *queue;

  queue = GST_QUEUE2 (gst_pad_get_parent (pad));

  if (active) {
    GST_QUEUE2_MUTEX_LOCK (queue);
    if (!QUEUE_IS_USING_QUEUE (queue)) {
      if (QUEUE_IS_USING_TEMP_FILE (queue)) {
        /* open the temp file now */
        result = gst_queue2_open_temp_location_file (queue);
      } else if (!queue->ring_buffer) {
        queue->ring_buffer = g_malloc (queue->ring_buffer_max_size);
        result = ! !queue->ring_buffer;
      } else {
        result = TRUE;
      }

      GST_DEBUG_OBJECT (queue, "activating pull mode");
      init_ranges (queue);
      queue->srcresult = GST_FLOW_OK;
      queue->sinkresult = GST_FLOW_OK;
      queue->is_eos = FALSE;
      queue->unexpected = FALSE;
      queue->upstream_size = 0;
    } else {
      GST_DEBUG_OBJECT (queue, "no temp file, cannot activate pull mode");
      /* this is not allowed, we cannot operate in pull mode without a temp
       * file. */
      queue->srcresult = GST_FLOW_WRONG_STATE;
      queue->sinkresult = GST_FLOW_WRONG_STATE;
      result = FALSE;
    }
    GST_QUEUE2_MUTEX_UNLOCK (queue);
  } else {
    GST_QUEUE2_MUTEX_LOCK (queue);
    GST_DEBUG_OBJECT (queue, "deactivating pull mode");
    queue->srcresult = GST_FLOW_WRONG_STATE;
    queue->sinkresult = GST_FLOW_WRONG_STATE;
    /* this will unlock getrange */
    GST_QUEUE2_SIGNAL_ADD (queue);
    result = TRUE;
    GST_QUEUE2_MUTEX_UNLOCK (queue);
  }
  gst_object_unref (queue);

  return result;
}

static GstStateChangeReturn
gst_queue2_change_state (GstElement * element, GstStateChange transition)
{
  GstQueue2 *queue;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  queue = GST_QUEUE2 (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_QUEUE2_MUTEX_LOCK (queue);
      if (!QUEUE_IS_USING_QUEUE (queue)) {
        if (QUEUE_IS_USING_TEMP_FILE (queue)) {
          if (!gst_queue2_open_temp_location_file (queue))
            ret = GST_STATE_CHANGE_FAILURE;
        } else {
          if (queue->ring_buffer) {
            g_free (queue->ring_buffer);
            queue->ring_buffer = NULL;
          }
          if (!(queue->ring_buffer = g_malloc (queue->ring_buffer_max_size)))
            ret = GST_STATE_CHANGE_FAILURE;
        }
        init_ranges (queue);
      }
      queue->segment_event_received = FALSE;
      queue->starting_segment = NULL;
      GST_QUEUE2_MUTEX_UNLOCK (queue);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_QUEUE2_MUTEX_LOCK (queue);
      if (!QUEUE_IS_USING_QUEUE (queue)) {
        if (QUEUE_IS_USING_TEMP_FILE (queue)) {
          gst_queue2_close_temp_location_file (queue);
        } else if (queue->ring_buffer) {
          g_free (queue->ring_buffer);
          queue->ring_buffer = NULL;
        }
        clean_ranges (queue);
      }
      if (queue->starting_segment != NULL) {
        gst_event_unref (queue->starting_segment);
        queue->starting_segment = NULL;
      }
      GST_QUEUE2_MUTEX_UNLOCK (queue);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

/* changing the capacity of the queue must wake up
 * the _chain function, it might have more room now
 * to store the buffer/event in the queue */
#define QUEUE_CAPACITY_CHANGE(q)\
  GST_QUEUE2_SIGNAL_DEL (queue);

/* Changing the minimum required fill level must
 * wake up the _loop function as it might now
 * be able to preceed.
 */
#define QUEUE_THRESHOLD_CHANGE(q)\
  GST_QUEUE2_SIGNAL_ADD (queue);

static void
gst_queue2_set_temp_template (GstQueue2 * queue, const gchar * template)
{
  GstState state;

  /* the element must be stopped in order to do this */
  GST_OBJECT_LOCK (queue);
  state = GST_STATE (queue);
  if (state != GST_STATE_READY && state != GST_STATE_NULL)
    goto wrong_state;
  GST_OBJECT_UNLOCK (queue);

  /* set new location */
  g_free (queue->temp_template);
  queue->temp_template = g_strdup (template);

  return;

/* ERROR */
wrong_state:
  {
    GST_WARNING_OBJECT (queue, "setting temp-template property in wrong state");
    GST_OBJECT_UNLOCK (queue);
  }
}

static void
gst_queue2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstQueue2 *queue = GST_QUEUE2 (object);

  /* someone could change levels here, and since this
   * affects the get/put funcs, we need to lock for safety. */
  GST_QUEUE2_MUTEX_LOCK (queue);

  switch (prop_id) {
    case PROP_MAX_SIZE_BYTES:
      queue->max_level.bytes = g_value_get_uint (value);
      QUEUE_CAPACITY_CHANGE (queue);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      queue->max_level.buffers = g_value_get_uint (value);
      QUEUE_CAPACITY_CHANGE (queue);
      break;
    case PROP_MAX_SIZE_TIME:
      queue->max_level.time = g_value_get_uint64 (value);
      /* set rate_time to the same value. We use an extra field in the level
       * structure so that we can easily access and compare it */
      queue->max_level.rate_time = queue->max_level.time;
      QUEUE_CAPACITY_CHANGE (queue);
      break;
    case PROP_USE_BUFFERING:
      queue->use_buffering = g_value_get_boolean (value);
      break;
    case PROP_USE_RATE_ESTIMATE:
      queue->use_rate_estimate = g_value_get_boolean (value);
      break;
    case PROP_LOW_PERCENT:
      queue->low_percent = g_value_get_int (value);
      break;
    case PROP_HIGH_PERCENT:
      queue->high_percent = g_value_get_int (value);
      break;
    case PROP_TEMP_TEMPLATE:
      gst_queue2_set_temp_template (queue, g_value_get_string (value));
      break;
    case PROP_TEMP_LOCATION:
      g_free (queue->temp_location);
      queue->temp_location = g_value_dup_string (value);
      /* you can set the property back to NULL to make it use the temp-tmpl
       * property. */
      queue->temp_location_set = queue->temp_location != NULL;
      break;
    case PROP_TEMP_REMOVE:
      queue->temp_remove = g_value_get_boolean (value);
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      queue->ring_buffer_max_size = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_QUEUE2_MUTEX_UNLOCK (queue);
}

static void
gst_queue2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstQueue2 *queue = GST_QUEUE2 (object);

  GST_QUEUE2_MUTEX_LOCK (queue);

  switch (prop_id) {
    case PROP_CUR_LEVEL_BYTES:
      g_value_set_uint (value, queue->cur_level.bytes);
      break;
    case PROP_CUR_LEVEL_BUFFERS:
      g_value_set_uint (value, queue->cur_level.buffers);
      break;
    case PROP_CUR_LEVEL_TIME:
      g_value_set_uint64 (value, queue->cur_level.time);
      break;
    case PROP_MAX_SIZE_BYTES:
      g_value_set_uint (value, queue->max_level.bytes);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      g_value_set_uint (value, queue->max_level.buffers);
      break;
    case PROP_MAX_SIZE_TIME:
      g_value_set_uint64 (value, queue->max_level.time);
      break;
    case PROP_USE_BUFFERING:
      g_value_set_boolean (value, queue->use_buffering);
      break;
    case PROP_USE_RATE_ESTIMATE:
      g_value_set_boolean (value, queue->use_rate_estimate);
      break;
    case PROP_LOW_PERCENT:
      g_value_set_int (value, queue->low_percent);
      break;
    case PROP_HIGH_PERCENT:
      g_value_set_int (value, queue->high_percent);
      break;
    case PROP_TEMP_TEMPLATE:
      g_value_set_string (value, queue->temp_template);
      break;
    case PROP_TEMP_LOCATION:
      g_value_set_string (value, queue->temp_location);
      break;
    case PROP_TEMP_REMOVE:
      g_value_set_boolean (value, queue->temp_remove);
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      g_value_set_uint64 (value, queue->ring_buffer_max_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_QUEUE2_MUTEX_UNLOCK (queue);
}
