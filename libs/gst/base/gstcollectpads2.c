/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sourceforge.net>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * gstcollectpads2.c:
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
 * SECTION:gstcollectpads2
 * @short_description: manages a set of pads that operate in collect mode
 * @see_also:
 *
 * Manages a set of pads that operate in collect mode. This means that control
 * is given to the manager of this object when all pads have data.
 * <itemizedlist>
 *   <listitem><para>
 *     Collectpads are created with gst_collect_pads2_new(). A callback should then
 *     be installed with gst_collect_pads2_set_function ().
 *   </para></listitem>
 *   <listitem><para>
 *     Pads are added to the collection with gst_collect_pads2_add_pad()/
 *     gst_collect_pads2_remove_pad(). The pad
 *     has to be a sinkpad. The chain and event functions of the pad are
 *     overridden. The element_private of the pad is used to store
 *     private information for the collectpads.
 *   </para></listitem>
 *   <listitem><para>
 *     For each pad, data is queued in the _chain function or by
 *     performing a pull_range.
 *   </para></listitem>
 *   <listitem><para>
 *     When data is queued on all pads in waiting mode, the callback function is called.
 *   </para></listitem>
 *   <listitem><para>
 *     Data can be dequeued from the pad with the gst_collect_pads2_pop() method.
 *     One can peek at the data with the gst_collect_pads2_peek() function.
 *     These functions will return NULL if the pad received an EOS event. When all
 *     pads return NULL from a gst_collect_pads2_peek(), the element can emit an EOS
 *     event itself.
 *   </para></listitem>
 *   <listitem><para>
 *     Data can also be dequeued in byte units using the gst_collect_pads2_available(),
 *     gst_collect_pads2_read() and gst_collect_pads2_flush() calls.
 *   </para></listitem>
 *   <listitem><para>
 *     Elements should call gst_collect_pads2_start() and gst_collect_pads2_stop() in
 *     their state change functions to start and stop the processing of the collectpads.
 *     The gst_collect_pads2_stop() call should be called before calling the parent
 *     element state change function in the PAUSED_TO_READY state change to ensure
 *     no pad is blocked and the element can finish streaming.
 *   </para></listitem>
 *   <listitem><para>
 *     gst_collect_pads2_collect() and gst_collect_pads2_collect_range() can be used by
 *     elements that start a #GstTask to drive the collect_pads2. This feature is however
 *     not yet implemented.
 *   </para></listitem>
 *   <listitem><para>
 *     gst_collect_pads2_set_waiting() sets a pad to waiting or non-waiting mode.
 *     CollectPads element is not waiting for data to be collected on non-waiting pads.
 *     Thus these pads may but need not have data when the callback is called.
 *     All pads are in waiting mode by default.
 *   </para></listitem>
 * </itemizedlist>
 *
 * Last reviewed on 2011-10-28 (0.10.36)
 *
 * Since: 0.10.36
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS
#include <gst/gst_private.h>

#include "gstcollectpads2.h"

#include "../../../gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (collect_pads2_debug);
#define GST_CAT_DEFAULT collect_pads2_debug

#define parent_class gst_collect_pads2_parent_class
G_DEFINE_TYPE (GstCollectPads2, gst_collect_pads2, GST_TYPE_OBJECT);

struct _GstCollectData2Private
{
  /* refcounting for struct, and destroy callback */
  GstCollectData2DestroyNotify destroy_notify;
  gint refcount;
};

struct _GstCollectPads2Private
{
  /* with LOCK and/or STREAM_LOCK */
  gboolean started;

  /* with STREAM_LOCK */
  guint32 cookie;               /* @data list cookie */
  guint numpads;                /* number of pads in @data */
  guint queuedpads;             /* number of pads with a buffer */
  guint eospads;                /* number of pads that are EOS */
  GstClockTime earliest_time;   /* Current earliest time */
  GstCollectData2 *earliest_data;       /* Pad data for current earliest time */

  /* with LOCK */
  GSList *pad_list;             /* updated pad list */
  guint32 pad_cookie;           /* updated cookie */

  GstCollectPads2Function func; /* function and user_data for callback */
  gpointer user_data;
  GstCollectPads2BufferFunction buffer_func;    /* function and user_data for buffer callback */
  gpointer buffer_user_data;
  GstCollectPads2CompareFunction compare_func;
  gpointer compare_user_data;
  GstCollectPads2EventFunction event_func;      /* function and data for event callback */
  gpointer event_user_data;
  GstCollectPads2ClipFunction clip_func;
  gpointer clip_user_data;

  /* no other lock needed */
  GMutex *evt_lock;             /* these make up sort of poor man's event signaling */
  GCond *evt_cond;
  guint32 evt_cookie;
};

static void gst_collect_pads2_clear (GstCollectPads2 * pads,
    GstCollectData2 * data);
static GstFlowReturn gst_collect_pads2_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_collect_pads2_event (GstPad * pad, GstEvent * event);
static void gst_collect_pads2_finalize (GObject * object);
static GstFlowReturn gst_collect_pads2_default_collected (GstCollectPads2 *
    pads, gpointer user_data);
static gint gst_collect_pads2_default_compare_func (GstCollectPads2 * pads,
    GstCollectData2 * data1, GstClockTime timestamp1, GstCollectData2 * data2,
    GstClockTime timestamp2, gpointer user_data);
static gboolean gst_collect_pads2_recalculate_full (GstCollectPads2 * pads);
static void ref_data (GstCollectData2 * data);
static void unref_data (GstCollectData2 * data);

/* Some properties are protected by LOCK, others by STREAM_LOCK
 * However, manipulating either of these partitions may require
 * to signal/wake a _WAIT, so use a separate (sort of) event to prevent races
 * Alternative implementations are possible, e.g. some low-level re-implementing
 * of the 2 above locks to drop both of them atomically when going into _WAIT.
 */
#define GST_COLLECT_PADS2_GET_EVT_COND(pads) (((GstCollectPads2 *)pads)->priv->evt_cond)
#define GST_COLLECT_PADS2_GET_EVT_LOCK(pads) (((GstCollectPads2 *)pads)->priv->evt_lock)
#define GST_COLLECT_PADS2_EVT_WAIT(pads, cookie) G_STMT_START {    \
  g_mutex_lock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));            \
  /* should work unless a lot of event'ing and thread starvation */\
  while (cookie == ((GstCollectPads2 *) pads)->priv->evt_cookie)         \
    g_cond_wait (GST_COLLECT_PADS2_GET_EVT_COND (pads),            \
        GST_COLLECT_PADS2_GET_EVT_LOCK (pads));                    \
  cookie = ((GstCollectPads2 *) pads)->priv->evt_cookie;                 \
  g_mutex_unlock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));          \
} G_STMT_END
#define GST_COLLECT_PADS2_EVT_WAIT_TIMED(pads, cookie, timeout) G_STMT_START { \
  GTimeVal __tv; \
  \
  g_get_current_time (&tv); \
  g_time_val_add (&tv, timeout); \
  \
  g_mutex_lock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));            \
  /* should work unless a lot of event'ing and thread starvation */\
  while (cookie == ((GstCollectPads2 *) pads)->priv->evt_cookie)         \
    g_cond_timed_wait (GST_COLLECT_PADS2_GET_EVT_COND (pads),            \
        GST_COLLECT_PADS2_GET_EVT_LOCK (pads), &tv);                    \
  cookie = ((GstCollectPads2 *) pads)->priv->evt_cookie;                 \
  g_mutex_unlock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));          \
} G_STMT_END
#define GST_COLLECT_PADS2_EVT_BROADCAST(pads) G_STMT_START {       \
  g_mutex_lock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));            \
  /* never mind wrap-around */                                     \
  ++(((GstCollectPads2 *) pads)->priv->evt_cookie);                      \
  g_cond_broadcast (GST_COLLECT_PADS2_GET_EVT_COND (pads));        \
  g_mutex_unlock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));          \
} G_STMT_END
#define GST_COLLECT_PADS2_EVT_INIT(cookie) G_STMT_START {          \
  g_mutex_lock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));            \
  cookie = ((GstCollectPads2 *) pads)->priv->evt_cookie;                 \
  g_mutex_unlock (GST_COLLECT_PADS2_GET_EVT_LOCK (pads));          \
} G_STMT_END

static void
gst_collect_pads2_class_init (GstCollectPads2Class * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstCollectPads2Private));

  GST_DEBUG_CATEGORY_INIT (collect_pads2_debug, "collectpads2", 0,
      "GstCollectPads2");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_collect_pads2_finalize);
}

static void
gst_collect_pads2_init (GstCollectPads2 * pads)
{
  pads->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pads, GST_TYPE_COLLECT_PADS2,
      GstCollectPads2Private);

  pads->data = NULL;
  pads->priv->cookie = 0;
  pads->priv->numpads = 0;
  pads->priv->queuedpads = 0;
  pads->priv->eospads = 0;
  pads->priv->started = FALSE;

  g_static_rec_mutex_init (&pads->stream_lock);

  pads->priv->func = gst_collect_pads2_default_collected;
  pads->priv->user_data = NULL;
  pads->priv->event_func = NULL;
  pads->priv->event_user_data = NULL;

  /* members for default muxing */
  pads->priv->buffer_func = NULL;
  pads->priv->buffer_user_data = NULL;
  pads->priv->compare_func = gst_collect_pads2_default_compare_func;
  pads->priv->compare_user_data = NULL;
  pads->priv->earliest_data = NULL;
  pads->priv->earliest_time = GST_CLOCK_TIME_NONE;

  /* members to manage the pad list */
  pads->priv->pad_cookie = 0;
  pads->priv->pad_list = NULL;

  /* members for event */
  pads->priv->evt_lock = g_mutex_new ();
  pads->priv->evt_cond = g_cond_new ();
  pads->priv->evt_cookie = 0;
}

static void
gst_collect_pads2_finalize (GObject * object)
{
  GstCollectPads2 *pads = GST_COLLECT_PADS2 (object);

  GST_DEBUG_OBJECT (object, "finalize");

  g_static_rec_mutex_free (&pads->stream_lock);

  g_cond_free (pads->priv->evt_cond);
  g_mutex_free (pads->priv->evt_lock);

  /* Remove pads and free pads list */
  g_slist_foreach (pads->priv->pad_list, (GFunc) unref_data, NULL);
  g_slist_foreach (pads->data, (GFunc) unref_data, NULL);
  g_slist_free (pads->data);
  g_slist_free (pads->priv->pad_list);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_collect_pads2_new:
 *
 * Create a new instance of #GstCollectsPads.
 *
 * Returns: a new #GstCollectPads2, or NULL in case of an error.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
GstCollectPads2 *
gst_collect_pads2_new (void)
{
  GstCollectPads2 *newcoll;

  newcoll = g_object_new (GST_TYPE_COLLECT_PADS2, NULL);

  return newcoll;
}

/* Must be called with GstObject lock! */
static void
gst_collect_pads2_set_buffer_function_locked (GstCollectPads2 * pads,
    GstCollectPads2BufferFunction func, gpointer user_data)
{
  pads->priv->buffer_func = func;
  pads->priv->buffer_user_data = user_data;
}

/**
 * gst_collect_pads2_set_buffer_function:
 * @pads: the collectpads to use
 * @func: the function to set
 * @user_data: user data passed to the function
 *
 * Set the callback function and user data that will be called with
 * the oldest buffer when all pads have been collected.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_set_buffer_function (GstCollectPads2 * pads,
    GstCollectPads2BufferFunction func, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  GST_OBJECT_LOCK (pads);
  gst_collect_pads2_set_buffer_function_locked (pads, func, user_data);
  GST_OBJECT_UNLOCK (pads);
}

/**
 * gst_collect_pads2_set_compare_function:
 * @pads: the pads to use
 * @func: the function to set
 * @user_data: user data passed to the function
 *
 * Set the timestamp comparison function.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
/* NOTE allowing to change comparison seems not advisable;
no known use-case, and collaboration with default algorithm is unpredictable.
If custom compairing/operation is needed, just use a collect function of
your own */
void
gst_collect_pads2_set_compare_function (GstCollectPads2 * pads,
    GstCollectPads2CompareFunction func, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  GST_OBJECT_LOCK (pads);
  pads->priv->compare_func = func;
  pads->priv->compare_user_data = user_data;
  GST_OBJECT_UNLOCK (pads);
}

/**
 * gst_collect_pads2_set_function:
 * @pads: the collectspads to use
 * @func: the function to set
 * @user_data: user data passed to the function
 *
 * CollectPads provides a default collection algorithm that will determine
 * the oldest buffer available on all of its pads, and then delegate
 * to a configured callback.
 * However, if circumstances are more complicated and/or more control
 * is desired, this sets a callback that will be invoked instead when
 * all the pads added to the collection have buffers queued.
 * Evidently, this callback is not compatible with
 * gst_collect_pads2_set_buffer_function() callback.
 * If this callback is set, the former will be unset.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_set_function (GstCollectPads2 * pads,
    GstCollectPads2Function func, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  GST_OBJECT_LOCK (pads);
  pads->priv->func = func;
  pads->priv->user_data = user_data;
  gst_collect_pads2_set_buffer_function_locked (pads, NULL, NULL);
  GST_OBJECT_UNLOCK (pads);
}

static void
ref_data (GstCollectData2 * data)
{
  g_assert (data != NULL);

  g_atomic_int_inc (&(data->priv->refcount));
}

static void
unref_data (GstCollectData2 * data)
{
  g_assert (data != NULL);
  g_assert (data->priv->refcount > 0);

  if (!g_atomic_int_dec_and_test (&(data->priv->refcount)))
    return;

  if (data->priv->destroy_notify)
    data->priv->destroy_notify (data);

  g_object_unref (data->pad);
  if (data->buffer) {
    gst_buffer_unref (data->buffer);
  }
  g_free (data->priv);
  g_free (data);
}

/**
 * gst_collect_pads2_set_event_function:
 * @pads: the collectspads to use
 * @func: the function to set
 * @user_data: user data passed to the function
 *
 * Set the event callback function and user data that will be called after
 * collectpads has processed and event originating from one of the collected
 * pads.  If the event being processed is a serialized one, this callback is
 * called with @pads STREAM_LOCK held, otherwise not.  As this lock should be
 * held when calling a number of CollectPads functions, it should be acquired
 * if so (unusually) needed.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_set_event_function (GstCollectPads2 * pads,
    GstCollectPads2EventFunction func, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  GST_OBJECT_LOCK (pads);
  pads->priv->event_func = func;
  pads->priv->event_user_data = user_data;
  GST_OBJECT_UNLOCK (pads);
}

 /**
 * gst_collect_pads2_set_clip_function:
 * @pads: the collectspads to use
 * @clipfunc: clip function to install
 * @user_data: user data to pass to @clip_func
 *
 * Install a clipping function that is called right after a buffer is received
 * on a pad managed by @pads. See #GstCollectPad2ClipFunction for more info.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_set_clip_function (GstCollectPads2 * pads,
    GstCollectPads2ClipFunction clipfunc, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  pads->priv->clip_func = clipfunc;
  pads->priv->clip_user_data = user_data;
}

/**
 * gst_collect_pads2_add_pad:
 * @pads: the collectspads to use
 * @pad: the pad to add
 * @size: the size of the returned #GstCollectData2 structure
 *
 * Add a pad to the collection of collect pads. The pad has to be
 * a sinkpad. The refcount of the pad is incremented. Use
 * gst_collect_pads2_remove_pad() to remove the pad from the collection
 * again.
 *
 * You specify a size for the returned #GstCollectData2 structure
 * so that you can use it to store additional information.
 *
 * The pad will be automatically activated in push mode when @pads is
 * started.
 *
 * This function calls gst_collect_pads2_add_pad() passing a value of NULL
 * for destroy_notify and TRUE for locked.
 *
 * Returns: a new #GstCollectData2 to identify the new pad. Or NULL
 *   if wrong parameters are supplied.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
GstCollectData2 *
gst_collect_pads2_add_pad (GstCollectPads2 * pads, GstPad * pad, guint size)
{
  return gst_collect_pads2_add_pad_full (pads, pad, size, NULL, TRUE);
}

/**
 * gst_collect_pads2_add_pad_full:
 * @pads: the collectspads to use
 * @pad: the pad to add
 * @size: the size of the returned #GstCollectData2 structure
 * @destroy_notify: function to be called before the returned #GstCollectData2
 * structure is freed
 * @lock: whether to lock this pad in usual waiting state
 *
 * Add a pad to the collection of collect pads. The pad has to be
 * a sinkpad. The refcount of the pad is incremented. Use
 * gst_collect_pads2_remove_pad() to remove the pad from the collection
 * again.
 *
 * You specify a size for the returned #GstCollectData2 structure
 * so that you can use it to store additional information.
 *
 * You can also specify a #GstCollectData2DestroyNotify that will be called
 * just before the #GstCollectData2 structure is freed. It is passed the
 * pointer to the structure and should free any custom memory and resources
 * allocated for it.
 *
 * Keeping a pad locked in waiting state is only relevant when using
 * the default collection algorithm (providing the oldest buffer).
 * It ensures a buffer must be available on this pad for a collection
 * to take place.  This is of typical use to a muxer element where
 * non-subtitle streams should always be in waiting state,
 * e.g. to assure that caps information is available on all these streams
 * when initial headers have to be written.
 *
 * The pad will be automatically activated in push mode when @pads is
 * started.
 *
 * Since: 0.10.36
 *
 * Returns: a new #GstCollectData2 to identify the new pad. Or NULL
 *   if wrong parameters are supplied.
 *
 * MT safe.
 */
GstCollectData2 *
gst_collect_pads2_add_pad_full (GstCollectPads2 * pads, GstPad * pad,
    guint size, GstCollectData2DestroyNotify destroy_notify, gboolean lock)
{
  GstCollectData2 *data;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), NULL);
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_PAD_IS_SINK (pad), NULL);
  g_return_val_if_fail (size >= sizeof (GstCollectData2), NULL);

  GST_DEBUG_OBJECT (pads, "adding pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  data = g_malloc0 (size);
  data->priv = g_new0 (GstCollectData2Private, 1);
  data->collect = pads;
  data->pad = gst_object_ref (pad);
  data->buffer = NULL;
  data->pos = 0;
  gst_segment_init (&data->segment, GST_FORMAT_UNDEFINED);
  data->state = GST_COLLECT_PADS2_STATE_WAITING;
  data->state |= lock ? GST_COLLECT_PADS2_STATE_LOCKED : 0;
  data->priv->refcount = 1;
  data->priv->destroy_notify = destroy_notify;

  GST_OBJECT_LOCK (pads);
  GST_OBJECT_LOCK (pad);
  gst_pad_set_element_private (pad, data);
  GST_OBJECT_UNLOCK (pad);
  pads->priv->pad_list = g_slist_append (pads->priv->pad_list, data);
  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_collect_pads2_chain));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_collect_pads2_event));
  /* backward compat, also add to data if stopped, so that the element already
   * has this in the public data list before going PAUSED (typically)
   * this can only be done when we are stopped because we don't take the
   * STREAM_LOCK to protect the pads->data list. */
  if (!pads->priv->started) {
    pads->data = g_slist_append (pads->data, data);
    ref_data (data);
  }
  /* activate the pad when needed */
  if (pads->priv->started)
    gst_pad_set_active (pad, TRUE);
  pads->priv->pad_cookie++;
  GST_OBJECT_UNLOCK (pads);

  return data;
}

static gint
find_pad (GstCollectData2 * data, GstPad * pad)
{
  if (data->pad == pad)
    return 0;
  return 1;
}

/**
 * gst_collect_pads2_remove_pad:
 * @pads: the collectspads to use
 * @pad: the pad to remove
 *
 * Remove a pad from the collection of collect pads. This function will also
 * free the #GstCollectData2 and all the resources that were allocated with
 * gst_collect_pads2_add_pad().
 *
 * The pad will be deactivated automatically when @pads is stopped.
 *
 * Returns: %TRUE if the pad could be removed.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_collect_pads2_remove_pad (GstCollectPads2 * pads, GstPad * pad)
{
  GstCollectData2 *data;
  GSList *list;

  g_return_val_if_fail (pads != NULL, FALSE);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_DEBUG_OBJECT (pads, "removing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_OBJECT_LOCK (pads);
  list =
      g_slist_find_custom (pads->priv->pad_list, pad, (GCompareFunc) find_pad);
  if (!list)
    goto unknown_pad;

  data = (GstCollectData2 *) list->data;

  GST_DEBUG_OBJECT (pads, "found pad %s:%s at %p", GST_DEBUG_PAD_NAME (pad),
      data);

  /* clear the stuff we configured */
  gst_pad_set_chain_function (pad, NULL);
  gst_pad_set_event_function (pad, NULL);
  GST_OBJECT_LOCK (pad);
  gst_pad_set_element_private (pad, NULL);
  GST_OBJECT_UNLOCK (pad);

  /* backward compat, also remove from data if stopped, note that this function
   * can only be called when we are stopped because we don't take the
   * STREAM_LOCK to protect the pads->data list. */
  if (!pads->priv->started) {
    GSList *dlist;

    dlist = g_slist_find_custom (pads->data, pad, (GCompareFunc) find_pad);
    if (dlist) {
      GstCollectData2 *pdata = dlist->data;

      pads->data = g_slist_delete_link (pads->data, dlist);
      unref_data (pdata);
    }
  }
  /* remove from the pad list */
  pads->priv->pad_list = g_slist_delete_link (pads->priv->pad_list, list);
  pads->priv->pad_cookie++;

  /* signal waiters because something changed */
  GST_COLLECT_PADS2_EVT_BROADCAST (pads);

  /* deactivate the pad when needed */
  if (!pads->priv->started)
    gst_pad_set_active (pad, FALSE);

  /* clean and free the collect data */
  unref_data (data);

  GST_OBJECT_UNLOCK (pads);

  return TRUE;

unknown_pad:
  {
    GST_WARNING_OBJECT (pads, "cannot remove unknown pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pads);
    return FALSE;
  }
}

/**
 * gst_collect_pads2_is_active:
 * @pads: the collectspads to use
 * @pad: the pad to check
 *
 * Check if a pad is active.
 *
 * This function is currently not implemented.
 *
 * Returns: %TRUE if the pad is active.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_collect_pads2_is_active (GstCollectPads2 * pads, GstPad * pad)
{
  g_return_val_if_fail (pads != NULL, FALSE);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  g_warning ("gst_collect_pads2_is_active() is not implemented");

  return FALSE;
}

/**
 * gst_collect_pads2_collect:
 * @pads: the collectspads to use
 *
 * Collect data on all pads. This function is usually called
 * from a #GstTask function in an element. 
 *
 * This function is currently not implemented.
 *
 * Returns: #GstFlowReturn of the operation.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_collect_pads2_collect (GstCollectPads2 * pads)
{
  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), GST_FLOW_ERROR);

  g_warning ("gst_collect_pads2_collect() is not implemented");

  return GST_FLOW_NOT_SUPPORTED;
}

/**
 * gst_collect_pads2_collect_range:
 * @pads: the collectspads to use
 * @offset: the offset to collect
 * @length: the length to collect
 *
 * Collect data with @offset and @length on all pads. This function
 * is typically called in the getrange function of an element. 
 *
 * This function is currently not implemented.
 *
 * Returns: #GstFlowReturn of the operation.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_collect_pads2_collect_range (GstCollectPads2 * pads, guint64 offset,
    guint length)
{
  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), GST_FLOW_ERROR);

  g_warning ("gst_collect_pads2_collect_range() is not implemented");

  return GST_FLOW_NOT_SUPPORTED;
}

/*
 * Must be called with STREAM_LOCK.
 */
static void
gst_collect_pads2_set_flushing_unlocked (GstCollectPads2 * pads,
    gboolean flushing)
{
  GSList *walk = NULL;

  /* Update the pads flushing flag */
  for (walk = pads->data; walk; walk = g_slist_next (walk)) {
    GstCollectData2 *cdata = walk->data;

    if (GST_IS_PAD (cdata->pad)) {
      GST_OBJECT_LOCK (cdata->pad);
      if (flushing)
        GST_PAD_SET_FLUSHING (cdata->pad);
      else
        GST_PAD_UNSET_FLUSHING (cdata->pad);
      if (flushing)
        GST_COLLECT_PADS2_STATE_SET (cdata, GST_COLLECT_PADS2_STATE_FLUSHING);
      else
        GST_COLLECT_PADS2_STATE_UNSET (cdata, GST_COLLECT_PADS2_STATE_FLUSHING);
      gst_collect_pads2_clear (pads, cdata);
      GST_OBJECT_UNLOCK (cdata->pad);
    }
  }

  /* inform _chain of changes */
  GST_COLLECT_PADS2_EVT_BROADCAST (pads);
}

/**
 * gst_collect_pads2_set_flushing:
 * @pads: the collectspads to use
 * @flushing: desired state of the pads
 *
 * Change the flushing state of all the pads in the collection. No pad
 * is able to accept anymore data when @flushing is %TRUE. Calling this
 * function with @flushing %FALSE makes @pads accept data again.
 * Caller must ensure that downstream streaming (thread) is not blocked,
 * e.g. by sending a FLUSH_START downstream.
 *
 * MT safe.
 *
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_set_flushing (GstCollectPads2 * pads, gboolean flushing)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  /* NOTE since this eventually calls _pop, some (STREAM_)LOCK is needed here */
  GST_COLLECT_PADS2_STREAM_LOCK (pads);
  gst_collect_pads2_set_flushing_unlocked (pads, flushing);
  GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
}

/**
 * gst_collect_pads2_start:
 * @pads: the collectspads to use
 *
 * Starts the processing of data in the collect_pads2.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_start (GstCollectPads2 * pads)
{
  GSList *collected;

  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  GST_DEBUG_OBJECT (pads, "starting collect pads");

  /* make sure stop and collect cannot be called anymore */
  GST_COLLECT_PADS2_STREAM_LOCK (pads);

  /* make pads streamable */
  GST_OBJECT_LOCK (pads);

  /* loop over the master pad list and reset the segment */
  collected = pads->priv->pad_list;
  for (; collected; collected = g_slist_next (collected)) {
    GstCollectData2 *data;

    data = collected->data;
    gst_segment_init (&data->segment, GST_FORMAT_UNDEFINED);
  }

  gst_collect_pads2_set_flushing_unlocked (pads, FALSE);

  /* Start collect pads */
  pads->priv->started = TRUE;
  GST_OBJECT_UNLOCK (pads);
  GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
}

/**
 * gst_collect_pads2_stop:
 * @pads: the collectspads to use
 *
 * Stops the processing of data in the collect_pads2. this function
 * will also unblock any blocking operations.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_stop (GstCollectPads2 * pads)
{
  GSList *collected;

  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));

  GST_DEBUG_OBJECT (pads, "stopping collect pads");

  /* make sure collect and start cannot be called anymore */
  GST_COLLECT_PADS2_STREAM_LOCK (pads);

  /* make pads not accept data anymore */
  GST_OBJECT_LOCK (pads);
  gst_collect_pads2_set_flushing_unlocked (pads, TRUE);

  /* Stop collect pads */
  pads->priv->started = FALSE;
  pads->priv->eospads = 0;
  pads->priv->queuedpads = 0;

  /* loop over the master pad list and flush buffers */
  collected = pads->priv->pad_list;
  for (; collected; collected = g_slist_next (collected)) {
    GstCollectData2 *data;
    GstBuffer **buffer_p;

    data = collected->data;
    if (data->buffer) {
      buffer_p = &data->buffer;
      gst_buffer_replace (buffer_p, NULL);
      data->pos = 0;
    }
    GST_COLLECT_PADS2_STATE_UNSET (data, GST_COLLECT_PADS2_STATE_EOS);
  }

  if (pads->priv->earliest_data)
    unref_data (pads->priv->earliest_data);
  pads->priv->earliest_data = NULL;
  pads->priv->earliest_time = GST_CLOCK_TIME_NONE;

  GST_OBJECT_UNLOCK (pads);
  /* Wake them up so they can end the chain functions. */
  GST_COLLECT_PADS2_EVT_BROADCAST (pads);

  GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
}

/**
 * gst_collect_pads2_peek:
 * @pads: the collectspads to peek
 * @data: the data to use
 *
 * Peek at the buffer currently queued in @data. This function
 * should be called with the @pads STREAM_LOCK held, such as in the callback
 * handler.
 *
 * Returns: The buffer in @data or NULL if no buffer is queued.
 *  should unref the buffer after usage.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
GstBuffer *
gst_collect_pads2_peek (GstCollectPads2 * pads, GstCollectData2 * data)
{
  GstBuffer *result;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  if ((result = data->buffer))
    gst_buffer_ref (result);

  GST_DEBUG_OBJECT (pads, "Peeking at pad %s:%s: buffer=%p",
      GST_DEBUG_PAD_NAME (data->pad), result);

  return result;
}

/**
 * gst_collect_pads2_pop:
 * @pads: the collectspads to pop
 * @data: the data to use
 *
 * Pop the buffer currently queued in @data. This function
 * should be called with the @pads STREAM_LOCK held, such as in the callback
 * handler.
 *
 * Returns: The buffer in @data or NULL if no buffer was queued.
 *   You should unref the buffer after usage.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
GstBuffer *
gst_collect_pads2_pop (GstCollectPads2 * pads, GstCollectData2 * data)
{
  GstBuffer *result;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  if ((result = data->buffer)) {
    data->buffer = NULL;
    data->pos = 0;
    /* one less pad with queued data now */
    if (GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_WAITING))
      pads->priv->queuedpads--;
  }

  GST_COLLECT_PADS2_EVT_BROADCAST (pads);

  GST_DEBUG_OBJECT (pads, "Pop buffer on pad %s:%s: buffer=%p",
      GST_DEBUG_PAD_NAME (data->pad), result);

  return result;
}

/* pop and unref the currently queued buffer, should be called with STREAM_LOCK
 * held */
static void
gst_collect_pads2_clear (GstCollectPads2 * pads, GstCollectData2 * data)
{
  GstBuffer *buf;

  if ((buf = gst_collect_pads2_pop (pads, data)))
    gst_buffer_unref (buf);
}

/**
 * gst_collect_pads2_available:
 * @pads: the collectspads to query
 *
 * Query how much bytes can be read from each queued buffer. This means
 * that the result of this call is the maximum number of bytes that can
 * be read from each of the pads.
 *
 * This function should be called with @pads STREAM_LOCK held, such as
 * in the callback.
 *
 * Returns: The maximum number of bytes queued on all pads. This function
 * returns 0 if a pad has no queued buffer.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
/* we might pre-calculate this in some struct field,
 * but would then have to maintain this in _chain and particularly _pop, etc,
 * even if element is never interested in this information */
guint
gst_collect_pads2_available (GstCollectPads2 * pads)
{
  GSList *collected;
  guint result = G_MAXUINT;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), 0);

  collected = pads->data;
  for (; collected; collected = g_slist_next (collected)) {
    GstCollectData2 *pdata;
    GstBuffer *buffer;
    gint size;

    pdata = (GstCollectData2 *) collected->data;

    /* ignore pad with EOS */
    if (G_UNLIKELY (GST_COLLECT_PADS2_STATE_IS_SET (pdata,
                GST_COLLECT_PADS2_STATE_EOS))) {
      GST_DEBUG_OBJECT (pads, "pad %p is EOS", pdata);
      continue;
    }

    /* an empty buffer without EOS is weird when we get here.. */
    if (G_UNLIKELY ((buffer = pdata->buffer) == NULL)) {
      GST_WARNING_OBJECT (pads, "pad %p has no buffer", pdata);
      goto not_filled;
    }

    /* this is the size left of the buffer */
    size = GST_BUFFER_SIZE (buffer) - pdata->pos;
    GST_DEBUG_OBJECT (pads, "pad %p has %d bytes left", pdata, size);

    /* need to return the min of all available data */
    if (size < result)
      result = size;
  }
  /* nothing changed, all must be EOS then, return 0 */
  if (G_UNLIKELY (result == G_MAXUINT))
    result = 0;

  return result;

not_filled:
  {
    return 0;
  }
}

/**
 * gst_collect_pads2_read:
 * @pads: the collectspads to query
 * @data: the data to use
 * @bytes: a pointer to a byte array
 * @size: the number of bytes to read
 *
 * Get a pointer in @bytes where @size bytes can be read from the
 * given pad data.
 *
 * This function should be called with @pads STREAM_LOCK held, such as
 * in the callback.
 *
 * Returns: The number of bytes available for consumption in the
 * memory pointed to by @bytes. This can be less than @size and
 * is 0 if the pad is end-of-stream.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
guint
gst_collect_pads2_read (GstCollectPads2 * pads, GstCollectData2 * data,
    guint8 ** bytes, guint size)
{
  guint readsize;
  GstBuffer *buffer;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), 0);
  g_return_val_if_fail (data != NULL, 0);
  g_return_val_if_fail (bytes != NULL, 0);

  /* no buffer, must be EOS */
  if ((buffer = data->buffer) == NULL)
    return 0;

  readsize = MIN (size, GST_BUFFER_SIZE (buffer) - data->pos);

  *bytes = GST_BUFFER_DATA (buffer) + data->pos;

  return readsize;
}

/**
 * gst_collect_pads2_flush:
 * @pads: the collectspads to query
 * @data: the data to use
 * @size: the number of bytes to flush
 *
 * Flush @size bytes from the pad @data.
 *
 * This function should be called with @pads STREAM_LOCK held, such as
 * in the callback.
 *
 * Returns: The number of bytes flushed This can be less than @size and
 * is 0 if the pad was end-of-stream.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
guint
gst_collect_pads2_flush (GstCollectPads2 * pads, GstCollectData2 * data,
    guint size)
{
  guint flushsize;
  GstBuffer *buffer;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), 0);
  g_return_val_if_fail (data != NULL, 0);

  /* no buffer, must be EOS */
  if ((buffer = data->buffer) == NULL)
    return 0;

  /* this is what we can flush at max */
  flushsize = MIN (size, GST_BUFFER_SIZE (buffer) - data->pos);

  data->pos += size;

  if (data->pos >= GST_BUFFER_SIZE (buffer))
    /* _clear will also reset data->pos to 0 */
    gst_collect_pads2_clear (pads, data);

  return flushsize;
}

/**
 * gst_collect_pads2_read_buffer:
 * @pads: the collectspads to query
 * @data: the data to use
 * @size: the number of bytes to read
 *
 * Get a subbuffer of @size bytes from the given pad @data.
 *
 * This function should be called with @pads STREAM_LOCK held, such as in the
 * callback.
 *
 * Since: 0.10.36
 *
 * Returns: A sub buffer. The size of the buffer can be less that requested.
 * A return of NULL signals that the pad is end-of-stream.
 * Unref the buffer after use.
 *
 * MT safe.
 */
GstBuffer *
gst_collect_pads2_read_buffer (GstCollectPads2 * pads, GstCollectData2 * data,
    guint size)
{
  guint readsize;
  GstBuffer *buffer;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  /* no buffer, must be EOS */
  if ((buffer = data->buffer) == NULL)
    return NULL;

  readsize = MIN (size, GST_BUFFER_SIZE (buffer) - data->pos);

  return gst_buffer_create_sub (buffer, data->pos, readsize);
}

/**
 * gst_collect_pads2_take_buffer:
 * @pads: the collectspads to query
 * @data: the data to use
 * @size: the number of bytes to read
 *
 * Get a subbuffer of @size bytes from the given pad @data. Flushes the amount
 * of read bytes.
 *
 * This function should be called with @pads STREAM_LOCK held, such as in the
 * callback.
 *
 * Since: 0.10.36
 *
 * Returns: A sub buffer. The size of the buffer can be less that requested.
 * A return of NULL signals that the pad is end-of-stream.
 * Unref the buffer after use.
 *
 * MT safe.
 */
GstBuffer *
gst_collect_pads2_take_buffer (GstCollectPads2 * pads, GstCollectData2 * data,
    guint size)
{
  GstBuffer *buffer = gst_collect_pads2_read_buffer (pads, data, size);

  if (buffer) {
    gst_collect_pads2_flush (pads, data, GST_BUFFER_SIZE (buffer));
  }
  return buffer;
}

/**
 * gst_collect_pads2_set_waiting:
 * @pads: the collectspads
 * @data: the data to use
 * @waiting: boolean indicating whether this pad should operate
 *           in waiting or non-waiting mode
 *
 * Sets a pad to waiting or non-waiting mode, if at least this pad
 * has not been created with locked waiting state,
 * in which case nothing happens.
 *
 * This function should be called with @pads STREAM_LOCK held, such as
 * in the callback.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_collect_pads2_set_waiting (GstCollectPads2 * pads, GstCollectData2 * data,
    gboolean waiting)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS2 (pads));
  g_return_if_fail (data != NULL);

  GST_DEBUG_OBJECT (pads, "Setting pad %s to waiting %d, locked %d",
      GST_PAD_NAME (data->pad), waiting,
      GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_LOCKED));

  /* Do something only on a change and if not locked */
  if (!GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_LOCKED) &&
      (GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_WAITING) !=
          ! !waiting)) {
    /* Set waiting state for this pad */
    if (waiting)
      GST_COLLECT_PADS2_STATE_SET (data, GST_COLLECT_PADS2_STATE_WAITING);
    else
      GST_COLLECT_PADS2_STATE_UNSET (data, GST_COLLECT_PADS2_STATE_WAITING);
    /* Update number of queued pads if needed */
    if (!data->buffer &&
        !GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_EOS)) {
      if (waiting)
        pads->priv->queuedpads--;
      else
        pads->priv->queuedpads++;
    }

    /* signal waiters because something changed */
    GST_COLLECT_PADS2_EVT_BROADCAST (pads);
  }
}

/* see if pads were added or removed and update our stats. Any pad
 * added after releasing the LOCK will get collected in the next
 * round.
 *
 * We can do a quick check by checking the cookies, that get changed
 * whenever the pad list is updated.
 *
 * Must be called with STREAM_LOCK.
 */
static void
gst_collect_pads2_check_pads (GstCollectPads2 * pads)
{
  /* the master list and cookie are protected with LOCK */
  GST_OBJECT_LOCK (pads);
  if (G_UNLIKELY (pads->priv->pad_cookie != pads->priv->cookie)) {
    GSList *collected;

    /* clear list and stats */
    g_slist_foreach (pads->data, (GFunc) unref_data, NULL);
    g_slist_free (pads->data);
    pads->data = NULL;
    pads->priv->numpads = 0;
    pads->priv->queuedpads = 0;
    pads->priv->eospads = 0;
    if (pads->priv->earliest_data)
      unref_data (pads->priv->earliest_data);
    pads->priv->earliest_data = NULL;
    pads->priv->earliest_time = GST_CLOCK_TIME_NONE;

    /* loop over the master pad list */
    collected = pads->priv->pad_list;
    for (; collected; collected = g_slist_next (collected)) {
      GstCollectData2 *data;

      /* update the stats */
      pads->priv->numpads++;
      data = collected->data;
      if (GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_EOS))
        pads->priv->eospads++;
      else if (data->buffer || !GST_COLLECT_PADS2_STATE_IS_SET (data,
              GST_COLLECT_PADS2_STATE_WAITING))
        pads->priv->queuedpads++;

      /* add to the list of pads to collect */
      ref_data (data);
      /* preserve order of adding/requesting pads */
      pads->data = g_slist_append (pads->data, data);
    }
    /* and update the cookie */
    pads->priv->cookie = pads->priv->pad_cookie;
  }
  GST_OBJECT_UNLOCK (pads);
}

/* checks if all the pads are collected and call the collectfunction
 *
 * Should be called with STREAM_LOCK.
 *
 * Returns: The #GstFlowReturn of collection.
 */
static GstFlowReturn
gst_collect_pads2_check_collected (GstCollectPads2 * pads)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstCollectPads2Function func;
  gpointer user_data;

  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), GST_FLOW_ERROR);

  GST_OBJECT_LOCK (pads);
  func = pads->priv->func;
  user_data = pads->priv->user_data;
  GST_OBJECT_UNLOCK (pads);

  g_return_val_if_fail (pads->priv->func != NULL, GST_FLOW_NOT_SUPPORTED);

  /* check for new pads, update stats etc.. */
  gst_collect_pads2_check_pads (pads);

  if (G_UNLIKELY (pads->priv->eospads == pads->priv->numpads)) {
    /* If all our pads are EOS just collect once to let the element
     * do its final EOS handling. */
    GST_DEBUG_OBJECT (pads, "All active pads (%d) are EOS, calling %s",
        pads->priv->numpads, GST_DEBUG_FUNCPTR_NAME (func));

    flow_ret = func (pads, user_data);
  } else {
    gboolean collected = FALSE;

    /* We call the collected function as long as our condition matches. */
    while (((pads->priv->queuedpads + pads->priv->eospads) >=
            pads->priv->numpads)) {
      GST_DEBUG_OBJECT (pads,
          "All active pads (%d + %d >= %d) have data, " "calling %s",
          pads->priv->queuedpads, pads->priv->eospads, pads->priv->numpads,
          GST_DEBUG_FUNCPTR_NAME (func));

      flow_ret = func (pads, user_data);
      collected = TRUE;

      /* break on error */
      if (flow_ret != GST_FLOW_OK)
        break;
      /* Don't keep looping after telling the element EOS or flushing */
      if (pads->priv->queuedpads == 0)
        break;
    }
    if (!collected)
      GST_DEBUG_OBJECT (pads, "Not all active pads (%d) have data, continuing",
          pads->priv->numpads);
  }
  return flow_ret;
}


/* General overview:
 * - only pad with a buffer can determine earliest_data (and earliest_time)
 * - only segment info determines (non-)waiting state
 * - ? perhaps use _stream_time for comparison
 *   (which muxers might have use as well ?)
 */

/*
 * Function to recalculate the waiting state of all pads.
 *
 * Must be called with STREAM_LOCK.
 *
 * Returns TRUE if a pad was set to waiting
 * (from non-waiting state).
 */
static gboolean
gst_collect_pads2_recalculate_waiting (GstCollectPads2 * pads)
{
  GSList *collected;
  gboolean result = FALSE;

  /* If earliest time is not known, there is nothing to do. */
  if (pads->priv->earliest_data == NULL)
    return FALSE;

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstCollectData2 *data = (GstCollectData2 *) collected->data;
    int cmp_res;

    /* check if pad has a segment */
    if (data->segment.format == GST_FORMAT_UNDEFINED)
      continue;

    /* check segment format */
    if (data->segment.format != GST_FORMAT_TIME) {
      GST_ERROR_OBJECT (pads, "GstCollectPads2 can handle only time segments.");
      continue;
    }

    /* check if the waiting state should be changed */
    cmp_res = pads->priv->compare_func (pads, data, data->segment.start,
        pads->priv->earliest_data, pads->priv->earliest_time,
        pads->priv->compare_user_data);
    if (cmp_res > 0)
      /* stop waiting */
      gst_collect_pads2_set_waiting (pads, data, FALSE);
    else {
      if (!GST_COLLECT_PADS2_STATE_IS_SET (data,
              GST_COLLECT_PADS2_STATE_WAITING)) {
        /* start waiting */
        gst_collect_pads2_set_waiting (pads, data, TRUE);
        result = TRUE;
      }
    }
  }

  return result;
}

/**
 * gst_collect_pads2_find_best_pad:
 * @pads: the collectpads to use
 * @data: returns the collectdata for earliest data
 * @time: returns the earliest available buffertime
 *
 * Find the oldest/best pad, i.e. pad holding the oldest buffer and
 * and return the corresponding #GstCollectData2 and buffertime.
 *
 * This function should be called with STREAM_LOCK held,
 * such as in the callback.
 *
 * Since: 0.10.36
 */
static void
gst_collect_pads2_find_best_pad (GstCollectPads2 * pads,
    GstCollectData2 ** data, GstClockTime * time)
{
  GSList *collected;
  GstCollectData2 *best = NULL;
  GstClockTime best_time = GST_CLOCK_TIME_NONE;

  g_return_if_fail (data != NULL);
  g_return_if_fail (time != NULL);

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstBuffer *buffer;
    GstCollectData2 *data = (GstCollectData2 *) collected->data;
    GstClockTime timestamp;

    buffer = gst_collect_pads2_peek (pads, data);
    /* if we have a buffer check if it is better then the current best one */
    if (buffer != NULL) {
      timestamp = GST_BUFFER_TIMESTAMP (buffer);
      gst_buffer_unref (buffer);
      if (best == NULL || pads->priv->compare_func (pads, data, timestamp,
              best, best_time, pads->priv->compare_user_data) < 0) {
        best = data;
        best_time = timestamp;
      }
    }
  }

  /* set earliest time */
  *data = best;
  *time = best_time;

  GST_DEBUG_OBJECT (pads, "best pad %s, best time %" GST_TIME_FORMAT,
      best ? GST_PAD_NAME (((GstCollectData2 *) best)->pad) : "(nil)",
      GST_TIME_ARGS (best_time));
}

/*
 * Function to recalculate earliest_data and earliest_timestamp. This also calls
 * gst_collect_pads2_recalculate_waiting
 *
 * Must be called with STREAM_LOCK.
 */
static gboolean
gst_collect_pads2_recalculate_full (GstCollectPads2 * pads)
{
  if (pads->priv->earliest_data)
    unref_data (pads->priv->earliest_data);
  gst_collect_pads2_find_best_pad (pads, &pads->priv->earliest_data,
      &pads->priv->earliest_time);
  if (pads->priv->earliest_data)
    ref_data (pads->priv->earliest_data);
  return gst_collect_pads2_recalculate_waiting (pads);
}

/*
 * Default collect callback triggered when #GstCollectPads2 gathered all data.
 *
 * Called with STREAM_LOCK.
 */
static GstFlowReturn
gst_collect_pads2_default_collected (GstCollectPads2 * pads, gpointer user_data)
{
  GstCollectData2 *best = NULL;
  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCollectPads2BufferFunction func;
  gpointer buffer_user_data;

  g_return_val_if_fail (GST_IS_COLLECT_PADS2 (pads), GST_FLOW_ERROR);

  GST_OBJECT_LOCK (pads);
  func = pads->priv->buffer_func;
  buffer_user_data = pads->priv->buffer_user_data;
  GST_OBJECT_UNLOCK (pads);

  g_return_val_if_fail (func != NULL, GST_FLOW_NOT_SUPPORTED);

  /* Find the oldest pad at all cost */
  if (gst_collect_pads2_recalculate_full (pads)) {
    /* waiting was switched on,
     * so give another thread a chance to deliver a possibly
     * older buffer; don't charge on yet with the current oldest */
    ret = GST_FLOW_OK;
    goto done;
  }

  best = pads->priv->earliest_data;

  /* No data collected means EOS. */
  if (G_UNLIKELY (best == NULL)) {
    ret = func (pads, best, NULL, buffer_user_data);
    if (ret == GST_FLOW_OK)
      ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  /* make sure that the pad we take a buffer from is waiting;
   * otherwise popping a buffer will seem not to have happened
   * and collectpads can get into a busy loop */
  gst_collect_pads2_set_waiting (pads, best, TRUE);

  /* Send buffer */
  buffer = gst_collect_pads2_pop (pads, best);
  ret = func (pads, best, buffer, buffer_user_data);

  /* maybe non-waiting was forced to waiting above due to
   * newsegment events coming too sparsely,
   * so re-check to restore state to avoid hanging/waiting */
  gst_collect_pads2_recalculate_full (pads);

done:
  return ret;
}

/*
 * Default timestamp compare function.
 */
static gint
gst_collect_pads2_default_compare_func (GstCollectPads2 * pads,
    GstCollectData2 * data1, GstClockTime timestamp1,
    GstCollectData2 * data2, GstClockTime timestamp2, gpointer user_data)
{

  GST_LOG_OBJECT (pads, "comparing %" GST_TIME_FORMAT
      " and %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp1),
      GST_TIME_ARGS (timestamp2));
  /* non-valid timestamps go first as they are probably headers or so */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp1)))
    return GST_CLOCK_TIME_IS_VALID (timestamp2) ? -1 : 0;

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp2)))
    return 1;

  /* compare timestamp */
  if (timestamp1 < timestamp2)
    return -1;

  if (timestamp1 > timestamp2)
    return 1;

  return 0;
}

static gboolean
gst_collect_pads2_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE, need_unlock = FALSE;
  GstCollectData2 *data;
  GstCollectPads2 *pads;
  GstCollectPads2EventFunction event_func;
  GstCollectPads2BufferFunction buffer_func;
  gpointer event_user_data;

  /* some magic to get the managing collect_pads2 */
  GST_OBJECT_LOCK (pad);
  data = (GstCollectData2 *) gst_pad_get_element_private (pad);
  if (G_UNLIKELY (data == NULL))
    goto pad_removed;
  ref_data (data);
  GST_OBJECT_UNLOCK (pad);

  res = FALSE;

  pads = data->collect;

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (data->pad));

  GST_OBJECT_LOCK (pads);
  event_func = pads->priv->event_func;
  event_user_data = pads->priv->event_user_data;
  buffer_func = pads->priv->buffer_func;
  GST_OBJECT_UNLOCK (pads);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      /* forward event to unblock check_collected */
      if (event_func)
        res = event_func (pads, data, event, event_user_data);
      if (!res)
        res = gst_pad_event_default (pad, event);

      /* now unblock the chain function.
       * no cond per pad, so they all unblock, 
       * non-flushing block again */
      GST_COLLECT_PADS2_STREAM_LOCK (pads);
      GST_COLLECT_PADS2_STATE_SET (data, GST_COLLECT_PADS2_STATE_FLUSHING);
      gst_collect_pads2_clear (pads, data);

      /* cater for possible default muxing functionality */
      if (buffer_func) {
        /* restore to initial state */
        gst_collect_pads2_set_waiting (pads, data, TRUE);
        /* if the current pad is affected, reset state, recalculate later */
        if (pads->priv->earliest_data == data) {
          unref_data (data);
          pads->priv->earliest_data = NULL;
          pads->priv->earliest_time = GST_CLOCK_TIME_NONE;
        }
      }

      GST_COLLECT_PADS2_STREAM_UNLOCK (pads);

      /* event already cleaned up by forwarding */
      res = TRUE;
      goto done;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      /* flush the 1 buffer queue */
      GST_COLLECT_PADS2_STREAM_LOCK (pads);
      GST_COLLECT_PADS2_STATE_UNSET (data, GST_COLLECT_PADS2_STATE_FLUSHING);
      gst_collect_pads2_clear (pads, data);
      /* we need new segment info after the flush */
      gst_segment_init (&data->segment, GST_FORMAT_UNDEFINED);
      GST_COLLECT_PADS2_STATE_UNSET (data, GST_COLLECT_PADS2_STATE_NEW_SEGMENT);
      /* if the pad was EOS, remove the EOS flag and
       * decrement the number of eospads */
      if (G_UNLIKELY (GST_COLLECT_PADS2_STATE_IS_SET (data,
                  GST_COLLECT_PADS2_STATE_EOS))) {
        if (!GST_COLLECT_PADS2_STATE_IS_SET (data,
                GST_COLLECT_PADS2_STATE_WAITING))
          pads->priv->queuedpads++;
        pads->priv->eospads--;
        GST_COLLECT_PADS2_STATE_UNSET (data, GST_COLLECT_PADS2_STATE_EOS);
      }
      GST_COLLECT_PADS2_STREAM_UNLOCK (pads);

      /* forward event */
      goto forward_or_default;
    }
    case GST_EVENT_EOS:
    {
      GST_COLLECT_PADS2_STREAM_LOCK (pads);
      /* if the pad was not EOS, make it EOS and so we
       * have one more eospad */
      if (G_LIKELY (!GST_COLLECT_PADS2_STATE_IS_SET (data,
                  GST_COLLECT_PADS2_STATE_EOS))) {
        GST_COLLECT_PADS2_STATE_SET (data, GST_COLLECT_PADS2_STATE_EOS);
        if (!GST_COLLECT_PADS2_STATE_IS_SET (data,
                GST_COLLECT_PADS2_STATE_WAITING))
          pads->priv->queuedpads--;
        pads->priv->eospads++;
      }
      /* check if we need collecting anything, we ignore the result. */
      gst_collect_pads2_check_collected (pads);
      GST_COLLECT_PADS2_STREAM_UNLOCK (pads);

      goto forward_or_eat;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 start, stop, time;
      gdouble rate, arate;
      GstFormat format;
      gboolean update;
      gint cmp_res;

      GST_COLLECT_PADS2_STREAM_LOCK (pads);

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (data->pad, "got newsegment, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      gst_segment_set_newsegment_full (&data->segment, update, rate, arate,
          format, start, stop, time);

      GST_COLLECT_PADS2_STATE_SET (data, GST_COLLECT_PADS2_STATE_NEW_SEGMENT);

      /* default muxing functionality */
      if (!buffer_func)
        goto newsegment_done;

      /* default collection can not handle other segment formats than time */
      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (pads, "GstCollectPads2 default collecting "
            "can only handle time segments.");
        goto newsegment_done;
      }

      /* If oldest time is not known, or current pad got newsegment;
       * recalculate the state */
      if (!pads->priv->earliest_data || pads->priv->earliest_data == data) {
        gst_collect_pads2_recalculate_full (pads);
        goto newsegment_done;
      }

      /* Check if the waiting state of the pad should change. */
      cmp_res =
          pads->priv->compare_func (pads, data, start,
          pads->priv->earliest_data, pads->priv->earliest_time,
          pads->priv->compare_user_data);

      if (cmp_res > 0)
        /* Stop waiting */
        gst_collect_pads2_set_waiting (pads, data, FALSE);

    newsegment_done:
      GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
      /* we must not forward this event since multiple segments will be
       * accumulated and this is certainly not what we want. */
      goto forward_or_eat;
    }
    default:
      /* forward other events */
      goto forward_or_default;
  }

forward_or_default:
  if (GST_EVENT_IS_SERIALIZED (event)) {
    GST_COLLECT_PADS2_STREAM_LOCK (pads);
    need_unlock = TRUE;
  }
  if (event_func)
    res = event_func (pads, data, event, event_user_data);
  if (!res)
    res = gst_pad_event_default (pad, event);
  if (need_unlock)
    GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
  goto done;

forward_or_eat:
  if (GST_EVENT_IS_SERIALIZED (event)) {
    GST_COLLECT_PADS2_STREAM_LOCK (pads);
    need_unlock = TRUE;
  }
  if (event_func)
    res = event_func (pads, data, event, event_user_data);
  if (!res) {
    gst_event_unref (event);
    res = TRUE;
  }
  if (need_unlock)
    GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
  goto done;

done:
  unref_data (data);
  return res;

  /* ERRORS */
pad_removed:
  {
    GST_DEBUG ("%s got removed from collectpads", GST_OBJECT_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
}

/* For each buffer we receive we check if our collected condition is reached
 * and if so we call the collected function. When this is done we check if
 * data has been unqueued. If data is still queued we wait holding the stream
 * lock to make sure no EOS event can happen while we are ready to be
 * collected 
 */
static GstFlowReturn
gst_collect_pads2_chain (GstPad * pad, GstBuffer * buffer)
{
  GstCollectData2 *data;
  GstCollectPads2 *pads;
  GstFlowReturn ret;
  GstBuffer **buffer_p;
  guint32 cookie;

  GST_DEBUG ("Got buffer for pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* some magic to get the managing collect_pads2 */
  GST_OBJECT_LOCK (pad);
  data = (GstCollectData2 *) gst_pad_get_element_private (pad);
  if (G_UNLIKELY (data == NULL))
    goto no_data;
  ref_data (data);
  GST_OBJECT_UNLOCK (pad);

  pads = data->collect;

  GST_COLLECT_PADS2_STREAM_LOCK (pads);
  /* if not started, bail out */
  if (G_UNLIKELY (!pads->priv->started))
    goto not_started;
  /* check if this pad is flushing */
  if (G_UNLIKELY (GST_COLLECT_PADS2_STATE_IS_SET (data,
              GST_COLLECT_PADS2_STATE_FLUSHING)))
    goto flushing;
  /* pad was EOS, we can refuse this data */
  if (G_UNLIKELY (GST_COLLECT_PADS2_STATE_IS_SET (data,
              GST_COLLECT_PADS2_STATE_EOS)))
    goto unexpected;

  /* see if we need to clip */
  if (pads->priv->clip_func) {
    GstBuffer *outbuf = NULL;
    ret =
        pads->priv->clip_func (pads, data, buffer, &outbuf,
        pads->priv->clip_user_data);
    buffer = outbuf;

    if (G_UNLIKELY (outbuf == NULL))
      goto clipped;

    if (G_UNLIKELY (ret == GST_FLOW_UNEXPECTED))
      goto unexpected;
    else if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto error;
  }

  GST_DEBUG_OBJECT (pads, "Queuing buffer %p for pad %s:%s", buffer,
      GST_DEBUG_PAD_NAME (pad));

  /* One more pad has data queued */
  if (GST_COLLECT_PADS2_STATE_IS_SET (data, GST_COLLECT_PADS2_STATE_WAITING))
    pads->priv->queuedpads++;
  buffer_p = &data->buffer;
  gst_buffer_replace (buffer_p, buffer);

  /* update segment last position if in TIME */
  if (G_LIKELY (data->segment.format == GST_FORMAT_TIME)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp))
      gst_segment_set_last_stop (&data->segment, GST_FORMAT_TIME, timestamp);
  }

  /* While we have data queued on this pad try to collect stuff */
  do {
    /* Check if our collected condition is matched and call the collected
     * function if it is */
    ret = gst_collect_pads2_check_collected (pads);
    /* when an error occurs, we want to report this back to the caller ASAP
     * without having to block if the buffer was not popped */
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto error;

    /* data was consumed, we can exit and accept new data */
    if (data->buffer == NULL)
      break;

    /* Having the _INIT here means we don't care about any broadcast up to here
     * (most of which occur with STREAM_LOCK held, so could not have happened
     * anyway).  We do care about e.g. a remove initiated broadcast as of this
     * point.  Putting it here also makes this thread ignores any evt it raised
     * itself (as is a usual WAIT semantic).
     */
    GST_COLLECT_PADS2_EVT_INIT (cookie);

    /* pad could be removed and re-added */
    unref_data (data);
    GST_OBJECT_LOCK (pad);
    if (G_UNLIKELY ((data = gst_pad_get_element_private (pad)) == NULL))
      goto pad_removed;
    ref_data (data);
    GST_OBJECT_UNLOCK (pad);

    GST_DEBUG_OBJECT (pads, "Pad %s:%s has a buffer queued, waiting",
        GST_DEBUG_PAD_NAME (pad));

    /* wait to be collected, this must happen from another thread triggered
     * by the _chain function of another pad. We release the lock so we
     * can get stopped or flushed as well. We can however not get EOS
     * because we still hold the STREAM_LOCK.
     */
    GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
    GST_COLLECT_PADS2_EVT_WAIT (pads, cookie);
    GST_COLLECT_PADS2_STREAM_LOCK (pads);

    GST_DEBUG_OBJECT (pads, "Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));

    /* after a signal, we could be stopped */
    if (G_UNLIKELY (!pads->priv->started))
      goto not_started;
    /* check if this pad is flushing */
    if (G_UNLIKELY (GST_COLLECT_PADS2_STATE_IS_SET (data,
                GST_COLLECT_PADS2_STATE_FLUSHING)))
      goto flushing;
  }
  while (data->buffer != NULL);

unlock_done:
  GST_COLLECT_PADS2_STREAM_UNLOCK (pads);
  unref_data (data);
  if (buffer)
    gst_buffer_unref (buffer);
  return ret;

pad_removed:
  {
    GST_WARNING ("%s got removed from collectpads", GST_OBJECT_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    ret = GST_FLOW_NOT_LINKED;
    goto unlock_done;
  }
  /* ERRORS */
no_data:
  {
    GST_DEBUG ("%s got removed from collectpads", GST_OBJECT_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_LINKED;
  }
not_started:
  {
    GST_DEBUG ("not started");
    gst_collect_pads2_clear (pads, data);
    ret = GST_FLOW_WRONG_STATE;
    goto unlock_done;
  }
flushing:
  {
    GST_DEBUG ("pad %s:%s is flushing", GST_DEBUG_PAD_NAME (pad));
    gst_collect_pads2_clear (pads, data);
    ret = GST_FLOW_WRONG_STATE;
    goto unlock_done;
  }
unexpected:
  {
    /* we should not post an error for this, just inform upstream that
     * we don't expect anything anymore */
    GST_DEBUG ("pad %s:%s is eos", GST_DEBUG_PAD_NAME (pad));
    ret = GST_FLOW_UNEXPECTED;
    goto unlock_done;
  }
clipped:
  {
    GST_DEBUG ("clipped buffer on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    ret = GST_FLOW_OK;
    goto unlock_done;
  }
error:
  {
    /* we print the error, the element should post a reasonable error
     * message for fatal errors */
    GST_DEBUG ("collect failed, reason %d (%s)", ret, gst_flow_get_name (ret));
    gst_collect_pads2_clear (pads, data);
    goto unlock_done;
  }
}
