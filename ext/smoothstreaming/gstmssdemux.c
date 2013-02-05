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
 *
 * Demuxes a Microsoft's Smooth Streaming manifest into its audio and/or video streams.
 *
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
 * is responsible for downloading fragments and storing in its own GstDataQueue.
 *
 * The mssdemux starts another GstTask, this one iterates through the streams
 * and selects the fragment with the smaller timestamp to push and repeats this.
 *
 * When a new connection-speed is set, mssdemux evaluates the available
 * QualityLevels and might decide to switch to another one. In this case it
 * exposes new pads for each stream, pushes EOS to the old ones and removes
 * them. This should make decodebin2 pad switching mechanism act and the
 * switch would be smooth for the final user.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gstmssdemux.h"

GST_DEBUG_CATEGORY (mssdemux_debug);

#define DEFAULT_CONNECTION_SPEED 0
#define DEFAULT_MAX_QUEUE_SIZE_BUFFERS 0
#define DEFAULT_BITRATE_LIMIT 0.8

enum
{
  PROP_0,

  PROP_CONNECTION_SPEED,
  PROP_MAX_QUEUE_SIZE_BUFFERS,
  PROP_BITRATE_LIMIT,
  PROP_LAST
};

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

GST_BOILERPLATE (GstMssDemux, gst_mss_demux, GstMssDemux, GST_TYPE_ELEMENT);

static void gst_mss_demux_dispose (GObject * object);
static void gst_mss_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mss_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_mss_demux_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_mss_demux_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_mss_demux_event (GstPad * pad, GstEvent * event);

static gboolean gst_mss_demux_src_query (GstPad * pad, GstQuery * query);

static void gst_mss_demux_download_loop (GstMssDemuxStream * stream);
static void gst_mss_demux_stream_loop (GstMssDemux * mssdemux);

static gboolean gst_mss_demux_process_manifest (GstMssDemux * mssdemux);

static void
gst_mss_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_mss_demux_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_mss_demux_videosrc_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_mss_demux_audiosrc_template);
  gst_element_class_set_details_simple (element_class, "Smooth Streaming "
      "demuxer", "Demuxer",
      "Parse and demultiplex a Smooth Streaming manifest into audio and video "
      "streams", "Thiago Santos <thiago.sousa.santos@collabora.com>");

  GST_DEBUG_CATEGORY_INIT (mssdemux_debug, "mssdemux", 0, "mssdemux plugin");
}

static void
gst_mss_demux_class_init (GstMssDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_mss_demux_dispose;
  gobject_class->set_property = gst_mss_demux_set_property;
  gobject_class->get_property = gst_mss_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXUINT / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_QUEUE_SIZE_BUFFERS,
      g_param_spec_uint ("max-queue-size-buffers", "Max queue size in buffers",
          "Maximum buffers that can be stored in each internal stream queue "
          "(0 = infinite)", 0, G_MAXUINT, DEFAULT_MAX_QUEUE_SIZE_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE_LIMIT,
      g_param_spec_float ("bitrate-limit",
          "Bitrate limit in %",
          "Limit of the available bitrate to use when switching to alternates.",
          0, 1, DEFAULT_BITRATE_LIMIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mss_demux_change_state);
}

static void
gst_mss_demux_init (GstMssDemux * mssdemux, GstMssDemuxClass * klass)
{
  mssdemux->sinkpad =
      gst_pad_new_from_static_template (&gst_mss_demux_sink_template, "sink");
  gst_pad_set_chain_function (mssdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mss_demux_chain));
  gst_pad_set_event_function (mssdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mss_demux_event));
  gst_element_add_pad (GST_ELEMENT_CAST (mssdemux), mssdemux->sinkpad);

  g_static_rec_mutex_init (&mssdemux->stream_lock);
  mssdemux->stream_task =
      gst_task_create ((GstTaskFunction) gst_mss_demux_stream_loop, mssdemux);
  gst_task_set_lock (mssdemux->stream_task, &mssdemux->stream_lock);

  mssdemux->data_queue_max_size = DEFAULT_MAX_QUEUE_SIZE_BUFFERS;
  mssdemux->bitrate_limit = DEFAULT_BITRATE_LIMIT;
}

static gboolean
_data_queue_check_full (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstMssDemuxStream *stream = checkdata;
  GstMssDemux *mssdemux = stream->parent;

  if (mssdemux->data_queue_max_size == 0)
    return FALSE;               /* never full */
  return visible >= mssdemux->data_queue_max_size;
}

static GstMssDemuxStream *
gst_mss_demux_stream_new (GstMssDemux * mssdemux,
    GstMssStream * manifeststream, GstPad * srcpad)
{
  GstMssDemuxStream *stream;

  stream = g_new0 (GstMssDemuxStream, 1);
  stream->downloader = gst_uri_downloader_new ();
  stream->dataqueue = gst_data_queue_new (_data_queue_check_full, stream);

  /* Downloading task */
  g_static_rec_mutex_init (&stream->download_lock);
  stream->download_task =
      gst_task_create ((GstTaskFunction) gst_mss_demux_download_loop, stream);
  gst_task_set_lock (stream->download_task, &stream->download_lock);

  stream->pad = srcpad;
  stream->manifest_stream = manifeststream;
  stream->parent = mssdemux;

  return stream;
}

static void
gst_mss_demux_stream_free (GstMssDemuxStream * stream)
{
  if (stream->download_task) {
    if (GST_TASK_STATE (stream->download_task) != GST_TASK_STOPPED) {
      GST_DEBUG_OBJECT (stream->parent, "Leaving streaming task %s:%s",
          GST_DEBUG_PAD_NAME (stream->pad));
      gst_uri_downloader_cancel (stream->downloader);
      gst_task_stop (stream->download_task);
      g_static_rec_mutex_lock (&stream->download_lock);
      g_static_rec_mutex_unlock (&stream->download_lock);
      GST_LOG_OBJECT (stream->parent, "Waiting for task to finish");
      gst_task_join (stream->download_task);
      GST_LOG_OBJECT (stream->parent, "Finished");
    }
    gst_object_unref (stream->download_task);
    g_static_rec_mutex_free (&stream->download_lock);
    stream->download_task = NULL;
  }

  if (stream->pending_newsegment) {
    gst_event_unref (stream->pending_newsegment);
    stream->pending_newsegment = NULL;
  }


  if (stream->downloader != NULL) {
    g_object_unref (stream->downloader);
    stream->downloader = NULL;
  }
  if (stream->dataqueue) {
    g_object_unref (stream->dataqueue);
    stream->dataqueue = NULL;
  }
  if (stream->pad) {
    gst_object_unref (stream->pad);
    stream->pad = NULL;
  }
  g_free (stream);
}

static void
gst_mss_demux_reset (GstMssDemux * mssdemux)
{
  GSList *iter;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    if (stream->downloader)
      gst_uri_downloader_cancel (stream->downloader);

    gst_data_queue_set_flushing (stream->dataqueue, TRUE);
  }

  if (GST_TASK_STATE (mssdemux->stream_task) != GST_TASK_STOPPED) {
    gst_task_stop (mssdemux->stream_task);
    g_static_rec_mutex_lock (&mssdemux->stream_lock);
    g_static_rec_mutex_unlock (&mssdemux->stream_lock);
    gst_task_join (mssdemux->stream_task);
  }

  if (mssdemux->manifest_buffer) {
    gst_buffer_unref (mssdemux->manifest_buffer);
    mssdemux->manifest_buffer = NULL;
  }

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    gst_element_remove_pad (GST_ELEMENT_CAST (mssdemux), stream->pad);
    gst_mss_demux_stream_free (stream);
  }
  g_slist_free (mssdemux->streams);
  mssdemux->streams = NULL;

  if (mssdemux->manifest) {
    gst_mss_manifest_free (mssdemux->manifest);
    mssdemux->manifest = NULL;
  }

  mssdemux->n_videos = mssdemux->n_audios = 0;
  g_free (mssdemux->base_url);
  mssdemux->base_url = NULL;
  g_free (mssdemux->manifest_uri);
  mssdemux->manifest_uri = NULL;
}

static void
gst_mss_demux_dispose (GObject * object)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (object);

  gst_mss_demux_reset (mssdemux);

  if (mssdemux->stream_task) {
    gst_object_unref (mssdemux->stream_task);
    g_static_rec_mutex_free (&mssdemux->stream_lock);
    mssdemux->stream_task = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mss_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX (object);

  switch (prop_id) {
    case PROP_CONNECTION_SPEED:
      GST_OBJECT_LOCK (mssdemux);
      mssdemux->connection_speed = g_value_get_uint (value) * 1000;
      mssdemux->update_bitrates = TRUE;
      GST_DEBUG_OBJECT (mssdemux, "Connection speed set to %llu",
          mssdemux->connection_speed);
      GST_OBJECT_UNLOCK (mssdemux);
      break;
    case PROP_MAX_QUEUE_SIZE_BUFFERS:
      mssdemux->data_queue_max_size = g_value_get_uint (value);
      break;
    case PROP_BITRATE_LIMIT:
      mssdemux->bitrate_limit = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mss_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX (object);

  switch (prop_id) {
    case PROP_CONNECTION_SPEED:
      g_value_set_uint (value, mssdemux->connection_speed / 1000);
      break;
    case PROP_MAX_QUEUE_SIZE_BUFFERS:
      g_value_set_uint (value, mssdemux->data_queue_max_size);
      break;
    case PROP_BITRATE_LIMIT:
      g_value_set_float (value, mssdemux->bitrate_limit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_mss_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mss_demux_reset (mssdemux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      break;
    }
    default:
      break;
  }

  return result;
}

static GstFlowReturn
gst_mss_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (GST_PAD_PARENT (pad));
  if (mssdemux->manifest_buffer == NULL)
    mssdemux->manifest_buffer = buffer;
  else
    mssdemux->manifest_buffer =
        gst_buffer_join (mssdemux->manifest_buffer, buffer);

  GST_INFO_OBJECT (mssdemux, "Received manifest buffer, total size is %i bytes",
      GST_BUFFER_SIZE (mssdemux->manifest_buffer));

  return GST_FLOW_OK;
}

static void
gst_mss_demux_start (GstMssDemux * mssdemux)
{
  GSList *iter;

  GST_INFO_OBJECT (mssdemux, "Starting streams' tasks");
  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    gst_task_start (stream->download_task);
  }

  gst_task_start (mssdemux->stream_task);
}

static gboolean
gst_mss_demux_push_src_event (GstMssDemux * mssdemux, GstEvent * event)
{
  GSList *iter;
  gboolean ret = TRUE;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    gst_event_ref (event);
    ret = ret & gst_pad_push_event (stream->pad, event);
  }
  gst_event_unref (event);
  return ret;
}

static gboolean
gst_mss_demux_event (GstPad * pad, GstEvent * event)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (GST_PAD_PARENT (pad));
  gboolean forward = TRUE;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_mss_demux_reset (mssdemux);
      break;
    case GST_EVENT_EOS:
      if (mssdemux->manifest_buffer == NULL) {
        GST_WARNING_OBJECT (mssdemux, "Received EOS without a manifest.");
        break;
      }
      GST_INFO_OBJECT (mssdemux, "Received EOS");

      if (gst_mss_demux_process_manifest (mssdemux))
        gst_mss_demux_start (mssdemux);
      forward = FALSE;
      break;
    default:
      break;
  }

  if (forward) {
    ret = gst_pad_event_default (pad, event);
  } else {
    gst_event_unref (event);
  }

  return ret;
}

static void
gst_mss_demux_stop_tasks (GstMssDemux * mssdemux, gboolean immediate)
{
  GSList *iter;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    gst_data_queue_set_flushing (stream->dataqueue, TRUE);

    if (immediate)
      gst_uri_downloader_cancel (stream->downloader);
    gst_task_pause (stream->download_task);
  }
  gst_task_pause (mssdemux->stream_task);

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    g_static_rec_mutex_lock (&stream->download_lock);
  }
  g_static_rec_mutex_lock (&mssdemux->stream_lock);
}

static void
gst_mss_demux_restart_tasks (GstMssDemux * mssdemux)
{
  GSList *iter;
  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    gst_uri_downloader_reset (stream->downloader);
    g_static_rec_mutex_unlock (&stream->download_lock);
  }
  g_static_rec_mutex_unlock (&mssdemux->stream_lock);
  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    gst_data_queue_set_flushing (stream->dataqueue, FALSE);
    gst_task_start (stream->download_task);
  }
  gst_task_start (mssdemux->stream_task);
}

static gboolean
gst_mss_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstMssDemux *mssdemux;

  mssdemux = GST_MSS_DEMUX (GST_PAD_PARENT (pad));

  switch (event->type) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstEvent *newsegment;
      GSList *iter;

      GST_INFO_OBJECT (mssdemux, "Received GST_EVENT_SEEK");

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format != GST_FORMAT_TIME)
        goto not_supported;

      GST_DEBUG_OBJECT (mssdemux,
          "seek event, rate: %f start: %" GST_TIME_FORMAT " stop: %"
          GST_TIME_FORMAT, rate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GstEvent *flush = gst_event_new_flush_start ();
        GST_DEBUG_OBJECT (mssdemux, "sending flush start");

        gst_event_set_seqnum (flush, gst_event_get_seqnum (event));
        gst_mss_demux_push_src_event (mssdemux, flush);
      }

      gst_mss_demux_stop_tasks (mssdemux, TRUE);

      if (!gst_mss_manifest_seek (mssdemux->manifest, start)) {;
        GST_WARNING_OBJECT (mssdemux, "Could not find seeked fragment");
        goto not_supported;
      }

      newsegment =
          gst_event_new_new_segment (FALSE, rate, format, start, stop, start);
      gst_event_set_seqnum (newsegment, gst_event_get_seqnum (event));
      for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
        GstMssDemuxStream *stream = iter->data;

        stream->eos = FALSE;
        gst_data_queue_flush (stream->dataqueue);
        gst_event_ref (newsegment);
        gst_event_replace (&stream->pending_newsegment, newsegment);
      }
      gst_event_unref (newsegment);

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GstEvent *flush = gst_event_new_flush_stop ();
        GST_DEBUG_OBJECT (mssdemux, "sending flush stop");

        gst_event_set_seqnum (flush, gst_event_get_seqnum (event));
        gst_mss_demux_push_src_event (mssdemux, flush);
      }

      gst_mss_demux_restart_tasks (mssdemux);

      gst_event_unref (event);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);

not_supported:
  gst_event_unref (event);
  return FALSE;
}

static gboolean
gst_mss_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstMssDemux *mssdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  mssdemux = GST_MSS_DEMUX (GST_PAD_PARENT (pad));

  switch (query->type) {
    case GST_QUERY_DURATION:{
      GstClockTime duration = -1;
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME && mssdemux->manifest) {
        /* TODO should we use the streams accumulated duration or the main manifest duration? */
        duration = gst_mss_manifest_get_gst_duration (mssdemux->manifest);

        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          ret = TRUE;
        }
      }
      GST_INFO_OBJECT (mssdemux, "GST_QUERY_DURATION returns %s with duration %"
          GST_TIME_FORMAT, ret ? "TRUE" : "FALSE", GST_TIME_ARGS (duration));
      break;
    }
    case GST_QUERY_LATENCY:{
      gboolean live = FALSE;

      live = mssdemux->manifest
          && gst_mss_manifest_is_live (mssdemux->manifest);

      gst_query_set_latency (query, live, 0, -1);
      ret = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint64 stop = -1;

      if (mssdemux->manifest && gst_mss_manifest_is_live (mssdemux->manifest)) {
        return FALSE;           /* no live seeking */
      }

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      GST_INFO_OBJECT (mssdemux, "Received GST_QUERY_SEEKING with format %d",
          fmt);
      if (fmt == GST_FORMAT_TIME) {
        GstClockTime duration;
        duration = gst_mss_manifest_get_gst_duration (mssdemux->manifest);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
          stop = duration;
        gst_query_set_seeking (query, fmt, TRUE, 0, stop);
        ret = TRUE;
        GST_INFO_OBJECT (mssdemux, "GST_QUERY_SEEKING returning with stop : %"
            GST_TIME_FORMAT, GST_TIME_ARGS (stop));
      }
      break;
    }
    default:
      /* Don't fordward queries upstream because of the special nature of this
       *  "demuxer", which relies on the upstream element only to be fed
       *  the Manifest
       */
      break;
  }

  return ret;
}

static void
_set_src_pad_functions (GstPad * pad)
{
  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_mss_demux_src_query));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_mss_demux_src_event));
}

static GstPad *
_create_pad (GstMssDemux * mssdemux, GstMssStream * manifeststream)
{
  gchar *name;
  GstPad *srcpad = NULL;
  GstMssStreamType streamtype;

  streamtype = gst_mss_stream_get_type (manifeststream);
  GST_DEBUG_OBJECT (mssdemux, "Found stream of type: %s",
      gst_mss_stream_type_name (streamtype));

  /* TODO use stream's name/bitrate/index as the pad name? */
  if (streamtype == MSS_STREAM_TYPE_VIDEO) {
    name = g_strdup_printf ("video_%02u", mssdemux->n_videos++);
    srcpad =
        gst_pad_new_from_static_template (&gst_mss_demux_videosrc_template,
        name);
    g_free (name);
  } else if (streamtype == MSS_STREAM_TYPE_AUDIO) {
    name = g_strdup_printf ("audio_%02u", mssdemux->n_audios++);
    srcpad =
        gst_pad_new_from_static_template (&gst_mss_demux_audiosrc_template,
        name);
    g_free (name);
  }

  if (!srcpad) {
    GST_WARNING_OBJECT (mssdemux, "Ignoring unknown type stream");
    return NULL;
  }

  _set_src_pad_functions (srcpad);
  return srcpad;
}

static void
gst_mss_demux_create_streams (GstMssDemux * mssdemux)
{
  GSList *streams = gst_mss_manifest_get_streams (mssdemux->manifest);
  GSList *iter;

  if (streams == NULL) {
    GST_INFO_OBJECT (mssdemux, "No streams found in the manifest");
    GST_ELEMENT_ERROR (mssdemux, STREAM, DEMUX,
        (_("This file contains no playable streams.")),
        ("no streams found at the Manifest"));
    return;
  }

  for (iter = streams; iter; iter = g_slist_next (iter)) {
    GstPad *srcpad = NULL;
    GstMssDemuxStream *stream = NULL;
    GstMssStream *manifeststream = iter->data;

    srcpad = _create_pad (mssdemux, manifeststream);

    if (!srcpad) {
      continue;
    }

    stream = gst_mss_demux_stream_new (mssdemux, manifeststream, srcpad);
    gst_mss_stream_set_active (manifeststream, TRUE);
    mssdemux->streams = g_slist_append (mssdemux->streams, stream);
  }

  /* select initial bitrates */
  GST_OBJECT_LOCK (mssdemux);
  GST_INFO_OBJECT (mssdemux, "Changing max bitrate to %llu",
      mssdemux->connection_speed);
  gst_mss_manifest_change_bitrate (mssdemux->manifest,
      mssdemux->connection_speed);
  mssdemux->update_bitrates = FALSE;
  GST_OBJECT_UNLOCK (mssdemux);
}

static gboolean
gst_mss_demux_expose_stream (GstMssDemux * mssdemux, GstMssDemuxStream * stream)
{
  GstCaps *caps;
  GstCaps *media_caps;
  GstPad *pad = stream->pad;

  media_caps = gst_mss_stream_get_caps (stream->manifest_stream);

  if (media_caps) {
    caps = gst_caps_new_simple ("video/quicktime", "variant", G_TYPE_STRING,
        "mss-fragmented", "timescale", G_TYPE_UINT64,
        gst_mss_stream_get_timescale (stream->manifest_stream), "media-caps",
        GST_TYPE_CAPS, media_caps, NULL);
    gst_caps_unref (media_caps);
    gst_pad_set_caps (pad, caps);
    gst_caps_unref (caps);

    gst_pad_set_active (pad, TRUE);
    GST_INFO_OBJECT (mssdemux, "Adding srcpad %s:%s with caps %" GST_PTR_FORMAT,
        GST_DEBUG_PAD_NAME (pad), caps);
    gst_object_ref (pad);
    gst_element_add_pad (GST_ELEMENT_CAST (mssdemux), pad);
  } else {
    GST_WARNING_OBJECT (mssdemux,
        "Couldn't get caps from manifest stream %p %s, not exposing it", stream,
        GST_PAD_NAME (stream->pad));
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_mss_demux_process_manifest (GstMssDemux * mssdemux)
{
  GstQuery *query;
  gchar *uri = NULL;
  gboolean ret;
  GSList *iter;

  g_return_val_if_fail (mssdemux->manifest_buffer != NULL, FALSE);
  g_return_val_if_fail (mssdemux->manifest == NULL, FALSE);

  query = gst_query_new_uri ();
  ret = gst_pad_peer_query (mssdemux->sinkpad, query);
  if (ret) {
    gchar *baseurl_end;
    gst_query_parse_uri (query, &uri);
    GST_INFO_OBJECT (mssdemux, "Upstream is using URI: %s", uri);

    mssdemux->manifest_uri = g_strdup (uri);
    baseurl_end = g_strrstr (uri, "/Manifest");
    if (baseurl_end) {
      /* set the new end of the string */
      baseurl_end[0] = '\0';
    } else {
      GST_WARNING_OBJECT (mssdemux, "Stream's URI didn't end with /manifest");
    }

    mssdemux->base_url = uri;
  }
  gst_query_unref (query);

  if (mssdemux->base_url == NULL) {
    GST_ELEMENT_ERROR (mssdemux, RESOURCE, NOT_FOUND,
        (_("Couldn't get the Manifest's URI")),
        ("need to get the manifest's URI from upstream elements"));
    return FALSE;
  }

  GST_INFO_OBJECT (mssdemux, "Received manifest: %i bytes",
      GST_BUFFER_SIZE (mssdemux->manifest_buffer));

  mssdemux->manifest = gst_mss_manifest_new (mssdemux->manifest_buffer);
  if (!mssdemux->manifest) {
    GST_ELEMENT_ERROR (mssdemux, STREAM, FORMAT, ("Bad manifest file"),
        ("Xml manifest file couldn't be parsed"));
    return FALSE;
  }

  GST_INFO_OBJECT (mssdemux, "Live stream: %d",
      gst_mss_manifest_is_live (mssdemux->manifest));

  gst_mss_demux_create_streams (mssdemux);
  for (iter = mssdemux->streams; iter;) {
    GSList *current = iter;
    GstMssDemuxStream *stream = iter->data;
    iter = g_slist_next (iter); /* do it ourselves as we want it done in the beginning of the loop */
    if (!gst_mss_demux_expose_stream (mssdemux, stream)) {
      gst_mss_demux_stream_free (stream);
      mssdemux->streams = g_slist_delete_link (mssdemux->streams, current);
    }
  }

  if (!mssdemux->streams) {
    /* no streams */
    GST_WARNING_OBJECT (mssdemux, "Couldn't identify the caps for any of the "
        "streams found in the manifest");
    GST_ELEMENT_ERROR (mssdemux, STREAM, DEMUX,
        (_("This file contains no playable streams.")),
        ("No known stream formats found at the Manifest"));
    return FALSE;
  }

  gst_element_no_more_pads (GST_ELEMENT_CAST (mssdemux));
  return TRUE;
}

static void
gst_mss_demux_reload_manifest (GstMssDemux * mssdemux)
{
  GstUriDownloader *downloader;
  GstFragment *manifest_data;
  GstBuffer *manifest_buffer;

  downloader = gst_uri_downloader_new ();

  manifest_data =
      gst_uri_downloader_fetch_uri (downloader, mssdemux->manifest_uri);
  manifest_buffer = gst_fragment_get_buffer (manifest_data);
  g_object_unref (manifest_data);

  gst_mss_manifest_reload_fragments (mssdemux->manifest, manifest_buffer);
  gst_buffer_replace (&mssdemux->manifest_buffer, manifest_buffer);
  gst_buffer_unref (manifest_buffer);

  g_object_unref (downloader);
}

static guint64
gst_mss_demux_get_download_bitrate (GstMssDemux * mssdemux)
{
  GSList *iter;
  guint64 total = 0;
  guint64 count = 0;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    total += stream->download_bitrate;
    count++;
  }

  return total / count;
}

static gboolean
gst_mss_demux_all_streams_have_data (GstMssDemux * mssdemux)
{
  GSList *iter;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    if (!stream->have_data)
      return FALSE;
  }

  return TRUE;
}

static void
gst_mss_demux_reconfigure (GstMssDemux * mssdemux)
{
  GSList *oldpads = NULL;
  GSList *iter;
  guint64 new_bitrate;

  /* TODO lock? */

  if (!gst_mss_demux_all_streams_have_data (mssdemux))
    return;

  new_bitrate = 0.8 * gst_mss_demux_get_download_bitrate (mssdemux) / 1000;
  if (mssdemux->connection_speed) {
    new_bitrate = MIN (mssdemux->connection_speed, new_bitrate);
  }

  GST_DEBUG_OBJECT ("Current suggested bitrate: %llu", new_bitrate);

  gst_mss_demux_stop_tasks (mssdemux, TRUE);
  if (gst_mss_manifest_change_bitrate (mssdemux->manifest, new_bitrate)) {
    GstClockTime newseg_ts = GST_CLOCK_TIME_NONE;

    GST_INFO_OBJECT ("Switching to bitrate %llu", new_bitrate);

    GST_DEBUG_OBJECT (mssdemux, "Creating new pad group");
    /* if we changed the bitrate, we need to add new pads */
    for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
      GstMssDemuxStream *stream = iter->data;
      GstPad *oldpad = stream->pad;
      GstClockTime ts = GST_CLOCK_TIME_NONE;

      oldpads = g_slist_prepend (oldpads, oldpad);

      /* since we are flushing the queue, get the next un-pushed timestamp to seek
       * and avoid gaps */
      gst_data_queue_set_flushing (stream->dataqueue, FALSE);
      if (!gst_data_queue_is_empty (stream->dataqueue)) {
        GstDataQueueItem *item = NULL;

        while (!gst_data_queue_is_empty (stream->dataqueue)
            && !GST_CLOCK_TIME_IS_VALID (ts)) {
          gst_data_queue_pop (stream->dataqueue, &item);

          if (!item) {
            g_assert_not_reached ();
            break;
          }

          if (GST_IS_BUFFER (item->object)) {
            GstBuffer *buffer = GST_BUFFER_CAST (item->object);

            ts = GST_BUFFER_TIMESTAMP (buffer);
          }
          item->destroy (item);
        }

      }
      if (!GST_CLOCK_TIME_IS_VALID (ts)) {
        ts = gst_mss_stream_get_fragment_gst_timestamp
            (stream->manifest_stream);
      }

      if (ts < newseg_ts)
        newseg_ts = ts;

      GST_DEBUG_OBJECT (mssdemux,
          "Seeking stream %p %s to ts %" GST_TIME_FORMAT, stream,
          GST_PAD_NAME (stream->pad), GST_TIME_ARGS (ts));
      gst_mss_stream_seek (stream->manifest_stream, ts);
      gst_data_queue_flush (stream->dataqueue);

      stream->pad = _create_pad (mssdemux, stream->manifest_stream);
      gst_mss_demux_expose_stream (mssdemux, stream);

      gst_pad_push_event (oldpad, gst_event_new_eos ());
      stream->have_data = FALSE;
    }

    gst_element_no_more_pads (GST_ELEMENT (mssdemux));

    for (iter = oldpads; iter; iter = g_slist_next (iter)) {
      GstPad *oldpad = iter->data;

      gst_pad_set_active (oldpad, FALSE);
      gst_element_remove_pad (GST_ELEMENT (mssdemux), oldpad);
      gst_object_unref (oldpad);
    }
    for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
      GstMssDemuxStream *stream = iter->data;

      stream->pending_newsegment =
          gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, newseg_ts, -1,
          newseg_ts);
    }
  }
  gst_mss_demux_restart_tasks (mssdemux);
}

static void
_free_data_queue_item (gpointer obj)
{
  GstDataQueueItem *item = obj;

  gst_mini_object_unref (item->object);
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_mss_demux_stream_store_object (GstMssDemuxStream * stream,
    GstMiniObject * obj)
{
  GstDataQueueItem *item;

  item = g_slice_new (GstDataQueueItem);
  item->object = (GstMiniObject *) obj;

  item->duration = 0;           /* we don't care */
  item->size = 0;
  item->visible = TRUE;

  item->destroy = (GDestroyNotify) _free_data_queue_item;

  if (!gst_data_queue_push (stream->dataqueue, item)) {
    GST_DEBUG_OBJECT (stream->parent, "Failed to store object %p", obj);
    item->destroy (item);
  }
}

static GstFlowReturn
gst_mss_demux_stream_download_fragment (GstMssDemuxStream * stream,
    GstBuffer ** buffer)
{
  GstMssDemux *mssdemux = stream->parent;
  gchar *path;
  gchar *url;
  GstFragment *fragment;
  GstBuffer *_buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 before_download, after_download;

  before_download = g_get_real_time ();

  GST_DEBUG_OBJECT (mssdemux, "Getting url for stream %p", stream);
  ret = gst_mss_stream_get_fragment_url (stream->manifest_stream, &path);
  switch (ret) {
    case GST_FLOW_OK:
      break;                    /* all is good, let's go */
    case GST_FLOW_UNEXPECTED:  /* EOS */
      if (gst_mss_manifest_is_live (mssdemux->manifest)) {
        gst_mss_demux_reload_manifest (mssdemux);
        return GST_FLOW_OK;
      }
      return GST_FLOW_UNEXPECTED;
    case GST_FLOW_ERROR:
      goto error;
    default:
      break;
  }
  if (!path) {
    goto no_url_error;
  }
  GST_DEBUG_OBJECT (mssdemux, "Got url path '%s' for stream %p", path, stream);

  url = g_strdup_printf ("%s/%s", mssdemux->base_url, path);

  GST_DEBUG_OBJECT (mssdemux, "Got url '%s' for stream %p", url, stream);

  fragment = gst_uri_downloader_fetch_uri (stream->downloader, url);
  g_free (path);
  g_free (url);

  if (!fragment) {
    GST_INFO_OBJECT (mssdemux, "No fragment downloaded");
    /* TODO check if we are truly stoping */
    if (gst_mss_manifest_is_live (mssdemux->manifest)) {
      /* looks like there is no way of knowing when a live stream has ended
       * Have to assume we are falling behind and cause a manifest reload */
      return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
  }

  _buffer = gst_fragment_get_buffer (fragment);
  _buffer = gst_buffer_make_metadata_writable (_buffer);
  gst_buffer_set_caps (_buffer, GST_PAD_CAPS (stream->pad));
  GST_BUFFER_TIMESTAMP (_buffer) =
      gst_mss_stream_get_fragment_gst_timestamp (stream->manifest_stream);
  GST_BUFFER_DURATION (_buffer) =
      gst_mss_stream_get_fragment_gst_duration (stream->manifest_stream);

  g_object_unref (fragment);

  if (buffer)
    *buffer = _buffer;

  after_download = g_get_real_time ();
  if (_buffer) {
    guint64 bitrate = 8 * GST_BUFFER_SIZE (_buffer) /
        ((after_download - before_download) / 1000000ULL);

    GST_DEBUG_OBJECT (mssdemux, "Measured download bitrate: %s %llu bps",
        GST_PAD_NAME (stream->pad), bitrate);
    stream->download_bitrate = bitrate;

    GST_DEBUG_OBJECT (mssdemux,
        "Storing buffer for stream %p - %s. Timestamp: %" GST_TIME_FORMAT
        " Duration: %" GST_TIME_FORMAT,
        stream, GST_PAD_NAME (stream->pad),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (_buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (_buffer)));
    gst_mss_demux_stream_store_object (stream, GST_MINI_OBJECT_CAST (_buffer));
  }


  GST_OBJECT_LOCK (mssdemux);
  mssdemux->update_bitrates = TRUE;
  GST_OBJECT_UNLOCK (mssdemux);

  return ret;

no_url_error:
  {
    GST_ELEMENT_ERROR (mssdemux, STREAM, DEMUX,
        (_("Failed to get fragment URL.")),
        ("An error happened when getting fragment URL"));
    gst_task_pause (stream->download_task);
    return GST_FLOW_ERROR;
  }
error:
  {
    GST_WARNING_OBJECT (mssdemux, "Error while pushing fragment");
    gst_task_pause (stream->download_task);
    return GST_FLOW_ERROR;
  }
}

static void
gst_mss_demux_download_loop (GstMssDemuxStream * stream)
{
  GstMssDemux *mssdemux = stream->parent;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;

  GST_LOG_OBJECT (mssdemux, "download loop start %p", stream);

  ret = gst_mss_demux_stream_download_fragment (stream, &buffer);
  switch (ret) {
    case GST_FLOW_OK:
      break;                    /* all is good, let's go */
    case GST_FLOW_UNEXPECTED:  /* EOS */
      goto eos;
    case GST_FLOW_ERROR:
      goto error;
    default:
      break;
  }

  if (buffer) {
    gst_mss_stream_advance_fragment (stream->manifest_stream);
  }
  GST_LOG_OBJECT (mssdemux, "download loop end %p", stream);
  return;

eos:
  {
    GST_DEBUG_OBJECT (mssdemux, "Storing EOS for pad %s:%s",
        GST_DEBUG_PAD_NAME (stream->pad));
    gst_mss_demux_stream_store_object (stream,
        GST_MINI_OBJECT_CAST (gst_event_new_eos ()));
    gst_task_pause (stream->download_task);
    return;
  }
error:
  {
    GST_WARNING_OBJECT (mssdemux, "Error while pushing fragment");
    gst_task_pause (stream->download_task);
    return;
  }
}

static GstFlowReturn
gst_mss_demux_select_latest_stream (GstMssDemux * mssdemux,
    GstMssDemuxStream ** stream)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMssDemuxStream *current = NULL;
  GstClockTime cur_time = GST_CLOCK_TIME_NONE;
  GSList *iter;

  if (!mssdemux->streams)
    return GST_FLOW_ERROR;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstClockTime time;
    GstMssDemuxStream *other;
    GstDataQueueItem *item;

    other = iter->data;
    if (other->eos) {
      continue;
    }

    if (!gst_data_queue_peek (other->dataqueue, &item)) {
      /* flushing */
      return GST_FLOW_WRONG_STATE;
    }

    if (GST_IS_EVENT (item->object)) {
      /* events have higher priority */
      current = other;
      break;
    }
    time = GST_BUFFER_TIMESTAMP (GST_BUFFER_CAST (item->object));
    if (time < cur_time) {
      cur_time = time;
      current = other;
    }
  }

  *stream = current;
  if (current == NULL)
    ret = GST_FLOW_UNEXPECTED;
  return ret;
}

static void
gst_mss_demux_stream_loop (GstMssDemux * mssdemux)
{
  GstMssDemuxStream *stream = NULL;
  GstFlowReturn ret;
  GstMiniObject *object = NULL;
  GstDataQueueItem *item = NULL;

  GST_LOG_OBJECT (mssdemux, "Starting stream loop");

  GST_OBJECT_LOCK (mssdemux);
  if (mssdemux->update_bitrates) {
    mssdemux->update_bitrates = FALSE;
    GST_OBJECT_UNLOCK (mssdemux);

    GST_DEBUG_OBJECT (mssdemux,
        "Starting streams reconfiguration due to bitrate changes");
    gst_mss_demux_reconfigure (mssdemux);
    GST_DEBUG_OBJECT (mssdemux, "Finished streams reconfiguration");
  } else {
    GST_OBJECT_UNLOCK (mssdemux);
  }

  ret = gst_mss_demux_select_latest_stream (mssdemux, &stream);

  if (stream)
    GST_DEBUG_OBJECT (mssdemux,
        "Stream loop selected %p stream of pad %s. %d - %s", stream,
        GST_PAD_NAME (stream->pad), ret, gst_flow_get_name (ret));
  else
    GST_DEBUG_OBJECT (mssdemux, "No streams selected -> %d - %s", ret,
        gst_flow_get_name (ret));

  switch (ret) {
    case GST_FLOW_OK:
      break;
    case GST_FLOW_ERROR:
      goto error;
    case GST_FLOW_UNEXPECTED:
      goto eos;
    case GST_FLOW_WRONG_STATE:
      GST_DEBUG_OBJECT (mssdemux, "Wrong state, stopping task");
      goto stop;
    default:
      g_assert_not_reached ();
  }

  GST_LOG_OBJECT (mssdemux, "popping next item from queue for stream %p %s",
      stream, GST_PAD_NAME (stream->pad));
  if (gst_data_queue_pop (stream->dataqueue, &item)) {
    if (item->object)
      object = gst_mini_object_ref (item->object);
    item->destroy (item);
  } else {
    GST_DEBUG_OBJECT (mssdemux,
        "Failed to get object from dataqueue on stream %p %s", stream,
        GST_PAD_NAME (stream->pad));
    goto stop;
  }

  if (G_UNLIKELY (stream->pending_newsegment)) {
    gst_pad_push_event (stream->pad, stream->pending_newsegment);
    stream->pending_newsegment = NULL;
  }

  if (G_LIKELY (GST_IS_BUFFER (object))) {
    if (GST_BUFFER_TIMESTAMP (object) != stream->next_timestamp) {
      GST_ERROR_OBJECT (mssdemux, "Marking buffer %p as discont buffer:%"
          GST_TIME_FORMAT " != expected:%" GST_TIME_FORMAT, object,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (object)),
          GST_TIME_ARGS (stream->next_timestamp));
      GST_BUFFER_FLAG_SET (object, GST_BUFFER_FLAG_DISCONT);
    }

    GST_DEBUG_OBJECT (mssdemux,
        "Pushing buffer %p %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
        " discont:%d on pad %s", object,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (object)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (object)),
        GST_BUFFER_FLAG_IS_SET (object, GST_BUFFER_FLAG_DISCONT),
        GST_PAD_NAME (stream->pad));

    stream->next_timestamp =
        GST_BUFFER_TIMESTAMP (object) + GST_BUFFER_DURATION (object);

    stream->have_data = TRUE;
    ret = gst_pad_push (stream->pad, GST_BUFFER_CAST (object));
  } else if (GST_IS_EVENT (object)) {
    if (GST_EVENT_TYPE (object) == GST_EVENT_EOS)
      stream->eos = TRUE;
    GST_DEBUG_OBJECT (mssdemux, "Pushing event %p on pad %s", object,
        GST_PAD_NAME (stream->pad));
    gst_pad_push_event (stream->pad, GST_EVENT_CAST (object));
  } else {
    g_return_if_reached ();
  }

  switch (ret) {
    case GST_FLOW_UNEXPECTED:
      goto eos;                 /* EOS ? */
    case GST_FLOW_ERROR:
      goto error;
    case GST_FLOW_NOT_LINKED:
      break;                    /* TODO what to do here? pause the task or just keep pushing? */
    case GST_FLOW_OK:
    default:
      break;
  }

  GST_LOG_OBJECT (mssdemux, "Stream loop end");
  return;

eos:
  {
    GST_DEBUG_OBJECT (mssdemux, "EOS on all pads");
    gst_task_pause (mssdemux->stream_task);
    return;
  }
error:
  {
    GST_WARNING_OBJECT (mssdemux, "Error while pushing fragment");
    gst_task_pause (mssdemux->stream_task);
    return;
  }
stop:
  {
    GST_DEBUG_OBJECT (mssdemux, "Pausing streaming task");
    gst_task_pause (mssdemux->stream_task);
    return;
  }
}
