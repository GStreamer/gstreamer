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
 * Gsthlsdemux.c:
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
/**
 * SECTION:element-hlsdemux2
 * @title: hlsdemux2
 *
 * HTTP Live Streaming demuxer element.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 playbin3 uri=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8
 * ]|
 *
 * Since: 1.22
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/tag/tag.h>

#include "gsthlselements.h"
#include "gstadaptivedemuxelements.h"
#include "gsthlsdemux.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-hls"));

GST_DEBUG_CATEGORY (gst_hls_demux2_debug);
#define GST_CAT_DEFAULT gst_hls_demux2_debug

enum
{
  PROP_0,

  PROP_START_BITRATE,
};

#define DEFAULT_START_BITRATE 0

/* Maximum values for mpeg-ts DTS values */
#define MPEG_TS_MAX_PTS (((((guint64)1) << 33) * (guint64)100000) / 9)

/* GObject */
static void gst_hls_demux_finalize (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

/* GstHLSDemux */
static GstFlowReturn gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean update, GError ** err);

/* FIXME: the return value is never used? */
static gboolean gst_hls_demux_change_playlist (GstHLSDemux * demux,
    guint max_bitrate, gboolean * changed);
static GstBuffer *gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstHLSDemuxStream * stream, GstBuffer * encrypted_buffer, GError ** err);
static gboolean
gst_hls_demux_stream_decrypt_start (GstHLSDemuxStream * stream,
    const guint8 * key_data, const guint8 * iv_data);
static void gst_hls_demux_stream_decrypt_end (GstHLSDemuxStream * stream);

static gboolean gst_hls_demux_is_live (GstAdaptiveDemux * demux);
static GstClockTime gst_hls_demux_get_duration (GstAdaptiveDemux * demux);
static gint64 gst_hls_demux_get_manifest_update_interval (GstAdaptiveDemux *
    demux);
static gboolean gst_hls_demux_process_manifest (GstAdaptiveDemux * demux,
    GstBuffer * buf);
static GstFlowReturn gst_hls_demux_stream_update_rendition_playlist (GstHLSDemux
    * demux, GstHLSDemuxStream * stream);
static GstFlowReturn gst_hls_demux_update_manifest (GstAdaptiveDemux * demux);

static void setup_initial_playlist (GstHLSDemux * demux,
    GstHLSMediaPlaylist * playlist);
static void gst_hls_demux_add_time_mapping (GstHLSDemux * demux,
    gint64 dsn, GstClockTimeDiff stream_time, GDateTime * pdt);
static void
gst_hls_update_time_mappings (GstHLSDemux * demux,
    GstHLSMediaPlaylist * playlist);

static void gst_hls_prune_time_mappings (GstHLSDemux * demux);

static gboolean gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek);

static GstFlowReturn gst_hls_demux_stream_seek (GstAdaptiveDemux2Stream *
    stream, gboolean forward, GstSeekFlags flags, GstClockTimeDiff ts,
    GstClockTimeDiff * final_ts);

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
static gboolean gst_hls_demux_stream_can_start (GstAdaptiveDemux2Stream *
    stream);
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

static gboolean hlsdemux2_element_init (GstPlugin * plugin);

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (hlsdemux2, hlsdemux2_element_init);

static void
gst_hls_demux_stream_class_init (GstHLSDemuxStreamClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAdaptiveDemux2StreamClass *adaptivedemux2stream_class =
      GST_ADAPTIVE_DEMUX2_STREAM_CLASS (klass);

  gobject_class->finalize = gst_hls_demux_stream_finalize;

  adaptivedemux2stream_class->update_fragment_info =
      gst_hls_demux_stream_update_fragment_info;
  adaptivedemux2stream_class->has_next_fragment =
      gst_hls_demux_stream_has_next_fragment;
  adaptivedemux2stream_class->stream_seek = gst_hls_demux_stream_seek;
  adaptivedemux2stream_class->advance_fragment =
      gst_hls_demux_stream_advance_fragment;
  adaptivedemux2stream_class->select_bitrate =
      gst_hls_demux_stream_select_bitrate;
  adaptivedemux2stream_class->can_start = gst_hls_demux_stream_can_start;
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

typedef struct _GstHLSDemux2 GstHLSDemux2;
typedef struct _GstHLSDemux2Class GstHLSDemux2Class;

#define gst_hls_demux2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstHLSDemux2, gst_hls_demux2, GST_TYPE_ADAPTIVE_DEMUX,
    hls2_element_init ());

static void gst_hls_demux_reset (GstAdaptiveDemux * demux);
static gboolean gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * start, gint64 * stop);
static void gst_hls_demux_set_current_variant (GstHLSDemux * hlsdemux,
    GstHLSVariantStream * variant);

static void
gst_hls_demux_finalize (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
  g_mutex_clear (&demux->keys_lock);
  if (demux->keys) {
    g_hash_table_unref (demux->keys);
    demux->keys = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_START_BITRATE:
      demux->start_bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hls_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_START_BITRATE:
      g_value_set_uint (value, demux->start_bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_hls_demux2_class_init (GstHLSDemux2Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstAdaptiveDemuxClass *adaptivedemux_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  adaptivedemux_class = (GstAdaptiveDemuxClass *) klass;

  gobject_class->set_property = gst_hls_demux_set_property;
  gobject_class->get_property = gst_hls_demux_get_property;
  gobject_class->finalize = gst_hls_demux_finalize;

  g_object_class_install_property (gobject_class, PROP_START_BITRATE,
      g_param_spec_uint ("start-bitrate", "Starting Bitrate",
          "Initial bitrate to use to choose first alternate (0 = automatic) (bits/s)",
          0, G_MAXUINT, DEFAULT_START_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_static_metadata (element_class,
      "HLS Demuxer",
      "Codec/Demuxer/Adaptive",
      "HTTP Live Streaming demuxer",
      "Edward Hervey <edward@centricular.com>\n"
      "Jan Schmidt <jan@centricular.com>");

  adaptivedemux_class->is_live = gst_hls_demux_is_live;
  adaptivedemux_class->get_live_seek_range = gst_hls_demux_get_live_seek_range;
  adaptivedemux_class->get_duration = gst_hls_demux_get_duration;
  adaptivedemux_class->get_manifest_update_interval =
      gst_hls_demux_get_manifest_update_interval;
  adaptivedemux_class->process_manifest = gst_hls_demux_process_manifest;
  adaptivedemux_class->update_manifest = gst_hls_demux_update_manifest;
  adaptivedemux_class->reset = gst_hls_demux_reset;
  adaptivedemux_class->seek = gst_hls_demux_seek;
}

static void
gst_hls_demux2_init (GstHLSDemux * demux)
{
  demux->keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_mutex_init (&demux->keys_lock);
}

static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstHLSDemux *demux = GST_HLS_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_hls_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));
      g_hash_table_remove_all (demux->keys);
      break;
    default:
      break;
  }
  return ret;
}

static guint64
gst_hls_demux_get_bitrate (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (hlsdemux);

  /* FIXME !!!
   *
   * No, there isn't a single output :D */

  /* Valid because hlsdemux only has a single output */
  if (demux->input_period->streams) {
    GstAdaptiveDemux2Stream *stream = demux->input_period->streams->data;
    return stream->current_download_rate;
  }

  return 0;
}

static void
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

static void
gst_hls_demux_clear_all_pending_data (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = (GstAdaptiveDemux *) hlsdemux;
  GList *walk;

  if (!demux->input_period)
    return;

  for (walk = demux->input_period->streams; walk != NULL; walk = walk->next) {
    GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (walk->data);
    gst_hls_demux_stream_clear_pending_data (hls_stream, TRUE);
  }
}

#define SEEK_UPDATES_PLAY_POSITION(r, start_type, stop_type) \
  ((r >= 0 && start_type != GST_SEEK_TYPE_NONE) || \
   (r < 0 && stop_type != GST_SEEK_TYPE_NONE))

#define IS_SNAP_SEEK(f) (f & (GST_SEEK_FLAG_SNAP_BEFORE |	  \
                              GST_SEEK_FLAG_SNAP_AFTER |	  \
                              GST_SEEK_FLAG_SNAP_NEAREST |	  \
			      GST_SEEK_FLAG_TRICKMODE_KEY_UNITS | \
			      GST_SEEK_FLAG_KEY_UNIT))

static gboolean
gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate, old_rate;
  GList *walk;
  gint64 current_pos, target_pos, final_pos;
  guint64 bitrate;

  gst_event_parse_seek (seek, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (!SEEK_UPDATES_PLAY_POSITION (rate, start_type, stop_type)) {
    /* nothing to do if we don't have to update the current position */
    return TRUE;
  }

  old_rate = demux->segment.rate;

  bitrate = gst_hls_demux_get_bitrate (hlsdemux);

  /* Use I-frame variants for trick modes */
  if (hlsdemux->master->iframe_variants != NULL
      && rate < -1.0 && old_rate >= -1.0 && old_rate <= 1.0) {
    GError *err = NULL;

    /* Switch to I-frame variant */
    gst_hls_demux_set_current_variant (hlsdemux,
        hlsdemux->master->iframe_variants->data);

    if (gst_hls_demux_update_playlist (hlsdemux, FALSE, &err) != GST_FLOW_OK) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;

    gst_hls_demux_change_playlist (hlsdemux, bitrate / ABS (rate), NULL);
  } else if (rate > -1.0 && rate <= 1.0 && (old_rate < -1.0 || old_rate > 1.0)) {
    GError *err = NULL;
    /* Switch to normal variant */
    gst_hls_demux_set_current_variant (hlsdemux,
        hlsdemux->master->variants->data);

    if (gst_hls_demux_update_playlist (hlsdemux, FALSE, &err) != GST_FLOW_OK) {
      GST_ELEMENT_ERROR_FROM_ERROR (hlsdemux, "Could not switch playlist", err);
      return FALSE;
    }
    //hlsdemux->discont = TRUE;
    /* TODO why not continue using the same? that was being used up to now? */
    gst_hls_demux_change_playlist (hlsdemux, bitrate, NULL);
  }

  target_pos = rate < 0 ? stop : start;
  final_pos = target_pos;

  /* properly cleanup pending decryption status */
  if (flags & GST_SEEK_FLAG_FLUSH) {
    gst_hls_demux_clear_all_pending_data (hlsdemux);
    gst_hls_prune_time_mappings (hlsdemux);
  }

  for (walk = demux->input_period->streams; walk; walk = g_list_next (walk)) {
    GstAdaptiveDemux2Stream *stream =
        GST_ADAPTIVE_DEMUX2_STREAM_CAST (walk->data);

    /* Only seek on selected streams */
    if (!gst_adaptive_demux2_stream_is_selected (stream))
      continue;

    if (gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos,
            &current_pos) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (stream, "Failed to seek on stream");
      return FALSE;
    }

    /* FIXME: use minimum position always ? */
    if (final_pos > current_pos)
      final_pos = current_pos;
  }

  if (IS_SNAP_SEEK (flags)) {
    if (rate >= 0)
      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          final_pos, stop_type, stop, NULL);
    else
      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          start, stop_type, final_pos, NULL);
  }

  return TRUE;
}

static GstFlowReturn
gst_hls_demux_stream_seek (GstAdaptiveDemux2Stream * stream, gboolean forward,
    GstSeekFlags flags, GstClockTimeDiff ts, GstClockTimeDiff * final_ts)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;
  GstM3U8MediaSegment *new_position;

  GST_DEBUG_OBJECT (stream,
      "is_variant:%d media:%p current_variant:%p forward:%d ts:%"
      GST_TIME_FORMAT, hls_stream->is_variant, hls_stream->current_rendition,
      hlsdemux->current_variant, forward, GST_TIME_ARGS (ts));

  /* If the rendition playlist needs to be updated, do it now */
  if (!hls_stream->is_variant && !hls_stream->playlist_fetched) {
    ret = gst_hls_demux_stream_update_rendition_playlist (hlsdemux, hls_stream);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (stream,
          "Failed to update the rendition playlist before seeking");
      return ret;
    }
  }

  new_position =
      gst_hls_media_playlist_seek (hls_stream->playlist, forward, flags, ts);
  if (new_position) {
    if (hls_stream->current_segment)
      gst_m3u8_media_segment_unref (hls_stream->current_segment);
    hls_stream->current_segment = new_position;
    hls_stream->reset_pts = TRUE;
    if (final_ts)
      *final_ts = new_position->stream_time;
  } else {
    GST_WARNING_OBJECT (stream, "Seeking failed");
    ret = GST_FLOW_ERROR;
  }

  return ret;
}

static GstFlowReturn
gst_hls_demux_update_manifest (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  return gst_hls_demux_update_playlist (hlsdemux, TRUE, NULL);
}

static GstAdaptiveDemux2Stream *
create_common_hls_stream (GstHLSDemux * demux, const gchar * name)
{
  GstAdaptiveDemux2Stream *stream;

  stream = g_object_new (GST_TYPE_HLS_DEMUX_STREAM, "name", name, NULL);
  gst_adaptive_demux2_add_stream ((GstAdaptiveDemux *) demux, stream);

  return stream;
}

static GstAdaptiveDemuxTrack *
new_track_for_rendition (GstHLSDemux * demux, GstHLSRenditionStream * rendition,
    GstCaps * caps, GstStreamFlags flags, GstTagList * tags)
{
  GstAdaptiveDemuxTrack *track;
  gchar *stream_id;
  GstStreamType stream_type = gst_stream_type_from_hls_type (rendition->mtype);

  if (rendition->name)
    stream_id =
        g_strdup_printf ("%s-%s", gst_stream_type_get_name (stream_type),
        rendition->name);
  else if (rendition->lang)
    stream_id =
        g_strdup_printf ("%s-%s", gst_stream_type_get_name (stream_type),
        rendition->lang);
  else
    stream_id = g_strdup (gst_stream_type_get_name (stream_type));

  if (rendition->lang) {
    if (tags == NULL)
      tags = gst_tag_list_new_empty ();
    if (gst_tag_check_language_code (rendition->lang))
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_LANGUAGE_CODE,
          rendition->lang, NULL);
    else
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_LANGUAGE_NAME,
          rendition->lang, NULL);
  }

  if (stream_type == GST_STREAM_TYPE_TEXT)
    flags |= GST_STREAM_FLAG_SPARSE;

  if (rendition->is_default)
    flags |= GST_STREAM_FLAG_SELECT;

  track =
      gst_adaptive_demux_track_new ((GstAdaptiveDemux *) demux, stream_type,
      flags, stream_id, caps, tags);
  g_free (stream_id);

  return track;
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
          new_track_for_rendition (hlsdemux, embedded_media, manifest_caps,
          flags, tags ? gst_tag_list_make_writable (tags) : tags);
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

static void
create_main_variant_stream (GstHLSDemux * demux)
{
  GstAdaptiveDemux2Stream *stream;
  GstHLSDemuxStream *hlsdemux_stream;

  GST_DEBUG_OBJECT (demux, "Creating main variant stream");

  stream = create_common_hls_stream (demux, "hlsstream-variant");
  demux->main_stream = hlsdemux_stream = (GstHLSDemuxStream *) stream;
  hlsdemux_stream->is_variant = TRUE;
  hlsdemux_stream->playlist_fetched = TRUE;
  /* Due to HLS manifest information being so unreliable/inconsistent, we will
   * create the actual tracks once we have information about the streams present
   * in the variant data stream */
  stream->pending_tracks = TRUE;
}

static GstHLSDemuxStream *
create_rendition_stream (GstHLSDemux * demux, GstHLSRenditionStream * media)
{
  GstAdaptiveDemux2Stream *stream;
  GstAdaptiveDemuxTrack *track;
  GstHLSDemuxStream *hlsdemux_stream;
  gchar *stream_name;

  GST_DEBUG_OBJECT (demux,
      "Creating stream for media %s lang:%s (%" GST_PTR_FORMAT ")", media->name,
      media->lang, media->caps);

  /* We can't reliably provide caps for HLS target tracks since they might
   * change at any point in time */
  track = new_track_for_rendition (demux, media, NULL, 0, NULL);

  stream_name = g_strdup_printf ("hlsstream-%s", track->stream_id);
  stream = create_common_hls_stream (demux, stream_name);
  g_free (stream_name);
  hlsdemux_stream = (GstHLSDemuxStream *) stream;

  hlsdemux_stream->is_variant = FALSE;
  hlsdemux_stream->playlist_fetched = FALSE;
  stream->stream_type = hlsdemux_stream->rendition_type =
      gst_stream_type_from_hls_type (media->mtype);
  if (media->lang)
    hlsdemux_stream->lang = g_strdup (media->lang);
  if (media->name)
    hlsdemux_stream->name = g_strdup (media->name);

  gst_adaptive_demux2_stream_add_track (stream, track);
  gst_adaptive_demux_track_unref (track);

  return hlsdemux_stream;
}

static GstHLSDemuxStream *
existing_rendition_stream (GList * streams, GstHLSRenditionStream * media)
{
  GList *tmp;
  GstStreamType stream_type = gst_stream_type_from_hls_type (media->mtype);

  for (tmp = streams; tmp; tmp = tmp->next) {
    GstHLSDemuxStream *demux_stream = tmp->data;

    if (demux_stream->is_variant)
      continue;

    if (demux_stream->rendition_type == stream_type) {
      if (!g_strcmp0 (demux_stream->name, media->name))
        return demux_stream;
      if (media->lang && !g_strcmp0 (demux_stream->lang, media->lang))
        return demux_stream;
    }
  }

  return NULL;
}

static gboolean
gst_hls_demux_setup_streams (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstHLSVariantStream *playlist = hlsdemux->current_variant;
  GList *tmp;
  GList *streams = NULL;

  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Can't configure streams - no variant selected");
    return FALSE;
  }

  GST_DEBUG_OBJECT (demux, "Setting up streams");

  /* If there are alternate renditions, we will produce a GstAdaptiveDemux2Stream
   * and GstAdaptiveDemuxTrack for each combination of GstStreamType and other
   * unique identifier (for now just language)
   *
   * Which actual GstHLSMedia to use for each stream will be determined based on
   * the `group-id` (if present and more than one) selected on the main variant
   * stream */
  for (tmp = hlsdemux->master->renditions; tmp; tmp = tmp->next) {
    GstHLSRenditionStream *media = tmp->data;
    GstHLSDemuxStream *media_stream, *previous_media_stream;

    GST_LOG_OBJECT (demux, "Rendition %s name:'%s' lang:'%s' uri:%s",
        gst_stream_type_get_name (gst_stream_type_from_hls_type (media->mtype)),
        media->name, media->lang, media->uri);

    if (media->uri == NULL) {
      GST_DEBUG_OBJECT (demux,
          "Skipping media '%s' , it's provided by the variant stream",
          media->name);
      continue;
    }

    media_stream = previous_media_stream =
        existing_rendition_stream (streams, media);

    if (!media_stream) {
      media_stream = create_rendition_stream (hlsdemux, tmp->data);
    } else
      GST_DEBUG_OBJECT (demux, "Re-using existing GstHLSDemuxStream %s %s",
          media_stream->name, media_stream->lang);

    /* Is this rendition active in the current variant ? */
    if (!g_strcmp0 (playlist->media_groups[media->mtype], media->group_id)) {
      GST_DEBUG_OBJECT (demux, "Enabling rendition");
      if (media_stream->current_rendition)
        gst_hls_rendition_stream_unref (media_stream->current_rendition);
      media_stream->current_rendition = gst_hls_rendition_stream_ref (media);
    }

    if (!previous_media_stream)
      streams = g_list_append (streams, media_stream);
  }

  /* Free the list (but not the contents, which are stored
   * elsewhere */
  if (streams)
    g_list_free (streams);

  create_main_variant_stream (hlsdemux);

  return TRUE;
}

static const gchar *
gst_adaptive_demux_get_manifest_ref_uri (GstAdaptiveDemux * d)
{
  return d->manifest_base_uri ? d->manifest_base_uri : d->manifest_uri;
}

static void
gst_hls_demux_set_current_variant (GstHLSDemux * hlsdemux,
    GstHLSVariantStream * variant)
{
  if (hlsdemux->current_variant == variant || variant == NULL)
    return;

  if (hlsdemux->current_variant != NULL) {
    GST_DEBUG_OBJECT (hlsdemux, "Will switch from variant '%s' to '%s'",
        hlsdemux->current_variant->name, variant->name);
    if (hlsdemux->pending_variant) {
      GST_ERROR_OBJECT (hlsdemux, "Already waiting for pending variant '%s'",
          hlsdemux->pending_variant->name);
      gst_hls_variant_stream_unref (hlsdemux->pending_variant);
    }
    hlsdemux->pending_variant = gst_hls_variant_stream_ref (variant);
  } else {
    GST_DEBUG_OBJECT (hlsdemux, "Setting variant '%s'", variant->name);
    hlsdemux->current_variant = gst_hls_variant_stream_ref (variant);
  }
}

static gboolean
gst_hls_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstHLSVariantStream *variant;
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gchar *playlist = NULL;
  gboolean ret;
  GstHLSMediaPlaylist *simple_media_playlist = NULL;

  GST_INFO_OBJECT (demux, "Initial playlist location: %s (base uri: %s)",
      demux->manifest_uri, demux->manifest_base_uri);

  playlist = gst_hls_buf_to_utf8_text (buf);
  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Error validating initial playlist");
    return FALSE;
  }

  if (hlsdemux->master) {
    gst_hls_master_playlist_unref (hlsdemux->master);
    hlsdemux->master = NULL;
  }
  hlsdemux->master = gst_hls_master_playlist_new_from_data (playlist,
      gst_adaptive_demux_get_manifest_ref_uri (demux));

  if (hlsdemux->master == NULL) {
    /* In most cases, this will happen if we set a wrong url in the
     * source element and we have received the 404 HTML response instead of
     * the playlist */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid playlist."),
        ("Could not parse playlist. Check if the URL is correct."));
    return FALSE;
  }

  if (hlsdemux->master->is_simple) {
    simple_media_playlist =
        gst_hls_media_playlist_parse (playlist,
        gst_adaptive_demux_get_manifest_ref_uri (demux), NULL);
  }

  /* select the initial variant stream */
  if (demux->connection_speed == 0) {
    variant = hlsdemux->master->default_variant;
  } else if (hlsdemux->start_bitrate > 0) {
    variant =
        gst_hls_master_playlist_get_variant_for_bitrate (hlsdemux->master,
        NULL, hlsdemux->start_bitrate, demux->min_bitrate);
  } else {
    variant =
        gst_hls_master_playlist_get_variant_for_bitrate (hlsdemux->master,
        NULL, demux->connection_speed, demux->min_bitrate);
  }

  if (variant) {
    GST_INFO_OBJECT (hlsdemux,
        "Manifest processed, initial variant selected : `%s`", variant->name);
    gst_hls_demux_set_current_variant (hlsdemux, variant);      // FIXME: inline?
  }

  GST_DEBUG_OBJECT (hlsdemux, "Manifest handled, now setting up streams");

  ret = gst_hls_demux_setup_streams (demux);

  if (simple_media_playlist) {
    hlsdemux->main_stream->playlist = simple_media_playlist;
    hlsdemux->main_stream->current_segment =
        gst_hls_media_playlist_get_starting_segment (simple_media_playlist);
    setup_initial_playlist (hlsdemux, simple_media_playlist);
    gst_hls_update_time_mappings (hlsdemux, simple_media_playlist);
    gst_hls_media_playlist_dump (simple_media_playlist);
  }

  /* get the selected media playlist (unless the initial list was one already) */
  if (!hlsdemux->master->is_simple) {
    GError *err = NULL;

    if (gst_hls_demux_update_playlist (hlsdemux, FALSE, &err) != GST_FLOW_OK) {
      GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not fetch media playlist",
          err);
      return FALSE;
    }
  }

  return ret;
}

static GstClockTime
gst_hls_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  if (hlsdemux->main_stream)
    duration =
        gst_hls_media_playlist_get_duration (hlsdemux->main_stream->playlist);

  return duration;
}

static gboolean
gst_hls_demux_is_live (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean is_live = FALSE;

  if (hlsdemux->main_stream)
    is_live = gst_hls_media_playlist_is_live (hlsdemux->main_stream->playlist);

  return is_live;
}

static const GstHLSKey *
gst_hls_demux_get_key (GstHLSDemux * demux, const gchar * key_url,
    const gchar * referer, gboolean allow_cache)
{
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX (demux);
  DownloadRequest *key_request;
  DownloadFlags dl_flags = DOWNLOAD_FLAG_NONE;
  GstBuffer *key_buffer;
  GstHLSKey *key;
  GError *err = NULL;

  GST_LOG_OBJECT (demux, "Looking up key for key url %s", key_url);

  g_mutex_lock (&demux->keys_lock);

  key = g_hash_table_lookup (demux->keys, key_url);

  if (key != NULL) {
    GST_LOG_OBJECT (demux, "Found key for key url %s in key cache", key_url);
    goto out;
  }

  GST_INFO_OBJECT (demux, "Fetching key %s", key_url);

  if (!allow_cache)
    dl_flags |= DOWNLOAD_FLAG_FORCE_REFRESH;

  key_request =
      downloadhelper_fetch_uri (adaptive_demux->download_helper,
      key_url, referer, dl_flags, &err);
  if (key_request == NULL) {
    GST_WARNING_OBJECT (demux, "Failed to download key to decrypt data: %s",
        err ? err->message : "error");
    g_clear_error (&err);
    goto out;
  }

  key_buffer = download_request_take_buffer (key_request);
  download_request_unref (key_request);

  key = g_new0 (GstHLSKey, 1);
  if (gst_buffer_extract (key_buffer, 0, key->data, 16) < 16)
    GST_WARNING_OBJECT (demux, "Download decryption key is too short!");

  g_hash_table_insert (demux->keys, g_strdup (key_url), key);

  gst_buffer_unref (key_buffer);

out:

  g_mutex_unlock (&demux->keys_lock);

  if (key != NULL)
    GST_MEMDUMP_OBJECT (demux, "Key", key->data, 16);

  return key;
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

static void
gst_hls_demux_start_rendition_streams (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = (GstAdaptiveDemux *) hlsdemux;
  GList *tmp;

  for (tmp = demux->input_period->streams; tmp; tmp = tmp->next) {
    GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) tmp->data;
    GstHLSDemuxStream *hls_stream = (GstHLSDemuxStream *) stream;

    if (!hls_stream->is_variant
        && gst_adaptive_demux2_stream_is_selected (stream))
      gst_adaptive_demux2_stream_start (stream);
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

static GstHLSTimeMap *
time_map_in_list (GList * list, gint64 dsn)
{
  GList *iter;

  for (iter = list; iter; iter = iter->next) {
    GstHLSTimeMap *map = iter->data;

    if (map->dsn == dsn)
      return map;
  }

  return NULL;
}

GstHLSTimeMap *
gst_hls_find_time_map (GstHLSDemux * demux, gint64 dsn)
{
  return time_map_in_list (demux->mappings, dsn);
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
gst_hlsdemux_handle_internal_time (GstHLSDemux * demux,
    GstHLSDemuxStream * hls_stream, GstClockTime internal_time)
{
  GstM3U8MediaSegment *current_segment = hls_stream->current_segment;
  GstHLSTimeMap *map;
  GstClockTimeDiff current_stream_time;
  GstClockTimeDiff real_stream_time, difference;

  g_return_val_if_fail (current_segment != NULL, GST_HLS_PARSER_RESULT_ERROR);

  current_stream_time = current_segment->stream_time;

  GST_DEBUG_OBJECT (hls_stream,
      "Got internal time %" GST_TIME_FORMAT " for current segment stream time %"
      GST_STIME_FORMAT, GST_TIME_ARGS (internal_time),
      GST_STIME_ARGS (current_stream_time));

  map = gst_hls_find_time_map (demux, current_segment->discont_sequence);

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
    /* Update the value */
    GST_DEBUG_OBJECT (hls_stream,
        "Updating current stream time to %" GST_STIME_FORMAT,
        GST_STIME_ARGS (real_stream_time));
    current_segment->stream_time = real_stream_time;

    gst_hls_media_playlist_recalculate_stream_time (hls_stream->playlist,
        hls_stream->current_segment);
    gst_hls_media_playlist_dump (hls_stream->playlist);

    if (ABS (difference) > (hls_stream->current_segment->duration / 2)) {
      GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) hls_stream;
      GstM3U8MediaSegment *actual_segment;

      /* We are at the wrong segment, try to figure out the *actual* segment */
      GST_DEBUG_OBJECT (hls_stream,
          "Trying to seek to the correct segment for %" GST_STIME_FORMAT,
          GST_STIME_ARGS (current_stream_time));
      actual_segment =
          gst_hls_media_playlist_seek (hls_stream->playlist, TRUE,
          GST_SEEK_FLAG_SNAP_NEAREST, current_stream_time);

      if (actual_segment) {
        GST_DEBUG_OBJECT (hls_stream, "Synced to position %" GST_STIME_FORMAT,
            GST_STIME_ARGS (actual_segment->stream_time));
        gst_m3u8_media_segment_unref (hls_stream->current_segment);
        hls_stream->current_segment = actual_segment;
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
      gst_hls_find_time_map (demux,
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
      hls_stream->do_typefind, hls_stream->current_segment->uri);

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

  GST_DEBUG_OBJECT (stream, "Finishing fragment uri:%s",
      hls_stream->current_segment->uri);

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
    return GST_FLOW_OK;
  }

  if (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED) {
    /* We can update the stream current position with a more accurate value
     * before advancing. Note that we don't have any period so we can set the
     * stream_time as-is on the stream current position */
    stream->current_position = hls_stream->current_segment->stream_time;
    return gst_adaptive_demux2_stream_advance_fragment (stream,
        hls_stream->current_segment->duration);
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

  if (file == NULL)
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
    gst_adaptive_demux2_stream_set_tags (stream,
        gst_tag_list_new (GST_TAG_DATE_TIME,
            gst_date_time_new_from_g_date_time (g_date_time_ref
                (file->datetime)), NULL));
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

  return gst_hls_media_playlist_has_next_fragment (hls_stream->playlist,
      hls_stream->current_segment, stream->demux->segment.rate > 0);
}

static GstFlowReturn
gst_hls_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstHLSDemux *hlsdemux = (GstHLSDemux *) stream->demux;
  GstM3U8MediaSegment *new_segment = NULL;

  GST_DEBUG_OBJECT (stream,
      "Current segment sn:%" G_GINT64_FORMAT " stream_time:%" GST_STIME_FORMAT
      " uri:%s", hlsdemux_stream->current_segment->sequence,
      GST_STIME_ARGS (hlsdemux_stream->current_segment->stream_time),
      hlsdemux_stream->current_segment->uri);

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
    GST_DEBUG_OBJECT (stream,
        "Advanced to segment sn:%" G_GINT64_FORMAT " stream_time:%"
        GST_STIME_FORMAT " uri:%s", hlsdemux_stream->current_segment->sequence,
        GST_STIME_ARGS (hlsdemux_stream->current_segment->stream_time),
        hlsdemux_stream->current_segment->uri);
    return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (stream, "Could not advance to next fragment");
  if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (hlsdemux_stream->playlist)) {
    gst_m3u8_media_segment_unref (hlsdemux_stream->current_segment);
    hlsdemux_stream->current_segment = NULL;
    return GST_FLOW_OK;
  }

  return GST_FLOW_EOS;
}

static GstHLSMediaPlaylist *
download_media_playlist (GstHLSDemux * demux, gchar * uri, GError ** err,
    GstHLSMediaPlaylist * current)
{
  GstAdaptiveDemux *adaptive_demux;
  const gchar *main_uri;
  DownloadRequest *download;
  GstBuffer *buf;
  gchar *playlist_data;
  GstHLSMediaPlaylist *playlist = NULL;
  gchar *base_uri;
  gboolean playlist_uri_change = FALSE;

  adaptive_demux = GST_ADAPTIVE_DEMUX (demux);
  main_uri = gst_adaptive_demux_get_manifest_ref_uri (adaptive_demux);

  /* If there's no previous playlist, or the URI changed this
   * is not a refresh/update but a switch to a new playlist */
  playlist_uri_change = (current == NULL || g_strcmp0 (uri, current->uri) != 0);

  if (!playlist_uri_change) {
    GST_LOG_OBJECT (demux, "Updating the playlist");
  }

  download =
      downloadhelper_fetch_uri (adaptive_demux->download_helper,
      uri, main_uri, DOWNLOAD_FLAG_COMPRESS | DOWNLOAD_FLAG_FORCE_REFRESH, err);

  if (download == NULL)
    return NULL;

  /* Set the base URI of the playlist to the redirect target if any */
  if (download->redirect_permanent && download->redirect_uri) {
    uri = g_strdup (download->redirect_uri);
    base_uri = NULL;
  } else {
    uri = g_strdup (download->uri);
    base_uri = g_strdup (download->redirect_uri);
  }

  if (download->state == DOWNLOAD_REQUEST_STATE_ERROR) {
    GST_WARNING_OBJECT (demux,
        "Couldn't get the playlist, got HTTP status code %d",
        download->status_code);
    download_request_unref (download);
    if (err)
      g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
          "Couldn't download the playlist");
    goto out;
  }
  buf = download_request_take_buffer (download);
  download_request_unref (download);

  /* there should be a buf if there wasn't an error (handled above) */
  g_assert (buf);

  playlist_data = gst_hls_buf_to_utf8_text (buf);
  gst_buffer_unref (buf);

  if (playlist_data == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't validate playlist encoding");
    if (err)
      g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
          "Couldn't validate playlist encoding");
    goto out;
  }

  if (!playlist_uri_change && current
      && gst_hls_media_playlist_has_same_data (current, playlist_data)) {
    GST_DEBUG_OBJECT (demux, "Same playlist data");
    playlist = gst_hls_media_playlist_ref (current);
    playlist->reloaded = TRUE;
    g_free (playlist_data);
  } else {
    playlist = gst_hls_media_playlist_parse (playlist_data, uri, base_uri);
    if (!playlist) {
      GST_WARNING_OBJECT (demux, "Couldn't parse playlist");
      if (err)
        g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
            "Couldn't parse playlist");
    }
  }

out:
  g_free (uri);
  g_free (base_uri);

  return playlist;
}

static GstHLSTimeMap *
gst_hls_time_map_new (void)
{
  GstHLSTimeMap *map = g_new0 (GstHLSTimeMap, 1);

  map->stream_time = GST_CLOCK_TIME_NONE;
  map->internal_time = GST_CLOCK_TIME_NONE;

  return map;
}

static void
gst_hls_time_map_free (GstHLSTimeMap * map)
{
  if (map->pdt)
    g_date_time_unref (map->pdt);
  g_free (map);
}

static void
gst_hls_demux_add_time_mapping (GstHLSDemux * demux, gint64 dsn,
    GstClockTimeDiff stream_time, GDateTime * pdt)
{
#ifndef GST_DISABLE_GST_DEBUG
  gchar *datestring = NULL;
#endif
  GstHLSTimeMap *map;
  GList *tmp;
  GstClockTime offset = 0;

  /* Check if we don't already have a mapping for the given dsn */
  for (tmp = demux->mappings; tmp; tmp = tmp->next) {
    GstHLSTimeMap *map = tmp->data;

    if (map->dsn == dsn) {
#ifndef GST_DISABLE_GST_DEBUG
      if (map->pdt)
        datestring = g_date_time_format_iso8601 (map->pdt);
      GST_DEBUG_OBJECT (demux,
          "Already have mapping, dsn:%" G_GINT64_FORMAT " stream_time:%"
          GST_TIME_FORMAT " internal_time:%" GST_TIME_FORMAT " pdt:%s",
          map->dsn, GST_TIME_ARGS (map->stream_time),
          GST_TIME_ARGS (map->internal_time), datestring);
      g_free (datestring);
#endif
      return;
    }
  }

#ifndef GST_DISABLE_GST_DEBUG
  if (pdt)
    datestring = g_date_time_format_iso8601 (pdt);
  GST_DEBUG_OBJECT (demux,
      "New mapping, dsn:%" G_GINT64_FORMAT " stream_time:%" GST_TIME_FORMAT
      " pdt:%s", dsn, GST_TIME_ARGS (stream_time), datestring);
  g_free (datestring);
#endif

  if (stream_time < 0) {
    offset = -stream_time;
    stream_time = 0;
    /* Handle negative stream times. This can happen for example when the server
     * returns an older playlist.
     *
     * Shift the values accordingly to end up with non-negative reference stream
     * time */
    GST_DEBUG_OBJECT (demux,
        "Shifting values before storage (offset : %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (offset));
  }

  map = gst_hls_time_map_new ();
  map->dsn = dsn;
  map->stream_time = stream_time;
  if (pdt) {
    if (offset)
      map->pdt = g_date_time_add (pdt, offset / GST_USECOND);
    else
      map->pdt = g_date_time_ref (pdt);
  }

  demux->mappings = g_list_append (demux->mappings, map);
}

/* Remove any time mapping which isn't currently used by any stream playlist */
static void
gst_hls_prune_time_mappings (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = (GstAdaptiveDemux *) hlsdemux;
  GList *active = NULL;
  GList *iterstream;

  for (iterstream = demux->input_period->streams; iterstream;
      iterstream = iterstream->next) {
    GstAdaptiveDemux2Stream *stream = iterstream->data;
    GstHLSDemuxStream *hls_stream = (GstHLSDemuxStream *) stream;
    gint64 dsn = G_MAXINT64;
    guint idx, len;

    if (!hls_stream->playlist)
      continue;
    len = hls_stream->playlist->segments->len;
    for (idx = 0; idx < len; idx++) {
      GstM3U8MediaSegment *segment =
          g_ptr_array_index (hls_stream->playlist->segments, idx);

      if (dsn == G_MAXINT64 || segment->discont_sequence != dsn) {
        dsn = segment->discont_sequence;
        if (!time_map_in_list (active, dsn)) {
          GstHLSTimeMap *map = gst_hls_find_time_map (hlsdemux, dsn);
          if (map) {
            GST_DEBUG_OBJECT (demux,
                "Keeping active time map dsn:%" G_GINT64_FORMAT, map->dsn);
            /* Move active dsn to active list */
            hlsdemux->mappings = g_list_remove (hlsdemux->mappings, map);
            active = g_list_append (active, map);
          }
        }
      }
    }
  }

  g_list_free_full (hlsdemux->mappings, (GDestroyNotify) gst_hls_time_map_free);
  hlsdemux->mappings = active;
}

/* Go over the DSN from the playlist and add any missing time mapping */
static void
gst_hls_update_time_mappings (GstHLSDemux * demux,
    GstHLSMediaPlaylist * playlist)
{
  guint idx, len = playlist->segments->len;
  gint64 dsn = G_MAXINT64;

  for (idx = 0; idx < len; idx++) {
    GstM3U8MediaSegment *segment = g_ptr_array_index (playlist->segments, idx);

    if (dsn == G_MAXINT64 || segment->discont_sequence != dsn) {
      dsn = segment->discont_sequence;
      if (!gst_hls_find_time_map (demux, segment->discont_sequence))
        gst_hls_demux_add_time_mapping (demux, segment->discont_sequence,
            segment->stream_time, segment->datetime);
    }
  }
}

static void
setup_initial_playlist (GstHLSDemux * demux, GstHLSMediaPlaylist * playlist)
{
  guint idx, len = playlist->segments->len;
  GstM3U8MediaSegment *segment;
  GstClockTimeDiff pos = 0;

  GST_DEBUG_OBJECT (demux,
      "Setting up initial variant segment and time mapping");

  /* This is the initial variant playlist. We will use it to base all our timing
   * from. */

  for (idx = 0; idx < len; idx++) {
    segment = g_ptr_array_index (playlist->segments, idx);

    segment->stream_time = pos;
    pos += segment->duration;
  }
}

/* Reset hlsdemux in case of live synchronization loss (i.e. when a media
 * playlist update doesn't match at all with the previous one) */
static void
gst_hls_demux_reset_for_lost_sync (GstHLSDemux * hlsdemux)
{
  GstAdaptiveDemux *demux = (GstAdaptiveDemux *) hlsdemux;
  GList *iter;

  GST_DEBUG_OBJECT (hlsdemux, "Resetting for lost sync");

  for (iter = demux->input_period->streams; iter; iter = iter->next) {
    GstHLSDemuxStream *hls_stream = iter->data;
    GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) hls_stream;

    if (hls_stream->current_segment)
      gst_m3u8_media_segment_unref (hls_stream->current_segment);
    hls_stream->current_segment = NULL;

    if (hls_stream->is_variant) {
      GstHLSTimeMap *map;
      /* Resynchronize the variant stream */
      g_assert (stream->current_position != GST_CLOCK_STIME_NONE);
      hls_stream->current_segment =
          gst_hls_media_playlist_get_starting_segment (hls_stream->playlist);
      hls_stream->current_segment->stream_time = stream->current_position;
      gst_hls_media_playlist_recalculate_stream_time (hls_stream->playlist,
          hls_stream->current_segment);
      GST_DEBUG_OBJECT (stream,
          "Resynced variant playlist to %" GST_STIME_FORMAT,
          GST_STIME_ARGS (stream->current_position));
      map =
          gst_hls_find_time_map (hlsdemux,
          hls_stream->current_segment->discont_sequence);
      if (map)
        map->internal_time = GST_CLOCK_TIME_NONE;
      gst_hls_update_time_mappings (hlsdemux, hls_stream->playlist);
      gst_hls_media_playlist_dump (hls_stream->playlist);
    } else {
      /* Force playlist update for the rendition streams, it will resync to the
       * variant stream on the next round */
      if (hls_stream->playlist)
        gst_hls_media_playlist_unref (hls_stream->playlist);
      hls_stream->playlist = NULL;
      hls_stream->playlist_fetched = FALSE;
    }
  }
}

static GstFlowReturn
gst_hls_demux_stream_update_media_playlist (GstHLSDemux * demux,
    GstHLSDemuxStream * stream, gchar ** uri, GError ** err)
{
  GstHLSMediaPlaylist *new_playlist;

  GST_DEBUG_OBJECT (stream, "Updating %s", *uri);

  new_playlist = download_media_playlist (demux, *uri, err, stream->playlist);
  if (new_playlist == NULL) {
    GST_WARNING_OBJECT (stream, "Could not get playlist '%s'", *uri);
    return GST_FLOW_ERROR;
  }

  /* Check if a redirect happened */
  if (g_strcmp0 (*uri, new_playlist->uri)) {
    GST_DEBUG_OBJECT (stream, "Playlist URI update : '%s'  =>  '%s'", *uri,
        new_playlist->uri);
    g_free (*uri);
    *uri = g_strdup (new_playlist->uri);
  }

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
        stream->current_segment->uri);

    /* Use best-effort techniques to find the correponding current media segment
     * in the new playlist. This might be off in some cases, but it doesn't matter
     * since we will be checking the embedded timestamp later */
    new_segment =
        gst_hls_media_playlist_sync_to_segment (new_playlist,
        stream->current_segment);
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

  if (stream->playlist) {
    gst_hls_media_playlist_unref (stream->playlist);
    stream->playlist = new_playlist;
  } else {
    if (stream->is_variant) {
      GST_DEBUG_OBJECT (stream, "Setting up initial playlist");
      setup_initial_playlist (demux, new_playlist);
    }
    stream->playlist = new_playlist;
  }

  if (stream->is_variant) {
    /* Update time mappings. We only use the variant stream for collecting
     * mappings since it is the reference on which rendition stream timing will
     * be based. */
    gst_hls_update_time_mappings (demux, stream->playlist);
  }
  gst_hls_media_playlist_dump (stream->playlist);

  if (stream->current_segment) {
    GST_DEBUG_OBJECT (stream,
        "After update, current segment now sn:%" G_GINT64_FORMAT
        " stream_time:%" GST_STIME_FORMAT " uri:%s",
        stream->current_segment->sequence,
        GST_STIME_ARGS (stream->current_segment->stream_time),
        stream->current_segment->uri);
  } else {
    GST_DEBUG_OBJECT (stream, "No current segment selected");
  }

  GST_DEBUG_OBJECT (stream, "done");

  return GST_FLOW_OK;

  /* ERRORS */
lost_sync:
  {
    /* Set new playlist, lost sync handler will know what to do with it */
    if (stream->playlist)
      gst_hls_media_playlist_unref (stream->playlist);
    stream->playlist = new_playlist;

    gst_hls_demux_reset_for_lost_sync (demux);

    return GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC;
  }
}

static GstFlowReturn
gst_hls_demux_stream_update_rendition_playlist (GstHLSDemux * demux,
    GstHLSDemuxStream * stream)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstHLSRenditionStream *target_rendition =
      stream->pending_rendition ? stream->
      pending_rendition : stream->current_rendition;

  ret = gst_hls_demux_stream_update_media_playlist (demux, stream,
      &target_rendition->uri, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  if (stream->pending_rendition) {
    gst_hls_rendition_stream_unref (stream->current_rendition);
    /* Stealing ref */
    stream->current_rendition = stream->pending_rendition;
    stream->pending_rendition = NULL;
  }

  stream->playlist_fetched = TRUE;

  return ret;
}

static GstFlowReturn
gst_hls_demux_stream_update_variant_playlist (GstHLSDemux * demux,
    GstHLSDemuxStream * stream, GError ** err)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstHLSVariantStream *target_variant =
      demux->pending_variant ? demux->pending_variant : demux->current_variant;

  ret = gst_hls_demux_stream_update_media_playlist (demux, stream,
      &target_variant->uri, err);
  if (ret != GST_FLOW_OK)
    return ret;

  if (demux->pending_variant) {
    gst_hls_variant_stream_unref (demux->current_variant);
    /* Stealing ref */
    demux->current_variant = demux->pending_variant;
    demux->pending_variant = NULL;
  }

  stream->playlist_fetched = TRUE;

  return ret;
}

static GstFlowReturn
gst_hls_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstHLSDemuxStream *hlsdemux_stream = GST_HLS_DEMUX_STREAM_CAST (stream);
  GstAdaptiveDemux *demux = stream->demux;
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstM3U8MediaSegment *file;
  gboolean discont;

  /* If the rendition playlist needs to be updated, do it now */
  if (!hlsdemux_stream->is_variant && !hlsdemux_stream->playlist_fetched) {
    ret = gst_hls_demux_stream_update_rendition_playlist (hlsdemux,
        hlsdemux_stream);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  GST_DEBUG_OBJECT (stream,
      "Updating fragment information, current_position:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->current_position));

  /* Find the current segment if we don't already have it */
  if (hlsdemux_stream->current_segment == NULL) {
    GST_LOG_OBJECT (stream, "No current segment");
    if (stream->current_position == GST_CLOCK_TIME_NONE) {
      GST_DEBUG_OBJECT (stream, "Setting up initial segment");
      hlsdemux_stream->current_segment =
          gst_hls_media_playlist_get_starting_segment
          (hlsdemux_stream->playlist);
    } else {
      if (gst_hls_media_playlist_has_lost_sync (hlsdemux_stream->playlist,
              stream->current_position)) {
        GST_WARNING_OBJECT (stream, "Lost SYNC !");
        return GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC;
      }
      GST_DEBUG_OBJECT (stream,
          "Looking up segment for position %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->current_position));
      hlsdemux_stream->current_segment =
          gst_hls_media_playlist_seek (hlsdemux_stream->playlist, TRUE,
          GST_SEEK_FLAG_SNAP_NEAREST, stream->current_position);

      if (hlsdemux_stream->current_segment == NULL) {
        GST_INFO_OBJECT (stream, "At the end of the current media playlist");
        return GST_FLOW_EOS;
      }

      /* Update time mapping. If it already exists it will be ignored */
      gst_hls_demux_add_time_mapping (hlsdemux,
          hlsdemux_stream->current_segment->discont_sequence,
          hlsdemux_stream->current_segment->stream_time,
          hlsdemux_stream->current_segment->datetime);
    }
  }

  file = hlsdemux_stream->current_segment;

  GST_DEBUG_OBJECT (stream, "Current segment stream_time %" GST_STIME_FORMAT,
      GST_STIME_ARGS (file->stream_time));

  discont = file->discont || stream->discont;

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
  }

  /* set up our source for download */
  if (hlsdemux_stream->reset_pts || discont || demux->segment.rate < 0.0) {
    stream->fragment.stream_time = file->stream_time;
  } else {
    stream->fragment.stream_time = GST_CLOCK_STIME_NONE;
  }

  g_free (hlsdemux_stream->current_key);
  hlsdemux_stream->current_key = g_strdup (file->key);
  g_free (hlsdemux_stream->current_iv);
  hlsdemux_stream->current_iv = g_memdup2 (file->iv, sizeof (file->iv));

  g_free (stream->fragment.uri);
  stream->fragment.uri = g_strdup (file->uri);

  GST_DEBUG_OBJECT (stream, "Stream URI now %s", file->uri);

  stream->fragment.range_start = file->offset;
  if (file->size != -1)
    stream->fragment.range_end = file->offset + file->size - 1;
  else
    stream->fragment.range_end = -1;

  stream->fragment.duration = file->duration;

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

/* Returns TRUE if the rendition stream switched group-id */
static gboolean
gst_hls_demux_update_rendition_stream (GstHLSDemux * hlsdemux,
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
  hls_stream->playlist_fetched = FALSE;
  if (hls_stream->pending_rendition) {
    GST_ERROR_OBJECT (hlsdemux,
        "Already had a pending rendition switch to '%s'",
        hls_stream->pending_rendition->name);
    gst_hls_rendition_stream_unref (hls_stream->pending_rendition);
  }
  hls_stream->pending_rendition =
      gst_hls_rendition_stream_ref (replacement_media);
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

  if (hls_stream->is_variant) {
    gdouble play_rate = gst_adaptive_demux_play_rate (demux);
    gboolean changed = FALSE;

    /* Handle variant streams */
    GST_DEBUG_OBJECT (hlsdemux,
        "Checking playlist change for main variant stream");
    gst_hls_demux_change_playlist (hlsdemux, bitrate / MAX (1.0,
            ABS (play_rate)), &changed);

    GST_DEBUG_OBJECT (hlsdemux, "Returning changed: %d", changed);
    return changed;
  }

  /* Handle rendition streams */
  return gst_hls_demux_update_rendition_stream (hlsdemux, hls_stream, NULL);
}

static void
gst_hls_demux_reset (GstAdaptiveDemux * ademux)
{
  GstHLSDemux *demux = GST_HLS_DEMUX_CAST (ademux);

  GST_DEBUG_OBJECT (demux, "resetting");

  if (ademux->input_period) {
    GList *walk;
    for (walk = ademux->input_period->streams; walk != NULL; walk = walk->next) {
      GstHLSDemuxStream *hls_stream = GST_HLS_DEMUX_STREAM_CAST (walk->data);
      hls_stream->pdt_tag_sent = FALSE;
    }
  }

  if (demux->master) {
    gst_hls_master_playlist_unref (demux->master);
    demux->master = NULL;
  }
  if (demux->current_variant != NULL) {
    gst_hls_variant_stream_unref (demux->current_variant);
    demux->current_variant = NULL;
  }
  if (demux->pending_variant != NULL) {
    gst_hls_variant_stream_unref (demux->pending_variant);
    demux->pending_variant = NULL;
  }

  g_list_free_full (demux->mappings, (GDestroyNotify) gst_hls_time_map_free);
  demux->mappings = NULL;

  gst_hls_demux_clear_all_pending_data (demux);
}

/*
 * update: TRUE only when requested from parent class (via
 * ::demux_update_manifest() or ::change_playlist() ).
 */
static GstFlowReturn
gst_hls_demux_update_playlist (GstHLSDemux * demux, gboolean update,
    GError ** err)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX (demux);

  GST_DEBUG_OBJECT (demux, "update:%d", update);

  /* Download and update the appropriate variant playlist (pending if any, else
   * current) */
  ret = gst_hls_demux_stream_update_variant_playlist (demux, demux->main_stream,
      err);
  if (ret != GST_FLOW_OK)
    return ret;

  if (update && gst_hls_demux_is_live (adaptive_demux)) {
    GList *tmp;
    GST_DEBUG_OBJECT (demux,
        "LIVE, Marking rendition streams to be updated next");
    /* We're live, instruct all rendition medias to be updated next */
    for (tmp = adaptive_demux->input_period->streams; tmp; tmp = tmp->next) {
      GstHLSDemuxStream *hls_stream = tmp->data;
      if (!hls_stream->is_variant)
        hls_stream->playlist_fetched = FALSE;
    }
  }

  return GST_FLOW_OK;
}

static gboolean
gst_hls_demux_change_playlist (GstHLSDemux * demux, guint max_bitrate,
    gboolean * changed)
{
  GstHLSVariantStream *lowest_variant, *lowest_ivariant;
  GstHLSVariantStream *previous_variant, *new_variant;
  gint old_bandwidth, new_bandwidth;
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX_CAST (demux);
  GstAdaptiveDemux2Stream *stream;

  g_return_val_if_fail (demux->main_stream != NULL, FALSE);
  stream = (GstAdaptiveDemux2Stream *) demux->main_stream;

  /* Make sure we keep a reference in case we need to switch back */
  previous_variant = gst_hls_variant_stream_ref (demux->current_variant);
  new_variant =
      gst_hls_master_playlist_get_variant_for_bitrate (demux->master,
      demux->current_variant, max_bitrate, adaptive_demux->min_bitrate);

retry_failover_protection:
  old_bandwidth = previous_variant->bandwidth;
  new_bandwidth = new_variant->bandwidth;

  /* Don't do anything else if the playlist is the same */
  if (new_bandwidth == old_bandwidth) {
    gst_hls_variant_stream_unref (previous_variant);
    return TRUE;
  }

  gst_hls_demux_set_current_variant (demux, new_variant);

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", old_bandwidth, max_bitrate, new_bandwidth);

  if (gst_hls_demux_update_playlist (demux, TRUE, NULL) == GST_FLOW_OK) {
    const gchar *main_uri;
    gchar *uri = new_variant->uri;

    main_uri = gst_adaptive_demux_get_manifest_ref_uri (adaptive_demux);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT_CAST (demux),
            gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                "manifest-uri", G_TYPE_STRING,
                main_uri, "uri", G_TYPE_STRING,
                uri, "bitrate", G_TYPE_INT, new_bandwidth, NULL)));
    if (changed)
      *changed = TRUE;
    stream->discont = TRUE;
  } else if (gst_adaptive_demux2_is_running (GST_ADAPTIVE_DEMUX_CAST (demux))) {
    GstHLSVariantStream *failover_variant = NULL;
    GList *failover;

    GST_INFO_OBJECT (demux, "Unable to update playlist. Switching back");

    /* we find variants by bitrate by going from highest to lowest, so it's
     * possible that there's another variant with the same bitrate before the
     * one selected which we can use as failover */
    failover = g_list_find (demux->master->variants, new_variant);
    if (failover != NULL)
      failover = failover->prev;
    if (failover != NULL)
      failover_variant = failover->data;
    if (failover_variant && new_bandwidth == failover_variant->bandwidth) {
      new_variant = failover_variant;
      goto retry_failover_protection;
    }

    gst_hls_demux_set_current_variant (demux, previous_variant);

    /*  Try a lower bitrate (or stop if we just tried the lowest) */
    if (previous_variant->iframe) {
      lowest_ivariant = demux->master->iframe_variants->data;
      if (new_bandwidth == lowest_ivariant->bandwidth) {
        gst_hls_variant_stream_unref (previous_variant);
        return FALSE;
      }
    } else {
      lowest_variant = demux->master->variants->data;
      if (new_bandwidth == lowest_variant->bandwidth) {
        gst_hls_variant_stream_unref (previous_variant);
        return FALSE;
      }
    }
    gst_hls_variant_stream_unref (previous_variant);
    return gst_hls_demux_change_playlist (demux, new_bandwidth - 1, changed);
  }

  gst_hls_variant_stream_unref (previous_variant);
  return TRUE;
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

static gint64
gst_hls_demux_get_manifest_update_interval (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime target_duration = 5 * GST_SECOND;

  if (hlsdemux->main_stream && hlsdemux->main_stream->playlist) {
    GstHLSMediaPlaylist *playlist = hlsdemux->main_stream->playlist;

    if (playlist->version > 5) {
      target_duration = hlsdemux->main_stream->playlist->targetduration;
    } else if (playlist->segments->len) {
      GstM3U8MediaSegment *last_seg =
          g_ptr_array_index (playlist->segments, playlist->segments->len - 1);
      target_duration = last_seg->duration;
    }
    if (playlist->reloaded && target_duration > (playlist->targetduration / 2)) {
      GST_DEBUG_OBJECT (demux,
          "Playlist didn't change previously, returning lower update interval");
      target_duration /= 2;
    }
  }

  GST_DEBUG_OBJECT (demux, "Returning update interval of %" GST_TIME_FORMAT,
      GST_TIME_ARGS (target_duration));

  return gst_util_uint64_scale (target_duration, G_USEC_PER_SEC, GST_SECOND);
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

static gboolean
gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean ret = FALSE;

  if (hlsdemux->main_stream && hlsdemux->main_stream->playlist)
    ret =
        gst_hls_media_playlist_get_seek_range (hlsdemux->main_stream->playlist,
        start, stop);

  return ret;
}

static gboolean
hlsdemux2_element_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_hls_demux2_debug, "hlsdemux2", 0,
      "hlsdemux2 element");

  if (!adaptivedemux2_base_element_init (plugin))
    return TRUE;

  ret = gst_element_register (plugin, "hlsdemux2",
      GST_RANK_PRIMARY + 1, GST_TYPE_HLS_DEMUX2);

  return ret;
}
