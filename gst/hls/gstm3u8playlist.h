/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * gstm3u8playlist.h:
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

#ifndef __GST_M3U8_PLAYLIST_H__
#define __GST_M3U8_PLAYLIST_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GstM3U8Playlist GstM3U8Playlist;
typedef struct _GstM3U8Entry GstM3U8Entry;


struct _GstM3U8Entry
{
  gfloat duration;
  gchar *title;
  gchar *url;
  GFile *file;
  gboolean discontinuous;
};

struct _GstM3U8Playlist
{
  guint version;
  gboolean allow_cache;
  gint window_size;
  gint type;
  gboolean end_list;
  guint sequence_number;

  /*< Private >*/
  GQueue *entries;
  GString *playlist_str;
};


GstM3U8Playlist * gst_m3u8_playlist_new (guint version, 
				         guint window_size,
					 gboolean allow_cache);
void gst_m3u8_playlist_free (GstM3U8Playlist * playlist);
gboolean gst_m3u8_playlist_add_entry (GstM3U8Playlist * playlist,
    				     const gchar * url,
    				     GFile * file,
				     const gchar *title,
				     gfloat duration,
				     guint index,
				     gboolean discontinuous);
gchar * gst_m3u8_playlist_render (GstM3U8Playlist * playlist); 
void gst_m3u8_playlist_clear (GstM3U8Playlist * playlist); 
guint gst_m3u8_playlist_n_entries (GstM3U8Playlist * playlist); 

G_END_DECLS
#endif /* __M3U8_H__ */
