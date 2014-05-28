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

#include "gst/gst-i18n-plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gstmssdemux.h"

GST_DEBUG_CATEGORY (mssdemux_debug);

#define DEFAULT_CONNECTION_SPEED 0
#define DEFAULT_MAX_QUEUE_SIZE_BUFFERS 0
#define DEFAULT_BITRATE_LIMIT 0.8

#define MAX_DOWNLOAD_ERROR_COUNT 3

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

#define gst_mss_demux_parent_class parent_class
G_DEFINE_TYPE (GstMssDemux, gst_mss_demux, GST_TYPE_BIN);

static void gst_mss_demux_dispose (GObject * object);
static void gst_mss_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mss_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_mss_demux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_mss_demux_handle_message (GstBin * bin, GstMessage * msg);
static GstFlowReturn gst_mss_demux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstFlowReturn gst_mss_demux_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mss_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void gst_mss_demux_download_loop (GstMssDemuxStream * stream);
static GstFlowReturn gst_mss_demux_stream_push_event (GstMssDemuxStream *
    stream, GstEvent * event);
static GstFlowReturn gst_mss_demux_combine_flows (GstMssDemux * mssdemux);

static gboolean gst_mss_demux_process_manifest (GstMssDemux * mssdemux);
static void
gst_mss_demux_stop_tasks (GstMssDemux * mssdemux, gboolean immediate);

static void
gst_mss_demux_class_init (GstMssDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_mss_demux_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_mss_demux_videosrc_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_mss_demux_audiosrc_template));
  gst_element_class_set_static_metadata (gstelement_class,
      "Smooth Streaming demuxer", "Codec/Demuxer/Adaptive",
      "Parse and demultiplex a Smooth Streaming manifest into audio and video "
      "streams", "Thiago Santos <thiago.sousa.santos@collabora.com>");

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
          "(0 = infinite) (deprecated)", 0, G_MAXUINT,
          DEFAULT_MAX_QUEUE_SIZE_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE_LIMIT,
      g_param_spec_float ("bitrate-limit",
          "Bitrate limit in %",
          "Limit of the available bitrate to use when switching to alternates.",
          0, 1, DEFAULT_BITRATE_LIMIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mss_demux_change_state);

  gstbin_class->handle_message = gst_mss_demux_handle_message;

  GST_DEBUG_CATEGORY_INIT (mssdemux_debug, "mssdemux", 0, "mssdemux plugin");
}

static void
gst_mss_demux_init (GstMssDemux * mssdemux)
{
  mssdemux->sinkpad =
      gst_pad_new_from_static_template (&gst_mss_demux_sink_template, "sink");
  gst_pad_set_chain_function (mssdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mss_demux_chain));
  gst_pad_set_event_function (mssdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mss_demux_event));
  gst_element_add_pad (GST_ELEMENT_CAST (mssdemux), mssdemux->sinkpad);

  mssdemux->data_queue_max_size = DEFAULT_MAX_QUEUE_SIZE_BUFFERS;
  mssdemux->bitrate_limit = DEFAULT_BITRATE_LIMIT;

  mssdemux->have_group_id = FALSE;
  mssdemux->group_id = G_MAXUINT;
}

static GstMssDemuxStream *
gst_mss_demux_stream_new (GstMssDemux * mssdemux,
    GstMssStream * manifeststream, GstPad * srcpad)
{
  GstMssDemuxStream *stream;

  stream = g_new0 (GstMssDemuxStream, 1);

  /* Downloading task */
  g_rec_mutex_init (&stream->download_lock);
  stream->download_task =
      gst_task_new ((GstTaskFunction) gst_mss_demux_download_loop, stream,
      NULL);
  gst_task_set_lock (stream->download_task, &stream->download_lock);

  stream->pad = srcpad;
  stream->manifest_stream = manifeststream;
  stream->parent = mssdemux;

  gst_segment_init (&stream->segment, GST_FORMAT_TIME);
  g_cond_init (&stream->fragment_download_cond);
  g_mutex_init (&stream->fragment_download_lock);

  return stream;
}

static void
gst_mss_demux_stream_free (GstMssDemuxStream * stream)
{
  if (stream->download_task) {
    if (GST_TASK_STATE (stream->download_task) != GST_TASK_STOPPED) {
      GST_DEBUG_OBJECT (stream->parent, "Leaving streaming task %s:%s",
          GST_DEBUG_PAD_NAME (stream->pad));
      g_cond_signal (&stream->fragment_download_cond);
      gst_task_stop (stream->download_task);
      GST_LOG_OBJECT (stream->parent, "Waiting for task to finish");
      gst_task_join (stream->download_task);
      GST_LOG_OBJECT (stream->parent, "Finished");
    }
    gst_object_unref (stream->download_task);
    g_rec_mutex_clear (&stream->download_lock);
    stream->download_task = NULL;
  }

  if (stream->pending_segment) {
    gst_event_unref (stream->pending_segment);
    stream->pending_segment = NULL;
  }

  if (stream->src_srcpad) {
    gst_object_unref (stream->src_srcpad);
    stream->src_srcpad = NULL;
  }

  if (stream->src) {
    gst_element_set_state (stream->src, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (stream->parent), stream->src);
    stream->src = NULL;
  }

  g_cond_clear (&stream->fragment_download_cond);
  g_mutex_clear (&stream->fragment_download_lock);

  if (stream->pad) {
    gst_object_unref (stream->pad);
    stream->pad = NULL;
  }
  if (stream->caps)
    gst_caps_unref (stream->caps);
  g_free (stream);
}

static void
gst_mss_demux_reset (GstMssDemux * mssdemux)
{
  GSList *iter;

  gst_mss_demux_stop_tasks (mssdemux, TRUE);

  if (mssdemux->manifest_buffer) {
    gst_buffer_unref (mssdemux->manifest_buffer);
    mssdemux->manifest_buffer = NULL;
  }

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    if (stream->pad)
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

  mssdemux->have_group_id = FALSE;
  mssdemux->group_id = G_MAXUINT;
}

static void
gst_mss_demux_dispose (GObject * object)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (object);

  gst_mss_demux_reset (mssdemux);

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
      GST_DEBUG_OBJECT (mssdemux, "Connection speed set to %" G_GUINT64_FORMAT,
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

  return result;
}

static void
gst_mss_demux_handle_message (GstBin * bin, GstMessage * msg)
{
  GstMssDemux *demux = GST_MSS_DEMUX_CAST (bin);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GSList *iter;
      GstMssDemuxStream *stream;
      GError *err = NULL;
      gchar *debug = NULL;

      for (iter = demux->streams; iter; iter = g_slist_next (iter)) {
        stream = iter->data;
        if (GST_OBJECT_CAST (stream->src) == GST_MESSAGE_SRC (msg)) {
          gst_message_parse_error (msg, &err, &debug);

          GST_WARNING_OBJECT (stream->pad, "Source posted error: %d:%d %s (%s)",
              err->domain, err->code, err->message, debug);

          /* error, but ask to retry */
          stream->last_ret = GST_FLOW_CUSTOM_ERROR;
          g_cond_signal (&stream->fragment_download_cond);

          g_error_free (err);
          g_free (debug);
          break;
        }
      }

      gst_message_unref (msg);
      msg = NULL;
    }
      break;
    default:
      break;
  }

  if (msg)
    GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}


static GstFlowReturn
gst_mss_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (parent);
  if (mssdemux->manifest_buffer == NULL)
    mssdemux->manifest_buffer = buffer;
  else
    mssdemux->manifest_buffer =
        gst_buffer_append (mssdemux->manifest_buffer, buffer);

  GST_INFO_OBJECT (mssdemux, "Received manifest buffer, total size is %i bytes",
      (gint) gst_buffer_get_size (mssdemux->manifest_buffer));

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
gst_mss_demux_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (parent);
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
    ret = gst_pad_event_default (pad, parent, event);
  } else {
    gst_event_unref (event);
  }

  return ret;
}

static void
gst_mss_demux_stop_tasks (GstMssDemux * mssdemux, gboolean immediate)
{
  GSList *iter;

  GST_OBJECT_LOCK (mssdemux);
  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    gst_task_stop (stream->download_task);
    stream->cancelled = TRUE;
    if (immediate)
      g_cond_signal (&stream->fragment_download_cond);
  }
  GST_OBJECT_UNLOCK (mssdemux);

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    gst_task_join (stream->download_task);
    stream->download_error_count = 0;
  }
}

static void
gst_mss_demux_restart_tasks (GstMssDemux * mssdemux)
{
  GSList *iter;
  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;
    stream->cancelled = FALSE;
    gst_task_start (stream->download_task);
  }
}

static gboolean
gst_mss_demux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMssDemux *mssdemux;

  mssdemux = GST_MSS_DEMUX_CAST (parent);

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
      gboolean update;

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

      for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
        GstMssDemuxStream *stream = iter->data;

        stream->eos = FALSE;
        if (flags & GST_SEEK_FLAG_FLUSH) {
          stream->last_ret = GST_FLOW_OK;
        }

        gst_segment_do_seek (&stream->segment, rate, format, flags,
            start_type, start, stop_type, stop, &update);

        newsegment = gst_event_new_segment (&stream->segment);
        gst_event_set_seqnum (newsegment, gst_event_get_seqnum (event));
        if (stream->pending_segment)
          gst_event_unref (stream->pending_segment);
        stream->pending_segment = newsegment;
      }

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GstEvent *flush = gst_event_new_flush_stop (TRUE);
        GST_DEBUG_OBJECT (mssdemux, "sending flush stop");

        gst_event_set_seqnum (flush, gst_event_get_seqnum (event));
        gst_mss_demux_push_src_event (mssdemux, flush);
      }

      gst_mss_demux_restart_tasks (mssdemux);

      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_RECONFIGURE:{
      GSList *iter;

      for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
        GstMssDemuxStream *stream = iter->data;

        if (stream->pad == pad) {
          GST_OBJECT_LOCK (mssdemux);

          if (stream->last_ret == GST_FLOW_NOT_LINKED) {
            GST_DEBUG_OBJECT (stream->pad, "Received reconfigure");
            stream->restart_download = TRUE;
            gst_task_start (stream->download_task);
          }
          GST_OBJECT_UNLOCK (mssdemux);
          gst_event_unref (event);
          return TRUE;
        }
      }
    }
      break;
    case GST_EVENT_LATENCY:{
      /* Upstream and our internal source are irrelevant
       * for latency, and we should not fail here to
       * configure the latency */
      gst_event_unref (event);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);

not_supported:
  gst_event_unref (event);
  return FALSE;
}

static gboolean
gst_mss_demux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMssDemux *mssdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  mssdemux = GST_MSS_DEMUX (parent);

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

      if (!mssdemux->manifest) {
        return FALSE;           /* no seeking without manifest */
      } else if (gst_mss_manifest_is_live (mssdemux->manifest)) {
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
  gchar *name = NULL;
  GstPad *srcpad = NULL;
  GstMssStreamType streamtype;
  GstPadTemplate *tmpl = NULL;

  streamtype = gst_mss_stream_get_type (manifeststream);
  GST_DEBUG_OBJECT (mssdemux, "Found stream of type: %s",
      gst_mss_stream_type_name (streamtype));

  /* TODO use stream's name/bitrate/index as the pad name? */
  if (streamtype == MSS_STREAM_TYPE_VIDEO) {
    name = g_strdup_printf ("video_%02u", mssdemux->n_videos++);
    tmpl = gst_static_pad_template_get (&gst_mss_demux_videosrc_template);
  } else if (streamtype == MSS_STREAM_TYPE_AUDIO) {
    name = g_strdup_printf ("audio_%02u", mssdemux->n_audios++);
    tmpl = gst_static_pad_template_get (&gst_mss_demux_audiosrc_template);
  }

  if (tmpl != NULL) {
    srcpad =
        GST_PAD_CAST (gst_ghost_pad_new_no_target_from_template (name, tmpl));
    g_free (name);
    gst_object_unref (tmpl);
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
    gst_pad_set_element_private (srcpad, stream);
    gst_mss_stream_set_active (manifeststream, TRUE);
    mssdemux->streams = g_slist_append (mssdemux->streams, stream);
  }

  /* select initial bitrates */
  GST_OBJECT_LOCK (mssdemux);
  GST_INFO_OBJECT (mssdemux, "Changing max bitrate to %" G_GUINT64_FORMAT,
      mssdemux->connection_speed);
  gst_mss_manifest_change_bitrate (mssdemux->manifest,
      mssdemux->connection_speed);
  mssdemux->update_bitrates = FALSE;
  GST_OBJECT_UNLOCK (mssdemux);
}

static GstCaps *
create_mss_caps (GstMssDemuxStream * stream, GstCaps * caps)
{
  return gst_caps_new_simple ("video/quicktime", "variant", G_TYPE_STRING,
      "mss-fragmented", "timescale", G_TYPE_UINT64,
      gst_mss_stream_get_timescale (stream->manifest_stream), "media-caps",
      GST_TYPE_CAPS, caps, NULL);
}

static gboolean
gst_mss_demux_expose_stream (GstMssDemux * mssdemux, GstMssDemuxStream * stream)
{
  GstCaps *caps;
  GstCaps *media_caps;
  GstPad *pad = stream->pad;

  media_caps = gst_mss_stream_get_caps (stream->manifest_stream);

  if (media_caps) {
    gchar *name = gst_pad_get_name (pad);
    const gchar *lang;
    GstEvent *event;
    gchar *stream_id;
    gst_pad_set_active (pad, TRUE);

    caps = create_mss_caps (stream, media_caps);
    gst_caps_unref (media_caps);

    stream_id =
        gst_pad_create_stream_id (pad, GST_ELEMENT_CAST (mssdemux), name);

    event =
        gst_pad_get_sticky_event (mssdemux->sinkpad, GST_EVENT_STREAM_START, 0);
    if (event) {
      if (gst_event_parse_group_id (event, &mssdemux->group_id))
        mssdemux->have_group_id = TRUE;
      else
        mssdemux->have_group_id = FALSE;
      gst_event_unref (event);
    } else if (!mssdemux->have_group_id) {
      mssdemux->have_group_id = TRUE;
      mssdemux->group_id = gst_util_group_id_next ();
    }
    event = gst_event_new_stream_start (stream_id);
    if (mssdemux->have_group_id)
      gst_event_set_group_id (event, mssdemux->group_id);

    gst_pad_push_event (pad, event);
    g_free (stream_id);
    g_free (name);

    gst_pad_set_caps (pad, caps);
    if (stream->caps)
      gst_caps_unref (stream->caps);
    stream->caps = caps;

    lang = gst_mss_stream_get_lang (stream->manifest_stream);
    if (lang != NULL) {
      GstTagList *tags;

      tags = gst_tag_list_new (GST_TAG_LANGUAGE_CODE, lang, NULL);
      gst_pad_push_event (stream->pad, gst_event_new_tag (tags));
    }

    gst_pad_push_event (stream->pad, gst_event_new_segment (&stream->segment));

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
  gboolean ret;
  GSList *iter;

  g_return_val_if_fail (mssdemux->manifest_buffer != NULL, FALSE);
  g_return_val_if_fail (mssdemux->manifest == NULL, FALSE);

  query = gst_query_new_uri ();
  ret = gst_pad_peer_query (mssdemux->sinkpad, query);
  if (ret) {
    gchar *uri, *redirect_uri;
    gboolean permanent;
    gchar *baseurl_end;

    gst_query_parse_uri (query, &uri);
    gst_query_parse_uri_redirection (query, &redirect_uri);
    gst_query_parse_uri_redirection_permanent (query, &permanent);

    GST_INFO_OBJECT (mssdemux, "Upstream is using URI: %s (redirect: %s)", uri,
        GST_STR_NULL (redirect_uri));

    mssdemux->manifest_uri = g_strdup ((permanent
            && redirect_uri) ? redirect_uri : uri);
    mssdemux->base_url = g_strdup (redirect_uri ? redirect_uri : uri);
    baseurl_end = g_strrstr (mssdemux->base_url, "/Manifest");
    if (baseurl_end == NULL) {
      /* second try */
      baseurl_end = g_strrstr (mssdemux->base_url, "/manifest");
    }

    if (baseurl_end) {
      /* set the new end of the string */
      baseurl_end[0] = '\0';
    } else {
      GST_WARNING_OBJECT (mssdemux, "Stream's URI didn't end with /manifest");
    }

    g_free (uri);
    g_free (redirect_uri);
  }
  gst_query_unref (query);

  if (mssdemux->base_url == NULL) {
    GST_ELEMENT_ERROR (mssdemux, RESOURCE, NOT_FOUND,
        (_("Couldn't get the Manifest's URI")),
        ("need to get the manifest's URI from upstream elements"));
    return FALSE;
  }

  GST_INFO_OBJECT (mssdemux, "Received manifest: %i bytes",
      (gint) gst_buffer_get_size (mssdemux->manifest_buffer));

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
  gchar *baseurl_end;

  downloader = gst_uri_downloader_new ();

  manifest_data =
      gst_uri_downloader_fetch_uri (downloader, mssdemux->manifest_uri, NULL,
      TRUE, TRUE, TRUE, NULL);

  g_free (mssdemux->manifest_uri);
  g_free (mssdemux->base_url);
  mssdemux->manifest_uri = g_strdup ((manifest_data->redirect_permanent
          && manifest_data->
          redirect_uri) ? manifest_data->redirect_uri : manifest_data->uri);
  mssdemux->base_url =
      g_strdup (manifest_data->
      redirect_uri ? manifest_data->redirect_uri : manifest_data->uri);
  baseurl_end = g_strrstr (mssdemux->base_url, "/Manifest");
  if (baseurl_end == NULL) {
    /* second try */
    baseurl_end = g_strrstr (mssdemux->base_url, "/manifest");
  }

  if (baseurl_end) {
    /* set the new end of the string */
    baseurl_end[0] = '\0';
  } else {
    GST_WARNING_OBJECT (mssdemux, "Stream's URI didn't end with /manifest");
  }

  manifest_buffer = gst_fragment_get_buffer (manifest_data);
  g_object_unref (manifest_data);

  gst_mss_manifest_reload_fragments (mssdemux->manifest, manifest_buffer);
  gst_buffer_replace (&mssdemux->manifest_buffer, manifest_buffer);
  gst_buffer_unref (manifest_buffer);

  g_object_unref (downloader);
}

static GstEvent *
gst_mss_demux_reconfigure_stream (GstMssDemuxStream * stream)
{
  GstEvent *capsevent = NULL;
  GstMssDemux *mssdemux = stream->parent;
  guint64 bitrate = 0;

  if (stream->download_total_time)
    bitrate =
        (stream->download_total_bytes * 8) /
        ((double) stream->download_total_time / G_GUINT64_CONSTANT (1000000));

  if (stream->current_download_rate != -1)
    bitrate = (stream->current_download_rate + bitrate * 3) / 4;
  if (bitrate > G_MAXINT)
    bitrate = G_MAXINT;
  stream->current_download_rate = bitrate;
  bitrate *= mssdemux->bitrate_limit;

  if (mssdemux->connection_speed) {
    bitrate = MIN (mssdemux->connection_speed, bitrate);
  }

  GST_DEBUG_OBJECT (stream->pad,
      "Current stream download bitrate %" G_GUINT64_FORMAT, bitrate);

  if (gst_mss_stream_select_bitrate (stream->manifest_stream, bitrate)) {
    GstCaps *caps;
    caps = gst_mss_stream_get_caps (stream->manifest_stream);

    GST_DEBUG_OBJECT (stream->pad,
        "Starting streams reconfiguration due to bitrate changes");
    if (stream->caps)
      gst_caps_unref (stream->caps);
    stream->caps = create_mss_caps (stream, caps);

    GST_DEBUG_OBJECT (stream->pad,
        "Stream changed bitrate to %" G_GUINT64_FORMAT " caps: %"
        GST_PTR_FORMAT,
        gst_mss_stream_get_current_bitrate (stream->manifest_stream), caps);

    gst_caps_unref (caps);

    capsevent = gst_event_new_caps (stream->caps);
    GST_DEBUG_OBJECT (stream->pad, "Finished streams reconfiguration");
  }
  return capsevent;
}

static GstFlowReturn
_src_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPad *srcpad = (GstPad *) parent;
  GstMssDemux *demux = (GstMssDemux *) GST_PAD_PARENT (srcpad);
  GstMssDemuxStream *stream = gst_pad_get_element_private (srcpad);
  GstFlowReturn ret;
  gboolean discont = FALSE;

  if (stream->starting_fragment) {
#if 0
    if (demux->segment.rate < 0)
      /* Set DISCONT flag for every first buffer in reverse playback mode
       * as each fragment for its own has to be reversed */
      discont = TRUE;
#endif
    stream->starting_fragment = FALSE;

    GST_BUFFER_PTS (buffer) =
        gst_mss_stream_get_fragment_gst_timestamp (stream->manifest_stream);

    GST_LOG_OBJECT (stream->pad, "set fragment pts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));

    if (stream->pending_segment) {
      /* And send a newsegment */
      gst_pad_push_event (stream->pad, stream->pending_segment);
      stream->pending_segment = NULL;
    }
    stream->segment.position = GST_BUFFER_PTS (buffer);

  } else {
    GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  }

  if (discont) {
    GST_DEBUG_OBJECT (stream->pad, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;

  /* accumulate time and size to get this chunk */
  stream->download_total_time +=
      g_get_monotonic_time () - stream->download_start_time;
  stream->download_total_bytes += gst_buffer_get_size (buffer);

  stream->have_data = TRUE;

  ret = gst_proxy_pad_chain_default (pad, parent, buffer);
  stream->download_start_time = g_get_monotonic_time ();
  GST_LOG_OBJECT (pad, "Chain res: %d %s", ret, gst_flow_get_name (ret));

  if (ret != GST_FLOW_OK) {
    if (ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL),
          ("stream stopped, reason %s", gst_flow_get_name (ret)));

      /* TODO push this on all pads */
      gst_pad_push_event (stream->pad, gst_event_new_eos ());
    } else {
      GST_DEBUG_OBJECT (stream->pad, "stream stopped, reason %s",
          gst_flow_get_name (ret));
    }

    /* TODO properly stop tasks */
    /* gst_hls_demux_pause_tasks (demux); */
  }

  /* avoid having the source handle the same error again */
  stream->last_ret = ret;
  ret = GST_FLOW_OK;

  return ret;
}

static gboolean
_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstPad *srcpad = GST_PAD_CAST (parent);
  GstMssDemuxStream *stream = gst_pad_get_element_private (srcpad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      g_cond_signal (&stream->fragment_download_cond);
      break;
    default:
      break;
  }

  gst_event_unref (event);

  return TRUE;
}

static gboolean
_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      return FALSE;
      break;
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_mss_demux_stream_update_source (GstMssDemuxStream * stream,
    const gchar * uri, const gchar * referer, gboolean refresh,
    gboolean allow_cache)
{
  GstMssDemux *demux = stream->parent;

  if (!gst_uri_is_valid (uri)) {
    GST_WARNING_OBJECT (stream->pad, "Invalid URI: %s", uri);
    stream->last_ret = GST_FLOW_ERROR;
    return FALSE;
  }

  if (stream->src != NULL) {
    gchar *old_protocol, *new_protocol;
    gchar *old_uri;

    old_uri = gst_uri_handler_get_uri (GST_URI_HANDLER (stream->src));
    old_protocol = gst_uri_get_protocol (old_uri);
    new_protocol = gst_uri_get_protocol (uri);

    if (!g_str_equal (old_protocol, new_protocol)) {
      gst_object_unref (stream->src_srcpad);
      gst_element_set_state (stream->src, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (demux), stream->src);
      stream->src = NULL;
      stream->src_srcpad = NULL;
      GST_DEBUG_OBJECT (demux, "Can't re-use old source element");
    } else {
      GError *err = NULL;

      GST_DEBUG_OBJECT (demux, "Re-using old source element");
      if (!gst_uri_handler_set_uri (GST_URI_HANDLER (stream->src), uri, &err)) {
        GST_DEBUG_OBJECT (demux, "Failed to re-use old source element: %s",
            err->message);
        g_clear_error (&err);
        gst_element_set_state (stream->src, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (demux), stream->src);
        stream->src = NULL;
      }
    }
    g_free (old_uri);
    g_free (old_protocol);
    g_free (new_protocol);
  }

  if (stream->src == NULL) {
    GObjectClass *gobject_class;
    GstPad *internal_pad;

    stream->src = gst_element_make_from_uri (GST_URI_SRC, uri, NULL, NULL);
    if (stream->src == NULL) {
      GST_ELEMENT_ERROR (demux, CORE, MISSING_PLUGIN,
          ("Missing plugin to handle URI: '%s'", uri), (NULL));
      return FALSE;
    }

    gobject_class = G_OBJECT_GET_CLASS (stream->src);

    if (g_object_class_find_property (gobject_class, "compress"))
      g_object_set (stream->src, "compress", FALSE, NULL);
    if (g_object_class_find_property (gobject_class, "keep-alive"))
      g_object_set (stream->src, "keep-alive", TRUE, NULL);
    if (g_object_class_find_property (gobject_class, "extra-headers")) {
      if (referer || refresh || !allow_cache) {
        GstStructure *extra_headers = gst_structure_new_empty ("headers");

        if (referer)
          gst_structure_set (extra_headers, "Referer", G_TYPE_STRING, referer,
              NULL);

        if (!allow_cache)
          gst_structure_set (extra_headers, "Cache-Control", G_TYPE_STRING,
              "no-cache", NULL);
        else if (refresh)
          gst_structure_set (extra_headers, "Cache-Control", G_TYPE_STRING,
              "max-age=0", NULL);

        g_object_set (stream->src, "extra-headers", extra_headers, NULL);

        gst_structure_free (extra_headers);
      } else {
        g_object_set (stream->src, "extra-headers", NULL, NULL);
      }
    }

    gst_element_set_locked_state (stream->src, TRUE);
    gst_bin_add (GST_BIN_CAST (demux), stream->src);
    stream->src_srcpad = gst_element_get_static_pad (stream->src, "src");

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (stream->pad),
        stream->src_srcpad);

    /* set up our internal pad to drop all events from
     * the http src we don't care about. On the chain function
     * we just push the buffer forward, but this way dash can get
     * the flow return from downstream */
    internal_pad =
        GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD (stream->pad)));
    gst_pad_set_chain_function (GST_PAD_CAST (internal_pad), _src_chain);
    gst_pad_set_event_function (GST_PAD_CAST (internal_pad), _src_event);
    /* need to set query otherwise deadlocks happen with allocation queries */
    gst_pad_set_query_function (GST_PAD_CAST (internal_pad), _src_query);
    gst_object_unref (internal_pad);
  }
  return TRUE;
}

/* must be called with the stream's fragment_download_lock */
static void
gst_mss_demux_stream_download_uri (GstMssDemux * demux,
    GstMssDemuxStream * stream, const gchar * uri, gint64 start, gint64 end)
{
  GST_DEBUG_OBJECT (stream->pad, "Downloading uri: %s, range:%" G_GINT64_FORMAT
      " - %" G_GINT64_FORMAT, uri, start, end);

  if (!gst_mss_demux_stream_update_source (stream, uri, NULL, FALSE, TRUE)) {
    return;
  }

  if (gst_element_set_state (stream->src,
          GST_STATE_READY) != GST_STATE_CHANGE_FAILURE) {
    if (start != 0 || end != -1) {
      if (!gst_element_send_event (stream->src, gst_event_new_seek (1.0,
                  GST_FORMAT_BYTES, (GstSeekFlags) GST_SEEK_FLAG_FLUSH,
                  GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_SET, end))) {

        /* looks like the source can't handle seeks in READY */
/*
        *err = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_NOT_IMPLEMENTED,
            "Source element can't handle range requests");
*/
        stream->last_ret = GST_FLOW_ERROR;
      }
    }

    if (G_LIKELY (stream->last_ret == GST_FLOW_OK)) {
      /* flush the proxypads so that the EOS state is reset */
      gst_pad_push_event (stream->src_srcpad, gst_event_new_flush_start ());
      gst_pad_push_event (stream->src_srcpad, gst_event_new_flush_stop (TRUE));

      stream->download_start_time = g_get_monotonic_time ();
      gst_element_sync_state_with_parent (stream->src);

      /* wait for the fragment to be completely downloaded */
      GST_DEBUG_OBJECT (stream->pad,
          "Waiting for fragment download to finish: %s", uri);
      g_cond_wait (&stream->fragment_download_cond,
          &stream->fragment_download_lock);
    }
  } else {
    stream->last_ret = GST_FLOW_CUSTOM_ERROR;
  }

  gst_element_set_state (stream->src, GST_STATE_READY);
}

static GstFlowReturn
gst_mss_demux_stream_download_fragment (GstMssDemuxStream * stream)
{
  GstMssDemux *mssdemux = stream->parent;
  gchar *path = NULL;
  gchar *url = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  /* special case for not-linked streams */
  if (stream->last_ret == GST_FLOW_NOT_LINKED) {
    GST_DEBUG_OBJECT (stream->pad, "Skipping download for not-linked stream %p",
        stream);
    return GST_FLOW_NOT_LINKED;
  }

  g_mutex_lock (&stream->fragment_download_lock);
  GST_DEBUG_OBJECT (stream->pad, "Getting url for stream");
  ret = gst_mss_stream_get_fragment_url (stream->manifest_stream, &path);
  switch (ret) {
    case GST_FLOW_OK:
      break;                    /* all is good, let's go */
    case GST_FLOW_EOS:
      g_free (path);
      if (gst_mss_manifest_is_live (mssdemux->manifest)) {
        gst_mss_demux_reload_manifest (mssdemux);
        g_mutex_unlock (&stream->fragment_download_lock);
        return GST_FLOW_OK;
      }
      g_mutex_unlock (&stream->fragment_download_lock);
      return GST_FLOW_EOS;
    case GST_FLOW_ERROR:
      g_mutex_unlock (&stream->fragment_download_lock);
      g_free (path);
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

  stream->starting_fragment = TRUE;

  gst_mss_demux_stream_download_uri (mssdemux, stream, url, 0, -1);

  g_free (path);
  g_free (url);
  g_mutex_unlock (&stream->fragment_download_lock);

  if (stream->last_ret != GST_FLOW_OK) {
    GST_INFO_OBJECT (mssdemux, "No fragment downloaded");
    /* TODO check if we are truly stoping */
    if (stream->last_ret != GST_FLOW_ERROR
        && gst_mss_manifest_is_live (mssdemux->manifest)) {
      /* looks like there is no way of knowing when a live stream has ended
       * Have to assume we are falling behind and cause a manifest reload */
      return GST_FLOW_OK;
    }
  }

  return stream->last_ret;

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
  GstFlowReturn ret;
  GstEvent *gap = NULL;
  GstEvent *capsevent = NULL;

  GST_LOG_OBJECT (stream->pad, "download loop start");

  GST_OBJECT_LOCK (mssdemux);
  if (G_UNLIKELY (stream->restart_download)) {
    GstClockTime cur, ts;
    gint64 pos;

    GST_DEBUG_OBJECT (stream->pad,
        "Activating stream due to reconfigure event");

    cur = stream->segment.position;

    if (gst_pad_peer_query_position (stream->pad, GST_FORMAT_TIME, &pos)) {
      ts = (GstClockTime) pos;
      GST_DEBUG_OBJECT (mssdemux, "Downstream position: %"
          GST_TIME_FORMAT, GST_TIME_ARGS (ts));
    } else {
      GST_DEBUG_OBJECT (mssdemux, "Downstream position query failed, "
          "failling back to segment position");
      ts = stream->segment.position;
    }

    /* we might have already pushed this data */
    ts = MAX (ts, cur);

    GST_DEBUG_OBJECT (stream->pad, "Restarting stream at "
        "position %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));

    if (GST_CLOCK_TIME_IS_VALID (ts)) {
      gst_mss_stream_seek (stream->manifest_stream, ts);

      if (cur < ts) {
        gap = gst_event_new_gap (cur, ts - cur);
      }
    }

    stream->restart_download = FALSE;
    stream->last_ret = GST_FLOW_OK;
  }
  capsevent = gst_mss_demux_reconfigure_stream (stream);
  GST_OBJECT_UNLOCK (mssdemux);

  if (G_UNLIKELY (gap != NULL))
    gst_pad_push_event (stream->pad, gap);
  if (G_UNLIKELY (capsevent != NULL))
    gst_pad_push_event (stream->pad, capsevent);

  ret = gst_mss_demux_stream_download_fragment (stream);

  GST_OBJECT_LOCK (mssdemux);
  if (stream->cancelled) {
    GST_OBJECT_UNLOCK (mssdemux);
    stream->last_ret = GST_FLOW_FLUSHING;
    goto cancelled;
  }

  switch (ret) {
    case GST_FLOW_OK:
      stream->download_error_count = 0;
      stream->segment.position +=
          gst_mss_stream_get_fragment_gst_duration (stream->manifest_stream);
      gst_mss_stream_advance_fragment (stream->manifest_stream);
      break;                    /* all is good, let's go */

    case GST_FLOW_EOS:
      GST_DEBUG_OBJECT (stream->pad, "EOS, stopping download loop");
      /* we push the EOS after releasing the object lock */
      gst_task_pause (stream->download_task);
      break;

    case GST_FLOW_NOT_LINKED:
      gst_task_pause (stream->download_task);
      if (gst_mss_demux_combine_flows (mssdemux) == GST_FLOW_NOT_LINKED) {
        GST_ELEMENT_ERROR (mssdemux, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("stream stopped, reason %s",
                gst_flow_get_name (GST_FLOW_NOT_LINKED)));
      }
      break;

    case GST_FLOW_FLUSHING:{
      GSList *iter;

      for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
        GstMssDemuxStream *other;

        other = iter->data;
        gst_task_pause (other->download_task);
      }
    }
      break;

    default:
      if (ret <= GST_FLOW_ERROR) {
        GST_WARNING_OBJECT (mssdemux, "Error while downloading fragment");
        if (++stream->download_error_count >= MAX_DOWNLOAD_ERROR_COUNT) {
          goto download_error;
        }
      }
      break;
  }
  GST_OBJECT_UNLOCK (mssdemux);

  if (G_UNLIKELY (ret == GST_FLOW_EOS)) {
    gst_mss_demux_stream_push_event (stream, gst_event_new_eos ());
  }

end:
  GST_LOG_OBJECT (stream->pad, "download loop end");
  return;

cancelled:
  {
    GST_DEBUG_OBJECT (stream->pad, "Stream has been cancelled");
    goto end;
  }
download_error:
  {
    GST_OBJECT_UNLOCK (mssdemux);
    GST_ELEMENT_ERROR (mssdemux, RESOURCE, NOT_FOUND,
        (_("Couldn't download fragments")),
        ("fragment downloading has failed too much consecutive times"));
    goto end;
  }
}

static GstFlowReturn
gst_mss_demux_combine_flows (GstMssDemux * mssdemux)
{
  gboolean all_notlinked = TRUE;
  GSList *iter;

  for (iter = mssdemux->streams; iter; iter = g_slist_next (iter)) {
    GstMssDemuxStream *stream = iter->data;

    if (stream->last_ret != GST_FLOW_NOT_LINKED)
      all_notlinked = FALSE;

    if (stream->last_ret <= GST_FLOW_NOT_NEGOTIATED
        || stream->last_ret == GST_FLOW_FLUSHING)
      return stream->last_ret;
  }
  if (all_notlinked)
    return GST_FLOW_NOT_LINKED;
  return GST_FLOW_OK;
}

static gboolean
gst_mss_demux_stream_push_event (GstMssDemuxStream * stream, GstEvent * event)
{
  gboolean ret;

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    stream->eos = TRUE;
  }
  GST_DEBUG_OBJECT (stream->pad, "Pushing event %" GST_PTR_FORMAT, event);
  ret = gst_pad_push_event (stream->pad, event);
  return ret;
}
