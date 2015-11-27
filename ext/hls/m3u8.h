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

G_BEGIN_DECLS

typedef struct _GstM3U8 GstM3U8;
typedef struct _GstM3U8MediaFile GstM3U8MediaFile;
typedef struct _GstM3U8Client GstM3U8Client;

#define GST_M3U8(m) ((GstM3U8*)m)
#define GST_M3U8_MEDIA_FILE(f) ((GstM3U8MediaFile*)f)

#define GST_M3U8_LOCK(m) g_mutex_lock (&m->lock);
#define GST_M3U8_UNLOCK(m) g_mutex_unlock (&m->lock);

#define GST_M3U8_IS_LIVE(m) ((m)->endlist == FALSE)

/* hlsdemux must not get closer to the end of a live stream than
   GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE fragments. Section 6.3.3
   "Playing the Playlist file" of the HLS draft states that this
   value is three fragments */
#define GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE 3

struct _GstM3U8
{
  gchar *uri;                   /* actually downloaded URI */
  gchar *base_uri;              /* URI to use as base for resolving relative URIs.
                                 * This will be different to uri in case of redirects */
  gchar *name;                  /* This will be the "name" of the playlist, the original
                                 * relative/absolute uri in a variant playlist */

  /* parsed info */
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

  /* state */
  GList *current_file;
  GstClockTime current_file_duration; /* Duration of current fragment */
  gint64 sequence;                    /* the next sequence for this client */
  GstClockTime sequence_position;     /* position of this sequence */
  gint64 highest_sequence_number;     /* largest seen sequence number */
  GstClockTime first_file_start;      /* timecode of the start of the first fragment in the current media playlist */
  GstClockTime last_file_end;         /* timecode of the end of the last fragment in the current media playlist */
  GstClockTime duration;              /* cached total duration */

  /*< private > */
  gchar *last_data;
  GList *lists;                 /* list of GstM3U8 from the main playlist */
  GList *iframe_lists;          /* I-frame lists from the main playlist */
  GList *current_variant;       /* Current variant playlist used */
  GMutex lock;

  gint ref_count;               /* ATOMIC */
};

GstM3U8 *          gst_m3u8_ref   (GstM3U8 * m3u8);

void               gst_m3u8_unref (GstM3U8 * m3u8);


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
  gint ref_count;               /* ATOMIC */
};

GstM3U8MediaFile * gst_m3u8_media_file_ref   (GstM3U8MediaFile * mfile);

void               gst_m3u8_media_file_unref (GstM3U8MediaFile * mfile);

GstM3U8 *          gst_m3u8_new (void);

gboolean           gst_m3u8_update               (GstM3U8  * m3u8,
                                                  gchar    * data);

void               gst_m3u8_set_uri              (GstM3U8      * m3u8,
                                                  const gchar  * uri,
                                                  const gchar  * base_uri,
                                                  const gchar  * name);

GstM3U8MediaFile * gst_m3u8_get_next_fragment    (GstM3U8      * m3u8,
                                                  gboolean       forward,
                                                  GstClockTime * sequence_position,
                                                  gboolean     * discont);

gboolean           gst_m3u8_has_next_fragment    (GstM3U8 * m3u8,
                                                  gboolean  forward);

void               gst_m3u8_advance_fragment     (GstM3U8 * m3u8,
                                                  gboolean  forward);

GstClockTime       gst_m3u8_get_duration         (GstM3U8 * m3u8);

GstClockTime       gst_m3u8_get_target_duration  (GstM3U8 * m3u8);

gchar *            gst_m3u8_get_uri              (GstM3U8 * m3u8);

gboolean           gst_m3u8_has_variant_playlist (GstM3U8 * m3u8);

gboolean           gst_m3u8_is_live              (GstM3U8 * m3u8);

gboolean           gst_m3u8_get_seek_range       (GstM3U8 * m3u8,
                                                  gint64  * start,
                                                  gint64  * stop);

GList *            gst_m3u8_get_playlist_for_bitrate (GstM3U8 * main,
                                                      guint     bitrate);

G_END_DECLS

#endif /* __M3U8_H__ */
