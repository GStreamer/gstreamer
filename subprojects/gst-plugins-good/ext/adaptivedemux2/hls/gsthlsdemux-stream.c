/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
 *
 * Copyright (C) 2021-2022 Centricular Ltd
 *   Author: Edward Hervey <edward@centricular.com>
 *   Author: Jan Schmidt <jan@centricular.com>
 *
 * gsthlsdemux-stream.c:
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
#  include "config.h"
#endif

#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/tag/tag.h>
#include <glib/gi18n-lib.h>

#include "gsthlsdemux.h"
#include "gsthlsdemux-stream.h"

GST_DEBUG_CATEGORY_EXTERN (gst_hls_demux2_debug);
#define GST_CAT_DEFAULT gst_hls_demux2_debug

/* Maximum values for mpeg-ts DTS values */
#define MPEG_TS_MAX_PTS (((((guint64)1) << 33) * (guint64)100000) / 9)

static GstBuffer *gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstHLSDemuxStream * stream, GstBuffer * encrypted_buffer, GError ** err);
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data);
static void gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream);

static gboolean
gst_hls_demux_stream_start_fragment (GstAdaptiveDemux2Stream * stream);
static GstFlowReturn
gst_hls_demux_stream_finish_fragment (GstAdaptiveDemux2Stream * stream);
static GstFlowReturn gst_hls_demux_stream_data_received (GstAdaptiveDemux2Stream
    * stream, GstBuffer * buffer);

static gboolean gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemux2Stream
    * stream);
static GstFlowReturn
gst_hls_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream);
static GstFlowReturn
gst_hls_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream);
static GstFlowReturn
gst_hls_demux_stream_submit_request (GstAdaptiveDemux2Stream * stream,
    DownloadRequest * download_req);
static void gst_hls_demux_stream_start (GstAdaptiveDemux2Stream * stream);
static void gst_hls_demux_stream_stop (GstAdaptiveDemux2Stream * stream);
static void gst_hls_demux_stream_create_tracks (GstAdaptiveDemux2Stream *
    stream);
static gboolean gst_hls_demux_stream_select_bitrate (GstAdaptiveDemux2Stream *
    stream, guint64 bitrate);
static GstClockTime
gst_hls_demux_stream_get_presentation_offset (GstAdaptiveDemux2Stream * stream);

static void gst_hls_demux_stream_finalize (GObject * object);

#define gst_hls_demux_stream_parent_class stream_parent_class
G_DEFINE_TYPE (GstHLSDemuxStream, gst_hls_demux_stream,
    GST_TYPE_ADAPTIVE_DEMUX2_STREAM);

static void
gst_hls_demux_stream_class_init (GstHLSDemuxStreamClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAdaptiveDemux2StreamClass *adaptivedemux2stream_class =
      GST_ADAPTIVE_DEMUX2_STREAM_CLASS (klass);

  gobject_class->finalize = gst_hls_demux_stream_finalize;

  adaptivedemux2stream_class->update_fragment_info =
      gst_hls_demux_stream_update_fragment_info;
  adaptivedemux2stream_class->submit_request =
      gst_hls_demux_stream_submit_request;
  adaptivedemux2stream_class->has_next_fragment =
      gst_hls_demux_stream_has_next_fragment;
  adaptivedemux2stream_class->stream_seek = gst_hls_demux_stream_seek;
  adaptivedemux2stream_class->advance_fragment =
      gst_hls_demux_stream_advance_fragment;
  adaptivedemux2stream_class->select_bitrate =
      gst_hls_demux_stream_select_bitrate;
  adaptivedemux2stream_class->start = gst_hls_demux_stream_start;
  adaptivedemux2stream_class->stop = gst_hls_demux_stream_stop;
  adaptivedemux2stream_class->create_tracks =
      gst_hls_demux_stream_create_tracks;

  adaptivedemux2stream_class->start_fragment =
      gst_hls_demux_stream_start_fragment;
  adaptivedemux2stream_class->finish_fragment =
      gst_hls_demux_stream_finish_fragment;
  adaptivedemux2stream_class->data_received =
      gst_hls_demux_stream_data_received;
  adaptivedemux2stream_class->get_presentation_offset =
      gst_hls_demux_stream_get_presentation_offset;

}

static void
gst_hls_demux_stream_init (GstHLSDemuxStream * stream)
{
  stream->parser_type = GST_HLS_PARSER_NONE;
  stream->do_typefind = TRUE;
  stream->reset_pts = TRUE;
  stream->presentation_offset = 60 * GST_SECOND;
  stream->pdt_tag_sent = FALSE;
}

void
gst_hls_demux_stream_clear_pending_data (GstHLSDemuxStream * hls_stream,
    gboolean force)
{
  GST_DEBUG_OBJECT (hls_stream, "force : %d", force);
  if (hls_stream->pending_encrypted_data)
    gst_adapter_clear (hls_stream->pending_encrypted_data);
  gst_buffer_replace (&hls_stream->pending_decrypted_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_typefind_buffer, NULL);
  if (force || !hls_stream->pending_data_is_header) {
    gst_buffer_replace (&hls_stream->pending_segment_data, NULL);
    hls_stream->pending_data_is_header = FALSE;
  }
  hls_stream->current_offset = -1;
  hls_stream->process_buffer_content = TRUE;
  gst_hls_demux_stream_decrypt_end (hls_stream);
}

GstFlowReturn
gst_hls_demux_stream_seek (GstAdaptiveDemux2Stream * stream, gboolean forward,
    GstSeekFlags flags, GstClockTimeDiff ts, GstClockTimeDiff * final_ts)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;

  GST_DEBUG_OBJECT (stream,
      "is_variant:%d media:%p current_variant:%p forward:%d ts:%"
      GST_TIME_FORMAT, hls_stream->is_variant, hls_stream->current_rendition,
      hlsdemux->current_variant, forward, GST_TIME_ARGS (ts));

  /* If this stream doesn't have a playlist yet, we can't seek on it */
  if (!hls_stream->playlist_fetched) {
    return GST_ADAPTIVE_DEMUX_FLOW_BUSY;
  }

  /* Allow jumping to partial segments in the last 2 segments in LL-HLS */
  if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (hls_stream->playlist))
    flags |= GST_HLS_M3U8_SEEK_FLAG_ALLOW_PARTIAL;

  GstM3U8SeekResult seek_result;
  if (gst_hls_media_playlist_seek (hls_stream->playlist, forward, flags, ts,
          &seek_result)) {
    if (hls_stream->current_segment)
      gst_m3u8_media_segment_unref (hls_stream->current_segment);
    hls_stream->current_segment = seek_result.segment;
    hls_stream->in_partial_segments = seek_result.found_partial_segment;
    hls_stream->part_idx = seek_result.part_idx;

    hls_stream->reset_pts = TRUE;
    if (final_ts)
      *final_ts = seek_result.stream_time;
  } else {
    GST_WARNING_OBJECT (stream, "Seeking failed");
    ret = GST_FLOW_ERROR;
  }

  return ret;
}

static GstCaps *
get_caps_of_stream_type (GstCaps * full_caps, GstStreamType streamtype)
{
  GstCaps *ret = NULL;

  guint i;
  for (i = 0; i < gst_caps_get_size (full_caps); i++) {
    GstStructure *st = gst_caps_get_structure (full_caps, i);

    if (gst_hls_get_stream_type_from_structure (st) == streamtype) {
      ret = gst_caps_new_empty ();
      gst_caps_append_structure (ret, gst_structure_copy (st));
      break;
    }
  }

  return ret;
}

static GstHLSRenditionStream *
find_uriless_rendition (GstHLSDemux * demux, GstStreamType stream_type)
{
  GList *tmp;

  for (tmp = demux->master->renditions; tmp; tmp = tmp->next) {
    GstHLSRenditionStream *media = tmp->data;
    if (media->uri == NULL &&
        gst_stream_type_from_hls_type (media->mtype) == stream_type)
      return media;
  }
  return NULL;
}

static void
gst_hls_demux_stream_create_tracks (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;
  GstHLSDemuxStream *hlsdemux_stream = (GstHLSDemuxStream *) stream;
  guint i;
  GstStreamType uriless_types = 0;
  GstCaps *variant_caps = NULL;

  GST_DEBUG_OBJECT (stream, "Update tracks of variant stream");

  if (hlsdemux->master->have_codecs) {
    variant_caps = gst_hls_master_playlist_get_common_caps (hlsdemux->master);
  }

  /* Use the stream->stream_collection and manifest to create the appropriate tracks */
  for (i = 0; i < gst_stream_collection_get_size (stream->stream_collection);
      i++) {
    GstStream *gst_stream =
        gst_stream_collection_get_stream (stream->stream_collection, i);
    GstStreamType stream_type = gst_stream_get_stream_type (gst_stream);
    GstAdaptiveDemuxTrack *track;
    GstHLSRenditionStream *embedded_media = NULL;
    /* tracks from the variant streams should be prefered over those provided by renditions */
    GstStreamFlags flags =
        gst_stream_get_stream_flags (gst_stream) | GST_STREAM_FLAG_SELECT;
    GstCaps *manifest_caps = NULL;

    if (stream_type == GST_STREAM_TYPE_UNKNOWN)
      continue;

    if (variant_caps)
      manifest_caps = get_caps_of_stream_type (variant_caps, stream_type);
    hlsdemux_stream->rendition_type |= stream_type;

    if ((uriless_types & stream_type) == 0) {
      /* Do we have a uriless media for this stream type */
      /* Find if there is a rendition without URI, it will be provided by this variant */
      embedded_media = find_uriless_rendition (hlsdemux, stream_type);
      /* Remember we used this type for a embedded media */
      uriless_types |= stream_type;
    }

    if (embedded_media) {
      GstTagList *tags = gst_stream_get_tags (gst_stream);
      GST_DEBUG_OBJECT (stream, "Adding track '%s' to main variant stream",
          embedded_media->name);
      track =
          gst_hls_demux_new_track_for_rendition (hlsdemux, embedded_media,
          manifest_caps, flags,
          tags ? gst_tag_list_make_writable (tags) : tags);
    } else {
      gchar *stream_id;
      stream_id =
          g_strdup_printf ("main-%s-%d", gst_stream_type_get_name (stream_type),
          i);

      GST_DEBUG_OBJECT (stream, "Adding track '%s' to main variant stream",
          stream_id);
      track =
          gst_adaptive_demux_track_new (stream->demux, stream_type,
          flags, stream_id, manifest_caps, NULL);
      g_free (stream_id);
    }
    track->upstream_stream_id =
        g_strdup (gst_stream_get_stream_id (gst_stream));
    gst_adaptive_demux2_stream_add_track (stream, track);
    gst_adaptive_demux_track_unref (track);
  }

  if (variant_caps)
    gst_caps_unref (variant_caps);

  /* Update the stream object with rendition types.
   * FIXME: rendition_type could be removed */
  stream->stream_type = hlsdemux_stream->rendition_type;
}

static gboolean
gst_hls_demux_stream_start_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  const GstHLSKey *key;
  GstHLSMediaPlaylist *m3u8;

  GST_DEBUG_OBJECT (stream, "Fragment starting");

  gst_hls_demux_stream_clear_pending_data (hls_stream, FALSE);

  /* If no decryption is needed, there's nothing to be done here */
  if (hls_stream->current_key == NULL)
    return TRUE;

  m3u8 = hls_stream->playlist;

  key = gst_hls_demux_get_key (hlsdemux, hls_stream->current_key,
      m3u8->uri, m3u8->allowcache);

  if (key == NULL)
    goto key_failed;

  if (!gst_hls_demux_stream_decrypt_start (hls_stream, key->data,
          hls_stream->current_iv))
    goto decrypt_start_failed;

  return TRUE;

key_failed:
  {
    GST_ELEMENT_ERROR (hlsdemux, STREAM, DECRYPT_NOKEY,
        ("Couldn't retrieve key for decryption"), (NULL));
    GST_WARNING_OBJECT (hlsdemux, "Failed to decrypt data");
    return FALSE;
  }
decrypt_start_failed:
  {
    GST_ELEMENT_ERROR (hlsdemux, STREAM, DECRYPT, ("Failed to start decrypt"),
        ("Couldn't set key and IV or plugin was built without crypto library"));
    return FALSE;
  }
}

static GstHLSParserType
caps_to_parser_type (const GstCaps * caps)
{
  const GstStructure *s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "video/mpegts"))
    return GST_HLS_PARSER_MPEGTS;
  if (gst_structure_has_name (s, "application/x-id3"))
    return GST_HLS_PARSER_ID3;
  if (gst_structure_has_name (s, "application/x-subtitle-vtt"))
    return GST_HLS_PARSER_WEBVTT;
  if (gst_structure_has_name (s, "video/quicktime"))
    return GST_HLS_PARSER_ISOBMFF;

  return GST_HLS_PARSER_NONE;
}

/* Identify the nature of data for this stream
 *
 * Will also setup the appropriate parser (tsreader) if needed
 *
 * Consumes the input buffer when it returns FALSE, but
 * replaces / returns the input buffer in the `buffer` parameter
 * when it returns TRUE.
 *
 * Returns TRUE if we are done with typefinding */
static gboolean
gst_hls_demux_typefind_stream (GstHLSDemux * hlsdemux,
    GstAdaptiveDemux2Stream * stream, GstBuffer ** out_buffer, gboolean at_eos,
    GstFlowReturn * ret)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstCaps *caps = NULL;
  guint buffer_size;
  GstTypeFindProbability prob = GST_TYPE_FIND_NONE;
  GstMapInfo info;
  GstBuffer *buffer = *out_buffer;

  if (hls_stream->pending_typefind_buffer) {
    /* Append to the existing typefind buffer and create a new one that
     * we'll return (or consume below) */
    buffer = *out_buffer =
        gst_buffer_append (hls_stream->pending_typefind_buffer, buffer);
    hls_stream->pending_typefind_buffer = NULL;
  }

  gst_buffer_map (buffer, &info, GST_MAP_READ);
  buffer_size = info.size;

  /* Typefind could miss if buffer is too small. In this case we
   * will retry later */
  if (buffer_size >= (2 * 1024) || at_eos) {
    caps =
        gst_type_find_helper_for_data (GST_OBJECT_CAST (hlsdemux), info.data,
        info.size, &prob);
  }

  if (G_UNLIKELY (!caps)) {
    /* Won't need this mapping any more all paths return inside this if() */
    gst_buffer_unmap (buffer, &info);

    /* Only fail typefinding if we already a good amount of data
     * and we still don't know the type */
    if (buffer_size > (2 * 1024 * 1024) || at_eos) {
      GST_ELEMENT_ERROR (hlsdemux, STREAM, TYPE_NOT_FOUND,
          ("Could not determine type of stream"), (NULL));
      gst_buffer_unref (buffer);
      *ret = GST_FLOW_NOT_NEGOTIATED;
    } else {
      GST_LOG_OBJECT (stream, "Not enough data to typefind");
      hls_stream->pending_typefind_buffer = buffer;     /* Transfer the ref */
      *ret = GST_FLOW_OK;
    }
    *out_buffer = NULL;
    return FALSE;
  }

  GST_DEBUG_OBJECT (stream,
      "Typefind result: %" GST_PTR_FORMAT " prob:%d", caps, prob);

  if (hls_stream->parser_type == GST_HLS_PARSER_NONE) {
    hls_stream->parser_type = caps_to_parser_type (caps);
    if (hls_stream->parser_type == GST_HLS_PARSER_NONE) {
      GST_WARNING_OBJECT (stream,
          "Unsupported stream type %" GST_PTR_FORMAT, caps);
      GST_MEMDUMP_OBJECT (stream, "unknown data", info.data,
          MIN (info.size, 128));
      gst_buffer_unref (buffer);
      *ret = GST_FLOW_ERROR;
      return FALSE;
    }
    if (hls_stream->parser_type == GST_HLS_PARSER_ISOBMFF)
      hls_stream->presentation_offset = 0;
  }

  gst_adaptive_demux2_stream_set_caps (stream, caps);

  hls_stream->do_typefind = FALSE;

  gst_buffer_unmap (buffer, &info);

  /* We are done with typefinding. Doesn't consume the input buffer */
  *ret = GST_FLOW_OK;
  return TRUE;
}

/* Compute the stream time for the given internal time, based on the provided
 * time map.
 *
 * Will handle mpeg-ts wraparound. */
GstClockTimeDiff
gst_hls_internal_to_stream_time (GstHLSTimeMap * map,
    GstClockTime internal_time)
{
  if (map->internal_time == GST_CLOCK_TIME_NONE)
    return GST_CLOCK_STIME_NONE;

  /* Handle MPEG-TS Wraparound */
  if (internal_time < map->internal_time &&
      map->internal_time - internal_time > (MPEG_TS_MAX_PTS / 2))
    internal_time += MPEG_TS_MAX_PTS;

  return (map->stream_time + internal_time - map->internal_time);
}

/* Handle the internal time discovered on a segment.
 *
 * This function is called by the individual buffer parsers once they have
 * extracted that internal time (which is most of the time based on mpegts time,
 * but can also be ISOBMFF pts).
 *
 * This will update the time map when appropriate.
 *
 * If a synchronization issue is detected, the appropriate steps will be taken
 * and the RESYNC return value will be returned
 */
GstHLSParserResult
gst_hlsdemux_stream_handle_internal_time (GstHLSDemuxStream * hls_stream,
    GstClockTime internal_time)
{
  GstM3U8MediaSegment *current_segment = hls_stream->current_segment;
  GstHLSTimeMap *map;
  GstClockTimeDiff current_stream_time;
  GstClockTimeDiff real_stream_time, difference;

  g_return_val_if_fail (current_segment != NULL, GST_HLS_PARSER_RESULT_ERROR);

  current_stream_time = current_segment->stream_time;
  if (hls_stream->in_partial_segments) {
    /* If the current partial segment is valid, update the stream current position to the partial
     * segment stream_time, otherwise leave it alone and fix it up later when we resync */
    if (current_segment->partial_segments
        && hls_stream->part_idx < current_segment->partial_segments->len) {
      GstM3U8PartialSegment *part =
          g_ptr_array_index (current_segment->partial_segments,
          hls_stream->part_idx);
      current_stream_time = part->stream_time;
    }
  }

  GST_DEBUG_OBJECT (hls_stream,
      "Got internal time %" GST_TIME_FORMAT " for current segment stream time %"
      GST_STIME_FORMAT, GST_TIME_ARGS (internal_time),
      GST_STIME_ARGS (current_stream_time));

  GstHLSDemux *demux =
      GST_HLS_DEMUX_CAST (GST_ADAPTIVE_DEMUX2_STREAM_CAST (hls_stream)->demux);
  map = gst_hls_demux_find_time_map (demux, current_segment->discont_sequence);

  /* Time mappings will always be created upon initial parsing and when advancing */
  g_assert (map);

  /* Handle the first internal time of a discont sequence. We can only store/use
   * those values for variant streams. */
  if (!GST_CLOCK_TIME_IS_VALID (map->internal_time)) {
    if (!hls_stream->is_variant) {
      GST_WARNING_OBJECT (hls_stream,
          "Got data from a new discont sequence on a rendition stream, can't validate stream time");
      return GST_HLS_PARSER_RESULT_DONE;
    }
    GST_DEBUG_OBJECT (hls_stream,
        "Updating time map dsn:%" G_GINT64_FORMAT " stream_time:%"
        GST_STIME_FORMAT " internal_time:%" GST_TIME_FORMAT, map->dsn,
        GST_STIME_ARGS (current_stream_time), GST_TIME_ARGS (internal_time));
    /* The stream time for a mapping should always be positive ! */
    g_assert (current_stream_time >= 0);

    if (hls_stream->parser_type == GST_HLS_PARSER_ISOBMFF)
      hls_stream->presentation_offset = internal_time - current_stream_time;

    map->stream_time = current_stream_time;
    map->internal_time = internal_time;

    gst_hls_demux_start_rendition_streams (demux);
    return GST_HLS_PARSER_RESULT_DONE;
  }

  /* The information in a discont is always valid */
  if (current_segment->discont) {
    GST_DEBUG_OBJECT (hls_stream,
        "DISCONT segment, Updating time map to stream_time:%" GST_STIME_FORMAT
        " internal_time:%" GST_TIME_FORMAT, GST_STIME_ARGS (internal_time),
        GST_TIME_ARGS (current_stream_time));
    map->stream_time = current_stream_time;
    map->internal_time = internal_time;
    return GST_HLS_PARSER_RESULT_DONE;
  }

  /* Check if the segment is the expected one */
  real_stream_time = gst_hls_internal_to_stream_time (map, internal_time);
  difference = current_stream_time - real_stream_time;
  GST_DEBUG_OBJECT (hls_stream,
      "Segment contains stream time %" GST_STIME_FORMAT
      " difference against expected : %" GST_STIME_FORMAT,
      GST_STIME_ARGS (real_stream_time), GST_STIME_ARGS (difference));

  if (ABS (difference) > 10 * GST_MSECOND) {
    GstClockTimeDiff wrong_position_threshold =
        hls_stream->current_segment->duration / 2;

    /* Update the value */
    GST_DEBUG_OBJECT (hls_stream,
        "Updating current stream time to %" GST_STIME_FORMAT,
        GST_STIME_ARGS (real_stream_time));

    /* For LL-HLS, make sure to update and recalculate stream time from
     * the right partial segment if playing one */
    if (hls_stream->in_partial_segments && hls_stream->part_idx != 0) {
      if (current_segment->partial_segments
          && hls_stream->part_idx < current_segment->partial_segments->len) {
        GstM3U8PartialSegment *part =
            g_ptr_array_index (current_segment->partial_segments,
            hls_stream->part_idx);
        part->stream_time = real_stream_time;

        gst_hls_media_playlist_recalculate_stream_time_from_part
            (hls_stream->playlist, hls_stream->current_segment,
            hls_stream->part_idx);

        /* When playing partial segments, the "Wrong position" threshold should be
         * half the part duration */
        wrong_position_threshold = part->duration / 2;
      }
    } else {
      /* Aligned to the start of the segment, update there */
      current_segment->stream_time = real_stream_time;
      gst_hls_media_playlist_recalculate_stream_time (hls_stream->playlist,
          hls_stream->current_segment);
    }
    gst_hls_media_playlist_dump (hls_stream->playlist);

    if (ABS (difference) > wrong_position_threshold) {
      GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) hls_stream;
      GstM3U8SeekResult seek_result;

      /* We are at the wrong segment, try to figure out the *actual* segment */
      GST_DEBUG_OBJECT (hls_stream,
          "Trying to find the correct segment in the playlist for %"
          GST_STIME_FORMAT, GST_STIME_ARGS (current_stream_time));
      if (gst_hls_media_playlist_find_position (hls_stream->playlist,
              current_stream_time, hls_stream->in_partial_segments,
              &seek_result)) {

        GST_DEBUG_OBJECT (hls_stream, "Synced to position %" GST_STIME_FORMAT,
            GST_STIME_ARGS (seek_result.stream_time));

        gst_m3u8_media_segment_unref (hls_stream->current_segment);
        hls_stream->current_segment = seek_result.segment;
        hls_stream->in_partial_segments = seek_result.found_partial_segment;
        hls_stream->part_idx = seek_result.part_idx;

        /* Ask parent class to restart this fragment */
        return GST_HLS_PARSER_RESULT_RESYNC;
      }

      GST_WARNING_OBJECT (hls_stream,
          "Could not find a replacement stream, carrying on with segment");
      stream->discont = TRUE;
      stream->fragment.stream_time = real_stream_time;
    }
  }

  return GST_HLS_PARSER_RESULT_DONE;
}

static GstHLSParserResult
gst_hls_demux_handle_buffer_content (GstHLSDemux * demux,
    GstHLSDemuxStream * hls_stream, gboolean draining, GstBuffer ** buffer)
{
  GstHLSTimeMap *map;
  GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) hls_stream;
  GstClockTimeDiff current_stream_time =
      hls_stream->current_segment->stream_time;
  GstClockTime current_duration = hls_stream->current_segment->duration;
  GstHLSParserResult parser_ret;

  GST_LOG_OBJECT (stream,
      "stream_time:%" GST_STIME_FORMAT " duration:%" GST_TIME_FORMAT
      " discont:%d draining:%d header:%d index:%d",
      GST_STIME_ARGS (current_stream_time), GST_TIME_ARGS (current_duration),
      hls_stream->current_segment->discont, draining,
      stream->downloading_header, stream->downloading_index);

  /* FIXME : Replace the boolean parser return value (and this function's return
   *  value) by an enum which clearly specifies whether:
   *
   * * The content parsing happened succesfully and it no longer needs to be
   *   called for the remainder of this fragment
   * * More data is needed in order to parse the data
   * * There was a fatal error parsing the contents (ex: invalid/incompatible
   *   content)
   * * The computed fragment stream time is out of sync
   */

  g_assert (demux->mappings);
  map =
      gst_hls_demux_find_time_map (demux,
      hls_stream->current_segment->discont_sequence);
  if (!map) {
    /* For rendition streams, we can't do anything without time mapping */
    if (!hls_stream->is_variant) {
      GST_DEBUG_OBJECT (stream,
          "No available time mapping for dsn:%" G_GINT64_FORMAT
          " using estimated stream time",
          hls_stream->current_segment->discont_sequence);
      goto out_done;
    }

    /* Variants will be able to fill in the the time mapping, so we can carry on without a time mapping */
  } else {
    GST_DEBUG_OBJECT (stream,
        "Using mapping dsn:%" G_GINT64_FORMAT " stream_time:%" GST_TIME_FORMAT
        " internal_time:%" GST_TIME_FORMAT, map->dsn,
        GST_TIME_ARGS (map->stream_time), GST_TIME_ARGS (map->internal_time));
  }

  switch (hls_stream->parser_type) {
    case GST_HLS_PARSER_MPEGTS:
      parser_ret =
          gst_hlsdemux_handle_content_mpegts (demux, hls_stream, draining,
          buffer);
      break;
    case GST_HLS_PARSER_ID3:
      parser_ret =
          gst_hlsdemux_handle_content_id3 (demux, hls_stream, draining, buffer);
      break;
    case GST_HLS_PARSER_WEBVTT:
    {
      /* Furthermore it will handle timeshifting itself */
      parser_ret =
          gst_hlsdemux_handle_content_webvtt (demux, hls_stream, draining,
          buffer);
      break;
    }
    case GST_HLS_PARSER_ISOBMFF:
      parser_ret =
          gst_hlsdemux_handle_content_isobmff (demux, hls_stream, draining,
          buffer);
      break;
    case GST_HLS_PARSER_NONE:
    default:
    {
      GST_ERROR_OBJECT (stream, "Unknown stream type");
      goto out_error;
    }
  }

  if (parser_ret == GST_HLS_PARSER_RESULT_NEED_MORE_DATA) {
    if (stream->downloading_index || stream->downloading_header)
      goto out_need_more;
    /* Else if we're draining, it's an error */
    if (draining)
      goto out_error;
    /* Else we just need more data */
    goto out_need_more;
  }

  if (parser_ret == GST_HLS_PARSER_RESULT_ERROR)
    goto out_error;

  if (parser_ret == GST_HLS_PARSER_RESULT_RESYNC)
    goto out_resync;

out_done:
  GST_DEBUG_OBJECT (stream, "Done. Finished parsing");
  return GST_HLS_PARSER_RESULT_DONE;

out_error:
  GST_DEBUG_OBJECT (stream, "Done. Error while parsing");
  return GST_HLS_PARSER_RESULT_ERROR;

out_need_more:
  GST_DEBUG_OBJECT (stream, "Done. Need more data");
  return GST_HLS_PARSER_RESULT_NEED_MORE_DATA;

out_resync:
  GST_DEBUG_OBJECT (stream, "Done. Resync required");
  return GST_HLS_PARSER_RESULT_RESYNC;
}

static GstFlowReturn
gst_hls_demux_stream_handle_buffer (GstAdaptiveDemux2Stream * stream,
    GstBuffer * buffer, gboolean at_eos)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *pending_header_data = NULL;

  /* If current segment is not present, this means that a playlist update
   * happened between the moment ::update_fragment_info() was called and the
   * moment we received data. And that playlist update couldn't match the
   * current position. This will happen in live playback when we are downloading
   * too slowly, therefore we try to "catch up" back to live
   */
  if (hls_stream->current_segment == NULL) {
    GST_WARNING_OBJECT (stream, "Lost sync");
    /* Drop the buffer */
    gst_buffer_unref (buffer);
    return GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC;
  }

  GST_DEBUG_OBJECT (stream,
      "buffer:%p at_eos:%d do_typefind:%d uri:%s", buffer, at_eos,
      hls_stream->do_typefind, GST_STR_NULL (stream->fragment.uri));

  if (buffer == NULL)
    goto out;

  /* If we need to do typefind and we're not done with it (or we errored), return */
  if (G_UNLIKELY (hls_stream->do_typefind) &&
      !gst_hls_demux_typefind_stream (hlsdemux, stream, &buffer, at_eos,
          &ret)) {
    goto out;
  }
  g_assert (hls_stream->pending_typefind_buffer == NULL);

  if (hls_stream->process_buffer_content) {
    GstHLSParserResult parse_ret;

    if (hls_stream->pending_segment_data) {
      if (hls_stream->pending_data_is_header) {
        /* Keep a copy of the header data in case we need to requeue it
         * due to GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT below */
        pending_header_data = gst_buffer_ref (hls_stream->pending_segment_data);
      }
      buffer = gst_buffer_append (hls_stream->pending_segment_data, buffer);
      hls_stream->pending_segment_data = NULL;
    }

    /* Try to get the timing information */
    parse_ret =
        gst_hls_demux_handle_buffer_content (hlsdemux, hls_stream, at_eos,
        &buffer);

    switch (parse_ret) {
      case GST_HLS_PARSER_RESULT_NEED_MORE_DATA:
        /* If we don't have enough, store and return */
        hls_stream->pending_segment_data = buffer;
        hls_stream->pending_data_is_header =
            (stream->downloading_header == TRUE);
        if (hls_stream->pending_data_is_header)
          stream->send_segment = TRUE;
        goto out;
      case GST_HLS_PARSER_RESULT_ERROR:
        /* Error, drop buffer and return */
        gst_buffer_unref (buffer);
        ret = GST_FLOW_ERROR;
        goto out;
      case GST_HLS_PARSER_RESULT_RESYNC:
        /* Resync, drop buffer and return */
        gst_buffer_unref (buffer);
        ret = GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT;
        /* If we had a pending set of header data, requeue it */
        if (pending_header_data != NULL) {
          g_assert (hls_stream->pending_segment_data == NULL);

          GST_DEBUG_OBJECT (hls_stream,
              "Requeueing header data %" GST_PTR_FORMAT
              " before returning RESTART_FRAGMENT", pending_header_data);
          hls_stream->pending_segment_data = pending_header_data;
          pending_header_data = NULL;
        }
        goto out;
      case GST_HLS_PARSER_RESULT_DONE:
        /* Done parsing, carry on */
        hls_stream->process_buffer_content = FALSE;
        break;
    }
  }

  if (!buffer)
    goto out;

  buffer = gst_buffer_make_writable (buffer);

  GST_BUFFER_OFFSET (buffer) = hls_stream->current_offset;
  hls_stream->current_offset += gst_buffer_get_size (buffer);
  GST_BUFFER_OFFSET_END (buffer) = hls_stream->current_offset;

  GST_DEBUG_OBJECT (stream, "We have a buffer, pushing: %" GST_PTR_FORMAT,
      buffer);

  ret = gst_adaptive_demux2_stream_push_buffer (stream, buffer);

out:
  if (pending_header_data != NULL) {
    /* Throw away the pending header data now. If it wasn't consumed above,
     * we won't need it */
    gst_buffer_unref (pending_header_data);
  }

  GST_DEBUG_OBJECT (stream, "Returning %s", gst_flow_get_name (ret));
  return ret;
}

static GstFlowReturn
gst_hls_demux_stream_finish_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);   // FIXME: pass HlsStream into function
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (stream, "Finishing %ssegment uri:%s",
      hls_stream->in_partial_segments ? "partial " : "",
      GST_STR_NULL (stream->fragment.uri));

  /* Drain all pending data */
  if (hls_stream->current_key)
    gst_hls_demux_stream_decrypt_end (hls_stream);

  if (hls_stream->current_segment && stream->last_ret == GST_FLOW_OK) {
    if (hls_stream->pending_decrypted_buffer) {
      if (hls_stream->current_key) {
        GstMapInfo info;
        gssize unpadded_size;

        /* Handle pkcs7 unpadding here */
        gst_buffer_map (hls_stream->pending_decrypted_buffer, &info,
            GST_MAP_READ);
        unpadded_size = info.size - info.data[info.size - 1];
        gst_buffer_unmap (hls_stream->pending_decrypted_buffer, &info);

        gst_buffer_resize (hls_stream->pending_decrypted_buffer, 0,
            unpadded_size);
      }

      ret =
          gst_hls_demux_stream_handle_buffer (stream,
          hls_stream->pending_decrypted_buffer, TRUE);
      hls_stream->pending_decrypted_buffer = NULL;
    }

    if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED) {
      if (G_UNLIKELY (hls_stream->pending_typefind_buffer)) {
        GstBuffer *buf = hls_stream->pending_typefind_buffer;
        hls_stream->pending_typefind_buffer = NULL;

        gst_hls_demux_stream_handle_buffer (stream, buf, TRUE);
      }

      if (hls_stream->pending_segment_data) {
        GstBuffer *buf = hls_stream->pending_segment_data;
        hls_stream->pending_segment_data = NULL;

        ret = gst_hls_demux_stream_handle_buffer (stream, buf, TRUE);
      }
    }
  }

  gst_hls_demux_stream_clear_pending_data (hls_stream, FALSE);

  if (G_UNLIKELY (stream->downloading_header || stream->downloading_index))
    return GST_FLOW_OK;

  if (hls_stream->current_segment == NULL) {
    /* We can't advance, we just return OK for now and let the base class
     * trigger a new download (or fail and resync itself) */
    GST_DEBUG_OBJECT (stream, "Can't advance - current_segment is NULL");
    return GST_FLOW_OK;
  }

  if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED) {
    GstClockTime duration = hls_stream->current_segment->duration;

    /* We can update the stream current position with a more accurate value
     * before advancing. Note that we don't have any period so we can set the
     * stream_time as-is on the stream current position */
    if (hls_stream->in_partial_segments) {
      GstM3U8MediaSegment *cur_segment = hls_stream->current_segment;

      /* If the current partial segment is valid, update the stream current position, otherwise
       * leave it alone and fix it up later when we resync */
      if (cur_segment->partial_segments
          && hls_stream->part_idx < cur_segment->partial_segments->len) {
        GstM3U8PartialSegment *part =
            g_ptr_array_index (cur_segment->partial_segments,
            hls_stream->part_idx);
        stream->current_position = part->stream_time;
        duration = part->duration;
      }
    } else {
      stream->current_position = hls_stream->current_segment->stream_time;
    }

    return gst_adaptive_demux2_stream_advance_fragment (stream, duration);
  }
  return ret;
}

static GstFlowReturn
gst_hls_demux_stream_data_received (GstAdaptiveDemux2Stream * stream,
    GstBuffer * buffer)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  GstM3U8MediaSegment *file = hls_stream->current_segment;

  if (hls_stream->current_segment == NULL)
    return GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC;

  if (hls_stream->current_offset == -1)
    hls_stream->current_offset = 0;

  /* Is it encrypted? */
  if (hls_stream->current_key) {
    GError *err = NULL;
    gsize size;
    GstBuffer *decrypted_buffer;
    GstBuffer *tmp_buffer;

    if (hls_stream->pending_encrypted_data == NULL)
      hls_stream->pending_encrypted_data = gst_adapter_new ();

    gst_adapter_push (hls_stream->pending_encrypted_data, buffer);
    size = gst_adapter_available (hls_stream->pending_encrypted_data);

    /* must be a multiple of 16 */
    size &= (~0xF);

    if (size == 0) {
      return GST_FLOW_OK;
    }

    buffer = gst_adapter_take_buffer (hls_stream->pending_encrypted_data, size);
    decrypted_buffer =
        gst_hls_demux_decrypt_fragment (hlsdemux, hls_stream, buffer, &err);
    if (err) {
      GST_ELEMENT_ERROR (hlsdemux, STREAM, DECODE, ("Failed to decrypt buffer"),
          ("decryption failed %s", err->message));
      g_error_free (err);
      return GST_FLOW_ERROR;
    }

    tmp_buffer = hls_stream->pending_decrypted_buffer;
    hls_stream->pending_decrypted_buffer = decrypted_buffer;
    buffer = tmp_buffer;
    if (!buffer)
      return GST_FLOW_OK;
  }

  if (!hls_stream->pdt_tag_sent && file != NULL && file->datetime != NULL) {
    GstDateTime *pdt_time = gst_date_time_new_from_g_date_time (g_date_time_ref
        (file->datetime));
    gst_adaptive_demux2_stream_set_tags (stream,
        gst_tag_list_new (GST_TAG_DATE_TIME, pdt_time, NULL));
    gst_date_time_unref (pdt_time);
    hls_stream->pdt_tag_sent = TRUE;
  }


  return gst_hls_demux_stream_handle_buffer (stream, buffer, FALSE);
}

static void
gst_hls_demux_stream_finalize (GObject * object)
{
  GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) object;
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (object);
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;

  if (hls_stream == hlsdemux->main_stream)
    hlsdemux->main_stream = NULL;

  g_free (hls_stream->lang);
  g_free (hls_stream->name);

  if (hls_stream->playlist) {
    gst_hls_media_playlist_unref (hls_stream->playlist);
    hls_stream->playlist = NULL;
  }

  if (hls_stream->init_file) {
    gst_m3u8_init_file_unref (hls_stream->init_file);
    hls_stream->init_file = NULL;
  }

  if (hls_stream->pending_encrypted_data)
    g_object_unref (hls_stream->pending_encrypted_data);

  gst_buffer_replace (&hls_stream->pending_decrypted_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_typefind_buffer, NULL);
  gst_buffer_replace (&hls_stream->pending_segment_data, NULL);

  if (hls_stream->playlistloader) {
    gst_hls_demux_playlist_loader_stop (hls_stream->playlistloader);
    gst_object_unparent (GST_OBJECT (hls_stream->playlistloader));
    gst_object_unref (hls_stream->playlistloader);
  }

  if (hls_stream->preloader) {
    gst_hls_demux_preloader_free (hls_stream->preloader);
    hls_stream->preloader = NULL;
  }

  if (hls_stream->moov)
    gst_isoff_moov_box_free (hls_stream->moov);

  if (hls_stream->current_key) {
    g_free (hls_stream->current_key);
    hls_stream->current_key = NULL;
  }
  if (hls_stream->current_iv) {
    g_free (hls_stream->current_iv);
    hls_stream->current_iv = NULL;
  }
  if (hls_stream->current_rendition) {
    gst_hls_rendition_stream_unref (hls_stream->current_rendition);
    hls_stream->current_rendition = NULL;
  }
  if (hls_stream->pending_rendition) {
    gst_hls_rendition_stream_unref (hls_stream->pending_rendition);
    hls_stream->pending_rendition = NULL;
  }

  if (hls_stream->current_segment) {
    gst_m3u8_media_segment_unref (hls_stream->current_segment);
    hls_stream->current_segment = NULL;
  }
  gst_hls_demux_stream_decrypt_end (hls_stream);

  G_OBJECT_CLASS (stream_parent_class)->finalize (object);
}

static gboolean
gst_hls_demux_stream_has_next_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemuxStream *hls_stream = (GstHLSDemuxStream *) stream;

  GST_DEBUG_OBJECT (stream, "has next ?");

  if (hls_stream->current_segment == NULL)
    return FALSE;

  return gst_hls_media_playlist_has_next_fragment (hls_stream->playlist,
      hls_stream->current_segment, stream->demux->segment.rate > 0);
}

static GstFlowReturn
gst_hls_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;
  GstM3U8MediaSegment *new_segment = NULL;

  /* If we're playing partial segments, we need to continue
   * doing that. We can only swap back to a full segment on a
   * segment boundary */
  if (hlsdemux_stream->in_partial_segments) {
    /* Check if there's another partial segment in this fragment */
    GstM3U8MediaSegment *cur_segment = hlsdemux_stream->current_segment;
    guint avail_segments =
        cur_segment->partial_segments !=
        NULL ? cur_segment->partial_segments->len : 0;

    if (hlsdemux_stream->part_idx + 1 < avail_segments) {
      /* Advance to the next partial segment */
      hlsdemux_stream->part_idx += 1;

      GstM3U8PartialSegment *part =
          g_ptr_array_index (cur_segment->partial_segments,
          hlsdemux_stream->part_idx);

      GST_DEBUG_OBJECT (stream,
          "Advanced to partial segment sn:%" G_GINT64_FORMAT
          " part %d stream_time:%" GST_STIME_FORMAT " uri:%s",
          hlsdemux_stream->current_segment->sequence, hlsdemux_stream->part_idx,
          GST_STIME_ARGS (part->stream_time), GST_STR_NULL (part->uri));

      return GST_FLOW_OK;
    } else if (cur_segment->partial_only) {
      /* There's no partial segment available, because we're at the live edge */
      GST_DEBUG_OBJECT (stream,
          "Hit live edge playing partial segments. Will wait for playlist update.");
      hlsdemux_stream->part_idx += 1;
      return GST_FLOW_OK;
    } else {
      /* At the end of the partial segments for this full segment. Advance to the next full segment */
      hlsdemux_stream->in_partial_segments = FALSE;
      GST_DEBUG_OBJECT (stream,
          "No more partial segments in current segment. Advancing");
    }
  }

  GST_DEBUG_OBJECT (stream,
      "Current segment sn:%" G_GINT64_FORMAT " stream_time:%" GST_STIME_FORMAT
      " uri:%s", hlsdemux_stream->current_segment->sequence,
      GST_STIME_ARGS (hlsdemux_stream->current_segment->stream_time),
      GST_STR_NULL (hlsdemux_stream->current_segment->uri));

  new_segment =
      gst_hls_media_playlist_advance_fragment (hlsdemux_stream->playlist,
      hlsdemux_stream->current_segment, stream->demux->segment.rate > 0);

  if (new_segment) {
    hlsdemux_stream->reset_pts = FALSE;
    if (new_segment->discont_sequence !=
        hlsdemux_stream->current_segment->discont_sequence)
      gst_hls_demux_add_time_mapping (hlsdemux, new_segment->discont_sequence,
          new_segment->stream_time, new_segment->datetime);
    gst_m3u8_media_segment_unref (hlsdemux_stream->current_segment);
    hlsdemux_stream->current_segment = new_segment;

    /* In LL-HLS, handle advancing into the partial-only segment */
    if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (hlsdemux_stream->playlist)
        && new_segment->partial_only) {
      hlsdemux_stream->in_partial_segments = TRUE;
      hlsdemux_stream->part_idx = 0;

      GstM3U8PartialSegment *new_part =
          g_ptr_array_index (new_segment->partial_segments,
          hlsdemux_stream->part_idx);

      GST_DEBUG_OBJECT (stream,
          "Advanced to partial segment sn:%" G_GINT64_FORMAT
          " part %u stream_time:%" GST_STIME_FORMAT " uri:%s",
          new_segment->sequence, hlsdemux_stream->part_idx,
          GST_STIME_ARGS (new_part->stream_time), GST_STR_NULL (new_part->uri));
      return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT (stream,
        "Advanced to segment sn:%" G_GINT64_FORMAT " stream_time:%"
        GST_STIME_FORMAT " uri:%s", hlsdemux_stream->current_segment->sequence,
        GST_STIME_ARGS (hlsdemux_stream->current_segment->stream_time),
        GST_STR_NULL (hlsdemux_stream->current_segment->uri));
    return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (stream, "Could not advance to next fragment");
  if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (hlsdemux_stream->playlist)) {
    gst_m3u8_media_segment_unref (hlsdemux_stream->current_segment);
    hlsdemux_stream->current_segment = NULL;
    hlsdemux_stream->in_partial_segments = FALSE;
    return GST_FLOW_OK;
  }

  return GST_FLOW_EOS;
}

static void
gst_hls_demux_stream_update_preloads (GstHLSDemuxStream * hlsdemux_stream)
{
  GstHLSMediaPlaylist *playlist = hlsdemux_stream->playlist;
  gboolean preloads_allowed = GST_HLS_MEDIA_PLAYLIST_IS_LIVE (playlist);

  if (playlist->preload_hints == NULL || !preloads_allowed) {
    if (hlsdemux_stream->preloader != NULL) {
      /* Cancel any preloads, the new playlist doesn't have them */
      gst_hls_demux_preloader_cancel (hlsdemux_stream->preloader,
          M3U8_PRELOAD_HINT_ALL);
    }
    /* Nothing to preload */
    return;
  }

  if (hlsdemux_stream->preloader == NULL) {
    GstAdaptiveDemux *demux =
        GST_ADAPTIVE_DEMUX2_STREAM (hlsdemux_stream)->demux;
    hlsdemux_stream->preloader =
        gst_hls_demux_preloader_new (demux->download_helper);
    if (hlsdemux_stream->preloader == NULL) {
      GST_WARNING_OBJECT (hlsdemux_stream, "Failed to create preload handler");
      return;
    }
  }

  /* The HLS spec says any extra preload hint of each type should be ignored */
  GstM3U8PreloadHintType seen_types = 0;
  guint idx;
  for (idx = 0; idx < playlist->preload_hints->len; idx++) {
    GstM3U8PreloadHint *hint = g_ptr_array_index (playlist->preload_hints, idx);
    switch (hint->hint_type) {
      case M3U8_PRELOAD_HINT_MAP:
      case M3U8_PRELOAD_HINT_PART:
        if (seen_types & hint->hint_type) {
          continue;             /* Ignore preload hint type we've already seen */
        }
        seen_types |= hint->hint_type;
        break;
      default:
        GST_FIXME_OBJECT (hlsdemux_stream, "Ignoring unknown preload type %d",
            hint->hint_type);
        continue;               /* Unknown hint type, ignore it */
    }
    gst_hls_demux_preloader_load (hlsdemux_stream->preloader, hint,
        playlist->uri);
  }
}

static GstFlowReturn
gst_hls_demux_stream_submit_request (GstAdaptiveDemux2Stream * stream,
    DownloadRequest * download_req)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  /* See if the request can be satisfied from a preload */
  if (hlsdemux_stream->preloader != NULL) {
    if (gst_hls_demux_preloader_provide_request (hlsdemux_stream->preloader,
            download_req))
      return GST_FLOW_OK;

    /* We're about to request something, but it wasn't the active preload,
     * so make sure that's been stopped / cancelled so we're not downloading
     * two things in parallel. This usually means the playlist refresh
     * took too long and the preload became obsolete */
    if (stream->downloading_header) {
      gst_hls_demux_preloader_cancel (hlsdemux_stream->preloader,
          M3U8_PRELOAD_HINT_MAP);
    } else {
      gst_hls_demux_preloader_cancel (hlsdemux_stream->preloader,
          M3U8_PRELOAD_HINT_PART);
    }
  }

  return
      GST_ADAPTIVE_DEMUX2_STREAM_CLASS (stream_parent_class)->submit_request
      (stream, download_req);
}

static void
gst_hls_demux_stream_handle_playlist_update (GstHLSDemuxStream * stream,
    const gchar * new_playlist_uri, GstHLSMediaPlaylist * new_playlist)
{
  GstHLSDemux *demux = GST_HLS_DEMUX_STREAM_GET_DEMUX (stream);

  /* Synchronize playlist with previous one. If we can't update the playlist
   * timing and inform the base class that we lost sync */
  if (stream->playlist
      && !gst_hls_media_playlist_sync_to_playlist (new_playlist,
          stream->playlist)) {
    /* Failure to synchronize with the previous media playlist is only fatal for
     * variant streams. */
    if (stream->is_variant) {
      GST_DEBUG_OBJECT (stream,
          "Could not synchronize new variant playlist with previous one !");
      goto lost_sync;
    }

    /* For rendition streams, we can attempt synchronization against the
     * variant playlist which is constantly updated */
    if (demux->main_stream->playlist
        && !gst_hls_media_playlist_sync_to_playlist (new_playlist,
            demux->main_stream->playlist)) {
      GST_DEBUG_OBJECT (stream,
          "Could not do fallback synchronization of rendition stream to variant stream");
      goto lost_sync;
    }
  } else if (!stream->is_variant && demux->main_stream->playlist) {
    /* For initial rendition media playlist, attempt to synchronize the playlist
     * against the variant stream. This is non-fatal if it fails. */
    GST_DEBUG_OBJECT (stream,
        "Attempting to synchronize initial rendition stream with variant stream");
    gst_hls_media_playlist_sync_to_playlist (new_playlist,
        demux->main_stream->playlist);
  }

  if (stream->current_segment) {
    GstM3U8MediaSegment *new_segment;
    GST_DEBUG_OBJECT (stream,
        "Current segment sn:%" G_GINT64_FORMAT " stream_time:%" GST_STIME_FORMAT
        " uri:%s", stream->current_segment->sequence,
        GST_STIME_ARGS (stream->current_segment->stream_time),
        GST_STR_NULL (stream->current_segment->uri));

    /* Use best-effort techniques to find the corresponding current media segment
     * in the new playlist. This might be off in some cases, but it doesn't matter
     * since we will be checking the embedded timestamp later */
    new_segment =
        gst_hls_media_playlist_sync_to_segment (new_playlist,
        stream->current_segment);

    /* Handle LL-HLS partial segment sync by checking our partial segment
     * still makes sense */
    if (stream->in_partial_segments && new_segment) {
      /* We must be either playing the trailing open-ended partial segment,
       * or if we're playing partials from a complete segment, check that we
       * still have a) partial segments attached (didn't get too old and
       * the server removed them from the playlist) and b) we didn't advance
       * beyond the end of that partial segment (when we advance past the live
       * edge and increment part_idx, then the segment completes without
       * adding any more partial segments) */
      if (!new_segment->partial_only) {
        if (new_segment->partial_segments == NULL) {
          GST_DEBUG_OBJECT (stream,
              "Partial segments we were playing became unavailable. Will try and resync");
          stream->in_partial_segments = FALSE;
          gst_m3u8_media_segment_unref (new_segment);
          new_segment = NULL;
        } else if (stream->part_idx >= new_segment->partial_segments->len) {
          GST_DEBUG_OBJECT (stream,
              "After playlist reload, there are no more partial segments to play in the current segment. Resyncing");
          stream->in_partial_segments = FALSE;
          gst_m3u8_media_segment_unref (new_segment);
          new_segment = NULL;
        }
      }
    }

    if (new_segment) {
      if (new_segment->discont_sequence !=
          stream->current_segment->discont_sequence)
        gst_hls_demux_add_time_mapping (demux, new_segment->discont_sequence,
            new_segment->stream_time, new_segment->datetime);
      /* This can happen in case of misaligned variants/renditions. Only warn about it */
      if (new_segment->stream_time != stream->current_segment->stream_time)
        GST_WARNING_OBJECT (stream,
            "Returned segment stream time %" GST_STIME_FORMAT
            " differs from current stream time %" GST_STIME_FORMAT,
            GST_STIME_ARGS (new_segment->stream_time),
            GST_STIME_ARGS (stream->current_segment->stream_time));
    } else {
      /* Not finding a matching segment only happens in live (otherwise we would
       * have found a match by stream time) when we are at the live edge. This is normal*/
      GST_DEBUG_OBJECT (stream, "Could not find a matching segment");
    }
    gst_m3u8_media_segment_unref (stream->current_segment);
    stream->current_segment = new_segment;
  } else {
    GST_DEBUG_OBJECT (stream, "No current segment");
  }

  if (stream->is_variant) {
    /* Updates on the variant playlist have some special requirements to
     * set up the time mapping and initial stream config */
    gst_hls_demux_handle_variant_playlist_update (demux, new_playlist_uri,
        new_playlist);
  } else if (stream->pending_rendition) {
    /* Switching rendition configures a new playlist on the loader,
     * and we should never get a callback for a stale download URI */
    g_assert (g_str_equal (stream->pending_rendition->uri, new_playlist_uri));

    gst_hls_rendition_stream_unref (stream->current_rendition);
    /* Stealing ref */
    stream->current_rendition = stream->pending_rendition;
    stream->pending_rendition = NULL;
  }

  if (stream->playlist)
    gst_hls_media_playlist_unref (stream->playlist);
  stream->playlist = gst_hls_media_playlist_ref (new_playlist);
  stream->playlist_fetched = TRUE;

  if (!GST_HLS_MEDIA_PLAYLIST_IS_LIVE (stream->playlist)) {
    /* Make sure to cancel any preloads if a playlist isn't live after reload */
    gst_hls_demux_stream_update_preloads (stream);
  }

  if (stream->current_segment) {
    GST_DEBUG_OBJECT (stream,
        "After update, current segment now sn:%" G_GINT64_FORMAT
        " stream_time:%" GST_STIME_FORMAT " uri:%s",
        stream->current_segment->sequence,
        GST_STIME_ARGS (stream->current_segment->stream_time),
        GST_STR_NULL (stream->current_segment->uri));
  } else {
    GST_DEBUG_OBJECT (stream, "No current segment selected");
  }

  GST_DEBUG_OBJECT (stream, "done");
  return;

  /* ERRORS */
lost_sync:
  {
    /* Set new playlist, lost sync handler will know what to do with it */
    if (stream->playlist)
      gst_hls_media_playlist_unref (stream->playlist);
    stream->playlist = new_playlist;
    stream->playlist = gst_hls_media_playlist_ref (new_playlist);
    stream->playlist_fetched = TRUE;

    gst_hls_demux_reset_for_lost_sync (demux);
  }
}

static void
on_playlist_update_success (GstHLSDemuxPlaylistLoader * pl,
    const gchar * new_playlist_uri, GstHLSMediaPlaylist * new_playlist,
    gpointer userdata)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (userdata);

  gst_hls_demux_stream_handle_playlist_update (hls_stream,
      new_playlist_uri, new_playlist);
  gst_adaptive_demux2_stream_mark_prepared (GST_ADAPTIVE_DEMUX2_STREAM_CAST
      (hls_stream));
}

static void
on_playlist_update_error (GstHLSDemuxPlaylistLoader * pl,
    const gchar * playlist_uri, gpointer userdata)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (userdata);

  /* FIXME: How to handle rendition playlist update errors? There's
   * not much we can do about it except throw an error */
  if (hls_stream->is_variant) {
    GstHLSDemux *demux = GST_HLS_DEMUX_STREAM_GET_DEMUX (hls_stream);
    gst_hls_demux_handle_variant_playlist_update_error (demux, playlist_uri);
  } else {
    GstHLSDemux *demux = GST_HLS_DEMUX_STREAM_GET_DEMUX (hls_stream);
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("Could not update rendition playlist"));
  }
}

static GstHLSDemuxPlaylistLoader *
gst_hls_demux_stream_get_playlist_loader (GstHLSDemuxStream * hls_stream)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX2_STREAM_CAST (hls_stream)->demux;
  if (hls_stream->playlistloader == NULL) {
    hls_stream->playlistloader =
        gst_hls_demux_playlist_loader_new (demux, demux->download_helper);
    gst_hls_demux_playlist_loader_set_callbacks (hls_stream->playlistloader,
        on_playlist_update_success, on_playlist_update_error, hls_stream);
  }

  return hls_stream->playlistloader;
}

void
gst_hls_demux_stream_set_playlist_uri (GstHLSDemuxStream * hls_stream,
    gchar * uri)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX2_STREAM_CAST (hls_stream)->demux;
  GstHLSDemuxPlaylistLoader *pl =
      gst_hls_demux_stream_get_playlist_loader (hls_stream);

  const gchar *main_uri = gst_adaptive_demux_get_manifest_ref_uri (demux);
  gst_hls_demux_playlist_loader_set_playlist_uri (pl, main_uri, uri);
}

void
gst_hls_demux_stream_start_playlist_loading (GstHLSDemuxStream * hls_stream)
{
  GstHLSDemuxPlaylistLoader *pl =
      gst_hls_demux_stream_get_playlist_loader (hls_stream);
  gst_hls_demux_playlist_loader_start (pl);
}

GstFlowReturn
gst_hls_demux_stream_check_current_playlist_uri (GstHLSDemuxStream * stream,
    gchar * uri)
{
  GstHLSDemuxPlaylistLoader *pl =
      gst_hls_demux_stream_get_playlist_loader (stream);

  if (!gst_hls_demux_playlist_loader_has_current_uri (pl, uri)) {
    GST_LOG_OBJECT (stream, "Target playlist not available yet");
    return GST_ADAPTIVE_DEMUX_FLOW_BUSY;
  }

  return GST_FLOW_OK;

#if 0
  /* Check if a redirect happened */
  if (g_strcmp0 (*uri, new_playlist->uri)) {
    GST_DEBUG_OBJECT (stream, "Playlist URI update : '%s'  =>  '%s'", *uri,
        new_playlist->uri);
    g_free (*uri);
    *uri = g_strdup (new_playlist->uri);
  }
#endif
}

static GstFlowReturn
gst_hls_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstAdaptiveDemux *demux = stream->demux;
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstM3U8MediaSegment *file;
  GstM3U8PartialSegment *part = NULL;
  gboolean discont;

  /* Return BUSY if no playlist is loaded yet. Even if
   * we switched an another playlist is loading, we'll keep*/
  if (!hlsdemux_stream->playlist_fetched) {
    gst_hls_demux_stream_start_playlist_loading (hlsdemux_stream);
    return GST_ADAPTIVE_DEMUX_FLOW_BUSY;
  }
  g_assert (hlsdemux_stream->playlist != NULL);
  if ((ret =
          gst_hls_demux_stream_check_current_playlist_uri (hlsdemux_stream,
              NULL)) != GST_FLOW_OK) {
    /* The URI of the playlist we have is not the target URI due
     * to a bitrate switch - wait for it to load */
    GST_DEBUG_OBJECT (hlsdemux_stream,
        "Playlist is stale. Waiting for new playlist");
    gst_hls_demux_stream_start_playlist_loading (hlsdemux_stream);
    return ret;
  }
#ifndef GST_DISABLE_GST_DEBUG
  GstClockTimeDiff live_edge_dist =
      GST_CLOCK_TIME_IS_VALID (stream->current_position) ?
      gst_hls_media_playlist_get_end_stream_time (hlsdemux_stream->playlist) -
      stream->current_position : GST_CLOCK_TIME_NONE;
  GstClockTime playlist_age =
      gst_adaptive_demux2_get_monotonic_time (GST_ADAPTIVE_DEMUX (demux)) -
      hlsdemux_stream->playlist->playlist_ts;
  GST_DEBUG_OBJECT (stream,
      "Updating fragment information, current_position:%" GST_TIME_FORMAT
      " which is %" GST_STIME_FORMAT " from live edge. Playlist age %"
      GST_TIME_FORMAT, GST_TIME_ARGS (stream->current_position),
      GST_STIME_ARGS (live_edge_dist), GST_TIME_ARGS (playlist_age));
#endif

  /* Find the current segment if we don't already have it */
  if (hlsdemux_stream->current_segment == NULL) {
    GST_LOG_OBJECT (stream, "No current segment");
    if (stream->current_position == GST_CLOCK_TIME_NONE) {
      GstM3U8SeekResult seek_result;

      GST_DEBUG_OBJECT (stream, "Setting up initial segment");

      if (gst_hls_media_playlist_get_starting_segment
          (hlsdemux_stream->playlist, &seek_result)) {
        hlsdemux_stream->current_segment = seek_result.segment;
        hlsdemux_stream->in_partial_segments =
            seek_result.found_partial_segment;
        hlsdemux_stream->part_idx = seek_result.part_idx;
      }
    } else {
      if (gst_hls_media_playlist_has_lost_sync (hlsdemux_stream->playlist,
              stream->current_position)) {
        GST_WARNING_OBJECT (stream, "Lost SYNC !");
        return GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC;
      }
      GST_DEBUG_OBJECT (stream,
          "Looking up segment for position %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->current_position));

      GstM3U8SeekResult seek_result;
      if (!gst_hls_media_playlist_find_position (hlsdemux_stream->playlist,
              stream->current_position, hlsdemux_stream->in_partial_segments,
              &seek_result)) {
        GST_INFO_OBJECT (stream, "At the end of the current media playlist");
        gst_hls_demux_stream_update_preloads (hlsdemux_stream);
        return GST_FLOW_EOS;
      }

      hlsdemux_stream->current_segment = seek_result.segment;
      hlsdemux_stream->in_partial_segments = seek_result.found_partial_segment;
      hlsdemux_stream->part_idx = seek_result.part_idx;

      /* If on a full segment, update time mapping. If it already exists it will be ignored.
       * Don't add time mappings for partial segments, wait for a full segment boundary */
      if (!hlsdemux_stream->in_partial_segments
          || hlsdemux_stream->part_idx == 0) {
        gst_hls_demux_add_time_mapping (hlsdemux,
            hlsdemux_stream->current_segment->discont_sequence,
            hlsdemux_stream->current_segment->stream_time,
            hlsdemux_stream->current_segment->datetime);
      }
    }
  }

  file = hlsdemux_stream->current_segment;

  if (hlsdemux_stream->in_partial_segments) {
    if (file->partial_segments == NULL) {
      /* I think this can only happen if we reloaded the playlist
       * and the segment we were in the middle of playing from
       * removed its partial segments because we were playing
       * too slowly */
      GST_DEBUG_OBJECT (stream,
          "Partial segment idx %d is not available in current playlist",
          hlsdemux_stream->part_idx);
      return GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC;
    }

    if (hlsdemux_stream->part_idx >= file->partial_segments->len) {
      /* Being beyond the available partial segments in the partial_only
       * segment at the end of the playlist in LL-HLS means we've
       * hit the live edge and need to wait for a playlist update */
      if (file->partial_only) {
        GST_INFO_OBJECT (stream, "At the end of the current media playlist");
        gst_hls_demux_stream_update_preloads (hlsdemux_stream);
        return GST_FLOW_EOS;
      }

      /* Otherwise, we reloaded the playlist and found that the partial_only segment we
       * were playing from became a real segment and we overstepped the end of
       * the parts. Reloading the playlist should have synced that up properly,
       * so we should never get here. */
      g_assert_not_reached ();
    }

    part =
        g_ptr_array_index (file->partial_segments, hlsdemux_stream->part_idx);

    GST_DEBUG_OBJECT (stream,
        "Current partial segment %d stream_time %" GST_STIME_FORMAT,
        hlsdemux_stream->part_idx, GST_STIME_ARGS (part->stream_time));
    discont = stream->discont;
    /* Use the segment discont flag only on the first partial segment */
    if (file->discont && hlsdemux_stream->part_idx == 0)
      discont = TRUE;
  } else {
    GST_DEBUG_OBJECT (stream, "Current segment stream_time %" GST_STIME_FORMAT,
        GST_STIME_ARGS (file->stream_time));
    discont = file->discont || stream->discont;
  }

  gboolean need_header = GST_ADAPTIVE_DEMUX2_STREAM_NEED_HEADER (stream);

  /* Check if the MAP header file changed and update it */
  if (file->init_file != NULL
      && !gst_m3u8_init_file_equal (hlsdemux_stream->init_file,
          file->init_file)) {
    GST_DEBUG_OBJECT (stream, "MAP header info changed. Updating");
    if (hlsdemux_stream->init_file != NULL)
      gst_m3u8_init_file_unref (hlsdemux_stream->init_file);
    hlsdemux_stream->init_file = gst_m3u8_init_file_ref (file->init_file);
    need_header = TRUE;
  }

  if (file->init_file && need_header) {
    GstM3U8InitFile *header_file = file->init_file;
    g_free (stream->fragment.header_uri);
    stream->fragment.header_uri = g_strdup (header_file->uri);
    stream->fragment.header_range_start = header_file->offset;
    if (header_file->size != -1) {
      stream->fragment.header_range_end =
          header_file->offset + header_file->size - 1;
    } else {
      stream->fragment.header_range_end = -1;
    }

    stream->need_header = TRUE;

    GST_DEBUG_OBJECT (stream, "Need header uri: %s %" G_GUINT64_FORMAT " %"
        G_GINT64_FORMAT, stream->fragment.header_uri,
        stream->fragment.header_range_start, stream->fragment.header_range_end);
  }

  /* set up our source for download */
  stream->fragment.stream_time = GST_CLOCK_STIME_NONE;
  g_free (stream->fragment.uri);
  stream->fragment.range_start = 0;
  stream->fragment.range_end = -1;

  /* Encryption params always come from the parent segment */
  g_free (hlsdemux_stream->current_key);
  hlsdemux_stream->current_key = g_strdup (file->key);
  g_free (hlsdemux_stream->current_iv);
  hlsdemux_stream->current_iv = g_memdup2 (file->iv, sizeof (file->iv));

  /* Other info could come from the part when playing partial segments */

  if (part == NULL) {
    if (hlsdemux_stream->reset_pts || discont || demux->segment.rate < 0.0) {
      stream->fragment.stream_time = file->stream_time;
    }
    stream->fragment.uri = g_strdup (file->uri);
    stream->fragment.range_start = file->offset;
    if (file->size != -1)
      stream->fragment.range_end = file->offset + file->size - 1;
    stream->fragment.duration = file->duration;
  } else {
    if (hlsdemux_stream->reset_pts || discont || demux->segment.rate < 0.0) {
      stream->fragment.stream_time = part->stream_time;
    }
    stream->fragment.uri = g_strdup (part->uri);
    stream->fragment.range_start = part->offset;
    if (part->size != -1)
      stream->fragment.range_end = part->offset + part->size - 1;
    stream->fragment.duration = part->duration;
  }

  GST_DEBUG_OBJECT (stream, "Stream URI now %s", stream->fragment.uri);

  stream->recommended_buffering_threshold =
      gst_hls_media_playlist_recommended_buffering_threshold
      (hlsdemux_stream->playlist);

  if (discont)
    stream->discont = TRUE;

  return ret;
}

static gboolean
gst_hls_demux_stream_can_start (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;
  GstHLSDemuxStream *hls_stream = (GstHLSDemuxStream *) stream;
  GList *tmp;

  GST_DEBUG_OBJECT (stream, "is_variant:%d mappings:%p", hls_stream->is_variant,
      hlsdemux->mappings);

  /* Variant streams can always start straight away */
  if (hls_stream->is_variant)
    return TRUE;

  /* Renditions of the exact same type as the variant are pure alternatives,
   * they must be started. This can happen for example with audio-only manifests
   * where the initial stream selected is a rendition and not a variant */
  if (hls_stream->rendition_type == hlsdemux->main_stream->rendition_type)
    return TRUE;

  /* Rendition streams only require delaying if we don't have time mappings yet */
  if (!hlsdemux->mappings)
    return FALSE;

  /* We can start if we have at least one internal time observation */
  for (tmp = hlsdemux->mappings; tmp; tmp = tmp->next) {
    GstHLSTimeMap *map = tmp->data;
    if (map->internal_time != GST_CLOCK_TIME_NONE)
      return TRUE;
  }

  /* Otherwise we have to wait */
  return FALSE;
}

static void
gst_hls_demux_stream_start (GstAdaptiveDemux2Stream * stream)
{
  if (!gst_hls_demux_stream_can_start (stream))
    return;

  /* Start the playlist loader */
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  gst_hls_demux_stream_start_playlist_loading (hls_stream);

  /* Chain up, to start the downloading */
  GST_ADAPTIVE_DEMUX2_STREAM_CLASS (stream_parent_class)->start (stream);
}

static void
gst_hls_demux_stream_stop (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  if (hls_stream->playlistloader && !hls_stream->is_variant) {
    /* Don't stop the loader for the variant stream, keep it running
     * until the scheduler itself is stopped so we keep updating
     * the live playlist timeline */
    gst_hls_demux_playlist_loader_stop (hls_stream->playlistloader);
  }

  /* Chain up, to stop the downloading */
  GST_ADAPTIVE_DEMUX2_STREAM_CLASS (stream_parent_class)->stop (stream);
}

/* Called when the variant is changed, to set a new rendition
 * for this stream to download. Returns TRUE if the rendition
 * stream switched group-id */
static gboolean
gst_hls_demux_update_rendition_stream_uri (GstHLSDemux * hlsdemux,
    GstHLSDemuxStream * hls_stream, GError ** err)
{
  gchar *current_group_id, *requested_group_id;
  GstHLSRenditionStream *replacement_media = NULL;
  GList *tmp;

  /* There always should be a current variant set */
  g_assert (hlsdemux->current_variant);
  /* There always is a GstHLSRenditionStream set for rendition streams */
  g_assert (hls_stream->current_rendition);

  requested_group_id =
      hlsdemux->current_variant->media_groups[hls_stream->
      current_rendition->mtype];
  current_group_id = hls_stream->current_rendition->group_id;

  GST_DEBUG_OBJECT (hlsdemux,
      "Checking playlist change for variant stream %s lang: %s current group-id: %s / requested group-id: %s",
      gst_stream_type_get_name (hls_stream->rendition_type), hls_stream->lang,
      current_group_id, requested_group_id);


  if (!g_strcmp0 (requested_group_id, current_group_id)) {
    GST_DEBUG_OBJECT (hlsdemux, "No change needed");
    return FALSE;
  }

  GST_DEBUG_OBJECT (hlsdemux,
      "group-id changed, looking for replacement playlist");

  /* Need to switch/update */
  for (tmp = hlsdemux->master->renditions; tmp; tmp = tmp->next) {
    GstHLSRenditionStream *cand = tmp->data;

    if (cand->mtype == hls_stream->current_rendition->mtype
        && !g_strcmp0 (cand->lang, hls_stream->lang)
        && !g_strcmp0 (cand->group_id, requested_group_id)) {
      replacement_media = cand;
      break;
    }
  }
  if (!replacement_media) {
    GST_ERROR_OBJECT (hlsdemux,
        "Could not find a replacement playlist. Staying with previous one");
    return FALSE;
  }

  GST_DEBUG_OBJECT (hlsdemux, "Use replacement playlist %s",
      replacement_media->name);
  if (hls_stream->pending_rendition) {
    GST_ERROR_OBJECT (hlsdemux,
        "Already had a pending rendition switch to '%s'",
        hls_stream->pending_rendition->name);
    gst_hls_rendition_stream_unref (hls_stream->pending_rendition);
  }
  hls_stream->pending_rendition =
      gst_hls_rendition_stream_ref (replacement_media);

  gst_hls_demux_stream_set_playlist_uri (hls_stream, replacement_media->uri);

  return TRUE;
}

static gboolean
gst_hls_demux_stream_select_bitrate (GstAdaptiveDemux2Stream * stream,
    guint64 bitrate)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (stream->demux);
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (stream->demux);
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);

  /* Fast-Path, no changes possible */
  if (hlsdemux->master == NULL || hlsdemux->master->is_simple)
    return FALSE;

  /* Currently playing partial segments, disallow bitrate
   * switches and rendition playlist changes - except exactly
   * at the first partial segment in a full segment (implying
   * we are about to play a partial segment but didn't yet) */
  if (hls_stream->in_partial_segments && hls_stream->part_idx > 0)
    return FALSE;

  if (hls_stream->is_variant) {
    gdouble play_rate = gst_adaptive_demux_play_rate (demux);
    gboolean changed = FALSE;

    /* If not calculated yet, continue using start bitrate */
    if (bitrate == 0)
      bitrate = hlsdemux->start_bitrate;

    /* Handle variant streams */
    GST_DEBUG_OBJECT (hlsdemux,
        "Checking playlist change for main variant stream");
    if (!gst_hls_demux_change_variant_playlist (hlsdemux,
            hlsdemux->current_variant->iframe,
            bitrate / MAX (1.0, ABS (play_rate)), &changed)) {
      GST_ERROR_OBJECT (hlsdemux, "Failed to choose a new variant to play");
    }

    GST_DEBUG_OBJECT (hlsdemux, "Returning changed: %d", changed);
    return changed;
  }

  /* Handle rendition streams */
  return gst_hls_demux_update_rendition_stream_uri (hlsdemux, hls_stream, NULL);
}

#if defined(HAVE_OPENSSL)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  EVP_CIPHER_CTX *ctx;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  EVP_CIPHER_CTX_init (&stream->aes_ctx);
  ctx = &stream->aes_ctx;
#else
  stream->aes_ctx = EVP_CIPHER_CTX_new ();
  ctx = stream->aes_ctx;
#endif
  if (!EVP_DecryptInit_ex (ctx, EVP_aes_128_cbc (), NULL, key_data, iv_data))
    return FALSE;
  EVP_CIPHER_CTX_set_padding (ctx, 0);
  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  int len, flen = 0;
  EVP_CIPHER_CTX *ctx;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  ctx = &stream->aes_ctx;
#else
  ctx = stream->aes_ctx;
#endif

  if (G_UNLIKELY (length > G_MAXINT || length % 16 != 0))
    return FALSE;

  len = (int) length;
  if (!EVP_DecryptUpdate (ctx, decrypted_data, &len, encrypted_data, len))
    return FALSE;
  EVP_DecryptFinal_ex (ctx, decrypted_data + len, &flen);
  g_return_val_if_fail (len + flen == length, FALSE);
  return TRUE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  EVP_CIPHER_CTX_cleanup (&stream->aes_ctx);
#else
  EVP_CIPHER_CTX_free (stream->aes_ctx);
  stream->aes_ctx = NULL;
#endif
}

#elif defined(HAVE_NETTLE)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  aes128_set_decrypt_key (&stream->aes_ctx.ctx, key_data);
  CBC_SET_IV (&stream->aes_ctx, iv_data);

  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  if (length % 16 != 0)
    return FALSE;

  CBC_DECRYPT (&stream->aes_ctx, aes128_decrypt, length, decrypted_data,
      encrypted_data);

  return TRUE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  /* NOP */
}

#elif defined(HAVE_LIBGCRYPT)
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  gcry_error_t err = 0;
  gboolean ret = FALSE;

  err =
      gcry_cipher_open (&stream->aes_ctx, GCRY_CIPHER_AES128,
      GCRY_CIPHER_MODE_CBC, 0);
  if (err)
    goto out;
  err = gcry_cipher_setkey (stream->aes_ctx, key_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_setiv (stream->aes_ctx, iv_data, 16);
  if (!err)
    ret = TRUE;

out:
  if (!ret)
    if (stream->aes_ctx)
      gcry_cipher_close (stream->aes_ctx);

  return ret;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  gcry_error_t err = 0;

  err = gcry_cipher_decrypt (stream->aes_ctx, decrypted_data, length,
      encrypted_data, length);

  return err == 0;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  if (stream->aes_ctx) {
    gcry_cipher_close (stream->aes_ctx);
    stream->aes_ctx = NULL;
  }
}

#else
/* NO crypto available */
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data)
{
  GST_ERROR ("No crypto available");
  return FALSE;
}

static gboolean
decrypt_fragment (GstHLSDemuxStream * stream, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  GST_ERROR ("Cannot decrypt fragment, no crypto available");
  return FALSE;
}

static void
gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream)
{
  return;
}
#endif

static GstBuffer *
gst_hls_demux_decrypt_fragment (GstHLSDemux * demux, GstHLSDemuxStream * stream,
    GstBuffer * encrypted_buffer, GError ** err)
{
  GstBuffer *decrypted_buffer = NULL;
  GstMapInfo encrypted_info, decrypted_info;

  decrypted_buffer =
      gst_buffer_new_allocate (NULL, gst_buffer_get_size (encrypted_buffer),
      NULL);

  gst_buffer_map (encrypted_buffer, &encrypted_info, GST_MAP_READ);
  gst_buffer_map (decrypted_buffer, &decrypted_info, GST_MAP_WRITE);

  if (!decrypt_fragment (stream, encrypted_info.size,
          encrypted_info.data, decrypted_info.data))
    goto decrypt_error;


  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);

  return decrypted_buffer;

decrypt_error:
  GST_ERROR_OBJECT (demux, "Failed to decrypt fragment");
  g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_DECRYPT,
      "Failed to decrypt fragment");

  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);
  gst_buffer_unref (decrypted_buffer);

  return NULL;
}

static GstClockTime
gst_hls_demux_stream_get_presentation_offset (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;
  GstHLSDemuxStream *hls_stream = (GstHLSDemuxStream *) stream;

  GST_DEBUG_OBJECT (stream, "presentation_offset %" GST_TIME_FORMAT,
      GST_TIME_ARGS (hls_stream->presentation_offset));

  /* If this stream and the variant stream are ISOBMFF, returns the presentation
   * offset of the variant stream */
  if (hls_stream->parser_type == GST_HLS_PARSER_ISOBMFF
      && hlsdemux->main_stream->parser_type == GST_HLS_PARSER_ISOBMFF)
    return hlsdemux->main_stream->presentation_offset;
  return hls_stream->presentation_offset;
}
