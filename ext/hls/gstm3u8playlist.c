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

#include "gstfragmented.h"
#include "gstm3u8playlist.h"

#define GST_CAT_DEFAULT fragmented_debug

#define M3U8_HEADER_TAG "#EXTM3U\n"
#define M3U8_VERSION_TAG "#EXT-X-VERSION:%d\n"
#define M3U8_ALLOW_CACHE_TAG "#EXT-X-ALLOW-CACHE:%s\n"
#define M3U8_TARGETDURATION_TAG "#EXT-X-TARGETDURATION:%d\n"
#define M3U8_MEDIA_SEQUENCE_TAG "#EXT-X-MEDIA-SEQUENCE:%d\n"
#define M3U8_DISCONTINUITY_TAG "#EXT-X-DISCONTINUITY\n"
#define M3U8_INT_INF_TAG "#EXTINF:%d,%s\n%s\n"
#define M3U8_FLOAT_INF_TAG "#EXTINF:%s,%s\n%s\n"
#define M3U8_ENDLIST_TAG "#EXT-X-ENDLIST"

enum
{
  GST_M3U8_PLAYLIST_TYPE_EVENT,
  GST_M3U8_PLAYLIST_TYPE_VOD,
};

static GstM3U8Entry *
gst_m3u8_entry_new (const gchar * url, GFile * file, const gchar * title,
    gfloat duration, gboolean discontinuous)
{
  GstM3U8Entry *entry;

  g_return_val_if_fail (url != NULL, NULL);
  g_return_val_if_fail (title != NULL, NULL);

  entry = g_new0 (GstM3U8Entry, 1);
  entry->url = g_strdup (url);
  entry->title = g_strdup (title);
  entry->duration = duration;
  entry->file = file;
  entry->discontinuous = discontinuous;
  return entry;
}

static void
gst_m3u8_entry_free (GstM3U8Entry * entry)
{
  g_return_if_fail (entry != NULL);

  g_free (entry->url);
  g_free (entry->title);
  if (entry->file != NULL)
    g_object_unref (entry->file);
  g_free (entry);
}

static gchar *
gst_m3u8_entry_render (GstM3U8Entry * entry, guint version)
{
  gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

  g_return_val_if_fail (entry != NULL, NULL);

  if (version < 3)
    return g_strdup_printf ("%s" M3U8_INT_INF_TAG,
        entry->discontinuous ? M3U8_DISCONTINUITY_TAG : "",
        (gint) ((entry->duration + 500 * GST_MSECOND) / GST_SECOND),
        entry->title, entry->url);

  return g_strdup_printf ("%s" M3U8_FLOAT_INF_TAG,
      entry->discontinuous ? M3U8_DISCONTINUITY_TAG : "",
      g_ascii_dtostr (buf, sizeof (buf), (entry->duration / GST_SECOND)),
      entry->title, entry->url);
}

GstM3U8Playlist *
gst_m3u8_playlist_new (guint version, guint window_size, gboolean allow_cache)
{
  GstM3U8Playlist *playlist;

  playlist = g_new0 (GstM3U8Playlist, 1);
  playlist->version = version;
  playlist->window_size = window_size;
  playlist->allow_cache = allow_cache;
  playlist->type = GST_M3U8_PLAYLIST_TYPE_EVENT;
  playlist->end_list = FALSE;
  playlist->entries = g_queue_new ();

  return playlist;
}

void
gst_m3u8_playlist_free (GstM3U8Playlist * playlist)
{
  g_return_if_fail (playlist != NULL);

  g_queue_foreach (playlist->entries, (GFunc) gst_m3u8_entry_free, NULL);
  g_queue_free (playlist->entries);
  g_free (playlist);
}


gboolean
gst_m3u8_playlist_add_entry (GstM3U8Playlist * playlist,
    const gchar * url, GFile * file, const gchar * title,
    gfloat duration, guint index, gboolean discontinuous)
{
  GstM3U8Entry *entry;

  g_return_val_if_fail (playlist != NULL, FALSE);
  g_return_val_if_fail (url != NULL, FALSE);
  g_return_val_if_fail (title != NULL, FALSE);

  if (playlist->type == GST_M3U8_PLAYLIST_TYPE_VOD)
    return FALSE;

  entry = gst_m3u8_entry_new (url, file, title, duration, discontinuous);

  if (playlist->window_size != -1) {
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
  gint i;
  GstM3U8Entry *entry;
  guint64 target_duration = 0;

  for (i = 0; i < playlist->entries->length; i++) {
    entry = (GstM3U8Entry *) g_queue_peek_nth (playlist->entries, i);
    if (entry->duration > target_duration)
      target_duration = entry->duration;
  }

  return (guint) ((target_duration + 500 * GST_MSECOND) / GST_SECOND);
}

static void
render_entry (GstM3U8Entry * entry, GstM3U8Playlist * playlist)
{
  gchar *entry_str;

  entry_str = gst_m3u8_entry_render (entry, playlist->version);
  g_string_append_printf (playlist->playlist_str, "%s", entry_str);
  g_free (entry_str);
}

gchar *
gst_m3u8_playlist_render (GstM3U8Playlist * playlist)
{
  gchar *pl;

  g_return_val_if_fail (playlist != NULL, NULL);

  playlist->playlist_str = g_string_new ("");

  /* #EXTM3U */
  g_string_append_printf (playlist->playlist_str, M3U8_HEADER_TAG);
  /* #EXT-X-VERSION */
  g_string_append_printf (playlist->playlist_str, M3U8_VERSION_TAG,
      playlist->version);
  /* #EXT-X-ALLOW_CACHE */
  g_string_append_printf (playlist->playlist_str, M3U8_ALLOW_CACHE_TAG,
      playlist->allow_cache ? "YES" : "NO");
  /* #EXT-X-MEDIA-SEQUENCE */
  g_string_append_printf (playlist->playlist_str, M3U8_MEDIA_SEQUENCE_TAG,
      playlist->sequence_number - playlist->entries->length);
  /* #EXT-X-TARGETDURATION */
  g_string_append_printf (playlist->playlist_str, M3U8_TARGETDURATION_TAG,
      gst_m3u8_playlist_target_duration (playlist));
  g_string_append_printf (playlist->playlist_str, "\n");

  /* Entries */
  g_queue_foreach (playlist->entries, (GFunc) render_entry, playlist);

  if (playlist->end_list)
    g_string_append_printf (playlist->playlist_str, M3U8_ENDLIST_TAG);

  pl = playlist->playlist_str->str;
  g_string_free (playlist->playlist_str, FALSE);
  return pl;
}

void
gst_m3u8_playlist_clear (GstM3U8Playlist * playlist)
{
  g_return_if_fail (playlist != NULL);

  g_queue_foreach (playlist->entries, (GFunc) gst_m3u8_entry_free, NULL);
  g_queue_clear (playlist->entries);
}

guint
gst_m3u8_playlist_n_entries (GstM3U8Playlist * playlist)
{
  g_return_val_if_fail (playlist != NULL, 0);

  return playlist->entries->length;
}
