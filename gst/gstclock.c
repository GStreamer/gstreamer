/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *
 * gstclock.c: Clock subsystem for maintaining time sync
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
 * SECTION:gstclock
 * @short_description: Abstract class for global clocks
 * @see_also: #GstSystemClock
 *
 * GStreamer uses a global clock to synchronise the plugins in a pipeline.
 * Different clock implementations are possible by implementing this abstract
 * base class.
 *
 * The clock time is always measured in nanoseconds and always increases. The
 * pipeline uses the clock to calculate the stream time.
 * Usually all renderers sync to the global clock so that the clock is always 
 * a good measure of the current playback time in the pipeline.
 */
#include <time.h>

#include "gst_private.h"

#include "gstclock.h"
#include "gstinfo.h"
#include "gstutils.h"

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE */
#include "gsttrace.h"
static GstAllocTrace *_gst_clock_entry_trace;
#endif

#define DEFAULT_EVENT_DIFF	(GST_SECOND)
#define DEFAULT_MAX_DIFF	(2 * GST_SECOND)

enum
{
  ARG_0,
  ARG_STATS,
  ARG_MAX_DIFF,
  ARG_EVENT_DIFF
};

static void gst_clock_class_init (GstClockClass * klass);
static void gst_clock_init (GstClock * clock);
static void gst_clock_finalize (GObject * object);

static void gst_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_clock_update_stats (GstClock * clock);


static GstObjectClass *parent_class = NULL;

/* static guint gst_clock_signals[LAST_SIGNAL] = { 0 }; */

static GstClockID
gst_clock_entry_new (GstClock * clock, GstClockTime time,
    GstClockTime interval, GstClockEntryType type)
{
  GstClockEntry *entry;

  entry = g_malloc0 (sizeof (GstClockEntry));
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_clock_entry_trace, entry);
#endif
  GST_CAT_DEBUG (GST_CAT_CLOCK, "created entry %p, time %" GST_TIME_FORMAT,
      entry, GST_TIME_ARGS (time));

  gst_atomic_int_set (&entry->refcount, 1);
  entry->clock = clock;
  entry->time = time;
  entry->interval = interval;
  entry->type = type;
  entry->status = GST_CLOCK_BUSY;

  return (GstClockID) entry;
}

/**
 * gst_clock_id_ref:
 * @id: The clockid to ref
 *
 * Increase the refcount of the given clockid.
 *
 * Returns: The same #GstClockID with increased refcount.
 *
 * MT safe.
 */
GstClockID
gst_clock_id_ref (GstClockID id)
{
  g_return_val_if_fail (id != NULL, NULL);

  g_atomic_int_inc (&((GstClockEntry *) id)->refcount);

  return id;
}

static void
_gst_clock_id_free (GstClockID id)
{
  g_return_if_fail (id != NULL);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "freed entry %p", id);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_clock_entry_trace, id);
#endif
  g_free (id);
}

/**
 * gst_clock_id_unref:
 * @id: The clockid to unref
 *
 * Unref the given clockid. When the refcount reaches 0 the
 * #GstClockID will be freed.
 *
 * MT safe.
 */
void
gst_clock_id_unref (GstClockID id)
{
  gint zero;

  g_return_if_fail (id != NULL);

  zero = g_atomic_int_dec_and_test (&((GstClockEntry *) id)->refcount);
  /* if we ended up with the refcount at zero, free the id */
  if (zero) {
    _gst_clock_id_free (id);
  }
}

/**
 * gst_clock_new_single_shot_id
 * @clock: The clockid to get a single shot notification from
 * @time: the requested time
 *
 * Get an ID from the given clock to trigger a single shot
 * notification at the requested time. The single shot id should be
 * unreffed after usage.
 *
 * Returns: An id that can be used to request the time notification.
 *
 * MT safe.
 */
GstClockID
gst_clock_new_single_shot_id (GstClock * clock, GstClockTime time)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  return gst_clock_entry_new (clock,
      time, GST_CLOCK_TIME_NONE, GST_CLOCK_ENTRY_SINGLE);
}

/**
 * gst_clock_new_periodic_id
 * @clock: The clockid to get a periodic notification id from
 * @start_time: the requested start time
 * @interval: the requested interval
 *
 * Get an ID from the given clock to trigger a periodic notification.
 * The periodeic notifications will be start at time start_time and
 * will then be fired with the given interval. The id should be unreffed
 * after usage.
 *
 * Returns: An id that can be used to request the time notification.
 *
 * MT safe.
 */
GstClockID
gst_clock_new_periodic_id (GstClock * clock, GstClockTime start_time,
    GstClockTime interval)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start_time), NULL);
  g_return_val_if_fail (interval != 0, NULL);

  return gst_clock_entry_new (clock,
      start_time, interval, GST_CLOCK_ENTRY_PERIODIC);
}

/**
 * gst_clock_id_compare_func
 * @id1: A clockid
 * @id2: A clockid to compare with
 *
 * Compares the two GstClockID instances. This function can be used
 * as a GCompareFunc when sorting ids.
 *
 * Returns: negative value if a < b; zero if a = b; positive value if a > b
 *
 * MT safe.
 */
gint
gst_clock_id_compare_func (gconstpointer id1, gconstpointer id2)
{
  GstClockEntry *entry1, *entry2;

  entry1 = (GstClockEntry *) id1;
  entry2 = (GstClockEntry *) id2;

  if (GST_CLOCK_ENTRY_TIME (entry1) > GST_CLOCK_ENTRY_TIME (entry2)) {
    return 1;
  }
  if (GST_CLOCK_ENTRY_TIME (entry1) < GST_CLOCK_ENTRY_TIME (entry2)) {
    return -1;
  }

  return entry1 - entry2;
}

/**
 * gst_clock_id_get_time
 * @id: The clockid to query
 *
 * Get the time of the clock ID
 *
 * Returns: the time of the given clock id.
 *
 * MT safe.
 */
GstClockTime
gst_clock_id_get_time (GstClockID id)
{
  g_return_val_if_fail (id != NULL, GST_CLOCK_TIME_NONE);

  return GST_CLOCK_ENTRY_TIME ((GstClockEntry *) id);
}


/**
 * gst_clock_id_wait
 * @id: The clockid to wait on
 * @jitter: A pointer that will contain the jitter
 *
 * Perform a blocking wait on the given ID. The jitter arg can be
 * NULL.
 *
 * Returns: the result of the blocking wait.
 *
 * MT safe.
 */
GstClockReturn
gst_clock_id_wait (GstClockID id, GstClockTimeDiff * jitter)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockReturn res;
  GstClockTime requested;
  GstClockClass *cclass;

  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);

  entry = (GstClockEntry *) id;
  requested = GST_CLOCK_ENTRY_TIME (entry);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (requested)))
    goto invalid_time;

  if (G_UNLIKELY (entry->status == GST_CLOCK_UNSCHEDULED))
    goto unscheduled;

  clock = GST_CLOCK_ENTRY_CLOCK (entry);
  cclass = GST_CLOCK_GET_CLASS (clock);

  if (G_LIKELY (cclass->wait)) {

    GST_CAT_DEBUG (GST_CAT_CLOCK, "waiting on clock entry %p", id);
    res = cclass->wait (clock, entry);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "done waiting entry %p", id);

    if (jitter) {
      GstClockTime now = gst_clock_get_time (clock);

      *jitter = now - requested;
    }
    if (entry->type == GST_CLOCK_ENTRY_PERIODIC) {
      entry->time += entry->interval;
    }

    if (clock->stats) {
      gst_clock_update_stats (clock);
    }
  } else {
    res = GST_CLOCK_UNSUPPORTED;
  }
  return res;

  /* ERRORS */
invalid_time:
  {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "invalid time requested, returning _BADTIME");
    return GST_CLOCK_BADTIME;
  }
unscheduled:
  {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "entry was unscheduled return _UNSCHEDULED");
    return GST_CLOCK_UNSCHEDULED;
  }
}

/**
 * gst_clock_id_wait_async:
 * @id: a #GstClockID to wait on
 * @func: The callback function 
 * @user_data: User data passed in the calback
 *
 * Register a callback on the given clockid with the given
 * function and user_data. When passing an id with an invalid
 * time to this function, the callback will be called immediatly
 * with  a time set to GST_CLOCK_TIME_NONE. The callback will
 * be called when the time of the id has been reached.
 *
 * Returns: the result of the non blocking wait.
 *
 * MT safe.
 */
GstClockReturn
gst_clock_id_wait_async (GstClockID id,
    GstClockCallback func, gpointer user_data)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockReturn res;
  GstClockClass *cclass;
  GstClockTime requested;

  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);
  g_return_val_if_fail (func != NULL, GST_CLOCK_ERROR);

  entry = (GstClockEntry *) id;
  requested = GST_CLOCK_ENTRY_TIME (entry);
  clock = GST_CLOCK_ENTRY_CLOCK (entry);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (requested)))
    goto invalid_time;

  if (G_UNLIKELY (entry->status == GST_CLOCK_UNSCHEDULED))
    goto unscheduled;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->wait_async) {
    entry->func = func;
    entry->user_data = user_data;

    res = cclass->wait_async (clock, entry);
  } else {
    res = GST_CLOCK_UNSUPPORTED;
  }
  return res;

  /* ERRORS */
invalid_time:
  {
    (func) (clock, GST_CLOCK_TIME_NONE, id, user_data);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "invalid time requested, returning _BADTIME");
    return GST_CLOCK_BADTIME;
  }
unscheduled:
  {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "entry was unscheduled return _UNSCHEDULED");
    return GST_CLOCK_UNSCHEDULED;
  }
}

/**
 * gst_clock_id_unschedule:
 * @id: The id to unschedule
 *
 * Cancel an outstanding request with the given ID. This can either
 * be an outstanding async notification or a pending sync notification.
 * After this call, the @id cannot be used anymore to receive sync or
 * async notifications, you need to create a new GstClockID.
 *
 * MT safe.
 */
void
gst_clock_id_unschedule (GstClockID id)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockClass *cclass;

  g_return_if_fail (id != NULL);

  entry = (GstClockEntry *) id;
  clock = entry->clock;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->unschedule)
    cclass->unschedule (clock, entry);
}


/**
 * GstClock abstract base class implementation
 */
GType
gst_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_clock_class_init,
      NULL,
      NULL,
      sizeof (GstClock),
      0,
      (GInstanceInitFunc) gst_clock_init,
      NULL
    };

    clock_type = g_type_register_static (GST_TYPE_OBJECT, "GstClock",
        &clock_info, G_TYPE_FLAG_ABSTRACT);
  }
  return clock_type;
}

static void
gst_clock_class_init (GstClockClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  if (!g_thread_supported ())
    g_thread_init (NULL);

#ifndef GST_DISABLE_TRACE
  _gst_clock_entry_trace =
      gst_alloc_trace_register (GST_CLOCK_ENTRY_TRACE_NAME);
#endif

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_clock_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_clock_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_clock_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STATS,
      g_param_spec_boolean ("stats", "Stats", "Enable clock stats",
          FALSE, G_PARAM_READWRITE));
}

static void
gst_clock_init (GstClock * clock)
{
  clock->adjust = 0;
  clock->last_time = 0;
  clock->entries = NULL;
  clock->entries_changed = g_cond_new ();
  clock->flags = 0;
  clock->stats = FALSE;
}

static void
gst_clock_finalize (GObject * object)
{
  GstClock *clock = GST_CLOCK (object);

  g_cond_free (clock->entries_changed);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_clock_set_resolution
 * @clock: The clock set the resolution on
 * @resolution: The resolution to set
 *
 * Set the accuracy of the clock.
 *
 * Returns: the new resolution of the clock.
 */
guint64
gst_clock_set_resolution (GstClock * clock, guint64 resolution)
{
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), G_GINT64_CONSTANT (0));
  g_return_val_if_fail (resolution != 0, G_GINT64_CONSTANT (0));

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->change_resolution)
    clock->resolution =
        cclass->change_resolution (clock, clock->resolution, resolution);

  return clock->resolution;
}

/**
 * gst_clock_get_resolution
 * @clock: The clock get the resolution of
 *
 * Get the accuracy of the clock.
 *
 * Returns: the resolution of the clock in microseconds.
 *
 * MT safe.
 */
guint64
gst_clock_get_resolution (GstClock * clock)
{
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), G_GINT64_CONSTANT (0));

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->get_resolution)
    return cclass->get_resolution (clock);

  return G_GINT64_CONSTANT (1);
}

/**
 * gst_clock_adjust_unlocked
 * @clock: a #GstClock to use
 * @internal: a clock time
 *
 * Converts the given @internal clock time to the real time, adjusting
 * and making sure that the returned time is increasing.
 * This function should be called with the clock lock held.
 *
 * Returns: the converted time of the clock.
 *
 * MT safe.
 */
GstClockTime
gst_clock_adjust_unlocked (GstClock * clock, GstClockTime internal)
{
  GstClockTime ret;

  ret = internal + clock->adjust;
  /* make sure the time is increasing, else return last_time */
  if ((gint64) ret < (gint64) clock->last_time) {
    ret = clock->last_time;
  } else {
    clock->last_time = ret;
  }
  return ret;
}

/**
 * gst_clock_get_time
 * @clock: a #GstClock to query
 *
 * Gets the current time of the given clock. The time is always
 * monotonically increasing.
 *
 * Returns: the time of the clock. Or GST_CLOCK_TIME_NONE when
 * giving wrong input.
 *
 * MT safe.
 */
GstClockTime
gst_clock_get_time (GstClock * clock)
{
  GstClockTime ret;
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_TIME_NONE);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->get_internal_time) {
    ret = cclass->get_internal_time (clock);
  } else {
    ret = G_GINT64_CONSTANT (0);
  }
  GST_CAT_DEBUG (GST_CAT_CLOCK, "internal time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ret));

  GST_LOCK (clock);
  ret = gst_clock_adjust_unlocked (clock, ret);
  GST_UNLOCK (clock);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "adjusted time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ret));

  return ret;
}

/**
 * gst_clock_set_time_adjust
 * @clock: a #GstClock to adjust
 * @adjust: the adjust value
 *
 * Adjusts the current time of the clock with the adjust value.
 * A positive value moves the clock forwards and a backwards value
 * moves it backwards. Note that _get_time() always returns 
 * increasing values so when you move the clock backwards, _get_time()
 * will report the previous value until the clock catches up.
 *
 * MT safe.
 */
void
gst_clock_set_time_adjust (GstClock * clock, GstClockTime adjust)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  GST_LOCK (clock);
  clock->adjust = adjust;
  GST_UNLOCK (clock);
}

static void
gst_clock_update_stats (GstClock * clock)
{
}

static void
gst_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstClock *clock;

  clock = GST_CLOCK (object);

  switch (prop_id) {
    case ARG_STATS:
      clock->stats = g_value_get_boolean (value);
      g_object_notify (object, "stats");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstClock *clock;

  clock = GST_CLOCK (object);

  switch (prop_id) {
    case ARG_STATS:
      g_value_set_boolean (value, clock->stats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
