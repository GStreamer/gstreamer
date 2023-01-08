/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "gstadaptivedemuxutils.h"

GST_DEBUG_CATEGORY_EXTERN (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

struct _GstAdaptiveDemuxClock
{
  gint ref_count;

  GstClock *gst_clock;
  GstClockTimeDiff clock_offset;        /* offset between realtime_clock and UTC */
};

struct _GstAdaptiveDemuxLoop
{
  gint ref_count;
  GCond cond;
  GMutex lock;

  GRecMutex context_lock;

  GThread *thread;
  GMainLoop *loop;
  GMainContext *context;

  gboolean stopped;
  gboolean paused;
};

GstAdaptiveDemuxClock *
gst_adaptive_demux_clock_new (void)
{
  GstAdaptiveDemuxClock *clock = g_new0 (GstAdaptiveDemuxClock, 1);
  GstClockType clock_type = GST_CLOCK_TYPE_OTHER;
  GObjectClass *gobject_class;

  g_atomic_int_set (&clock->ref_count, 1);

  clock->gst_clock = gst_system_clock_obtain ();
  g_assert (clock->gst_clock != NULL);

  gobject_class = G_OBJECT_GET_CLASS (clock->gst_clock);
  if (g_object_class_find_property (gobject_class, "clock-type")) {
    g_object_get (clock->gst_clock, "clock-type", &clock_type, NULL);
  } else {
    GST_WARNING ("System clock does not have clock-type property");
  }

  if (clock_type == GST_CLOCK_TYPE_REALTIME) {
    clock->clock_offset = 0;
  } else {
    GDateTime *utc_now;

    utc_now = g_date_time_new_now_utc ();
    gst_adaptive_demux_clock_set_utc_time (clock, utc_now);
    g_date_time_unref (utc_now);
  }

  return clock;
}

GstAdaptiveDemuxClock *
gst_adaptive_demux_clock_ref (GstAdaptiveDemuxClock * clock)
{
  g_return_val_if_fail (clock != NULL, NULL);
  g_atomic_int_inc (&clock->ref_count);
  return clock;
}

void
gst_adaptive_demux_clock_unref (GstAdaptiveDemuxClock * clock)
{
  g_return_if_fail (clock != NULL);
  if (g_atomic_int_dec_and_test (&clock->ref_count)) {
    gst_object_unref (clock->gst_clock);
    g_free (clock);
  }
}

GstClockTime
gst_adaptive_demux_clock_get_time (GstAdaptiveDemuxClock * clock)
{
  g_return_val_if_fail (clock != NULL, GST_CLOCK_TIME_NONE);
  return gst_clock_get_time (clock->gst_clock);
}

GDateTime *
gst_adaptive_demux_clock_get_now_utc (GstAdaptiveDemuxClock * clock)
{
  GstClockTime rtc_now;
  GDateTime *unix_datetime;
  GDateTime *result_datetime;
  gint64 utc_now_in_us;

  rtc_now = gst_clock_get_time (clock->gst_clock);
  utc_now_in_us = clock->clock_offset + GST_TIME_AS_USECONDS (rtc_now);
  unix_datetime =
      g_date_time_new_from_unix_utc (utc_now_in_us / G_TIME_SPAN_SECOND);
  result_datetime =
      g_date_time_add (unix_datetime, utc_now_in_us % G_TIME_SPAN_SECOND);
  g_date_time_unref (unix_datetime);
  return result_datetime;
}

void
gst_adaptive_demux_clock_set_utc_time (GstAdaptiveDemuxClock * clock,
    GDateTime * utc_now)
{
  GstClockTime rtc_now = gst_clock_get_time (clock->gst_clock);
  GstClockTimeDiff clock_offset;

  clock_offset =
      g_date_time_to_unix (utc_now) * G_TIME_SPAN_SECOND +
      g_date_time_get_microsecond (utc_now) - GST_TIME_AS_USECONDS (rtc_now);

  GST_INFO ("Changing UTC clock offset to %" GST_STIME_FORMAT
      " was %" GST_STIME_FORMAT, GST_STIME_ARGS (clock_offset),
      GST_STIME_ARGS (clock->clock_offset));

  clock->clock_offset = clock_offset;
}

GstAdaptiveDemuxLoop *
gst_adaptive_demux_loop_new (void)
{
  GstAdaptiveDemuxLoop *loop = g_new0 (GstAdaptiveDemuxLoop, 1);
  g_atomic_int_set (&loop->ref_count, 1);

  g_mutex_init (&loop->lock);
  g_rec_mutex_init (&loop->context_lock);
  g_cond_init (&loop->cond);

  loop->stopped = TRUE;
  loop->paused = FALSE;

  return loop;
}

GstAdaptiveDemuxLoop *
gst_adaptive_demux_loop_ref (GstAdaptiveDemuxLoop * loop)
{
  g_return_val_if_fail (loop != NULL, NULL);
  g_atomic_int_inc (&loop->ref_count);
  return loop;
}

void
gst_adaptive_demux_loop_unref (GstAdaptiveDemuxLoop * loop)
{
  g_return_if_fail (loop != NULL);
  if (g_atomic_int_dec_and_test (&loop->ref_count)) {
    gst_adaptive_demux_loop_stop (loop, TRUE);

    g_mutex_clear (&loop->lock);
    g_rec_mutex_clear (&loop->context_lock);
    g_cond_clear (&loop->cond);

    g_free (loop);
  }
}

static gpointer
_gst_adaptive_demux_loop_thread (GstAdaptiveDemuxLoop * loop)
{
  g_mutex_lock (&loop->lock);

  loop->loop = g_main_loop_new (loop->context, FALSE);

  while (!loop->stopped) {
    g_mutex_unlock (&loop->lock);

    g_rec_mutex_lock (&loop->context_lock);

    g_main_context_push_thread_default (loop->context);
    g_main_loop_run (loop->loop);
    g_main_context_pop_thread_default (loop->context);

    g_rec_mutex_unlock (&loop->context_lock);

    g_mutex_lock (&loop->lock);
    while (loop->paused)
      g_cond_wait (&loop->cond, &loop->lock);
  }

  g_main_loop_unref (loop->loop);
  loop->loop = NULL;

  g_cond_broadcast (&loop->cond);

  g_main_context_unref (loop->context);
  loop->context = NULL;

  g_mutex_unlock (&loop->lock);

  gst_adaptive_demux_loop_unref (loop);

  return NULL;
}

void
gst_adaptive_demux_loop_start (GstAdaptiveDemuxLoop * loop)
{
  g_mutex_lock (&loop->lock);
  if (loop->thread != NULL)
    goto done;                  /* Already running */

  loop->stopped = FALSE;
  loop->context = g_main_context_new ();

  loop->thread =
      g_thread_new ("AdaptiveDemux",
      (GThreadFunc) _gst_adaptive_demux_loop_thread,
      gst_adaptive_demux_loop_ref (loop));

done:
  g_mutex_unlock (&loop->lock);
}

static gboolean
do_quit_cb (GstAdaptiveDemuxLoop * loop)
{
  g_main_loop_quit (loop->loop);
  return G_SOURCE_REMOVE;
}

void
gst_adaptive_demux_loop_stop (GstAdaptiveDemuxLoop * loop, gboolean wait)
{
  g_mutex_lock (&loop->lock);

  if (!loop->stopped) {
    loop->stopped = TRUE;

    GSource *s = g_idle_source_new ();
    g_source_set_callback (s, (GSourceFunc) do_quit_cb,
        gst_adaptive_demux_loop_ref (loop),
        (GDestroyNotify) gst_adaptive_demux_loop_unref);
    g_source_attach (s, loop->context);
    g_source_unref (s);

    if (wait) {
      while (loop->loop != NULL)
        g_cond_wait (&loop->cond, &loop->lock);
    }

    if (loop->thread != NULL) {
      g_thread_unref (loop->thread);
      loop->thread = NULL;
    }
  }

  g_mutex_unlock (&loop->lock);
}

gboolean
gst_adaptive_demux_loop_pause_and_lock (GstAdaptiveDemuxLoop * loop)
{
  /* Try and acquire the context lock directly. This will succeed
   * if called when the loop is not running, and we can avoid
   * adding an unnecessary extra idle source to quit the loop. */
  if (!g_rec_mutex_trylock (&loop->context_lock)) {
    g_mutex_lock (&loop->lock);

    if (loop->stopped) {
      g_mutex_unlock (&loop->lock);
      return FALSE;
    }

    loop->paused = TRUE;

    {
      GSource *s = g_idle_source_new ();
      g_source_set_callback (s, (GSourceFunc) do_quit_cb,
          gst_adaptive_demux_loop_ref (loop),
          (GDestroyNotify) gst_adaptive_demux_loop_unref);
      g_source_attach (s, loop->context);
      g_source_unref (s);
    }

    g_mutex_unlock (&loop->lock);

    g_rec_mutex_lock (&loop->context_lock);
  }
  g_main_context_push_thread_default (loop->context);

  return TRUE;
}

gboolean
gst_adaptive_demux_loop_unlock_and_unpause (GstAdaptiveDemuxLoop * loop)
{
  gboolean stopped;

  g_main_context_pop_thread_default (loop->context);
  g_rec_mutex_unlock (&loop->context_lock);

  g_mutex_lock (&loop->lock);
  loop->paused = FALSE;

  stopped = loop->stopped;

  /* Wake up the loop to run again, regardless of stopped state */
  g_cond_broadcast (&loop->cond);
  g_mutex_unlock (&loop->lock);

  if (stopped)
    return FALSE;

  return TRUE;
}

guint
gst_adaptive_demux_loop_call (GstAdaptiveDemuxLoop * loop, GSourceFunc func,
    gpointer data, GDestroyNotify notify)
{
  guint ret = 0;

  g_mutex_lock (&loop->lock);
  if (loop->context) {
    GSource *s = g_idle_source_new ();
    g_source_set_callback (s, func, data, notify);
    ret = g_source_attach (s, loop->context);
    g_source_unref (s);
  } else if (notify != NULL) {
    notify (data);
  }

  g_mutex_unlock (&loop->lock);

  return ret;
}

guint
gst_adaptive_demux_loop_call_delayed (GstAdaptiveDemuxLoop * loop,
    GstClockTime delay, GSourceFunc func, gpointer data, GDestroyNotify notify)
{
  guint ret = 0;

  g_mutex_lock (&loop->lock);
  if (loop->context) {
    GSource *s = g_timeout_source_new (GST_TIME_AS_MSECONDS (delay));
    g_source_set_callback (s, func, data, notify);
    ret = g_source_attach (s, loop->context);
    g_source_unref (s);
  } else if (notify != NULL) {
    notify (data);
  }

  g_mutex_unlock (&loop->lock);

  return ret;
}

void
gst_adaptive_demux_loop_cancel_call (GstAdaptiveDemuxLoop * loop, guint cb_id)
{

  g_mutex_lock (&loop->lock);
  if (loop->context) {
    GSource *s = g_main_context_find_source_by_id (loop->context, cb_id);
    if (s)
      g_source_destroy (s);
  }
  g_mutex_unlock (&loop->lock);
}

struct Rfc5322TimeZone
{
  const gchar *name;
  gfloat tzoffset;
};

/*
 Parse an RFC5322 (section 3.3) date-time from the Date: field in the
 HTTP response.
 See https://tools.ietf.org/html/rfc5322#section-3.3
*/
GstDateTime *
gst_adaptive_demux_util_parse_http_head_date (const gchar * http_date)
{
  static const gchar *months[] = { NULL, "Jan", "Feb", "Mar", "Apr",
    "May", "Jun", "Jul", "Aug",
    "Sep", "Oct", "Nov", "Dec", NULL
  };
  static const struct Rfc5322TimeZone timezones[] = {
    {"Z", 0},
    {"UT", 0},
    {"GMT", 0},
    {"BST", 1},
    {"EST", -5},
    {"EDT", -4},
    {"CST", -6},
    {"CDT", -5},
    {"MST", -7},
    {"MDT", -6},
    {"PST", -8},
    {"PDT", -7},
    {NULL, 0}
  };
  gint ret;
  const gchar *pos;
  gint year = -1, month = -1, day = -1, hour = -1, minute = -1, second = -1;
  gchar zone[6];
  gchar monthstr[4];
  gfloat tzoffset = 0;
  gboolean parsed_tz = FALSE;

  g_return_val_if_fail (http_date != NULL, NULL);

  /* skip optional text version of day of the week */
  pos = strchr (http_date, ',');
  if (pos)
    pos++;
  else
    pos = http_date;

  ret =
      sscanf (pos, "%02d %3s %04d %02d:%02d:%02d %5s", &day, monthstr, &year,
      &hour, &minute, &second, zone);

  if (ret == 7) {
    gchar *z = zone;
    gint i;

    for (i = 1; months[i]; ++i) {
      if (g_ascii_strncasecmp (months[i], monthstr, strlen (months[i])) == 0) {
        month = i;
        break;
      }
    }
    for (i = 0; timezones[i].name && !parsed_tz; ++i) {
      if (g_ascii_strncasecmp (timezones[i].name, z,
              strlen (timezones[i].name)) == 0) {
        tzoffset = timezones[i].tzoffset;
        parsed_tz = TRUE;
      }
    }
    if (!parsed_tz) {
      gint hh, mm;
      gboolean neg = FALSE;
      /* check if it is in the form +-HHMM */
      if (*z == '+' || *z == '-') {
        if (*z == '+')
          ++z;
        else if (*z == '-') {
          ++z;
          neg = TRUE;
        }
        ret = sscanf (z, "%02d%02d", &hh, &mm);
        if (ret == 2) {
          tzoffset = hh;
          tzoffset += mm / 60.0;
          if (neg)
            tzoffset = -tzoffset;
          parsed_tz = TRUE;
        }
      }
    }
    /* Accept year in both 2 digit or 4 digit format */
    if (year < 100)
      year += 2000;
  }

  if (month < 1 || !parsed_tz)
    return NULL;

  return gst_date_time_new (tzoffset, year, month, day, hour, minute, second);
}

typedef struct
{
  gboolean delivered;
  GstEvent *event;
  guint sticky_order;
} PadEvent;

void
gst_event_store_init (GstEventStore * store)
{
  store->events = g_array_sized_new (FALSE, TRUE, sizeof (PadEvent), 16);
  store->events_pending = FALSE;
}

void
gst_event_store_flush (GstEventStore * store)
{
  guint i, len;
  GArray *events = store->events;

  len = events->len;
  for (i = 0; i < len; i++) {
    PadEvent *ev = &g_array_index (events, PadEvent, i);
    GstEvent *event = ev->event;

    ev->event = NULL;

    gst_event_unref (event);
  }
  g_array_set_size (events, 0);

  store->events_pending = FALSE;
}

void
gst_event_store_deinit (GstEventStore * store)
{
  gst_event_store_flush (store);
  g_array_free (store->events, TRUE);
}

void
gst_event_store_insert_event (GstEventStore * store, GstEvent * event,
    gboolean delivered)
{
  guint i, len;
  GArray *events;
  GQuark name_id = 0;
  gboolean insert = TRUE;

  GstEventType type = GST_EVENT_TYPE (event);
  guint event_sticky_order = gst_event_type_to_sticky_ordering (type);

  if (type & GST_EVENT_TYPE_STICKY_MULTI)
    name_id = gst_structure_get_name_id (gst_event_get_structure (event));

  events = store->events;

  len = events->len;
  for (i = 0; i < len; i++) {
    PadEvent *ev = &g_array_index (events, PadEvent, i);

    if (ev->event == NULL)
      continue;

    if (type == GST_EVENT_TYPE (ev->event)) {
      /* matching types, check matching name if needed */
      if (name_id && !gst_event_has_name_id (ev->event, name_id))
        continue;

      /* overwrite if different */
      if (gst_event_replace (&ev->event, event)) {
        ev->delivered = delivered;
        /* If the event was not delivered, mark that we have a pending
         * undelivered event */
        if (!delivered)
          store->events_pending = TRUE;
      }

      insert = FALSE;
      break;
    }

    if (event_sticky_order < ev->sticky_order
        || (type != GST_EVENT_TYPE (ev->event)
            && GST_EVENT_TYPE (ev->event) == GST_EVENT_EOS)) {
      /* STREAM_START, CAPS and SEGMENT must be delivered in this order. By
       * storing the sticky ordered we can check that this is respected. */
      if (G_UNLIKELY (ev->sticky_order <=
              gst_event_type_to_sticky_ordering (GST_EVENT_SEGMENT)
              || GST_EVENT_TYPE (ev->event) == GST_EVENT_EOS)) {
        g_warning (G_STRLOC
            ":%s:<store %p> Sticky event misordering, got '%s' before '%s'",
            G_STRFUNC, store,
            gst_event_type_get_name (GST_EVENT_TYPE (ev->event)),
            gst_event_type_get_name (type));
      }
      break;
    }
  }
  if (insert) {
    PadEvent ev;
    ev.event = gst_event_ref (event);
    ev.sticky_order = event_sticky_order;
    ev.delivered = delivered;
    g_array_insert_val (events, i, ev);

    /* If the event was not delivered, mark that we have a pending
     * undelivered event */
    if (!delivered)
      store->events_pending = TRUE;
    GST_LOG ("store %p stored sticky event %s", store,
        GST_EVENT_TYPE_NAME (event));
  }
}

/* Find the first non-pending event and return a ref to it, owned by the caller */
GstEvent *
gst_event_store_get_next_pending (GstEventStore * store)
{
  GArray *events;
  guint i, len;

  if (!store->events_pending)
    return NULL;

  events = store->events;
  len = events->len;
  for (i = 0; i < len; i++) {
    PadEvent *ev = &g_array_index (events, PadEvent, i);

    if (ev->event == NULL || ev->delivered)
      continue;

    /* Found an undelivered event, return it. The caller will mark it
     * as delivered once it has done so successfully by calling
     * gst_event_store_mark_delivered() */
    return gst_event_ref (ev->event);
  }

  store->events_pending = FALSE;
  return NULL;
}

void
gst_event_store_mark_delivered (GstEventStore * store, GstEvent * event)
{
  gboolean events_pending = FALSE;
  GArray *events;
  guint i, len;

  events = store->events;
  len = events->len;
  for (i = 0; i < len; i++) {
    PadEvent *ev = &g_array_index (events, PadEvent, i);

    if (ev->event == NULL)
      continue;

    /* Check if there are any pending events other than
     * the passed one, so we can update the events_pending
     * flag at the end */
    if (ev->event != event && !ev->delivered) {
      events_pending = TRUE;
      continue;
    }

    ev->delivered = TRUE;
  }

  store->events_pending = events_pending;
}

void
gst_event_store_mark_all_undelivered (GstEventStore * store)
{
  gboolean events_pending = FALSE;
  GArray *events;
  guint i, len;

  events = store->events;
  len = events->len;
  for (i = 0; i < len; i++) {
    PadEvent *ev = &g_array_index (events, PadEvent, i);

    if (ev->event == NULL)
      continue;

    ev->delivered = FALSE;
    events_pending = TRUE;
  }

  /* Only set the flag if there was at least
   * one sticky event in the store */
  store->events_pending = events_pending;
}
