/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalsaclock.h"

/* clock functions */
static void gst_alsa_clock_class_init (gpointer g_class, gpointer class_data);
static void gst_alsa_clock_init (GstAlsaClock * clock);

static GstClockTime gst_alsa_clock_get_internal_time (GstClock * clock);
static guint64 gst_alsa_clock_get_resolution (GstClock * clock);
static GstClockEntryStatus gst_alsa_clock_wait (GstClock * clock,
    GstClockEntry * entry);
static void gst_alsa_clock_unlock (GstClock * clock, GstClockEntry * entry);

static GstClockClass *clock_parent_class = NULL;

/* static guint gst_alsa_clock_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_alsa_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstAlsaClockClass),
      NULL,
      NULL,
      gst_alsa_clock_class_init,
      NULL,
      NULL,
      sizeof (GstAlsaClock),
      4,
      (GInstanceInitFunc) gst_alsa_clock_init,
      NULL
    };

    clock_type = g_type_register_static (GST_TYPE_CLOCK, "GstAlsaClock",
        &clock_info, 0);
  }
  return clock_type;
}
static void
gst_alsa_clock_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstClockClass *gstclock_class;
  GstAlsaClockClass *klass;

  klass = (GstAlsaClockClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstclock_class = (GstClockClass *) klass;

  clock_parent_class = g_type_class_ref (GST_TYPE_CLOCK);

  gstclock_class->get_internal_time = gst_alsa_clock_get_internal_time;
  gstclock_class->get_resolution = gst_alsa_clock_get_resolution;
  gstclock_class->wait = gst_alsa_clock_wait;
  gstclock_class->unlock = gst_alsa_clock_unlock;
}
static void
gst_alsa_clock_init (GstAlsaClock * clock)
{
  gst_object_set_name (GST_OBJECT (clock), "GstAlsaClock");

  clock->start_time = GST_CLOCK_TIME_NONE;
}

GstAlsaClock *
gst_alsa_clock_new (gchar * name, GstAlsaClockGetTimeFunc get_time,
    GstAlsa * owner)
{
  GstAlsaClock *alsa_clock =
      GST_ALSA_CLOCK (g_object_new (GST_TYPE_ALSA_CLOCK, NULL));

  g_assert (alsa_clock);

  alsa_clock->get_time = get_time;
  alsa_clock->owner = owner;
  alsa_clock->adjust = 0;

  gst_object_set_name (GST_OBJECT (alsa_clock), name);
  gst_object_set_parent (GST_OBJECT (alsa_clock), GST_OBJECT (owner));

  return alsa_clock;
}

void
gst_alsa_clock_start (GstAlsaClock * clock)
{
  g_assert (!GST_CLOCK_TIME_IS_VALID (clock->start_time));

  if (clock->owner->format) {
    clock->start_time = gst_clock_get_event_time (GST_CLOCK (clock))
        - clock->get_time (clock->owner);
  } else {
    clock->start_time = gst_clock_get_event_time (GST_CLOCK (clock));
  }
}
void
gst_alsa_clock_stop (GstAlsaClock * clock)
{
  GTimeVal timeval;

  g_get_current_time (&timeval);

  g_assert (GST_CLOCK_TIME_IS_VALID (clock->start_time));

  clock->adjust +=
      GST_TIMEVAL_TO_TIME (timeval) -
      gst_clock_get_event_time (GST_CLOCK (clock));
  clock->start_time = GST_CLOCK_TIME_NONE;
}

static GstClockTime
gst_alsa_clock_get_internal_time (GstClock * clock)
{
  GstAlsaClock *alsa_clock = GST_ALSA_CLOCK (clock);

  if (GST_CLOCK_TIME_IS_VALID (alsa_clock->start_time)) {
    return alsa_clock->get_time (alsa_clock->owner) + alsa_clock->start_time;
  } else {
    GTimeVal timeval;

    g_get_current_time (&timeval);
    return GST_TIMEVAL_TO_TIME (timeval) + alsa_clock->adjust;
  }
}
static guint64
gst_alsa_clock_get_resolution (GstClock * clock)
{
  GstAlsaClock *this = GST_ALSA_CLOCK (clock);

  if (this->owner->format) {
    return GST_SECOND / this->owner->format->rate;
  } else {
    /* FIXME: is there an "unknown" value? We just return the sysclock's time by default */
    return 1 * GST_USECOND;
  }
}
static GstClockEntryStatus
gst_alsa_clock_wait (GstClock * clock, GstClockEntry * entry)
{
  GstClockTime target, entry_time;
  GstClockTimeDiff diff;
  GstAlsaClock *this = GST_ALSA_CLOCK (clock);

  entry_time = gst_alsa_clock_get_internal_time (clock);
  diff = GST_CLOCK_ENTRY_TIME (entry) - gst_clock_get_time (clock);

  if (diff < 0)
    return GST_CLOCK_ENTRY_EARLY;

  if (diff > clock->max_diff) {
    GST_INFO_OBJECT (this,
        "GstAlsaClock: abnormal clock request diff: %" G_GINT64_FORMAT ") >"
        "  %" G_GINT64_FORMAT, diff, clock->max_diff);
    return GST_CLOCK_ENTRY_EARLY;
  }

  target = entry_time + diff;

  GST_DEBUG_OBJECT (this, "real_target %" G_GUINT64_FORMAT
      " target %" G_GUINT64_FORMAT
      " now %" G_GUINT64_FORMAT,
      target, GST_CLOCK_ENTRY_TIME (entry), entry_time);

  while (gst_alsa_clock_get_internal_time (clock) < target &&
      this->last_unlock < entry_time) {
    g_usleep (gst_alsa_clock_get_resolution (clock) * G_USEC_PER_SEC /
        GST_SECOND);
  }

  return entry->status;
}
static void
gst_alsa_clock_unlock (GstClock * clock, GstClockEntry * entry)
{
  GstAlsaClock *this = GST_ALSA_CLOCK (clock);

  this->last_unlock = this->get_time (this->owner);
}
