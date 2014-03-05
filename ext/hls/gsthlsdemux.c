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
 *
 * Last reviewed on 2010-10-07
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#ifdef HAVE_NETTLE
#include <nettle/aes.h>
#include <nettle/cbc.h>
#else
#include <gcrypt.h>
#endif
#include "gsthlsdemux.h"

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
static gboolean gst_hls_demux_switch_playlist (GstHLSDemux * demux,
    GstFragment * fragment);
static GstFragment *gst_hls_demux_get_next_fragment (GstHLSDemux * demux,
    gboolean * end_of_playlist, GError ** err);
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean update, GError ** err);
static void gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose);
static gboolean gst_hls_demux_set_location (GstHLSDemux * demux,
    const gchar * uri);
static gchar *gst_hls_src_buf_to_utf8_playlist (GstBuffer * buf);

#define gst_hls_demux_parent_class parent_class
G_DEFINE_TYPE (GstHLSDemux, gst_hls_demux, GST_TYPE_ELEMENT);

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

  g_mutex_clear (&demux->download_lock);
  g_cond_clear (&demux->download_cond);
  g_mutex_clear (&demux->updates_timed_lock);
  g_cond_clear (&demux->updates_timed_cond);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_hls_demux_class_init (GstHLSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

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
    default:
      break;
  }
  return ret;
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
      gint current_sequence;
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

      GST_DEBUG_OBJECT (demux, "seek event, rate: %f start: %" GST_TIME_FORMAT
          " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      GST_M3U8_CLIENT_LOCK (demux->client);
      file = GST_M3U8_MEDIA_FILE (demux->client->current->files->data);
      current_sequence = file->sequence;
      current_pos = 0;
      target_pos = (GstClockTime) start;
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

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush start");
        gst_pad_push_event (demux->srcpad, gst_event_new_flush_start ());
      }

      gst_hls_demux_pause_tasks (demux);

      /* wait for streaming to finish */
      g_rec_mutex_lock (&demux->updates_lock);
      g_rec_mutex_unlock (&demux->updates_lock);

      g_rec_mutex_lock (&demux->stream_lock);

      GST_M3U8_CLIENT_LOCK (demux->client);
      GST_DEBUG_OBJECT (demux, "seeking to sequence %d", current_sequence);
      demux->client->sequence = current_sequence;
      demux->client->sequence_position = current_pos;
      demux->position_shift = start - current_pos;
      demux->need_segment = TRUE;
      GST_M3U8_CLIENT_UNLOCK (demux->client);

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
  gchar *uri;

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
        gst_query_parse_uri_redirection (query, &uri);
        if (uri == NULL)
          gst_query_parse_uri (query, &uri);
        gst_hls_demux_set_location (demux, uri);
        g_free (uri);
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
    gst_task_stop (demux->stream_task);
    g_rec_mutex_lock (&demux->stream_lock);
    g_rec_mutex_unlock (&demux->stream_lock);
  }
}

static void
switch_pads (GstHLSDemux * demux, GstCaps * newcaps)
{
  GstPad *oldpad = demux->srcpad;
  GstEvent *event;
  gchar *stream_id;
  gchar *name;

  GST_DEBUG ("Switching pads (oldpad:%p) with caps: %" GST_PTR_FORMAT, oldpad,
      newcaps);

  /* First create and activate new pad */
  name = g_strdup_printf ("src_%u", demux->srcpad_counter++);
  demux->srcpad = gst_pad_new_from_static_template (&srctemplate, name);
  g_free (name);
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

  if (newcaps != NULL)
    gst_pad_set_caps (demux->srcpad, newcaps);

  gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad);

  gst_element_no_more_pads (GST_ELEMENT (demux));

  if (oldpad) {
    /* Push out EOS */
    gst_pad_push_event (oldpad, gst_event_new_eos ());
    gst_pad_set_active (oldpad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (demux), oldpad);
  }
}

static gboolean
gst_hls_demux_configure_src_pad (GstHLSDemux * demux, GstFragment * fragment)
{
  GstCaps *bufcaps = NULL, *srccaps = NULL;
  GstBuffer *buf = NULL;
  /* Figure out if we need to create/switch pads */
  if (G_LIKELY (demux->srcpad))
    srccaps = gst_pad_get_current_caps (demux->srcpad);
  if (fragment) {
    bufcaps = gst_fragment_get_caps (fragment);
    if (G_UNLIKELY (!bufcaps)) {
      if (srccaps)
        gst_caps_unref (srccaps);
      return FALSE;
    }
    buf = gst_fragment_get_buffer (fragment);
  }

  if (G_UNLIKELY (!srccaps || demux->discont || (buf
              && GST_BUFFER_IS_DISCONT (buf)))) {
    switch_pads (demux, bufcaps);
    demux->need_segment = TRUE;
    demux->discont = FALSE;
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  }
  if (bufcaps)
    gst_caps_unref (bufcaps);
  if (G_LIKELY (srccaps))
    gst_caps_unref (srccaps);

  if (demux->need_segment) {
    GstSegment segment;
    GstClockTime start =
        buf ? GST_BUFFER_PTS (buf) : demux->client->sequence_position;

    start += demux->position_shift;
    /* And send a newsegment */
    GST_DEBUG_OBJECT (demux, "Sending new-segment. segment start:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (start));
    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.start = start;
    segment.time = start;
    gst_pad_push_event (demux->srcpad, gst_event_new_segment (&segment));
    demux->need_segment = FALSE;
    demux->position_shift = 0;
  }
  if (buf)
    gst_buffer_unref (buf);
  return TRUE;
}

static void
gst_hls_demux_stream_loop (GstHLSDemux * demux)
{
  GstFragment *fragment;
  GstBuffer *buf;
  GstFlowReturn ret;
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

  demux->next_download = g_get_monotonic_time ();
  if ((fragment =
          gst_hls_demux_get_next_fragment (demux, &end_of_playlist,
              &err)) == NULL) {
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
                  NULL, NULL, NULL, NULL, NULL, NULL)
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
      if (demux->download_failed_count < DEFAULT_FAILED_COUNT) {
        GST_WARNING_OBJECT (demux, "Could not fetch the next fragment");
        g_clear_error (&err);
        /* Wait half the fragment duration before retrying */
        demux->next_download +=
            gst_util_uint64_scale (gst_m3u8_client_get_current_fragment_duration
            (demux->client), G_USEC_PER_SEC, 2 * GST_SECOND);
        g_mutex_lock (&demux->download_lock);
        if (demux->stop_stream_task) {
          g_mutex_unlock (&demux->download_lock);
          goto pause_task;
        }
        g_cond_wait_until (&demux->download_cond, &demux->download_lock,
            demux->next_download);
        g_mutex_unlock (&demux->download_lock);
        GST_DEBUG_OBJECT (demux, "Retrying now");
        return;
      } else {
        gst_element_post_message (GST_ELEMENT_CAST (demux),
            gst_message_new_error (GST_OBJECT_CAST (demux), err,
                "Could not fetch the next fragment"));
        g_clear_error (&err);
        goto pause_task;
      }
    }
  } else {
    demux->download_failed_count = 0;
    gst_m3u8_client_advance_fragment (demux->client);

    if (demux->stop_updates_task) {
      g_object_unref (fragment);
      goto pause_task;
    }
  }

  if (demux->stop_updates_task) {
    g_object_unref (fragment);
    goto pause_task;
  }

  if (!gst_hls_demux_configure_src_pad (demux, fragment)) {
    g_object_unref (fragment);
    goto type_not_found;
  }

  buf = gst_fragment_get_buffer (fragment);

  GST_DEBUG_OBJECT (demux, "Pushing buffer %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  ret = gst_pad_push (demux->srcpad, buf);
  if (ret != GST_FLOW_OK)
    goto error_pushing;

  /* try to switch to another bitrate if needed */
  gst_hls_demux_switch_playlist (demux, fragment);
  g_object_unref (fragment);

  GST_DEBUG_OBJECT (demux, "Pushed buffer");

  return;

end_of_playlist:
  {
    GST_DEBUG_OBJECT (demux, "Reached end of playlist, sending EOS");

    gst_hls_demux_configure_src_pad (demux, NULL);

    gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    gst_hls_demux_pause_tasks (demux);
    return;
  }

type_not_found:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND,
        ("Could not determine type of stream"), (NULL));
    gst_hls_demux_pause_tasks (demux);
    return;
  }

error_pushing:
  {
    g_object_unref (fragment);
    if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL),
          ("stream stopped, reason %s", gst_flow_get_name (ret)));
      gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    } else {
      GST_DEBUG_OBJECT (demux, "stream stopped, reason %s",
          gst_flow_get_name (ret));
    }
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
    demux->client = gst_m3u8_client_new ("");
  }

  demux->position_shift = 0;
  demux->need_segment = TRUE;
  demux->discont = TRUE;

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;

  demux->srcpad_counter = 0;
  if (demux->srcpad) {
    gst_element_remove_pad (GST_ELEMENT_CAST (demux), demux->srcpad);
    demux->srcpad = NULL;
  }
}

static gboolean
gst_hls_demux_set_location (GstHLSDemux * demux, const gchar * uri)
{
  if (demux->client)
    gst_m3u8_client_free (demux->client);
  demux->client = gst_m3u8_client_new (uri);
  GST_INFO_OBJECT (demux, "Changed location: %s", uri);
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
      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_error (GST_OBJECT_CAST (demux), err,
              "Could not fetch the child playlist"));
      g_error_free (err);
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
      if (demux->client->update_failed_count < DEFAULT_FAILED_COUNT) {
        GST_WARNING_OBJECT (demux, "Could not update the playlist");
        demux->next_update =
            g_get_monotonic_time () +
            gst_util_uint64_scale (gst_m3u8_client_get_target_duration
            (demux->client), G_USEC_PER_SEC, 2 * GST_SECOND);
      } else {
        gst_element_post_message (GST_ELEMENT_CAST (demux),
            gst_message_new_error (GST_OBJECT_CAST (demux), err,
                "Could not update the playlist"));
        g_error_free (err);
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
  gboolean updated = FALSE;

  const gchar *uri = gst_m3u8_client_get_current_uri (demux->client);

  download = gst_uri_downloader_fetch_uri (demux->downloader, uri, TRUE, err);
  if (download == NULL)
    return FALSE;

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

  /*  If it's a live source, do not let the sequence number go beyond
   * three fragments before the end of the list */
  if (updated && update == FALSE && demux->client->current &&
      gst_m3u8_client_is_live (demux->client)) {
    guint last_sequence;

    GST_M3U8_CLIENT_LOCK (demux->client);
    last_sequence =
        GST_M3U8_MEDIA_FILE (g_list_last (demux->client->current->files)->
        data)->sequence;

    if (demux->client->sequence >= last_sequence - 3) {
      GST_DEBUG_OBJECT (demux, "Sequence is beyond playlist. Moving back to %d",
          last_sequence - 3);
      demux->need_segment = TRUE;
      demux->client->sequence = last_sequence - 3;
    }
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

retry_failover_protection:
  old_bandwidth = GST_M3U8 (previous_variant->data)->bandwidth;
  new_bandwidth = GST_M3U8 (current_variant->data)->bandwidth;

  /* Don't do anything else if the playlist is the same */
  if (new_bandwidth == old_bandwidth) {
    return TRUE;
  }

  demux->client->main->current_variant = current_variant;
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_m3u8_client_set_current (demux->client, current_variant->data);

  GST_INFO_OBJECT (demux, "Client was on %dbps, max allowed is %dbps, switching"
      " to bitrate %dbps", old_bandwidth, max_bitrate, new_bandwidth);
  demux->discont = TRUE;

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
    if (new_bandwidth ==
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
gst_hls_demux_switch_playlist (GstHLSDemux * demux, GstFragment * fragment)
{
  GstClockTime diff;
  gsize size;
  gint bitrate;
  GstBuffer *buffer;

  GST_M3U8_CLIENT_LOCK (demux->client);
  if (!demux->client->main->lists || !fragment) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  /* compare the time when the fragment was downloaded with the time when it was
   * scheduled */
  diff = fragment->download_stop_time - fragment->download_start_time;
  buffer = gst_fragment_get_buffer (fragment);
  size = gst_buffer_get_size (buffer);
  bitrate = (size * 8) / ((double) diff / GST_SECOND);

  GST_DEBUG ("Downloaded %d bytes in %" GST_TIME_FORMAT ". Bitrate is : %d",
      (guint) size, GST_TIME_ARGS (diff), bitrate);

  gst_buffer_unref (buffer);
  return gst_hls_demux_change_playlist (demux, bitrate * demux->bitrate_limit);
}

#ifdef HAVE_NETTLE
static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data,
    const guint8 * key_data, const guint8 * iv_data)
{
  struct CBC_CTX (struct aes_ctx, AES_BLOCK_SIZE) aes_ctx;

  if (length % 16 != 0)
    return FALSE;

  aes_set_decrypt_key (&aes_ctx.ctx, 16, key_data);
  CBC_SET_IV (&aes_ctx, iv_data);

  CBC_DECRYPT (&aes_ctx, aes_decrypt, length, decrypted_data, encrypted_data);

  return TRUE;
}
#else
static gboolean
decrypt_fragment (GstHLSDemux * demux, gsize length,
    const guint8 * encrypted_data, guint8 * decrypted_data,
    const guint8 * key_data, const guint8 * iv_data)
{
  gcry_cipher_hd_t aes_ctx = NULL;
  gcry_error_t err = 0;
  gboolean ret = FALSE;

  err =
      gcry_cipher_open (&aes_ctx, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_CBC, 0);
  if (err)
    goto out;
  err = gcry_cipher_setkey (aes_ctx, key_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_setiv (aes_ctx, iv_data, 16);
  if (err)
    goto out;
  err = gcry_cipher_decrypt (aes_ctx, decrypted_data, length,
      encrypted_data, length);
  if (err)
    goto out;

  ret = TRUE;

out:
  if (aes_ctx)
    gcry_cipher_close (aes_ctx);

  return ret;
}
#endif

static GstFragment *
gst_hls_demux_decrypt_fragment (GstHLSDemux * demux,
    GstFragment * encrypted_fragment, const gchar * key, const guint8 * iv,
    GError ** err)
{
  GstFragment *key_fragment, *ret = NULL;
  GstBuffer *key_buffer, *encrypted_buffer, *decrypted_buffer;
  GstMapInfo key_info, encrypted_info, decrypted_info;
  gsize unpadded_size;

  if (demux->key_url && strcmp (demux->key_url, key) == 0) {
    key_fragment = g_object_ref (demux->key_fragment);
  } else {
    g_free (demux->key_url);
    demux->key_url = NULL;

    if (demux->key_fragment)
      g_object_unref (demux->key_fragment);
    demux->key_fragment = NULL;

    GST_INFO_OBJECT (demux, "Fetching key %s", key);
    key_fragment =
        gst_uri_downloader_fetch_uri (demux->downloader, key, FALSE, err);
    if (key_fragment == NULL)
      goto key_failed;
    demux->key_url = g_strdup (key);
    demux->key_fragment = g_object_ref (key_fragment);
  }

  key_buffer = gst_fragment_get_buffer (key_fragment);
  encrypted_buffer = gst_fragment_get_buffer (encrypted_fragment);
  decrypted_buffer =
      gst_buffer_new_allocate (NULL, gst_buffer_get_size (encrypted_buffer),
      NULL);

  gst_buffer_map (key_buffer, &key_info, GST_MAP_READ);
  gst_buffer_map (encrypted_buffer, &encrypted_info, GST_MAP_READ);
  gst_buffer_map (decrypted_buffer, &decrypted_info, GST_MAP_WRITE);

  if (key_info.size != 16)
    goto decrypt_error;
  if (!decrypt_fragment (demux, encrypted_info.size,
          encrypted_info.data, decrypted_info.data, key_info.data, iv))
    goto decrypt_error;

  /* Handle pkcs7 unpadding here */
  unpadded_size =
      decrypted_info.size - decrypted_info.data[decrypted_info.size - 1];

  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);
  gst_buffer_unmap (key_buffer, &key_info);

  gst_buffer_resize (decrypted_buffer, 0, unpadded_size);

  gst_buffer_unref (key_buffer);
  gst_buffer_unref (encrypted_buffer);
  g_object_unref (key_fragment);

  ret = gst_fragment_new ();
  gst_fragment_add_buffer (ret, decrypted_buffer);
  ret->completed = TRUE;
key_failed:
  g_object_unref (encrypted_fragment);
  return ret;

decrypt_error:
  GST_ERROR_OBJECT (demux, "Failed to decrypt fragment");
  g_set_error (err, GST_STREAM_ERROR, GST_STREAM_ERROR_DECRYPT,
      "Failed to decrypt fragment");

  gst_buffer_unmap (decrypted_buffer, &decrypted_info);
  gst_buffer_unmap (encrypted_buffer, &encrypted_info);
  gst_buffer_unmap (key_buffer, &key_info);

  gst_buffer_unref (key_buffer);
  gst_buffer_unref (encrypted_buffer);
  gst_buffer_unref (decrypted_buffer);

  g_object_unref (key_fragment);
  g_object_unref (encrypted_fragment);
  return ret;
}

static GstFragment *
gst_hls_demux_get_next_fragment (GstHLSDemux * demux,
    gboolean * end_of_playlist, GError ** err)
{
  GstFragment *download;
  const gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  GstBuffer *buf;
  gboolean discont;
  gint64 range_start, range_end;
  const gchar *key = NULL;
  const guint8 *iv = NULL;

  *end_of_playlist = FALSE;
  if (!gst_m3u8_client_get_next_fragment (demux->client, &discont,
          &next_fragment_uri, &duration, &timestamp, &range_start, &range_end,
          &key, &iv)) {
    GST_INFO_OBJECT (demux, "This playlist doesn't contain more fragments");
    *end_of_playlist = TRUE;
    return NULL;
  }

  GST_INFO_OBJECT (demux,
      "Fetching next fragment %s (range=%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
      ")", next_fragment_uri, range_start, range_end);

  download = gst_uri_downloader_fetch_uri_with_range (demux->downloader,
      next_fragment_uri, FALSE, range_start, range_end, err);

  if (download == NULL)
    goto error;

  if (key) {
    download = gst_hls_demux_decrypt_fragment (demux, download, key, iv, err);
    if (download == NULL)
      goto error;
  }

  buf = gst_fragment_get_buffer (download);

  GST_DEBUG_OBJECT (demux, "set fragment pts=%" GST_TIME_FORMAT " duration=%"
      GST_TIME_FORMAT, GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

  GST_BUFFER_DURATION (buf) = duration;
  GST_BUFFER_PTS (buf) = timestamp;

  /* We actually need to do this every time we switch bitrate */
  if (G_UNLIKELY (demux->do_typefind)) {
    GstCaps *caps = gst_fragment_get_caps (download);

    if (G_UNLIKELY (!caps)) {
      GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND,
          ("Could not determine type of stream"), (NULL));
      gst_buffer_unref (buf);
      g_object_unref (download);
      goto error;
    }

    if (!demux->input_caps || !gst_caps_is_equal (caps, demux->input_caps)) {
      gst_caps_replace (&demux->input_caps, caps);
      /* gst_pad_set_caps (demux->srcpad, demux->input_caps); */
      GST_INFO_OBJECT (demux, "Input source caps: %" GST_PTR_FORMAT,
          demux->input_caps);
      demux->do_typefind = FALSE;
    }
    gst_caps_unref (caps);
  } else {
    gst_fragment_set_caps (download, demux->input_caps);
  }

  if (discont) {
    GST_DEBUG_OBJECT (demux, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  } else {
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
  }

  /* The buffer ref is still kept inside the fragment download */
  gst_buffer_unref (buf);

  return download;

error:
  {
    return NULL;
  }
}
