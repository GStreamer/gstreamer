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

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstclock.h"

#define CLASS(clock)  GST_CLOCK_CLASS (G_OBJECT_GET_CLASS (clock))


static void		gst_clock_class_init		(GstClockClass *klass);
static void		gst_clock_init			(GstClock *clock);

static GstObjectClass *parent_class = NULL;
/* static guint gst_clock_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_clock_class_init,
      NULL,
      NULL,
      sizeof (GstObject),
      4,
      (GInstanceInitFunc) gst_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_OBJECT, "GstClock", 
		    			 &clock_info, G_TYPE_FLAG_ABSTRACT);
  }
  return clock_type;
}

static void
gst_clock_class_init (GstClockClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);
}

static void
gst_clock_init (GstClock *clock)
{
  clock->speed = 1.0;
  clock->active = FALSE;
  clock->start_time = 0;

  clock->active_mutex = g_mutex_new ();
  clock->active_cond = g_cond_new ();
}

void
gst_clock_reset (GstClock *clock)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  clock->start_time = 0;
  clock->active = FALSE;

  if (CLASS (clock)->reset)
    CLASS (clock)->reset (clock);
}

void
gst_clock_activate (GstClock *clock, gboolean active)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  clock->active = active;

  if (CLASS (clock)->activate)
    CLASS (clock)->activate (clock, active);

  g_mutex_lock (clock->active_mutex);	
  g_cond_signal (clock->active_cond);	
  g_mutex_unlock (clock->active_mutex);	

}

gboolean
gst_clock_is_active (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);

  return clock->active;
}

void
gst_clock_set_time (GstClock *clock, GstClockTime time)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  if (CLASS (clock)->set_time)
    CLASS (clock)->set_time (clock, time);
}

GstClockTime
gst_clock_get_time (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  if (CLASS (clock)->get_time)
    return CLASS (clock)->get_time (clock);

  return 0LL;
}

GstClockReturn
gst_clock_wait (GstClock *clock, GstClockTime time)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_STOPPED);

  if (!clock->active) {
    g_mutex_lock (clock->active_mutex);	
    g_cond_wait (clock->active_cond, clock->active_mutex);	
    g_mutex_unlock (clock->active_mutex);	
  }
  if (CLASS (clock)->wait)
    return CLASS (clock)->wait (clock, time);

  return GST_CLOCK_TIMEOUT;
}

void
gst_clock_set_resolution (GstClock *clock, guint64 resolution)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  if (CLASS (clock)->set_resolution)
    CLASS (clock)->set_resolution (clock, resolution);
}

guint64
gst_clock_get_resolution (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  if (CLASS (clock)->get_resolution)
    return CLASS (clock)->get_resolution (clock);

  return 0LL;
}

