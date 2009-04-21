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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpegclock.h"

static void gst_mpeg_clock_class_init (GstMPEGClockClass * klass);
static void gst_mpeg_clock_init (GstMPEGClock * clock);

static GstClockTime gst_mpeg_clock_get_internal_time (GstClock * clock);

static GstSystemClockClass *parent_class = NULL;

/* static guint gst_mpeg_clock_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mpeg_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstMPEGClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_mpeg_clock_class_init,
      NULL,
      NULL,
      sizeof (GstMPEGClock),
      4,
      (GInstanceInitFunc) gst_mpeg_clock_init,
      NULL
    };

    clock_type = g_type_register_static (GST_TYPE_SYSTEM_CLOCK, "GstMPEGClock",
        &clock_info, 0);
  }
  return clock_type;
}


static void
gst_mpeg_clock_class_init (GstMPEGClockClass * klass)
{
  GstClockClass *gstclock_class;

  gstclock_class = (GstClockClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstclock_class->get_internal_time = gst_mpeg_clock_get_internal_time;
}

static void
gst_mpeg_clock_init (GstMPEGClock * clock)
{
  gst_object_set_name (GST_OBJECT (clock), "GstMPEGClock");
}

GstClock *
gst_mpeg_clock_new (gchar * name, GstMPEGClockGetTimeFunc func,
    gpointer user_data)
{
  GstMPEGClock *mpeg_clock =
      GST_MPEG_CLOCK (g_object_new (GST_TYPE_MPEG_CLOCK, NULL));

  mpeg_clock->func = func;
  mpeg_clock->user_data = user_data;

  return GST_CLOCK (mpeg_clock);
}

static GstClockTime
gst_mpeg_clock_get_internal_time (GstClock * clock)
{
  GstMPEGClock *mpeg_clock = GST_MPEG_CLOCK (clock);

  return mpeg_clock->func (clock, mpeg_clock->user_data);
}
