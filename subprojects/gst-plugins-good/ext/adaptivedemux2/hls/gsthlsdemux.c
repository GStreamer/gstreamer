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
#include <glib/gi18n-lib.h>

/* FIXME: Only needed for scheduler-unlock/lock hack */
#include <gstadaptivedemux-private.h>

#include "gsthlselements.h"
#include "gstadaptivedemuxelements.h"
#include "gsthlsdemux.h"
#include "gsthlsdemux-stream.h"

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

/* GObject */
static void gst_hls_demux_finalize (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

/* GstHLSDemux */
static GstFlowReturn
gst_hls_demux_check_variant_playlist_loaded (GstHLSDemux * demux);

static gboolean gst_hls_demux_is_live (GstAdaptiveDemux * demux);
static GstClockTime gst_hls_demux_get_duration (GstAdaptiveDemux * demux);
static gboolean gst_hls_demux_process_initial_manifest (GstAdaptiveDemux *
    demux, GstBuffer * buf);
static GstFlowReturn gst_hls_demux_update_manifest (GstAdaptiveDemux * demux);

static void gst_hls_prune_time_mappings (GstHLSDemux * demux);

static gboolean gst_hls_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek);

static gboolean hlsdemux2_element_init (GstPlugin * plugin);

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (hlsdemux2, hlsdemux2_element_init);

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

static gboolean
hlsdemux_requires_periodical_playlist_update_default (GstAdaptiveDemux *
    demux G_GNUC_UNUSED)
{
  /* We don't need the base class to update our manifest periodically, the
   * playlist loader for the main stream will do that and trigger
   * an update manual */
  return FALSE;
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
  adaptivedemux_class->requires_periodical_playlist_update =
      hlsdemux_requires_periodical_playlist_update_default;
  adaptivedemux_class->process_manifest =
      gst_hls_demux_process_initial_manifest;
  adaptivedemux_class->reset = gst_hls_demux_reset;
  adaptivedemux_class->update_manifest = gst_hls_demux_update_manifest;
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
   * No, there isn't a single output :D.
   * Until the download helper can do estimates,
   * use the main variant, or a video stream if the
   * main variant stream is not loading */

  /* Valid because hlsdemux only has a single output */
  if (demux->input_period->streams) {
    GstAdaptiveDemux2Stream *stream = demux->input_period->streams->data;
    return stream->current_download_rate;
  }

  return 0;
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

/* Wait until the current variant playlist finishes loading, only
 * for use when called from an external thread - seeking or initial
 * manifest. From the scheduler task it will just hang */
static GstFlowReturn
gst_hls_demux_wait_for_variant_playlist (GstHLSDemux * hlsdemux)
{
  GstFlowReturn flow_ret;

  while ((flow_ret = gst_hls_demux_check_variant_playlist_loaded (hlsdemux)
          == GST_ADAPTIVE_DEMUX_FLOW_BUSY)) {
    if (!gst_adaptive_demux2_stream_wait_prepared (GST_ADAPTIVE_DEMUX2_STREAM
            (hlsdemux->main_stream))) {
      GST_DEBUG_OBJECT (hlsdemux,
          "Interrupted waiting for stream to be prepared");
      return GST_FLOW_FLUSHING;
    }
  }

  return flow_ret;
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

    /* Switch to I-frame variant */
    if (!gst_hls_demux_change_variant_playlist (hlsdemux, TRUE,
            bitrate / ABS (rate), NULL))
      return FALSE;

  } else if (rate > -1.0 && rate <= 1.0 && (old_rate < -1.0 || old_rate > 1.0)) {
    /* Switch to normal variant */
    if (!gst_hls_demux_change_variant_playlist (hlsdemux, FALSE, bitrate, NULL))
      return FALSE;
  }

  /* Of course the playlist isn't loaded as soon as we ask - we need to wait */
  GstFlowReturn flow_ret = gst_hls_demux_wait_for_variant_playlist (hlsdemux);
  if (flow_ret == GST_FLOW_FLUSHING)
    return FALSE;
  if (flow_ret != GST_FLOW_OK) {
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        (_("Internal data stream error.")), ("Could not switch playlist"));
    return FALSE;
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

    GstFlowReturn flow_ret;

    while ((flow_ret =
            gst_hls_demux_stream_seek (stream, rate >= 0, flags, target_pos,
                &current_pos) == GST_ADAPTIVE_DEMUX_FLOW_BUSY)) {
      if (!gst_adaptive_demux2_stream_wait_prepared (GST_ADAPTIVE_DEMUX2_STREAM
              (stream))) {
        GST_DEBUG_OBJECT (hlsdemux,
            "Interrupted waiting for stream to be prepared for seek");
        return FALSE;
      }
    }

    if (flow_ret != GST_FLOW_OK) {
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

static GstAdaptiveDemux2Stream *
create_common_hls_stream (GstHLSDemux * demux, const gchar * name)
{
  GstAdaptiveDemux2Stream *stream;

  stream = g_object_new (GST_TYPE_HLS_DEMUX_STREAM, "name", name, NULL);

  gst_adaptive_demux2_add_stream ((GstAdaptiveDemux *) demux, stream);

  return stream;
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

  /* Due to HLS manifest information being so unreliable/inconsistent, we will
   * create the actual tracks once we have information about the streams present
   * in the variant data stream */
  stream->pending_tracks = TRUE;

  gst_hls_demux_stream_set_playlist_uri (hlsdemux_stream,
      demux->current_variant->uri);
  gst_hls_demux_stream_start_playlist_loading (hlsdemux_stream);
}

GstAdaptiveDemuxTrack *
gst_hls_demux_new_track_for_rendition (GstHLSDemux * demux,
    GstHLSRenditionStream * rendition,
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
  track = gst_hls_demux_new_track_for_rendition (demux, media, NULL, 0, NULL);

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
      gst_hls_demux_stream_set_playlist_uri (media_stream, media->uri);
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
      if (hlsdemux->pending_variant != variant) {
        GST_DEBUG_OBJECT (hlsdemux, "Already waiting for pending variant '%s'",
            hlsdemux->pending_variant->name);
      }
      gst_hls_variant_stream_unref (hlsdemux->pending_variant);
    }
    hlsdemux->pending_variant = gst_hls_variant_stream_ref (variant);
  } else {
    GST_DEBUG_OBJECT (hlsdemux, "Setting variant '%s'", variant->name);
    hlsdemux->current_variant = gst_hls_variant_stream_ref (variant);
  }

  if (hlsdemux->main_stream) {
    /* The variant stream exists, update the playlist we're loading */
    gst_hls_demux_stream_set_playlist_uri (hlsdemux->main_stream, variant->uri);
  }
}

/* Called to process the initial multi-variant (or simple playlist)
 * received on the element's sinkpad */
static gboolean
gst_hls_demux_process_initial_manifest (GstAdaptiveDemux * demux,
    GstBuffer * buf)
{
  GstHLSVariantStream *variant;
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gchar *playlist = NULL;
  guint start_bitrate = hlsdemux->start_bitrate;
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
        gst_hls_media_playlist_parse (playlist, GST_CLOCK_TIME_NONE,
        gst_adaptive_demux_get_manifest_ref_uri (demux), NULL);
  }

  if (start_bitrate == 0)
    start_bitrate = demux->connection_speed;

  /* select the initial variant stream */
  if (start_bitrate > 0) {
    variant =
        gst_hls_master_playlist_get_variant_for_bitrate (hlsdemux->master,
        FALSE, start_bitrate, demux->min_bitrate, hlsdemux->failed_variants);
  } else {
    variant = hlsdemux->master->default_variant;
  }

  if (variant == NULL) {
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("Could not find an initial variant to play"));
  }

  GST_INFO_OBJECT (hlsdemux,
      "Manifest processed, initial variant selected : `%s`", variant->name);
  gst_hls_demux_set_current_variant (hlsdemux, variant);

  GST_DEBUG_OBJECT (hlsdemux, "Manifest handled, now setting up streams");

  ret = gst_hls_demux_setup_streams (demux);
  if (!ret)
    return FALSE;

  if (simple_media_playlist) {
    GstM3U8SeekResult seek_result;
    GstM3U8MediaSegment *segment;

    hlsdemux->main_stream->playlist = simple_media_playlist;
    /* This is the initial variant playlist. We will use it to base all our timing
     * from. */
    segment = g_ptr_array_index (simple_media_playlist->segments, 0);
    if (segment) {
      segment->stream_time = 0;
      gst_hls_media_playlist_recalculate_stream_time (simple_media_playlist,
          segment);
    }

    if (!gst_hls_media_playlist_get_starting_segment (simple_media_playlist,
            &seek_result)) {
      GST_DEBUG_OBJECT (hlsdemux->main_stream,
          "Failed to find a segment to start at");
      return FALSE;
    }
    hlsdemux->main_stream->current_segment = seek_result.segment;
    hlsdemux->main_stream->in_partial_segments =
        seek_result.found_partial_segment;
    hlsdemux->main_stream->part_idx = seek_result.part_idx;

    gst_hls_demux_handle_variant_playlist_update (hlsdemux,
        simple_media_playlist->uri, simple_media_playlist);
  }

  /* If this is a multi-variant playlist, wait for the initial variant playlist to load */
  if (!hlsdemux->master->is_simple) {
    GstFlowReturn flow_ret = gst_hls_demux_wait_for_variant_playlist (hlsdemux);
    if (flow_ret == GST_FLOW_FLUSHING)
      return FALSE;
    if (flow_ret != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (demux, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("Could not fetch media playlist"));
      return FALSE;
    }
  }

  /* Make sure the external manifest copy of the main playlist
   * is available to the baseclass at the start */
  gst_hls_demux_update_manifest (demux);

  return TRUE;
}

static GstClockTime
gst_hls_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  if (hlsdemux->main_playlist)
    duration = gst_hls_media_playlist_get_duration (hlsdemux->main_playlist);

  return duration;
}

/* Called from base class with the MANIFEST_LOCK held */
static gboolean
gst_hls_demux_is_live (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean is_live = FALSE;

  if (hlsdemux->main_playlist)
    is_live = gst_hls_media_playlist_is_live (hlsdemux->main_playlist);

  return is_live;
}

const GstHLSKey *
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

void
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
gst_hls_demux_find_time_map (GstHLSDemux * demux, gint64 dsn)
{
  return time_map_in_list (demux->mappings, dsn);
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

void
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
          GstHLSTimeMap *map = gst_hls_demux_find_time_map (hlsdemux, dsn);
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
void
gst_hls_update_time_mappings (GstHLSDemux * demux,
    GstHLSMediaPlaylist * playlist)
{
  guint idx, len = playlist->segments->len;
  gint64 dsn = G_MAXINT64;

  for (idx = 0; idx < len; idx++) {
    GstM3U8MediaSegment *segment = g_ptr_array_index (playlist->segments, idx);

    if (dsn == G_MAXINT64 || segment->discont_sequence != dsn) {
      dsn = segment->discont_sequence;
      if (!gst_hls_demux_find_time_map (demux, segment->discont_sequence))
        gst_hls_demux_add_time_mapping (demux, segment->discont_sequence,
            segment->stream_time, segment->datetime);
    }
  }
}

/* Called by the base class with the manifest lock held */
static GstFlowReturn
gst_hls_demux_update_manifest (GstAdaptiveDemux * demux)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);

  /* Take a copy of the main variant playlist for base class
   * calls that need access from outside the scheduler task,
   * holding the MANIFEST_LOCK */
  if (hlsdemux->main_stream && hlsdemux->main_stream->playlist) {
    if (hlsdemux->main_playlist)
      gst_hls_media_playlist_unref (hlsdemux->main_playlist);
    hlsdemux->main_playlist =
        gst_hls_media_playlist_ref (hlsdemux->main_stream->playlist);
    return GST_FLOW_OK;
  }

  return GST_ADAPTIVE_DEMUX_FLOW_BUSY;
}

void
gst_hls_demux_handle_variant_playlist_update (GstHLSDemux * demux,
    const gchar * playlist_uri, GstHLSMediaPlaylist * playlist)
{
  if (demux->main_stream == NULL || !demux->main_stream->playlist_fetched) {
    GstM3U8MediaSegment *segment;

    GST_DEBUG_OBJECT (demux,
        "Setting up initial variant segment and time mapping");

    /* This is the initial variant playlist. We will use it to base all our timing
     * from. */
    segment = g_ptr_array_index (playlist->segments, 0);
    if (segment) {
      segment->stream_time = 0;
      gst_hls_media_playlist_recalculate_stream_time (playlist, segment);
    }
  }

  if (demux->pending_variant) {
    /* The pending variant must always match the one that just got updated:
     * The loader should only do a callback for the most recently set URI */
    g_assert (g_str_equal (demux->pending_variant->uri, playlist_uri));

    gboolean changed = (demux->pending_variant != demux->current_variant);

    gst_hls_variant_stream_unref (demux->current_variant);
    /* Stealing ref */
    demux->current_variant = demux->pending_variant;
    demux->pending_variant = NULL;

    if (changed) {
      GstAdaptiveDemux *basedemux = GST_ADAPTIVE_DEMUX (demux);
      const gchar *main_uri =
          gst_adaptive_demux_get_manifest_ref_uri (basedemux);
      gchar *uri = demux->current_variant->uri;
      gint new_bandwidth = demux->current_variant->bandwidth;

      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_element (GST_OBJECT_CAST (demux),
              gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
                  "manifest-uri", G_TYPE_STRING,
                  main_uri, "uri", G_TYPE_STRING,
                  uri, "bitrate", G_TYPE_INT, new_bandwidth, NULL)));

      /* Mark discont on the next packet after switching variant */
      GST_ADAPTIVE_DEMUX2_STREAM (demux->main_stream)->discont = TRUE;
    }
  }

  /* Update time mappings. We only use the variant stream for collecting
   * mappings since it is the reference on which rendition stream timing will
   * be based. */
  gst_hls_update_time_mappings (demux, playlist);
  gst_hls_media_playlist_dump (playlist);

  /* Get the base class to call the update_manifest() vfunc with the MANIFEST_LOCK()
   * held */
  gst_adaptive_demux2_manual_manifest_update (GST_ADAPTIVE_DEMUX (demux));
}

void
gst_hls_demux_handle_variant_playlist_update_error (GstHLSDemux * demux,
    const gchar * playlist_uri)
{
  GST_DEBUG_OBJECT (demux, "Playlist update failure for variant URI %s",
      playlist_uri);

  /* Check if this is a new load of the pending variant, or a reload
   * of the current variant */
  GstHLSVariantStream *variant = demux->pending_variant;
  if (variant == NULL)
    variant = demux->current_variant;

  /* If the variant has an fallback URIs available, we can try one of those */
  if (variant->fallback != NULL) {
    gchar *fallback_uri = (gchar *) (variant->fallback->data);

    GST_DEBUG_OBJECT (demux,
        "Variant playlist update failed. Switching to fallback URI %s",
        fallback_uri);

    variant->fallback = g_list_remove (variant->fallback, fallback_uri);
    g_free (variant->uri);
    variant->uri = fallback_uri;

    if (demux->main_stream) {
      /* The variant stream exists, update the playlist we're loading */
      gst_hls_demux_stream_set_playlist_uri (demux->main_stream, variant->uri);
    }
    return;
  }

  GST_DEBUG_OBJECT (demux, "Variant playlist update failed. "
      "Marking variant URL %s as failed and switching over to another variant",
      playlist_uri);

  /* The variant must always match the one that just got updated:
   * The loader should only do a callback for the most recently set URI */
  g_assert (g_str_equal (variant->uri, playlist_uri));

  /* If we didn't already add this playlist to the failed variants list
   * do so now. It's possible we get an update error again if we failed
   * to choose a new variant and posted error but didn't get shut down
   * yet */
  if (g_list_find (demux->failed_variants, variant) == NULL) {
    demux->failed_variants =
        g_list_prepend (demux->failed_variants,
        gst_hls_variant_stream_ref (variant));
  }

  /* Now try to find another variant to play */
  gdouble play_rate = gst_adaptive_demux_play_rate (GST_ADAPTIVE_DEMUX (demux));
  guint64 bitrate = gst_hls_demux_get_bitrate (demux);

  GST_DEBUG_OBJECT (demux, "Trying to find failover variant playlist");

  if (!gst_hls_demux_change_variant_playlist (demux,
          variant->iframe, bitrate / MAX (1.0, ABS (play_rate)), NULL)) {
    GST_ERROR_OBJECT (demux, "Failed to choose a new variant to play");
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("Could not update any variant playlist"));
  }
}

/* Reset hlsdemux in case of live synchronization loss (i.e. when a media
 * playlist update doesn't match at all with the previous one) */
void
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
      GstM3U8SeekResult seek_result;

      /* Resynchronize the variant stream */
      g_assert (stream->current_position != GST_CLOCK_STIME_NONE);
      if (gst_hls_media_playlist_get_starting_segment (hls_stream->playlist,
              &seek_result)) {
        hls_stream->current_segment = seek_result.segment;
        hls_stream->in_partial_segments = seek_result.found_partial_segment;
        hls_stream->part_idx = seek_result.part_idx;

        hls_stream->current_segment->stream_time = stream->current_position;
        gst_hls_media_playlist_recalculate_stream_time (hls_stream->playlist,
            hls_stream->current_segment);
        GST_DEBUG_OBJECT (stream,
            "Resynced variant playlist to %" GST_STIME_FORMAT,
            GST_STIME_ARGS (stream->current_position));
        map =
            gst_hls_demux_find_time_map (hlsdemux,
            hls_stream->current_segment->discont_sequence);
        if (map)
          map->internal_time = GST_CLOCK_TIME_NONE;
        gst_hls_update_time_mappings (hlsdemux, hls_stream->playlist);
        gst_hls_media_playlist_dump (hls_stream->playlist);
      } else {
        GST_ERROR_OBJECT (stream, "Failed to locate a segment to restart at!");
      }
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
  if (demux->main_playlist) {
    gst_hls_media_playlist_unref (demux->main_playlist);
    demux->main_playlist = NULL;
  }
  if (demux->current_variant != NULL) {
    gst_hls_variant_stream_unref (demux->current_variant);
    demux->current_variant = NULL;
  }
  if (demux->pending_variant != NULL) {
    gst_hls_variant_stream_unref (demux->pending_variant);
    demux->pending_variant = NULL;
  }
  if (demux->failed_variants != NULL) {
    g_list_free_full (demux->failed_variants,
        (GDestroyNotify) gst_hls_variant_stream_unref);
    demux->failed_variants = NULL;
  }

  g_list_free_full (demux->mappings, (GDestroyNotify) gst_hls_time_map_free);
  demux->mappings = NULL;

  gst_hls_demux_clear_all_pending_data (demux);
}

static GstFlowReturn
gst_hls_demux_check_variant_playlist_loaded (GstHLSDemux * demux)
{
  GstHLSVariantStream *target_variant =
      demux->pending_variant ? demux->pending_variant : demux->current_variant;
  GstHLSDemuxStream *stream = demux->main_stream;

  return gst_hls_demux_stream_check_current_playlist_uri (stream,
      target_variant->uri);
}

gboolean
gst_hls_demux_change_variant_playlist (GstHLSDemux * demux,
    gboolean iframe_variant, guint max_bitrate, gboolean * changed)
{
  GstAdaptiveDemux *adaptive_demux = GST_ADAPTIVE_DEMUX_CAST (demux);

  if (changed)
    *changed = FALSE;

  /* Make sure we keep a reference for the debug output below */
  GstHLSVariantStream *new_variant =
      gst_hls_master_playlist_get_variant_for_bitrate (demux->master,
      iframe_variant, max_bitrate, adaptive_demux->min_bitrate,
      demux->failed_variants);

  /* We're out of available variants to use */
  if (new_variant == NULL) {
    return FALSE;
  }

  GstHLSVariantStream *previous_variant =
      gst_hls_variant_stream_ref (demux->current_variant);

  /* Don't do anything else if the playlist is the same */
  if (new_variant == previous_variant) {
    GST_TRACE_OBJECT (demux, "Variant didn't change from bandwidth %dbps",
        new_variant->bandwidth);
    gst_hls_variant_stream_unref (previous_variant);
    return TRUE;
  }

  gst_hls_demux_set_current_variant (demux, new_variant);

  gint new_bandwidth = new_variant->bandwidth;

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", previous_variant->bandwidth, max_bitrate,
      new_bandwidth);

  gst_hls_variant_stream_unref (previous_variant);
  if (changed)
    *changed = TRUE;
  return TRUE;
}

static gboolean
gst_hls_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstHLSDemux *hlsdemux = GST_HLS_DEMUX_CAST (demux);
  gboolean ret = FALSE;

  if (hlsdemux->main_playlist) {
    ret =
        gst_hls_media_playlist_get_seek_range (hlsdemux->main_playlist, start,
        stop);
  }

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
