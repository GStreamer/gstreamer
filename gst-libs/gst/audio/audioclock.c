/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * audioclock.c: Clock for use by audio plugins
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

#include "audioclock.h"

static void gst_audio_clock_class_init (GstAudioClockClass * klass);
static void gst_audio_clock_init (GstAudioClock * clock);

static GstClockTime gst_audio_clock_get_internal_time (GstClock * clock);
static GstClockReturn gst_audio_clock_id_wait_async (GstClock * clock,
    GstClockEntry * entry);
static void gst_audio_clock_id_unschedule (GstClock * clock,
    GstClockEntry * entry);

static GstSystemClockClass *parent_class = NULL;

/* static guint gst_audio_clock_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_audio_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstAudioClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_audio_clock_class_init,
      NULL,
      NULL,
      sizeof (GstAudioClock),
      4,
      (GInstanceInitFunc) gst_audio_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_SYSTEM_CLOCK, "GstAudioClock",
	&clock_info, 0);
  }
  return clock_type;
}


static void
gst_audio_clock_class_init (GstAudioClockClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstClockClass *gstclock_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstclock_class = (GstClockClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_SYSTEM_CLOCK);

  gstclock_class->get_internal_time = gst_audio_clock_get_internal_time;
  gstclock_class->wait_async = gst_audio_clock_id_wait_async;
  gstclock_class->unschedule = gst_audio_clock_id_unschedule;
}

static void
gst_audio_clock_init (GstAudioClock * clock)
{
  gst_object_set_name (GST_OBJECT (clock), "GstAudioClock");

  clock->prev1 = 0;
  clock->prev2 = 0;
}

GstClock *
gst_audio_clock_new (gchar * name, GstAudioClockGetTimeFunc func,
    gpointer user_data)
{
  GstAudioClock *aclock =
      GST_AUDIO_CLOCK (g_object_new (GST_TYPE_AUDIO_CLOCK, NULL));

  aclock->func = func;
  aclock->user_data = user_data;
  aclock->adjust = 0;

  return (GstClock *) aclock;
}

void
gst_audio_clock_set_active (GstAudioClock * aclock, gboolean active)
{
  GstClockTime time;
  GstClock *clock;

  g_return_if_fail (GST_IS_AUDIO_CLOCK (aclock));
  clock = GST_CLOCK (aclock);

  time = gst_clock_get_event_time (clock);

  if (active) {
    aclock->adjust = time - aclock->func (clock, aclock->user_data);
  } else {
    GTimeVal timeval;

    g_get_current_time (&timeval);

    aclock->adjust = GST_TIMEVAL_TO_TIME (timeval) - time;
  }

  aclock->active = active;
}

static GstClockTime
gst_audio_clock_get_internal_time (GstClock * clock)
{
  GstAudioClock *aclock = GST_AUDIO_CLOCK (clock);

  if (aclock->active) {
    return aclock->func (clock, aclock->user_data) + aclock->adjust;
  } else {
    GTimeVal timeval;

    g_get_current_time (&timeval);
    return GST_TIMEVAL_TO_TIME (timeval);
  }
}

void
gst_audio_clock_update_time (GstAudioClock * aclock, GstClockTime time)
{
  /* I don't know of a purpose in updating these; perhaps they can be removed */
  aclock->prev2 = aclock->prev1;
  aclock->prev1 = time;

  /* FIXME: the wait_async subsystem should be made threadsafe, but I don't want
   * to lock and unlock a mutex on every iteration... */
  while (aclock->async_entries) {
    GstClockEntry *entry = (GstClockEntry *) aclock->async_entries->data;

    if (entry->time > time)
      break;

    entry->func ((GstClock *) aclock, time, entry, entry->user_data);

    aclock->async_entries = g_slist_delete_link (aclock->async_entries,
	aclock->async_entries);
    /* do I need to free the entry? */
  }
}

static gint
compare_clock_entries (GstClockEntry * entry1, GstClockEntry * entry2)
{
  return entry1->time - entry2->time;
}

static GstClockReturn
gst_audio_clock_id_wait_async (GstClock * clock, GstClockEntry * entry)
{
  GstAudioClock *aclock = (GstAudioClock *) clock;

  aclock->async_entries = g_slist_insert_sorted (aclock->async_entries,
      entry, (GCompareFunc) compare_clock_entries);

  /* is this the proper return val? */
  return GST_CLOCK_EARLY;
}

static void
gst_audio_clock_id_unschedule (GstClock * clock, GstClockEntry * entry)
{
  GstAudioClock *aclock = (GstAudioClock *) clock;

  aclock->async_entries = g_slist_remove (aclock->async_entries, entry);
}
