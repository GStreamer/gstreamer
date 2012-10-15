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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include "gstdashdemux.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/xml"));

GST_DEBUG_CATEGORY_STATIC (gst_dash_demux_debug);
#define GST_CAT_DEFAULT gst_dash_demux_debug

enum
{
  PROP_0,

  PROP_MIN_BUFFERING_TIME,
  PROP_MAX_BUFFERING_TIME,
  PROP_BANDWIDTH_USAGE,
  PROP_MAX_BITRATE,
  PROP_LAST
};

/* Default values for properties */
#define DEFAULT_MIN_BUFFERING_TIME	5       /* in seconds */
#define DEFAULT_MAX_BUFFERING_TIME	30      /* in seconds */
#define DEFAULT_BANDWIDTH_USAGE		0.8     /* 0 to 1     */
#define DEFAULT_MAX_BITRATE	24000000        /* in Mbit/s  */

#define DEFAULT_FAILED_COUNT 3


/* GObject */
static void gst_dash_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dash_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_dash_demux_dispose (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_dash_demux_change_state (GstElement * element, GstStateChange transition);

/* GstDashDemux */
static GstFlowReturn gst_dash_demux_pad (GstPad * pad, GstBuffer * buf);
static gboolean gst_dash_demux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_dash_demux_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_dash_demux_src_query (GstPad * pad, GstQuery * query);
static void gst_dash_demux_stream_loop (GstDashDemux * demux);
static void gst_dash_demux_download_loop (GstDashDemux * demux);
static void gst_dash_demux_stop (GstDashDemux * demux);
static void gst_dash_demux_pause_stream_task (GstDashDemux * demux);
static void gst_dash_demux_resume_stream_task (GstDashDemux * demux);
static void gst_dash_demux_resume_download_task (GstDashDemux * demux);
static gboolean gst_dash_demux_schedule (GstDashDemux * demux);
static gboolean gst_dash_demux_select_representation (GstDashDemux * demux,
    guint64 current_bitrate);
static gboolean gst_dash_demux_get_next_fragment (GstDashDemux * demux,
    gboolean caching);

static void gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose);
static GstClockTime gst_dash_demux_get_buffering_time (GstDashDemux * demux);
static float gst_dash_demux_get_buffering_ratio (GstDashDemux * demux);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "dashdemux", 0,
      "dashdemux element");
}

GST_BOILERPLATE_FULL (GstDashDemux, gst_dash_demux, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_dash_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class,
      "DASH Demuxer",
      "Codec/Demuxer",
      "Dynamic Adaptive Streaming over HTTP demuxer",
      "David Corvoysier <david.corvoysier@orange.com>\n\
                Hamid Zakari <hamid.zakari@gmail.com>\n\
                Gianluca Gennari <gennarone@gmail.com>");
}

static void
gst_dash_demux_dispose (GObject * obj)
{
  GstDashDemux *demux = GST_DASH_DEMUX (obj);

  if (demux->stream_task) {
    if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED) {
      GST_DEBUG_OBJECT (demux, "Leaving streaming task");
      gst_task_stop (demux->stream_task);
      gst_task_join (demux->stream_task);
    }
    gst_object_unref (demux->stream_task);
    g_static_rec_mutex_free (&demux->stream_lock);
    demux->stream_task = NULL;
  }

  if (demux->download_task) {
    if (GST_TASK_STATE (demux->download_task) != GST_TASK_STOPPED) {
      GST_DEBUG_OBJECT (demux, "Leaving download task");
      gst_task_stop (demux->download_task);
      gst_task_join (demux->download_task);
    }
    gst_object_unref (demux->download_task);
    g_static_rec_mutex_free (&demux->download_lock);
    demux->download_task = NULL;
  }

  if (demux->downloader != NULL) {
    g_object_unref (demux->downloader);
    demux->downloader = NULL;
  }

  gst_dash_demux_reset (demux, TRUE);

  g_queue_free (demux->queue);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_dash_demux_class_init (GstDashDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dash_demux_set_property;
  gobject_class->get_property = gst_dash_demux_get_property;
  gobject_class->dispose = gst_dash_demux_dispose;

  g_object_class_install_property (gobject_class, PROP_MIN_BUFFERING_TIME,
      g_param_spec_uint ("min-buffering-time", "Minimum buffering time",
          "Minimum number of seconds of buffer accumulated before playback",
          1, G_MAXUINT, DEFAULT_MIN_BUFFERING_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BUFFERING_TIME,
      g_param_spec_uint ("max-buffering-time", "Maximum buffering time",
          "Maximum number of seconds of buffer accumulated during playback",
          2, G_MAXUINT, DEFAULT_MAX_BUFFERING_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BANDWIDTH_USAGE,
      g_param_spec_float ("bandwidth-usage",
          "Bandwidth usage [0..1]",
          "Percentage of the available bandwidth to use when selecting representations",
          0, 1, DEFAULT_BANDWIDTH_USAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max bitrate",
          "Max of bitrate supported by target decoder",
          1000, G_MAXUINT, DEFAULT_MAX_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dash_demux_change_state);
}

static void
gst_dash_demux_init (GstDashDemux * demux, GstDashDemuxClass * klass)
{
  /* sink pad */
  demux->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dash_demux_pad));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dash_demux_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* Downloader */
  demux->downloader = gst_uri_downloader_new ();

  /* Properties */
  demux->min_buffering_time = DEFAULT_MIN_BUFFERING_TIME * GST_SECOND;
  demux->max_buffering_time = DEFAULT_MAX_BUFFERING_TIME * GST_SECOND;
  demux->bandwidth_usage = DEFAULT_BANDWIDTH_USAGE;
  demux->max_bitrate = DEFAULT_MAX_BITRATE;

  demux->queue = g_queue_new ();
  /* Updates task */
  g_static_rec_mutex_init (&demux->download_lock);
  demux->download_task =
      gst_task_create ((GstTaskFunction) gst_dash_demux_download_loop, demux);
  gst_task_set_lock (demux->download_task, &demux->download_lock);
  demux->download_timed_lock = g_mutex_new ();

  /* Streaming task */
  g_static_rec_mutex_init (&demux->stream_lock);
  demux->stream_task =
      gst_task_create ((GstTaskFunction) gst_dash_demux_stream_loop, demux);
  gst_task_set_lock (demux->stream_task, &demux->stream_lock);
  demux->stream_timed_lock = g_mutex_new ();
}

static void
gst_dash_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDashDemux *demux = GST_DASH_DEMUX (object);

  switch (prop_id) {
    case PROP_MIN_BUFFERING_TIME:
      demux->min_buffering_time = g_value_get_uint (value) * GST_SECOND;
      break;
    case PROP_MAX_BUFFERING_TIME:
      demux->max_buffering_time = g_value_get_uint (value) * GST_SECOND;
      break;
    case PROP_BANDWIDTH_USAGE:
      demux->bandwidth_usage = g_value_get_float (value);
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
  GstDashDemux *demux = GST_DASH_DEMUX (object);

  switch (prop_id) {
    case PROP_MIN_BUFFERING_TIME:
      g_value_set_uint (value, demux->min_buffering_time);
      demux->min_buffering_time *= GST_SECOND;
      break;
    case PROP_MAX_BUFFERING_TIME:
      g_value_set_uint (value, demux->max_buffering_time);
      demux->max_buffering_time *= GST_SECOND;
      break;
    case PROP_BANDWIDTH_USAGE:
      g_value_set_float (value, demux->bandwidth_usage);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, demux->max_bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_dash_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDashDemux *demux = GST_DASH_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_dash_demux_reset (demux, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* Start the streaming loop in paused only if we already received
         the manifest. It might have been stopped if we were in PAUSED
         state and we filled our queue with enough cached fragments
       */
      if (gst_mpdparser_get_baseURL (demux->client) != NULL)
        gst_dash_demux_resume_stream_task (demux);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_dash_demux_pause_stream_task (demux);
      gst_task_pause (demux->stream_task);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      demux->cancelled = TRUE;
      gst_dash_demux_stop (demux);
      gst_task_join (demux->stream_task);
      gst_dash_demux_reset (demux, FALSE);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_dash_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstDashDemux *demux;

  demux = GST_DASH_DEMUX (gst_pad_get_element_private (pad));

  switch (event->type) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GList *walk;
      GstClockTime current_pos, target_pos;
      gint current_sequence;
      GstActiveStream *stream;
      GstMediaSegment *chunk;
      guint nb_active_stream;
      guint stream_idx;

      GST_INFO_OBJECT (demux, "Received GST_EVENT_SEEK");

      if (gst_mpd_client_is_live (demux->client)) {
        GST_WARNING_OBJECT (demux, "Received seek event for live stream");
        return FALSE;
      }

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      nb_active_stream = gst_mpdparser_get_nb_active_stream (demux->client);
      gst_task_stop (demux->download_task);
      GST_DEBUG_OBJECT (demux, "seek event, rate: %f start: %" GST_TIME_FORMAT
          " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      GST_MPD_CLIENT_LOCK (demux->client);
      stream =
          g_list_nth_data (demux->client->active_streams,
          demux->client->stream_idx);

      current_pos = 0;
      target_pos = (GstClockTime) start;
      for (walk = stream->segments; walk; walk = walk->next) {
        chunk = walk->data;
        current_sequence = chunk->number;
        if (current_pos <= target_pos
            && target_pos < current_pos + chunk->duration) {
          break;
        }
        current_pos += chunk->duration;
      }
      GST_MPD_CLIENT_UNLOCK (demux->client);

      if (walk == NULL) {
        gst_dash_demux_resume_stream_task (demux);
        gst_dash_demux_resume_download_task (demux);
        GST_WARNING_OBJECT (demux, "Could not find seeked fragment");
        return FALSE;
      }


      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush start");
        stream_idx = 0;
        while (stream_idx < nb_active_stream) {
          gst_pad_push_event (demux->srcpad[stream_idx],
              gst_event_new_flush_start ());
          stream_idx++;
        }
      }

      demux->cancelled = TRUE;
      gst_dash_demux_pause_stream_task (demux);
      gst_uri_downloader_cancel (demux->downloader);

      /* wait for streaming to finish */
      g_static_rec_mutex_lock (&demux->stream_lock);

      while (!g_queue_is_empty (demux->queue)) {
        GList *listfragment = g_queue_pop_head (demux->queue);
        guint j = 0;
        while (j < g_list_length (listfragment)) {
          GstFragment *fragment = g_list_nth_data (listfragment, j);
          g_object_unref (fragment);
          j++;
        }
      }
      g_queue_clear (demux->queue);

      GST_MPD_CLIENT_LOCK (demux->client);
      GST_DEBUG_OBJECT (demux, "seeking to sequence %d", current_sequence);
      stream_idx = 0;
      while (stream_idx < nb_active_stream) {
        stream =
            gst_mpdparser_get_active_stream_by_index (demux->client,
            stream_idx);
        stream->segment_idx = current_sequence;
        stream_idx++;
      }
      gst_mpd_client_get_current_position (demux->client, &demux->position);
      demux->position_shift = start - demux->position;
      demux->need_segment = TRUE;
      GST_MPD_CLIENT_UNLOCK (demux->client);


      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush stop on all pad");
        stream_idx = 0;
        while (stream_idx < nb_active_stream) {
          gst_pad_push_event (demux->srcpad[stream_idx],
              gst_event_new_flush_stop ());
          stream_idx++;
        }
      }

      demux->cancelled = FALSE;
      gst_dash_demux_resume_download_task (demux);
      gst_dash_demux_resume_stream_task (demux);
      g_static_rec_mutex_unlock (&demux->stream_lock);

      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static gboolean
gst_dash_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstDashDemux *demux = GST_DASH_DEMUX (gst_pad_get_parent (pad));

  switch (event->type) {
    case GST_EVENT_EOS:{
      gchar *manifest;
      GstQuery *query;
      gboolean res;

      if (demux->manifest == NULL) {
        GST_WARNING_OBJECT (demux, "Received EOS without a manifest.");
        break;
      }

      GST_DEBUG_OBJECT (demux,
          "Got EOS on the sink pad: manifest fetched");

      if (demux->client)
        gst_mpd_client_free (demux->client);
      demux->client = gst_mpd_client_new ();

      query = gst_query_new_uri ();
      res = gst_pad_peer_query (pad, query);
      if (res) {
        gst_query_parse_uri (query, &demux->client->mpd_uri);
        GST_DEBUG_OBJECT (demux, "Fetched MPD file at URI: %s",
            demux->client->mpd_uri);
      } else {
        GST_WARNING_OBJECT (demux, "MPD URI query failed.");
      }
      gst_query_unref (query);

      manifest = (gchar *) GST_BUFFER_DATA (demux->manifest);
      if (manifest == NULL) {
        GST_WARNING_OBJECT (demux, "Error validating the manifest.");
      } else if (!gst_mpd_parse (demux->client, manifest,
              GST_BUFFER_SIZE (demux->manifest))) {
        /* In most cases, this will happen if we set a wrong url in the
         * source element and we have received the 404 HTML response instead of
         * the manifest */
        GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid manifest."),
            (NULL));
        return FALSE;
      }
      gst_buffer_unref (demux->manifest);
      demux->manifest = NULL;

      if (!gst_mpd_client_setup_streaming (demux->client, GST_STREAM_VIDEO, "")) {
        GST_ELEMENT_ERROR (demux, STREAM, DECODE,
            ("Incompatible manifest file."), (NULL));
        return FALSE;
      }

      GList *listLang = NULL;
      guint nb_audio =
          gst_mpdparser_get_list_and_nb_of_audio_language (&listLang,
          demux->client->cur_period->AdaptationSets);
      if (nb_audio == 0)
        nb_audio = 1;
      GST_INFO_OBJECT (demux, "Number of language is=%d", nb_audio);
      guint i = 0;
      for (i = 0; i < nb_audio; i++) {
        gchar *lang = (gchar *) g_list_nth_data (listLang, i);
        if (gst_mpdparser_get_nb_adaptationSet (demux->client) > 1)
          if (!gst_mpd_client_setup_streaming (demux->client, GST_STREAM_AUDIO,
                  lang))
            GST_INFO_OBJECT (demux, "No audio adaptation set found");

        if (gst_mpdparser_get_nb_adaptationSet (demux->client) > nb_audio)
          if (!gst_mpd_client_setup_streaming (demux->client,
                  GST_STREAM_APPLICATION, lang)) {
            GST_INFO_OBJECT (demux, "No application adaptation set found");
          }
      }

      /* Send duration message */
      if (!gst_mpd_client_is_live (demux->client)) {
        GstClockTime duration = gst_mpd_client_get_duration (demux->client);

        GST_DEBUG_OBJECT (demux, "Sending duration message : %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration));
        if (duration != GST_CLOCK_TIME_NONE)
          gst_element_post_message (GST_ELEMENT (demux),
              gst_message_new_duration (GST_OBJECT (demux),
                  GST_FORMAT_TIME, duration));
      }
      gst_dash_demux_resume_download_task (demux);
      gst_dash_demux_resume_stream_task (demux);
      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_NEWSEGMENT:
      /* Swallow newsegments, we'll push our own */
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static gboolean
gst_dash_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstDashDemux *dashdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  dashdemux = GST_DASH_DEMUX (gst_pad_get_element_private (pad));

  switch (query->type) {
    case GST_QUERY_DURATION:{
      GstClockTime duration = -1;
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME) {
        duration = gst_mpd_client_get_duration (dashdemux->client);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          ret = TRUE;
        }
      }
      GST_DEBUG_OBJECT (dashdemux,
          "GST_QUERY_DURATION returns %s with duration %" GST_TIME_FORMAT,
          ret ? "TRUE" : "FALSE", GST_TIME_ARGS (duration));
      break;
    }
    case GST_QUERY_URI:
      if (dashdemux->client) {
        const gchar *initializationURL;
        gchar *header_uri;
        /* GG: I would answer with the URI of the initialization segment, or,
         * if there is no initialization segment, with the URI of the first segment
         * as this are usually mp4 files */
        if (!gst_mpd_client_get_next_header (dashdemux->client,
                &initializationURL, 0)) {
          if (strncmp (initializationURL, "http://", 7) != 0) {
            header_uri =
                g_strconcat (gst_mpdparser_get_baseURL (dashdemux->client),
                initializationURL, NULL);
          } else {
            header_uri = g_strdup (initializationURL);
          }
          gst_query_set_uri (query, header_uri);
          g_free (header_uri);
          ret = TRUE;
        } else {
          ret = FALSE;
        }
      }
      break;
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint64 stop = -1;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      GST_DEBUG_OBJECT (dashdemux, "Received GST_QUERY_SEEKING with format %d",
          fmt);
      if (fmt == GST_FORMAT_TIME) {
        GstClockTime duration;

        duration = gst_mpd_client_get_duration (dashdemux->client);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
          stop = duration;

        gst_query_set_seeking (query, fmt,
            !gst_mpd_client_is_live (dashdemux->client), 0, stop);
        ret = TRUE;
        GST_DEBUG_OBJECT (dashdemux, "GST_QUERY_SEEKING returning with stop : %"
            GST_TIME_FORMAT, GST_TIME_ARGS (stop));
      }
      break;
    }
    default:
      /* Don't fordward queries upstream because of the special nature of this
       * "demuxer", which relies on the upstream element only to be fed with the
       * manifest file */
      break;
  }

  return ret;
}

static GstFlowReturn
gst_dash_demux_pad (GstPad * pad, GstBuffer * buf)
{
  GstDashDemux *demux = GST_DASH_DEMUX (gst_pad_get_parent (pad));

  if (demux->manifest == NULL)
    demux->manifest = buf;
  else
    demux->manifest = gst_buffer_join (demux->manifest, buf);

  gst_object_unref (demux);

  return GST_FLOW_OK;
}

static void
gst_dash_demux_stop (GstDashDemux * demux)
{
  gst_uri_downloader_cancel (demux->downloader);

  if (GST_TASK_STATE (demux->download_task) != GST_TASK_STOPPED) {
    demux->stop_stream_task = TRUE;
    gst_task_stop (demux->download_task);
    GST_TASK_SIGNAL (demux->download_task);
  }

  if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED)
    gst_task_stop (demux->stream_task);
}

static void
switch_pads (GstDashDemux * demux, guint nb_adaptation_set)
{
  GstPad *oldpad[MAX_LANGUAGES];
  guint i = 0;
  while (i < nb_adaptation_set) {
    oldpad[i] = demux->srcpad[i];
    if (oldpad[i]) {
      GST_DEBUG_OBJECT (demux,
          "Switching pads (oldpad:%p)" GST_PTR_FORMAT, oldpad[i]);
    }
    i++;
  }
  /* First create and activate new pad */
  i = 0;
  while (i < nb_adaptation_set) {
    demux->srcpad[i] = gst_pad_new_from_static_template (&srctemplate, NULL);
    gst_pad_set_event_function (demux->srcpad[i],
        GST_DEBUG_FUNCPTR (gst_dash_demux_src_event));
    gst_pad_set_query_function (demux->srcpad[i],
        GST_DEBUG_FUNCPTR (gst_dash_demux_src_query));
    gst_pad_set_element_private (demux->srcpad[i], demux);
    gst_pad_set_active (demux->srcpad[i], TRUE);
    gst_pad_set_caps (demux->srcpad[i], demux->output_caps[i]);
    gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad[i]);
    i++;
  }
  gst_element_no_more_pads (GST_ELEMENT (demux));
  /* Push out EOS and remove the last chain/group */
  i = 0;
  while (i < nb_adaptation_set) {
    if (oldpad[i]) {
      gst_pad_push_event (oldpad[i], gst_event_new_eos ());
      gst_pad_set_active (oldpad[i], FALSE);
      gst_element_remove_pad (GST_ELEMENT (demux), oldpad[i]);
    }
    i++;
  }
}

static gboolean
needs_pad_switch (GstDashDemux * demux, GList * fragment)
{

  gboolean switch_pad = FALSE;
  guint i = 0;
  while (i < g_list_length (fragment)) {
    GstFragment *newFragment = g_list_nth_data (fragment, i);
    if (newFragment == NULL) {
      continue;
    }
    GstCaps *srccaps = NULL;
    demux->output_caps[i] = gst_fragment_get_caps (newFragment);
    if (G_LIKELY (demux->srcpad[i]))
      srccaps = gst_pad_get_negotiated_caps (demux->srcpad[i]);
    if (G_UNLIKELY (!srccaps
            || (!gst_caps_is_equal_fixed (demux->output_caps[i], srccaps))
            || demux->need_segment)) {
      switch_pad = TRUE;
      GST_INFO_OBJECT (demux, "Switch pad i =%d", i);
    }
    if (G_LIKELY (srccaps))
      gst_caps_unref (srccaps);
    i++;
  }
  return switch_pad;
}


static void
gst_dash_demux_stream_loop (GstDashDemux * demux)
{
  GList *listfragment;
  GstFlowReturn ret;
  GstBufferList *buffer_list;
  guint nb_adaptation_set = 0;
  GstActiveStream *stream;
  /* Loop for the source pad task.
   * 
   * Startup: 
   * The task is started as soon as we have received the manifest and
   * waits for the first fragment to be downloaded and pushed in the
   * queue. Once this fragment has been pushed, the task pauses itself
   * until actual playback begins.
   * 
   * During playback:  
   * The task pushes fragments downstream at regular intervals based on
   * the fragment duration. If it detects a queue underrun, it sends
   * a buffering event to tell the main application to pause.
   * 
   * Teardown:
   * The task is stopped when we reach the end of the manifest */

  /* Wait until the next scheduled push downstream */
  if (g_cond_timed_wait (GST_TASK_GET_COND (demux->stream_task),
          demux->stream_timed_lock, &demux->next_stream)) {
    goto pause_task;
  }

  if (g_queue_is_empty (demux->queue)) {
    if (demux->end_of_manifest)
      goto end_of_manifest;

    return;
  }

  if (GST_STATE (demux) == GST_STATE_PLAYING) {
    if (!demux->end_of_manifest
        && gst_dash_demux_get_buffering_time (demux) <
        demux->min_buffering_time) {
      /* Warn we are below our threshold: this will eventually pause 
       * the pipeline */
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_buffering (GST_OBJECT (demux),
              100 * gst_dash_demux_get_buffering_ratio (demux)));
    }
  }
  listfragment = g_queue_pop_head (demux->queue);
  nb_adaptation_set = g_list_length (listfragment);
  /* Figure out if we need to create/switch pads */
  gboolean switch_pad = needs_pad_switch (demux, listfragment);
  if (switch_pad) {
    switch_pads (demux, nb_adaptation_set);
    demux->need_segment = TRUE;
  }
  guint i = 0;
  for (i = 0; i < nb_adaptation_set; i++) {
    GstFragment *fragment = g_list_nth_data (listfragment, i);
    stream = gst_mpdparser_get_active_stream_by_index (demux->client, i);
    if (demux->need_segment) {
      GstClockTime start = fragment->start_time + demux->position_shift;
      /* And send a newsegment */
      GST_DEBUG_OBJECT (demux, "Sending new-segment. segment start:%"
          GST_TIME_FORMAT, GST_TIME_ARGS (start));
      gst_pad_push_event (demux->srcpad[i],
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
              start, GST_CLOCK_TIME_NONE, start));
      demux->need_segment = FALSE;
      demux->position_shift = 0;
    }

    GST_DEBUG_OBJECT (demux, "Pushing fragment #%d", fragment->index);
    buffer_list = gst_fragment_get_buffer_list (fragment);
    g_object_unref (fragment);
    ret = gst_pad_push_list (demux->srcpad[i], buffer_list);
    if ((ret != GST_FLOW_OK) && (stream->mimeType == GST_STREAM_VIDEO))
      goto error_pushing;
  }
  if (GST_STATE (demux) == GST_STATE_PLAYING) {
    /* Schedule the next push */
    gst_dash_demux_schedule (demux);
  } else {
    /* The pipeline is now set up, wait until playback begins */
    goto pause_task;
  }

  return;

end_of_manifest:
  {
    GST_DEBUG_OBJECT (demux, "Reached end of manifest, sending EOS");
    guint i = 0;
    for (i = 0; i < nb_adaptation_set; i++) {
      gst_pad_push_event (demux->srcpad[i], gst_event_new_eos ());
    }
    gst_dash_demux_stop (demux);
    return;
  }

error_pushing:
  {
    /* FIXME: handle error */
    GST_DEBUG_OBJECT (demux, "Error pushing buffer: %s... stopping task",
        gst_flow_get_name (ret));
    gst_dash_demux_stop (demux);
    return;
  }

pause_task:
  {
    gst_task_pause (demux->stream_task);
    return;
  }
}

static void
gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose)
{
  demux->end_of_manifest = FALSE;
  demux->cancelled = FALSE;

  guint i = 0;
  for (i = 0; i < MAX_LANGUAGES; i++)
    if (demux->input_caps[i]) {
      gst_caps_unref (demux->input_caps[i]);
      demux->input_caps[i] = NULL;
    }

  if (demux->manifest) {
    gst_buffer_unref (demux->manifest);
    demux->manifest = NULL;
  }
  if (demux->client) {
    gst_mpd_client_free (demux->client);
    demux->client = NULL;
  }
  if (!dispose) {
    demux->client = gst_mpd_client_new ();
  }

  while (!g_queue_is_empty (demux->queue)) {
    GList *listfragment = g_queue_pop_head (demux->queue);
    guint j = 0;
    while (j < g_list_length (listfragment)) {
      GstFragment *fragment = g_list_nth_data (listfragment, j);
      g_object_unref (fragment);
      j++;
    }
  }
  g_queue_clear (demux->queue);

  demux->position = 0;
  demux->position_shift = 0;
  demux->need_segment = TRUE;
}

static GstClockTime
gst_dash_demux_get_buffering_time (GstDashDemux * demux)
{
  return (g_queue_get_length (demux->queue)) *
      gst_mpd_client_get_target_duration (demux->client);
}

static float
gst_dash_demux_get_buffering_ratio (GstDashDemux * demux)
{
  float buffering_time = gst_dash_demux_get_buffering_time (demux);
  if (buffering_time >= demux->min_buffering_time) {
    return 1.0;
  } else
    return buffering_time / demux->min_buffering_time;
}

void
gst_dash_demux_download_loop (GstDashDemux * demux)
{
  /* Loop for downloading the fragments. It's started from the stream
   * loop, and fetches new fragments to maintain the number of queued
   * items within a predefined range. When a new fragment is downloaded,
   *  it evaluates the download time to check if we can or should
   * switch to a different bitrate */

  /* Wait until the next scheduled download */
  if (g_cond_timed_wait (GST_TASK_GET_COND (demux->download_task),
          demux->download_timed_lock, &demux->next_download)) {
    goto quit;
  }

  /* Target buffering time MUST at least exceeds mimimum buffering time 
   * by the duration of a fragment, but SHOULD NOT exceed maximum
   * buffering time */
  GstClockTime target_buffering_time =
      demux->min_buffering_time +
      gst_mpd_client_get_target_duration (demux->client);
  if (demux->max_buffering_time > target_buffering_time)
    target_buffering_time = demux->max_buffering_time;
  if (!demux->end_of_manifest
      && gst_dash_demux_get_buffering_time (demux) < target_buffering_time) {
    if (GST_STATE (demux) != GST_STATE_PLAYING) {
      /* Signal our buffering status (this will eventually restart the
       * pipeline when we have reached 100 %) */
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_buffering (GST_OBJECT (demux),
              100 * gst_dash_demux_get_buffering_ratio (demux)));
    }

    /* fetch the next fragment */
    /* try to switch to another bitrate if needed */
    gst_dash_demux_select_representation (demux,
        demux->bandwidth_usage * demux->dnl_rate *
        gst_dash_demux_get_buffering_ratio (demux));

    if (!gst_dash_demux_get_next_fragment (demux, FALSE)) {
      if (!demux->end_of_manifest && !demux->cancelled) {
        demux->client->update_failed_count++;
        if (demux->client->update_failed_count < DEFAULT_FAILED_COUNT) {
          GST_WARNING_OBJECT (demux, "Could not fetch the next fragment");
          return;
        } else {
          GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
              ("Could not fetch the next fragment"), (NULL));
          goto quit;
        }
      }
    } else {
      GST_INFO_OBJECT (demux, "Internal buffering : %d s",
          gst_dash_demux_get_buffering_time (demux) / GST_SECOND);
      demux->client->update_failed_count = 0;
    }
  } else {
    /* schedule the next download in 100 ms */
    g_get_current_time (&demux->next_download);
    g_time_val_add (&demux->next_download, 100000);
  }
  return;
quit:
  {
    GST_DEBUG_OBJECT (demux, "Stopped download task");
    gst_dash_demux_stop (demux);
  }
}

static void
gst_dash_demux_pause_stream_task (GstDashDemux * demux)
{
  /* Send a signal to the stream task so that it pauses itself */
  GST_TASK_SIGNAL (demux->stream_task);
  /* Pause it explicitly (if it was not in the COND) */
  gst_task_pause (demux->stream_task);
}

static void
gst_dash_demux_resume_stream_task (GstDashDemux * demux)
{
  g_get_current_time (&demux->next_stream);
  gst_task_start (demux->stream_task);
}

static void
gst_dash_demux_resume_download_task (GstDashDemux * demux)
{
  g_get_current_time (&demux->next_download);
  gst_task_start (demux->download_task);
}

static gboolean
gst_dash_demux_schedule (GstDashDemux * demux)
{
  /* schedule the next push */
  g_get_current_time (&demux->next_stream);
  g_time_val_add (&demux->next_stream,
      gst_mpd_client_get_target_duration (demux->client)
      / GST_SECOND * G_USEC_PER_SEC);
  GST_INFO_OBJECT (demux, "Next push scheduled at %s",
      g_time_val_to_iso8601 (&demux->next_stream));

  return TRUE;
}

/* gst_dash_demux_select_representation:
 *
 * Select the most appropriate media representation based on a target 
 * bitrate.
 * 
 * Returns TRUE if a new representation has been selected
 */
static gboolean
gst_dash_demux_select_representation (GstDashDemux * demux, guint64 bitrate)
{
  GstActiveStream *stream = NULL;
  GList *rep_list = NULL;
  gint new_index;
  gboolean ret = FALSE;

  guint i = 0;
  while (i < gst_mpdparser_get_nb_active_stream (demux->client)) {
    if (demux->client->active_streams)
      stream = g_list_nth_data (demux->client->active_streams, i);
    if (!stream)
      return FALSE;

    /* retrieve representation list */
    if (stream->cur_adapt_set)
      rep_list = stream->cur_adapt_set->Representations;
    if (!rep_list)
      return FALSE;

    /* get representation index with current max_bandwidth */
    new_index =
        gst_mpdparser_get_rep_idx_with_max_bandwidth (rep_list, bitrate);

    /* if no representation has the required bandwidth, take the lowest one */
    if (new_index == -1)
      new_index = 0;

    if (new_index != stream->representation_idx) {
      GST_MPD_CLIENT_LOCK (demux->client);
      ret =
          gst_mpd_client_setup_representation (demux->client, stream,
          g_list_nth_data (rep_list, new_index));
      GST_MPD_CLIENT_UNLOCK (demux->client);
      if (ret) {
        GST_INFO_OBJECT (demux, "Switching bitrate to %d",
            stream->cur_representation->bandwidth);
      } else {
        GST_WARNING_OBJECT (demux,
            "Can not switch representation, aborting...");
      }
    }
    i++;
  }
  return ret;
}

static GstFragment *
gst_dash_demux_get_next_header (GstDashDemux * demux, guint stream_idx)
{
  const gchar *next_header_uri, *initializationURL;

  if (!gst_mpd_client_get_next_header (demux->client, &initializationURL,
          stream_idx))
    return NULL;

  if (strncmp (initializationURL, "http://", 7) != 0) {
    next_header_uri =
        g_strconcat (gst_mpdparser_get_baseURL (demux->client),
        initializationURL, NULL);
  } else {
    next_header_uri = g_strdup (initializationURL);
  }

  GST_INFO_OBJECT (demux, "Fetching header %s", next_header_uri);

  return gst_uri_downloader_fetch_uri (demux->downloader, next_header_uri);
}

static GstBufferListItem
gst_dash_demux_add_buffer_cb (GstBuffer ** buffer,
    guint group, guint idx, gpointer user_data)
{
  GstFragment *frag = GST_FRAGMENT (user_data);
  /* This buffer still belongs to the original fragment */
  /* so we need to increase refcount */
  gst_fragment_add_buffer (frag, gst_buffer_ref (*buffer));
  return GST_BUFFER_LIST_CONTINUE;
}

/* Since we cannot add headers after the chunk has been downloaded, we have to recreate a new fragment */
static GstFragment *
gst_dash_demux_prepend_header (GstDashDemux * demux,
    GstFragment * frag, GstFragment * header)
{
  GstFragment *res = gst_fragment_new ();
  res->name = g_strdup (frag->name);
  res->download_start_time = frag->download_start_time;
  res->download_stop_time = frag->download_stop_time;
  res->start_time = frag->start_time;
  res->stop_time = frag->stop_time;
  res->index = frag->index;
  res->discontinuous = frag->discontinuous;

  GstBufferList *list;
  list = gst_fragment_get_buffer_list (header);
  gst_buffer_list_foreach (list, gst_dash_demux_add_buffer_cb, res);
  gst_buffer_list_unref (list);
  list = gst_fragment_get_buffer_list (frag);
  gst_buffer_list_foreach (list, gst_dash_demux_add_buffer_cb, res);
  gst_buffer_list_unref (list);

  res->completed = TRUE;

  return res;
}

const gchar *
gst_mpd_mimetype_to_caps (const gchar * mimeType)
{
  if (mimeType == NULL)
    return NULL;
  if (strcmp (mimeType, "video/mp2t") == 0) {
    return "video/mpegts";
  } else if (strcmp (mimeType, "video/mp4") == 0) {
    return "video/quicktime";
  } else if (strcmp (mimeType, "audio/mp4") == 0) {
    return "audio/x-m4a";
  } else
    return mimeType;
}

static GstCaps *
gst_dash_demux_get_video_input_caps (GstDashDemux * demux,
    GstActiveStream * stream)
{
  guint width, height;
  const gchar *mimeType;
  GstCaps *caps = NULL;
  GstRepresentationBaseType *RepresentationBase;
  if (stream == NULL)
    return NULL;

  if (stream->cur_representation->RepresentationBase) {
    RepresentationBase = stream->cur_representation->RepresentationBase;
  } else {
    RepresentationBase = stream->cur_adapt_set->RepresentationBase;
  }
  if (RepresentationBase == NULL)
    return NULL;

  width = gst_mpd_client_get_width_of_video_current_stream (RepresentationBase);
  height =
      gst_mpd_client_get_height_of_video_current_stream (RepresentationBase);
  mimeType = gst_mpd_mimetype_to_caps (RepresentationBase->mimeType);
  caps =
      gst_caps_new_simple (mimeType, "width", G_TYPE_INT, width, "height",
      G_TYPE_INT, height, NULL);
  return caps;
}

static GstCaps *
gst_dash_demux_get_audio_input_caps (GstDashDemux * demux,
    GstActiveStream * stream)
{
  guint rate, channels;
  const gchar *mimeType;
  GstCaps *caps = NULL;
  GstRepresentationBaseType *RepresentationBase;
  if (stream == NULL)
    return NULL;

  if (stream->cur_representation->RepresentationBase) {
    RepresentationBase = stream->cur_representation->RepresentationBase;
  } else {
    RepresentationBase = stream->cur_adapt_set->RepresentationBase;
  }
  if (RepresentationBase == NULL)
    return NULL;

  channels =
      gst_mpd_client_get_num_channels_of_audio_current_stream
      (RepresentationBase);
  rate = gst_mpd_client_get_rate_of_audio_current_stream (RepresentationBase);
  mimeType = gst_mpd_mimetype_to_caps (RepresentationBase->mimeType);
  caps =
      gst_caps_new_simple (mimeType, "channels", G_TYPE_INT, channels, "rate",
      G_TYPE_INT, rate, NULL);
  return caps;
}

static GstCaps *
gst_dash_demux_get_application_input_caps (GstDashDemux * demux,
    GstActiveStream * stream)
{
  const gchar *mimeType;
  GstCaps *caps = NULL;
  GstRepresentationBaseType *RepresentationBase;
  if (stream == NULL)
    return NULL;

  if (stream->cur_representation->RepresentationBase) {
    RepresentationBase = stream->cur_representation->RepresentationBase;
  } else {
    RepresentationBase = stream->cur_adapt_set->RepresentationBase;
  }
  if (RepresentationBase == NULL)
    return NULL;

  mimeType = gst_mpd_mimetype_to_caps (RepresentationBase->mimeType);
  caps = gst_caps_new_simple (mimeType, NULL);
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

static gboolean
need_add_header (GstDashDemux * demux)
{
  GstActiveStream *stream;
  guint stream_idx = 0;
  gboolean switch_caps = FALSE;
  while (stream_idx < gst_mpdparser_get_nb_active_stream (demux->client)) {
    stream =
        gst_mpdparser_get_active_stream_by_index (demux->client, stream_idx);
    if (stream == NULL)
      return FALSE;
    GstCaps *caps = gst_dash_demux_get_input_caps (demux, stream);
    if (!demux->input_caps[stream_idx]
        || !gst_caps_is_equal (caps, demux->input_caps[stream_idx])) {
      switch_caps = TRUE;
      break;
    }
    stream_idx++;
  }
  return switch_caps;
}

static gboolean
gst_dash_demux_get_next_fragment (GstDashDemux * demux, gboolean caching)
{
  GstActiveStream *stream;
  GstFragment *download, *header;
  GList *list_fragment;
  const gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;
  GTimeVal now;
  GTimeVal start;
  GstClockTime diff;
  guint64 size_buffer = 0;

  g_get_current_time (&start);
  /* support multiple streams */
  gboolean need_header = need_add_header (demux);
  int stream_idx = 0;
  list_fragment = NULL;
  while (stream_idx < gst_mpdparser_get_nb_active_stream (demux->client)) {
    if (!gst_mpd_client_get_next_fragment (demux->client,
            stream_idx, &discont, &next_fragment_uri, &duration, &timestamp)) {
      GST_INFO_OBJECT (demux, "This manifest doesn't contain more fragments");
      demux->end_of_manifest = TRUE;
      gst_task_start (demux->stream_task);
      return FALSE;
    }

    GST_INFO_OBJECT (demux, "Fetching next fragment %s", next_fragment_uri);

    download = gst_uri_downloader_fetch_uri (demux->downloader,
        next_fragment_uri);

    if (download == NULL)
      goto error;

    download->start_time = timestamp;
    download->stop_time = timestamp + duration;

    stream =
        gst_mpdparser_get_active_stream_by_index (demux->client, stream_idx);
    if (stream == NULL)
      goto error;
    download->index = stream->segment_idx;

    GstCaps *caps = gst_dash_demux_get_input_caps (demux, stream);

    if (need_header) {
      /* We changed spatial representation */
      gst_caps_replace (&demux->input_caps[stream_idx], caps);
      GST_INFO_OBJECT (demux, "Input source caps: %" GST_PTR_FORMAT,
          demux->input_caps[stream_idx]);
      /* We need to fetch a new header */
      if ((header = gst_dash_demux_get_next_header (demux, stream_idx)) == NULL) {
        GST_INFO_OBJECT (demux, "Unable to fetch header");
      } else {
        /* Replace fragment with a new one including the header */
        GstFragment *new_fragment =
            gst_dash_demux_prepend_header (demux, download, header);
        g_object_unref (header);
        g_object_unref (download);
        download = new_fragment;
      }
    } else
      gst_caps_unref (caps);

    gst_fragment_set_caps (download, demux->input_caps[stream_idx]);
    list_fragment = g_list_append (list_fragment, download);
    size_buffer += gst_fragment_get_buffer_size (download);
    stream_idx++;
  }
  g_queue_push_tail (demux->queue, list_fragment);
  if (!caching) {
    GST_TASK_SIGNAL (demux->download_task);
  }
  g_get_current_time (&now);
  diff = (GST_TIMEVAL_TO_TIME (now) - GST_TIMEVAL_TO_TIME (start));
  demux->dnl_rate = (size_buffer * 8) / ((double) diff / GST_SECOND);
  GST_INFO_OBJECT (demux, "Download rate = %d Kbits/s (%d Ko in %.2f s)",
      demux->dnl_rate / 1000, size_buffer / 1024, ((double) diff / GST_SECOND));
  return TRUE;

error:
  {
    gst_dash_demux_stop (demux);
    return FALSE;
  }
}

