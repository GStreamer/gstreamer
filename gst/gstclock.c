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
#define DEBUG_ENABLED
#include <gstclock.h>

static GstClock *the_system_clock = NULL;
static int num;

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
  num =0;
  clock->locking = TRUE;
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
    DEBUG("gst_clock: registered sink object 0x%p\n", obj);
    clock->sinkobjects = g_list_append(clock->sinkobjects, obj);
    num++;
  }
}

void gst_clock_set(GstClock *clock, GstClockTime time) {
  struct timeval tfnow;
  GstClockTime target, now;

  g_mutex_lock(clock->lock);
  gettimeofday(&tfnow, (struct timezone *)NULL);
  now = tfnow.tv_sec*1000000+tfnow.tv_usec;
  clock->adjust = now - (clock->start_time + time);
  clock->current_time = (clock->start_time + time);
  //DEBUG("gst_clock: setting clock to %llu %llu %lld\n", (guint64)clock->start_time+time, (guint64)now, (gint64)clock->adjust);
  g_mutex_unlock(clock->lock);
}

void gst_clock_reset(GstClock *clock) {
  struct timeval tfnow;

  gettimeofday(&tfnow, (struct timezone *)NULL);
  clock->start_time = tfnow.tv_sec*1000000+tfnow.tv_usec;
  clock->current_time = clock->start_time;
  clock->adjust = 0LL;
  DEBUG("gst_clock: setting start clock %llu\n", clock->start_time);
}

void gst_clock_wait(GstClock *clock, GstClockTime time, GstObject *obj) {
  struct timeval tfnow;
  GstClockTime target, now;
  GstClockTimeDiff diff;
  GList *elements;

  g_mutex_lock(clock->lock);
  elements = clock->sinkobjects;
  while (elements && clock->locking) {
    if (elements->data == obj) {
      DEBUG("gst_clock: registered sink object 0x%p\n", obj);
      num--;
      if (num) {
        DEBUG("gst_clock: 0x%p locked\n", obj);
        g_mutex_unlock(clock->lock);
        g_mutex_lock(clock->sinkmutex);
        g_mutex_lock(clock->lock);
	clock->locking = FALSE;
      }
      else {
	gst_clock_reset(clock);
        DEBUG("gst_clock: unlock all %p\n", obj);
        g_mutex_unlock(clock->sinkmutex);
	clock->locking = FALSE;
      }
      break;
    }
    elements = g_list_next(elements);
  }

  target = clock->start_time + time;
  gettimeofday(&tfnow, (struct timezone *)NULL);
  now = tfnow.tv_sec*1000000+tfnow.tv_usec + clock->adjust;
  //now = clock->current_time + clock->adjust;

  //DEBUG("gst_clock: 0x%p waiting for time %llu %llu\n", obj, time, target);

  diff = GST_CLOCK_DIFF(target, now);
  // if we are not behind wait a bit
   
  if (diff > 1000 ) {
    tfnow.tv_usec = diff % 1000000;
    tfnow.tv_sec = diff / 1000000;
    // FIXME, this piece of code does not work with egcs optimisations on, had to use the following line
    if (tfnow.tv_sec) fprintf(stderr, "gst_clock: waiting %u %llu %llu %llu seconds\n", (int)tfnow.tv_sec, now, diff, target);
    g_mutex_unlock(clock->lock);
    //DEBUG("gst_clock: 0x%p waiting for time %llu %llu %lld %llu\n", obj, time, target, diff, now);
    select(0, NULL, NULL, NULL, &tfnow);
    //DEBUG("gst_clock: 0x%p waiting done time %llu %llu\n", obj, time, target);
    g_mutex_lock(clock->lock);
  }
 // clock->current_time = clock->start_time + time;
  g_mutex_unlock(clock->lock);
  //gst_clock_set(clock, time);

}
