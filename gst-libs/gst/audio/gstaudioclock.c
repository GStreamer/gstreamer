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

/**
 * SECTION:gstaudioclock
 * @short_description: Helper object for implementing audio clocks
 * @see_also: #GstBaseAudioSink, #GstSystemClock
 *
 * #GstAudioClock makes it easy for elements to implement a #GstClock, they
 * simply need to provide a function that returns the current clock time.
 *
 * This object is internally used to implement the clock in #GstBaseAudioSink.
 *
 * Last reviewed on 2006-09-27 (0.10.12)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaudioclock.h"

static void gst_audio_clock_class_init (GstAudioClockClass * klass);
static void gst_audio_clock_init (GstAudioClock * clock);

static GstClockTime gst_audio_clock_get_internal_time (GstClock * clock);

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

  parent_class = g_type_class_peek_parent (klass);

  gstclock_class->get_internal_time = gst_audio_clock_get_internal_time;
}

static void
gst_audio_clock_init (GstAudioClock * clock)
{
  clock->last_time = 0;
  clock->abidata.ABI.time_offset = 0;
  GST_OBJECT_FLAG_SET (clock, GST_CLOCK_FLAG_CAN_SET_MASTER);
}

/**
 * gst_audio_clock_new:
 * @name: the name of the clock
 * @func: a function
 * @user_data: user data
 *
 * Create a new #GstAudioClock instance. Whenever the clock time should be
 * calculated it will call @func with @user_data. When @func returns
 * #GST_CLOCK_TIME_NONE, the clock will return the last reported time.
 *
 * Returns: a new #GstAudioClock casted to a #GstClock.
 */
GstClock *
gst_audio_clock_new (gchar * name, GstAudioClockGetTimeFunc func,
    gpointer user_data)
{
  GstAudioClock *aclock =
      GST_AUDIO_CLOCK (g_object_new (GST_TYPE_AUDIO_CLOCK, "name", name, NULL));

  aclock->func = func;
  aclock->user_data = user_data;

  return (GstClock *) aclock;
}

/**
 * gst_audio_clock_reset:
 * @clock: a #GstAudioClock
 * @time: a #GstClockTime
 *
 * Inform @clock that future calls to #GstAudioClockGetTimeFunc will return values
 * starting from @time. The clock will update an internal offset to make sure that
 * future calls to internal_time will return an increasing result as required by
 * the #GstClock object.
 */
void
gst_audio_clock_reset (GstAudioClock * clock, GstClockTime time)
{
  if (clock->last_time >= time)
    clock->abidata.ABI.time_offset = clock->last_time - time;
  else
    clock->abidata.ABI.time_offset = -(time - clock->last_time);
}

static GstClockTime
gst_audio_clock_get_internal_time (GstClock * clock)
{
  GstAudioClock *aclock;
  GstClockTime result;

  aclock = GST_AUDIO_CLOCK (clock);

  result = aclock->func (clock, aclock->user_data);
  if (result == GST_CLOCK_TIME_NONE)
    result = aclock->last_time;
  else {
    result += aclock->abidata.ABI.time_offset;
    aclock->last_time = result;
  }
  return result;
}
