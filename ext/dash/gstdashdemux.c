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

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <string.h>
#include <inttypes.h>
#include <gst/base/gsttypefindhelper.h>
#include "gstdashdemux.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/dash+xml"));

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
#define DEFAULT_MIN_BUFFERING_TIME        5     /* in seconds */
#define DEFAULT_MAX_BUFFERING_TIME       30     /* in seconds */
#define DEFAULT_BANDWIDTH_USAGE         0.8     /* 0 to 1     */
#define DEFAULT_MAX_BITRATE        24000000     /* in bit/s  */

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
static gboolean gst_dash_demux_setup_all_streams (GstDashDemux * demux);
static gboolean gst_dash_demux_select_representations (GstDashDemux * demux,
    guint64 current_bitrate);
static gboolean gst_dash_demux_get_next_fragment_set (GstDashDemux * demux);

static void gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose);
static GstClockTime gst_dash_demux_get_buffering_time (GstDashDemux * demux);
static float gst_dash_demux_get_buffering_ratio (GstDashDemux * demux);
static GstCaps *gst_dash_demux_get_input_caps (GstDashDemux * demux,
    GstActiveStream * stream);
static GstClockTime gst_dash_demux_stream_get_buffering_time (GstDashDemuxStream
    * stream);

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
      if (demux->client->mpd_node != NULL)
        gst_dash_demux_resume_stream_task (demux);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_dash_demux_pause_stream_task (demux);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      demux->cancelled = TRUE;
      gst_dash_demux_stop (demux);
      gst_task_join (demux->stream_task);
      gst_task_join (demux->download_task);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_dash_demux_all_queues_have_data (GstDashDemux * demux)
{
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    if (gst_data_queue_is_empty (stream->queue)) {
      return FALSE;
    }
  }
  return TRUE;
}

static void
gst_dash_demux_clear_queues (GstDashDemux * demux)
{
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    gst_data_queue_flush (stream->queue);
  }
}

static gboolean
_check_queue_full (GstDataQueue * q, guint visible, guint bytes, guint64 time,
    GstDashDemuxStream * stream)
{
  /* TODO add limits */
  return FALSE;
}

static void
_data_queue_item_destroy (GstDataQueueItem * item)
{
  gst_mini_object_unref (item->object);
  g_free (item);
}

static void
gst_dash_demux_stream_push_data (GstDashDemuxStream * stream,
    GstBuffer * fragment)
{
  GstDataQueueItem *item = g_new0 (GstDataQueueItem, 1);

  item->object = GST_MINI_OBJECT_CAST (fragment);
  item->duration = GST_BUFFER_DURATION (fragment);
  item->visible = TRUE;
  item->size = GST_BUFFER_SIZE (fragment);

  item->destroy = (GDestroyNotify) _data_queue_item_destroy;

  gst_data_queue_push (stream->queue, item);
}

static gboolean
gst_dash_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstDashDemux *demux;

  demux = GST_DASH_DEMUX (gst_pad_get_element_private (pad));
  GST_WARNING_OBJECT (demux, "Received an event");

  switch (event->type) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GList *list;
      GstClockTime current_pos, target_pos;
      guint current_sequence, current_period;
      GstActiveStream *active_stream;
      GstMediaSegment *chunk;
      GstStreamPeriod *period;
      GSList *iter;

      GST_WARNING_OBJECT (demux, "Received seek event");

      if (gst_mpd_client_is_live (demux->client)) {
        GST_WARNING_OBJECT (demux, "Received seek event for live stream");
        return FALSE;
      }

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      GST_DEBUG_OBJECT (demux,
          "seek event, rate: %f type: %d start: %" GST_TIME_FORMAT " stop: %"
          GST_TIME_FORMAT, rate, start_type, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush start");
        for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream;
          stream = iter->data;
          gst_pad_push_event (stream->pad, gst_event_new_flush_start ());
        }
      }

      /* Stop the demux */
      demux->cancelled = TRUE;
      gst_dash_demux_stop (demux);

      /* Wait for streaming to finish */
      g_static_rec_mutex_lock (&demux->stream_lock);

      /* Clear the buffering queue */
      /* FIXME: allow seeking in the buffering queue */
      gst_dash_demux_clear_queues (demux);

      //GST_MPD_CLIENT_LOCK (demux->client);

      /* select the requested Period in the Media Presentation */
      target_pos = (GstClockTime) start;
      current_period = 0;
      for (list = g_list_first (demux->client->periods); list;
          list = g_list_next (list)) {
        period = list->data;
        current_pos = period->start;
        current_period = period->number;
        if (current_pos <= target_pos
            && target_pos < current_pos + period->duration) {
          break;
        }
      }
      if (list == NULL) {
        GST_WARNING_OBJECT (demux, "Could not find seeked Period");
        return FALSE;
      }
      if (current_period != gst_mpd_client_get_period_index (demux->client)) {
        GST_DEBUG_OBJECT (demux, "Seeking to Period %d", current_period);
        /* setup video, audio and subtitle streams, starting from the new Period */
        if (!gst_mpd_client_set_period_index (demux->client, current_period) ||
            !gst_dash_demux_setup_all_streams (demux))
          return FALSE;
      }

      if (list == NULL) {
        GST_WARNING_OBJECT (demux, "Could not find seeked fragment");
        return FALSE;
      }

      /* Update the current sequence on all streams */
      for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
        GstDashDemuxStream *stream = iter->data;

        active_stream =
            gst_mpdparser_get_active_stream_by_index (demux->client,
            stream->index);
        current_pos = 0;
        current_sequence = 0;
        for (list = g_list_first (active_stream->segments); list;
            list = g_list_next (list)) {
          chunk = list->data;
          current_pos = chunk->start_time;
          //current_sequence = chunk->number;
          GST_WARNING_OBJECT (demux, "%llu <= %llu (%llu)", current_pos,
              target_pos, chunk->duration);
          if (current_pos <= target_pos
              && target_pos < current_pos + chunk->duration) {
            break;
          }
          current_sequence++;
        }
        gst_mpd_client_set_segment_index (active_stream, current_sequence);
      }
      /* Calculate offset in the next fragment */
      demux->position = gst_mpd_client_get_current_position (demux->client);
      demux->position_shift = start - demux->position;
      demux->need_segment = TRUE;
      demux->need_header = TRUE;
      //GST_MPD_CLIENT_UNLOCK (demux->client);

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "Sending flush stop on all pad");
        for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream;

          stream = iter->data;
          gst_pad_push_event (stream->pad, gst_event_new_flush_stop ());
        }
      }

      /* Restart the demux */
      demux->cancelled = FALSE;
      demux->end_of_manifest = FALSE;
      for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
        GstDashDemuxStream *stream = iter->data;
        gst_data_queue_set_flushing (stream->queue, FALSE);
      }
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
gst_dash_demux_setup_all_streams (GstDashDemux * demux)
{
  GList *listLang = NULL;
  guint i, nb_audio;
  gchar *lang;

  GST_MPD_CLIENT_LOCK (demux->client);
  /* clean old active stream list, if any */
  gst_active_streams_free (demux->client);

  if (!gst_mpd_client_setup_streaming (demux->client, GST_STREAM_VIDEO, ""))
    GST_INFO_OBJECT (demux, "No video adaptation set found");

  nb_audio =
      gst_mpdparser_get_list_and_nb_of_audio_language (demux->client,
      &listLang);
  if (nb_audio == 0)
    nb_audio = 1;
  GST_INFO_OBJECT (demux, "Number of language is=%d", nb_audio);

  for (i = 0; i < nb_audio; i++) {
    lang = (gchar *) g_list_nth_data (listLang, i);
    GST_INFO ("nb adaptation set: %i",
        gst_mpdparser_get_nb_adaptationSet (demux->client));
    if (!gst_mpd_client_setup_streaming (demux->client, GST_STREAM_AUDIO, lang))
      GST_INFO_OBJECT (demux, "No audio adaptation set found");

    if (gst_mpdparser_get_nb_adaptationSet (demux->client) > nb_audio)
      if (!gst_mpd_client_setup_streaming (demux->client,
              GST_STREAM_APPLICATION, lang))
        GST_INFO_OBJECT (demux, "No application adaptation set found");
  }


  GST_DEBUG_OBJECT (demux, "Creating stream objects");
  for (i = 0; i < gst_mpdparser_get_nb_active_stream (demux->client); i++) {
    GstDashDemuxStream *stream;
    GstActiveStream *active_stream;
    GstCaps *caps;

    active_stream = gst_mpdparser_get_active_stream_by_index (demux->client, i);
    if (active_stream == NULL)
      continue;

    stream = g_new0 (GstDashDemuxStream, 1);
    caps = gst_dash_demux_get_input_caps (demux, active_stream);
    stream->queue =
        gst_data_queue_new ((GstDataQueueCheckFullFunction) _check_queue_full,
        stream);

    stream->index = i;
    stream->input_caps = caps;

    GST_LOG_OBJECT (demux, "Creating stream %d %" GST_PTR_FORMAT, i, caps);
    demux->streams = g_slist_prepend (demux->streams, stream);
  }
  demux->streams = g_slist_reverse (demux->streams);

  GST_MPD_CLIENT_UNLOCK (demux->client);

  return TRUE;
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

      GST_DEBUG_OBJECT (demux, "Got EOS on the sink pad: manifest fetched");

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

      if (!gst_mpd_client_setup_media_presentation (demux->client)) {
        GST_ELEMENT_ERROR (demux, STREAM, DECODE,
            ("Incompatible manifest file."), (NULL));
        return FALSE;
      }

      /* setup video, audio and subtitle streams, starting from first Period */
      if (!gst_mpd_client_set_period_index (demux->client, 0) ||
          !gst_dash_demux_setup_all_streams (demux))
        return FALSE;

      /* start playing from the first segment */
      gst_mpd_client_set_segment_index_for_all_streams (demux->client, 0);

      /* Send duration message */
      if (!gst_mpd_client_is_live (demux->client)) {
        GstClockTime duration =
            gst_mpd_client_get_media_presentation_duration (demux->client);

        if (duration != GST_CLOCK_TIME_NONE) {
          GST_DEBUG_OBJECT (demux,
              "Sending duration message : %" GST_TIME_FORMAT,
              GST_TIME_ARGS (duration));
          gst_element_post_message (GST_ELEMENT (demux),
              gst_message_new_duration (GST_OBJECT (demux), GST_FORMAT_TIME,
                  duration));
        } else {
          GST_DEBUG_OBJECT (demux,
              "mediaPresentationDuration unknown, can not send the duration message");
        }
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
        duration =
            gst_mpd_client_get_media_presentation_duration (dashdemux->client);
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
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint64 start;
      gint64 end;
      gint64 stop = -1;

      gst_query_parse_seeking (query, &fmt, NULL, &start, &end);
      GST_DEBUG_OBJECT (dashdemux,
          "Received GST_QUERY_SEEKING with format %d - %i %i", fmt, start, end);
      if (fmt == GST_FORMAT_TIME) {
        GstClockTime duration;

        duration =
            gst_mpd_client_get_media_presentation_duration (dashdemux->client);
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
    default:{
      // By default, do not forward queries upstream
      break;
    }
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
  GSList *iter;

  gst_uri_downloader_cancel (demux->downloader);
  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    gst_data_queue_set_flushing (stream->queue, TRUE);
  }

  if (GST_TASK_STATE (demux->download_task) != GST_TASK_STOPPED) {
    GST_TASK_SIGNAL (demux->download_task);
    gst_task_stop (demux->download_task);
  }
  if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED) {
    GST_TASK_SIGNAL (demux->stream_task);
    gst_task_stop (demux->stream_task);
  }
}

/* switch_pads:
 * 
 * Called when switching from one set of representations to another, but
 * only if one of the new representations requires different downstream 
 * elements (see the next function).
 * 
 * This function first creates the new pads, then sends a no-more-pads
 * event (that will tell decodebin to create a new group), then sends
 * EOS on the old pads to trigger the group switch.
 * 
 */
static void
switch_pads (GstDashDemux * demux, guint nb_adaptation_set)
{
  GSList *oldpads = NULL;
  GSList *iter;
  /* Remember old pads */
  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    GstPad *oldpad = stream->pad;
    if (oldpad) {
      oldpads = g_slist_prepend (oldpads, oldpad);
      GST_DEBUG_OBJECT (demux,
          "Switching pads (oldpad:%p) %" GST_PTR_FORMAT, oldpad, oldpad);
    }
  }
  /* Create and activate new pads */
  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    stream->pad = gst_pad_new_from_static_template (&srctemplate, NULL);
    gst_pad_set_event_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_dash_demux_src_event));
    gst_pad_set_query_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_dash_demux_src_query));
    gst_pad_set_element_private (stream->pad, demux);
    gst_pad_set_active (stream->pad, TRUE);
    gst_pad_set_caps (stream->pad, stream->output_caps);
    gst_element_add_pad (GST_ELEMENT (demux), gst_object_ref (stream->pad));
    GST_INFO_OBJECT (demux, "Adding srcpad %s:%s with caps %" GST_PTR_FORMAT,
        GST_DEBUG_PAD_NAME (stream->pad), stream->output_caps);
  }
  /* Send 'no-more-pads' to have decodebin create the new group */
  gst_element_no_more_pads (GST_ELEMENT (demux));

  /* Push out EOS on all old pads to switch to the new group */
  for (iter = oldpads; iter; iter = g_slist_next (iter)) {
    GstPad *pad = iter->data;

    gst_pad_push_event (pad, gst_event_new_eos ());
    gst_pad_set_active (pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (demux), pad);
    gst_object_unref (pad);
  }
  g_slist_free (oldpads);
}

/* needs_pad_switch:
 * 
 * Figure out if the newly selected representations require a new set
 * of demuxers and decoders or if we can carry on with the existing ones.
 * 
 * Basically, we look at the list of fragments we need to push downstream, 
 * and compare their caps with those of the corresponding src pads.
 * 
 * As soon as one fragment requires a new set of caps, we need to switch
 * all decoding pads to recreate a whole decoding group as we cannot 
 * move pads between groups (FIXME: or can we ?).
 * 
 * FIXME: redundant with need_add_header
 * 
 */
static gboolean
needs_pad_switch (GstDashDemux * demux)
{
  gboolean switch_pad = FALSE;
  guint i = 0;
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDataQueueItem *item;
    GstDashDemuxStream *stream = iter->data;
    GstCaps *srccaps = NULL;
    GstBuffer *buffer;

    if (!gst_data_queue_peek (stream->queue, &item))
      continue;

    buffer = GST_BUFFER_CAST (item->object);

    gst_caps_replace (&stream->output_caps, GST_BUFFER_CAPS (buffer));

    if (G_LIKELY (stream->pad))
      srccaps = gst_pad_get_negotiated_caps (stream->pad);
    if (G_UNLIKELY (!srccaps
            || (!gst_caps_is_equal_fixed (stream->output_caps, srccaps)))
        || demux->need_segment) {
      switch_pad = TRUE;
    }
    if (G_LIKELY (srccaps))
      gst_caps_unref (srccaps);
    i++;
  }
  return switch_pad;
}

/* gst_dash_demux_stream_loop:
 * 
 * Loop for the "stream' task that pushes fragments to the src pads.
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
 * The task is stopped when we have reached the end of the manifest
 * and emptied our queue.
 * 
 */
static void
gst_dash_demux_stream_loop (GstDashDemux * demux)
{
  GstFlowReturn ret;
  guint nb_adaptation_set = 0;
  GstActiveStream *active_stream;
  gboolean switch_pad;
  guint i = 0;
  GSList *iter;

  GST_LOG_OBJECT (demux, "Starting stream loop");

  if (!gst_dash_demux_all_queues_have_data (demux)) {
    if (demux->end_of_manifest)
      goto end_of_manifest;

    GST_DEBUG_OBJECT (demux, "Ending stream loop, no buffers to push");
    return;
  }

  if (GST_STATE (demux) == GST_STATE_PLAYING) {
    if (!demux->end_of_manifest
        && gst_dash_demux_get_buffering_time (demux) <
        demux->min_buffering_time) {
      /* Warn we are below our threshold: this will eventually pause 
       * the pipeline */
      GST_WARNING
          ("Below the threshold: this will eventually pause the pipeline");
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_buffering (GST_OBJECT (demux),
              100 * gst_dash_demux_get_buffering_ratio (demux)));
    }
  }

  /* Figure out if we need to create/switch pads */
  switch_pad = needs_pad_switch (demux);
  if (switch_pad) {
    GST_WARNING ("Switching pads");
    switch_pads (demux, nb_adaptation_set);
    demux->need_segment = TRUE;
  }

  for (iter = demux->streams, i = 0; iter; i++, iter = g_slist_next (iter)) {
    GstDataQueueItem *item;
    GstBuffer *buffer;
    GstDashDemuxStream *stream = iter->data;
    if (!gst_data_queue_pop (stream->queue, &item))
      continue;

    buffer = GST_BUFFER_CAST (item->object);

    active_stream = gst_mpdparser_get_active_stream_by_index (demux->client, i);
    if (demux->need_segment) {
      GstClockTime start =
          GST_BUFFER_TIMESTAMP (buffer) + demux->position_shift;
      /* And send a newsegment */
      GST_DEBUG_OBJECT (demux, "Sending new-segment. segment start:%"
          GST_TIME_FORMAT, GST_TIME_ARGS (start));
      gst_pad_push_event (stream->pad,
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
              start, GST_CLOCK_TIME_NONE, start));
    }

    GST_DEBUG_OBJECT (demux,
        "Pushing fragment %p #%d (stream %i) ts:%" GST_TIME_FORMAT " dur:%"
        GST_TIME_FORMAT, buffer, GST_BUFFER_OFFSET (buffer), i,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
    ret = gst_pad_push (stream->pad, gst_buffer_ref (buffer));
    item->destroy (item);
    if ((ret != GST_FLOW_OK) && (active_stream->mimeType == GST_STREAM_VIDEO))
      goto error_pushing;
  }
  demux->need_segment = FALSE;
  demux->position_shift = 0;

  return;

end_of_manifest:
  {
    GST_INFO_OBJECT (demux, "Reached end of manifest, sending EOS");
    for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
      GstDashDemuxStream *stream = iter->data;
      gst_pad_push_event (stream->pad, gst_event_new_eos ());
    }
    GST_INFO_OBJECT (demux, "Stopped streaming task");
    gst_task_stop (demux->stream_task);
    return;
  }

error_pushing:
  {
    /* FIXME: handle error */
    GST_ERROR_OBJECT (demux,
        "Error pushing buffer: %s... terminating the demux",
        gst_flow_get_name (ret));
    gst_dash_demux_stop (demux);
    return;
  }
}

static void
gst_dash_demux_stream_free (GstDashDemuxStream * stream)
{
  if (stream->input_caps)
    gst_caps_unref (stream->input_caps);
  if (stream->output_caps)
    gst_caps_unref (stream->output_caps);
  if (stream->pad)
    gst_object_unref (stream->pad);

  /* TODO flush the queue */
  g_object_unref (stream->queue);

  g_free (stream);
}

static void
gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose)
{
  GSList *iter;

  demux->end_of_period = FALSE;
  demux->end_of_manifest = FALSE;
  demux->cancelled = FALSE;

  gst_dash_demux_clear_queues (demux);

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    gst_dash_demux_stream_free (stream);
  }
  g_slist_free (demux->streams);
  demux->streams = NULL;

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

  demux->last_manifest_update = GST_CLOCK_TIME_NONE;
  demux->position = 0;
  demux->position_shift = 0;
  demux->need_header = TRUE;
  demux->need_segment = TRUE;
}

static GstClockTime
gst_dash_demux_get_buffering_time (GstDashDemux * demux)
{
  GstClockTime buffer_time = 0;
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    buffer_time = gst_dash_demux_stream_get_buffering_time (iter->data);

    if (buffer_time)
      return buffer_time;
  }

  return 0;
}

static GstClockTime
gst_dash_demux_stream_get_buffering_time (GstDashDemuxStream * stream)
{
  GstDataQueueSize level;

  gst_data_queue_get_level (stream->queue, &level);

  return (GstClockTime) level.time;
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

/* gst_dash_demux_download_loop:
 * 
 * Loop for the "download' task that fetches fragments based on the 
 * selected representations.
 * 
 * Startup: 
 * 
 * The task is started from the stream loop.
 * 
 * During playback:  
 * 
 * It sequentially fetches fragments corresponding to the current 
 * representations and pushes them into a queue.
 * 
 * It tries to maintain the number of queued items within a predefined 
 * range: if the queue is full, it will pause, checking every 100 ms if 
 * it needs to restart downloading fragments.
 * 
 * When a new set of fragments has been downloaded, it evaluates the
 * download time to check if we can or should switch to a different 
 * set of representations.
 *
 * Teardown:
 * 
 * The task will exit when it encounters an error or when the end of the
 * manifest has been reached.
 * 
 */
void
gst_dash_demux_download_loop (GstDashDemux * demux)
{
  GstClockTime target_buffering_time;
  GstClock *clock = gst_element_get_clock (GST_ELEMENT (demux));
  gint64 update_period = demux->client->mpd_node->minimumUpdatePeriod;

  /* Wait until the next scheduled download */
  if (g_cond_timed_wait (GST_TASK_GET_COND (demux->download_task),
          demux->download_timed_lock, &demux->next_download)) {
    goto quit;
  }

  if (clock && gst_mpd_client_is_live (demux->client)
      && demux->client->mpd_uri != NULL && update_period != -1) {
    GstFragment *download;
    GstBuffer *buffer;
    GstClockTime duration, now = gst_clock_get_time (clock);

    /* init reference time for manifest file updates */
    if (!GST_CLOCK_TIME_IS_VALID (demux->last_manifest_update))
      demux->last_manifest_update = now;

    /* update the manifest file */
    if (now >= demux->last_manifest_update + update_period * GST_MSECOND) {
      GST_DEBUG_OBJECT (demux, "Updating manifest file from URL %s",
          demux->client->mpd_uri);
      download =
          gst_uri_downloader_fetch_uri (demux->downloader,
          demux->client->mpd_uri);
      if (download == NULL) {
        GST_WARNING_OBJECT (demux,
            "Failed to update the manifest file from URL %s",
            demux->client->mpd_uri);
      } else {
        buffer = gst_fragment_get_buffer (download);
        g_object_unref (download);
        /* parse the manifest file */
        if (buffer == NULL) {
          GST_WARNING_OBJECT (demux, "Error validating the manifest.");
        } else if (!gst_mpd_parse (demux->client,
                (gchar *) GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
          /* In most cases, this will happen if we set a wrong url in the
           * source element and we have received the 404 HTML response instead of
           * the manifest */
          GST_WARNING_OBJECT (demux, "Error parsing the manifest.");
          gst_buffer_unref (buffer);
        } else {
          GstActiveStream *stream;
          guint segment_index;

          gst_buffer_unref (buffer);
          stream = gst_mpdparser_get_active_stream_by_index (demux->client, 0);
          segment_index = gst_mpd_client_get_segment_index (stream);
          /* setup video, audio and subtitle streams, starting from first Period */
          if (!gst_mpd_client_setup_media_presentation (demux->client) ||
              !gst_mpd_client_set_period_index (demux->client,
                  gst_mpd_client_get_period_index (demux->client))
              || !gst_dash_demux_setup_all_streams (demux)) {
            GST_DEBUG_OBJECT (demux,
                "Error setting up the updated manifest file");
            goto end_of_manifest;
          }
          /* continue playing from the the next segment */
          /* FIXME: support multiple streams with different segment duration */
          gst_mpd_client_set_segment_index_for_all_streams (demux->client,
              segment_index);

          /* Send an updated duration message */
          duration =
              gst_mpd_client_get_media_presentation_duration (demux->client);

          if (duration != GST_CLOCK_TIME_NONE) {
            GST_DEBUG_OBJECT (demux,
                "Sending duration message : %" GST_TIME_FORMAT,
                GST_TIME_ARGS (duration));
            gst_element_post_message (GST_ELEMENT (demux),
                gst_message_new_duration (GST_OBJECT (demux), GST_FORMAT_TIME,
                    duration));
          } else {
            GST_DEBUG_OBJECT (demux,
                "mediaPresentationDuration unknown, can not send the duration message");
          }
          demux->last_manifest_update += update_period * GST_MSECOND;
          GST_DEBUG_OBJECT (demux, "Manifest file successfully updated");
        }
      }
    }
  }


  /* Target buffering time MUST at least exceeds mimimum buffering time 
   * by the duration of a fragment, but SHOULD NOT exceed maximum
   * buffering time */
  GST_DEBUG_OBJECT (demux, "download loop %i", demux->end_of_manifest);
  target_buffering_time =
      demux->min_buffering_time +
      gst_mpd_client_get_next_fragment_duration (demux->client);
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

    /* try to switch to another set of representations if needed */
    gst_dash_demux_select_representations (demux,
        demux->bandwidth_usage * demux->dnl_rate *
        gst_dash_demux_get_buffering_ratio (demux));

    /* fetch the next fragment */
    while (!gst_dash_demux_get_next_fragment_set (demux)) {
      if (demux->end_of_period) {
        GST_INFO_OBJECT (demux, "Reached the end of the Period");
        /* setup video, audio and subtitle streams, starting from the next Period */
        if (!gst_mpd_client_set_period_index (demux->client,
                gst_mpd_client_get_period_index (demux->client) + 1)
            || !gst_dash_demux_setup_all_streams (demux)) {
          GST_INFO_OBJECT (demux, "Reached the end of the manifest file");
          demux->end_of_manifest = TRUE;
          if (GST_STATE (demux) != GST_STATE_PLAYING) {
            /* Restart the pipeline regardless of the current buffering level */
            gst_element_post_message (GST_ELEMENT (demux),
                gst_message_new_buffering (GST_OBJECT (demux), 100));
          }
          gst_task_start (demux->stream_task);
          goto end_of_manifest;
        }
        /* start playing from the first segment of the new period */
        gst_mpd_client_set_segment_index_for_all_streams (demux->client, 0);
        demux->end_of_period = FALSE;
      } else if (!demux->cancelled) {
        demux->client->update_failed_count++;
        if (demux->client->update_failed_count < DEFAULT_FAILED_COUNT) {
          GST_WARNING_OBJECT (demux, "Could not fetch the next fragment");
          goto quit;
        } else {
          goto error_downloading;
        }
      } else {
        goto quit;
      }
    }
    GST_INFO_OBJECT (demux, "Internal buffering : %" PRIu64 " s",
        gst_dash_demux_get_buffering_time (demux) / GST_SECOND);
    demux->client->update_failed_count = 0;
  } else {
    /* schedule the next download in 100 ms */
    g_get_current_time (&demux->next_download);
    g_time_val_add (&demux->next_download, 100000);
  }

quit:
  {
    return;
  }

end_of_manifest:
  {
    GST_INFO_OBJECT (demux, "Stopped download task");
    gst_task_stop (demux->download_task);
    return;
  }

error_downloading:
  {
    GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
        ("Could not fetch the next fragment"), (NULL));
    gst_dash_demux_stop (demux);
    return;
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
  gst_task_start (demux->stream_task);
}

static void
gst_dash_demux_resume_download_task (GstDashDemux * demux)
{
  g_get_current_time (&demux->next_download);
  gst_task_start (demux->download_task);
}

/* gst_dash_demux_select_representations:
 *
 * Select the most appropriate media representations based on a target 
 * bitrate.
 * 
 * FIXME: all representations are selected against the same bitrate, but
 * they will share the same bandwidth. This only works today because the
 * audio representations bitrate usage is negligible as compared to the
 * video representation one.
 * 
 * Returns TRUE if a new set of representations has been selected
 */
static gboolean
gst_dash_demux_select_representations (GstDashDemux * demux, guint64 bitrate)
{
  GstActiveStream *stream = NULL;
  GList *rep_list = NULL;
  gint new_index;
  gboolean ret = FALSE;

  guint i = 0;
  while (i < gst_mpdparser_get_nb_active_stream (demux->client)) {
    stream = gst_mpdparser_get_active_stream_by_index (demux->client, i);
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
      new_index = gst_mpdparser_get_rep_idx_with_min_bandwidth (rep_list);

#if 0
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
#endif
    i++;
  }
  return ret;
}

static GstFragment *
gst_dash_demux_get_next_header (GstDashDemux * demux, guint stream_idx)
{
  const gchar *initializationURL;
  gchar *next_header_uri;
  GstFragment *fragment;

  if (!gst_mpd_client_get_next_header (demux->client, &initializationURL,
          stream_idx))
    return NULL;

  if (strncmp (initializationURL, "http://", 7) != 0) {
    next_header_uri =
        g_strconcat (gst_mpdparser_get_baseURL (demux->client, stream_idx),
        initializationURL, NULL);
  } else {
    next_header_uri = g_strdup (initializationURL);
  }

  GST_INFO_OBJECT (demux, "Fetching header %s", next_header_uri);

  fragment = gst_uri_downloader_fetch_uri (demux->downloader, next_header_uri);
  g_free (next_header_uri);

  return fragment;
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

  caps = gst_caps_new_simple (mimeType, NULL);
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

  caps = gst_caps_new_simple (mimeType, NULL);
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
  GstActiveStream *active_stream;
  GstCaps *caps;
  gboolean switch_caps = FALSE;
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    active_stream =
        gst_mpdparser_get_active_stream_by_index (demux->client, stream->index);
    if (active_stream == NULL)
      return FALSE;
    caps = gst_dash_demux_get_input_caps (demux, active_stream);
    if (!stream->input_caps || !gst_caps_is_equal (caps, stream->input_caps)
        || demux->need_header) {
      demux->need_header = FALSE;
      switch_caps = TRUE;
      gst_caps_unref (caps);
      break;
    }
    gst_caps_unref (caps);
  }
  return switch_caps;
}

/* gst_dash_demux_get_next_fragment_set:
 *
 * Get the next set of fragments for the current representations.
 * 
 * This function uses the generic URI downloader API.
 *
 * Returns FALSE if an error occured while downloading fragments
 * 
 */
static gboolean
gst_dash_demux_get_next_fragment_set (GstDashDemux * demux)
{
  GstActiveStream *active_stream;
  GstFragment *download, *header;
  GList *fragment_set;
  gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;
  GTimeVal now;
  GTimeVal start;
  GstClockTime diff;
  guint64 size_buffer = 0;
  gboolean need_header;
  GSList *iter;

  g_get_current_time (&start);
  /* Figure out if we will need to switch pads, thus requiring a new
   * header to initialize the new decoding chain
   * FIXME: redundant with needs_pad_switch */
  need_header = need_add_header (demux);
  fragment_set = NULL;
  /* Get the fragment corresponding to each stream index */
  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    guint stream_idx = stream->index;
    GstBuffer *buffer;

    if (!gst_mpd_client_get_next_fragment (demux->client,
            stream_idx, &discont, &next_fragment_uri, &duration, &timestamp)) {
      GST_INFO_OBJECT (demux, "This Period doesn't contain more fragments");
      demux->end_of_period = TRUE;
      return FALSE;
    }

    GST_INFO_OBJECT (demux, "Next fragment for stream #%i", stream_idx);
    GST_INFO_OBJECT (demux,
        "Fetching next fragment %s ts:%" GST_TIME_FORMAT " dur:%"
        GST_TIME_FORMAT, next_fragment_uri, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (duration));

    download = gst_uri_downloader_fetch_uri (demux->downloader,
        next_fragment_uri);
    g_free (next_fragment_uri);

    if (download == NULL)
      return FALSE;

    buffer = gst_fragment_get_buffer (download);

    active_stream =
        gst_mpdparser_get_active_stream_by_index (demux->client, stream_idx);
    if (stream == NULL)         /* TODO unref fragments */
      return FALSE;

    if (need_header) {
      /* We need to fetch a new header */
      if ((header = gst_dash_demux_get_next_header (demux, stream_idx)) == NULL) {
        GST_INFO_OBJECT (demux, "Unable to fetch header");
      } else {
        GstBuffer *header_buffer;
        /* Replace fragment with a new one including the header */

        header_buffer = gst_fragment_get_buffer (header);
        buffer = gst_buffer_join (header_buffer, buffer);
      }
    }

    buffer = gst_buffer_make_metadata_writable (buffer);

    GST_BUFFER_TIMESTAMP (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = duration;
    GST_BUFFER_OFFSET (buffer) =
        gst_mpd_client_get_segment_index (active_stream) - 1;

    gst_buffer_set_caps (buffer, stream->input_caps);
    gst_dash_demux_stream_push_data (stream, buffer);
    size_buffer += GST_BUFFER_SIZE (buffer);
  }

  /* Wake the download task up */
  GST_TASK_SIGNAL (demux->download_task);
  g_get_current_time (&now);
  diff = (GST_TIMEVAL_TO_TIME (now) - GST_TIMEVAL_TO_TIME (start));
  demux->dnl_rate = (size_buffer * 8) / ((double) diff / GST_SECOND);
  GST_INFO_OBJECT (demux,
      "Download rate = %" PRIu64 " Kbits/s (%" PRIu64 " Ko in %.2f s)",
      demux->dnl_rate / 1000, size_buffer / 1024, ((double) diff / GST_SECOND));
  return TRUE;
}
