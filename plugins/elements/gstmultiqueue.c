/* GStreamer
 * Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
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

  /* queue of data */
  GstDataQueue *queue;
  GstDataQueueSize max_size, extra_size;
  gboolean inextra;             /* TRUE if the queue is currently in extradata mode */

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
static void compute_next_non_linked (GstMultiQueue * mq);

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

static const GstElementDetails gst_multi_queue_details =
GST_ELEMENT_DETAILS ("MultiQueue",
    "Generic",
    "Multiple data queue",
    "Edward Hervey <edward@fluendo.com>");

#define DEFAULT_MAX_SIZE_BYTES 10 * 1024 * 1024 /* 10 MB */
#define DEFAULT_MAX_SIZE_BUFFERS 200
#define DEFAULT_MAX_SIZE_TIME GST_SECOND

#define DEFAULT_EXTRA_SIZE_BYTES 10 * 1024 * 1024       /* 10 MB */
#define DEFAULT_EXTRA_SIZE_BUFFERS 200
#define DEFAULT_EXTRA_SIZE_TIME GST_SECOND

/* Signals and args */
enum
{
  SIGNAL_UNDERRUN,
  SIGNAL_OVERRUN,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_EXTRA_SIZE_BYTES,
  ARG_EXTRA_SIZE_BUFFERS,
  ARG_EXTRA_SIZE_TIME,
  ARG_MAX_SIZE_BYTES,
  ARG_MAX_SIZE_BUFFERS,
  ARG_MAX_SIZE_TIME,
};

#define GST_MULTI_QUEUE_MUTEX_LOCK(q) G_STMT_START {                          \
  GST_CAT_LOG_OBJECT (multi_queue_debug, q,                                \
      "locking qlock from thread %p",                                   \
      g_thread_self ());                                                \
  g_mutex_lock (q->qlock);                                              \
  GST_CAT_LOG_OBJECT (multi_queue_debug, q,                                \
      "locked qlock from thread %p",                                    \
      g_thread_self ());                                                \
} G_STMT_END

#define GST_MULTI_QUEUE_MUTEX_UNLOCK(q) G_STMT_START {                        \
  GST_CAT_LOG_OBJECT (multi_queue_debug, q,                                \
      "unlocking qlock from thread %p",                                 \
      g_thread_self ());                                                \
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

#define _do_init(bla) \
  GST_DEBUG_CATEGORY_INIT (multi_queue_debug, "multiqueue", 0, "multiqueue element");

GST_BOILERPLATE_FULL (GstMultiQueue, gst_multi_queue, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static guint gst_multi_queue_signals[LAST_SIGNAL] = { 0 };

static void
gst_multi_queue_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details (gstelement_class, &gst_multi_queue_details);
}

static void
gst_multi_queue_class_init (GstMultiQueueClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_multi_queue_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_multi_queue_get_property);

  /* SIGNALS */
  gst_multi_queue_signals[SIGNAL_UNDERRUN] =
      g_signal_new ("underrun", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstMultiQueueClass, underrun), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gst_multi_queue_signals[SIGNAL_OVERRUN] =
      g_signal_new ("overrun", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstMultiQueueClass, overrun), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /* PROPERTIES */

  g_object_class_install_property (gobject_class, ARG_MAX_SIZE_BYTES,
      g_param_spec_uint ("max-size-bytes", "Max. size (kB)",
          "Max. amount of data in the queue (bytes, 0=disable)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BYTES, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_SIZE_BUFFERS,
      g_param_spec_uint ("max-size-buffers", "Max. size (buffers)",
          "Max. number of buffers in the queue (0=disable)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BUFFERS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
          "Max. amount of data in the queue (in ns, 0=disable)",
          0, G_MAXUINT64, DEFAULT_MAX_SIZE_TIME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_EXTRA_SIZE_BYTES,
      g_param_spec_uint ("extra-size-bytes", "Extra Size (kB)",
          "Amount of data the queues can grow if one of them is empty (bytes, 0=disable)",
          0, G_MAXUINT, DEFAULT_EXTRA_SIZE_BYTES, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_EXTRA_SIZE_BUFFERS,
      g_param_spec_uint ("extra-size-buffers", "Extra Size (buffers)",
          "Amount of buffers the queues can grow if one of them is empty (0=disable)",
          0, G_MAXUINT, DEFAULT_EXTRA_SIZE_BUFFERS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_EXTRA_SIZE_TIME,
      g_param_spec_uint64 ("extra-size-time", "Extra Size (ns)",
          "Amount of time the queues can grow if one of them is empty (in ns, 0=disable)",
          0, G_MAXUINT64, DEFAULT_EXTRA_SIZE_TIME, G_PARAM_READWRITE));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_multi_queue_finalize);

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

  mqueue->counter = 0;
  mqueue->highid = -1;
  mqueue->nextnotlinked = -1;

  mqueue->qlock = g_mutex_new ();

  /* FILLME ? */
}

static void
gst_multi_queue_finalize (GObject * object)
{
  GstMultiQueue *mqueue = GST_MULTI_QUEUE (object);

  g_list_foreach (mqueue->queues, (GFunc) gst_single_queue_free, NULL);
  g_list_free (mqueue->queues);
  mqueue->queues = NULL;

  /* free/unref instance data */
  g_mutex_free (mqueue->qlock);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

#define SET_CHILD_PROPERTY(mq,name,value) G_STMT_START {	\
    GList * tmp = mq->queues;					\
    while (tmp) {						\
      GstSingleQueue *q = (GstSingleQueue*)tmp->data;		\
      g_object_set_property ((GObject*) q->queue, name, value);	\
      tmp = g_list_next(tmp);					\
    };								\
} G_STMT_END

static void
gst_multi_queue_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiQueue *mq = GST_MULTI_QUEUE (object);

  switch (prop_id) {
    case ARG_MAX_SIZE_BYTES:
      mq->max_size.bytes = g_value_get_uint (value);
      SET_CHILD_PROPERTY (mq, "max-size-bytes", value);
      break;
    case ARG_MAX_SIZE_BUFFERS:
      mq->max_size.visible = g_value_get_uint (value);
      SET_CHILD_PROPERTY (mq, "max-size-visible", value);
      break;
    case ARG_MAX_SIZE_TIME:
      mq->max_size.time = g_value_get_uint64 (value);
      SET_CHILD_PROPERTY (mq, "max-size-time", value);
      break;
    case ARG_EXTRA_SIZE_BYTES:
      mq->extra_size.bytes = g_value_get_uint (value);
      break;
    case ARG_EXTRA_SIZE_BUFFERS:
      mq->extra_size.visible = g_value_get_uint (value);
      break;
    case ARG_EXTRA_SIZE_TIME:
      mq->extra_size.time = g_value_get_uint64 (value);
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
    case ARG_EXTRA_SIZE_BYTES:
      g_value_set_uint (value, mq->extra_size.bytes);
      break;
    case ARG_EXTRA_SIZE_BUFFERS:
      g_value_set_uint (value, mq->extra_size.visible);
      break;
    case ARG_EXTRA_SIZE_TIME:
      g_value_set_uint64 (value, mq->extra_size.time);
      break;
    case ARG_MAX_SIZE_BYTES:
      g_value_set_uint (value, mq->max_size.bytes);
      break;
    case ARG_MAX_SIZE_BUFFERS:
      g_value_set_uint (value, mq->max_size.visible);
      break;
    case ARG_MAX_SIZE_TIME:
      g_value_set_uint64 (value, mq->max_size.time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
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

  GST_LOG_OBJECT (element, "name : %s", name);

  /* Create a new single queue, add the sink and source pad and return the sink pad */
  squeue = gst_single_queue_new (mqueue);

  GST_MULTI_QUEUE_MUTEX_LOCK (mqueue);
  mqueue->queues = g_list_append (mqueue->queues, squeue);
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

  /* FIXME : recompute next-non-linked */
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);

  /* delete SingleQueue */
  gst_data_queue_set_flushing (sq->queue, TRUE);

  gst_pad_set_active (sq->srcpad, FALSE);
  gst_pad_set_active (sq->sinkpad, FALSE);
  gst_element_remove_pad (element, sq->srcpad);
  gst_element_remove_pad (element, sq->sinkpad);
  gst_single_queue_free (sq);
}

static gboolean
gst_single_queue_push_one (GstMultiQueue * mq, GstSingleQueue * sq,
    GstMiniObject * object)
{
  if (GST_IS_BUFFER (object)) {
    GstBuffer *buf;

    buf = gst_buffer_ref (GST_BUFFER_CAST (object));
    sq->srcresult = gst_pad_push (sq->srcpad, buf);

    if ((sq->srcresult != GST_FLOW_OK)
        && (sq->srcresult != GST_FLOW_NOT_LINKED)) {
      GST_DEBUG_OBJECT (mq, "GstSingleQueue %d : pausing queue, reason %s",
          sq->id, gst_flow_get_name (sq->srcresult));
      gst_data_queue_set_flushing (sq->queue, TRUE);
      gst_pad_pause_task (sq->srcpad);
    }
  } else if (GST_IS_EVENT (object)) {
    GstEvent *event;

    event = gst_event_ref (GST_EVENT_CAST (object));
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      sq->srcresult = GST_FLOW_UNEXPECTED;

      GST_DEBUG_OBJECT (mq, "GstSingleQueue %d : pausing queue, got EOS",
          sq->id);
      gst_data_queue_set_flushing (sq->queue, TRUE);
      gst_pad_pause_task (sq->srcpad);
    }
    gst_pad_push_event (sq->srcpad, event);
  } else {
    g_warning ("Unexpected object in singlequeue %d (refcounting problem?)",
        sq->id);
  }

  return FALSE;
}

static void
gst_multi_queue_item_destroy (GstMultiQueueItem * item)
{
  gst_mini_object_unref (item->object);
  g_free (item);
}

/* takes ownership of passed mini object! */
static GstMultiQueueItem *
gst_multi_queue_item_new (GstMiniObject * object)
{
  GstMultiQueueItem *item;

  item = g_new0 (GstMultiQueueItem, 1);
  item->object = object;
  item->destroy = (GDestroyNotify) gst_multi_queue_item_destroy;

  if (GST_IS_BUFFER (object)) {
    item->size = GST_BUFFER_SIZE (object);
    item->duration = GST_BUFFER_DURATION (object);
    if (item->duration == GST_CLOCK_TIME_NONE)
      item->duration = 0;
    item->visible = TRUE;
  }

  return item;
}

static void
gst_multi_queue_loop (GstPad * pad)
{
  GstSingleQueue *sq;
  GstMultiQueueItem *item;
  GstDataQueueItem *sitem;
  GstMultiQueue *mq;
  GstMiniObject *object;
  guint32 newid;
  guint32 oldid = -1;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = sq->mqueue;

restart:
  GST_DEBUG_OBJECT (mq, "SingleQueue %d : trying to pop an object", sq->id);
  if (!(gst_data_queue_pop (sq->queue, &sitem))) {
    /* QUEUE FLUSHING */
    if (sq->srcresult != GST_FLOW_OK)
      goto out_flushing;
    else
      GST_WARNING_OBJECT (mq,
          "data_queue_pop() returned FALSE, but srcresult == GST_FLOW_OK !");
  } else {
    item = (GstMultiQueueItem *) sitem;
    newid = item->posid;
    object = item->object;

    GST_LOG_OBJECT (mq, "SingleQueue %d : newid:%d , oldid:%d",
        sq->id, newid, oldid);

    /* 1. Only check turn if :
     * _ We haven't pushed anything yet 
     * _ OR the new id isn't the follower of the previous one (continuous segment) */
    if ((oldid == -1) || (newid != (oldid + 1))) {
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);

      GST_LOG_OBJECT (mq, "CHECKING sq->srcresult: %s",
          gst_flow_get_name (sq->srcresult));

      /* preamble : if we're not linked, set the newid as the next one we want */
      if (sq->srcresult == GST_FLOW_NOT_LINKED)
        sq->nextid = newid;

      /* store the last id we outputted */
      if (oldid != -1)
        sq->oldid = oldid;

      /* 2. If there's a queue waiting to push, wake it up. If it's us the */
      /*    check below (3.) will avoid us waiting. */
      wake_up_next_non_linked (mq);

      /* 3. If we're not linked AND our nextid is higher than the highest oldid outputted
       * _ Update global next-not-linked
       * _ Wait on our conditional 
       */
      while ((sq->srcresult == GST_FLOW_NOT_LINKED)
          && (mq->nextnotlinked != sq->id)) {
        compute_next_non_linked (mq);
        g_cond_wait (sq->turn, mq->qlock);
      }

      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

      /* 4. Check again status, maybe we're flushing */
      if ((sq->srcresult != GST_FLOW_OK)
          && (sq->srcresult != GST_FLOW_NOT_LINKED)) {
        gst_multi_queue_item_destroy (item);
        goto out_flushing;
      }
    }

    GST_LOG_OBJECT (mq, "BEFORE PUSHING sq->srcresult: %s",
        gst_flow_get_name (sq->srcresult));

    /* 4. Try to push out the new object */
    gst_single_queue_push_one (mq, sq, object);

    GST_LOG_OBJECT (mq, "AFTER PUSHING sq->srcresult: %s",
        gst_flow_get_name (sq->srcresult));

    gst_multi_queue_item_destroy (item);
    oldid = newid;

    /* 5. if GstFlowReturn is non-fatal, goto restart */
    if ((sq->srcresult == GST_FLOW_OK)
        || (sq->srcresult == GST_FLOW_NOT_LINKED))
      goto restart;
  }


beach:
  return;

out_flushing:
  {
    gst_pad_pause_task (sq->srcpad);
    GST_CAT_LOG_OBJECT (multi_queue_debug, mq,
        "SingleQueue[%d] task paused, reason:%s",
        sq->id, gst_flow_get_name (sq->srcresult));
    goto beach;
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
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 curid;

  sq = gst_pad_get_element_private (pad);
  mq = (GstMultiQueue *) gst_pad_get_parent (pad);

  /* Get an unique incrementing id */
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  curid = mq->counter++;
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  GST_LOG_OBJECT (mq, "SingleQueue %d : about to push buffer with id %d",
      sq->id, curid);

  item = gst_multi_queue_item_new ((GstMiniObject *) buffer);
  item->posid = curid;

  if (!(gst_data_queue_push (sq->queue, (GstDataQueueItem *) item))) {
    GST_LOG_OBJECT (mq, "SingleQueue %d : exit because task paused, reason: %s",
        sq->id, gst_flow_get_name (sq->srcresult));
    gst_multi_queue_item_destroy (item);
    ret = sq->srcresult;
  }

  gst_object_unref (mq);
  return ret;
}

static gboolean
gst_multi_queue_sink_activate_push (GstPad * pad, gboolean active)
{
  GstSingleQueue *sq;

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);

  if (active)
    sq->srcresult = GST_FLOW_OK;
  else {
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

  sq = (GstSingleQueue *) gst_pad_get_element_private (pad);
  mq = (GstMultiQueue *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (mq, "SingleQueue %d : received flush start event",
          sq->id);

      gst_pad_push_event (sq->srcpad, event);

      sq->srcresult = GST_FLOW_WRONG_STATE;
      gst_data_queue_set_flushing (sq->queue, TRUE);

      /* wake up non-linked task */
      GST_LOG_OBJECT (mq, "SingleQueue %d : waking up eventually waiting task",
          sq->id);
      GST_MULTI_QUEUE_MUTEX_LOCK (mq);
      g_cond_signal (sq->turn);
      GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

      gst_pad_pause_task (sq->srcpad);
      goto done;

    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (mq, "SingleQueue %d : received flush stop event",
          sq->id);

      gst_pad_push_event (sq->srcpad, event);

      gst_data_queue_flush (sq->queue);
      gst_data_queue_set_flushing (sq->queue, FALSE);
      sq->srcresult = GST_FLOW_OK;
      sq->nextid = -1;
      sq->oldid = -1;

      GST_DEBUG_OBJECT (mq, "SingleQueue %d : restarting task", sq->id);
      gst_pad_start_task (sq->srcpad, (GstTaskFunction) gst_multi_queue_loop,
          sq->srcpad);
      goto done;

    default:
      if (!(GST_EVENT_IS_SERIALIZED (event))) {
        gst_pad_push_event (sq->srcpad, event);
        goto done;
      }
      break;
  }

  /* Get an unique incrementing id */
  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  curid = mq->counter++;
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  item = gst_multi_queue_item_new ((GstMiniObject *) event);
  item->posid = curid;

  GST_DEBUG_OBJECT (mq,
      "SingleQueue %d : Adding event %p of type %s with id %d", sq->id, event,
      GST_EVENT_TYPE_NAME (event), curid);

  if (!(gst_data_queue_push (sq->queue, (GstDataQueueItem *) item))) {
    GST_LOG_OBJECT (mq, "SingleQueue %d : exit because task paused, reason: %s",
        sq->id, gst_flow_get_name (sq->srcresult));
    gst_multi_queue_item_destroy (item);
  }

done:
  gst_object_unref (mq);
  return TRUE;
}

static GstCaps *
gst_multi_queue_getcaps (GstPad * pad)
{
  GstSingleQueue *sq = gst_pad_get_element_private (pad);
  GstPad *otherpad;
  GstCaps *result;

  GST_LOG_OBJECT (pad, "...");

  otherpad = (pad == sq->srcpad) ? sq->sinkpad : sq->srcpad;

  GST_LOG_OBJECT (otherpad, "Getting caps from the peer of this pad");

  result = gst_pad_peer_get_caps (otherpad);
  if (result == NULL)
    result = gst_caps_new_any ();

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

  GST_LOG ("SingleQueue %d", sq->id);

  if (active) {
    sq->srcresult = GST_FLOW_OK;
    gst_data_queue_set_flushing (sq->queue, FALSE);
    result = gst_pad_start_task (pad, (GstTaskFunction) gst_multi_queue_loop,
        pad);
  } else {
    /* 1. unblock loop function */
    sq->srcresult = GST_FLOW_WRONG_STATE;
    gst_data_queue_set_flushing (sq->queue, TRUE);

    /* 2. unblock potentially non-linked pad */
    GST_LOG_OBJECT (mq, "SingleQueue %d : waking up eventually waiting task",
        sq->id);
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    g_cond_signal (sq->turn);
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

    /* 3. make sure streaming finishes */
    result = gst_pad_stop_task (pad);
    gst_data_queue_set_flushing (sq->queue, FALSE);
  }

  return result;
}

static gboolean
gst_multi_queue_acceptcaps (GstPad * pad, GstCaps * caps)
{
  return TRUE;
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

  /* FILLME */
  /* Handle position offset depending on queue size */

  /* default handling */
  if (!(peerpad = gst_pad_get_peer (sq->sinkpad)))
    goto no_peer;

  res = gst_pad_query (peerpad, query);

  gst_object_unref (peerpad);

  return res;

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

  GST_LOG ("mq->nextnotlinked:%d", mq->nextnotlinked);

  /* maybe no-one is waiting */
  if (mq->nextnotlinked == -1)
    return;

  /* Else figure out which singlequeue it is and wake it up */
  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;

    if (sq->srcresult == GST_FLOW_NOT_LINKED)
      if (sq->id == mq->nextnotlinked) {
        GST_LOG_OBJECT (mq, "Waking up singlequeue %d", sq->id);
        g_cond_signal (sq->turn);
        return;
      }
  }
}

/* WITH LOCK TAKEN */
static void
compute_next_non_linked (GstMultiQueue * mq)
{
  GList *tmp;
  guint32 lowest = G_MAXUINT32;
  gint nextid = -1;

  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;

    GST_LOG ("inspecting sq:%d , nextid:%d, oldid:%d, srcresult:%s",
        sq->id, sq->nextid, sq->oldid, gst_flow_get_name (sq->srcresult));

    if (sq->srcresult == GST_FLOW_NOT_LINKED)
      if (lowest > sq->nextid) {
        lowest = sq->nextid;
        nextid = sq->id;
      }

    /* If we don't have a global highid, or the global highid is lower than */
    /* this single queue's last outputted id, store the queue's one */
    if ((mq->highid == -1) || (mq->highid < sq->oldid))
      mq->highid = sq->oldid;
  }

  mq->nextnotlinked = nextid;
  GST_LOG_OBJECT (mq,
      "Next-non-linked is sq #%d with nextid : %d. Highid is now : %d", nextid,
      lowest, mq->highid);
}

/*
 * GstSingleQueue functions
 */

static void
single_queue_overrun_cb (GstDataQueue * dq, GstSingleQueue * sq)
{
  GstMultiQueue *mq = sq->mqueue;
  GList *tmp;

  GST_LOG_OBJECT (sq->mqueue, "Single Queue %d is full", sq->id);

  if (!sq->inextra) {
    /* Check if at least one other queue is empty... */
    GST_MULTI_QUEUE_MUTEX_LOCK (mq);
    for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
      GstSingleQueue *ssq = (GstSingleQueue *) tmp->data;

      if (gst_data_queue_is_empty (ssq->queue)) {
        /* ... if so set sq->inextra to TRUE and don't emit overrun signal */
        GST_DEBUG_OBJECT (mq,
            "Another queue is empty, bumping single queue into extra data mode");
        sq->inextra = TRUE;
        GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
        goto beach;
      }
    }
    GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);
  }

  /* Overrun is always forwarded, since this is blocking the upstream element */
  g_signal_emit (G_OBJECT (sq->mqueue), gst_multi_queue_signals[SIGNAL_OVERRUN],
      0);

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
      "Single Queue %d is empty, Checking if all single queues are empty",
      sq->id);

  GST_MULTI_QUEUE_MUTEX_LOCK (mq);
  for (tmp = mq->queues; tmp; tmp = g_list_next (tmp)) {
    GstSingleQueue *sq = (GstSingleQueue *) tmp->data;

    if (!gst_data_queue_is_empty (sq->queue)) {
      empty = FALSE;
      break;
    }
  }
  GST_MULTI_QUEUE_MUTEX_UNLOCK (mq);

  if (empty) {
    GST_DEBUG_OBJECT (mq, "All queues are empty, signalling it");
    g_signal_emit (G_OBJECT (mq), gst_multi_queue_signals[SIGNAL_UNDERRUN], 0);
  }
}

static gboolean
single_queue_check_full (GstDataQueue * dataq, guint visible, guint bytes,
    guint64 time, GstSingleQueue * sq)
{
  gboolean res;

  /* In all cases (extra mode or not), we check how the queue current level
   * compares to max_size. */
  res = (((sq->max_size.visible != 0) &&
          sq->max_size.visible < visible) ||
      ((sq->max_size.bytes != 0) &&
          sq->max_size.bytes < bytes) ||
      ((sq->max_size.time != 0) && sq->max_size.time < time));

  if (G_UNLIKELY (sq->inextra)) {
    /* If we're in extra mode, one of two things can happen to check for
     * fullness: */

    if (!res)
      /* #1 : Either we are not full against normal max_size levels, in which
       * case we can go out of extra mode. */
      sq->inextra = FALSE;
    else
      /* #2 : Or else, the check should be done against max_size + extra_size */
      res = (((sq->max_size.visible != 0) &&
              (sq->max_size.visible + sq->extra_size.visible) < visible) ||
          ((sq->max_size.bytes != 0) &&
              (sq->max_size.bytes + sq->extra_size.bytes) < bytes) ||
          ((sq->max_size.time != 0) &&
              (sq->max_size.time + sq->extra_size.time) < time));

  }
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
  /* FIXME : We can't modify those values once the single queue is created
   * since we don't have any lock protecting those values. */
  sq->max_size.visible = mqueue->max_size.visible;
  sq->max_size.bytes = mqueue->max_size.bytes;
  sq->max_size.time = mqueue->max_size.time;

  sq->extra_size.visible = mqueue->extra_size.visible;
  sq->extra_size.bytes = mqueue->extra_size.bytes;
  sq->extra_size.time = mqueue->extra_size.time;

  GST_MULTI_QUEUE_MUTEX_UNLOCK (mqueue);

  GST_DEBUG_OBJECT (mqueue, "Creating GstSingleQueue id:%d", sq->id);

  sq->mqueue = mqueue;
  sq->srcresult = GST_FLOW_OK;
  sq->queue = gst_data_queue_new ((GstDataQueueCheckFullFunction)
      single_queue_check_full, sq);

  sq->nextid = -1;
  sq->oldid = -1;
  sq->turn = g_cond_new ();

  /* FIXME : attach to underrun/overrun signals to handle non-starvation 
   * OR should this be  handled when we check if the queue is full/empty before pushing/popping ? */

  g_signal_connect (G_OBJECT (sq->queue), "full",
      G_CALLBACK (single_queue_overrun_cb), sq);

  g_signal_connect (G_OBJECT (sq->queue), "empty",
      G_CALLBACK (single_queue_underrun_cb), sq);

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
  gst_pad_set_bufferalloc_function (sq->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_bufferalloc));

  tmp = g_strdup_printf ("src%d", sq->id);
  sq->srcpad = gst_pad_new_from_static_template (&srctemplate, tmp);
  g_free (tmp);

  gst_pad_set_activatepush_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_activate_push));
  gst_pad_set_acceptcaps_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_acceptcaps));
  gst_pad_set_getcaps_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_getcaps));
  gst_pad_set_event_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_event));
  gst_pad_set_query_function (sq->srcpad,
      GST_DEBUG_FUNCPTR (gst_multi_queue_src_query));

  gst_pad_set_element_private (sq->sinkpad, (gpointer) sq);
  gst_pad_set_element_private (sq->srcpad, (gpointer) sq);

  gst_pad_set_active (sq->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (mqueue), sq->srcpad);

  gst_pad_set_active (sq->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (mqueue), sq->sinkpad);

  GST_DEBUG_OBJECT (mqueue, "GstSingleQueue [%d] created and pads added",
      sq->id);

  return sq;
}
