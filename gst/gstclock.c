/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
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

#include <sys/time.h>

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstelement.h"
#include "gstclock.h"


static GstClock *the_system_clock = NULL;

/**
 * gst_clock_new:
 * @name: the name of the new clock
 *
 * create a new clock element
 *
 * Returns: the new clock element
 */
GstClock*
gst_clock_new (gchar *name)
{
  GstClock *clock = (GstClock *) g_malloc(sizeof(GstClock));

  clock->name = g_strdup (name);
  clock->sinkobjects = NULL;
  clock->sinkmutex = g_mutex_new ();
  clock->lock = g_mutex_new ();
  g_mutex_lock (clock->sinkmutex);

  clock->num = 0;
  clock->num_locked = 0;
  clock->locking = FALSE;

  return clock;
}

/**
 * gst_clock_get_system:
 *
 * Get the global system clock
 *
 * Returns: the global clock
 */
GstClock*
gst_clock_get_system(void)
{
  if (the_system_clock == NULL) {
    the_system_clock = gst_clock_new ("system_clock");
    gst_clock_reset (the_system_clock);
  }
  return the_system_clock;
}

/**
 * gst_clock_register:
 * @clock: the name of the clock to register to
 * @obj: the object registering to the clock
 *
 * State that an object is interested in listening to the
 * given clock
 */
void
gst_clock_register (GstClock *clock, GstObject *obj)
{
  if ((GST_ELEMENT(obj))->numsrcpads == 0) {
    GST_DEBUG (GST_CAT_CLOCK,"gst_clock: setting registered sink object 0x%p\n", obj);
    clock->sinkobjects = g_list_append (clock->sinkobjects, obj);
    clock->num++;
  }
}

/**
 * gst_clock_set:
 * @clock: The clock to set
 * @time: the time to set
 *
 * Set the time of the given clock to time.
 */
void
gst_clock_set (GstClock *clock, GstClockTime time)
{
  struct timeval tfnow;
  GstClockTime now;

  gettimeofday (&tfnow, (struct timezone *)NULL);
  now = tfnow.tv_sec*1000000LL+tfnow.tv_usec;
  g_mutex_lock (clock->lock);
  clock->start_time = now - time;
  g_mutex_unlock (clock->lock);
  GST_DEBUG (GST_CAT_CLOCK,"gst_clock: setting clock to %llu %llu %llu\n",
             time, now, clock->start_time);
}

/**
 * gst_clock_current_diff:
 * @clock: the clock to calculate the diff against
 * @time: the time
 *
 * Calculate the difference between the given clock and the
 * given time
 *
 * Returns: the clock difference
 */
GstClockTimeDiff
gst_clock_current_diff (GstClock *clock, GstClockTime time)
{
  struct timeval tfnow;
  GstClockTime now;

  gettimeofday (&tfnow, (struct timezone *)NULL);
  g_mutex_lock (clock->lock);
  now = ((guint64)tfnow.tv_sec*1000000LL+tfnow.tv_usec) - (guint64)clock->start_time;
  g_mutex_unlock (clock->lock);

  return GST_CLOCK_DIFF (time, now);
}

/**
 * gst_clock_reset:
 * @clock: the clock to reset
 *
 * Reset the given clock. The of the clock will be adjusted back
 * to 0.
 */
void
gst_clock_reset (GstClock *clock)
{
  struct timeval tfnow;

  gettimeofday (&tfnow, (struct timezone *)NULL);
  g_mutex_lock (clock->lock);
  clock->start_time = ((guint64)tfnow.tv_sec)*1000000LL+tfnow.tv_usec;
  clock->current_time = clock->start_time;
  clock->adjust = 0LL;
  GST_DEBUG (GST_CAT_CLOCK,"gst_clock: setting start clock %llu\n", clock->start_time);
  g_mutex_unlock (clock->lock);
}

/**
 * gst_clock_wait:
 * @clock: the clock to wait on
 * @time: the time to wait for
 * @obj: the object performing the wait
 *
 * Wait for a specific clock tick on the given clock.
 */
void
gst_clock_wait (GstClock *clock, GstClockTime time, GstObject *obj)
{
  struct timeval tfnow;
  GstClockTime now;
  GstClockTimeDiff diff;


  gettimeofday (&tfnow, (struct timezone *)NULL);
  g_mutex_lock (clock->lock);
  now = tfnow.tv_sec*1000000LL+tfnow.tv_usec - clock->start_time;

  diff = GST_CLOCK_DIFF (time, now);
  // if we are not behind wait a bit
  GST_DEBUG (GST_CAT_CLOCK,"gst_clock: %s waiting for time %08llu %08llu %08lld\n",
             GST_OBJECT_NAME (obj), time, now, diff);

  g_mutex_unlock (clock->lock);
  if (diff > 10000 ) {
    tfnow.tv_usec = (diff % 1000000);
    tfnow.tv_sec = diff / 1000000;
    // FIXME, this piece of code does not work with egcs optimisations on, had to use the following line
    if (!tfnow.tv_sec) {
      select(0, NULL, NULL, NULL, &tfnow);
    }
    else GST_DEBUG (GST_CAT_CLOCK,"gst_clock: %s waiting %u %llu %llu %llu seconds\n",
                    GST_OBJECT_NAME (obj), (int)tfnow.tv_sec, now, diff, time);
  }
  GST_DEBUG (GST_CAT_CLOCK,"gst_clock: %s waiting for time %08llu %08llu %08lld done \n", 
             GST_OBJECT_NAME (obj), time, now, diff);
}
