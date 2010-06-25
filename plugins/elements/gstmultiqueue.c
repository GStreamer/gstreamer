/* GStreamer
 * Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
 * Copyright (C) 2007 Jan Schmidt <jan@fluendo.com>
 * Copyright (C) 2007 Wim Taymans <wim@fluendo.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-multiqueue
 * @see_also: #GstQueue
 *
 * <refsect2>
 * <para>
 * Multiqueue is similar to a normal #GstQueue with the following additional
 * features:
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Multiple streamhandling</title>
 *   <listitem><para>
 *     The element handles queueing data on more than one stream at once. To
 *     achieve such a feature it has request sink pads (sink&percnt;d) and
 *     'sometimes' src pads (src&percnt;d).
 *   </para><para>
 *     When requesting a given sinkpad with gst_element_get_request_pad(),
 *     the associated srcpad for that stream will be created.
 *     Example: requesting sink1 will generate src1.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Non-starvation on multiple streams</title>
 *   <listitem><para>
 *     If more than one stream is used with the element, the streams' queues
 *     will be dynamically grown (up to a limit), in order to ensure that no
 *     stream is risking data starvation. This guarantees that at any given
 *     time there are at least N bytes queued and available for each individual
 *     stream.
 *   </para><para>
 *     If an EOS event comes through a srcpad, the associated queue will be
 *     considered as 'not-empty' in the queue-size-growing algorithm.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Non-linked srcpads graceful handling</title>
 *   <listitem><para>
 *     In order to better support dynamic switching between streams, the multiqueue
 *     (unlike the current GStreamer queue) continues to push buffers on non-linked
 *     pads rather than shutting down.
 *   </para><para>
 *     In addition, to prevent a non-linked stream from very quickly consuming all
 *     available buffers and thus 'racing ahead' of the other streams, the element
 *     must ensure that buffers and inlined events for a non-linked stream are pushed
 *     in the same order as they were received, relative to the other streams
 *     controlled by the element. This means that a buffer cannot be pushed to a
 *     non-linked pad any sooner than buffers in any other stream which were received
 *     before it.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 * </para>
 * <para>
 *   Data is queued until one of the limits specified by the
 *   #GstMultiQueue:max-size-buffers, #GstMultiQueue:max-size-bytes and/or
 *   #GstMultiQueue:max-size-time properties has been reached. Any attempt to push
 *   more buffers into the queue will block the pushing thread until more space
 *   becomes available. #GstMultiQueue:extra-size-buffers,
 * </para>
 * <para>
 *   #GstMultiQueue:extra-size-bytes and #GstMultiQueue:extra-size-time are
 *   currently unused.
 * </para>
 * <para>
 *   The default queue size limits are 5 buffers, 10MB of data, or
 *   two second worth of data, whichever is reached first. Note that the number
 *   of buffers will dynamically grow depending on the fill level of 
 *   other queues.
 * </para>
 * <para>
 *   The #GstMultiQueue::underrun signal is emitted when all of the queues
 *   are empty. The #GstMultiQueue::overrun signal is emitted when one of the
 *   queues is filled.
 *   Both signals are emitted from the context of the streaming thread.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-25 (0.10.17)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstmultiqueue.h"

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

  GstMultiQueue *mqueue;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* flowreturn of previous srcpad push */
  GstFlowReturn srcresult;

  /* segments */
  GstSegment sink_segment;
  GstSegment src_segment;

  /* position of src/sink */
  GstClockTime sinktime, srctime;
  /* TRUE if either position needs to be recalculated */
  gboolean sink_tainted, src_tainted;

  /* queue of data */
  GstDataQueue *queue;
  GstDataQueueSize max_size, extra_size;
  GstClockTime cur_time;
  gboolean is_eos;

  /* Protected by global lock */
  guint32 nextid;               /* ID of the next object waiting to be pushed */
  guint32 oldid;                /* ID of the last object pushed (last in a series) */
  GCond *turn;                  /* SingleQueue turn waiting conditional */
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
};

static GstSingleQueue *gst_single_queue_new (GstMultiQueue * mqueue);
static void gst_single_queue_free (GstSingleQueue * squeue);

static void wake_up_next_non_linked (GstMultiQueue * mq);
static void compute_high_id (GstMultiQueue * mq);
static void single_queue_overrun_cb (GstDataQueue * dq, GstSingleQueue * sq);
static void single_queue_underrun_cb (GstDataQueue * dq, GstSingleQueue * sq);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src%d",
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
#define DEFAULT_LOW_PERCENT   10
#define DEFAULT_HIGH_PERCENT  99

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
  PROP_LAST
};

#define GST_MULTI_QUEUE_MUTEX_LOCK(q) G_STMT_START {                          \
  g_mutex_lock (q->qlock);                                              \
} G_STMT_END

#define GST_MULTI_QUEUE_MUTEX_UNLOCK(q) G_STMT_START {                        \
  g_mutex_unlock (q->qlock);                                            \
} G_STMT_END

static void gst_multi_queue_finalize (GObject * object);
static void gst_multi_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_multi_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_multi_queue_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * name);
static void gst_multi_queue_release_pad (GstElement * element, GstPad * pad);

static void gst_multi_queue_loop (GstPad * pad);

#define _do_init(bla) \
  GST_DEBUG_CATEGORY_INIT (multi_queue_debug, "multiqueue", 0, "multiqueue element");

GST_BOILERPLATE_FULL (GstMultiQueue, gst_multi_queue, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static guint gst_multi_queue_signals[LAST_SIGNAL] = { 0 };

static void
gst_multi_queue_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "MultiQueue",
      "Generic", "Multiple data queue", "Edward Hervey <edward@fluendo.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
}

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
   * @multiqueue: the multqueue instance
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
   * GstMultiQueue:use-buffering
   * 
   * Enable the buffering option in multiqueue so that BUFFERING messages are
   * emited based on low-/high-percent thresholds.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_USE_BUFFERING,
      g_param_spec_boolean ("use-buffering", "Use buffering",
          "Emit GST_MESSAGE_BUFFERING based on low-/high-percent thresholds",
          DEFAULT_USE_BUFFERING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiQueue:low-percent
   * 
   * Low threshold percent for buffering to start.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_LOW_PERCENT,
      g_param_spec_int ("low-percent", "Low percent",
          "Low threshold for buffering to start", 0, 100,
          DEFAULT_LOW_PERCENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiQueue:high-percent
   * 
   * High threshold percent for buffering to finish.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class, PROP_HIGH_PERCENT,
      g_param_spec_int ("high-percent", "High percent",
          "High threshold for buffering to finish", 0, 100,
          DEFAULT_HIGH_PERCENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gobject_class->finalize = gst_multi_queue_finalize;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_multi_queue_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_multi_queue_release_pad);
}

static void
gst_multi_queue_init (GstMultiQueue * mqueue, GstMultiQueueClass * klass)
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
  mqueue->low_percent = DEFAULT_LOW_PERCENT;
  mqueue->high_percent = DEFAULT_HIGH_PERCENT;

  mqueue->counter = 1;
  mqueue->highid = -1;
  mqueue->nextnotlinked = -1;

  mqueue->qlock = g_mutex_new ();
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
  g_mutex_free (mqueue->qlock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#define SET_CHILD_PROPERTY(mq,format) G_STMT_START {	        \
    GList * tmp = mq->queues;					\
    while (tmp) {						\
      GstSingleQueue *q = (GstSingleQueue*)tmp->data;		\
      q->max_size.format = mq->max_size.format;                 \
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
      break;
    case PROP_MAX_SIZE_BUFFERS:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      mq->max_size.visible = g_value_get_uint (value);
      SET_CHILD_PROPERTY (mq, visible);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      break;
    case PROP_MAX_SIZE_TIME:
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      mq->max_size.time = g_value_get_uint64 (value);
      SET_CHILD_PROPERTY (mq, time);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
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
      break;
    case PROP_LOW_PERCENT:
      mq->low_percent = g_value_get_int (value);
      break;
    case PROP_HIGH_PERCENT:
      mq->high_percent = g_value_get_int (value);
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
      g_value_set_int (value, mq->low_percent);
      break;
    case PROP_HIGH_PERCENT:
      g_value_set_int (value, mq->high_percent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
}

static GstIterator *
gst_multi_queue_iterate_internal_links (GstPad * pad)
{
  GstIterator *it = NULL;
  GstPad *opad;
  GstSingleQueue *squeue;
  GstMultiQueue *mq = GST_MULTI_QUEUE (gst_pad_get_parent (pad));

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

  it = gst_iterator_new_single (GST_TYPE_PAD, opad,
      (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);

  gst_object_unref (opad);

out:
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  gst_object_unref (mq);

  return it;
}


/*
 * GstElement methods
 */

static GstPad *
gst_multi_queue_request_new_pad (GstElement * element, GstPadTemplate * temp,
    const gchar * name)
{
  GstMultiQueue *mqueue = GST_MULTI_QUEUE (element);
  GstSingleQueue *squeue;

  GST_LOG_OBJECT (element, "name : %s", GST_STR_NULL (name));

  /* Create a new single queue, add the sink and source pad and return the sink pad */
  squeue = gst_single_queue_new (mqueue);

  GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);
  mqueue->queues = g_list_append (mqueue->queues, squeue);
  mqueue->queues_cookie++;
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);

  GST_DEBUG_OBJECT (mqueue, "Returning pad %s:%s",
      GST_DEBUG_PAD_NAME (squeue->sinkpad));

  return squeue->sinkpad;
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

static gboolean
gst_single_queue_flush (GstMultiQueue * mq, GstSingleQueue * sq, gboolean flush)
{
  gboolean result;

  GST_DEBUG_OBJECT (mq, "flush %s queue %d", (flush ? "start" : "stop"),
      sq->id);

  if (flush) {
    sq->srcresult = GST_FLOW_WRONG_STATE;
    gst_data_queue_set_flushing (sq->queue, TRUE);

    /* wake up non-linked task */
    GST_LOG_OBJECT (mq, "SingleQueue %d : waking up eventually waiting task",
        sq->id);
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    g_cond_signal (sq->turn);
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

    GST_LOG_OBJECT (mq, "SingleQueue %d : pausing task", sq->id);
    result = gst_pad_pause_task (sq->srcpad);
    sq->sink_tainted = sq->src_tainted = TRUE;
  } else {
    gst_data_queue_flush (sq->queue);
    gst_segment_init (&sq->sink_segment, GST_FORMAT_TIME);
    gst_segment_init (&sq->src_segment, GST_FORMAT_TIME);
    /* All pads start off not-linked for a smooth kick-off */
    sq->srcresult = GST_FLOW_OK;
    sq->cur_time = 0;
    sq->max_size.visible = mq->max_size.visible;
    sq->is_eos = FALSE;
    sq->nextid = 0;
    sq->oldid = 0;
    gst_data_queue_set_flushing (sq->queue, FALSE);

    GST_LOG_OBJECT (mq, "SingleQueue %d : starting task", sq->id);
    result =
        gst_pad_start_task (sq->srcpad, (GstTaskFunction) gst_multi_queue_loop,
        sq->srcpad);
  }
  return result;
}

static void
update_buffering (GstMultiQueue * mq, GstSingleQueue * sq)
{
  GstDataQueueSize size;
  gint percent, tmp;
  gboolean post = FALSE;

  /* nothing to dowhen we are not in buffering mode */
  if (!mq->use_buffering)
    return;

  gst_data_queue_get_level (sq->queue, &size);

  GST_DEBUG_OBJECT (mq,
      "queue %d: visible %u/%u, bytes %u/%u, time %" G_GUINT64_FORMAT "/%"
      G_GUINT64_FORMAT, sq->id, size.visible, sq->max_size.visible,
      size.bytes, sq->max_size.bytes, sq->cur_time, sq->max_size.time);

  /* get bytes and time percentages and take the max */
  if (sq->is_eos) {
    percent = 100;
  } else {
    percent = 0;
    if (sq->max_size.time > 0) {
      tmp = (sq->cur_time * 100) / sq->max_size.time;
      percent = MAX (percent, tmp);
    }
    if (sq->max_size.bytes > 0) {
      tmp = (size.bytes * 100) / sq->max_size.bytes;
      percent = MAX (percent, tmp);
    }
  }

  if (mq->buffering) {
    post = TRUE;
    if (percent >= mq->high_percent) {
      mq->buffering = FALSE;
    }
    /* make sure it increases */
    percent = MAX (mq->percent, percent);

    if (percent == mq->percent)
      /* don't post if nothing changed */
      post = FALSE;
    else
      /* else keep last value we posted */
      mq->percent = percent;
  } else {
    if (percent < mq->low_percent) {
      mq->buffering = TRUE;
      mq->percent = percent;
      post = TRUE;
    }
  }
  if (post) {
    GstMessage *message;

    /* scale to high percent so that it becomes the 100% mark */
    percent = percent * 100 / mq->high_percent;
    /* clip */
    if (percent > 100)
      percent = 100;

    GST_DEBUG_OBJECT (mq, "buffering %d percent", percent);
    message = gst_message_new_buffering (GST_OBJECT_CAST (mq), percent);

    gst_element_post_message (GST_ELEMENT_CAST (mq), message);
  } else {
    GST_DEBUG_OBJECT (mq, "filled %d percent", percent);
  }
}

/* calculate the diff between running time on the sink and src of the queue.
 * This is the total amount of time in the queue. 
 * WITH LOCK TAKEN */
static void
update_time_level (GstMultiQueue * mq, GstSingleQueue * sq)
{
  gint64 sink_time, src_time;

  if (sq->sink_tainted) {
    sink_time = sq->sinktime =
        gst_segment_to_running_time (&sq->sink_segment, GST_FORMAT_TIME,
        sq->sink_segment.last_stop);

    if (G_UNLIKELY (sink_time != GST_CLOCK_TIME_NONE))
      /* if we have a time, we become untainted and use the time */
      sq->sink_tainted = FALSE;
  } else
    sink_time = sq->sinktime;

  if (sq->src_tainted) {
    src_time = sq->srctime =
        gst_segment_to_running_time (&sq->src_segment, GST_FORMAT_TIME,
        sq->src_segment.last_stop);
    /* if we have a time, we become untainted and use the time */
    if (G_UNLIKELY (src_time != GST_CLOCK_TIME_NONE))
      sq->src_tainted = FALSE;
  } else
    src_time = sq->srctime;

  GST_DEBUG_OBJECT (mq,
      "queue %d, sink %" GST_TIME_FORMAT ", src %" GST_TIME_FORMAT, sq->id,
      GST_TIME_ARGS (sink_time), GST_TIME_ARGS (src_time));

  /* This allows for streams with out of order timestamping - sometimes the
   * emerging timestamp is later than the arriving one(s) */
  if (G_LIKELY (sink_time != -1 && src_time != -1 && sink_time > src_time))
    sq->cur_time = sink_time - src_time;
  else
    sq->cur_time = 0;

  /* updating the time level can change the buffering state */
  update_buffering (mq, sq);

  return;
}

/* take a NEWSEGMENT event and apply the values to segment, updating the time
 * level of queue. */
static void
apply_segment (GstMultiQueue * mq, GstSingleQueue * sq, GstEvent * event,
    GstSegment * segment)
{
  gboolean update;
  GstFormat format;
  gdouble rate, arate;
  gint64 start, stop, time;

  gst_event_parse_new_segment_full (event, &update, &rate, &arate,
      &format, &start, &stop, &time);

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

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);

  gst_segment_set_newsegment_full (segment, update,
      rate, arate, format, start, stop, time);

  if (segment == &sq->sink_segment)
    sq->sink_tainted = TRUE;
  else
    sq->src_tainted = TRUE;

  GST_DEBUG_OBJECT (mq,
      "queue %d, configured NEWSEGMENT %" GST_SEGMENT_FORMAT, sq->id, segment);

  /* segment can update the time level of the queue */
  update_time_level (mq, sq);

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
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
    timestamp = segment->last_stop;

  /* add duration */
  if (duration != GST_CLOCK_TIME_NONE)
    timestamp += duration;

  GST_DEBUG_OBJECT (mq, "queue %d, last_stop updated to %" GST_TIME_FORMAT,
      sq->id, GST_TIME_ARGS (timestamp));

  gst_segment_set_last_stop (segment, GST_FORMAT_TIME, timestamp);

  if (segment == &sq->sink_segment)
    sq->sink_tainted = TRUE;
  else
    sq->src_tainted = TRUE;

  /* calc diff with other end */
  update_time_level (mq, sq);
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
}

static GstFlowReturn
gst_single_queue_push_one (GstMultiQueue * mq, GstSingleQueue * sq,
    GstMiniObject * object)
{
  GstFlowReturn result = GST_FLOW_OK;

  if (GST_IS_BUFFER (object)) {
    GstBuffer *buffer;
    GstClockTime timestamp, duration;
    GstCaps *caps;

    buffer = GST_BUFFER_CAST (object);
    timestamp = GST_BUFFER_TIMESTAMP (buffer);
    duration = GST_BUFFER_DURATION (buffer);
    caps = GST_BUFFER_CAPS (buffer);

    apply_buffer (mq, sq, timestamp, duration, &sq->src_segment);

    /* Applying the buffer may have made the queue non-full again, unblock it if needed */
    gst_data_queue_limits_changed (sq->queue);

    GST_DEBUG_OBJECT (mq,
        "SingleQueue %d : Pushing buffer %p with ts %" GST_TIME_FORMAT,
        sq->id, buffer, GST_TIME_ARGS (timestamp));

    /* Set caps on pad before pushing, this avoids core calling the acceptcaps
     * function on the srcpad, which will call acceptcaps upstream, which might
     * not accept these caps (anymore). */
    if (caps && caps != GST_PAD_CAPS (sq->srcpad))
      gst_pad_set_caps (sq->srcpad, caps);

    result = gst_pad_push (sq->srcpad, buffer);
  } else if (GST_IS_EVENT (object)) {
    GstEvent *event;

    event = GST_EVENT_CAST (object);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        result = GST_FLOW_UNEXPECTED;
        break;
      case GST_EVENT_NEWSEGMENT:
        apply_segment (mq, sq, event, &sq->src_segment);
        /* Applying the segment may have made the queue non-full again, unblock it if needed */
        gst_data_queue_limits_changed (sq->queue);
        break;
      default:
        break;
    }

    GST_DEBUG_OBJECT (mq,
        "SingleQueue %d : Pushing event %p of type %s",
        sq->id, event, GST_EVENT_TYPE_NAME (event));

    gst_pad_push_event (sq->srcpad, event);
  } else {
    g_warning ("Unexpected object in singlequeue %d (refcounting problem?)",
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
  if (item->object)
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

  item->size = GST_BUFFER_SIZE (object);
  item->duration = GST_BUFFER_DURATION (object);
  if (item->duration == GST_CLOCK_TIME_NONE)
    item->duration = 0;
  item->visible = TRUE;
  return item;
}

static GstMultiQueueItem *
gst_multi_queue_event_item_new (GstMiniObject * object, guint32 curid)
{
  GstMultiQueueItem *item;

  item = g_slice_new (GstMultiQueueItem);
  item->object = object;
  item->destroy = (GDestroyNotify) gst_multi_queue_item_destroy;
  item->posid = curid;

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
  GstMiniObject *object;
  guint32 newid;
  guint32 oldid = G_MAXUINT32;
  GstFlowReturn result;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = sq->mqueue;

  do {
    GST_DEBUG_OBJECT (mq, "SingleQueue %d : trying to pop an object", sq->id);

    /* Get something from the queue, blocking until that happens, or we get
     * flushed */
    if (!(gst_data_queue_pop (sq->queue, &sitem)))
      goto out_flushing;

    item = (GstMultiQueueItem *) sitem;
    newid = item->posid;

    /* steal the object and destroy the item */
    object = gst_multi_queue_item_steal_object (item);
    gst_multi_queue_item_destroy (item);

    GST_LOG_OBJECT (mq, "SingleQueue %d : newid:%d , oldid:%d",
        sq->id, newid, oldid);

    /* If we're not-linked, we do some extra work because we might need to
     * wait before pushing. If we're linked but there's a gap in the IDs,
     * or it's the first loop, or we just passed the previous highid, 
     * we might need to wake some sleeping pad up, so there's extra work 
     * there too */
    if (sq->srcresult == GST_FLOW_NOT_LINKED ||
        (oldid == G_MAXUINT32) || (newid != (oldid + 1)) ||
        oldid > mq->highid) {
      GST_LOG_OBJECT (mq, "CHECKING sq->srcresult: %s",
          gst_flow_get_name (sq->srcresult));

      GST_MULTI_QUEUE_MUTEX_LOCK (mq);

      /* Update the nextid so other threads know when to wake us up */
      sq->nextid = newid;

      /* Update the oldid (the last ID we output) for highid tracking */
      if (oldid != G_MAXUINT32)
        sq->oldid = oldid;

      if (sq->srcresult == GST_FLOW_NOT_LINKED) {
        /* Go to sleep until it's time to push this buffer */

        /* Recompute the highid */
        compute_high_id (mq);
        while (newid > mq->highid && sq->srcresult == GST_FLOW_NOT_LINKED) {
          GST_DEBUG_OBJECT (mq, "queue %d sleeping for not-linked wakeup with "
              "newid %u and highid %u", sq->id, newid, mq->highid);


          /* Wake up all non-linked pads before we sleep */
          wake_up_next_non_linked (mq);

          mq->numwaiting++;
          g_cond_wait (sq->turn, mq->qlock);
          mq->numwaiting--;

          GST_DEBUG_OBJECT (mq, "queue %d woken from sleeping for not-linked "
              "wakeup with newid %u and highid %u", sq->id, newid, mq->highid);
        }

        /* Re-compute the high_id in case someone else pushed */
        compute_high_id (mq);
      } else {
        compute_high_id (mq);
        /* Wake up all non-linked pads */
        wake_up_next_non_linked (mq);
      }
      /* We're done waiting, we can clear the nextid */
      sq->nextid = 0;

      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
    }

    GST_LOG_OBJECT (mq, "BEFORE PUSHING sq->srcresult: %s",
        gst_flow_get_name (sq->srcresult));

    /* Try to push out the new object */
    result = gst_single_queue_push_one (mq, sq, object);
    sq->srcresult = result;

    if (result != GST_FLOW_OK && result != GST_FLOW_NOT_LINKED
        && result != GST_FLOW_UNEXPECTED)
      goto out_flushing;

    GST_LOG_OBJECT (mq, "AFTER PUSHING sq->srcresult: %s",
        gst_flow_get_name (sq->srcresult));

    oldid = newid;
  }
  while (TRUE);

out_flushing:
  {
    /* Need to make sure wake up any sleeping pads when we exit */
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    compute_high_id (mq);
    wake_up_next_non_linked (mq);
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

    /* upstream needs to see fatal result ASAP to shut things down,
     * but might be stuck in one of our other full queues;
     * so empty this one and trigger dynamic queue growth */
    if (GST_FLOW_IS_FATAL (sq->srcresult)) {
      gst_data_queue_flush (sq->queue);
      single_queue_underrun_cb (sq->queue, sq);
    }
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
 * _ we don't have leak behavioures,
 * _ we push with a unique id (curid)
 */
static GstFlowReturn
gst_multi_queue_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSingleQueue *sq;
  GstMultiQueue *mq;
  GstMultiQueueItem *item;
  guint32 curid;
  GstClockTime timestamp, duration;

  sq = gst_pad_get_element_private (pad);
  mq = sq->mqueue;

  /* Get a unique incrementing id */
  curid = mq->counter++;

  GST_LOG_OBJECT (mq, "SingleQueue %d : about to enqueue buffer %p with id %d",
      sq->id, buffer, curid);

  item = gst_multi_queue_buffer_item_new (GST_MINI_OBJECT_CAST (buffer), curid);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

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
}

static gboolean
gst_multi_queue_sink_activate_push (GstPad * pad, gboolean active)
{
  GstSingleQueue *sq;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);

  if (active) {
    /* All pads start off linked until they push one buffer */
    sq->srcresult = GST_FLOW_OK;
  } else {
    sq->srcresult = GST_FLOW_WRONG_STATE;
    gst_data_queue_flush (sq->queue);
  }
  return TRUE;
}

static gboolean
gst_multi_queue_sink_event (GstPad * pad, GstEvent * event)
{
  GstSingleQueue *sq;
  GstMultiQueue *mq;
  guint32 curid;
  GstMultiQueueItem *item;
  gboolean res;
  GstEventType type;
  GstEvent *sref = NULL;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = (GstMultiQueue *) gst_pad_get_parent (pad);

  type = GST_EVENT_TYPE (event);

  switch (type) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (mq, "SingleQueue %d : received flush start event",
          sq->id);

      res = gst_pad_push_event (sq->srcpad, event);

      gst_single_queue_flush (mq, sq, TRUE);
      goto done;

    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (mq, "SingleQueue %d : received flush stop event",
          sq->id);

      res = gst_pad_push_event (sq->srcpad, event);

      gst_single_queue_flush (mq, sq, FALSE);
      goto done;
    case GST_EVENT_NEWSEGMENT:
      /* take ref because the queue will take ownership and we need the event
       * afterwards to update the segment */
      sref = gst_event_ref (event);
      break;

    default:
      if (!(GST_EVENT_IS_SERIALIZED (event))) {
        res = gst_pad_push_event (sq->srcpad, event);
        goto done;
      }
      break;
  }

  /* Get an unique incrementing id. protected with the STREAM_LOCK, unserialized
   * events already got pushed and don't end up in the queue. */
  curid = mq->counter++;

  item = gst_multi_queue_event_item_new ((GstMiniObject *) event, curid);

  GST_DEBUG_OBJECT (mq,
      "SingleQueue %d : Enqueuing event %p of type %s with id %d",
      sq->id, event, GST_EVENT_TYPE_NAME (event), curid);

  if (!(res = gst_data_queue_push (sq->queue, (GstDataQueueItem *) item)))
    goto flushing;

  /* mark EOS when we received one, we must do that after putting the
   * buffer in the queue because EOS marks the buffer as filled. No need to take
   * a lock, the _check_full happens from this thread only, right before pushing
   * into dataqueue. */
  switch (type) {
    case GST_EVENT_EOS:
      sq->is_eos = TRUE;
      /* EOS affects the buffering state */
      update_buffering (mq, sq);
      single_queue_overrun_cb (sq->queue, sq);
      break;
    case GST_EVENT_NEWSEGMENT:
      apply_segment (mq, sq, sref, &sq->sink_segment);
      gst_event_unref (sref);
      break;
    default:
      break;
  }
done:
  gst_object_unref (mq);
  return res;

flushing:
  {
    GST_LOG_OBJECT (mq, "SingleQueue %d : exit because task paused, reason: %s",
        sq->id, gst_flow_get_name (sq->srcresult));
    if (sref)
      gst_event_unref (sref);
    gst_multi_queue_item_destroy (item);
    goto done;
  }
}

static GstCaps *
gst_multi_queue_getcaps (GstPad * pad)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);
  GstPad *otherpad;
  GstCaps *result;

  otherpad = (pad == sq->srcpad) ? sq->sinkpad : sq->srcpad;

  GST_LOG_OBJECT (otherpad, "Getting caps from the peer of this pad");

  result = gst_pad_peer_get_caps (otherpad);
  if (result == NULL)
    result = gst_caps_new_any ();

  return result;
}

static gboolean
gst_multi_queue_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);
  GstPad *otherpad;
  gboolean result;

  otherpad = (pad == sq->srcpad) ? sq->sinkpad : sq->srcpad;

  GST_LOG_OBJECT (otherpad, "Accept caps from the peer of this pad");

  result = gst_pad_peer_accept_caps (otherpad, caps);

  return result;
}

static GstFlowReturn
gst_multi_queue_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);

  return gst_pad_alloc_buffer (sq->srcpad, offset, size, caps, buf);
}

static gboolean
gst_multi_queue_src_activate_push (GstPad * pad, gboolean active)
{
  GstMultiQueue *mq;
  GstSingleQueue *sq;
  gboolean result = FALSE;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = sq->mqueue;

  GST_DEBUG_OBJECT (mq, "SingleQueue %d", sq->id);

  if (active) {
    result = gst_single_queue_flush (mq, sq, FALSE);
  } else {
    result = gst_single_queue_flush (mq, sq, TRUE);
    /* make sure streaming finishes */
    result |= gst_pad_stop_task (pad);
  }
  return result;
}

static gboolean
gst_multi_queue_src_event (GstPad * pad, GstEvent * event)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);

  return gst_pad_push_event (sq->sinkpad, event);
}

static gboolean
gst_multi_queue_src_query (GstPad * pad, GstQuery * query)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);
  GstPad *peerpad;
  gboolean res;

  /* FIXME, Handle position offset depending on queue size */

  /* default handling */
  if (!(peerpad = gst_pad_get_peer (sq->sinkpad)))
    goto no_peer;

  res = gst_pad_query (peerpad, query);

  gst_object_unref (peerpad);

  return res;

  /* ERRORS */
no_peer:
  {
    GST_LOG_OBJECT (sq->sinkpad, "Couldn't send query because we have no peer");
    return FALSE;
  }
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

  /* Else figure out which singlequeue(s) need waking up */
  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;

    if (sq->srcresult == GST_FLOW_NOT_LINKED) {
      if (sq->nextid != 0 && sq->nextid <= mq->highid) {
        GST_LOG_OBJECT (mq, "Waking up singlequeue %d", sq->id);
        g_cond_signal (sq->turn);
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
    } else if (sq->srcresult != GST_FLOW_UNEXPECTED) {
      /* If we don't have a global highid, or the global highid is lower than
       * this single queue's last outputted id, store the queue's one, 
       * unless the singlequeue is at EOS (srcresult = UNEXPECTED) */
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
  gboolean filled = FALSE;

  gst_data_queue_get_level (sq->queue, &size);

  GST_LOG_OBJECT (mq, "Single Queue %d is full", sq->id);

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *oq = (GstSingleQueue *) tmp->data;
    GstDataQueueSize ssize;

    GST_LOG_OBJECT (mq, "Checking Queue %d", oq->id);

    if (gst_data_queue_is_empty (oq->queue)) {
      GST_LOG_OBJECT (mq, "Queue %d is empty", oq->id);
      if (IS_FILLED (sq, visible, size.visible)) {
        sq->max_size.visible = size.visible + 1;
        GST_DEBUG_OBJECT (mq,
            "Another queue is empty, bumping single queue %d max visible to %d",
            sq->id, sq->max_size.visible);
      }
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
      goto beach;
    }
    /* check if we reached the hard time/bytes limits */
    gst_data_queue_get_level (oq->queue, &ssize);

    GST_DEBUG_OBJECT (mq,
        "queue %d: visible %u/%u, bytes %u/%u, time %" G_GUINT64_FORMAT "/%"
        G_GUINT64_FORMAT, oq->id, ssize.visible, oq->max_size.visible,
        ssize.bytes, oq->max_size.bytes, oq->cur_time, oq->max_size.time);

    /* if this queue is filled completely we must signal overrun.
     * FIXME, this seems wrong in many ways
     *  - we're comparing the filled level of this queue against the
     *    values of the other one
     *  - we should only do this after we found no empty queues, ie, move
     *    this check outside of the loop
     *  - the debug statement talks about a different queue than the one
     *    we are checking here.
     */
    if (sq->is_eos || IS_FILLED (sq, bytes, ssize.bytes) ||
        IS_FILLED (sq, time, sq->cur_time)) {
      GST_LOG_OBJECT (mq, "Queue %d is filled", oq->id);
      filled = TRUE;
    }
  }
  /* no queues were empty */
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  /* Overrun is always forwarded, since this is blocking the upstream element */
  if (filled) {
    GST_DEBUG_OBJECT (mq, "A queue is filled, signalling overrun");
    g_signal_emit (mq, gst_multi_queue_signals[SIGNAL_OVERRUN], 0);
  }

beach:
  return;
}

static void
single_queue_underrun_cb (GstDataQueue * dq, GstSingleQueue * sq)
{
  gboolean empty = TRUE;
  GstMultiQueue *mq = sq->mqueue;
  GList *tmp;

  GST_LOG_OBJECT (mq,
      "Single Queue %d is empty, Checking other single queues", sq->id);

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
    if (!gst_data_queue_is_empty (oq->queue))
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
  res = IS_FILLED (sq, time, sq->cur_time) || IS_FILLED (sq, bytes, bytes);

  return res;
}

static void
gst_single_queue_free (GstSingleQueue * sq)
{
  /* DRAIN QUEUE */
  gst_data_queue_flush (sq->queue);
  g_object_unref (sq->queue);
  g_cond_free (sq->turn);
  g_free (sq);
}

static GstSingleQueue *
gst_single_queue_new (GstMultiQueue * mqueue)
{
  GstSingleQueue *sq;
  gchar *tmp;

  sq = g_new0 (GstSingleQueue, 1);

  GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);
  sq->id = mqueue->nbqueues++;

  /* copy over max_size and extra_size so we don't need to take the lock
   * any longer when checking if the queue is full. */
  sq->max_size.visible = mqueue->max_size.visible;
  sq->max_size.bytes = mqueue->max_size.bytes;
  sq->max_size.time = mqueue->max_size.time;

  sq->extra_size.visible = mqueue->extra_size.visible;
  sq->extra_size.bytes = mqueue->extra_size.bytes;
  sq->extra_size.time = mqueue->extra_size.time;

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);

  GST_DEBUG_OBJECT (mqueue, "Creating GstSingleQueue id:%d", sq->id);

  sq->mqueue = mqueue;
  sq->srcresult = GST_FLOW_WRONG_STATE;
  sq->queue = gst_data_queue_new_full ((GstDataQueueCheckFullFunction)
      single_queue_check_full,
      (GstDataQueueFullCallback) single_queue_overrun_cb,
      (GstDataQueueEmptyCallback) single_queue_underrun_cb, sq);
  sq->is_eos = FALSE;
  gst_segment_init (&sq->sink_segment, GST_FORMAT_TIME);
  gst_segment_init (&sq->src_segment, GST_FORMAT_TIME);

  sq->nextid = 0;
  sq->oldid = 0;
  sq->turn = g_cond_new ();

  sq->sinktime = GST_CLOCK_TIME_NONE;
  sq->srctime = GST_CLOCK_TIME_NONE;
  sq->sink_tainted = TRUE;
  sq->src_tainted = TRUE;

  tmp = g_strdup_printf ("sink%d", sq->id);
  sq->sinkpad = gst_pad_new_from_static_template (&sinktemplate, tmp);
  g_free (tmp);

  gst_pad_set_chain_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_chain));
  gst_pad_set_activatepush_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_sink_activate_push));
  gst_pad_set_event_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_sink_event));
  gst_pad_set_getcaps_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_getcaps));
  gst_pad_set_acceptcaps_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_acceptcaps));
  gst_pad_set_bufferalloc_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_bufferalloc));
  gst_pad_set_iterate_internal_links_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_iterate_internal_links));

  tmp = g_strdup_printf ("src%d", sq->id);
  sq->srcpad = gst_pad_new_from_static_template (&srctemplate, tmp);
  g_free (tmp);

  gst_pad_set_activatepush_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_activate_push));
  gst_pad_set_getcaps_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_getcaps));
  gst_pad_set_acceptcaps_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_acceptcaps));
  gst_pad_set_event_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_event));
  gst_pad_set_query_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_query));
  gst_pad_set_iterate_internal_links_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_iterate_internal_links));

  gst_pad_set_element_private (sq->sinkpad, (gpointer) sq);
  gst_pad_set_element_private (sq->srcpad, (gpointer) sq);

  /* only activate the pads when we are not in the NULL state
   * and add the pad under the state_lock to prevend state changes
   * between activating and adding */
  g_static_rec_mutex_lock (GST_STATE_GET_LOCK (mqueue));
  if (GST_STATE_TARGET (mqueue) != GST_STATE_NULL) {
    gst_pad_set_active (sq->srcpad, TRUE);
    gst_pad_set_active (sq->sinkpad, TRUE);
  }
  gst_element_add_pad (GST_ELEMENT (mqueue), sq->srcpad);
  gst_element_add_pad (GST_ELEMENT (mqueue), sq->sinkpad);
  g_static_rec_mutex_unlock (GST_STATE_GET_LOCK (mqueue));

  GST_DEBUG_OBJECT (mqueue, "GstSingleQueue [%d] created and pads added",
      sq->id);

  return sq;
}
