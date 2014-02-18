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
#include "gstdashdemux.h"
#include "gstdash_debug.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
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

#define DEFAULT_FAILED_COUNT 3
#define DOWNLOAD_RATE_HISTORY_MAX 3

/* Custom internal event to signal end of period */
#define GST_EVENT_DASH_EOP GST_EVENT_MAKE_TYPE(81, GST_EVENT_TYPE_DOWNSTREAM | GST_EVENT_TYPE_SERIALIZED)
static GstEvent *
gst_event_new_dash_eop (void)
{
  return gst_event_new_custom (GST_EVENT_DASH_EOP, NULL);
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
static GstFlowReturn gst_dash_demux_pad (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_dash_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dash_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dash_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void gst_dash_demux_stream_loop (GstDashDemux * demux);
static void gst_dash_demux_download_loop (GstDashDemux * demux);
static void gst_dash_demux_stop (GstDashDemux * demux);
static void gst_dash_demux_resume_stream_task (GstDashDemux * demux);
static void gst_dash_demux_resume_download_task (GstDashDemux * demux);
static gboolean gst_dash_demux_setup_all_streams (GstDashDemux * demux);
static gboolean gst_dash_demux_select_representations (GstDashDemux * demux);
static gboolean gst_dash_demux_get_next_fragment (GstDashDemux * demux,
    GstActiveStream ** stream, GstClockTime * next_ts);
static gboolean gst_dash_demux_advance_period (GstDashDemux * demux);
static void gst_dash_demux_download_wait (GstDashDemux * demux,
    GstClockTime time_diff);

static void gst_dash_demux_expose_streams (GstDashDemux * demux);
static void gst_dash_demux_remove_streams (GstDashDemux * demux,
    GSList * streams);
static void gst_dash_demux_stream_free (GstDashDemuxStream * stream);
static void gst_dash_demux_reset (GstDashDemux * demux, gboolean dispose);
#ifndef GST_DISABLE_GST_DEBUG
static GstClockTime gst_dash_demux_get_buffering_time (GstDashDemux * demux);
static GstClockTime gst_dash_demux_stream_get_buffering_time (GstDashDemuxStream
    * stream);
#endif
static GstCaps *gst_dash_demux_get_input_caps (GstDashDemux * demux,
    GstActiveStream * stream);
static GstPad *gst_dash_demux_create_pad (GstDashDemux * demux);

#define gst_dash_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDashDemux, gst_dash_demux, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "dashdemux", 0,
        "dashdemux element"););

static void
gst_dash_demux_dispose (GObject * obj)
{
  GstDashDemux *demux = GST_DASH_DEMUX (obj);

  gst_dash_demux_reset (demux, TRUE);

  if (demux->stream_task) {
    gst_object_unref (demux->stream_task);
    g_rec_mutex_clear (&demux->stream_task_lock);
    demux->stream_task = NULL;
  }

  if (demux->download_task) {
    gst_object_unref (demux->download_task);
    g_rec_mutex_clear (&demux->download_task_lock);
    demux->download_task = NULL;
  }
  g_cond_clear (&demux->download_cond);
  g_mutex_clear (&demux->download_mutex);

  if (demux->downloader != NULL) {
    g_object_unref (demux->downloader);
    demux->downloader = NULL;
  }

  g_mutex_clear (&demux->streams_lock);

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

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "DASH Demuxer",
      "Codec/Demuxer",
      "Dynamic Adaptive Streaming over HTTP demuxer",
      "David Corvoysier <david.corvoysier@orange.com>\n\
                Hamid Zakari <hamid.zakari@gmail.com>\n\
                Gianluca Gennari <gennarone@gmail.com>");
}

static void
gst_dash_demux_init (GstDashDemux * demux)
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
  demux->last_manifest_update = GST_CLOCK_TIME_NONE;

  /* Updates task */
  g_rec_mutex_init (&demux->download_task_lock);
  demux->download_task =
      gst_task_new ((GstTaskFunction) gst_dash_demux_download_loop, demux,
      NULL);
  gst_task_set_lock (demux->download_task, &demux->download_task_lock);
  g_cond_init (&demux->download_cond);
  g_mutex_init (&demux->download_mutex);

  /* Streaming task */
  g_rec_mutex_init (&demux->stream_task_lock);
  demux->stream_task =
      gst_task_new ((GstTaskFunction) gst_dash_demux_stream_loop, demux, NULL);
  gst_task_set_lock (demux->stream_task, &demux->stream_task_lock);

  g_mutex_init (&demux->streams_lock);
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
      g_value_set_uint (value, demux->max_buffering_time / GST_SECOND);
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
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
_check_queue_full (GstDataQueue * q, guint visible, guint bytes, guint64 time,
    GstDashDemux * demux)
{
  return time >= demux->max_buffering_time;
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

  gst_data_queue_push_force (stream->queue, item);
}

static void
gst_dash_demux_stream_push_data (GstDashDemuxStream * stream,
    GstBuffer * fragment)
{
  GstDataQueueItem *item = g_new (GstDataQueueItem, 1);

  item->object = GST_MINI_OBJECT_CAST (fragment);
  item->duration = GST_BUFFER_DURATION (fragment);
  item->visible = TRUE;
  item->size = gst_buffer_get_size (fragment);

  item->destroy = (GDestroyNotify) _data_queue_item_destroy;

  gst_data_queue_push (stream->queue, item);
}

static gboolean
gst_dash_demux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDashDemux *demux;

  demux = GST_DASH_DEMUX (parent);

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

      GST_INFO_OBJECT (demux, "Received seek event");

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

      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
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
        g_rec_mutex_lock (&demux->stream_task_lock);

        /* select the requested Period in the Media Presentation */
        target_pos = (GstClockTime) demux->segment.start;
        GST_DEBUG_OBJECT (demux, "Seeking to target %" GST_TIME_FORMAT,
            GST_TIME_ARGS (target_pos));
        current_period = 0;
        for (list = g_list_first (demux->client->periods); list;
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
        if (current_period != gst_mpd_client_get_period_index (demux->client)) {
          GSList *streams = NULL;

          GST_DEBUG_OBJECT (demux, "Seeking to Period %d", current_period);
          streams = demux->streams;
          demux->streams = NULL;
          /* clean old active stream list, if any */
          gst_active_streams_free (demux->client);

          /* setup video, audio and subtitle streams, starting from the new Period */
          if (!gst_mpd_client_set_period_index (demux->client, current_period)
              || !gst_dash_demux_setup_all_streams (demux))
            return FALSE;

          gst_dash_demux_expose_streams (demux);

          gst_dash_demux_remove_streams (demux, streams);
        }

        /* Update the current sequence on all streams */
        for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream = iter->data;
          gint seg_i;

          active_stream =
              gst_mpdparser_get_active_stream_by_index (demux->client,
              stream->index);
          current_pos = 0;
          current_sequence = 0;
          for (seg_i = 0; seg_i < active_stream->segments->len; seg_i++) {
            chunk = g_ptr_array_index (active_stream->segments, seg_i);
            current_pos = chunk->start_time;
            /* current_sequence = chunk->number; */
            GST_DEBUG_OBJECT (demux, "current_pos:%" GST_TIME_FORMAT
                " <= target_pos:%" GST_TIME_FORMAT " duration:%"
                GST_TIME_FORMAT, GST_TIME_ARGS (current_pos),
                GST_TIME_ARGS (target_pos), GST_TIME_ARGS (chunk->duration));
            if (current_pos <= target_pos
                && target_pos < current_pos + chunk->duration) {
              GST_DEBUG_OBJECT (demux,
                  "selecting sequence %d for stream %" GST_PTR_FORMAT,
                  current_sequence, stream);
              break;
            }
            current_sequence++;
          }
          gst_mpd_client_set_segment_index (active_stream, current_sequence);
        }

        if (flags & GST_SEEK_FLAG_FLUSH) {
          GST_DEBUG_OBJECT (demux, "Sending flush stop on all pad");
          for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
            GstDashDemuxStream *stream;

            stream = iter->data;
            stream->has_data_queued = FALSE;
            stream->need_header = TRUE;
            stream->download_end_of_period = FALSE;
            stream->stream_end_of_period = FALSE;
            stream->stream_eos = FALSE;
            gst_pad_push_event (stream->pad, gst_event_new_flush_stop (TRUE));
          }
        }

        /* Restart the demux */
        demux->cancelled = FALSE;
        demux->end_of_manifest = FALSE;
        for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream = iter->data;
          gst_data_queue_set_flushing (stream->queue, FALSE);
        }
        demux->timestamp_offset = 0;
        demux->need_segment = TRUE;
        gst_uri_downloader_reset (demux->downloader);
        GST_DEBUG_OBJECT (demux, "Resuming tasks after seeking");
        gst_dash_demux_resume_download_task (demux);
        gst_dash_demux_resume_stream_task (demux);
        g_rec_mutex_unlock (&demux->stream_task_lock);
      }

      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_dash_demux_setup_mpdparser_streams (GstDashDemux * demux,
    GstMpdClient * client)
{
  GList *listLang = NULL;
  guint i, nb_audio;
  gchar *lang;
  gboolean has_streams = FALSE;

  if (!gst_mpd_client_setup_streaming (client, GST_STREAM_VIDEO, ""))
    GST_INFO_OBJECT (demux, "No video adaptation set found");
  else
    has_streams = TRUE;

  nb_audio =
      gst_mpdparser_get_list_and_nb_of_audio_language (client, &listLang);
  if (nb_audio == 0)
    nb_audio = 1;
  GST_INFO_OBJECT (demux, "Number of languages is=%d", nb_audio);

  for (i = 0; i < nb_audio; i++) {
    lang = (gchar *) g_list_nth_data (listLang, i);
    GST_INFO ("nb adaptation set: %i",
        gst_mpdparser_get_nb_adaptationSet (client));
    if (!gst_mpd_client_setup_streaming (client, GST_STREAM_AUDIO, lang))
      GST_INFO_OBJECT (demux, "No audio adaptation set found");
    else
      has_streams = TRUE;

    if (gst_mpdparser_get_nb_adaptationSet (client) > nb_audio) {
      if (!gst_mpd_client_setup_streaming (client,
              GST_STREAM_APPLICATION, lang)) {
        GST_INFO_OBJECT (demux, "No application adaptation set found");
      } else {
        has_streams = TRUE;
      }
    }
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
  GSList *streams = NULL;

  GST_MPD_CLIENT_LOCK (demux->client);
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
    GstEvent *event;
    gchar *stream_id;

    active_stream = gst_mpdparser_get_active_stream_by_index (demux->client, i);
    if (active_stream == NULL)
      continue;

    stream = g_new0 (GstDashDemuxStream, 1);
    caps = gst_dash_demux_get_input_caps (demux, active_stream);
    stream->queue =
        gst_data_queue_new ((GstDataQueueCheckFullFunction) _check_queue_full,
        NULL, NULL, demux);

    stream->index = i;
    stream->input_caps = caps;
    stream->need_header = TRUE;
    stream->has_data_queued = FALSE;
    gst_download_rate_init (&stream->dnl_rate);
    gst_download_rate_set_max_length (&stream->dnl_rate,
        DOWNLOAD_RATE_HISTORY_MAX);

    GST_LOG_OBJECT (demux, "Creating stream %d %" GST_PTR_FORMAT, i, caps);
    streams = g_slist_prepend (streams, stream);
    stream->pad = gst_dash_demux_create_pad (demux);

    stream_id =
        gst_pad_create_stream_id_printf (stream->pad,
        GST_ELEMENT_CAST (demux), "%d", i);

    event =
        gst_pad_get_sticky_event (demux->sinkpad, GST_EVENT_STREAM_START, 0);
    if (event) {
      if (gst_event_parse_group_id (event, &demux->group_id))
        demux->have_group_id = TRUE;
      else
        demux->have_group_id = FALSE;
      gst_event_unref (event);
    } else if (!demux->have_group_id) {
      demux->have_group_id = TRUE;
      demux->group_id = gst_util_group_id_next ();
    }
    event = gst_event_new_stream_start (stream_id);
    if (demux->have_group_id)
      gst_event_set_group_id (event, demux->group_id);

    gst_pad_push_event (stream->pad, event);
    g_free (stream_id);

    gst_dash_demux_stream_push_event (stream, gst_event_new_caps (caps));
  }
  streams = g_slist_reverse (streams);

  demux->next_periods = g_slist_append (demux->next_periods, streams);
  GST_MPD_CLIENT_UNLOCK (demux->client);

  return TRUE;
}

static gboolean
gst_dash_demux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDashDemux *demux = GST_DASH_DEMUX (parent);

  switch (event->type) {
    case GST_EVENT_FLUSH_STOP:
      gst_dash_demux_reset (demux, FALSE);
      break;
    case GST_EVENT_EOS:{
      gchar *manifest;
      GstQuery *query;
      gboolean query_res;
      gboolean ret = TRUE;
      GstMapInfo mapinfo;

      if (demux->manifest == NULL) {
        GST_WARNING_OBJECT (demux, "Received EOS without a manifest.");
        break;
      }

      GST_DEBUG_OBJECT (demux, "Got EOS on the sink pad: manifest fetched");

      if (demux->client)
        gst_mpd_client_free (demux->client);
      demux->client = gst_mpd_client_new ();

      query = gst_query_new_uri ();
      query_res = gst_pad_peer_query (pad, query);
      if (query_res) {
        gst_query_parse_uri (query, &demux->client->mpd_uri);
        GST_DEBUG_OBJECT (demux, "Fetched MPD file at URI: %s",
            demux->client->mpd_uri);
      } else {
        GST_WARNING_OBJECT (demux, "MPD URI query failed.");
      }
      gst_query_unref (query);

      if (gst_buffer_map (demux->manifest, &mapinfo, GST_MAP_READ)) {
        manifest = (gchar *) mapinfo.data;
        if (!gst_mpd_parse (demux->client, manifest, mapinfo.size)) {
          /* In most cases, this will happen if we set a wrong url in the
           * source element and we have received the 404 HTML response instead of
           * the manifest */
          GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid manifest."),
              (NULL));
          ret = FALSE;
        }
        gst_buffer_unmap (demux->manifest, &mapinfo);
      } else {
        GST_WARNING_OBJECT (demux, "Error validating the manifest.");
        ret = FALSE;
      }
      gst_buffer_unref (demux->manifest);
      demux->manifest = NULL;

      if (!ret)
        goto seek_quit;

      if (!gst_mpd_client_setup_media_presentation (demux->client)) {
        GST_ELEMENT_ERROR (demux, STREAM, DECODE,
            ("Incompatible manifest file."), (NULL));
        ret = FALSE;
        goto seek_quit;
      }

      /* setup video, audio and subtitle streams, starting from first Period */
      if (!gst_mpd_client_set_period_index (demux->client, 0) ||
          !gst_dash_demux_setup_all_streams (demux)) {
        ret = FALSE;
        goto seek_quit;
      }

      gst_dash_demux_advance_period (demux);

      /* If stream is live, try to find the segment that is closest to current time */
      if (gst_mpd_client_is_live (demux->client)) {
        GSList *iter;
        GstDateTime *now = gst_date_time_new_now_utc ();
        gint seg_idx;

        GST_DEBUG_OBJECT (demux,
            "Seeking to current time of day for live stream ");
        if (demux->client->mpd_node->suggestedPresentationDelay != -1) {
          GstDateTime *target = gst_mpd_client_add_time_difference (now,
              demux->client->mpd_node->suggestedPresentationDelay * -1000);
          gst_date_time_unref (now);
          now = target;
        }
        for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream = iter->data;
          GstActiveStream *active_stream;

          active_stream =
              gst_mpdparser_get_active_stream_by_index (demux->client,
              stream->index);

          /* Get segment index corresponding to current time. */
          seg_idx =
              gst_mpd_client_get_segment_index_at_time (demux->client,
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

        gst_date_time_unref (now);
      } else {
        GST_DEBUG_OBJECT (demux,
            "Seeking to first segment for on-demand stream ");

        /* start playing from the first segment */
        gst_mpd_client_set_segment_index_for_all_streams (demux->client, 0);
      }

      /* Send duration message */
      if (!gst_mpd_client_is_live (demux->client)) {
        GstClockTime duration =
            gst_mpd_client_get_media_presentation_duration (demux->client);

        if (duration != GST_CLOCK_TIME_NONE) {
          GST_DEBUG_OBJECT (demux,
              "Sending duration message : %" GST_TIME_FORMAT,
              GST_TIME_ARGS (duration));
          gst_element_post_message (GST_ELEMENT (demux),
              gst_message_new_duration_changed (GST_OBJECT (demux)));
        } else {
          GST_DEBUG_OBJECT (demux,
              "mediaPresentationDuration unknown, can not send the duration message");
        }
      }
      demux->timestamp_offset = -1;
      demux->need_segment = TRUE;
      gst_dash_demux_resume_download_task (demux);
      gst_dash_demux_resume_stream_task (demux);

    seek_quit:
      gst_event_unref (event);
      return ret;
    }
    case GST_EVENT_SEGMENT:
      /* Swallow newsegments, we'll push our own */
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_dash_demux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDashDemux *dashdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  dashdemux = GST_DASH_DEMUX (parent);

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
          "Received GST_QUERY_SEEKING with format %d - %" G_GINT64_FORMAT
          " %" G_GINT64_FORMAT, fmt, start, end);
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
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;

      gst_query_parse_latency (query, &live, &min, &max);

      if (dashdemux->client && gst_mpd_client_is_live (dashdemux->client))
        live = TRUE;

      if (dashdemux->max_buffering_time > 0)
        max += dashdemux->max_buffering_time;

      gst_query_set_latency (query, live, min, max);
      break;
    }
    default:{
      /* By default, do not forward queries upstream */
      break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_dash_demux_pad (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDashDemux *demux = GST_DASH_DEMUX (parent);

  if (demux->manifest == NULL)
    demux->manifest = buf;
  else
    demux->manifest = gst_buffer_append (demux->manifest, buf);

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
    g_mutex_lock (&demux->download_mutex);
    g_cond_signal (&demux->download_cond);
    g_mutex_unlock (&demux->download_mutex);
    g_rec_mutex_lock (&demux->download_task_lock);
    g_rec_mutex_unlock (&demux->download_task_lock);
    gst_task_join (demux->download_task);
  }
  if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED) {
    GST_TASK_SIGNAL (demux->stream_task);
    gst_task_stop (demux->stream_task);
    g_rec_mutex_lock (&demux->stream_task_lock);
    g_rec_mutex_unlock (&demux->stream_task_lock);
    gst_task_join (demux->stream_task);
  }

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    gst_data_queue_flush (stream->queue);
    stream->has_data_queued = FALSE;
  }
}

static GstPad *
gst_dash_demux_create_pad (GstDashDemux * demux)
{
  GstPad *pad;

  /* Create and activate new pads */
  pad = gst_pad_new_from_static_template (&srctemplate, NULL);
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_dash_demux_src_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_dash_demux_src_query));
  gst_pad_set_element_private (pad, demux);
  gst_pad_set_active (pad, TRUE);
  GST_INFO_OBJECT (demux, "Creating srcpad %s:%s", GST_DEBUG_PAD_NAME (pad));
  return pad;
}

static void
gst_dash_demux_expose_streams (GstDashDemux * demux)
{
  GSList *iter;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;

    GST_LOG_OBJECT (demux, "Exposing stream %d %" GST_PTR_FORMAT, stream->index,
        stream->input_caps);
    gst_element_add_pad (GST_ELEMENT (demux), gst_object_ref (stream->pad));
  }
  gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
}

static void
gst_dash_demux_remove_streams (GstDashDemux * demux, GSList * streams)
{
  GSList *iter;
  GstEvent *eos = gst_event_new_eos ();

  for (iter = streams; iter; iter = g_slist_next (iter)) {
    GstDashDemuxStream *stream = iter->data;;

    GST_LOG_OBJECT (demux, "Removing stream %d %" GST_PTR_FORMAT, stream->index,
        stream->input_caps);
    gst_pad_push_event (stream->pad, gst_event_ref (eos));
    gst_pad_set_active (stream->pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (demux), stream->pad);
    gst_dash_demux_stream_free (stream);
  }
  gst_event_unref (eos);
  g_slist_free (streams);
}

static gboolean
gst_dash_demux_advance_period (GstDashDemux * demux)
{
  GSList *old_period = NULL;
  g_mutex_lock (&demux->streams_lock);

  GST_DEBUG_OBJECT (demux, "Advancing period from %p", demux->streams);

  if (demux->streams) {
    g_assert (demux->streams == demux->next_periods->data);

    demux->next_periods = g_slist_remove (demux->next_periods, demux->streams);
    old_period = demux->streams;
    demux->streams = NULL;
  }

  GST_DEBUG_OBJECT (demux, "Next period %p", demux->next_periods);

  if (demux->next_periods) {
    demux->streams = demux->next_periods->data;
  } else {
    GST_DEBUG_OBJECT (demux, "No next periods, return FALSE");
    g_mutex_unlock (&demux->streams_lock);
    return FALSE;
  }

  gst_dash_demux_expose_streams (demux);
  gst_dash_demux_remove_streams (demux, old_period);

  g_mutex_unlock (&demux->streams_lock);
  return TRUE;
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
  GSList *iter;
  GstClockTime best_time;
  GstDashDemuxStream *selected_stream;
  gboolean eos = TRUE;
  gboolean eop = TRUE;

  GST_LOG_OBJECT (demux, "Starting stream loop");

  best_time = GST_CLOCK_TIME_NONE;
  selected_stream = NULL;

  for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
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
      GstBuffer *buffer;
      GstClockTime timestamp;

      buffer = GST_BUFFER_CAST (item->object);
      timestamp = GST_BUFFER_TIMESTAMP (buffer);

      GST_LOG_OBJECT (demux, "Buffer with time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));

      if (timestamp < best_time) {
        GST_DEBUG_OBJECT (demux, "Found new best time: %" GST_TIME_FORMAT " %p",
            GST_TIME_ARGS (timestamp), buffer);
        best_time = timestamp;
        selected_stream = stream;
      } else if (!GST_CLOCK_TIME_IS_VALID (timestamp)) {
        selected_stream = stream;
        GST_DEBUG_OBJECT (demux, "Buffer without timestamp selected %p",
            buffer);
        break;
      }
    } else {
      GST_DEBUG_OBJECT (demux, "Non buffers have preference %" GST_PTR_FORMAT, item->object);
      selected_stream = stream;
      break;
    }
  }

  if (selected_stream) {
    GstDataQueueItem *item;

    GST_DEBUG_OBJECT (demux, "Selected stream %p %d", selected_stream,
        selected_stream->index);

    if (!gst_data_queue_pop (selected_stream->queue, &item))
      goto end;

    if (G_LIKELY (GST_IS_BUFFER (item->object))) {
      GstBuffer *buffer;
      GstClockTime timestamp;

      buffer = GST_BUFFER_CAST (item->object);
      active_stream =
          gst_mpdparser_get_active_stream_by_index (demux->client,
          selected_stream->index);

      timestamp = GST_BUFFER_TIMESTAMP (buffer);

      if (demux->need_segment) {
        if (demux->timestamp_offset == -1)
          demux->timestamp_offset = timestamp;

        /* And send a newsegment */
        for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
          GstDashDemuxStream *stream = iter->data;
          gst_pad_push_event (stream->pad,
              gst_event_new_segment (&demux->segment));
        }
        demux->need_segment = FALSE;
      }
      /* make timestamp start from 0 by subtracting the offset */
      timestamp -= demux->timestamp_offset;

      GST_BUFFER_TIMESTAMP (buffer) = timestamp;

      GST_DEBUG_OBJECT (demux,
          "Pushing fragment ts: %" GST_TIME_FORMAT " at pad %s",
          GST_TIME_ARGS (timestamp), GST_PAD_NAME (selected_stream->pad));
#if 0
      GST_DEBUG_OBJECT (demux,
          "Pushing fragment %p #%d (stream %i) ts:%" GST_TIME_FORMAT " dur:%"
          GST_TIME_FORMAT " at pad: %s:%s", buffer, GST_BUFFER_OFFSET (buffer),
          selected_stream->index, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
          GST_DEBUG_PAD_NAME (selected_stream->pad));
#endif
      ret = gst_pad_push (selected_stream->pad, gst_buffer_ref (buffer));
      demux->segment.position = timestamp;

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
      gst_dash_demux_advance_period (demux);
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

  if (demux->next_periods) {
    g_assert (demux->next_periods->data == demux->streams);
    demux->next_periods =
        g_slist_delete_link (demux->next_periods, demux->next_periods);
  }

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

  for (iter = demux->next_periods; iter; iter = g_slist_next (iter)) {
    GSList *streams = iter->data;
    g_slist_free_full (streams, (GDestroyNotify) gst_dash_demux_stream_free);
  }
  g_slist_free (demux->next_periods);
  demux->next_periods = NULL;

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
  demux->cancelled = FALSE;
}

#ifndef GST_DISABLE_GST_DEBUG
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
#endif

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

static GstFlowReturn
gst_dash_demux_refresh_mpd (GstDashDemux * demux)
{
  GstFragment *download;
  GstBuffer *buffer;
  GstClockTime duration, now = gst_util_get_timestamp ();
  gint64 update_period = demux->client->mpd_node->minimumUpdatePeriod;

  if (update_period == -1) {
    GST_DEBUG_OBJECT (demux, "minimumUpdatePeriod unspecified, "
        "will not update MPD");
    return GST_FLOW_OK;
  }

  /* init reference time for manifest file updates */
  if (!GST_CLOCK_TIME_IS_VALID (demux->last_manifest_update))
    demux->last_manifest_update = now;

  GST_DEBUG_OBJECT (demux,
      "Next update: %" GST_TIME_FORMAT " now: %" GST_TIME_FORMAT,
      GST_TIME_ARGS ((demux->last_manifest_update +
              update_period * GST_MSECOND)), GST_TIME_ARGS (now));

  /* update the manifest file */
  if (now >= demux->last_manifest_update + update_period * GST_MSECOND) {
    GST_DEBUG_OBJECT (demux, "Updating manifest file from URL %s",
        demux->client->mpd_uri);
    download = gst_uri_downloader_fetch_uri (demux->downloader,
        demux->client->mpd_uri);
    if (download) {
      GstMpdClient *new_client = NULL;

      buffer = gst_fragment_get_buffer (download);
      g_object_unref (download);
      /* parse the manifest file */
      if (buffer != NULL) {
        GstMapInfo mapinfo;

        new_client = gst_mpd_client_new ();
        new_client->mpd_uri = g_strdup (demux->client->mpd_uri);

        gst_buffer_map (buffer, &mapinfo, GST_MAP_READ);

        if (gst_mpd_parse (new_client, (gchar *) mapinfo.data, mapinfo.size)) {
          const gchar *period_id;
          guint period_idx;
          GSList *iter;

          /* prepare the new manifest and try to transfer the stream position
           * status from the old manifest client  */

          gst_buffer_unmap (buffer, &mapinfo);
          gst_buffer_unref (buffer);

          GST_DEBUG_OBJECT (demux, "Updating manifest");

          period_id = gst_mpd_client_get_period_id (demux->client);
          period_idx = gst_mpd_client_get_period_index (demux->client);

          /* setup video, audio and subtitle streams, starting from current Period */
          if (!gst_mpd_client_setup_media_presentation (new_client)) {
            /* TODO */
          }

          if (period_idx) {
            if (!gst_mpd_client_set_period_id (new_client, period_id)) {
              GST_DEBUG_OBJECT (demux,
                  "Error setting up the updated manifest file");
              return GST_FLOW_EOS;
            }
          } else {
            if (!gst_mpd_client_set_period_index (new_client, period_idx)) {
              GST_DEBUG_OBJECT (demux,
                  "Error setting up the updated manifest file");
              return GST_FLOW_EOS;
            }
          }

          if (!gst_dash_demux_setup_mpdparser_streams (demux, new_client)) {
            GST_ERROR_OBJECT (demux, "Failed to setup streams on manifest "
                "update");
            return GST_FLOW_ERROR;
          }

          /* update the streams to play from the next segment */
          for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
            GstDashDemuxStream *demux_stream = iter->data;
            GstActiveStream *new_stream;
            GstClockTime ts;

            new_stream =
                gst_mpdparser_get_active_stream_by_index (new_client,
                demux_stream->index);

            if (!new_stream) {
              GST_DEBUG_OBJECT (demux,
                  "Stream of index %d is missing from manifest update",
                  demux_stream->index);
              return GST_FLOW_EOS;
            }

            if (gst_mpd_client_get_next_fragment_timestamp (demux->client,
                    demux_stream->index, &ts)) {
              gst_mpd_client_stream_seek (new_client, new_stream, ts);
            } else
                if (gst_mpd_client_get_last_fragment_timestamp (demux->client,
                    demux_stream->index, &ts)) {
              /* try to set to the old timestamp + 1 */
              gst_mpd_client_stream_seek (new_client, new_stream, ts + 1);
            }
          }

          gst_mpd_client_free (demux->client);
          demux->client = new_client;

          /* Send an updated duration message */
          duration =
              gst_mpd_client_get_media_presentation_duration (demux->client);

          if (duration != GST_CLOCK_TIME_NONE) {
            GST_DEBUG_OBJECT (demux,
                "Sending duration message : %" GST_TIME_FORMAT,
                GST_TIME_ARGS (duration));
            gst_element_post_message (GST_ELEMENT (demux),
                gst_message_new_duration_changed (GST_OBJECT (demux)));
          } else {
            GST_DEBUG_OBJECT (demux,
                "mediaPresentationDuration unknown, can not send the duration message");
          }
          demux->last_manifest_update = gst_util_get_timestamp ();
          GST_DEBUG_OBJECT (demux, "Manifest file successfully updated");
        } else {
          /* In most cases, this will happen if we set a wrong url in the
           * source element and we have received the 404 HTML response instead of
           * the manifest */
          GST_WARNING_OBJECT (demux, "Error parsing the manifest.");
          gst_buffer_unmap (buffer, &mapinfo);
          gst_buffer_unref (buffer);
        }
      } else {
        /* download suceeded, but resulting buffer is NULL */
        GST_WARNING_OBJECT (demux, "Error validating the manifest.");
      }
    } else {
      /* download failed */
      GST_WARNING_OBJECT (demux,
          "Failed to update the manifest file from URL %s",
          demux->client->mpd_uri);
    }
  }
  return GST_FLOW_OK;
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
  GstClockTime fragment_ts = GST_CLOCK_TIME_NONE;
  GstActiveStream *fragment_stream = NULL;

  GST_LOG_OBJECT (demux, "Starting download loop");

  if (gst_mpd_client_is_live (demux->client)
      && demux->client->mpd_uri != NULL) {
    switch (gst_dash_demux_refresh_mpd (demux)) {
      case GST_FLOW_EOS:
        goto end_of_manifest;
      default:
        break;
    }
  }

  GST_DEBUG_OBJECT (demux, "download loop %i", demux->end_of_manifest);

  /* try to switch to another set of representations if needed */
  if (gst_dash_demux_all_streams_have_data (demux)) {
    gst_dash_demux_select_representations (demux);
  }

  /* fetch the next fragment */
  while (!gst_dash_demux_get_next_fragment (demux, &fragment_stream,
          &fragment_ts)) {
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
      /* Download failed 'by itself'
       * in case this is live, we might be ahead or before playback, where
       * segments don't exist (are still being created or were already deleted)
       * so we either wait or jump ahead */
      if (gst_mpd_client_is_live (demux->client)) {
        gint64 time_diff;
        gint pos;

        pos =
            gst_mpd_client_check_time_position (demux->client, fragment_stream,
            fragment_ts, &time_diff);
        GST_DEBUG_OBJECT (demux,
            "Checked position for fragment ts %" GST_TIME_FORMAT
            ", res: %d, diff: %" G_GINT64_FORMAT, GST_TIME_ARGS (fragment_ts),
            pos, time_diff);

        time_diff *= GST_USECOND;
        if (pos < 0) {
          /* we're behind, try moving to the 'present' */
          GDateTime *now = g_date_time_new_now_utc ();

          GST_DEBUG_OBJECT (demux,
              "Falling behind live stream, moving forward");
          gst_mpd_client_seek_to_time (demux->client, now);
          g_date_time_unref (now);

        } else if (pos > 0) {
          /* we're ahead, wait a little */

          GST_DEBUG_OBJECT (demux, "Waiting for next segment to be created");
          gst_mpd_client_set_segment_index (fragment_stream,
              fragment_stream->segment_idx - 1);
          gst_dash_demux_download_wait (demux, time_diff);
        } else {
          gst_mpd_client_set_segment_index (fragment_stream,
              fragment_stream->segment_idx - 1);
          demux->client->update_failed_count++;
        }
      } else {
        demux->client->update_failed_count++;
      }

      if (demux->client->update_failed_count < DEFAULT_FAILED_COUNT) {
        GST_WARNING_OBJECT (demux, "Could not fetch the next fragment");
        goto quit;
      } else {
        goto error_downloading;
      }
    } else if (demux->cancelled) {
      goto cancelled;
    }
  }

  GST_INFO_OBJECT (demux, "Internal buffering : %" G_GUINT64_FORMAT " s",
      gst_dash_demux_get_buffering_time (demux) / GST_SECOND);
  demux->client->update_failed_count = 0;

quit:
  GST_DEBUG_OBJECT (demux, "Finishing download loop");
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
gst_dash_demux_resume_stream_task (GstDashDemux * demux)
{
  gst_task_start (demux->stream_task);
}

static void
gst_dash_demux_resume_download_task (GstDashDemux * demux)
{
  gst_task_start (demux->download_task);
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
    GST_DEBUG_OBJECT (demux, "Trying to change to bitrate: %" G_GUINT64_FORMAT,
        bitrate);

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
        stream->need_header = TRUE;
        stream->has_data_queued = FALSE;
        GST_INFO_OBJECT (demux, "Switching bitrate to %d",
            active_stream->cur_representation->bandwidth);
        gst_caps_unref (stream->input_caps);
        stream->input_caps =
            gst_dash_demux_get_input_caps (demux, active_stream);
        gst_dash_demux_stream_push_event (stream,
            gst_event_new_caps (stream->input_caps));
      } else {
        GST_WARNING_OBJECT (demux,
            "Can not switch representation, aborting...");
      }
    }
    i++;
  }
  GST_MPD_CLIENT_UNLOCK (demux->client);
  return ret;
}

static GstBuffer *
gst_dash_demux_download_header_fragment (GstDashDemux * demux, guint stream_idx,
    gchar * path, gint64 range_start, gint64 range_end)
{
  GstBuffer *buffer = NULL;
  gchar *next_header_uri;
  GstFragment *fragment;

  if (strncmp (path, "http://", 7) != 0) {
    next_header_uri =
        g_strconcat (gst_mpdparser_get_baseURL (demux->client, stream_idx),
        path, NULL);
    g_free (path);
  } else {
    next_header_uri = path;
  }

  fragment = gst_uri_downloader_fetch_uri_with_range (demux->downloader,
      next_header_uri, range_start, range_end);
  g_free (next_header_uri);
  if (fragment) {
    buffer = gst_fragment_get_buffer (fragment);
    g_object_unref (fragment);
  }
  return buffer;
}

static GstBuffer *
gst_dash_demux_get_next_header (GstDashDemux * demux, guint stream_idx)
{
  gchar *initializationURL;
  GstBuffer *header_buffer, *index_buffer = NULL;
  gint64 range_start, range_end;

  if (!gst_mpd_client_get_next_header (demux->client, &initializationURL,
          stream_idx, &range_start, &range_end))
    return NULL;

  GST_INFO_OBJECT (demux, "Fetching header %s %" G_GINT64_FORMAT "-%"
      G_GINT64_FORMAT, initializationURL, range_start, range_end);
  header_buffer = gst_dash_demux_download_header_fragment (demux, stream_idx,
      initializationURL, range_start, range_end);

  /* check if we have an index */
  if (header_buffer
      && gst_mpd_client_get_next_header_index (demux->client,
          &initializationURL, stream_idx, &range_start, &range_end)) {
    GST_INFO_OBJECT (demux,
        "Fetching index %s %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT,
        initializationURL, range_start, range_end);
    index_buffer =
        gst_dash_demux_download_header_fragment (demux, stream_idx,
        initializationURL, range_start, range_end);
  }

  if (header_buffer == NULL) {
    GST_WARNING_OBJECT (demux, "Unable to fetch header");
    return NULL;
  }

  if (index_buffer) {
    header_buffer = gst_buffer_append (header_buffer, index_buffer);
  }

  return header_buffer;
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

/* gst_dash_demux_get_next_fragment:
 *
 * Get the next fragments for the stream with the earlier timestamp.
 * It returns the selected timestamp so the caller can deal with
 * sync issues in case the stream is live.
 * 
 * This function uses the generic URI downloader API.
 *
 * Returns FALSE if an error occured while downloading fragments
 * 
 */
static gboolean
gst_dash_demux_get_next_fragment (GstDashDemux * demux,
    GstActiveStream ** stream, GstClockTime * selected_ts)
{
  GstActiveStream *active_stream;
  GstFragment *download;
  GstBuffer *header_buffer;
  GTimeVal now;
  GTimeVal start;
  GstClockTime diff;
  guint64 size_buffer = 0;
  GSList *iter;
  gboolean end_of_period = TRUE;
  GstDashDemuxStream *selected_stream = NULL;
  GstClockTime best_time = GST_CLOCK_TIME_NONE;
  GSList *streams;

  g_mutex_lock (&demux->streams_lock);
  /* TODO add check */
  streams = g_slist_last (demux->next_periods)->data;
  g_mutex_unlock (&demux->streams_lock);

  for (iter = streams; iter; iter = g_slist_next (iter)) {
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

      /* check if this is live and we should wait for more data */
      if (gst_mpd_client_is_live (demux->client)
          && demux->client->mpd_node->minimumUpdatePeriod != -1) {
        end_of_period = FALSE;
        continue;
      }

      if (gst_mpd_client_has_next_period (demux->client)) {
        event = gst_event_new_dash_eop ();
      } else {
        GST_DEBUG_OBJECT (demux,
            "No more fragments or periods for this stream, setting EOS");
        event = gst_event_new_eos ();
      }
      stream->download_end_of_period = TRUE;
      gst_dash_demux_stream_push_event (stream, event);
    }
  }
  if (selected_ts)
    *selected_ts = best_time;
  if (stream && selected_stream)
    *stream =
        gst_mpdparser_get_active_stream_by_index (demux->client,
        selected_stream->index);

  /* Get the fragment corresponding to each stream index */
  if (selected_stream) {
    guint stream_idx = selected_stream->index;
    GstBuffer *buffer;
    GstMediaFragmentInfo fragment;

    if (gst_mpd_client_get_next_fragment (demux->client, stream_idx, &fragment)) {

      g_get_current_time (&start);
      GST_INFO_OBJECT (demux, "Next fragment for stream #%i", stream_idx);
      GST_INFO_OBJECT (demux,
          "Fetching next fragment %s ts:%" GST_TIME_FORMAT " dur:%"
          GST_TIME_FORMAT " Range:%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT,
          fragment.uri, GST_TIME_ARGS (fragment.timestamp),
          GST_TIME_ARGS (fragment.duration),
          fragment.range_start, fragment.range_end);

      /* got a fragment to fetch, no end of period */
      end_of_period = FALSE;

      download = gst_uri_downloader_fetch_uri_with_range (demux->downloader,
          fragment.uri, fragment.range_start, fragment.range_end);

      if (download == NULL) {
        gst_media_fragment_info_clear (&fragment);
        return FALSE;
      }

      active_stream =
          gst_mpdparser_get_active_stream_by_index (demux->client, stream_idx);
      if (active_stream == NULL) {
        gst_media_fragment_info_clear (&fragment);
        g_object_unref (download);
        return FALSE;
      }

      buffer = gst_fragment_get_buffer (download);
      g_object_unref (download);

      /* it is possible to have an index per fragment, so check and download */
      if (fragment.index_uri || fragment.index_range_start
          || fragment.index_range_end != -1) {
        const gchar *uri = fragment.index_uri;
        GstBuffer *index_buffer;

        if (!uri)               /* fallback to default media uri */
          uri = fragment.uri;

        GST_DEBUG_OBJECT (demux,
            "Fragment index download: %s %" G_GINT64_FORMAT "-%"
            G_GINT64_FORMAT, uri, fragment.index_range_start,
            fragment.index_range_end);
        download =
            gst_uri_downloader_fetch_uri_with_range (demux->downloader, uri,
            fragment.index_range_start, fragment.index_range_end);
        if (download) {
          index_buffer = gst_fragment_get_buffer (download);
          if (index_buffer)
            buffer = gst_buffer_append (index_buffer, buffer);
          g_object_unref (download);
        }
      }

      if (selected_stream->need_header) {
        /* We need to fetch a new header */
        if ((header_buffer =
                gst_dash_demux_get_next_header (demux, stream_idx)) != NULL) {
          buffer = gst_buffer_append (header_buffer, buffer);
        }
        selected_stream->need_header = FALSE;
      }
      g_get_current_time (&now);

      buffer = gst_buffer_make_writable (buffer);

      GST_BUFFER_TIMESTAMP (buffer) = fragment.timestamp;
      GST_BUFFER_DURATION (buffer) = fragment.duration;
      GST_BUFFER_OFFSET (buffer) =
          gst_mpd_client_get_segment_index (active_stream) - 1;

      gst_media_fragment_info_clear (&fragment);

      gst_dash_demux_stream_push_data (selected_stream, buffer);
      selected_stream->has_data_queued = TRUE;
      size_buffer += gst_buffer_get_size (buffer);
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
#ifndef GST_DISABLE_GST_DEBUG
    guint64 brate;
#endif

    diff = (GST_TIMEVAL_TO_TIME (now) - GST_TIMEVAL_TO_TIME (start));
    gst_download_rate_add_rate (&selected_stream->dnl_rate, size_buffer, diff);

#ifndef GST_DISABLE_GST_DEBUG
    brate = (size_buffer * 8) / ((double) diff / GST_SECOND);
#endif
    GST_INFO_OBJECT (demux,
        "Stream: %d Download rate = %" G_GUINT64_FORMAT " Kbits/s (%" G_GUINT64_FORMAT
        " Ko in %.2f s)", selected_stream->index,
        brate / 1000, size_buffer / 1024, ((double) diff / GST_SECOND));
  }
  return TRUE;
}

static void
gst_dash_demux_download_wait (GstDashDemux * demux, GstClockTime time_diff)
{
  gint64 end_time = g_get_monotonic_time () + time_diff / GST_USECOND;

  GST_DEBUG_OBJECT (demux, "Download waiting for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time_diff));
  g_cond_wait_until (&demux->download_cond, &demux->download_mutex, end_time);
  GST_DEBUG_OBJECT (demux, "Download finished waiting");
}
