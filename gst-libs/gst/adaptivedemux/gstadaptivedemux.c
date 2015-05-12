/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
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
 * SECTION:gstadaptivedemux
 * @short_description: Base class for adaptive demuxers
 * @see_also:
 *
 * What is an adaptive demuxer?
 * Adaptive demuxers are special demuxers in the sense that they don't
 * actually demux data received from upstream but download the data
 * themselves.
 *
 * Adaptive formats (HLS, DASH, MSS) are composed of a manifest file and
 * a set of fragments. The manifest describes the available media and
 * the sequence of fragments to use. Each fragments contains a small
 * part of the media (typically only a few seconds). It is possible for
 * the manifest to have the same media available in different configurations
 * (bitrates for example) so that the client can select the one that
 * best suits its scenario (network fluctuation, hardware requirements...).
 * It is possible to switch from one representation of the media to another
 * during playback. That's why it is called 'adaptive', because it can be
 * adapted to the client's needs.
 *
 * Architectural overview:
 * The manifest is received by the demuxer in its sink pad and, upon receiving
 * EOS, it parses the manifest and exposes the streams available in it. For
 * each stream a source element will be created and will download the list
 * of fragments one by one. Once a fragment is finished downloading, the next
 * URI is set to the source element and it starts fetching it and pushing
 * through the stream's pad. This implies that each stream is independent from
 * each other as it runs on a separate thread.
 *
 * After downloading each fragment, the download rate of it is calculated and
 * the demuxer has a chance to switch to a different bitrate if needed. The
 * switch can be done by simply pushing a new caps before the next fragment
 * when codecs are the same, or by exposing a new pad group if it needs
 * a codec change.
 *
 * Extra features:
 * - Not linked streams: Streams that are not-linked have their download threads
 *                       interrupted to save network bandwidth. When they are
 *                       relinked a reconfigure event is received and the
 *                       stream is restarted.
 *
 * Subclasses:
 * While GstAdaptiveDemux is responsible for the workflow, it knows nothing
 * about the intrinsics of the subclass formats, so the subclasses are
 * resposible for maintaining the manifest data structures and stream
 * information.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstadaptivedemux.h"
#include "gst/gst-i18n-plugin.h"
#include <gst/base/gstadapter.h>

GST_DEBUG_CATEGORY (adaptivedemux_debug);
#define GST_CAT_DEFAULT adaptivedemux_debug

#define GST_ADAPTIVE_DEMUX_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_ADAPTIVE_DEMUX, \
        GstAdaptiveDemuxPrivate))

#define MAX_DOWNLOAD_ERROR_COUNT 3
#define DEFAULT_FAILED_COUNT 3
#define DEFAULT_LOOKBACK_FRAGMENTS 3
#define DEFAULT_CONNECTION_SPEED 0
#define DEFAULT_BITRATE_LIMIT 0.8

enum
{
  PROP_0,
  PROP_LOOKBACK_FRAGMENTS,
  PROP_CONNECTION_SPEED,
  PROP_BITRATE_LIMIT,
  PROP_LAST
};

enum GstAdaptiveDemuxFlowReturn
{
  GST_ADAPTIVE_DEMUX_FLOW_SWITCH = GST_FLOW_CUSTOM_SUCCESS_2 + 1
};

struct _GstAdaptiveDemuxPrivate
{
  GstAdapter *input_adapter;
  gboolean have_manifest;

  GstUriDownloader *downloader;

  GList *old_streams;

  GstTask *updates_task;
  GRecMutex updates_lock;
  GMutex updates_timed_lock;
  GCond updates_timed_cond;
  gboolean stop_updates_task;
  gint update_failed_count;

  gint64 next_update;

  gboolean exposing;
};

static GstBinClass *parent_class = NULL;
static void gst_adaptive_demux_class_init (GstAdaptiveDemuxClass * klass);
static void gst_adaptive_demux_init (GstAdaptiveDemux * dec,
    GstAdaptiveDemuxClass * klass);
static void gst_adaptive_demux_finalize (GObject * object);
static GstStateChangeReturn gst_adaptive_demux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_adaptive_demux_handle_message (GstBin * bin, GstMessage * msg);

static gboolean gst_adaptive_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_adaptive_demux_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_adaptive_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_adaptive_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean
gst_adaptive_demux_push_src_event (GstAdaptiveDemux * demux, GstEvent * event);

static void gst_adaptive_demux_updates_loop (GstAdaptiveDemux * demux);
static void gst_adaptive_demux_stream_download_loop (GstAdaptiveDemuxStream *
    stream);
static void gst_adaptive_demux_reset (GstAdaptiveDemux * demux);
static gboolean gst_adaptive_demux_expose_streams (GstAdaptiveDemux * demux,
    gboolean first_segment);
static gboolean gst_adaptive_demux_is_live (GstAdaptiveDemux * demux);
static GstFlowReturn gst_adaptive_demux_stream_seek (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstClockTime ts);
static gboolean gst_adaptive_demux_stream_has_next_fragment (GstAdaptiveDemux *
    demux, GstAdaptiveDemuxStream * stream);
static gboolean gst_adaptive_demux_stream_select_bitrate (GstAdaptiveDemux *
    demux, GstAdaptiveDemuxStream * stream, guint64 bitrate);
static GstFlowReturn
gst_adaptive_demux_stream_update_fragment_info (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static gint64
gst_adaptive_demux_stream_get_fragment_waiting_time (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn gst_adaptive_demux_update_manifest (GstAdaptiveDemux *
    demux);
static GstFlowReturn
gst_adaptive_demux_update_manifest_default (GstAdaptiveDemux * demux);
static gboolean gst_adaptive_demux_has_next_period (GstAdaptiveDemux * demux);
static void gst_adaptive_demux_advance_period (GstAdaptiveDemux * demux);

static void gst_adaptive_demux_stream_free (GstAdaptiveDemuxStream * stream);
static GstFlowReturn
gst_adaptive_demux_stream_push_event (GstAdaptiveDemuxStream * stream,
    GstEvent * event);
static void gst_adaptive_demux_stream_download_wait (GstAdaptiveDemuxStream *
    stream, GstClockTime time_diff);

static void gst_adaptive_demux_start_tasks (GstAdaptiveDemux * demux);
static void gst_adaptive_demux_stop_tasks (GstAdaptiveDemux * demux);
static GstFlowReturn gst_adaptive_demux_combine_flows (GstAdaptiveDemux *
    demux);
static void
gst_adaptive_demux_stream_fragment_download_finish (GstAdaptiveDemuxStream *
    stream, GstFlowReturn ret, GError * err);
static GstFlowReturn
gst_adaptive_demux_stream_data_received_default (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);
static GstFlowReturn
gst_adaptive_demux_stream_finish_fragment_default (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream);


/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_adaptive_demux_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstAdaptiveDemuxClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_adaptive_demux_class_init,
      NULL,
      NULL,
      sizeof (GstAdaptiveDemux),
      0,
      (GInstanceInitFunc) gst_adaptive_demux_init,
    };

    _type = g_type_register_static (GST_TYPE_BIN,
        "GstAdaptiveDemux", &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static void
gst_adaptive_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX (object);

  switch (prop_id) {
    case PROP_LOOKBACK_FRAGMENTS:
      demux->num_lookback_fragments = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_SPEED:
      demux->connection_speed = g_value_get_uint (value) * 1000;
      GST_DEBUG_OBJECT (demux, "Connection speed set to %u",
          demux->connection_speed);
      break;
    case PROP_BITRATE_LIMIT:
      demux->bitrate_limit = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_adaptive_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX (object);

  switch (prop_id) {
    case PROP_LOOKBACK_FRAGMENTS:
      g_value_set_uint (value, demux->num_lookback_fragments);
      break;
    case PROP_CONNECTION_SPEED:
      g_value_set_uint (value, demux->connection_speed / 1000);
      break;
    case PROP_BITRATE_LIMIT:
      g_value_set_float (value, demux->bitrate_limit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_adaptive_demux_class_init (GstAdaptiveDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (adaptivedemux_debug, "adaptivedemux", 0,
      "Base Adaptive Demux");

  parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (GstAdaptiveDemuxPrivate));

  gobject_class->set_property = gst_adaptive_demux_set_property;
  gobject_class->get_property = gst_adaptive_demux_get_property;
  gobject_class->finalize = gst_adaptive_demux_finalize;

  g_object_class_install_property (gobject_class, PROP_LOOKBACK_FRAGMENTS,
      g_param_spec_uint ("num-lookback-fragments",
          "Number of fragments to look back",
          "The number of fragments the demuxer will look back to calculate an average bitrate",
          1, G_MAXUINT, DEFAULT_LOOKBACK_FRAGMENTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = calculate from downloaded"
          " fragments)", 0, G_MAXUINT / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* FIXME 2.0: rename this property to bandwidth-usage or any better name */
  g_object_class_install_property (gobject_class, PROP_BITRATE_LIMIT,
      g_param_spec_float ("bitrate-limit",
          "Bitrate limit in %",
          "Limit of the available bitrate to use when switching to alternates.",
          0, 1, DEFAULT_BITRATE_LIMIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_adaptive_demux_change_state;

  gstbin_class->handle_message = gst_adaptive_demux_handle_message;

  klass->data_received = gst_adaptive_demux_stream_data_received_default;
  klass->finish_fragment = gst_adaptive_demux_stream_finish_fragment_default;
  klass->update_manifest = gst_adaptive_demux_update_manifest_default;
}

static void
gst_adaptive_demux_init (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxClass * klass)
{
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG_OBJECT (demux, "gst_adaptive_demux_init");

  demux->priv = GST_ADAPTIVE_DEMUX_GET_PRIVATE (demux);
  demux->priv->input_adapter = gst_adapter_new ();
  demux->downloader = gst_uri_downloader_new ();
  demux->stream_struct_size = sizeof (GstAdaptiveDemuxStream);

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  g_rec_mutex_init (&demux->priv->updates_lock);
  demux->priv->updates_task =
      gst_task_new ((GstTaskFunction) gst_adaptive_demux_updates_loop,
      demux, NULL);
  gst_task_set_lock (demux->priv->updates_task, &demux->priv->updates_lock);

  g_mutex_init (&demux->priv->updates_timed_lock);
  g_cond_init (&demux->priv->updates_timed_cond);

  g_cond_init (&demux->manifest_cond);
  g_mutex_init (&demux->manifest_lock);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  demux->sinkpad = pad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_sink_event));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_sink_chain));

  /* Properties */
  demux->num_lookback_fragments = DEFAULT_LOOKBACK_FRAGMENTS;
  demux->bitrate_limit = DEFAULT_BITRATE_LIMIT;
  demux->connection_speed = DEFAULT_CONNECTION_SPEED;

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
}

static void
gst_adaptive_demux_finalize (GObject * object)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (object);
  GstAdaptiveDemuxPrivate *priv = demux->priv;

  GST_DEBUG_OBJECT (object, "finalize");

  g_object_unref (priv->input_adapter);
  g_object_unref (demux->downloader);

  g_mutex_clear (&priv->updates_timed_lock);
  g_cond_clear (&priv->updates_timed_cond);
  g_cond_clear (&demux->manifest_cond);
  g_object_unref (priv->updates_task);
  g_rec_mutex_clear (&priv->updates_lock);
  g_mutex_clear (&demux->manifest_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_adaptive_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adaptive_demux_reset (demux);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return result;
}

static gboolean
gst_adaptive_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (parent);
  GstAdaptiveDemuxClass *demux_class;

  switch (event->type) {
    case GST_EVENT_FLUSH_STOP:
      gst_adaptive_demux_reset (demux);
      break;
    case GST_EVENT_EOS:{
      GstQuery *query;
      gboolean query_res;
      gboolean ret = TRUE;
      gsize available;
      GstBuffer *manifest_buffer;

      demux_class = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
      available = gst_adapter_available (demux->priv->input_adapter);

      if (available == 0) {
        GST_WARNING_OBJECT (demux, "Received EOS without a manifest.");
        break;
      }

      GST_DEBUG_OBJECT (demux, "Got EOS on the sink pad: manifest fetched");

      /* Need to get the URI to use it as a base to generate the fragment's
       * uris */
      query = gst_query_new_uri ();
      query_res = gst_pad_peer_query (pad, query);
      GST_MANIFEST_LOCK (demux);
      if (query_res) {
        gchar *uri, *redirect_uri;
        gboolean permanent;

        gst_query_parse_uri (query, &uri);
        gst_query_parse_uri_redirection (query, &redirect_uri);
        gst_query_parse_uri_redirection_permanent (query, &permanent);

        if (permanent && redirect_uri) {
          demux->manifest_uri = redirect_uri;
          demux->manifest_base_uri = NULL;
          g_free (uri);
        } else {
          demux->manifest_uri = uri;
          demux->manifest_base_uri = redirect_uri;
        }

        GST_DEBUG_OBJECT (demux, "Fetched manifest at URI: %s (base: %s)",
            demux->manifest_uri, GST_STR_NULL (demux->manifest_base_uri));
      } else {
        GST_WARNING_OBJECT (demux, "Upstream URI query failed.");
      }
      gst_query_unref (query);

      /* Let the subclass parse the manifest */
      manifest_buffer =
          gst_adapter_take_buffer (demux->priv->input_adapter, available);
      if (!demux_class->process_manifest (demux, manifest_buffer)) {
        /* In most cases, this will happen if we set a wrong url in the
         * source element and we have received the 404 HTML response instead of
         * the manifest */
        GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid manifest."),
            (NULL));
        ret = FALSE;
      } else {
        demux->priv->have_manifest = TRUE;
      }
      gst_buffer_unref (manifest_buffer);

      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_element (GST_OBJECT_CAST (demux),
              gst_structure_new (STATISTICS_MESSAGE_NAME,
                  "manifest-uri", G_TYPE_STRING,
                  demux->manifest_uri, "uri", G_TYPE_STRING,
                  demux->manifest_uri,
                  "manifest-download-start", GST_TYPE_CLOCK_TIME,
                  GST_CLOCK_TIME_NONE,
                  "manifest-download-stop", GST_TYPE_CLOCK_TIME,
                  gst_util_get_timestamp (), NULL)));

      if (ret) {
        /* Send duration message */
        if (!gst_adaptive_demux_is_live (demux)) {
          GstClockTime duration = demux_class->get_duration (demux);

          if (duration != GST_CLOCK_TIME_NONE) {
            GST_DEBUG_OBJECT (demux,
                "Sending duration message : %" GST_TIME_FORMAT,
                GST_TIME_ARGS (duration));
            gst_element_post_message (GST_ELEMENT (demux),
                gst_message_new_duration_changed (GST_OBJECT (demux)));
          } else {
            GST_DEBUG_OBJECT (demux,
                "media duration unknown, can not send the duration message");
          }
        }

        if (demux->next_streams) {
          gst_adaptive_demux_expose_streams (demux, TRUE);
          gst_adaptive_demux_start_tasks (demux);
          if (gst_adaptive_demux_is_live (demux)) {
            /* Task to periodically update the manifest */
            gst_task_start (demux->priv->updates_task);
          }
        } else {
          /* no streams */
          GST_WARNING_OBJECT (demux, "No streams created from manifest");
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
              (_("This file contains no playable streams.")),
              ("No known stream formats found at the Manifest"));
          ret = FALSE;
        }

      }
      GST_MANIFEST_UNLOCK (demux);

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

static GstFlowReturn
gst_adaptive_demux_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (parent);
  gst_adapter_push (demux->priv->input_adapter, buffer);

  GST_INFO_OBJECT (demux, "Received manifest buffer, total size is %i bytes",
      (gint) gst_adapter_available (demux->priv->input_adapter));

  return GST_FLOW_OK;
}

static void
gst_adaptive_demux_reset (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GList *iter;

  gst_adaptive_demux_stop_tasks (demux);
  gst_uri_downloader_reset (demux->downloader);

  if (klass->reset)
    klass->reset (demux);

  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;
    if (stream->pad)
      gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->pad);
    gst_adaptive_demux_stream_free (stream);
  }
  g_list_free (demux->streams);
  demux->streams = NULL;

  if (demux->priv->old_streams) {
    g_list_free_full (demux->priv->old_streams,
        (GDestroyNotify) gst_adaptive_demux_stream_free);
    demux->priv->old_streams = NULL;
  }

  g_free (demux->manifest_uri);
  g_free (demux->manifest_base_uri);
  demux->manifest_uri = NULL;
  demux->manifest_base_uri = NULL;

  gst_adapter_clear (demux->priv->input_adapter);
  demux->priv->have_manifest = FALSE;

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;
  demux->priv->exposing = FALSE;
}

static void
gst_adaptive_demux_handle_message (GstBin * bin, GstMessage * msg)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (bin);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GList *iter;
      GstAdaptiveDemuxStream *stream;
      GError *err = NULL;
      gchar *debug = NULL;
      gchar *new_error = NULL;

      for (iter = demux->streams; iter; iter = g_list_next (iter)) {
        stream = iter->data;
        if (GST_OBJECT_CAST (stream->src) == GST_MESSAGE_SRC (msg)) {
          gst_message_parse_error (msg, &err, &debug);

          GST_WARNING_OBJECT (GST_ADAPTIVE_DEMUX_STREAM_PAD (stream),
              "Source posted error: %d:%d %s (%s)", err->domain, err->code,
              err->message, debug);

          if (debug)
            new_error = g_strdup_printf ("%s: %s\n", err->message, debug);
          if (new_error) {
            g_free (err->message);
            err->message = new_error;
          }

          /* error, but ask to retry */
          gst_adaptive_demux_stream_fragment_download_finish (stream,
              GST_FLOW_CUSTOM_ERROR, err);

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

void
gst_adaptive_demux_set_stream_struct_size (GstAdaptiveDemux * demux,
    gsize struct_size)
{
  demux->stream_struct_size = struct_size;
}

static gboolean
gst_adaptive_demux_expose_stream (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstPad *pad = stream->pad;
  gchar *name = gst_pad_get_name (pad);
  GstEvent *event;
  gchar *stream_id;

  gst_pad_set_active (pad, TRUE);
  stream->need_header = TRUE;

  stream_id = gst_pad_create_stream_id (pad, GST_ELEMENT_CAST (demux), name);

  event =
      gst_pad_get_sticky_event (GST_ADAPTIVE_DEMUX_SINK_PAD (demux),
      GST_EVENT_STREAM_START, 0);
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

  gst_pad_push_event (pad, event);
  g_free (stream_id);
  g_free (name);

  GST_DEBUG_OBJECT (demux, "Adding srcpad %s:%s with caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), stream->pending_caps);

  if (stream->pending_caps) {
    gst_pad_set_caps (pad, stream->pending_caps);
    gst_caps_unref (stream->pending_caps);
    stream->pending_caps = NULL;
  }

  stream->download_finished = FALSE;
  stream->discont = FALSE;

  gst_object_ref (pad);

  return gst_element_add_pad (GST_ELEMENT_CAST (demux), pad);
}

static GstClockTime
gst_adaptive_demux_stream_get_presentation_offset (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemuxClass *klass;

  klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->get_presentation_offset == NULL)
    return 0;

  return klass->get_presentation_offset (demux, stream);
}

static gboolean
gst_adaptive_demux_expose_streams (GstAdaptiveDemux * demux,
    gboolean first_segment)
{
  GList *iter;
  GList *old_streams;
  GstClockTime min_pts = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (demux->next_streams != NULL, FALSE);

  demux->priv->exposing = TRUE;
  old_streams = demux->streams;
  demux->streams = demux->next_streams;
  demux->next_streams = NULL;

  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;

    if (!gst_adaptive_demux_expose_stream (demux,
            GST_ADAPTIVE_DEMUX_STREAM_CAST (stream))) {
      /* TODO act on error */
    }

    if (first_segment) {
      /* TODO we only need the first timestamp, maybe create a simple function */
      gst_adaptive_demux_stream_update_fragment_info (demux, stream);

      if (GST_CLOCK_TIME_IS_VALID (min_pts)) {
        min_pts = MIN (min_pts, stream->fragment.timestamp);
      } else {
        min_pts = stream->fragment.timestamp;
      }
    }
  }

  if (first_segment)
    demux->segment.start = demux->segment.position = demux->segment.time =
        min_pts;
  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;
    GstClockTime offset;

    offset = gst_adaptive_demux_stream_get_presentation_offset (demux, stream);
    stream->segment = demux->segment;
    stream->segment.start = stream->segment.position =
        stream->fragment.timestamp + offset;
    stream->pending_segment = gst_event_new_segment (&stream->segment);
  }

  gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
  demux->priv->exposing = FALSE;

  if (old_streams) {
    GstEvent *eos = gst_event_new_eos ();

    for (iter = old_streams; iter; iter = g_list_next (iter)) {
      GstAdaptiveDemuxStream *stream = iter->data;

      GST_LOG_OBJECT (stream->pad, "Removing stream");
      gst_pad_push_event (stream->pad, gst_event_ref (eos));
      gst_pad_set_active (stream->pad, FALSE);
      gst_element_remove_pad (GST_ELEMENT (demux), stream->pad);
    }
    gst_event_unref (eos);

    /* The list should be freed from another thread as we can't properly
     * cleanup a GstTask from itself */
    GST_OBJECT_LOCK (demux);
    demux->priv->old_streams =
        g_list_concat (demux->priv->old_streams, old_streams);
    GST_OBJECT_UNLOCK (demux);
  }

  return TRUE;
}

GstAdaptiveDemuxStream *
gst_adaptive_demux_stream_new (GstAdaptiveDemux * demux, GstPad * pad)
{
  GstAdaptiveDemuxStream *stream;

  stream = g_malloc0 (demux->stream_struct_size);

  /* Downloading task */
  g_rec_mutex_init (&stream->download_lock);
  stream->download_task =
      gst_task_new ((GstTaskFunction) gst_adaptive_demux_stream_download_loop,
      stream, NULL);
  gst_task_set_lock (stream->download_task, &stream->download_lock);

  stream->pad = pad;
  stream->demux = demux;
  stream->fragment_bitrates =
      g_malloc0 (sizeof (guint64) * demux->num_lookback_fragments);
  gst_pad_set_element_private (pad, stream);

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_src_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_src_event));

  gst_segment_init (&stream->segment, GST_FORMAT_TIME);
  g_cond_init (&stream->fragment_download_cond);
  g_mutex_init (&stream->fragment_download_lock);
  stream->adapter = gst_adapter_new ();

  demux->next_streams = g_list_append (demux->next_streams, stream);

  return stream;
}

static void
gst_adaptive_demux_stream_free (GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->stream_free)
    klass->stream_free (stream);

  g_clear_error (&stream->last_error);
  if (stream->download_task) {
    if (GST_TASK_STATE (stream->download_task) != GST_TASK_STOPPED) {
      GST_DEBUG_OBJECT (demux, "Leaving streaming task %s:%s",
          GST_DEBUG_PAD_NAME (stream->pad));

      g_cond_signal (&stream->fragment_download_cond);
      gst_task_stop (stream->download_task);
    }
    GST_LOG_OBJECT (demux, "Waiting for task to finish");
    gst_task_join (stream->download_task);
    GST_LOG_OBJECT (demux, "Finished");
    gst_object_unref (stream->download_task);
    g_rec_mutex_clear (&stream->download_lock);
    stream->download_task = NULL;
  }

  gst_adaptive_demux_stream_fragment_clear (&stream->fragment);

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
    gst_bin_remove (GST_BIN_CAST (demux), stream->src);
    stream->src = NULL;
  }

  g_cond_clear (&stream->fragment_download_cond);
  g_mutex_clear (&stream->fragment_download_lock);

  g_free (stream->fragment_bitrates);

  if (stream->pad) {
    gst_object_unref (stream->pad);
    stream->pad = NULL;
  }
  if (stream->pending_caps)
    gst_caps_unref (stream->pending_caps);

  g_object_unref (stream->adapter);

  g_free (stream);
}

static gboolean
gst_adaptive_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * range_start, gint64 * range_stop)
{
  GstAdaptiveDemuxClass *klass;

  klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  g_return_val_if_fail (klass->get_live_seek_range, FALSE);

  return klass->get_live_seek_range (demux, range_start, range_stop);
}

static gboolean
gst_adaptive_demux_can_seek (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass;

  klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  if (gst_adaptive_demux_is_live (demux)) {
    return klass->get_live_seek_range != NULL;
  }

  return klass->seek != NULL;
}

static gboolean
gst_adaptive_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAdaptiveDemux *demux;
  GstAdaptiveDemuxClass *demux_class;

  demux = GST_ADAPTIVE_DEMUX_CAST (parent);
  demux_class = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  switch (event->type) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      guint32 seqnum;
      GList *iter;
      gboolean update;
      gboolean ret = TRUE;
      GstSegment oldsegment;

      GST_INFO_OBJECT (demux, "Received seek event");

      GST_MANIFEST_LOCK (demux);
      if (!gst_adaptive_demux_can_seek (demux)) {
        GST_MANIFEST_UNLOCK (demux);
        gst_event_unref (event);
        return FALSE;
      }
      GST_MANIFEST_UNLOCK (demux);

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        gst_event_unref (event);
        return FALSE;
      }

      if (gst_adaptive_demux_is_live (demux)) {
        gint64 range_start, range_stop;
        if (!gst_adaptive_demux_get_live_seek_range (demux, &range_start,
                &range_stop)) {
          gst_event_unref (event);
          return FALSE;
        }
        if (start < range_start || start >= range_stop) {
          GST_WARNING_OBJECT (demux, "Seek to invalid position");
          gst_event_unref (event);
          return FALSE;
        }
      }

      seqnum = gst_event_get_seqnum (event);

      GST_DEBUG_OBJECT (demux,
          "seek event, rate: %f type: %d start: %" GST_TIME_FORMAT " stop: %"
          GST_TIME_FORMAT, rate, start_type, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      /* have a backup in case seek fails */
      gst_segment_copy_into (&demux->segment, &oldsegment);

      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          start, stop_type, stop, &update);

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GstEvent *fevent;

        GST_DEBUG_OBJECT (demux, "sending flush start");
        fevent = gst_event_new_flush_start ();
        gst_event_set_seqnum (fevent, seqnum);
        gst_adaptive_demux_push_src_event (demux, fevent);
      }
      gst_adaptive_demux_stop_tasks (demux);
      GST_DEBUG_OBJECT (demux, "Seeking to segment %" GST_SEGMENT_FORMAT,
          &demux->segment);

      GST_MANIFEST_LOCK (demux);
      ret = demux_class->seek (demux, event);

      if (ret) {
        GstEvent *seg_evt;

        seg_evt = gst_event_new_segment (&demux->segment);
        gst_event_set_seqnum (seg_evt, seqnum);
        for (iter = demux->streams; iter; iter = g_list_next (iter)) {
          GstAdaptiveDemuxStream *stream = iter->data;

          gst_event_replace (&stream->pending_segment, seg_evt);
        }
        gst_event_unref (seg_evt);
      } else {
        /* Is there anything else we can do if it fails? */
        gst_segment_copy_into (&oldsegment, &demux->segment);
      }

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GstEvent *fevent;

        GST_DEBUG_OBJECT (demux, "Sending flush stop on all pad");
        fevent = gst_event_new_flush_stop (TRUE);
        gst_event_set_seqnum (fevent, seqnum);
        gst_adaptive_demux_push_src_event (demux, fevent);
      }
      if (demux->next_streams) {
        gst_adaptive_demux_expose_streams (demux, TRUE);
      }

      /* Restart the demux */
      gst_adaptive_demux_start_tasks (demux);
      GST_MANIFEST_UNLOCK (demux);

      gst_event_unref (event);
      return ret;
    }
    case GST_EVENT_RECONFIGURE:{
      GList *iter;

      /* When exposing pads reconfigure events are received as result
       * of the linking of the pads. The exposing and reconfigure happens
       * from the same thread. This prevents a deadlock and, although
       * not beautiful, makes this safe by avoiding that the demux->streams
       * list gets modified while this loop is running */
      if (!demux->priv->exposing)
        GST_MANIFEST_LOCK (demux);

      for (iter = demux->streams; iter; iter = g_list_next (iter)) {
        GstAdaptiveDemuxStream *stream = iter->data;

        if (stream->pad == pad) {
          g_mutex_lock (&stream->fragment_download_lock);
          if (stream->last_ret == GST_FLOW_NOT_LINKED) {
            stream->last_ret = GST_FLOW_OK;
            stream->restart_download = TRUE;
            stream->need_header = TRUE;
            stream->discont = TRUE;
            GST_DEBUG_OBJECT (stream->pad, "Restarting download loop");
            gst_task_start (stream->download_task);
          }
          g_mutex_unlock (&stream->fragment_download_lock);
          gst_event_unref (event);
          if (!demux->priv->exposing)
            GST_MANIFEST_UNLOCK (demux);
          return TRUE;
        }
      }
      if (!demux->priv->exposing)
        GST_MANIFEST_UNLOCK (demux);
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
}

static gboolean
gst_adaptive_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstAdaptiveDemux *demux;
  GstAdaptiveDemuxClass *demux_class;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  demux = GST_ADAPTIVE_DEMUX_CAST (parent);
  demux_class = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  switch (query->type) {
    case GST_QUERY_DURATION:{
      GstClockTime duration = -1;
      GstFormat fmt;

      GST_MANIFEST_LOCK (demux);
      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME && demux->priv->have_manifest
          && !gst_adaptive_demux_is_live (demux)) {
        duration = demux_class->get_duration (demux);

        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          ret = TRUE;
        }
      }
      GST_MANIFEST_UNLOCK (demux);
      GST_DEBUG_OBJECT (demux, "GST_QUERY_DURATION returns %s with duration %"
          GST_TIME_FORMAT, ret ? "TRUE" : "FALSE", GST_TIME_ARGS (duration));
      break;
    }
    case GST_QUERY_LATENCY:{
      gst_query_set_latency (query, FALSE, 0, -1);
      ret = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint64 stop = -1;
      gint64 start = 0;

      GST_MANIFEST_LOCK (demux);
      if (!demux->priv->have_manifest) {
        GST_MANIFEST_UNLOCK (demux);
        GST_INFO_OBJECT (demux,
            "Don't have manifest yet, can't answer seeking query");
        return FALSE;           /* can't answer without manifest */
      }

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      GST_INFO_OBJECT (demux, "Received GST_QUERY_SEEKING with format %d", fmt);
      if (fmt == GST_FORMAT_TIME) {
        GstClockTime duration;
        gboolean can_seek = gst_adaptive_demux_can_seek (demux);

        ret = TRUE;
        if (can_seek) {
          if (gst_adaptive_demux_is_live (demux)) {
            ret = gst_adaptive_demux_get_live_seek_range (demux, &start, &stop);
          } else {
            duration = demux_class->get_duration (demux);
            if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
              stop = duration;
          }
        }
        gst_query_set_seeking (query, fmt, can_seek, start, stop);
        GST_INFO_OBJECT (demux, "GST_QUERY_SEEKING returning with start : %"
            GST_TIME_FORMAT ", stop : %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
      }
      GST_MANIFEST_UNLOCK (demux);
      break;
    }
    case GST_QUERY_URI:
      /* TODO HLS can answer this differently it seems */
      GST_MANIFEST_LOCK (demux);
      if (demux->manifest_uri) {
        /* FIXME: (hls) Do we answer with the variant playlist, with the current
         * playlist or the the uri of the last downlowaded fragment? */
        gst_query_set_uri (query, demux->manifest_uri);
        ret = TRUE;
      }
      GST_MANIFEST_UNLOCK (demux);
      break;
    default:
      /* Don't forward queries upstream because of the special nature of this
       *  "demuxer", which relies on the upstream element only to be fed
       *  the Manifest
       */
      break;
  }

  return ret;
}

static void
gst_adaptive_demux_start_tasks (GstAdaptiveDemux * demux)
{
  GList *iter;

  GST_INFO_OBJECT (demux, "Starting streams' tasks");
  demux->cancelled = FALSE;
  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;
    stream->last_ret = GST_FLOW_OK;
    gst_task_start (stream->download_task);
  }
}

static void
gst_adaptive_demux_stop_tasks (GstAdaptiveDemux * demux)
{
  GList *iter;

  demux->cancelled = TRUE;

  demux->priv->stop_updates_task = TRUE;
  gst_task_stop (demux->priv->updates_task);
  g_cond_signal (&demux->priv->updates_timed_cond);

  GST_MANIFEST_LOCK (demux);
  g_cond_broadcast (&demux->manifest_cond);
  GST_MANIFEST_UNLOCK (demux);

  gst_uri_downloader_cancel (demux->downloader);
  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;

    gst_task_stop (stream->download_task);
    if (stream->src)
      gst_element_set_state (stream->src, GST_STATE_READY);
    g_mutex_lock (&stream->fragment_download_lock);
    stream->download_finished = TRUE;
    g_cond_signal (&stream->fragment_download_cond);
    g_mutex_unlock (&stream->fragment_download_lock);
  }

  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;

    gst_task_join (stream->download_task);
    stream->download_error_count = 0;
    stream->need_header = TRUE;
    gst_adapter_clear (stream->adapter);
  }
  gst_task_join (demux->priv->updates_task);
}

static gboolean
gst_adaptive_demux_push_src_event (GstAdaptiveDemux * demux, GstEvent * event)
{
  GList *iter;
  gboolean ret = TRUE;

  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;
    gst_event_ref (event);
    ret = ret & gst_pad_push_event (stream->pad, event);
  }
  gst_event_unref (event);
  return ret;
}

void
gst_adaptive_demux_stream_set_caps (GstAdaptiveDemuxStream * stream,
    GstCaps * caps)
{
  GST_DEBUG_OBJECT (stream->pad, "setting new caps for stream %" GST_PTR_FORMAT,
      caps);
  gst_caps_replace (&stream->pending_caps, caps);
  gst_caps_unref (caps);
}

void
gst_adaptive_demux_stream_set_tags (GstAdaptiveDemuxStream * stream,
    GstTagList * tags)
{
  GST_DEBUG_OBJECT (stream->pad, "setting new tags for stream %" GST_PTR_FORMAT,
      tags);
  if (stream->pending_tags) {
    gst_tag_list_unref (stream->pending_tags);
  }
  stream->pending_tags = tags;
}

static guint64
_update_average_bitrate (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, guint64 new_bitrate)
{
  gint index = stream->moving_index % demux->num_lookback_fragments;

  stream->moving_bitrate -= stream->fragment_bitrates[index];
  stream->fragment_bitrates[index] = new_bitrate;
  stream->moving_bitrate += new_bitrate;

  stream->moving_index += 1;

  if (stream->moving_index > demux->num_lookback_fragments)
    return stream->moving_bitrate / demux->num_lookback_fragments;
  return stream->moving_bitrate / stream->moving_index;
}

static guint64
gst_adaptive_demux_stream_update_current_bitrate (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  guint64 average_bitrate;
  guint64 fragment_bitrate;

  fragment_bitrate =
      (stream->fragment_total_size * 8) /
      ((double) stream->fragment_total_time / G_GUINT64_CONSTANT (1000000));
  stream->fragment_total_size = 0;
  stream->fragment_total_time = 0;

  average_bitrate = _update_average_bitrate (demux, stream, fragment_bitrate);

  GST_INFO_OBJECT (stream, "last fragment bitrate was %" G_GUINT64_FORMAT,
      fragment_bitrate);
  GST_INFO_OBJECT (stream,
      "Last %u fragments average bitrate is %" G_GUINT64_FORMAT,
      demux->num_lookback_fragments, average_bitrate);

  /* Conservative approach, make sure we don't upgrade too fast */
  stream->current_download_rate = MIN (average_bitrate, fragment_bitrate);

  if (demux->connection_speed) {
    GST_LOG_OBJECT (demux, "Connection-speed is set to %u kbps, using it",
        demux->connection_speed / 1000);
    return demux->connection_speed;
  }

  stream->current_download_rate *= demux->bitrate_limit;
  GST_DEBUG_OBJECT (demux, "Bitrate after bitrate limit (%0.2f): %"
      G_GUINT64_FORMAT, demux->bitrate_limit, stream->current_download_rate);

  return stream->current_download_rate;
}

static GstFlowReturn
gst_adaptive_demux_combine_flows (GstAdaptiveDemux * demux)
{
  gboolean all_notlinked = TRUE;
  gboolean all_eos = TRUE;
  GList *iter;

  for (iter = demux->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemuxStream *stream = iter->data;

    g_mutex_lock (&stream->fragment_download_lock);
    if (stream->last_ret != GST_FLOW_NOT_LINKED) {
      all_notlinked = FALSE;
      if (stream->last_ret != GST_FLOW_EOS)
        all_eos = FALSE;
    }

    if (stream->last_ret <= GST_FLOW_NOT_NEGOTIATED
        || stream->last_ret == GST_FLOW_FLUSHING) {
      g_mutex_unlock (&stream->fragment_download_lock);
      return stream->last_ret;
    }
    g_mutex_unlock (&stream->fragment_download_lock);
  }
  if (all_notlinked)
    return GST_FLOW_NOT_LINKED;
  else if (all_eos)
    return GST_FLOW_EOS;
  return GST_FLOW_OK;
}

GstFlowReturn
gst_adaptive_demux_stream_push_buffer (GstAdaptiveDemuxStream * stream,
    GstBuffer * buffer)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean discont = FALSE;
  GstClockTime offset =
      gst_adaptive_demux_stream_get_presentation_offset (demux, stream);

  if (stream->first_fragment_buffer) {
    if (demux->segment.rate < 0)
      /* Set DISCONT flag for every first buffer in reverse playback mode
       * as each fragment for its own has to be reversed */
      discont = TRUE;

    GST_BUFFER_PTS (buffer) = stream->fragment.timestamp;
    if (GST_BUFFER_PTS_IS_VALID (buffer))
      stream->segment.position = GST_BUFFER_PTS (buffer);
  } else {
    GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  }

  if (stream->discont) {
    discont = TRUE;
    stream->discont = FALSE;
  }

  if (discont) {
    GST_DEBUG_OBJECT (stream->pad, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  stream->first_fragment_buffer = FALSE;

  if (GST_BUFFER_PTS (buffer) != GST_CLOCK_TIME_NONE)
    GST_BUFFER_PTS (buffer) += offset;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;

  if (G_UNLIKELY (stream->pending_caps)) {
    GST_DEBUG_OBJECT (stream->pad, "Setting pending caps: %" GST_PTR_FORMAT,
        stream->pending_caps);
    gst_pad_set_caps (stream->pad, stream->pending_caps);
    gst_caps_unref (stream->pending_caps);
    stream->pending_caps = NULL;
  }
  if (G_UNLIKELY (stream->pending_segment)) {
    GST_DEBUG_OBJECT (stream->pad, "Sending pending seg: %" GST_PTR_FORMAT,
        stream->pending_segment);
    gst_pad_push_event (stream->pad, stream->pending_segment);
    stream->pending_segment = NULL;
  }
  if (G_UNLIKELY (stream->pending_tags)) {
    GST_DEBUG_OBJECT (stream->pad, "Sending pending tags: %" GST_PTR_FORMAT,
        stream->pending_tags);
    gst_pad_push_event (stream->pad, gst_event_new_tag (stream->pending_tags));
    stream->pending_tags = NULL;
  }

  ret = gst_pad_push (stream->pad, buffer);
  GST_LOG_OBJECT (stream->pad, "Push result: %d %s", ret,
      gst_flow_get_name (ret));

  return ret;
}

static GstFlowReturn
gst_adaptive_demux_stream_finish_fragment_default (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  /* No need to advance, this isn't a real fragment */
  if (G_UNLIKELY (stream->downloading_header || stream->downloading_index))
    return GST_FLOW_OK;

  return gst_adaptive_demux_stream_advance_fragment (demux, stream,
      stream->fragment.duration);
}

static GstFlowReturn
gst_adaptive_demux_stream_data_received_default (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstBuffer *buffer;

  buffer = gst_adapter_take_buffer (stream->adapter,
      gst_adapter_available (stream->adapter));
  return gst_adaptive_demux_stream_push_buffer (stream, buffer);
}

static GstFlowReturn
_src_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPad *srcpad = (GstPad *) parent;
  GstAdaptiveDemuxStream *stream = gst_pad_get_element_private (srcpad);
  GstAdaptiveDemux *demux = stream->demux;
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstFlowReturn ret = GST_FLOW_OK;

  if (stream->starting_fragment) {
    stream->starting_fragment = FALSE;
    if (klass->start_fragment) {
      klass->start_fragment (demux, stream);
    }

    GST_BUFFER_PTS (buffer) = stream->fragment.timestamp;

    GST_LOG_OBJECT (stream->pad, "set fragment pts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));

    if (GST_BUFFER_PTS_IS_VALID (buffer))
      stream->segment.position = GST_BUFFER_PTS (buffer);

  } else {
    GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  }

  stream->download_total_time +=
      g_get_monotonic_time () - stream->download_chunk_start_time;
  stream->download_total_bytes += gst_buffer_get_size (buffer);

  stream->fragment_total_size += gst_buffer_get_size (buffer);
  stream->fragment_total_time +=
      g_get_monotonic_time () - stream->download_chunk_start_time;

  gst_adapter_push (stream->adapter, buffer);
  GST_DEBUG_OBJECT (stream->pad, "Received buffer of size %" G_GSIZE_FORMAT
      ". Now %" G_GSIZE_FORMAT " on adapter", gst_buffer_get_size (buffer),
      gst_adapter_available (stream->adapter));
  ret = klass->data_received (demux, stream);
  stream->download_chunk_start_time = g_get_monotonic_time ();

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

    gst_adaptive_demux_stream_fragment_download_finish (stream, ret, NULL);
    if (ret == (GstFlowReturn) GST_ADAPTIVE_DEMUX_FLOW_SWITCH)
      ret = GST_FLOW_EOS;       /* return EOS to make the source stop */
  }

  return ret;
}

static void
gst_adaptive_demux_stream_fragment_download_finish (GstAdaptiveDemuxStream *
    stream, GstFlowReturn ret, GError * err)
{
  g_mutex_lock (&stream->fragment_download_lock);
  stream->download_finished = TRUE;

  /* if we have an error, only replace last_ret if it was OK before to avoid
   * overwriting the first error we got */
  if (err) {
    if (stream->last_ret == GST_FLOW_OK) {
      stream->last_ret = ret;
      g_clear_error (&stream->last_error);
      stream->last_error = g_error_copy (err);
    }
  } else {
    stream->last_ret = ret;
  }
  g_cond_signal (&stream->fragment_download_cond);
  g_mutex_unlock (&stream->fragment_download_lock);
}

static gboolean
_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstPad *srcpad = GST_PAD_CAST (parent);
  GstAdaptiveDemuxStream *stream = gst_pad_get_element_private (srcpad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GstAdaptiveDemuxClass *klass;
      GstFlowReturn ret;

      klass = GST_ADAPTIVE_DEMUX_GET_CLASS (stream->demux);
      ret = klass->finish_fragment (stream->demux, stream);
      gst_adaptive_demux_stream_fragment_download_finish (stream, ret, NULL);
      break;
    }
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
gst_adaptive_demux_stream_wait_manifest_update (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  gboolean ret = TRUE;

  /* Wait until we're cancelled or there's something for
   * us to download in the playlist or the playlist
   * became non-live */
  while (TRUE) {
    if (demux->cancelled) {
      ret = FALSE;
      break;
    }

    /* Got a new fragment or not live anymore? */
    if (gst_adaptive_demux_stream_has_next_fragment (demux, stream)) {
      GST_DEBUG_OBJECT (demux, "new fragment available, "
          "not waiting for manifest update");
      ret = TRUE;
      break;
    }

    if (!gst_adaptive_demux_is_live (demux)) {
      GST_DEBUG_OBJECT (demux, "Not live anymore, "
          "not waiting for manifest update");
      ret = FALSE;
      break;
    }

    GST_DEBUG_OBJECT (demux, "No fragment left but live playlist, wait a bit");
    g_cond_wait (&demux->manifest_cond, GST_MANIFEST_GET_LOCK (demux));
  }
  GST_DEBUG_OBJECT (demux, "Retrying now");
  return ret;
}

static gboolean
_adaptive_demux_pad_remove_eos_sticky (GstPad * pad, GstEvent ** event,
    gpointer udata)
{
  if (GST_EVENT_TYPE (*event) == GST_EVENT_EOS) {
    gst_event_replace (event, NULL);
    return FALSE;
  }
  return TRUE;
}

static void
gst_adaptive_demux_stream_clear_eos_and_flush_state (GstAdaptiveDemuxStream *
    stream)
{
  GstPad *internal_pad;

  internal_pad =
      GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD (stream->pad)));
  gst_pad_sticky_events_foreach (internal_pad,
      _adaptive_demux_pad_remove_eos_sticky, NULL);
  GST_OBJECT_FLAG_UNSET (internal_pad, GST_PAD_FLAG_EOS);
  /* In case the stream is recovering from a flushing seek it is also needed
   * to remove the flushing state from this pad. The flushing state is set
   * because of the flow return propagating until the source element */
  GST_PAD_UNSET_FLUSHING (internal_pad);

  gst_object_unref (internal_pad);
}

static gboolean
gst_adaptive_demux_stream_update_source (GstAdaptiveDemuxStream * stream,
    const gchar * uri, const gchar * referer, gboolean refresh,
    gboolean allow_cache)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (!gst_uri_is_valid (uri)) {
    GST_WARNING_OBJECT (stream->pad, "Invalid URI: %s", uri);
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
        gst_object_unref (stream->src_srcpad);
        gst_element_set_state (stream->src, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (demux), stream->src);
        stream->src = NULL;
        stream->src_srcpad = NULL;
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
static GstFlowReturn
gst_adaptive_demux_stream_download_uri (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, const gchar * uri, gint64 start,
    gint64 end)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GST_DEBUG_OBJECT (stream->pad, "Downloading uri: %s, range:%" G_GINT64_FORMAT
      " - %" G_GINT64_FORMAT, uri, start, end);

  stream->download_finished = FALSE;

  if (!gst_adaptive_demux_stream_update_source (stream, uri, NULL, FALSE, TRUE)) {
    g_mutex_lock (&stream->fragment_download_lock);
    ret = stream->last_ret = GST_FLOW_ERROR;
    g_mutex_unlock (&stream->fragment_download_lock);
    return ret;
  }

  if (gst_element_set_state (stream->src,
          GST_STATE_READY) != GST_STATE_CHANGE_FAILURE) {
    if (start != 0 || end != -1) {
      if (!gst_element_send_event (stream->src, gst_event_new_seek (1.0,
                  GST_FORMAT_BYTES, (GstSeekFlags) GST_SEEK_FLAG_FLUSH,
                  GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_SET, end))) {

        /* looks like the source can't handle seeks in READY */
        g_clear_error (&stream->last_error);
        stream->last_error = g_error_new (GST_CORE_ERROR,
            GST_CORE_ERROR_NOT_IMPLEMENTED,
            "Source element can't handle range requests");
        stream->last_ret = GST_FLOW_ERROR;
      }
    }

    g_mutex_lock (&stream->fragment_download_lock);
    if (G_LIKELY (stream->last_ret == GST_FLOW_OK)) {
      stream->download_start_time = g_get_monotonic_time ();
      stream->download_chunk_start_time = g_get_monotonic_time ();
      g_mutex_unlock (&stream->fragment_download_lock);
      gst_element_sync_state_with_parent (stream->src);
      g_mutex_lock (&stream->fragment_download_lock);

      /* wait for the fragment to be completely downloaded */
      GST_DEBUG_OBJECT (stream->pad,
          "Waiting for fragment download to finish: %s", uri);
      while (!stream->demux->cancelled && !stream->download_finished) {
        g_cond_wait (&stream->fragment_download_cond,
            &stream->fragment_download_lock);
      }
      ret = stream->last_ret;

      GST_DEBUG_OBJECT (stream->pad, "Fragment download finished: %s", uri);
    }
    g_mutex_unlock (&stream->fragment_download_lock);
  } else {
    g_mutex_lock (&stream->fragment_download_lock);
    if (stream->last_ret == GST_FLOW_OK)
      stream->last_ret = GST_FLOW_CUSTOM_ERROR;
    ret = GST_FLOW_CUSTOM_ERROR;
    g_mutex_unlock (&stream->fragment_download_lock);
  }

  /* flush the proxypads so that the EOS state is reset */
  gst_pad_push_event (stream->src_srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (stream->src_srcpad, gst_event_new_flush_stop (TRUE));

  gst_element_set_state (stream->src, GST_STATE_READY);
  gst_adaptive_demux_stream_clear_eos_and_flush_state (stream);

  return ret;
}

static GstFlowReturn
gst_adaptive_demux_stream_download_header_fragment (GstAdaptiveDemuxStream *
    stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstFlowReturn ret = GST_FLOW_OK;

  if (stream->fragment.header_uri != NULL) {
    GST_DEBUG_OBJECT (demux, "Fetching header %s %" G_GINT64_FORMAT "-%"
        G_GINT64_FORMAT, stream->fragment.header_uri,
        stream->fragment.header_range_start, stream->fragment.header_range_end);

    stream->downloading_header = TRUE;
    ret = gst_adaptive_demux_stream_download_uri (demux, stream,
        stream->fragment.header_uri, stream->fragment.header_range_start,
        stream->fragment.header_range_end);
    stream->downloading_header = FALSE;
  }

  /* check if we have an index */
  if (!demux->cancelled && ret == GST_FLOW_OK) {        /* TODO check for other valid types */

    if (stream->fragment.index_uri != NULL) {
      GST_DEBUG_OBJECT (demux,
          "Fetching index %s %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT,
          stream->fragment.index_uri,
          stream->fragment.index_range_start, stream->fragment.index_range_end);
      stream->downloading_index = TRUE;
      ret = gst_adaptive_demux_stream_download_uri (demux, stream,
          stream->fragment.index_uri, stream->fragment.index_range_start,
          stream->fragment.index_range_end);
      stream->downloading_index = FALSE;
    }
  }

  return ret;
}

static GstFlowReturn
gst_adaptive_demux_stream_download_fragment (GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  gchar *url = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  g_mutex_lock (&stream->fragment_download_lock);
  stream->starting_fragment = TRUE;
  stream->last_ret = GST_FLOW_OK;
  stream->first_fragment_buffer = TRUE;
  g_mutex_unlock (&stream->fragment_download_lock);

  if (stream->fragment.uri == NULL && stream->fragment.header_uri == NULL &&
      stream->fragment.index_uri == NULL)
    goto no_url_error;

  if (stream->need_header) {
    ret = gst_adaptive_demux_stream_download_header_fragment (stream);
    stream->need_header = FALSE;
    if (ret != GST_FLOW_OK) {
      return ret;
    }
  }

  url = stream->fragment.uri;
  GST_DEBUG_OBJECT (stream->pad, "Got url '%s' for stream %p", url, stream);
  if (url) {
    ret =
        gst_adaptive_demux_stream_download_uri (demux, stream, url,
        stream->fragment.range_start, stream->fragment.range_end);
    GST_DEBUG_OBJECT (stream->pad, "Fragment download result: %d %s",
        stream->last_ret, gst_flow_get_name (stream->last_ret));
    if (ret != GST_FLOW_OK) {
      /* TODO check if we are truly stoping */
      if (ret != GST_FLOW_ERROR && gst_adaptive_demux_is_live (demux)) {
        /* looks like there is no way of knowing when a live stream has ended
         * Have to assume we are falling behind and cause a manifest reload */
        return GST_FLOW_EOS;
      }
    }
  }

  return ret;

no_url_error:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
        (_("Failed to get fragment URL.")),
        ("An error happened when getting fragment URL"));
    gst_task_stop (stream->download_task);
    return GST_FLOW_ERROR;
  }
}

static void
gst_adaptive_demux_stream_download_loop (GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  guint64 next_download = 0;
  GstFlowReturn ret;
  gboolean live;

  GST_LOG_OBJECT (stream->pad, "download loop start");

  /* Check if we're done with our segment */
  if (demux->segment.rate > 0) {
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop)
        && stream->segment.position >= demux->segment.stop) {
      ret = GST_FLOW_EOS;
      gst_task_stop (stream->download_task);
      goto end_of_manifest;
    }
  } else {
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.start)
        && stream->segment.position < demux->segment.start) {
      ret = GST_FLOW_EOS;
      gst_task_stop (stream->download_task);
      goto end_of_manifest;
    }
  }

  GST_OBJECT_LOCK (demux);
  if (demux->cancelled) {
    stream->last_ret = GST_FLOW_FLUSHING;
    goto cancelled;
  }

  /* Cleanup old streams if any */
  if (G_UNLIKELY (demux->priv->old_streams != NULL)) {
    GList *old_streams = demux->priv->old_streams;
    demux->priv->old_streams = NULL;
    GST_OBJECT_UNLOCK (demux);

    /* Need to unlock as it might post messages to the bus */
    GST_DEBUG_OBJECT (stream->pad, "Cleaning up old streams");
    g_list_free_full (old_streams,
        (GDestroyNotify) gst_adaptive_demux_stream_free);
    GST_DEBUG_OBJECT (stream->pad, "Cleaning up old streams (done)");
  } else {
    GST_OBJECT_UNLOCK (demux);
  }

  GST_MANIFEST_LOCK (demux);
  if (G_UNLIKELY (stream->restart_download)) {
    GstSegment segment;
    GstEvent *seg_event;
    GstClockTime cur, ts;
    gint64 pos;

    GST_DEBUG_OBJECT (stream->pad,
        "Activating stream due to reconfigure event");

    cur = ts = stream->segment.position;

    if (gst_pad_peer_query_position (stream->pad, GST_FORMAT_TIME, &pos)) {
      ts = (GstClockTime) pos;
      GST_DEBUG_OBJECT (demux, "Downstream position: %"
          GST_TIME_FORMAT, GST_TIME_ARGS (ts));
    } else {
      /* query other pads as some faulty element in the pad's branch might
       * reject position queries. This should be better than using the
       * demux segment position that can be much ahead */
      for (GList * iter = demux->streams; iter != NULL;
          iter = g_list_next (iter)) {
        GstAdaptiveDemuxStream *cur_stream =
            (GstAdaptiveDemuxStream *) iter->data;

        if (gst_pad_peer_query_position (cur_stream->pad, GST_FORMAT_TIME,
                &pos)) {
          ts = (GstClockTime) pos;
          GST_DEBUG_OBJECT (stream->pad, "Downstream position: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (ts));
          break;
        }
      }
    }

    /* we might have already pushed this data */
    ts = MAX (ts, cur);

    GST_DEBUG_OBJECT (stream->pad, "Restarting stream at "
        "position %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));
    gst_segment_copy_into (&demux->segment, &segment);

    if (GST_CLOCK_TIME_IS_VALID (ts)) {
      /* TODO check return */
      gst_adaptive_demux_stream_seek (demux, stream, ts);

      if (cur < ts) {
        segment.position = ts;
      }
    }
    seg_event = gst_event_new_segment (&segment);
    GST_DEBUG_OBJECT (stream->pad, "Sending restart segment: %"
        GST_PTR_FORMAT, seg_event);
    gst_pad_push_event (stream->pad, seg_event);

    stream->restart_download = FALSE;
  }
  GST_OBJECT_LOCK (demux);
  if (demux->cancelled) {
    stream->last_ret = GST_FLOW_FLUSHING;
    GST_MANIFEST_UNLOCK (demux);
    goto cancelled;
  }
  GST_OBJECT_UNLOCK (demux);

  live = gst_adaptive_demux_is_live (demux);

  ret = gst_adaptive_demux_stream_update_fragment_info (demux, stream);
  GST_DEBUG_OBJECT (stream->pad, "Fragment info update result: %d %s",
      ret, gst_flow_get_name (ret));
  if (ret == GST_FLOW_OK) {

    /* wait for live fragments to be available */
    if (live) {
      gint64 wait_time =
          gst_adaptive_demux_stream_get_fragment_waiting_time (demux, stream);
      if (wait_time > 0)
        gst_adaptive_demux_stream_download_wait (stream, wait_time);
    }

    GST_MANIFEST_UNLOCK (demux);

    GST_OBJECT_LOCK (demux);
    if (demux->cancelled) {
      stream->last_ret = GST_FLOW_FLUSHING;
      goto cancelled;
    }
    GST_OBJECT_UNLOCK (demux);

    stream->last_ret = GST_FLOW_OK;
    next_download = g_get_monotonic_time ();
    ret = gst_adaptive_demux_stream_download_fragment (stream);

    GST_OBJECT_LOCK (demux);
    if (demux->cancelled) {
      stream->last_ret = GST_FLOW_FLUSHING;
      goto cancelled;
    }
    GST_OBJECT_UNLOCK (demux);

    GST_MANIFEST_LOCK (demux);
  }

  switch (ret) {
    case GST_FLOW_OK:
      break;                    /* all is good, let's go */
    case GST_FLOW_EOS:
      GST_DEBUG_OBJECT (stream->pad, "EOS, checking to stop download loop");
      /* we push the EOS after releasing the object lock */
      if (gst_adaptive_demux_is_live (demux)) {
        if (gst_adaptive_demux_stream_wait_manifest_update (demux, stream)) {
          GST_MANIFEST_UNLOCK (demux);
          return;
        }
        gst_task_stop (stream->download_task);
      } else {
        gst_task_stop (stream->download_task);
        if (gst_adaptive_demux_combine_flows (demux) == GST_FLOW_EOS) {
          if (gst_adaptive_demux_has_next_period (demux)) {
            gst_adaptive_demux_advance_period (demux);
            ret = GST_FLOW_OK;
          }
        }
      }
      break;

    case GST_FLOW_NOT_LINKED:
      gst_task_stop (stream->download_task);
      if (gst_adaptive_demux_combine_flows (demux)
          == GST_FLOW_NOT_LINKED) {
        GST_ELEMENT_ERROR (demux, STREAM, FAILED,
            (_("Internal data stream error.")), ("stream stopped, reason %s",
                gst_flow_get_name (GST_FLOW_NOT_LINKED)));
      }
      break;

    case GST_FLOW_FLUSHING:{
      GList *iter;

      for (iter = demux->streams; iter; iter = g_list_next (iter)) {
        GstAdaptiveDemuxStream *other;

        other = iter->data;
        gst_task_stop (other->download_task);
      }
    }
      break;

    default:
      if (ret <= GST_FLOW_ERROR) {
        gboolean is_live = gst_adaptive_demux_is_live (demux);
        GST_WARNING_OBJECT (demux, "Error while downloading fragment");
        if (++stream->download_error_count > MAX_DOWNLOAD_ERROR_COUNT) {
          GST_MANIFEST_UNLOCK (demux);
          goto download_error;
        }

        g_clear_error (&stream->last_error);

        /* First try to update the playlist for non-live playlists
         * in case the URIs have changed in the meantime. But only
         * try it the first time, after that we're going to wait a
         * a bit to not flood the server */
        if (stream->download_error_count == 1 && !is_live) {
          /* TODO hlsdemux had more options to this function (boolean and err) */
          GST_MANIFEST_UNLOCK (demux);
          if (gst_adaptive_demux_update_manifest (demux) == GST_FLOW_OK) {
            /* Retry immediately, the playlist actually has changed */
            GST_DEBUG_OBJECT (demux, "Updated the playlist");
            return;
          }
          GST_MANIFEST_LOCK (demux);
        }

        /* Wait half the fragment duration before retrying */
        next_download +=
            gst_util_uint64_scale
            (stream->fragment.duration, G_USEC_PER_SEC, 2 * GST_SECOND);

        GST_MANIFEST_UNLOCK (demux);
        g_mutex_lock (&stream->fragment_download_lock);
        if (demux->cancelled) {
          g_mutex_unlock (&stream->fragment_download_lock);
          return;
        }
        g_cond_wait_until (&stream->fragment_download_cond,
            &stream->fragment_download_lock, next_download);
        g_mutex_unlock (&stream->fragment_download_lock);
        GST_DEBUG_OBJECT (demux, "Retrying now");

        /* Refetch the playlist now after we waited */
        if (!is_live
            && gst_adaptive_demux_update_manifest (demux) == GST_FLOW_OK) {
          GST_DEBUG_OBJECT (demux, "Updated the playlist");
        }
        return;
      }
      break;
  }

  GST_MANIFEST_UNLOCK (demux);
end_of_manifest:
  if (G_UNLIKELY (ret == GST_FLOW_EOS)) {
    gst_adaptive_demux_stream_push_event (stream, gst_event_new_eos ());
  }

end:
  GST_LOG_OBJECT (stream->pad, "download loop end");
  return;

cancelled:
  {
    GST_DEBUG_OBJECT (stream->pad, "Stream has been cancelled");
    GST_OBJECT_UNLOCK (demux);
    goto end;
  }
download_error:
  {
    GstMessage *msg;

    if (stream->last_error) {
      gchar *debug = g_strdup_printf ("Error on stream %s:%s",
          GST_DEBUG_PAD_NAME (stream->pad));
      msg =
          gst_message_new_error (GST_OBJECT_CAST (demux), stream->last_error,
          debug);
      GST_ERROR_OBJECT (stream->pad, "Download error: %s",
          stream->last_error->message);
      g_free (debug);
    } else {
      GError *err =
          g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
          _("Couldn't download fragments"));
      msg =
          gst_message_new_error (GST_OBJECT_CAST (demux), err,
          "Fragment downloading has failed consecutive times");
      g_error_free (err);
      GST_ERROR_OBJECT (stream->pad,
          "Download error: Couldn't download fragments, too many failures");
    }

    gst_element_post_message (GST_ELEMENT_CAST (demux), msg);
    goto end;
  }
}

static void
gst_adaptive_demux_updates_loop (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  /* Loop for updating of the playlist. This periodically checks if
   * the playlist is updated and does so, then signals the streaming
   * thread in case it can continue downloading now. */

  /* block until the next scheduled update or the signal to quit this thread */
  GST_DEBUG_OBJECT (demux, "Started updates task");

  demux->priv->next_update =
      g_get_monotonic_time () + klass->get_manifest_update_interval (demux);

  /* Updating playlist only needed for live playlists */
  while (gst_adaptive_demux_is_live (demux)) {
    GstFlowReturn ret = GST_FLOW_OK;

    /* Wait here until we should do the next update or we're cancelled */
    GST_DEBUG_OBJECT (demux, "Wait for next playlist update");

    g_mutex_lock (&demux->priv->updates_timed_lock);
    if (demux->priv->stop_updates_task) {
      g_mutex_unlock (&demux->priv->updates_timed_lock);
      goto quit;
    }
    g_cond_wait_until (&demux->priv->updates_timed_cond,
        &demux->priv->updates_timed_lock, demux->priv->next_update);
    if (demux->priv->stop_updates_task) {
      g_mutex_unlock (&demux->priv->updates_timed_lock);
      goto quit;
    }
    g_mutex_unlock (&demux->priv->updates_timed_lock);

    GST_DEBUG_OBJECT (demux, "Updating playlist");
    ret = gst_adaptive_demux_update_manifest (demux);
    if (ret == GST_FLOW_EOS) {
    } else if (ret != GST_FLOW_OK) {
      if (demux->priv->stop_updates_task)
        goto quit;
      demux->priv->update_failed_count++;
      if (demux->priv->update_failed_count <= DEFAULT_FAILED_COUNT) {
        GST_WARNING_OBJECT (demux, "Could not update the playlist");
        demux->priv->next_update =
            g_get_monotonic_time () +
            klass->get_manifest_update_interval (demux);
      } else {
        GST_ELEMENT_ERROR (demux, STREAM, FAILED,
            (_("Internal data stream error.")), ("Could not update playlist"));
        goto error;
      }
    } else {
      GST_DEBUG_OBJECT (demux, "Updated playlist successfully");
      GST_MANIFEST_LOCK (demux);
      demux->priv->next_update =
          g_get_monotonic_time () + klass->get_manifest_update_interval (demux);
      /* Wake up download task */
      g_cond_broadcast (&demux->manifest_cond);
      GST_MANIFEST_UNLOCK (demux);
    }
  }

quit:
  {
    GST_DEBUG_OBJECT (demux, "Stopped updates task");
    gst_task_stop (demux->priv->updates_task);
    return;
  }

error:
  {
    GST_DEBUG_OBJECT (demux, "Stopped updates task because of error");
    gst_task_stop (demux->priv->updates_task);
  }
}


static gboolean
gst_adaptive_demux_stream_push_event (GstAdaptiveDemuxStream * stream,
    GstEvent * event)
{
  gboolean ret;

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    stream->eos = TRUE;
  }
  GST_DEBUG_OBJECT (GST_ADAPTIVE_DEMUX_STREAM_PAD (stream),
      "Pushing event %" GST_PTR_FORMAT, event);
  ret = gst_pad_push_event (GST_ADAPTIVE_DEMUX_STREAM_PAD (stream), event);
  return ret;
}

static gboolean
gst_adaptive_demux_is_live (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->is_live)
    return klass->is_live (demux);
  return FALSE;
}

static GstFlowReturn
gst_adaptive_demux_stream_seek (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstClockTime ts)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->stream_seek)
    return klass->stream_seek (stream, ts);
  return GST_FLOW_ERROR;
}

static gboolean
gst_adaptive_demux_stream_has_next_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  gboolean ret = TRUE;

  if (klass->stream_has_next_fragment)
    ret = klass->stream_has_next_fragment (stream);

  return ret;
}

GstFlowReturn
gst_adaptive_demux_stream_advance_fragment (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstClockTime duration)
{
  GstFlowReturn ret;

  GST_MANIFEST_LOCK (demux);
  if (stream->last_ret == GST_FLOW_OK) {
    stream->last_ret =
        gst_adaptive_demux_stream_advance_fragment_unlocked (demux, stream,
        duration);
  }
  ret = stream->last_ret;
  GST_MANIFEST_UNLOCK (demux);

  return ret;
}

GstFlowReturn
gst_adaptive_demux_stream_advance_fragment_unlocked (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream, GstClockTime duration)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstFlowReturn ret;

  g_return_val_if_fail (klass->stream_advance_fragment != NULL, GST_FLOW_ERROR);

  stream->download_error_count = 0;
  g_clear_error (&stream->last_error);
  stream->download_total_time +=
      g_get_monotonic_time () - stream->download_chunk_start_time;
  stream->fragment_total_time +=
      g_get_monotonic_time () - stream->download_chunk_start_time;

  /* FIXME - url has no indication of byte ranges for subsegments */
  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux),
          gst_structure_new (STATISTICS_MESSAGE_NAME,
              "manifest-uri", G_TYPE_STRING,
              demux->manifest_uri, "uri", G_TYPE_STRING,
              stream->fragment.uri, "fragment-start-time",
              GST_TYPE_CLOCK_TIME, stream->download_start_time,
              "fragment-stop-time", GST_TYPE_CLOCK_TIME,
              gst_util_get_timestamp (), "fragment-size", G_TYPE_UINT64,
              stream->download_total_bytes, "fragment-download-time",
              GST_TYPE_CLOCK_TIME,
              stream->download_total_time * GST_USECOND, NULL)));

  if (GST_CLOCK_TIME_IS_VALID (duration))
    stream->segment.position += duration;

  if (gst_adaptive_demux_is_live (demux)
      || gst_adaptive_demux_stream_has_next_fragment (demux, stream)) {
    ret = klass->stream_advance_fragment (stream);
  } else {
    ret = GST_FLOW_EOS;
  }

  stream->download_start_time = stream->download_chunk_start_time =
      g_get_monotonic_time ();

  if (ret == GST_FLOW_OK) {
    if (gst_adaptive_demux_stream_select_bitrate (demux, stream,
            gst_adaptive_demux_stream_update_current_bitrate (demux, stream))) {
      stream->need_header = TRUE;
      gst_adapter_clear (stream->adapter);
      ret = (GstFlowReturn) GST_ADAPTIVE_DEMUX_FLOW_SWITCH;
    }

    /* the subclass might want to switch pads */
    if (G_UNLIKELY (demux->next_streams)) {
      gst_task_stop (stream->download_task);
      /* TODO only allow switching streams if other downloads are not ongoing */
      GST_DEBUG_OBJECT (demux, "Subclass wants new pads "
          "to do bitrate switching");
      gst_adaptive_demux_expose_streams (demux, FALSE);
      gst_adaptive_demux_start_tasks (demux);
      ret = GST_FLOW_EOS;
    }
  }

  return ret;
}

static gboolean
gst_adaptive_demux_stream_select_bitrate (GstAdaptiveDemux *
    demux, GstAdaptiveDemuxStream * stream, guint64 bitrate)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  /* FIXME: Currently several issues have be found when letting bitrate adaptation
   * happen using trick modes (such as 'All streams finished without buffers') and
   * the adaptive algorithm does not properly behave. */
  if (demux->segment.rate != 1.0)
    return FALSE;

  if (klass->stream_select_bitrate)
    return klass->stream_select_bitrate (stream, bitrate);
  return FALSE;
}

static GstFlowReturn
gst_adaptive_demux_stream_update_fragment_info (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  g_return_val_if_fail (klass->stream_update_fragment_info != NULL,
      GST_FLOW_ERROR);

  return klass->stream_update_fragment_info (stream);
}

static gint64
gst_adaptive_demux_stream_get_fragment_waiting_time (GstAdaptiveDemux *
    demux, GstAdaptiveDemuxStream * stream)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->stream_get_fragment_waiting_time)
    return klass->stream_get_fragment_waiting_time (stream);
  return 0;
}

static GstFlowReturn
gst_adaptive_demux_update_manifest_default (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstFragment *download;
  GstBuffer *buffer;
  GstFlowReturn ret;

  download = gst_uri_downloader_fetch_uri (demux->downloader,
      demux->manifest_uri, NULL, TRUE, TRUE, TRUE, NULL);
  if (download) {
    GST_MANIFEST_LOCK (demux);
    g_free (demux->manifest_uri);
    g_free (demux->manifest_base_uri);
    if (download->redirect_permanent && download->redirect_uri) {
      demux->manifest_uri = g_strdup (download->redirect_uri);
      demux->manifest_base_uri = NULL;
    } else {
      demux->manifest_uri = g_strdup (download->uri);
      demux->manifest_base_uri = g_strdup (download->redirect_uri);
    }

    buffer = gst_fragment_get_buffer (download);
    g_object_unref (download);
    ret = klass->update_manifest_data (demux, buffer);
    gst_buffer_unref (buffer);
    GST_MANIFEST_UNLOCK (demux);
    /* FIXME: Should the manifest uri vars be reverted to original
     * values if updating fails? */
  } else {
    ret = GST_FLOW_NOT_LINKED;
  }

  return ret;
}

static GstFlowReturn
gst_adaptive_demux_update_manifest (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstFlowReturn ret;

  ret = klass->update_manifest (demux);

  if (ret == GST_FLOW_OK) {
    GstClockTime duration;
    GST_MANIFEST_LOCK (demux);
    /* Send an updated duration message */
    duration = klass->get_duration (demux);
    GST_MANIFEST_UNLOCK (demux);
    if (duration != GST_CLOCK_TIME_NONE) {
      GST_DEBUG_OBJECT (demux,
          "Sending duration message : %" GST_TIME_FORMAT,
          GST_TIME_ARGS (duration));
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_duration_changed (GST_OBJECT (demux)));
    } else {
      GST_DEBUG_OBJECT (demux,
          "Duration unknown, can not send the duration message");
    }
  }

  return ret;
}

void
gst_adaptive_demux_stream_fragment_clear (GstAdaptiveDemuxStreamFragment * f)
{
  g_free (f->uri);
  f->uri = NULL;
  f->range_start = 0;
  f->range_end = -1;

  g_free (f->header_uri);
  f->header_uri = NULL;
  f->header_range_start = 0;
  f->header_range_end = -1;

  g_free (f->index_uri);
  f->index_uri = NULL;
  f->index_range_start = 0;
  f->index_range_end = -1;
}

static gboolean
gst_adaptive_demux_has_next_period (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  gboolean ret = FALSE;

  if (klass->has_next_period)
    ret = klass->has_next_period (demux);
  GST_DEBUG_OBJECT (demux, "Has next period: %d", ret);
  return ret;
}

static void
gst_adaptive_demux_advance_period (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  g_return_if_fail (klass->advance_period != NULL);

  GST_DEBUG_OBJECT (demux, "Advancing to next period");
  klass->advance_period (demux);
  gst_adaptive_demux_expose_streams (demux, FALSE);
  gst_adaptive_demux_start_tasks (demux);
}

static void
gst_adaptive_demux_stream_download_wait (GstAdaptiveDemuxStream * stream,
    GstClockTime time_diff)
{
  gint64 end_time = g_get_monotonic_time () + time_diff / GST_USECOND;

  GST_DEBUG_OBJECT (stream->pad, "Download waiting for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time_diff));
  g_mutex_lock (&stream->fragment_download_lock);
  g_cond_wait_until (&stream->fragment_download_cond,
      &stream->fragment_download_lock, end_time);
  g_mutex_unlock (&stream->fragment_download_lock);
  GST_DEBUG_OBJECT (stream->pad, "Download finished waiting");
}
