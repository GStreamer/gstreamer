/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * m3u8.h:
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

#ifndef __M3U8_H__
#define __M3U8_H__

#include <glib.h>

G_BEGIN_DECLS typedef struct _GstM3U8 GstM3U8;
typedef struct _GstM3U8MediaFile GstM3U8MediaFile;
typedef struct _GstM3U8Client GstM3U8Client;

#define GST_M3U8(m) ((GstM3U8*)m)
#define GST_M3U8_MEDIA_FILE(f) ((GstM3U8MediaFile*)f)

#define GST_M3U8_CLIENT_LOCK(c) g_mutex_lock (&c->lock);
#define GST_M3U8_CLIENT_UNLOCK(c) g_mutex_unlock (&c->lock);

struct _GstM3U8
{
  gchar *uri;                   /* actually downloaded URI */
  gchar *base_uri;              /* URI to use as base for resolving relative URIs.
                                 * This will be different to uri in case of redirects */
  gchar *name;                  /* This will be the "name" of the playlist, the original
                                 * relative/absolute uri in a variant playlist */

  gboolean endlist;             /* if ENDLIST has been reached */
  gint version;                 /* last EXT-X-VERSION */
  GstClockTime targetduration;  /* last EXT-X-TARGETDURATION */
  gboolean allowcache;          /* last EXT-X-ALLOWCACHE */

  gint bandwidth;
  gint program_id;
  gchar *codecs;
  gint width;
  gint height;
  gboolean iframe;
  GList *files;

  /*< private > */
  gchar *last_data;
  GList *lists;                 /* list of GstM3U8 from the main playlist */
  GList *iframe_lists;          /* I-frame lists from the main playlist */
  GList *current_variant;       /* Current variant playlist used */
  GstM3U8 *parent;              /* main playlist (if any) */
  gint64 mediasequence;          /* EXT-X-MEDIA-SEQUENCE & increased with new media file */
};

struct _GstM3U8MediaFile
{
  gchar *title;
  GstClockTime duration;
  gchar *uri;
  gint64 sequence;               /* the sequence nb of this file */
  gboolean discont;             /* this file marks a discontinuity */
  gchar *key;
  guint8 iv[16];
  gint64 offset, size;
};

struct _GstM3U8Client
{
  GstM3U8 *main;                /* main playlist */
  GstM3U8 *current;
  guint update_failed_count;
  gint64 sequence;              /* the next sequence for this client */
  GstClockTime sequence_position; /* position of this sequence */
  GMutex lock;
};


GstM3U8Client *gst_m3u8_client_new (const gchar * uri, const gchar * base_uri);
void gst_m3u8_client_free (GstM3U8Client * client);
gboolean gst_m3u8_client_update (GstM3U8Client * client, gchar * data);
gboolean gst_m3u8_client_update_variant_playlist (GstM3U8Client * client, gchar * data, const gchar * uri, const gchar * base_uri);
void gst_m3u8_client_set_current (GstM3U8Client * client, GstM3U8 * m3u8);
gboolean gst_m3u8_client_get_next_fragment (GstM3U8Client * client,
    gboolean * discontinuity, const gchar ** uri, GstClockTime * duration,
    GstClockTime * timestamp, gint64 * range_start, gint64 * range_end,
    const gchar ** key, const guint8 ** iv, gboolean forward);
void gst_m3u8_client_advance_fragment (GstM3U8Client * client, gboolean forward);
GstClockTime gst_m3u8_client_get_duration (GstM3U8Client * client);
GstClockTime gst_m3u8_client_get_target_duration (GstM3U8Client * client);
const gchar *gst_m3u8_client_get_uri(GstM3U8Client * client);
const gchar *gst_m3u8_client_get_current_uri(GstM3U8Client * client);
gboolean gst_m3u8_client_has_variant_playlist(GstM3U8Client * client);
gboolean gst_m3u8_client_is_live(GstM3U8Client * client);
GList * gst_m3u8_client_get_playlist_for_bitrate (GstM3U8Client * client,
    guint bitrate);

guint64 gst_m3u8_client_get_current_fragment_duration (GstM3U8Client * client);

G_END_DECLS
#endif /* __M3U8_H__ */
