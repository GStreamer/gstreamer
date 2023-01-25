/* GStreamer
 * Copyright (C) 2012 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
 *
 * gstmssdemux.c:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-mssdemux
 * @title: mssdemux
 *
 * Demuxes a Microsoft's Smooth Streaming manifest into its audio and/or video streams.
 *
 */

/*
 * == Internals
 *
 * = Smooth streaming in a few lines
 * A SS stream is defined by a xml manifest file. This file has a list of
 * tracks (StreamIndex), each one can have multiple QualityLevels, that define
 * different encoding/bitrates. When playing a track, only one of those
 * QualityLevels can be active at a time (per stream).
 *
 * The StreamIndex defines a URL with {time} and {bitrate} tags that are
 * replaced by values indicated by the fragment start times and the selected
 * QualityLevel, that generates the fragments URLs.
 *
 * Another relevant detail is that the Isomedia fragments for smoothstreaming
 * won't contains a 'moov' atom, nor a 'stsd', so there is no information
 * about the media type/configuration on the fragments, it must be extracted
 * from the Manifest and passed downstream. mssdemux does this via GstCaps.
 *
 * = How mssdemux works
 * There is a gstmssmanifest.c utility that holds the manifest and parses
 * and has functions to extract information from it. mssdemux received the
 * manifest from its sink pad and starts processing it when it gets EOS.
 *
 * The Manifest is parsed and the streams are exposed, 1 pad for each, with
 * a initially selected QualityLevel. Each stream starts its own GstTaks that
 * is responsible for downloading fragments and pushing them downstream.
 *
 * When a new connection-speed is set, mssdemux evaluates the available
 * QualityLevels and might decide to switch to another one. In this case it
 * pushes a new GstCaps event indicating the new caps on the pads.
 *
 * All operations that intend to update the GstTasks state should be protected
 * with the GST_OBJECT_LOCK.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gstadaptivedemuxelements.h"
#include "gstmssdemux.h"

GST_DEBUG_CATEGORY (mssdemux2_debug);
#define GST_CAT_DEFAULT mssdemux2_debug

static GstStaticPadTemplate gst_mss_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.ms-sstr+xml")
    );

static GstStaticPadTemplate gst_mss_demux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_mss_demux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

typedef struct _GstMssDemux2 GstMssDemux2;
typedef struct _GstMssDemux2Class GstMssDemux2Class;

#define gst_mss_demux2_parent_class parent_class
G_DEFINE_TYPE (GstMssDemux2, gst_mss_demux2, GST_TYPE_ADAPTIVE_DEMUX);

#define gst_mss_demux_stream_parent_class stream_parent_class
G_DEFINE_TYPE (GstMssDemuxStream, gst_mss_demux_stream,
    GST_TYPE_ADAPTIVE_DEMUX2_STREAM);

static gboolean mssdemux_element_init (GstPlugin * plugin);

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (mssdemux2, mssdemux_element_init);

static void gst_mss_demux_dispose (GObject * object);

static gboolean gst_mss_demux_is_live (GstAdaptiveDemux * demux);
static gboolean gst_mss_demux_process_manifest (GstAdaptiveDemux * demux,
    GstBuffer * buffer);
static GstClockTime gst_mss_demux_get_duration (GstAdaptiveDemux * demux);
static void gst_mss_demux_reset (GstAdaptiveDemux * demux);
static GstFlowReturn gst_mss_demux_stream_seek (GstAdaptiveDemux2Stream *
    stream, gboolean forward, GstSeekFlags flags, GstClockTimeDiff ts,
    GstClockTimeDiff * final_ts);
static gboolean gst_mss_demux_stream_has_next_fragment (GstAdaptiveDemux2Stream
    * stream);
static GstFlowReturn
gst_mss_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream);
static gboolean gst_mss_demux_stream_select_bitrate (GstAdaptiveDemux2Stream *
    stream, guint64 bitrate);
static GstFlowReturn
gst_mss_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream);
static gboolean gst_mss_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek);
static gint64
gst_mss_demux_get_manifest_update_interval (GstAdaptiveDemux * demux);
static GstClockTime
gst_mss_demux_stream_get_fragment_waiting_time (GstAdaptiveDemux2Stream *
    stream);
static GstFlowReturn
gst_mss_demux_update_manifest_data (GstAdaptiveDemux * demux,
    GstBuffer * buffer);
static gboolean gst_mss_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * start, gint64 * stop);
static GstFlowReturn gst_mss_demux_stream_data_received (GstAdaptiveDemux2Stream
    * stream, GstBuffer * buffer);
static gboolean
gst_mss_demux_requires_periodical_playlist_update (GstAdaptiveDemux * demux);
GstStreamType gst_stream_type_from_mss_type (GstMssStreamType mtype);

static void
gst_mss_demux_stream_class_init (GstMssDemuxStreamClass * klass)
{
  GstAdaptiveDemux2StreamClass *adaptivedemux2stream_class =
      GST_ADAPTIVE_DEMUX2_STREAM_CLASS (klass);

  adaptivedemux2stream_class->stream_seek = gst_mss_demux_stream_seek;

  adaptivedemux2stream_class->get_fragment_waiting_time =
      gst_mss_demux_stream_get_fragment_waiting_time;
  adaptivedemux2stream_class->advance_fragment =
      gst_mss_demux_stream_advance_fragment;
  adaptivedemux2stream_class->has_next_fragment =
      gst_mss_demux_stream_has_next_fragment;
  adaptivedemux2stream_class->select_bitrate =
      gst_mss_demux_stream_select_bitrate;
  adaptivedemux2stream_class->update_fragment_info =
      gst_mss_demux_stream_update_fragment_info;

  adaptivedemux2stream_class->data_received =
      gst_mss_demux_stream_data_received;
}

static void
gst_mss_demux_stream_init (GstMssDemuxStream * stream)
{
}

static void
gst_mss_demux2_class_init (GstMssDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAdaptiveDemuxClass *gstadaptivedemux_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstadaptivedemux_class = (GstAdaptiveDemuxClass *) klass;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_mss_demux_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_mss_demux_videosrc_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_mss_demux_audiosrc_template);
  gst_element_class_set_static_metadata (gstelement_class,
      "Smooth Streaming demuxer (v2)", "Codec/Demuxer/Adaptive",
      "Parse and demultiplex a Smooth Streaming manifest into audio and video "
      "streams", "Thiago Santos <thiago.sousa.santos@collabora.com>");

  gobject_class->dispose = gst_mss_demux_dispose;

  gstadaptivedemux_class->process_manifest = gst_mss_demux_process_manifest;
  gstadaptivedemux_class->is_live = gst_mss_demux_is_live;
  gstadaptivedemux_class->get_duration = gst_mss_demux_get_duration;
  gstadaptivedemux_class->get_manifest_update_interval =
      gst_mss_demux_get_manifest_update_interval;
  gstadaptivedemux_class->reset = gst_mss_demux_reset;
  gstadaptivedemux_class->seek = gst_mss_demux_seek;

  gstadaptivedemux_class->update_manifest_data =
      gst_mss_demux_update_manifest_data;

  gstadaptivedemux_class->get_live_seek_range =
      gst_mss_demux_get_live_seek_range;
  gstadaptivedemux_class->requires_periodical_playlist_update =
      gst_mss_demux_requires_periodical_playlist_update;

}

static void
gst_mss_demux2_init (GstMssDemux * mssdemux)
{
}

static void
gst_mss_demux_reset (GstAdaptiveDemux * demux)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  if (mssdemux->manifest) {
    gst_mss_manifest_free (mssdemux->manifest);
    mssdemux->manifest = NULL;
  }
  g_free (mssdemux->base_url);
  mssdemux->base_url = NULL;
}

static void
gst_mss_demux_dispose (GObject * object)
{
  gst_mss_demux_reset (GST_ADAPTIVE_DEMUX_CAST (object));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_mss_demux_is_live (GstAdaptiveDemux * demux)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  g_return_val_if_fail (mssdemux->manifest != NULL, FALSE);

  return gst_mss_manifest_is_live (mssdemux->manifest);
}

static GstClockTime
gst_mss_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  g_return_val_if_fail (mssdemux->manifest != NULL, FALSE);

  return gst_mss_manifest_get_gst_duration (mssdemux->manifest);
}

static GstFlowReturn
gst_mss_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream)
{
  GstMssDemuxStream *mssstream = (GstMssDemuxStream *) stream;
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (stream->demux);
  GstFlowReturn ret;
  gchar *path = NULL;

  gst_adaptive_demux2_stream_fragment_clear (&stream->fragment);
  ret = gst_mss_stream_get_fragment_url (mssstream->manifest_stream, &path);

  if (ret == GST_FLOW_OK) {
    GstUri *base_url, *frag_url;

    base_url = gst_uri_from_string (mssdemux->base_url);
    frag_url = gst_uri_from_string_with_base (base_url, path);

    g_free (stream->fragment.uri);
    stream->fragment.uri = gst_uri_to_string (frag_url);
    stream->fragment.stream_time =
        gst_mss_stream_get_fragment_gst_timestamp (mssstream->manifest_stream);
    stream->fragment.duration =
        gst_mss_stream_get_fragment_gst_duration (mssstream->manifest_stream);

    gst_uri_unref (base_url);
    gst_uri_unref (frag_url);
  }
  g_free (path);

  return ret;
}

static GstFlowReturn
gst_mss_demux_stream_seek (GstAdaptiveDemux2Stream * stream, gboolean forward,
    GstSeekFlags flags, GstClockTimeDiff ts, GstClockTimeDiff * final_ts)
{
  GstMssDemuxStream *mssstream = (GstMssDemuxStream *) stream;

  gst_mss_stream_seek (mssstream->manifest_stream, forward, flags, ts,
      final_ts);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mss_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstMssDemuxStream *mssstream = (GstMssDemuxStream *) stream;

  if (stream->demux->segment.rate >= 0)
    return gst_mss_stream_advance_fragment (mssstream->manifest_stream);
  else
    return gst_mss_stream_regress_fragment (mssstream->manifest_stream);
}

static GstCaps *
create_mss_caps (GstMssDemuxStream * stream, GstCaps * caps)
{
  return gst_caps_new_simple ("video/quicktime", "variant", G_TYPE_STRING,
      "mss-fragmented", "timescale", G_TYPE_UINT64,
      gst_mss_stream_get_timescale (stream->manifest_stream), "media-caps",
      GST_TYPE_CAPS, caps, NULL);
}

static void
gst_mss_demux_apply_protection_system (GstCaps * caps,
    const gchar * selected_system)
{
  GstStructure *s;

  g_return_if_fail (selected_system);
  s = gst_caps_get_structure (caps, 0);
  gst_structure_set (s,
      "original-media-type", G_TYPE_STRING, gst_structure_get_name (s),
      GST_PROTECTION_SYSTEM_ID_CAPS_FIELD, G_TYPE_STRING, selected_system,
      NULL);
  gst_structure_set_name (s, "application/x-cenc");

}

static gboolean
gst_mss_demux_setup_streams (GstAdaptiveDemux * demux)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);
  GSList *streams = gst_mss_manifest_get_streams (mssdemux->manifest);
  GSList *active_streams = NULL;
  GSList *iter;
  const gchar *protection_system_id =
      gst_mss_manifest_get_protection_system_id (mssdemux->manifest);
  const gchar *protection_data =
      gst_mss_manifest_get_protection_data (mssdemux->manifest);
  gboolean protected = protection_system_id && protection_data;
  const gchar *selected_system = NULL;
  guint64 max_bitrate = G_MAXUINT64;

  if (streams == NULL) {
    GST_INFO_OBJECT (mssdemux, "No streams found in the manifest");
    GST_ELEMENT_ERROR (mssdemux, STREAM, DEMUX,
        (_("This file contains no playable streams.")),
        ("no streams found at the Manifest"));
    return FALSE;
  }

  if (protected) {
    const gchar *sys_ids[2] = { protection_system_id, NULL };

    selected_system = gst_protection_select_system (sys_ids);
    if (!selected_system) {
      GST_ERROR_OBJECT (mssdemux, "stream is protected, but no "
          "suitable decryptor element has been found");
      return FALSE;
    }
  }

  if (demux->connection_speed != 0)
    max_bitrate = demux->connection_speed;

  for (iter = streams; iter; iter = g_slist_next (iter)) {
    GstAdaptiveDemux2Stream *stream = NULL;
    GstMssDemuxStream *mss_stream;
    GstMssStream *manifeststream = iter->data;
    GstAdaptiveDemuxTrack *track;
    GstStreamType stream_type =
        gst_stream_type_from_mss_type (gst_mss_stream_get_type
        (manifeststream));
    const gchar *lang = gst_mss_stream_get_lang (manifeststream);
    const gchar *name = gst_mss_stream_get_name (manifeststream);
    gchar *stream_id;
    GstCaps *caps;
    GstTagList *tags = NULL;

    if (stream_type == GST_STREAM_TYPE_UNKNOWN) {
      GST_WARNING_OBJECT (mssdemux, "Skipping unknown stream %s", name);
      continue;
    }

    if (name)
      stream_id =
          g_strdup_printf ("mss-stream-%s-%s",
          gst_stream_type_get_name (stream_type),
          gst_mss_stream_get_name (manifeststream));
    else if (lang)
      stream_id =
          g_strdup_printf ("mss-stream-%s-%s",
          gst_stream_type_get_name (stream_type), lang);
    else
      stream_id =
          g_strdup_printf ("mss-stream-%s",
          gst_stream_type_get_name (stream_type));

    mss_stream =
        g_object_new (GST_TYPE_MSS_DEMUX_STREAM, "name", stream_id, NULL);

    stream = GST_ADAPTIVE_DEMUX2_STREAM_CAST (mss_stream);
    stream->stream_type = stream_type;

    mss_stream->manifest_stream = manifeststream;
    gst_mss_stream_set_active (manifeststream, TRUE);

    /* Set the maximum bitrate now that the underlying stream is active. This
     * ensures that we get the proper caps and information. */
    gst_mss_stream_select_bitrate (manifeststream, max_bitrate);

    caps = gst_mss_stream_get_caps (mss_stream->manifest_stream);
    gst_adaptive_demux2_stream_set_caps (stream, create_mss_caps (mss_stream,
            caps));
    if (lang != NULL)
      tags = gst_tag_list_new (GST_TAG_LANGUAGE_CODE, lang, NULL);

    if (tags)
      gst_adaptive_demux2_stream_set_tags (stream, gst_tag_list_ref (tags));

    track = gst_adaptive_demux_track_new (demux, stream_type,
        GST_STREAM_FLAG_NONE, (gchar *) stream_id, create_mss_caps (mss_stream,
            caps), tags);

    g_free (stream_id);
    gst_adaptive_demux2_add_stream (demux, stream);
    gst_adaptive_demux2_stream_add_track (stream, track);
    gst_adaptive_demux_track_unref (track);

    GST_DEBUG_OBJECT (stream, "Current quality bitrate %" G_GUINT64_FORMAT,
        gst_mss_stream_get_current_bitrate (manifeststream));

    active_streams = g_slist_prepend (active_streams, mss_stream);
  }

  for (iter = active_streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    if (protected) {
      GstBuffer *protection_buffer =
          gst_buffer_new_wrapped (g_strdup (protection_data),
          strlen (protection_data));
      GstEvent *event =
          gst_event_new_protection (protection_system_id, protection_buffer,
          "smooth-streaming");

      GST_LOG_OBJECT (stream, "Queueing Protection event on source pad");
      gst_adaptive_demux2_stream_queue_event ((GstAdaptiveDemux2Stream *)
          stream, event);
      gst_buffer_unref (protection_buffer);
    }
  }

  g_slist_free (active_streams);
  return TRUE;
}

static void
gst_mss_demux_update_base_url (GstMssDemux * mssdemux)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (mssdemux);
  GstUri *base_url;
  gchar *path;

  g_free (mssdemux->base_url);

  mssdemux->base_url =
      g_strdup (demux->manifest_base_uri ? demux->manifest_base_uri : demux->
      manifest_uri);

  base_url = gst_uri_from_string (mssdemux->base_url);
  path = gst_uri_get_path (base_url);
  GST_DEBUG ("%s", path);

  if (!g_str_has_suffix (path, "/Manifest")
      && !g_str_has_suffix (path, "/manifest"))
    GST_WARNING_OBJECT (mssdemux, "Stream's URI didn't end with /manifest");

  g_free (path);
  gst_uri_unref (base_url);
}

static gboolean
gst_mss_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  gst_mss_demux_update_base_url (mssdemux);

  mssdemux->manifest = gst_mss_manifest_new (buf);
  if (!mssdemux->manifest) {
    GST_ELEMENT_ERROR (mssdemux, STREAM, FORMAT, ("Bad manifest file"),
        ("Xml manifest file couldn't be parsed"));
    return FALSE;
  }
  return gst_mss_demux_setup_streams (demux);
}

static gboolean
gst_mss_demux_stream_select_bitrate (GstAdaptiveDemux2Stream * stream,
    guint64 bitrate)
{
  GstMssDemuxStream *mssstream = (GstMssDemuxStream *) stream;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (stream,
      "Using stream download bitrate %" G_GUINT64_FORMAT, bitrate);

  if (gst_mss_stream_select_bitrate (mssstream->manifest_stream,
          bitrate / MAX (1.0, ABS (stream->demux->segment.rate)))) {
    GstCaps *caps;
    GstCaps *msscaps;
    GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (stream->demux);
    const gchar *protection_system_id =
        gst_mss_manifest_get_protection_system_id (mssdemux->manifest);
    const gchar *protection_data =
        gst_mss_manifest_get_protection_data (mssdemux->manifest);
    gboolean protected = protection_system_id && protection_data;

    caps = gst_mss_stream_get_caps (mssstream->manifest_stream);

    GST_DEBUG_OBJECT (stream,
        "Starting streams reconfiguration due to bitrate changes");

    if (protected) {
      const gchar *sys_ids[2] = { protection_system_id, NULL };
      const gchar *selected_system = gst_protection_select_system (sys_ids);

      if (!selected_system) {
        GST_ERROR_OBJECT (mssdemux, "stream is protected, but no "
            "suitable decryptor element has been found");
        gst_caps_unref (caps);
        return FALSE;
      }

      gst_mss_demux_apply_protection_system (caps, selected_system);
    }

    msscaps = create_mss_caps (mssstream, caps);

    GST_DEBUG_OBJECT (stream,
        "Stream changed bitrate to %" G_GUINT64_FORMAT " caps: %"
        GST_PTR_FORMAT,
        gst_mss_stream_get_current_bitrate (mssstream->manifest_stream), caps);

    gst_caps_unref (caps);

    gst_adaptive_demux2_stream_set_caps (stream, msscaps);
    ret = TRUE;
    GST_DEBUG_OBJECT (stream, "Finished streams reconfiguration");
  }
  return ret;
}

#define SEEK_UPDATES_PLAY_POSITION(r, start_type, stop_type) \
  ((r >= 0 && start_type != GST_SEEK_TYPE_NONE) || \
   (r < 0 && stop_type != GST_SEEK_TYPE_NONE))

static gboolean
gst_mss_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  gst_event_parse_seek (seek, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  GST_DEBUG_OBJECT (mssdemux,
      "seek event, rate: %f start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT, rate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  if (SEEK_UPDATES_PLAY_POSITION (rate, start_type, stop_type)) {
    if (rate >= 0)
      gst_mss_manifest_seek (mssdemux->manifest, rate >= 0, start);
    else
      gst_mss_manifest_seek (mssdemux->manifest, rate >= 0, stop);
  }

  return TRUE;
}

static gboolean
gst_mss_demux_stream_has_next_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstMssDemuxStream *mssstream = (GstMssDemuxStream *) stream;

  return gst_mss_stream_has_next_fragment (mssstream->manifest_stream);
}

static gint64
gst_mss_demux_get_manifest_update_interval (GstAdaptiveDemux * demux)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);
  GstClockTime interval;

  /* Not much information about this in the MSS spec. It seems that
   * the fragments contain an UUID box that should tell the next
   * fragments time and duration so one wouldn't need to fetch
   * the Manifest again, but we need a fallback here. So use 2 times
   * the current fragment duration */

  interval = gst_mss_manifest_get_min_fragment_duration (mssdemux->manifest);
  if (!GST_CLOCK_TIME_IS_VALID (interval))
    interval = 2 * GST_SECOND;  /* default to 2 seconds */

  interval = 2 * (interval / GST_USECOND);

  return interval;
}

static GstClockTime
gst_mss_demux_stream_get_fragment_waiting_time (GstAdaptiveDemux2Stream *
    stream)
{
  /* Wait a second for live streams so we don't try premature fragments downloading */
  return GST_SECOND;
}

static GstFlowReturn
gst_mss_demux_update_manifest_data (GstAdaptiveDemux * demux,
    GstBuffer * buffer)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  gst_mss_demux_update_base_url (mssdemux);

  gst_mss_manifest_reload_fragments (mssdemux->manifest, buffer);
  return GST_FLOW_OK;
}

static gboolean
gst_mss_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (demux);

  return gst_mss_manifest_get_live_seek_range (mssdemux->manifest, start, stop);
}

static GstFlowReturn
gst_mss_demux_stream_data_received (GstAdaptiveDemux2Stream * stream,
    GstBuffer * buffer)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (stream->demux);
  GstMssDemuxStream *mssstream = (GstMssDemuxStream *) stream;
  gsize available;

  if (!gst_mss_manifest_is_live (mssdemux->manifest)) {
    return gst_adaptive_demux2_stream_push_buffer (stream, buffer);
  }

  if (gst_mss_stream_fragment_parsing_needed (mssstream->manifest_stream)) {
    gst_mss_manifest_live_adapter_push (mssstream->manifest_stream, buffer);
    available =
        gst_mss_manifest_live_adapter_available (mssstream->manifest_stream);
    // FIXME: try to reduce this minimal size.
    if (available < 4096) {
      return GST_FLOW_OK;
    } else {
      GST_LOG_OBJECT (stream, "enough data, parsing fragment.");
      buffer =
          gst_mss_manifest_live_adapter_take_buffer (mssstream->manifest_stream,
          available);
      gst_mss_stream_parse_fragment (mssstream->manifest_stream, buffer);
    }
  }

  return gst_adaptive_demux2_stream_push_buffer (stream, buffer);
}

static gboolean
gst_mss_demux_requires_periodical_playlist_update (GstAdaptiveDemux * demux)
{
  return TRUE;
}

GstStreamType
gst_stream_type_from_mss_type (GstMssStreamType mtype)
{
  switch (mtype) {
    case MSS_STREAM_TYPE_AUDIO:
      return GST_STREAM_TYPE_AUDIO;
    case MSS_STREAM_TYPE_VIDEO:
      return GST_STREAM_TYPE_VIDEO;
    default:
      return GST_STREAM_TYPE_UNKNOWN;
  }
}

static gboolean
mssdemux_element_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (mssdemux2_debug, "mssdemux2", 0,
      "mssdemux2 element");

  if (!adaptivedemux2_base_element_init (plugin))
    return TRUE;

  ret =
      gst_element_register (plugin, "mssdemux2", GST_RANK_PRIMARY + 1,
      GST_TYPE_MSS_DEMUX2);

  return ret;
}
