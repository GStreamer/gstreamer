/* GStreamer
 * Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
 *
 * gstdataqueue.c:
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
 * SECTION:gstdataqueue
 * @short_description: Threadsafe queueing object
 *
 * #EGLGstDataQueue is an object that handles threadsafe queueing of objects. It
 * also provides size-related functionality. This object should be used for
 * any #GstElement that wishes to provide some sort of queueing functionality.
 */

#include <gst/gst.h>
#include <string.h>

#include "gstdataqueue.h"
#include "gstqueuearray.h"

GST_DEBUG_CATEGORY_STATIC (data_queue_debug);
#define GST_CAT_DEFAULT (data_queue_debug)
GST_DEBUG_CATEGORY_STATIC (data_queue_dataflow);


/* Queue signals and args */
enum
{
  SIGNAL_EMPTY,
  SIGNAL_FULL,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CUR_LEVEL_VISIBLE,
  PROP_CUR_LEVEL_BYTES,
  PROP_CUR_LEVEL_TIME
      /* FILL ME */
};

struct _EGLGstDataQueuePrivate
{
  /* the array of data we're keeping our grubby hands on */
  EGLGstQueueArray *queue;

  EGLGstDataQueueSize cur_level;   /* size of the queue */
  EGLGstDataQueueCheckFullFunction checkfull;      /* Callback to check if the queue is full */
  gpointer *checkdata;

  GMutex qlock;                 /* lock for queue (vs object lock) */
  gboolean waiting_add;
  GCond item_add;               /* signals buffers now available for reading */
  gboolean waiting_del;
  GCond item_del;               /* signals space now available for writing */
  gboolean flushing;            /* indicates whether conditions where signalled because
                                 * of external flushing */
  EGLGstDataQueueFullCallback fullcallback;
  EGLGstDataQueueEmptyCallback emptycallback;
};

#define EGL_GST_DATA_QUEUE_MUTEX_LOCK(q) G_STMT_START {                     \
    GST_CAT_TRACE (data_queue_dataflow,                                 \
      "locking qlock from thread %p",                                   \
      g_thread_self ());                                                \
  g_mutex_lock (&q->priv->qlock);                                       \
  GST_CAT_TRACE (data_queue_dataflow,                                   \
      "locked qlock from thread %p",                                    \
      g_thread_self ());                                                \
} G_STMT_END

#define EGL_GST_DATA_QUEUE_MUTEX_LOCK_CHECK(q, label) G_STMT_START {        \
    EGL_GST_DATA_QUEUE_MUTEX_LOCK (q);                                      \
    if (q->priv->flushing)                                              \
      goto label;                                                       \
  } G_STMT_END

#define EGL_GST_DATA_QUEUE_MUTEX_UNLOCK(q) G_STMT_START {                   \
    GST_CAT_TRACE (data_queue_dataflow,                                 \
      "unlocking qlock from thread %p",                                 \
      g_thread_self ());                                                \
  g_mutex_unlock (&q->priv->qlock);                                     \
} G_STMT_END

#define STATUS(q, msg)                                                  \
  GST_CAT_LOG (data_queue_dataflow,                                     \
               "queue:%p " msg ": %u visible items, %u "                \
               "bytes, %"G_GUINT64_FORMAT                               \
               " ns, %u elements",                                      \
               queue,                                                   \
               q->priv->cur_level.visible,                              \
               q->priv->cur_level.bytes,                                \
               q->priv->cur_level.time,                                 \
               egl_gst_queue_array_get_length (q->priv->queue))

static void egl_gst_data_queue_finalize (GObject * object);

static void egl_gst_data_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void egl_gst_data_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static guint egl_gst_data_queue_signals[LAST_SIGNAL] = { 0 };

#define _do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (data_queue_debug, "egldataqueue", 0, \
      "data queue object"); \
  GST_DEBUG_CATEGORY_INIT (data_queue_dataflow, "egldata_queue_dataflow", 0, \
      "dataflow inside the data queue object"); \
}

#define parent_class egl_gst_data_queue_parent_class
G_DEFINE_TYPE_WITH_CODE (EGLGstDataQueue, egl_gst_data_queue, G_TYPE_OBJECT, _do_init);

static void
egl_gst_data_queue_class_init (EGLGstDataQueueClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EGLGstDataQueuePrivate));

  gobject_class->set_property = egl_gst_data_queue_set_property;
  gobject_class->get_property = egl_gst_data_queue_get_property;

  /* signals */
  /**
   * EGLGstDataQueue::empty:
   * @queue: the queue instance
   *
   * Reports that the queue became empty (empty).
   * A queue is empty if the total amount of visible items inside it (num-visible, time,
   * size) is lower than the boundary values which can be set through the GObject
   * properties.
   */
  egl_gst_data_queue_signals[SIGNAL_EMPTY] =
      g_signal_new ("empty", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (EGLGstDataQueueClass, empty), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * EGLGstDataQueue::full:
   * @queue: the queue instance
   *
   * Reports that the queue became full (full).
   * A queue is full if the total amount of data inside it (num-visible, time,
   * size) is higher than the boundary values which can be set through the GObject
   * properties.
   */
  egl_gst_data_queue_signals[SIGNAL_FULL] =
      g_signal_new ("full", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (EGLGstDataQueueClass, full), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /* properties */
  g_object_class_install_property (gobject_class, PROP_CUR_LEVEL_BYTES,
      g_param_spec_uint ("current-level-bytes", "Current level (kB)",
          "Current amount of data in the queue (bytes)",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CUR_LEVEL_VISIBLE,
      g_param_spec_uint ("current-level-visible",
          "Current level (visible items)",
          "Current number of visible items in the queue", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CUR_LEVEL_TIME,
      g_param_spec_uint64 ("current-level-time", "Current level (ns)",
          "Current amount of data in the queue (in ns)", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = egl_gst_data_queue_finalize;
}

static void
egl_gst_data_queue_init (EGLGstDataQueue * queue)
{
  queue->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (queue, EGL_GST_TYPE_DATA_QUEUE,
      EGLGstDataQueuePrivate);

  queue->priv->cur_level.visible = 0;   /* no content */
  queue->priv->cur_level.bytes = 0;     /* no content */
  queue->priv->cur_level.time = 0;      /* no content */

  queue->priv->checkfull = NULL;

  g_mutex_init (&queue->priv->qlock);
  g_cond_init (&queue->priv->item_add);
  g_cond_init (&queue->priv->item_del);
  queue->priv->queue = egl_gst_queue_array_new (50);

  GST_DEBUG ("initialized queue's not_empty & not_full conditions");
}

/**
 * egl_gst_data_queue_new:
 * @checkfull: the callback used to tell if the element considers the queue full
 * or not.
 * @fullcallback: the callback which will be called when the queue is considered full.
 * @emptycallback: the callback which will be called when the queue is considered empty.
 * @checkdata: a #gpointer that will be given in the @checkfull callback.
 *
 * Creates a new #EGLGstDataQueue. The difference with @egl_gst_data_queue_new is that it will
 * not emit the 'full' and 'empty' signals, but instead calling directly @fullcallback
 * or @emptycallback.
 *
 * Returns: a new #EGLGstDataQueue.
 *
 * Since: 1.2.0
 */
EGLGstDataQueue *
egl_gst_data_queue_new (EGLGstDataQueueCheckFullFunction checkfull,
    EGLGstDataQueueFullCallback fullcallback,
    EGLGstDataQueueEmptyCallback emptycallback, gpointer checkdata)
{
  EGLGstDataQueue *ret;

  g_return_val_if_fail (checkfull != NULL, NULL);

  ret = g_object_newv (EGL_GST_TYPE_DATA_QUEUE, 0, NULL);
  ret->priv->checkfull = checkfull;
  ret->priv->checkdata = checkdata;
  ret->priv->fullcallback = fullcallback;
  ret->priv->emptycallback = emptycallback;

  return ret;
}

static void
egl_gst_data_queue_cleanup (EGLGstDataQueue * queue)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  while (!egl_gst_queue_array_is_empty (priv->queue)) {
    EGLGstDataQueueItem *item = egl_gst_queue_array_pop_head (priv->queue);

    /* Just call the destroy notify on the item */
    item->destroy (item);
  }
  priv->cur_level.visible = 0;
  priv->cur_level.bytes = 0;
  priv->cur_level.time = 0;
}

/* called only once, as opposed to dispose */
static void
egl_gst_data_queue_finalize (GObject * object)
{
  EGLGstDataQueue *queue = EGL_GST_DATA_QUEUE (object);
  EGLGstDataQueuePrivate *priv = queue->priv;

  GST_DEBUG ("finalizing queue");

  egl_gst_data_queue_cleanup (queue);
  egl_gst_queue_array_free (priv->queue);

  GST_DEBUG ("free mutex");
  g_mutex_clear (&priv->qlock);
  GST_DEBUG ("done free mutex");

  g_cond_clear (&priv->item_add);
  g_cond_clear (&priv->item_del);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static inline void
egl_gst_data_queue_locked_flush (EGLGstDataQueue * queue)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  STATUS (queue, "before flushing");
  egl_gst_data_queue_cleanup (queue);
  STATUS (queue, "after flushing");
  /* we deleted something... */
  if (priv->waiting_del)
    g_cond_signal (&priv->item_del);
}

static inline gboolean
egl_gst_data_queue_locked_is_empty (EGLGstDataQueue * queue)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  return (egl_gst_queue_array_get_length (priv->queue) == 0);
}

static inline gboolean
egl_gst_data_queue_locked_is_full (EGLGstDataQueue * queue)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  return priv->checkfull (queue, priv->cur_level.visible,
      priv->cur_level.bytes, priv->cur_level.time, priv->checkdata);
}

/**
 * egl_gst_data_queue_flush:
 * @queue: a #EGLGstDataQueue.
 *
 * Flushes all the contents of the @queue. Any call to #egl_gst_data_queue_push and
 * #egl_gst_data_queue_pop will be released.
 * MT safe.
 *
 * Since: 1.2.0
 */
void
egl_gst_data_queue_flush (EGLGstDataQueue * queue)
{
  GST_DEBUG ("queue:%p", queue);
  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);
  egl_gst_data_queue_locked_flush (queue);
  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}

/**
 * egl_gst_data_queue_is_empty:
 * @queue: a #EGLGstDataQueue.
 *
 * Queries if there are any items in the @queue.
 * MT safe.
 *
 * Returns: #TRUE if @queue is empty.
 *
 * Since: 1.2.0
 */
gboolean
egl_gst_data_queue_is_empty (EGLGstDataQueue * queue)
{
  gboolean res;

  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);
  res = egl_gst_data_queue_locked_is_empty (queue);
  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return res;
}

/**
 * egl_gst_data_queue_is_full:
 * @queue: a #EGLGstDataQueue.
 *
 * Queries if @queue is full. This check will be done using the
 * #EGLGstDataQueueCheckFullFunction registered with @queue.
 * MT safe.
 *
 * Returns: #TRUE if @queue is full.
 *
 * Since: 1.2.0
 */
gboolean
egl_gst_data_queue_is_full (EGLGstDataQueue * queue)
{
  gboolean res;

  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);
  res = egl_gst_data_queue_locked_is_full (queue);
  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return res;
}

/**
 * egl_gst_data_queue_set_flushing:
 * @queue: a #EGLGstDataQueue.
 * @flushing: a #gboolean stating if the queue will be flushing or not.
 *
 * Sets the queue to flushing state if @flushing is #TRUE. If set to flushing
 * state, any incoming data on the @queue will be discarded. Any call currently
 * blocking on #egl_gst_data_queue_push or #egl_gst_data_queue_pop will return straight
 * away with a return value of #FALSE. While the @queue is in flushing state, 
 * all calls to those two functions will return #FALSE.
 *
 * MT Safe.
 *
 * Since: 1.2.0
 */
void
egl_gst_data_queue_set_flushing (EGLGstDataQueue * queue, gboolean flushing)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  GST_DEBUG ("queue:%p , flushing:%d", queue, flushing);

  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);
  priv->flushing = flushing;
  if (flushing) {
    /* release push/pop functions */
    if (priv->waiting_add)
      g_cond_signal (&priv->item_add);
    if (priv->waiting_del)
      g_cond_signal (&priv->item_del);
  }
  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}

/**
 * egl_gst_data_queue_push:
 * @queue: a #EGLGstDataQueue.
 * @item: a #EGLGstDataQueueItem.
 *
 * Pushes a #EGLGstDataQueueItem (or a structure that begins with the same fields)
 * on the @queue. If the @queue is full, the call will block until space is
 * available, OR the @queue is set to flushing state.
 * MT safe.
 *
 * Note that this function has slightly different semantics than gst_pad_push()
 * and gst_pad_push_event(): this function only takes ownership of @item and
 * the #GstMiniObject contained in @item if the push was successful. If FALSE
 * is returned, the caller is responsible for freeing @item and its contents.
 *
 * Returns: #TRUE if the @item was successfully pushed on the @queue.
 *
 * Since: 1.2.0
 */
gboolean
egl_gst_data_queue_push (EGLGstDataQueue * queue, EGLGstDataQueueItem * item)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  g_return_val_if_fail (EGL_GST_IS_DATA_QUEUE (queue), FALSE);
  g_return_val_if_fail (item != NULL, FALSE);

  EGL_GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

  STATUS (queue, "before pushing");

  /* We ALWAYS need to check for queue fillness */
  if (egl_gst_data_queue_locked_is_full (queue)) {
    EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    if (G_LIKELY (priv->fullcallback))
      priv->fullcallback (queue, priv->checkdata);
    else
      g_signal_emit (queue, egl_gst_data_queue_signals[SIGNAL_FULL], 0);
    EGL_GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

    /* signal might have removed some items */
    while (egl_gst_data_queue_locked_is_full (queue)) {
      priv->waiting_del = TRUE;
      g_cond_wait (&priv->item_del, &priv->qlock);
      priv->waiting_del = FALSE;
      if (priv->flushing)
        goto flushing;
    }
  }

  egl_gst_queue_array_push_tail (priv->queue, item);

  if (item->visible)
    priv->cur_level.visible++;
  priv->cur_level.bytes += item->size;
  priv->cur_level.time += item->duration;

  STATUS (queue, "after pushing");
  if (priv->waiting_add)
    g_cond_signal (&priv->item_add);

  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return TRUE;

  /* ERRORS */
flushing:
  {
    GST_DEBUG ("queue:%p, we are flushing", queue);
    EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    return FALSE;
  }
}

/**
 * egl_gst_data_queue_pop:
 * @queue: a #EGLGstDataQueue.
 * @item: pointer to store the returned #EGLGstDataQueueItem.
 *
 * Retrieves the first @item available on the @queue. If the queue is currently
 * empty, the call will block until at least one item is available, OR the
 * @queue is set to the flushing state.
 * MT safe.
 *
 * Returns: #TRUE if an @item was successfully retrieved from the @queue.
 *
 * Since: 1.2.0
 */
gboolean
egl_gst_data_queue_pop (EGLGstDataQueue * queue, EGLGstDataQueueItem ** item)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  g_return_val_if_fail (EGL_GST_IS_DATA_QUEUE (queue), FALSE);
  g_return_val_if_fail (item != NULL, FALSE);

  EGL_GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

  STATUS (queue, "before popping");

  if (egl_gst_data_queue_locked_is_empty (queue)) {
    EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    if (G_LIKELY (priv->emptycallback))
      priv->emptycallback (queue, priv->checkdata);
    else
      g_signal_emit (queue, egl_gst_data_queue_signals[SIGNAL_EMPTY], 0);
    EGL_GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

    while (egl_gst_data_queue_locked_is_empty (queue)) {
      priv->waiting_add = TRUE;
      g_cond_wait (&priv->item_add, &priv->qlock);
      priv->waiting_add = FALSE;
      if (priv->flushing)
        goto flushing;
    }
  }

  /* Get the item from the GQueue */
  *item = egl_gst_queue_array_pop_head (priv->queue);

  /* update current level counter */
  if ((*item)->visible)
    priv->cur_level.visible--;
  priv->cur_level.bytes -= (*item)->size;
  priv->cur_level.time -= (*item)->duration;

  STATUS (queue, "after popping");
  if (priv->waiting_del)
    g_cond_signal (&priv->item_del);

  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return TRUE;

  /* ERRORS */
flushing:
  {
    GST_DEBUG ("queue:%p, we are flushing", queue);
    EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    return FALSE;
  }
}

static gint
is_of_type (gconstpointer a, gconstpointer b)
{
  return !G_TYPE_CHECK_INSTANCE_TYPE (a, GPOINTER_TO_SIZE (b));
}

/**
 * egl_gst_data_queue_drop_head:
 * @queue: The #EGLGstDataQueue to drop an item from.
 * @type: The #GType of the item to drop.
 *
 * Pop and unref the head-most #GstMiniObject with the given #GType.
 *
 * Returns: TRUE if an element was removed.
 *
 * Since: 1.2.0
 */
gboolean
egl_gst_data_queue_drop_head (EGLGstDataQueue * queue, GType type)
{
  gboolean res = FALSE;
  EGLGstDataQueueItem *leak = NULL;
  guint idx;
  EGLGstDataQueuePrivate *priv = queue->priv;

  g_return_val_if_fail (EGL_GST_IS_DATA_QUEUE (queue), FALSE);

  GST_DEBUG ("queue:%p", queue);

  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);
  idx = egl_gst_queue_array_find (priv->queue, is_of_type, GSIZE_TO_POINTER (type));

  if (idx == -1)
    goto done;

  leak = egl_gst_queue_array_drop_element (priv->queue, idx);

  if (leak->visible)
    priv->cur_level.visible--;
  priv->cur_level.bytes -= leak->size;
  priv->cur_level.time -= leak->duration;

  leak->destroy (leak);

  res = TRUE;

done:
  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  GST_DEBUG ("queue:%p , res:%d", queue, res);

  return res;
}

/**
 * egl_gst_data_queue_limits_changed:
 * @queue: The #EGLGstDataQueue 
 *
 * Inform the queue that the limits for the fullness check have changed and that
 * any blocking egl_gst_data_queue_push() should be unblocked to recheck the limts.
 *
 * Since: 1.2.0
 */
void
egl_gst_data_queue_limits_changed (EGLGstDataQueue * queue)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  g_return_if_fail (EGL_GST_IS_DATA_QUEUE (queue));

  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);
  if (priv->waiting_del) {
    GST_DEBUG ("signal del");
    g_cond_signal (&priv->item_del);
  }
  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}

/**
 * egl_gst_data_queue_get_level:
 * @queue: The #EGLGstDataQueue
 * @level: the location to store the result
 *
 * Get the current level of the queue.
 *
 * Since: 1.2.0
 */
void
egl_gst_data_queue_get_level (EGLGstDataQueue * queue, EGLGstDataQueueSize * level)
{
  EGLGstDataQueuePrivate *priv = queue->priv;

  memcpy (level, (&priv->cur_level), sizeof (EGLGstDataQueueSize));
}

static void
egl_gst_data_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
egl_gst_data_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  EGLGstDataQueue *queue = EGL_GST_DATA_QUEUE (object);
  EGLGstDataQueuePrivate *priv = queue->priv;

  EGL_GST_DATA_QUEUE_MUTEX_LOCK (queue);

  switch (prop_id) {
    case PROP_CUR_LEVEL_BYTES:
      g_value_set_uint (value, priv->cur_level.bytes);
      break;
    case PROP_CUR_LEVEL_VISIBLE:
      g_value_set_uint (value, priv->cur_level.visible);
      break;
    case PROP_CUR_LEVEL_TIME:
      g_value_set_uint64 (value, priv->cur_level.time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  EGL_GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}
