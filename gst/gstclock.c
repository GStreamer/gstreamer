/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
//#define DEBUG_ENABLED
#include <gstclock.h>

static GstClock *the_system_clock = NULL;

/**
 * gst_clock_new:
 * @name: the name of the new clock
 *
 * create a new clock element
 *
 * Returns: the new clock element
 */
GstClock *gst_clock_new(gchar *name) {
  GstClock *clock = (GstClock *) g_malloc(sizeof(GstClock));
  clock->name = g_strdup(name);
  clock->sinkobjects = NULL;
  clock->sinkmutex = g_mutex_new();
  clock->lock = g_mutex_new();
  g_mutex_lock(clock->sinkmutex);
  clock->num = 0;
  clock->num_locked = 0;
  clock->locking = FALSE;
  return clock;
}

GstClock *gst_clock_get_system() {
  if (the_system_clock == NULL) {
    the_system_clock = gst_clock_new("system_clock");
    gst_clock_reset(the_system_clock);
  }
  return the_system_clock;
}

void gst_clock_register(GstClock *clock, GstObject *obj) {
  if (GST_IS_SINK(obj)) {
    DEBUG("gst_clock: setting registered sink object 0x%p\n", obj);
    clock->sinkobjects = g_list_append(clock->sinkobjects, obj);
    clock->num++;
  }
}

void gst_clock_set(GstClock *clock, GstClockTime time) {
  struct timeval tfnow;
  GstClockTime now;

  gettimeofday(&tfnow, (struct timezone *)NULL);
  now = tfnow.tv_sec*1000000LL+tfnow.tv_usec;
  g_mutex_lock(clock->lock);
  clock->start_time = now - time;
  g_mutex_unlock(clock->lock);
  DEBUG("gst_clock: setting clock to %llu %llu %llu\n", time, now, clock->start_time);
}

GstClockTimeDiff gst_clock_current_diff(GstClock *clock, GstClockTime time)
{
  struct timeval tfnow;
  GstClockTime now;

  gettimeofday(&tfnow, (struct timezone *)NULL);
  g_mutex_lock(clock->lock);
  now = ((guint64)tfnow.tv_sec*1000000LL+tfnow.tv_usec) - (guint64)clock->start_time; 
  //if (clock->locking) now = 0;
  g_mutex_unlock(clock->lock);

  //DEBUG("gst_clock: diff with for time %08llu %08lld %08lld %08llu\n", time, now, GST_CLOCK_DIFF(time, now), clock->start_time);

  return GST_CLOCK_DIFF(time, now);
}

void gst_clock_reset(GstClock *clock) {
  struct timeval tfnow;

  gettimeofday(&tfnow, (struct timezone *)NULL);
  g_mutex_lock(clock->lock);
  clock->start_time = ((guint64)tfnow.tv_sec)*1000000LL+tfnow.tv_usec;
  clock->current_time = clock->start_time;
  clock->adjust = 0LL;
  DEBUG("gst_clock: setting start clock %llu\n", clock->start_time);
  //clock->locking = TRUE;
  g_mutex_unlock(clock->lock);
}

void gst_clock_wait(GstClock *clock, GstClockTime time, GstObject *obj) {
  struct timeval tfnow;
  GstClockTime now;
  GstClockTimeDiff diff;

  //DEBUG("gst_clock: requesting clock object 0x%p %08llu %08llu\n", obj, time, clock->current_time);

  gettimeofday(&tfnow, (struct timezone *)NULL);
  g_mutex_lock(clock->lock);
  now = tfnow.tv_sec*1000000LL+tfnow.tv_usec - clock->start_time;
  //now = clock->current_time + clock->adjust;

  diff = GST_CLOCK_DIFF(time, now);
  // if we are not behind wait a bit
  DEBUG("gst_clock: %s waiting for time %08llu %08llu %08lld\n", gst_element_get_name(GST_ELEMENT(obj)), time, now, diff);
   
  g_mutex_unlock(clock->lock);
  if (diff > 10000 ) {
    tfnow.tv_usec = (diff % 1000000);
    tfnow.tv_sec = diff / 1000000;
    // FIXME, this piece of code does not work with egcs optimisations on, had to use the following line
    if (!tfnow.tv_sec) {
      select(0, NULL, NULL, NULL, &tfnow);
    }
    else printf("gst_clock: %s waiting %u %llu %llu %llu seconds\n", gst_element_get_name(GST_ELEMENT(obj)), 
		    (int)tfnow.tv_sec, now, diff, time);
    //DEBUG("gst_clock: 0x%p waiting for time %llu %llu %lld %llu\n", obj, time, target, diff, now);
    //DEBUG("waiting %d.%08d\n",tfnow.tv_sec, tfnow.tv_usec);
    //DEBUG("gst_clock: 0x%p waiting done time %llu \n", obj, time);
  }
  DEBUG("gst_clock: %s waiting for time %08llu %08llu %08lld done \n", gst_element_get_name(GST_ELEMENT(obj)), time, now, diff);
 // clock->current_time = clock->start_time + time;
  //gst_clock_set(clock, time);

}
