/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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
 * SECTION:element-hlsdemux
 *
 * HTTP Live Streaming demuxer element.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch souphttpsrc location=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8 ! hlsdemux ! decodebin2 ! videoconvert ! videoscale ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include "gsthlsdemux.h"

#define GST_ELEMENT_ERROR_FROM_ERROR(el, msg, err) G_STMT_START { \
  gchar *__dbg = g_strdup_printf ("%s: %s", msg, err->message);         \
  GST_WARNING_OBJECT (el, "error: %s", __dbg);                          \
  gst_element_message_full (GST_ELEMENT(el), GST_MESSAGE_ERROR,         \
    err->domain, err->code,                                             \
    NULL, __dbg, __FILE__, GST_FUNCTION, __LINE__);                     \
  g_clear_error (&err); \
} G_STMT_END

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-hls"));

GST_DEBUG_CATEGORY_STATIC (gst_hls_demux_debug);
#define GST_CAT_DEFAULT gst_hls_demux_debug

enum
{
  PROP_0,

  PROP_FRAGMENTS_CACHE,
  PROP_BITRATE_LIMIT,
  PROP_CONNECTION_SPEED,
  PROP_LAST
};

#define DEFAULT_FRAGMENTS_CACHE 1
#define DEFAULT_FAILED_COUNT 3
#define DEFAULT_BITRATE_LIMIT 0.8
#define DEFAULT_CONNECTION_SPEED    0

/* GObject */
static void gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hls_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_hls_demux_dispose (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

static void gst_hls_demux_handle_message (GstBin * bin, GstMessage * msg);

/* GstHLSDemux */
static GstFlowReturn gst_hls_demux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_hls_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_hls_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_hls_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void gst_hls_demux_stream_loop (GstHLSDemux * demux);
static void gst_hls_demux_updates_loop (GstHLSDemux * demux);
static void gst_hls_demux_stop (GstHLSDemux * demux);
static void gst_hls_demux_pause_tasks (GstHLSDemux * demux);
static gboolean gst_hls_demux_switch_playlist (GstHLSDemux * demux);
static gboolean gst_hls_demux_get_next_fragment (GstHLSDemux * demux,
    gboolean * end_of_playlist, GError ** err);
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean update, GError ** err);
static void gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose);
static gboolean gst_hls_demux_set_location (GstHLSDemux * demux,
    const gchar * uri, const gchar * base_uri);
static gchar *gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf);

static gboolean gst_hls_demux_change_playlist (GstHLSDemux * demux,
    guint max_bitrate);
static GstBuffer *gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstBuffer * encrypted_buffer, GError ** err);
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data);
static void gst_hls_demux_decrypt_end (GstHLSDemux * demux);

#define gst_hls_demux_parent_class parent_class
G_DEFINE_TYPE (GstHLSDemux, gst_hls_demux, GST_TYPE_BIN);

static void
gst_hls_demux_dispose (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  if (demux->stream_task) {
    gst_object_unref (demux->stream_task);
    g_rec_mutex_clear (&demux->stream_lock);
    demux->stream_task = NULL;
  }

  if (demux->updates_task) {
    gst_object_unref (demux->updates_task);
    g_rec_mutex_clear (&demux->updates_lock);
    demux->updates_task = NULL;
  }

  if (demux->downloader != NULL) {
    g_object_unref (demux->downloader);
    demux->downloader = NULL;
  }

  gst_hls_demux_reset (demux, TRUE);

  if (demux->src_srcpad) {
    gst_object_unref (demux->src_srcpad);
    demux->src_srcpad = NULL;
  }

  g_mutex_clear (&demux->download_lock);
  g_cond_clear (&demux->download_cond);
  g_mutex_clear (&demux->updates_timed_lock);
  g_cond_clear (&demux->updates_timed_cond);
  g_mutex_clear (&demux->fragment_download_lock);
  g_cond_clear (&demux->fragment_download_cond);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_hls_demux_class_init (GstHLSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBinClass *bin_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  bin_class = (GstBinClass *) klass;

  gobject_class->set_property = gst_hls_demux_set_property;
  gobject_class->get_property = gst_hls_demux_get_property;
  gobject_class->dispose = gst_hls_demux_dispose;

  g_object_class_install_property (gobject_class, PROP_FRAGMENTS_CACHE,
      g_param_spec_uint ("fragments-cache", "Fragments cache",
          "Number of fragments needed to be cached to start playing "
          "(DEPRECATED: Has no effect since 1.3.1)",
          1, G_MAXUINT, DEFAULT_FRAGMENTS_CACHE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE_LIMIT,
      g_param_spec_float ("bitrate-limit",
          "Bitrate limit in %",
          "Limit of the available bitrate to use when switching to alternates.",
          0, 1, DEFAULT_BITRATE_LIMIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXUINT / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (element_class,
      "HLS Demuxer",
      "Codec/Demuxer/Adaptive",
      "HTTP Live Streaming demuxer",
      "Marc-Andre Lureau <marcandre.lureau@gmail.com>\n"
      "Andoni Morales Alastruey <ylatuya@gmail.com>");

  bin_class->handle_message = gst_hls_demux_handle_message;

  GST_DEBUG_CATEGORY_INIT (gst_hls_demux_debug, "hlsdemux", 0,
      "hlsdemux element");
}

static void
gst_hls_demux_init (GstHLSDemux * demux)
{
  /* sink pad */
  demux->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* Downloader */
  demux->downloader = gst_uri_downloader_new ();

  demux->do_typefind = TRUE;

  /* Properties */
  demux->bitrate_limit = DEFAULT_BITRATE_LIMIT;
  demux->connection_speed = DEFAULT_CONNECTION_SPEED;

  g_mutex_init (&demux->download_lock);
  g_cond_init (&demux->download_cond);
  g_mutex_init (&demux->updates_timed_lock);
  g_cond_init (&demux->updates_timed_cond);
  g_mutex_init (&demux->fragment_download_lock);
  g_cond_init (&demux->fragment_download_cond);

  /* Updates task */
  g_rec_mutex_init (&demux->updates_lock);
  demux->updates_task =
      gst_task_new ((GstTaskFunction) gst_hls_demux_updates_loop, demux, NULL);
  gst_task_set_lock (demux->updates_task, &demux->updates_lock);

  /* Streaming task */
  g_rec_mutex_init (&demux->stream_lock);
  demux->stream_task =
      gst_task_new ((GstTaskFunction) gst_hls_demux_stream_loop, demux, NULL);
  gst_task_set_lock (demux->stream_task, &demux->stream_lock);

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;
}

static void
gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      break;
    case PROP_BITRATE_LIMIT:
      demux->bitrate_limit = g_value_get_float (value);
      break;
    case PROP_CONNECTION_SPEED:
      demux->connection_speed = g_value_get_uint (value) * 1000;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hls_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      g_value_set_uint (value, 1);
      break;
    case PROP_BITRATE_LIMIT:
      g_value_set_float (value, demux->bitrate_limit);
      break;
    case PROP_CONNECTION_SPEED:
      g_value_set_uint (value, demux->connection_speed / 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstHLSDemux *demux = GST_HLS_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_hls_demux_reset (demux, FALSE);
      gst_uri_downloader_reset (demux->downloader);
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
      demux->adapter = gst_adapter_new ();
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_hls_demux_stop (demux);
      gst_task_join (demux->updates_task);
      gst_task_join (demux->stream_task);
      gst_hls_demux_reset (demux, FALSE);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_object_unref (demux->adapter);
      demux->adapter = NULL;
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_hls_demux_handle_message (GstBin * bin, GstMessage * msg)
{
  GstHLSDemux *demux = GST_HLS_DEMUX_CAST (bin);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;
      gchar *new_error = NULL;

      gst_message_parse_error (msg, &err, &debug);

      GST_WARNING_OBJECT (demux, "Source posted error: %d:%d %s (%s)",
          err->domain, err->code, err->message, debug);

      if (debug)
        new_error = g_strdup_printf ("%s: %s\n", err->message, debug);
      if (new_error) {
        g_free (err->message);
        err->message = new_error;
      }

      /* error, but ask to retry */
      g_mutex_lock (&demux->fragment_download_lock);
      demux->last_ret = GST_FLOW_CUSTOM_ERROR;
      g_clear_error (&demux->last_error);
      demux->last_error = g_error_copy (err);
      g_cond_signal (&demux->fragment_download_cond);
      g_mutex_unlock (&demux->fragment_download_lock);

      g_error_free (err);
      g_free (debug);
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

static gboolean
gst_hls_demux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstHLSDemux *demux;

  demux = GST_HLS_DEMUX (parent);

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
      gint64 current_sequence;
      GstM3U8MediaFile *file;

      GST_INFO_OBJECT (demux, "Received GST_EVENT_SEEK");

      if (gst_m3u8_client_is_live (demux->client)) {
        GST_WARNING_OBJECT (demux, "Received seek event for live stream");
        gst_event_unref (event);
        return FALSE;
      }

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        gst_event_unref (event);
        return FALSE;
      }

      if ((rate > 1.0 || rate < -1.0) && (!demux->client->main
              || !demux->client->main->iframe_lists)) {
        GST_ERROR_OBJECT (demux,
            "Trick modes only allowed for streams with I-frame lists");
        gst_event_unref (event);
        return FALSE;
      }

      GST_DEBUG_OBJECT (demux, "seek event, rate: %f start: %" GST_TIME_FORMAT
          " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush start");
        gst_pad_push_event (demux->srcpad, gst_event_new_flush_start ());
      }

      gst_hls_demux_pause_tasks (demux);

      /* wait for streaming to finish */
      g_rec_mutex_lock (&demux->updates_lock);
      g_rec_mutex_unlock (&demux->updates_lock);

      g_rec_mutex_lock (&demux->stream_lock);

      /* properly cleanup pending decryption status */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        if (demux->adapter)
          gst_adapter_clear (demux->adapter);
        if (demux->pending_buffer)
          gst_buffer_unref (demux->pending_buffer);
        demux->pending_buffer = NULL;
        gst_hls_demux_decrypt_end (demux);
      }

      /* Use I-frame variants for trick modes */
      if ((rate > 1.0 || rate < -1.0) && demux->segment.rate >= -1.0
          && demux->segment.rate <= 1.0) {
        GError *err = NULL;

        GST_M3U8_CLIENT_LOCK (demux->client);
        /* Switch to I-frame variant */
        demux->client->main->current_variant =
            demux->client->main->iframe_lists;
        GST_M3U8_CLIENT_UNLOCK (demux->client);
        gst_m3u8_client_set_current (demux->client,
            demux->client->main->iframe_lists->data);
        gst_uri_downloader_reset (demux->downloader);
        if (!gst_hls_demux_update_playlist (demux, FALSE, &err)) {
          g_rec_mutex_unlock (&demux->stream_lock);
          GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not switch playlist",
              err);
          gst_event_unref (event);
          return FALSE;
        }
        demux->discont = TRUE;
        demux->new_playlist = TRUE;
        demux->do_typefind = TRUE;

        gst_hls_demux_change_playlist (demux,
            demux->current_download_rate * demux->bitrate_limit / ABS (rate));
      } else if (rate > -1.0 && rate <= 1.0 && (demux->segment.rate < -1.0
              || demux->segment.rate > 1.0)) {
        GError *err = NULL;

        GST_M3U8_CLIENT_LOCK (demux->client);
        /* Switch to normal variant */
        demux->client->main->current_variant = demux->client->main->lists;
        GST_M3U8_CLIENT_UNLOCK (demux->client);
        gst_m3u8_client_set_current (demux->client,
            demux->client->main->lists->data);

        gst_uri_downloader_reset (demux->downloader);

        if (!gst_hls_demux_update_playlist (demux, FALSE, &err)) {
          g_rec_mutex_unlock (&demux->stream_lock);

          GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not switch playlist",
              err);
          gst_event_unref (event);
          return FALSE;
        }
        demux->discont = TRUE;
        demux->new_playlist = TRUE;
        demux->do_typefind = TRUE;

        gst_hls_demux_change_playlist (demux,
            demux->current_download_rate * demux->bitrate_limit);
      }

      GST_M3U8_CLIENT_LOCK (demux->client);
      file = GST_M3U8_MEDIA_FILE (demux->client->current->files->data);
      current_sequence = file->sequence;
      current_pos = 0;
      target_pos = rate > 0 ? start : stop;
      /* FIXME: Here we need proper discont handling */
      for (walk = demux->client->current->files; walk; walk = walk->next) {
        file = walk->data;

        current_sequence = file->sequence;
        if (current_pos <= target_pos
            && target_pos < current_pos + file->duration) {
          break;
        }
        current_pos += file->duration;
      }
      GST_M3U8_CLIENT_UNLOCK (demux->client);

      if (walk == NULL) {
        GST_DEBUG_OBJECT (demux, "seeking further than track duration");
        current_sequence++;
      }

      GST_M3U8_CLIENT_LOCK (demux->client);
      GST_DEBUG_OBJECT (demux, "seeking to sequence %u",
          (guint) current_sequence);
      demux->client->sequence = current_sequence;
      demux->client->sequence_position = current_pos;
      GST_M3U8_CLIENT_UNLOCK (demux->client);

      gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
          start, stop_type, stop, NULL);
      demux->need_segment = TRUE;

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush stop");
        gst_pad_push_event (demux->srcpad, gst_event_new_flush_stop (TRUE));
      }

      demux->stop_updates_task = FALSE;
      gst_uri_downloader_reset (demux->downloader);
      demux->stop_stream_task = FALSE;

      gst_task_start (demux->updates_task);
      g_rec_mutex_unlock (&demux->stream_lock);

      gst_event_unref (event);
      return TRUE;
    }
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
gst_hls_demux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstHLSDemux *demux;
  GstQuery *query;
  gboolean ret;

  demux = GST_HLS_DEMUX (parent);

  switch (event->type) {
    case GST_EVENT_EOS:{
      gchar *playlist = NULL;

      if (demux->playlist == NULL) {
        GST_WARNING_OBJECT (demux, "Received EOS without a playlist.");
        break;
      }

      GST_DEBUG_OBJECT (demux,
          "Got EOS on the sink pad: main playlist fetched");

      query = gst_query_new_uri ();
      ret = gst_pad_peer_query (demux->sinkpad, query);
      if (ret) {
        gboolean permanent;
        gchar *uri, *redirect_uri;

        gst_query_parse_uri (query, &uri);
        gst_query_parse_uri_redirection (query, &redirect_uri);
        gst_query_parse_uri_redirection_permanent (query, &permanent);

        if (permanent && redirect_uri) {
          gst_hls_demux_set_location (demux, redirect_uri, NULL);
        } else {
          gst_hls_demux_set_location (demux, uri, redirect_uri);
        }
        g_free (uri);
        g_free (redirect_uri);
      }
      gst_query_unref (query);

      playlist = gst_hls_src_buf_to_utf8_playlist (demux->playlist);
      demux->playlist = NULL;
      if (playlist == NULL) {
        GST_WARNING_OBJECT (demux, "Error validating first playlist.");
      } else if (!gst_m3u8_client_update (demux->client, playlist)) {
        /* In most cases, this will happen if we set a wrong url in the
         * source element and we have received the 404 HTML response instead of
         * the playlist */
        GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid playlist."),
            (NULL));
        return FALSE;
      }

      if (!ret && gst_m3u8_client_is_live (demux->client)) {
        GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
            ("Failed querying the playlist uri, "
                "required for live sources."), (NULL));
        return FALSE;
      }

      gst_task_start (demux->updates_task);
      gst_event_unref (event);
      return TRUE;
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
gst_hls_demux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstHLSDemux *hlsdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  hlsdemux = GST_HLS_DEMUX (parent);

  switch (query->type) {
    case GST_QUERY_DURATION:{
      GstClockTime duration = -1;
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME) {
        duration = gst_m3u8_client_get_duration (hlsdemux->client);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          ret = TRUE;
        }
      }
      GST_INFO_OBJECT (hlsdemux, "GST_QUERY_DURATION returns %s with duration %"
          GST_TIME_FORMAT, ret ? "TRUE" : "FALSE", GST_TIME_ARGS (duration));
      break;
    }
    case GST_QUERY_URI:
      if (hlsdemux->client) {
        /* FIXME: Do we answer with the variant playlist, with the current
         * playlist or the the uri of the least downlowaded fragment? */
        gst_query_set_uri (query, gst_m3u8_client_get_uri (hlsdemux->client));
        ret = TRUE;
      }
      break;
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint64 stop = -1;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      GST_INFO_OBJECT (hlsdemux, "Received GST_QUERY_SEEKING with format %d",
          fmt);
      if (fmt == GST_FORMAT_TIME) {
        GstClockTime duration;

        duration = gst_m3u8_client_get_duration (hlsdemux->client);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
          stop = duration;

        gst_query_set_seeking (query, fmt,
            !gst_m3u8_client_is_live (hlsdemux->client), 0, stop);
        ret = TRUE;
        GST_INFO_OBJECT (hlsdemux, "GST_QUERY_SEEKING returning with stop : %"
            GST_TIME_FORMAT, GST_TIME_ARGS (stop));
      }
      break;
    }
    default:
      /* Don't fordward queries upstream because of the special nature of this
       * "demuxer", which relies on the upstream element only to be fed with the
       * first playlist */
      break;
  }

  return ret;
}

static GstFlowReturn
gst_hls_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (parent);

  if (demux->playlist == NULL)
    demux->playlist = buf;
  else
    demux->playlist = gst_buffer_append (demux->playlist, buf);

  return GST_FLOW_OK;
}

static void
gst_hls_demux_pause_tasks (GstHLSDemux * demux)
{
  if (GST_TASK_STATE (demux->updates_task) != GST_TASK_STOPPED) {
    g_mutex_lock (&demux->updates_timed_lock);
    demux->stop_updates_task = TRUE;
    g_cond_signal (&demux->updates_timed_cond);
    g_mutex_unlock (&demux->updates_timed_lock);
    gst_uri_downloader_cancel (demux->downloader);
    gst_task_pause (demux->updates_task);
  }

  if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED) {
    g_mutex_lock (&demux->download_lock);
    demux->stop_stream_task = TRUE;
    g_cond_signal (&demux->download_cond);
    g_mutex_unlock (&demux->download_lock);
    g_mutex_lock (&demux->fragment_download_lock);
    g_cond_signal (&demux->fragment_download_cond);
    g_mutex_unlock (&demux->fragment_download_lock);
    gst_task_pause (demux->stream_task);
  }
}

static void
gst_hls_demux_stop (GstHLSDemux * demux)
{
  if (GST_TASK_STATE (demux->updates_task) != GST_TASK_STOPPED) {
    g_mutex_lock (&demux->updates_timed_lock);
    demux->stop_updates_task = TRUE;
    g_cond_signal (&demux->updates_timed_cond);
    g_mutex_unlock (&demux->updates_timed_lock);
    gst_uri_downloader_cancel (demux->downloader);
    gst_task_stop (demux->updates_task);
    g_rec_mutex_lock (&demux->updates_lock);
    g_rec_mutex_unlock (&demux->updates_lock);
  }

  if (GST_TASK_STATE (demux->stream_task) != GST_TASK_STOPPED) {
    g_mutex_lock (&demux->download_lock);
    demux->stop_stream_task = TRUE;
    g_cond_signal (&demux->download_cond);
    g_mutex_unlock (&demux->download_lock);
    g_mutex_lock (&demux->fragment_download_lock);
    g_cond_signal (&demux->fragment_download_cond);
    g_mutex_unlock (&demux->fragment_download_lock);
    gst_task_stop (demux->stream_task);
    g_rec_mutex_lock (&demux->stream_lock);
    g_rec_mutex_unlock (&demux->stream_lock);
  }
}

static GstFlowReturn
_src_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPad *srcpad = (GstPad *) parent;
  GstHLSDemux *demux = (GstHLSDemux *) GST_PAD_PARENT (srcpad);
  GstFlowReturn ret;
  GstCaps *caps;

  /* Is it encrypted? */
  if (demux->current_key) {
    GError *err = NULL;
    GstBuffer *tmp_buffer;
    gsize available;

    /* restart the decrypting lib for a new fragment */
    if (demux->reset_crypto) {
      GstFragment *key_fragment;
      GstBuffer *key_buffer;
      GstMapInfo key_info;

      /* new key? */
      if (demux->key_url && strcmp (demux->key_url, demux->current_key) == 0) {
        key_fragment = g_object_ref (demux->key_fragment);
      } else {
        g_free (demux->key_url);
        demux->key_url = NULL;

        if (demux->key_fragment)
          g_object_unref (demux->key_fragment);
        demux->key_fragment = NULL;

        GST_INFO_OBJECT (demux, "Fetching key %s", demux->current_key);
        key_fragment =
            gst_uri_downloader_fetch_uri (demux->downloader,
            demux->current_key, demux->client->main ?
            demux->client->main->uri : NULL, FALSE, FALSE,
            demux->client->current ? demux->client->current->allowcache : TRUE,
            &err);
        if (key_fragment == NULL)
          goto key_failed;
        demux->key_url = g_strdup (demux->current_key);
        demux->key_fragment = g_object_ref (key_fragment);
      }

      key_buffer = gst_fragment_get_buffer (key_fragment);
      gst_buffer_map (key_buffer, &key_info, GST_MAP_READ);

      gst_hls_demux_decrypt_start (demux, key_info.data, demux->current_iv);

      gst_buffer_unmap (key_buffer, &key_info);
      gst_buffer_unref (key_buffer);
      g_object_unref (key_fragment);

      demux->reset_crypto = FALSE;
    }

    gst_adapter_push (demux->adapter, buffer);

    /* must be a multiple of 16 */
    available = gst_adapter_available (demux->adapter) & (~0xF);

    if (available == 0) {
      return GST_FLOW_OK;
    }

    buffer = gst_adapter_take_buffer (demux->adapter, available);
    buffer = gst_hls_demux_decrypt_fragment (demux, buffer, &err);
    if (buffer == NULL) {
      GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Failed to decrypt buffer"),
          ("decryption failed %s", err->message));
      g_error_free (err);

      demux->last_ret = GST_FLOW_ERROR;
      return GST_FLOW_ERROR;
    }

    tmp_buffer = demux->pending_buffer;
    demux->pending_buffer = buffer;
    buffer = tmp_buffer;
  }

  if (!buffer) {
    return GST_FLOW_OK;
  }

  if (demux->starting_fragment) {
    GST_LOG_OBJECT (demux, "set buffer pts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->current_timestamp));
    GST_BUFFER_PTS (buffer) = demux->current_timestamp;

    if (demux->segment.rate < 0)
      /* Set DISCONT flag for every first buffer in reverse playback mode
       * as each fragment for its own has to be reversed */
      demux->discont = TRUE;
    demux->starting_fragment = FALSE;
    demux->segment.position = GST_BUFFER_PTS (buffer);
  } else {
    GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  }

  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;

  /* We actually need to do this every time we switch bitrate */
  if (G_UNLIKELY (demux->do_typefind)) {
    caps = gst_type_find_helper_for_buffer (NULL, buffer, NULL);
    if (G_UNLIKELY (!caps)) {
      GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND,
          ("Could not determine type of stream"), (NULL));
      gst_buffer_unref (buffer);
      demux->last_ret = GST_FLOW_NOT_NEGOTIATED;
      return GST_FLOW_NOT_NEGOTIATED;
    }

    if (!demux->input_caps || !gst_caps_is_equal (caps, demux->input_caps)) {
      gst_caps_replace (&demux->input_caps, caps);
      GST_INFO_OBJECT (demux, "Input source caps: %" GST_PTR_FORMAT,
          demux->input_caps);
    }
    gst_pad_set_caps (srcpad, caps);
    demux->do_typefind = FALSE;
    gst_caps_unref (caps);
  }

  if (demux->discont) {
    GST_DEBUG_OBJECT (demux, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    demux->discont = FALSE;
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  demux->starting_fragment = FALSE;

  if (demux->need_segment) {
    /* And send a newsegment */
    GST_DEBUG_OBJECT (demux, "Sending segment event: %"
        GST_SEGMENT_FORMAT, &demux->segment);
    gst_pad_push_event (demux->srcpad, gst_event_new_segment (&demux->segment));
    demux->need_segment = FALSE;
  }

  /* accumulate time and size to get this chunk */
  demux->download_total_time +=
      g_get_monotonic_time () - demux->download_start_time;
  demux->download_total_bytes += gst_buffer_get_size (buffer);

  ret = gst_proxy_pad_chain_default (pad, parent, buffer);
  demux->download_start_time = g_get_monotonic_time ();

  if (ret != GST_FLOW_OK) {
    if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL),
          ("stream stopped, reason %s", gst_flow_get_name (ret)));
      gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    } else {
      GST_DEBUG_OBJECT (demux, "stream stopped, reason %s",
          gst_flow_get_name (ret));
    }
    gst_hls_demux_pause_tasks (demux);
  }

  /* avoid having the source handle the same error again */
  demux->last_ret = ret;
  ret = GST_FLOW_OK;

  return ret;

key_failed:
  /* TODO Raise this error to the user */
  GST_WARNING_OBJECT (demux, "Failed to decrypt data");
  demux->last_ret = GST_FLOW_ERROR;
  return GST_FLOW_ERROR;
}

static gboolean
_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstPad *srcpad = GST_PAD_CAST (parent);
  GstHLSDemux *demux = (GstHLSDemux *) GST_PAD_PARENT (srcpad);;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (demux->current_key)
        gst_hls_demux_decrypt_end (demux);

      /* ideally this should be empty, but this eos might have been
       * caused by an error on the source element */
      GST_DEBUG_OBJECT (demux, "Data still on the adapter when EOS was received"
          ": %" G_GSIZE_FORMAT, gst_adapter_available (demux->adapter));
      gst_adapter_clear (demux->adapter);

      /* pending buffer is only used for encrypted streams */
      if (demux->last_ret == GST_FLOW_OK) {
        if (demux->pending_buffer) {
          GstMapInfo info;
          gsize unpadded_size;

          /* Handle pkcs7 unpadding here */
          gst_buffer_map (demux->pending_buffer, &info, GST_MAP_READ);
          unpadded_size = info.size - info.data[info.size - 1];
          gst_buffer_unmap (demux->pending_buffer, &info);

          gst_buffer_resize (demux->pending_buffer, 0, unpadded_size);

          demux->download_total_time +=
              g_get_monotonic_time () - demux->download_start_time;
          demux->download_total_bytes +=
              gst_buffer_get_size (demux->pending_buffer);
          demux->last_ret = gst_pad_push (demux->srcpad, demux->pending_buffer);

          demux->pending_buffer = NULL;
        }
      } else {
        if (demux->pending_buffer)
          gst_buffer_unref (demux->pending_buffer);
        demux->pending_buffer = NULL;
      }

      GST_DEBUG_OBJECT (demux, "Fragment download finished");

      g_mutex_lock (&demux->fragment_download_lock);
      g_cond_signal (&demux->fragment_download_cond);
      g_mutex_unlock (&demux->fragment_download_lock);
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

static void
switch_pads (GstHLSDemux * demux)
{
  GstPad *oldpad = demux->srcpad;
  GstEvent *event;
  gchar *stream_id;
  gchar *name;
  GstPadTemplate *tmpl;
  GstProxyPad *internal_pad;

  GST_DEBUG_OBJECT (demux, "Switching pad (oldpad:%p)", oldpad);

  if (oldpad) {
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (oldpad), NULL);
  }

  /* First create and activate new pad */
  name = g_strdup_printf ("src_%u", demux->srcpad_counter++);
  tmpl = gst_static_pad_template_get (&srctemplate);
  demux->srcpad =
      gst_ghost_pad_new_from_template (name, demux->src_srcpad, tmpl);
  gst_object_unref (tmpl);
  g_free (name);

  /* set up our internal pad to drop all events from
   * the http src we don't care about. On the chain function
   * we just push the buffer forward, but this way hls can get
   * the flow return from downstream */
  internal_pad = gst_proxy_pad_get_internal (GST_PROXY_PAD (demux->srcpad));
  gst_pad_set_chain_function (GST_PAD_CAST (internal_pad), _src_chain);
  gst_pad_set_event_function (GST_PAD_CAST (internal_pad), _src_event);
  /* need to set query otherwise deadlocks happen with allocation queries */
  gst_pad_set_query_function (GST_PAD_CAST (internal_pad), _src_query);
  gst_object_unref (internal_pad);

  gst_pad_set_event_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_src_event));
  gst_pad_set_query_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_src_query));
  gst_pad_use_fixed_caps (demux->srcpad);
  gst_pad_set_active (demux->srcpad, TRUE);

  stream_id =
      gst_pad_create_stream_id (demux->srcpad, GST_ELEMENT_CAST (demux), NULL);

  event = gst_pad_get_sticky_event (demux->sinkpad, GST_EVENT_STREAM_START, 0);
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

  gst_pad_push_event (demux->srcpad, event);
  g_free (stream_id);

  gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad);

  gst_element_no_more_pads (GST_ELEMENT (demux));

  demux->new_playlist = FALSE;

  if (oldpad) {
    /* Push out EOS */
    gst_pad_push_event (oldpad, gst_event_new_eos ());
    gst_pad_set_active (oldpad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (demux), oldpad);
  }
}

static void
gst_hls_demux_configure_src_pad (GstHLSDemux * demux)
{
  if (G_UNLIKELY (!demux->srcpad || demux->new_playlist)) {
    switch_pads (demux);
    demux->need_segment = TRUE;
  }
}

static void
gst_hls_demux_stream_loop (GstHLSDemux * demux)
{
  gboolean end_of_playlist;
  GError *err = NULL;

  /* This task will download fragments as fast as possible, sends
   * SEGMENT and CAPS events and switches pads if necessary.
   * If downloading a fragment fails we try again up to 3 times
   * after waiting a bit. If we're at the end of the playlist
   * we wait for the playlist to update before getting the next
   * fragment.
   */
  GST_DEBUG_OBJECT (demux, "Enter task");

  if (demux->stop_stream_task)
    goto pause_task;

  /* Check if we're done with our segment */
  if (demux->segment.rate > 0) {
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop)
        && demux->segment.position >= demux->segment.stop)
      goto end_of_playlist;
  } else {
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.start)
        && demux->segment.position < demux->segment.start)
      goto end_of_playlist;
  }

  demux->next_download = g_get_monotonic_time ();
  if (!gst_hls_demux_get_next_fragment (demux, &end_of_playlist, &err)) {
    if (demux->stop_stream_task) {
      g_clear_error (&err);
      goto pause_task;
    }

    if (end_of_playlist) {
      if (!gst_m3u8_client_is_live (demux->client)) {
        GST_DEBUG_OBJECT (demux, "End of playlist");
        demux->end_of_playlist = TRUE;
        goto end_of_playlist;
      } else {
        g_mutex_lock (&demux->download_lock);

        /* Wait until we're cancelled or there's something for
         * us to download in the playlist or the playlist
         * became non-live */
        while (TRUE) {
          if (demux->stop_stream_task) {
            g_mutex_unlock (&demux->download_lock);
            goto pause_task;
          }

          /* Got a new fragment or not live anymore? */
          if (gst_m3u8_client_get_next_fragment (demux->client, NULL, NULL,
                  NULL, NULL, NULL, NULL, NULL, NULL, demux->segment.rate > 0)
              || !gst_m3u8_client_is_live (demux->client))
            break;

          GST_DEBUG_OBJECT (demux,
              "No fragment left but live playlist, wait a bit");
          g_cond_wait (&demux->download_cond, &demux->download_lock);
        }
        g_mutex_unlock (&demux->download_lock);
        GST_DEBUG_OBJECT (demux, "Retrying now");
        return;
      }
    } else {
      demux->download_failed_count++;
      if (demux->download_failed_count <= DEFAULT_FAILED_COUNT) {
        GST_WARNING_OBJECT (demux, "Could not fetch the next fragment");
        g_clear_error (&err);

        /* First try to update the playlist for non-live playlists
         * in case the URIs have changed in the meantime. But only
         * try it the first time, after that we're going to wait a
         * a bit to not flood the server */
        if (demux->download_failed_count == 1
            && !gst_m3u8_client_is_live (demux->client)
            && gst_hls_demux_update_playlist (demux, FALSE, &err)) {
          /* Retry immediately, the playlist actually has changed */
          GST_DEBUG_OBJECT (demux, "Updated the playlist");
          return;
        } else {
          /* Wait half the fragment duration before retrying */
          demux->next_download +=
              gst_util_uint64_scale
              (gst_m3u8_client_get_current_fragment_duration (demux->client),
              G_USEC_PER_SEC, 2 * GST_SECOND);
        }

        g_clear_error (&err);

        g_mutex_lock (&demux->download_lock);
        if (demux->stop_stream_task) {
          g_mutex_unlock (&demux->download_lock);
          goto pause_task;
        }
        g_cond_wait_until (&demux->download_cond, &demux->download_lock,
            demux->next_download);
        g_mutex_unlock (&demux->download_lock);
        GST_DEBUG_OBJECT (demux, "Retrying now");

        /* Refetch the playlist now after we waited */
        if (!gst_m3u8_client_is_live (demux->client)
            && gst_hls_demux_update_playlist (demux, FALSE, &err)) {
          GST_DEBUG_OBJECT (demux, "Updated the playlist");
        }
        return;
      } else {
        GST_ELEMENT_ERROR_FROM_ERROR (demux,
            "Could not fetch the next fragment", err);
        goto pause_task;
      }
    }
  } else {
    demux->download_failed_count = 0;
    gst_m3u8_client_advance_fragment (demux->client, demux->segment.rate > 0);

    if (demux->stop_updates_task) {
      goto pause_task;
    }
  }

  if (demux->stop_updates_task) {
    goto pause_task;
  }

  /* try to switch to another bitrate if needed */
  gst_hls_demux_switch_playlist (demux);
  demux->download_total_bytes = 0;
  demux->download_total_time = 0;

  GST_DEBUG_OBJECT (demux, "Finished pushing fragment");

  return;

end_of_playlist:
  {
    GST_DEBUG_OBJECT (demux, "Reached end of playlist, sending EOS");

    gst_hls_demux_configure_src_pad (demux);

    gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    gst_hls_demux_pause_tasks (demux);
    return;
  }

pause_task:
  {
    GST_DEBUG_OBJECT (demux, "Pause task");
    /* Pausing a stopped task will start it */
    gst_hls_demux_pause_tasks (demux);
    return;
  }
}

static void
gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose)
{
  demux->end_of_playlist = FALSE;
  demux->stop_updates_task = FALSE;
  demux->do_typefind = TRUE;

  demux->download_failed_count = 0;

  g_free (demux->key_url);
  demux->key_url = NULL;

  if (demux->key_fragment)
    g_object_unref (demux->key_fragment);
  demux->key_fragment = NULL;

  if (demux->input_caps) {
    gst_caps_unref (demux->input_caps);
    demux->input_caps = NULL;
  }

  if (demux->playlist) {
    gst_buffer_unref (demux->playlist);
    demux->playlist = NULL;
  }

  if (demux->client) {
    gst_m3u8_client_free (demux->client);
    demux->client = NULL;
  }

  if (!dispose) {
    demux->client = gst_m3u8_client_new ("", NULL);
  }

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->need_segment = TRUE;
  demux->discont = TRUE;

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;

  demux->srcpad_counter = 0;
  if (demux->srcpad) {
    gst_element_remove_pad (GST_ELEMENT_CAST (demux), demux->srcpad);
    demux->srcpad = NULL;
  }

  if (demux->src) {
    gst_element_set_state (demux->src, GST_STATE_NULL);
  }

  g_clear_error (&demux->last_error);

  if (demux->adapter)
    gst_adapter_clear (demux->adapter);
  if (demux->pending_buffer)
    gst_buffer_unref (demux->pending_buffer);
  demux->pending_buffer = NULL;
  demux->current_key = NULL;
  demux->current_iv = NULL;
  gst_hls_demux_decrypt_end (demux);

  demux->current_download_rate = -1;
}

static gboolean
gst_hls_demux_set_location (GstHLSDemux * demux, const gchar * uri,
    const gchar * base_uri)
{
  if (demux->client)
    gst_m3u8_client_free (demux->client);
  demux->client = gst_m3u8_client_new (uri, base_uri);
  GST_INFO_OBJECT (demux, "Changed location: %s (base uri: %s)", uri,
      GST_STR_NULL (base_uri));
  return TRUE;
}

void
gst_hls_demux_updates_loop (GstHLSDemux * demux)
{
  /* Loop for updating of the playlist. This periodically checks if
   * the playlist is updated and does so, then signals the streaming
   * thread in case it can continue downloading now.
   * For non-live playlists this thread is not doing much else than
   * setting up the initial playlist and then stopping. */

  /* block until the next scheduled update or the signal to quit this thread */
  GST_DEBUG_OBJECT (demux, "Started updates task");

  /* If this playlist is a variant playlist, select the first one
   * and update it */
  if (gst_m3u8_client_has_variant_playlist (demux->client)) {
    GstM3U8 *child = NULL;
    GError *err = NULL;

    if (demux->connection_speed == 0) {
      GST_M3U8_CLIENT_LOCK (demux->client);
      child = demux->client->main->current_variant->data;
      GST_M3U8_CLIENT_UNLOCK (demux->client);
    } else {
      GList *tmp = gst_m3u8_client_get_playlist_for_bitrate (demux->client,
          demux->connection_speed);

      child = GST_M3U8 (tmp->data);
    }

    gst_m3u8_client_set_current (demux->client, child);
    if (!gst_hls_demux_update_playlist (demux, FALSE, &err)) {
      GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not fetch the child playlist",
          err);
      goto error;
    }
  }

  if (!gst_m3u8_client_is_live (demux->client)) {
    GstClockTime duration = gst_m3u8_client_get_duration (demux->client);

    GST_DEBUG_OBJECT (demux, "Sending duration message : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (duration));
    if (duration != GST_CLOCK_TIME_NONE)
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_duration_changed (GST_OBJECT (demux)));
  }

  /* Now start stream task */
  gst_task_start (demux->stream_task);

  demux->next_update =
      g_get_monotonic_time () +
      gst_util_uint64_scale (gst_m3u8_client_get_target_duration
      (demux->client), G_USEC_PER_SEC, GST_SECOND);

  /* Updating playlist only needed for live playlists */
  while (gst_m3u8_client_is_live (demux->client)) {
    GError *err = NULL;

    /* Wait here until we should do the next update or we're cancelled */
    GST_DEBUG_OBJECT (demux, "Wait for next playlist update");
    g_mutex_lock (&demux->updates_timed_lock);
    if (demux->stop_updates_task) {
      g_mutex_unlock (&demux->updates_timed_lock);
      goto quit;
    }
    g_cond_wait_until (&demux->updates_timed_cond, &demux->updates_timed_lock,
        demux->next_update);
    if (demux->stop_updates_task) {
      g_mutex_unlock (&demux->updates_timed_lock);
      goto quit;
    }
    g_mutex_unlock (&demux->updates_timed_lock);

    GST_DEBUG_OBJECT (demux, "Updating playlist");
    if (!gst_hls_demux_update_playlist (demux, TRUE, &err)) {
      if (demux->stop_updates_task)
        goto quit;
      demux->client->update_failed_count++;
      if (demux->client->update_failed_count <= DEFAULT_FAILED_COUNT) {
        GST_WARNING_OBJECT (demux, "Could not update the playlist");
        demux->next_update =
            g_get_monotonic_time () +
            gst_util_uint64_scale (gst_m3u8_client_get_target_duration
            (demux->client), G_USEC_PER_SEC, 2 * GST_SECOND);
      } else {
        GST_ELEMENT_ERROR_FROM_ERROR (demux, "Could not update playlist", err);
        goto error;
      }
    } else {
      GST_DEBUG_OBJECT (demux, "Updated playlist successfully");
      demux->next_update =
          g_get_monotonic_time () +
          gst_util_uint64_scale (gst_m3u8_client_get_target_duration
          (demux->client), G_USEC_PER_SEC, GST_SECOND);
      /* Wake up download task */
      g_mutex_lock (&demux->download_lock);
      g_cond_signal (&demux->download_cond);
      g_mutex_unlock (&demux->download_lock);
    }
  }

quit:
  {
    GST_DEBUG_OBJECT (demux, "Stopped updates task");
    gst_task_pause (demux->updates_task);
    return;
  }

error:
  {
    GST_DEBUG_OBJECT (demux, "Stopped updates task because of error");
    gst_hls_demux_pause_tasks (demux);
  }
}

static gchar *
gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf)
{
  GstMapInfo info;
  gchar *playlist;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    goto map_error;

  if (!g_utf8_validate ((gchar *) info.data, info.size, NULL))
    goto validate_error;

  /* alloc size + 1 to end with a null character */
  playlist = g_malloc0 (info.size + 1);
  memcpy (playlist, info.data, info.size);

  gst_buffer_unmap (buf, &info);
  gst_buffer_unref (buf);
  return playlist;

validate_error:
  gst_buffer_unmap (buf, &info);
map_error:
  gst_buffer_unref (buf);
  return NULL;
}

static gboolean
gst_hls_demux_update_playlist (GstHLSDemux * demux, gboolean update,
    GError ** err)
{
  GstFragment *download;
  GstBuffer *buf;
  gchar *playlist;
  gboolean main_checked = FALSE, updated = FALSE;
  const gchar *uri;

retry:
  uri = gst_m3u8_client_get_current_uri (demux->client);
  download =
      gst_uri_downloader_fetch_uri (demux->downloader, uri,
      demux->client->main ? demux->client->main->uri : NULL, TRUE, TRUE, TRUE,
      err);
  if (download == NULL) {
    if (update && !main_checked
        && gst_m3u8_client_has_variant_playlist (demux->client)
        && demux->client->main) {
      GError *err2 = NULL;
      GST_INFO_OBJECT (demux,
          "Updating playlist %s failed, attempt to refresh variant playlist %s",
          uri, demux->client->main->uri);
      download =
          gst_uri_downloader_fetch_uri (demux->downloader,
          demux->client->main->uri, NULL, TRUE, TRUE, TRUE, &err2);
      g_clear_error (&err2);
      if (download != NULL) {
        gchar *base_uri;

        buf = gst_fragment_get_buffer (download);
        playlist = gst_hls_src_buf_to_utf8_playlist (buf);

        if (playlist == NULL) {
          GST_WARNING_OBJECT (demux,
              "Failed to validate variant playlist encoding");
          return FALSE;
        }

        if (download->redirect_permanent && download->redirect_uri) {
          uri = download->redirect_uri;
          base_uri = NULL;
        } else {
          uri = download->uri;
          base_uri = download->redirect_uri;
        }

        if (!gst_m3u8_client_update_variant_playlist (demux->client, playlist,
                uri, base_uri)) {
          GST_WARNING_OBJECT (demux, "Failed to update the variant playlist");
          return FALSE;
        }

        g_object_unref (download);

        g_clear_error (err);
        main_checked = TRUE;
        goto retry;
      } else {
        return FALSE;
      }
    } else {
      return FALSE;
    }
  }

  /* Set the base URI of the playlist to the redirect target if any */
  GST_M3U8_CLIENT_LOCK (demux->client);
  g_free (demux->client->current->uri);
  g_free (demux->client->current->base_uri);
  if (download->redirect_permanent && download->redirect_uri) {
    demux->client->current->uri = g_strdup (download->redirect_uri);
    demux->client->current->base_uri = NULL;
  } else {
    demux->client->current->uri = g_strdup (download->uri);
    demux->client->current->base_uri = g_strdup (download->redirect_uri);
  }
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  buf = gst_fragment_get_buffer (download);
  playlist = gst_hls_src_buf_to_utf8_playlist (buf);
  g_object_unref (download);

  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't validate playlist encoding");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_WRONG_TYPE,
        "Couldn't validate playlist encoding");
    return FALSE;
  }

  updated = gst_m3u8_client_update (demux->client, playlist);
  if (!updated) {
    GST_WARNING_OBJECT (demux, "Couldn't update playlist");
    g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Couldn't update playlist");
    return FALSE;
  }

  /* If it's a live source, do not let the sequence number go beyond
   * three fragments before the end of the list */
  if (update == FALSE && demux->client->current &&
      gst_m3u8_client_is_live (demux->client)) {
    gint64 last_sequence;

    GST_M3U8_CLIENT_LOCK (demux->client);
    last_sequence =
        GST_M3U8_MEDIA_FILE (g_list_last (demux->client->current->
            files)->data)->sequence;

    if (demux->client->sequence >= last_sequence - 3) {
      GST_DEBUG_OBJECT (demux, "Sequence is beyond playlist. Moving back to %u",
          (guint) (last_sequence - 3));
      demux->need_segment = TRUE;
      demux->client->sequence = last_sequence - 3;
    }
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  } else if (demux->client->current && !gst_m3u8_client_is_live (demux->client)) {
    GstClockTime current_pos, target_pos;
    guint sequence = 0;
    GList *walk;

    /* Sequence numbers are not guaranteed to be the same in different
     * playlists, so get the correct fragment here based on the current
     * position
     */
    GST_M3U8_CLIENT_LOCK (demux->client);
    current_pos = 0;
    target_pos = demux->segment.position;
    for (walk = demux->client->current->files; walk; walk = walk->next) {
      GstM3U8MediaFile *file = walk->data;

      sequence = file->sequence;
      if (current_pos <= target_pos
          && target_pos < current_pos + file->duration) {
        break;
      }
      current_pos += file->duration;
    }
    /* End of playlist */
    if (!walk)
      sequence++;
    demux->client->sequence = sequence;
    demux->client->sequence_position = current_pos;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  }

  return updated;
}

static gboolean
gst_hls_demux_change_playlist (GstHLSDemux * demux, guint max_bitrate)
{
  GList *previous_variant, *current_variant;
  gint old_bandwidth, new_bandwidth;

  /* If user specifies a connection speed never use a playlist with a bandwidth
   * superior than it */
  if (demux->connection_speed != 0 && max_bitrate > demux->connection_speed)
    max_bitrate = demux->connection_speed;

  previous_variant = demux->client->main->current_variant;
  current_variant = gst_m3u8_client_get_playlist_for_bitrate (demux->client,
      max_bitrate);

  GST_M3U8_CLIENT_LOCK (demux->client);

retry_failover_protection:
  old_bandwidth = GST_M3U8 (previous_variant->data)->bandwidth;
  new_bandwidth = GST_M3U8 (current_variant->data)->bandwidth;

  /* Don't do anything else if the playlist is the same */
  if (new_bandwidth == old_bandwidth) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }

  demux->client->main->current_variant = current_variant;
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_m3u8_client_set_current (demux->client, current_variant->data);

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", old_bandwidth, max_bitrate, new_bandwidth);
  demux->discont = TRUE;
  demux->new_playlist = TRUE;

  if (gst_hls_demux_update_playlist (demux, FALSE, NULL)) {
    GstStructure *s;

    s = gst_structure_new ("playlist",
        "uri", G_TYPE_STRING, gst_m3u8_client_get_current_uri (demux->client),
        "bitrate", G_TYPE_INT, new_bandwidth, NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT_CAST (demux), s));
  } else {
    GList *failover = NULL;

    GST_INFO_OBJECT (demux, "Unable to update playlist. Switching back");
    GST_M3U8_CLIENT_LOCK (demux->client);

    failover = g_list_previous (current_variant);
    if (failover && new_bandwidth == GST_M3U8 (failover->data)->bandwidth) {
      current_variant = failover;
      goto retry_failover_protection;
    }

    demux->client->main->current_variant = previous_variant;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    gst_m3u8_client_set_current (demux->client, previous_variant->data);
    /*  Try a lower bitrate (or stop if we just tried the lowest) */
    if (GST_M3U8 (previous_variant->data)->iframe && new_bandwidth ==
        GST_M3U8 (g_list_first (demux->client->main->iframe_lists)->data)->
        bandwidth)
      return FALSE;
    else if (!GST_M3U8 (previous_variant->data)->iframe && new_bandwidth ==
        GST_M3U8 (g_list_first (demux->client->main->lists)->data)->bandwidth)
      return FALSE;
    else
      return gst_hls_demux_change_playlist (demux, new_bandwidth - 1);
  }

  /* Force typefinding since we might have changed media type */
  demux->do_typefind = TRUE;

  return TRUE;
}

static gboolean
gst_hls_demux_switch_playlist (GstHLSDemux * demux)
{
  gint64 bitrate;

  /* compare the time when the fragment was downloaded with the time when it was
   * scheduled */
  bitrate =
      (demux->download_total_bytes * 8) / ((double) demux->download_total_time /
      G_GUINT64_CONSTANT (1000000));

  GST_DEBUG_OBJECT (demux,
      "Downloaded %u bytes in %" GST_TIME_FORMAT ". Bitrate is : %d",
      (guint) demux->download_total_bytes,
      GST_TIME_ARGS (demux->download_total_time * GST_USECOND), (gint) bitrate);

  /* Take old rate into account too */
  if (demux->current_download_rate != -1)
    bitrate = (demux->current_download_rate + bitrate * 3) / 4;
  if (bitrate > G_MAXINT)
    bitrate = G_MAXINT;
  demux->current_download_rate = bitrate;

  GST_DEBUG_OBJECT (demux, "Using current download rate: %d", (gint) bitrate);

  GST_M3U8_CLIENT_LOCK (demux->client);
  if (!demux->client->main->lists) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  return gst_hls_demux_change_playlist (demux, bitrate * demux->bitrate_limit);
}

#ifdef HAVE_NETTLE
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data)
{
  aes_set_decrypt_key (&demux->aes_ctx.ctx, 16, key_data);
  CBC_SET_IV (&demux->aes_ctx, iv_data);

  return TRUE;
}

static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  if (length % 16 != 0)
    return FALSE;

  CBC_DECRYPT (&demux->aes_ctx, aes_decrypt, length, decrypted_data,
      encrypted_data);

  return TRUE;
}

static void
gst_hls_demux_decrypt_end (GstHLSDemux * demux)
{
  /* NOP */
}

#else
static gboolean
gst_hls_demux_decrypt_start (GstHLSDemux * demux, const guint8 * key_data,
    const guint8 * iv_data)
{
  gcry_error_t err = 0;
  gboolean ret = FALSE;

  err =
      gcry_cipher_open (&demux->aes_ctx, GCRY_CIPHER_AES128,
      GCRY_CIPHER_MODE_CBC, 0);
  if (err)
    goto out;
  err = gcry_cipher_setkey (demux->aes_ctx, key_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_setiv (demux->aes_ctx, iv_data, 16);
  if (!err)
    ret = TRUE;

out:
  if (!ret)
    if (demux->aes_ctx)
      gcry_cipher_close (demux->aes_ctx);

  return ret;
}

static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data)
{
  gcry_error_t err = 0;

  err = gcry_cipher_decrypt (demux->aes_ctx, decrypted_data, length,
      encrypted_data, length);

  return err == 0;
}

static void
gst_hls_demux_decrypt_end (GstHLSDemux * demux)
{
  if (demux->aes_ctx) {
    gcry_cipher_close (demux->aes_ctx);
    demux->aes_ctx = NULL;
  }
}
#endif

static GstBuffer *
gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstBuffer * encrypted_buffer, GError ** err)
{
  GstBuffer *decrypted_buffer = NULL;
  GstMapInfo encrypted_info, decrypted_info;

  decrypted_buffer =
      gst_buffer_new_allocate (NULL, gst_buffer_get_size (encrypted_buffer),
      NULL);

  gst_buffer_map (encrypted_buffer, &encrypted_info, GST_MAP_READ);
  gst_buffer_map (decrypted_buffer, &decrypted_info, GST_MAP_WRITE);

  if (!decrypt_fragment (demux, encrypted_info.size,
          encrypted_info.data, decrypted_info.data))
    goto decrypt_error;


  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);

  return decrypted_buffer;

decrypt_error:
  GST_ERROR_OBJECT (demux, "Failed to decrypt fragment");
  g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_DECRYPT,
      "Failed to decrypt fragment");

  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);

  gst_buffer_unref (encrypted_buffer);
  gst_buffer_unref (decrypted_buffer);

  return NULL;
}

static gboolean
gst_hls_demux_update_source (GstHLSDemux * demux, const gchar * uri,
    const gchar * referer, gboolean refresh, gboolean allow_cache)
{
  if (!gst_uri_is_valid (uri))
    return FALSE;

  if (demux->src != NULL) {
    gchar *old_protocol, *new_protocol;
    gchar *old_uri;

    old_uri = gst_uri_handler_get_uri (GST_URI_HANDLER (demux->src));
    old_protocol = gst_uri_get_protocol (old_uri);
    new_protocol = gst_uri_get_protocol (uri);

    if (!g_str_equal (old_protocol, new_protocol)) {
      gst_object_unref (demux->src_srcpad);
      gst_element_set_state (demux->src, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (demux), demux->src);
      demux->src = NULL;
      demux->src_srcpad = NULL;
      GST_DEBUG_OBJECT (demux, "Can't re-use old source element");
    } else {
      GError *err = NULL;

      GST_DEBUG_OBJECT (demux, "Re-using old source element");
      if (!gst_uri_handler_set_uri (GST_URI_HANDLER (demux->src), uri, &err)) {
        GST_DEBUG_OBJECT (demux, "Failed to re-use old source element: %s",
            err->message);
        g_clear_error (&err);
        gst_element_set_state (demux->src, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (demux), demux->src);
        demux->src = NULL;
      }
    }
    g_free (old_uri);
    g_free (old_protocol);
    g_free (new_protocol);
  }

  if (demux->src == NULL) {
    GObjectClass *gobject_class;

    demux->src = gst_element_make_from_uri (GST_URI_SRC, uri, NULL, NULL);
    if (demux->src == NULL) {
      GST_WARNING_OBJECT (demux, "No element to handle uri: %s", uri);
      return FALSE;
    }

    gobject_class = G_OBJECT_GET_CLASS (demux->src);

    if (g_object_class_find_property (gobject_class, "compress"))
      g_object_set (demux->src, "compress", FALSE, NULL);
    if (g_object_class_find_property (gobject_class, "keep-alive"))
      g_object_set (demux->src, "keep-alive", TRUE, NULL);
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

        g_object_set (demux->src, "extra-headers", extra_headers, NULL);

        gst_structure_free (extra_headers);
      } else {
        g_object_set (demux->src, "extra-headers", NULL, NULL);
      }
    }

    gst_element_set_locked_state (demux->src, TRUE);
    gst_bin_add (GST_BIN_CAST (demux), demux->src);
    demux->src_srcpad = gst_element_get_static_pad (demux->src, "src");
  }
  return TRUE;
}

static gboolean
gst_hls_demux_get_next_fragment (GstHLSDemux * demux,
    gboolean * end_of_playlist, GError ** err)
{
  const gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;
  gint64 range_start, range_end;
  const gchar *key = NULL;
  const guint8 *iv = NULL;

  *end_of_playlist = FALSE;
  if (!gst_m3u8_client_get_next_fragment (demux->client, &discont,
          &next_fragment_uri, &duration, &timestamp, &range_start, &range_end,
          &key, &iv, demux->segment.rate > 0)) {
    GST_INFO_OBJECT (demux, "This playlist doesn't contain more fragments");
    *end_of_playlist = TRUE;
    return FALSE;
  }

  g_mutex_lock (&demux->fragment_download_lock);
  GST_DEBUG_OBJECT (demux,
      "Fetching next fragment %s %" GST_TIME_FORMAT "(range=%" G_GINT64_FORMAT
      "-%" G_GINT64_FORMAT ")", next_fragment_uri, GST_TIME_ARGS (timestamp),
      range_start, range_end);

  /* set up our source for download */
  demux->current_timestamp = timestamp;
  demux->current_duration = duration;
  demux->starting_fragment = TRUE;
  demux->reset_crypto = TRUE;
  demux->current_key = key;
  demux->current_iv = iv;

  /* Reset last flow return */
  demux->last_ret = GST_FLOW_OK;
  g_clear_error (&demux->last_error);

  if (!gst_hls_demux_update_source (demux, next_fragment_uri,
          demux->client->main ? demux->client->main->uri : NULL,
          FALSE,
          demux->client->current ? demux->client->current->allowcache : TRUE)) {
    *err =
        g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN,
        "Missing plugin to handle URI: '%s'", next_fragment_uri);
    g_mutex_unlock (&demux->fragment_download_lock);
    return FALSE;
  }

  gst_hls_demux_configure_src_pad (demux);

  if (gst_element_set_state (demux->src,
          GST_STATE_READY) != GST_STATE_CHANGE_FAILURE) {
    if (range_start != 0 || range_end != -1) {
      if (!gst_element_send_event (demux->src, gst_event_new_seek (1.0,
                  GST_FORMAT_BYTES, (GstSeekFlags) GST_SEEK_FLAG_FLUSH,
                  GST_SEEK_TYPE_SET, range_start, GST_SEEK_TYPE_SET,
                  range_end))) {

        /* looks like the source can't handle seeks in READY */
        *err = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_NOT_IMPLEMENTED,
            "Source element can't handle range requests");
        demux->last_ret = GST_FLOW_ERROR;
      }
    }

    if (G_LIKELY (demux->last_ret == GST_FLOW_OK)) {
      /* flush the proxypads so that the EOS state is reset */
      gst_pad_push_event (demux->src_srcpad, gst_event_new_flush_start ());
      gst_pad_push_event (demux->src_srcpad, gst_event_new_flush_stop (TRUE));

      demux->download_start_time = g_get_monotonic_time ();
      gst_element_sync_state_with_parent (demux->src);

      /* wait for the fragment to be completely downloaded */
      GST_DEBUG_OBJECT (demux, "Waiting for fragment download to finish: %s",
          next_fragment_uri);
      g_cond_wait (&demux->fragment_download_cond,
          &demux->fragment_download_lock);
    }
  } else {
    demux->last_ret = GST_FLOW_CUSTOM_ERROR;
  }
  g_mutex_unlock (&demux->fragment_download_lock);

  if (demux->last_ret != GST_FLOW_OK) {
    gst_element_set_state (demux->src, GST_STATE_NULL);
    if (*err == NULL) {
      if (demux->last_error) {
        *err = demux->last_error;
        demux->last_error = NULL;
      } else {
        *err = g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
            "Failed to download fragment");
      }
    }
  } else {
    gst_element_set_state (demux->src, GST_STATE_READY);
    if (demux->segment.rate > 0)
      demux->segment.position += demux->current_duration;
  }

  if (demux->last_ret != GST_FLOW_OK)
    return FALSE;

  return TRUE;
}
