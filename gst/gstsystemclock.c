/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstsystemclock.h"

#define CLASS(clock)  GST_SYSTEM_CLOCK_CLASS (G_OBJECT_GET_CLASS (clock))

static GstClock *_the_system_clock = NULL;

static void		gst_system_clock_class_init	(GstSystemClockClass *klass);
static void		gst_system_clock_init		(GstSystemClock *clock);

static void		gst_system_clock_activate 	(GstClock *clock, gboolean active);
static void		gst_system_clock_reset	 	(GstClock *clock);
static void		gst_system_clock_set_time	(GstClock *clock, GstClockTime time);
static GstClockTime	gst_system_clock_get_time	(GstClock *clock);
static GstClockReturn	gst_system_clock_wait 		(GstClock *clock, GstClockTime time);
static guint64		gst_system_clock_get_resolution (GstClock *clock);


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

  gstclock_class->activate = 		gst_system_clock_activate;
  gstclock_class->reset = 		gst_system_clock_reset;
  gstclock_class->set_time = 		gst_system_clock_set_time;
  gstclock_class->get_time = 		gst_system_clock_get_time;
  gstclock_class->wait =		gst_system_clock_wait;
  gstclock_class->get_resolution =	gst_system_clock_get_resolution;
}

static void
gst_system_clock_init (GstSystemClock *clock)
{
}

GstClock*
gst_system_clock_obtain (void)
{
  if (_the_system_clock == NULL) {
    _the_system_clock = GST_CLOCK (g_object_new (GST_TYPE_SYSTEM_CLOCK, NULL));
    gst_object_set_name (GST_OBJECT (_the_system_clock), "GstSystemClock");
  }
  return _the_system_clock;
}

static void
gst_system_clock_activate (GstClock *clock, gboolean active)
{
  GTimeVal timeval;
  GstSystemClock *sys_clock = GST_SYSTEM_CLOCK (clock);

  g_get_current_time (&timeval);
  GST_LOCK (clock);
  if (active) {
    sys_clock->absolute_start = GST_TIMEVAL_TO_TIME (timeval) - sys_clock->current_time;;
  }
  else {
    sys_clock->current_time = GST_TIMEVAL_TO_TIME (timeval) - sys_clock->absolute_start;
  }
  GST_UNLOCK (clock);
}

static void
gst_system_clock_set_time (GstClock *clock, GstClockTime time)
{
  GTimeVal timeval;
  GstSystemClock *sys_clock = GST_SYSTEM_CLOCK (clock);

  g_get_current_time (&timeval);

  GST_LOCK (clock);
  sys_clock->absolute_start = GST_TIMEVAL_TO_TIME (timeval) - time;
  sys_clock->current_time = time;
  GST_UNLOCK (clock);
}

static void
gst_system_clock_reset (GstClock *clock)
{
  gst_system_clock_set_time (clock, 0LL);
}

static GstClockTime
gst_system_clock_get_time (GstClock *clock)
{
  GstSystemClock *sys_clock = GST_SYSTEM_CLOCK (clock);
  GstClockTime res;

  if (!clock->active) {
    GST_LOCK (clock);
    res = sys_clock->current_time;
  }
  else {
    GTimeVal timeval;

    g_get_current_time (&timeval);

    GST_LOCK (clock);
    res = GST_TIMEVAL_TO_TIME (timeval) - sys_clock->absolute_start;
  }
  GST_UNLOCK (clock);

  return res;
}

static GstClockReturn
gst_system_clock_wait (GstClock *clock, GstClockTime time)
{
  GstClockTime target;
  GTimeVal timeval;
  GCond *cond = g_cond_new ();
  GstSystemClock *sys_clock = GST_SYSTEM_CLOCK (clock);
  GstClockReturn ret;

  GST_LOCK (clock);
  target = time + sys_clock->absolute_start;
	
  timeval.tv_usec = target % 1000000;
  timeval.tv_sec = target / 1000000;

  g_cond_timed_wait (cond, GST_GET_LOCK (clock), &timeval); 
  GST_UNLOCK (clock);

  ret = GST_CLOCK_TIMEOUT;

  g_cond_free (cond);

  return ret;
}

static guint64
gst_system_clock_get_resolution (GstClock *clock)
{
  return 1;
}


