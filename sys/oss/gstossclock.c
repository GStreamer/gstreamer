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

#include "gstossclock.h"

static void 		gst_oss_clock_class_init 	(GstOssClockClass *klass);
static void 		gst_oss_clock_init 		(GstOssClock *clock);

static GstClockTime	gst_oss_clock_get_internal_time 		(GstClock *clock);

static GstSystemClockClass *parent_class = NULL;
/* static guint gst_oss_clock_signals[LAST_SIGNAL] = { 0 }; */
  
GType
gst_oss_clock_get_type (void)
{ 
  static GType clock_type = 0;
	    
  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstOssClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_oss_clock_class_init,
      NULL,
      NULL,
      sizeof (GstOssClock),
      4,
      (GInstanceInitFunc) gst_oss_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_SYSTEM_CLOCK, "GstOssClock",
                                         &clock_info, 0);
  }
  return clock_type;
}


static void
gst_oss_clock_class_init (GstOssClockClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstClockClass *gstclock_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;
  gstclock_class = (GstClockClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_SYSTEM_CLOCK);

  gstclock_class->get_internal_time =	gst_oss_clock_get_internal_time;
}

static void
gst_oss_clock_init (GstOssClock *clock)
{
  gst_object_set_name (GST_OBJECT (clock), "GstOssClock");

  clock->prev1 = 0;
  clock->prev2 = 0;
}

GstOssClock*
gst_oss_clock_new (gchar *name, GstOssClockGetTimeFunc func, gpointer user_data)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (g_object_new (GST_TYPE_OSS_CLOCK, NULL));

  oss_clock->func = func;
  oss_clock->user_data = user_data;
  oss_clock->adjust = 0;

  return oss_clock;
}

void
gst_oss_clock_set_active (GstClock *clock, gboolean active)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);
  GTimeVal timeval;
  GstClockTime time;
  GstClockTime osstime;

  g_get_current_time (&timeval);
  time = GST_TIMEVAL_TO_TIME (timeval);
  osstime = oss_clock->func (clock, oss_clock->user_data);

  if (active) {
    oss_clock->adjust = time - osstime;
  }
  else {
    oss_clock->adjust = osstime - time;
  }

  oss_clock->active = active;
}

static GstClockTime
gst_oss_clock_get_internal_time (GstClock *clock)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);
  
  if (oss_clock->active) {
    GstClockTime osstime;
    
    osstime = oss_clock->func (clock, oss_clock->user_data) + oss_clock->adjust;

    return osstime;
  }
  else {
    GstClockTime time;
    GTimeVal timeval;
    
    g_get_current_time (&timeval);
    time = GST_TIMEVAL_TO_TIME (timeval);

    return time;
  }
}

