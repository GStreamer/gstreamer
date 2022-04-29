/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * gstm3u8playlist.c:
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

#include <glib.h>

#include "gstm3u8playlist.h"
#include "gsthlselements.h"

#define GST_CAT_DEFAULT hls_debug

enum
{
  GST_M3U8_PLAYLIST_TYPE_EVENT,
  GST_M3U8_PLAYLIST_TYPE_VOD,
};

typedef struct _GstM3U8Entry GstM3U8Entry;

struct _GstM3U8Entry
{
  gfloat duration;
  gchar *title;
  gchar *url;
  gboolean discontinuous;
  GDateTime *program_dt;
};

static GstM3U8Entry *
gst_m3u8_entry_new (const gchar * url, const gchar * title,
    gfloat duration, gboolean discontinuous, GDateTime * program_dt)
{
  GstM3U8Entry *entry;

  g_return_val_if_fail (url != NULL, NULL);

  entry = g_new0 (GstM3U8Entry, 1);
  entry->url = g_strdup (url);
  entry->title = g_strdup (title);
  entry->duration = duration;
  entry->discontinuous = discontinuous;
  entry->program_dt = program_dt;
  return entry;
}

static void
gst_m3u8_entry_free (GstM3U8Entry * entry)
{
  g_return_if_fail (entry != NULL);

  g_free (entry->url);
  g_free (entry->title);
  g_clear_pointer (&entry->program_dt, g_date_time_unref);
  g_free (entry);
}

GstM3U8Playlist *
gst_m3u8_playlist_new (guint version, guint window_size)
{
  GstM3U8Playlist *playlist;

  playlist = g_new0 (GstM3U8Playlist, 1);
  playlist->version = version;
  playlist->window_size = window_size;
  playlist->type = GST_M3U8_PLAYLIST_TYPE_EVENT;
  playlist->end_list = FALSE;
  playlist->entries = g_queue_new ();
  playlist->start_dt = NULL;

  return playlist;
}

void
gst_m3u8_playlist_free (GstM3U8Playlist * playlist)
{
  g_return_if_fail (playlist != NULL);

  g_queue_foreach (playlist->entries, (GFunc) gst_m3u8_entry_free, NULL);
  g_queue_free (playlist->entries);
  g_clear_pointer (&playlist->start_dt, g_date_time_unref);
  g_free (playlist);
}

static gchar *
_gst_m3u8_playlist_date_time_format_iso8601z (GDateTime * datetime)
{
  GString *outstr = NULL;
  const gchar *format = "%C%y-%m-%dT%H:%M:%S";
  gchar *main_date = g_date_time_format (datetime, format);
  outstr = g_string_new (main_date);
  g_string_append_printf (outstr, ".%03" G_GUINT64_FORMAT "Z",
      gst_util_uint64_scale_round (g_date_time_get_microsecond (datetime), 1,
          G_TIME_SPAN_MILLISECOND));
  g_free (main_date);
  return g_string_free (outstr, FALSE);
}

void
gst_m3u8_playlist_calc_start_date_time (GstM3U8Playlist * playlist,
    GstClockTime running_time, GstElement * sink)
{
  GstClock *clock;
  GstClockTime base_time, now_time, pts_clock_time;
  GstClockTimeDiff diff;
  gdouble add_s;
  GDateTime *now_utc, *start_date_time;

  clock = gst_element_get_clock (sink);
  if (!clock) {
    GST_WARNING_OBJECT (sink,
        "element has no clock, can't determine PROGRAM_DATE_TIME");
    return;
  }
  base_time = gst_element_get_base_time (sink);
  now_time = gst_clock_get_time (clock);
  now_utc = g_date_time_new_now_utc ();
  pts_clock_time = running_time + base_time;
  diff = (now_time - pts_clock_time);
  add_s = (-0.001) * GST_TIME_AS_MSECONDS (diff);
  start_date_time = g_date_time_add_seconds (now_utc, add_s);

  if (G_UNLIKELY (GST_LEVEL_DEBUG <= _gst_debug_min) &&
      GST_LEVEL_DEBUG <= gst_debug_category_get_threshold (GST_CAT_DEFAULT)) {
    gchar *fmt_now_utc = _gst_m3u8_playlist_date_time_format_iso8601z (now_utc);
    gchar *fmt_start_date_time =
        _gst_m3u8_playlist_date_time_format_iso8601z (start_date_time);

    /* *INDENT-OFF* */
    GST_DEBUG_OBJECT (sink, "Calculating start PROGRAM_DATE_TIME:\n"
        "        base_time = %" GST_TIME_FORMAT " [hlssink element base time]\n"
        "         now_time = %" GST_TIME_FORMAT " [current wall clock time]\n"
        "     running_time = %" GST_TIME_FORMAT " [running time at beginning of fragment]\n"
        "   pts_clock_time = %" GST_TIME_FORMAT " [running + base time]\n"
        "             diff = %" GST_STIME_FORMAT " (add_s=%f) [now_time - pts_clock_time]\n"
        "          now_utc = %s\n"
        "  start_date_time = %s",
        GST_TIME_ARGS (base_time), GST_TIME_ARGS (now_time),
        GST_TIME_ARGS (running_time), GST_TIME_ARGS (pts_clock_time),
        GST_STIME_ARGS (diff), add_s, fmt_now_utc, fmt_start_date_time);
    /* *INDENT-ON* */

    g_free (fmt_now_utc);
    g_free (fmt_start_date_time);
  }

  g_date_time_unref (now_utc);
  gst_object_unref (clock);
  if (playlist->start_dt)
    g_date_time_unref (playlist->start_dt);
  playlist->start_dt = start_date_time;
}

gboolean
gst_m3u8_playlist_add_entry (GstM3U8Playlist * playlist,
    const gchar * url, const gchar * title,
    gfloat duration, guint index, gboolean discontinuous)
{
  GstM3U8Entry *entry;

  g_return_val_if_fail (playlist != NULL, FALSE);
  g_return_val_if_fail (url != NULL, FALSE);

  if (playlist->type == GST_M3U8_PLAYLIST_TYPE_VOD)
    return FALSE;

  entry = gst_m3u8_entry_new (url, title, duration, discontinuous, NULL);

  if (playlist->window_size > 0) {
    /* Delete old entries from the playlist */
    while (playlist->entries->length >= playlist->window_size) {
      GstM3U8Entry *old_entry;

      old_entry = g_queue_pop_head (playlist->entries);
      gst_m3u8_entry_free (old_entry);
    }
  }

  playlist->sequence_number = index + 1;
  g_queue_push_tail (playlist->entries, entry);

  return TRUE;
}

gboolean
gst_m3u8_playlist_add_entry_with_pts (GstM3U8Playlist * playlist,
    const gchar * url, const gchar * title, GstClockTime start_time,
    GstClockTime end_time, guint index, gboolean discontinuous)
{
  GstM3U8Entry *entry;
  gfloat duration = 0;
  GDateTime *program_dt = NULL;

  g_return_val_if_fail (playlist != NULL, FALSE);
  g_return_val_if_fail (url != NULL, FALSE);

  if (playlist->type == GST_M3U8_PLAYLIST_TYPE_VOD)
    return FALSE;

  if (GST_CLOCK_TIME_IS_VALID (start_time)) {
    if (GST_CLOCK_TIME_IS_VALID (end_time) && end_time >= start_time) {
      duration = GST_CLOCK_DIFF (start_time, end_time);
    }

    if (playlist->start_dt) {
      gdouble add_s;

      add_s = start_time / (gdouble) GST_SECOND;
      program_dt = g_date_time_add_seconds (playlist->start_dt, add_s);
    }
  }

  entry = gst_m3u8_entry_new (url, title, duration, discontinuous, program_dt);

  if (playlist->window_size > 0) {
    /* Delete old entries from the playlist */
    while (playlist->entries->length >= playlist->window_size) {
      GstM3U8Entry *old_entry;

      old_entry = g_queue_pop_head (playlist->entries);
      gst_m3u8_entry_free (old_entry);
    }
  }

  playlist->sequence_number = index + 1;
  g_queue_push_tail (playlist->entries, entry);

  return TRUE;
}

static guint
gst_m3u8_playlist_target_duration (GstM3U8Playlist * playlist)
{
  guint64 target_duration = 0;
  GList *l;

  for (l = playlist->entries->head; l != NULL; l = l->next) {
    GstM3U8Entry *entry = l->data;

    if (entry->duration > target_duration)
      target_duration = entry->duration;
  }

  return (guint) ((target_duration + 500 * GST_MSECOND) / GST_SECOND);
}

gchar *
gst_m3u8_playlist_render (GstM3U8Playlist * playlist)
{
  GString *playlist_str;
  GList *l = playlist->entries->head;
  gchar *program_dt = NULL;
  GstM3U8Entry *entry = l != NULL ? l->data : NULL;

  g_return_val_if_fail (playlist != NULL, NULL);

  playlist_str = g_string_new ("#EXTM3U\n");

  g_string_append_printf (playlist_str, "#EXT-X-VERSION:%d\n",
      playlist->version);

  g_string_append_printf (playlist_str, "#EXT-X-MEDIA-SEQUENCE:%d\n",
      playlist->sequence_number - playlist->entries->length);

  g_string_append_printf (playlist_str, "#EXT-X-TARGETDURATION:%u\n",
      gst_m3u8_playlist_target_duration (playlist));

  if (entry && entry->program_dt) {
    program_dt =
        _gst_m3u8_playlist_date_time_format_iso8601z (entry->program_dt);
    g_string_append_printf (playlist_str, "#EXT-X-PROGRAM-DATE-TIME:%s\n",
        program_dt);
    g_free (program_dt);
  }

  g_string_append (playlist_str, "\n");

  /* Entries */
  for (; l != NULL; l = l->next) {
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    GstM3U8Entry *entry = l->data;

    if (entry->discontinuous)
      g_string_append (playlist_str, "#EXT-X-DISCONTINUITY\n");

    if (playlist->version < 3) {
      g_string_append_printf (playlist_str, "#EXTINF:%d,%s\n",
          (gint) ((entry->duration + 500 * GST_MSECOND) / GST_SECOND),
          entry->title ? entry->title : "");
    } else {
      g_string_append_printf (playlist_str, "#EXTINF:%s,%s\n",
          g_ascii_dtostr (buf, sizeof (buf), entry->duration / GST_SECOND),
          entry->title ? entry->title : "");
    }

    g_string_append_printf (playlist_str, "%s\n", entry->url);
  }

  if (playlist->end_list)
    g_string_append (playlist_str, "#EXT-X-ENDLIST");

  return g_string_free (playlist_str, FALSE);
}
