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

static GstClockTime	gst_system_clock_get_internal_time	(GstClock *clock);
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

  gstclock_class->get_internal_time =	gst_system_clock_get_internal_time;
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
  return 1;
}


