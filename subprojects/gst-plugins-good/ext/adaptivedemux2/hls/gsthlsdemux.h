/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
 *
 * gsthlsdemux.h:
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


#ifndef __GST_HLS_DEMUX_H__
#define __GST_HLS_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "m3u8.h"
#include "gstisoff.h"
#include "gstadaptivedemux.h"

G_BEGIN_DECLS

#define GST_TYPE_HLS_DEMUX2 \
  (gst_hls_demux2_get_type())
#define GST_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX2,GstHLSDemux))
#define GST_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HLS_DEMUX2,GstHLSDemuxClass))
#define GST_IS_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HLS_DEMUX2))
#define GST_IS_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HLS_DEMUX2))
#define GST_HLS_DEMUX_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_HLS_DEMUX2,GstHLSDemuxClass))
#define GST_HLS_DEMUX_CAST(obj) \
  ((GstHLSDemux *)obj)

typedef struct _GstHLSDemux2 GstHLSDemux;
typedef struct _GstHLSDemux2Class GstHLSDemuxClass;

typedef struct _GstHLSDemuxStream GstHLSDemuxStream;

typedef enum {
  /* More data is needed to parse the fragment */
  GST_HLS_PARSER_RESULT_NEED_MORE_DATA,
  /* An error happened, discard the fragment */
  GST_HLS_PARSER_RESULT_ERROR,
  /* Parsing suceeded, it no longer needs to be called for the fragment */
  GST_HLS_PARSER_RESULT_DONE,
  /* The fragment wasn't the expected one. Current data must be dropped and
   * GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT returned to the parent class */
  GST_HLS_PARSER_RESULT_RESYNC
} GstHLSParserResult;

typedef struct {
  guint8 data[16];
} GstHLSKey;

/**
 * GstHLSDemux:
 *
 * Opaque #GstHLSDemux data structure.
 */
struct _GstHLSDemux2
{
  GstAdaptiveDemux parent;

  /* Initial bitrate to use before any bandwidth measurement */
  guint start_bitrate;

  /* Decryption key cache: url => GstHLSKey */
  GHashTable *keys;
  GMutex      keys_lock;

  /* The master playlist with the available variant streams,
   * created at demuxer start based on the input multivariant playlist */
  GstHLSMasterPlaylist *master;

  /* A ref to the main playlist, for access from external threads */
  GstHLSMediaPlaylist *main_playlist;

  GstHLSVariantStream  *current_variant;
  /* The variant we're switching to (currently being loaded by the playlist loader) */
  GstHLSVariantStream  *pending_variant;

  /* List of failed variants that should be ignored */
  GList *failed_variants;

  GstHLSDemuxStream *main_stream;

  /* Time Mappings (GstHLSTimeMap) */
  GList *mappings;
};

struct _GstHLSDemux2Class
{
  GstAdaptiveDemuxClass parent_class;
};

GstAdaptiveDemuxTrack *
gst_hls_demux_new_track_for_rendition (GstHLSDemux * demux,
		GstHLSRenditionStream * rendition,
    GstCaps * caps, GstStreamFlags flags, GstTagList * tags);

void gst_hls_demux_start_rendition_streams (GstHLSDemux * hlsdemux);
void gst_hls_demux_reset_for_lost_sync (GstHLSDemux * hlsdemux);
const GstHLSKey *gst_hls_demux_get_key (GstHLSDemux * demux,
    const gchar * key_url, const gchar * referer, gboolean allow_cache);

void gst_hls_demux_handle_variant_playlist_update (GstHLSDemux * demux,
    const gchar *playlist_uri, GstHLSMediaPlaylist * playlist);
void gst_hls_demux_handle_variant_playlist_update_error (GstHLSDemux * demux,
    const gchar *playlist_uri);
gboolean gst_hls_demux_change_variant_playlist (GstHLSDemux * demux,
    gboolean iframe_variant, guint max_bitrate, gboolean * changed);
GstFlowReturn gst_hls_demux_update_variant_playlist (GstHLSDemux * demux,
    GError ** err);

void gst_hls_demux_add_time_mapping (GstHLSDemux * demux, gint64 dsn,
    GstClockTimeDiff stream_time, GDateTime * pdt);
void gst_hls_update_time_mappings (GstHLSDemux * demux,
    GstHLSMediaPlaylist * playlist);

gchar *gst_hls_buf_to_utf8_text (GstBuffer * buf);

/* Private */
GstHLSParserResult gst_hlsdemux_handle_content_mpegts (GstHLSDemux       *demux,
						       GstHLSDemuxStream *hls_stream,
						       gboolean           draining,
						       GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_content_id3 (GstHLSDemux       *demux,
						    GstHLSDemuxStream *hls_stream,
						    gboolean           draining,
						    GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_content_isobmff (GstHLSDemux       *demux,
							GstHLSDemuxStream *hls_stream,
							gboolean           draining,
							GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_content_webvtt (GstHLSDemux        *demux,
						       GstHLSDemuxStream *hls_stream,
						       gboolean           draining,
						       GstBuffer        **buffer);

GstClockTimeDiff gst_hls_internal_to_stream_time (GstHLSTimeMap *map,
						  GstClockTime   internal_time);

GstHLSTimeMap *gst_hls_demux_find_time_map (GstHLSDemux * demux, gint64 dsn);

GType gst_hls_demux2_get_type (void);
GType gst_hls_demux_stream_get_type (void);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_H__ */
