/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

#include <sys/time.h>

#include "gst_private.h"
#include "gstinfo.h"

#include "gstsystemclock.h"

static GstClock *_the_system_clock = NULL;

static void			gst_system_clock_class_init	(GstSystemClockClass *klass);
static void			gst_system_clock_init		(GstSystemClock *clock);
static void 			gst_system_clock_dispose 	(GObject *object);

static GstClockTime		gst_system_clock_get_internal_time	(GstClock *clock);
static guint64			gst_system_clock_get_resolution (GstClock *clock);
static GstClockEntryStatus	gst_system_clock_wait		(GstClock *clock, GstClockEntry *entry);
static void 			gst_system_clock_unlock 	(GstClock *clock, GstClockEntry *entry);

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
      4,
      (GInstanceInitFunc) gst_system_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_CLOCK, "GstSystemClock", 
		    			 &clock_info, 0);
  }
  return clock_type;
}

static void
gst_system_clock_class_init (GstSystemClockClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstClockClass *gstclock_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;
  gstclock_class = (GstClockClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_CLOCK);

  gobject_class->dispose	 	= gst_system_clock_dispose;

  gstclock_class->get_internal_time 	= gst_system_clock_get_internal_time;
  gstclock_class->get_resolution 	= gst_system_clock_get_resolution;
  gstclock_class->wait		 	= gst_system_clock_wait;
  gstclock_class->unlock		= gst_system_clock_unlock;
}

static void
gst_system_clock_init (GstSystemClock *clock)
{
  clock->mutex = g_mutex_new();
  clock->cond = g_cond_new ();
}

static void
gst_system_clock_dispose (GObject *object)
{
  GstClock *clock = (GstClock *) object;
  GstSystemClock *sysclock = (GstSystemClock *) object;

  /* there are subclasses of GstSystemClock running around... */
  if (_the_system_clock == clock) {
    g_warning ("disposing systemclock!");

    /* no parent dispose here, this is bad enough already */
  } else {
    G_OBJECT_CLASS (parent_class)->dispose (object);

    /* FIXME: Notifying before freeing? */
    g_cond_free (sysclock->cond);
    g_mutex_free (sysclock->mutex);
  }
}

/**
 * gst_system_clock_obtain 
 *
 * Get a handle to the default system clock.
 *
 * Returns: the default clock.
 */
GstClock*
gst_system_clock_obtain (void)
{
  GstClock *clock = _the_system_clock;

  if (clock == NULL) {
    g_static_mutex_lock (&_gst_sysclock_mutex);
    if (_the_system_clock != NULL) {
      clock = _the_system_clock;
      g_static_mutex_unlock (&_gst_sysclock_mutex);
      goto have_clock;
    }
      
    clock = GST_CLOCK (g_object_new (GST_TYPE_SYSTEM_CLOCK, NULL));
    
    gst_object_set_name (GST_OBJECT (clock), "GstSystemClock");

    gst_object_ref (GST_OBJECT (clock));
    gst_object_sink (GST_OBJECT (clock));

    _the_system_clock = clock;
    g_static_mutex_unlock (&_gst_sysclock_mutex);
  }

have_clock:
  gst_object_ref (GST_OBJECT (clock));
  return clock;
}

static GstClockTime
gst_system_clock_get_internal_time (GstClock *clock)
{
  GTimeVal timeval;

  g_get_current_time (&timeval);
  
  return GST_TIMEVAL_TO_TIME (timeval);
}

static guint64
gst_system_clock_get_resolution (GstClock *clock)
{
  return 1 * GST_USECOND;
}

static GstClockEntryStatus
gst_system_clock_wait (GstClock *clock, GstClockEntry *entry)
{
  GstClockEntryStatus res;
  GstClockTime current, target;
  gint64 diff;
  GstSystemClock *sysclock = GST_SYSTEM_CLOCK (clock);

  current = gst_clock_get_time (clock);
  diff = GST_CLOCK_ENTRY_TIME (entry) - current;

  if (diff + clock->max_diff < 0) {
    GST_WARNING_OBJECT (clock, "clock is way behind: %" G_GINT64_FORMAT
	"s (max allowed is %" G_GINT64_FORMAT "s", -diff, clock->max_diff);
    return GST_CLOCK_ENTRY_EARLY;
  }
  
  target = gst_system_clock_get_internal_time (clock) + diff;

  GST_CAT_DEBUG (GST_CAT_CLOCK, "real_target %" G_GUINT64_FORMAT
		            " target %" G_GUINT64_FORMAT
			    " now %" G_GUINT64_FORMAT,
                            target, GST_CLOCK_ENTRY_TIME (entry), current);

  if (((gint64)target) > 0) {
    GTimeVal tv;

    GST_TIME_TO_TIMEVAL (target, tv);
    g_mutex_lock (sysclock->mutex);
    g_cond_timed_wait (sysclock->cond, sysclock->mutex, &tv);
    g_mutex_unlock (sysclock->mutex);
    res = entry->status;
  }
  else {
    res = GST_CLOCK_ENTRY_EARLY;
  }
  return res;
}

static void
gst_system_clock_unlock (GstClock *clock, GstClockEntry *entry)
{
  GstSystemClock *sysclock = GST_SYSTEM_CLOCK (clock);

  g_mutex_lock (sysclock->mutex);
  g_cond_broadcast (sysclock->cond);
  g_mutex_unlock (sysclock->mutex);
}
