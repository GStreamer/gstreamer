/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *
 * gstsystemclock.c: Default clock, uses the system clock
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
 * SECTION:gstsystemclock
 * @short_description: Default clock that uses the current system time
 * @see_also: #GstClock
 *
 * The GStreamer core provides a GstSystemClock based on the system time.
 * Asynchronous callbacks are scheduled from an internal thread.
 *
 * Clock implementors are encouraged to subclass this systemclock as it
 * implements the async notification.
 *
 * Subclasses can however override all of the important methods for sync and
 * async notifications to implement their own callback methods or blocking
 * wait operations.
 *
 * Last reviewed on 2005-10-28 (0.9.4)
 */

#include "gst_private.h"
#include "gstinfo.h"

#include "gstsystemclock.h"

/* the one instance of the systemclock */
static GstClock *_the_system_clock = NULL;

static void gst_system_clock_class_init (GstSystemClockClass * klass);
static void gst_system_clock_init (GstSystemClock * clock);
static void gst_system_clock_dispose (GObject * object);

static GstClockTime gst_system_clock_get_internal_time (GstClock * clock);
static guint64 gst_system_clock_get_resolution (GstClock * clock);
static GstClockReturn gst_system_clock_id_wait (GstClock * clock,
    GstClockEntry * entry);
static GstClockReturn gst_system_clock_id_wait_unlocked
    (GstClock * clock, GstClockEntry * entry);
static GstClockReturn gst_system_clock_id_wait_async (GstClock * clock,
    GstClockEntry * entry);
static void gst_system_clock_id_unschedule (GstClock * clock,
    GstClockEntry * entry);
static void gst_system_clock_async_thread (GstClock * clock);

static GStaticMutex _gst_sysclock_mutex = G_STATIC_MUTEX_INIT;

static GstClockClass *parent_class = NULL;

/* static guint gst_system_clock_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_system_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstSystemClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_system_clock_class_init,
      NULL,
      NULL,
      sizeof (GstSystemClock),
      0,
      (GInstanceInitFunc) gst_system_clock_init,
      NULL
    };

    clock_type = g_type_register_static (GST_TYPE_CLOCK, "GstSystemClock",
        &clock_info, 0);
  }
  return clock_type;
}

static void
gst_system_clock_class_init (GstSystemClockClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstClockClass *gstclock_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstclock_class = (GstClockClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_CLOCK);

  gobject_class->dispose = gst_system_clock_dispose;

  gstclock_class->get_internal_time = gst_system_clock_get_internal_time;
  gstclock_class->get_resolution = gst_system_clock_get_resolution;
  gstclock_class->wait = gst_system_clock_id_wait;
  gstclock_class->wait_async = gst_system_clock_id_wait_async;
  gstclock_class->unschedule = gst_system_clock_id_unschedule;
}

static void
gst_system_clock_init (GstSystemClock * clock)
{
  GError *error = NULL;

  GST_OBJECT_FLAG_SET (clock,
      GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC |
      GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC |
      GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC |
      GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC);

  GST_OBJECT_LOCK (clock);
  clock->thread = g_thread_create ((GThreadFunc) gst_system_clock_async_thread,
      clock, TRUE, &error);
  if (error)
    goto no_thread;

  /* wait for it to spin up */
  GST_CLOCK_WAIT (clock);
  GST_OBJECT_UNLOCK (clock);
  return;

no_thread:
  {
    g_warning ("could not create async clock thread: %s", error->message);
    GST_OBJECT_UNLOCK (clock);
  }
}

static void
gst_system_clock_dispose (GObject * object)
{
  GstClock *clock = (GstClock *) object;

  GstSystemClock *sysclock = GST_SYSTEM_CLOCK (clock);
  GList *entries;

  /* else we have to stop the thread */
  GST_OBJECT_LOCK (clock);
  sysclock->stopping = TRUE;
  /* unschedule all entries */
  for (entries = clock->entries; entries; entries = g_list_next (entries)) {
    GstClockEntry *entry = (GstClockEntry *) entries->data;

    GST_CAT_DEBUG (GST_CAT_CLOCK, "unscheduling entry %p", entry);
    entry->status = GST_CLOCK_UNSCHEDULED;
  }
  g_list_free (clock->entries);
  clock->entries = NULL;
  GST_CLOCK_BROADCAST (clock);
  GST_OBJECT_UNLOCK (clock);

  if (sysclock->thread)
    g_thread_join (sysclock->thread);
  sysclock->thread = NULL;
  GST_CAT_DEBUG (GST_CAT_CLOCK, "joined thread");

  G_OBJECT_CLASS (parent_class)->dispose (object);

  if (_the_system_clock == clock) {
    _the_system_clock = NULL;
    GST_CAT_DEBUG (GST_CAT_CLOCK, "disposed system clock");
  }
}

/**
 * gst_system_clock_obtain:
 *
 * Get a handle to the default system clock. The refcount of the
 * clock will be increased so you need to unref the clock after
 * usage.
 *
 * Returns: the default clock.
 *
 * MT safe.
 */
GstClock *
gst_system_clock_obtain (void)
{
  GstClock *clock;

  g_static_mutex_lock (&_gst_sysclock_mutex);
  clock = _the_system_clock;

  if (clock == NULL) {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "creating new static system clock");
    clock = g_object_new (GST_TYPE_SYSTEM_CLOCK,
        "name", "GstSystemClock", NULL);

    /* we created the global clock; take ownership so
     * we can hand out instances later */
    gst_object_ref (clock);
    gst_object_sink (GST_OBJECT (clock));

    _the_system_clock = clock;
    g_static_mutex_unlock (&_gst_sysclock_mutex);
  } else {
    g_static_mutex_unlock (&_gst_sysclock_mutex);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "returning static system clock");
  }

  /* we ref it since we are a clock factory. */
  gst_object_ref (clock);
  return clock;
}

/* this thread reads the sorted clock entries from the queue.
 *
 * It waits on each of them and fires the callback when the timeout occurs.
 *
 * When an entry in the queue was canceled, it is simply skipped.
 *
 * When waiting for an entry, it can become canceled, in that case we don't
 * call the callback but move to the next item in the queue.
 *
 * MT safe.
 */
static void
gst_system_clock_async_thread (GstClock * clock)
{
  GstSystemClock *sysclock = GST_SYSTEM_CLOCK (clock);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "enter system clock thread");
  GST_OBJECT_LOCK (clock);
  /* signal spinup */
  GST_CLOCK_BROADCAST (clock);
  /* now enter our (almost) infinite loop */
  while (!sysclock->stopping) {
    GstClockEntry *entry;
    GstClockReturn res;

    /* check if something to be done */
    while (clock->entries == NULL) {
      GST_CAT_DEBUG (GST_CAT_CLOCK, "no clock entries, waiting..");
      /* wait for work to do */
      GST_CLOCK_WAIT (clock);
      GST_CAT_DEBUG (GST_CAT_CLOCK, "got signal");
      /* clock was stopping, exit */
      if (sysclock->stopping)
        goto exit;
    }

    /* pick the next entry */
    entry = clock->entries->data;
    /* if it was unscheduled, just move on to the next entry */
    if (entry->status == GST_CLOCK_UNSCHEDULED) {
      GST_CAT_DEBUG (GST_CAT_CLOCK, "entry %p was unscheduled", entry);
      goto next_entry;
    }

    /* now wait for the entry, we already hold the lock */
    res = gst_system_clock_id_wait_unlocked (clock, (GstClockID) entry);

    switch (res) {
      case GST_CLOCK_UNSCHEDULED:
        /* entry was unscheduled, move to the next */
        GST_CAT_DEBUG (GST_CAT_CLOCK, "async entry %p unscheduled", entry);
        goto next_entry;
      case GST_CLOCK_OK:
      case GST_CLOCK_EARLY:
      {
        /* entry timed out normally, fire the callback and move to the next
         * entry */
        GST_CAT_DEBUG (GST_CAT_CLOCK, "async entry %p unlocked", entry);
        if (entry->func) {
          /* unlock before firing the callback */
          GST_OBJECT_UNLOCK (clock);
          entry->func (clock, entry->time, (GstClockID) entry,
              entry->user_data);
          GST_OBJECT_LOCK (clock);
        }
        if (entry->type == GST_CLOCK_ENTRY_PERIODIC) {
          /* adjust time now */
          entry->time += entry->interval;
          /* and resort the list now */
          clock->entries =
              g_list_sort (clock->entries, gst_clock_id_compare_func);
          /* and restart */
          continue;
        } else {
          goto next_entry;
        }
      }
      case GST_CLOCK_BUSY:
        /* somebody unlocked the entry but is was not canceled, This means that
         * either a new entry was added in front of the queue or some other entry
         * was canceled. Whatever it is, pick the head entry of the list and
         * continue waiting. */
        GST_CAT_DEBUG (GST_CAT_CLOCK, "async entry %p needs restart", entry);
        continue;
      default:
        GST_CAT_DEBUG (GST_CAT_CLOCK,
            "strange result %d waiting for %p, skipping", res, entry);
        g_warning ("%s: strange result %d waiting for %p, skipping",
            GST_OBJECT_NAME (clock), res, entry);
        goto next_entry;
    }
  next_entry:
    /* we remove the current entry and unref it */
    clock->entries = g_list_remove (clock->entries, entry);
    gst_clock_id_unref ((GstClockID) entry);
  }
exit:
  /* signal exit */
  GST_CLOCK_BROADCAST (clock);
  GST_OBJECT_UNLOCK (clock);
  GST_CAT_DEBUG (GST_CAT_CLOCK, "exit system clock thread");
}

/* MT safe */
static GstClockTime
gst_system_clock_get_internal_time (GstClock * clock)
{
  GTimeVal timeval;

  g_get_current_time (&timeval);

  return GST_TIMEVAL_TO_TIME (timeval);
}

static guint64
gst_system_clock_get_resolution (GstClock * clock)
{
  return 1 * GST_USECOND;
}

/* synchronously wait on the given GstClockEntry.
 *
 * We do this by blocking on the global clock GCond variable with
 * the requested time as a timeout. This allows us to unblock the
 * entry by signaling the GCond variable.
 *
 * Note that signaling the global GCond unlocks all waiting entries. So
 * we need to check if an unlocked entry has changed when it unlocks.
 *
 * Entries that arrive too late are simply not waited on and a
 * GST_CLOCK_EARLY result is returned.
 *
 * should be called with LOCK held.
 *
 * MT safe.
 */
static GstClockReturn
gst_system_clock_id_wait_unlocked (GstClock * clock, GstClockEntry * entry)
{
  GstClockTime entryt, real, now, target;
  GstClockTimeDiff diff;

  /* need to call the overridden method */
  real = GST_CLOCK_GET_CLASS (clock)->get_internal_time (clock);
  entryt = GST_CLOCK_ENTRY_TIME (entry);

  now = gst_clock_adjust_unlocked (clock, real);
  diff = entryt - now;
  target = gst_system_clock_get_internal_time (clock) + diff;

  GST_CAT_DEBUG (GST_CAT_CLOCK, "entry %p"
      " target %" GST_TIME_FORMAT
      " entry %" GST_TIME_FORMAT
      " now %" GST_TIME_FORMAT
      " real %" GST_TIME_FORMAT
      " diff %" G_GINT64_FORMAT,
      entry,
      GST_TIME_ARGS (target),
      GST_TIME_ARGS (entryt), GST_TIME_ARGS (now), GST_TIME_ARGS (real), diff);

  if (diff > 0) {
    GTimeVal tv;

    GST_TIME_TO_TIMEVAL (target, tv);

    while (TRUE) {
      /* now wait on the entry, it either times out or the cond is signaled. */
      if (!GST_CLOCK_TIMED_WAIT (clock, &tv)) {
        /* timeout, this is fine, we can report success now */
        GST_CAT_DEBUG (GST_CAT_CLOCK, "entry %p unlocked after timeout", entry);
        entry->status = GST_CLOCK_OK;
        break;
      } else {
        /* the waiting is interrupted because the GCond was signaled. This can
         * be because this or some other entry was unscheduled. */
        GST_CAT_DEBUG (GST_CAT_CLOCK, "entry %p unlocked with signal", entry);
        /* if the entry is unscheduled, we can stop waiting for it */
        if (entry->status == GST_CLOCK_UNSCHEDULED)
          break;
      }
    }
  } else {
    entry->status = GST_CLOCK_EARLY;
  }
  return entry->status;
}

static GstClockReturn
gst_system_clock_id_wait (GstClock * clock, GstClockEntry * entry)
{
  GstClockReturn ret;

  GST_OBJECT_LOCK (clock);
  ret = gst_system_clock_id_wait_unlocked (clock, entry);
  GST_OBJECT_UNLOCK (clock);

  return ret;
}

/* Add an entry to the list of pending async waits. The entry is inserted
 * in sorted order. If we inserted the entry at the head of the list, we
 * need to signal the thread as it might either be waiting on it or waiting
 * for a new entry.
 *
 * MT safe.
 */
static GstClockReturn
gst_system_clock_id_wait_async (GstClock * clock, GstClockEntry * entry)
{
  GST_CAT_DEBUG (GST_CAT_CLOCK, "adding entry %p", entry);

  GST_OBJECT_LOCK (clock);
  /* need to take a ref */
  gst_clock_id_ref ((GstClockID) entry);
  /* insert the entry in sorted order */
  clock->entries = g_list_insert_sorted (clock->entries, entry,
      gst_clock_id_compare_func);

  /* only need to send the signal if the entry was added to the
   * front, else the thread is just waiting for another entry and
   * will get to this entry automatically. */
  if (clock->entries->data == entry) {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "send signal");
    GST_CLOCK_BROADCAST (clock);
  }
  GST_OBJECT_UNLOCK (clock);

  return GST_CLOCK_OK;
}

/* unschedule an entry. This will set the state of the entry to GST_CLOCK_UNSCHEDULED
 * and will signal any thread waiting for entries to recheck their entry.
 * We cannot really decide if the signal is needed or not because the entry
 * could be waited on in async or sync mode.
 *
 * MT safe.
 */
static void
gst_system_clock_id_unschedule (GstClock * clock, GstClockEntry * entry)
{
  GST_CAT_DEBUG (GST_CAT_CLOCK, "unscheduling entry %p", entry);

  GST_OBJECT_LOCK (clock);
  entry->status = GST_CLOCK_UNSCHEDULED;
  GST_CAT_DEBUG (GST_CAT_CLOCK, "send signal");
  GST_CLOCK_BROADCAST (clock);
  GST_OBJECT_UNLOCK (clock);
}
