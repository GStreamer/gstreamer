/* GStreamer
 * Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
 * Copyright (C) 2007 Jan Schmidt <jan@fluendo.com>
 * Copyright (C) 2007 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2011 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * gstmultiqueue.c:
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-multiqueue
 * @title: multiqueue
 * @see_also: #GstQueue
 *
 * Multiqueue is similar to a normal #GstQueue with the following additional
 * features:
 *
 * 1) Multiple streamhandling
 *
 *  * The element handles queueing data on more than one stream at once. To
 * achieve such a feature it has request sink pads (sink%u) and
 * 'sometimes' src pads (src%u). When requesting a given sinkpad with gst_element_request_pad(),
 * the associated srcpad for that stream will be created.
 * Example: requesting sink1 will generate src1.
 *
 * 2) Non-starvation on multiple stream
 *
 * * If more than one stream is used with the element, the streams' queues
 * will be dynamically grown (up to a limit), in order to ensure that no
 * stream is risking data starvation. This guarantees that at any given
 * time there are at least N bytes queued and available for each individual
 * stream. If an EOS event comes through a srcpad, the associated queue will be
 * considered as 'not-empty' in the queue-size-growing algorithm.
 *
 * 3) Non-linked srcpads graceful handling
 *
 * * In order to better support dynamic switching between streams, the multiqueue
 * (unlike the current GStreamer queue) continues to push buffers on non-linked
 * pads rather than shutting down. In addition, to prevent a non-linked stream from very quickly consuming all
 * available buffers and thus 'racing ahead' of the other streams, the element
 * must ensure that buffers and inlined events for a non-linked stream are pushed
 * in the same order as they were received, relative to the other streams
 * controlled by the element. This means that a buffer cannot be pushed to a
 * non-linked pad any sooner than buffers in any other stream which were received
 * before it.
 *
 * Data is queued until one of the limits specified by the
 * #GstMultiQueue:max-size-buffers, #GstMultiQueue:max-size-bytes and/or
 * #GstMultiQueue:max-size-time properties has been reached. Any attempt to push
 * more buffers into the queue will block the pushing thread until more space
 * becomes available. #GstMultiQueue:extra-size-buffers,
 *
 *
 * #GstMultiQueue:extra-size-bytes and #GstMultiQueue:extra-size-time are
 * currently unused.
 *
 * The default queue size limits are 5 buffers, 10MB of data, or
 * two second worth of data, whichever is reached first. Note that the number
 * of buffers will dynamically grow depending on the fill level of
 * other queues.
 *
 * The #GstMultiQueue::underrun signal is emitted when all of the queues
 * are empty. The #GstMultiQueue::overrun signal is emitted when one of the
 * queues is filled.
 * Both signals are emitted from the context of the streaming thread.
 *
 * When using #GstMultiQueue:sync-by-running-time the unlinked streams will
 * be throttled by the highest running-time of linked streams. This allows
 * further relinking of those unlinked streams without them being in the
 * future (i.e. to achieve gapless playback).
 * When dealing with streams which have got different consumption requirements
 * downstream (ex: video decoders which will consume more buffer (in time) than
 * audio decoders), it is recommended to group streams of the same type
 * by using the pad "group-id" property. This will further throttle streams
 * in time within that group.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <stdio.h>
#include "gstmultiqueue.h"
#include <gst/glib-compat-private.h>

/**
 * GstSingleQueue:
 * @sinkpad: associated sink #GstPad
 * @srcpad: associated source #GstPad
 *
 * Structure containing all information and properties about
 * a single queue.
 */
typedef struct _GstSingleQueue GstSingleQueue;

struct _GstSingleQueue
{
  /* unique identifier of the queue */
  guint id;
  /* group of streams to which this queue belongs to */
  guint groupid;
  GstClockTimeDiff group_high_time;

  GstMultiQueue *mqueue;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* flowreturn of previous srcpad push */
  GstFlowReturn srcresult;
  /* If something was actually pushed on
   * this pad after flushing/pad activation
   * and the srcresult corresponds to something
   * real
   */
  gboolean pushed;

  /* segments */
  GstSegment sink_segment;
  GstSegment src_segment;
  gboolean has_src_segment;     /* preferred over initializing the src_segment to
                                 * UNDEFINED as this doesn't requires adding ifs
                                 * in every segment usage */

  /* position of src/sink */
  GstClockTimeDiff sinktime, srctime;
  /* cached input value, used for interleave */
  GstClockTimeDiff cached_sinktime;
  /* TRUE if either position needs to be recalculated */
  gboolean sink_tainted, src_tainted;

  /* queue of data */
  GstDataQueue *queue;
  GstDataQueueSize max_size, extra_size;
  GstClockTime cur_time;
  gboolean is_eos;
  gboolean is_sparse;
  gboolean flushing;
  gboolean active;

  /* Protected by global lock */
  guint32 nextid;               /* ID of the next object waiting to be pushed */
  guint32 oldid;                /* ID of the last object pushed (last in a series) */
  guint32 last_oldid;           /* Previously observed old_id, reset to MAXUINT32 on flush */
  GstClockTimeDiff next_time;   /* End running time of next buffer to be pushed */
  GstClockTimeDiff last_time;   /* Start running time of last pushed buffer */
  GCond turn;                   /* SingleQueue turn waiting conditional */

  /* for serialized queries */
  GCond query_handled;
  gboolean last_query;
  GstQuery *last_handled_query;

  /* For interleave calculation */
  GThread *thread;
};


/* Extension of GstDataQueueItem structure for our usage */
typedef struct _GstMultiQueueItem GstMultiQueueItem;

struct _GstMultiQueueItem
{
  GstMiniObject *object;
  guint size;
  guint64 duration;
  gboolean visible;

  GDestroyNotify destroy;
  guint32 posid;

  gboolean is_query;
};

static GstSingleQueue *gst_single_queue_new (GstMultiQueue * mqueue, guint id);
static void gst_single_queue_free (GstSingleQueue * squeue);

static void wake_up_next_non_linked (GstMultiQueue * mq);
static void compute_high_id (GstMultiQueue * mq);
static void compute_high_time (GstMultiQueue * mq, guint groupid);
static void single_queue_overrun_cb (GstDataQueue * dq, GstSingleQueue * sq);
static void single_queue_underrun_cb (GstDataQueue * dq, GstSingleQueue * sq);

static void update_buffering (GstMultiQueue * mq, GstSingleQueue * sq);
static void gst_multi_queue_post_buffering (GstMultiQueue * mq);
static void recheck_buffering_status (GstMultiQueue * mq);

static void gst_single_queue_flush_queue (GstSingleQueue * sq, gboolean full);

static void calculate_interleave (GstMultiQueue * mq);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (multi_queue_debug);
#define GST_CAT_DEFAULT (multi_queue_debug)

/* Signals and args */
enum
{
  SIGNAL_UNDERRUN,
  SIGNAL_OVERRUN,
  LAST_SIGNAL
};

/* default limits, we try to keep up to 2 seconds of data and if there is not
 * time, up to 10 MB. The number of buffers is dynamically scaled to make sure
 * there is data in the queues. Normally, the byte and time limits are not hit
 * in theses conditions. */
#define DEFAULT_MAX_SIZE_BYTES 10 * 1024 * 1024 /* 10 MB */
#define DEFAULT_MAX_SIZE_BUFFERS 5
#define DEFAULT_MAX_SIZE_TIME 2 * GST_SECOND

/* second limits. When we hit one of the above limits we are probably dealing
 * with a badly muxed file and we scale the limits to these emergency values.
 * This is currently not yet implemented.
 * Since we dynamically scale the queue buffer size up to the limits but avoid
 * going above the max-size-buffers when we can, we don't really need this
 * aditional extra size. */
#define DEFAULT_EXTRA_SIZE_BYTES 10 * 1024 * 1024       /* 10 MB */
#define DEFAULT_EXTRA_SIZE_BUFFERS 5
#define DEFAULT_EXTRA_SIZE_TIME 3 * GST_SECOND

#define DEFAULT_USE_BUFFERING FALSE
#define DEFAULT_LOW_WATERMARK  0.01
#define DEFAULT_HIGH_WATERMARK 0.99
#define DEFAULT_SYNC_BY_RUNNING_TIME FALSE
#define DEFAULT_USE_INTERLEAVE FALSE
#define DEFAULT_UNLINKED_CACHE_TIME 250 * GST_MSECOND

#define DEFAULT_MINIMUM_INTERLEAVE (250 * GST_MSECOND)

enum
{
  PROP_0,
  PROP_EXTRA_SIZE_BYTES,
  PROP_EXTRA_SIZE_BUFFERS,
  PROP_EXTRA_SIZE_TIME,
  PROP_MAX_SIZE_BYTES,
  PROP_MAX_SIZE_BUFFERS,
  PROP_MAX_SIZE_TIME,
  PROP_USE_BUFFERING,
  PROP_LOW_PERCENT,
  PROP_HIGH_PERCENT,
  PROP_LOW_WATERMARK,
  PROP_HIGH_WATERMARK,
  PROP_SYNC_BY_RUNNING_TIME,
  PROP_USE_INTERLEAVE,
  PROP_UNLINKED_CACHE_TIME,
  PROP_MINIMUM_INTERLEAVE,
  PROP_LAST
};

/* Explanation for buffer levels and percentages:
 *
 * The buffering_level functions here return a value in a normalized range
 * that specifies the current fill level of a queue. The range goes from 0 to
 * MAX_BUFFERING_LEVEL. The low/high watermarks also use this same range.
 *
 * This is not to be confused with the buffering_percent value, which is
 * a *relative* quantity - relative to the low/high watermarks.
 * buffering_percent = 0% means overall buffering_level is at the low watermark.
 * buffering_percent = 100% means overall buffering_level is at the high watermark.
 * buffering_percent is used for determining if the fill level has reached
 * the high watermark, and for producing BUFFERING messages. This value
 * always uses a 0..100 range (since it is a percentage).
 *
 * To avoid future confusions, whenever "buffering level" is mentioned, it
 * refers to the absolute level which is in the 0..MAX_BUFFERING_LEVEL
 * range. Whenever "buffering_percent" is mentioned, it refers to the
 * percentage value that is relative to the low/high watermark. */

/* Using a buffering level range of 0..1000000 to allow for a
 * resolution in ppm (1 ppm = 0.0001%) */
#define MAX_BUFFERING_LEVEL 1000000

/* How much 1% makes up in the buffer level range */
#define BUF_LEVEL_PERCENT_FACTOR ((MAX_BUFFERING_LEVEL) / 100)

/* GstMultiQueuePad */

#define DEFAULT_PAD_GROUP_ID 0

enum
{
  PROP_PAD_0,
  PROP_PAD_GROUP_ID,
};

#define GST_TYPE_MULTIQUEUE_PAD            (gst_multiqueue_pad_get_type())
#define GST_MULTIQUEUE_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTIQUEUE_PAD,GstMultiQueuePad))
#define GST_IS_MULTIQUEUE_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTIQUEUE_PAD))
#define GST_MULTIQUEUE_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_MULTIQUEUE_PAD,GstMultiQueuePadClass))
#define GST_IS_MULTIQUEUE_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_MULTIQUEUE_PAD))
#define GST_MULTIQUEUE_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_MULTIQUEUE_PAD,GstMultiQueuePadClass))

struct _GstMultiQueuePad
{
  GstPad parent;

  GstSingleQueue *sq;
};

struct _GstMultiQueuePadClass
{
  GstPadClass parent_class;
};

GType gst_multiqueue_pad_get_type (void);

G_DEFINE_TYPE (GstMultiQueuePad, gst_multiqueue_pad, GST_TYPE_PAD);
static void
gst_multiqueue_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiQueuePad *pad = GST_MULTIQUEUE_PAD (object);

  switch (prop_id) {
    case PROP_PAD_GROUP_ID:
      if (pad->sq)
        g_value_set_uint (value, pad->sq->groupid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multiqueue_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiQueuePad *pad = GST_MULTIQUEUE_PAD (object);

  switch (prop_id) {
    case PROP_PAD_GROUP_ID:
      GST_OBJECT_LOCK (pad);
      if (pad->sq)
        pad->sq->groupid = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multiqueue_pad_class_init (GstMultiQueuePadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_multiqueue_pad_set_property;
  gobject_class->get_property = gst_multiqueue_pad_get_property;

  /**
   * GstMultiQueuePad:group-id:
   *
   * Group to which this pad belongs.
   *
   * Since: 1.10
   */
  g_object_class_install_property (gobject_class, PROP_PAD_GROUP_ID,
      g_param_spec_uint ("group-id", "Group ID",
          "Group to which this pad belongs", 0, G_MAXUINT32,
          DEFAULT_PAD_GROUP_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_multiqueue_pad_init (GstMultiQueuePad * pad)
{

}


#define GST_MULTI_QUEUE_MUTEX_LOCK(q) G_STMT_START {                          \
  g_mutex_lock (&q->qlock);                                              \
} G_STMT_END

#define GST_MULTI_QUEUE_MUTEX_UNLOCK(q) G_STMT_START {                        \
  g_mutex_unlock (&q->qlock);                                            \
} G_STMT_END

#define SET_PERCENT(mq, perc) G_STMT_START {                             \
  if (perc != mq->buffering_percent) {                                   \
    mq->buffering_percent = perc;                                        \
    mq->buffering_percent_changed = TRUE;                                \
    GST_DEBUG_OBJECT (mq, "buffering %d percent", perc);                 \
  }                                                                      \
} G_STMT_END

/* Convenience function */
static inline GstClockTimeDiff
my_segment_to_running_time (GstSegment * segment, GstClockTime val)
{
  GstClockTimeDiff res = GST_CLOCK_STIME_NONE;

  if (GST_CLOCK_TIME_IS_VALID (val)) {
    gboolean sign =
        gst_segment_to_running_time_full (segment, GST_FORMAT_TIME, val, &val);
    if (sign > 0)
      res = val;
    else if (sign < 0)
      res = -val;
  }
  return res;
}

static void gst_multi_queue_finalize (GObject * object);
static void gst_multi_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_multi_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_multi_queue_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * name, const GstCaps * caps);
static void gst_multi_queue_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn gst_multi_queue_change_state (GstElement *
    element, GstStateChange transition);

static void gst_multi_queue_loop (GstPad * pad);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (multi_queue_debug, "multiqueue", 0, "multiqueue element");
#define gst_multi_queue_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMultiQueue, gst_multi_queue, GST_TYPE_ELEMENT,
    _do_init);

static guint gst_multi_queue_signals[LAST_SIGNAL] = { 0 };

static void
gst_multi_queue_class_init (GstMultiQueueClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_multi_queue_set_property;
  gobject_class->get_property = gst_multi_queue_get_property;

  /* SIGNALS */

  /**
   * GstMultiQueue::underrun:
   * @multiqueue: the multiqueue instance
   *
   * This signal is emitted from the streaming thread when there is
   * no data in any of the queues inside the multiqueue instance (underrun).
   *
   * This indicates either starvation or EOS from the upstream data sources.
   */
  gst_multi_queue_signals[SIGNAL_UNDERRUN] =
      g_signal_new ("underrun", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstMultiQueueClass, underrun), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstMultiQueue::overrun:
   * @multiqueue: the multiqueue instance
   *
   * Reports that one of the queues in the multiqueue is full (overrun).
   * A queue is full if the total amount of data inside it (num-buffers, time,
   * size) is higher than the boundary values which can be set through the
   * GObject properties.
   *
   * This can be used as an indicator of pre-roll.
   */
  gst_multi_queue_signals[SIGNAL_OVERRUN] =
      g_signal_new ("overrun", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstMultiQueueClass, overrun), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /* PROPERTIES */

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint ("max-size-bytes", "Max. size (kB)",
          "Max. amount of data in the queue (bytes, 0=disable)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BYTES,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BUFFERS,
      g_param_spec_uint ("max-size-buffers", "Max. size (buffers)",
          "Max. number of buffers in the queue (0=disable)", 0, G_MAXUINT,
          DEFAULT_MAX_SIZE_BUFFERS,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
          "Max. amount of data in the queue (in ns, 0=disable)", 0, G_MAXUINT64,
          DEFAULT_MAX_SIZE_TIME, G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EXTRA_SIZE_BYTES,
      g_param_spec_uint ("extra-size-bytes", "Extra Size (kB)",
          "Amount of data the queues can grow if one of them is empty (bytes, 0=disable)"
          " (NOT IMPLEMENTED)",
          0, G_MAXUINT, DEFAULT_EXTRA_SIZE_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXTRA_SIZE_BUFFERS,
      g_param_spec_uint ("extra-size-buffers", "Extra Size (buffers)",
          "Amount of buffers the queues can grow if one of them is empty (0=disable)"
          " (NOT IMPLEMENTED)",
          0, G_MAXUINT, DEFAULT_EXTRA_SIZE_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXTRA_SIZE_TIME,
      g_param_spec_uint64 ("extra-size-time", "Extra Size (ns)",
          "Amount of time the queues can grow if one of them is empty (in ns, 0=disable)"
          " (NOT IMPLEMENTED)",
          0, G_MAXUINT64, DEFAULT_EXTRA_SIZE_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiQueue:use-buffering:
   *
   * Enable the buffering option in multiqueue so that BUFFERING messages are
   * emitted based on low-/high-percent thresholds.
   */
  g_object_class_install_property (gobject_class, PROP_USE_BUFFERING,
      g_param_spec_boolean ("use-buffering", "Use buffering",
          "Emit GST_MESSAGE_BUFFERING based on low-/high-percent thresholds",
          DEFAULT_USE_BUFFERING, G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiQueue:low-percent:
   *
   * Low threshold percent for buffering to start.
   */
  g_object_class_install_property (gobject_class, PROP_LOW_PERCENT,
      g_param_spec_int ("low-percent", "Low percent",
          "Low threshold for buffering to start. Only used if use-buffering is True "
          "(Deprecated: use low-watermark instead)",
          0, 100, DEFAULT_LOW_WATERMARK * 100,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiQueue:high-percent:
   *
   * High threshold percent for buffering to finish.
   */
  g_object_class_install_property (gobject_class, PROP_HIGH_PERCENT,
      g_param_spec_int ("high-percent", "High percent",
          "High threshold for buffering to finish. Only used if use-buffering is True "
          "(Deprecated: use high-watermark instead)",
          0, 100, DEFAULT_HIGH_WATERMARK * 100,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiQueue:low-watermark:
   *
   * Low threshold watermark for buffering to start.
   *
   * Since: 1.10
   */
  g_object_class_install_property (gobject_class, PROP_LOW_WATERMARK,
      g_param_spec_double ("low-watermark", "Low watermark",
          "Low threshold for buffering to start. Only used if use-buffering is True",
          0.0, 1.0, DEFAULT_LOW_WATERMARK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiQueue:high-watermark:
   *
   * High threshold watermark for buffering to finish.
   *
   * Since: 1.10
   */
  g_object_class_install_property (gobject_class, PROP_HIGH_WATERMARK,
      g_param_spec_double ("high-watermark", "High watermark",
          "High threshold for buffering to finish. Only used if use-buffering is True",
          0.0, 1.0, DEFAULT_HIGH_WATERMARK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultiQueue:sync-by-running-time:
   *
   * If enabled multiqueue will synchronize deactivated or not-linked streams
   * to the activated and linked streams by taking the running time.
   * Otherwise multiqueue will synchronize the deactivated or not-linked
   * streams by keeping the order in which buffers and events arrived compared
   * to active and linked streams.
   */
  g_object_class_install_property (gobject_class, PROP_SYNC_BY_RUNNING_TIME,
      g_param_spec_boolean ("sync-by-running-time", "Sync By Running Time",
          "Synchronize deactivated or not-linked streams by running time",
          DEFAULT_SYNC_BY_RUNNING_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USE_INTERLEAVE,
      g_param_spec_boolean ("use-interleave", "Use interleave",
          "Adjust time limits based on input interleave",
          DEFAULT_USE_INTERLEAVE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_UNLINKED_CACHE_TIME,
      g_param_spec_uint64 ("unlinked-cache-time", "Unlinked cache time (ns)",
          "Extra buffering in time for unlinked streams (if 'sync-by-running-time')",
          0, G_MAXUINT64, DEFAULT_UNLINKED_CACHE_TIME,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MINIMUM_INTERLEAVE,
      g_param_spec_uint64 ("min-interleave-time", "Minimum interleave time",
          "Minimum extra buffering for deinterleaving (size of the queues) when use-interleave=true",
          0, G_MAXUINT64, DEFAULT_MINIMUM_INTERLEAVE,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_multi_queue_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "MultiQueue",
      "Generic", "Multiple data queue", "Edward Hervey <edward@fluendo.com>");
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_multi_queue_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_multi_queue_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_multi_queue_change_state);
}

static void
gst_multi_queue_init (GstMultiQueue * mqueue)
{
  mqueue->nbqueues = 0;
  mqueue->queues = NULL;

  mqueue->max_size.bytes = DEFAULT_MAX_SIZE_BYTES;
  mqueue->max_size.visible = DEFAULT_MAX_SIZE_BUFFERS;
  mqueue->max_size.time = DEFAULT_MAX_SIZE_TIME;

  mqueue->extra_size.bytes = DEFAULT_EXTRA_SIZE_BYTES;
  mqueue->extra_size.visible = DEFAULT_EXTRA_SIZE_BUFFERS;
  mqueue->extra_size.time = DEFAULT_EXTRA_SIZE_TIME;

  mqueue->use_buffering = DEFAULT_USE_BUFFERING;
  mqueue->low_watermark = DEFAULT_LOW_WATERMARK * MAX_BUFFERING_LEVEL;
  mqueue->high_watermark = DEFAULT_HIGH_WATERMARK * MAX_BUFFERING_LEVEL;

  mqueue->sync_by_running_time = DEFAULT_SYNC_BY_RUNNING_TIME;
  mqueue->use_interleave = DEFAULT_USE_INTERLEAVE;
  mqueue->min_interleave_time = DEFAULT_MINIMUM_INTERLEAVE;
  mqueue->unlinked_cache_time = DEFAULT_UNLINKED_CACHE_TIME;

  mqueue->counter = 1;
  mqueue->highid = -1;
  mqueue->high_time = GST_CLOCK_STIME_NONE;

  g_mutex_init (&mqueue->qlock);
  g_mutex_init (&mqueue->buffering_post_lock);
}

static void
gst_multi_queue_finalize (GObject * object)
{
  GstMultiQueue *mqueue = GST_MULTI_QUEUE (object);

  g_list_foreach (mqueue->queues, (GFunc) gst_single_queue_free, NULL);
  g_list_free (mqueue->queues);
  mqueue->queues = NULL;
  mqueue->queues_cookie++;

  /* free/unref instance data */
  g_mutex_clear (&mqueue->qlock);
  g_mutex_clear (&mqueue->buffering_post_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#define SET_CHILD_PROPERTY(mq,format) G_STMT_START {	        \
    GList * tmp = mq->queues;					\
    while (tmp) {						\
      GstSingleQueue *q = (GstSingleQueue*)tmp->data;		\
      q->max_size.format = mq->max_size.format;                 \
      update_buffering (mq, q);                                 \
      gst_data_queue_limits_changed (q->queue);                 \
      tmp = g_list_next(tmp);					\
    };								\
} G_STMT_END

static void
gst_multi_queue_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiQueue *mq = GST_MULTI_QUEUE (object);

  switch (prop_id) {
    case PROP_MAX_SIZE_BYTES:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      mq->max_size.bytes = g_value_get_uint (value);
      SET_CHILD_PROPERTY (mq, bytes);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      gst_multi_queue_post_buffering (mq);
      break;
    case PROP_MAX_SIZE_BUFFERS:
    {
      GList *tmp;
      gint new_size = g_value_get_uint (value);

      GST_MULTI_QUEUE_MUTEX_LOCK (mq);

      mq->max_size.visible = new_size;

      tmp = mq->queues;
      while (tmp) {
        GstDataQueueSize size;
        GstSingleQueue *q = (GstSingleQueue *) tmp->data;
        gst_data_queue_get_level (q->queue, &size);

        GST_DEBUG_OBJECT (mq, "Queue %d: Requested buffers size: %d,"
            " current: %d, current max %d", q->id, new_size, size.visible,
            q->max_size.visible);

        /* do not reduce max size below current level if the single queue
         * has grown because of empty queue */
        if (new_size == 0) {
          q->max_size.visible = new_size;
        } else if (q->max_size.visible == 0) {
          q->max_size.visible = MAX (new_size, size.visible);
        } else if (new_size > size.visible) {
          q->max_size.visible = new_size;
        }
        update_buffering (mq, q);
        gst_data_queue_limits_changed (q->queue);
        tmp = g_list_next (tmp);
      }

      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      gst_multi_queue_post_buffering (mq);

      break;
    }
    case PROP_MAX_SIZE_TIME:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      mq->max_size.time = g_value_get_uint64 (value);
      SET_CHILD_PROPERTY (mq, time);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      gst_multi_queue_post_buffering (mq);
      break;
    case PROP_EXTRA_SIZE_BYTES:
      mq->extra_size.bytes = g_value_get_uint (value);
      break;
    case PROP_EXTRA_SIZE_BUFFERS:
      mq->extra_size.visible = g_value_get_uint (value);
      break;
    case PROP_EXTRA_SIZE_TIME:
      mq->extra_size.time = g_value_get_uint64 (value);
      break;
    case PROP_USE_BUFFERING:
      mq->use_buffering = g_value_get_boolean (value);
      recheck_buffering_status (mq);
      break;
    case PROP_LOW_PERCENT:
      mq->low_watermark = g_value_get_int (value) * BUF_LEVEL_PERCENT_FACTOR;
      /* Recheck buffering status - the new low_watermark value might
       * be above the current fill level. If the old low_watermark one
       * was below the current level, this means that mq->buffering is
       * disabled and needs to be re-enabled. */
      recheck_buffering_status (mq);
      break;
    case PROP_HIGH_PERCENT:
      mq->high_watermark = g_value_get_int (value) * BUF_LEVEL_PERCENT_FACTOR;
      recheck_buffering_status (mq);
      break;
    case PROP_LOW_WATERMARK:
      mq->low_watermark = g_value_get_double (value) * MAX_BUFFERING_LEVEL;
      recheck_buffering_status (mq);
      break;
    case PROP_HIGH_WATERMARK:
      mq->high_watermark = g_value_get_double (value) * MAX_BUFFERING_LEVEL;
      recheck_buffering_status (mq);
      break;
    case PROP_SYNC_BY_RUNNING_TIME:
      mq->sync_by_running_time = g_value_get_boolean (value);
      break;
    case PROP_USE_INTERLEAVE:
      mq->use_interleave = g_value_get_boolean (value);
      break;
    case PROP_UNLINKED_CACHE_TIME:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      mq->unlinked_cache_time = g_value_get_uint64 (value);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      gst_multi_queue_post_buffering (mq);
      break;
    case PROP_MINIMUM_INTERLEAVE:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      mq->min_interleave_time = g_value_get_uint64 (value);
      if (mq->use_interleave)
        calculate_interleave (mq);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_queue_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiQueue *mq = GST_MULTI_QUEUE (object);

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  switch (prop_id) {
    case PROP_EXTRA_SIZE_BYTES:
      g_value_set_uint (value, mq->extra_size.bytes);
      break;
    case PROP_EXTRA_SIZE_BUFFERS:
      g_value_set_uint (value, mq->extra_size.visible);
      break;
    case PROP_EXTRA_SIZE_TIME:
      g_value_set_uint64 (value, mq->extra_size.time);
      break;
    case PROP_MAX_SIZE_BYTES:
      g_value_set_uint (value, mq->max_size.bytes);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      g_value_set_uint (value, mq->max_size.visible);
      break;
    case PROP_MAX_SIZE_TIME:
      g_value_set_uint64 (value, mq->max_size.time);
      break;
    case PROP_USE_BUFFERING:
      g_value_set_boolean (value, mq->use_buffering);
      break;
    case PROP_LOW_PERCENT:
      g_value_set_int (value, mq->low_watermark / BUF_LEVEL_PERCENT_FACTOR);
      break;
    case PROP_HIGH_PERCENT:
      g_value_set_int (value, mq->high_watermark / BUF_LEVEL_PERCENT_FACTOR);
      break;
    case PROP_LOW_WATERMARK:
      g_value_set_double (value, mq->low_watermark /
          (gdouble) MAX_BUFFERING_LEVEL);
      break;
    case PROP_HIGH_WATERMARK:
      g_value_set_double (value, mq->high_watermark /
          (gdouble) MAX_BUFFERING_LEVEL);
      break;
    case PROP_SYNC_BY_RUNNING_TIME:
      g_value_set_boolean (value, mq->sync_by_running_time);
      break;
    case PROP_USE_INTERLEAVE:
      g_value_set_boolean (value, mq->use_interleave);
      break;
    case PROP_UNLINKED_CACHE_TIME:
      g_value_set_uint64 (value, mq->unlinked_cache_time);
      break;
    case PROP_MINIMUM_INTERLEAVE:
      g_value_set_uint64 (value, mq->min_interleave_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
}

static GstIterator *
gst_multi_queue_iterate_internal_links (GstPad * pad, GstObject * parent)
{
  GstIterator *it = NULL;
  GstPad *opad;
  GstSingleQueue *squeue;
  GstMultiQueue *mq = GST_MULTI_QUEUE (parent);
  GValue val = { 0, };

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  squeue = gst_pad_get_element_private (pad);
  if (!squeue)
    goto out;

  if (squeue->sinkpad == pad)
    opad = gst_object_ref (squeue->srcpad);
  else if (squeue->srcpad == pad)
    opad = gst_object_ref (squeue->sinkpad);
  else
    goto out;

  g_value_init (&val, GST_TYPE_PAD);
  g_value_set_object (&val, opad);
  it = gst_iterator_new_single (GST_TYPE_PAD, &val);
  g_value_unset (&val);

  gst_object_unref (opad);

out:
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  return it;
}


/*
 * GstElement methods
 */

static GstPad *
gst_multi_queue_request_new_pad (GstElement * element, GstPadTemplate * temp,
    const gchar * name, const GstCaps * caps)
{
  GstMultiQueue *mqueue = GST_MULTI_QUEUE (element);
  GstSingleQueue *squeue;
  GstPad *new_pad;
  guint temp_id = -1;

  if (name) {
    sscanf (name + 4, "_%u", &temp_id);
    GST_LOG_OBJECT (element, "name : %s (id %d)", GST_STR_NULL (name), temp_id);
  }

  /* Create a new single queue, add the sink and source pad and return the sink pad */
  squeue = gst_single_queue_new (mqueue, temp_id);

  new_pad = squeue ? squeue->sinkpad : NULL;

  GST_DEBUG_OBJECT (mqueue, "Returning pad %" GST_PTR_FORMAT, new_pad);

  return new_pad;
}

static void
gst_multi_queue_release_pad (GstElement * element, GstPad * pad)
{
  GstMultiQueue *mqueue = GST_MULTI_QUEUE (element);
  GstSingleQueue *sq = NULL;
  GList *tmp;

  GST_LOG_OBJECT (element, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);
  /* Find which single queue it belongs to, knowing that it should be a sinkpad */
  for (tmp = mqueue->queues; tmp; tmp = g_list_next (tmp)) {
    sq = (GstSingleQueue *) tmp->data;

    if (sq->sinkpad == pad)
      break;
  }

  if (!tmp) {
    GST_WARNING_OBJECT (mqueue, "That pad doesn't belong to this element ???");
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);
    return;
  }

  /* FIXME: The removal of the singlequeue should probably not happen until it
   * finishes draining */

  /* remove it from the list */
  mqueue->queues = g_list_delete_link (mqueue->queues, tmp);
  mqueue->queues_cookie++;

  /* FIXME : recompute next-non-linked */
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);

  /* delete SingleQueue */
  gst_data_queue_set_flushing (sq->queue, TRUE);

  gst_pad_set_active (sq->srcpad, FALSE);
  gst_pad_set_active (sq->sinkpad, FALSE);
  gst_pad_set_element_private (sq->srcpad, NULL);
  gst_pad_set_element_private (sq->sinkpad, NULL);
  gst_element_remove_pad (element, sq->srcpad);
  gst_element_remove_pad (element, sq->sinkpad);
  gst_single_queue_free (sq);
}

static GstStateChangeReturn
gst_multi_queue_change_state (GstElement * element, GstStateChange transition)
{
  GstMultiQueue *mqueue = GST_MULTI_QUEUE (element);
  GstSingleQueue *sq = NULL;
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GList *tmp;

      /* Set all pads to non-flushing */
      GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);
      for (tmp = mqueue->queues; tmp; tmp = g_list_next (tmp)) {
        sq = (GstSingleQueue *) tmp->data;
        sq->flushing = FALSE;
      }

      /* the visible limit might not have been set on single queues that have grown because of other queueus were empty */
      SET_CHILD_PROPERTY (mqueue, visible);

      GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);
      gst_multi_queue_post_buffering (mqueue);

      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      GList *tmp;

      /* Un-wait all waiting pads */
      GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);
      for (tmp = mqueue->queues; tmp; tmp = g_list_next (tmp)) {
        sq = (GstSingleQueue *) tmp->data;
        sq->flushing = TRUE;
        g_cond_signal (&sq->turn);

        sq->last_query = FALSE;
        g_cond_signal (&sq->query_handled);
      }
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);
      break;
    }
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return result;
}

static gboolean
gst_single_queue_flush (GstMultiQueue * mq, GstSingleQueue * sq, gboolean flush,
    gboolean full)
{
  gboolean result;

  GST_DEBUG_OBJECT (mq, "flush %s queue %d", (flush ? "start" : "stop"),
      sq->id);

  if (flush) {
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    sq->srcresult = GST_FLOW_FLUSHING;
    gst_data_queue_set_flushing (sq->queue, TRUE);

    sq->flushing = TRUE;

    /* wake up non-linked task */
    GST_LOG_OBJECT (mq, "SingleQueue %d : waking up eventually waiting task",
        sq->id);
    g_cond_signal (&sq->turn);
    sq->last_query = FALSE;
    g_cond_signal (&sq->query_handled);
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

    GST_LOG_OBJECT (mq, "SingleQueue %d : pausing task", sq->id);
    result = gst_pad_pause_task (sq->srcpad);
    sq->sink_tainted = sq->src_tainted = TRUE;
  } else {
    gst_single_queue_flush_queue (sq, full);

    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    gst_segment_init (&sq->sink_segment, GST_FORMAT_TIME);
    gst_segment_init (&sq->src_segment, GST_FORMAT_TIME);
    sq->has_src_segment = FALSE;
    /* All pads start off not-linked for a smooth kick-off */
    sq->srcresult = GST_FLOW_OK;
    sq->pushed = FALSE;
    sq->cur_time = 0;
    sq->max_size.visible = mq->max_size.visible;
    sq->is_eos = FALSE;
    sq->nextid = 0;
    sq->oldid = 0;
    sq->last_oldid = G_MAXUINT32;
    sq->next_time = GST_CLOCK_STIME_NONE;
    sq->last_time = GST_CLOCK_STIME_NONE;
    sq->cached_sinktime = GST_CLOCK_STIME_NONE;
    sq->group_high_time = GST_CLOCK_STIME_NONE;
    gst_data_queue_set_flushing (sq->queue, FALSE);

    /* We will become active again on the next buffer/gap */
    sq->active = FALSE;

    /* Reset high time to be recomputed next */
    mq->high_time = GST_CLOCK_STIME_NONE;

    sq->flushing = FALSE;
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

    GST_LOG_OBJECT (mq, "SingleQueue %d : starting task", sq->id);
    result =
        gst_pad_start_task (sq->srcpad, (GstTaskFunction) gst_multi_queue_loop,
        sq->srcpad, NULL);
  }
  return result;
}

/* WITH LOCK TAKEN */
static gint
get_buffering_level (GstSingleQueue * sq)
{
  GstDataQueueSize size;
  gint buffering_level, tmp;

  gst_data_queue_get_level (sq->queue, &size);

  GST_DEBUG_OBJECT (sq->mqueue,
      "queue %d: visible %u/%u, bytes %u/%u, time %" G_GUINT64_FORMAT "/%"
      G_GUINT64_FORMAT, sq->id, size.visible, sq->max_size.visible,
      size.bytes, sq->max_size.bytes, sq->cur_time, sq->max_size.time);

  /* get bytes and time buffer levels and take the max */
  if (sq->is_eos || sq->srcresult == GST_FLOW_NOT_LINKED || sq->is_sparse) {
    buffering_level = MAX_BUFFERING_LEVEL;
  } else {
    buffering_level = 0;
    if (sq->max_size.time > 0) {
      tmp =
          gst_util_uint64_scale (sq->cur_time,
          MAX_BUFFERING_LEVEL, sq->max_size.time);
      buffering_level = MAX (buffering_level, tmp);
    }
    if (sq->max_size.bytes > 0) {
      tmp =
          gst_util_uint64_scale_int (size.bytes,
          MAX_BUFFERING_LEVEL, sq->max_size.bytes);
      buffering_level = MAX (buffering_level, tmp);
    }
  }

  return buffering_level;
}

/* WITH LOCK TAKEN */
static void
update_buffering (GstMultiQueue * mq, GstSingleQueue * sq)
{
  gint buffering_level, percent;

  /* nothing to dowhen we are not in buffering mode */
  if (!mq->use_buffering)
    return;

  buffering_level = get_buffering_level (sq);

  /* scale so that if buffering_level equals the high watermark,
   * the percentage is 100% */
  percent = gst_util_uint64_scale (buffering_level, 100, mq->high_watermark);
  /* clip */
  if (percent > 100)
    percent = 100;

  if (mq->buffering) {
    if (buffering_level >= mq->high_watermark) {
      mq->buffering = FALSE;
    }
    /* make sure it increases */
    percent = MAX (mq->buffering_percent, percent);

    SET_PERCENT (mq, percent);
  } else {
    GList *iter;
    gboolean is_buffering = TRUE;

    for (iter = mq->queues; iter; iter = g_list_next (iter)) {
      GstSingleQueue *oq = (GstSingleQueue *) iter->data;

      if (get_buffering_level (oq) >= mq->high_watermark) {
        is_buffering = FALSE;

        break;
      }
    }

    if (is_buffering && buffering_level < mq->low_watermark) {
      mq->buffering = TRUE;
      SET_PERCENT (mq, percent);
    }
  }
}

static void
gst_multi_queue_post_buffering (GstMultiQueue * mq)
{
  GstMessage *msg = NULL;

  g_mutex_lock (&mq->buffering_post_lock);
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  if (mq->buffering_percent_changed) {
    gint percent = mq->buffering_percent;

    mq->buffering_percent_changed = FALSE;

    GST_DEBUG_OBJECT (mq, "Going to post buffering: %d%%", percent);
    msg = gst_message_new_buffering (GST_OBJECT_CAST (mq), percent);
  }
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  if (msg != NULL)
    gst_element_post_message (GST_ELEMENT_CAST (mq), msg);

  g_mutex_unlock (&mq->buffering_post_lock);
}

static void
recheck_buffering_status (GstMultiQueue * mq)
{
  if (!mq->use_buffering && mq->buffering) {
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    mq->buffering = FALSE;
    GST_DEBUG_OBJECT (mq,
        "Buffering property disabled, but queue was still buffering; "
        "setting buffering percentage to 100%%");
    SET_PERCENT (mq, 100);
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  }

  if (mq->use_buffering) {
    GList *tmp;
    gint old_perc;

    GST_MULTI_QUEUE_MUTEX_LOCK (mq);

    /* force buffering percentage to be recalculated */
    old_perc = mq->buffering_percent;
    mq->buffering_percent = 0;

    tmp = mq->queues;
    while (tmp) {
      GstSingleQueue *q = (GstSingleQueue *) tmp->data;
      update_buffering (mq, q);
      gst_data_queue_limits_changed (q->queue);
      tmp = g_list_next (tmp);
    }

    GST_DEBUG_OBJECT (mq,
        "Recalculated buffering percentage: old: %d%% new: %d%%",
        old_perc, mq->buffering_percent);

    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  }

  gst_multi_queue_post_buffering (mq);
}

static void
calculate_interleave (GstMultiQueue * mq)
{
  GstClockTimeDiff low, high;
  GstClockTime interleave;
  GList *tmp;

  low = high = GST_CLOCK_STIME_NONE;
  interleave = mq->interleave;
  /* Go over all single queues and calculate lowest/highest value */
  for (tmp = mq->queues; tmp; tmp = tmp->next) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;
    /* Ignore sparse streams for interleave calculation */
    if (sq->is_sparse)
      continue;
    /* If a stream is not active yet (hasn't received any buffers), set
     * a maximum interleave to allow it to receive more data */
    if (!sq->active) {
      GST_LOG_OBJECT (mq,
          "queue %d is not active yet, forcing interleave to 5s", sq->id);
      mq->interleave = 5 * GST_SECOND;
      /* Update max-size time */
      mq->max_size.time = mq->interleave;
      SET_CHILD_PROPERTY (mq, time);
      goto beach;
    }
    if (GST_CLOCK_STIME_IS_VALID (sq->cached_sinktime)) {
      if (low == GST_CLOCK_STIME_NONE || sq->cached_sinktime < low)
        low = sq->cached_sinktime;
      if (high == GST_CLOCK_STIME_NONE || sq->cached_sinktime > high)
        high = sq->cached_sinktime;
    }
    GST_LOG_OBJECT (mq,
        "queue %d , sinktime:%" GST_STIME_FORMAT " low:%" GST_STIME_FORMAT
        " high:%" GST_STIME_FORMAT, sq->id,
        GST_STIME_ARGS (sq->cached_sinktime), GST_STIME_ARGS (low),
        GST_STIME_ARGS (high));
  }

  if (GST_CLOCK_STIME_IS_VALID (low) && GST_CLOCK_STIME_IS_VALID (high)) {
    interleave = high - low;
    /* Padding of interleave and minimum value */
    interleave = (150 * interleave / 100) + mq->min_interleave_time;

    /* Update the stored interleave if:
     * * No data has arrived yet (high == low)
     * * Or it went higher
     * * Or it went lower and we've gone past the previous interleave needed */
    if (high == low || interleave > mq->interleave ||
        ((mq->last_interleave_update + (2 * MIN (GST_SECOND,
                        mq->interleave)) < low)
            && interleave < (mq->interleave * 3 / 4))) {
      /* Update the interleave */
      mq->interleave = interleave;
      mq->last_interleave_update = high;
      /* Update max-size time */
      mq->max_size.time = mq->interleave;
      SET_CHILD_PROPERTY (mq, time);
    }
  }

beach:
  GST_DEBUG_OBJECT (mq,
      "low:%" GST_STIME_FORMAT " high:%" GST_STIME_FORMAT " interleave:%"
      GST_TIME_FORMAT " mq->interleave:%" GST_TIME_FORMAT
      " last_interleave_update:%" GST_STIME_FORMAT, GST_STIME_ARGS (low),
      GST_STIME_ARGS (high), GST_TIME_ARGS (interleave),
      GST_TIME_ARGS (mq->interleave),
      GST_STIME_ARGS (mq->last_interleave_update));
}


/* calculate the diff between running time on the sink and src of the queue.
 * This is the total amount of time in the queue.
 * WITH LOCK TAKEN */
static void
update_time_level (GstMultiQueue * mq, GstSingleQueue * sq)
{
  GstClockTimeDiff sink_time, src_time;

  if (sq->sink_tainted) {
    sink_time = sq->sinktime = my_segment_to_running_time (&sq->sink_segment,
        sq->sink_segment.position);

    GST_DEBUG_OBJECT (mq,
        "queue %d sink_segment.position:%" GST_TIME_FORMAT ", sink_time:%"
        GST_STIME_FORMAT, sq->id, GST_TIME_ARGS (sq->sink_segment.position),
        GST_STIME_ARGS (sink_time));

    if (G_UNLIKELY (sq->last_time == GST_CLOCK_STIME_NONE)) {
      /* If the single queue still doesn't have a last_time set, this means
       * that nothing has been pushed out yet.
       * In order for the high_time computation to be as efficient as possible,
       * we set the last_time */
      sq->last_time = sink_time;
    }
    if (G_UNLIKELY (sink_time != GST_CLOCK_STIME_NONE)) {
      /* if we have a time, we become untainted and use the time */
      sq->sink_tainted = FALSE;
      if (mq->use_interleave) {
        sq->cached_sinktime = sink_time;
        calculate_interleave (mq);
      }
    }
  } else
    sink_time = sq->sinktime;

  if (sq->src_tainted) {
    GstSegment *segment;
    gint64 position;

    if (sq->has_src_segment) {
      segment = &sq->src_segment;
      position = sq->src_segment.position;
    } else {
      /*
       * If the src pad had no segment yet, use the sink segment
       * to avoid signalling overrun if the received sink segment has a
       * a position > max-size-time while the src pad time would be the default=0
       *
       * This can happen when switching pads on chained/adaptive streams and the
       * new chain has a segment with a much larger position
       */
      segment = &sq->sink_segment;
      position = sq->sink_segment.position;
    }

    src_time = sq->srctime = my_segment_to_running_time (segment, position);
    /* if we have a time, we become untainted and use the time */
    if (G_UNLIKELY (src_time != GST_CLOCK_STIME_NONE)) {
      sq->src_tainted = FALSE;
    }
  } else
    src_time = sq->srctime;

  GST_DEBUG_OBJECT (mq,
      "queue %d, sink %" GST_STIME_FORMAT ", src %" GST_STIME_FORMAT, sq->id,
      GST_STIME_ARGS (sink_time), GST_STIME_ARGS (src_time));

  /* This allows for streams with out of order timestamping - sometimes the
   * emerging timestamp is later than the arriving one(s) */
  if (G_LIKELY (GST_CLOCK_STIME_IS_VALID (sink_time) &&
          GST_CLOCK_STIME_IS_VALID (src_time) && sink_time > src_time))
    sq->cur_time = sink_time - src_time;
  else
    sq->cur_time = 0;

  /* updating the time level can change the buffering state */
  update_buffering (mq, sq);

  return;
}

/* take a SEGMENT event and apply the values to segment, updating the time
 * level of queue. */
static void
apply_segment (GstMultiQueue * mq, GstSingleQueue * sq, GstEvent * event,
    GstSegment * segment)
{
  gst_event_copy_segment (event, segment);

  /* now configure the values, we use these to track timestamps on the
   * sinkpad. */
  if (segment->format != GST_FORMAT_TIME) {
    /* non-time format, pretent the current time segment is closed with a
     * 0 start and unknown stop time. */
    segment->format = GST_FORMAT_TIME;
    segment->start = 0;
    segment->stop = -1;
    segment->time = 0;
  }
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  /* Make sure we have a valid initial segment position (and not garbage
   * from upstream) */
  if (segment->rate > 0.0)
    segment->position = segment->start;
  else
    segment->position = segment->stop;
  if (segment == &sq->sink_segment)
    sq->sink_tainted = TRUE;
  else {
    sq->has_src_segment = TRUE;
    sq->src_tainted = TRUE;
  }

  GST_DEBUG_OBJECT (mq,
      "queue %d, configured SEGMENT %" GST_SEGMENT_FORMAT, sq->id, segment);

  /* segment can update the time level of the queue */
  update_time_level (mq, sq);

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  gst_multi_queue_post_buffering (mq);
}

/* take a buffer and update segment, updating the time level of the queue. */
static void
apply_buffer (GstMultiQueue * mq, GstSingleQueue * sq, GstClockTime timestamp,
    GstClockTime duration, GstSegment * segment)
{
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  /* if no timestamp is set, assume it's continuous with the previous
   * time */
  if (timestamp == GST_CLOCK_TIME_NONE)
    timestamp = segment->position;

  /* add duration */
  if (duration != GST_CLOCK_TIME_NONE)
    timestamp += duration;

  GST_DEBUG_OBJECT (mq, "queue %d, %s position updated to %" GST_TIME_FORMAT,
      sq->id, segment == &sq->sink_segment ? "sink" : "src",
      GST_TIME_ARGS (timestamp));

  segment->position = timestamp;

  if (segment == &sq->sink_segment)
    sq->sink_tainted = TRUE;
  else
    sq->src_tainted = TRUE;

  /* calc diff with other end */
  update_time_level (mq, sq);
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  gst_multi_queue_post_buffering (mq);
}

static void
apply_gap (GstMultiQueue * mq, GstSingleQueue * sq, GstEvent * event,
    GstSegment * segment)
{
  GstClockTime timestamp;
  GstClockTime duration;

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  gst_event_parse_gap (event, &timestamp, &duration);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {

    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      timestamp += duration;
    }

    segment->position = timestamp;

    if (segment == &sq->sink_segment)
      sq->sink_tainted = TRUE;
    else
      sq->src_tainted = TRUE;

    /* calc diff with other end */
    update_time_level (mq, sq);
  }

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  gst_multi_queue_post_buffering (mq);
}

static GstClockTimeDiff
get_running_time (GstSegment * segment, GstMiniObject * object, gboolean end)
{
  GstClockTimeDiff time = GST_CLOCK_STIME_NONE;

  if (GST_IS_BUFFER (object)) {
    GstBuffer *buf = GST_BUFFER_CAST (object);
    GstClockTime btime = GST_BUFFER_DTS_OR_PTS (buf);

    if (GST_CLOCK_TIME_IS_VALID (btime)) {
      if (end && GST_BUFFER_DURATION_IS_VALID (buf))
        btime += GST_BUFFER_DURATION (buf);
      if (btime > segment->stop)
        btime = segment->stop;
      time = my_segment_to_running_time (segment, btime);
    }
  } else if (GST_IS_BUFFER_LIST (object)) {
    GstBufferList *list = GST_BUFFER_LIST_CAST (object);
    gint i, n;
    GstBuffer *buf;

    n = gst_buffer_list_length (list);
    for (i = 0; i < n; i++) {
      GstClockTime btime;
      buf = gst_buffer_list_get (list, i);
      btime = GST_BUFFER_DTS_OR_PTS (buf);
      if (GST_CLOCK_TIME_IS_VALID (btime)) {
        if (end && GST_BUFFER_DURATION_IS_VALID (buf))
          btime += GST_BUFFER_DURATION (buf);
        if (btime > segment->stop)
          btime = segment->stop;
        time = my_segment_to_running_time (segment, btime);
        if (!end)
          goto done;
      } else if (!end) {
        goto done;
      }
    }
  } else if (GST_IS_EVENT (object)) {
    GstEvent *event = GST_EVENT_CAST (object);

    /* For newsegment events return the running time of the start position */
    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
      const GstSegment *new_segment;

      gst_event_parse_segment (event, &new_segment);
      if (new_segment->format == GST_FORMAT_TIME) {
        time =
            my_segment_to_running_time ((GstSegment *) new_segment,
            new_segment->start);
      }
    }
  }

done:
  return time;
}

static GstFlowReturn
gst_single_queue_push_one (GstMultiQueue * mq, GstSingleQueue * sq,
    GstMiniObject * object, gboolean * allow_drop)
{
  GstFlowReturn result = sq->srcresult;

  if (GST_IS_BUFFER (object)) {
    GstBuffer *buffer;
    GstClockTime timestamp, duration;

    buffer = GST_BUFFER_CAST (object);
    timestamp = GST_BUFFER_DTS_OR_PTS (buffer);
    duration = GST_BUFFER_DURATION (buffer);

    apply_buffer (mq, sq, timestamp, duration, &sq->src_segment);

    /* Applying the buffer may have made the queue non-full again, unblock it if needed */
    gst_data_queue_limits_changed (sq->queue);

    if (G_UNLIKELY (*allow_drop)) {
      GST_DEBUG_OBJECT (mq,
          "SingleQueue %d : Dropping EOS buffer %p with ts %" GST_TIME_FORMAT,
          sq->id, buffer, GST_TIME_ARGS (timestamp));
      gst_buffer_unref (buffer);
    } else {
      GST_DEBUG_OBJECT (mq,
          "SingleQueue %d : Pushing buffer %p with ts %" GST_TIME_FORMAT,
          sq->id, buffer, GST_TIME_ARGS (timestamp));
      result = gst_pad_push (sq->srcpad, buffer);
    }
  } else if (GST_IS_EVENT (object)) {
    GstEvent *event;

    event = GST_EVENT_CAST (object);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        result = GST_FLOW_EOS;
        if (G_UNLIKELY (*allow_drop))
          *allow_drop = FALSE;
        break;
      case GST_EVENT_SEGMENT:
        apply_segment (mq, sq, event, &sq->src_segment);
        /* Applying the segment may have made the queue non-full again, unblock it if needed */
        gst_data_queue_limits_changed (sq->queue);
        if (G_UNLIKELY (*allow_drop)) {
          result = GST_FLOW_OK;
          *allow_drop = FALSE;
        }
        break;
      case GST_EVENT_GAP:
        apply_gap (mq, sq, event, &sq->src_segment);
        /* Applying the gap may have made the queue non-full again, unblock it if needed */
        gst_data_queue_limits_changed (sq->queue);
        break;
      default:
        break;
    }

    if (G_UNLIKELY (*allow_drop)) {
      GST_DEBUG_OBJECT (mq,
          "SingleQueue %d : Dropping EOS event %p of type %s",
          sq->id, event, GST_EVENT_TYPE_NAME (event));
      gst_event_unref (event);
    } else {
      GST_DEBUG_OBJECT (mq,
          "SingleQueue %d : Pushing event %p of type %s",
          sq->id, event, GST_EVENT_TYPE_NAME (event));

      gst_pad_push_event (sq->srcpad, event);
    }
  } else if (GST_IS_QUERY (object)) {
    GstQuery *query;
    gboolean res;

    query = GST_QUERY_CAST (object);

    if (G_UNLIKELY (*allow_drop)) {
      GST_DEBUG_OBJECT (mq,
          "SingleQueue %d : Dropping EOS query %p", sq->id, query);
      gst_query_unref (query);
      res = FALSE;
    } else {
      res = gst_pad_peer_query (sq->srcpad, query);
    }

    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    sq->last_query = res;
    sq->last_handled_query = query;
    g_cond_signal (&sq->query_handled);
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  } else {
    g_warning ("Unexpected object in singlequeue %u (refcounting problem?)",
        sq->id);
  }
  return result;

  /* ERRORS */
}

static GstMiniObject *
gst_multi_queue_item_steal_object (GstMultiQueueItem * item)
{
  GstMiniObject *res;

  res = item->object;
  item->object = NULL;

  return res;
}

static void
gst_multi_queue_item_destroy (GstMultiQueueItem * item)
{
  if (!item->is_query && item->object)
    gst_mini_object_unref (item->object);
  g_slice_free (GstMultiQueueItem, item);
}

/* takes ownership of passed mini object! */
static GstMultiQueueItem *
gst_multi_queue_buffer_item_new (GstMiniObject * object, guint32 curid)
{
  GstMultiQueueItem *item;

  item = g_slice_new (GstMultiQueueItem);
  item->object = object;
  item->destroy = (GDestroyNotify) gst_multi_queue_item_destroy;
  item->posid = curid;
  item->is_query = GST_IS_QUERY (object);

  item->size = gst_buffer_get_size (GST_BUFFER_CAST (object));
  item->duration = GST_BUFFER_DURATION (object);
  if (item->duration == GST_CLOCK_TIME_NONE)
    item->duration = 0;
  item->visible = TRUE;
  return item;
}

static GstMultiQueueItem *
gst_multi_queue_mo_item_new (GstMiniObject * object, guint32 curid)
{
  GstMultiQueueItem *item;

  item = g_slice_new (GstMultiQueueItem);
  item->object = object;
  item->destroy = (GDestroyNotify) gst_multi_queue_item_destroy;
  item->posid = curid;
  item->is_query = GST_IS_QUERY (object);

  item->size = 0;
  item->duration = 0;
  item->visible = FALSE;
  return item;
}

/* Each main loop attempts to push buffers until the return value
 * is not-linked. not-linked pads are not allowed to push data beyond
 * any linked pads, so they don't 'rush ahead of the pack'.
 */
static void
gst_multi_queue_loop (GstPad * pad)
{
  GstSingleQueue *sq;
  GstMultiQueueItem *item;
  GstDataQueueItem *sitem;
  GstMultiQueue *mq;
  GstMiniObject *object = NULL;
  guint32 newid;
  GstFlowReturn result;
  GstClockTimeDiff next_time;
  gboolean is_buffer;
  gboolean do_update_buffering = FALSE;
  gboolean dropping = FALSE;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = sq->mqueue;

next:
  GST_DEBUG_OBJECT (mq, "SingleQueue %d : trying to pop an object", sq->id);

  if (sq->flushing)
    goto out_flushing;

  /* Get something from the queue, blocking until that happens, or we get
   * flushed */
  if (!(gst_data_queue_pop (sq->queue, &sitem)))
    goto out_flushing;

  item = (GstMultiQueueItem *) sitem;
  newid = item->posid;

  /* steal the object and destroy the item */
  object = gst_multi_queue_item_steal_object (item);
  gst_multi_queue_item_destroy (item);

  is_buffer = GST_IS_BUFFER (object);

  /* Get running time of the item. Events will have GST_CLOCK_STIME_NONE */
  next_time = get_running_time (&sq->src_segment, object, FALSE);

  GST_LOG_OBJECT (mq, "SingleQueue %d : newid:%d , oldid:%d",
      sq->id, newid, sq->last_oldid);

  /* If we're not-linked, we do some extra work because we might need to
   * wait before pushing. If we're linked but there's a gap in the IDs,
   * or it's the first loop, or we just passed the previous highid,
   * we might need to wake some sleeping pad up, so there's extra work
   * there too */
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  if (sq->srcresult == GST_FLOW_NOT_LINKED
      || (sq->last_oldid == G_MAXUINT32) || (newid != (sq->last_oldid + 1))
      || sq->last_oldid > mq->highid) {
    GST_LOG_OBJECT (mq, "CHECKING sq->srcresult: %s",
        gst_flow_get_name (sq->srcresult));

    /* Check again if we're flushing after the lock is taken,
     * the flush flag might have been changed in the meantime */
    if (sq->flushing) {
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      goto out_flushing;
    }

    /* Update the nextid so other threads know when to wake us up */
    sq->nextid = newid;
    /* Take into account the extra cache time since we're unlinked */
    if (GST_CLOCK_STIME_IS_VALID (next_time))
      next_time += mq->unlinked_cache_time;
    sq->next_time = next_time;

    /* Update the oldid (the last ID we output) for highid tracking */
    if (sq->last_oldid != G_MAXUINT32)
      sq->oldid = sq->last_oldid;

    if (sq->srcresult == GST_FLOW_NOT_LINKED) {
      gboolean should_wait;
      /* Go to sleep until it's time to push this buffer */

      /* Recompute the highid */
      compute_high_id (mq);
      /* Recompute the high time */
      compute_high_time (mq, sq->groupid);

      GST_DEBUG_OBJECT (mq,
          "groupid %d high_time %" GST_STIME_FORMAT " next_time %"
          GST_STIME_FORMAT, sq->groupid, GST_STIME_ARGS (sq->group_high_time),
          GST_STIME_ARGS (next_time));

      if (mq->sync_by_running_time) {
        if (sq->group_high_time == GST_CLOCK_STIME_NONE) {
          should_wait = GST_CLOCK_STIME_IS_VALID (next_time) &&
              (mq->high_time == GST_CLOCK_STIME_NONE
              || next_time > mq->high_time);
        } else {
          should_wait = GST_CLOCK_STIME_IS_VALID (next_time) &&
              next_time > sq->group_high_time;
        }
      } else
        should_wait = newid > mq->highid;

      while (should_wait && sq->srcresult == GST_FLOW_NOT_LINKED) {

        GST_DEBUG_OBJECT (mq,
            "queue %d sleeping for not-linked wakeup with "
            "newid %u, highid %u, next_time %" GST_STIME_FORMAT
            ", high_time %" GST_STIME_FORMAT, sq->id, newid, mq->highid,
            GST_STIME_ARGS (next_time), GST_STIME_ARGS (sq->group_high_time));

        /* Wake up all non-linked pads before we sleep */
        wake_up_next_non_linked (mq);

        mq->numwaiting++;
        g_cond_wait (&sq->turn, &mq->qlock);
        mq->numwaiting--;

        if (sq->flushing) {
          GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
          goto out_flushing;
        }

        /* Recompute the high time and ID */
        compute_high_time (mq, sq->groupid);
        compute_high_id (mq);

        GST_DEBUG_OBJECT (mq, "queue %d woken from sleeping for not-linked "
            "wakeup with newid %u, highid %u, next_time %" GST_STIME_FORMAT
            ", high_time %" GST_STIME_FORMAT " mq high_time %" GST_STIME_FORMAT,
            sq->id, newid, mq->highid,
            GST_STIME_ARGS (next_time), GST_STIME_ARGS (sq->group_high_time),
            GST_STIME_ARGS (mq->high_time));

        if (mq->sync_by_running_time) {
          if (sq->group_high_time == GST_CLOCK_STIME_NONE) {
            should_wait = GST_CLOCK_STIME_IS_VALID (next_time) &&
                (mq->high_time == GST_CLOCK_STIME_NONE
                || next_time > mq->high_time);
          } else {
            should_wait = GST_CLOCK_STIME_IS_VALID (next_time) &&
                next_time > sq->group_high_time;
          }
        } else
          should_wait = newid > mq->highid;
      }

      /* Re-compute the high_id in case someone else pushed */
      compute_high_id (mq);
      compute_high_time (mq, sq->groupid);
    } else {
      compute_high_id (mq);
      compute_high_time (mq, sq->groupid);
      /* Wake up all non-linked pads */
      wake_up_next_non_linked (mq);
    }
    /* We're done waiting, we can clear the nextid and nexttime */
    sq->nextid = 0;
    sq->next_time = GST_CLOCK_STIME_NONE;
  }
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  if (sq->flushing)
    goto out_flushing;

  GST_LOG_OBJECT (mq, "sq:%d BEFORE PUSHING sq->srcresult: %s", sq->id,
      gst_flow_get_name (sq->srcresult));

  /* Update time stats */
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  next_time = get_running_time (&sq->src_segment, object, TRUE);
  if (GST_CLOCK_STIME_IS_VALID (next_time)) {
    if (sq->last_time == GST_CLOCK_STIME_NONE || sq->last_time < next_time)
      sq->last_time = next_time;
    if (mq->high_time == GST_CLOCK_STIME_NONE || mq->high_time <= next_time) {
      /* Wake up all non-linked pads now that we advanced the high time */
      mq->high_time = next_time;
      wake_up_next_non_linked (mq);
    }
  }
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  /* Try to push out the new object */
  result = gst_single_queue_push_one (mq, sq, object, &dropping);
  object = NULL;

  /* Check if we pushed something already and if this is
   * now a switch from an active to a non-active stream.
   *
   * If it is, we reset all the waiting streams, let them
   * push another buffer to see if they're now active again.
   * This allows faster switching between streams and prevents
   * deadlocks if downstream does any waiting too.
   */
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  if (sq->pushed && sq->srcresult == GST_FLOW_OK
      && result == GST_FLOW_NOT_LINKED) {
    GList *tmp;

    GST_LOG_OBJECT (mq, "SingleQueue %d : Changed from active to non-active",
        sq->id);

    compute_high_id (mq);
    compute_high_time (mq, sq->groupid);
    do_update_buffering = TRUE;

    /* maybe no-one is waiting */
    if (mq->numwaiting > 0) {
      /* Else figure out which singlequeue(s) need waking up */
      for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
        GstSingleQueue *sq2 = (GstSingleQueue *) tmp->data;

        if (sq2->srcresult == GST_FLOW_NOT_LINKED) {
          GST_LOG_OBJECT (mq, "Waking up singlequeue %d", sq2->id);
          sq2->pushed = FALSE;
          sq2->srcresult = GST_FLOW_OK;
          g_cond_signal (&sq2->turn);
        }
      }
    }
  }

  if (is_buffer)
    sq->pushed = TRUE;

  /* now hold on a bit;
   * can not simply throw this result to upstream, because
   * that might already be onto another segment, so we have to make
   * sure we are relaying the correct info wrt proper segment */
  if (result == GST_FLOW_EOS && !dropping &&
      sq->srcresult != GST_FLOW_NOT_LINKED) {
    GST_DEBUG_OBJECT (mq, "starting EOS drop on sq %d", sq->id);
    dropping = TRUE;
    /* pretend we have not seen EOS yet for upstream's sake */
    result = sq->srcresult;
  } else if (dropping && gst_data_queue_is_empty (sq->queue)) {
    /* queue empty, so stop dropping
     * we can commit the result we have now,
     * which is either OK after a segment, or EOS */
    GST_DEBUG_OBJECT (mq, "committed EOS drop on sq %d", sq->id);
    dropping = FALSE;
    result = GST_FLOW_EOS;
  }
  sq->srcresult = result;
  sq->last_oldid = newid;

  if (do_update_buffering)
    update_buffering (mq, sq);

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  gst_multi_queue_post_buffering (mq);

  GST_LOG_OBJECT (mq, "sq:%d AFTER PUSHING sq->srcresult: %s (is_eos:%d)",
      sq->id, gst_flow_get_name (sq->srcresult), GST_PAD_IS_EOS (sq->srcpad));

  /* Need to make sure wake up any sleeping pads when we exit */
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  if (mq->numwaiting > 0 && (GST_PAD_IS_EOS (sq->srcpad)
          || sq->srcresult == GST_FLOW_EOS)) {
    compute_high_time (mq, sq->groupid);
    compute_high_id (mq);
    wake_up_next_non_linked (mq);
  }
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  if (dropping)
    goto next;

  if (result != GST_FLOW_OK && result != GST_FLOW_NOT_LINKED
      && result != GST_FLOW_EOS)
    goto out_flushing;

  return;

out_flushing:
  {
    if (object)
      gst_mini_object_unref (object);

    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    sq->last_query = FALSE;
    g_cond_signal (&sq->query_handled);

    /* Post an error message if we got EOS while downstream
     * has returned an error flow return. After EOS there
     * will be no further buffer which could propagate the
     * error upstream */
    if (sq->is_eos && sq->srcresult < GST_FLOW_EOS) {
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      GST_ELEMENT_FLOW_ERROR (mq, sq->srcresult);
    } else {
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
    }

    /* upstream needs to see fatal result ASAP to shut things down,
     * but might be stuck in one of our other full queues;
     * so empty this one and trigger dynamic queue growth. At
     * this point the srcresult is not OK, NOT_LINKED
     * or EOS, i.e. a real failure */
    gst_single_queue_flush_queue (sq, FALSE);
    single_queue_underrun_cb (sq->queue, sq);
    gst_data_queue_set_flushing (sq->queue, TRUE);
    gst_pad_pause_task (sq->srcpad);
    GST_CAT_LOG_OBJECT (multi_queue_debug, mq,
        "SingleQueue[%d] task paused, reason:%s",
        sq->id, gst_flow_get_name (sq->srcresult));
    return;
  }
}

/**
 * gst_multi_queue_chain:
 *
 * This is similar to GstQueue's chain function, except:
 * _ we don't have leak behaviours,
 * _ we push with a unique id (curid)
 */
static GstFlowReturn
gst_multi_queue_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSingleQueue *sq;
  GstMultiQueue *mq;
  GstMultiQueueItem *item;
  guint32 curid;
  GstClockTime timestamp, duration;

  sq = gst_pad_get_element_private (pad);
  mq = sq->mqueue;

  /* if eos, we are always full, so avoid hanging incoming indefinitely */
  if (sq->is_eos)
    goto was_eos;

  sq->active = TRUE;

  /* Get a unique incrementing id */
  curid = g_atomic_int_add ((gint *) & mq->counter, 1);

  timestamp = GST_BUFFER_DTS_OR_PTS (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  GST_LOG_OBJECT (mq,
      "SingleQueue %d : about to enqueue buffer %p with id %d (pts:%"
      GST_TIME_FORMAT " dts:%" GST_TIME_FORMAT " dur:%" GST_TIME_FORMAT ")",
      sq->id, buffer, curid, GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DTS (buffer)), GST_TIME_ARGS (duration));

  item = gst_multi_queue_buffer_item_new (GST_MINI_OBJECT_CAST (buffer), curid);

  /* Update interleave before pushing data into queue */
  if (mq->use_interleave) {
    GstClockTime val = timestamp;
    GstClockTimeDiff dval;

    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    if (val == GST_CLOCK_TIME_NONE)
      val = sq->sink_segment.position;
    if (duration != GST_CLOCK_TIME_NONE)
      val += duration;

    dval = my_segment_to_running_time (&sq->sink_segment, val);
    if (GST_CLOCK_STIME_IS_VALID (dval)) {
      sq->cached_sinktime = dval;
      GST_DEBUG_OBJECT (mq,
          "Queue %d cached sink time now %" G_GINT64_FORMAT " %"
          GST_STIME_FORMAT, sq->id, sq->cached_sinktime,
          GST_STIME_ARGS (sq->cached_sinktime));
      calculate_interleave (mq);
    }
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  }

  if (!(gst_data_queue_push (sq->queue, (GstDataQueueItem *) item)))
    goto flushing;

  /* update time level, we must do this after pushing the data in the queue so
   * that we never end up filling the queue first. */
  apply_buffer (mq, sq, timestamp, duration, &sq->sink_segment);

done:
  return sq->srcresult;

  /* ERRORS */
flushing:
  {
    GST_LOG_OBJECT (mq, "SingleQueue %d : exit because task paused, reason: %s",
        sq->id, gst_flow_get_name (sq->srcresult));
    gst_multi_queue_item_destroy (item);
    goto done;
  }
was_eos:
  {
    GST_DEBUG_OBJECT (mq, "we are EOS, dropping buffer, return EOS");
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_multi_queue_sink_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;
  GstSingleQueue *sq;
  GstMultiQueue *mq;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = (GstMultiQueue *) gst_pad_get_parent (pad);

  /* mq is NULL if the pad is activated/deactivated before being
   * added to the multiqueue */
  if (mq)
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        /* All pads start off linked until they push one buffer */
        sq->srcresult = GST_FLOW_OK;
        sq->pushed = FALSE;
        gst_data_queue_set_flushing (sq->queue, FALSE);
      } else {
        sq->srcresult = GST_FLOW_FLUSHING;
        sq->last_query = FALSE;
        g_cond_signal (&sq->query_handled);
        gst_data_queue_set_flushing (sq->queue, TRUE);

        /* Wait until streaming thread has finished */
        if (mq)
          GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
        GST_PAD_STREAM_LOCK (pad);
        if (mq)
          GST_MULTI_QUEUE_MUTEX_LOCK (mq);
        gst_data_queue_flush (sq->queue);
        if (mq)
          GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
        GST_PAD_STREAM_UNLOCK (pad);
        if (mq)
          GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      }
      res = TRUE;
      break;
    default:
      res = FALSE;
      break;
  }

  if (mq) {
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
    gst_object_unref (mq);
  }

  return res;
}

static GstFlowReturn
gst_multi_queue_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSingleQueue *sq;
  GstMultiQueue *mq;
  guint32 curid;
  GstMultiQueueItem *item;
  gboolean res = TRUE;
  GstFlowReturn flowret = GST_FLOW_OK;
  GstEventType type;
  GstEvent *sref = NULL;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = (GstMultiQueue *) parent;

  type = GST_EVENT_TYPE (event);

  switch (type) {
    case GST_EVENT_STREAM_START:
    {
      if (mq->sync_by_running_time) {
        GstStreamFlags stream_flags;
        gst_event_parse_stream_flags (event, &stream_flags);
        if ((stream_flags & GST_STREAM_FLAG_SPARSE)) {
          GST_INFO_OBJECT (mq, "SingleQueue %d is a sparse stream", sq->id);
          sq->is_sparse = TRUE;
        }
      }

      sq->thread = g_thread_self ();

      /* Remove EOS flag */
      sq->is_eos = FALSE;
      break;
    }
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (mq, "SingleQueue %d : received flush start event",
          sq->id);

      res = gst_pad_push_event (sq->srcpad, event);

      gst_single_queue_flush (mq, sq, TRUE, FALSE);
      goto done;

    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (mq, "SingleQueue %d : received flush stop event",
          sq->id);

      res = gst_pad_push_event (sq->srcpad, event);

      gst_single_queue_flush (mq, sq, FALSE, FALSE);
      goto done;

    case GST_EVENT_SEGMENT:
      sref = gst_event_ref (event);
      break;
    case GST_EVENT_GAP:
      /* take ref because the queue will take ownership and we need the event
       * afterwards to update the segment */
      sref = gst_event_ref (event);
      if (mq->use_interleave) {
        GstClockTime val, dur;
        GstClockTime stime;
        gst_event_parse_gap (event, &val, &dur);
        if (GST_CLOCK_TIME_IS_VALID (val)) {
          GST_MULTI_QUEUE_MUTEX_LOCK (mq);
          if (GST_CLOCK_TIME_IS_VALID (dur))
            val += dur;
          stime = my_segment_to_running_time (&sq->sink_segment, val);
          if (GST_CLOCK_STIME_IS_VALID (stime)) {
            sq->cached_sinktime = stime;
            calculate_interleave (mq);
          }
          GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
        }
      }
      break;

    default:
      if (!(GST_EVENT_IS_SERIALIZED (event))) {
        res = gst_pad_push_event (sq->srcpad, event);
        goto done;
      }
      break;
  }

  /* if eos, we are always full, so avoid hanging incoming indefinitely */
  if (sq->is_eos)
    goto was_eos;

  /* Get an unique incrementing id. */
  curid = g_atomic_int_add ((gint *) & mq->counter, 1);

  item = gst_multi_queue_mo_item_new ((GstMiniObject *) event, curid);

  GST_DEBUG_OBJECT (mq,
      "SingleQueue %d : Enqueuing event %p of type %s with id %d",
      sq->id, event, GST_EVENT_TYPE_NAME (event), curid);

  if (!gst_data_queue_push (sq->queue, (GstDataQueueItem *) item))
    goto flushing;

  /* mark EOS when we received one, we must do that after putting the
   * buffer in the queue because EOS marks the buffer as filled. */
  switch (type) {
    case GST_EVENT_EOS:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      sq->is_eos = TRUE;

      /* Post an error message if we got EOS while downstream
       * has returned an error flow return. After EOS there
       * will be no further buffer which could propagate the
       * error upstream */
      if (sq->srcresult < GST_FLOW_EOS) {
        GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
        GST_ELEMENT_FLOW_ERROR (mq, sq->srcresult);
      } else {
        GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      }

      /* EOS affects the buffering state */
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      update_buffering (mq, sq);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      single_queue_overrun_cb (sq->queue, sq);
      gst_multi_queue_post_buffering (mq);
      break;
    case GST_EVENT_SEGMENT:
      apply_segment (mq, sq, sref, &sq->sink_segment);
      gst_event_unref (sref);
      /* a new segment allows us to accept more buffers if we got EOS
       * from downstream */
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      if (sq->srcresult == GST_FLOW_EOS)
        sq->srcresult = GST_FLOW_OK;
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      break;
    case GST_EVENT_GAP:
      sq->active = TRUE;
      apply_gap (mq, sq, sref, &sq->sink_segment);
      gst_event_unref (sref);
    default:
      break;
  }

done:
  if (res == FALSE)
    flowret = GST_FLOW_ERROR;
  GST_DEBUG_OBJECT (mq, "SingleQueue %d : returning %s", sq->id,
      gst_flow_get_name (flowret));
  return flowret;

flushing:
  {
    GST_LOG_OBJECT (mq, "SingleQueue %d : exit because task paused, reason: %s",
        sq->id, gst_flow_get_name (sq->srcresult));
    if (sref)
      gst_event_unref (sref);
    gst_multi_queue_item_destroy (item);
    return sq->srcresult;
  }
was_eos:
  {
    GST_DEBUG_OBJECT (mq, "we are EOS, dropping event, return GST_FLOW_EOS");
    gst_event_unref (event);
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_multi_queue_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;
  GstSingleQueue *sq;
  GstMultiQueue *mq;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = (GstMultiQueue *) parent;

  switch (GST_QUERY_TYPE (query)) {
    default:
      if (GST_QUERY_IS_SERIALIZED (query)) {
        guint32 curid;
        GstMultiQueueItem *item;

        GST_MULTI_QUEUE_MUTEX_LOCK (mq);
        if (sq->srcresult != GST_FLOW_OK)
          goto out_flushing;

        /* serialized events go in the queue. We need to be certain that we
         * don't cause deadlocks waiting for the query return value. We check if
         * the queue is empty (nothing is blocking downstream and the query can
         * be pushed for sure) or we are not buffering. If we are buffering,
         * the pipeline waits to unblock downstream until our queue fills up
         * completely, which can not happen if we block on the query..
         * Therefore we only potentially block when we are not buffering. */
        if (!mq->use_buffering || gst_data_queue_is_empty (sq->queue)) {
          /* Get an unique incrementing id. */
          curid = g_atomic_int_add ((gint *) & mq->counter, 1);

          item = gst_multi_queue_mo_item_new ((GstMiniObject *) query, curid);

          GST_DEBUG_OBJECT (mq,
              "SingleQueue %d : Enqueuing query %p of type %s with id %d",
              sq->id, query, GST_QUERY_TYPE_NAME (query), curid);
          GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
          res = gst_data_queue_push (sq->queue, (GstDataQueueItem *) item);
          GST_MULTI_QUEUE_MUTEX_LOCK (mq);
          if (!res || sq->flushing)
            goto out_flushing;
          /* it might be that the query has been taken out of the queue
           * while we were unlocked. So, we need to check if the last
           * handled query is the same one than the one we just
           * pushed. If it is, we don't need to wait for the condition
           * variable, otherwise we wait for the condition variable to
           * be signaled. */
          while (!sq->flushing && sq->srcresult == GST_FLOW_OK
              && sq->last_handled_query != query)
            g_cond_wait (&sq->query_handled, &mq->qlock);
          res = sq->last_query;
          sq->last_handled_query = NULL;
        } else {
          GST_DEBUG_OBJECT (mq, "refusing query, we are buffering and the "
              "queue is not empty");
          res = FALSE;
        }
        GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      } else {
        /* default handling */
        res = gst_pad_query_default (pad, parent, query);
      }
      break;
  }
  return res;

out_flushing:
  {
    GST_DEBUG_OBJECT (mq, "Flushing");
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
    return FALSE;
  }
}

static gboolean
gst_multi_queue_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstMultiQueue *mq;
  GstSingleQueue *sq;
  gboolean result;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = sq->mqueue;

  GST_DEBUG_OBJECT (mq, "SingleQueue %d", sq->id);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        result = gst_single_queue_flush (mq, sq, FALSE, TRUE);
      } else {
        result = gst_single_queue_flush (mq, sq, TRUE, TRUE);
        /* make sure streaming finishes */
        result |= gst_pad_stop_task (pad);
      }
      break;
    default:
      result = FALSE;
      break;
  }
  return result;
}

static gboolean
gst_multi_queue_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);
  GstMultiQueue *mq = sq->mqueue;
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_RECONFIGURE:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      if (sq->srcresult == GST_FLOW_NOT_LINKED) {
        sq->srcresult = GST_FLOW_OK;
        g_cond_signal (&sq->turn);
      }
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

      ret = gst_pad_push_event (sq->sinkpad, event);
      break;
    default:
      ret = gst_pad_push_event (sq->sinkpad, event);
      break;
  }

  return ret;
}

static gboolean
gst_multi_queue_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  /* FIXME, Handle position offset depending on queue size */
  switch (GST_QUERY_TYPE (query)) {
    default:
      /* default handling */
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

/*
 * Next-non-linked functions
 */

/* WITH LOCK TAKEN */
static void
wake_up_next_non_linked (GstMultiQueue * mq)
{
  GList *tmp;

  /* maybe no-one is waiting */
  if (mq->numwaiting < 1)
    return;

  if (mq->sync_by_running_time && GST_CLOCK_STIME_IS_VALID (mq->high_time)) {
    /* Else figure out which singlequeue(s) need waking up */
    for (tmp = mq->queues; tmp; tmp = tmp->next) {
      GstSingleQueue *sq = (GstSingleQueue *) tmp->data;
      if (sq->srcresult == GST_FLOW_NOT_LINKED) {
        GstClockTimeDiff high_time;

        if (GST_CLOCK_STIME_IS_VALID (sq->group_high_time))
          high_time = sq->group_high_time;
        else
          high_time = mq->high_time;

        if (GST_CLOCK_STIME_IS_VALID (sq->next_time) &&
            GST_CLOCK_STIME_IS_VALID (high_time)
            && sq->next_time <= high_time) {
          GST_LOG_OBJECT (mq, "Waking up singlequeue %d", sq->id);
          g_cond_signal (&sq->turn);
        }
      }
    }
  } else {
    /* Else figure out which singlequeue(s) need waking up */
    for (tmp = mq->queues; tmp; tmp = tmp->next) {
      GstSingleQueue *sq = (GstSingleQueue *) tmp->data;
      if (sq->srcresult == GST_FLOW_NOT_LINKED &&
          sq->nextid != 0 && sq->nextid <= mq->highid) {
        GST_LOG_OBJECT (mq, "Waking up singlequeue %d", sq->id);
        g_cond_signal (&sq->turn);
      }
    }
  }
}

/* WITH LOCK TAKEN */
static void
compute_high_id (GstMultiQueue * mq)
{
  /* The high-id is either the highest id among the linked pads, or if all
   * pads are not-linked, it's the lowest not-linked pad */
  GList *tmp;
  guint32 lowest = G_MAXUINT32;
  guint32 highid = G_MAXUINT32;

  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;

    GST_LOG_OBJECT (mq, "inspecting sq:%d , nextid:%d, oldid:%d, srcresult:%s",
        sq->id, sq->nextid, sq->oldid, gst_flow_get_name (sq->srcresult));

    if (sq->srcresult == GST_FLOW_NOT_LINKED) {
      /* No need to consider queues which are not waiting */
      if (sq->nextid == 0) {
        GST_LOG_OBJECT (mq, "sq:%d is not waiting - ignoring", sq->id);
        continue;
      }

      if (sq->nextid < lowest)
        lowest = sq->nextid;
    } else if (!GST_PAD_IS_EOS (sq->srcpad) && sq->srcresult != GST_FLOW_EOS) {
      /* If we don't have a global highid, or the global highid is lower than
       * this single queue's last outputted id, store the queue's one,
       * unless the singlequeue output is at EOS */
      if ((highid == G_MAXUINT32) || (sq->oldid > highid))
        highid = sq->oldid;
    }
  }

  if (highid == G_MAXUINT32 || lowest < highid)
    mq->highid = lowest;
  else
    mq->highid = highid;

  GST_LOG_OBJECT (mq, "Highid is now : %u, lowest non-linked %u", mq->highid,
      lowest);
}

/* WITH LOCK TAKEN */
static void
compute_high_time (GstMultiQueue * mq, guint groupid)
{
  /* The high-time is either the highest last time among the linked
   * pads, or if all pads are not-linked, it's the lowest nex time of
   * not-linked pad */
  GList *tmp;
  GstClockTimeDiff highest = GST_CLOCK_STIME_NONE;
  GstClockTimeDiff lowest = GST_CLOCK_STIME_NONE;
  GstClockTimeDiff group_high = GST_CLOCK_STIME_NONE;
  GstClockTimeDiff group_low = GST_CLOCK_STIME_NONE;
  GstClockTimeDiff res;
  /* Number of streams which belong to groupid */
  guint group_count = 0;

  if (!mq->sync_by_running_time)
    /* return GST_CLOCK_STIME_NONE; */
    return;

  for (tmp = mq->queues; tmp; tmp = tmp->next) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;

    GST_LOG_OBJECT (mq,
        "inspecting sq:%d (group:%d) , next_time:%" GST_STIME_FORMAT
        ", last_time:%" GST_STIME_FORMAT ", srcresult:%s", sq->id, sq->groupid,
        GST_STIME_ARGS (sq->next_time), GST_STIME_ARGS (sq->last_time),
        gst_flow_get_name (sq->srcresult));

    if (sq->groupid == groupid)
      group_count++;

    if (sq->srcresult == GST_FLOW_NOT_LINKED) {
      /* No need to consider queues which are not waiting */
      if (!GST_CLOCK_STIME_IS_VALID (sq->next_time)) {
        GST_LOG_OBJECT (mq, "sq:%d is not waiting - ignoring", sq->id);
        continue;
      }

      if (lowest == GST_CLOCK_STIME_NONE || sq->next_time < lowest)
        lowest = sq->next_time;
      if (sq->groupid == groupid && (group_low == GST_CLOCK_STIME_NONE
              || sq->next_time < group_low))
        group_low = sq->next_time;
    } else if (!GST_PAD_IS_EOS (sq->srcpad) && sq->srcresult != GST_FLOW_EOS) {
      /* If we don't have a global high time, or the global high time
       * is lower than this single queue's last outputted time, store
       * the queue's one, unless the singlequeue output is at EOS. */
      if (highest == GST_CLOCK_STIME_NONE
          || (sq->last_time != GST_CLOCK_STIME_NONE && sq->last_time > highest))
        highest = sq->last_time;
      if (sq->groupid == groupid && (group_high == GST_CLOCK_STIME_NONE
              || (sq->last_time != GST_CLOCK_STIME_NONE
                  && sq->last_time > group_high)))
        group_high = sq->last_time;
    }
    GST_LOG_OBJECT (mq,
        "highest now %" GST_STIME_FORMAT " lowest %" GST_STIME_FORMAT,
        GST_STIME_ARGS (highest), GST_STIME_ARGS (lowest));
    if (sq->groupid == groupid)
      GST_LOG_OBJECT (mq,
          "grouphigh %" GST_STIME_FORMAT " grouplow %" GST_STIME_FORMAT,
          GST_STIME_ARGS (group_high), GST_STIME_ARGS (group_low));
  }

  if (highest == GST_CLOCK_STIME_NONE)
    mq->high_time = lowest;
  else
    mq->high_time = highest;

  /* If there's only one stream of a given type, use the global high */
  if (group_count < 2)
    res = GST_CLOCK_STIME_NONE;
  else if (group_high == GST_CLOCK_STIME_NONE)
    res = group_low;
  else
    res = group_high;

  GST_LOG_OBJECT (mq, "group count %d for groupid %u", group_count, groupid);
  GST_LOG_OBJECT (mq,
      "MQ High time is now : %" GST_STIME_FORMAT ", group %d high time %"
      GST_STIME_FORMAT ", lowest non-linked %" GST_STIME_FORMAT,
      GST_STIME_ARGS (mq->high_time), groupid, GST_STIME_ARGS (mq->high_time),
      GST_STIME_ARGS (lowest));

  for (tmp = mq->queues; tmp; tmp = tmp->next) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;
    if (groupid == sq->groupid)
      sq->group_high_time = res;
  }
}

#define IS_FILLED(q, format, value) (((q)->max_size.format) != 0 && \
     ((q)->max_size.format) <= (value))

/*
 * GstSingleQueue functions
 */
static void
single_queue_overrun_cb (GstDataQueue * dq, GstSingleQueue * sq)
{
  GstMultiQueue *mq = sq->mqueue;
  GList *tmp;
  GstDataQueueSize size;
  gboolean filled = TRUE;
  gboolean empty_found = FALSE;

  gst_data_queue_get_level (sq->queue, &size);

  GST_LOG_OBJECT (mq,
      "Single Queue %d: EOS %d, visible %u/%u, bytes %u/%u, time %"
      G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT, sq->id, sq->is_eos, size.visible,
      sq->max_size.visible, size.bytes, sq->max_size.bytes, sq->cur_time,
      sq->max_size.time);

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  /* check if we reached the hard time/bytes limits;
     time limit is only taken into account for non-sparse streams */
  if (sq->is_eos || IS_FILLED (sq, bytes, size.bytes) ||
      (!sq->is_sparse && IS_FILLED (sq, time, sq->cur_time))) {
    goto done;
  }

  /* Search for empty queues */
  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *oq = (GstSingleQueue *) tmp->data;

    if (oq == sq)
      continue;

    if (oq->srcresult == GST_FLOW_NOT_LINKED) {
      GST_LOG_OBJECT (mq, "Queue %d is not-linked", oq->id);
      continue;
    }

    GST_LOG_OBJECT (mq, "Checking Queue %d", oq->id);
    if (gst_data_queue_is_empty (oq->queue) && !oq->is_sparse) {
      GST_LOG_OBJECT (mq, "Queue %d is empty", oq->id);
      empty_found = TRUE;
      break;
    }
  }

  /* if hard limits are not reached then we allow one more buffer in the full
   * queue, but only if any of the other singelqueues are empty */
  if (empty_found) {
    if (IS_FILLED (sq, visible, size.visible)) {
      sq->max_size.visible = size.visible + 1;
      GST_DEBUG_OBJECT (mq,
          "Bumping single queue %d max visible to %d",
          sq->id, sq->max_size.visible);
      filled = FALSE;
    }
  }

done:
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  /* Overrun is always forwarded, since this is blocking the upstream element */
  if (filled) {
    GST_DEBUG_OBJECT (mq, "Queue %d is filled, signalling overrun", sq->id);
    g_signal_emit (mq, gst_multi_queue_signals[SIGNAL_OVERRUN], 0);
  }
}

static void
single_queue_underrun_cb (GstDataQueue * dq, GstSingleQueue * sq)
{
  gboolean empty = TRUE;
  GstMultiQueue *mq = sq->mqueue;
  GList *tmp;

  if (sq->srcresult == GST_FLOW_NOT_LINKED) {
    GST_LOG_OBJECT (mq, "Single Queue %d is empty but not-linked", sq->id);
    return;
  } else {
    GST_LOG_OBJECT (mq,
        "Single Queue %d is empty, Checking other single queues", sq->id);
  }

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *oq = (GstSingleQueue *) tmp->data;

    if (gst_data_queue_is_full (oq->queue)) {
      GstDataQueueSize size;

      gst_data_queue_get_level (oq->queue, &size);
      if (IS_FILLED (oq, visible, size.visible)) {
        oq->max_size.visible = size.visible + 1;
        GST_DEBUG_OBJECT (mq,
            "queue %d is filled, bumping its max visible to %d", oq->id,
            oq->max_size.visible);
        gst_data_queue_limits_changed (oq->queue);
      }
    }
    if (!gst_data_queue_is_empty (oq->queue) || oq->is_sparse)
      empty = FALSE;
  }
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  if (empty) {
    GST_DEBUG_OBJECT (mq, "All queues are empty, signalling it");
    g_signal_emit (mq, gst_multi_queue_signals[SIGNAL_UNDERRUN], 0);
  }
}

static gboolean
single_queue_check_full (GstDataQueue * dataq, guint visible, guint bytes,
    guint64 time, GstSingleQueue * sq)
{
  gboolean res;
  GstMultiQueue *mq = sq->mqueue;

  GST_DEBUG_OBJECT (mq,
      "queue %d: visible %u/%u, bytes %u/%u, time %" G_GUINT64_FORMAT "/%"
      G_GUINT64_FORMAT, sq->id, visible, sq->max_size.visible, bytes,
      sq->max_size.bytes, sq->cur_time, sq->max_size.time);

  /* we are always filled on EOS */
  if (sq->is_eos)
    return TRUE;

  /* we never go past the max visible items unless we are in buffering mode */
  if (!mq->use_buffering && IS_FILLED (sq, visible, visible))
    return TRUE;

  /* check time or bytes */
  res = IS_FILLED (sq, bytes, bytes);
  /* We only care about limits in time if we're not a sparse stream or
   * we're not syncing by running time */
  if (!sq->is_sparse || !mq->sync_by_running_time) {
    /* If unlinked, take into account the extra unlinked cache time */
    if (mq->sync_by_running_time && sq->srcresult == GST_FLOW_NOT_LINKED) {
      if (sq->cur_time > mq->unlinked_cache_time)
        res |= IS_FILLED (sq, time, sq->cur_time - mq->unlinked_cache_time);
      else
        res = FALSE;
    } else
      res |= IS_FILLED (sq, time, sq->cur_time);
  }

  return res;
}

static void
gst_single_queue_flush_queue (GstSingleQueue * sq, gboolean full)
{
  GstDataQueueItem *sitem;
  GstMultiQueueItem *mitem;
  gboolean was_flushing = FALSE;

  while (!gst_data_queue_is_empty (sq->queue)) {
    GstMiniObject *data;

    /* FIXME: If this fails here although the queue is not empty,
     * we're flushing... but we want to rescue all sticky
     * events nonetheless.
     */
    if (!gst_data_queue_pop (sq->queue, &sitem)) {
      was_flushing = TRUE;
      gst_data_queue_set_flushing (sq->queue, FALSE);
      continue;
    }

    mitem = (GstMultiQueueItem *) sitem;

    data = sitem->object;

    if (!full && !mitem->is_query && GST_IS_EVENT (data)
        && GST_EVENT_IS_STICKY (data)
        && GST_EVENT_TYPE (data) != GST_EVENT_SEGMENT
        && GST_EVENT_TYPE (data) != GST_EVENT_EOS) {
      gst_pad_store_sticky_event (sq->srcpad, GST_EVENT_CAST (data));
    }

    sitem->destroy (sitem);
  }

  gst_data_queue_flush (sq->queue);
  if (was_flushing)
    gst_data_queue_set_flushing (sq->queue, TRUE);

  GST_MULTI_QUEUE_MUTEX_LOCK (sq->mqueue);
  update_buffering (sq->mqueue, sq);
  GST_MULTI_QUEUE_MUTEX_UNLOCK (sq->mqueue);
  gst_multi_queue_post_buffering (sq->mqueue);
}

static void
gst_single_queue_free (GstSingleQueue * sq)
{
  /* DRAIN QUEUE */
  gst_data_queue_flush (sq->queue);
  g_object_unref (sq->queue);
  g_cond_clear (&sq->turn);
  g_cond_clear (&sq->query_handled);
  g_free (sq);
}

static GstSingleQueue *
gst_single_queue_new (GstMultiQueue * mqueue, guint id)
{
  GstSingleQueue *sq;
  GstMultiQueuePad *mqpad;
  GstPadTemplate *templ;
  gchar *name;
  GList *tmp;
  guint temp_id = (id == -1) ? 0 : id;

  GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);

  /* Find an unused queue ID, if possible the passed one */
  for (tmp = mqueue->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *sq2 = (GstSingleQueue *) tmp->data;
    /* This works because the IDs are sorted in ascending order */
    if (sq2->id == temp_id) {
      /* If this ID was requested by the caller return NULL,
       * otherwise just get us the next one */
      if (id == -1) {
        temp_id = sq2->id + 1;
      } else {
        GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);
        return NULL;
      }
    } else if (sq2->id > temp_id) {
      break;
    }
  }

  sq = g_new0 (GstSingleQueue, 1);
  mqueue->nbqueues++;
  sq->id = temp_id;
  sq->groupid = DEFAULT_PAD_GROUP_ID;
  sq->group_high_time = GST_CLOCK_STIME_NONE;

  mqueue->queues = g_list_insert_before (mqueue->queues, tmp, sq);
  mqueue->queues_cookie++;

  /* copy over max_size and extra_size so we don't need to take the lock
   * any longer when checking if the queue is full. */
  sq->max_size.visible = mqueue->max_size.visible;
  sq->max_size.bytes = mqueue->max_size.bytes;
  sq->max_size.time = mqueue->max_size.time;

  sq->extra_size.visible = mqueue->extra_size.visible;
  sq->extra_size.bytes = mqueue->extra_size.bytes;
  sq->extra_size.time = mqueue->extra_size.time;

  GST_DEBUG_OBJECT (mqueue, "Creating GstSingleQueue id:%d", sq->id);

  sq->mqueue = mqueue;
  sq->srcresult = GST_FLOW_FLUSHING;
  sq->pushed = FALSE;
  sq->queue = gst_data_queue_new ((GstDataQueueCheckFullFunction)
      single_queue_check_full,
      (GstDataQueueFullCallback) single_queue_overrun_cb,
      (GstDataQueueEmptyCallback) single_queue_underrun_cb, sq);
  sq->is_eos = FALSE;
  sq->is_sparse = FALSE;
  sq->flushing = FALSE;
  sq->active = FALSE;
  gst_segment_init (&sq->sink_segment, GST_FORMAT_TIME);
  gst_segment_init (&sq->src_segment, GST_FORMAT_TIME);

  sq->nextid = 0;
  sq->oldid = 0;
  sq->next_time = GST_CLOCK_STIME_NONE;
  sq->last_time = GST_CLOCK_STIME_NONE;
  g_cond_init (&sq->turn);
  g_cond_init (&sq->query_handled);

  sq->sinktime = GST_CLOCK_STIME_NONE;
  sq->srctime = GST_CLOCK_STIME_NONE;
  sq->sink_tainted = TRUE;
  sq->src_tainted = TRUE;

  name = g_strdup_printf ("sink_%u", sq->id);
  templ = gst_static_pad_template_get (&sinktemplate);
  sq->sinkpad = g_object_new (GST_TYPE_MULTIQUEUE_PAD, "name", name,
      "direction", templ->direction, "template", templ, NULL);
  gst_object_unref (templ);
  g_free (name);

  mqpad = (GstMultiQueuePad *) sq->sinkpad;
  mqpad->sq = sq;

  gst_pad_set_chain_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_chain));
  gst_pad_set_activatemode_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_sink_activate_mode));
  gst_pad_set_event_full_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_sink_event));
  gst_pad_set_query_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_sink_query));
  gst_pad_set_iterate_internal_links_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_iterate_internal_links));
  GST_OBJECT_FLAG_SET (sq->sinkpad, GST_PAD_FLAG_PROXY_CAPS);

  name = g_strdup_printf ("src_%u", sq->id);
  sq->srcpad = gst_pad_new_from_static_template (&srctemplate, name);
  g_free (name);

  gst_pad_set_activatemode_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_activate_mode));
  gst_pad_set_event_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_event));
  gst_pad_set_query_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_query));
  gst_pad_set_iterate_internal_links_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_iterate_internal_links));
  GST_OBJECT_FLAG_SET (sq->srcpad, GST_PAD_FLAG_PROXY_CAPS);

  gst_pad_set_element_private (sq->sinkpad, (gpointer) sq);
  gst_pad_set_element_private (sq->srcpad, (gpointer) sq);

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);

  /* only activate the pads when we are not in the NULL state
   * and add the pad under the state_lock to prevend state changes
   * between activating and adding */
  g_rec_mutex_lock (GST_STATE_GET_LOCK (mqueue));
  if (GST_STATE_TARGET (mqueue) != GST_STATE_NULL) {
    gst_pad_set_active (sq->srcpad, TRUE);
    gst_pad_set_active (sq->sinkpad, TRUE);
  }
  gst_element_add_pad (GST_ELEMENT (mqueue), sq->srcpad);
  gst_element_add_pad (GST_ELEMENT (mqueue), sq->sinkpad);
  g_rec_mutex_unlock (GST_STATE_GET_LOCK (mqueue));

  GST_DEBUG_OBJECT (mqueue, "GstSingleQueue [%d] created and pads added",
      sq->id);

  return sq;
}
