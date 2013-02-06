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

  PROP_MAX_BUFFERING_TIME,
  PROP_BANDWIDTH_USAGE,
  PROP_MAX_BITRATE,
  PROP_LAST
};

/* Default values for properties */
#define DEFAULT_MAX_BUFFERING_TIME       30     /* in seconds */
#define DEFAULT_BANDWIDTH_USAGE         0.8     /* 0 to 1     */
#define DEFAULT_MAX_BITRATE        24000000     /* in bit/s  */

#define DEFAULT_FAILED_COUNT 3
#define DOWNLOAD_RATE_HISTORY_MAX 3

/* Custom internal event to signal end of period */
#define GST_EVENT_DASH_EOP GST_EVENT_MAKE_TYPE(81, GST_EVENT_TYPE_DOWNSTREAM | GST_EVENT_TYPE_SERIALIZED)
static GstEvent *
gst_event_new_dash_eop (void)
{
  return gst_event_new_custom (GST_EVENT_DASH_EOP, NULL);
}

/* Custom internal event to indicate a pad switch is needed */
#define GST_EVENT_DASH_PAD_SWITCH GST_EVENT_MAKE_TYPE(82, GST_EVENT_TYPE_DOWNSTREAM | GST_EVENT_TYPE_SERIALIZED)
static GstEvent *
gst_event_new_dash_event_pad_switch (GstCaps * caps)
{
  GstStructure *structure;

  g_assert (caps != NULL);

  structure =
      gst_structure_new ("dash-pad-switch", "caps", GST_TYPE_CAPS, caps, NULL);
  return gst_event_new_custom (GST_EVENT_DASH_PAD_SWITCH, structure);
}


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
static gboolean gst_dash_demux_select_representations (GstDashDemux * demux);
static gboolean gst_dash_demux_get_next_fragment (GstDashDemux * demux);

static void gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose);
static GstClockTime gst_dash_demux_get_buffering_time (GstDashDemux * demux);
static GstCaps *gst_dash_demux_get_input_caps (GstDashDemux * demux,
    GstActiveStream * stream);
static GstClockTime gst_dash_demux_stream_get_buffering_time (GstDashDemuxStream
    * stream);
static void gst_dash_demux_prepare_pad_switch (GstDashDemux * demux);

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

  gst_dash_demux_reset (demux, TRUE);

  if (demux->stream_task) {
    gst_object_unref (demux->stream_task);
    g_static_rec_mutex_free (&demux->stream_lock);
    demux->stream_task = NULL;
  }

  if (demux->download_task) {
    gst_object_unref (demux->download_task);
    g_static_rec_mutex_free (&demux->download_lock);
    demux->download_task = NULL;
  }

  if (demux->downloader != NULL) {
    g_object_unref (demux->downloader);
    demux->downloader = NULL;
  }

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

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  /* Downloader */
  demux->downloader = gst_uri_downloader_new ();

  /* Properties */
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
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_dash_demux_reset (demux, FALSE);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
      break;
    default:
      break;
  }
  return ret;
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
gst_dash_demux_stream_push_event (GstDashDemuxStream * stream, GstEvent * event)
{
  GstDataQueueItem *item = g_new0 (GstDataQueueItem, 1);

  item->object = GST_MINI_OBJECT_CAST (event);
  item->destroy = (GDestroyNotify) _data_queue_item_destroy;

  gst_data_queue_push (stream->queue, item);
}

static void
gst_dash_demux_stream_push_data (GstDashDemuxStream * stream,
    GstBuffer * fragment)
{
  GstDataQueueItem *item = g_new (GstDataQueueItem, 1);

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
      gboolean update;

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

      gst_segment_set_seek (&demux->segment, rate, format, flags, start_type,
          start, stop_type, stop, &update);

      if (update) {

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
        /* Stop the demux, also clears the buffering queue */
        gst_dash_demux_stop (demux);

        /* Wait for streaming to finish */
        g_static_rec_mutex_lock (&demux->stream_lock);

        //GST_MPD_CLIENT_LOCK (demux->client);

        /* select the requested Period in the Media Presentation */
        target_pos = (GstClockTime) demux->segment.start;
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
          if (!gst_mpd_client_set_period_index (demux->client, current_period)
              || !gst_dash_demux_setup_all_streams (demux))
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
        demux->position_shift = demux->segment.start - demux->position;
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
          stream->need_segment = TRUE;
          gst_data_queue_set_flushing (stream->queue, FALSE);
        }
        gst_uri_downloader_reset (demux->downloader);
        gst_dash_demux_resume_download_task (demux);
        gst_dash_demux_resume_stream_task (demux);
        g_static_rec_mutex_unlock (&demux->stream_lock);
      }

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
    gst_download_rate_init (&stream->dnl_rate);
    gst_download_rate_set_max_length (&stream->dnl_rate,
        DOWNLOAD_RATE_HISTORY_MAX);

    GST_LOG_OBJECT (demux, "Creating stream %d %" GST_PTR_FORMAT, i, caps);
    demux->streams = g_slist_prepend (demux->streams, stream);
  }
  demux->streams = g_slist_reverse (demux->streams);

  gst_dash_demux_prepare_pad_switch (demux);
  GST_MPD_CLIENT_UNLOCK (demux->client);

  return TRUE;
}

static gboolean
gst_dash_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstDashDemux *demux = GST_DASH_DEMUX (GST_PAD_PARENT (pad));

  switch (event->type) {
    case GST_EVENT_FLUSH_STOP:
      gst_dash_demux_reset (demux, FALSE);
      break;
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
  GstDashDemux *demux = GST_DASH_DEMUX (GST_PAD_PARENT (pad));

  if (demux->manifest == NULL)
    demux->manifest = buf;
  else
    demux->manifest = gst_buffer_join (demux->manifest, buf);

  return GST_FLOW_OK;
}

static void
gst_dash_demux_stop (GstDashDemux * demux)
{
  GSList *iter;

  GST_DEBUG_OBJECT (demux, "Stopping demux");

  if (demux->downloader)
    gst_uri_downloader_cancel (demux->downloader);

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    gst_data_queue_set_flushing (stream->queue, TRUE);
  }

  if (GST_TASK_STATE (demux->download_task) != GST_TASK_STOPPED) {
    GST_TASK_SIGNAL (demux->download_task);
    gst_task_stop (demux->download_task);
    g_static_rec_mutex_lock (&demux->download_lock);
    g_static_rec_mutex_unlock (&demux->download_lock);
    gst_task_join (demux->download_task);
  }
  if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED) {
    GST_TASK_SIGNAL (demux->stream_task);
    gst_task_stop (demux->stream_task);
    g_static_rec_mutex_lock (&demux->stream_lock);
    g_static_rec_mutex_unlock (&demux->stream_lock);
    gst_task_join (demux->stream_task);
  }

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    gst_data_queue_flush (stream->queue);
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
switch_pads (GstDashDemux * demux)
{
  GSList *oldpads = NULL;
  GSList *iter;

  /* Create and activate new pads */
  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    GstDataQueueItem *item;
    GstEvent *event;
    const GstStructure *structure;
    GstCaps *caps = NULL;
    GstPad *oldpad;

    if (!gst_data_queue_pop (stream->queue, &item)) {
      if (demux->cancelled) {
        break;
      }
      g_assert_not_reached ();
    }

    g_assert (GST_IS_EVENT (item->object));
    g_assert (GST_EVENT_TYPE (item->object) == GST_EVENT_DASH_PAD_SWITCH);

    event = GST_EVENT_CAST (item->object);
    structure = gst_event_get_structure (event);
    g_assert (structure != NULL);
    gst_structure_get (structure, "caps", GST_TYPE_CAPS, &caps, NULL);
    g_assert (caps != NULL);

    stream->need_segment = TRUE;
    oldpad = stream->pad;
    if (oldpad)
      oldpads = g_slist_prepend (oldpads, oldpad);

    stream->pad = gst_pad_new_from_static_template (&srctemplate, NULL);
    gst_pad_set_event_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_dash_demux_src_event));
    gst_pad_set_query_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_dash_demux_src_query));
    gst_pad_set_element_private (stream->pad, demux);
    gst_pad_set_active (stream->pad, TRUE);
    gst_pad_set_caps (stream->pad, caps);
    gst_element_add_pad (GST_ELEMENT (demux), gst_object_ref (stream->pad));
    GST_DEBUG_OBJECT (demux,
        "Switching pads (oldpad:%s:%s) (newpad:%s:%s)",
        GST_DEBUG_PAD_NAME (oldpad), GST_DEBUG_PAD_NAME (stream->pad));
    GST_INFO_OBJECT (demux, "Adding srcpad %s:%s with caps %" GST_PTR_FORMAT,
        GST_DEBUG_PAD_NAME (stream->pad), caps);

    gst_caps_unref (caps);
    item->destroy (item);
  }
  /* Send 'no-more-pads' to have decodebin create the new group */
  gst_element_no_more_pads (GST_ELEMENT (demux));

  /* Push out EOS on all old pads to switch to the new group */
  for (iter = oldpads; iter; iter = g_slist_next (iter)) {
    GstPad *pad = iter->data;

    GST_INFO_OBJECT (demux, "Removing old srcpad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    gst_pad_push_event (pad, gst_event_new_eos ());
    gst_pad_set_active (pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (demux), pad);
    gst_object_unref (pad);
  }
  g_slist_free (oldpads);
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
  GstActiveStream *active_stream;
  guint i = 0;
  GSList *iter;
  GstClockTime best_time;
  GstDashDemuxStream *selected_stream;
  gboolean eos = TRUE;
  gboolean eop = TRUE;
  gboolean pad_switch;

  GST_LOG_OBJECT (demux, "Starting stream loop");

  best_time = GST_CLOCK_TIME_NONE;
  selected_stream = NULL;
  pad_switch = TRUE;
  for (iter = demux->streams, i = 0; iter; i++, iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    GstDataQueueItem *item;

    GST_DEBUG_OBJECT (demux, "Peeking stream %d", stream->index);

    if (stream->stream_eos) {
      GST_DEBUG_OBJECT (demux, "Stream %d is eos, skipping", stream->index);
      continue;
    }

    if (stream->stream_end_of_period) {
      GST_DEBUG_OBJECT (demux, "Stream %d is eop, skipping", stream->index);
      eos = FALSE;
      continue;
    }
    eos = FALSE;
    eop = FALSE;

    GST_DEBUG_OBJECT (demux, "peeking at the queue for stream %d",
        stream->index);
    if (!gst_data_queue_peek (stream->queue, &item)) {
      /* flushing */
      goto flushing;
    }

    if (G_LIKELY (GST_IS_BUFFER (item->object))) {
      pad_switch = FALSE;
      if (GST_BUFFER_TIMESTAMP (item->object) < best_time) {
        best_time = GST_BUFFER_TIMESTAMP (item->object);
        selected_stream = stream;
      } else if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (item->object))) {
        selected_stream = stream;
        break;
      }
    } else {
      if (GST_EVENT_TYPE (item->object) == GST_EVENT_DASH_PAD_SWITCH) {
        continue;
      }
      selected_stream = stream;
      pad_switch = FALSE;
      break;
    }
  }

  if (pad_switch) {
    switch_pads (demux);
    goto end;
  }

  if (selected_stream) {
    GstDataQueueItem *item;
    GstBuffer *buffer;

    GST_DEBUG_OBJECT (demux, "Selected stream %p %d", selected_stream,
        selected_stream->index);

    if (!gst_data_queue_pop (selected_stream->queue, &item))
      goto end;

    if (G_LIKELY (GST_IS_BUFFER (item->object))) {
      buffer = GST_BUFFER_CAST (item->object);
      active_stream =
          gst_mpdparser_get_active_stream_by_index (demux->client,
          selected_stream->index);
      if (selected_stream->need_segment) {
        /* And send a newsegment */
        for (iter = demux->streams, i = 0; iter;
            i++, iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream = iter->data;
          gst_pad_push_event (stream->pad,
              gst_event_new_new_segment (TRUE, demux->segment.rate,
                  GST_FORMAT_TIME, demux->segment.start, demux->segment.stop,
                  0));
        }
        selected_stream->need_segment = FALSE;
        demux->position_shift = 0;
      }

      GST_DEBUG_OBJECT (demux,
          "Pushing fragment %p #%d (stream %i) ts:%" GST_TIME_FORMAT " dur:%"
          GST_TIME_FORMAT, buffer, GST_BUFFER_OFFSET (buffer),
          selected_stream->index, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
      ret = gst_pad_push (selected_stream->pad, gst_buffer_ref (buffer));
      gst_segment_set_last_stop (&demux->segment, GST_FORMAT_TIME,
          GST_BUFFER_TIMESTAMP (buffer));
      item->destroy (item);
      if ((ret != GST_FLOW_OK) && (active_stream
              && active_stream->mimeType == GST_STREAM_VIDEO))
        goto error_pushing;
    } else {
      /* a GstEvent */
      if (GST_EVENT_TYPE (item->object) == GST_EVENT_EOS) {
        selected_stream->stream_end_of_period = TRUE;
        selected_stream->stream_eos = TRUE;
      } else if (GST_EVENT_TYPE (item->object) == GST_EVENT_DASH_EOP) {
        selected_stream->stream_end_of_period = TRUE;
      }

      if (GST_EVENT_TYPE (item->object) != GST_EVENT_DASH_EOP) {
        gst_pad_push_event (selected_stream->pad,
            gst_event_ref (GST_EVENT_CAST (item->object)));
      }
      item->destroy (item);
    }
  } else {
    if (eos) {
      goto end_of_manifest;
    } else if (eop) {
      /* TODO advance to next period */
    }
  }

end:
  GST_INFO_OBJECT (demux, "Leaving streaming task");
  return;

flushing:
  {
    GST_WARNING_OBJECT (demux, "Flushing, leaving streaming task");
    gst_task_stop (demux->stream_task);
    return;
  }

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
    gst_task_stop (demux->stream_task);
    return;
  }
}

static void
gst_dash_demux_stream_free (GstDashDemuxStream * stream)
{
  gst_download_rate_deinit (&stream->dnl_rate);
  if (stream->input_caps) {
    gst_caps_unref (stream->input_caps);
    stream->input_caps = NULL;
  }
  if (stream->pad) {
    gst_object_unref (stream->pad);
    stream->pad = NULL;
  }
  if (stream->queue) {
    g_object_unref (stream->queue);
    stream->queue = NULL;
  }

  g_free (stream);
}

static void
gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose)
{
  GSList *iter;

  GST_DEBUG_OBJECT (demux, "Resetting demux");

  demux->end_of_period = FALSE;
  demux->end_of_manifest = FALSE;

  demux->cancelled = TRUE;
  gst_dash_demux_stop (demux);
  if (demux->downloader)
    gst_uri_downloader_reset (demux->downloader);

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    if (stream->pad) {
      GST_INFO_OBJECT (demux, "Removing stream pad %s:%s",
          GST_DEBUG_PAD_NAME (stream->pad));
      gst_element_remove_pad (GST_ELEMENT (demux), stream->pad);
    }
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

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->last_manifest_update = GST_CLOCK_TIME_NONE;
  demux->position = 0;
  demux->position_shift = 0;
  demux->cancelled = FALSE;
}

static GstClockTime
gst_dash_demux_get_buffering_time (GstDashDemux * demux)
{
  GstClockTime buffer_time = GST_CLOCK_TIME_NONE;
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstClockTime btime = gst_dash_demux_stream_get_buffering_time (iter->data);

    if (!GST_CLOCK_TIME_IS_VALID (buffer_time) || buffer_time > btime)
      buffer_time = btime;
  }

  if (!GST_CLOCK_TIME_IS_VALID (buffer_time))
    buffer_time = 0;
  return buffer_time;
}

static GstClockTime
gst_dash_demux_stream_get_buffering_time (GstDashDemuxStream * stream)
{
  GstDataQueueSize level;

  gst_data_queue_get_level (stream->queue, &level);

  return (GstClockTime) level.time;
}

static gboolean
gst_dash_demux_all_streams_have_data (GstDashDemux * demux)
{
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    if (!stream->has_data_queued)
      return FALSE;
  }

  return TRUE;
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
  GstClock *clock = gst_element_get_clock (GST_ELEMENT (demux));
  gint64 update_period = demux->client->mpd_node->minimumUpdatePeriod;

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


  GST_DEBUG_OBJECT (demux, "download loop %i", demux->end_of_manifest);

  /* try to switch to another set of representations if needed */
  if (gst_dash_demux_all_streams_have_data (demux)) {
    gst_dash_demux_select_representations (demux);
  }

  /* fetch the next fragment */
  while (!gst_dash_demux_get_next_fragment (demux)) {
    if (demux->end_of_period) {
      GST_INFO_OBJECT (demux, "Reached the end of the Period");
      /* setup video, audio and subtitle streams, starting from the next Period */
      if (!gst_mpd_client_set_period_index (demux->client,
              gst_mpd_client_get_period_index (demux->client) + 1)
          || !gst_dash_demux_setup_all_streams (demux)) {
        GST_INFO_OBJECT (demux, "Reached the end of the manifest file");
        demux->end_of_manifest = TRUE;
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
    } else if (demux->cancelled) {
      goto cancelled;
    } else {
      goto quit;
    }
  }
  GST_INFO_OBJECT (demux, "Internal buffering : %" PRIu64 " s",
      gst_dash_demux_get_buffering_time (demux) / GST_SECOND);
  demux->client->update_failed_count = 0;

quit:
  return;

cancelled:
  {
    GST_WARNING_OBJECT (demux, "Cancelled, leaving download task");
    gst_task_stop (demux->download_task);
    return;
  }

end_of_manifest:
  {
    GST_INFO_OBJECT (demux, "End of manifest, leaving download task");
    gst_task_stop (demux->download_task);
    return;
  }

error_downloading:
  {
    GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
        ("Could not fetch the next fragment, leaving download task"), (NULL));
    gst_task_stop (demux->download_task);
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
  gst_task_start (demux->download_task);
}

static void
gst_dash_demux_prepare_pad_switch (GstDashDemux * demux)
{
  GSList *iter;
  GstDashDemuxStream *stream;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstActiveStream *active_stream;
    GstCaps *caps;
    stream = iter->data;

    active_stream =
        gst_mpdparser_get_active_stream_by_index (demux->client, stream->index);
    caps = gst_dash_demux_get_input_caps (demux, active_stream);

    GST_DEBUG_OBJECT (demux, "Setting need header for stream %p", stream);
    stream->need_header = TRUE;

    g_assert (caps != NULL);
    gst_dash_demux_stream_push_event (stream,
        gst_event_new_dash_event_pad_switch (caps));
    stream->has_data_queued = FALSE;
  }
}

/* gst_dash_demux_select_representations:
 *
 * Select the most appropriate media representations based on current target 
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
gst_dash_demux_select_representations (GstDashDemux * demux)
{
  GstActiveStream *active_stream = NULL;
  GList *rep_list = NULL;
  gint new_index;
  gboolean ret = FALSE;
  GSList *iter;
  GstDashDemuxStream *stream;

  guint i = 0;


  GST_MPD_CLIENT_LOCK (demux->client);
  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    guint64 bitrate;
    stream = iter->data;
    active_stream =
        gst_mpdparser_get_active_stream_by_index (demux->client, stream->index);
    if (!active_stream)
      return FALSE;

    /* retrieve representation list */
    if (active_stream->cur_adapt_set)
      rep_list = active_stream->cur_adapt_set->Representations;
    if (!rep_list)
      return FALSE;

    bitrate =
        gst_download_rate_get_current_rate (&stream->dnl_rate) *
        demux->bandwidth_usage;
    GST_DEBUG_OBJECT (demux, "Trying to change to bitrate: %llu", bitrate);

    /* get representation index with current max_bandwidth */
    new_index =
        gst_mpdparser_get_rep_idx_with_max_bandwidth (rep_list, bitrate);

    /* if no representation has the required bandwidth, take the lowest one */
    if (new_index == -1)
      new_index = gst_mpdparser_get_rep_idx_with_min_bandwidth (rep_list);

    if (new_index != active_stream->representation_idx) {
      GstRepresentationNode *rep = g_list_nth_data (rep_list, new_index);
      GST_INFO_OBJECT (demux, "Changing representation idx: %d %d %u",
          stream->index, new_index, rep->bandwidth);
      if (gst_mpd_client_setup_representation (demux->client, active_stream,
              rep)) {
        ret = TRUE;
        GST_INFO_OBJECT (demux, "Switching bitrate to %d",
            active_stream->cur_representation->bandwidth);
      } else {
        GST_WARNING_OBJECT (demux,
            "Can not switch representation, aborting...");
      }
    }
    i++;
  }
  if (ret)
    gst_dash_demux_prepare_pad_switch (demux);
  GST_MPD_CLIENT_UNLOCK (demux->client);
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

/* gst_dash_demux_get_next_fragment:
 *
 * Get the next fragments for the stream with the earlier timestamp.
 * 
 * This function uses the generic URI downloader API.
 *
 * Returns FALSE if an error occured while downloading fragments
 * 
 */
static gboolean
gst_dash_demux_get_next_fragment (GstDashDemux * demux)
{
  GstActiveStream *active_stream;
  GstFragment *download, *header;
  gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;
  GTimeVal now;
  GTimeVal start;
  GstClockTime diff;
  guint64 size_buffer = 0;
  GSList *iter;
  gboolean end_of_period = TRUE;
  GstDashDemuxStream *selected_stream = NULL;
  GstClockTime best_time = GST_CLOCK_TIME_NONE;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;
    GstClockTime ts;

    if (stream->download_end_of_period)
      continue;

    if (gst_mpd_client_get_next_fragment_timestamp (demux->client,
            stream->index, &ts)) {
      if (ts < best_time || !GST_CLOCK_TIME_IS_VALID (best_time)) {
        selected_stream = stream;
        best_time = ts;
      }
    } else {
      GstEvent *event = NULL;

      GST_INFO_OBJECT (demux,
          "This Period doesn't contain more fragments for stream %u",
          stream->index);
      if (gst_mpd_client_has_next_period (demux->client)) {
        event = gst_event_new_dash_eop ();
      } else {
        event = gst_event_new_eos ();
      }
      stream->download_end_of_period = TRUE;
      gst_dash_demux_stream_push_event (stream, event);
    }
  }

  /* Get the fragment corresponding to each stream index */
  if (selected_stream) {
    guint stream_idx = selected_stream->index;
    GstBuffer *buffer;

    if (gst_mpd_client_get_next_fragment (demux->client,
            stream_idx, &discont, &next_fragment_uri, &duration, &timestamp)) {

      g_get_current_time (&start);
      GST_INFO_OBJECT (demux, "Next fragment for stream #%i", stream_idx);
      GST_INFO_OBJECT (demux,
          "Fetching next fragment %s ts:%" GST_TIME_FORMAT " dur:%"
          GST_TIME_FORMAT, next_fragment_uri, GST_TIME_ARGS (timestamp),
          GST_TIME_ARGS (duration));

      /* got a fragment to fetch, no end of period */
      end_of_period = FALSE;

      download = gst_uri_downloader_fetch_uri (demux->downloader,
          next_fragment_uri);
      g_free (next_fragment_uri);

      if (download == NULL)
        return FALSE;

      active_stream =
          gst_mpdparser_get_active_stream_by_index (demux->client, stream_idx);
      if (active_stream == NULL)        /* TODO unref fragments */
        return FALSE;

      buffer = gst_fragment_get_buffer (download);

      if (selected_stream->need_header) {
        /* We need to fetch a new header */
        if ((header =
                gst_dash_demux_get_next_header (demux, stream_idx)) == NULL) {
          GST_WARNING_OBJECT (demux, "Unable to fetch header");
        } else {
          GstBuffer *header_buffer;
          /* Replace fragment with a new one including the header */

          header_buffer = gst_fragment_get_buffer (header);
          buffer = gst_buffer_join (header_buffer, buffer);
        }
        selected_stream->need_header = FALSE;
      }
      g_get_current_time (&now);

      buffer = gst_buffer_make_metadata_writable (buffer);

      GST_BUFFER_TIMESTAMP (buffer) = timestamp;
      GST_BUFFER_DURATION (buffer) = duration;
      GST_BUFFER_OFFSET (buffer) =
          gst_mpd_client_get_segment_index (active_stream) - 1;

      gst_buffer_set_caps (buffer, selected_stream->input_caps);
      gst_dash_demux_stream_push_data (selected_stream, buffer);
      selected_stream->has_data_queued = TRUE;
      size_buffer += GST_BUFFER_SIZE (buffer);
    } else {
      GST_WARNING_OBJECT (demux, "Failed to download fragment for stream %p %d",
          selected_stream, selected_stream->index);
    }
  }

  demux->end_of_period = end_of_period;
  if (end_of_period)
    return FALSE;

  /* Wake the download task up */
  GST_TASK_SIGNAL (demux->download_task);
  if (selected_stream) {
    guint64 brate;

    diff = (GST_TIMEVAL_TO_TIME (now) - GST_TIMEVAL_TO_TIME (start));
    gst_download_rate_add_rate (&selected_stream->dnl_rate, size_buffer, diff);

    brate = (size_buffer * 8) / ((double) diff / GST_SECOND);
    GST_INFO_OBJECT (demux,
        "Stream: %d Download rate = %" PRIu64 " Kbits/s (%" PRIu64
        " Ko in %.2f s)", selected_stream->index,
        brate / 1000, size_buffer / 1024, ((double) diff / GST_SECOND));
  }
  return TRUE;
}
