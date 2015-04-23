/*
 * DASH demux plugin for GStreamer
 *
 * gstdashdemux.c
 *
 * Copyright (C) 2012 Orange
 *
 * Authors:
 *   David Corvoysier <david.corvoysier@orange.com>
 *   Hamid Zakari <hamid.zakari@gmail.com>
 *
 * Copyright (C) 2013 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-dashdemux
 *
 * DASH demuxer element.
 * <title>Example launch line</title>
 * |[
 * gst-launch playbin2 uri="http://www-itec.uni-klu.ac.at/ftp/datasets/mmsys12/RedBullPlayStreets/redbull_4s/RedBullPlayStreets_4s_isoffmain_DIS_23009_1_v_2_1c2_2011_08_30.mpd"
 * ]|
 */

/* Implementation notes:
 *
 * The following section describes how dashdemux works internally.
 *
 * Introduction:
 *
 * dashdemux is a "fake" demux, as unlike traditional demux elements, it
 * doesn't split data streams contained in an enveloppe to expose them
 * to downstream decoding elements.
 *
 * Instead, it parses an XML file called a manifest to identify a set of
 * individual stream fragments it needs to fetch and expose to the actual
 * demux elements that will handle them (this behavior is sometimes
 * referred as the "demux after a demux" scenario).
 *
 * For a given section of content, several representations corresponding
 * to different bitrates may be available: dashdemux will select the most
 * appropriate representation based on local conditions (typically the
 * available bandwidth and the amount of buffering available, capped by
 * a maximum allowed bitrate).
 *
 * The representation selection algorithm can be configured using
 * specific properties: max bitrate, min/max buffering, bandwidth ratio.
 *
 *
 * General Design:
 *
 * dashdemux has a single sink pad that accepts the data corresponding
 * to the manifest, typically fetched from an HTTP or file source.
 *
 * dashdemux exposes the streams it recreates based on the fragments it
 * fetches through dedicated src pads corresponding to the caps of the
 * fragments container (ISOBMFF/MP4 or MPEG2TS).
 *
 * During playback, new representations will typically be exposed as a
 * new set of pads (see 'Switching between representations' below).
 *
 * Fragments downloading is performed using a dedicated task that fills
 * an internal queue. Another task is in charge of popping fragments
 * from the queue and pushing them downstream.
 *
 * Switching between representations:
 *
 * Decodebin supports scenarios allowing to seamlessly switch from one
 * stream to another inside the same "decoding chain".
 *
 * To achieve that, it combines the elements it autoplugged in chains
 *  and groups, allowing only one decoding group to be active at a given
 * time for a given chain.
 *
 * A chain can signal decodebin that it is complete by sending a
 * no-more-pads event, but even after that new pads can be added to
 * create new subgroups, providing that a new no-more-pads event is sent.
 *
 * We take advantage of that to dynamically create a new decoding group
 * in order to select a different representation during playback.
 *
 * Typically, assuming that each fragment contains both audio and video,
 * the following tree would be created:
 *
 * chain "DASH Demux"
 * |_ group "Representation set 1"
 * |   |_ chain "Qt Demux 0"
 * |       |_ group "Stream 0"
 * |           |_ chain "H264"
 * |           |_ chain "AAC"
 * |_ group "Representation set 2"
 *     |_ chain "Qt Demux 1"
 *         |_ group "Stream 1"
 *             |_ chain "H264"
 *             |_ chain "AAC"
 *
 * Or, if audio and video are contained in separate fragments:
 *
 * chain "DASH Demux"
 * |_ group "Representation set 1"
 * |   |_ chain "Qt Demux 0"
 * |   |   |_ group "Stream 0"
 * |   |       |_ chain "H264"
 * |   |_ chain "Qt Demux 1"
 * |       |_ group "Stream 1"
 * |           |_ chain "AAC"
 * |_ group "Representation set 2"
 *     |_ chain "Qt Demux 3"
 *     |   |_ group "Stream 2"
 *     |       |_ chain "H264"
 *     |_ chain "Qt Demux 4"
 *         |_ group "Stream 3"
 *             |_ chain "AAC"
 *
 * In both cases, when switching from Set 1 to Set 2 an EOS is sent on
 * each end pad corresponding to Rep 0, triggering the "drain" state to
 * propagate upstream.
 * Once both EOS have been processed, the "Set 1" group is completely
 * drained, and decodebin2 will switch to the "Set 2" group.
 *
 * Note: nothing can be pushed to the new decoding group before the
 * old one has been drained, which means that in order to be able to
 * adapt quickly to bandwidth changes, we will not be able to rely
 * on downstream buffering, and will instead manage an internal queue.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <inttypes.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/tag/tag.h>
#include "gst/gst-i18n-plugin.h"
#include "gstdashdemux.h"
#include "gstdash_debug.h"

static GstStaticPadTemplate gst_dash_demux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_dash_demux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/dash+xml"));

GST_DEBUG_CATEGORY (gst_dash_demux_debug);
#define GST_CAT_DEFAULT gst_dash_demux_debug

enum
{
  PROP_0,

  PROP_MAX_BUFFERING_TIME,
  PROP_BANDWIDTH_USAGE,
  PROP_MAX_BITRATE,
  PROP_LAST
};

/* Default values for properties */
#define DEFAULT_MAX_BUFFERING_TIME       30     /* in seconds */
#define DEFAULT_BANDWIDTH_USAGE         0.8     /* 0 to 1     */
#define DEFAULT_MAX_BITRATE        24000000     /* in bit/s  */

/* GObject */
static void gst_dash_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dash_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_dash_demux_dispose (GObject * obj);

/* GstAdaptiveDemux */
static GstClockTime gst_dash_demux_get_duration (GstAdaptiveDemux * ademux);
static gboolean gst_dash_demux_is_live (GstAdaptiveDemux * ademux);
static void gst_dash_demux_reset (GstAdaptiveDemux * ademux);
static gboolean gst_dash_demux_process_manifest (GstAdaptiveDemux * ademux,
    GstBuffer * buf);
static gboolean gst_dash_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek);
static GstFlowReturn
gst_dash_demux_stream_update_fragment_info (GstAdaptiveDemuxStream * stream);
static GstFlowReturn gst_dash_demux_stream_seek (GstAdaptiveDemuxStream *
    stream, GstClockTime ts);
static GstFlowReturn
gst_dash_demux_stream_advance_fragment (GstAdaptiveDemuxStream * stream);
static gboolean
gst_dash_demux_stream_advance_subfragment (GstAdaptiveDemuxStream * stream);
static gboolean gst_dash_demux_stream_select_bitrate (GstAdaptiveDemuxStream *
    stream, guint64 bitrate);
static gint64
gst_dash_demux_get_manifest_update_interval (GstAdaptiveDemux * demux);
static GstFlowReturn
gst_dash_demux_update_manifest_data (GstAdaptiveDemux * demux, GstBuffer * buf);
static gint64
gst_dash_demux_stream_get_fragment_waiting_time (GstAdaptiveDemuxStream *
    stream);
static void gst_dash_demux_advance_period (GstAdaptiveDemux * demux);
static gboolean gst_dash_demux_has_next_period (GstAdaptiveDemux * demux);
static GstFlowReturn gst_dash_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn
gst_dash_demux_stream_fragment_finished (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);

/* GstDashDemux */
static gboolean gst_dash_demux_setup_all_streams (GstDashDemux * demux);
static void gst_dash_demux_stream_free (GstAdaptiveDemuxStream * stream);

static GstCaps *gst_dash_demux_get_input_caps (GstDashDemux * demux,
    GstActiveStream * stream);
static GstPad *gst_dash_demux_create_pad (GstDashDemux * demux,
    GstActiveStream * stream);

#define SIDX(s) (&(s)->sidx_parser.sidx)
#define SIDX_ENTRY(s,i) (&(SIDX(s)->entries[(i)]))
#define SIDX_CURRENT_ENTRY(s) SIDX_ENTRY(s, SIDX(s)->entry_index)

#define gst_dash_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDashDemux, gst_dash_demux, GST_TYPE_ADAPTIVE_DEMUX,
    GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "dashdemux", 0,
        "dashdemux element");
    );

static void
gst_dash_demux_dispose (GObject * obj)
{
  GstDashDemux *demux = GST_DASH_DEMUX (obj);

  gst_dash_demux_reset (GST_ADAPTIVE_DEMUX_CAST (demux));

  if (demux->client) {
    gst_mpd_client_free (demux->client);
    demux->client = NULL;
  }

  g_mutex_clear (&demux->client_lock);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static gboolean
gst_dash_demux_get_live_seek_range (GstAdaptiveDemux * demux, gint64 * start,
    gint64 * stop)
{
  GstDashDemux *self = GST_DASH_DEMUX (demux);
  GDateTime *now = g_date_time_new_now_utc ();
  GDateTime *mstart =
      gst_date_time_to_g_date_time (self->client->
      mpd_node->availabilityStartTime);
  GTimeSpan stream_now;

  stream_now = g_date_time_difference (now, mstart);
  g_date_time_unref (now);
  g_date_time_unref (mstart);
  *stop = stream_now * GST_USECOND;

  *start = *stop - (self->client->mpd_node->timeShiftBufferDepth * GST_MSECOND);
  return TRUE;
}

static GstClockTime
gst_dash_demux_get_presentation_offset (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);

  return gst_mpd_parser_get_stream_presentation_offset (dashdemux->client,
      dashstream->index);
}

static void
gst_dash_demux_class_init (GstDashDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAdaptiveDemuxClass *gstadaptivedemux_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstadaptivedemux_class = (GstAdaptiveDemuxClass *) klass;

  gobject_class->set_property = gst_dash_demux_set_property;
  gobject_class->get_property = gst_dash_demux_get_property;
  gobject_class->dispose = gst_dash_demux_dispose;

#ifndef GST_REMOVE_DEPRECATED
  g_object_class_install_property (gobject_class, PROP_MAX_BUFFERING_TIME,
      g_param_spec_uint ("max-buffering-time", "Maximum buffering time",
          "Maximum number of seconds of buffer accumulated during playback"
          "(deprecated)",
          2, G_MAXUINT, DEFAULT_MAX_BUFFERING_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED));

  g_object_class_install_property (gobject_class, PROP_BANDWIDTH_USAGE,
      g_param_spec_float ("bandwidth-usage",
          "Bandwidth usage [0..1]",
          "Percentage of the available bandwidth to use when "
          "selecting representations (deprecated)",
          0, 1, DEFAULT_BANDWIDTH_USAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max bitrate",
          "Max of bitrate supported by target decoder",
          1000, G_MAXUINT, DEFAULT_MAX_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dash_demux_audiosrc_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dash_demux_videosrc_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "DASH Demuxer",
      "Codec/Demuxer/Adaptive",
      "Dynamic Adaptive Streaming over HTTP demuxer",
      "David Corvoysier <david.corvoysier@orange.com>\n\
                Hamid Zakari <hamid.zakari@gmail.com>\n\
                Gianluca Gennari <gennarone@gmail.com>");


  gstadaptivedemux_class->get_duration = gst_dash_demux_get_duration;
  gstadaptivedemux_class->is_live = gst_dash_demux_is_live;
  gstadaptivedemux_class->reset = gst_dash_demux_reset;
  gstadaptivedemux_class->seek = gst_dash_demux_seek;

  gstadaptivedemux_class->process_manifest = gst_dash_demux_process_manifest;
  gstadaptivedemux_class->update_manifest_data =
      gst_dash_demux_update_manifest_data;
  gstadaptivedemux_class->get_manifest_update_interval =
      gst_dash_demux_get_manifest_update_interval;

  gstadaptivedemux_class->has_next_period = gst_dash_demux_has_next_period;
  gstadaptivedemux_class->advance_period = gst_dash_demux_advance_period;
  gstadaptivedemux_class->stream_advance_fragment =
      gst_dash_demux_stream_advance_fragment;
  gstadaptivedemux_class->stream_get_fragment_waiting_time =
      gst_dash_demux_stream_get_fragment_waiting_time;
  gstadaptivedemux_class->stream_seek = gst_dash_demux_stream_seek;
  gstadaptivedemux_class->stream_select_bitrate =
      gst_dash_demux_stream_select_bitrate;
  gstadaptivedemux_class->stream_update_fragment_info =
      gst_dash_demux_stream_update_fragment_info;
  gstadaptivedemux_class->stream_free = gst_dash_demux_stream_free;
  gstadaptivedemux_class->get_live_seek_range =
      gst_dash_demux_get_live_seek_range;
  gstadaptivedemux_class->get_presentation_offset =
      gst_dash_demux_get_presentation_offset;
}

static void
gst_dash_demux_init (GstDashDemux * demux)
{
  /* Properties */
  demux->max_buffering_time = DEFAULT_MAX_BUFFERING_TIME * GST_SECOND;
  demux->max_bitrate = DEFAULT_MAX_BITRATE;

  g_mutex_init (&demux->client_lock);

  gst_adaptive_demux_set_stream_struct_size (GST_ADAPTIVE_DEMUX_CAST (demux),
      sizeof (GstDashDemuxStream));
}

static void
gst_dash_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAdaptiveDemux *adaptivedemux = GST_ADAPTIVE_DEMUX_CAST (object);
  GstDashDemux *demux = GST_DASH_DEMUX (object);

  switch (prop_id) {
    case PROP_MAX_BUFFERING_TIME:
      demux->max_buffering_time = g_value_get_uint (value) * GST_SECOND;
      break;
    case PROP_BANDWIDTH_USAGE:
      adaptivedemux->bitrate_limit = g_value_get_float (value);
      break;
    case PROP_MAX_BITRATE:
      demux->max_bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dash_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAdaptiveDemux *adaptivedemux = GST_ADAPTIVE_DEMUX_CAST (object);
  GstDashDemux *demux = GST_DASH_DEMUX (object);

  switch (prop_id) {
    case PROP_MAX_BUFFERING_TIME:
      g_value_set_uint (value, demux->max_buffering_time / GST_SECOND);
      break;
    case PROP_BANDWIDTH_USAGE:
      g_value_set_float (value, adaptivedemux->bitrate_limit);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, demux->max_bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_dash_demux_setup_mpdparser_streams (GstDashDemux * demux,
    GstMpdClient * client)
{
  gboolean has_streams = FALSE;
  GList *adapt_sets, *iter;

  adapt_sets = gst_mpd_client_get_adaptation_sets (client);
  for (iter = adapt_sets; iter; iter = g_list_next (iter)) {
    GstAdaptationSetNode *adapt_set_node = iter->data;

    gst_mpd_client_setup_streaming (client, adapt_set_node);
    has_streams = TRUE;
  }

  if (!has_streams) {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, ("Manifest has no playable "
            "streams"), ("No streams could be activated from the manifest"));
  }
  return has_streams;
}

static gboolean
gst_dash_demux_setup_all_streams (GstDashDemux * demux)
{
  guint i;

  GST_DEBUG_OBJECT (demux, "Setting up streams for period %d",
      gst_mpd_client_get_period_index (demux->client));

  /* clean old active stream list, if any */
  gst_active_streams_free (demux->client);

  if (!gst_dash_demux_setup_mpdparser_streams (demux, demux->client)) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (demux, "Creating stream objects");
  for (i = 0; i < gst_mpdparser_get_nb_active_stream (demux->client); i++) {
    GstDashDemuxStream *stream;
    GstActiveStream *active_stream;
    GstCaps *caps;
    GstPad *srcpad;
    gchar *lang = NULL;
    GstTagList *tags = NULL;

    active_stream = gst_mpdparser_get_active_stream_by_index (demux->client, i);
    if (active_stream == NULL)
      continue;
    /* TODO: support 'application' mimeType */
    if (active_stream->mimeType == GST_STREAM_APPLICATION)
      continue;

    srcpad = gst_dash_demux_create_pad (demux, active_stream);
    caps = gst_dash_demux_get_input_caps (demux, active_stream);
    GST_LOG_OBJECT (demux, "Creating stream %d %" GST_PTR_FORMAT, i, caps);

    if (active_stream->cur_adapt_set) {
      GstAdaptationSetNode *adp_set = active_stream->cur_adapt_set;
      lang = adp_set->lang;

      /* Fallback to the language in ContentComponent node */
      if (lang == NULL && g_list_length (adp_set->ContentComponents) == 1) {
        GstContentComponentNode *cc_node = adp_set->ContentComponents->data;
        lang = cc_node->lang;
      }
    }

    if (lang) {
      if (gst_tag_check_language_code (lang))
        tags = gst_tag_list_new (GST_TAG_LANGUAGE_CODE, lang, NULL);
      else
        tags = gst_tag_list_new (GST_TAG_LANGUAGE_NAME, lang, NULL);
    }

    stream = (GstDashDemuxStream *)
        gst_adaptive_demux_stream_new (GST_ADAPTIVE_DEMUX_CAST (demux), srcpad);
    stream->active_stream = active_stream;
    gst_adaptive_demux_stream_set_caps (GST_ADAPTIVE_DEMUX_STREAM_CAST (stream),
        caps);
    if (tags)
      gst_adaptive_demux_stream_set_tags (GST_ADAPTIVE_DEMUX_STREAM_CAST
          (stream), tags);
    stream->index = i;
    stream->pending_seek_ts = GST_CLOCK_TIME_NONE;
    gst_isoff_sidx_parser_init (&stream->sidx_parser);
  }

  return TRUE;
}

static GstClockTime
gst_dash_demux_get_duration (GstAdaptiveDemux * ademux)
{
  GstDashDemux *demux = GST_DASH_DEMUX_CAST (ademux);

  g_return_val_if_fail (demux->client != NULL, GST_CLOCK_TIME_NONE);

  return gst_mpd_client_get_media_presentation_duration (demux->client);
}

static gboolean
gst_dash_demux_is_live (GstAdaptiveDemux * ademux)
{
  GstDashDemux *demux = GST_DASH_DEMUX_CAST (ademux);

  g_return_val_if_fail (demux->client != NULL, FALSE);

  return gst_mpd_client_is_live (demux->client);
}

static gboolean
gst_dash_demux_setup_streams (GstAdaptiveDemux * demux)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);
  gboolean ret = TRUE;
  GstDateTime *now = NULL;
  guint period_idx;

  /* setup video, audio and subtitle streams, starting from first Period if
   * non-live */
  period_idx = 0;
  if (gst_mpd_client_is_live (dashdemux->client)) {

    /* get period index for period encompassing the current time */
    now = gst_date_time_new_now_utc ();
    if (dashdemux->client->mpd_node->suggestedPresentationDelay != -1) {
      GstDateTime *target = gst_mpd_client_add_time_difference (now,
          dashdemux->client->mpd_node->suggestedPresentationDelay * -1000);
      gst_date_time_unref (now);
      now = target;
    }
    period_idx =
        gst_mpd_client_get_period_index_at_time (dashdemux->client, now);
    if (period_idx == G_MAXUINT) {
#ifndef GST_DISABLE_GST_DEBUG
      gchar *date_str = gst_date_time_to_iso8601_string (now);
      GST_DEBUG_OBJECT (demux, "Unable to find live period active at %s",
          date_str);
      g_free (date_str);
#endif
      ret = FALSE;
      goto done;
    }
  }

  if (!gst_mpd_client_set_period_index (dashdemux->client, period_idx) ||
      !gst_dash_demux_setup_all_streams (dashdemux)) {
    ret = FALSE;
    goto done;
  }

  /* If stream is live, try to find the segment that
   * is closest to current time */
  if (gst_mpd_client_is_live (dashdemux->client)) {
    GList *iter;
    gint seg_idx;

    GST_DEBUG_OBJECT (demux, "Seeking to current time of day for live stream ");
    for (iter = demux->next_streams; iter; iter = g_list_next (iter)) {
      GstDashDemuxStream *stream = iter->data;
      GstActiveStream *active_stream = stream->active_stream;

      /* Get segment index corresponding to current time. */
      seg_idx =
          gst_mpd_client_get_segment_index_at_time (dashdemux->client,
          active_stream, now);
      if (seg_idx < 0) {
        GST_WARNING_OBJECT (demux,
            "Failed to find a segment that is available "
            "at this point in time for stream %d.", stream->index);
        seg_idx = 0;
      }
      GST_INFO_OBJECT (demux,
          "Segment index corresponding to current time for stream "
          "%d is %d.", stream->index, seg_idx);
      gst_mpd_client_set_segment_index (active_stream, seg_idx);
    }

  } else {
    GST_DEBUG_OBJECT (demux, "Seeking to first segment for on-demand stream ");

    /* start playing from the first segment */
    gst_mpd_client_set_segment_index_for_all_streams (dashdemux->client, 0);
  }

done:
  if (now != NULL)
    gst_date_time_unref (now);
  return ret;
}

static gboolean
gst_dash_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstAdaptiveDemuxClass *klass;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);
  gboolean ret = FALSE;
  gchar *manifest;
  GstMapInfo mapinfo;

  if (dashdemux->client)
    gst_mpd_client_free (dashdemux->client);
  dashdemux->client = gst_mpd_client_new ();

  dashdemux->client->mpd_uri = g_strdup (demux->manifest_uri);
  dashdemux->client->mpd_base_uri = g_strdup (demux->manifest_base_uri);

  GST_DEBUG_OBJECT (demux, "Fetched MPD file at URI: %s (base: %s)",
      dashdemux->client->mpd_uri,
      GST_STR_NULL (dashdemux->client->mpd_base_uri));

  if (gst_buffer_map (buf, &mapinfo, GST_MAP_READ)) {
    manifest = (gchar *) mapinfo.data;
    if (gst_mpd_parse (dashdemux->client, manifest, mapinfo.size)) {
      if (gst_mpd_client_has_isoff_ondemand_profile (dashdemux->client)) {
        klass = GST_ADAPTIVE_DEMUX_GET_CLASS (dashdemux);

        klass->data_received = gst_dash_demux_data_received;
        klass->finish_fragment = gst_dash_demux_stream_fragment_finished;
      }

      if (gst_mpd_client_setup_media_presentation (dashdemux->client)) {
        ret = TRUE;
      } else {
        GST_ELEMENT_ERROR (demux, STREAM, DECODE,
            ("Incompatible manifest file."), (NULL));
      }
    }
    gst_buffer_unmap (buf, &mapinfo);
  } else {
    GST_WARNING_OBJECT (demux, "Failed to map manifest buffer");
  }

  if (ret)
    ret = gst_dash_demux_setup_streams (demux);

  return ret;
}

static GstPad *
gst_dash_demux_create_pad (GstDashDemux * demux, GstActiveStream * stream)
{
  GstPad *pad;
  GstPadTemplate *tmpl;
  gchar *name;

  switch (stream->mimeType) {
    case GST_STREAM_AUDIO:
      name = g_strdup_printf ("audio_%02u", demux->n_audio_streams++);
      tmpl = gst_static_pad_template_get (&gst_dash_demux_audiosrc_template);
      break;
    case GST_STREAM_VIDEO:
      name = g_strdup_printf ("video_%02u", demux->n_video_streams++);
      tmpl = gst_static_pad_template_get (&gst_dash_demux_videosrc_template);
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }

  /* Create and activate new pads */
  pad = gst_ghost_pad_new_no_target_from_template (name, tmpl);
  g_free (name);
  gst_object_unref (tmpl);

  gst_pad_set_active (pad, TRUE);
  GST_INFO_OBJECT (demux, "Creating srcpad %s:%s", GST_DEBUG_PAD_NAME (pad));
  return pad;
}

static void
gst_dash_demux_reset (GstAdaptiveDemux * ademux)
{
  GstDashDemux *demux = GST_DASH_DEMUX_CAST (ademux);

  GST_DEBUG_OBJECT (demux, "Resetting demux");

  demux->end_of_period = FALSE;
  demux->end_of_manifest = FALSE;

  if (demux->client) {
    gst_mpd_client_free (demux->client);
    demux->client = NULL;
  }
  demux->client = gst_mpd_client_new ();

  demux->n_audio_streams = 0;
  demux->n_video_streams = 0;
}

static GstCaps *
gst_dash_demux_get_video_input_caps (GstDashDemux * demux,
    GstActiveStream * stream)
{
  guint width = 0, height = 0;
  const gchar *mimeType = NULL;
  GstCaps *caps = NULL;

  if (stream == NULL)
    return NULL;

  /* if bitstreamSwitching is true we dont need to swich pads on resolution change */
  if (!gst_mpd_client_get_bitstream_switching_flag (stream)) {
    width = gst_mpd_client_get_video_stream_width (stream);
    height = gst_mpd_client_get_video_stream_height (stream);
  }
  mimeType = gst_mpd_client_get_stream_mimeType (stream);
  if (mimeType == NULL)
    return NULL;

  caps = gst_caps_from_string (mimeType);
  if (width > 0 && height > 0) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width, "height",
        G_TYPE_INT, height, NULL);
  }

  return caps;
}

static GstCaps *
gst_dash_demux_get_audio_input_caps (GstDashDemux * demux,
    GstActiveStream * stream)
{
  guint rate = 0, channels = 0;
  const gchar *mimeType;
  GstCaps *caps = NULL;

  if (stream == NULL)
    return NULL;

  /* if bitstreamSwitching is true we dont need to swich pads on rate/channels change */
  if (!gst_mpd_client_get_bitstream_switching_flag (stream)) {
    channels = gst_mpd_client_get_audio_stream_num_channels (stream);
    rate = gst_mpd_client_get_audio_stream_rate (stream);
  }
  mimeType = gst_mpd_client_get_stream_mimeType (stream);
  if (mimeType == NULL)
    return NULL;

  caps = gst_caps_from_string (mimeType);
  if (rate > 0) {
    gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
  }
  if (channels > 0) {
    gst_caps_set_simple (caps, "channels", G_TYPE_INT, channels, NULL);
  }

  return caps;
}

static GstCaps *
gst_dash_demux_get_application_input_caps (GstDashDemux * demux,
    GstActiveStream * stream)
{
  const gchar *mimeType;
  GstCaps *caps = NULL;

  if (stream == NULL)
    return NULL;

  mimeType = gst_mpd_client_get_stream_mimeType (stream);
  if (mimeType == NULL)
    return NULL;

  caps = gst_caps_from_string (mimeType);

  return caps;
}

static GstCaps *
gst_dash_demux_get_input_caps (GstDashDemux * demux, GstActiveStream * stream)
{
  switch (stream->mimeType) {
    case GST_STREAM_VIDEO:
      return gst_dash_demux_get_video_input_caps (demux, stream);
    case GST_STREAM_AUDIO:
      return gst_dash_demux_get_audio_input_caps (demux, stream);
    case GST_STREAM_APPLICATION:
      return gst_dash_demux_get_application_input_caps (demux, stream);
    default:
      return GST_CAPS_NONE;
  }
}

static void
gst_dash_demux_stream_update_headers_info (GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (stream->demux);
  gchar *path = NULL;

  gst_mpd_client_get_next_header (dashdemux->client,
      &path, dashstream->index,
      &stream->fragment.header_range_start, &stream->fragment.header_range_end);

  if (path != NULL && strncmp (path, "http://", 7) != 0) {
    stream->fragment.header_uri =
        gst_uri_join_strings (gst_mpdparser_get_baseURL (dashdemux->client,
            dashstream->index), path);
    g_free (path);
  } else {
    stream->fragment.header_uri = path;
  }
  path = NULL;

  gst_mpd_client_get_next_header_index (dashdemux->client,
      &path, dashstream->index,
      &stream->fragment.index_range_start, &stream->fragment.index_range_end);

  if (path != NULL && strncmp (path, "http://", 7) != 0) {
    stream->fragment.index_uri =
        gst_uri_join_strings (gst_mpdparser_get_baseURL (dashdemux->client,
            dashstream->index), path);
    g_free (path);
  } else {
    stream->fragment.index_uri = path;
  }
}

static GstFlowReturn
gst_dash_demux_stream_update_fragment_info (GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (stream->demux);
  GstClockTime ts;
  GstMediaFragmentInfo fragment;
  gboolean isombff;

  gst_adaptive_demux_stream_fragment_clear (&stream->fragment);

  isombff = gst_mpd_client_has_isoff_ondemand_profile (dashdemux->client);

  if (GST_ADAPTIVE_DEMUX_STREAM_NEED_HEADER (stream) && isombff) {
    gst_dash_demux_stream_update_headers_info (stream);
    dashstream->sidx_base_offset = stream->fragment.index_range_end + 1;
    if (dashstream->sidx_index != 0) {
      /* request only the index to be downloaded as we need to reposition the
       * stream to a subsegment */
      return GST_FLOW_OK;
    }
  }

  if (gst_mpd_client_get_next_fragment_timestamp (dashdemux->client,
          dashstream->index, &ts)) {
    if (GST_ADAPTIVE_DEMUX_STREAM_NEED_HEADER (stream)) {
      gst_dash_demux_stream_update_headers_info (stream);
    }

    gst_mpd_client_get_next_fragment (dashdemux->client, dashstream->index,
        &fragment);

    stream->fragment.uri = fragment.uri;
    if (isombff && dashstream->sidx_index != 0) {
      GstSidxBoxEntry *entry = SIDX_CURRENT_ENTRY (dashstream);
      stream->fragment.range_start =
          dashstream->sidx_base_offset + entry->offset;
      stream->fragment.timestamp = entry->pts;
      stream->fragment.duration = entry->duration;
      if (stream->demux->segment.rate < 0.0) {
        stream->fragment.range_end =
            stream->fragment.range_start + entry->size - 1;
      } else {
        stream->fragment.range_end = fragment.range_end;
      }
    } else {
      stream->fragment.timestamp = fragment.timestamp;
      stream->fragment.duration = fragment.duration;
      stream->fragment.range_start =
          MAX (fragment.range_start, dashstream->sidx_base_offset);
      stream->fragment.range_end = fragment.range_end;
    }

    return GST_FLOW_OK;
  }

  return GST_FLOW_EOS;
}

static void
gst_dash_demux_stream_sidx_seek (GstDashDemuxStream * dashstream,
    GstClockTime ts)
{
  GstSidxBox *sidx = SIDX (dashstream);
  gint i;

  /* TODO optimize to a binary search */
  for (i = 0; i < sidx->entries_count; i++) {
    if (sidx->entries[i].pts + sidx->entries[i].duration >= ts)
      break;
  }
  sidx->entry_index = i;
  dashstream->sidx_index = i;
  if (i < sidx->entries_count)
    dashstream->sidx_current_remaining = sidx->entries[i].size;
  else
    dashstream->sidx_current_remaining = 0;
}

static GstFlowReturn
gst_dash_demux_stream_seek (GstAdaptiveDemuxStream * stream, GstClockTime ts)
{
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (stream->demux);

  if (gst_mpd_client_has_isoff_ondemand_profile (dashdemux->client)) {
    if (dashstream->sidx_parser.status == GST_ISOFF_SIDX_PARSER_FINISHED) {
      gst_dash_demux_stream_sidx_seek (dashstream, ts);
    } else {
      /* no index yet, seek when we have it */
      dashstream->pending_seek_ts = ts;
    }
  }

  gst_mpd_client_stream_seek (dashdemux->client, dashstream->active_stream, ts);
  return GST_FLOW_OK;
}

static gboolean
gst_dash_demux_stream_advance_subfragment (GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;

  GstSidxBox *sidx = SIDX (dashstream);
  gboolean fragment_finished = TRUE;

  if (dashstream->sidx_parser.status == GST_ISOFF_SIDX_PARSER_FINISHED) {
    if (stream->demux->segment.rate > 0.0) {
      sidx->entry_index++;
      if (sidx->entry_index < sidx->entries_count) {
        fragment_finished = FALSE;
      }
    } else {
      sidx->entry_index--;
      if (sidx->entry_index >= 0) {
        fragment_finished = FALSE;
      }
    }
  }

  GST_DEBUG_OBJECT (stream->pad, "New sidx index: %d / %d. "
      "Finished fragment: %d", sidx->entry_index, sidx->entries_count,
      fragment_finished);

  if (!fragment_finished) {
    dashstream->sidx_current_remaining = sidx->entries[sidx->entry_index].size;
  }
  return !fragment_finished;
}

static GstFlowReturn
gst_dash_demux_stream_advance_fragment (GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (stream->demux);

  GST_DEBUG_OBJECT (stream->pad, "Advance fragment");

  if (gst_mpd_client_has_isoff_ondemand_profile (dashdemux->client)) {
    if (gst_dash_demux_stream_advance_subfragment (stream))
      return GST_FLOW_OK;
  }

  return gst_mpd_client_advance_segment (dashdemux->client,
      dashstream->active_stream, stream->demux->segment.rate > 0.0);
}

static gboolean
gst_dash_demux_stream_select_bitrate (GstAdaptiveDemuxStream * stream,
    guint64 bitrate)
{
  GstActiveStream *active_stream = NULL;
  GList *rep_list = NULL;
  gint new_index;
  GstDashDemux *demux = GST_DASH_DEMUX_CAST (stream->demux);
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  gboolean ret = FALSE;

  active_stream = dashstream->active_stream;
  if (active_stream == NULL) {
    goto end;
  }

  /* retrieve representation list */
  if (active_stream->cur_adapt_set)
    rep_list = active_stream->cur_adapt_set->Representations;
  if (!rep_list) {
    goto end;
  }

  GST_DEBUG_OBJECT (stream->pad,
      "Trying to change to bitrate: %" G_GUINT64_FORMAT, bitrate);

  /* get representation index with current max_bandwidth */
  new_index = gst_mpdparser_get_rep_idx_with_max_bandwidth (rep_list, bitrate);

  /* if no representation has the required bandwidth, take the lowest one */
  if (new_index == -1)
    new_index = gst_mpdparser_get_rep_idx_with_min_bandwidth (rep_list);

  if (new_index != active_stream->representation_idx) {
    GstRepresentationNode *rep = g_list_nth_data (rep_list, new_index);
    GST_INFO_OBJECT (demux, "Changing representation idx: %d %d %u",
        dashstream->index, new_index, rep->bandwidth);
    if (gst_mpd_client_setup_representation (demux->client, active_stream, rep)) {
      GstCaps *caps;

      GST_INFO_OBJECT (demux, "Switching bitrate to %d",
          active_stream->cur_representation->bandwidth);
      caps = gst_dash_demux_get_input_caps (demux, active_stream);
      gst_adaptive_demux_stream_set_caps (stream, caps);
      ret = TRUE;

    } else {
      GST_WARNING_OBJECT (demux, "Can not switch representation, aborting...");
    }
  }

  if (gst_mpd_client_has_isoff_ondemand_profile (demux->client)) {

    /* store our current position to change to the same one in a different
     * representation if needed */
    dashstream->sidx_index = SIDX (dashstream)->entry_index;
    if (ret) {
      /* TODO cache indexes to avoid re-downloading and parsing */
      /* if we switched, we need a new index */
      gst_isoff_sidx_parser_clear (&dashstream->sidx_parser);
      gst_isoff_sidx_parser_init (&dashstream->sidx_parser);
    }
  }

end:
  return ret;
}

static gboolean
gst_dash_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  GList *list;
  GstClockTime current_pos, target_pos;
  guint current_period;
  GstStreamPeriod *period;
  GList *iter;
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);

  gst_event_parse_seek (seek, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  /* TODO check if start-type/stop-type is SET */
  if (demux->segment.rate > 0.0)
    target_pos = (GstClockTime) demux->segment.start;
  else
    target_pos = (GstClockTime) demux->segment.stop;

  /* select the requested Period in the Media Presentation */
  current_period = 0;
  for (list = g_list_first (dashdemux->client->periods); list;
      list = g_list_next (list)) {
    period = list->data;
    current_pos = period->start;
    current_period = period->number;
    GST_DEBUG_OBJECT (demux, "Looking at period %u pos %" GST_TIME_FORMAT,
        current_period, GST_TIME_ARGS (current_pos));
    if (current_pos <= target_pos
        && target_pos < current_pos + period->duration) {
      break;
    }
  }
  if (list == NULL) {
    GST_WARNING_OBJECT (demux, "Could not find seeked Period");
    return FALSE;
  }
  if (current_period != gst_mpd_client_get_period_index (dashdemux->client)) {
    GST_DEBUG_OBJECT (demux, "Seeking to Period %d", current_period);

    /* clean old active stream list, if any */
    gst_active_streams_free (dashdemux->client);

    /* setup video, audio and subtitle streams, starting from the new Period */
    if (!gst_mpd_client_set_period_index (dashdemux->client, current_period)
        || !gst_dash_demux_setup_all_streams (dashdemux))
      return FALSE;
  }

  /* Update the current sequence on all streams */
  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstDashDemuxStream *dashstream = iter->data;

    if (flags & GST_SEEK_FLAG_FLUSH) {
      gst_isoff_sidx_parser_clear (&dashstream->sidx_parser);
      gst_isoff_sidx_parser_init (&dashstream->sidx_parser);
    }
    gst_dash_demux_stream_seek (iter->data, target_pos);
  }
  return TRUE;
}

static gint64
gst_dash_demux_get_manifest_update_interval (GstAdaptiveDemux * demux)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);
  return dashdemux->client->mpd_node->minimumUpdatePeriod * 1000;
}

static GstFlowReturn
gst_dash_demux_update_manifest_data (GstAdaptiveDemux * demux,
    GstBuffer * buffer)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);
  GstMpdClient *new_client = NULL;
  GstMapInfo mapinfo;

  GST_DEBUG_OBJECT (demux, "Updating manifest file from URL");

  /* parse the manifest file */
  new_client = gst_mpd_client_new ();
  new_client->mpd_uri = g_strdup (demux->manifest_uri);
  new_client->mpd_base_uri = g_strdup (demux->manifest_base_uri);
  gst_buffer_map (buffer, &mapinfo, GST_MAP_READ);

  if (gst_mpd_parse (new_client, (gchar *) mapinfo.data, mapinfo.size)) {
    const gchar *period_id;
    guint period_idx;
    GList *iter;
    GList *streams_iter;

    /* prepare the new manifest and try to transfer the stream position
     * status from the old manifest client  */

    GST_DEBUG_OBJECT (demux, "Updating manifest");

    period_id = gst_mpd_client_get_period_id (dashdemux->client);
    period_idx = gst_mpd_client_get_period_index (dashdemux->client);

    /* setup video, audio and subtitle streams, starting from current Period */
    if (!gst_mpd_client_setup_media_presentation (new_client)) {
      /* TODO */
    }

    if (period_id) {
      if (!gst_mpd_client_set_period_id (new_client, period_id)) {
        GST_DEBUG_OBJECT (demux, "Error setting up the updated manifest file");
        return GST_FLOW_EOS;
      }
    } else {
      if (!gst_mpd_client_set_period_index (new_client, period_idx)) {
        GST_DEBUG_OBJECT (demux, "Error setting up the updated manifest file");
        return GST_FLOW_EOS;
      }
    }

    if (!gst_dash_demux_setup_mpdparser_streams (dashdemux, new_client)) {
      GST_ERROR_OBJECT (demux, "Failed to setup streams on manifest " "update");
      return GST_FLOW_ERROR;
    }

    /* update the streams to play from the next segment */
    for (iter = demux->streams, streams_iter = new_client->active_streams;
        iter && streams_iter;
        iter = g_list_next (iter), streams_iter = g_list_next (streams_iter)) {
      GstDashDemuxStream *demux_stream = iter->data;
      GstActiveStream *new_stream = streams_iter->data;
      GstClockTime ts;

      if (!new_stream) {
        GST_DEBUG_OBJECT (demux,
            "Stream of index %d is missing from manifest update",
            demux_stream->index);
        return GST_FLOW_EOS;
      }

      if (gst_mpd_client_get_next_fragment_timestamp (dashdemux->client,
              demux_stream->index, &ts)) {
        gst_mpd_client_stream_seek (new_client, new_stream, ts);
      } else
          if (gst_mpd_client_get_last_fragment_timestamp (dashdemux->client,
              demux_stream->index, &ts)) {
        /* try to set to the old timestamp + 1 */
        gst_mpd_client_stream_seek (new_client, new_stream, ts + 1);
      }
      demux_stream->active_stream = new_stream;
    }

    gst_mpd_client_free (dashdemux->client);
    dashdemux->client = new_client;

    GST_DEBUG_OBJECT (demux, "Manifest file successfully updated");
  } else {
    /* In most cases, this will happen if we set a wrong url in the
     * source element and we have received the 404 HTML response instead of
     * the manifest */
    GST_WARNING_OBJECT (demux, "Error parsing the manifest.");
    gst_buffer_unmap (buffer, &mapinfo);
    return GST_FLOW_ERROR;
  }

  gst_buffer_unmap (buffer, &mapinfo);

  return GST_FLOW_OK;
}

static gint64
gst_dash_demux_stream_get_fragment_waiting_time (GstAdaptiveDemuxStream *
    stream)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (stream->demux);
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;
  GstDateTime *seg_end_time;
  GstActiveStream *active_stream = dashstream->active_stream;

  seg_end_time =
      gst_mpd_client_get_next_segment_availability_end_time (dashdemux->client,
      active_stream);

  if (seg_end_time) {
    gint64 diff;
    GstDateTime *cur_time;

    cur_time = gst_date_time_new_now_utc ();
    diff = gst_mpd_client_calculate_time_difference (cur_time, seg_end_time);
    gst_date_time_unref (seg_end_time);
    gst_date_time_unref (cur_time);
    return diff;
  }
  return 0;
}

static gboolean
gst_dash_demux_has_next_period (GstAdaptiveDemux * demux)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);

  if (demux->segment.rate >= 0)
    return gst_mpd_client_has_next_period (dashdemux->client);
  else
    return gst_mpd_client_has_previous_period (dashdemux->client);
}

static void
gst_dash_demux_advance_period (GstAdaptiveDemux * demux)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);

  g_return_if_fail (gst_mpd_client_has_next_period (dashdemux->client));

  if (demux->segment.rate >= 0) {
    if (!gst_mpd_client_set_period_index (dashdemux->client,
            gst_mpd_client_get_period_index (dashdemux->client) + 1)) {
      /* TODO error */
      return;
    }
  } else {
    if (!gst_mpd_client_set_period_index (dashdemux->client,
            gst_mpd_client_get_period_index (dashdemux->client) - 1)) {
      /* TODO error */
      return;
    }
  }

  gst_dash_demux_setup_all_streams (dashdemux);
  gst_mpd_client_set_segment_index_for_all_streams (dashdemux->client, 0);
}

static GstBuffer *
_gst_buffer_split (GstBuffer * buffer, gint offset, gsize size)
{
  GstBuffer *newbuf = gst_buffer_copy_region (buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META
      | GST_BUFFER_COPY_MEMORY, offset, size - offset);

  gst_buffer_resize (buffer, 0, offset);

  return newbuf;
}

static GstFlowReturn
gst_dash_demux_stream_fragment_finished (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstDashDemux *dashdemux = GST_DASH_DEMUX_CAST (demux);
  GstDashDemuxStream *dashstream = (GstDashDemuxStream *) stream;

  if (gst_mpd_client_has_isoff_ondemand_profile (dashdemux->client) &&
      dashstream->sidx_parser.status == GST_ISOFF_SIDX_PARSER_FINISHED) {
    /* fragment is advanced on data_received when byte limits are reached */
    return GST_FLOW_OK;
  } else {
    if (G_UNLIKELY (stream->downloading_header || stream->downloading_index))
      return GST_FLOW_OK;
    return gst_adaptive_demux_stream_advance_fragment (demux, stream,
        stream->fragment.duration);
  }
}

static GstFlowReturn
gst_dash_demux_data_received (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dash_stream = (GstDashDemuxStream *) stream;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer;
  gsize available;

  if (stream->downloading_index) {
    GstIsoffParserResult res;
    guint consumed;

    available = gst_adapter_available (stream->adapter);
    buffer = gst_adapter_take_buffer (stream->adapter, available);

    if (dash_stream->sidx_parser.status != GST_ISOFF_SIDX_PARSER_FINISHED) {
      res =
          gst_isoff_sidx_parser_add_buffer (&dash_stream->sidx_parser, buffer,
          &consumed);

      if (res == GST_ISOFF_PARSER_ERROR) {
      } else if (res == GST_ISOFF_PARSER_UNEXPECTED) {
        /* this is not a 'sidx' index, just skip it and continue playback */
      } else {
        /* when finished, prepare for real data streaming */
        if (dash_stream->sidx_parser.status == GST_ISOFF_SIDX_PARSER_FINISHED) {
          if (GST_CLOCK_TIME_IS_VALID (dash_stream->pending_seek_ts)) {
            gst_dash_demux_stream_sidx_seek (dash_stream,
                dash_stream->pending_seek_ts);
            dash_stream->pending_seek_ts = GST_CLOCK_TIME_NONE;
          } else {
            SIDX (dash_stream)->entry_index = dash_stream->sidx_index;
          }
          dash_stream->sidx_current_remaining =
              SIDX_CURRENT_ENTRY (dash_stream)->size;
        } else if (consumed < available) {
          GstBuffer *pending;
          /* we still need to keep some data around for the next parsing round
           * so just push what was already processed by the parser */
          pending = _gst_buffer_split (buffer, consumed, -1);
          gst_adapter_push (stream->adapter, pending);
        }
      }
    }
    ret = gst_adaptive_demux_stream_push_buffer (stream, buffer);
  } else if (dash_stream->sidx_parser.status == GST_ISOFF_SIDX_PARSER_FINISHED) {

    while (ret == GST_FLOW_OK
        && ((available = gst_adapter_available (stream->adapter)) > 0)) {
      gboolean advance = FALSE;

      if (available < dash_stream->sidx_current_remaining) {
        buffer = gst_adapter_take_buffer (stream->adapter, available);
        dash_stream->sidx_current_remaining -= available;
      } else {
        buffer =
            gst_adapter_take_buffer (stream->adapter,
            dash_stream->sidx_current_remaining);
        dash_stream->sidx_current_remaining = 0;
        advance = TRUE;
      }
      ret = gst_adaptive_demux_stream_push_buffer (stream, buffer);
      if (advance) {
        GstFlowReturn new_ret;
        new_ret =
            gst_adaptive_demux_stream_advance_fragment (demux, stream,
            SIDX_CURRENT_ENTRY (dash_stream)->duration);

        /* only overwrite if it was OK before */
        if (ret == GST_FLOW_OK)
          ret = new_ret;
      }
    }
  } else {
    /* this should be the main header, just push it all */
    ret =
        gst_adaptive_demux_stream_push_buffer (stream,
        gst_adapter_take_buffer (stream->adapter,
            gst_adapter_available (stream->adapter)));
  }

  return ret;
}

static void
gst_dash_demux_stream_free (GstAdaptiveDemuxStream * stream)
{
  GstDashDemuxStream *dash_stream = (GstDashDemuxStream *) stream;

  gst_isoff_sidx_parser_clear (&dash_stream->sidx_parser);
}
